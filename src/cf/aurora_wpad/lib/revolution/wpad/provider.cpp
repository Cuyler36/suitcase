// suitcase: stands in for aurora's present-target provider.cpp, which needs that branch's input
// rework (bindings, profiles). Mouse, keyboard and gamepads on channel 0, through aurora main's input
// layers: the mouse is the IR pointer, the buttons and keys follow the branch's default keyboard
// bindings. Motion sensors are not mapped. Delete with the rest of src/cf/aurora_wpad once aurora
// main has WPAD.
//
// Any SDL gamepad (Xbox, PlayStation, Switch, a GameCube controller, a Classic Controller on an
// adapter) is a Wii Remote and Nunchuk: City Folk reads nothing else. The left stick is the
// Nunchuk's, the right stick (the GameCube controller's C stick) moves the pointer, and the buttons
// follow the pad's layout (kStandardLayout, kGameCubeLayout). The pointer follows whichever of the
// mouse and the right stick moved last; a pad's left stick starting to move the player, or a D-pad
// press, hides it, as on a Wii Remote pointed away, so the game's menus take the stick and the
// cursor goes. Every pad drives channel 0
// together with the keyboard and mouse.
#include "backend.hpp"

#include <suitcase/wpad_host.hpp>

#include "../../input/port.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <span>
#include <unordered_map>

namespace aurora::wpad {
namespace {

using input::InputEvent;

// The emulated extension; aurora_wpad_set_device overrides it per channel.
constexpr u8 kDefaultDevice = WPAD_DEV_FREESTYLE;

// A pad's left stick at full length, in Nunchuk counts. A Nunchuk reads about 100 at the edge of its
// gate. The game clamps each axis from 30 to 71 (KPADSetFSStickClamp(30, 71) in d_sys.cpp, KPAD's
// cross clamp), so a diagonal (about 70 per axis) still reads full length, and the game's speed is
// the stick's length (dAcPlayer_c::calcStickInput). At 71 a diagonal read 0.69. The gain scales the
// stick's length (not per axis, which would snap the direction), for gates whose diagonals fall
// short. The gain, dead zone, trigger point and pointer speed are settings (suitcase/wpad_host.hpp).
constexpr float kNunchukReach = 100.f;
// The game reads nothing from an axis up to this many counts (the bottom of its KPAD clamp)
constexpr int kGameStickDeadZone = 30;
suitcase::wpad::PadSettings settings;
suitcase::wpad::StickReading leftStick;

struct PadButton {
  SDL_GamepadButton button;
  u16 wpad;
};

// SDL's buttons are positional (south is A on Xbox pads, B on Nintendo's)
constexpr PadButton kStandardButtons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, WPAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_EAST, WPAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, WPAD_BUTTON_1},
    {SDL_GAMEPAD_BUTTON_NORTH, WPAD_BUTTON_2},
    {SDL_GAMEPAD_BUTTON_START, WPAD_BUTTON_PLUS},
    {SDL_GAMEPAD_BUTTON_BACK, WPAD_BUTTON_MINUS},
    {SDL_GAMEPAD_BUTTON_GUIDE, WPAD_BUTTON_HOME},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, WPAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, WPAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, WPAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, WPAD_BUTTON_RIGHT},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, WPAD_BUTTON_Z},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, WPAD_BUTTON_C},
};

// The GameCube controller as SDL names its buttons (as aurora's PAD defaults do: A south, B west,
// X east, Y north, Z the right shoulder, L and R's digital clicks MISC3 and MISC4). L and R are Z, Z
// is 1 (the photo), X is +, Y and Start are -; there is no C, 2 or Home.
constexpr PadButton kGameCubeButtons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, WPAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_WEST, WPAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_EAST, WPAD_BUTTON_PLUS},
    {SDL_GAMEPAD_BUTTON_NORTH, WPAD_BUTTON_MINUS},
    {SDL_GAMEPAD_BUTTON_START, WPAD_BUTTON_MINUS},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, WPAD_BUTTON_1},
    {SDL_GAMEPAD_BUTTON_MISC3, WPAD_BUTTON_Z},
    {SDL_GAMEPAD_BUTTON_MISC4, WPAD_BUTTON_Z},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, WPAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, WPAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, WPAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, WPAD_BUTTON_RIGHT},
};

