#include "Particles.h"

#include <algorithm>
#include <cmath>

namespace cr {

namespace {

constexpr float kPi = 3.14159265358979323846f;

Color blendColor(Color a, Color b, float t) {
    return Color{
        static_cast<unsigned char>(a.r + (b.r - a.r) * t),
        static_cast<unsigned char>(a.g + (b.g - a.g) * t),
        static_cast<unsigned char>(a.b + (b.b - a.b) * t),
        static_cast<unsigned char>(a.a + (b.a - a.a) * t),
    };
}

} // namespace

ParticleSystem::ParticleSystem(size_t pool_size)
    : pool_size_(pool_size),
      pos_(pool_size),
      vel_(pool_size),
      age_(pool_size, 1.0f),
      lifetime_(pool_size, 0.0f),
      c0_(pool_size),
      c1_(pool_size),
      s0_(pool_size, 0.0f),
      s1_(pool_size, 0.0f),
      target_(pool_size),
      accel_(pool_size, 0.0f) {}

float ParticleSystem::frand() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 7;
    rng_ ^= rng_ << 17;
    return static_cast<float>((rng_ >> 40) & 0xFFFFFF) / static_cast<float>(1 << 24);
}

float ParticleSystem::frand(float lo, float hi) {
    return lo + frand() * (hi - lo);
}

void ParticleSystem::spawn(Vec2 pos, Vec2 vel, float lifetime,
                           Color c_start, Color c_end, float size_start, float size_end,
                           Vec2 target, float accel) {
    const size_t i = next_;
    next_ = (next_ + 1) % pool_size_;
    pos_[i]      = pos;
    vel_[i]      = vel;
    age_[i]      = 0.0f;
    lifetime_[i] = lifetime;
    c0_[i]       = c_start;
    c1_[i]       = c_end;
    s0_[i]       = size_start;
    s1_[i]       = size_end;
    target_[i]   = target;  // (0,0) for linear particles
    accel_[i]    = accel;   // 0 for linear particles
}

void ParticleSystem::update(float frame_dt) {
    for (size_t i = 0; i < pool_size_; ++i) {
        if (age_[i] >= lifetime_[i]) continue;
        age_[i] += frame_dt;
        if (accel_[i] > 0.0f) {
            // Gravity mode: accelerate toward target from rest, no drag. Velocity
            // builds up over time, so the particle starts slow and ends fast --
            // a classic "sucked into the singularity" arc.
            float dx = target_[i].x - pos_[i].x;
            float dy = target_[i].y - pos_[i].y;
            float dsq = dx * dx + dy * dy;
            if (dsq > 1.0f) {
                float d    = std::sqrt(dsq);
                float dirx = dx / d;
                float diry = dy / d;
                vel_[i].x += dirx * accel_[i] * frame_dt;
                vel_[i].y += diry * accel_[i] * frame_dt;
            }
            pos_[i].x += vel_[i].x * frame_dt;
            pos_[i].y += vel_[i].y * frame_dt;
        } else {
            // Linear mode: constant velocity with per-frame soft drag.
            pos_[i].x += vel_[i].x * frame_dt;
            pos_[i].y += vel_[i].y * frame_dt;
            vel_[i].x *= 0.94f;
            vel_[i].y *= 0.94f;
        }
    }
}

void ParticleSystem::draw() const {
    for (size_t i = 0; i < pool_size_; ++i) {
        if (age_[i] >= lifetime_[i]) continue;
        float t = age_[i] / lifetime_[i];
        if (t > 1.0f) t = 1.0f;
        float size = s0_[i] + (s1_[i] - s0_[i]) * t;
        Color c    = blendColor(c0_[i], c1_[i], t);
        if (size > 0.5f && c.a > 0) {
            DrawCircleV(Vector2{pos_[i].x, pos_[i].y}, size, c);
        }
    }
}

size_t ParticleSystem::liveCount() const {
    size_t n = 0;
    for (size_t i = 0; i < pool_size_; ++i) {
        if (age_[i] < lifetime_[i]) ++n;
    }
    return n;
}

// ---- Spawn templates ----

