# Generates the PC revolution/ header layer in src/cf/include/revolution/ (and its lib/revolution/ mirror).
# Usage: python tools/gen_pc_revolution_layer.py .  (regenerates the directories from scratch)
import os
import sys

root = sys.argv[1]
out = os.path.join(root, "src/cf/include/revolution")
A = os.path.join(root, "extern/aurora/include/dolphin")

import shutil

for d in (out, os.path.join(root, "src/cf/include/lib/revolution")):
    shutil.rmtree(d, ignore_errors=True)


def write(rel, text):
    path = os.path.join(out, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        f.write(text.lstrip("\n"))


def forward(rel, target, note="aurora implements this module"):
    guard = "CF_REVOLUTION_" + rel.upper().replace("/", "_").replace(".", "_")
    write(rel, f"""
#ifndef {guard}
#define {guard}
// PC: {note}; forwards to its header.
#include <{target}>
#endif
""")


# Subheaders aurora has under the same name
for sub in ("gx", "os"):
    for name in sorted(os.listdir(os.path.join(root, "include/lib/revolution", sub))):
        if os.path.exists(os.path.join(A, sub, name)):
            forward(f"{sub}/{name}", f"revolution/{sub}.h", "aurora implements this module (with the RVL additions)")

forward("mtx/GeoTypes.h", "revolution/mtx.h", "aurora implements MTX (with the RVL types)")
forward("base/PPCArch.h", "dolphin/base/PPCArch.h")
forward("base/PPCWGPipe.h", "dolphin/gx/GXVert.h", "aurora defines PPCWGPipe in GXVert.h")
forward("pad.h", "dolphin/pad.h")

# WPAD/KPAD: aurora's implementation, vendored from its present-target branch until it is on main
# (src/cf/aurora_wpad), plus the decomp's names aurora does not define.
write("wpad.h", """
#ifndef CF_REVOLUTION_WPAD_H
#define CF_REVOLUTION_WPAD_H
// PC: aurora's WPAD (vendored in src/cf/aurora_wpad until aurora main has it), plus the names of the
// decomp's include/lib/revolution/wpad.h that aurora does not define.
#include <revolution/types.h>
#include "../../aurora_wpad/include/revolution/wpad.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WPAD_DEV_FUTURE 251
#define WPAD_DEV_NULL 254

#define WPAD_MOTOR_OFF WPAD_MOTOR_STOP
#define WPAD_MOTOR_ON WPAD_MOTOR_RUMBLE

#define WPAD_SPEAKER_OFF 0
#define WPAD_SPEAKER_ON 1
#define WPAD_SPEAKER_MUTE 2
#define WPAD_SPEAKER_UNMUTE 3
#define WPAD_SPEAKER_PLAY 4

#define WPAD_SENSOR_BAR_BOTTOM WPAD_SENSOR_BAR_POS_BOTTOM
#define WPAD_SENSOR_BAR_TOP WPAD_SENSOR_BAR_POS_TOP

// Not in aurora; stubbed in src/cf/sdk/stubs_wpad.cpp
s32 WPADReadFaceData(s32 chan, void* dst, u16 size, u16 src, WPADCallback cb);
s32 WPADWriteFaceData(s32 chan, void* dst, u16 size, u16 src, WPADCallback cb);

#ifdef __cplusplus
}
#endif

#endif
""")
write("kpad.h", """
#ifndef CF_REVOLUTION_KPAD_H
#define CF_REVOLUTION_KPAD_H
// PC: aurora's KPAD (vendored in src/cf/aurora_wpad until aurora main has it), plus what the decomp's
// include/lib/revolution/kpad.h has that aurora does not.
#include <revolution/mtx.h>
#include <revolution/wpad.h>
#include "../../aurora_wpad/include/revolution/kpad.h"

#ifdef __cplusplus
extern "C" {
#endif

// Not in aurora; stubbed in src/cf/sdk/stubs_wpad.cpp
void KPADEnableStickCrossClamp(void);

#ifdef __cplusplus
}
#endif

#endif
""")
forward("ai.h", "dolphin/ai.h")
write("mtx.h", """
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
""")
# The OS module headers that aurora splits differently
forward("os/OSArena.h", "revolution/os.h", "the arena functions are declared in revolution/os.h")
forward("os/OSBootInfo.h", "revolution/os.h", "aurora defines OSBootInfo in os.h")
forward("os/OSLink.h", "dolphin/os/OSModule.h", "aurora's OSModule.h is the RVL OSLink.h")
for name in ("vi3in1.h", "vi3in1types.h", "vifuncs.h", "vitypes.h"):
    forward(f"vi/{name}", "revolution/vi.h", "VI is aurora's; the RVL additions are in revolution/vi.h")

write("types.h", """
#ifndef REVOLUTION_TYPES_H
#define REVOLUTION_TYPES_H
// PC: aurora's base types (identical to include/types.h on PC) plus the RVL additions of the
// decomp's include/lib/revolution/types.h.
#include <dolphin/types.h>
#include <decomp.h>

#if defined(_MSC_VER) && !defined(__clang__)
// aurora's MSVC ATTRIBUTE_ALIGN is __declspec(align), which MSVC rejects after the declarator, where
// the decomp puts it. aurora's headers do not use the macro, so dropping it changes no aurora layout.
#undef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(num)
#endif

typedef unsigned int uBOOL;
typedef volatile BOOL vBOOL;
typedef volatile uBOOL vuBOOL;

#define DEFAULT_ALIGN 32
#define ALIGN32 ATTRIBUTE_ALIGN(DEFAULT_ALIGN)
#define ALIGN64 ATTRIBUTE_ALIGN(64)

#if defined(__GNUC__) || defined(__clang__)
#define PACKED __attribute__((packed))
#else
#define PACKED
#endif

// UTF-16 text, as in include/types.h (same guard): char16_t in C++, unsigned short in C
#ifndef DECOMP_WCHAR16_DEFINED
#define DECOMP_WCHAR16_DEFINED
#ifdef __cplusplus
typedef char16_t wchar16_t;
#else
typedef unsigned short wchar16_t;
#endif
#define WSTR(s) u##s
#endif
#ifdef __cplusplus
#include <port/wchar16.h>
#endif

#endif // REVOLUTION_TYPES_H
""")

write("os.h", """
#ifndef REVOLUTION_OS_H
#define REVOLUTION_OS_H
// PC: aurora's OS, plus the RVL (Wii-only) parts of the decomp's include/lib/revolution/os.h.
// Functions without an aurora implementation are provided by src/cf/sdk.
#include <revolution/types.h>
#include <dolphin/os.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OS_CONSOLE_MASK (0xF000 << 16)
#define OS_CONSOLE_MASK_RVL (0x0000 << 16)
#define OS_CONSOLE_MASK_DEV (0x1000 << 16)
#define OS_CONSOLE_MASK_TDEV (0x2000 << 16)
#define OS_CONSOLE_RETAIL (OS_CONSOLE_MASK_RVL + 0x0021)

enum {
    OS_APP_TYPE_IPL = 0x40,
    OS_APP_TYPE_DVD = 0x80,
    OS_APP_TYPE_CHANNEL = 0x81,
    OS_APP_TYPE_84 = 0x84,
};

u8 OSGetAppType(void);
const char* OSGetAppGamename(void);

// MEM1/MEM2 arenas (OSArena.h)
void* OSGetMEM1ArenaHi(void);
void* OSGetMEM2ArenaHi(void);
void* OSGetMEM1ArenaLo(void);
void* OSGetMEM2ArenaLo(void);
void OSSetMEM1ArenaHi(void* hi);
void OSSetMEM2ArenaHi(void* hi);
void OSSetMEM1ArenaLo(void* lo);
void OSSetMEM2ArenaLo(void* lo);
void* OSAllocFromMEM1ArenaLo(u32 size, u32 align);
void* OSAllocFromMEM2ArenaLo(u32 size, u32 align);

// OSThread.h
typedef void* (*OSThreadFunc)(void* param);

// OSTime.h
void OSSleepTicks(OSTime ticks);
#define OSSleepSeconds(s) OSSleepTicks(OSSecondsToTicks((OSTime)s))
#define OSSleepMilliseconds(ms) OSSleepTicks(OSMillisecondsToTicks((OSTime)ms))
#define OSSleepMicroseconds(us) OSSleepTicks(OSMicrosecondsToTicks((OSTime)us))

// OSAlarm.h
void OSSetAlarmUserData(OSAlarm* alarm, void* userData);
void* OSGetAlarmUserData(OSAlarm* alarm);

// OSReset.h / OSResetSW.h
void OSRebootSystem(void);
void OSReturnToMenu(void);
void OSShutdownSystem(void);
void OSRestart(u32 resetCode);
void OSReturnToDataManager(void);
typedef void (*OSPowerCallback)(void);
OSPowerCallback OSSetPowerCallback(OSPowerCallback callback);

// OSFastCast.h: aurora's fast casts need no GQR setup
#define OS_GQR_F32 0x0000
#define OS_GQR_U8 0x0004
#define OS_GQR_U16 0x0005
#define OS_GQR_S8 0x0006
#define OS_GQR_S16 0x0007
static inline void OSSetGQR6(u32 type, u32 scale) {
    (void)type;
    (void)scale;
}
static inline void OSSetGQR7(u32 type, u32 scale) {
    (void)type;
    (void)scale;
}

#ifndef ASSERTLINE
#define ASSERTLINE(x, line) ((void)0)
#define ASSERTMSGLINE(x, line, ...) ((void)0)
#endif

// OSError.h
#define OSHalt(msg, line) OSPanic(__FILE__, line, msg)
#define OSAssertMsg(exp, msg, line)     if (!(exp))                         OSHalt(msg, line)
#define OSAssertVMsg(exp, line, ...)     if (!(exp))                          OSPanic(__FILE__, line, __VA_ARGS__)

#ifdef __cplusplus
}

extern "C++" {
// The RVL SDK's macro accepts const pointers; aurora's function does not
inline u32 OSCachedToPhysical(const void* caddr) {
    return OSCachedToPhysical(const_cast<void*>(caddr));
}
}
#endif

#endif // REVOLUTION_OS_H
""")

write("gx.h", """
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
""")

write("vi.h", """
#ifndef REVOLUTION_VI_H
#define REVOLUTION_VI_H
// PC: aurora's VI, plus the RVL additions of the decomp's include/lib/revolution/vi/*.h.
#include <revolution/types.h>
#include <dolphin/vi.h>

#ifdef __cplusplus
extern "C" {
#endif

// TV modes aurora's VITVMode lacks
#define VI_TVMODE_PAL_PROG ((VITVMode)VI_TVMODE(VI_PAL, VI_PROGRESSIVE))
#define VI_TVMODE_EURGB60_PROG ((VITVMode)VI_TVMODE(VI_EURGB60, VI_PROGRESSIVE))
#define VI_TVMODE_MPAL_PROG ((VITVMode)VI_TVMODE(VI_MPAL, VI_PROGRESSIVE))

// VIGetDTVStatus(): a component (D-terminal) cable is connected
#define VI_VISEL_COMPONENT (1 << 0)

#define VI_MAX_WIDTH_FRAMEBUFFER 608
#define VI_MAX_HEIGHT_FRAMEBUFFER 456

s32 VIGetDimmingCount(void);
BOOL VIEnableDimming(BOOL flag);

#ifdef __cplusplus
}
#endif

#endif // REVOLUTION_VI_H
""")

write("dvd.h", """
#ifndef REVOLUTION_DVD_H
#define REVOLUTION_DVD_H
// PC: aurora's DVD (backed by nod), plus the RVL additions of the decomp's dvd.h.
#include <revolution/types.h>
#include <dolphin/dvd.h>

#ifdef __cplusplus
extern "C" {
#endif

// RVL command priorities
enum {
    DVD_PRIO_HIGHEST = 0,
    DVD_PRIO_HIGH,
    DVD_PRIO_MEDIUM,
    DVD_PRIO_LOW,

    DVD_PRIO_MAX,
};

// RVL names for aurora's states and results. aurora reports the GameCube values, so each RVL name
// maps to aurora's code for the same condition (DVD_RESULT_CANCELED is aurora's -6, not the RVL -3).
#define DVD_RESULT_OK DVD_RESULT_GOOD
#define DVD_RESULT_FATAL DVD_RESULT_FATAL_ERROR
#define DVD_STATE_FATAL DVD_STATE_FATAL_ERROR
#define DVD_STATE_IDLE DVD_STATE_END
#define DVD_STATE_COVER_OPENED DVD_STATE_COVER_OPEN
#define DVD_STATE_WRONG_DISK_ID DVD_STATE_WRONG_DISK
#define DVD_STATE_PAUSED DVD_STATE_PAUSING
// RVL only (11 there, which is aurora's DVD_STATE_RETRY): a value aurora never reports
#define DVD_STATE_DISK_ERROR 100

// The RVL name of aurora's DVDCBCallback (same signature)
typedef DVDCBCallback DVDCommandCallback;

BOOL DVDCheckDiskAsync(DVDCommandBlock* block, DVDCommandCallback callback);

#ifdef __cplusplus
}
#endif

#endif // REVOLUTION_DVD_H
""")
# Game code also includes SDK headers as <lib/revolution/...>; mirror the overrides there
for dp, _, fs in os.walk(out):
    for f in fs:
        rel = os.path.relpath(os.path.join(dp, f), out).replace(os.sep, "/")
        mirror = os.path.join(root, "src/cf/include/lib/revolution", rel)
        os.makedirs(os.path.dirname(mirror), exist_ok=True)
        guard = "CF_LIB_REVOLUTION_" + rel.upper().replace("/", "_").replace(".", "_")
        with open(mirror, "w", newline=chr(10)) as h:
            h.write(chr(10).join([
                f"#ifndef {guard}",
                f"#define {guard}",
                f"// PC: the same override as <revolution/{rel}>",
                f"#include <revolution/{rel}>",
                "#endif",
                "",
            ]))
print("written")
for dp, _, fs in os.walk(out):
    for f in sorted(fs):
        print(os.path.relpath(os.path.join(dp, f), out))
