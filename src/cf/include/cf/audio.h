// The PC's audio output (src/cf/sdk/ax_rvl.cpp, the software AX)
#pragma once

namespace cf {
namespace audio {

// The output volume, 0 (silent) to 1 (as the game mixes it): applied after the game's own master
// volume (AXSetMasterVolume), so its fades and settings still work. Any thread.
void SetVolume(float volume);

} // namespace audio
} // namespace cf
