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
      s1_(pool_size, 0.0f) {}

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
                           Color c_start, Color c_end, float size_start, float size_end) {
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
}

void ParticleSystem::update(float frame_dt) {
    for (size_t i = 0; i < pool_size_; ++i) {
        if (age_[i] >= lifetime_[i]) continue;
        age_[i] += frame_dt;
        pos_[i].x += vel_[i].x * frame_dt;
        pos_[i].y += vel_[i].y * frame_dt;
        // Soft drag.
        vel_[i].x *= 0.94f;
        vel_[i].y *= 0.94f;
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

} // namespace cr
