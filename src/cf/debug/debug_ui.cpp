// The ImGui debug UI (src/cf/include/cf/debug.h). aurora owns the ImGui context and frame: draw()
// runs between aurora_begin_frame and aurora_end_frame. While ImGui wants the mouse or keyboard,
// aurora's ImGui input layer keeps them from the game.
#include <cf/config.h>
#include <cf/debug.h>
#include <cf/frame.h>

#include <borealis/config.hpp>

#include <imgui.h>

#include <string>

#include "debug_windows.h"

namespace cf {
namespace debug {

namespace {

bool sVisible = false;
bool sShowInput = false;
bool sShowDemo = false;

// The FPS counter: frames presented and the game's ticks per second, in the top-right corner
void DrawFpsCounter() {
    const cf::frame::Stats stats = cf::frame::GetStats();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float margin = 10.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin, viewport->WorkPos.y + margin),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##fps", nullptr, flags)) {
        ImGui::Text("%.0f FPS (%.2f ms)", stats.framesPerSecond, stats.frameTimeMs);
        ImGui::Text("%.0f ticks/s", stats.ticksPerSecond);
    }
    ImGui::End();
}

} // namespace

void draw() {
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        sVisible = !sVisible;
    }
    if (config::debug::showFps) {
        DrawFpsCounter();
    }
    if (!sVisible) {
        return;
    }

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem("Input", nullptr, &sShowInput);
            bool showFps = config::debug::showFps;
            if (ImGui::MenuItem("FPS counter", nullptr, &showFps)) {
                config::debug::showFps.set(showFps);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Dump 2D layouts")) {
                std::string path;
                DumpLayouts(&path);
            }
            ImGui::MenuItem("ImGui demo", nullptr, &sShowDemo);
            ImGui::EndMenu();
        }
        DrawSceneMenu();
        if (ImGui::BeginMenu("Config")) {
            if (auto* resettiDisabled = borealis::config::find<bool>("game.resettiDisabled")) {
                bool value = *resettiDisabled;
                if (ImGui::MenuItem("Disable Resetti", nullptr, &value)) {
                    resettiDisabled->set(value);
                }
            }

            if (auto* frameRate = borealis::config::find<int>("graphics.frameRate")) {
                // Above 60, frames between the game's 60 Hz ticks (cf/frame.h)
                if (ImGui::BeginMenu("Frame Rate")) {
                    static const int kRates[] = {60, 120, 144, 165, 240, 0};
                    for (int rate : kRates) {
                        const std::string label = rate == 0 ? "Unlimited" : std::to_string(rate);
                        if (ImGui::MenuItem(label.c_str(), nullptr, frameRate->get() == rate)) {
                            frameRate->set(rate);
                        }
                    }
                    ImGui::EndMenu();
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
