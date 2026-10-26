// The Wii Remote pointer as it is now (src/cf/aurora_wpad/lib/revolution/wpad/provider.cpp), for frames
// between ticks: the game reads it once a tick, but the cursor it draws can follow it every frame.
#pragma once

namespace cf {
namespace pointer {

// The pointer's position now, as KPAD gives it to the game (-1..1, y down; KPAD does not filter it):
// false when there is none (pointing away, the pointer hidden)
bool GetLive(float* x, float* y);

} // namespace pointer
} // namespace cf
