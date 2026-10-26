#ifndef NW4R_G3D_RES_RES_TEX_H
#define NW4R_G3D_RES_RES_TEX_H
#include <nw4r/types_nw4r.h>
#include <port/endian.h>
#if TARGET_PC
#include <port/side_slot.h>
#endif

#include <nw4r/g3d/res/g3d_rescommon.h>

#include <revolution/gx.h>

namespace nw4r {
namespace g3d {

struct ResTexData {
    enum Flag {
        FLAG_CIFMT = (1 << 0),
    };

    ResBlockHeaderData header; // at 0x0
    BE(u32) revision;              // at 0x8
    BE(s32) toResFileData;         // at 0xC
    BE(s32) toTexData;             // at 0x10
    BE(s32) name;                  // at 0x14
    BE(u32) flag;                  // at 0x18
    BE(u16) width;                 // at 0x1C
    BE(u16) height;                // at 0x1E
    union {
        BE(GXTexFmt) fmt;
        BE(GXCITexFmt) cifmt;
    }; // at 0x20
    BE(u32) mipmap_level;  // at 0x24
    BE(f32) min_lod;       // at 0x28
    BE(f32) max_lod;       // at 0x2C
    BE(s32) original_path; // at 0x30
    BE(s32) toResUserData; // at 0x34
};

class ResTex : public ResCommon<ResTexData> {
public:
    static const ulong SIGNATURE = 'TEX0';
    static const int REVISION = 1;

public:
    NW4R_G3D_RESOURCE_FUNC_DEF(ResTex);

    void Init();

    void DCStore(bool sync);
    void EndEdit() {
        DCStore(false);
    }

    ulong GetRevision() const {
        return ref().revision;
    }

    bool CheckRevision() const {
        return GetRevision() == REVISION;
    }

    bool GetTexObjParam(void** ppTexData, u16* pWidth, u16* pHeight,
                        GXTexFmt* pFormat, f32* pMinLod, f32* pMaxLod,
                        GXBool* pMipMap) const;

    bool GetTexObjCIParam(void** ppTexData, u16* pWidth, u16* pHeight,
                          GXCITexFmt* pFormatCI, f32* pMinLod, f32* pMaxLod,
                          GXBool* pMipMap) const;

    bool IsCIFmt() const {
        return ref().flag & ResTexData::FLAG_CIFMT;
    }

    u16 GetWidth() const {
        return ref().width;
    }
    u16 GetHeight() const {
        return ref().height;
    }

#if TARGET_PC
    // The game points textures at its own buffers by writing toTexData = buffer - &ref(). On PC
    // that difference does not fit 32 bits (static data in the image, MEM1 and MEM2 are far
    // apart), so SetTexData stores the pointer in the side table and toTexData holds
    // TEXDATA_SIDE, which no file offset uses.
    static const s32 TEXDATA_SIDE = (s32)0x80000000;

    void SetTexData(void* pData) {
        ResTexData& r = ref();
        r.toTexData = TEXDATA_SIDE;
        port::SideSlotSetPtr(&r.toTexData, pData);
    }

    void* GetTexData() {
        ResTexData& r = ref();

        if (r.toTexData == TEXDATA_SIDE) {
            return port::SideSlotGetPtr(&r.toTexData);
        }
        return (r.toTexData != 0) ? (void*)((u8*)&r + r.toTexData) : NULL;
    }

    const void* GetTexData() const {
        const ResTexData& r = ref();

        if (r.toTexData == TEXDATA_SIDE) {
            return port::SideSlotGetPtr(&r.toTexData);
        }
        return (r.toTexData != 0) ? (void*)((u8*)&r + r.toTexData) : NULL;
    }
#else
    void* GetTexData() {
        ResTexData& r = ref();

        return (r.toTexData != 0) ? (void*)((u8*)&r + r.toTexData) : NULL;
    }

    const void* GetTexData() const {
        const ResTexData& r = ref();

        return (r.toTexData != 0) ? (void*)((u8*)&r + r.toTexData) : NULL;
    }
#endif
};

} // namespace g3d
} // namespace nw4r

#endif
