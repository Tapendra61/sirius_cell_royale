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
    float    mass   = 0.0f;
};

struct Food {
    EntityId id = INVALID_ENTITY;
    Vec2     pos;
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
    int                                  bucket_size_;
    int                                  cols_;
    int                                  rows_;
    std::vector<std::vector<EntityId>>   buckets_;
};

class World {
public:
    World(uint64_t seed, int width, int height, int bucket_size = 400);

    EntityId spawnCell(PlayerId owner, Vec2 pos, float mass);
    EntityId spawnFood(Vec2 pos);

    Cell* findCell(EntityId id);
    Food* findFood(EntityId id);

    const std::vector<Cell>& cells() const { return cells_; }
    const std::vector<Food>& food() const { return food_; }
    std::vector<Cell>&       cellsMut() { return cells_; }
    std::vector<Food>&       foodMut() { return food_; }

    Rng&       rng() { return rng_; }
    const Rng& rng() const { return rng_; }

    Tick currentTick() const { return tick_; }
    void advanceTick() { ++tick_; }

    int width() const { return width_; }
    int height() const { return height_; }

    void               rebuildGrid();
    const SpatialGrid& grid() const { return grid_; }

private:
    int               width_;
    int               height_;
    Rng               rng_;
    Tick              tick_     = 0;
    EntityId          next_id_  = 1;
    std::vector<Cell> cells_;
    std::vector<Food> food_;
    SpatialGrid       grid_;
};

} // namespace cr
