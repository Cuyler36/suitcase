// suitcase: settings and readings for the host-input provider (lib/revolution/wpad/provider.cpp),
// which stands in for aurora present-target's bindings. Not part of aurora; set from the config
// (src/cf/config) and shown in the debug UI (src/cf/debug). Main thread only.
#pragma once

namespace suitcase::wpad {

struct PadSettings {
  // Gain on a pad's left stick length, for gates whose diagonals fall short
  float stickGain = 1.f;
  // Radial stick dead zone, [0, 1), for drift: the game has its own per axis (KPAD's clamp)
  float deadZone = 0.1f;
  // Where an analog trigger counts as pressed
  float triggerThreshold = 0.5f;
  // The pointer's speed at full right-stick deflection, in pointer widths per second
  float pointerSpeed = 1.2f;
};

void set_pad_settings(const PadSettings& settings);
const PadSettings& pad_settings();

// The pad pushing its left stick furthest, as of the last sample
struct StickReading {
  bool connected = false;
  float rawX = 0, rawY = 0;         // SDL's axes, Y up
  int nunchukX = 0, nunchukY = 0;   // the Nunchuk counts sent (before KPAD's clamp), Y up
};

StickReading left_stick_reading();

} // namespace suitcase::wpad