void ParticleSystem::spawnAbsorbBurst(Vec2 pos, float mass_gained, Color color) {
    // Min 10 particles even for tiny food so it reads. Scale up to 36 for big absorbs.
    int count = static_cast<int>(std::min(36.0f, 10.0f + mass_gained * 0.3f));
    if (count < 10) count = 10;
    Color faded = color;
    faded.a = 0;
    for (int i = 0; i < count; ++i) {
        float angle = frand(0.0f, 2.0f * kPi);
        float speed = frand(80.0f, 260.0f);
        Vec2  vel{std::cos(angle) * speed, std::sin(angle) * speed};
        float life = frand(0.35f, 0.65f);
        float s0   = frand(5.0f, 9.0f);
        spawn(pos, vel, life, color, faded, s0, 1.0f);
    }
    // Bright white sparks layered on top -- short-lived, give the burst a visible "snap"
    // against any background regardless of predator/food color.
    int sparks = (mass_gained <= 5.0f) ? 3 : std::min(6, 2 + static_cast<int>(mass_gained * 0.04f));
    for (int i = 0; i < sparks; ++i) {
        float angle = frand(0.0f, 2.0f * kPi);
        float speed = frand(140.0f, 280.0f);
        Vec2  vel{std::cos(angle) * speed, std::sin(angle) * speed};
        spawn(pos, vel, frand(0.18f, 0.30f),
              Color{255, 255, 255, 230}, Color{255, 255, 255, 0},
              frand(4.0f, 6.5f), 0.5f);
    }
}

void ParticleSystem::spawnDeathBurst(Vec2 pos, float mass, Color color) {
    int count = static_cast<int>(std::min(80.0f, 12.0f + mass * 0.04f));
    Color faded = color;
    faded.a = 0;
    for (int i = 0; i < count; ++i) {
        float angle = frand(0.0f, 2.0f * kPi);
        float speed = frand(120.0f, 480.0f);
        Vec2  vel{std::cos(angle) * speed, std::sin(angle) * speed};
        float life = frand(0.55f, 1.10f);
        float s0   = frand(6.0f, 14.0f);
        spawn(pos, vel, life, color, faded, s0, 1.0f);
    }
}

void ParticleSystem::spawnSplitPuff(Vec2 pos, Color color) {
    Color faded = color;
    faded.a = 0;
    for (int i = 0; i < 16; ++i) {
        float angle = frand(0.0f, 2.0f * kPi);
        float speed = frand(180.0f, 320.0f);
        Vec2  vel{std::cos(angle) * speed, std::sin(angle) * speed};
        spawn(pos, vel, frand(0.25f, 0.45f), color, faded, frand(3.0f, 6.0f), 1.0f);
    }
}

void ParticleSystem::spawnNearMissSparks(Vec2 pos, Color color) {
    Color faded = color;
    faded.a = 0;
    for (int i = 0; i < 10; ++i) {
        float angle = frand(0.0f, 2.0f * kPi);
        float speed = frand(80.0f, 200.0f);
        Vec2  vel{std::cos(angle) * speed, std::sin(angle) * speed};
        spawn(pos, vel, frand(0.20f, 0.40f), color, faded, frand(3.0f, 5.0f), 0.5f);
    }
}

void ParticleSystem::spawnDashTrail(Vec2 pos, Vec2 vel, Color color) {
    Color faded = color;
    faded.a = 0;
    // Trail particles drift opposite to motion.
    Vec2 back{-vel.x * 0.3f, -vel.y * 0.3f};
    for (int i = 0; i < 4; ++i) {
        Vec2 jitter{frand(-30.0f, 30.0f), frand(-30.0f, 30.0f)};
        spawn(Vec2{pos.x + jitter.x * 0.2f, pos.y + jitter.y * 0.2f},
              Vec2{back.x + jitter.x, back.y + jitter.y},
              frand(0.20f, 0.40f), color, faded,
              frand(5.0f, 9.0f), 1.0f);
    }
}

void ParticleSystem::spawnBlastBurst(Vec2 center, float radius, Color color) {
    // 1) Outward ring -- 36 particles evenly distributed, all moving outward at a
    //    speed that lands them near `radius` over the lifetime. The drag in the
    //    linear path naturally decelerates them so the ring expands and slows.
    constexpr int   kRingCount = 36;
    const     float lifetime   = 0.55f;
    // distance ≈ initial_speed / 60 * (1 - 0.94^N) / 0.06 where N = lifetime * 60.
    // Empirically, initial_speed ≈ radius * 2.0 lands the ring near the desired
    // outer radius with the existing drag.
    const float ring_speed = radius * 2.0f;
    Color faded = color; faded.a = 0;
    for (int i = 0; i < kRingCount; ++i) {
        float a = (i * 2.0f * kPi) / kRingCount;
        Vec2  v{std::cos(a) * ring_speed, std::sin(a) * ring_speed};
        spawn(center, v, lifetime,
              Color{255, 255, 255, 230}, faded,
              /*size_start=*/6.0f, /*size_end=*/1.0f);
    }
    // 2) Central energy burst -- 24 particles flying outward at varied angles +
    //    speeds in the tinted player color.
    for (int i = 0; i < 24; ++i) {
        float a    = frand(0.0f, 2.0f * kPi);
        float spd  = frand(radius * 1.2f, radius * 2.5f);
        Vec2  v{std::cos(a) * spd, std::sin(a) * spd};
        spawn(center, v, frand(0.35f, 0.55f),
              color, faded,
              /*size_start=*/frand(5.0f, 9.0f), /*size_end=*/1.0f);
    }
}

