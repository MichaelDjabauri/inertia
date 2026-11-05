#include "world.hpp"

#include <chrono>
#include <limits>

namespace phys {
namespace {

constexpr float kSpeculativeBase = 0.02f;  // contacts form this far before touching
constexpr float kTouchingSlop = 0.01f;
constexpr float kImpactMinSpeed = 2.0f;

uint64_t pairKey(int a, int b) {
    const auto lo = static_cast<uint64_t>(static_cast<uint32_t>(std::min(a, b)));
    const auto hi = static_cast<uint64_t>(static_cast<uint32_t>(std::max(a, b)));
    return (lo << 32) | hi;
}

using Clock = std::chrono::steady_clock;

float millisSince(Clock::time_point start) {
    return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
}

int findRoot(std::vector<int>& parent, int i) {
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

}  // namespace

World::World() { clear(); }

void World::clear() {
    bodies_.clear();
    freeBodies_.clear();
    joints_.clear();
    freeJoints_.clear();
    contacts_.clear();
    previousContacts_.clear();
    activeContacts_.clear();
    impacts_.clear();
    noCollide_.clear();
    forceField = nullptr;
    stats_ = {};

    BodyDef groundDef;
    groundDef.type = BodyType::Static;
    groundDef.collides = false;
    groundDef.visible = false;
    ground_ = createBody(groundDef);
}

int World::createBody(const BodyDef& def) {
    int id;
    if (!freeBodies_.empty()) {
        id = freeBodies_.back();
        freeBodies_.pop_back();
    } else {
        id = static_cast<int>(bodies_.size());
        bodies_.emplace_back();
    }

    Body b;
    b.alive = true;
    b.serial = nextSerial_++;
    b.type = def.type;
    b.shape = def.shape;
    b.pos = def.pos;
    b.angle = def.angle;
    b.q = Rot(def.angle);
    b.vel = def.vel;
    b.angVel = def.angVel;
    b.friction = def.friction;
    b.restitution = def.restitution;
    b.linearDamping = def.linearDamping;
    b.angularDamping = def.angularDamping;
    b.gravityScale = def.gravityScale;
    b.group = def.group;
    b.collides = def.collides;
    b.canSleep = def.canSleep;
    b.prevPos = def.pos;
    b.prevAngle = def.angle;
    b.startAngle = def.angle;
    b.color = def.color;
    b.glow = def.glow;
    b.visible = def.visible;
    b.trail = def.trail;

    if (def.type == BodyType::Dynamic) {
        const MassData md = computeMass(def.shape, def.density);
        b.mass = md.mass;
        b.inertia = md.inertia;
        b.invMass = md.mass > 0.0f ? 1.0f / md.mass : 0.0f;
        b.invInertia = md.inertia > 0.0f ? 1.0f / md.inertia : 0.0f;
    }

    bodies_[id] = b;
    return id;
}

void World::destroyBody(int id) {
    if (id < 0 || id >= static_cast<int>(bodies_.size()) || !bodies_[id].alive) return;

    bool removedJoint = false;
    for (int i = 0; i < static_cast<int>(joints_.size()); ++i) {
        Joint& j = joints_[i];
        if (j.alive && (j.a == id || j.b == id)) {
            j.alive = false;
            freeJoints_.push_back(i);
            removedJoint = true;
        }
    }
    if (removedJoint) rebuildNoCollide();

    // Anything resting on this body has to notice that it is gone.
    auto removeFrom = [&](std::vector<Contact>& list) {
        size_t out = 0;
        for (const Contact& c : list) {
            if (c.a == id || c.b == id) {
                wake(c.a == id ? c.b : c.a);
            } else {
                list[out++] = c;
            }
        }
        list.resize(out);
    };
    removeFrom(contacts_);
    activeContacts_.clear();

    bodies_[id].alive = false;
    freeBodies_.push_back(id);
}

void World::wake(int id) {
    Body& b = bodies_[id];
    if (b.isDynamic()) {
        b.awake = true;
        b.sleepTime = 0.0f;
    }
}

void World::wakeAll() {
    for (int i = 0; i < static_cast<int>(bodies_.size()); ++i) {
        if (bodies_[i].alive) wake(i);
    }
}

int World::addJoint(Joint joint) {
    joint.alive = true;
    int id;
    if (!freeJoints_.empty()) {
        id = freeJoints_.back();
        freeJoints_.pop_back();
        joints_[id] = joint;
    } else {
        id = static_cast<int>(joints_.size());
        joints_.push_back(joint);
    }
    linkNoCollide(joints_[id]);
    wake(joint.a);
    wake(joint.b);
    return id;
}

void World::linkNoCollide(const Joint& j) {
    if (!j.collideConnected && j.type != JointType::Mouse) noCollide_.insert(pairKey(j.a, j.b));
}

void World::rebuildNoCollide() {
    noCollide_.clear();
    for (const Joint& j : joints_) {
        if (j.alive) linkNoCollide(j);
    }
}

int World::createRevolute(int a, int b, Vec2 anchor) {
    Joint j;
    j.type = JointType::Revolute;
    j.a = a;
    j.b = b;
    j.localA = applyInv(bodies_[a].transform(), anchor);
    j.localB = applyInv(bodies_[b].transform(), anchor);
    j.referenceAngle = bodies_[b].angle - bodies_[a].angle;
    return addJoint(j);
}

int World::createDistance(int a, int b, Vec2 anchorA, Vec2 anchorB) {
    Joint j;
    j.type = JointType::Distance;
    j.a = a;
    j.b = b;
    j.localA = applyInv(bodies_[a].transform(), anchorA);
    j.localB = applyInv(bodies_[b].transform(), anchorB);
    j.length = length(anchorB - anchorA);
    return addJoint(j);
}

int World::createSpring(int a, int b, Vec2 anchorA, Vec2 anchorB, float hertz, float dampingRatio) {
    const int id = createDistance(a, b, anchorA, anchorB);
    joints_[id].hertz = hertz;
    joints_[id].dampingRatio = dampingRatio;
    return id;
}

int World::createRope(int a, int b, Vec2 anchorA, Vec2 anchorB, float maxLength) {
    const int id = createDistance(a, b, anchorA, anchorB);
    joints_[id].rope = true;
    joints_[id].length = maxLength;
    return id;
}

int World::createMouse(int body, Vec2 anchor, float maxForce, float hertz, float dampingRatio) {
    Joint j;
    j.type = JointType::Mouse;
    j.a = ground_;
    j.b = body;
    j.localB = applyInv(bodies_[body].transform(), anchor);
    j.target = anchor;
    j.maxForce = maxForce;
    j.hertz = hertz;
    j.dampingRatio = dampingRatio;
    return addJoint(j);
}

void World::destroyJoint(int id) {
    if (id < 0 || id >= static_cast<int>(joints_.size()) || !joints_[id].alive) return;
    joints_[id].alive = false;
    freeJoints_.push_back(id);
    wake(joints_[id].a);
    wake(joints_[id].b);
    rebuildNoCollide();
}

int World::bodyAt(Vec2 p) const {
    int fallback = -1;
    for (int i = static_cast<int>(bodies_.size()) - 1; i >= 0; --i) {
        const Body& b = bodies_[i];
        if (!b.alive || !b.collides) continue;
        if (!containsPoint(b.shape, b.transform(), p)) continue;
        if (b.isDynamic()) return i;
        fallback = i;
    }
    return fallback;
}

void World::explode(Vec2 centre, float radius, float speed) {
    for (int i = 0; i < static_cast<int>(bodies_.size()); ++i) {
        Body& b = bodies_[i];
        if (!b.alive || !b.isDynamic()) continue;
        const Vec2 d = b.pos - centre;
        const float dist = length(d);
        if (dist >= radius) continue;
        const float falloff = 1.0f - dist / radius;
        wake(i);
        b.vel += normalized(d) * (speed * falloff * falloff);
        // A little spin so that debris tumbles rather than sliding away flat.
        b.angVel += (d.x >= 0.0f ? -1.0f : 1.0f) * speed * 0.5f * falloff;
    }
}

float World::totalEnergy() const {
    float energy = 0.0f;
    for (const Body& b : bodies_) {
        if (!b.alive || !b.isDynamic()) continue;
        energy += 0.5f * b.mass * lengthSq(b.vel) + 0.5f * b.inertia * b.angVel * b.angVel;
        energy -= b.mass * b.gravityScale * dot(gravity, b.pos);
    }
    return energy;
}

bool World::isActive(const Body& b) const {
    switch (b.type) {
        case BodyType::Static: return false;
        case BodyType::Dynamic: return b.awake;
        case BodyType::Kinematic: return lengthSq(b.vel) > 0.0f || b.angVel != 0.0f;
    }
    return false;
}

float World::motionBound(const Body& b, float dt) const {
    return (length(b.vel) + std::abs(b.angVel) * b.shape.boundRadius) * dt;
}

void World::findPairs(float dt) {
    proxies_.clear();

    // Sweep along whichever axis the bodies are most spread out on.
    float sumX = 0.0f, sumY = 0.0f, sumXX = 0.0f, sumYY = 0.0f;
    int dynamicCount = 0;
    for (const Body& b : bodies_) {
        if (!b.alive || !b.collides || !b.isDynamic()) continue;
        sumX += b.pos.x;
        sumY += b.pos.y;
        sumXX += b.pos.x * b.pos.x;
        sumYY += b.pos.y * b.pos.y;
        ++dynamicCount;
    }
    const float n = static_cast<float>(std::max(dynamicCount, 1));
    const float varX = sumXX / n - (sumX / n) * (sumX / n);
    const float varY = sumYY / n - (sumY / n) * (sumY / n);
    const bool sweepX = varX >= varY;

    for (int i = 0; i < static_cast<int>(bodies_.size()); ++i) {
        const Body& b = bodies_[i];
        if (!b.alive || !b.collides) continue;
        AABB box = computeAABB(b.shape, b.transform());
        const float margin = b.type == BodyType::Static ? 0.0f : kSpeculativeBase + motionBound(b, dt);
        box.min -= Vec2{margin, margin};
        box.max += Vec2{margin, margin};
        Proxy p;
        p.id = i;
        p.lo = sweepX ? box.min.x : box.min.y;
        p.hi = sweepX ? box.max.x : box.max.y;
        p.crossLo = sweepX ? box.min.y : box.min.x;
        p.crossHi = sweepX ? box.max.y : box.max.x;
        proxies_.push_back(p);
    }

    std::sort(proxies_.begin(), proxies_.end(),
              [](const Proxy& l, const Proxy& r) { return l.lo < r.lo; });

    pairs_.clear();
    const size_t count = proxies_.size();
    for (size_t i = 0; i < count; ++i) {
        const Proxy& p = proxies_[i];
        const Body& a = bodies_[p.id];
        for (size_t j = i + 1; j < count && proxies_[j].lo <= p.hi; ++j) {
            const Proxy& q = proxies_[j];
            if (q.crossLo > p.crossHi || q.crossHi < p.crossLo) continue;
            const Body& b = bodies_[q.id];
            if (!a.isDynamic() && !b.isDynamic()) continue;
            if (a.group != 0 && a.group == b.group) continue;
            const uint64_t key = pairKey(p.id, q.id);
            if (!noCollide_.empty() && noCollide_.count(key) != 0) continue;
            pairs_.push_back(key);
        }
    }
    std::sort(pairs_.begin(), pairs_.end());
}

void World::updateContacts(float dt) {
    previousContacts_.swap(contacts_);
    contacts_.clear();

    // Both lists are sorted by key, so last step's contacts can be found with a merge.
    size_t cursor = 0;
    for (const uint64_t key : pairs_) {
        while (cursor < previousContacts_.size() && previousContacts_[cursor].key < key) ++cursor;
        const Contact* previous = cursor < previousContacts_.size() && previousContacts_[cursor].key == key
                                      ? &previousContacts_[cursor]
                                      : nullptr;

        const int ia = static_cast<int>(key >> 32);
        const int ib = static_cast<int>(key & 0xFFFFFFFFu);
        Body& a = bodies_[ia];
        Body& b = bodies_[ib];

        // Nothing here can change, so keep the contact and its impulses for later.
        if (!isActive(a) && !isActive(b)) {
            if (previous != nullptr) contacts_.push_back(*previous);
            continue;
        }

        const float margin = kSpeculativeBase + motionBound(a, dt) + motionBound(b, dt);
        Manifold m;
        if (!collide(a.shape, a.transform(), b.shape, b.transform(), margin, m)) continue;

        if (a.isDynamic() && !a.awake) wake(ia);
        if (b.isDynamic() && !b.awake) wake(ib);

        Contact c;
        c.key = key;
        c.a = ia;
        c.b = ib;
        c.normal = m.normal;
        c.count = m.count;
        c.friction = std::sqrt(a.friction * b.friction);
        c.restitution = std::max({a.restitution, b.restitution, minRestitution});

        float minSeparation = std::numeric_limits<float>::max();
        for (int i = 0; i < m.count; ++i) {
            ContactPoint& cp = c.points[i];
            cp.point = m.points[i].point;
            cp.separation = m.points[i].separation;
            cp.id = m.points[i].id;
            minSeparation = std::min(minSeparation, cp.separation);

            if (previous != nullptr) {
                for (int k = 0; k < previous->count; ++k) {
                    const ContactPoint& old = previous->points[k];
                    if (old.id == cp.id) {
                        cp.normalImpulse = old.normalImpulse;
                        cp.tangentImpulse = old.tangentImpulse;
                        break;
                    }
                }
            }
        }
        c.touching = minSeparation < kTouchingSlop;
        contacts_.push_back(c);
    }

    // A jointed body shares the fate of its partner.
    for (const Joint& j : joints_) {
        if (!j.alive) continue;
        if (isActive(bodies_[j.a]) && bodies_[j.b].isDynamic() && !bodies_[j.b].awake) wake(j.b);
        if (isActive(bodies_[j.b]) && bodies_[j.a].isDynamic() && !bodies_[j.a].awake) wake(j.a);
    }
}

void World::prepareContacts(float h) {
    activeContacts_.clear();
    const float hertz = std::min(contactHertz, 0.25f / h);
    const Softness soft = makeSoft(hertz, contactDamping, h);
    // Contacts against immovable bodies can be stiffer without any risk of instability.
    const Softness staticSoft = makeSoft(2.0f * hertz, contactDamping, h);

    for (int i = 0; i < static_cast<int>(contacts_.size()); ++i) {
        Contact& c = contacts_[i];
        const Body& a = bodies_[c.a];
        const Body& b = bodies_[c.b];
        if (!isActive(a) && !isActive(b)) continue;

        c.softness = (a.invMass == 0.0f || b.invMass == 0.0f) ? staticSoft : soft;
        const Vec2 n = c.normal;
        const Vec2 t = perpRight(n);
        const float mA = a.invMass, mB = b.invMass, iA = a.invInertia, iB = b.invInertia;

        for (int k = 0; k < c.count; ++k) {
            ContactPoint& cp = c.points[k];
            cp.rA = cp.point - a.pos;
            cp.rB = cp.point - b.pos;
            cp.adjustedSeparation = cp.separation - dot(cp.rB - cp.rA, n);
            cp.maxNormalImpulse = 0.0f;

            const float rnA = cross(cp.rA, n);
            const float rnB = cross(cp.rB, n);
            const float kn = mA + mB + iA * rnA * rnA + iB * rnB * rnB;
            cp.normalMass = kn > 0.0f ? 1.0f / kn : 0.0f;

            const float rtA = cross(cp.rA, t);
            const float rtB = cross(cp.rB, t);
            const float kt = mA + mB + iA * rtA * rtA + iB * rtB * rtB;
            cp.tangentMass = kt > 0.0f ? 1.0f / kt : 0.0f;

            const Vec2 dv = (b.vel + cross(b.angVel, cp.rB)) - (a.vel + cross(a.angVel, cp.rA));
            cp.approachVelocity = dot(dv, n);
        }
        activeContacts_.push_back(i);
    }
}

void World::integrateVelocities(float h) {
    if (forceField) forceField(*this, h);

    for (Body& b : bodies_) {
        if (!b.alive || !b.isDynamic() || !b.awake) continue;

        b.vel += (gravity * b.gravityScale + b.force * b.invMass) * h;
        b.angVel += b.torque * b.invInertia * h;

        b.vel *= 1.0f / (1.0f + h * b.linearDamping);
        b.angVel *= 1.0f / (1.0f + h * b.angularDamping);

        const float speedSq = lengthSq(b.vel);
        if (speedSq > maxSpeed * maxSpeed) b.vel *= maxSpeed / std::sqrt(speedSq);
    }
}

void World::integratePositions(float h) {
    for (Body& b : bodies_) {
        if (!b.alive || !isActive(b)) continue;
        const Vec2 delta = b.vel * h;
        b.pos += delta;
        b.dp += delta;
        b.angle += b.angVel * h;
        b.q = Rot(b.angle);
        b.dq = Rot(b.angle - b.startAngle);
    }
}

void World::warmStart() {
    for (Joint& j : joints_) {
        if (!j.alive) continue;
        Body& a = bodies_[j.a];
        Body& b = bodies_[j.b];
        if (isActive(a) || isActive(b)) warmStartJoint(j, a, b);
    }

    for (const int index : activeContacts_) {
        const Contact& c = contacts_[index];
        Body& a = bodies_[c.a];
        Body& b = bodies_[c.b];
        const Vec2 n = c.normal;
        const Vec2 t = perpRight(n);
        for (int k = 0; k < c.count; ++k) {
            const ContactPoint& cp = c.points[k];
            const Vec2 p = n * cp.normalImpulse + t * cp.tangentImpulse;
            a.vel -= a.invMass * p;
            a.angVel -= a.invInertia * cross(cp.rA, p);
            b.vel += b.invMass * p;
            b.angVel += b.invInertia * cross(cp.rB, p);
        }
    }
}

void World::solveContacts(float h, bool useBias) {
    const float invH = 1.0f / h;

    for (const int index : activeContacts_) {
        Contact& c = contacts_[index];
        Body& a = bodies_[c.a];
        Body& b = bodies_[c.b];
        const float mA = a.invMass, mB = b.invMass, iA = a.invInertia, iB = b.invInertia;
        Vec2 vA = a.vel, vB = b.vel;
        float wA = a.angVel, wB = b.angVel;

        const Vec2 n = c.normal;
        const Vec2 t = perpRight(n);
        const Vec2 dp = b.dp - a.dp;

        for (int k = 0; k < c.count; ++k) {
            ContactPoint& cp = c.points[k];

            // How far apart the anchors are now, after the earlier substeps moved the bodies.
            const Vec2 d = dp + (rotate(b.dq, cp.rB) - rotate(a.dq, cp.rA));
            const float s = dot(d, n) + cp.adjustedSeparation;

            float bias = 0.0f, massScale = 1.0f, impulseScale = 0.0f;
            if (s > 0.0f) {
                bias = s * invH;  // speculative: allow exactly the motion that closes the gap
            } else if (useBias) {
                bias = std::max(c.softness.biasRate * s, -contactPushout);
                massScale = c.softness.massScale;
                impulseScale = c.softness.impulseScale;
            }

            const Vec2 dv = (vB + cross(wB, cp.rB)) - (vA + cross(wA, cp.rA));
            const float vn = dot(dv, n);
            float lambda = -cp.normalMass * massScale * (vn + bias) - impulseScale * cp.normalImpulse;
            const float next = std::max(cp.normalImpulse + lambda, 0.0f);
            lambda = next - cp.normalImpulse;
            cp.normalImpulse = next;
            cp.maxNormalImpulse = std::max(cp.maxNormalImpulse, next);

            const Vec2 p = n * lambda;
            vA -= mA * p;
            wA -= iA * cross(cp.rA, p);
            vB += mB * p;
            wB += iB * cross(cp.rB, p);
        }

        for (int k = 0; k < c.count; ++k) {
            ContactPoint& cp = c.points[k];
            const Vec2 dv = (vB + cross(wB, cp.rB)) - (vA + cross(wA, cp.rA));
            const float vt = dot(dv, t);
            const float maxFriction = c.friction * cp.normalImpulse;
            const float next = std::clamp(cp.tangentImpulse - cp.tangentMass * vt, -maxFriction, maxFriction);
            const float lambda = next - cp.tangentImpulse;
            cp.tangentImpulse = next;

            const Vec2 p = t * lambda;
            vA -= mA * p;
            wA -= iA * cross(cp.rA, p);
            vB += mB * p;
            wB += iB * cross(cp.rB, p);
        }

        a.vel = vA;
        a.angVel = wA;
        b.vel = vB;
        b.angVel = wB;
    }
}

void World::solve(float h, bool useBias) {
    // Sweep joints forwards on one pass and backwards on the next. A one-way sweep needs
    // a whole chain's length of passes to carry a load from one end to the other.
    const int count = static_cast<int>(joints_.size());
    for (int i = 0; i < count; ++i) {
        Joint& j = joints_[useBias ? i : count - 1 - i];
        if (!j.alive) continue;
        Body& a = bodies_[j.a];
        Body& b = bodies_[j.b];
        if (isActive(a) || isActive(b)) solveJoint(j, a, b, h, useBias, jointSoftness_);
    }
    solveContacts(h, useBias);
}

void World::applyRestitution() {
    for (const int index : activeContacts_) {
        Contact& c = contacts_[index];
        if (c.restitution <= 0.0f) continue;

        Body& a = bodies_[c.a];
        Body& b = bodies_[c.b];
        const Vec2 n = c.normal;
        for (int k = 0; k < c.count; ++k) {
            ContactPoint& cp = c.points[k];
            if (cp.approachVelocity > -restitutionThreshold || cp.maxNormalImpulse <= 0.0f) continue;

            const Vec2 dv = (b.vel + cross(b.angVel, cp.rB)) - (a.vel + cross(a.angVel, cp.rA));
            const float vn = dot(dv, n);
            float lambda = -cp.normalMass * (vn + c.restitution * cp.approachVelocity);
            const float next = std::max(cp.normalImpulse + lambda, 0.0f);
            lambda = next - cp.normalImpulse;
            cp.normalImpulse = next;
            cp.maxNormalImpulse = std::max(cp.maxNormalImpulse, next);

            const Vec2 p = n * lambda;
            a.vel -= a.invMass * p;
            a.angVel -= a.invInertia * cross(cp.rA, p);
            b.vel += b.invMass * p;
            b.angVel += b.invInertia * cross(cp.rB, p);
        }
    }
}

void World::collectImpacts() {
    impacts_.clear();
    for (const int index : activeContacts_) {
        const Contact& c = contacts_[index];
        for (int k = 0; k < c.count; ++k) {
            const ContactPoint& cp = c.points[k];
            if (cp.approachVelocity > -kImpactMinSpeed || cp.maxNormalImpulse <= 0.0f) continue;
            impacts_.push_back({cp.point, c.normal, -cp.approachVelocity, cp.maxNormalImpulse, c.a, c.b});
        }
    }
}

void World::updateSleep(float dt) {
    if (!allowSleep) return;

    const int count = static_cast<int>(bodies_.size());
    islandParent_.resize(count);
    islandMinSleep_.assign(count, std::numeric_limits<float>::max());
    for (int i = 0; i < count; ++i) islandParent_[i] = i;

    for (Body& b : bodies_) {
        if (!b.alive || !b.isDynamic() || !b.awake) continue;
        const float speed = length(b.vel) + std::abs(b.angVel) * b.shape.boundRadius;
        b.sleepTime = (speed > sleepTolerance || !b.canSleep) ? 0.0f : b.sleepTime + dt;
    }

    auto merge = [&](int x, int y) {
        if (bodies_[x].isDynamic() && bodies_[y].isDynamic()) {
            islandParent_[findRoot(islandParent_, x)] = findRoot(islandParent_, y);
        }
    };
    for (const Contact& c : contacts_) {
        if (c.touching) merge(c.a, c.b);
    }
    for (const Joint& j : joints_) {
        if (j.alive) merge(j.a, j.b);
    }

    for (int i = 0; i < count; ++i) {
        const Body& b = bodies_[i];
        if (!b.alive || !b.isDynamic() || !b.awake) continue;
        float& slot = islandMinSleep_[findRoot(islandParent_, i)];
        slot = std::min(slot, b.sleepTime);
    }

    for (int i = 0; i < count; ++i) {
        Body& b = bodies_[i];
        if (!b.alive || !b.isDynamic() || !b.awake) continue;
        if (islandMinSleep_[findRoot(islandParent_, i)] >= sleepDelay) {
            b.awake = false;
            b.vel = {};
            b.angVel = 0.0f;
        }
    }
}

void World::step(float dt) {
    if (dt <= 0.0f) return;

    const auto collideStart = Clock::now();
    for (Body& b : bodies_) {
        if (!b.alive) continue;
        b.prevPos = b.pos;
        b.prevAngle = b.angle;
        b.dp = {};
        b.dq = Rot();
        b.startAngle = b.angle;
    }

    findPairs(dt);
    updateContacts(dt);
    stats_.collideMs = millisSince(collideStart);

    const auto solveStart = Clock::now();
    const int n = std::max(substeps, 1);
    const float h = dt / static_cast<float>(n);

    jointSoftness_ = makeSoft(std::min(jointHertz, 0.5f / h), jointDamping, h);
    prepareContacts(h);

    for (int i = 0; i < n; ++i) {
        integrateVelocities(h);
        warmStart();
        solve(h, true);
        integratePositions(h);
        solve(h, false);
    }

    applyRestitution();
    stats_.solveMs = millisSince(solveStart);

    collectImpacts();
    updateSleep(dt);

    stats_.bodies = 0;
    stats_.awakeBodies = 0;
    for (Body& b : bodies_) {
        if (!b.alive) continue;
        b.force = {};
        b.torque = 0.0f;
        if (b.isDynamic()) {
            ++stats_.bodies;
            if (b.awake) ++stats_.awakeBodies;
        }
    }
    stats_.joints = static_cast<int>(joints_.size() - freeJoints_.size());
    stats_.contacts = static_cast<int>(contacts_.size());
    stats_.pairs = static_cast<int>(pairs_.size());
}

}  // namespace phys
