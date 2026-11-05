// Rigid bodies. Positions are in metres, y points up.
#pragma once

#include "shape.hpp"

namespace phys {

enum class BodyType : uint8_t {
    Static,     // never moves
    Kinematic,  // moves at a set velocity, ignores forces and impacts
    Dynamic,    // fully simulated
};

struct BodyDef {
    BodyType type = BodyType::Dynamic;
    Shape shape = makeCircle(0.5f);
    Vec2 pos;
    float angle = 0.0f;
    Vec2 vel;
    float angVel = 0.0f;

    float density = 1.0f;
    float friction = 0.5f;
    float restitution = 0.1f;
    float linearDamping = 0.0f;
    float angularDamping = 0.05f;
    float gravityScale = 1.0f;

    int group = 0;          // bodies sharing a non-zero group never collide
    bool collides = true;
    bool canSleep = true;

    // Appearance hints, ignored by the simulation.
    uint32_t color = 0xFFFFFF;  // 0xRRGGBB
    float glow = 0.0f;          // emissive strength
    bool visible = true;
    bool trail = false;         // always leave a light trail, whatever the speed
};

struct Body {
    bool alive = false;
    uint32_t serial = 0;  // unique per spawned body, survives slot reuse
    BodyType type = BodyType::Dynamic;
    Shape shape;

    Vec2 pos;
    float angle = 0.0f;
    Rot q;
    Vec2 vel;
    float angVel = 0.0f;

    Vec2 force;           // accumulated for the current step
    float torque = 0.0f;

    float mass = 0.0f;
    float invMass = 0.0f;
    float inertia = 0.0f;
    float invInertia = 0.0f;
    float friction = 0.5f;
    float restitution = 0.1f;
    float linearDamping = 0.0f;
    float angularDamping = 0.05f;
    float gravityScale = 1.0f;

    int group = 0;
    bool collides = true;
    bool canSleep = true;

    bool awake = true;
    float sleepTime = 0.0f;

    // Displacement since the start of the step, used to refresh contact separation.
    Vec2 dp;
    Rot dq;
    float startAngle = 0.0f;

    // Pose at the previous step, used to interpolate rendering between steps.
    Vec2 prevPos;
    float prevAngle = 0.0f;

    // Appearance hints.
    uint32_t color = 0xFFFFFF;
    float glow = 0.0f;
    float flash = 0.0f;  // decaying impact highlight, owned by the renderer
    bool visible = true;
    bool trail = false;

    Transform transform() const { return {pos, q}; }
    bool isDynamic() const { return type == BodyType::Dynamic; }
    Vec2 velocityAt(Vec2 worldPoint) const { return vel + cross(angVel, worldPoint - pos); }
};

}  // namespace phys