// The Nintendo Switch Online GameCube controller (a Switch Pro type to SDL): L and R are the
// shoulders, Z is Back
constexpr PadButton kSwitchGameCubeButtons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, WPAD_BUTTON_A},
    {SDL_GAMEPAD_BUTTON_WEST, WPAD_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_EAST, WPAD_BUTTON_PLUS},
    {SDL_GAMEPAD_BUTTON_NORTH, WPAD_BUTTON_MINUS},
    {SDL_GAMEPAD_BUTTON_START, WPAD_BUTTON_MINUS},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, WPAD_BUTTON_Z},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, WPAD_BUTTON_Z},
    {SDL_GAMEPAD_BUTTON_BACK, WPAD_BUTTON_1},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, WPAD_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, WPAD_BUTTON_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, WPAD_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, WPAD_BUTTON_RIGHT},
};

struct PadLayout {
  std::span<const PadButton> buttons;
  SDL_GamepadButton recenter; // recentres the pointer (none: SDL_GAMEPAD_BUTTON_INVALID)
  u16 leftTrigger, rightTrigger; // the analog triggers past the trigger threshold
};

// Clicking the right stick recentres the pointer; the triggers are C and Z
constexpr PadLayout kStandardLayout{kStandardButtons, SDL_GAMEPAD_BUTTON_RIGHT_STICK, WPAD_BUTTON_C, WPAD_BUTTON_Z};
// L and R (analog) are Z; the pointer comes back centred after the stick or D-pad hid it
constexpr PadLayout kGameCubeLayout{kGameCubeButtons, SDL_GAMEPAD_BUTTON_INVALID, WPAD_BUTTON_Z, WPAD_BUTTON_Z};
constexpr PadLayout kSwitchGameCubeLayout{kSwitchGameCubeButtons, SDL_GAMEPAD_BUTTON_INVALID, WPAD_BUTTON_Z,
                                          WPAD_BUTTON_Z};

// Picks the layout from SDL's type (and IDs, as aurora's gamepad.cpp does)
const PadLayout* pad_layout(input::SourceId source) {
  const SDL_JoystickID joystick = input::gamepad_for_source(source);
  if (joystick != 0) {
    const SDL_GamepadType type = SDL_GetGamepadTypeForID(joystick);
    const Uint16 vendor = SDL_GetGamepadVendorForID(joystick);
    const Uint16 product = SDL_GetGamepadProductForID(joystick);
    if (type == SDL_GAMEPAD_TYPE_GAMECUBE || (vendor == 0x057E && product == 0x0337)) {
      return &kGameCubeLayout;
    }
    if (type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && product == 0x2073) {
      return &kSwitchGameCubeLayout;
    }
  }
  return &kStandardLayout;
}

struct PadState {
  const PadLayout* layout = &kStandardLayout;
  u16 buttons = 0; // WPAD_BUTTON_*
  float leftX = 0, leftY = 0, rightX = 0, rightY = 0; // SDL axes: positive right / down
  float leftTrigger = 0, rightTrigger = 0;
};

struct HostState {
  u16 buttons = 0;       // WPAD_BUTTON_*, from the mouse and keyboard
  s8 stickX = 0, stickY = 0;
  int keyStickX = 0, keyStickY = 0;
  AuroraWpadPointer pointer{}; // the mouse's
  std::unordered_map<input::SourceId, PadState> pads;
  // The right stick's pointer, while it moved last
  bool padPointer = false;
  // A pad's stick or D-pad hid the pointer (as pointing away from the screen does), so the game
  // takes them for its menus and hides the cursor; the mouse or the right stick shows it again
  bool pointerHidden = false;
  // A pad's left stick moved the player at the last sample: it hides the pointer when it starts to,
  // so a stick held (or drifting) off-centre does not keep hiding it after the mouse moves
  bool padStickMoving = false;
  float padPointerX = 0, padPointerY = 0;
  uint64_t lastSampleNs = 0;
};

