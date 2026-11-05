// Headless checks that the solver behaves like physics, not just that it runs.
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "physics/world.hpp"

using namespace phys;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("    FAIL  %s\n", what.c_str());
    }
}

std::string fmt(const char* f, double a, double b = 0, double c = 0) {
    char buf[200];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

constexpr float kDt = 1.0f / 60.0f;

void run(World& w, float seconds) {
    const int steps = static_cast<int>(std::lround(seconds / kDt));
    for (int i = 0; i < steps; ++i) w.step(kDt);
}

int addGround(World& w, float y = -0.5f, float halfWidth = 100.0f) {
    BodyDef d;
    d.type = BodyType::Static;
    d.shape = makeBox(halfWidth, 0.5f);
    d.pos = {0.0f, y};
    return w.createBody(d);
}

void testFreeFall() {
    World w;
    BodyDef d;
    d.pos = {0.0f, 100.0f};
    d.angularDamping = 0.0f;
    const int ball = w.createBody(d);
    run(w, 2.0f);
    const float expected = 100.0f - 0.5f * 10.0f * 4.0f;
    check(std::abs(w.bodies()[ball].pos.y - expected) < 0.1f,
          fmt("height %.3f, expected %.3f", w.bodies()[ball].pos.y, expected));
    check(std::abs(w.bodies()[ball].vel.y + 20.0f) < 0.01f,
          fmt("velocity %.3f, expected -20", w.bodies()[ball].vel.y));
}

void testMassProperties() {
    const Shape box = makeBox(1.0f, 2.0f);
    const MassData m = computeMass(box, 2.0f);
    const float expectedMass = 2.0f * 2.0f * 4.0f;
    const float expectedInertia = expectedMass * (4.0f + 16.0f) / 12.0f;
    check(std::abs(m.mass - expectedMass) < 1e-4f, fmt("box mass %.4f vs %.4f", m.mass, expectedMass));
    check(std::abs(m.inertia - expectedInertia) < 1e-3f,
          fmt("box inertia %.4f vs %.4f", m.inertia, expectedInertia));
}

void testRestingBoxSleeps() {
    World w;
    addGround(w);
    BodyDef d;
    d.shape = makeBox(0.5f, 0.5f);
    d.pos = {0.0f, 0.5f};
    const int box = w.createBody(d);
    run(w, 3.0f);
    const Body& b = w.bodies()[box];
    check(std::abs(b.pos.y - 0.5f) < 0.02f, fmt("resting height %.4f", b.pos.y));
    check(std::abs(b.pos.x) < 0.001f, fmt("sideways drift %.5f", b.pos.x));
    check(!b.awake, "box should fall asleep");
}

void testWakeOnImpact() {
    World w;
    addGround(w);
    BodyDef d;
    d.shape = makeBox(0.5f, 0.5f);
    d.pos = {0.0f, 0.5f};
    const int box = w.createBody(d);
    run(w, 2.0f);
    check(!w.bodies()[box].awake, "precondition: box asleep");
    BodyDef ball;
    ball.pos = {0.0f, 5.0f};
    ball.density = 8.0f;
    w.createBody(ball);
    run(w, 1.0f);
    check(w.bodies()[box].awake || std::abs(w.bodies()[box].pos.y - 0.5f) < 0.05f,
          "sleeping box should react to being hit");
}

void testRemovedSupportWakes() {
    World w;
    addGround(w);
    BodyDef d;
    d.shape = makeBox(0.5f, 0.5f);
    d.pos = {0.0f, 0.5f};
    const int lower = w.createBody(d);
    d.pos = {0.0f, 1.5f};
    const int upper = w.createBody(d);
    run(w, 2.0f);
    check(!w.bodies()[upper].awake, "precondition: stack asleep");
    w.destroyBody(lower);
    run(w, 1.5f);
    check(std::abs(w.bodies()[upper].pos.y - 0.5f) < 0.03f,
          fmt("upper box should fall to the ground, y=%.3f", w.bodies()[upper].pos.y));
}

void testPyramidStands() {
    World w;
    addGround(w);
    const int rows = 16;
    std::vector<int> ids;
    std::vector<Vec2> start;
    for (int row = 0; row < rows; ++row) {
        for (int i = 0; i < rows - row; ++i) {
            BodyDef d;
            d.shape = makeBox(0.5f, 0.5f);
            d.pos = {(static_cast<float>(i) - 0.5f * static_cast<float>(rows - row - 1)) * 1.05f,
                     0.5f + static_cast<float>(row) * 1.0f};
            d.friction = 0.6f;
            d.restitution = 0.0f;
            ids.push_back(w.createBody(d));
            start.push_back(d.pos);
        }
    }
    run(w, 10.0f);
    float worst = 0.0f;
    for (size_t i = 0; i < ids.size(); ++i) {
        worst = std::max(worst, length(w.bodies()[ids[i]].pos - start[i]));
    }
    check(worst < 0.12f, fmt("worst displacement in pyramid %.4f m", worst));
    std::printf("    pyramid of %zu boxes: worst drift %.4f m after 10 s, %d awake\n", ids.size(), worst,
                w.stats().awakeBodies);
}

void testFrictionOnSlope(float slopeDegrees, bool expectSlide) {
    World w;
    const float a = slopeDegrees * kPi / 180.0f;
    BodyDef slope;
    slope.type = BodyType::Static;
    slope.shape = makeBox(40.0f, 0.5f);
    slope.angle = a;
    slope.friction = 0.5f;
    w.createBody(slope);

    const Vec2 up{-std::sin(a), std::cos(a)};
    BodyDef d;
    d.shape = makeBox(0.5f, 0.5f);
    d.pos = up * 1.01f;
    d.angle = a;
    d.friction = 0.5f;  // combined friction 0.5, sliding starts at atan(0.5) = 26.6 degrees
    const int box = w.createBody(d);
    const Vec2 start = d.pos;
    run(w, 2.0f);
    const float moved = length(w.bodies()[box].pos - start);
    if (expectSlide) {
        check(moved > 1.0f, fmt("at %.0f degrees box should slide, moved %.3f", slopeDegrees, moved));
    } else {
        check(moved < 0.05f, fmt("at %.0f degrees box should grip, moved %.3f", slopeDegrees, moved));
    }
}

void testElasticBallsSwapVelocity() {
    World w;
    w.gravity = {};
    BodyDef d;
    d.restitution = 1.0f;
    d.friction = 0.0f;
    d.angularDamping = 0.0f;
    d.pos = {-3.0f, 0.0f};
    d.vel = {5.0f, 0.0f};
    const int left = w.createBody(d);
    d.pos = {3.0f, 0.0f};
    d.vel = {};
    const int right = w.createBody(d);
    run(w, 2.0f);
    check(std::abs(w.bodies()[left].vel.x) < 0.05f, fmt("left ball v=%.4f", w.bodies()[left].vel.x));
    check(std::abs(w.bodies()[right].vel.x - 5.0f) < 0.05f, fmt("right ball v=%.4f", w.bodies()[right].vel.x));
}

void testMomentumConservedInElasticCollision() {
    World w;
    w.gravity = {};
    BodyDef d;
    d.restitution = 1.0f;
    d.friction = 0.0f;
    d.angularDamping = 0.0f;
    d.density = 3.0f;
    d.pos = {-3.0f, 0.2f};
    d.vel = {4.0f, 0.0f};
    const int heavy = w.createBody(d);
    d.density = 1.0f;
    d.pos = {3.0f, 0.0f};
    d.vel = {-4.0f, 0.0f};
    const int light = w.createBody(d);

    auto momentum = [&] {
        const Body& a = w.bodies()[heavy];
        const Body& b = w.bodies()[light];
        return a.vel * a.mass + b.vel * b.mass;
    };
    auto kinetic = [&] {
        const Body& a = w.bodies()[heavy];
        const Body& b = w.bodies()[light];
        return 0.5f * a.mass * lengthSq(a.vel) + 0.5f * b.mass * lengthSq(b.vel);
    };
    const Vec2 p0 = momentum();
    const float e0 = kinetic();
    run(w, 2.0f);
    const Vec2 p1 = momentum();
    check(length(p1 - p0) < 0.01f * length(p0) + 0.01f, fmt("momentum changed by %.5f", length(p1 - p0)));
    check(std::abs(kinetic() - e0) < 0.03f * e0, fmt("kinetic energy %.3f vs %.3f", kinetic(), e0));
}

void testNoTunnelling() {
    World w;
    w.gravity = {};
    BodyDef wall;
    wall.type = BodyType::Static;
    wall.shape = makeBox(0.05f, 20.0f);
    w.createBody(wall);

    BodyDef ball;
    ball.shape = makeCircle(0.1f);
    ball.pos = {-10.0f, 0.0f};
    ball.vel = {150.0f, 0.0f};  // 2.5 m per step against a 10 cm wall
    ball.restitution = 0.0f;
    const int fast = w.createBody(ball);
    run(w, 1.0f);
    check(w.bodies()[fast].pos.x < 0.0f, fmt("ball went through the wall, x=%.3f", w.bodies()[fast].pos.x));

    BodyDef box;
    box.shape = makeBox(0.1f, 0.1f);
    box.pos = {-10.0f, 3.0f};
    box.vel = {120.0f, 0.0f};
    box.restitution = 0.0f;
    const int fastBox = w.createBody(box);
    run(w, 1.0f);
    check(w.bodies()[fastBox].pos.x < 0.0f, fmt("box went through the wall, x=%.3f", w.bodies()[fastBox].pos.x));
}

void testPendulumConservesEnergy() {
    World w;
    w.allowSleep = false;
    w.substeps = 12;
    BodyDef d;
    d.shape = makeCircle(0.3f);
    d.pos = {3.0f, 0.0f};
    d.angularDamping = 0.0f;
    d.collides = false;
    const int bob = w.createBody(d);
    const int joint = w.createDistance(w.ground(), bob, {0.0f, 0.0f}, d.pos);

    const float e0 = w.totalEnergy();
    float worst = 0.0f;
    for (int i = 0; i < 60 * 20; ++i) {
        w.step(kDt);
        worst = std::max(worst, std::abs(w.totalEnergy() - e0));
    }
    const float mass = w.bodies()[bob].mass;
    const float swing = mass * 10.0f * 3.0f;  // energy between release and lowest point
    check(worst < 0.05f * swing, fmt("energy drift %.4f of swing %.4f", worst, swing));
    const float stretch = std::abs(length(w.bodies()[bob].pos) - w.joints()[joint].length);
    check(stretch < 0.01f, fmt("rod stretched by %.5f", stretch));
    std::printf("    pendulum: worst energy error %.2f%% of swing over 20 s\n", 100.0f * worst / swing);
}

void testHeavyChainHolds() {
    World w;
    w.allowSleep = false;
    int previous = w.ground();
    Vec2 anchor{0.0f, 20.0f};
    const int links = 16;
    for (int i = 0; i < links; ++i) {
        BodyDef d;
        d.shape = makeBox(0.25f, 0.06f);
        d.pos = anchor + Vec2{0.25f, 0.0f};
        d.density = 2.0f;
        d.group = 1;
        const int link = w.createBody(d);
        w.createRevolute(previous, link, anchor);
        previous = link;
        anchor += Vec2{0.5f, 0.0f};
    }
    BodyDef ballDef;
    ballDef.shape = makeCircle(1.2f);
    ballDef.pos = anchor;
    ballDef.density = 8.0f;  // ball is ~50 times heavier than one link
    ballDef.group = 1;
    const int ball = w.createBody(ballDef);
    w.createRevolute(previous, ball, anchor);

    // The ball swings through the bottom at about three times its weight, so track the peak.
    float reach = 0.0f;
    for (int i = 0; i < 60 * 12; ++i) {
        w.step(kDt);
        reach = std::max(reach, length(w.bodies()[ball].pos - Vec2{0.0f, 20.0f}));
    }
    const float nominal = 0.5f * static_cast<float>(links);
    check(reach < nominal * 1.08f, fmt("chain stretched to %.3f of %.3f", reach, nominal));
    std::printf("    chain with 150:1 ball: peak reach %.3f m of %.3f m\n", reach, nominal);
}

void testNewtonsCradle() {
    World w;
    w.allowSleep = false;
    const int count = 5;
    const float r = 0.5f;
    const float gap = 0.03f;  // gives each collision its own step, as no restitution chain exists
    const float len = 4.0f;
    std::vector<int> balls;
    for (int i = 0; i < count; ++i) {
        const float x = static_cast<float>(i) * (2.0f * r + gap);
        BodyDef d;
        d.shape = makeCircle(r);
        d.restitution = 1.0f;
        d.friction = 0.0f;
        d.angularDamping = 0.0f;
        d.pos = {x, 0.0f};
        if (i == 0) {
            const float a = 0.6f;
            d.pos = {x - len * std::sin(a), len - len * std::cos(a)};
        }
        const int id = w.createBody(d);
        w.createDistance(w.ground(), id, {x, len}, d.pos);
        balls.push_back(id);
    }

    float peak = 0.0f;
    float others = 0.0f;
    // A quarter period to the strike, another quarter for the far ball to rise.
    for (int i = 0; i < 130; ++i) {
        w.step(kDt);
        peak = std::max(peak, w.bodies()[balls.back()].pos.y);
    }
    for (int i = 1; i + 1 < count; ++i) others = std::max(others, std::abs(w.bodies()[balls[i]].pos.y));
    const float released = len - len * std::cos(0.6f);
    check(peak > released * 0.95f, fmt("end ball reached %.3f of %.3f", peak, released));
    std::printf("    cradle: far ball rises to %.1f%% of release height, middle balls lift %.4f m\n",
                100.0f * peak / released, others);
}

void testMotorDrivesRotor() {
    World w;
    w.gravity = {};
    w.allowSleep = false;
    BodyDef d;
    d.shape = makeBox(2.0f, 0.2f);
    d.angularDamping = 0.0f;
    const int rotor = w.createBody(d);
    const int j = w.createRevolute(w.ground(), rotor, {0.0f, 0.0f});
    w.joints()[j].motor = true;
    w.joints()[j].motorSpeed = 5.0f;
    w.joints()[j].maxMotorTorque = 1000.0f;
    run(w, 1.0f);
    check(std::abs(w.bodies()[rotor].angVel - 5.0f) < 0.05f, fmt("rotor speed %.3f", w.bodies()[rotor].angVel));
}

void testJointLimit() {
    World w;
    w.allowSleep = false;
    BodyDef d;
    d.shape = makeBox(1.0f, 0.1f);
    d.pos = {1.0f, 0.0f};
    const int arm = w.createBody(d);
    const int j = w.createRevolute(w.ground(), arm, {0.0f, 0.0f});
    w.joints()[j].limit = true;
    w.joints()[j].lower = -0.5f;
    w.joints()[j].upper = 0.5f;
    run(w, 3.0f);
    check(w.bodies()[arm].angle > -0.53f, fmt("arm passed its limit, angle %.4f", w.bodies()[arm].angle));
    check(w.bodies()[arm].angle < -0.3f, fmt("arm should rest near the limit, angle %.4f", w.bodies()[arm].angle));
}

void testSpringFrequency() {
    World w;
    w.gravity = {};
    w.allowSleep = false;
    w.substeps = 8;
    BodyDef d;
    d.collides = false;
    d.pos = {2.0f, 0.0f};
    const int bob = w.createBody(d);
    const int j = w.createSpring(w.ground(), bob, {0.0f, 0.0f}, {2.0f, 0.0f}, 2.0f, 0.0f);
    w.joints()[j].length = 1.0f;

    // Count crossings of the rest length to measure the oscillation frequency.
    int crossings = 0;
    float previous = length(w.bodies()[bob].pos) - 1.0f;
    const int steps = 240;
    for (int i = 0; i < steps; ++i) {
        w.step(kDt);
        const float current = length(w.bodies()[bob].pos) - 1.0f;
        if (previous * current < 0.0f) ++crossings;
        previous = current;
    }
    const float measuredHz = static_cast<float>(crossings) * 0.5f / (static_cast<float>(steps) * kDt);
    check(std::abs(measuredHz - 2.0f) < 0.3f, fmt("spring frequency %.2f Hz, expected 2", measuredHz));
}

void testOrbitStaysCircular() {
    World w;
    w.gravity = {};
    w.allowSleep = false;
    w.substeps = 8;
    const float gm = 100.0f;
    const float r = 10.0f;
    BodyDef d;
    d.collides = false;
    d.angularDamping = 0.0f;
    d.pos = {r, 0.0f};
    d.vel = {0.0f, std::sqrt(gm / r)};
    const int planet = w.createBody(d);
    w.forceField = [gm](World& world, float h) {
        for (Body& b : world.bodies()) {
            if (!b.alive || !b.isDynamic()) continue;
            const float distSq = lengthSq(b.pos);
            b.vel -= b.pos * (gm / (distSq * std::sqrt(distSq)) * h);
        }
    };
    float lo = r, hi = r;
    for (int i = 0; i < 60 * 60; ++i) {
        w.step(kDt);
        const float d2 = length(w.bodies()[planet].pos);
        lo = std::min(lo, d2);
        hi = std::max(hi, d2);
    }
    check(hi - lo < 0.1f, fmt("orbit radius wandered between %.3f and %.3f", lo, hi));
}

void testDestroyedSlotsAreReusedSafely() {
    World w;
    addGround(w);
    std::vector<int> ids;
    for (int i = 0; i < 50; ++i) {
        BodyDef d;
        d.pos = {static_cast<float>(i % 7) - 3.0f, 1.0f + static_cast<float>(i) * 1.1f};
        ids.push_back(w.createBody(d));
    }
    for (int round = 0; round < 40; ++round) {
        run(w, 0.1f);
        w.destroyBody(ids[static_cast<size_t>(round) % ids.size()]);
        BodyDef d;
        d.shape = makeBox(0.3f, 0.3f);
        d.pos = {0.0f, 12.0f};
        ids[static_cast<size_t>(round) % ids.size()] = w.createBody(d);
    }
    run(w, 3.0f);
    bool finite = true;
    for (const Body& b : w.bodies()) {
        if (b.alive && !(std::isfinite(b.pos.x) && std::isfinite(b.pos.y))) finite = false;
    }
    check(finite, "positions should stay finite");
}

void testPolygonRecentred() {
    const Vec2 pts[4] = {{10.0f, 10.0f}, {10.0f, 12.0f}, {12.0f, 12.0f}, {12.0f, 10.0f}};  // clockwise
    const Shape s = makePolygon(pts, 4);
    Vec2 sum;
    for (int i = 0; i < s.count; ++i) sum += s.verts[i];
    check(length(sum) < 1e-4f, "polygon should be centred on its centroid");
    const MassData m = computeMass(s, 1.0f);
    check(m.mass > 0.0f, "clockwise input should be rewound and give positive mass");
}

void testSleepPerformanceIsStable() {
    // A pile of mixed shapes must settle rather than jitter forever.
    World w;
    addGround(w);
    BodyDef wall;
    wall.type = BodyType::Static;
    wall.shape = makeBox(0.5f, 20.0f);
    wall.pos = {-8.5f, 20.0f};
    w.createBody(wall);
    wall.pos = {8.5f, 20.0f};
    w.createBody(wall);

    for (int i = 0; i < 200; ++i) {
        BodyDef d;
        d.pos = {-6.0f + static_cast<float>(i % 13), 1.0f + static_cast<float>(i / 13) * 1.2f};
        if (i % 3 == 0) d.shape = makeCircle(0.45f);
        else if (i % 3 == 1) d.shape = makeBox(0.45f, 0.45f);
        else d.shape = makeRegularPolygon(5, 0.5f);
        w.createBody(d);
    }
    run(w, 20.0f);
    check(w.stats().awakeBodies < 20, fmt("%.0f of 200 bodies still awake after 20 s", w.stats().awakeBodies));
    std::printf("    pile of 200: %d still awake after 20 s\n", w.stats().awakeBodies);
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"free fall matches the analytic solution", testFreeFall},
        {"mass properties of a box", testMassProperties},
        {"polygon input is rewound and re-centred", testPolygonRecentred},
        {"a resting box stays put and sleeps", testRestingBoxSleeps},
        {"a sleeping body wakes when hit", testWakeOnImpact},
        {"a sleeping stack falls when its support is removed", testRemovedSupportWakes},
        {"a pyramid stands", testPyramidStands},
        {"box grips a shallow slope", [] { testFrictionOnSlope(20.0f, false); }},
        {"box slides down a steep slope", [] { testFrictionOnSlope(35.0f, true); }},
        {"equal balls swap velocity", testElasticBallsSwapVelocity},
        {"elastic collision conserves momentum and energy", testMomentumConservedInElasticCollision},
        {"fast bodies do not tunnel through thin walls", testNoTunnelling},
        {"a pendulum conserves energy", testPendulumConservesEnergy},
        {"a chain holds a heavy ball without stretching", testHeavyChainHolds},
        {"Newton's cradle passes the impact down the line", testNewtonsCradle},
        {"a motor reaches its target speed", testMotorDrivesRotor},
        {"a revolute limit stops the arm", testJointLimit},
        {"a spring oscillates at its set frequency", testSpringFrequency},
        {"an orbit stays circular", testOrbitStaysCircular},
        {"destroying bodies and reusing slots is safe", testDestroyedSlotsAreReusedSafely},
        {"a mixed pile settles", testSleepPerformanceIsStable},
    };

    for (const auto& [name, fn] : tests) {
        const int before = failures;
        fn();
        std::printf("%s  %s\n", failures == before ? "ok  " : "FAIL", name);
    }
    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
