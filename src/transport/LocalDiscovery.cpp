#include "LocalDiscovery.h"

#ifdef CR_NETWORK
#  include <enet/enet.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cr {

namespace {

// Wire format: magic ('CRDS') + version + game_port (LE) + 32-byte name.
// Total 39 bytes -- a single UDP datagram, no fragmentation risk.
constexpr char     kMagic[4]     = {'C', 'R', 'D', 'S'};
constexpr uint8_t  kVersion      = 1;
constexpr size_t   kNameSize     = 32;
constexpr size_t   kPacketSize   = 4 + 1 + 2 + kNameSize;

#ifdef CR_NETWORK
// Cast helper since the header stores the socket as int64_t to avoid leaking
// <enet/enet.h>. ENET_SOCKET_NULL is the platform's "invalid socket" value.
ENetSocket toSock(int64_t v) { return static_cast<ENetSocket>(v); }
int64_t fromSock(ENetSocket s) { return static_cast<int64_t>(s); }
#endif

void packAnnounce(uint8_t out[kPacketSize], uint16_t game_port,
                  const char name[kNameSize]) {
    std::memcpy(out + 0, kMagic, 4);
    out[4] = kVersion;
    // Little-endian uint16. Hand-rolled rather than memcpy so it's portable
    // on a big-endian host (we don't target any but the comment matters).
    out[5] = static_cast<uint8_t>(game_port & 0xFF);
    out[6] = static_cast<uint8_t>((game_port >> 8) & 0xFF);
    std::memcpy(out + 7, name, kNameSize);
}

bool unpackAnnounce(const uint8_t* data, size_t len,
                    uint16_t& out_game_port, char out_name[kNameSize]) {
    if (len != kPacketSize) return false;
    if (std::memcmp(data, kMagic, 4) != 0) return false;
    if (data[4] != kVersion) return false;
    out_game_port = static_cast<uint16_t>(data[5]) |
                    (static_cast<uint16_t>(data[6]) << 8);
    std::memcpy(out_name, data + 7, kNameSize);
    // Always null-terminate -- a remote sender could ship an unterminated
    // payload and our DiscoveredHostEntry::name copy would otherwise read
    // beyond the buffer.
    out_name[kNameSize - 1] = '\0';
    return true;
}

} // namespace

LocalDiscovery::LocalDiscovery() = default;
LocalDiscovery::~LocalDiscovery() { stop(); }

bool LocalDiscovery::startHost(uint16_t game_port,
                               const std::string& display_name) {
#ifdef CR_NETWORK
    stop();
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL) {
        std::fprintf(stderr, "[discovery] socket_create failed (host)\n");
        return false;
    }
    if (enet_socket_set_option(s, ENET_SOCKOPT_BROADCAST, 1) < 0) {
        std::fprintf(stderr, "[discovery] enable BROADCAST failed (host)\n");
        enet_socket_destroy(s);
        return false;
    }
    if (enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1) < 0) {
        // Non-fatal: announcing is fire-and-forget so a blocking sendto is
        // also fine. Log + continue.
        std::fprintf(stderr, "[discovery] enable NONBLOCK failed (host)\n");
    }
    socket_    = fromSock(s);
    game_port_ = game_port;
    std::memset(name_, 0, sizeof(name_));
    std::strncpy(name_, display_name.c_str(), kNameSize - 1);
    mode_ = Mode::Host;
    std::printf("[discovery] host announces on udp/%u for game port %u\n",
                static_cast<unsigned>(kDiscoveryPort),
                static_cast<unsigned>(game_port));
    return true;
#else
    (void)game_port;
    (void)display_name;
    return false;
#endif
}

bool LocalDiscovery::startClient() {
#ifdef CR_NETWORK
    stop();
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL) {
        std::fprintf(stderr, "[discovery] socket_create failed (client)\n");
        return false;
    }
    // Reuse the port so a second instance on the same machine can also
    // listen. Without this, two simultaneous JOIN screens on one box can't
    // both discover (the second startClient fails to bind). Cross-platform
    // via ENet's REUSEADDR socket option.
    if (enet_socket_set_option(s, ENET_SOCKOPT_REUSEADDR, 1) < 0) {
        // Not fatal -- log and try the bind anyway. Single-instance JOIN
        // will still work.
        std::fprintf(stderr, "[discovery] enable REUSEADDR failed (client)\n");
    }
    if (enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1) < 0) {
        std::fprintf(stderr, "[discovery] enable NONBLOCK failed (client)\n");
        enet_socket_destroy(s);
        return false;
    }
    ENetAddress bind_addr{};
    bind_addr.host = ENET_HOST_ANY;
    bind_addr.port = kDiscoveryPort;
    if (enet_socket_bind(s, &bind_addr) < 0) {
        std::fprintf(stderr, "[discovery] bind failed on udp/%u\n",
                     static_cast<unsigned>(kDiscoveryPort));
        enet_socket_destroy(s);
        return false;
    }
    socket_ = fromSock(s);
    mode_   = Mode::Client;
    std::printf("[discovery] client listens on udp/%u\n",
                static_cast<unsigned>(kDiscoveryPort));
    return true;
#else
    return false;
