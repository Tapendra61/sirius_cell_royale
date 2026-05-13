#pragma once

#include "core/Rng.h"
#include "core/Types.h"

#include <cstdint>
#include <vector>

namespace cr {

struct Cell {
    EntityId id     = INVALID_ENTITY;
    PlayerId owner  = INVALID_PLAYER;
    Vec2     pos;
    Vec2     vel;
    Vec2     target;
    Vec2     launch_vel{};               // additive velocity from splits / dashes; decays each tick
    float    mass   = 0.0f;
    Tick     dash_until           = 0;   // sim-tick when dash speed ends
    Tick     invuln_until         = 0;   // sim-tick when invuln frames end
    Tick     dash_cooldown_until  = 0;   // sim-tick when dash is usable again
    Tick     recombine_at         = 0;   // earliest tick this cell can merge with same-owner
    bool     god                  = false; // dev: never gets absorbed
};

struct Food {
    EntityId id   = INVALID_ENTITY;
    Vec2     pos;
    Vec2     vel;                       // non-zero while a pellet is in flight
    float    mass = 1.0f;
    PlayerId from_player = INVALID_PLAYER;
};

struct Virus {
    EntityId id   = INVALID_ENTITY;
    Vec2     pos;
    Vec2     vel;                       // pushed by ejected mass
    float    mass = 200.0f;
};

// Uniform-grid spatial index. Bucket size = 2 * max expected entity radius (~400px default).
// Stores VECTOR INDICES (not entity ids) so callers can dereference directly without an
// id-to-index lookup. World owns three separate grids (one per type) so queries return
// only entities of the type you want.
//
// Results come back in deterministic order (bucket-major, then insertion order within
// each bucket). An entity that straddles bucket boundaries can appear multiple times in
// the query output; callers should be idempotent (e.g. use a dead-flag guard).
class SpatialGrid {
public:
    SpatialGrid(int world_w, int world_h, int bucket_size);

    void clear();
    void insert(uint32_t index, Vec2 pos, float radius);
    void query(Vec2 min, Vec2 max, std::vector<uint32_t>& out) const;

    int bucketSize() const { return bucket_size_; }
    int cols() const { return cols_; }
    int rows() const { return rows_; }

private:
    int                                bucket_size_;
    int                                cols_;
    int                                rows_;
    std::vector<std::vector<uint32_t>> buckets_;
};

class World {
public:
    World(uint64_t seed, int width, int height, int bucket_size = 400);

    EntityId spawnCell(PlayerId owner, Vec2 pos, float mass);
    EntityId spawnFood(Vec2 pos);
    EntityId spawnFood(Vec2 pos, float mass, Vec2 vel, PlayerId from_player);
    EntityId spawnVirus(Vec2 pos, float mass);

    Cell*        findCell(EntityId id);
    Food*        findFood(EntityId id);
    Virus*       findVirus(EntityId id);
    const Cell*  findCell(EntityId id) const;
    const Food*  findFood(EntityId id) const;
    const Virus* findVirus(EntityId id) const;

    const std::vector<Cell>&  cells() const { return cells_; }
    const std::vector<Food>&  food() const { return food_; }
    const std::vector<Virus>& viruses() const { return viruses_; }

    std::vector<Cell>&  cellsMut() { return cells_; }
    std::vector<Food>&  foodMut() { return food_; }
    std::vector<Virus>& virusesMut() { return viruses_; }

    int playerCellCount(PlayerId p) const;

    Rng&       rng() { return rng_; }
    const Rng& rng() const { return rng_; }

    Tick currentTick() const { return tick_; }
    void advanceTick() { ++tick_; }

    int width() const { return width_; }
    int height() const { return height_; }

    // Rebuild all three typed spatial grids from the current cell/food/virus arrays.
    // Callers should invoke this AFTER motion has settled but BEFORE collision-driven
    // interactions that need spatial queries. Grids store vector indices, so they're
    // invalidated by any subsequent push_back / compactDead -- don't query after those.
    void               rebuildGrids();
    const SpatialGrid& cellsGrid()   const { return cells_grid_; }
    const SpatialGrid& foodsGrid()   const { return foods_grid_; }
    const SpatialGrid& virusesGrid() const { return viruses_grid_; }

private:
    int                width_;
    int                height_;
    Rng                rng_;
    Tick               tick_     = 0;
    EntityId           next_id_  = 1;
    std::vector<Cell>  cells_;
    std::vector<Food>  food_;
    std::vector<Virus> viruses_;
    SpatialGrid        cells_grid_;
    SpatialGrid        foods_grid_;
    SpatialGrid        viruses_grid_;
};

} // namespace cr
