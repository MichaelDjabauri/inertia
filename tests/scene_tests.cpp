// Runs every lesson headless and checks that what it measures agrees with the theory it teaches.
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "scenes/scene.hpp"

using namespace scene;
using phys::Body;

namespace {

constexpr float kDt = 1.0f / 60.0f;
int failures = 0;

void expect(bool ok, const char* name, const std::string& what, double value = 0.0) {
    if (ok) return;
    ++failures;
    std::printf("    FAIL  %s: %s (%.4f)\n", name, what.c_str(), value);
}

void advance(Scene& s, float seconds) {
    const int steps = static_cast<int>(std::lround(seconds / kDt));
    for (int i = 0; i < steps; ++i) s.step(kDt);
}

int findScene(const char* name) {
    const auto& list = sceneList();
    for (size_t i = 0; i < list.size(); ++i) {
        if (std::string_view(list[i].name) == name) return static_cast<int>(i);
    }
    std::printf("no scene named %s\n", name);
    std::abort();
}

void load(Scene& s, const char* name, const std::vector<float>& presets = {}) {
    loadScene(s, findScene(name), presets);
}

const Readout* find(const Scene& s, const char* lesson, const char* label) {
    const Readout* r = s.readout(label);
    expect(r != nullptr, lesson, std::string("no readout called ") + label);
    return r;
}

// The measurement is within `tolerance` of the theory, relative to the theory's size.
void agrees(const Scene& s, const char* lesson, const char* label, float tolerance) {
    const Readout* r = find(s, lesson, label);
    if (r == nullptr) return;
    const float error = std::abs(r->value - r->expected) / std::max(std::abs(r->expected), 1e-3f);
    expect(std::isfinite(r->value) && error <= tolerance, lesson, std::string(label) + " should match the theory", r->value);
    std::printf("  %-18s %-24s %10.3f  theory %10.3f  (%5.2f%% off)\n", lesson, label, r->value, r->expected, 100.0f * error);
}

// The measurement is within `tolerance` of `target`, in its own units.
void near(const Scene& s, const char* lesson, const char* label, float target, float tolerance) {
    const Readout* r = find(s, lesson, label);
    if (r == nullptr) return;
    expect(std::isfinite(r->value) && std::abs(r->value - target) <= tolerance, lesson,
           std::string(label) + " should be near " + std::to_string(target), r->value);
    std::printf("  %-18s %-24s %10.3f  wanted %10.3f\n", lesson, label, r->value, target);
}

float value(const Scene& s, const char* lesson, const char* label) {
    const Readout* r = find(s, lesson, label);
    return r != nullptr ? r->value : NAN;
}

bool allFinite(const Scene& s) {
    for (const Body& b : s.world.bodies()) {
        if (!b.alive) continue;
        if (!std::isfinite(b.pos.x) || !std::isfinite(b.pos.y) || !std::isfinite(b.angle)) return false;
    }
    return true;
}

void testEverySceneRuns() {
    const auto& list = sceneList();
    Scene s;
    for (size_t i = 0; i < list.size(); ++i) {
        loadScene(s, static_cast<int>(i));
        expect(!s.readouts.empty(), list[i].name, "should measure something");
        float worst = 0.0f;
        for (int k = 0; k < 60 * 12; ++k) {
            s.step(kDt);
            worst = std::max(worst, s.world.stats().collideMs + s.world.stats().solveMs);
        }
        expect(allFinite(s), list[i].name, "positions went non-finite");
        std::printf("  %-18s %5d bodies  %5d joints  %5d contacts  %4.1f ms worst step\n", list[i].name,
                    s.world.stats().bodies, s.world.stats().joints, s.world.stats().contacts, worst);
    }
}

void testProjectile() {
    Scene s;
    load(s, "Projectile Motion");
    advance(s, 4.0f);
    agrees(s, "projectile", "Range", 0.01f);
    agrees(s, "projectile", "Time of flight", 0.01f);
    agrees(s, "projectile", "Highest point", 0.01f);

    // Complementary angles land in the same place.
    load(s, "Projectile Motion", {12.0f, 30.0f});
    advance(s, 3.0f);
    const float low = value(s, "projectile", "Range");
    load(s, "Projectile Motion", {12.0f, 60.0f});
    advance(s, 3.5f);
    const float high = value(s, "projectile", "Range");
    expect(std::abs(low - high) < 0.02f * low, "projectile", "30° and 60° should land together", high - low);
    std::printf("  %-18s 30° lands at %.2f m, 60° at %.2f m\n", "projectile", low, high);
}

void testFriction() {
    Scene s;
    load(s, "Friction on a Slope");  // 18°: only μ 0.2 slides
    advance(s, 2.0f);
    agrees(s, "friction 18°", "a, block μ = 0.2", 0.03f);
    near(s, "friction 18°", "a, block μ = 0.4", 0.0f, 0.02f);
    near(s, "friction 18°", "a, block μ = 0.6", 0.0f, 0.02f);

    load(s, "Friction on a Slope", {30.0f});
    advance(s, 1.5f);
    agrees(s, "friction 30°", "a, block μ = 0.2", 0.03f);
    agrees(s, "friction 30°", "a, block μ = 0.4", 0.03f);
    near(s, "friction 30°", "a, block μ = 0.6", 0.0f, 0.02f);
}

void testBalance() {
    Scene s;
    load(s, "Centre of Mass");
    advance(s, 5.0f);
    const Readout* r = find(s, "balance", "Stack");
    expect(r != nullptr && r->text == "standing", "balance", "a stack at 90% of the limit should stand");
    agrees(s, "balance", "Top block reaches", 0.02f);

    load(s, "Centre of Mass", {10.0f, 95.0f});
    advance(s, 5.0f);
    r = find(s, "balance 10", "Stack");
    expect(r != nullptr && r->text == "standing", "balance", "ten blocks at 95% should still stand");

    load(s, "Centre of Mass", {6.0f, 115.0f});
    advance(s, 5.0f);
    r = find(s, "balance", "Stack");
    expect(r != nullptr && r->text == "toppled", "balance", "a stack past the limit should fall");
}

void testChain() {
    Scene s;
    load(s, "Hanging Chain");
    advance(s, 6.0f);
    agrees(s, "chain", "Lift at each anchor", 0.03f);
    agrees(s, "chain", "Sideways pull H", 0.05f);
    agrees(s, "chain", "Tension at anchor", 0.05f);
    agrees(s, "chain", "Tension at bottom", 0.06f);
    agrees(s, "chain", "Sag", 0.05f);

    load(s, "Hanging Chain", {120.0f, 20.0f});
    advance(s, 20.0f);  // the weight bounces for a while first
    agrees(s, "chain + 20 kg", "Lift at each anchor", 0.03f);
}

void testCollisions() {
    Scene s;
    load(s, "Collisions");
    advance(s, 2.5f);
    agrees(s, "collision 1:1", "Momentum after", 0.01f);
    agrees(s, "collision 1:1", "Energy after", 0.02f);
    near(s, "collision 1:1", "A's velocity after", 0.0f, 0.1f);
    agrees(s, "collision 1:1", "B's velocity after", 0.02f);

    load(s, "Collisions", {2.0f, 6.0f, 0.5f});
    advance(s, 2.5f);
    agrees(s, "collision 1:3", "Momentum after", 0.01f);
    agrees(s, "collision 1:3", "Energy after", 0.04f);
    agrees(s, "collision 1:3", "A's velocity after", 0.04f);
    agrees(s, "collision 1:3", "B's velocity after", 0.03f);

    load(s, "Collisions", {2.0f, 2.0f, 0.0f});
    advance(s, 2.5f);
    agrees(s, "collision e = 0", "Momentum after", 0.01f);
    agrees(s, "collision e = 0", "Energy after", 0.04f);
}

void testCradle() {
    Scene s;
    for (const float lifted : {1.0f, 2.0f}) {
        load(s, "Newton's Cradle", {lifted});
        advance(s, 5.0f);
        near(s, lifted == 1.0f ? "cradle, 1 lifted" : "cradle, 2 lifted", "Balls swinging out", lifted, 0.0f);
        agrees(s, lifted == 1.0f ? "cradle, 1 lifted" : "cradle, 2 lifted", "Rebound height", 0.07f);
    }
}

void testPendulum() {
    Scene s;
    load(s, "Pendulum");
    advance(s, 12.0f);
    agrees(s, "pendulum 30°", "Period T", 0.01f);
    agrees(s, "pendulum 30°", "Total energy", 0.01f);

    load(s, "Pendulum", {8.0f, 9.0f, 80.0f});
    advance(s, 12.0f);
    agrees(s, "pendulum 80°", "Period T", 0.01f);
    const float formula = value(s, "pendulum 80°", "2π√(L/g)");
    expect(value(s, "pendulum 80°", "Period T") > 1.1f * formula, "pendulum", "big swings should take longer");
}

void testSprings() {
    Scene s;
    load(s, "Springs & Damping");
    advance(s, 10.0f);
    agrees(s, "springs", "Period, ζ = 0.10", 0.02f);
    agrees(s, "springs", "Settles, ζ = 1", 0.06f);
    agrees(s, "springs", "Settles, ζ = 2.5", 0.06f);

    load(s, "Springs & Damping", {80.0f, 1.0f, 0.05f});
    advance(s, 6.0f);
    agrees(s, "stiff springs", "Period, ζ = 0.05", 0.02f);
}

void testDominoes() {
    Scene s;
    for (const float ratio : {1.0f, 1.1f, 1.2f, 1.3f}) {
        load(s, "Dominoes", {ratio});
        advance(s, 16.0f);
        const Readout* r = find(s, "dominoes", "Dominoes fallen");
        if (r == nullptr) continue;
        // The lesson says the chain breaks once each domino is too much bigger than the last.
        if (ratio < 1.25f) {
            expect(r->value == r->expected, "dominoes", "every domino should fall at ratio " + std::to_string(ratio), r->value);
        } else {
            expect(r->value < r->expected, "dominoes", "the chain should stop at ratio " + std::to_string(ratio), r->value);
        }
        std::printf("  %-18s ratio %.2f: %.0f of %.0f fell, energy amplified %.0f×\n", "dominoes", ratio, r->value, r->expected,
                    value(s, "dominoes", "Amplification"));
    }
}

void testOrbit() {
    Scene s;
    load(s, "Orbits");
    advance(s, 12.0f);
    agrees(s, "orbit 85%", "Period", 0.01f);
    agrees(s, "orbit 85%", "r × v", 0.005f);
    agrees(s, "orbit 85%", "Energy per kg", 0.01f);
    agrees(s, "orbit 85%", "Area per interval", 0.01f);

    load(s, "Orbits", {150.0f});
    advance(s, 10.0f);
    expect(value(s, "orbit 150%", "Distance r") > 50.0f, "orbit", "150% of circular speed should escape");
}

void testGalton() {
    Scene s;
    load(s, "Galton Board");
    advance(s, 26.0f);
    expect(value(s, "galton", "Balls landed") >= 335.0f, "galton", "almost all balls should reach the bins");
    near(s, "galton", "Mean position", 0.0f, 0.3f);
    agrees(s, "galton", "Spread σ", 0.1f);
}

void testDrum() {
    Scene s;
    load(s, "Mixing & Entropy");
    advance(s, 1.0f);
    const float start = value(s, "drum", "Mixing entropy");
    advance(s, 40.0f);
    const float mixed = value(s, "drum", "Mixing entropy");
    s.params[0].value = -s.params[0].value;  // run it backwards
    advance(s, 20.0f);
    const float reversed = value(s, "drum", "Mixing entropy");
    expect(start < 0.45f, "drum", "should start sorted", start);
    expect(mixed > 0.75f, "drum", "should end up mixed", mixed);
    expect(reversed > 0.7f, "drum", "running it backwards should not unmix it", reversed);
    int escaped = 0;
    for (const Body& b : s.world.bodies()) {
        if (b.alive && b.isDynamic() && phys::length(b.pos - Vec2{0.0f, 11.8f}) > 12.5f) ++escaped;
    }
    expect(escaped == 0, "drum", "no ball should leave the drum", escaped);
    std::printf("  %-18s entropy %.2f at 1 s, %.2f at 41 s, %.2f after 20 s in reverse\n", "drum", start, mixed, reversed);
}

void testChaos() {
    Scene s;
    load(s, "Chaos");
    auto gap = [&]() {
        float most = 0.0f;
        std::vector<int> bobs;
        for (int i = 0; i < static_cast<int>(s.world.bodies().size()); ++i) {
            if (s.world.bodies()[i].trail) bobs.push_back(i);
        }
        return phys::length(s.world.bodies()[bobs.front()].pos - s.world.bodies()[bobs.back()].pos) + most;
    };
    advance(s, 3.0f);
    const float early = gap();
    advance(s, 22.0f);
    const float late = gap();
    expect(early < 1.5f, "chaos", "pendulums should agree at first", early);
    expect(late > 3.0f, "chaos", "pendulums should end up far apart", late);

    load(s, "Chaos", {20.0f, -3.0f});
    advance(s, 25.0f);
    const float calm = gap();
    expect(calm < 0.5f, "chaos", "small swings should not be chaotic", calm);
    std::printf("  %-18s at 143°: gap %.3f m at 3 s, %.2f m at 25 s; at 20°: %.4f m at 25 s\n", "chaos", early, late, calm);
}

}  // namespace

int main() {
    testEverySceneRuns();
    std::puts("");
    testProjectile();
    testFriction();
    testBalance();
    testChain();
    testCollisions();
    testCradle();
    testPendulum();
    testSprings();
    testDominoes();
    testOrbit();
    testGalton();
    testDrum();
    testChaos();
    std::printf("\n%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
