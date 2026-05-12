#pragma once

#include <cmath>
#include <cstdint>

namespace cr {

using EntityId = uint32_t;
using PlayerId = uint16_t;
using Tick     = uint32_t;

constexpr EntityId INVALID_ENTITY = 0;
constexpr PlayerId INVALID_PLAYER = 0;

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

} // namespace cr
