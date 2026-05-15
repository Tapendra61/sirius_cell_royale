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
    EntityId id     = INVALID_ENTITY;
    Vec2     pos;
    Vec2     vel;
    float    radius = 0.0f;
    Vec2     telegraph_start;
    Vec2     telegraph_end;
    float    telegraph_norm = 1.0f; // 0..1; 1 = active (no longer telegraphed)

    bool operator==(const CometSnap&) const = default;
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

    bool operator==(const Snapshot&) const = default;
};

} // namespace cr
