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
