#include "effects.hpp"

#include <algorithm>
#include <cmath>

namespace gfx {
namespace {

constexpr int kMaxSparks = 3000;
constexpr int kShortTrail = 26;  // length for bodies without the `trail` flag

float randomUnit(unsigned int& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>((seed >> 8) & 0xFFFFFF) / 16777216.0f;
}

}  // namespace

void Effects::clear() {
    sparks_.clear();
    waves_.clear();
}

void Effects::sparks(Vec2 point, Vec2 normal, float speed, const Col& colour, int count) {
    for (int i = 0; i < count && static_cast<int>(sparks_.size()) < kMaxSparks; ++i) {
        Spark s;
        const float side = randomUnit(seed_) * 2.0f - 1.0f;
        const float direction = randomUnit(seed_) < 0.5f ? -1.0f : 1.0f;
        const Vec2 dir = phys::normalized(normal * (direction * (0.5f + randomUnit(seed_))) +
                                          phys::perpLeft(normal) * (side * 1.3f));
        const float pace = speed * (0.12f + 0.4f * randomUnit(seed_));
        s.pos = point;
        s.vel = dir * pace;
        s.life = 0.2f + 0.4f * randomUnit(seed_);
        s.size = 0.5f + 0.7f * randomUnit(seed_);
        s.colour = mix(colour, {1.0f, 1.0f, 1.0f, 1.0f}, 0.1f + 0.4f * randomUnit(seed_));
        sparks_.push_back(s);
    }
}

void Effects::shockwave(Vec2 point, float radius, const Col& colour, float duration) {
    Wave w;
    w.pos = point;
    w.radius = radius;
    w.life = duration;
    w.colour = colour;
    waves_.push_back(w);
}

void Effects::update(float dt, Vec2 gravity) {
    size_t kept = 0;
    for (Spark& s : sparks_) {
        s.age += dt;
        if (s.age >= s.life) continue;
        s.vel += gravity * (0.6f * dt);
        s.vel *= 1.0f / (1.0f + 2.5f * dt);
        s.pos += s.vel * dt;
        sparks_[kept++] = s;
    }
    sparks_.resize(kept);

    kept = 0;
    for (Wave& w : waves_) {
        w.age += dt;
        if (w.age < w.life) waves_[kept++] = w;
    }
    waves_.resize(kept);
}

void Effects::draw(Canvas& canvas, const View& view, float dpi) const {
    for (const Spark& s : sparks_) {
        const float t = 1.0f - s.age / s.life;
        const Vec2 head = view.toPixels(s.pos);
        const Vec2 tail = view.toPixels(s.pos - s.vel * 0.035f);
        canvas.line(tail, head, s.size * dpi * t, withAlpha(s.colour, 0.75f * t), dpi);
    }
    for (const Wave& w : waves_) {
        const float t = w.age / w.life;
        const float eased = 1.0f - (1.0f - t) * (1.0f - t);
        const float fade = (1.0f - t) * (1.0f - t);
        const float radius = eased * w.radius * view.pixelsPerMetre;
        canvas.ring(view.toPixels(w.pos), radius, 3.0f * dpi * (1.0f - t) + 0.5f * dpi, withAlpha(w.colour, fade), 2.0f * dpi);
    }
}

void Trails::configure(int length, float minSpeed) {
    length_ = std::max(length, 2);
    minSpeed_ = minSpeed;
    tracks_.clear();
}

void Trails::clear() { tracks_.clear(); }

void Trails::sample(const phys::World& world) {
    const auto& bodies = world.bodies();
    if (tracks_.size() < bodies.size()) tracks_.resize(bodies.size());

    for (size_t i = 0; i < bodies.size(); ++i) {
        const phys::Body& b = bodies[i];
        Track& track = tracks_[i];
        if (!b.alive || !b.isDynamic() || !b.visible) {
            track.count = 0;
            track.serial = 0;
            continue;
        }
        if (track.serial != b.serial) {
            // A new body in a reused slot. Its trail may need a different length.
            track.serial = b.serial;
            track.count = 0;
            track.head = 0;
            track.samples.clear();
        }
        if (!b.awake && track.count == 0) continue;

        const float speed = phys::length(b.vel);
        const float energy = b.trail ? 1.0f : std::clamp((speed - minSpeed_) / 8.0f, 0.0f, 1.0f);
        if (energy <= 0.0f && track.count == 0) continue;

        if (track.samples.empty()) {
            // Only the flagged bodies need the long history.
            track.samples.resize(b.trail ? length_ : std::min(length_, kShortTrail));
        }
        const int capacity = static_cast<int>(track.samples.size());
        track.samples[track.head] = {b.pos, energy};
        track.head = (track.head + 1) % capacity;
        track.count = std::min(track.count + 1, capacity);
    }
}

void Trails::draw(Canvas& canvas, const phys::World& world, const View& view, float alpha, float dpi) {
    const auto& bodies = world.bodies();
    const size_t count = std::min(tracks_.size(), bodies.size());

    for (size_t i = 0; i < count; ++i) {
        const Track& track = tracks_[i];
        if (track.count < 3) continue;
        const phys::Body& b = bodies[i];
        if (!b.alive) continue;

        const int capacity = static_cast<int>(track.samples.size());
        const int newest = (track.head - 1 + capacity) % capacity;
        const Sample& latest = track.samples[newest];
        const Sample& middle = track.samples[(newest - track.count / 2 + capacity) % capacity];
        if (latest.energy <= 0.0f && middle.energy <= 0.0f && !b.trail) continue;

        const Col base = linearHex(b.color);
        const float radiusPixels = b.shape.boundRadius * view.pixelsPerMetre;
        const float headWidth = std::clamp(radiusPixels * 0.55f, 1.6f * dpi, 7.0f * dpi);

        points_.clear();
        widths_.clear();
        colours_.clear();

        // Start at the interpolated body position so the ribbon stays attached between steps.
        points_.push_back(view.toPixels(phys::lerp(b.prevPos, b.pos, alpha)));
        widths_.push_back(headWidth);
        colours_.push_back(withAlpha(base, latest.energy * 0.7f));

        const Vec2 cornerA{-200.0f, -200.0f};
        const Vec2 cornerB{view.width + 200.0f, view.height + 200.0f};
        bool anyVisible = false;

        for (int k = 0; k < track.count; ++k) {
            const Sample& s = track.samples[(newest - k + capacity) % capacity];
            const float age = static_cast<float>(k + 1) / static_cast<float>(track.count);
            const float life = 1.0f - age;
            const Vec2 p = view.toPixels(s.pos);
            if (p.x > cornerA.x && p.y > cornerA.y && p.x < cornerB.x && p.y < cornerB.y) anyVisible = true;
            points_.push_back(p);
            widths_.push_back(headWidth * (0.25f + 0.75f * life));
            colours_.push_back(withAlpha(base, s.energy * life * life * 0.7f));
        }
        if (anyVisible) {
            canvas.ribbon(points_.data(), widths_.data(), colours_.data(), static_cast<int>(points_.size()));
        }
    }
}

}  // namespace gfx
