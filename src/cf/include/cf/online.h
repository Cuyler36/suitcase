// The online service the port uses in place of Nintendo's (WiiConnect24 and Wi-Fi Connection): every
// service speaks Nintendo's original protocols and formats, so it only changes where requests go
// (src/cf/sdk/online_service.cpp; docs/wc24/README.md). Chosen by the online.service setting.
#pragma once

namespace cf {
namespace online {

enum class Service {
    WiiLink,
};

struct ServiceInfo {
    const char* name;      // shown in the menu
    const char* dnsServer; // resolves Nintendo's host names to the service, as the console's DNS setting
    const char* wfcDomain; // Wi-Fi Connection: nas.<domain>, gpcm.gs.<domain> (WiiLink's patch's names)
    // Files the service does not have come from Nintendo's own servers where they are still up
    bool nintendoFallback;
    // WiiConnect24 mail: the CGI directory (account.cgi, check.cgi, receive.cgi, delete.cgi, send.cgi; the
    // URLs a Wii's nwc24msg.cfg holds) and the account domain of its addresses (w<Wii number>@<domain>)
    const char* mailUrl;
    const char* mailDomain;
};

const ServiceInfo& Info(Service service);

// The service in use (the setting; any thread)
Service Current();
void SetCurrent(Service service);

} // namespace online
} // namespace cf
