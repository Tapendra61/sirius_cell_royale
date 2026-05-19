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

// Match-end event: emitted once by the authoritative sim when the configured
// match duration elapses (Tuning::match_duration_sec). The "winner" is the
// player with the highest total cell mass at the time of the call. Clients use
// this to transition into the MatchEnd phase and show the winner overlay.
//
// reason is a small enum so future end-conditions (first-to-N-mass, last-cell-
// standing, etc.) slot in without breaking the wire format.
struct MatchEndEvent {
    enum Reason : uint8_t {
        TimeLimit = 0,
    };
    PlayerId winner_player = INVALID_PLAYER;
    float    winner_mass   = 0.0f;
    Reason   reason        = Reason::TimeLimit;
};

// Same-owner recombine event. Fired by processRecombine when two cells of
// the same player merge back together. Carries the merge position and the
// total mass of the result so clients can scale the visual / audio feel
// (bigger merges land harder). `player` lets the client tell "is this the
// watched player?" without a cell lookup.
struct RecombineEvent {
    PlayerId player    = INVALID_PLAYER;
    Vec2     at;
    float    new_mass  = 0.0f; // total mass of the merged cell after recombine
};

// Food-rush world event: emitted by the authoritative sim when the rare
// "3x mass on every food pellet" event begins or ends. Client uses Start to
// fire the golden announcement banner + chime sound; End to fade the banner.
// During the rush the per-tick Snapshot also carries food_rush_time_left_sec
// so a client that misses the Start event (transport loss, late join) still
// renders the golden food pulse for the remaining window.
struct FoodRushEvent {
    enum Phase : uint8_t {
        Start = 0, // rush just began (duration_sec valid)
        End   = 1, // rush just ended (duration_sec = 0)
    };
    Phase phase        = Phase::Start;
    float duration_sec = 0.0f; // only meaningful for Start; banner uses this
                                // to decide how long to hold before fading.
};

using GameEvent = std::variant<AbsorbEvent, DeathEvent, SplitEvent, CritEvent,
                               NearMissEvent, PickupCollectedEvent, BlastEvent,
                               CometEvent, MatchEndEvent, RecombineEvent,
                               FoodRushEvent>;

} // namespace cr
