// The Wii's USB keyboard library (KBD) on PC, fed from the host keyboard through aurora's input
// layers. The game's text input (lib/keyboard's HKBManager, dKbd's listener) takes key events with USB
// HID codes and translates them with KBDTranslateHidCode, so the host keyboard types in the name
// entry, letters and chat as a USB keyboard does on the Wii.
//
// - One keyboard, on channel 0, attached when its first key is pressed. SDL scancodes are USB HID
//   usages, so a key's scancode is its HID code.
// - Key events go to the key callback from aurora's event pump, under the interrupt lock (the Wii
//   calls it from the USB interrupt). The host's key repeat stands in for KBD's: repeats are DOWN |
//   REPEAT events, as kbdRepeatHandler sends.
// - KBDTranslateHidCode gives the SDK's codes (KBK_*) for the keys that do not type (Enter, the
//   arrows, the keypad...: the SDK's US map, lib/RVL_SDK/kbd), and the host layout's characters for
//   the rest, so any keyboard types what its keys say. The country is not used.
// - LEDs and the region maps do nothing.
//
// The host keyboard also drives the Wii Remote (src/cf/aurora_wpad). While the game takes text
// (cf::kbd::SetTextInput, from dKbd::calcHKB), this layer captures the keyboard from the layers
// below, so typing does not press Wii Remote buttons.
#include <revolution/kbd.h>

#include <cf/kbd.h>

#include <aurora/input.hpp>

#include <SDL3/SDL_keyboard.h>

#include <atomic>
#include <bitset>

#include "interrupt_lock.h"

