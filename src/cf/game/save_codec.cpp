// The save file in the Wii's format (save_codec.h).
#include <cf/save_codec.h>
#include "save_visit.h"

#include <game/game/d_animal.hpp>
#include <game/game/d_friend.hpp>
#include <game/game/d_mail.hpp>
#include <game/game/d_private_data.hpp>
#include <game/game/d_save_building.hpp>
#include <game/game/d_save_check.hpp>
#include <game/game/d_save_data.hpp>
#include <game/game/d_save_dl_item.hpp>
#include <game/game/d_save_town.hpp>
#include <game/game/d_sv_extra_header.hpp>
#include <game/sLib/s_crc.hpp>

#include <borealis/log_c.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace cf {
namespace save {
namespace {

// ---- the byte-order map ----------------------------------------------------------------------

// A run of count values of size bytes; one bit-field unit (widths, in declaration order); or a union
// whose layout depends on the data (fn, for the struct at structBase)
struct Span {
    u32 offset;
    u32 size;
    u32 count;
    u8 widthCount; // bit-field fields; 0: values
    u8 widths[32];
    Visitor::DynamicFn fn;
    u32 structBase;
};

class Recorder : public Visitor {
public:
    void Value(u32 offset, u32 size, u32 count) override {
        if (!mSpans.empty()) {
            Span& last = mSpans.back();
            if (last.widthCount == 0 && last.fn == nullptr && last.size == size &&
                last.offset + last.size * last.count == offset) {
                last.count += count;
                return;
            }
        }
        mSpans.push_back({offset, size, count, 0, {}, nullptr, 0});
    }

    void Bitfield(u32 offset, u32 size, std::initializer_list<u8> widths) override {
        Span span{offset, size, 1, static_cast<u8>(widths.size()), {}, nullptr, 0};
        std::copy(widths.begin(), widths.end(), span.widths);
        mSpans.push_back(span);
    }

    void Dynamic(u32 structBase, u32 offset, u32 size, DynamicFn fn) override {
        mSpans.push_back({offset, size, 1, 0, {}, fn, structBase});
    }

    std::vector<Span> mSpans;
};

} // namespace

template <>
void VisitStruct<dSaveData_c>(Visitor& v, u32 base); // generated

namespace {

const std::vector<Span>& GetMap() {
    static std::vector<Span> sMap;
    static std::once_flag sOnce;
    std::call_once(sOnce, [] {
        Recorder recorder;
        VisitStruct<dSaveData_c>(recorder, 0);
        sMap = std::move(recorder.mSpans);
        std::sort(sMap.begin(), sMap.end(), [](const Span& a, const Span& b) { return a.offset < b.offset; });
        borealis_log_printf(BOREALIS_LOG_INFO, "cf::save", "save map: %u spans", static_cast<unsigned>(sMap.size()));
    });
    return sMap;
}

u64 Load(const u8* p, u32 size, bool big) {
    u64 v = 0;
    for (u32 i = 0; i < size; i++) {
        v |= u64(p[big ? i : size - 1 - i]) << (8 * (size - 1 - i));
    }
    return v;
}

void Store(u8* p, u32 size, u64 v, bool big) {
    for (u32 i = 0; i < size; i++) {
        p[big ? i : size - 1 - i] = static_cast<u8>(v >> (8 * (size - 1 - i)));
    }
}

// Moves a unit's fields between MSVC's positions (from the least significant bit, in declaration
// order) and MWCC's (from the most significant)
u64 Repack(u64 v, u32 size, const u8* widths, u32 widthCount, bool toWii) {
    const u32 bits = size * 8;
    u64 out = 0;
    u32 low = 0;
    for (u32 i = 0; i < widthCount; i++) {
        const u32 w = widths[i];
        const u32 high = bits - low - w; // the field's position counted from the other end
        const u32 from = toWii ? low : high;
        const u32 to = toWii ? high : low;
        const u64 mask = (w == 64 ? ~0ull : (1ull << w) - 1);
        out |= ((v >> from) & mask) << to;
        low += w;
    }
    return out;
}

// Converts the values of [begin, begin + size) of buf (file offsets from begin) between the two forms
class Converter : public Visitor {
public:
    Converter(u32 begin, u8* buf, u32 size, bool toFile) : mBegin(begin), mEnd(begin + size), mBuf(buf), mToFile(toFile) {}

