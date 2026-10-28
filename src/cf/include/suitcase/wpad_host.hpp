// Host input on aurora's WPAD (src/cf/input/host_input.cpp): aurora's own Wii Remote and binding profiles,
// with every gamepad a Classic Controller that the adapter turns into a Wii Remote and Nunchuk
// (src/cf/input/classic_adapter.cpp, cf/classic.h). The settings come from the config (src/cf/config) and
// show in the debug UI (src/cf/debug). Main thread only.
#pragma once

namespace suitcase::wpad {

// Sets up aurora's profiles and the cursor (after aurora_initialize); removes the cursor's layer (before
// aurora_shutdown)
void install();
void shutdown();
// Gives each newly connected gamepad its Classic Controller profile. Once a tick, before the Wii Remotes
// are sampled.
void update();

struct PadSettings {
  // Gain on a pad's left stick length, for gates whose diagonals fall short
  float stickGain = 1.f;
  // Radial stick dead zone, [0, 1), for drift: the game has its own per axis (KPAD's clamp)
  float deadZone = 0.1f;
  // Where an analog trigger counts as pressed
  float triggerThreshold = 0.5f;
  // The pointer's speed at full right-stick deflection, in pointer widths per second
  float pointerSpeed = 1.2f;
  // Hide the OS cursor over the game (it draws its own); layers above (the debug UI) still show it
  bool hideCursor = true;
};

void set_pad_settings(const PadSettings& settings);
const PadSettings& pad_settings();

// The pad's left stick, as of the last sample
struct StickReading {
  bool connected = false;
  float rawX = 0, rawY = 0;         // the Classic Controller's stick, Y up
  int nunchukX = 0, nunchukY = 0;   // the Nunchuk counts made of it (before KPAD's clamp), Y up
};

StickReading left_stick_reading();

} // namespace suitcase::wpad
