#pragma once

#include "ITransport.h"

#include <cstdint>
#include <deque>
#include <string>

namespace cr {

// Skeleton class for the upcoming LAN multiplayer transport. The intent is:
//   * Host side: bind to a UDP port, accept incoming peers, forward inbound Commands
//     into pollCommand() and broadcast outbound Snapshots/Events to all peers.
//   * Client side: connect to a host's UDP endpoint, send local Commands, and pump
//     received Snapshots/Events through pollSnapshot()/pollEvent().
//
// In this skeleton everything is local in-process: data goes into deques and stays
// there, exactly like LocalTransport. The class exists now so the rest of the codebase
// can be coded against its API (Host / Client modes in runMatch, lobby plumbing in
// LocalLobby) without waiting for the real net implementation.
//
// Wiring plan when this becomes real:
//   - host(port): bind ENet host, listen for peer connections
//   - connect(addr, port): create ENet client, establish session
//   - sendCommand: host -> queue locally; client -> serialise + enet_packet_send
//   - pollCommand: drain inbound packet queue (host only)
//   - sendSnapshot/Event: host -> serialise + broadcast; client -> queue locally
//   - pollSnapshot/Event: drain inbound queue (client only)
//
// Determinism: snapshots include `rng_state`, so clients can verify they're in sync
// with the host's authoritative sim. Out-of-band desync detection lives in a future
// `SyncVerifier` companion.
class NetworkTransport : public ITransport {
public:
    enum class Role {
        Idle,    // not yet host/connected
        Host,
        Client,
    };

    NetworkTransport();
    ~NetworkTransport() override;

    NetworkTransport(const NetworkTransport&)            = delete;
    NetworkTransport& operator=(const NetworkTransport&) = delete;

    // --- Lifecycle ---

    // Bind a host on the given UDP port. Returns false if the port can't be bound.
    // SKELETON: no-op that sets role + port; always returns true.
    bool host(uint16_t port);

    // Establish a client connection to a host endpoint. `address` is dotted-quad +
    // optional ":port" (e.g. "192.168.1.42:7456"). Default port used if omitted.
    // SKELETON: no-op that records the target; always returns true.
    bool connect(const std::string& address);

    // Tear down the host listener / client connection. Safe to call multiple times.
    void disconnect();

    // Role + connection inspectors used by the lobby UI and runMatch.
    Role               role()       const { return role_; }
    bool               isHosting()  const { return role_ == Role::Host; }
    bool               isClient()   const { return role_ == Role::Client; }
    bool               isIdle()     const { return role_ == Role::Idle; }
    uint16_t           hostPort()   const { return host_port_; }
    const std::string& clientPeer() const { return client_peer_; }

    // Number of remote peers currently connected. SKELETON: always 0.
    int peerCount() const { return peer_count_; }

    // Per-frame pump. Real impl: drain the OS socket / ENet event queue, enqueue
    // received Commands/Snapshots/Events into the internal deques. Skeleton no-op.
    void poll();

    // --- ITransport ---
    void sendCommand(Command cmd) override;
    bool pollCommand(Command& out) override;

    void sendSnapshot(Snapshot snap) override;
    bool pollSnapshot(Snapshot& out) override;

    void sendEvent(GameEvent ev) override;
    bool pollEvent(GameEvent& out) override;

private:
    Role        role_         = Role::Idle;
    uint16_t    host_port_    = 0;
    std::string client_peer_;
    int         peer_count_   = 0;

    // Local queues. Real impl will replace these with serialised network packets, but
    // the interface stays identical so the rest of the codebase doesn't have to care.
    std::deque<Command>   commands_;
    std::deque<Snapshot>  snapshots_;
    std::deque<GameEvent> events_;
};

} // namespace cr
