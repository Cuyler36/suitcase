// Host input on aurora's WPAD: aurora's own Wii Remote on channel 0, driven by its binding profiles.
//
// The keyboard and mouse are a Wii Remote and Nunchuk (aurora's keyboard defaults, plus Classic Controller
// controls so they keep working while a pad is attached). Every gamepad is a Classic Controller: its
// profile drives only Classic Controller controls, which City Folk does not read, so the adapter where the
// game reads its controllers (src/cf/input/classic_adapter.cpp, from EGG::CoreController::beginFrame)
// turns them into a Wii Remote and Nunchuk with suitcase's stick and pointer tuning. Standard pads map by
// the Xbox labels (south A, east B, west X, north Y); GameCube controllers by their own labels.
#include <suitcase/wpad_host.hpp>

#include <aurora/input.hpp>
#include <aurora/pad.hpp>
#include <aurora/wpad.hpp>

#include <SDL3/SDL_gamepad.h>

#include <algorithm>
#include <initializer_list>
#include <unordered_set>
#include <vector>

namespace suitcase::wpad {
namespace {

namespace input = aurora::input;
namespace pad = aurora::pad;
using aurora::binding::Binding;
using aurora::binding::ControlId;
using aurora::binding::PhysicalInput;

PadSettings settings;
input::LayerId layer = input::kInvalidLayerId;
// The pads whose profile was set up this run
std::unordered_set<input::SourceId> configured;

Binding key(ControlId target, SDL_Scancode scancode, float scale = 1.f) {
  return {.input = {.control = PhysicalInput::Key{scancode}}, .target = target, .scale = scale};
}

Binding button(ControlId target, SDL_GamepadButton gamepadButton) {
  return {.input = {.control = PhysicalInput::GamepadButton{gamepadButton}}, .target = target};
}

Binding axis(ControlId target, SDL_GamepadAxis gamepadAxis) {
  return {.input = {.control = PhysicalInput::GamepadAxis{gamepadAxis}}, .target = target};
}

enum class PadKind { Standard, GameCube, SwitchGameCube };

// From SDL's type and IDs, as aurora's gamepad defaults tell them apart
PadKind pad_kind(input::SourceId source) {
  const SDL_JoystickID joystick = input::gamepad_for_source(source);
  if (joystick != 0) {
    const SDL_GamepadType type = SDL_GetGamepadTypeForID(joystick);
    const Uint16 vendor = SDL_GetGamepadVendorForID(joystick);
    const Uint16 product = SDL_GetGamepadProductForID(joystick);
    if (type == SDL_GAMEPAD_TYPE_GAMECUBE || (vendor == 0x057E && product == 0x0337)) {
      return PadKind::GameCube;
    }
    if (type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && product == 0x2073) {
      return PadKind::SwitchGameCube;
    }
  }
  return PadKind::Standard;
}

// A pad's profile: a Classic Controller, with only Classic Controller controls (the adapter makes the Wii
// Remote and Nunchuk), and suitcase's dead zones (the adapter's, radial) and trigger point
void configure(input::SourceId source) {
  const auto target = pad::ProfileTarget::controller(source);
  const pad::Profile* profile = pad::profile(target);
  pad::ProfileSettings profileSettings = profile != nullptr ? profile->settings : pad::ProfileSettings{};
  profileSettings.wpadExtension = 2; // WPAD_DEV_CLASSIC
  profileSettings.useDeadzones = false;
  profileSettings.emulateTriggers = true;
  profileSettings.leftTriggerThreshold = settings.triggerThreshold;
  profileSettings.rightTriggerThreshold = settings.triggerThreshold;
  pad::set_settings(target, profileSettings);

  const auto& c = aurora::wpad::controls();
  for (const ControlId control : {c.a, c.b, c.one, c.two, c.plus, c.minus, c.home, c.up, c.down, c.left, c.right,
                                  c.irX, c.irY, c.recenter, c.shake, c.fs.c, c.fs.z, c.fs.stickX, c.fs.stickY,
                                  c.fs.shake}) {
    pad::set_binding(target, control, {});
  }

  const auto& cl = c.cl;
  switch (pad_kind(source)) {
  case PadKind::GameCube:
    // SDL's GameCube buttons: A south, B west, X east, Y north, Z the right shoulder, and L and R's
    // digital clicks MISC3 and MISC4 (their analog travel is the triggers). Z is ZL (a photo); there is no ZR.
    pad::set_binding(target, cl.a, {button(cl.a, SDL_GAMEPAD_BUTTON_SOUTH)});
    pad::set_binding(target, cl.b, {button(cl.b, SDL_GAMEPAD_BUTTON_WEST)});
    pad::set_binding(target, cl.x, {button(cl.x, SDL_GAMEPAD_BUTTON_EAST)});
    pad::set_binding(target, cl.y, {button(cl.y, SDL_GAMEPAD_BUTTON_NORTH)});
    pad::set_binding(target, cl.zl, {button(cl.zl, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)});
    pad::set_binding(target, cl.zr, {});
    pad::set_binding(target, cl.l,
                     {axis(cl.l, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), button(cl.l, SDL_GAMEPAD_BUTTON_MISC3)});
    pad::set_binding(target, cl.r,
                     {axis(cl.r, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), button(cl.r, SDL_GAMEPAD_BUTTON_MISC4)});
    break;
  case PadKind::SwitchGameCube:
    // The Nintendo Switch Online GameCube controller (a Switch Pro type to SDL): L and R are the
    // shoulders, Z is Back (ZL, a photo)
    pad::set_binding(target, cl.a, {button(cl.a, SDL_GAMEPAD_BUTTON_SOUTH)});
    pad::set_binding(target, cl.b, {button(cl.b, SDL_GAMEPAD_BUTTON_WEST)});
    pad::set_binding(target, cl.x, {button(cl.x, SDL_GAMEPAD_BUTTON_EAST)});
    pad::set_binding(target, cl.y, {button(cl.y, SDL_GAMEPAD_BUTTON_NORTH)});
    pad::set_binding(target, cl.zl, {button(cl.zl, SDL_GAMEPAD_BUTTON_BACK)});
    pad::set_binding(target, cl.zr, {});
    pad::set_binding(target, cl.minus, {});
    pad::set_binding(target, cl.l, {button(cl.l, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)});
    pad::set_binding(target, cl.r, {button(cl.r, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)});
    break;
  case PadKind::Standard:
    break;
  }
}

} // namespace

void install() {
  if (layer != input::kInvalidLayerId) {
    return;
  }

  // The game draws its own cursor. aurora asks the layers from the top: over the debug UI (which takes
  // the mouse) the cursor shows.
  layer = input::register_layer({
      .label = "suitcase.cursor",
      .priority = input::kGameLayerPriority,
      .onEvent = [](const input::InputEvent&, void*) { return input::EventResult::Pass; },
      .pointerMode = [](void*) { return settings.hideCursor ? input::PointerMode::Hidden : input::PointerMode::None; },
  });

  const auto& c = aurora::wpad::controls();
  const auto& cl = c.cl;
  // Standard pads by the Xbox labels, as before (aurora's defaults go by Nintendo's: east A, south B)
  pad::set_default_controller_bindings({
      button(cl.a, SDL_GAMEPAD_BUTTON_SOUTH),
      button(cl.b, SDL_GAMEPAD_BUTTON_EAST),
      button(cl.x, SDL_GAMEPAD_BUTTON_WEST),
      button(cl.y, SDL_GAMEPAD_BUTTON_NORTH),
  });
  // With a pad (a Classic Controller) on port 0 the Nunchuk's controls are not read: the keyboard's
  // stick and Z also drive the Classic Controller's left stick and ZR, as the adapter maps them. (Its C
  // has no Classic Controller button: ZL is a photo.)
  pad::set_default_keyboard_bindings(0, {
                                            key(cl.leftX, SDL_SCANCODE_D, 1.f),
                                            key(cl.leftX, SDL_SCANCODE_A, -1.f),
                                            key(cl.leftY, SDL_SCANCODE_W, 1.f),
                                            key(cl.leftY, SDL_SCANCODE_S, -1.f),
                                            key(cl.zr, SDL_SCANCODE_SPACE),
                                        });
}

void shutdown() {
  if (layer != input::kInvalidLayerId) {
    input::unregister_layer(layer);
  }
  layer = input::kInvalidLayerId;
  configured.clear();
}

void update() {
  for (const input::InputSource& source : input::sources()) {
    if (source.kind == input::InputSource::Kind::Controller && !configured.contains(source.id)) {
      configured.insert(source.id);
      configure(source.id);
    }
  }
}

void set_pad_settings(const PadSettings& value) {
  const bool reconfigure = value.triggerThreshold != settings.triggerThreshold;
  settings = value;
  settings.deadZone = std::clamp(value.deadZone, 0.f, 0.95f);
  if (reconfigure) {
    configured.clear();
  }
}

const PadSettings& pad_settings() { return settings; }

} // namespace suitcase::wpad
