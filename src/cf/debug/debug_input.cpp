// The Input window: the gamepad settings (cf::config::input), the left stick and the player's
// movement, live
#include <cf/config.h>

#include <game/game/d_a_player.hpp>
#include <game/game/d_pad.hpp>
#include <game/game/d_player_mgr.hpp>

#include <imgui.h>

#include <suitcase/wpad_host.hpp>

#include <cmath>
#include <string>

#include "debug_windows.h"

namespace cf {
namespace debug {

namespace {

// A slider over the var's range, with a reset button while it differs from the default
void VarSlider(const char* label, borealis::config::Var<float>& var, const char* format = "%.2f") {
    float value = var;
    const auto& options = var.options();
    if (ImGui::SliderFloat(label, &value, options.min.value_or(0.f), options.max.value_or(1.f), format)) {
        var.set(value);
    }
    if (var.modified()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(("Reset##" + var.key()).c_str())) {
            var.reset();
        }
    }
}

// The stick's square of travel: the pad's raw position (grey), the dead zone (circle) and what the
// game reads after KPAD's clamp (green, dPad)
void DrawStick(const suitcase::wpad::StickReading& stick, float deadZone, float gameX, float gameY) {
    const float size = 10.f * ImGui::GetFontSize();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 centre{origin.x + size / 2, origin.y + size / 2};
    const float radius = size / 2;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRect(origin, {origin.x + size, origin.y + size}, IM_COL32(120, 120, 120, 255));
    draw->AddCircle(centre, radius, IM_COL32(90, 90, 90, 255), 64);
    draw->AddCircleFilled(centre, radius * deadZone, IM_COL32(70, 40, 40, 255), 32);
    // Y up
    const ImVec2 raw{centre.x + stick.rawX * radius, centre.y - stick.rawY * radius};
    const ImVec2 game{centre.x + gameX * radius, centre.y - gameY * radius};
    draw->AddCircleFilled(raw, 4.f, IM_COL32(180, 180, 180, 255));
    draw->AddCircleFilled(game, 5.f, IM_COL32(80, 220, 100, 255));
    ImGui::Dummy({size, size});
}

} // namespace

void DrawInputWindow(bool* open) {
    if (!ImGui::Begin("Input", open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Gamepad");
    VarSlider("Stick gain", config::input::stickGain);
    VarSlider("Dead zone", config::input::deadZone);
    VarSlider("Trigger threshold", config::input::triggerThreshold);
    VarSlider("Pointer speed", config::input::pointerSpeed, "%.2f widths/s");

    ImGui::SeparatorText("Left stick");
    const auto stick = suitcase::wpad::left_stick_reading();
    if (!stick.connected) {
        ImGui::TextDisabled("No gamepad");
    }
    const float gameX = dPad::getStickX();
    const float gameY = dPad::getStickY();
    DrawStick(stick, config::input::deadZone, gameX, gameY);
    ImGui::Text("Pad      x %+.3f  y %+.3f  r %.3f", stick.rawX, stick.rawY, std::hypot(stick.rawX, stick.rawY));
    ImGui::Text("Nunchuk  x %+4d    y %+4d   (KPAD clamp 30..71)", stick.nunchukX, stick.nunchukY);
    ImGui::Text("Game     x %+.3f  y %+.3f  r %.3f", gameX, gameY, std::hypot(gameX, gameY));

    ImGui::SeparatorText("Player");
    if (dAcPlayer_c* player = dPlayerMgr::getActorRaw(dPlayerMgr::PLAYER_INDEX_CURRENT)) {
        ImGui::Text("Speed       %.3f", player->getSpeedF());
        ImGui::Text("Rotation Y  %+.1f deg", static_cast<s16>(player->getAngleY()) * 360.f / 65536.f);
    } else {
        ImGui::TextDisabled("No player");
    }

    ImGui::End();
}

} // namespace debug
} // namespace cf
