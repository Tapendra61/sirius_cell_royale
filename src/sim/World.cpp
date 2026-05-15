#include "World.h"

#include <algorithm>
#include <cstdint>

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

void SpatialGrid::insert(uint32_t index, Vec2 pos, float radius) {
    int cx0 = std::max(0, static_cast<int>((pos.x - radius) / bucket_size_));
    int cy0 = std::max(0, static_cast<int>((pos.y - radius) / bucket_size_));
    int cx1 = std::min(cols_ - 1, static_cast<int>((pos.x + radius) / bucket_size_));
    int cy1 = std::min(rows_ - 1, static_cast<int>((pos.y + radius) / bucket_size_));
    for (int y = cy0; y <= cy1; ++y) {
        for (int x = cx0; x <= cx1; ++x) {
            buckets_[static_cast<size_t>(y) * cols_ + x].push_back(index);
        }
    }
}

void SpatialGrid::query(Vec2 min, Vec2 max, std::vector<uint32_t>& out) const {
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
      cells_grid_(width, height, bucket_size),
      foods_grid_(width, height, bucket_size),
      viruses_grid_(width, height, bucket_size) {}

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
    return spawnFood(pos, 1.0f, Vec2{0.0f, 0.0f}, INVALID_PLAYER);
}

EntityId World::spawnFood(Vec2 pos, float mass, Vec2 vel, PlayerId from_player) {
    Food f;
    f.id          = next_id_++;
    f.pos         = pos;
    f.vel         = vel;
    f.mass        = mass;
    f.from_player = from_player;
    food_.push_back(f);
    return f.id;
}

EntityId World::spawnVirus(Vec2 pos, float mass) {
    Virus v;
    v.id   = next_id_++;
    v.pos  = pos;
    v.mass = mass;
    viruses_.push_back(v);
    return v.id;
}

EntityId World::spawnPickup(Vec2 pos, PickupKind kind) {
    Pickup p;
    p.id   = next_id_++;
    p.pos  = pos;
    p.kind = kind;
    pickups_.push_back(p);
    return p.id;
}

EntityId World::spawnBlackHole(Vec2 pos, float radius, float pull_radius) {
    BlackHole b;
    b.id          = next_id_++;
    b.pos         = pos;
    b.radius      = radius;
    b.pull_radius = pull_radius;
    blackholes_.push_back(b);
    return b.id;
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

Virus* World::findVirus(EntityId id) {
    for (auto& v : viruses_) {
        if (v.id == id) return &v;
    }
    return nullptr;
}

const Cell* World::findCell(EntityId id) const {
    for (const auto& c : cells_) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

const Food* World::findFood(EntityId id) const {
    for (const auto& f : food_) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

const Virus* World::findVirus(EntityId id) const {
    for (const auto& v : viruses_) {
        if (v.id == id) return &v;
    }
    return nullptr;
}

Pickup* World::findPickup(EntityId id) {
    for (auto& p : pickups_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const Pickup* World::findPickup(EntityId id) const {
    for (const auto& p : pickups_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

BlackHole* World::findBlackHole(EntityId id) {
    for (auto& b : blackholes_) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

const BlackHole* World::findBlackHole(EntityId id) const {
    for (const auto& b : blackholes_) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

int World::playerCellCount(PlayerId p) const {
    int n = 0;
    for (const auto& c : cells_) if (c.owner == p) ++n;
    return n;
}

void World::rebuildGrids() {
    cells_grid_.clear();
    foods_grid_.clear();
    viruses_grid_.clear();
    for (size_t i = 0; i < cells_.size(); ++i) {
        cells_grid_.insert(static_cast<uint32_t>(i),
                           cells_[i].pos, cellRadius(cells_[i].mass));
    }
    for (size_t i = 0; i < food_.size(); ++i) {
        foods_grid_.insert(static_cast<uint32_t>(i),
                           food_[i].pos, foodRadius(food_[i].mass));
    }
    for (size_t i = 0; i < viruses_.size(); ++i) {
        viruses_grid_.insert(static_cast<uint32_t>(i),
                             viruses_[i].pos, cellRadius(viruses_[i].mass));
    }
}

} // namespace cr