#endif
}

void LocalDiscovery::stop() {
#ifdef CR_NETWORK
    if (mode_ != Mode::Idle && socket_ != -1) {
        enet_socket_destroy(toSock(socket_));
    }
#endif
    socket_    = -1;
    game_port_ = 0;
    name_[0]   = '\0';
    mode_      = Mode::Idle;
    hosts_.clear();
}

void LocalDiscovery::announceNow() {
#ifdef CR_NETWORK
    if (mode_ != Mode::Host || socket_ == -1) return;
    uint8_t packet[kPacketSize];
    packAnnounce(packet, game_port_, name_);
    ENetBuffer buf{};
    buf.data       = packet;
    buf.dataLength = kPacketSize;

    // Destination 1: limited broadcast (255.255.255.255). Reaches every host
    // on the LAN via the default interface's broadcast address.
    ENetAddress bcast{};
    bcast.host = ENET_HOST_BROADCAST;
    bcast.port = kDiscoveryPort;
    int s1 = enet_socket_send(toSock(socket_), &bcast, &buf, 1);

    // Destination 2: 127.0.0.1 loopback. macOS (and a number of Linux distros
    // with strict reverse-path filtering) DOESN'T deliver 255.255.255.255
    // packets to a local listener bound to 0.0.0.0 -- the packet goes out the
    // physical interface and never crosses back. For two-instances-on-one-
    // machine testing we explicitly cc the loopback so the local JOIN screen
    // also sees the announce.
    ENetAddress loop{};
    enet_address_set_host(&loop, "127.0.0.1");
    loop.port = kDiscoveryPort;
    int s2 = enet_socket_send(toSock(socket_), &loop, &buf, 1);

    // One-time confirmation print so it's obvious from the terminal whether
    // the host is actually shipping announces. After the first success we
    // shut up to avoid spamming.
    static bool logged_once = false;
    if (!logged_once && (s1 > 0 || s2 > 0)) {
        std::printf("[discovery] announce sent (bcast=%d bytes, loopback=%d bytes)\n",
                    s1, s2);
        logged_once = true;
    }
#endif
}

void LocalDiscovery::pollIncoming() {
#ifdef CR_NETWORK
    if (mode_ != Mode::Client || socket_ == -1) return;
    // Drain everything pending; non-blocking socket returns 0 when empty.
    for (int safety = 0; safety < 64; ++safety) {
        uint8_t       buf_data[kPacketSize + 16]; // a few extra so we can
                                                  // reject oversized packets
        ENetBuffer    buf{};
        buf.data       = buf_data;
        buf.dataLength = sizeof(buf_data);
        ENetAddress   src{};
        int received = enet_socket_receive(toSock(socket_), &src, &buf, 1);
        if (received <= 0) break; // 0 means "no data available" w/ NONBLOCK

        uint16_t game_port = 0;
        char     name_buf[kNameSize] = {0};
        if (!unpackAnnounce(buf_data,
                            static_cast<size_t>(received),
                            game_port, name_buf)) {
            continue; // not one of ours
        }
        // Render the source IP to dotted-quad for the address field.
        char addr_str[64] = {0};
        if (enet_address_get_host_ip(&src, addr_str, sizeof(addr_str)) < 0) {
            // Fallback: print the raw 32-bit host field as dotted quad.
            const uint8_t* b = reinterpret_cast<const uint8_t*>(&src.host);
            std::snprintf(addr_str, sizeof(addr_str), "%u.%u.%u.%u",
                          b[0], b[1], b[2], b[3]);
        }
        // Dedupe: if we've already seen this (address, game_port), bump
        // last_seen and update name; otherwise append.
        bool found = false;
        for (auto& h : hosts_) {
            if (h.address == addr_str && h.game_port == game_port) {
                // Use a wall-clock seconds counter via enet_time_get / 1000.
                h.last_seen = static_cast<double>(enet_time_get()) * 0.001;
                std::memcpy(h.name, name_buf, kNameSize);
                h.name[kNameSize - 1] = '\0';
                found = true;
                break;
            }
        }
        if (!found) {
            DiscoveredHostEntry e;
            e.address   = addr_str;
            e.game_port = game_port;
            e.last_seen = static_cast<double>(enet_time_get()) * 0.001;
            std::memset(e.name, 0, sizeof(e.name));
            std::memcpy(e.name, name_buf, kNameSize);
            e.name[sizeof(e.name) - 1] = '\0';
            std::printf("[discovery] new host found: %s:%u (%s)\n",
                        e.address.c_str(),
                        static_cast<unsigned>(e.game_port),
                        e.name);
            hosts_.push_back(std::move(e));
        }
    }
#endif
}

void LocalDiscovery::getKnownHosts(std::vector<DiscoveredHostEntry>& out,
                                   double now_sec,
                                   double stale_after_sec) {
    // Drop stale entries first.
    hosts_.erase(std::remove_if(hosts_.begin(), hosts_.end(),
                                [&](const DiscoveredHostEntry& e) {
                                    return (now_sec - e.last_seen) > stale_after_sec;
                                }),
                 hosts_.end());
    out.assign(hosts_.begin(), hosts_.end());
}

} // namespace cr
