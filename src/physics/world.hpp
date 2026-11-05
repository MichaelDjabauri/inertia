// The simulation. World::step runs one fixed time step:
//
//   1. Broad phase    sort-and-sweep to find bodies that might touch.
//   2. Narrow phase   build contact manifolds, reusing last step's impulses.
//   3. Substeps       integrate, warm start, solve with bias, move, relax.
//   4. Restitution    bounce, applied once all the substeps are finished.
//   5. Sleeping       groups of touching bodies that have settled go to sleep.
#pragma once

#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

#include "body.hpp"
#include "collision.hpp"
#include "joint.hpp"

namespace phys {

struct ContactPoint {
    Vec2 point;
    Vec2 rA, rB;              // anchors relative to each body at the start of the step
    float separation = 0.0f;
    float adjustedSeparation = 0.0f;
    float normalMass = 0.0f;
    float tangentMass = 0.0f;
    float normalImpulse = 0.0f;
    float tangentImpulse = 0.0f;
    float maxNormalImpulse = 0.0f;
    float approachVelocity = 0.0f;  // relative normal velocity at the start of the step
    uint32_t id = 0;
};

struct Contact {
    uint64_t key = 0;
    int a = -1;
    int b = -1;
    Vec2 normal;
    int count = 0;
    ContactPoint points[2];
    float friction = 0.0f;
    float restitution = 0.0f;
    bool touching = false;
    Softness softness;
};

// Emitted for hard hits, which the renderer turns into sparks.
struct Impact {
    Vec2 point;
    Vec2 normal;
    float speed = 0.0f;
    float impulse = 0.0f;
    int a = -1;
    int b = -1;
};

struct WorldStats {
    int bodies = 0;
    int awakeBodies = 0;
    int joints = 0;
    int contacts = 0;
    int pairs = 0;
    float collideMs = 0.0f;
    float solveMs = 0.0f;
};

class World {
public:
    World();

    // Tunables.
    Vec2 gravity{0.0f, -10.0f};
    int substeps = 6;
    float contactHertz = 30.0f;
    float contactDamping = 10.0f;
    float contactPushout = 3.0f;      // cap on the speed used to resolve overlap
    float jointHertz = 120.0f;
    float jointDamping = 2.0f;
    float restitutionThreshold = 1.0f;
    float minRestitution = 0.0f;      // raises the bounce of every contact
    float maxSpeed = 200.0f;
    bool allowSleep = true;
    float sleepTolerance = 0.05f;
    float sleepDelay = 0.5f;

    // Called once per substep to add custom forces, by changing velocities directly.
    std::function<void(World&, float h)> forceField;

    void step(float dt);
    void clear();

    int createBody(const BodyDef& def);
    void destroyBody(int id);
    void wake(int id);
    void wakeAll();

    int createRevolute(int a, int b, Vec2 worldAnchor);
    int createDistance(int a, int b, Vec2 worldAnchorA, Vec2 worldAnchorB);
    int createSpring(int a, int b, Vec2 worldAnchorA, Vec2 worldAnchorB, float hertz,
                     float dampingRatio);
    int createRope(int a, int b, Vec2 worldAnchorA, Vec2 worldAnchorB, float maxLength);
    int createMouse(int body, Vec2 worldAnchor, float maxForce, float hertz = 6.0f,
                    float dampingRatio = 1.0f);
    void destroyJoint(int id);

    // Bodies occupying a point, topmost dynamic body first; -1 if nothing is there.
    int bodyAt(Vec2 worldPoint) const;

    // Radial velocity kick with a smooth falloff.
    void explode(Vec2 centre, float radius, float speed);

    // The invisible static body that joints use to pin things to the world.
    int ground() const { return ground_; }

    std::vector<Body>& bodies() { return bodies_; }
    const std::vector<Body>& bodies() const { return bodies_; }
    std::vector<Joint>& joints() { return joints_; }
    const std::vector<Joint>& joints() const { return joints_; }
    const std::vector<Contact>& contacts() const { return contacts_; }
    const std::vector<Impact>& impacts() const { return impacts_; }
    const WorldStats& stats() const { return stats_; }

    // Kinetic plus gravitational potential energy of all dynamic bodies.
    float totalEnergy() const;

private:
    struct Proxy {
        float lo = 0.0f, hi = 0.0f;
        float crossLo = 0.0f, crossHi = 0.0f;
        int id = -1;
    };

    bool isActive(const Body& b) const;
    float motionBound(const Body& b, float dt) const;
    void findPairs(float dt);
    void updateContacts(float dt);
    void prepareContacts(float h);
    void integrateVelocities(float h);
    void integratePositions(float h);
    void warmStart();
    void solve(float h, bool useBias);
    void solveContacts(float h, bool useBias);
    void applyRestitution();
    void collectImpacts();
    void updateSleep(float dt);
    int addJoint(Joint joint);
    void linkNoCollide(const Joint& joint);
    void rebuildNoCollide();

    std::vector<Body> bodies_;
    std::vector<int> freeBodies_;
    std::vector<Joint> joints_;
    std::vector<int> freeJoints_;

    std::vector<Proxy> proxies_;
    std::vector<uint64_t> pairs_;
    std::vector<Contact> contacts_;
    std::vector<Contact> previousContacts_;
    std::vector<int> activeContacts_;
    std::vector<Impact> impacts_;
    std::unordered_set<uint64_t> noCollide_;

    std::vector<int> islandParent_;
    std::vector<float> islandMinSleep_;

    WorldStats stats_;
    Softness jointSoftness_;
    int ground_ = -1;
    uint32_t nextSerial_ = 1;
};

}  // namespace phys
