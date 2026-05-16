#pragma once

#include "core/Command.h"
#include "core/Events.h"
#include "core/Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cr::codec {

// Wire-format versions. Bump the relevant constant whenever a field layout changes;
// decoders use them to reject incompatible packets cleanly. Each stream versions
// independently because Snapshot is the largest (and most likely to evolve) and
// Command/Event/Welcome/ClientHello/PeerInfo are small enough that a bump rarely
// costs anything.
constexpr uint8_t kSnapshotVersion    = 4; // v2 adds CellSnap::blast_cooldown_norm
                                            // v3 adds Snapshot::match_time_left_sec
                                            // v4 adds currents / wormholes / geysers
constexpr uint8_t kCommandVersion     = 1;
constexpr uint8_t kEventVersion       = 2; // v2 adds RecombineEvent variant
constexpr uint8_t kWelcomeVersion     = 2; // v2 adds host_name
constexpr uint8_t kClientHelloVersion = 2; // v2 adds explicit player_id
constexpr uint8_t kPeerInfoVersion    = 1;

// Type discriminator for messages on CHAN_CONTROL. Each control packet begins
// with one of these bytes so the receiver can dispatch to the right decoder.
enum class ControlMsgType : uint8_t {
    Welcome     = 0, // host -> just-connected-peer
    ClientHello = 1, // peer -> host (peer's display name)
    PeerInfo    = 2, // host -> all peers (PlayerId + display name)
};

// Maximum on-the-wire player-name length. Mirrors kMaxPlayerNameLen in
// meta/SaveFile.h; defined here too so the transport layer doesn't need to
// pull in meta/.
constexpr uint8_t kMaxWirePlayerNameLen = 16;

// Host -> just-connected-peer handshake. Carries the player slot the host has
// allocated for this peer + the EntityId of the cell the host spawned for them.
// v2 also includes the HOST's display name so the joining peer immediately knows
// who's hosting (their killfeed / leaderboard / nameplate uses it). The joining
// peer's own name + any *other* peers' names arrive via PeerInfoMsg messages.
struct WelcomeMsg {
    PlayerId    player_id = INVALID_PLAYER;
    EntityId    cell_id   = INVALID_ENTITY;
    std::string host_name;
};

// Client -> host. Carries the joining peer's display name (from SaveData::
// player_name in Settings) and the PlayerId the host allocated to it in the
// preceding Welcome (so the host doesn't have to attribute the hello to
// "first peer without a name" -- that heuristic broke if two peers connected
// in the same tick). Sent once immediately after the client consumes the
// Welcome message. Host registers + broadcasts via PeerInfoMsg.
struct ClientHelloMsg {
    PlayerId    player_id = INVALID_PLAYER;
    std::string name;
};

// Host -> all peers. Announces (or updates) a player's display name. Sent at
// least once per peer: right after the host receives ClientHelloMsg for that
// peer, the host broadcasts a PeerInfo to everyone. Also sent for existing
// peers when a new peer joins, so the new peer learns everyone's names without
// having to wait for the existing peers to re-Hello.
struct PeerInfoMsg {
    PlayerId    player_id = INVALID_PLAYER;
    std::string name;
};

// Byte-buffer codec for the three stream types that flow over the wire between host
// and clients:
//
//   - Snapshot: authoritative world state, host -> all clients, every sim tick.
//   - Command : input intent, client -> host (player wants to move/split/etc.).
//   - GameEvent: gameplay notifications, host -> all clients (absorb sting, blast
//                shockwave, comet spawn, etc.). Decoupled from Snapshot so clients
//                can play SFX/particles without waiting for the next snapshot.
//
// All three functions assume little-endian hosts (x86, ARM-LE). On big-endian targets
// the byte layout would need explicit swapping; not currently a concern for any of
// our build targets.
//
// `encode*` always clears `out` and writes a self-describing blob (version byte + fields).
// `decode*` returns false on buffer underrun OR version mismatch; the output is left
// in a partially-filled state and should not be used.
bool encodeSnapshot(const Snapshot& s, std::vector<uint8_t>& out);
bool decodeSnapshot(const uint8_t* data, size_t len, Snapshot& out);

bool encodeCommand(const Command& c, std::vector<uint8_t>& out);
bool decodeCommand(const uint8_t* data, size_t len, Command& out);

bool encodeEvent(const GameEvent& e, std::vector<uint8_t>& out);
bool decodeEvent(const uint8_t* data, size_t len, GameEvent& out);

bool encodeWelcome(const WelcomeMsg& m, std::vector<uint8_t>& out);
bool decodeWelcome(const uint8_t* data, size_t len, WelcomeMsg& out);

bool encodeClientHello(const ClientHelloMsg& m, std::vector<uint8_t>& out);
bool decodeClientHello(const uint8_t* data, size_t len, ClientHelloMsg& out);

bool encodePeerInfo(const PeerInfoMsg& m, std::vector<uint8_t>& out);
bool decodePeerInfo(const uint8_t* data, size_t len, PeerInfoMsg& out);

} // namespace cr::codec
