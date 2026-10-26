// The host keyboard as the Wii's USB keyboard (src/cf/sdk/kbd.cpp).
//
// The host keyboard also drives the Wii Remote (src/cf/aurora_wpad: WASD, arrows, Enter...). While
// the game takes text, the keyboard only types: the KBD layer captures it from the Wii Remote's.
#pragma once

namespace cf {
namespace kbd {

// Whether the game is taking text from the keyboard (dKbd::calcHKB, each frame)
void SetTextInput(bool active);

} // namespace kbd
} // namespace cf
