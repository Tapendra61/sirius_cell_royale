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

    bool operator==(const CellSnap&) const = default;
};

struct FoodSnap {
    EntityId id = INVALID_ENTITY;
    Vec2     pos;

    bool operator==(const FoodSnap&) const = default;
};

struct Snapshot {
    Tick                  tick      = 0;
    uint64_t              rng_state = 0;
    std::vector<CellSnap> cells;
    std::vector<FoodSnap> food;

    bool operator==(const Snapshot&) const = default;
};

} // namespace cr