    void Value(u32 offset, u32 size, u32 count) override {
        // Runs can cross a range's edge (an array of values continuing into the next struct); a
        // single value cannot
        const u32 first = offset < mBegin ? (mBegin - offset + size - 1) / size : 0;
        for (u32 i = first; i < count; i++) {
            const u32 off = offset + i * size;
            if (off + size > mEnd) {
                break;
            }
            u8* p = mBuf + (off - mBegin);
            std::reverse(p, p + size);
        }
    }

    void Bitfield(u32 offset, u32 size, std::initializer_list<u8> widths) override {
        Unit(offset, size, widths.begin(), static_cast<u32>(widths.size()));
    }

    void Unit(u32 offset, u32 size, const u8* widths, u32 widthCount) {
        if (offset < mBegin || offset + size > mEnd) {
            return;
        }
        u8* p = mBuf + (offset - mBegin);
        Store(p, size, Repack(Load(p, size, !mToFile), size, widths, widthCount, mToFile), mToFile);
    }

    void Dynamic(u32 structBase, u32 offset, u32 size, DynamicFn fn) override {
        // The struct's bytes decide the view (bytes, the same in both forms)
        if (structBase >= mBegin && offset + size <= mEnd) {
            fn(*this, structBase, mBuf + (structBase - mBegin));
        }
    }

private:
    u32 mBegin;
    u32 mEnd;
    u8* mBuf;
    bool mToFile;
};

void Convert(u32 begin, u8* buf, u32 size, bool toFile) {
    const std::vector<Span>& map = GetMap();
    const u32 end = begin + size;
    Converter conv(begin, buf, size, toFile);
    auto it = std::upper_bound(map.begin(), map.end(), begin, [](u32 off, const Span& s) { return off < s.offset; });
    if (it != map.begin()) {
        --it;
    }
    for (; it != map.end() && it->offset < end; ++it) {
        const Span& s = *it;
        if (s.offset + s.size * s.count <= begin) {
            continue;
        }
        if (s.fn != nullptr) {
            conv.Dynamic(s.structBase, s.offset, s.size, s.fn);
        } else if (s.widthCount != 0) {
            conv.Unit(s.offset, s.size, s.widths, s.widthCount);
        } else {
            conv.Value(s.offset, s.size, s.count);
        }
    }
}

} // namespace

// ---- unions that depend on the data (tools/gen_save_swap.py UNION_HANDLERS) ---------------------

// dMailAddress_c: a player, a villager or text, by mType
template <>
void VisitUnion<dMailAddress_c>(Visitor& v, u32 base, const u8* obj) {
    switch (obj[offsetof(dMailAddress_c, mType)]) {
    case MAIL_ADDR_PLAYER:
    case MAIL_ADDR_PLAYER_NONAME:
    case MAIL_ADDR_FUTURE_SELF:
        Visit<decltype(dMailAddress_c::mPlayer)>(v, base + offsetof(dMailAddress_c, mPlayer));
        break;
    case MAIL_ADDR_ANIMAL:
    case MAIL_ADDR_ANIMAL_NONAME:
        Visit<decltype(dMailAddress_c::mAnimal)>(v, base + offsetof(dMailAddress_c, mAnimal));
        break;
    case MAIL_ADDR_TEXT:
    case MAIL_ADDR_OTHER_NONAME:
        // OTHER_NONAME: hideName on any other type, so usually text (a future-self address would be a
        // player ID; its name is not shown either way)
        Visit<decltype(dMailAddress_c::mText)>(v, base + offsetof(dMailAddress_c, mText));
        break;
    default:
        break; // MAIL_ADDR_NONE: cleared
    }
}

namespace {

// ---- checksums -------------------------------------------------------------------------------

// A checksummed struct at a file offset: where its CRC is and how to compute it from its bytes
struct Checksum {
    u32 offset;   // the struct
    u32 size;     // the struct's size
    u32 crcField; // its CRC, from offset
    u32 (*calc)(const u8* obj, const u8* native); // over obj's bytes; native: the same struct natively
};

template <class T>
u32 CalcWith(const u8* obj) {
    return const_cast<T*>(reinterpret_cast<const T*>(obj))->calcChecksum();
}

// dAnimal_c::calcChecksum: 0 for an empty slot (decided on the native struct)
u32 CalcAnimal(const u8* obj, const u8* native) {
    if (!reinterpret_cast<const dAnimal_c*>(native)->mID.isValid()) {
        return 0;
    }
    return sCrc::calcCRC32(obj, offsetof(dAnimal_c, mChecksum), 0x04201018, -1);
}

#define CF_CHECKSUM(type, field) \
    [](u32 at) { \
        return Checksum{at, sizeof(type), static_cast<u32>(offsetof(type, field)), \
                        [](const u8* obj, const u8*) { return CalcWith<type>(obj); }}; \
    }

// Inner structs first: an outer CRC covers the inner CRCs
const std::vector<Checksum>& GetChecksums() {
    static std::vector<Checksum> sList;
    static std::once_flag sOnce;
    std::call_once(sOnce, [] {
        const u32 players = offsetof(dSaveData_c, mPlayers);
        for (u32 i = 0; i < PLAYER_NUM; i++) {
            const u32 player = players + i * sizeof(dPrivateData_c);
            sList.push_back(CF_CHECKSUM(dFriendList_c, mChecksum)(player + offsetof(dPrivateData_c, mFriends)));
            sList.push_back(CF_CHECKSUM(dPrivateData_c, mChecksum)(player));
        }
        const u32 animals = offsetof(dSaveData_c, mAnimals);
        const u32 blocks[] = {
            static_cast<u32>(animals + offsetof(dAnimalSave_c, mTown) + offsetof(dAnimalBlock_c, mAnimals)),
            static_cast<u32>(animals + offsetof(dAnimalSave_c, mMoved) + offsetof(dMovedAnimalList_c, mAnimals)),
        };
        for (u32 block : blocks) {
            for (u32 i = 0; i < ANIMAL_NUM; i++) {
                sList.push_back({block + i * static_cast<u32>(sizeof(dAnimal_c)), sizeof(dAnimal_c),
                                 static_cast<u32>(offsetof(dAnimal_c, mChecksum)), CalcAnimal});
            }
        }
        sList.push_back(CF_CHECKSUM(dAnimalSave_c, mChecksum)(animals));
        sList.push_back(CF_CHECKSUM(dSaveBuildingList_c, mChecksum)(
            offsetof(dSaveData_c, mBuilding) + offsetof(dSaveBuilding_c, mList)));
        sList.push_back(CF_CHECKSUM(dSaveCheck_c, mChecksum)(offsetof(dSaveData_c, mHeader)));
        const u32 extra = offsetof(dSaveData_c, mExtra);
        sList.push_back(CF_CHECKSUM(dSvExtraHeader::Header_c, mChecksum)(extra + offsetof(dSaveExtra_c, mHeader)));
        sList.push_back(CF_CHECKSUM(dSaveExtra_c, mChecksum)(extra));
        const u32 items = offsetof(dSaveData_c, mDLItems);
        for (u32 i = 0; i < DL_ITEM_SLOT_NUM; i++) {
            sList.push_back(CF_CHECKSUM(dSaveDLItem_c, mChecksum)(
                items + offsetof(dSaveDLItemList_c, mItems) + i * static_cast<u32>(sizeof(dSaveDLItem_c))));
        }
        sList.push_back(CF_CHECKSUM(dSaveDLItemList_c, mChecksum)(items));
        // The town CRC covers what follows mTownChecksum in dSaveTown_c: computed as a dSaveTown_c
        sList.push_back({0, sizeof(dSaveTown_c), static_cast<u32>(offsetof(dSaveTown_c, mTownChecksum)),
                         [](const u8* obj, const u8*) {
                             return reinterpret_cast<const dSaveTown_c*>(obj)->calcTownChecksum();
                         }});
    });
    return sList;
}

bool InRange(const Checksum& c, u32 begin, u32 end) {
    return c.offset >= begin && c.offset + c.size <= end;
}

} // namespace

void Encode(u32 fileOffset, const void* native, void* file, u32 size) {
    const u8* src = static_cast<const u8*>(native);
    u8* out = static_cast<u8*>(file);
    if (out != src) {
        std::memmove(out, src, size);
    }
    Convert(fileOffset, out, size, true);
    for (const Checksum& c : GetChecksums()) {
        if (!InRange(c, fileOffset, fileOffset + size)) {
            continue;
        }
        const u32 rel = c.offset - fileOffset;
        const u32 crc = c.calc(out + rel, src + rel);
        Store(out + rel + c.crcField, 4, crc, true);
    }
}

void Decode(u32 fileOffset, const void* file, void* native, u32 size) {
    const u8* in = static_cast<const u8*>(file);
    u8* out = static_cast<u8*>(native);
    // The file's CRCs, checked on the file's bytes (before `out`, which may be `in`, changes)
    const std::vector<Checksum>& list = GetChecksums();
    std::vector<u8> valid(list.size(), 0);
    u32 invalid = 0;
    std::vector<u8> fileCopy;
    if (out == in) {
        fileCopy.assign(in, in + size);
        in = fileCopy.data();
    } else {
        std::memmove(out, in, size);
    }
    Convert(fileOffset, out, size, false);
    for (size_t i = 0; i < list.size(); i++) {
        const Checksum& c = list[i];
        if (!InRange(c, fileOffset, fileOffset + size)) {
            continue;
        }
        const u32 rel = c.offset - fileOffset;
        // An empty villager slot (decided natively) has CRC 0; the outer CRCs are checked before the
        // inner ones change, as the list is in order and `in` keeps the file's bytes
        valid[i] = c.calc(in + rel, out + rel) == static_cast<u32>(Load(in + rel + c.crcField, 4, true));
        if (!valid[i]) {
            invalid++;
        }
    }
    if (invalid != 0) {
        // Unused slots (download items, emptied villagers) keep stale CRCs on the Wii too
        borealis_log_printf(BOREALIS_LOG_INFO, "cf::save", "%u of the checksums in 0x%X..0x%X do not match", invalid,
                            fileOffset, fileOffset + size);
    }
    // Native CRCs, inner first: valid ones recomputed; the others keep the file's value, so the
    // game's own checks fail as they would on a Wii (the Wii leaves some stale, e.g. an emptied
    // villager slot's)
    for (size_t i = 0; i < list.size(); i++) {
        const Checksum& c = list[i];
        if (!InRange(c, fileOffset, fileOffset + size)) {
            continue;
        }
        const u32 rel = c.offset - fileOffset;
        const u32 crc = valid[i] ? c.calc(out + rel, out + rel) : static_cast<u32>(Load(in + rel + c.crcField, 4, true));
        Store(out + rel + c.crcField, 4, crc, false);
    }
}

bool SelfTest(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        borealis_log_printf(BOREALIS_LOG_ERROR, "cf::save", "self-test: cannot open %s", path);
        return false;
    }
    std::vector<u8> file(sizeof(dSaveData_c));
    const size_t read = std::fread(file.data(), 1, file.size(), f);
    std::fclose(f);
    if (read != file.size()) {
        borealis_log_printf(BOREALIS_LOG_ERROR, "cf::save", "self-test: %s is %zu bytes, not 0x%zX", path, read,
                            sizeof(dSaveData_c));
        return false;
    }
    // Decode logs each checksum that does not match on the file's bytes
    std::vector<u8> native(file.size());
    Decode(0, file.data(), native.data(), static_cast<u32>(native.size()));
    std::vector<u8> again(file.size());
    Encode(0, native.data(), again.data(), static_cast<u32>(again.size()));
    // Encoding recomputes every CRC (as the game does before it writes), so the stale ones change;
    // everything else must come back as it was
    std::vector<u8> isCrc(file.size(), 0);
    for (const Checksum& c : GetChecksums()) {
        std::memset(&isCrc[c.offset + c.crcField], 1, 4);
    }
    u32 diff = 0;
    u32 crcDiff = 0;
    for (size_t i = 0; i < file.size(); i++) {
        if (file[i] == again[i]) {
            continue;
        }
        if (isCrc[i]) {
            crcDiff++;
            continue;
        }
        if (diff < 16) {
            borealis_log_printf(BOREALIS_LOG_WARNING, "cf::save", "self-test: 0x%zX: file %02X, re-encoded %02X", i,
                                file[i], again[i]);
        }
        diff++;
    }
    borealis_log_printf(diff == 0 ? BOREALIS_LOG_INFO : BOREALIS_LOG_ERROR, "cf::save",
                        "self-test: %u data bytes and %u checksum bytes differ after decoding and encoding %s", diff,
                        crcDiff, path);
    return diff == 0;
}

} // namespace save
} // namespace cf
