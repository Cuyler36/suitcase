// The online services (cf/online.h)
#include <cf/online.h>

#include <atomic>
#include <optional>

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

std::string MapHost(const std::string& host) {
    const ServiceInfo& service = Info(Current());
    if (service.wfcDomain == nullptr || service.wfcDomain[0] == '\0') {
        return host;
    }
    auto replaceSuffix = [&](const char* suffix, const std::string& replacement) -> std::optional<std::string> {
        const std::string tail = suffix;
        if (host.size() >= tail.size() && host.compare(host.size() - tail.size(), tail.size(), tail) == 0 &&
            (host.size() == tail.size() || host[host.size() - tail.size() - 1] == '.')) {
            return host.substr(0, host.size() - tail.size()) + replacement;
        }
        return std::nullopt;
    };
    if (auto mapped = replaceSuffix("nintendowifi.net", service.wfcDomain)) {
        return *mapped;
    }
    if (auto mapped = replaceSuffix("gamespy.com", std::string("gs.") + service.wfcDomain)) {
        return *mapped;
    }
    return host;
}

Service Current() {
    return sCurrent.load();
}

void SetCurrent(Service service) {
    sCurrent = service;
}

} // namespace online
} // namespace cf
