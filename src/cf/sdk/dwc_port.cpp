// The port's side of DWC (lib/dwc, built for the host): whether players get the port's own profile IDs
// (the TARGET_PC hooks in lib/dwc/account/dwc_account.c), the console's DWC user ID, and friend codes
// for the host code.
#include <cf/wfc.h>

#include "console_keys.h"
#include "nand_fs.h"

#include <borealis/log_c.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

extern "C" std::uint64_t DWC_Acc_CreateFriendKey(int gsProfileId, std::uint32_t gameCode, int method); // dwc_account.c

namespace {

constexpr int kFriendKeyGeneratorMd5 = 1; // DWCi_FRIENDKEY_GENERATOR_MD5

} // namespace

// Port-only without the player's console keys: no WiiLink login, so players without a profile get the
// port's own (pc_port_wc24.md)
extern "C" int DWCi_Port_IsPortOnly(void) {
    static std::once_flag once;
    static bool portOnly = true;
    std::call_once(once, [] {
        std::string error;
        portOnly = !cf::sdk::LoadConsoleKeys(&error).has_value();
    });
    return portOnly ? 1 : 0;
}

std::uint64_t cf::wfc::FriendCodeOf(int profileId, std::uint32_t gameCode) {
    return DWC_Acc_CreateFriendKey(profileId, gameCode, kFriendKeyGeneratorMd5);
}

namespace cf::sdk {

// The console's DWC user ID: its NAND's /shared2/DWC_AUTHDATA (the ID big-endian, then 24 bytes of
// padding), which every DWC game on the Wii shares, put in the user directory's console/ folder beside
// the console keys. Optional: without it, the port's NAND is a console that has never been online, and
// DWC has WiiLink create its account. Copied into the port's NAND when it differs; only with the
// console's keys.
void ImportConsoleAuthData() {
    std::string error;
    if (!LoadConsoleKeys(&error)) {
        return;
    }
    auto read = [](const std::filesystem::path& path, std::array<std::uint8_t, 32>* data) {
        std::FILE* file = OpenHostFile(path, "rb");
        if (file == nullptr) {
            return false;
        }
        const bool ok = std::fread(data->data(), 1, data->size(), file) == data->size() && std::fgetc(file) == EOF;
        std::fclose(file);
        return ok;
    };
    std::array<std::uint8_t, 32> console{};
    const std::filesystem::path source = UserDirPath() / "console" / "DWC_AUTHDATA";
    if (!read(source, &console)) {
        if (std::FILE* file = OpenHostFile(source, "rb")) {
            std::fclose(file);
            borealis_log_printf(BOREALIS_LOG_WARNING, "cf::dwc", "console/DWC_AUTHDATA is not 32 bytes; ignored");
        }
        return;
    }
    std::uint64_t userId = 0;
    for (int i = 0; i < 8; i++) {
        userId = (userId << 8) | console[i];
    }
    if (userId == 0) {
        borealis_log_printf(BOREALIS_LOG_WARNING, "cf::dwc", "console/DWC_AUTHDATA holds no user ID; ignored");
        return;
    }
    const std::filesystem::path target = NandHostPath("/shared2/DWC_AUTHDATA");
    std::array<std::uint8_t, 32> current{};
    if (read(target, &current) && current == console) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    std::FILE* file = OpenHostFile(target, "wb");
    if (file == nullptr || std::fwrite(console.data(), 1, console.size(), file) != console.size()) {
        if (file != nullptr) {
            std::fclose(file);
        }
        borealis_log_printf(BOREALIS_LOG_ERROR, "cf::dwc", "cannot write /shared2/DWC_AUTHDATA");
        return;
    }
    std::fclose(file);
    borealis_log_printf(BOREALIS_LOG_INFO, "cf::dwc", "imported console/DWC_AUTHDATA: DWC logs in as the console");
}

} // namespace cf::sdk
