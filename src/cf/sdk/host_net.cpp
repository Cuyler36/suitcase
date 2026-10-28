// Host networking for the online layers (host_net.h), on borealis::socket
#include "host_net.h"

#include <borealis/socket.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace cf {
namespace sdk {
namespace net {

namespace bs = borealis::socket;

namespace {

constexpr auto kTimeout = std::chrono::seconds(15);

// Waits for the socket to be ready for events, up to the timeout
bool WaitFor(const bs::Socket& socket, std::uint32_t events, std::chrono::milliseconds timeout) {
    bs::PollEntry entry{&socket, events};
    return bs::poll(std::span(&entry, 1), timeout) > 0 && (entry.revents & (events | bs::PollError | bs::PollHangup));
}

} // namespace

bool Init() {
    return bs::available();
}

struct Socket::Impl {
    bs::Socket socket;
};

Socket::Socket() : mImpl(std::make_unique<Impl>()) {}

Socket::~Socket() = default;

bool Socket::Connect(const std::string& host, std::uint16_t port) {
    std::vector<bs::Address> addresses;
    if (auto numeric = bs::parse_address(host)) {
        addresses.push_back(*numeric);
    } else {
        addresses = bs::resolve(host);
    }
    for (bs::Address address : addresses) {
        address.port = port;
        bs::Socket socket = bs::Socket::open(address.family, bs::Type::Stream);
        if (!socket.valid()) {
            continue;
        }
        socket.set_nonblocking(true);
        bs::Error error = socket.connect(address);
        if (error == bs::Error::InProgress &&
            WaitFor(socket, bs::PollWrite, std::chrono::duration_cast<std::chrono::milliseconds>(kTimeout))) {
            error = socket.pending_error();
        }
        if (error == bs::Error::None) {
            socket.set_nonblocking(false);
            mImpl->socket = std::move(socket);
            return true;
        }
    }
    return false;
}

bool Socket::SendAll(const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        bs::Error error;
        const std::ptrdiff_t n = mImpl->socket.send(
            std::as_bytes(std::span(data.data() + sent, data.size() - sent)), &error);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool Socket::ReceiveUntil(std::string* data, const char* marker) {
    std::byte buf[4096];
    const auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(kTimeout);
    for (;;) {
        if (marker[0] != '\0' && data->find(marker) != std::string::npos) {
            return true;
        }
        if (!WaitFor(mImpl->socket, bs::PollRead, timeout)) {
            return false;
        }
        bs::Error error;
        const std::ptrdiff_t n = mImpl->socket.receive(buf, &error);
        if (n <= 0) {
            return marker[0] == '\0' && n == 0;
        }
        data->append(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(n));
    }
}

// A DNS query (RFC 1035) for the host's A record, over UDP
std::string ResolveWith(const std::string& dnsServer, const std::string& host) {
    const auto server = bs::parse_address(dnsServer, 53);
    if (!server) {
        return {};
    }
    std::random_device random;
    const std::uint16_t id = static_cast<std::uint16_t>(random());
    std::vector<std::uint8_t> query = {static_cast<std::uint8_t>(id >> 8), static_cast<std::uint8_t>(id), 0x01, 0x00,
                                       0, 1, 0, 0, 0, 0, 0, 0}; // recursion desired, one question
    std::size_t start = 0;
    while (start <= host.size()) {
        std::size_t dot = host.find('.', start);
        if (dot == std::string::npos) {
            dot = host.size();
        }
        const std::size_t length = dot - start;
        if (length == 0 || length > 63) {
            return {};
        }
        query.push_back(static_cast<std::uint8_t>(length));
        query.insert(query.end(), host.begin() + start, host.begin() + dot);
        start = dot + 1;
    }
    query.insert(query.end(), {0, 0, 1, 0, 1}); // end of name, type A, class IN

    bs::Socket socket = bs::Socket::open(bs::Family::IPv4, bs::Type::Datagram);
    if (!socket.valid()) {
        return {};
    }
    std::uint8_t reply[512];
    std::ptrdiff_t n = -1;
    for (int attempt = 0; attempt < 2 && n < 12; attempt++) {
        socket.send_to(std::as_bytes(std::span(query)), *server, nullptr);
        if (WaitFor(socket, bs::PollRead, std::chrono::seconds(5))) {
            n = socket.receive_from(std::as_writable_bytes(std::span(reply)), nullptr, nullptr);
        }
    }
    if (n < 12 || reply[0] != query[0] || reply[1] != query[1] || (reply[3] & 0x0F) != 0) {
        return {};
    }
    const int questions = (reply[4] << 8) | reply[5];
    const int answers = (reply[6] << 8) | reply[7];
    std::size_t pos = 12;
    auto skipName = [&] {
        while (pos < static_cast<std::size_t>(n)) {
            const std::uint8_t length = reply[pos];
            if (length == 0) {
                pos++;
                return;
            }
            if ((length & 0xC0) == 0xC0) {
                pos += 2;
                return;
            }
            pos += 1 + length;
        }
    };
    for (int i = 0; i < questions; i++) {
        skipName();
        pos += 4;
    }
    for (int i = 0; i < answers && pos + 10 <= static_cast<std::size_t>(n); i++) {
        skipName();
        const int type = (reply[pos] << 8) | reply[pos + 1];
        const int length = (reply[pos + 8] << 8) | reply[pos + 9];
        pos += 10;
        if (type == 1 && length == 4 && pos + 4 <= static_cast<std::size_t>(n)) {
            char address[16];
            std::snprintf(address, sizeof(address), "%u.%u.%u.%u", reply[pos], reply[pos + 1], reply[pos + 2],
                          reply[pos + 3]);
            return address;
        }
        pos += length; // a CNAME: its A record follows
    }
    return {};
}

HttpResponse HttpRequest(const std::string& url, const std::string& address, const std::string& contentType,
                         const std::string& body, const std::string& extraHeaders) {
    HttpResponse response;
    const std::string scheme = "http://";
    if (url.compare(0, scheme.size(), scheme) != 0) {
        response.error = "not an http:// URL";
        return response;
    }
    const std::size_t pathStart = url.find('/', scheme.size());
    std::string hostPort = url.substr(scheme.size(), pathStart - scheme.size());
    std::string path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
    // A request line ends the path at a space, as the console's HTTP does with City Folk's URLs
    path = path.substr(0, path.find(' '));
    std::uint16_t port = 80;
    std::string host = hostPort;
    if (const std::size_t colon = hostPort.find(':'); colon != std::string::npos) {
        host = hostPort.substr(0, colon);
        port = static_cast<std::uint16_t>(std::stoi(hostPort.substr(colon + 1)));
    }

    Socket socket;
    if (!socket.Connect(address.empty() ? host : address, port)) {
        response.error = "cannot connect to " + host;
        return response;
    }
    const bool post = !contentType.empty();
    std::string request = std::string(post ? "POST " : "GET ") + path + " HTTP/1.1\r\nHost: " + hostPort +
                          "\r\nUser-Agent: RVL SDK/1.0\r\n" + extraHeaders;
    if (post) {
        request += "Content-Type: " + contentType + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    }
    request += "Connection: close\r\n\r\n" + body;
    std::string raw;
    if (!socket.SendAll(request) || !socket.ReceiveUntil(&raw, "")) {
        response.error = "no response from " + host;
        return response;
    }
    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos || raw.compare(0, 5, "HTTP/") != 0) {
        response.error = "not an HTTP response";
        return response;
    }
    response.headers = raw.substr(0, headerEnd + 2);
    response.status = std::atoi(raw.c_str() + raw.find(' ') + 1);
    std::string content = raw.substr(headerEnd + 4);
    std::string lower = response.headers;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower.find("transfer-encoding: chunked") != std::string::npos) {
        std::string decoded;
        std::size_t pos = 0;
        while (pos < content.size()) {
            const std::size_t lineEnd = content.find("\r\n", pos);
            if (lineEnd == std::string::npos) {
                break;
            }
            const std::size_t size = std::strtoul(content.substr(pos, lineEnd - pos).c_str(), nullptr, 16);
            if (size == 0) {
                break;
            }
            decoded.append(content, lineEnd + 2, size);
            pos = lineEnd + 2 + size + 2;
        }
        content = std::move(decoded);
    }
    response.body = std::move(content);
    return response;
}

} // namespace net
} // namespace sdk
} // namespace cf
