#pragma once

#include "core/Command.h"
#include "core/Events.h"
#include "core/Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cr::codec {

// Wire-format versions. Bump the relevant constant whenever a field layout changes;
// decoders use them to reject incompatible packets cleanly. The three streams version
// independently because Snapshot is the largest (and most likely to evolve) and
// Command/Event are small enough that a bump rarely costs anything.
constexpr uint8_t kSnapshotVersion = 1;
constexpr uint8_t kCommandVersion  = 1;
constexpr uint8_t kEventVersion    = 1;

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

} // namespace cr::codec
