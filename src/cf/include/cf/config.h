// suitcase's settings: borealis::config vars, saved to config.json in the user directory
// (src/cf/config/config.cpp). Read a var with get() or *var, change it with set(); the change is
// saved by borealis::config::update() each frame (after a short delay). Main thread only.
#pragma once

#include <borealis/config.hpp>

#include <cf/online.h>

namespace cf::online {

// The online.service setting's names
constexpr auto config_enum_values(Service) {
    return borealis::config::enum_table<Service>({{Service::WiiLink, "wiilink"}});
}

} // namespace cf::online

namespace cf {
namespace config {

// Loads config.json from the user directory and applies the settings (main.cpp, after
// aurora_initialize)
void Load(const char* userPath);

namespace graphics {

// The game's widescreen mode, filling the window at any shape (cf/screen.h); read at boot
extern borealis::config::Var<bool> widescreen;
// Frames per second: 60 is the game's; higher draws frames between its 60 Hz ticks; 0 is unlimited
// (cf/frame.h)
extern borealis::config::Var<int> frameRate;

} // namespace graphics

namespace audio {

// The output volume, 0 to 1 (cf/audio.h)
extern borealis::config::Var<float> volume;

} // namespace audio

namespace online {

// The service for WiiConnect24 and Wi-Fi Connection (cf/online.h)
extern borealis::config::Var<cf::online::Service> service;

} // namespace online

namespace debug {

// The FPS counter overlay (src/cf/debug)
extern borealis::config::Var<bool> showFps;

} // namespace debug

namespace input {

// Gamepads (Classic Controllers, src/cf/input/host_input.cpp and classic_adapter.cpp): suitcase::wpad::PadSettings
extern borealis::config::Var<float> stickGain;
extern borealis::config::Var<float> deadZone;
extern borealis::config::Var<float> triggerThreshold;
extern borealis::config::Var<float> pointerSpeed;
extern borealis::config::Var<bool> hideCursor;

} // namespace input

} // namespace config
} // namespace cf
