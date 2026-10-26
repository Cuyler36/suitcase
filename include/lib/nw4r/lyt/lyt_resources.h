#ifndef NW4R_LYT_RESOURCES_H
#define NW4R_LYT_RESOURCES_H
#include <nw4r/types_nw4r.h>

#include <nw4r/lyt/lyt_types.h>
#include <port/endian.h>

namespace nw4r {
namespace lyt {

// Forward declarations
class ResourceAccessor;

namespace res {

/******************************************************************************
 *
 * Big-endian twins of runtime types stored in the files (the same types on the Wii)
 *
 ******************************************************************************/
#if TARGET_PC
struct VEC2 {
    BE(f32) x;
    BE(f32) y;
    operator math::VEC2() const {
        return math::VEC2(x, y);
    }
};

struct VEC3 {
    BE(f32) x;
    BE(f32) y;
    BE(f32) z;
    operator math::VEC3() const {
        return math::VEC3(x, y, z);
    }
};

struct Size {
    BE(f32) width;
    BE(f32) height;
    operator lyt::Size() const {
        return lyt::Size(width, height);
    }
};

struct TexSRT {
    res::VEC2 translate; // at 0x0
    BE(f32) rotate;      // at 0x8
    res::VEC2 scale;     // at 0xC
    operator lyt::TexSRT() const {
        lyt::TexSRT srt;
        srt.translate = translate;
        srt.rotate = rotate;
        srt.scale = scale;
        return srt;
    }
};

struct ColorS10 {
    BE(s16) r;
    BE(s16) g;
    BE(s16) b;
    BE(s16) a;
    operator GXColorS10() const {
        GXColorS10 c = {r, g, b, a};
        return c;
    }
};
#else
typedef math::VEC2 VEC2;
typedef math::VEC3 VEC3;
typedef lyt::Size Size;
typedef lyt::TexSRT TexSRT;
typedef GXColorS10 ColorS10;
#endif

/******************************************************************************
 *
 * BinaryFileHeader
 *
 ******************************************************************************/
struct BinaryFileHeader {
    char signature[4]; // at 0x0
    BE(u16) byteOrder;     // at 0x4
    BE(u16) version;       // at 0x6
    BE(u32) fileSize;      // at 0x8
    BE(u16) headerSize;    // at 0xC
    BE(u16) dataBlocks;    // at 0xE
};

/******************************************************************************
 *
 * DataBlockHeader
 *
 ******************************************************************************/
struct DataBlockHeader {
    char kind[4]; // at 0x0
    BE(u32) size;     // at 0x4
};

enum ExtUserDataType {
    TYPE_STRING,
    TYPE_INT,
    TYPE_FLOAT,
};

struct ExtUserData {
    inline const char *GetString() const {
        return detail::ConvertOffsToPtr<const char>(this, datOffs);
    }

    inline float GetFloat() const {
        return *detail::ConvertOffsToPtr<float>(this, datOffs);
    }

    inline int GetInt() const {
        return *detail::ConvertOffsToPtr<int>(this, datOffs);
    }

    inline const char *GetName() const {
        return nameOffs != 0 ? detail::ConvertOffsToPtr<const char>(this, nameOffs) : nullptr;
    }

    inline ExtUserDataType GetType() const {
        return (ExtUserDataType)type;
    }

