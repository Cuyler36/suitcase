// The RevoEX SO library (revolution/so.h: the Wii's BSD-style sockets, which DWC and the GameSpy SDK use)
// on the host's sockets through borealis::socket. The calls keep SO's conventions:
//   - results are counts or 0, errors negative SO_E* codes (SO_EAGAIN for would-block);
//   - SOSockAddrIn is {len, family, port, addr} with the port and address in network byte order, and
//     every IPv4 address that crosses the API (SOInAddr, SOGetHostID, IPAddrEntry, host entries) is a
//     network-order value, as the Wii's are. SOHtoNl and the others swap on the little-endian host, so
//     code that fills the structures through them is right on both;
//   - receive flags: 1 out-of-band, 2 peek, 4 don't wait (GameSpy always passes 4);
//   - SOFcntl F_GETFL / F_SETFL with O_NONBLOCK 4; SOPoll timeouts in OSTime ticks.
// Host names go through the online service (cf::online::MapHost), as WiiLink's patch renames them.
// IPv4 only, like the Wii.
#include <revolution/so.h>

#include <borealis/log_c.h>
#include <borealis/socket.hpp>
#include <cf/online.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace bs = borealis::socket;

namespace {

constexpr int kRecvFlagPeek = 2;
constexpr int kRecvFlagDontWait = 4;
constexpr int kAddrInfoFlagNumericHost = 4; // SO's AI_NUMERICHOST
constexpr long long kTicksPerMs = OS_BUS_CLOCK / 4 / 1000;

struct Entry {
    bs::Socket socket;
    bs::Type type = bs::Type::Stream;
    bool nonblocking = false;
    bool connecting = false; // a non-blocking connect started and not yet seen finished
};

std::mutex sMutex;
std::vector<std::unique_ptr<Entry>> sSockets; // index: the SO descriptor

// CF_NET_LOG=1: every socket opened, bound and closed (failures are always logged)
bool Logging() {
    static const bool sOn = [] {
        const char* v = std::getenv("CF_NET_LOG");
        return v != nullptr && v[0] != 0 && v[0] != '0';
    }();
    return sOn;
}

std::size_t OpenCount() {
    std::size_t n = 0;
    for (const auto& entry : sSockets) {
        n += entry != nullptr;
    }
    return n;
}

int ToSoError(bs::Error error) {
    switch (error) {
    case bs::Error::None:
        return SO_SUCCESS;
    case bs::Error::WouldBlock:
        return SO_EAGAIN;
    case bs::Error::InProgress:
        return SO_EINPROGRESS;
    case bs::Error::Interrupted:
        return SO_EINTR;
    case bs::Error::Invalid:
        return SO_EINVAL;
    case bs::Error::Unsupported:
        return SO_ENETDOWN;
    case bs::Error::Resolve:
        return SO_EHOSTUNREACH;
    case bs::Error::Refused:
        return SO_ECONNREFUSED;
    case bs::Error::Unreachable:
        return SO_ENETUNREACH;
    case bs::Error::Reset:
        return SO_ECONNRESET;
    case bs::Error::AddressInUse:
        return SO_EADDRINUSE;
    case bs::Error::Permission:
        return SO_EACCES;
    case bs::Error::Timeout:
        return SO_ETIMEDOUT;
    default:
        return SO_EIO;
    }
}

Entry* Find(int s) {
    return s >= 0 && static_cast<std::size_t>(s) < sSockets.size() ? sSockets[s].get() : nullptr;
}

// Network-order u16/u32 (as stored in the SO structures) and host values
std::uint16_t NetToHost16(std::uint16_t net) {
    const auto* b = reinterpret_cast<const std::uint8_t*>(&net);
    return static_cast<std::uint16_t>((b[0] << 8) | b[1]);
}

std::uint16_t HostToNet16(std::uint16_t host) {
    std::uint16_t net;
    auto* b = reinterpret_cast<std::uint8_t*>(&net);
    b[0] = static_cast<std::uint8_t>(host >> 8);
    b[1] = static_cast<std::uint8_t>(host);
    return net;
}

std::uint32_t AddressToNet(const bs::Address& address) {
    std::uint32_t net;
    std::memcpy(&net, address.bytes.data(), 4);
    return net;
}

bs::Address FromSockAddr(const void* sockAddr) {
    const auto* in = static_cast<const SOSockAddrIn*>(sockAddr);
    bs::Address address;
    std::memcpy(address.bytes.data(), &in->addr.addr, 4);
    address.port = NetToHost16(in->port);
    return address;
}

void ToSockAddr(const bs::Address& address, void* sockAddr) {
    auto* in = static_cast<SOSockAddrIn*>(sockAddr);
    in->len = sizeof(SOSockAddrIn);
    in->family = SO_PF_INET;
    in->port = HostToNet16(address.port);
    std::memcpy(&in->addr.addr, address.bytes.data(), 4);
}

// The connect result once a non-blocking connect is seen finished
int FinishConnect(Entry* e) {
    bs::PollEntry poll{&e->socket, bs::PollWrite};
    bs::poll(std::span(&poll, 1), std::chrono::milliseconds(0));
    if ((poll.revents & (bs::PollWrite | bs::PollError | bs::PollHangup)) == 0) {
        return SO_EALREADY;
    }
    e->connecting = false;
    const bs::Error error = e->socket.pending_error();
    return error == bs::Error::None ? SO_EISCONN : ToSoError(error);
}

// A don't-wait call on a blocking socket: whether it would wait
bool WouldWait(Entry* e, std::uint32_t events) {
    bs::PollEntry poll{&e->socket, events};
    return bs::poll(std::span(&poll, 1), std::chrono::milliseconds(0)) == 0;
}

std::string MapHost(const char* name) {
    return cf::online::MapHost(name);
}

} // namespace

