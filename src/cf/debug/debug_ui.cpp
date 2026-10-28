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
        if (frame::IsSpeedup()) {
            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f), "Speedup ON (F2 to stop)");
        }
    }
    ImGui::End();
}

} // namespace

void draw() {
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        sVisible = !sVisible;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        frame::SetSpeedup(!frame::IsSpeedup());
    }
    if (config::debug::showFps || frame::IsSpeedup()) {
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
        DrawOnlineMenu();
        if (ImGui::BeginMenu("Sound")) {
            // The output volume (cf/audio.h), as a percentage
            float percent = config::audio::volume * 100.f;
            if (ImGui::SliderFloat("Volume", &percent, 0.f, 100.f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
                config::audio::volume.set(percent / 100.f);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Config")) {
            if (auto* resettiDisabled = borealis::config::find<bool>("game.resettiDisabled")) {
                bool value = *resettiDisabled;
                if (ImGui::MenuItem("Disable Resetti", nullptr, &value)) {
                    resettiDisabled->set(value);
                }
            }

            if (ImGui::BeginMenu("Grass")) {
                if (auto* detScale = borealis::config::find<float>("game.grass.detScale")) {
                    float value = *detScale;

                    if (ImGui::SliderFloat("Deterioration", &value, 0.0f, 100.0f, "%.2f")) {
                        detScale->set(value);
                    }
                }

                if (auto* restScale = borealis::config::find<float>("game.grass.restScale")) {
                    float value = *restScale;

                    if (ImGui::SliderFloat("Restoration", &value, 0.0f, 100.0f, "%.2f")) {
                        restScale->set(value);
                    }
                }

                ImGui::EndMenu();
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
        if (ImGui::Button(frame::IsSpeedup() ? "Stop speedup (F2)" : "Speedup (F2)")) {
            frame::SetSpeedup(!frame::IsSpeedup());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Unlock the retrace wait and run gameplay as fast as possible for testing.");
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
    DrawOnlineWindows();
}

} // namespace debug
} // namespace cf
