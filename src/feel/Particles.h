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

    // Continuous "sucked-in bubble" spawned at a random angle on the pull-ring edge,
    // velocity pointing at the black hole's centre. Caller invokes this once per
    // frame per black hole; the particle's lifetime is sized so it visually
    // reaches the centre around the time it dies (size shrinks to zero).
    void spawnBlackHoleBubble(Vec2 center, float ring_radius);

    // One-shot shockwave: an expanding ring of particles flying outward at constant
    // speed for the duration of the ring, plus a central bright burst. Used by the
    // 4th-ability (Mass Blast).
    void spawnBlastBurst(Vec2 center, float radius, Color color);

    // Fiery ember bubbles shed by the crashing-comet as it travels. Each call spawns
    // a small burst of short-lived particles around the comet's surface with
    // velocities pointing outward (sparks flying off) plus a small backward drift so
    // they appear to peel off the trail. Caller invokes once per frame per active
    // comet; the shader trail covers the body of the streak, this layer adds the
    // motion-blur shimmer of particles being shed mid-flight.
    void spawnCometEmber(Vec2 pos, Vec2 vel, float radius);

    size_t poolSize() const { return pool_size_; }
    size_t liveCount() const;

private:
    void spawn(Vec2 pos, Vec2 vel, float lifetime,
               Color c_start, Color c_end, float size_start, float size_end,
               Vec2 target = Vec2{0.0f, 0.0f}, float accel = 0.0f);
    float frand();              // [0, 1)
    float frand(float lo, float hi);

    size_t              pool_size_;
    size_t              next_  = 0;
    uint64_t            rng_   = 0xC0FFEE12345678ull;

    // SoA storage. target_ + accel_ are gravity-mode fields: when accel_[i] > 0, the
    // particle accelerates from rest toward target_[i] (no drag). When accel_[i] == 0
    // the particle uses the classic linear-velocity-with-drag motion. Both groups
    // share the same pool slots.
    std::vector<Vec2>   pos_;
    std::vector<Vec2>   vel_;
    std::vector<float>  age_;
    std::vector<float>  lifetime_;
    std::vector<Color>  c0_, c1_;
    std::vector<float>  s0_, s1_;
    std::vector<Vec2>   target_;
    std::vector<float>  accel_;
};

} // namespace cr
