#pragma once

#include "Types.h"

#include <cstdint>
#include <vector>

namespace cr {

struct CellSnap {
    EntityId id    = INVALID_ENTITY;
    PlayerId owner = INVALID_PLAYER;
    Vec2     pos;
    Vec2     vel;
    float    mass = 0.0f;
    // Phase 4 visual flags. Don't break replay determinism comparison (bool eq is bit eq).
    bool     invuln              = false;
    bool     dashing             = false;
    bool     god                 = false;
    float    dash_cooldown_norm  = 1.0f; // 0 = just used, 1 = ready
    float    blast_cooldown_norm = 1.0f; // 0 = just used, 1 = ready (Q ability)
    // Phase 5: bot personality (0 = human/unknown; 1..5 = personality enum + 1).
    uint8_t  personality_tag     = 0;
    // Phase 6: Hunter dash-windup intensity (0 = not winding up, 1 = dash imminent).
    float    dash_telegraph_norm = 0.0f;
    // Phase 6: bot was scaled-up at spawn ("elite"); renderer draws a pulsing halo.
    bool     is_elite            = false;
    // Power-up effect flags + countdowns. The renderer draws an aura per active effect
    // and the AI / eating rules consult these. Norm fields are 0..1 (1 = full).
    bool     shield_active       = false;
    bool     magnet_active       = false;
    bool     stealth_active      = false;
    float    shield_norm         = 0.0f;
    float    magnet_norm         = 0.0f;
    float    stealth_norm        = 0.0f;
    // Black-hole hiding state. `hiding` is true only when fully hidden (mid-entry
    // and mid-exit animations are NOT hiding so the renderer still draws them).
    // `blackhole_visual_scale` drives a cell-radius scaling: 1 = full size (open
    // world), 0 = invisible (deep inside the hole), values in between during the
    // entry/exit lerp animation.
    bool     hiding                 = false;
    EntityId hiding_in_id           = INVALID_ENTITY;
    float    blackhole_stamina_norm = 1.0f;
    float    blackhole_visual_scale = 1.0f;

    bool operator==(const CellSnap&) const = default;
};

struct FoodSnap {
    EntityId id   = INVALID_ENTITY;
    Vec2     pos;
    Vec2     vel;           // non-zero while a pellet is in flight
    float    mass = 1.0f;

    bool operator==(const FoodSnap&) const = default;
};

struct VirusSnap {
    EntityId id   = INVALID_ENTITY;
    Vec2     pos;
    float    mass = 0.0f;

    bool operator==(const VirusSnap&) const = default;
};

struct PickupSnap {
    EntityId   id   = INVALID_ENTITY;
    Vec2       pos;
    PickupKind kind = PickupKind::None;

    bool operator==(const PickupSnap&) const = default;
};

struct BlackHoleSnap {
    EntityId id          = INVALID_ENTITY;
    Vec2     pos;
    float    radius      = 0.0f;
    float    pull_radius = 0.0f;
    // Number of cells currently hiding inside, surfaced so the renderer can draw a
    // small "occupied" tell on the vortex without scanning cells itself.
    uint8_t  occupancy   = 0;

    bool operator==(const BlackHoleSnap&) const = default;
};

// Periodic world-event comet. `telegraph_norm` is 0..1 across the warning window: 0 at
// spawn, 1 when the comet becomes active (and then stays 1 until despawn). The renderer
// uses telegraph_norm < 1 to draw the predicted path line + fade the comet's own
// visibility in.
struct CometSnap {
    EntityId     id      = INVALID_ENTITY;
    Vec2         pos;
    Vec2         vel;
    float        radius  = 0.0f;
    Vec2         telegraph_start;
    Vec2         telegraph_end;
    float        telegraph_norm = 1.0f; // 0..1; 1 = active (no longer telegraphed)
    CometVariant variant        = CometVariant::Orange;

