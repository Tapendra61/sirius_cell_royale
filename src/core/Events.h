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

// World-event comet: fires once when the telegraph starts (so the client can play
// the warning stinger + HUD text), again when the comet becomes active, and once more
// when it leaves the map (in case the client wants a fadeout cue). The same struct
// covers all three via `phase`.
struct CometEvent {
    enum Phase : uint8_t {
        Telegraph = 0, // warning window started
        Active    = 1, // comet became visible / dangerous
        Despawn   = 2, // left the world
    };
    EntityId id    = INVALID_ENTITY;
    Phase    phase = Phase::Telegraph;
    Vec2     at;             // current comet position (or telegraph start)
    Vec2     dir;             // unit direction (zero during Despawn)
};

using GameEvent = std::variant<AbsorbEvent, DeathEvent, SplitEvent, CritEvent,
                               NearMissEvent, PickupCollectedEvent, BlastEvent,
                               CometEvent>;

} // namespace cr
