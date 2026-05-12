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

using CommandPayload = std::variant<MoveCmd, SplitCmd, EjectCmd, DashCmd>;

enum class CommandTag : uint8_t {
    Move  = 0,
    Split = 1,
    Eject = 2,
    Dash  = 3,
};

struct Command {
    PlayerId       player = INVALID_PLAYER;
    Tick           tick   = 0;
    CommandPayload payload;

    bool operator==(const Command&) const = default;
};

} // namespace cr
