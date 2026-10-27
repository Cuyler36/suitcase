// suitcase's settings (src/cf/include/cf/config.h). The vars are defined here, where
// <borealis/config_codec.hpp> gives them their JSON encoding; keys are "<group>.<name>".
#include <cf/config.h>
#include <cf/game_config.h>

#include <borealis/config_codec.hpp>
#include <borealis/log.hpp>

#include <suitcase/wpad_host.hpp>

#include <cf/audio.h>
#include <cf/frame.h>
#include <cf/screen.h>

#include <filesystem>
#include <string>

namespace cf {
namespace config {

namespace game {

borealis::config::Var<bool> resettiDisabled{"game.resettiDisabled", false};
borealis::config::Var<bool> skipSplashScreens{"game.skipSplashScreens", true};

bool IsResettiDisabled() {
    return resettiDisabled;
}

bool SkipSplashScreens() {
    return skipSplashScreens;
}

}

namespace {
constexpr borealis::Log Log{"cf::config"};
} // namespace

namespace graphics {

borealis::config::Var<bool> widescreen{"graphics.widescreen", true};
borealis::config::Var<int> frameRate{"graphics.frameRate", frame::kTickRate, {.min = 0, .max = 1000}};

} // namespace graphics

namespace audio {

borealis::config::Var<float> volume{"audio.volume", 1.f, {.min = 0.f, .max = 1.f}};

} // namespace audio

namespace online {

borealis::config::Var<cf::online::Service> service{"online.service", cf::online::Service::WiiLink};

} // namespace online

namespace debug {

borealis::config::Var<bool> showFps{"debug.showFps", false};

} // namespace debug

namespace input {

borealis::config::Var<float> stickGain{"input.stickGain", 1.f, {.min = 1.f, .max = 2.f}};
borealis::config::Var<float> deadZone{"input.deadZone", 0.1f, {.min = 0.f, .max = 0.5f}};
borealis::config::Var<float> triggerThreshold{"input.triggerThreshold", 0.5f, {.min = 0.1f, .max = 0.9f}};
borealis::config::Var<float> pointerSpeed{"input.pointerSpeed", 1.2f, {.min = 0.2f, .max = 4.f}};
borealis::config::Var<bool> hideCursor{"input.hideCursor", true};

namespace {

void ApplyPadSettings() {
    suitcase::wpad::set_pad_settings({
        .stickGain = stickGain,
        .deadZone = deadZone,
        .triggerThreshold = triggerThreshold,
        .pointerSpeed = pointerSpeed,
        .hideCursor = hideCursor,
    });
}

} // namespace

} // namespace input

void Load(const char* userPath) {
    const std::u8string dir(reinterpret_cast<const char8_t*>(userPath));
    const auto status = borealis::config::load({.path = std::filesystem::path(dir) / "config.json"});
    if (!status && status.code != borealis::config::ErrorCode::FileMissing) {
        Log.warn("Could not load the config: {}", status.message);
    }

    // Applied now and on every change, for the vars' lifetime
    for (borealis::config::Var<float>* var :
         {&input::stickGain, &input::deadZone, &input::triggerThreshold, &input::pointerSpeed}) {
        var->on_change([] { input::ApplyPadSettings(); }).release();
    }
    input::hideCursor.on_change([] { input::ApplyPadSettings(); }).release();
    input::ApplyPadSettings();

    // The game reads it once, at boot: later changes apply at the next start
    screen::SetWidescreen(graphics::widescreen);

    graphics::frameRate.observe([](int rate) { frame::SetFrameRate(rate); }).release();
    audio::volume.observe([](float value) { cf::audio::SetVolume(value); }).release();
    online::service.observe([](cf::online::Service value) { cf::online::SetCurrent(value); }).release();
}

} // namespace config
} // namespace cf
