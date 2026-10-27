// The player's own console identity for WiiLink's Wi-Fi Connection: the console's NG key and device
// certificate (from a BootMii / xyzzy dump the player supplies; the port ships none), kept in the user
// directory's console/ folder as keys.txt and device.cert. With them the port signs as the console's
// IOS does (ES_Sign: an AP certificate signed by the NG key, the data signed by the AP key), for the
// login signature WiiLink's server checks (wfc-server gpcm/login.go verifySignature).
#pragma once

#include "wii_ecc.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cf {
namespace sdk {

struct ConsoleKeys {
    std::uint32_t ngId = 0;                     // the device ID (keys.txt console_id)
    ecc::PrivateKey ngPrivateKey{};             // keys.txt ecc_private_key
    std::array<std::uint8_t, 6> macAddress{};   // keys.txt mac_address
    std::array<std::uint8_t, 0x180> certificate{}; // device.cert: the NG certificate, signed by Nintendo
};

// The console keys in <user dir>/console, checked (the certificate is the key's and the ID's); empty with
// the reason in *error when there are none or they do not check out
std::optional<ConsoleKeys> LoadConsoleKeys(std::string* error);

// The WiiLink login signature (wl:sig, before encoding) of a login's auth token for a title:
// the NG ID, the NG certificate's fields and Nintendo's signature of it, then a new AP certificate
// signed by the NG key and the token's signature by the AP key (0x144 bytes)
std::vector<std::uint8_t> MakeLoginSignature(const ConsoleKeys& keys, const std::string& authToken, std::uint64_t titleId);

// DWC's Base64 ('.', '-' and '*' padding), as the login's fields use
std::string EncodeDwcBase64(const std::vector<std::uint8_t>& data);

} // namespace sdk
} // namespace cf
