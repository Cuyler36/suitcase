// WiiLink's Wi-Fi Connection (WFC) from the port (src/cf/sdk/wfc_login.cpp; docs/wc24/README.md): the
// login a Wii makes through DWC, signed with the player's own console keys (<user dir>/console).
#pragma once

#include <cstdint>
#include <string>

namespace cf {
namespace wfc {

struct LoginResult {
    bool ok = false;
    std::string error;  // why it failed (ok false)
    std::string log;    // the exchange, one step per line (no keys or tokens)
    int profileId = 0;  // the account's profile ID
    std::uint64_t friendCode = 0;
};

// Logs in to WiiLink WFC as the player whose DWC record is userData (DWCUserData, 0x40 bytes in the
// save's byte order), then logs out: the account login the game's DWC makes (NAS login, then the
// GameSpy presence server with WiiLink's console signature). Blocking; changes nothing locally.
LoginResult TestLogin(const std::uint8_t userData[0x40]);

// A friend code (12 digits) of a profile ID for a DWC game code ('RUUJ' for City Folk)
std::uint64_t FriendCodeOf(int profileId, std::uint32_t gameCode);

} // namespace wfc
} // namespace cf
