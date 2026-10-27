// Friend cards: what a player's friends learn about them on the first online visit (dWifi::updateFriend
// from the visitor's data), as a file or a text code to give to another player, so friends can write to
// each other without a visit (docs/wc24/README.md).
//
// A card holds the player's player and town ID (dPersonalID_c, in the save's byte order, so cards work
// between any hosts), name, town, greeting (dFriendList_c::mName), console Wii number and Wi-Fi
// Connection profile ID. Importing one into a player's friend list fills a slot as the game does once a
// friend has visited: the friend's DWC data (their profile ID, as a mutual friend) and dFriend_c (Wii
// number, ID, greeting). An entry for the same friend (the ID, or a friend code of the profile) is
// reused; else the first free slot. The game's WiiConnect24 check then registers them as a mail friend.
#include <cf/save_codec.h>
#include <cf/wc24.h>

#include "debug_windows.h"

#include <dwc/dwc.h>
#include <game/game/d_friend.hpp>
#include <game/game/d_player_mgr.hpp>
#include <game/game/d_private_data.hpp>
#include <game/game/d_save_data.hpp>
#include <game/game/d_wifi.hpp>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using nlohmann::json;

namespace cf {
namespace debug {

namespace {

constexpr const char* kCardFormat = "suitcase-friend-card";
constexpr int kCardVersion = 1;
constexpr const char* kCodePrefix = "SCFC1:"; // a card as text: this, then the card's JSON in Base64
// DWCFriendData word 0: the data type in bits 11..12 (3: a profile ID), bit 13 a mutual (buddy) friend
constexpr u32 kProfileFriendFlags = (3u | 4u) << 11;

std::string sStatus;
char sCodeInput[2048];

// ---------------------------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------------------------
std::string ToUtf8(const wchar16_t* text, std::size_t max) {
    std::string out;
    for (std::size_t i = 0; i < max && text[i] != 0; i++) {
        const u32 c = text[i];
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

void FromUtf8(const std::string& text, wchar16_t* out, std::size_t capacity) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < text.size() && n + 1 < capacity;) {
        const unsigned char c = text[i];
        u32 code;
        if (c < 0x80) {
            code = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            code = ((c & 0x1F) << 6) | (text[i + 1] & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            code = ((c & 0x0F) << 12) | ((text[i + 1] & 0x3F) << 6) | (text[i + 2] & 0x3F);
            i += 3;
        } else {
            code = '?';
            i += 1;
        }
        out[n++] = static_cast<wchar16_t>(code);
    }
    out[n] = 0;
}

std::string Hex(const u8* data, std::size_t size) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < size; i++) {
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 15]);
    }
    return out;
}

bool Unhex(const std::string& text, u8* out, std::size_t size) {
    if (text.size() != size * 2) {
        return false;
    }
    for (std::size_t i = 0; i < size; i++) {
        const int hi = std::isxdigit(static_cast<unsigned char>(text[i * 2])) ? std::stoi(text.substr(i * 2, 1), nullptr, 16) : -1;
        const int lo = std::isxdigit(static_cast<unsigned char>(text[i * 2 + 1])) ? std::stoi(text.substr(i * 2 + 1, 1), nullptr, 16) : -1;
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = static_cast<u8>((hi << 4) | lo);
    }
    return true;
}

const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64(const std::string& data) {
    std::string out;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const u32 n = (u32(u8(data[i])) << 16) | (i + 1 < data.size() ? u32(u8(data[i + 1])) << 8 : 0) |
                      (i + 2 < data.size() ? u8(data[i + 2]) : 0);
        out.push_back(kBase64[(n >> 18) & 63]);
        out.push_back(kBase64[(n >> 12) & 63]);
        out.push_back(i + 1 < data.size() ? kBase64[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < data.size() ? kBase64[n & 63] : '=');
    }
    return out;
}

