// The game's screen shape on PC (src/cf/game/screen.cpp).
//
// The Wii has two: 4:3 (608 x 456 virtual units) and 16:9 (832 x 456, shown anamorphic: the TV
// stretches the 640-pixel picture). On PC, widescreen is the game's 16:9 mode with its width
// following the window: 456 x the window's aspect, so any window shape is filled undistorted (aurora
// stretches the 640-pixel picture to the window). The 3D view widens with the window; the 2D layouts
// keep their 4:3 design, centred, as on a 16:9 TV.
//
// The game's widescreen code hard-codes the 16:9 width (832, its half 416, and the ratios 832 / 608 =
// 1.368421 and 608 / 832 = 0.730769, or 19 / 26); on PC those use WideWidth() and WideScale().
#pragma once

#include <types.h>

namespace cf {
namespace screen {

// The 4:3 virtual width, and the virtual height of both modes
constexpr f32 kWidth4x3 = 608.0f;
constexpr f32 kHeight = 456.0f;

// Widescreen (the graphics.widescreen setting): the game reads it at boot (SCGetAspectRatio), so set
// it before the game starts
void SetWidescreen(bool widescreen);
bool IsWidescreen();

// Each frame, before the game's: follows the window's shape
void Update();

// The wide mode's virtual width (832 on the Wii)
f32 WideWidth();
// WideWidth() / 608 (the Wii's 1.368421)
f32 WideScale();

} // namespace screen
} // namespace cf
