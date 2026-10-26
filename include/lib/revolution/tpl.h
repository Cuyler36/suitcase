#ifndef REVOLUTION_TPL_H
#define REVOLUTION_TPL_H

#include <revolution/gx/GXEnum.h>
#include <revolution/gx/GXStruct.h>
#include <revolution/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TPL_MAGIC 0x0020AF30

#if TARGET_PC
#ifdef __cplusplus
}
#endif
// PC: the structs in the file's layout (big-endian, 32-bit offsets). TPLBind relocates the offsets in
// place (OFFSET_PTR) instead of turning them into pointers, which do not fit; the GX enums are 32-bit
// words. TPLBind and TPLGet are in src/cf/sdk/tpl_pc.cpp.
#ifdef __cplusplus
#include <port/offset_ptr.h>

typedef struct TPLHeader {
    BE(u16) height;          // 0x00
    BE(u16) width;           // 0x02
    BE(u32) format;          // 0x04
    OFFSET_PTR(char) data;   // 0x08
    BE(u32) wrapS;           // 0x0C
    BE(u32) wrapT;           // 0x10
    BE(u32) minFilter;       // 0x14
    BE(u32) magFilter;       // 0x18
    BE(f32) LODBias;         // 0x1C
    GXBool edgeLODEnable;    // 0x20
    u8 minLOD;               // 0x21
    u8 maxLOD;               // 0x22
    u8 unpacked;             // 0x23
} TPLHeader;

typedef struct TPLClutHeader {
    BE(u16) numEntries;      // 0x00
    u8 unpacked;             // 0x02
    u8 pad;                  // 0x03
    BE(u32) format;          // 0x04
    OFFSET_PTR(char) data;   // 0x08
} TPLClutHeader;

typedef struct TPLDescriptor {
    OFFSET_PTR(TPLHeader) textureHeader;   // 0x00
    OFFSET_PTR(TPLClutHeader) CLUTHeader;  // 0x04
} TPLDescriptor;

typedef struct TPLPalette {
    BE(u32) versionNumber;                     // 0x00
    BE(u32) numDescriptors;                    // 0x04
    OFFSET_PTR(TPLDescriptor) descriptorArray; // 0x08
} TPLPalette;
#else
typedef struct TPLPalette TPLPalette; // C: opaque (TPL.c's functions are C++ on PC)
typedef struct TPLDescriptor TPLDescriptor;
#endif
#ifdef __cplusplus
extern "C" {
#endif
#else

typedef struct TPLHeader {
    u16 height;  // 0x00
    u16 width;   // 0x02

    u32 format;  // 0x04

    char* data;  // 0x08

    GXTexWrapMode wrapS;  // 0x0C
    GXTexWrapMode wrapT;  // 0x10

    GXTexFilter minFilter;  // 0x14
    GXTexFilter magFilter;  // 0x18

    f32 LODBias;           // 0x1C
    GXBool edgeLODEnable;  // 0x20
    u8 minLOD;             // 0x21
    u8 maxLOD;             // 0x22

    u8 unpacked;  // 0x23
} TPLHeader;

typedef struct TPLClutHeader {
    u16 numEntries;    // 0x00
    u8 unpacked;       // 0x01
    GXTlutFmt format;  // 0x04
    char* data;        // 0x08
} TPLClutHeader;

typedef struct TPLDescriptor {
    TPLHeader* textureHeader;   // 0x00
    TPLClutHeader* CLUTHeader;  // 0x04
} TPLDescriptor;

typedef struct TPLPalette {
    u32 versionNumber;               // 0x00
    u32 numDescriptors;              // 0x04
    TPLDescriptor* descriptorArray;  // 0x08
} TPLPalette;
#endif // TARGET_PC

void TPLBind(TPLPalette* pal);

TPLDescriptor* TPLGet(TPLPalette* pal, u32 id);
void TPLGetGXTexObjFromPalette(TPLPalette* pal, GXTexObj* texObj, u32 id);

#ifdef __cplusplus
}
#endif

#endif  // REVOLUTION_TPL_H
