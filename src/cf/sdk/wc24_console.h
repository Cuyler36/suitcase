// This console's network identity (made on first use, kept in the NAND's /shared2/wc24/identity.json;
// nwc24_revoex.cpp): what the Wii keeps in its system files.
#pragma once

#include <cstdint>

namespace cf {
namespace sdk {
namespace wc24 {

// The console's Wii number (NWC24GetMyUserId)
std::uint64_t ConsoleWiiNumber();

// The console's DWC user ID (43 bits): every player's DWC pseudo login ID carries it, as the Wii's
// DWC takes it from the console's Wi-Fi Connection save
std::uint64_t ConsoleDwcUserId();

} // namespace wc24
} // namespace sdk
} // namespace cf