namespace {

namespace input = aurora::input;

constexpr KBDChannel CHANNEL = 0;

// The SDK's codes for the keys from Enter (0x28) to 0x67 that do not type; 0 for the host layout
constexpr KBDHidCode SPECIAL_FIRST = 0x28;
constexpr KBDUnicode SPECIAL_KEYS[] = {
    0xF1CD, 0xF1DB, 0xF1C8, 0xF1C9, 0,      0,      0,      0,      // Enter Esc Backspace Tab
    0,      0,      0,      0,      0,      0,      0,      0,      //
    0,      0xF008, 0xF061, 0xF062, 0xF063, 0xF064, 0xF065, 0xF066, // Caps Lock, F1..
    0xF067, 0xF068, 0xF069, 0xF06A, 0xF06B, 0xF06C, 0xF020, 0xF021, // ..F12, Print Screen, Scroll Lock
    0xF022, 0xF1B0, 0xF1B7, 0xF1B9, 0xF1AE, 0xF1B1, 0xF1B3, 0xF1B6, // Pause Insert Home PgUp Del End PgDn →
    0xF1B4, 0xF1B2, 0xF1B8, 0xF007, 0xF12F, 0xF12A, 0xF12D, 0xF12B, // ← ↓ ↑ Num Lock, keypad / * - +
    0xF10D, 0,      0,      0,      0,      0,      0,      0,      // keypad Enter, keypad 1..
    0,      0,      0,      0,      0,      0xF02F, 0,      0xF13D, // ..keypad ., Application, keypad =
};
constexpr KBDHidCode SPECIAL_LAST = SPECIAL_FIRST + sizeof(SPECIAL_KEYS) / sizeof(SPECIAL_KEYS[0]) - 1;

// The keypad's 1..9, 0 and . (0x59..0x63): with Num Lock (and without Shift) the characters, else
// the navigation keys
constexpr KBDHidCode KEYPAD_FIRST = 0x59;
constexpr KBDHidCode KEYPAD_LAST = 0x63;
constexpr KBDUnicode KEYPAD_CHARS[] = {
    0xF131, 0xF132, 0xF133, 0xF134, 0xF135, 0xF136, 0xF137, 0xF138, 0xF139, 0xF130, 0xF12E,
};
constexpr KBDUnicode KEYPAD_NAVIGATION[] = {
    0xF171, 0xF172, 0xF173, 0xF174, 0xF175, 0xF176, 0xF177, 0xF178, 0xF179, 0xF170, 0xF16E,
};

// The modifiers (0xE0..0xE7): Control, Shift, Alt, GUI, left then right
constexpr KBDHidCode MODIFIER_FIRST = KBD_HID_Left_Control;
constexpr KBDHidCode MODIFIER_LAST = KBD_HID_Right_GUI;
constexpr KBDUnicode MODIFIER_KEYS[] = {
    KBK_Mod_Control, KBK_Mod_Shift, KBK_Mod_Alt,   KBK_Mod_GUI,
    KBK_Mod_Control, KBK_Mod_Shift, KBK_Mod_AltGr, KBK_Mod_GUI,
};

bool sInitialized = false;
bool sAttached = false;
KBDDevCallbackF sAttachCallback = nullptr;
KBDDevCallbackF sDetachCallback = nullptr;
KBDKeyCallbackF sKeyCallback = nullptr;
// The lock states (Caps, Num, Scroll Lock): toggled here with lock processing, else set by the game
u32 sLockState = 0;
bool sLockProcessing = true;
std::bitset<256> sHeld;
input::LayerId sLayer = input::kInvalidLayerId;
std::atomic<bool> sTextInput = false;

u32 TransientModState(SDL_Keymod mods) {
    u32 state = 0;
    if (mods & SDL_KMOD_CTRL) {
        state |= KBD_MS_CTRL;
    }
    if (mods & SDL_KMOD_SHIFT) {
        state |= KBD_MS_SHIFT;
    }
    if (mods & SDL_KMOD_LALT) {
        state |= KBD_MS_ALT;
    }
    if (mods & (SDL_KMOD_RALT | SDL_KMOD_MODE)) {
        state |= KBD_MS_ALTGR;
    }
    if (mods & SDL_KMOD_GUI) {
        state |= KBD_MS_GUI;
    }
    return state;
}

void ToggleLock(KBDHidCode hid) {
    switch (hid) {
    case 0x39:
        sLockState ^= KBD_MS_CAPS_LOCK;
        break;
    case 0x53:
        sLockState ^= KBD_MS_NUM_LOCK;
        break;
    case 0x47:
        sLockState ^= KBD_MS_SCROLL_LOCK;
        break;
    }
}

void SendKey(KBDHidCode hid, KBDKeyMode mode, SDL_Keymod mods) {
    cf::sdk::InterruptLockScope interrupts;
    if (!sAttached) {
        sAttached = true;
        KBDDevEvent attach = {CHANNEL};
        if (sAttachCallback != nullptr) {
            sAttachCallback(&attach);
        }
    }
    if (mode == KBD_KM_DOWN && sLockProcessing) {
        ToggleLock(hid);
    }
    if (sKeyCallback == nullptr) {
        return;
    }
    KBDKeyEvent event = {};
    event.channel = CHANNEL;
    event.hid = hid;
    event.mode = mode;
    event.modState = static_cast<KBDModState>(sLockState | TransientModState(mods));
    event.unicode = KBDTranslateHidCode(hid, event.modState, KBD_CC_UNITED_STATES);
    sKeyCallback(&event);
}

input::EventResult OnEvent(const input::InputEvent& event, void*) {
    if (event.source.kind != input::InputSource::Kind::Keyboard) {
        return input::EventResult::Pass;
    }
    if (event.payload.is<input::InputEvent::Cancelled>()) {
        // Focus lost: the held keys go up
        for (u32 hid = 0; hid < sHeld.size(); hid++) {
            if (sHeld[hid]) {
                SendKey(static_cast<KBDHidCode>(hid), KBD_KM_NULL, SDL_KMOD_NONE);
            }
        }
        sHeld.reset();
    } else if (const auto* key = event.payload.get_if<input::InputEvent::KeyChanged>()) {
        const u32 hid = key->scancode;
        if (hid < 4 || hid > MODIFIER_LAST) {
            return input::EventResult::Pass;
        }
        if (!key->pressed) {
            if (sHeld[hid]) {
                sHeld[hid] = false;
                SendKey(static_cast<KBDHidCode>(hid), KBD_KM_NULL, key->modifiers);
            }
        } else if (!sHeld[hid] || !key->repeat) {
            sHeld[hid] = true;
            SendKey(static_cast<KBDHidCode>(hid), KBD_KM_DOWN, key->modifiers);
        } else {
            SendKey(static_cast<KBDHidCode>(hid), static_cast<KBDKeyMode>(KBD_KM_DOWN | KBD_KM_REPEAT),
                    key->modifiers);
        }
    }
    return input::EventResult::Pass;
}

bool CapturesSource(const input::InputSource& source, void*) {
    return source.kind == input::InputSource::Kind::Keyboard && sTextInput.load(std::memory_order_relaxed);
}

// A character from the host layout, or 0 for a key that does not type one
KBDUnicode LayoutChar(KBDHidCode hid, u32 modState) {
    SDL_Keymod mods = SDL_KMOD_NONE;
    if (modState & KBD_MS_SHIFT) {
        mods |= SDL_KMOD_LSHIFT;
    }
    if (modState & KBD_MS_CAPS_LOCK) {
        mods |= SDL_KMOD_CAPS;
    }
    // AltGr, or Control + Alt (Windows' AltGr)
    if ((modState & KBD_MS_ALTGR) || (modState & (KBD_MS_CTRL | KBD_MS_ALT)) == (KBD_MS_CTRL | KBD_MS_ALT)) {
        mods |= SDL_KMOD_MODE;
    }
    const SDL_Keycode key = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(hid), mods, false);
    if ((key & SDLK_SCANCODE_MASK) || key < 0x20 || key == 0x7F || key > 0xFFFF) {
        return 0;
    }
    return static_cast<KBDUnicode>(key);
}

} // namespace

namespace cf {
namespace kbd {

void SetTextInput(bool active) {
    if (sTextInput.exchange(active) != active && sLayer != input::kInvalidLayerId) {
        // Cancels the Wii Remote's held keys when the capture starts
        input::reconcile();
    }
}

} // namespace kbd
} // namespace cf

