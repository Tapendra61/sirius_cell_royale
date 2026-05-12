#include "World.h"

#include <algorithm>

namespace cr {

// ---- SpatialGrid ----

SpatialGrid::SpatialGrid(int world_w, int world_h, int bucket_size)
    : bucket_size_(bucket_size),
      cols_((world_w + bucket_size - 1) / bucket_size),
      rows_((world_h + bucket_size - 1) / bucket_size),
      buckets_(static_cast<size_t>(cols_) * static_cast<size_t>(rows_)) {}

void SpatialGrid::clear() {
    for (auto& b : buckets_) b.clear();
}

void SpatialGrid::insert(EntityId id, Vec2 pos, float radius) {
    int cx0 = std::max(0, static_cast<int>((pos.x - radius) / bucket_size_));
    int cy0 = std::max(0, static_cast<int>((pos.y - radius) / bucket_size_));
    int cx1 = std::min(cols_ - 1, static_cast<int>((pos.x + radius) / bucket_size_));
    int cy1 = std::min(rows_ - 1, static_cast<int>((pos.y + radius) / bucket_size_));
    for (int y = cy0; y <= cy1; ++y) {
        for (int x = cx0; x <= cx1; ++x) {
            buckets_[static_cast<size_t>(y) * cols_ + x].push_back(id);
        }
    }
}

void SpatialGrid::query(Vec2 min, Vec2 max, std::vector<EntityId>& out) const {
    int cx0 = std::max(0, static_cast<int>(min.x / bucket_size_));
    int cy0 = std::max(0, static_cast<int>(min.y / bucket_size_));
    int cx1 = std::min(cols_ - 1, static_cast<int>(max.x / bucket_size_));
    int cy1 = std::min(rows_ - 1, static_cast<int>(max.y / bucket_size_));
    for (int y = cy0; y <= cy1; ++y) {
        for (int x = cx0; x <= cx1; ++x) {
            const auto& bucket = buckets_[static_cast<size_t>(y) * cols_ + x];
            out.insert(out.end(), bucket.begin(), bucket.end());
        }
    }
}

// ---- World ----

World::World(uint64_t seed, int width, int height, int bucket_size)
    : width_(width),
      height_(height),
      rng_(seed),
      grid_(width, height, bucket_size) {}

EntityId World::spawnCell(PlayerId owner, Vec2 pos, float mass) {
    Cell c;
    c.id     = next_id_++;
    c.owner  = owner;
    c.pos    = pos;
    c.target = pos;
    c.mass   = mass;
    cells_.push_back(c);
    return c.id;
}

EntityId World::spawnFood(Vec2 pos) {
    Food f;
    f.id  = next_id_++;
    f.pos = pos;
    food_.push_back(f);
    return f.id;
}

Cell* World::findCell(EntityId id) {
    for (auto& c : cells_) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

Food* World::findFood(EntityId id) {
    for (auto& f : food_) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

void World::rebuildGrid() {
    grid_.clear();
    // Insert cells first (larger radii, more buckets), then food (point-sized).
    for (const auto& c : cells_) {
        grid_.insert(c.id, c.pos, cellRadius(c.mass));
    }
    for (const auto& f : food_) {
        grid_.insert(f.id, f.pos, 1.0f);
    }
}

} // namespace cr
