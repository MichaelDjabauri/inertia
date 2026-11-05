// Joints: pins (optionally motorised and limited), rods, ropes, springs and the
// soft point constraint behind mouse dragging.
#pragma once

#include "body.hpp"

namespace phys {

// Coefficients of a soft constraint, the implicit form of a damped spring.
struct Softness {
    float biasRate = 0.0f;
    float massScale = 1.0f;
    float impulseScale = 0.0f;
};

Softness makeSoft(float hertz, float dampingRatio, float h);

enum class JointType : uint8_t { Revolute, Distance, Mouse };

struct Joint {
    JointType type = JointType::Revolute;
    bool alive = false;
    int a = -1;
    int b = -1;
    Vec2 localA;
    Vec2 localB;
    bool collideConnected = false;
    bool visible = true;

    // Distance. `hertz > 0` makes a spring, `rope` only resists stretching.
    float length = 1.0f;
    float hertz = 0.0f;
    float dampingRatio = 0.7f;
    bool rope = false;

    // Revolute.
    bool motor = false;
    float motorSpeed = 0.0f;
    float maxMotorTorque = 0.0f;
    bool limit = false;
    float lower = 0.0f;
    float upper = 0.0f;
    float referenceAngle = 0.0f;

    // Mouse. Anchored to `target` rather than to a second body.
    Vec2 target;
    float maxForce = 0.0f;

    // Accumulated impulses for warm starting.
    Vec2 impulse;
    float axialImpulse = 0.0f;
    float motorImpulse = 0.0f;
    float lowerImpulse = 0.0f;
    float upperImpulse = 0.0f;
};

void warmStartJoint(Joint& joint, Body& a, Body& b);
void solveJoint(Joint& joint, Body& a, Body& b, float h, bool useBias, const Softness& soft);

}  // namespace phys
