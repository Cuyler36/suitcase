// A Classic Controller (every gamepad, src/cf/input/host_input.cpp) as a Wii Remote and Nunchuk, in the
// samples the game reads (cf/classic.h). City Folk reads nothing else.
//
// Buttons: A, B, +, -, Home and the D-pad as the Wii Remote's, X as + (the map), Y as - (the pockets), ZL
// as 1 (a photo) and ZR as Z. L and R cycle the menu tabs (ClassicTrigger, dAcUitag) and outside menus run:
// B held but never pressed (ClassicRunHeld, dPad::updateButtons), so they do not pick up or pluck. The left stick is the Nunchuk's, with a radial
// dead zone and gain (suitcase/wpad_host.hpp), reaching past the game's KPAD clamp (KPADSetFSStickClamp(30,
// 71) in d_sys.cpp) so a diagonal reads full length. The right stick moves the pointer, at the same
// on-screen speed both ways; the pointer follows whichever of the mouse and the right stick moved last, and
// the left stick starting to move the player, or a D-pad press, hides it, as on a Wii Remote pointed away,
// so the game's menus take the stick and the cursor goes.
#include <cf/classic.h>

#include <suitcase/wpad_host.hpp>

#include <game/game/d_menu_mng.hpp>

#include <revolution/kpad.h>
#include <revolution/wpad.h>

