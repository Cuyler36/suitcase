// suitcase's settings: borealis::config vars, saved to config.json in the user directory
// (src/cf/config/config.cpp). Read a var with get() or *var, change it with set(); the change is
// saved by borealis::config::update() each frame (after a short delay). Main thread only.
#pragma once

#include <borealis/config.hpp>

namespace cf {
namespace config {

// Loads config.json from the user directory and applies the settings (main.cpp, after
// aurora_initialize)
void Load(const char* userPath);

namespace input {

// Gamepads (the Wii Remote stand-in, src/cf/aurora_wpad): suitcase::wpad::PadSettings
extern borealis::config::Var<float> stickGain;
extern borealis::config::Var<float> deadZone;
extern borealis::config::Var<float> triggerThreshold;
extern borealis::config::Var<float> pointerSpeed;

} // namespace input

} // namespace config
} // namespace cf
