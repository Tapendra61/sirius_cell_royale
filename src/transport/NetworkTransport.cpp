#include "NetworkTransport.h"

#include "Codec.h"

#ifdef CR_NETWORK
#  include <enet/enet.h>
#endif

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

// ENet wrapper. When CR_NETWORK is defined the four overrides do real UDP socket
// work; without it they fall back to the in-memory queues (which is the same loopback
// behaviour the skeleton commit shipped).
//
// Channels:
//   CHAN_COMMAND  = 0  (client -> host: input intent, reliable)
//   CHAN_SNAPSHOT = 1  (host -> clients: world state, reliable, the big one)
//   CHAN_EVENT    = 2  (host -> clients: gameplay event stings, reliable)
// We're treating LAN as low-latency + reliable; snapshot bandwidth dominates, but
// LAN can absorb the cost. Phase 11 will revisit with deltas + unreliable snapshots.

namespace cr {

namespace {

#ifdef CR_NETWORK

// ENet requires a one-time global initialisation per process. NetworkTransport
// constructors / destructors refcount through these so multiple matches don't
// re-init or double-deinit.
std::mutex      g_enet_init_mutex;
int             g_enet_init_refcount = 0;
std::atomic_bool g_enet_init_failed{false};

void enetGlobalAcquire() {
    std::lock_guard<std::mutex> lock(g_enet_init_mutex);
    if (g_enet_init_refcount == 0) {
        if (enet_initialize() != 0) {
            g_enet_init_failed.store(true);
            std::fprintf(stderr, "[net] enet_initialize() failed\n");
            return;
        }
        g_enet_init_failed.store(false);
    }
    ++g_enet_init_refcount;
}

void enetGlobalRelease() {
    std::lock_guard<std::mutex> lock(g_enet_init_mutex);
    if (g_enet_init_refcount <= 0) return;
    --g_enet_init_refcount;
    if (g_enet_init_refcount == 0 && !g_enet_init_failed.load()) {
        enet_deinitialize();
    }
}

// Channels mirror the codec stream split.
constexpr enet_uint8 CHAN_COMMAND  = 0;
constexpr enet_uint8 CHAN_SNAPSHOT = 1;
constexpr enet_uint8 CHAN_EVENT    = 2;
constexpr enet_uint8 CHAN_CONTROL  = 3; // handshake / welcome / future room mgmt
constexpr size_t     kChannelCount = 4;

// Wrap a byte buffer in an ENet packet. RELIABLE = guaranteed-delivery + ordered.
// We move the bytes in -- ENet copies internally so the caller's vector can be reused
// after this call.
ENetPacket* makePacket(const std::vector<uint8_t>& bytes) {
    return enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
}

// Parse "host[:port]" into ENetAddress. Returns true on success. Falls back to the
// caller-provided default_port if the input has no ':port' suffix.
bool parseAddress(const std::string& s, ENetAddress& out, uint16_t default_port) {
    if (s.empty()) return false;
    std::string host_part = s;
    uint16_t    port      = default_port;
    auto colon = s.find_last_of(':');
    if (colon != std::string::npos) {
        host_part = s.substr(0, colon);
        try {
            int p = std::stoi(s.substr(colon + 1));
            if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
            else                    return false;
        } catch (...) {
            return false;
        }
    }
    out.port = port;
    return enet_address_set_host(&out, host_part.c_str()) == 0;
}

#endif // CR_NETWORK

} // namespace

NetworkTransport::NetworkTransport() {
#ifdef CR_NETWORK
    enetGlobalAcquire();
#endif
}

NetworkTransport::~NetworkTransport() {
    disconnect();
#ifdef CR_NETWORK
    enetGlobalRelease();
#endif
}

bool NetworkTransport::host(uint16_t port) {
#ifdef CR_NETWORK
    if (g_enet_init_failed.load()) {
        std::fprintf(stderr, "[net] host(): ENet failed to init; falling back to "
                              "loopback queues\n");
        role_      = Role::Host;
        host_port_ = port;
        return false;
    }
    if (enet_host_ != nullptr) disconnect();

    ENetAddress address;
    address.host = ENET_HOST_ANY; // bind on all interfaces
    address.port = port;

    // Up to 8 incoming peers, kChannelCount channels, no bandwidth caps.
    auto* host = enet_host_create(&address,
                                  /*peerCount=*/8,
                                  /*channelLimit=*/kChannelCount,
                                  /*incomingBandwidth=*/0,
                                  /*outgoingBandwidth=*/0);
    if (host == nullptr) {
        std::fprintf(stderr, "[net] enet_host_create (listen) failed on port %u\n",
                     static_cast<unsigned>(port));
        return false;
    }
    enet_host_  = static_cast<void*>(host);
    enet_peer_  = nullptr;
    role_       = Role::Host;
    host_port_  = port;
    peer_count_ = 0;
    std::printf("[net] host bound on udp/%u (waiting for peers)\n",
                static_cast<unsigned>(port));
    return true;
#else
    // No ENet -> skeleton fallback: record the role so the lobby can show the
    // "hosting" state, but nothing is actually listening.
    role_      = Role::Host;
    host_port_ = port;
    return true;
#endif
}

bool NetworkTransport::connect(const std::string& address) {
#ifdef CR_NETWORK
    if (g_enet_init_failed.load()) {
        std::fprintf(stderr, "[net] connect(): ENet failed to init; falling back to "
                              "loopback queues\n");
        role_        = Role::Client;
        client_peer_ = address;
        return false;
    }
    if (enet_host_ != nullptr) disconnect();

    // Client host (no incoming peers, just outbound). 1 outgoing peer slot is enough
    // for the single host we're connecting to.
    auto* host = enet_host_create(/*address=*/nullptr,
                                  /*peerCount=*/1,
                                  /*channelLimit=*/kChannelCount,
                                  /*incomingBandwidth=*/0,
                                  /*outgoingBandwidth=*/0);
    if (host == nullptr) {
        std::fprintf(stderr, "[net] enet_host_create (client) failed\n");
        return false;
    }

    constexpr uint16_t kDefaultHostPort = 7456;
    ENetAddress addr{};
    if (!parseAddress(address, addr, kDefaultHostPort)) {
        std::fprintf(stderr, "[net] connect(): could not parse address '%s'\n",
                     address.c_str());
        enet_host_destroy(host);
        return false;
    }

    auto* peer = enet_host_connect(host, &addr, kChannelCount, /*data=*/0);
    if (peer == nullptr) {
        std::fprintf(stderr, "[net] enet_host_connect failed (no peer slot)\n");
        enet_host_destroy(host);
        return false;
    }

    enet_host_   = static_cast<void*>(host);
    enet_peer_   = static_cast<void*>(peer);
    role_        = Role::Client;
    client_peer_ = address;
    peer_count_  = 0; // bumps to 1 in poll() once the CONNECT event fires
    std::printf("[net] client connecting to %s (port %u resolved)\n",
                address.c_str(), static_cast<unsigned>(addr.port));
    return true;
#else
    role_        = Role::Client;
    client_peer_ = address;
    return true;
#endif
}

void NetworkTransport::disconnect() {
#ifdef CR_NETWORK
    if (enet_host_ != nullptr) {
        auto* host = static_cast<ENetHost*>(enet_host_);
        // For clients, ask the peer for a graceful disconnect and service briefly.
        if (enet_peer_ != nullptr) {
            auto* peer = static_cast<ENetPeer*>(enet_peer_);
            enet_peer_disconnect(peer, 0);
            // Service for ~100 ms so the disconnect packet actually leaves the
            // box. We drain any final events but ignore their content.
            ENetEvent ev;
            while (enet_host_service(host, &ev, 100) > 0) {
                if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                    enet_packet_destroy(ev.packet);
                }
                if (ev.type == ENET_EVENT_TYPE_DISCONNECT) break;
            }
        }
        enet_host_destroy(host);
    }
    enet_host_ = nullptr;
    enet_peer_ = nullptr;
#endif
    role_        = Role::Idle;
    host_port_   = 0;
    peer_count_  = 0;
    client_peer_.clear();
    commands_.clear();
    snapshots_.clear();
    events_.clear();
    new_peers_.clear();
    departed_peers_.clear();
    welcomes_.clear();
}

