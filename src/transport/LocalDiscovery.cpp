#include "LocalDiscovery.h"

#ifdef CR_NETWORK
#  include <enet/enet.h>
#endif

#include <algorithm>
#include <cerrno>
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
        std::fprintf(stderr, "[discovery] socket_create failed (host) errno=%d (%s)\n",
                     errno, std::strerror(errno));
        return false;
    }
    if (enet_socket_set_option(s, ENET_SOCKOPT_BROADCAST, 1) < 0) {
        std::fprintf(stderr, "[discovery] enable BROADCAST failed (host) errno=%d (%s)\n",
                     errno, std::strerror(errno));
        enet_socket_destroy(s);
        return false;
    }
    if (enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1) < 0) {
        // Non-fatal: announcing is fire-and-forget so a blocking sendto is
        // also fine. Log + continue.
        std::fprintf(stderr, "[discovery] enable NONBLOCK failed (host) errno=%d (%s)\n",
                     errno, std::strerror(errno));
    }
    socket_    = fromSock(s);
    game_port_ = game_port;
    std::memset(name_, 0, sizeof(name_));
    std::strncpy(name_, display_name.c_str(), kNameSize - 1);
    mode_ = Mode::Host;
    std::printf("[discovery] host announces on udp/%u..%u for game port %u\n",
                static_cast<unsigned>(kDiscoveryPortBase),
                static_cast<unsigned>(kDiscoveryPortBase + kDiscoveryPortCount - 1),
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
    // We try each port in the range [base, base+count). EADDRINUSE on the
    // first port is common (some background daemon snagged it); falling back
    // to the next one usually wins. We create a fresh socket per attempt so
    // we don't have to undo prior setsockopt state.
    for (int i = 0; i < kDiscoveryPortCount; ++i) {
        const uint16_t port = static_cast<uint16_t>(kDiscoveryPortBase + i);
        ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if (s == ENET_SOCKET_NULL) {
            std::fprintf(stderr, "[discovery] socket_create failed (client) errno=%d (%s)\n",
                         errno, std::strerror(errno));
            return false;
        }
        if (enet_socket_set_option(s, ENET_SOCKOPT_REUSEADDR, 1) < 0) {
            std::fprintf(stderr, "[discovery] enable REUSEADDR failed (client) errno=%d (%s)\n",
                         errno, std::strerror(errno));
        }
        if (enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1) < 0) {
            std::fprintf(stderr, "[discovery] enable NONBLOCK failed (client) errno=%d (%s)\n",
                         errno, std::strerror(errno));
            enet_socket_destroy(s);
            return false;
        }
        ENetAddress bind_addr{};
        bind_addr.host = ENET_HOST_ANY;
        bind_addr.port = port;
        if (enet_socket_bind(s, &bind_addr) == 0) {
            socket_ = fromSock(s);
            mode_   = Mode::Client;
            std::printf("[discovery] client listens on udp/%u\n",
                        static_cast<unsigned>(port));
            return true;
        }
        // EADDRINUSE on this port is expected if a daemon owns it. Try the
        // next one. Log the first failure so the user knows what happened
        // (subsequent retries are logged at full chain end if all fail).
        if (i == 0) {
            std::fprintf(stderr, "[discovery] bind failed on udp/%u errno=%d (%s); "
                                  "trying next port...\n",
                         static_cast<unsigned>(port),
                         errno, std::strerror(errno));
            if (errno == EADDRINUSE) {
                std::fprintf(stderr, "[discovery] hint: `lsof -nP -iUDP:%u` will show "
                                      "what's holding it.\n",
                             static_cast<unsigned>(port));
            }
        }
        enet_socket_destroy(s);
    }
    std::fprintf(stderr, "[discovery] all %d fallback ports busy (udp/%u..%u); "
                          "manual host:port entry still works\n",
                 kDiscoveryPortCount,
                 static_cast<unsigned>(kDiscoveryPortBase),
                 static_cast<unsigned>(kDiscoveryPortBase + kDiscoveryPortCount - 1));
    return false;
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

    // Fire to BOTH the LAN broadcast AND the loopback for each port in the
    // fallback range. Client may be listening on any one of these depending
    // on which ports were free when it bound. Total per call: at most
    // 2 * kDiscoveryPortCount sends (~6 tiny UDP datagrams). Cheap.
    int total_sent = 0;
    for (int i = 0; i < kDiscoveryPortCount; ++i) {
        const uint16_t port = static_cast<uint16_t>(kDiscoveryPortBase + i);

        // LAN broadcast. Reaches every host on the subnet via the default
        // interface's broadcast address.
        ENetAddress bcast{};
        bcast.host = ENET_HOST_BROADCAST;
        bcast.port = port;
        int s1 = enet_socket_send(toSock(socket_), &bcast, &buf, 1);
        if (s1 > 0) ++total_sent;

        // Loopback. macOS (and Linux configs with strict reverse-path
        // filtering) doesn't deliver 255.255.255.255 to local listeners
        // bound to 0.0.0.0, so we explicitly cc 127.0.0.1 for same-machine
        // testing.
        ENetAddress loop{};
        enet_address_set_host(&loop, "127.0.0.1");
        loop.port = port;
        int s2 = enet_socket_send(toSock(socket_), &loop, &buf, 1);
        if (s2 > 0) ++total_sent;
    }

    // First-success confirmation + periodic heartbeat. Without the heartbeat
    // it's impossible to tell from the log whether the announcer is silently
    // dying (all sends returning -1) or is happily firing but the receiving
    // side is the broken half. Once per ~10s is plenty for diagnostics
    // without spamming.
    static bool   logged_first = false;
    static int    call_counter = 0;
    if (!logged_first && total_sent > 0) {
        std::printf("[discovery] announce sent (%d packets across %d ports)\n",
                    total_sent, kDiscoveryPortCount);
        logged_first = true;
    }
    if ((++call_counter % 10) == 0) {
        std::printf("[discovery] heartbeat: %d announces fired so far "
                    "(last: %d / %d packets ok)\n",
                    call_counter, total_sent, 2 * kDiscoveryPortCount);
    }
    // If a send EVER fails, log it once. This catches "permission denied"
    // (macOS Local Network privacy prompt declined) and similar errors
    // that would otherwise be silent.
    if (total_sent < 2 * kDiscoveryPortCount) {
        static bool logged_fail = false;
        if (!logged_fail) {
            std::fprintf(stderr,
                         "[discovery] partial send failure (%d of %d packets ok). "
                         "errno=%d (%s)\n",
                         total_sent, 2 * kDiscoveryPortCount,
                         errno, std::strerror(errno));
            logged_fail = true;
        }
    }
#endif
}

void LocalDiscovery::pollIncoming(double now_sec) {
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
            // Diagnostic: log the first malformed packet we see so we can
            // tell whether the wire is silent vs. carrying noise we reject.
            static bool logged_bad = false;
            if (!logged_bad) {
                std::fprintf(stderr,
                             "[discovery] rejected packet (len=%d) -- not one of ours\n",
                             received);
                logged_bad = true;
            }
            continue;
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
                // Stamp with the caller's clock so getKnownHosts can compare
                // last_seen against the same source -- no skew, no false
                // staleness drops.
                h.last_seen = now_sec;
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
            e.last_seen = now_sec;
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
