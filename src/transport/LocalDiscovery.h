#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace cr {

// Lightweight LAN discovery layer that sits *next to* NetworkTransport. Uses a
// separate UDP socket on a fixed broadcast port (kDiscoveryPort below) so the
// game's reliable ENet host doesn't have to handle stateless probes.
//
// Two modes (mutually exclusive, switched via startHost / startClient / stop):
//
//   Host: opens a UDP socket with SO_BROADCAST and periodically sends an
//         announce packet to 255.255.255.255. Carries the game's port so the
//         client can connect to the right ENet host on the same machine.
//
//   Client: opens a UDP socket bound to kDiscoveryPort, reads announce packets,
//           dedupes by source address+game_port, and surfaces them through
//           pollDiscovered().
//
// Cross-platform via ENet's socket primitives (enet_socket_*); no per-platform
// #ifdef. Discovery is intentionally unreliable -- a lost announce just means
// the next 1s tick repopulates the list.
//
// Port range: we try kDiscoveryPortBase first, then +1 / +2 as fallbacks so a
// single port being taken by a background daemon doesn't break discovery
// entirely. Host broadcasts to ALL three each announce; client listens on
// whichever it could successfully bind. Avoid 7457 because at least one
// commonly-installed macOS daemon already holds it (mDNSResponder relatives,
// some screen-sharing apps).
constexpr uint16_t kDiscoveryPortBase  = 47457;
constexpr int      kDiscoveryPortCount = 3; // tries base, base+1, base+2

struct DiscoveredHostEntry {
    std::string address;     // dotted-quad of the sender (e.g. "192.168.1.42")
    uint16_t    game_port;   // ENet host port the joiner should connect to
    double      last_seen;   // monotonic seconds (host-side wall clock)
    char        name[32];    // human-readable label (host writes "Player <N>'s game")
};

class LocalDiscovery {
public:
    LocalDiscovery();
    ~LocalDiscovery();

    LocalDiscovery(const LocalDiscovery&)            = delete;
    LocalDiscovery& operator=(const LocalDiscovery&) = delete;

    enum class Mode { Idle, Host, Client };

    // Start broadcasting announces (Host mode). `game_port` is the ENet game
    // socket the joiners should connect to. `display_name` is what the JOIN
    // screen shows in its list (truncated to 31 chars). Returns false if the
    // socket couldn't be created.
    bool startHost(uint16_t game_port, const std::string& display_name);

    // Start listening for announces (Client mode). Returns false on bind
    // failure (e.g., another listener already owns kDiscoveryPort on this
    // machine -- which is expected in the loopback two-instance flow if both
    // instances are JOIN screens; harmless because each instance only needs
    // one role at a time anyway).
    bool startClient();

    void stop();

    Mode mode() const { return mode_; }

    // Host mode: send one announce packet now. Caller drives the cadence
    // (typically ~1s from the match loop). No-op when not in Host mode.
    void announceNow();

    // Client mode: drain any received announce packets, dedupe by (address,
    // game_port). Caller drives the cadence (typically every frame on the
    // JOIN screen) and supplies `now_sec` so the timestamp stored on each
    // entry shares a clock with `getKnownHosts(now_sec, ...)`. Previously
    // pollIncoming used `enet_time_get()/1000` internally while getKnownHosts
    // used raylib's `GetTime()` from the caller -- a small but non-zero
    // skew between the two clocks could prematurely mark fresh entries as
    // stale, causing the JOIN list to flicker / stay empty even with live
    // announces in flight.
    // No-op when not in Client mode.
    void pollIncoming(double now_sec);

    // Client mode: read the current list of known hosts. Entries get dropped
    // from the underlying list once they're more than `staleAfterSec` old.
    // The vector is rebuilt per call to keep this side simple; expect <10
    // entries on a typical LAN.
    void getKnownHosts(std::vector<DiscoveredHostEntry>& out,
                       double now_sec,
                       double stale_after_sec = 5.0);

private:
    Mode mode_ = Mode::Idle;
    // Opaque ENet socket handle. Stored as int64_t so the header doesn't have
    // to include <enet/enet.h> -- the .cpp casts back to ENetSocket (which is
    // an int on POSIX, a SOCKET on Windows; both fit in int64_t).
    int64_t  socket_      = -1;
    uint16_t game_port_   = 0;
    char     name_[32]    = {0};

    // Last-known-good list. Updated by pollIncoming(); pruned by getKnownHosts.
    std::deque<DiscoveredHostEntry> hosts_;
};

} // namespace cr