std::mutex mutex;
HostState host;
std::array<std::shared_ptr<ChannelProvider>, 4> providers;
input::LayerId layer = input::kInvalidLayerId;
float distance = 1.f;
uint64_t (*clockFn)() = SDL_GetTicksNS;
void (*updateHook)() = nullptr;
void (*shutdownHook)() = nullptr;

s16 raw(float g, float unit, float limit) {
  return static_cast<s16>(std::lround(std::clamp(std::isfinite(g) ? g * unit : 0.f, -limit, limit)));
}

// Copied from present-target's provider.cpp: matches the EFB/XFB presentation viewport.
AuroraWpadPointer map_pointer(SDL_FPoint position, const AuroraWindowSize& size) {
  if (!size.width || !size.height || !size.fb_width || !size.fb_height || !size.native_fb_width ||
      !size.native_fb_height) {
    return {};
  }
  uint32_t width = size.native_fb_width;
  uint32_t height =
      std::min(size.native_fb_height,
               std::max(1u, static_cast<uint32_t>(std::lround(double(width) * size.fb_height / size.fb_width))));
  if (height == size.native_fb_height) {
    width = std::min(size.native_fb_width,
                     std::max(1u, static_cast<uint32_t>(std::lround(double(height) * size.fb_width / size.fb_height))));
  }

  const float left = static_cast<float>(size.native_fb_width - width) / 2.f;
  const float top = static_cast<float>(size.native_fb_height - height) / 2.f;
  return {
      std::clamp(2.f * (position.x * size.native_fb_width / size.width - left) / width - 1.f, -1.f, 1.f),
      std::clamp(2.f * (position.y * size.native_fb_height / size.height - top) / height - 1.f, -1.f, 1.f),
      TRUE,
  };
}

u16 mouse_button(uint8_t button) {
  switch (button) {
  case SDL_BUTTON_LEFT:
    return WPAD_BUTTON_A;
  case SDL_BUTTON_RIGHT:
    return WPAD_BUTTON_B;
  default:
    return 0;
  }
}

u16 key_button(SDL_Scancode key) {
  switch (key) {
  case SDL_SCANCODE_1:
    return WPAD_BUTTON_1;
  case SDL_SCANCODE_2:
    return WPAD_BUTTON_2;
  case SDL_SCANCODE_RETURN:
    return WPAD_BUTTON_PLUS;
  case SDL_SCANCODE_TAB:
    return WPAD_BUTTON_MINUS;
  case SDL_SCANCODE_BACKSPACE:
    return WPAD_BUTTON_HOME;
  case SDL_SCANCODE_UP:
    return WPAD_BUTTON_UP;
  case SDL_SCANCODE_DOWN:
    return WPAD_BUTTON_DOWN;
  case SDL_SCANCODE_LEFT:
    return WPAD_BUTTON_LEFT;
  case SDL_SCANCODE_RIGHT:
    return WPAD_BUTTON_RIGHT;
  case SDL_SCANCODE_SPACE:
    return WPAD_BUTTON_Z;
  case SDL_SCANCODE_LSHIFT:
    return WPAD_BUTTON_C;
  default:
    return 0;
  }
}

void key_stick(SDL_Scancode key, bool pressed) {
  const int delta = pressed ? 1 : -1;
  switch (key) {
  case SDL_SCANCODE_D:
    host.keyStickX += delta;
    break;
  case SDL_SCANCODE_A:
    host.keyStickX -= delta;
    break;
  case SDL_SCANCODE_W:
    host.keyStickY += delta;
    break;
  case SDL_SCANCODE_S:
    host.keyStickY -= delta;
    break;
  default:
    return;
  }
  host.stickX = static_cast<s8>(std::clamp(host.keyStickX, -1, 1) * 71);
  host.stickY = static_cast<s8>(std::clamp(host.keyStickY, -1, 1) * 71);
}

