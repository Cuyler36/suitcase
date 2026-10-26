#ifndef REVOLUTION_GX_H
#define REVOLUTION_GX_H
// PC: aurora's GX, plus the RVL additions of the decomp's include/lib/revolution/gx/*.h.
// Functions without an aurora implementation are provided by src/cf/sdk.
#include <revolution/types.h>
#include <dolphin/gx.h>

#ifdef __cplusplus
extern "C" {
#endif

// Struct/enum tags the decomp uses (aurora's typedefs are untagged)
typedef GXColor _GXColor;
typedef GXTevRegID _GXTevRegID;
typedef GXTevKColorID _GXTevKColorID;

// GXVert.h: normal formats aurora has no direct call for (src/cf/sdk/gx_rvl.cpp)
void GXNormal3u8(u8 x, u8 y, u8 z);
void GXNormal3u16(u16 x, u16 y, u16 z);

// GXTexture.h
typedef GXTlutRegion* (*GXTlutRegionCallback)(u32 idx);
void GXInitTlutRegion(GXTlutRegion* region, u32 tmem_addr, GXTlutSize tlut_size);
GXTlutRegionCallback GXSetTlutRegionCallback(GXTlutRegionCallback f);

// GXTransform.h
#define GX_VIEWPORT_SZ 6

typedef enum {
    GX_FIELD_EVEN,
    GX_FIELD_ODD,
} GXInterlaceField;

// GXStruct.h
#ifndef GX_FOG_ADJ_TABLE_SZ
#define GX_FOG_ADJ_TABLE_SZ 10
#endif

// GXFrameBuffer.h: the RVL render mode aurora lacks (src/cf/sdk/gx_rvl.cpp)
extern GXRenderModeObj GXEurgb60Hz480IntDf;

// GXPixel.h
void GXInitFogAdjTable(GXFogAdjTable* table, u16 width, const f32 projmtx[4][4]);

// GXLighting.h
void GXGetLightDir(const GXLightObj* lt_obj, f32* nx, f32* ny, f32* nz);

// GXGeometry.h: on PC aurora needs the array's size and byte order (GXSETARRAY). Unconverted Wii
// calls go through a shim that passes no size and logs each attribute once (src/cf/sdk/gx_rvl.cpp).
void CFGXSetArrayUnsized(GXAttr attr, const void* data, u8 stride);
#ifndef __cplusplus
// C has no overloads (C++ gets one below, which leaves members named GXSetArray alone)
#define GXSetArray(attr, data, stride) CFGXSetArrayUnsized((attr), (data), (stride))
// Converted sites call aurora's GXSetArray itself: the parenthesized name bypasses the macro above
#undef GXSETARRAY
#define GXSETARRAY(attr, data, size, stride, le) (GXSetArray)((attr), (data), (size), (stride), (le))
#endif

static inline void GXSetViewportv(const f32* view) {
    GXSetViewport(view[0], view[1], view[2], view[3], view[4], view[5]);
}

#ifdef __cplusplus
}

// RVL signatures that differ from aurora's, as C++ overloads. extern "C++" because gx.h can be
// included inside an extern "C" block.
extern "C++" {
// The Wii's three-argument GXSetArray (see CFGXSetArrayUnsized)
inline void GXSetArray(GXAttr attr, const void* data, u8 stride) {
    CFGXSetArrayUnsized(attr, data, stride);
}

// The RVL SDK takes const objects and Mtx rows where aurora takes non-const or const void*
inline void GXLoadTexObj(const GXTexObj* obj, GXTexMapID id) {
    GXLoadTexObj(const_cast<GXTexObj*>(obj), id);
}
inline void GXLoadLightObjImm(const GXLightObj* lt_obj, GXLightID light) {
    GXLoadLightObjImm(const_cast<GXLightObj*>(lt_obj), light);
}
inline void GXLoadPosMtxImm(const f32 (*mtx)[4], u32 id) {
    GXLoadPosMtxImm(static_cast<const void*>(mtx), id);
}
inline void GXLoadNrmMtxImm(const f32 (*mtx)[4], u32 id) {
    GXLoadNrmMtxImm(static_cast<const void*>(mtx), id);
}
inline void GXLoadTexMtxImm(const f32 (*mtx)[4], u32 id, GXTexMtxType type) {
    GXLoadTexMtxImm(static_cast<const void*>(mtx), id, type);
}
}
#endif

#endif // REVOLUTION_GX_H
