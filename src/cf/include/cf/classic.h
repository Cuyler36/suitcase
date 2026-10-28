// Every gamepad is a Classic Controller (src/cf/input/host_input.cpp), which City Folk does not read: the
// adapter (src/cf/input/classic_adapter.cpp) turns it into a Wii Remote and Nunchuk where the game reads
// its controllers, with suitcase's stick and pointer tuning, and keeps its L and R for tab cycling.
#pragma once

#include <revolution/types.h>

struct KPADStatus;

namespace cf {
namespace input {

// KPADRead's samples for a channel (newest first), in place: a Classic Controller's become a Wii Remote
// and Nunchuk's. EGG::CoreController::beginFrame, right after KPADRead.
void AdaptKpad(s32 channel, KPADStatus* samples, s32 count);

// The Classic Controller buttons (WPAD_CL_*) pressed on a channel since the last AdaptKpad
u32 ClassicTrigger(s32 channel);

// L or R held to run, outside menus: dPad::updateButtons holds B (and cancel) for it but never presses it,
// which would pick up or pluck
bool ClassicRunHeld(s32 channel);

} // namespace input
} // namespace cf
