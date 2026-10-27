// Blocking host networking for the online layers (WiiConnect24 downloads and mail, WiiLink WFC):
// TCP sockets with timeouts, DNS A lookups against a chosen server, and HTTP/1.1 GET and POST.
// Call from worker threads, never the game's.
#pragma once

#include <cstdint>
#include <string>

namespace cf {
namespace sdk {
namespace net {

bool Init();

class Socket {
public:
    Socket() = default;
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // host is a name or an IPv4 address; the system resolves names
    bool Connect(const std::string& host, std::uint16_t port);
    bool SendAll(const std::string& data);
    // Appends what arrives until the data contains marker, or until the peer closes (empty marker)
    bool ReceiveUntil(std::string* data, const char* marker);

private:
    std::intptr_t mHandle = -1;
};

// The IPv4 address of a host name from a DNS server (dotted), empty when there is none
std::string ResolveWith(const std::string& dnsServer, const std::string& host);

struct HttpResponse {
    int status = 0; // 0: no response
    std::string body;
    std::string error;
};

// An http:// request. address is where to connect (empty: resolve the URL's host normally); the
// Host header is always the URL's. contentType/body make it a POST.
HttpResponse HttpRequest(const std::string& url, const std::string& address, const std::string& contentType = "",
                         const std::string& body = "", const std::string& extraHeaders = "");

} // namespace net
} // namespace sdk
} // namespace cf
