#pragma once

#include "Types.h"

#include <variant>

namespace cr {

struct AbsorbEvent {
    EntityId predator    = INVALID_ENTITY;
    EntityId prey        = INVALID_ENTITY;
    Vec2     at;
    float    mass_gained = 0.0f;
};

struct DeathEvent {
    PlayerId player = INVALID_PLAYER;
    EntityId by     = INVALID_ENTITY;
};

struct SplitEvent {
    PlayerId player = INVALID_PLAYER;
    EntityId from   = INVALID_ENTITY;
    EntityId into   = INVALID_ENTITY;
};

struct CritEvent {
    EntityId predator    = INVALID_ENTITY;
    Vec2     at;
    float    mass_gained = 0.0f;
};

struct NearMissEvent {
    EntityId hunter = INVALID_ENTITY;
    EntityId prey   = INVALID_ENTITY;
    Vec2     at;
};

using GameEvent = std::variant<AbsorbEvent, DeathEvent, SplitEvent, CritEvent, NearMissEvent>;

} // namespace cr
