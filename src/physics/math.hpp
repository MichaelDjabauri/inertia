// Small 2D math kit used by the physics core. No dependencies beyond the STL.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace phys {

constexpr float kPi = 3.14159265358979f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
};

constexpr Vec2 operator*(float s, Vec2 v) { return {s * v.x, s * v.y}; }

constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
constexpr float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
// Angular velocity crossed with a radius: w x r.
constexpr Vec2 cross(float w, Vec2 r) { return {-w * r.y, w * r.x}; }
constexpr float lengthSq(Vec2 v) { return dot(v, v); }
inline float length(Vec2 v) { return std::sqrt(lengthSq(v)); }

inline Vec2 normalized(Vec2 v, Vec2 fallback = {0.0f, 1.0f}) {
    const float len = length(v);
    return len > 1e-7f ? v / len : fallback;
}

constexpr Vec2 perpLeft(Vec2 v) { return {-v.y, v.x}; }
constexpr Vec2 perpRight(Vec2 v) { return {v.y, -v.x}; }

constexpr Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
constexpr float lerp(float a, float b, float t) { return a + (b - a) * t; }

// A rotation stored as its cosine and sine.
struct Rot {
    float c = 1.0f;
    float s = 0.0f;

    Rot() = default;
    explicit Rot(float angle) : c(std::cos(angle)), s(std::sin(angle)) {}
};

constexpr Vec2 rotate(Rot q, Vec2 v) { return {q.c * v.x - q.s * v.y, q.s * v.x + q.c * v.y}; }
constexpr Vec2 invRotate(Rot q, Vec2 v) { return {q.c * v.x + q.s * v.y, -q.s * v.x + q.c * v.y}; }

struct Transform {
    Vec2 p;
    Rot q;
};

constexpr Vec2 apply(const Transform& t, Vec2 v) { return rotate(t.q, v) + t.p; }
constexpr Vec2 applyInv(const Transform& t, Vec2 v) { return invRotate(t.q, v - t.p); }

struct AABB {
    Vec2 min;
    Vec2 max;
};

}  // namespace phys