void NetworkTransport::poll() {
#ifdef CR_NETWORK
    if (enet_host_ == nullptr) return;
    auto*     host = static_cast<ENetHost*>(enet_host_);
    ENetEvent ev;
    // Service repeatedly with 0-timeout so we drain everything that's pending without
    // blocking the frame.
    while (enet_host_service(host, &ev, 0) > 0) {
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                ++peer_count_;
                std::printf("[net] peer connected (peers=%d)\n", peer_count_);
                // Host-side: surface the peer pointer so the match loop can spawn
                // a cell for them and send their welcome packet. The client side
                // also gets a CONNECT (against the host) but ignores new_peers_.
                if (role_ == Role::Host) {
                    PeerHandle ph;
                    ph.enet_peer = static_cast<void*>(ev.peer);
                    new_peers_.push_back(ph);
                }
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                if (peer_count_ > 0) --peer_count_;
                std::printf("[net] peer disconnected (peers=%d)\n", peer_count_);
                // If WE were the client and the host vanished, clear enet_peer_ so
                // future sends short-circuit instead of hitting a stale peer.
                if (role_ == Role::Client && ev.peer == static_cast<ENetPeer*>(enet_peer_)) {
                    enet_peer_ = nullptr;
                }
                // Host-side: surface the departing peer so the match loop can
                // despawn their cells + free their PlayerId slot.
                if (role_ == Role::Host) {
                    PeerHandle ph;
                    ph.enet_peer = static_cast<void*>(ev.peer);
                    departed_peers_.push_back(ph);
                }
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                // Route the packet to the right decoder by channelID. We trust the
                // channel split because the codec also re-checks the version byte
                // inside the payload (so a peer sending Commands on the Snapshot
                // channel would still decode/fail correctly).
                const uint8_t* data = ev.packet->data;
                const size_t   len  = ev.packet->dataLength;
                switch (ev.channelID) {
                    case CHAN_COMMAND: {
                        Command c;
                        if (codec::decodeCommand(data, len, c)) {
                            commands_.push_back(std::move(c));
                        } else {
                            std::fprintf(stderr, "[net] decodeCommand failed (%zu bytes)\n", len);
                        }
                        break;
                    }
                    case CHAN_SNAPSHOT: {
                        Snapshot s;
                        if (codec::decodeSnapshot(data, len, s)) {
                            snapshots_.push_back(std::move(s));
                        } else {
                            std::fprintf(stderr, "[net] decodeSnapshot failed (%zu bytes)\n", len);
                        }
                        break;
                    }
                    case CHAN_EVENT: {
                        GameEvent e;
                        if (codec::decodeEvent(data, len, e)) {
                            events_.push_back(std::move(e));
                        } else {
                            std::fprintf(stderr, "[net] decodeEvent failed (%zu bytes)\n", len);
                        }
                        break;
                    }
                    case CHAN_CONTROL: {
                        codec::WelcomeMsg w;
                        if (codec::decodeWelcome(data, len, w)) {
                            welcomes_.push_back(w);
                            std::printf("[net] welcome: player_id=%u cell_id=%u\n",
                                        static_cast<unsigned>(w.player_id),
                                        static_cast<unsigned>(w.cell_id));
                        } else {
                            std::fprintf(stderr, "[net] decodeWelcome failed (%zu bytes)\n", len);
                        }
                        break;
                    }
                    default:
                        std::fprintf(stderr, "[net] packet on unknown channel %u\n",
                                     static_cast<unsigned>(ev.channelID));
                        break;
                }
                enet_packet_destroy(ev.packet);
                break;
            }
            case ENET_EVENT_TYPE_NONE:
                break;
        }
    }
