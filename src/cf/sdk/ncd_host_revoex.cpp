// The network configuration DWC asks the Wii for (RevoEX NCD and NET), on the host: the link the host's
// network is, reported as a wired connection (DWC only sends a Wi-Fi setup method for wireless ones), and
// a MAC address (DWC seeds its random numbers with it and sends it to the NAS at login: the player's
// console's, from their keys, else a fixed one).
#include <revolution/ncd.h>
#include <revolution/net.h>

#include "console_keys.h"

#include <cstring>
#include <mutex>
#include <string>

extern "C" {

NCDErr NCDGetCurrentIfConfig(NCDIfConfig* ifConfig) {
    std::memset(ifConfig, 0, sizeof(*ifConfig));
    ifConfig->selectedMedia = 0; // wired
    return NCD_RESULT_SUCCESS;
}

int NETGetWirelessMacAddress(void* data) {
    static std::once_flag once;
    static unsigned char mac[6] = {0x00, 0x17, 0xAB, 0x00, 0x00, 0x01};
    std::call_once(once, [] {
        std::string error;
        if (const auto keys = cf::sdk::LoadConsoleKeys(&error)) {
            std::memcpy(mac, keys->macAddress.data(), sizeof(mac));
        }
    });
    std::memcpy(data, mac, sizeof(mac));
    return 0;
}

} // extern "C"
