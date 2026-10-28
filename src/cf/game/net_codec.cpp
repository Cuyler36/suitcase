// City Folk's peer traffic in the Wii's format (net_codec.h). The layouts are the ones the game's
// senders and receivers use (src/dol/game/d_net.cpp, d_net_sync.inc and the packets' owners); the save
// structs go through the save codec at the file offset of one of their instances in the save.
#include <cf/net_codec.h>
#include <cf/save_codec.h>

#include <game/game/d_animal.hpp>
#include <game/game/d_net.hpp>
#include <game/game/d_save_data.hpp>
#include <game/game/d_save_dl_item.hpp>
#include <game/game/d_sv_notice.hpp>

#include <borealis/log_c.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <vector>

extern "C" u16 RFLiCalculateCRC(void* head, u32 size); // lib/RVLFaceLib/src/RFL_Database.c

namespace cf {
namespace net {

// CF_NET_LOG=1: every block and direct message, by direction and id
bool Logging() {
    static const bool sOn = [] {
        const char* v = std::getenv("CF_NET_LOG");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return sOn;
}

void Log(const char* format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    // Seconds since the first line, to line the log up with what happened on screen
    static const auto sStart = std::chrono::steady_clock::now();
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - sStart).count();
    borealis_log_printf(BOREALIS_LOG_INFO, "cf::net", "[%8.3f] %s", t, text);
}

namespace {

#define NET_LOG(...) Log(__VA_ARGS__)

// A short payload's bytes for the log (longer ones: none)
struct Hex {
    char text[3 * 32 + 1] = {};
    Hex(const u8* p, u32 size) {
        static const char kDigits[] = "0123456789ABCDEF";
        if (size > 32) {
            return;
        }
        char* out = text;
        for (u32 i = 0; i < size; i++) {
            *out++ = ' ';
            *out++ = kDigits[p[i] >> 4];
            *out++ = kDigits[p[i] & 15];
        }
    }
};

// A dropped packet (malformed, or not converted as the Wii sent it): always reported, rate-limited
void Dropped(const char* why, u32 a, u32 b) {
    static std::atomic<u32> sCount{0};
    const u32 n = ++sCount;
    if (n <= 50 || n % 100 == 0) {
        borealis_log_printf(BOREALIS_LOG_WARNING, "cf::net", "dropped a received packet (#%u): %s (%u, %u)", n, why, a, b);
    }
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

// A storage unit's fields between MSVC's positions (from the least significant bit, in declaration
// order) and MWCC's (from the most significant), as the save codec does
u64 Repack(u64 v, u32 size, std::initializer_list<u8> widths, bool toWii) {
    const u32 bits = size * 8;
    u64 out = 0;
    u32 low = 0;
    for (const u8 w : widths) {
        const u32 high = bits - low - w;
        const u32 from = toWii ? low : high;
        const u32 to = toWii ? high : low;
        const u64 mask = w == 64 ? ~0ull : (1ull << w) - 1;
        out |= ((v >> from) & mask) << to;
        low += w;
    }
    return out;
}

// The save file offsets of an instance of each save struct that travels (save_codec.h works by offset)
const u32 kPlayerOffset = offsetof(dSaveData_c, mPlayers);
const u32 kAnimalOffset = static_cast<u32>(offsetof(dSaveData_c, mAnimals) + offsetof(dAnimalSave_c, mTown) +
                                           offsetof(dAnimalBlock_c, mAnimals));
const u32 kMailOffset = static_cast<u32>(kAnimalOffset + offsetof(dAnimal_c, mLetter));
const u32 kDLItemOffset = static_cast<u32>(offsetof(dSaveData_c, mDLItems) + offsetof(dSaveDLItemList_c, mItems));
const u32 kHostOffset = offsetof(dSaveData_c, mTownHost);
const u32 kNoticeOffset = static_cast<u32>(offsetof(dSaveData_c, mNoticeBoard) + offsetof(dNoticeBoard_c, mNotices));
const u32 kFishingOffset = offsetof(dSaveData_c, mFishing);
const u32 kBugOffOffset = offsetof(dSaveData_c, mBugOff);
const u32 kRecycleBinOffset = offsetof(dSaveData_c, mRecycleBin);
const u32 kMelodyOffset = offsetof(dSaveData_c, mVillageMelody);

constexpr u32 kPlayerSize = 0x86C0; // NET_PLAYER_DATA_SIZE
constexpr u32 kDLBlockSize = 0x2000;
constexpr u32 kMiiSize = 0x54;      // RFLiExchangeBuffer with one RFLiStoreData
constexpr u32 kTownPrefix = 0x1635; // dNetTransfer_c::sendTown's first part
constexpr u32 kRecordBufSize = 0x15CA;
static_assert(sizeof(dPrivateData_c) == kPlayerSize, "dPrivateData_c on the wire");
static_assert(sizeof(dSaveDLItem_c) == kDLBlockSize, "dSaveDLItem_c on the wire");
static_assert(sizeof(dAnimal_c) == 0x3040, "dAnimal_c on the wire");
static_assert(sizeof(dPrivateHost_c) == 0x2E, "dPrivateHost_c on the wire");

// The bytes [p, p + size) of one message, converted toWii or back. Each step checks its range; a short
// message fails (received data decides the sizes, and the game's own checks are compiled out)
class Conv {
public:
    Conv(u8* p, u32 size, bool toWii) : mP(p), mSize(size), mToWii(toWii) {}

    bool Fits(u32 off, u32 size) {
        if (off > mSize || size > mSize - off) {
            mOk = false;
        }
        return mOk;
    }
    bool Ok() const { return mOk; }
    bool ToWii() const { return mToWii; }
    u32 Size() const { return mSize; }
    u8* At(u32 off) { return mP + off; }
    u8 Byte(u32 off) { return Fits(off, 1) ? mP[off] : 0; }

    // A value as it is now (native before encoding, the Wii's before decoding): read it before converting
    u64 Peek(u32 off, u32 size) { return Fits(off, size) ? Load(mP + off, size, !mToWii) : 0; }

    // count values of size bytes
    void V(u32 off, u32 size, u32 count = 1) {
        if (Fits(off, size * count)) {
            for (u32 i = 0; i < count; i++) {
                std::reverse(mP + off + i * size, mP + off + (i + 1) * size);
            }
        }
    }
    // A bit-field storage unit of unit bytes with these field widths (declaration order)
    void B(u32 off, u32 unit, std::initializer_list<u8> widths) {
        if (Fits(off, unit)) {
            Store(mP + off, unit, Repack(Load(mP + off, unit, !mToWii), unit, widths, mToWii), mToWii);
        }
    }
    // A save struct of size bytes, through the save codec at the offset of one of its instances
    void Save(u32 off, u32 fileOffset, u32 size) {
        if (Fits(off, size)) {
            if (mToWii) {
                save::Encode(fileOffset, mP + off, mP + off, size);
            } else {
                save::Decode(fileOffset, mP + off, mP + off, size);
            }
        }
    }
    void Fail() { mOk = false; }

private:
    u8* mP;
    u32 mSize;
    bool mToWii;
    bool mOk = true;
};

// The Mii exchange block (RFLiExchangeBuffer, lib/RVLFaceLib/include/internal/RFLi_HiddenDatabase.h):
// valid mask, count, then one RFLiStoreData (RFLiCharData's u16 bit-field units and names, and a CRC16
// over the data as stored, appended big-endian: RFLiIsValidRawData)
void ConvertMii(Conv& c, u32 off) {
    if (!c.Fits(off, kMiiSize)) {
        return;
    }
    c.V(off, 4);
    c.V(off + 4, 2);
    const u32 d = off + 8;
    constexpr u32 kStoreSize = 0x4C;
    // The received CRC is checked on the Wii's bytes: an invalid Mii stays invalid
    const bool valid = c.ToWii() || RFLiCalculateCRC(c.At(d), kStoreSize) == 0;
    c.B(d + 0x00, 2, {1, 1, 4, 5, 4, 1});
    c.V(d + 0x02, 2, 10); // name
    c.B(d + 0x20, 2, {3, 3, 4, 3, 1, 2});
    c.B(d + 0x22, 2, {7, 3, 1, 5});
    c.B(d + 0x24, 2, {5, 5, 6});
    c.B(d + 0x26, 2, {3, 4, 5, 4});
    c.B(d + 0x28, 2, {6, 5, 5});
    c.B(d + 0x2A, 2, {3, 4, 4, 5});
    c.B(d + 0x2C, 2, {4, 4, 5, 3});
    c.B(d + 0x2E, 2, {5, 2, 4, 5});
    c.B(d + 0x30, 2, {4, 3, 4, 5});
    c.B(d + 0x32, 2, {2, 2, 3, 4, 5});
    c.B(d + 0x34, 2, {1, 4, 5, 5, 1});
    c.V(d + 0x36, 2, 10); // creator name
    if (valid) {
        u8* crc = c.At(d + 0x4A);
        crc[0] = crc[1] = 0;
        Store(crc, 2, RFLiCalculateCRC(c.At(d), kStoreSize), true);
    }
}

// A player's action parameters (dAcPlayer_c::mRemoteActionParams, 9 bytes): the first bytes of the
// struct the player's state uses (pickUpMsgParam_c { u16 item; u8 x, z, full; }, moveParam_c { f32;
// u8; } ...), so their multi-byte fields by state (dAcPlayer_c's state table, d_a_player.cpp; the
// accessors in d_a_player.hpp). The other states send bytes, or what the last one left, unread
struct ActionParamFields {
    u8 state;
    u8 count;
    u8 fields[3][2]; // offset, size
};
const ActionParamFields kActionParamFields[] = {
    {0x01, 1, {{0, 4}}},                                     // STATE_WALK: moveParam_c
    {0x02, 1, {{0, 2}}},                                     // STATE_RUN: runParam_c
    {0x05, 1, {{0, 2}}}, {0x4D, 1, {{0, 2}}}, {0x8B, 1, {{0, 2}}}, // equipParam_c (startChangeEquip)
    {0x08, 3, {{0, 2}, {2, 2}, {4, 2}}},                     // STATE_DEMO_WAIT: demoWaitParam_s
    {0x0E, 1, {{0, 2}}},                                     // net catch: netCatchParam_s
    {0x13, 1, {{0, 2}}},                                     // STATE_AXE_HIT: axeHitParam_c
    {0x18, 2, {{0, 4}, {4, 4}}}, {0x19, 2, {{0, 4}, {4, 4}}}, {0x1A, 2, {{0, 4}, {4, 4}}}, // fishing: float x, z
    {0x1B, 2, {{0, 4}, {4, 4}}}, {0x1C, 2, {{0, 4}, {4, 4}}}, {0x1D, 2, {{0, 4}, {4, 4}}},
    {0x1E, 2, {{0, 4}, {4, 4}}},
    {0x23, 2, {{0, 4}, {4, 4}}}, {0x2B, 2, {{0, 4}, {4, 4}}}, {0x32, 2, {{0, 4}, {4, 4}}}, // x, z, pose
    {0x36, 2, {{0, 4}, {4, 4}}}, {0x37, 2, {{0, 4}, {4, 4}}},
    {0x40, 2, {{0, 2}, {2, 2}}},                             // STATE_SHOVEL_DIG: shovelDigParam_s
    {0x41, 1, {{0, 2}}},                                     // STATE_SHOVEL_HIT: shovelHitParam_s
    {0x45, 1, {{0, 2}}}, {0x46, 1, {{0, 2}}}, {0x47, 1, {{0, 2}}}, // bury, dig up, show dug item
    {0x49, 2, {{0, 4}, {4, 2}}},                             // exit room: f32 speed, s16 angle
    {0x4A, 2, {{0, 4}, {4, 4}}},                             // enter room: x, z
    {0x52, 1, {{0, 2}}}, {0x53, 1, {{0, 2}}}, {0x54, 1, {{0, 2}}}, // pick up, its message, show it
    {0x5A, 1, {{0, 4}}},                                     // STATE_PITFALL_STRUGGLE: f32 rate
    {0x5D, 1, {{0, 2}}}, {0x5F, 1, {{0, 2}}}, {0x60, 1, {{0, 2}}}, // furniture item, catches
    {0x78, 1, {{0, 2}}},                                     // STATE_EAT
    {0x7E, 1, {{0, 2}}},                                     // STATE_PICK_UP_UNIT_ITEM: takeItemParam_c
    {0x80, 2, {{0, 4}, {4, 4}}}, {0x82, 2, {{0, 4}, {4, 4}}}, // stairs: x, z
    {0x97, 1, {{0, 2}}},                                     // STATE_EAT_SPECIAL
    {0xA0, 1, {{0, 2}}},                                     // outfit and held item: u16 flags
    {0xA1, 2, {{0, 2}, {2, 2}}},                             // listening to K.K.: s16 yaw, tilt
};

void ConvertActionParam(Conv& c, u32 off, u32 state) {
    for (const ActionParamFields& f : kActionParamFields) {
        if (f.state == state) {
            for (u32 i = 0; i < f.count; i++) {
                c.V(off + f.fields[i][0], f.fields[i][1]);
            }
            return;
        }
    }
}

// A shared record (NET_REC_*, sizes lbl_80474620)
void ConvertRecord(Conv& c, u32 id, u32 off) {
    if (id >= 0x04 && id <= 0x07) {
        c.V(off, 4, 2); // the player's x and z (f32)
    } else if (id >= 0x08 && id <= 0x0B) {
        c.V(off, 2); // its angle
    } else if (id >= 0x0C && id <= 0x0F) {
        c.V(off, 4); // sync value; then scene, state + 1 (0xAE: locked, the parameters unread)
        const u8 state = c.Byte(off + 5);
        if (state != 0 && state != 0xAE) {
            ConvertActionParam(c, off + 6, state - 1);
        }
    } else if (id == NET_REC_SEWING_CLOTH) {
        c.V(off, 2);
    } else if (id == NET_REC_RACCO) {
        c.B(off, 2, {7, 7, 2});
    } else if (id >= 0x2B && id <= 0x95) {
        // dNpc::daub_c: its multi-byte values are stored big-endian on PC (d_npc.cpp); the valid flag
        c.B(off + 0x2E, 1, {1, 7});
    }
    // 0x00-0x03 (unused), insects, play sync, lid / mailbox states, cockroaches: bytes
}

// The scene channels' entries (dNetSceneCloseEntry_c and its kin): {u32 idx : 8, size : 24} and a body
void ConvertEntries(Conv& c) {
    u32 off = 0;
    while (c.Ok() && off + 4 <= c.Size()) {
        const u32 v = static_cast<u32>(c.Peek(off, 4));
        const u32 len = c.ToWii() ? v >> 8 : v & 0xFFFFFF;
        c.B(off, 4, {8, 24});
        off += 4;
        if (!c.Fits(off, len)) {
            return;
        }
        off += len;
    }
}

// A block's payload by its packet id (NET_PACKET_*)
void ConvertBlock(Conv& c, u32 id) {
    switch (id) {
    case NET_PACKET_SCENE_REQUEST:
        c.B(0, 1, {1, 7}); // dNetSceneReq_c
        break;
    case NET_PACKET_SCENE_LEADER: // dNetMemberScene_c
        c.V(0, 4);
        if (c.ToWii() && c.Fits(5, 3)) {
            std::memset(c.At(5), 0, 3);
        }
        break;
    case NET_PACKET_SCENE_SAVE_HOST:
    case NET_PACKET_SCENE_LOAD:
    case NET_PACKET_SCENE_EXIT_DATA:
    case NET_PACKET_SCENE_ENTER_DATA:
    case NET_PACKET_SCENE_HANDOVER_DATA:
        ConvertEntries(c);
        break;
    case NET_PACKET_SYNC_BUF: {
        // dNetMemberMask_c (bytes), then the three entries behind their flags (dNetSyncBuf_c::pack)
        u32 off = 2;
        static const u32 sizes[] = {1, sizeof(dPrivateHost_c), sizeof(dAnimal_c)};
        for (u32 i = 0; i < 3 && off < c.Size() && c.Ok(); i++) {
            const u8 on = c.Byte(off++);
            if (on) {
                if (i == 1) {
                    c.Save(off, kHostOffset, sizes[i]);
                } else if (i == 2) {
                    c.Save(off, kAnimalOffset, sizes[i]);
                }
                off += sizes[i];
            }
        }
        break;
    }
    case NET_PACKET_PLAYER_MNGR_25:
    case NET_PACKET_PLAYER_MNGR_26:
        c.V(1, 2); // part, item
        break;
    case NET_PACKET_PLAYER_STATE:
        c.V(0, 4); // sync value; then the state and its action parameters
        ConvertActionParam(c, 5, c.Byte(4));
        break;
    case NET_PACKET_FG_MNG_REQ: // dFgMngReq_c
        c.B(0, 1, {2, 2, 2, 1, 1});
        c.B(1, 1, {5, 2, 1});
        c.V(2, 2, 4);
        break;
    case NET_PACKET_FG_MNG_CMD: // dFgMngCmd_c, SEND_SIZE_SHORT or SEND_SIZE_FULL bytes
        c.B(0, 1, {1, 2, 5});
        c.B(1, 1, {2, 1, 2, 2, 1});
        c.V(2, 2, 3);
        if (c.Size() >= 0x12) {
            c.V(0xA, 2, 4); // drop positions (dFgMngPackedPos_c: one u16 each), drop item
        }
        break;
    case NET_PACKET_FG_MNG_TASK_DONE: // dFgMngTaskMsg_c
        c.V(0, 2);
        c.B(2, 1, {2, 1, 5});
        break;
    case NET_PACKET_FG_MNG_UNIT_FG:
        c.V(2, 2); // x, z, fg
        break;
    case NET_PACKET_MAIL:
        c.Save(0, kMailOffset, sizeof(dMail_c));
        break;
    case NET_PACKET_NOTICE_ADD:
        c.Save(0, kNoticeOffset, sizeof(dNoticeData_c));
        break;
    case NET_PACKET_STR_LOCK:
    case NET_PACKET_STR_UNLOCK:
        c.B(0, 1, {2, 6}); // lockPacket_c (d_str.cpp)
        break;
    case NET_PACKET_FTR_ACTION:
        c.B(0, 4, {8, 4, 4, 7, 1, 1, 4, 1, 1, 1}); // switchPacket_c (d_ftr.cpp)
        break;
    case NET_PACKET_FTR_ACTION2:
        c.B(0, 2, {7, 1, 1}); // roomFlagPacket_c
        break;
    case NET_PACKET_FTR_PUT:
        c.B(0, 4, {16, 4, 4, 4, 1, 1, 1}); // putPacket_c
        break;
    case NET_PACKET_FTR_REMOVE:
        c.B(0, 4, {16, 4, 4, 1, 1, 1}); // removePacket_c
        break;
    case NET_PACKET_FTR_SET_UNIT:
        c.B(0, 4, {16, 4, 4}); // unitItemPacket_c
        break;
    case NET_PACKET_FTR_OWNER:
        c.B(0, 2, {7, 1, 4, 4}); // ownerSwitchPacket_c
        break;
    case NET_PACKET_ROOM_ITEM: // dBgUtil::roomItemMsg_c
        c.V(0, 2);
        c.B(2, 2, {7, 1, 2, 1, 1, 4});
        break;
    case NET_PACKET_PATTERN_COPY:
        c.B(0, 4, {4, 10, 4, 10, 1, 3}); // dPatternCopy packet_s
        break;
    case NET_PACKET_SHOP_SALE: // dShopSale_c
        c.B(0, 4, {8, 23, 1});
        c.B(4, 4, {8, 8, 8, 8});
        break;
    case NET_PACKET_FISHING_RECORD:
        c.Save(0, kFishingOffset, sizeof(dEvFishing_c));
        break;
    case NET_PACKET_BUG_OFF:
        c.Save(0, kBugOffOffset, sizeof(dBugOff_c));
        break;
    case NET_PACKET_NPC_EVENT_DATA: // totakekePacket_c
        c.V(0, 2, 0x11);
        c.V(0x22, 2);
        break;
    case NET_PACKET_CHAT: // kind, then UTF-16 text to the end
        if (c.Size() > 1) {
            c.V(1, 2, (c.Size() - 1) / 2);
        }
        break;
    case NET_PACKET_RECYCLE_BIN:
        c.Save(0, kRecycleBinOffset, sizeof(dRecycleBin_c));
        break;
    case NET_PACKET_MELODY:
        c.Save(0, kMelodyOffset, sizeof(dSaveMelody_c));
        break;
    case NET_PACKET_ANIMAL_SPOT_TIME:
        c.V(0, 8); // OSTime, then kind, idx
        break;
    // A dItem::Item (u16) first, then bytes (d_npc.cpp's build*Packet)
    case NET_PACKET_ANIMAL_HELD_ITEM:
    case NET_PACKET_ANIMAL_FTR:
    case NET_PACKET_ANIMAL_FTR_SET:
    case NET_PACKET_ANIMAL_NEW_ITEM_ADD:
    case NET_PACKET_ANIMAL_NEW_ITEM_REMOVE:
    case NET_PACKET_ANIMAL_WALL:
    case NET_PACKET_ANIMAL_CARPET:
    case NET_PACKET_ANIMAL_MUSIC:
    case NET_PACKET_ANIMAL_PRESENT:
    case NET_PACKET_ANIMAL_CLOTH:
    case NET_PACKET_PERMIT_STATE:
    case NET_PACKET_PERMIT_SET_STATE:
    case NET_PACKET_PERMIT_FLAG:
    case NET_PACKET_PERMIT_QUERY:
    case NET_PACKET_PERMIT_ANSWER:
    case NET_PACKET_PERMIT_CLEAR_STATE:
    case NET_PACKET_PERMIT_SET2:
    case NET_PACKET_PERMIT_CLEAR:
        c.V(0, 2);
        break;
    default:
        // Bytes, hand-packed bits, explicit byte orders (DEMO_SYNC's little-endian id, INSECT_FIELD,
        // PLAY_SYNC), dNetSceneMsg_c (declared byte by byte on PC)
        break;
    }
}

// A direct message's payload by its kind (NET_DIRECT_*)
void ConvertDirect(Conv& c, u32 kind) {
    switch (kind) {
    case NET_DIRECT_XFER_PLAYER: { // dNetTransfer_c::sendPlayer()
        c.Save(0, kPlayerOffset, kPlayerSize);
        u32 off = kPlayerSize + 0x20; // then the DL item mask
        c.V(off, 8);                  // the Wii number
        off += 8;
        const u8 equipNum = c.Byte(off++);
        for (u32 i = 0; i < equipNum && c.Ok(); i++) {
            c.Save(off, kDLItemOffset, kDLBlockSize);
            off += kDLBlockSize;
        }
        if (c.Byte(off++)) {
            c.Save(off, kAnimalOffset, sizeof(dAnimal_c));
            off += sizeof(dAnimal_c);
        }
        if (c.Byte(off++)) {
            ConvertMii(c, off);
        }
        break;
    }
    case NET_DIRECT_XFER_TOWN: { // dNetTransfer_c::sendTown: the town bytes are EncodeTown's already
        const u32 offset = static_cast<u32>(c.Peek(0, 4));
        c.V(0, 4);
        if (offset < kTownPrefix && c.Fits(4, kTownPrefix)) {
            c.V(4, 8); // the Wii number; player slot, sync buffer 0
            const u32 records = 4 + 0xB;
            for (u32 id = 0; id < NET_REC_NUM; id++) {
                ConvertRecord(c, id, records + lbl_80474908[id]);
            }
            c.V(4 + 0x15D5, 8); // ticks; then the Mii flag
            ConvertMii(c, 4 + 0x15DE);
        }
        break;
    }
    case NET_DIRECT_XFER_PLAYER_REPLY: // dNetTransfer_c::sendPlayer(member)
        c.Save(0, kPlayerOffset, kPlayerSize);
        c.V(kPlayerSize, 8);
        if (c.Byte(kPlayerSize + 8)) {
            ConvertMii(c, kPlayerSize + 9);
        }
        break;
    case NET_DIRECT_XFER_ITEMS: // the last flag, then DL items
        if (c.Size() < 1 || (c.Size() - 1) % kDLBlockSize != 0) {
            c.Fail();
            break;
        }
        for (u32 off = 1; off < c.Size(); off += kDLBlockSize) {
            c.Save(off, kDLItemOffset, kDLBlockSize);
        }
        break;
    default:
        // HELLO, the join handshake, XFER_START / ITEM_MASK (bytes), TOWN_REQUEST, JOIN_DONE*
        break;
    }
}

bool ConvertPacket(u8* p, u32 size, bool toWii, u32 peer) {
    if (size < 2) {
        return false;
    }
    Conv c(p, size, toWii);
    // dNetPacketHeader: valid 1, credit 2, direct 1, kind 4; then the counter
    if (!toWii) {
        c.B(0, 1, {1, 2, 1, 4});
    }
    const bool direct = (p[0] >> 3) & 1;
    const u32 kind = p[0] >> 4;
    const u32 credit = (p[0] >> 1) & 3;
    if (toWii) {
        c.B(0, 1, {1, 2, 1, 4});
    }
    // The direction, the other console (send: the member; receive: the DWC aid) and the packet's counter
    char dir[32];
    std::snprintf(dir, sizeof(dir), "%s %u #%u", toWii ? "send to" : "recv from", peer, p[1]);
    // Every packet, with the credit it returns (the receive buffers it freed: with one buffer per member,
    // a sender waits for it before its next stream packet to anyone)
    if (Logging()) {
        if (direct) {
            NET_LOG("%s: credit %u, %s %u, %u bytes", dir, credit, kind < NET_DIRECT_NUM ? "direct" : "credit only",
                    kind, size);
        } else if (size >= 6) {
            NET_LOG("%s: credit %u, stream: records %u bytes, blocks %u bytes", dir, credit,
                    static_cast<u32>(c.Peek(2, 2)), static_cast<u32>(c.Peek(4, 2)));
        }
    }
    if (direct) {
        if (kind < NET_DIRECT_NUM) {
            if (Logging()) {
                NET_LOG("%s direct %u, %u bytes", dir, kind, size - 2);
            }
            Conv d(p + 2, size - 2, toWii);
            ConvertDirect(d, kind);
            if (!d.Ok() && !toWii) {
                Dropped("direct message too short for its layout", kind, size - 2);
            }
            return d.Ok();
        }
        if (!(kind == NET_DIRECT_NUM && size == 2) && !toWii) {
            Dropped("bad direct kind", kind, size);
            return false;
        }
        return true; // the credit-only packet
    }
    // A stream: {u16 records, u16 blocks}, the records ([id][record]), the blocks ([header][payload])
    const u32 stream = static_cast<u32>(c.Peek(2, 2));
    const u32 extra = static_cast<u32>(c.Peek(4, 2));
    c.V(2, 2, 2);
    if (!c.Fits(6, stream + extra)) {
        if (!toWii) {
            Dropped("stream sizes past the packet", stream + extra, size);
        }
        return false;
    }
    u32 off = 6;
    const u32 recordsEnd = 6 + stream;
    while (off < recordsEnd) {
        const u32 id = p[off++];
        if (id >= NET_REC_NUM || lbl_80474620[id] > recordsEnd - off) {
            if (!toWii) {
                Dropped("bad record", id, recordsEnd - off);
            }
            return false;
        }
        ConvertRecord(c, id, off);
        off += lbl_80474620[id];
    }
    const u32 blocksEnd = recordsEnd + extra;
    while (off < blocksEnd) {
        // dNetBlockHeader: id, size low, size middle, {size high 2, dest 3}, {scene 7}, {from 2}
        if (blocksEnd - off < 6) {
            if (!toWii) {
                Dropped("truncated block header", blocksEnd - off, 6);
            }
            return false;
        }
        u8* h = p + off;
        const u32 high = toWii ? (h[3] & 3) : (h[3] >> 6);
        const u32 len = h[1] | (h[2] << 8) | (high << 16);
        c.B(off + 3, 1, {2, 3, 3});
        c.B(off + 4, 1, {7, 1});
        c.B(off + 5, 1, {2, 6});
        off += 6;
        if (len > blocksEnd - off) {
            if (!toWii) {
                Dropped("block past the packet", h[0], len);
            }
            return false;
        }
        // Logged as they travel (the Wii's bytes), with the header's dest, scene and sender (read from the
        // repacked header: the Wii's layout when sending, the native one when receiving)
        const u32 dest = toWii ? (h[3] >> 3) & 7 : (h[3] >> 2) & 7;
        const u32 scene = toWii ? h[4] >> 1 : h[4] & 0x7F;
        const u32 from = toWii ? h[5] >> 6 : h[5] & 3;
        if (Logging() && !toWii) {
            NET_LOG("%s block 0x%02X (to %u, scene 0x%02X, from %u), %u bytes:%s", dir, h[0], dest, scene, from, len,
                    Hex(p + off, len).text);
        }
        Conv b(p + off, len, toWii);
        ConvertBlock(b, h[0]);
        if (Logging() && toWii) {
            NET_LOG("%s block 0x%02X (to %u, scene 0x%02X, from %u), %u bytes:%s", dir, h[0], dest, scene, from, len,
                    Hex(p + off, len).text);
        }
        if (!b.Ok()) {
            if (!toWii) {
                Dropped("block too short for its layout", h[0], len);
            }
            return false;
        }
        off += len;
    }
    return c.Ok();
}

std::vector<u8>& TownScratch() {
    static std::vector<u8> sTown(sizeof(dSaveTown_c));
    return sTown;
}

} // namespace

void EncodePacket(void* data, u32 size, u32 member) {
    ConvertPacket(static_cast<u8*>(data), size, true, member);
}

bool DecodePacket(void* data, u32 size, u32 aid) {
    // Wii Speak's voice packets ('_VCT', lib/vct) when the other player has voice chat on: the PC has
    // none (stubs_vct.cpp), and they are not the game's
    if (size >= 4 && std::memcmp(data, "_VCT", 4) == 0) {
        if (Logging()) {
            NET_LOG("recv Wii Speak packet, %u bytes (dropped)", size);
        }
        return false;
    }
    return ConvertPacket(static_cast<u8*>(data), size, false, aid);
}

const u8* EncodeTown(const void* town) {
    std::vector<u8>& out = TownScratch();
    save::Encode(0, town, out.data(), static_cast<u32>(out.size()));
    return out.data();
}

void DecodeTown(void* town) {
    save::Decode(0, town, town, sizeof(dSaveTown_c));
}

void EncodeStatus(void* dst, const void* src, u32 size) {
    std::memcpy(dst, src, size);
    u8* p = static_cast<u8*>(dst);
    for (u32 i = 0; i + 1 < size; i += 2) {
        std::swap(p[i], p[i + 1]);
    }
}

} // namespace net
} // namespace cf
