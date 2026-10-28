// The Online menu: the online service (online.service), WiiConnect24 downloads on demand (cf/wc24.h),
// and a Wi-Fi Connection login test for a player of the town (cf/wfc.h), run on a worker thread; its
// steps show in a window. The login test reads the player's DWC record and changes nothing.
#include <cf/config.h>
#include <cf/save_codec.h>
#include <cf/online.h>
#include <cf/wc24.h>
#include <cf/wfc.h>

#include "debug_windows.h"

#include <game/game/d_animal.hpp>
#include <game/game/d_friend.hpp>
#include <game/game/d_mail.hpp>
#include <game/game/d_net_wc24.hpp>
#include <game/game/d_player_mgr.hpp>
#include <game/game/d_post_office.hpp>
#include <game/game/d_private_data.hpp>
#include <game/game/d_save_data.hpp>
#include <game/game/d_wifi.hpp>

#include <imgui.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <array>
#include <mutex>
#include <string>
#include <thread>

dItem::Item makeItemFromBaseId(u16 baseId); // d_item.cpp

namespace cf {
namespace debug {

namespace {

// ---------------------------------------------------------------------------------------------
// Test letter: a letter from one of the town's villagers to the current player, sent through the game's
// own WiiConnect24 path (dNetWc24_c::sendMessage, on a dWifi thread) to this console's Wii number. It
// comes back through the mail server and the game delivers it at its next WiiConnect24 check (loading the
// save from the title): a villager's letter needs no friend (dWifi onReceive).
// ---------------------------------------------------------------------------------------------
constexpr u16 kTestLetterPaper = 460; // the paper of the WiiLink distribution letter

dMail_c sTestLetter;
std::atomic<int> sTestLetterState{0}; // 0 idle, 1 sending, 2 sent, 3 failed
std::string sTestLetterNote;
bool sTestLetterThreadOpen = false; // its dWifi thread's stack not freed yet

void CopyText(wchar16_t* out, std::size_t capacity, const char* text) {
    std::size_t i = 0;
    for (; text[i] != '\0' && i + 1 < capacity; i++) {
        out[i] = static_cast<wchar16_t>(static_cast<unsigned char>(text[i]));
    }
    out[i] = 0;
}

void* SendTestLetterThread(void* arg) {
    const dPostRequest_c* req = static_cast<const dPostRequest_c*>(arg);
    BOOL sent = FALSE;
    if (dNetWc24::g_wc24.open()) {
        u64 key = dNetWc24::g_wc24.mUserId;
        dNetWc24::dNetWc24MsgTag_c tag;
        tag.mRaw = 0;
        tag.mBits.mKind = WIFI_MSG_LETTER;
        sent = dNetWc24::g_wc24.sendMessage(&key, 1, req, tag.mRaw, 0);
        dNetWc24::g_wc24.close();
    }
    sTestLetterState = sent ? 2 : 3;
    return NULL;
}

// Writes the letter and starts the send; the reason in sTestLetterNote when it cannot
void SendTestLetter() {
    dPrivateData_c* player = dPlayerMgr::Mgr_c::getCurrentPlayer();
    if (player == NULL || !player->mPID.isValid()) {
        sTestLetterNote = "No player is in play.";
        return;
    }
    const dAnimal_c* sender = NULL;
    for (const dAnimal_c& animal : dSaveData_c::getTown()->mAnimals.mTown.mAnimals) {
        if (animal.mID.isValid()) {
            sender = &animal;
            break;
        }
    }
    if (sender == NULL) {
        sTestLetterNote = "The town has no villager to send it.";
        return;
    }
    const dItem::Item paper = makeItemFromBaseId(kTestLetterPaper);
    sTestLetter.setup(&sender->mID, &player->mPID, &paper);
    CopyText(sTestLetter.mHeader, ARRAY_SIZE(sTestLetter.mHeader), "Dear \n,");
    dMail_c::findNamePos(sTestLetter.mHeader, &sTestLetter.mNamePos);
    CopyText(sTestLetter.mBody, ARRAY_SIZE(sTestLetter.mBody),
             "This letter went out over\nWiiConnect24 and came\nback to your town.\nIt works!");
    CopyText(sTestLetter.mFooter, ARRAY_SIZE(sTestLetter.mFooter), "Your neighbor");

    dPostRequest_c req;
    req.mData[2] = &sTestLetter;
    req.mSize[2] = sizeof(dMail_c);
    sTestLetterState = 1;
    sTestLetterThreadOpen = true;
    if (!dWifi::createThread(SendTestLetterThread, &req)) {
        sTestLetterThreadOpen = false;
        sTestLetterState = 0;
        sTestLetterNote = "The game's WiiConnect24 thread is busy; try again.";
        return;
    }
    sTestLetterNote.clear();
}


struct LoginTest {
    std::mutex mutex;
    std::atomic<bool> running{false};
    bool done = false;
    std::string player;
    wfc::LoginResult result;
};

LoginTest sTest;
bool sShowWindow = false;
bool sShowDownloads = false;
bool sShowMail = false;
bool sShowFriendCards = false;

// The player's name, ASCII letters kept (the menu's label)
std::string NameOf(const dPrivateData_c* data) {
    std::string name;
    for (const wchar16_t c : data->mPID.player.mName) {
        if (c == 0) {
            break;
        }
        name.push_back(c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?');
    }
    return name;
}

void StartLogin(int slot, const dPrivateData_c* data) {
    if (sTest.running.exchange(true)) {
        return;
    }
    std::array<std::uint8_t, 0x40> userData;
    // The record as the save file holds it (big-endian words): what the login reads
    const u32 offset = static_cast<u32>(offsetof(dSaveTown_c, mPlayers) + slot * sizeof(dPrivateData_c) +
                                        offsetof(dPrivateData_c, mFriends) + offsetof(dFriendList_c, mUserData));
    cf::save::Encode(offset, &data->mFriends.mUserData, userData.data(), static_cast<u32>(userData.size()));
    {
        std::lock_guard lock(sTest.mutex);
        sTest.done = false;
        sTest.player = "Player " + std::to_string(slot + 1) + " (" + NameOf(data) + ")";
        sTest.result = {};
    }
    sShowWindow = true;
    std::thread([userData] {
        wfc::LoginResult result = wfc::TestLogin(userData.data());
        std::lock_guard lock(sTest.mutex);
        sTest.result = std::move(result);
        sTest.done = true;
        sTest.running = false;
    }).detach();
}

void DrawLoginWindow() {
    if (!sShowWindow) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(520, 260), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("WiiLink login test", &sShowWindow)) {
        std::lock_guard lock(sTest.mutex);
        ImGui::TextUnformatted(sTest.player.c_str());
        ImGui::Separator();
        if (!sTest.done) {
            ImGui::TextUnformatted("Logging in...");
        } else {
            if (sTest.result.ok) {
                ImGui::Text("Logged in: profile %d, friend code %04llu-%04llu-%04llu", sTest.result.profileId,
                            static_cast<unsigned long long>(sTest.result.friendCode / 100000000),
                            static_cast<unsigned long long>(sTest.result.friendCode / 10000 % 10000),
                            static_cast<unsigned long long>(sTest.result.friendCode % 10000));
            } else {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed: %s", sTest.result.error.c_str());
            }
            ImGui::Separator();
            ImGui::TextUnformatted(sTest.result.log.c_str());
            if (ImGui::Button("Copy log")) {
                ImGui::SetClipboardText(sTest.result.log.c_str());
            }
        }
    }
    ImGui::End();
}

void DrawDownloadsWindow() {
    if (!sShowDownloads) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(560, 220), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("WiiConnect24 downloads", &sShowDownloads)) {
        if (ImGui::Button("Download now")) {
            wc24::DownloadNow();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("the game reads them at its next WiiConnect24 check (at boot)");
        ImGui::Separator();
        const std::string log = wc24::DownloadLog();
        ImGui::TextUnformatted(log.empty() ? "(nothing yet)" : log.c_str());
    }
    ImGui::End();
}

void DrawMailWindow() {
    if (!sShowMail) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(560, 260), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("WiiConnect24 mail", &sShowMail)) {
        ImGui::TextUnformatted(wc24::MailStatus().c_str());
        if (ImGui::Button("Send and receive now")) {
            wc24::MailNow();
        }
        ImGui::SameLine();
        if (ImGui::Button("Send test mail to myself")) {
            wc24::MailSelfTest();
        }
        ImGui::SameLine();
        if (ImGui::Button("Send test letter to myself") && sTestLetterState != 1) {
            SendTestLetter();
        }
        switch (sTestLetterState.load()) {
        case 1:
            ImGui::TextUnformatted("Test letter: sending...");
            break;
        case 2:
            ImGui::TextUnformatted("Test letter: sent. Save, quit to the title and load the save: it comes in the mail.");
            break;
        case 3:
            ImGui::TextUnformatted("Test letter: the game could not send it (see the log).");
            break;
        }
        if (!sTestLetterNote.empty()) {
            ImGui::TextUnformatted(sTestLetterNote.c_str());
        }
        ImGui::Separator();
        const std::string log = wc24::MailLog();
        ImGui::TextUnformatted(log.empty() ? "(nothing yet)" : log.c_str());
    }
    ImGui::End();
}

} // namespace

void DrawOnlineMenu() {
    if (ImGui::BeginMenu("Online")) {
        if (ImGui::BeginMenu("Service")) {
            // Every service speaks Nintendo's formats; only WiiLink exists for now
            for (const online::Service service : {online::Service::WiiLink}) {
                if (ImGui::MenuItem(online::Info(service).name, nullptr, config::online::service.get() == service)) {
                    config::online::service.set(service);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::MenuItem("Friend cards", nullptr, &sShowFriendCards);
        ImGui::MenuItem("WiiConnect24 mail", nullptr, &sShowMail);
        if (ImGui::MenuItem("WiiConnect24 downloads")) {
            sShowDownloads = true;
            wc24::DownloadNow();
        }
        if (ImGui::BeginMenu("WiiLink login test", !sTest.running)) {
            for (int slot = 0; slot < 4; slot++) {
                const dPrivateData_c* data = dPlayerMgr::Mgr_c::getPlayer(slot);
                const bool exists = data != nullptr && data->mPID.isValid();
                char label[64];
                std::snprintf(label, sizeof(label), "Player %d%s%s%s", slot + 1, exists ? " (" : "",
                              exists ? NameOf(data).c_str() : "", exists ? ")" : "");
                if (ImGui::MenuItem(label, nullptr, false, exists)) {
                    StartLogin(slot, data);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::MenuItem("Login test window", nullptr, &sShowWindow);
        ImGui::EndMenu();
    }
}

void DrawOnlineWindows() {
    // The dWifi thread's stack is freed by whoever polls it, and the game polls only its own jobs
    if (sTestLetterThreadOpen && sTestLetterState != 1 && dWifi::isThreadDone()) {
        sTestLetterThreadOpen = false;
    }
    DrawLoginWindow();
    DrawDownloadsWindow();
    DrawMailWindow();
    DrawFriendCardWindow(&sShowFriendCards);
}

} // namespace debug
} // namespace cf
