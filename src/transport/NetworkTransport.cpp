#include "NetworkTransport.h"

#include <utility>

// NOTE: Codec.h carries the encode/decode functions for Snapshot / Command /
// GameEvent. We don't pull it in here yet because the skeleton still uses in-memory
// queues (no serialisation needed). The TODO comments below mark the exact spots
// where codec::encodeSnapshot etc. plug in once ENet/UDP socket I/O lands.

namespace cr {

NetworkTransport::NetworkTransport()  = default;
NetworkTransport::~NetworkTransport() { disconnect(); }

bool NetworkTransport::host(uint16_t port) {
    // TODO(net): bind ENet host on `port`. Return false if the bind fails so the
    // lobby UI can surface "port in use" or similar. Skeleton just records the role
    // and reports success so callers can wire the rest of the flow.
    role_      = Role::Host;
    host_port_ = port;
    return true;
}

bool NetworkTransport::connect(const std::string& address) {
    // TODO(net): parse `address` as "host[:port]", resolve, open an ENet client
    // session against the resolved endpoint. Block briefly (or return a pending
    // handle) until the handshake completes.
    role_        = Role::Client;
    client_peer_ = address;
    return true;
}

void NetworkTransport::disconnect() {
    // TODO(net): cleanly close ENet host/client (peer disconnect with
    // ENET_RELIABLE then service for a brief grace window). Skeleton just resets
    // the role flag and drains the in-memory queues.
    role_        = Role::Idle;
    host_port_   = 0;
    peer_count_  = 0;
    client_peer_.clear();
    commands_.clear();
    snapshots_.clear();
    events_.clear();
}

void NetworkTransport::poll() {
    // TODO(net): drain ENet events:
    //   - ENET_EVENT_TYPE_CONNECT    -> ++peer_count_, push hello message
    //   - ENET_EVENT_TYPE_RECEIVE    -> route by stream type:
    //         hosts receive Commands  (codec::decodeCommand into commands_)
    //         clients receive Snapshots / Events (codec::decodeSnapshot/Event into
    //                                              snapshots_ / events_)
    //   - ENET_EVENT_TYPE_DISCONNECT -> --peer_count_
    // Skeleton: nothing to pump because nothing connects over the wire yet. The
    // codec is already in place (see Codec.cpp) so this slot just needs the
    // socket-event loop.
}

void NetworkTransport::sendCommand(Command cmd) {
    // Skeleton: same-process queue. Real impl:
    //   - if hosting  : keep this path (the host's own local-player input).
    //   - if a client : codec::encodeCommand + enet_peer_send to the host peer.
    commands_.push_back(std::move(cmd));
}

bool NetworkTransport::pollCommand(Command& out) {
    if (commands_.empty()) return false;
    out = std::move(commands_.front());
    commands_.pop_front();
    return true;
}

void NetworkTransport::sendSnapshot(Snapshot snap) {
    // Skeleton: same-process queue. Real impl (host only):
    //   codec::encodeSnapshot(snap, buf);
    //   for each connected peer: enet_peer_send(peer, packet_from(buf), CHAN_SNAP);
    // Clients ignore their own sendSnapshot calls (they don't author state).
    snapshots_.push_back(std::move(snap));
}

bool NetworkTransport::pollSnapshot(Snapshot& out) {
    if (snapshots_.empty()) return false;
    out = std::move(snapshots_.front());
    snapshots_.pop_front();
    return true;
}

void NetworkTransport::sendEvent(GameEvent ev) {
    // Skeleton: same-process queue. Real impl (host only):
    //   codec::encodeEvent(ev, buf);
    //   for each connected peer: enet_peer_send(peer, packet_from(buf), CHAN_EVENT);
    events_.push_back(std::move(ev));
}

bool NetworkTransport::pollEvent(GameEvent& out) {
    if (events_.empty()) return false;
    out = std::move(events_.front());
    events_.pop_front();
    return true;
}

} // namespace cr