// A stick with the radial dead zone taken out, rescaled to [-1, 1]
SDL_FPoint dead_zone(float x, float y) {
  const float magnitude = std::hypot(x, y);
  if (magnitude <= settings.deadZone) {
    return {0, 0};
  }
  const float scale = std::min((magnitude - settings.deadZone) / (1 - settings.deadZone), 1.f) / magnitude;
  return {x * scale, y * scale};
}

PadState& pad_state(input::SourceId source) {
  const auto [it, added] = host.pads.try_emplace(source);
  if (added) {
    it->second.layout = pad_layout(source);
  }
  return it->second;
}

void on_pad_event(const InputEvent& event) {
  const input::SourceId id = event.source.id;
  if (event.payload.is<InputEvent::Cancelled>()) {
    host.pads.erase(id);
  } else if (const auto* change = event.payload.get_if<InputEvent::SourceChanged>()) {
    if (change->change == InputEvent::SourceChanged::Change::Disconnected) {
      host.pads.erase(id);
    }
  } else if (const auto* button = event.payload.get_if<InputEvent::ButtonChanged>()) {
    auto& pad = pad_state(id);
    if (button->button == pad.layout->recenter) {
      if (button->pressed) {
        host.padPointer = true;
        host.pointerHidden = false;
        host.padPointerX = host.padPointerY = 0;
      }
      return;
    }
    if (button->pressed && (button->button == SDL_GAMEPAD_BUTTON_DPAD_UP ||
                            button->button == SDL_GAMEPAD_BUTTON_DPAD_DOWN ||
                            button->button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                            button->button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
      host.pointerHidden = true;
    }
    for (const auto& mapping : pad.layout->buttons) {
      if (mapping.button == button->button) {
        pad.buttons = button->pressed ? pad.buttons | mapping.wpad : pad.buttons & ~mapping.wpad;
      }
    }
  } else if (const auto* axis = event.payload.get_if<InputEvent::AxisChanged>()) {
    auto& pad = pad_state(id);
    switch (axis->axis) {
    case SDL_GAMEPAD_AXIS_LEFTX:
      pad.leftX = axis->value;
      break;
    case SDL_GAMEPAD_AXIS_LEFTY:
      pad.leftY = axis->value;
      break;
    case SDL_GAMEPAD_AXIS_RIGHTX:
      pad.rightX = axis->value;
      break;
    case SDL_GAMEPAD_AXIS_RIGHTY:
      pad.rightY = axis->value;
      break;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
      pad.leftTrigger = axis->value;
      break;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
      pad.rightTrigger = axis->value;
      break;
    default:
      break;
    }
  }
}

input::EventResult on_event(const InputEvent& event, void*) {
  std::lock_guard lock{mutex};
  if (event.source.kind == input::InputSource::Kind::Controller) {
    on_pad_event(event);
    return input::EventResult::Pass;
  }
  if (event.payload.is<InputEvent::Cancelled>()) {
    if (event.source.kind == input::InputSource::Kind::Mouse) {
      host.pointer.valid = FALSE;
      host.buttons &= ~(WPAD_BUTTON_A | WPAD_BUTTON_B);
    } else if (event.source.kind == input::InputSource::Kind::Keyboard) {
      host.buttons &= WPAD_BUTTON_A | WPAD_BUTTON_B;
      host.keyStickX = host.keyStickY = 0;
      host.stickX = host.stickY = 0;
    }
  } else if (const auto* pointer = event.payload.get_if<InputEvent::PointerChanged>()) {
    using Phase = InputEvent::PointerChanged::Phase;
    if (pointer->phase == Phase::Cancel) {
      host.pointer.valid = FALSE;
    } else {
      host.pointer = map_pointer(pointer->position, aurora_get_window_size());
      host.padPointer = false;
      host.pointerHidden = false;
    }
    if (pointer->phase == Phase::Down) {
      host.buttons |= mouse_button(pointer->button);
    } else if (pointer->phase == Phase::Up) {
      host.buttons &= ~mouse_button(pointer->button);
    }
  } else if (const auto* key = event.payload.get_if<InputEvent::KeyChanged>()) {
    if (!key->repeat) {
      if (const auto bit = key_button(key->scancode)) {
        host.buttons = key->pressed ? host.buttons | bit : host.buttons & ~bit;
      }
      key_stick(key->scancode, key->pressed);
    }
  }

  return input::EventResult::Pass;
}

ChannelSample host_sample(uint32_t port, uint64_t timestampNs) {
  ChannelSample result;
  if (port != 0) {
    result.core.err = WPAD_ERR_NO_CONTROLLER;
    result.core.dev = WPAD_DEV_NOT_FOUND;
    return result;
  }

  std::lock_guard lock{mutex};
  const float dt = host.lastSampleNs != 0 && timestampNs > host.lastSampleNs
                       ? std::min(static_cast<float>(timestampNs - host.lastSampleNs) / 1e9f, 0.1f)
                       : 0.f;
  host.lastSampleNs = timestampNs;

  // The pads: buttons together, each stick from the pad pushing it furthest
  u16 buttons = host.buttons;
  SDL_FPoint left{}, right{};
  SDL_FPoint rawLeft{}; // for the debug UI: the furthest-pushed raw left stick
  for (const auto& [id, pad] : host.pads) {
    if (std::hypot(pad.leftX, pad.leftY) >= std::hypot(rawLeft.x, rawLeft.y)) {
      rawLeft = {pad.leftX, pad.leftY};
    }
    buttons |= pad.buttons;
    if (pad.rightTrigger >= settings.triggerThreshold) {
      buttons |= pad.layout->rightTrigger;
    }
    if (pad.leftTrigger >= settings.triggerThreshold) {
      buttons |= pad.layout->leftTrigger;
    }
    const SDL_FPoint padLeft = dead_zone(pad.leftX, pad.leftY);
    if (std::hypot(padLeft.x, padLeft.y) > std::hypot(left.x, left.y)) {
      left = padLeft;
    }
    const SDL_FPoint padRight = dead_zone(pad.rightX, pad.rightY);
    if (std::hypot(padRight.x, padRight.y) > std::hypot(right.x, right.y)) {
      right = padRight;
    }
  }

  // The left stick as a Nunchuk (Y up), and whether the game sees it move (past its clamp's bottom)
  s8 padStickX = 0, padStickY = 0;
  if (left.x != 0 || left.y != 0) {
    const float length = std::hypot(left.x, left.y);
    const float scale = std::min(length * settings.stickGain, 1.f) / length;
    padStickX = static_cast<s8>(std::lround(left.x * scale * kNunchukReach));
    padStickY = static_cast<s8>(std::lround(-left.y * scale * kNunchukReach));
  }
  const bool padStickMoving = std::abs(padStickX) > kGameStickDeadZone || std::abs(padStickY) > kGameStickDeadZone;
  if (padStickMoving && !host.padStickMoving && right.x == 0 && right.y == 0) {
    host.pointerHidden = true;
  }
  host.padStickMoving = padStickMoving;

  // The right stick moves the pointer from where it is, at the same on-screen speed both ways
  if (right.x != 0 || right.y != 0) {
    if (host.pointerHidden) {
      host.pointerHidden = false;
      host.padPointer = true;
      host.padPointerX = host.padPointerY = 0;
    } else if (!host.padPointer) {
      host.padPointer = true;
      host.padPointerX = host.pointer.valid ? host.pointer.x : 0.f;
      host.padPointerY = host.pointer.valid ? host.pointer.y : 0.f;
    }
    const auto size = aurora_get_window_size();
    const float aspect = size.width && size.height ? static_cast<float>(size.width) / size.height : 1.f;
    // Squared for fine control near the centre
    const float speed = 2.f * settings.pointerSpeed * std::hypot(right.x, right.y) * dt;
    host.padPointerX = std::clamp(host.padPointerX + right.x * speed, -1.f, 1.f);
    host.padPointerY = std::clamp(host.padPointerY + right.y * speed * aspect, -1.f, 1.f);
  }

  result.core.err = WPAD_ERR_NONE;
  result.core.dev = kDefaultDevice;
  result.distance = distance;
  result.synthesizedPointer = true;
  if (host.pointerHidden) {
    result.pointer = {};
  } else if (host.padPointer) {
    result.pointer = {host.padPointerX, host.padPointerY, TRUE};
  } else {
    result.pointer = host.pointer;
  }
  result.core.button = buttons;
  if (left.x != 0 || left.y != 0) {
    result.fs.fsStickX = padStickX;
    result.fs.fsStickY = padStickY;
  } else {
    result.fs.fsStickX = host.stickX;
    result.fs.fsStickY = host.stickY;
  }
  leftStick = {
      .connected = !host.pads.empty(),
      .rawX = rawLeft.x,
      .rawY = -rawLeft.y,
      .nunchukX = result.fs.fsStickX,
      .nunchukY = result.fs.fsStickY,
  };
  // At rest, face up: (0, -1, 0) g in KPAD axes
  result.core.accX = raw(0, 106, 360);
  result.core.accY = raw(0, 106, 360);
  result.core.accZ = raw(1, 106, 360);
  result.fs.fsAccX = raw(0, 204, 428);
  result.fs.fsAccY = raw(0, 204, 428);
  result.fs.fsAccZ = raw(1, 204, 428);
  return result;
}

} // namespace