void ParticleSystem::spawnBlackHoleBubble(Vec2 center, float ring_radius) {
    // Spawn just outside the pull-ring edge, at a random angle, with zero initial
    // velocity. The gravity branch in update() then accelerates the bubble toward
    // the centre -- slow at first, fast at the end, like proper infall physics.
    const float angle   = frand(0.0f, 2.0f * kPi);
    const float spawn_r = ring_radius * frand(0.92f, 1.05f);
    const Vec2  pos {center.x + std::cos(angle) * spawn_r,
                     center.y + std::sin(angle) * spawn_r};

    // Constant acceleration tuned so the bubble covers spawn_r in `lifetime`
    // seconds starting from rest: distance = 0.5 * a * t^2  =>  a = 2 d / t^2.
    // The 1.10 multiplier is a small overshoot so the bubble arrives a touch
    // past the centre (where the BH shader's pure black core hides it cleanly).
    const float lifetime = frand(1.30f, 1.70f);
    const float accel    = 1.10f * 2.0f * spawn_r / (lifetime * lifetime);

    // Dark red gradient with a true alpha-zero end so the bubble fades cleanly.
    Color c_start{
        static_cast<unsigned char>(160 + frand(-15.0f, 15.0f)),
        static_cast<unsigned char>( 25 + frand(-10.0f, 10.0f)),
        static_cast<unsigned char>( 40 + frand(-10.0f, 10.0f)),
        230};
    Color c_end{20, 0, 10, 0};

    spawn(pos, /*vel=*/Vec2{0.0f, 0.0f}, lifetime,
          c_start, c_end,
          /*size_start=*/frand(5.5f, 9.5f),
          /*size_end=*/1.0f,
          /*target=*/center,
          /*accel=*/accel);
}

void ParticleSystem::spawnCometTrail(Vec2 pos, Vec2 vel, float radius) {
    // Compute the unit "backwards" direction once so the trail streams behind the
    // head rather than around it.
    const float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
    Vec2  back        = (speed > 1.0f)
                         ? Vec2{-vel.x / speed, -vel.y / speed}
                         : Vec2{-1.0f, 0.0f};
    // Perp vector for lateral scatter (rotate `back` 90 degrees).
    const Vec2 perp{-back.y, back.x};

    // Spawn a small clump per call -- caller invokes once per frame, so 4-6 sparks
    // per frame at 60fps = a thick fiery trail without saturating the pool.
    constexpr int kPerCall = 5;
    for (int i = 0; i < kPerCall; ++i) {
        // Drop the emit point a little behind the head so the trail "starts" at the
        // back of the fireball and doesn't fight the shader's edge noise.
        const float drop_back = radius * frand(0.55f, 0.85f);
        const float lateral   = radius * frand(-0.45f, 0.45f);
        Vec2 spawn_pos{pos.x + back.x * drop_back + perp.x * lateral,
                       pos.y + back.y * drop_back + perp.y * lateral};

        // Particle drifts backward at a fraction of the comet's own speed plus a
        // small lateral component so the trail visibly billows.
        const float back_speed_scale = frand(0.25f, 0.55f);
        const float side_speed       = frand(-80.0f, 80.0f);
        Vec2 v{back.x * speed * back_speed_scale + perp.x * side_speed,
               back.y * speed * back_speed_scale + perp.y * side_speed};

        // Color: blazing yellow-orange at birth, deep red-grey at death. Random
        // brightness per spark gives the trail a flickery texture.
        const float bright = frand(0.85f, 1.0f);
        Color c_start{
            static_cast<unsigned char>(255 * bright),
            static_cast<unsigned char>((180 + frand(-25.0f, 25.0f)) * bright),
            static_cast<unsigned char>(( 60 + frand(-20.0f, 20.0f)) * bright),
            235};
        Color c_end{40, 8, 4, 0};

        const float lifetime = frand(0.40f, 0.80f);
        const float size0    = radius * frand(0.18f, 0.32f);
        const float size1    = radius * frand(0.02f, 0.06f);

        spawn(spawn_pos, v, lifetime, c_start, c_end, size0, size1);
    }
}

} // namespace cr
