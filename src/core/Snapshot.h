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
    // Phase 5: bot personality (0 = human/unknown; 1..5 = personality enum + 1).
    uint8_t  personality_tag     = 0;

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

struct Snapshot {
    Tick                   tick      = 0;
    uint64_t               rng_state = 0;
    std::vector<CellSnap>  cells;
    std::vector<FoodSnap>  food;
    std::vector<VirusSnap> viruses;

    bool operator==(const Snapshot&) const = default;
};

} // namespace cr