#endif
}

void NetworkTransport::sendCommand(Command cmd) {
#ifdef CR_NETWORK
    if (role_ == Role::Client && enet_host_ != nullptr && enet_peer_ != nullptr) {
        // Client: serialise + send to host.
        std::vector<uint8_t> buf;
        if (codec::encodeCommand(cmd, buf)) {
            auto* peer = static_cast<ENetPeer*>(enet_peer_);
            enet_peer_send(peer, CHAN_COMMAND, makePacket(buf));
        }
        return;
    }
    // Host: the local-player's own commands stay in-process via the deque (peers'
    // commands enter via poll() into the same deque).
#endif
    commands_.push_back(std::move(cmd));
}

bool NetworkTransport::pollCommand(Command& out) {
    if (commands_.empty()) return false;
    out = std::move(commands_.front());
    commands_.pop_front();
    return true;
}

void NetworkTransport::sendSnapshot(Snapshot snap) {
#ifdef CR_NETWORK
    if (role_ == Role::Host && enet_host_ != nullptr) {
        std::vector<uint8_t> buf;
        if (codec::encodeSnapshot(snap, buf)) {
            auto* host   = static_cast<ENetHost*>(enet_host_);
            auto* packet = makePacket(buf);
            enet_host_broadcast(host, CHAN_SNAPSHOT, packet);
        }
        // Don't also push to the local deque -- the host already has the source
        // truth from sim.buildSnapshot(); it doesn't need to drain its own
        // broadcasts.
        return;
    }
    // Clients drop their own sendSnapshot calls (only the host authors state). The
    // skeleton-fallback in-memory queue path is only meaningful when CR_NETWORK is
    // off, so we fall through to the deque push below in that case.
#endif
    snapshots_.push_back(std::move(snap));
}

