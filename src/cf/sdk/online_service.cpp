// The online services (cf/online.h)
#include <cf/online.h>

#include <atomic>

namespace cf {
namespace online {

namespace {

// WiiLink: its DNS (wiilink.ca/guide/dns) answers for Nintendo's WiiConnect24 hosts; its Wi-Fi
// Connection is under wiilink.ca (wfc-patcher-wii WWFC_DOMAIN); its mail is where its Mail-Patcher points a
// Wii's nwc24msg.cfg (mtw.rc24.xyz, addresses @rc24.xyz)
const ServiceInfo kWiiLink = {"WiiLink", "167.235.229.36", "wiilink.ca", true, "http://mtw.rc24.xyz/cgi-bin/", "rc24.xyz"};

std::atomic<Service> sCurrent{Service::WiiLink};

} // namespace

const ServiceInfo& Info(Service service) {
    switch (service) {
    case Service::WiiLink:
    default:
        return kWiiLink;
    }
}

Service Current() {
    return sCurrent.load();
}

void SetCurrent(Service service) {
    sCurrent = service;
}

} // namespace online
} // namespace cf
