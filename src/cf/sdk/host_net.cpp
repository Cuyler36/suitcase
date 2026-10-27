// Host networking for the online layers (host_net.h)
#include "host_net.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SocketHandle = int;
#endif

namespace cf {
namespace sdk {
namespace net {

namespace {

constexpr std::intptr_t kNoSocket = -1;

SocketHandle Handle(std::intptr_t h) {
    return static_cast<SocketHandle>(h);
}

void CloseHandle(std::intptr_t h) {
#ifdef _WIN32
    closesocket(Handle(h));
#else
    close(Handle(h));
#endif
}

void SetTimeouts(SocketHandle h, int seconds) {
#ifdef _WIN32
    DWORD timeout = seconds * 1000;
    setsockopt(h, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(h, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{seconds, 0};
    setsockopt(h, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(h, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

bool IsValid(SocketHandle h) {
#ifdef _WIN32
    return h != INVALID_SOCKET;
#else
    return h >= 0;
#endif
}

} // namespace

bool Init() {
#ifdef _WIN32
    static const bool ok = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
#else
    return true;
#endif
}

Socket::~Socket() {
    if (mHandle != kNoSocket) {
        CloseHandle(mHandle);
    }
}

bool Socket::Connect(const std::string& host, std::uint16_t port) {
    if (!Init()) {
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list) != 0) {
        return false;
    }
    for (addrinfo* a = list; a != nullptr; a = a->ai_next) {
        const SocketHandle h = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (!IsValid(h)) {
            continue;
        }
        SetTimeouts(h, 15);
        if (connect(h, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) {
            mHandle = static_cast<std::intptr_t>(h);
            freeaddrinfo(list);
            return true;
        }
        CloseHandle(static_cast<std::intptr_t>(h));
    }
    freeaddrinfo(list);
    return false;
}

bool Socket::SendAll(const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int n = send(Handle(mHandle), data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool Socket::ReceiveUntil(std::string* data, const char* marker) {
    char buf[4096];
    for (;;) {
        if (marker[0] != '\0' && data->find(marker) != std::string::npos) {
            return true;
        }
        const int n = recv(Handle(mHandle), buf, sizeof(buf), 0);
        if (n <= 0) {
            return marker[0] == '\0' && n == 0;
        }
        data->append(buf, static_cast<std::size_t>(n));
    }
}

// A DNS query (RFC 1035) for the host's A record, over UDP
std::string ResolveWith(const std::string& dnsServer, const std::string& host) {
    if (!Init()) {
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

    const SocketHandle h = socket(AF_INET, SOCK_DGRAM, 0);
    if (!IsValid(h)) {
        return {};
    }
    SetTimeouts(h, 5);
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(53);
    if (inet_pton(AF_INET, dnsServer.c_str(), &server.sin_addr) != 1) {
        CloseHandle(static_cast<std::intptr_t>(h));
        return {};
    }
    std::uint8_t reply[512];
    int n = -1;
    for (int attempt = 0; attempt < 2 && n < 12; attempt++) {
        sendto(h, reinterpret_cast<const char*>(query.data()), static_cast<int>(query.size()), 0,
               reinterpret_cast<const sockaddr*>(&server), sizeof(server));
        n = recv(h, reinterpret_cast<char*>(reply), sizeof(reply), 0);
    }
    CloseHandle(static_cast<std::intptr_t>(h));
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
    const std::string headers = raw.substr(0, headerEnd);
    response.status = std::atoi(raw.c_str() + raw.find(' ') + 1);
    std::string content = raw.substr(headerEnd + 4);
    std::string lower = headers;
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