std::string Unbase64(const std::string& text) {
    std::string out;
    u32 bits = 0;
    int count = 0;
    for (const char c : text) {
        const char* p = std::strchr(kBase64, c);
        if (c == '\0' || p == nullptr) {
            continue;
        }
        bits = (bits << 6) | static_cast<u32>(p - kBase64);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out.push_back(static_cast<char>((bits >> count) & 0xFF));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Cards
// ---------------------------------------------------------------------------------------------

// Where a player's ID is in the save file: its conversion between native and file order
u32 SavePidOffset(int slot) {
    return static_cast<u32>(offsetof(dSaveTown_c, mPlayers) + slot * sizeof(dPrivateData_c) +
                            offsetof(dPrivateData_c, mPID));
}

int ProfileIdOf(const dPrivateData_c* player) {
    DWCFriendData token;
    DWC_CreateExchangeToken(&player->mFriends.mUserData, &token);
    return DWC_GetGsProfileId(&player->mFriends.mUserData, &token);
}

json MakeCard(int slot) {
    const dPrivateData_c* player = dPlayerMgr::Mgr_c::getPlayer(slot);
    u8 pid[sizeof(dPersonalID_c)];
    cf::save::Encode(SavePidOffset(slot), &player->mPID, pid, sizeof(pid));
    char wiiNumber[24];
    std::snprintf(wiiNumber, sizeof(wiiNumber), "%016llu", cf::wc24::WiiNumber());
    return {{"format", kCardFormat},
            {"version", kCardVersion},
            {"name", ToUtf8(player->mPID.player.mName, ARRAY_SIZE(player->mPID.player.mName))},
            {"town", ToUtf8(player->mPID.land.mName, ARRAY_SIZE(player->mPID.land.mName))},
            {"greeting", ToUtf8(player->mFriends.mName, ARRAY_SIZE(player->mFriends.mName))},
            {"wiiNumber", wiiNumber},
            {"profileId", ProfileIdOf(player)},
            {"pid", Hex(pid, sizeof(pid))}};
}

fs::path CardDir() {
    return fs::u8path(UserPath()) / "friend_cards";
}

std::string FileNameOf(const json& card) {
    std::string name;
    for (const char c : card.value("name", std::string()) + "_" + card.value("town", std::string())) {
        name.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    }
    return name + "_" + card.value("wiiNumber", std::string()) + ".json";
}

void ExportCard(int slot) {
    const json card = MakeCard(slot);
    std::error_code ec;
    fs::create_directories(CardDir(), ec);
    const fs::path path = CardDir() / fs::u8path(FileNameOf(card));
    std::ofstream(path, std::ios::binary) << card.dump(2);
    ImGui::SetClipboardText((kCodePrefix + Base64(card.dump())).c_str());
    sStatus = "Exported " + card.value("name", std::string()) + "'s card to friend_cards/" + FileNameOf(card) +
              " and copied its code to the clipboard.";
}

// The card's fields, checked; empty with the reason in sStatus
struct Card {
    std::string name;
    unsigned long long wiiNumber = 0;
    int profileId = 0;
    dPersonalID_c pid;
    wchar16_t greeting[0x21];
};

std::optional<Card> ReadCard(const json& j, int slot) {
    if (!j.is_object() || j.value("format", std::string()) != kCardFormat || j.value("version", 0) != kCardVersion) {
        sStatus = "That is not a suitcase friend card.";
        return std::nullopt;
    }
    Card card;
    card.name = j.value("name", std::string());
    card.wiiNumber = std::strtoull(j.value("wiiNumber", std::string("0")).c_str(), nullptr, 10);
    card.profileId = j.value("profileId", 0);
    u8 pid[sizeof(dPersonalID_c)];
    if (card.wiiNumber == 0 || card.profileId <= 0 || !Unhex(j.value("pid", std::string()), pid, sizeof(pid))) {
        sStatus = "The card is incomplete.";
        return std::nullopt;
    }
    cf::save::Decode(SavePidOffset(slot), pid, &card.pid, sizeof(pid));
    if (!card.pid.isValid()) {
        sStatus = "The card's player is not valid.";
        return std::nullopt;
    }
    FromUtf8(j.value("greeting", std::string()), card.greeting, ARRAY_SIZE(card.greeting));
    return card;
}

void ImportCard(const json& j, int slot) {
    dPrivateData_c* player = dPlayerMgr::Mgr_c::getPlayer(slot);
    const std::optional<Card> card = ReadCard(j, slot);
    if (!card) {
        return;
    }
    if (card->pid == player->mPID) {
        sStatus = "That is this player's own card.";
        return;
    }
    dFriendList_c* list = &player->mFriends;
    int idx = -1;
    for (int i = 0; i < FRIEND_NUM && idx < 0; i++) {
        if (DWC_IsValidFriendData(&list->mKeys[i]) &&
            (list->mFriends[i].mPID == card->pid || DWC_GetGsProfileId(&list->mUserData, &list->mKeys[i]) == card->profileId)) {
            idx = i;
        }
    }
    for (int i = 0; i < FRIEND_NUM && idx < 0; i++) {
        if (!DWC_IsValidFriendData(&list->mKeys[i])) {
            idx = i;
        }
    }
    if (idx < 0) {
        sStatus = "The friend roster is full.";
        return;
    }
    // The friend's DWC data in the save's byte order (DWCFriendData stays as bytes: dwc_account.cpp)
    u8* key = reinterpret_cast<u8*>(&list->mKeys[idx]);
    std::memset(key, 0, sizeof(list->mKeys[idx]));
    for (int b = 0; b < 4; b++) {
        key[b] = static_cast<u8>(kProfileFriendFlags >> (24 - b * 8));
        key[4 + b] = static_cast<u8>(static_cast<u32>(card->profileId) >> (24 - b * 8));
    }
    dFriend_c* fr = &list->mFriends[idx];
    fr->clear();
    fr->mKey = card->wiiNumber;
    fr->mPID = card->pid;
    std::memcpy(fr->mMessage, card->greeting, sizeof(fr->mMessage));
    player->updateChecksum();
    sStatus = "Added " + card->name + " to Player " + std::to_string(slot + 1) + "'s friend roster (slot " +
              std::to_string(idx + 1) + "). Save the game to keep it.";
}

std::optional<json> ParseCode(const std::string& code) {
    const std::size_t at = code.find(kCodePrefix);
    if (at == std::string::npos) {
        sStatus = "No card code (it starts with " + std::string(kCodePrefix) + ").";
        return std::nullopt;
    }
    json j = json::parse(Unbase64(code.substr(at + std::strlen(kCodePrefix))), nullptr, false);
    if (j.is_discarded()) {
        sStatus = "The card code is damaged.";
        return std::nullopt;
    }
    return j;
}

bool PlayerExists(int slot) {
    const dPrivateData_c* player = dPlayerMgr::Mgr_c::getPlayer(slot);
    return player != nullptr && player->mPID.isValid();
}

std::string PlayerLabel(int slot) {
    const dPrivateData_c* player = dPlayerMgr::Mgr_c::getPlayer(slot);
    return "Player " + std::to_string(slot + 1) +
           (PlayerExists(slot) ? " (" + ToUtf8(player->mPID.player.mName, ARRAY_SIZE(player->mPID.player.mName)) + ")" : "");
}

} // namespace

void DrawFriendCardWindow(bool* open) {
    if (!*open) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(620, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Friend cards", open)) {
        ImGui::End();
        return;
    }
    static int sSlot = 0;
    if (ImGui::BeginCombo("Player", PlayerLabel(sSlot).c_str())) {
        for (int slot = 0; slot < PLAYER_NUM; slot++) {
            if (ImGui::Selectable(PlayerLabel(slot).c_str(), slot == sSlot, PlayerExists(slot) ? 0 : ImGuiSelectableFlags_Disabled)) {
                sSlot = slot;
            }
        }
        ImGui::EndCombo();
    }
    const bool exists = PlayerExists(sSlot);
    ImGui::BeginDisabled(!exists);
    if (ImGui::Button("Export this player's card")) {
        ExportCard(sSlot);
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Import a card into this player's friend roster:");
    ImGui::InputTextMultiline("##code", sCodeInput, sizeof(sCodeInput), ImVec2(-1, 60));
    if (ImGui::Button("Paste")) {
        const char* clip = ImGui::GetClipboardText();
        std::snprintf(sCodeInput, sizeof(sCodeInput), "%s", clip != nullptr ? clip : "");
    }
    ImGui::SameLine();
    if (ImGui::Button("Import code")) {
        if (const std::optional<json> card = ParseCode(sCodeInput)) {
            ImportCard(*card, sSlot);
        }
    }
    ImGui::TextUnformatted("Or a card file from friend_cards:");
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(CardDir(), ec)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        const auto name = entry.path().filename().u8string();
        if (ImGui::SmallButton(("Import##" + std::string(name.begin(), name.end())).c_str())) {
            std::ifstream in(entry.path(), std::ios::binary);
            const json card = json::parse(std::string(std::istreambuf_iterator<char>(in), {}), nullptr, false);
            ImportCard(card, sSlot);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(std::string(name.begin(), name.end()).c_str());
    }
    ImGui::EndDisabled();
    if (!sStatus.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", sStatus.c_str());
    }
    ImGui::End();
}

} // namespace debug
} // namespace cf
