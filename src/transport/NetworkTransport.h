#pragma once

#include "Codec.h"  // codec::WelcomeMsg
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

    // Client-side: true once a DISCONNECT event has fired for the host peer
    // we were connected to (host quit, network drop, kicked, etc.). The client
    // match loop polls this and returns to the lobby cleanly when set. Reset
    // by the next connect() / disconnect() / host() call.
    bool hostDisconnected() const { return host_disconnected_; }

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

    // --- Handshake (host <-> just-connected client) ---

    // Opaque per-peer handle. Identifies a remote peer in the host-side flow so
    // the host can send messages addressed to a single connection. Returned by
    // pollNewPeer / consumed by sendWelcomeTo. Comparing handles by `enet_peer`
    // pointer is stable for the lifetime of the connection.
    struct PeerHandle {
        void* enet_peer = nullptr;
        bool  isValid() const { return enet_peer != nullptr; }
    };

    // Host-side: drains the queue of peers that connected since the last call.
    // Returns true and fills `out` when a new peer was popped; false when the
    // queue is empty. The host loop calls this each frame to learn about new
    // joiners and spawn their cell + send their welcome.
    bool pollNewPeer(PeerHandle& out);

    // Host-side: drains the queue of peers that disconnected since the last
    // call. The host loop uses this to despawn the leaving peer's cells + free
    // their PlayerId slot. NB: the PeerHandle in `out` references an ENetPeer
    // that's already gone -- don't try to send to it; the pointer is just a
    // stable identifier for the host's peer_to_player map.
    bool pollDepartedPeer(PeerHandle& out);

    // Host-side: ship the welcome message to a specific peer. No-op (or queue
    // for loopback) if !CR_NETWORK.
    void sendWelcomeTo(PeerHandle peer, const codec::WelcomeMsg& msg);

    // Host-side: send a PeerInfoMsg (PlayerId + display name) to a specific
    // peer. Used right after sending Welcome so the new peer learns the names
    // of every existing peer.
    void sendPeerInfoTo(PeerHandle peer, const codec::PeerInfoMsg& msg);

    // Host-side: broadcast a PeerInfoMsg to ALL connected peers (used when a
    // new peer's name arrives via ClientHello).
    void broadcastPeerInfo(const codec::PeerInfoMsg& msg);

    // Client-side: send the local player's display name to the host. Called
    // once right after the client consumes the Welcome.
    void sendClientHelloToHost(const codec::ClientHelloMsg& msg);

    // Host-side: gracefully disconnect a specific peer (used by `kick`). The
    // peer's DISCONNECT event will land in pollDepartedPeer's queue just like
    // a peer that closed their own window. No-op when !CR_NETWORK or the
    // handle is null.
    void disconnectPeer(PeerHandle peer);

    // Client-side: returns true exactly once after a welcome arrives from the
    // host, filling `out`. Subsequent calls return false until another welcome
    // is received (which shouldn't happen in the normal single-handshake flow).
    bool consumeWelcome(codec::WelcomeMsg& out);

    // Host-side: drain the queue of ClientHelloMsg received from peers. Each
    // call returns the next pending one. The host uses these to register the
    // peer's display name and then broadcast a PeerInfo to everyone. The
    // hello arrives anonymously (we know which peer via the surrounding
    // network event flow -- callers correlate by recency).
    bool pollClientHello(codec::ClientHelloMsg& out);

    // Client-side: drain peer-info messages from the host. Each carries a
    // PlayerId + display name; the client maintains its own map.
    bool pollPeerInfo(codec::PeerInfoMsg& out);

private:
    Role        role_              = Role::Idle;
    uint16_t    host_port_         = 0;
    std::string client_peer_;
    int         peer_count_        = 0;
    bool        host_disconnected_ = false; // client-side: host DISCONNECT seen

    // In-memory pumps. With CR_NETWORK off these are the entire pipeline (used by
    // unit tests / single-process scenarios). With CR_NETWORK on, sendCommand on a
    // client serialises + transmits and ALSO pushes to commands_ if the client is
    // looping packets back (currently we don't); pollSnapshot on a client drains
    // packets that poll() already decoded into snapshots_. Same with events_.
    std::deque<Command>   commands_;
    std::deque<Snapshot>  snapshots_;
    std::deque<GameEvent> events_;

    // ENet handles are intentionally opaque void* in this header so we don't leak
    // <enet/enet.h> into every translation unit that #includes NetworkTransport.h.
    // The real impl in NetworkTransport.cpp casts these back to ENetHost* /
    // ENetPeer*. Null when CR_NETWORK is undefined OR before host()/connect().
    void* enet_host_  = nullptr;  // ENetHost*  (server listener OR client socket)
    void* enet_peer_  = nullptr;  // ENetPeer*  (client only -- the host peer)

    // Handshake queues. new_peers_ collects host-side CONNECT events for the
    // host loop to drain; departed_peers_ collects DISCONNECT events; welcomes_
    // collects client-side decoded codec::WelcomeMsgs.
    // client_hellos_ are received on the host; peer_infos_ on the client.
    std::deque<PeerHandle>             new_peers_;
    std::deque<PeerHandle>             departed_peers_;
    std::deque<codec::WelcomeMsg>      welcomes_;
    std::deque<codec::ClientHelloMsg>  client_hellos_;
    std::deque<codec::PeerInfoMsg>     peer_infos_;
};

} // namespace cr
