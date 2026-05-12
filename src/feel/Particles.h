#pragma once

#include "core/Types.h"
#include "raylib.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cr {

// Fixed-size particle pool. SoA layout so update() can stream through positions /
// velocities / ages without thrashing the cache. Spawning round-robins through slots
// (oldest expire first); a single-tick burst of N particles overwrites the oldest N
// slots. Pool size comes from tuning.particle_pool_size_desktop / _mobile.
//
// Pure client-side: uses its own RNG (not the sim's), so spawns don't affect determinism.
class ParticleSystem {
public:
    explicit ParticleSystem(size_t pool_size = 4096);

    void update(float frame_dt);
    void draw() const; // call inside BeginMode2D — particles live in world space

    // Spawn templates -- each maps a sim event to a visual burst.
    void spawnAbsorbBurst(Vec2 pos, float mass_gained, Color color);
    void spawnDeathBurst(Vec2 pos, float mass, Color color);
    void spawnSplitPuff(Vec2 pos, Color color);
    void spawnNearMissSparks(Vec2 pos, Color color);
    void spawnDashTrail(Vec2 pos, Vec2 vel, Color color);

    size_t poolSize() const { return pool_size_; }
    size_t liveCount() const;

private:
    void spawn(Vec2 pos, Vec2 vel, float lifetime,
               Color c_start, Color c_end, float size_start, float size_end);
    float frand();              // [0, 1)
    float frand(float lo, float hi);

    size_t              pool_size_;
    size_t              next_  = 0;
    uint64_t            rng_   = 0xC0FFEE12345678ull;

    // SoA storage.
    std::vector<Vec2>   pos_;
    std::vector<Vec2>   vel_;
    std::vector<float>  age_;
    std::vector<float>  lifetime_;
    std::vector<Color>  c0_, c1_;
    std::vector<float>  s0_, s1_;
};

} // namespace cr
