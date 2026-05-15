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
    PlayerId player          = INVALID_PLAYER; // owner of the cell that just died
    EntityId by              = INVALID_ENTITY; // predator cell id (for death-cam focus)
    PlayerId predator_player = INVALID_PLAYER; // owner of `by` -- duplicated here so the
                                               // killfeed can read names without looking
                                               // up the predator cell (which may have
                                               // moved or been compacted by next read).
    uint8_t  prey_personality      = 0;        // personality_tag of the dead cell's bot
    uint8_t  predator_personality  = 0;        // personality_tag of the predator's bot
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

struct PickupCollectedEvent {
    EntityId   collector = INVALID_ENTITY;   // cell that consumed the pickup
    PlayerId   player    = INVALID_PLAYER;
    PickupKind kind      = PickupKind::None;
    Vec2       at;
};

// Mass-blast shockwave fired by a cell. The sim has already applied the radial push
// to nearby cells/food; the client uses this for particles + audio + screen shake.
struct BlastEvent {
    EntityId source = INVALID_ENTITY;
    PlayerId player = INVALID_PLAYER;
    Vec2     at;
    float    radius = 0.0f;
};

using GameEvent = std::variant<AbsorbEvent, DeathEvent, SplitEvent, CritEvent,
                               NearMissEvent, PickupCollectedEvent, BlastEvent>;

} // namespace cr
