#pragma once

#include "core/Rng.h"
#include "core/Types.h"

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
// Entities are inserted into every bucket their AABB overlaps; queries scan only buckets
// touched by the query AABB. Result IDs come back in deterministic order (bucket-major,
// then insertion order within each bucket).
class SpatialGrid {
public:
    SpatialGrid(int world_w, int world_h, int bucket_size);

    void clear();
    void insert(EntityId id, Vec2 pos, float radius);
    void query(Vec2 min, Vec2 max, std::vector<EntityId>& out) const;

    int bucketSize() const { return bucket_size_; }
    int cols() const { return cols_; }
    int rows() const { return rows_; }

private:
    int                                bucket_size_;
    int                                cols_;
    int                                rows_;
    std::vector<std::vector<EntityId>> buckets_;
};

class World {
public:
    World(uint64_t seed, int width, int height, int bucket_size = 400);

    EntityId spawnCell(PlayerId owner, Vec2 pos, float mass);
    EntityId spawnFood(Vec2 pos);
    EntityId spawnFood(Vec2 pos, float mass, Vec2 vel, PlayerId from_player);
    EntityId spawnVirus(Vec2 pos, float mass);

    Cell*  findCell(EntityId id);
    Food*  findFood(EntityId id);
    Virus* findVirus(EntityId id);

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

    void               rebuildGrid();
    const SpatialGrid& grid() const { return grid_; }

private:
    int                width_;
    int                height_;
    Rng                rng_;
    Tick               tick_     = 0;
    EntityId           next_id_  = 1;
    std::vector<Cell>  cells_;
    std::vector<Food>  food_;
    std::vector<Virus> viruses_;
    SpatialGrid        grid_;
};

} // namespace cr