extern "C" {

// ---------------------------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------------------------
int SOInit(const SOLibraryConfig* config) {
    (void)config;
    return bs::available() ? SO_SUCCESS : SO_ENETDOWN;
}

int SOFinish(void) {
    return SO_SUCCESS;
}

int SOStartup(void) {
    return bs::available() ? SO_SUCCESS : SO_ENETDOWN;
}

int SOStartupEx(int timeout) {
    (void)timeout;
    return SOStartup();
}

int SOCleanup(void) {
    return SO_SUCCESS;
}

// ---------------------------------------------------------------------------------------------
// Byte order
// ---------------------------------------------------------------------------------------------
u32 SOHtoNl(u32 hostlong) {
    std::uint32_t net;
    auto* b = reinterpret_cast<std::uint8_t*>(&net);
    b[0] = static_cast<std::uint8_t>(hostlong >> 24);
    b[1] = static_cast<std::uint8_t>(hostlong >> 16);
    b[2] = static_cast<std::uint8_t>(hostlong >> 8);
    b[3] = static_cast<std::uint8_t>(hostlong);
    return net;
}

u32 SONtoHl(u32 netlong) {
    const auto* b = reinterpret_cast<const std::uint8_t*>(&netlong);
    return (u32(b[0]) << 24) | (u32(b[1]) << 16) | (u32(b[2]) << 8) | b[3];
}

u16 SOHtoNs(u16 hostshort) {
    return HostToNet16(hostshort);
}

u16 SONtoHs(u16 netshort) {
    return NetToHost16(netshort);
}

// ---------------------------------------------------------------------------------------------
// Addresses
// ---------------------------------------------------------------------------------------------
int SOInetAtoN(const char* cp, SOInAddr* inp) {
    const auto address = bs::parse_address(cp != nullptr ? cp : "");
    if (!address || address->family != bs::Family::IPv4) {
        return 0;
    }
    inp->addr = AddressToNet(*address);
    return 1;
}

char* SOInetNtoA(SOInAddr in) {
    static thread_local char text[16];
    const auto* b = reinterpret_cast<const std::uint8_t*>(&in.addr);
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return text;
}

int SOInetPtoN(int af, const char* src, void* dst) {
    if (af != SO_PF_INET) {
        return SO_EAFNOSUPPORT;
    }
    SOInAddr in;
    if (!SOInetAtoN(src, &in)) {
        return 0;
    }
    std::memcpy(dst, &in.addr, 4);
    return 1;
}

const char* SOInetNtoP(int af, const void* src, char* dst, unsigned len) {
    if (af != SO_PF_INET || dst == nullptr) {
        return nullptr;
    }
    SOInAddr in;
    std::memcpy(&in.addr, src, 4);
    std::snprintf(dst, len, "%s", SOInetNtoA(in));
    return dst;
}

// The host's address toward the internet (network order), as NAT negotiation and QR2 report it
long SOGetHostID(void) {
    const auto local = bs::local_address();
    return local ? static_cast<long>(AddressToNet(*local)) : 0;
}

// One host entry per thread, as the Wii's (callers copy it)
SOHostEnt* SOGetHostByName(const char* name) {
    struct Storage {
        SOHostEnt entry;
        char name[256];
        char* aliases[1];
        std::uint8_t addresses[8][4];
        std::uint8_t* list[9];
    };
    static thread_local Storage storage;
    if (name == nullptr) {
        return nullptr;
    }
    const std::string host = MapHost(name);
    const std::vector<bs::Address> found = bs::resolve(host);
    if (found.empty()) {
        borealis_log_printf(BOREALIS_LOG_WARNING, "cf::so", "cannot resolve %s", host.c_str());
        return nullptr;
    }
    std::snprintf(storage.name, sizeof(storage.name), "%s", name);
    storage.aliases[0] = nullptr;
    std::size_t n = 0;
    for (; n < found.size() && n < 8; n++) {
        std::memcpy(storage.addresses[n], found[n].bytes.data(), 4);
        storage.list[n] = storage.addresses[n];
    }
    storage.list[n] = nullptr;
    storage.entry.name = storage.name;
    storage.entry.aliases = storage.aliases;
    storage.entry.addrType = SO_PF_INET;
    storage.entry.length = 4;
    storage.entry.addrList = storage.list;
    return &storage.entry;
}

SOHostEnt* SOGetHostByAddr(const void* addr, int len, int type) {
    (void)addr;
    (void)len;
    (void)type;
    return nullptr; // no reverse lookups (nothing the game needs)
}

int SOGetAddrInfo(const char* nodeName, const char* servName, const SOAddrInfo* hints, SOAddrInfo** res) {
    *res = nullptr;
    if (nodeName == nullptr) {
        return SO_EINVAL;
    }
    const std::uint16_t port = servName != nullptr ? static_cast<std::uint16_t>(std::atoi(servName)) : 0;
    std::vector<bs::Address> found;
    if (const auto numeric = bs::parse_address(nodeName); numeric && numeric->family == bs::Family::IPv4) {
        found.push_back(*numeric);
    } else if (hints == nullptr || (hints->flags & kAddrInfoFlagNumericHost) == 0) {
        found = bs::resolve(MapHost(nodeName));
    }
    if (found.empty()) {
        return SO_EHOSTUNREACH;
    }
    SOAddrInfo** tail = res;
    for (bs::Address address : found) {
        address.port = port;
        auto* node = static_cast<SOAddrInfo*>(std::calloc(1, sizeof(SOAddrInfo) + sizeof(SOSockAddrIn)));
        node->family = SO_PF_INET;
        node->sockType = hints != nullptr ? hints->sockType : 0;
        node->protocol = hints != nullptr ? hints->protocol : 0;
        node->addrLen = sizeof(SOSockAddrIn);
        node->addr = node + 1;
        ToSockAddr(address, node->addr);
        *tail = node;
        tail = &node->next;
    }
    return SO_SUCCESS;
}

void SOFreeAddrInfo(SOAddrInfo* head) {
    while (head != nullptr) {
        SOAddrInfo* next = head->next;
        std::free(head);
        head = next;
    }
}

int SOGetNameInfo(const void* sockAddr, char* node, unsigned nodeLen, char* service, unsigned serviceLen, int flags) {
    (void)flags;
    const bs::Address address = FromSockAddr(sockAddr);
    if (node != nullptr && nodeLen != 0) {
        std::snprintf(node, nodeLen, "%s", bs::format_address(address, true).c_str());
    }
    if (service != nullptr && serviceLen != 0) {
        std::snprintf(service, serviceLen, "%u", address.port);
    }
    return SO_SUCCESS;
}

// ---------------------------------------------------------------------------------------------
// Sockets
// ---------------------------------------------------------------------------------------------
int SOSocket(int pf, int type, int protocol) {
    (void)protocol;
    if (pf != SO_PF_INET) {
        return SO_EAFNOSUPPORT;
    }
    if (type != SO_SOCK_STREAM && type != SO_SOCK_DGRAM) {
        return SO_EPROTOTYPE;
    }
    auto entry = std::make_unique<Entry>();
    entry->type = type == SO_SOCK_STREAM ? bs::Type::Stream : bs::Type::Datagram;
    bs::Error error;
    entry->socket = bs::Socket::open(bs::Family::IPv4, entry->type, &error);
    if (!entry->socket.valid()) {
        borealis_log_printf(BOREALIS_LOG_WARNING, "cf::so", "socket(%s) failed: %d (%s)",
                            type == SO_SOCK_STREAM ? "tcp" : "udp", ToSoError(error),
                            entry->socket.last_error_message().c_str());
        return ToSoError(error);
    }
    std::lock_guard lock(sMutex);
    int fd = -1;
    for (std::size_t i = 0; i < sSockets.size(); i++) {
        if (!sSockets[i]) {
            sSockets[i] = std::move(entry);
            fd = static_cast<int>(i);
            break;
        }
    }
    if (fd < 0) {
        sSockets.push_back(std::move(entry));
        fd = static_cast<int>(sSockets.size() - 1);
    }
    if (Logging()) {
        borealis_log_printf(BOREALIS_LOG_INFO, "cf::so", "socket %d (%s), %u open", fd,
                            type == SO_SOCK_STREAM ? "tcp" : "udp", static_cast<unsigned>(OpenCount()));
    }
    return fd;
}

int __SOCreateSocket(int pf, int type, int protocol) {
    return SOSocket(pf, type, protocol);
}

int SOClose(int s) {
    std::lock_guard lock(sMutex);
    if (Find(s) == nullptr) {
        return SO_EBADF;
    }
    sSockets[s].reset();
    if (Logging()) {
        borealis_log_printf(BOREALIS_LOG_INFO, "cf::so", "close %d, %u open", s, static_cast<unsigned>(OpenCount()));
    }
    return SO_SUCCESS;
}

int SOShutdown(int s, int how) {
    std::lock_guard lock(sMutex);
    Entry* e = Find(s);
    if (e == nullptr) {
        return SO_EBADF;
    }
    if (how != 0) { // SHUT_WR or SHUT_RDWR: no more sends
        e->socket.shutdown_send();
    }
    return SO_SUCCESS;
}

int SOBind(int s, const void* sockAddr) {
    std::lock_guard lock(sMutex);
    Entry* e = Find(s);
    if (e == nullptr) {
        return SO_EBADF;
    }
    if (e->type == bs::Type::Stream) {
        e->socket.set_reuse_address(true);
    }
    const bs::Address address = FromSockAddr(sockAddr);
    const int result = ToSoError(e->socket.bind(address));
    if (result != SO_SUCCESS) {
        borealis_log_printf(BOREALIS_LOG_WARNING, "cf::so", "bind %d to %s failed: %d (%s)", s,
                            bs::format_address(address, true).c_str(), result, e->socket.last_error_message().c_str());
    } else if (Logging()) {
        borealis_log_printf(BOREALIS_LOG_INFO, "cf::so", "bind %d to %s", s, bs::format_address(address, true).c_str());
    }
    return result;
}

int SOListen(int s, int backlog) {
    std::lock_guard lock(sMutex);
    Entry* e = Find(s);
    return e == nullptr ? SO_EBADF : ToSoError(e->socket.listen(backlog));
}

int SOAccept(int s, void* sockAddr) {
    std::unique_ptr<Entry> accepted;
    {
        std::lock_guard lock(sMutex);
        Entry* e = Find(s);
        if (e == nullptr) {
            return SO_EBADF;
        }
        bs::Address peer;
        bs::Error error;
        bs::Socket socket = e->socket.accept(&peer, &error);
        if (!socket.valid()) {
            return ToSoError(error);
        }
        if (sockAddr != nullptr) {
            ToSockAddr(peer, sockAddr);
        }
        accepted = std::make_unique<Entry>();
        accepted->socket = std::move(socket);
        accepted->type = bs::Type::Stream;
    }
    std::lock_guard lock(sMutex);
    sSockets.push_back(std::move(accepted));
    return static_cast<int>(sSockets.size() - 1);
}

int SOConnect(int s, const void* sockAddr) {
    std::lock_guard lock(sMutex);
    Entry* e = Find(s);
    if (e == nullptr) {
        return SO_EBADF;
    }
    if (e->connecting) {
        return FinishConnect(e);
    }
    const bs::Error error = e->socket.connect(FromSockAddr(sockAddr));
    if (error == bs::Error::InProgress) {
        e->connecting = true;
    }
    return ToSoError(error);
}

int SOGetSockName(int s, void* sockAddr) {
    std::lock_guard lock(sMutex);
    Entry* e = Find(s);
    if (e == nullptr) {
        return SO_EBADF;
    }
    const auto local = e->socket.local();
    if (!local) {
        return SO_EINVAL;
    }
    ToSockAddr(*local, sockAddr);
    return SO_SUCCESS;
}

int SOGetPeerName(int s, void* sockAddr) {
    std::lock_guard lock(sMutex);
    Entry* e = Find(s);
    if (e == nullptr) {
        return SO_EBADF;
    }
    const auto peer = e->socket.peer();
    if (!peer) {
        return SO_ENOTCONN;
    }
    ToSockAddr(*peer, sockAddr);
    return SO_SUCCESS;
}

int SORecvFrom(int s, void* buf, int len, int flags, void* sockFrom) {
    std::lock_guard lock(sMutex);
    Entry* e = Find(s);
    if (e == nullptr) {
        return SO_EBADF;
    }
    if ((flags & kRecvFlagDontWait) != 0 && !e->nonblocking && WouldWait(e, bs::PollRead)) {
        return SO_EAGAIN;
    }
    bs::Error error;
    bs::Address from;
    const std::ptrdiff_t n = e->socket.receive_from(
        std::span(static_cast<std::byte*>(buf), static_cast<std::size_t>(len)), &from, &error,
        (flags & kRecvFlagPeek) != 0);
    if (n < 0) {
        return ToSoError(error);
    }
    if (sockFrom != nullptr) {
        ToSockAddr(from, sockFrom);
    }
    return static_cast<int>(n);
}

int SORecv(int s, void* buf, int len, int flags) {
    std::lock_guard lock(sMutex);
    Entry* e = Find(s);
    if (e == nullptr) {
        return SO_EBADF;
    }
    if ((flags & kRecvFlagDontWait) != 0 && !e->nonblocking && WouldWait(e, bs::PollRead)) {
        return SO_EAGAIN;
    }
    bs::Error error;
    const std::ptrdiff_t n = e->socket.receive(std::span(static_cast<std::byte*>(buf), static_cast<std::size_t>(len)),
                                               &error, (flags & kRecvFlagPeek) != 0);
    return n < 0 ? ToSoError(error) : static_cast<int>(n);
}

int SORead(int s, void* buf, int len) {
    return SORecv(s, buf, len, 0);
}

int SOSendTo(int s, const void* buf, int len, int flags, const void* sockTo) {
    std::lock_guard lock(sMutex);
    Entry* e = Find(s);
    if (e == nullptr) {
        return SO_EBADF;
    }
    if ((flags & kRecvFlagDontWait) != 0 && !e->nonblocking && WouldWait(e, bs::PollWrite)) {
        return SO_EAGAIN;
    }
    bs::Error error;
    const auto data = std::span(static_cast<const std::byte*>(buf), static_cast<std::size_t>(len));
    const std::ptrdiff_t n = sockTo != nullptr ? e->socket.send_to(data, FromSockAddr(sockTo), &error)
                                               : e->socket.send(data, &error);
    return n < 0 ? ToSoError(error) : static_cast<int>(n);
}

int SOSend(int s, const void* buf, int len, int flags) {
    return SOSendTo(s, buf, len, flags, nullptr);
}

int SOWrite(int s, const void* buf, int len) {
    return SOSend(s, buf, len, 0);
}

int SOFcntl(int s, int cmd, ...) {
    std::lock_guard lock(sMutex);
    Entry* e = Find(s);
    if (e == nullptr) {
        return SO_EBADF;
    }
    if (cmd == SO_F_GETFL) {
        return e->nonblocking ? SO_O_NONBLOCK : 0;
    }
    if (cmd == SO_F_SETFL) {
        va_list args;
        va_start(args, cmd);
        const int value = va_arg(args, int);
        va_end(args);
        const bool nonblocking = (value & SO_O_NONBLOCK) != 0;
        const bs::Error error = e->socket.set_nonblocking(nonblocking);
        if (error == bs::Error::None) {
            e->nonblocking = nonblocking;
        }
        return ToSoError(error);
    }
    return SO_EINVAL;
}

int SOSockAtMark(int s) {
    (void)s;
    return 0; // no out-of-band data
}

int SOPoll(SOPollFD fds[], unsigned nfds, OSTime timeout) {
    std::vector<bs::PollEntry> entries(nfds);
    {
        std::lock_guard lock(sMutex);
        for (unsigned i = 0; i < nfds; i++) {
            Entry* e = Find(fds[i].fd);
            entries[i].socket = e != nullptr ? &e->socket : nullptr;
            entries[i].events = ((fds[i].events & SO_POLLIN) ? bs::PollRead : 0) |
                                ((fds[i].events & (SO_POLLWRNORM | SO_POLLWRBAND)) ? bs::PollWrite : 0);
        }
    }
    const auto wait = timeout < 0 ? std::chrono::milliseconds(-1)
                                  : std::chrono::milliseconds(static_cast<long long>(timeout) / kTicksPerMs);
    bs::Error error;
    const int ready = bs::poll(entries, wait, &error);
    if (ready < 0) {
        return ToSoError(error);
    }
    int count = 0;
    for (unsigned i = 0; i < nfds; i++) {
        const std::uint32_t r = entries[i].revents;
        int revents = 0;
        if (r & bs::PollRead) {
            revents |= fds[i].events & SO_POLLIN ? fds[i].events & SO_POLLIN : SO_POLLRDNORM;
        }
        if (r & bs::PollWrite) {
            revents |= SO_POLLWRNORM;
        }
        if (r & bs::PollError) {
            revents |= SO_POLLERR;
        }
        if (r & bs::PollHangup) {
            revents |= SO_POLLHUP;
        }
        if (r & bs::PollInvalid) {
            revents |= SO_POLLNVAL;
        }
        fds[i].revents = revents;
        count += revents != 0 ? 1 : 0;
    }
    return count;
}

// ---------------------------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------------------------
int SOSetSockOpt(int s, int level, int optname, const void* optval, int optlen) {
    std::lock_guard lock(sMutex);
    Entry* e = Find(s);
    if (e == nullptr) {
        return SO_EBADF;
    }
    if (level == SO_SOL_SOCKET && optval != nullptr && optlen >= static_cast<int>(sizeof(int))) {
        int value;
        std::memcpy(&value, optval, sizeof(value));
        if (optname == SO_SO_SNDBUF) {
            return ToSoError(e->socket.set_send_buffer(value));
        }
        if (optname == SO_SO_RCVBUF) {
            return ToSoError(e->socket.set_receive_buffer(value));
        }
    }
    return SO_SUCCESS; // others: the host's defaults serve
}

int SOGetSockOpt(int s, int level, int optname, void* optval, int* optlen) {
    std::lock_guard lock(sMutex);
    Entry* e = Find(s);
    if (e == nullptr) {
        return SO_EBADF;
    }
    if (optval == nullptr || optlen == nullptr || *optlen < static_cast<int>(sizeof(int))) {
        return SO_EINVAL;
    }
    int value = 0;
    if (level == SO_SOL_SOCKET && optname == SO_SO_SNDBUF) {
        value = e->socket.send_buffer().value_or(0);
    } else if (level == SO_SOL_SOCKET && optname == SO_SO_RCVBUF) {
        value = e->socket.receive_buffer().value_or(0);
    }
    std::memcpy(optval, &value, sizeof(value));
    *optlen = sizeof(value);
    return SO_SUCCESS;
}

// The interface: its address table, its link (up), its MAC address
int SOGetInterfaceOpt(IPInterface* iface, int level, int optname, void* optval, int* optlen) {
    (void)iface;
    if (level != SO_SOL_CONFIG || optval == nullptr || optlen == nullptr) {
        return SO_EINVAL;
    }
    switch (optname) {
    case SO_CONFIG_IP_ADDR_NUMBER: {
        const int count = bs::local_address() ? 1 : 0;
        if (*optlen < static_cast<int>(sizeof(int))) {
            return SO_EINVAL;
        }
        std::memcpy(optval, &count, sizeof(count));
        *optlen = sizeof(count);
        return SO_SUCCESS;
    }
    case SO_CONFIG_IP_ADDR_TABLE: {
        const auto local = bs::local_address();
        if (!local || *optlen < static_cast<int>(sizeof(IPAddrEntry))) {
            *optlen = 0;
            return SO_SUCCESS;
        }
        IPAddrEntry entry;
        std::memcpy(entry.addr, local->bytes.data(), 4);
        const std::uint8_t mask[4] = {255, 255, 255, 0};
        std::memcpy(entry.netMask, mask, 4);
        for (int i = 0; i < 4; i++) {
            entry.bcastAddr[i] = static_cast<std::uint8_t>(entry.addr[i] | ~entry.netMask[i]);
        }
        std::memcpy(optval, &entry, sizeof(entry));
        *optlen = sizeof(entry);
        return SO_SUCCESS;
    }
    case SO_CONFIG_LINK_STATE: {
        const int up = 1;
        if (*optlen >= static_cast<int>(sizeof(int))) {
            std::memcpy(optval, &up, sizeof(up));
            *optlen = sizeof(up);
        }
        return SO_SUCCESS;
    }
    case SO_CONFIG_MAC_ADDRESS: {
        const std::uint8_t mac[6] = {0x00, 0x17, 0xAB, 0x00, 0x00, 0x01}; // a Nintendo prefix; nothing checks it
        const int n = *optlen < 6 ? *optlen : 6;
        std::memcpy(optval, mac, static_cast<std::size_t>(n));
        *optlen = n;
        return SO_SUCCESS;
    }
    default:
        *optlen = 0;
        return SO_SUCCESS;
    }
}

int SOSetInterfaceOpt(IPInterface* iface, int level, int optname, const void* optval, int optlen) {
    (void)iface, (void)level, (void)optname, (void)optval, (void)optlen;
    return SO_SUCCESS;
}

} // extern "C"