bool NetworkTransport::pollSnapshot(Snapshot& out) {
    if (snapshots_.empty()) return false;
    out = std::move(snapshots_.front());
    snapshots_.pop_front();
    return true;
}

void NetworkTransport::sendEvent(GameEvent ev) {
#ifdef CR_NETWORK
    if (role_ == Role::Host && enet_host_ != nullptr) {
        std::vector<uint8_t> buf;
        if (codec::encodeEvent(ev, buf)) {
            auto* host   = static_cast<ENetHost*>(enet_host_);
            auto* packet = makePacket(buf);
            enet_host_broadcast(host, CHAN_EVENT, packet);
        }
        return;
    }
#endif
    events_.push_back(std::move(ev));
}

bool NetworkTransport::pollEvent(GameEvent& out) {
    if (events_.empty()) return false;
    out = std::move(events_.front());
    events_.pop_front();
    return true;
}

bool NetworkTransport::pollNewPeer(PeerHandle& out) {
    if (new_peers_.empty()) return false;
    out = new_peers_.front();
    new_peers_.pop_front();
    return true;
}

bool NetworkTransport::pollDepartedPeer(PeerHandle& out) {
    if (departed_peers_.empty()) return false;
    out = departed_peers_.front();
    departed_peers_.pop_front();
    return true;
}

void NetworkTransport::disconnectPeer(PeerHandle peer) {
#ifdef CR_NETWORK
    if (role_ != Role::Host)  return;
    if (!peer.isValid())      return;
    auto* ep = static_cast<ENetPeer*>(peer.enet_peer);
    enet_peer_disconnect(ep, 0);
    std::printf("[net] kicked peer (graceful disconnect requested)\n");
    // The DISCONNECT event will land in poll() shortly and queue this peer
    // into departed_peers_, where the host loop will pick it up and despawn
    // the leaving player's cells.
#else
    (void)peer;
#endif
}

void NetworkTransport::sendWelcomeTo(PeerHandle peer, const codec::WelcomeMsg& msg) {
#ifdef CR_NETWORK
    if (!peer.isValid()) return;
    if (role_ != Role::Host) return;
    std::vector<uint8_t> buf;
    if (!codec::encodeWelcome(msg, buf)) return;
    auto* ep = static_cast<ENetPeer*>(peer.enet_peer);
    enet_peer_send(ep, CHAN_CONTROL, makePacket(buf));
    std::printf("[net] sent welcome to peer (player_id=%u cell_id=%u)\n",
                static_cast<unsigned>(msg.player_id),
                static_cast<unsigned>(msg.cell_id));
#else
    (void)peer;
    // Without ENet, push the welcome straight into the local welcomes_ queue so a
    // same-process test (or a future loopback transport) can still observe it.
    welcomes_.push_back(msg);
#endif
}

bool NetworkTransport::consumeWelcome(codec::WelcomeMsg& out) {
    if (welcomes_.empty()) return false;
    out = welcomes_.front();
    welcomes_.pop_front();
    return true;
}

} // namespace cr
