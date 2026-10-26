#ifndef REVOLUTION_MTX_H
#define REVOLUTION_MTX_H
// PC: aurora's MTX, plus the RVL types of the decomp's include/lib/revolution/mtx/GeoTypes.h.
#include <revolution/types.h>
#include <dolphin/mtx.h>

#ifdef __cplusplus
extern "C" {
#endif

// Also defined by aurora's revolution/kpad.h (src/cf/aurora_wpad), behind the same guard
#ifndef REVOLUTION_VEC2_DEFINED
#define REVOLUTION_VEC2_DEFINED
typedef struct Vec2 {
    f32 x; // 0x00
    f32 y; // 0x04
} Vec2;
#endif

typedef struct Rect {
    f32 left;   // 0x00
    f32 top;    // 0x04
    f32 right;  // 0x08
    f32 bottom; // 0x0C
} Rect;

typedef f32 Mtx23[2][3];
typedef f32 (*Mtx2Ptr)[2];
typedef f32 Mtx33[3][3];
typedef f32 (*Mtx3Ptr)[3];
typedef f32 (*Mtx4Ptr)[4];

#ifdef __cplusplus
}
#endif

#endif // REVOLUTION_MTX_H