void initialize_provider() {
  if (layer != input::kInvalidLayerId) {
    return;
  }

  layer = input::register_layer({
      .label = "suitcase.wpad",
      .priority = input::kGameLayerPriority,
      .onEvent = on_event,
      // The game draws its own cursor. aurora asks the layers from the top: over the debug UI (which
      // takes the mouse) the cursor shows.
      .pointerMode = [](void*) { return settings.hideCursor ? input::PointerMode::Hidden : input::PointerMode::None; },
  });
}

void shutdown_provider() {
  if (layer != input::kInvalidLayerId) {
    input::unregister_layer(layer);
  }

  layer = input::kInvalidLayerId;
  providers = {};
  std::lock_guard lock{mutex};
  host = {};
}

void set_channel_provider(uint32_t port, std::shared_ptr<ChannelProvider> provider) {
  if (port < 4) {
    providers[port] = std::move(provider);
  }
}

bool is_simulated_channel(uint32_t port) { return !providers[port]; }

ChannelSample read_channel(uint32_t port, uint64_t timestampNs) {
  return providers[port] ? providers[port]->sample(port, timestampNs) : host_sample(port, timestampNs);
}

// present-target calls these from aurora's input pump; aurora main has no such hook, so the game
// calls aurora_wpad_update() once per frame instead (src/cf/sdk/rvl_misc.cpp, VIWaitForRetrace).
void install_hooks(void (*update)(), void (*shutdown)()) {
  updateHook = update;
  shutdownHook = shutdown;
}

