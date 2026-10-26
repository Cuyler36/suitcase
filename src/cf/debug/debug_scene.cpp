// The Scene menu: changes the room or area as a door does (dScene::requestSceneChange), the player at
// the scene's default position. The menu queues the change; RunGameFrame (src/cf/game/boot.cpp) makes
// it at the start of the next tick.
#include <cf/debug.h>

#include "debug_windows.h"

#include <game/game/d_player_mgr.hpp>
#include <game/game/d_private_data.hpp>
#include <game/game/d_save_data.hpp>
#include <game/game/d_save_shop.hpp>
#include <game/game/d_scene.hpp>
#include <game/game/d_scene_base.hpp>

#include <imgui.h>

#include <cstdio>

namespace cf {
namespace debug {

namespace {

int sPendingScene = -1;

const char* const kHouseFloors[] = {"Main floor", "Upstairs", "Basement", "Loft"};
const char* const kMuseumRooms[] = {"Entrance", "Aquarium", "Insects", "Fossils (1)", "Fossils (2)",
                                    "Paintings (1)", "Paintings (2)", "Cafe", "Observatory"};

// A scene's item: its label, the game's name beside it, checked while it is the current scene
void SceneItem(const char* label, u8 scene, bool enabled = true) {
    if (ImGui::MenuItem(label, dScene::getSceneName(scene), dScene::getCurrentScene() == scene, enabled)) {
        sPendingScene = scene;
    }
}

bool PlayerExists(int player) {
    dPrivateData_c* data = dPlayerMgr::Mgr_c::getPlayer(player);
    return data != nullptr && data->mPID.isValid();
}

// In play: a gameplay scene with its player, no scene change already going
bool CanChangeScene() {
    return dScene_c::m_nowScene != nullptr && dScene_c::m_nowScene->getKind() != SCENE_KIND_BOOT &&
           dScene::getScenePlayer() != nullptr && !dScene::getSceneChange()->isChanging() &&
           dScene::getNextScene() == dScene::SCENE_NONE && !dScene::isCurrentSceneAttr(dScene::SCENE_ATTR_DEMO);
}

} // namespace

void DrawSceneMenu() {
    if (!ImGui::BeginMenu("Scene", CanChangeScene())) {
        return;
    }
    ImGui::TextDisabled("Now: %s", dScene::getSceneName(dScene::getCurrentScene()));
    ImGui::Separator();

    SceneItem("Town", dScene::SCENE_FIELD);

    if (ImGui::BeginMenu("Player houses")) {
        for (int player = 0; player < 4; player++) {
            char label[16];
            std::snprintf(label, sizeof(label), "Player %d", player + 1);
            const bool exists = PlayerExists(player);
            if (ImGui::BeginMenu(label, exists)) {
                for (int floor = 0; floor < 4; floor++) {
                    SceneItem(kHouseFloors[floor], dScene::SCENE_RM_HS0_F1 + player * 4 + floor);
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Villager houses")) {
        for (int i = 0; i < 10; i++) {
            const u8 scene = dScene::SCENE_RM_NPC0 + i;
            char label[16];
            std::snprintf(label, sizeof(label), "Villager %d", i + 1);
            SceneItem(label, scene, dScene::isSceneBuildingPlaced(scene) != FALSE);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Town buildings")) {
        SceneItem("Town Hall", dScene::SCENE_RM_OFFICE);
        SceneItem("Gate", dScene::SCENE_RM_CHKP);
        SceneItem("Able Sisters", dScene::SCENE_RM_TAILOR);

        // Only the store's current stage: the others are not built
        const int stage = dSaveData_c::getTown()->mShops.mShop.mStage;
        if (stage == SHOP_STAGE_NOOKINGTONS) {
            SceneItem("Nookington's (1F)", dScene::SCENE_RM_SHOP3_F1);
            SceneItem("Nookington's (2F)", dScene::SCENE_RM_SHOP3_F2);
        } else {
            static const char* const kStores[] = {"Nook's Cranny", "Nook 'n' Go", "Nookway"};
            if (stage >= 0 && stage < 3) {
                SceneItem(kStores[stage], dScene::SCENE_RM_SHOP0 + stage);
            }
        }

        if (ImGui::BeginMenu("Museum")) {
            for (int i = 0; i < 9; i++) {
                SceneItem(kMuseumRooms[i], dScene::SCENE_RM_MM_ENT + i);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("City")) {
        SceneItem("City", dScene::SCENE_TOWN);
        ImGui::Separator();
        static const struct {
            const char* label;
            u8 scene;
        } kCity[] = {
            {"Crazy Redd's", dScene::SCENE_RM_BROKER},
            {"Shampoodle", dScene::SCENE_RM_BARBER},
            {"Katrina's", dScene::SCENE_RM_FORTUNE},
            {"GracieGrace", dScene::SCENE_RM_GRACE},
            {"HHA (entrance)", dScene::SCENE_RM_HAPPY_ENT},
            {"HHA (model room)", dScene::SCENE_RM_HAPPY_MDL},
            {"Theater (entrance)", dScene::SCENE_RM_THEATER_ENT},
            {"Theater (show)", dScene::SCENE_RM_THEATER_SHOW},
            {"Auction House", dScene::SCENE_RM_AUCTION},
        };
        for (const auto& entry : kCity) {
            SceneItem(entry.label, entry.scene, dScene::isSceneBuildingPlaced(entry.scene) != FALSE);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Other")) {
        SceneItem("Gate (online)", dScene::SCENE_RM_CHKP_NET);
        SceneItem("Resetti's room", dScene::SCENE_RM_RESET);
        SceneItem("Check field", dScene::SCENE_CHECK_FIELD);
        SceneItem("Sample room", dScene::SCENE_RM_SAMPLE);
        ImGui::EndMenu();
    }

    ImGui::EndMenu();
}

void applyPending() {
    if (sPendingScene < 0) {
        return;
    }
    // Wipe 0: the door wipe
    if (CanChangeScene()) {
        dScene::requestSceneChange(static_cast<u8>(sPendingScene), 0, 0);
    }
    sPendingScene = -1;
}

} // namespace debug
} // namespace cf
