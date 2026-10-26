#ifndef NW4R_G3D_RES_RES_PLTT_H
#define NW4R_G3D_RES_RES_PLTT_H
#include <nw4r/types_nw4r.h>
#include <port/endian.h>
#if TARGET_PC
#include <port/side_slot.h>
#endif

#include <nw4r/g3d/res/g3d_rescommon.h>

#include <revolution/gx.h>

namespace nw4r {
namespace g3d {

struct ResPlttData {
    ResBlockHeaderData header; // at 0x0
    BE(u32) revision;              // at 0x8
    BE(s32) toResFileData;         // at 0xC
    BE(s32) toPlttData;            // at 0x10
    BE(s32) name;                  // at 0x14
    BE(GXTlutFmt) fmt;             // at 0x18
    BE(u16) numEntries;            // at 0x1C
    BE(u16) PADDING_0x1E;          // at 0x1E
    BE(s32) original_path;         // at 0x20
    BE(s32) toResUserData;         // at 0x24
};

class ResPltt : public ResCommon<ResPlttData> {
public:
    static const ulong SIGNATURE = 'PLT0';
    static const int REVISION = 1;

public:
    NW4R_G3D_RESOURCE_FUNC_DEF(ResPltt);

    void Init() {
        DCStore(false);
    }

    ulong GetRevision() const {
        return ref().revision;
    }

    bool CheckRevision() const {
        return GetRevision() == REVISION;
    }

    void DCStore(bool sync);

#if TARGET_PC
    // As ResTex::SetTexData: a palette the game points at its own buffer
    static const s32 PLTTDATA_SIDE = (s32)0x80000000;

    void SetPlttData(void* pData) {
        ResPlttData& r = ref();
        r.toPlttData = PLTTDATA_SIDE;
        port::SideSlotSetPtr(&r.toPlttData, pData);
    }

    u16* GetPlttData() {
        ResPlttData& r = ref();

        if (r.toPlttData == PLTTDATA_SIDE) {
            return static_cast<u16*>(port::SideSlotGetPtr(&r.toPlttData));
        }
        return r.toPlttData != 0
            ? reinterpret_cast<u16*>(reinterpret_cast<u8*>(&r) + r.toPlttData)
            : NULL;
    }

    const u16* GetPlttData() const {
        const ResPlttData& r = ref();

        if (r.toPlttData == PLTTDATA_SIDE) {
            return static_cast<const u16*>(port::SideSlotGetPtr(&r.toPlttData));
        }
        return r.toPlttData != 0
            ? reinterpret_cast<const u16*>(reinterpret_cast<const u8*>(&r) + r.toPlttData)
            : NULL;
    }
#else
    u16* GetPlttData() {
        ResPlttData& r = ref();

        // clang-format off
        return r.toPlttData != 0
            ? reinterpret_cast<u16*>(reinterpret_cast<u8*>(&r) + r.toPlttData)
            : NULL;
        // clang-format on
    }

    const u16* GetPlttData() const {
        const ResPlttData& r = ref();

        // clang-format off
        return r.toPlttData != 0
            ? reinterpret_cast<const u16*>(reinterpret_cast<const u8*>(&r) + r.toPlttData)
            : NULL;
        // clang-format on
    }
#endif

    GXTlutFmt GetFmt() const {
        return ref().fmt;
    }

    ulong GetNumEntries() const {
        return ref().numEntries;
    }
};

} // namespace g3d
} // namespace nw4r

#endif