uint64_t timestamp() { return clockFn(); }
double now() { return static_cast<double>(timestamp()) / 1e9; }
float sensor_bar_distance() { return distance; }
void update() { aurora_wpad_update(); }
void shutdown() { aurora_wpad_shutdown(); }

namespace detail {

AuroraWpadPointer map_window_pointer(SDL_FPoint position, const AuroraWindowSize& size) {
  return map_pointer(position, size);
}

void set_clock(uint64_t (*clock)()) { clockFn = clock ? clock : SDL_GetTicksNS; }

} // namespace detail
} // namespace aurora::wpad

void suitcase::wpad::set_pad_settings(const PadSettings& value) {
  std::lock_guard lock{aurora::wpad::mutex};
  aurora::wpad::settings = value;
  aurora::wpad::settings.deadZone = std::clamp(value.deadZone, 0.f, 0.95f);
}

const suitcase::wpad::PadSettings& suitcase::wpad::pad_settings() { return aurora::wpad::settings; }

suitcase::wpad::StickReading suitcase::wpad::left_stick_reading() {
  std::lock_guard lock{aurora::wpad::mutex};
  return aurora::wpad::leftStick;
}

bool aurora::pad::detail::blocked() { return false; }

void aurora_wpad_set_sensor_bar_distance(f32 value) {
  if (std::isfinite(value) && value > 0) {
    aurora::wpad::distance = value;
  }
}
