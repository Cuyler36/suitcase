// RVL GX functions and data that aurora does not provide (declared in src/cf/include/revolution/gx.h).
#include <revolution/gx.h>

#include <borealis/log_c.h>

#include <math.h>

namespace {

bool sUnsizedArrayLogged[GX_VA_MAX_ATTR];

GXTlutRegionCallback sTlutRegionCallback;

} // namespace

// aurora needs each vertex array's size and byte order; Wii-style calls have neither. Log each
// attribute once so the remaining call sites can be converted to GXSETARRAY.
void CFGXSetArrayUnsized(GXAttr attr, const void* data, u8 stride) {
    if (attr >= 0 && attr < GX_VA_MAX_ATTR && !sUnsizedArrayLogged[attr]) {
        sUnsizedArrayLogged[attr] = true;
        borealis_log_printf(BOREALIS_LOG_WARNING, "cf::gx", "GXSetArray(%d) without a size: convert the call to GXSETARRAY", (int)attr);
    }
    GXSETARRAY(attr, data, 0, stride, false);
}

void GXNormal3u8(u8 x, u8 y, u8 z) {
    GXNormal3f32(x, y, z);
}

void GXNormal3u16(u16 x, u16 y, u16 z) {
    GXNormal3f32(x, y, z);
}

// TLUT regions are a TMEM concept; aurora manages palettes itself
void GXInitTlutRegion(GXTlutRegion* region, u32 tmem_addr, GXTlutSize tlut_size) {
    (void)region;
    (void)tmem_addr;
    (void)tlut_size;
}

GXTlutRegionCallback GXSetTlutRegionCallback(GXTlutRegionCallback f) {
    GXTlutRegionCallback old = sTlutRegionCallback;
    sTlutRegionCallback = f;
    return old;
}

// The RVL SDK's range-adjustment table: the distance factor for each 32-pixel column band, from the
// projection's near plane and horizontal extent
void GXInitFogAdjTable(GXFogAdjTable* table, u16 width, const f32 projmtx[4][4]) {
    f32 nearZ, sideX;
    if (projmtx[3][3] == 0.0f) {
        // perspective
        nearZ = projmtx[2][3] / (projmtx[2][2] - 1.0f);
        sideX = nearZ / projmtx[0][0];
    } else {
        // orthographic
        sideX = 1.0f / projmtx[0][0];
        nearZ = 1.73205f * sideX;
    }

    f32 iw = 2.0f / width;
    for (u32 i = 0; i < GX_FOG_ADJ_TABLE_SZ; i++) {
        f32 xi = static_cast<f32>((i + 1) << 5) * iw * sideX;
        f32 rangeVal = sqrtf(1.0f + (xi * xi) / (nearZ * nearZ));
        table->r[i] = static_cast<u16>(rangeVal * 256.0f);
    }
}

// The NTSC 480 interlaced/double-field mode on a 60 Hz PAL (EURGB60) TV, as in the RVL SDK
GXRenderModeObj GXEurgb60Hz480IntDf = {
    VI_TVMODE_EURGB60_INT,
    640,
    480,
    480,
    40,
    0,
    640,
    480,
    VI_XFBMODE_DF,
    0,
    0,
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {8, 8, 10, 12, 10, 8, 8},
};