    BE(u32) nameOffs;   // at 0x00
    BE(u32) datOffs;    // at 0x04
    BE(u16) numEntries; // at 0x08
    u8 type;        // at 0x0A
    u8 padding;     // at 0x0B
};

struct ExtUserDataList {
    DataBlockHeader blockHeader; // at 0x0
    BE(u16) num;                     // at 0x8
    u8 padding[2];               // at 0xA
};

/******************************************************************************
 *
 * Texture
 *
 ******************************************************************************/
struct Texture {
    BE(u32) nameStrOffset;         // at 0x0
    u8 type;                   // at 0x4
    u8 PADDING_0x5[0x8 - 0x5]; // at 0x5
};
/******************************************************************************
 *
 * TextureList
 *
 ******************************************************************************/
struct TextureList {
    res::DataBlockHeader blockHeader; // at 0x0
    BE(u16) texNum;                       // at 0x8
    u8 PADDING_0xA[0xC - 0xA];        // at 0xA
};

/******************************************************************************
 *
 * Font
 *
 ******************************************************************************/
struct Font {
    BE(u32) nameStrOffset;         // at 0x0
    u8 type;                   // at 0x4
    u8 PADDING_0x5[0x8 - 0x5]; // at 0x5
};
/******************************************************************************
 *
 * FontList
 *
 ******************************************************************************/
struct FontList {
    res::DataBlockHeader blockHeader; // at 0x0
    BE(u16) fontNum;                      // at 0x8
    u8 PADDING_0xA[0xC - 0xA];        // at 0xA
};

/******************************************************************************
 *
 * MaterialList
 *
 ******************************************************************************/
struct MaterialList {
    res::DataBlockHeader blockHeader; // at 0x0
    BE(u16) materialNum;                  // at 0x8
    u8 PADDING_0xA[0xC - 0xA];        // at 0xA
};

/******************************************************************************
 *
 * TexMap
 *
 ******************************************************************************/
struct TexMap {
    GXTexWrapMode GetWarpModeS() const {
        return static_cast<GXTexWrapMode>(detail::GetBits<u8>(wrapSflt, 0, 2));
    }
    GXTexWrapMode GetWarpModeT() const {
        return static_cast<GXTexWrapMode>(detail::GetBits<u8>(wrapTflt, 0, 2));
    }

    GXTexFilter GetMinFilter() const {
        const u8 bitData = detail::GetBits<u8>(wrapSflt, 2, 3);
        return static_cast<GXTexFilter>((bitData + 1) & 7);
    }
    GXTexFilter GetMagFilter() const {
        const u8 bitData = detail::GetBits<u8>(wrapTflt, 2, 1);
        return static_cast<GXTexFilter>((bitData + 1) & 1);
    }

    BE(u16) texIdx;  // at 0x0
    u8 wrapSflt; // at 0x2
    u8 wrapTflt; // at 0x3
};

} // namespace res

/******************************************************************************
 *
 * ResBlockSet
 *
 ******************************************************************************/
struct ResBlockSet {
    const res::TextureList* pTextureList;   // at 0x0
    const res::FontList* pFontList;         // at 0x4
    const res::MaterialList* pMaterialList; // at 0x8
    ResourceAccessor* pResAccessor;         // at 0xC
};

/******************************************************************************
 *
 * MaterialResourceNum
 *
 ******************************************************************************/
struct MaterialResourceNum {
    BE(u32) bits;

    u8 GetTexMapNum() const {
        return detail::GetBits(bits, 0, 4);
    }
    u8 GetTexSRTNum() const {
        return detail::GetBits(bits, 4, 4);
    }
    u8 GetTexCoordGenNum() const {
        return detail::GetBits(bits, 8, 4);
    }
    bool HasTevSwapTable() const {
        return detail::TestBit(bits, 12);
    }
    u8 GetIndTexSRTNum() const {
        return detail::GetBits(bits, 13, 2);
    }
    u8 GetIndTexStageNum() const {
        return detail::GetBits(bits, 15, 3);
    }
    u8 GetTevStageNum() const {
        return detail::GetBits(bits, 18, 5);
    }
    bool HasAlphaCompare() const {
        return detail::TestBit(bits, 23);
    }
    bool HasBlendMode() const {
        return detail::TestBit(bits, 24);
    }
    u8 GetChanCtrlNum() const {
        return detail::GetBits(bits, 25, 1);
    }
    u8 GetMatColNum() const {
        return detail::GetBits(bits, 27, 1);
    }
};

} // namespace lyt
} // namespace nw4r

#endif
