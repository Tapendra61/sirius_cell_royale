#pragma once

#include <cmath>
#include <cstdint>

namespace cr {

using EntityId = uint32_t;
using PlayerId = uint16_t;
using Tick     = uint32_t;

constexpr EntityId INVALID_ENTITY = 0;
constexpr PlayerId INVALID_PLAYER = 0;

// Sim runs at this fixed rate. Used everywhere that converts between seconds and ticks.
constexpr float kSimHz = 30.0f;

inline Tick secondsToTicks(float seconds) {
    if (seconds <= 0.0f) return 0;
    return static_cast<Tick>(seconds * kSimHz + 0.5f);
}

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    bool operator==(const Vec2&) const = default;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline Vec2 operator*(float s, Vec2 a) { return {a.x * s, a.y * s}; }

inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float lengthSq(Vec2 v) { return dot(v, v); }
inline float length(Vec2 v) { return std::sqrt(lengthSq(v)); }
inline float distance(Vec2 a, Vec2 b) { return length(b - a); }

inline Vec2 normalize(Vec2 v) {
    float len = length(v);
    if (len < 1e-6f) return {0.0f, 0.0f};
    return {v.x / len, v.y / len};
}

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec2  lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }

// Cell radius derived from mass: at mass=100, radius=30 (matches tuning comment).
inline float cellRadius(float mass) { return 3.0f * std::sqrt(mass); }

// Power-up pickup kinds. Spawned sparsely across the world, consumed by overlap with
// a cell (like food). Each kind applies a timed effect via a corresponding `*_until`
// field on Cell.
enum class PickupKind : uint8_t {
    None    = 0,
    Shield  = 1, // brief invulnerability -- you can't be absorbed
    Magnet  = 2, // food within radius drifts toward you
    Stealth = 3, // bot AI stops targeting you (you read as a virus to them)
};

// Color variant tag for crashing-comet world events. Originally there was only
// one comet at a time, always orange/fire-tinted. The comet-shower event
// introduces formations of comets where the main one keeps the original look
// and the satellites use distinct red / blue palettes so the player + bots
// can read the spread of the formation at a glance.
//
// Wire format: 1 byte (uint8_t). Append-only; never reorder values. Unknown
// indices decode as Orange so old replays + forward-compat are safe.
enum class CometVariant : uint8_t {
    Orange = 0, // original fire-orange palette (single-comet event, shower's main comet)
    Red    = 1, // shower satellite variant -- crimson palette
    Blue   = 2, // shower satellite variant -- cobalt palette
};

constexpr int kPickupKindCount = 3;

// Pickup visual radius (same size for all three kinds so they read as one "category").
inline float pickupRadius() { return 18.0f; }

// Food radius -- discrete tiers so common food is uniform while bigger drops stand out
// without being so large they read as "tiny cell". Capped well below cell radius.
inline float foodRadius(float mass) {
    if (mass >= 30.0f) return 9.5f; // legendary food (mass 36, purple-red pulse)
    if (mass >= 15.0f) return 8.5f; // ejected pellets / settled pellets
    if (mass >= 8.0f)  return 6.5f; // epic food
    if (mass >= 4.0f)  return 5.5f; // rare food
    if (mass >= 2.0f)  return 5.0f; // uncommon food
    return 4.0f;                    // common food (mass 1)
}

} // namespace cr
