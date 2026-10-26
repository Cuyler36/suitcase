// The game's screen shape on PC (src/cf/include/cf/screen.h).
#include <cf/screen.h>

#include <aurora/aurora.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>

#include <lib/egg/gfxe/eggScreen.h>
#include <lib/egg/gfxe/eggStateGX.h>

#include <cmath>

namespace cf {
namespace screen {

namespace {

bool sWidescreen = true;
bool sStretchSet = false;
// Whether the picture is locked to 4:3 (letterboxed), as last set
int sLocked4x3 = -1;

// The game's picture is 640 pixels for 608 (4:3) or WideWidth() virtual units: aurora stretches it to
// the window, or to a 4:3 box in it (VILockAspectRatio; its plain fit would keep the 640 x 456
// frame's shape, 5% too wide)
void SetLocked4x3(bool locked) {
    if (!sStretchSet) {
        sStretchSet = true;
        AuroraSetViewportPolicy(AURORA_VIEWPORT_STRETCH);
    }
    if (sLocked4x3 != static_cast<int>(locked)) {
        sLocked4x3 = locked;
        if (locked) {
            VILockAspectRatio(4, 3);
        } else {
            VIUnlockAspectRatio();
        }
    }
}

} // namespace

void SetWidescreen(bool widescreen) {
    sWidescreen = widescreen;
}

bool IsWidescreen() {
    return sWidescreen;
}

void Update() {
    if (!sWidescreen) {
        // 4:3: letterboxed in the window
        SetLocked4x3(true);
        return;
    }
    const AuroraWindowSize size = aurora_get_window_size();
    if (size.native_fb_width == 0 || size.native_fb_height == 0) {
        return;
    }
    const f32 aspect = static_cast<f32>(size.native_fb_width) / static_cast<f32>(size.native_fb_height);
    // Narrower than 4:3: the 4:3 width, letterboxed (the game's layouts need at least that much).
    // Rounded first: a 4:3 window is 607.99 units before rounding.
    u16 width = static_cast<u16>(std::lround(aspect * kHeight));
    const bool narrow = width < kWidth4x3;
    SetLocked4x3(narrow);
    if (narrow) {
        width = static_cast<u16>(kWidth4x3);
    }

    EGG::Screen::TVModeInfo& wide = EGG::Screen::sTVModeInfo[EGG::Screen::TV_MODE_16_9];
    if (wide.width == width) {
        return;
    }
    wide.width = width;
    if (EGG::StateGX::getEfbWidth() != 0) {
        wide.ratios.x = static_cast<f32>(EGG::StateGX::getEfbWidth()) / static_cast<f32>(width);
    }
    // Resizes the root screen and tells the game (the screens made per draw follow by themselves)
    if (EGG::Screen::sTVMode == EGG::Screen::TV_MODE_16_9) {
        EGG::Screen::SetTVMode(EGG::Screen::TV_MODE_16_9);
    }
}

f32 WideWidth() {
    return EGG::Screen::sTVModeInfo[EGG::Screen::TV_MODE_16_9].width;
}

f32 WideScale() {
    return WideWidth() / kWidth4x3;
}

f32 ViewWidthScale() {
    if (!sWidescreen) {
        return 1.0f;
    }
    const f32 scale = WideWidth() / 832.0f;
    return scale > 1.0f ? scale : 1.0f;
}

} // namespace screen
} // namespace cf
