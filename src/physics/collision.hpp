// Narrow phase: turns a pair of shapes into a contact manifold.
#pragma once

#include "shape.hpp"

namespace phys {

struct ManifoldPoint {
    Vec2 point;        // world-space contact point
    float separation;  // positive while the shapes are still apart (speculative)
    uint32_t id;       // stable feature id, used to carry impulses between steps
};

struct Manifold {
    Vec2 normal;  // unit normal pointing from shape A towards shape B
    ManifoldPoint points[2];
    int count = 0;
};

// Fills `out` if the shapes are closer than `margin`. A positive margin yields
// speculative contacts, which is how fast bodies are stopped before they tunnel.
bool collide(const Shape& a, const Transform& xfA, const Shape& b, const Transform& xfB,
             float margin, Manifold& out);

}  // namespace phys
