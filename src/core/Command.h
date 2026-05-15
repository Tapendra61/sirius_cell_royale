#pragma once

#include "Types.h"

#include <variant>

namespace cr {

struct MoveCmd {
    Vec2 target;
    bool operator==(const MoveCmd&) const = default;
};

struct SplitCmd {
    bool operator==(const SplitCmd&) const = default;
};

struct EjectCmd {
    bool operator==(const EjectCmd&) const = default;
};

struct DashCmd {
    bool operator==(const DashCmd&) const = default;
};

struct BlastCmd {
    bool operator==(const BlastCmd&) const = default;
};

// Multiplayer client -> host respawn request. The host owns spawning, so a dead
// client can't just allocate a cell locally; it sends this command up and the host
// runs the spawn logic deterministically. SinglePlayer / LocalHost can either route
// their own respawn through this command (uniform code path) or do it inline.
struct RespawnCmd {
    bool operator==(const RespawnCmd&) const = default;
};

using CommandPayload =
    std::variant<MoveCmd, SplitCmd, EjectCmd, DashCmd, BlastCmd, RespawnCmd>;

enum class CommandTag : uint8_t {
    Move    = 0,
    Split   = 1,
    Eject   = 2,
    Dash    = 3,
    Blast   = 4,
    Respawn = 5,
};

struct Command {
    PlayerId       player = INVALID_PLAYER;
    Tick           tick   = 0;
    CommandPayload payload;

    bool operator==(const Command&) const = default;
};

} // namespace cr