#include <aurora/aurora.h>
#include <aurora/wpad.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace cf {
namespace input {
namespace {

// The left stick at full length, in Nunchuk counts. A Nunchuk reads about 100 at the edge of its gate; the
// game's clamp takes each axis from 30 to 71, so at 71 a diagonal read 0.69.
constexpr float kNunchukReach = 100.f;
constexpr int kClampMin = 30;
constexpr int kClampMax = 71;
constexpr float kSampleSeconds = static_cast<float>(aurora::wpad::kSampleIntervalNs) / 1e9f;

constexpr std::array<std::pair<u32, u32>, 13> kButtons{{
    {WPAD_CL_BUTTON_A, WPAD_BUTTON_A},
    {WPAD_CL_BUTTON_B, WPAD_BUTTON_B},
    {WPAD_CL_BUTTON_X, WPAD_BUTTON_PLUS},  // the map (dAcUitag::getButtonTab)
    {WPAD_CL_BUTTON_Y, WPAD_BUTTON_MINUS}, // the pockets
    {WPAD_CL_BUTTON_PLUS, WPAD_BUTTON_PLUS},
    {WPAD_CL_BUTTON_MINUS, WPAD_BUTTON_MINUS},
    {WPAD_CL_BUTTON_HOME, WPAD_BUTTON_HOME},
    {WPAD_CL_BUTTON_UP, WPAD_BUTTON_UP},
    {WPAD_CL_BUTTON_DOWN, WPAD_BUTTON_DOWN},
    {WPAD_CL_BUTTON_LEFT, WPAD_BUTTON_LEFT},
    {WPAD_CL_BUTTON_RIGHT, WPAD_BUTTON_RIGHT},
    {WPAD_CL_TRIGGER_ZL, WPAD_BUTTON_1},   // a photo (dAcUitag)
    {WPAD_CL_TRIGGER_ZR, WPAD_BUTTON_Z},
}};
// Outside menus, L and R run
constexpr u32 kClassicRun = WPAD_CL_TRIGGER_L | WPAD_CL_TRIGGER_R;
constexpr u32 kClassicDpad = WPAD_CL_BUTTON_UP | WPAD_CL_BUTTON_DOWN | WPAD_CL_BUTTON_LEFT | WPAD_CL_BUTTON_RIGHT;

struct Channel {
  u32 buttons = 0;     // the Wii Remote buttons held, made from the last sample's
  u32 trigger = 0;     // the Classic Controller presses since the last AdaptKpad
  bool run = false;    // L or R held to run, outside menus
  bool stickMoving = false;
  // The right stick's pointer, while it moved last
  bool padPointer = false;
  bool pointerHidden = false;
  float pointerX = 0, pointerY = 0;
  // The mouse's pointer as last seen, to tell when it moves
  bool mouseValid = false;
  float mouseX = 0, mouseY = 0;
};

std::array<Channel, WPAD_MAX_CONTROLLERS> channels;
suitcase::wpad::StickReading leftStick;

// A stick with the radial dead zone taken out, rescaled to [-1, 1]
Vec2 dead_zone(Vec2 stick) {
  const float deadZone = suitcase::wpad::pad_settings().deadZone;
  const float magnitude = std::hypot(stick.x, stick.y);
  if (magnitude <= deadZone) {
    return {0, 0};
  }
  const float scale = std::min((magnitude - deadZone) / (1 - deadZone), 1.f) / magnitude;
  return {stick.x * scale, stick.y * scale};
}

// KPAD's cross clamp on a Nunchuk count (kpad.cpp), and its normalization
float clamp_count(float count) {
  const int value = static_cast<int>(std::lround(count));
  const int sign = value < 0 ? -1 : 1;
  const int clamped = sign * std::max(std::abs(value) - kClampMin, 0);
  return std::clamp(static_cast<float>(clamped) / (kClampMax - kClampMin), -1.f, 1.f);
}

// Classic Controller buttons (WPAD_CL_*) as the Wii Remote's
u32 remote_buttons(u32 classic) {
  u32 buttons = 0;
  for (const auto& [from, to] : kButtons) {
    if (classic & from) {
      buttons |= to;
    }
  }
  return buttons;
}

void adapt(Channel& channel, KPADStatus& sample) {
  const auto cl = sample.ex_status.cl; // a copy: the Nunchuk's status shares its storage
  channel.trigger |= cl.trig;

  // KPAD gives every sample of a read the read's presses and releases (the game looks at the newest), so
  // these are translated as they are, not per sample
  const u32 held = remote_buttons(cl.hold);
  sample.hold |= held;
  sample.trig |= remote_buttons(cl.trig);
  sample.release |= remote_buttons(cl.release);
  channel.buttons = held;

  // The left stick as a Nunchuk's counts (Y up), then as KPAD gives them
  const Vec2 left = dead_zone(cl.lstick);
  float countX = 0, countY = 0;
  if (left.x != 0 || left.y != 0) {
    const float length = std::hypot(left.x, left.y);
    const float scale = std::min(length * suitcase::wpad::pad_settings().stickGain, 1.f) / length;
    countX = left.x * scale * kNunchukReach;
    countY = left.y * scale * kNunchukReach;
  }
  Vec2 stick{clamp_count(countX), clamp_count(countY)};
  if (const float magnitude = std::hypot(stick.x, stick.y); magnitude > 1) {
    stick.x /= magnitude;
    stick.y /= magnitude;
  }
  leftStick = {
      .connected = true,
      .rawX = cl.lstick.x,
      .rawY = cl.lstick.y,
      .nunchukX = static_cast<int>(std::lround(countX)),
      .nunchukY = static_cast<int>(std::lround(countY)),
  };

  sample.dev_type = WPAD_DEV_FREESTYLE;
  sample.ex_status.fs = {};
  sample.ex_status.fs.stick = stick;
  // At rest, face up
  sample.ex_status.fs.acc = {0, -1, 0};
  sample.ex_status.fs.acc_value = 1;

  // The mouse moved: its pointer again
  if (sample.dpd_valid_fg > 0 &&
      (!channel.mouseValid || sample.pos.x != channel.mouseX || sample.pos.y != channel.mouseY)) {
    channel.mouseValid = true;
    channel.mouseX = sample.pos.x;
    channel.mouseY = sample.pos.y;
    channel.padPointer = false;
    channel.pointerHidden = false;
  }

  // The left stick starting to move the player, or a D-pad press, hides the pointer
  const Vec2 right = dead_zone(cl.rstick);
  const bool rightMoving = right.x != 0 || right.y != 0;
  const bool stickMoving = stick.x != 0 || stick.y != 0;
  if ((stickMoving && !channel.stickMoving && !rightMoving) || (cl.trig & kClassicDpad)) {
    channel.pointerHidden = true;
  }
  channel.stickMoving = stickMoving;

  // The right stick moves the pointer from where it is, at the same on-screen speed both ways
  if (rightMoving) {
    if (channel.pointerHidden) {
      channel.pointerHidden = false;
      channel.padPointer = true;
      channel.pointerX = channel.pointerY = 0;
    } else if (!channel.padPointer) {
      channel.padPointer = true;
      channel.pointerX = channel.mouseValid ? channel.mouseX : 0.f;
      channel.pointerY = channel.mouseValid ? channel.mouseY : 0.f;
    }
    const auto size = aurora_get_window_size();
    const float aspect = size.width && size.height ? static_cast<float>(size.width) / size.height : 1.f;
    // Squared for fine control near the centre
    const float speed =
        2.f * suitcase::wpad::pad_settings().pointerSpeed * std::hypot(right.x, right.y) * kSampleSeconds;
    channel.pointerX = std::clamp(channel.pointerX + right.x * speed, -1.f, 1.f);
    channel.pointerY = std::clamp(channel.pointerY - right.y * speed * aspect, -1.f, 1.f);
  }

  if (channel.pointerHidden) {
    sample.dpd_valid_fg = 0;
  } else if (channel.padPointer) {
    sample.pos = {channel.pointerX, channel.pointerY};
    sample.dpd_valid_fg = 2;
  }
}

} // namespace

void AdaptKpad(s32 channelId, KPADStatus* samples, s32 count) {
  if (channelId < 0 || channelId >= WPAD_MAX_CONTROLLERS) {
    return;
  }
  Channel& channel = channels[channelId];
  channel.trigger = 0;
  // L and R running, from the newest sample (read before adapting: the Nunchuk's status shares its storage)
  channel.run = count > 0 && samples[0].dev_type == WPAD_DEV_CLASSIC &&
                (samples[0].ex_status.cl.hold & kClassicRun) != 0 && dMenuMng_c::isFree();
  // Oldest first, so presses and releases follow the samples
  for (s32 i = count - 1; i >= 0; i--) {
    KPADStatus& sample = samples[i];
    if (sample.dev_type == WPAD_DEV_CLASSIC) {
      adapt(channel, sample);
    } else {
      // No Classic Controller (or it went: its buttons are released)
      sample.release |= channel.buttons & ~sample.hold;
      channel.buttons = 0;
      leftStick = {};
    }
  }
}

bool ClassicRunHeld(s32 channelId) {
  return channelId >= 0 && channelId < WPAD_MAX_CONTROLLERS && channels[channelId].run;
}

u32 ClassicTrigger(s32 channelId) {
  return channelId >= 0 && channelId < WPAD_MAX_CONTROLLERS ? channels[channelId].trigger : 0;
}

} // namespace input
} // namespace cf

suitcase::wpad::StickReading suitcase::wpad::left_stick_reading() { return cf::input::leftStick; }