extern "C" {

KBDEc KBDInit(void) {
    if (sInitialized) {
        return KBD_ERR_ALREADY_INITIALIZED;
    }
    sInitialized = true;
    sLayer = input::register_layer({
        .label = "suitcase.kbd",
        // Above the Wii Remote's layer (kGameLayerPriority), which it captures the keyboard from
        .priority = input::kGameLayerPriority + 1,
        .onEvent = OnEvent,
        .capturesSource = CapturesSource,
    });
    return KBD_SUCCESS;
}

void KBDInitRegionUS(void) {
}

void KBDInitRegionEU(void) {
}

void KBDInitRegionJP(void) {
}

KBDDevCallbackF KBDSetAttachCallback(KBDDevCallbackF callback) {
    cf::sdk::InterruptLockScope interrupts;
    KBDDevCallbackF old = sAttachCallback;
    sAttachCallback = callback;
    return old;
}

KBDDevCallbackF KBDSetDetachCallback(KBDDevCallbackF callback) {
    cf::sdk::InterruptLockScope interrupts;
    KBDDevCallbackF old = sDetachCallback;
    sDetachCallback = callback;
    return old;
}

KBDKeyCallbackF KBDSetKeyCallback(KBDKeyCallbackF callback) {
    cf::sdk::InterruptLockScope interrupts;
    KBDKeyCallbackF old = sKeyCallback;
    sKeyCallback = callback;
    return old;
}

KBDEc KBDResetChannel(KBDChannel channel) {
    if (!sInitialized) {
        return KBD_ERR_NOT_INITIALIZED;
    }
    return channel < KBD_MAX_CHANNELS ? KBD_SUCCESS : KBD_ERR_BAD_VALUE;
}

KBDEc KBDSetLedsAsync(KBDChannel channel, KBDLedState leds, KBDLedCallbackF callback, void* cbArg) {
    if (!sInitialized) {
        return KBD_ERR_NOT_INITIALIZED;
    }
    if (channel >= KBD_MAX_CHANNELS) {
        return KBD_ERR_BAD_VALUE;
    }
    if (callback != nullptr) {
        KBDLedEvent event = {channel, leds, KBD_SUCCESS};
        callback(&event, cbArg);
    }
    return KBD_SUCCESS;
}

KBDEc KBDSetLeds(KBDChannel channel, KBDLedState leds) {
    return KBDSetLedsAsync(channel, leds, nullptr, nullptr);
}

KBDEc KBDSetCountry(KBDChannel channel, KBDCountryCode country) {
    if (!sInitialized) {
        return KBD_ERR_NOT_INITIALIZED;
    }
    return channel < KBD_MAX_CHANNELS && country < KBD_NUM_COUNTRY_CODES ? KBD_SUCCESS : KBD_ERR_BAD_VALUE;
}

KBDEc KBDSetLockProcessing(KBDChannel channel, BOOL enable) {
    if (!sInitialized) {
        return KBD_ERR_NOT_INITIALIZED;
    }
    if (channel >= KBD_MAX_CHANNELS) {
        return KBD_ERR_BAD_VALUE;
    }
    if (channel == CHANNEL) {
        sLockProcessing = enable != FALSE;
    }
    return KBD_SUCCESS;
}

KBDEc KBDSetModState(KBDChannel channel, KBDModState modState) {
    if (!sInitialized) {
        return KBD_ERR_NOT_INITIALIZED;
    }
    if (channel >= KBD_MAX_CHANNELS) {
        return KBD_ERR_BAD_VALUE;
    }
    if (channel == CHANNEL) {
        sLockState = modState & KBD_MS_TOGGLES;
    }
    return KBD_SUCCESS;
}

KBDEc KBDGetModState(KBDChannel channel, KBDModState* modState) {
    if (!sInitialized) {
        return KBD_ERR_NOT_INITIALIZED;
    }
    if (channel >= KBD_MAX_CHANNELS) {
        return KBD_ERR_BAD_VALUE;
    }
    *modState = static_cast<KBDModState>(channel == CHANNEL ? sLockState : 0);
    return KBD_SUCCESS;
}

KBDUnicode KBDTranslateHidCode(KBDHidCode hid, KBDModState modState, KBDCountryCode country) {
    if (!sInitialized || country >= KBD_NUM_COUNTRY_CODES || hid < 4) {
        return KBK_Error;
    }
    if (hid >= MODIFIER_FIRST && hid <= MODIFIER_LAST) {
        return MODIFIER_KEYS[hid - MODIFIER_FIRST];
    }
    if (hid >= KEYPAD_FIRST && hid <= KEYPAD_LAST) {
        const bool chars = (modState & KBD_MS_NUM_LOCK) && !(modState & KBD_MS_SHIFT);
        return (chars ? KEYPAD_CHARS : KEYPAD_NAVIGATION)[hid - KEYPAD_FIRST];
    }
    if (hid >= SPECIAL_FIRST && hid <= SPECIAL_LAST && SPECIAL_KEYS[hid - SPECIAL_FIRST] != 0) {
        return SPECIAL_KEYS[hid - SPECIAL_FIRST];
    }
    return LayoutChar(hid, modState);
}

} // extern "C"
