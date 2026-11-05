#include "joint.hpp"

namespace phys {

Softness makeSoft(float hertz, float dampingRatio, float h) {
    if (hertz <= 0.0f) return {};
    const float omega = 2.0f * kPi * hertz;
    const float a1 = 2.0f * dampingRatio + h * omega;
    const float a2 = h * omega * a1;
    const float a3 = 1.0f / (1.0f + a2);
    return {omega / a1, a2 * a3, a3};
}

namespace {

struct Mat2 {
    float a11, a12, a22;  // symmetric

    Vec2 solve(Vec2 b) const {
        float det = a11 * a22 - a12 * a12;
        det = det != 0.0f ? 1.0f / det : 0.0f;
        return {det * (a22 * b.x - a12 * b.y), det * (a11 * b.y - a12 * b.x)};
    }
};

Mat2 pointMass(const Body& a, const Body& b, Vec2 rA, Vec2 rB) {
    const float mA = a.invMass, mB = b.invMass, iA = a.invInertia, iB = b.invInertia;
    return {mA + mB + iA * rA.y * rA.y + iB * rB.y * rB.y,
            -iA * rA.y * rA.x - iB * rB.y * rB.x,
            mA + mB + iA * rA.x * rA.x + iB * rB.x * rB.x};
}

void applyLinear(Body& a, Body& b, Vec2 rA, Vec2 rB, Vec2 p) {
    a.vel -= a.invMass * p;
    a.angVel -= a.invInertia * cross(rA, p);
    b.vel += b.invMass * p;
    b.angVel += b.invInertia * cross(rB, p);
}

Vec2 relativeVelocity(const Body& a, const Body& b, Vec2 rA, Vec2 rB) {
    return (b.vel + cross(b.angVel, rB)) - (a.vel + cross(a.angVel, rA));
}

void solvePoint(Joint& j, Body& a, Body& b, Vec2 rA, Vec2 rB, Vec2 anchorError, bool useBias,
                const Softness& soft) {
    Vec2 bias;
    float massScale = 1.0f;
    float impulseScale = 0.0f;
    if (useBias) {
        bias = soft.biasRate * anchorError;
        massScale = soft.massScale;
        impulseScale = soft.impulseScale;
    }
    const Vec2 cdot = relativeVelocity(a, b, rA, rB);
    const Vec2 solved = pointMass(a, b, rA, rB).solve(cdot + bias);
    const Vec2 p = -massScale * solved - impulseScale * j.impulse;
    j.impulse += p;
    applyLinear(a, b, rA, rB, p);
}

void solveRevolute(Joint& j, Body& a, Body& b, Vec2 rA, Vec2 rB, float h, bool useBias,
                   const Softness& soft) {
    const float iA = a.invInertia, iB = b.invInertia;
    const float axialMass = iA + iB > 0.0f ? 1.0f / (iA + iB) : 0.0f;

    if (j.motor) {
        const float cdot = b.angVel - a.angVel - j.motorSpeed;
        const float maxImpulse = h * j.maxMotorTorque;
        const float previous = j.motorImpulse;
        j.motorImpulse = std::clamp(previous - axialMass * cdot, -maxImpulse, maxImpulse);
        const float delta = j.motorImpulse - previous;
        a.angVel -= iA * delta;
        b.angVel += iB * delta;
    }

    if (j.limit) {
        const float angle = (b.angle - a.angle) - j.referenceAngle;

        // Lower bound. Inside the range the limit only stops motion that would cross it.
        {
            const float c = angle - j.lower;
            float bias = 0.0f, massScale = 1.0f, impulseScale = 0.0f;
            if (c > 0.0f) {
                bias = c / h;
            } else if (useBias) {
                bias = soft.biasRate * c;
                massScale = soft.massScale;
                impulseScale = soft.impulseScale;
            }
            const float cdot = b.angVel - a.angVel;
            const float lambda = -axialMass * massScale * (cdot + bias) - impulseScale * j.lowerImpulse;
            const float previous = j.lowerImpulse;
            j.lowerImpulse = std::max(previous + lambda, 0.0f);
            const float delta = j.lowerImpulse - previous;
            a.angVel -= iA * delta;
            b.angVel += iB * delta;
        }
        // Upper bound, mirrored.
        {
            const float c = j.upper - angle;
            float bias = 0.0f, massScale = 1.0f, impulseScale = 0.0f;
            if (c > 0.0f) {
                bias = c / h;
            } else if (useBias) {
                bias = soft.biasRate * c;
                massScale = soft.massScale;
                impulseScale = soft.impulseScale;
            }
            const float cdot = a.angVel - b.angVel;
            const float lambda = -axialMass * massScale * (cdot + bias) - impulseScale * j.upperImpulse;
            const float previous = j.upperImpulse;
            j.upperImpulse = std::max(previous + lambda, 0.0f);
            const float delta = j.upperImpulse - previous;
            a.angVel += iA * delta;
            b.angVel -= iB * delta;
        }
    }

    solvePoint(j, a, b, rA, rB, (b.pos + rB) - (a.pos + rA), useBias, soft);
}

void solveDistance(Joint& j, Body& a, Body& b, Vec2 rA, Vec2 rB, float h, bool useBias,
                   const Softness& soft) {
    const Vec2 d = (b.pos + rB) - (a.pos + rA);
    const float len = length(d);
    const Vec2 axis = normalized(d);
    const float crA = cross(rA, axis);
    const float crB = cross(rB, axis);
    const float k = a.invMass + b.invMass + a.invInertia * crA * crA + b.invInertia * crB * crB;
    const float axialMass = k > 0.0f ? 1.0f / k : 0.0f;
    const float c = len - j.length;
    const float cdot = dot(axis, relativeVelocity(a, b, rA, rB));

    float bias = 0.0f, massScale = 1.0f, impulseScale = 0.0f;
    float lambda;

    if (j.hertz > 0.0f) {
        // A spring acts in both solver phases: its softness is the physics, not an artefact.
        const Softness spring = makeSoft(j.hertz, j.dampingRatio, h);
        bias = spring.biasRate * c;
        lambda = -axialMass * spring.massScale * (cdot + bias) - spring.impulseScale * j.axialImpulse;
        j.axialImpulse += lambda;
    } else if (j.rope) {
        if (c < 0.0f) {
            bias = c / h;  // slack: only stop motion that would overshoot the rope's length
        } else if (useBias) {
            bias = soft.biasRate * c;
            massScale = soft.massScale;
            impulseScale = soft.impulseScale;
        }
        lambda = -axialMass * massScale * (cdot + bias) - impulseScale * j.axialImpulse;
        const float previous = j.axialImpulse;
        j.axialImpulse = std::min(previous + lambda, 0.0f);
        lambda = j.axialImpulse - previous;
    } else {
        if (useBias) {
            bias = soft.biasRate * c;
            massScale = soft.massScale;
            impulseScale = soft.impulseScale;
        }
        lambda = -axialMass * massScale * (cdot + bias) - impulseScale * j.axialImpulse;
        j.axialImpulse += lambda;
    }

    applyLinear(a, b, rA, rB, lambda * axis);
}

void solveMouse(Joint& j, Body& b, Vec2 rB, float h) {
    const Softness soft = makeSoft(j.hertz, j.dampingRatio, h);
    const float mB = b.invMass, iB = b.invInertia;

    const Vec2 error = (b.pos + rB) - j.target;
    const Vec2 cdot = b.vel + cross(b.angVel, rB);
    const Mat2 k{mB + iB * rB.y * rB.y, -iB * rB.y * rB.x, mB + iB * rB.x * rB.x};
    const Vec2 solved = k.solve(cdot + soft.biasRate * error);

    const Vec2 previous = j.impulse;
    Vec2 next = previous - soft.massScale * solved - soft.impulseScale * previous;
    const float maxImpulse = h * j.maxForce;
    if (lengthSq(next) > maxImpulse * maxImpulse) next = normalized(next) * maxImpulse;
    j.impulse = next;

    const Vec2 p = next - previous;
    b.vel += mB * p;
    b.angVel += iB * cross(rB, p);
}

}  // namespace

void warmStartJoint(Joint& j, Body& a, Body& b) {
    const Vec2 rA = rotate(a.q, j.localA);
    const Vec2 rB = rotate(b.q, j.localB);
    switch (j.type) {
        case JointType::Revolute: {
            const float axial = j.motorImpulse + j.lowerImpulse - j.upperImpulse;
            applyLinear(a, b, rA, rB, j.impulse);
            a.angVel -= a.invInertia * axial;
            b.angVel += b.invInertia * axial;
            break;
        }
        case JointType::Distance: {
            const Vec2 axis = normalized((b.pos + rB) - (a.pos + rA));
            applyLinear(a, b, rA, rB, j.axialImpulse * axis);
            break;
        }
        case JointType::Mouse:
            b.vel += b.invMass * j.impulse;
            b.angVel += b.invInertia * cross(rB, j.impulse);
            break;
    }
}

void solveJoint(Joint& j, Body& a, Body& b, float h, bool useBias, const Softness& soft) {
    const Vec2 rA = rotate(a.q, j.localA);
    const Vec2 rB = rotate(b.q, j.localB);
    switch (j.type) {
        case JointType::Revolute: solveRevolute(j, a, b, rA, rB, h, useBias, soft); break;
        case JointType::Distance: solveDistance(j, a, b, rA, rB, h, useBias, soft); break;
        case JointType::Mouse: solveMouse(j, b, rB, h); break;
    }
}

}  // namespace phys
