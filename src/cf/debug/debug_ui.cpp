// The ImGui debug UI (src/cf/include/cf/debug.h). aurora owns the ImGui context and frame: draw()
// runs between aurora_begin_frame and aurora_end_frame. While ImGui wants the mouse or keyboard,
// aurora's ImGui input layer keeps them from the game.
#include <cf/debug.h>

#include <borealis/config.hpp>

#include <imgui.h>

#include "debug_windows.h"

namespace cf {
namespace debug {

namespace {

bool sVisible = false;
bool sShowInput = false;
bool sShowDemo = false;

} // namespace

void draw() {
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        sVisible = !sVisible;
    }
    if (!sVisible) {
        return;
    }

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem("Input", nullptr, &sShowInput);
            ImGui::Separator();
            ImGui::MenuItem("ImGui demo", nullptr, &sShowDemo);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Config")) {
            if (auto* resettiDisabled = borealis::config::find<bool>("game.resettiDisabled")) {
                bool value = *resettiDisabled;
                if (ImGui::MenuItem("Disable Resetti", nullptr, &value)) {
                    resettiDisabled->set(value);
                }
            }

            if (auto* skipSplash = borealis::config::find<bool>("game.skipSplashScreens")) {
                bool value = *skipSplash;
                if (ImGui::MenuItem("Skip Splash Screens", nullptr, &value)) {
                    skipSplash->set(value);
                }
            }

            // Changes save by themselves shortly after; this saves at once
            if (ImGui::MenuItem("Save now", nullptr, false, borealis::config::Registry::global().dirty())) {
                borealis::config::flush();
            }
            ImGui::EndMenu();
        }
        ImGui::TextDisabled("F1 hides");
        ImGui::EndMainMenuBar();
    }

    if (sShowInput) {
        DrawInputWindow(&sShowInput);
    }
    if (sShowDemo) {
        ImGui::ShowDemoWindow(&sShowDemo);
    }
}

} // namespace debug
} // namespace cf