    bool operator==(const CometSnap&) const = default;
};

// Horizontal tidal-current band. Spans the full world width at a fixed
// y-coordinate (`pos.y` is the band centre); `pos.x` is the band's world-x
// centroid for renderer convenience but the physics treats the band as
// infinitely wide. `half_height` is the vertical reach above/below pos.y --
// any cell whose y falls inside `[pos.y - half_height, pos.y + half_height]`
// is in the band and gets a velocity bias along `dir` (always ±x for these
// horizontal bands). Force scales inversely with sqrt(mass) so small cells
// are swept and mega-cells barely feel it.
struct CurrentSnap {
    EntityId id     = INVALID_ENTITY;
    Vec2     pos;
    Vec2     dir;             // unit vector, direction of flow (±x for bands)
    float    half_height = 0.0f; // vertical reach above/below pos.y
    float    strength    = 0.0f; // velocity bias applied per second to a 1.0-mass cell

    bool operator==(const CurrentSnap&) const = default;
};

// Linked teleport pair. Two snap entries share the same `pair_id` so the
// renderer can draw both endpoints with matching colors and so the host can
// look up "the other end" in O(N) once per teleport. Cells entering within
// `radius` are warped to the partner if their per-cell wormhole cooldown is
// elapsed (see Cell::wormhole_cooldown_until).
struct WormholeSnap {
    EntityId id      = INVALID_ENTITY;
    EntityId pair_id = INVALID_ENTITY;
    Vec2     pos;
    float    radius  = 0.0f;
    // Slow per-endpoint rotation phase. Driven on the host so all clients see
    // the same swirl angle (no client-side `GetTime()` drift between peers).
    float    spin_phase = 0.0f;

    bool operator==(const WormholeSnap&) const = default;
};

// Periodic food-eruption geyser. Stationary; cycles between a quiet phase, a
// telegraphed warm-up, and a one-tick eruption that spawns a burst of food.
// `state` mirrors the sim's runtime state so the renderer doesn't have to
// guess from cooldowns:
//   0 = Idle (quiet, between eruptions)
//   1 = Telegraphing (warning ring growing; eruption imminent)
//   2 = Erupting (this tick spawned food -- typically held for one snapshot
//                 frame so clients see the flash, then immediately resets)
//
// `phase_norm` is 0..1 across whichever phase is active. For Idle it's the
// fraction of the inter-eruption interval elapsed; for Telegraphing it's the
// fraction of the warning window elapsed; for Erupting it's always 1.
struct GeyserSnap {
    EntityId id          = INVALID_ENTITY;
    Vec2     pos;
    float    radius      = 0.0f;
    uint8_t  state       = 0;   // 0=Idle, 1=Telegraph, 2=Erupt
    float    phase_norm  = 0.0f;

    bool operator==(const GeyserSnap&) const = default;
};

struct Snapshot {
    Tick                       tick      = 0;
    uint64_t                   rng_state = 0;
    std::vector<CellSnap>      cells;
    std::vector<FoodSnap>      food;
    std::vector<VirusSnap>     viruses;
    std::vector<PickupSnap>    pickups;
    std::vector<BlackHoleSnap> blackholes;
    std::vector<CometSnap>     comets;
    std::vector<CurrentSnap>   currents;
    std::vector<WormholeSnap>  wormholes;
    std::vector<GeyserSnap>    geysers;
    // Seconds remaining in the current match. 0 when no match timer is
    // active (Tuning::match_duration_sec == 0). Negative-clamped: once the
    // match has ended the value reads 0, never goes below.
    float                      match_time_left_sec = 0.0f;
    // Seconds remaining in the active Food Rush world event (3x mass on
    // every food pellet eaten). 0 = no rush active. Renderer reads this to
    // pulse all food gold for the remaining window; client also derives
    // the banner-hold duration from it on join-late so the announcement
    // doesn't pop up if the rush is already halfway done.
    float                      food_rush_time_left_sec = 0.0f;

    bool operator==(const Snapshot&) const = default;
};

} // namespace cr
