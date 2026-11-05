// Runs every scene headless and checks that it does what its description claims.
#include <cmath>
#include <cstdio>

#include "scenes/scene.hpp"

using namespace scene;
using phys::Body;

namespace {

constexpr float kDt = 1.0f / 60.0f;
int failures = 0;

void expect(bool ok, const char* name, const char* what, double value = 0.0) {
    if (ok) return;
    ++failures;
    std::printf("    FAIL  %s: %s (%.3f)\n", name, what, value);
}

void advance(Scene& s, float seconds) {
    const Input input;
    const int steps = static_cast<int>(std::lround(seconds / kDt));
    for (int i = 0; i < steps; ++i) {
        if (s.update) s.update(s, input, kDt);
        s.world.step(kDt);
        s.time += kDt;
    }
}

bool allFinite(const Scene& s) {
    for (const Body& b : s.world.bodies()) {
        if (!b.alive) continue;
        if (!std::isfinite(b.pos.x) || !std::isfinite(b.pos.y) || !std::isfinite(b.angle)) return false;
    }
    return true;
}

int findScene(const char* name) {
    const auto& list = sceneList();
    for (size_t i = 0; i < list.size(); ++i) {
        if (std::string_view(list[i].name) == name) return static_cast<int>(i);
    }
    std::printf("no scene named %s\n", name);
    std::abort();
}

void testEverySceneRuns() {
    const auto& list = sceneList();
    Scene s;
    for (size_t i = 0; i < list.size(); ++i) {
        loadScene(s, static_cast<int>(i));
        float worst = 0.0f;
        const int steps = 60 * 12;
        const Input input;
        for (int k = 0; k < steps; ++k) {
            if (s.update) s.update(s, input, kDt);
            s.world.step(kDt);
            s.time += kDt;
            worst = std::max(worst, s.world.stats().collideMs + s.world.stats().solveMs);
        }
        expect(allFinite(s), list[i].name, "positions went non-finite");
        std::printf("  %-18s %5d bodies  %5d joints  %5d contacts  %4.1f ms worst step\n", list[i].name,
                    s.world.stats().bodies, s.world.stats().joints, s.world.stats().contacts, worst);
    }
}

void testCradle() {
    Scene s;
    loadScene(s, findScene("Newton's Cradle"));
    std::vector<int> balls;
    for (int i = 0; i < static_cast<int>(s.world.bodies().size()); ++i) {
        const Body& b = s.world.bodies()[i];
        if (b.alive && b.isDynamic()) balls.push_back(i);
    }
    const float start = s.world.bodies()[balls.front()].pos.y;
    const float bottom = s.world.bodies()[balls.back()].pos.y;
    float peak = 0.0f, middle = 0.0f;
    for (int i = 0; i < 60 * 4; ++i) {  // half a swing is 3.3 s
        advance(s, kDt);
        peak = std::max(peak, s.world.bodies()[balls.back()].pos.y);
        middle = std::max(middle, s.world.bodies()[balls[2]].pos.y - bottom);
    }
    const float share = (peak - bottom) / (start - bottom);
    expect(share > 0.93f, "cradle", "far ball should rise as high as the first was released", share);
    expect(middle < 0.1f, "cradle", "middle ball should barely move", middle);
    std::printf("  cradle: far ball rises to %.1f%% of release height; middle ball lifts %.3f m\n", 100.0f * share, middle);
}

void testPyramidFalls() {
    Scene s;
    loadScene(s, findScene("Pyramid"));
    advance(s, 2.5f);
    int moved = 0;
    for (const Body& b : s.world.bodies()) {
        if (b.alive && b.isDynamic() && std::abs(b.angle) > 0.05f) ++moved;
    }
    expect(moved == 0, "pyramid", "should stand before the cannonball arrives", moved);
    advance(s, 3.0f);
    moved = 0;
    for (const Body& b : s.world.bodies()) {
        if (b.alive && b.isDynamic() && std::abs(b.angle) > 0.05f) ++moved;
    }
    expect(moved > 30, "pyramid", "cannonball should bring it down", moved);
    std::printf("  pyramid: %d boxes knocked over by the cannonball\n", moved);
}

void testDominoes() {
    Scene s;
    loadScene(s, findScene("Dominoes"));
    int last = -1;
    for (int i = 0; i < static_cast<int>(s.world.bodies().size()); ++i) {
        const Body& b = s.world.bodies()[i];
        if (b.alive && b.isDynamic()) last = i;
    }
    advance(s, 14.0f);
    int fallen = 0, total = 0;
    for (const Body& b : s.world.bodies()) {
        if (!b.alive || !b.isDynamic() || b.shape.type != phys::ShapeType::Polygon) continue;
        ++total;
        if (std::abs(b.angle) > 0.5f) ++fallen;
    }
    expect(fallen == total, "dominoes", "every domino should topple", fallen);
    expect(std::abs(s.world.bodies()[last].angle) > 0.5f, "dominoes", "last domino should fall");
    std::printf("  dominoes: %d of %d toppled\n", fallen, total);
}

void testGalton() {
    Scene s;
    loadScene(s, findScene("Galton Board"));
    advance(s, 26.0f);
    // Mean and spread of where the balls landed, against the binomial prediction.
    const float theory = 0.5f * std::sqrt(15.0f);
    float sum = 0.0f, sumSq = 0.0f;
    int n = 0, escaped = 0;
    for (const Body& b : s.world.bodies()) {
        if (!b.alive || !b.isDynamic()) continue;
        if (std::abs(b.pos.x) > 7.8f) ++escaped;
        if (b.pos.y > 12.0f) continue;
        sum += b.pos.x;
        sumSq += b.pos.x * b.pos.x;
        ++n;
    }
    const float mean = sum / static_cast<float>(n);
    const float sd = std::sqrt(sumSq / static_cast<float>(n) - mean * mean);
    expect(n >= 335, "galton", "almost all balls should reach the bins", n);
    expect(std::abs(mean) < 0.3f, "galton", "pile should be centred", mean);
    expect(std::abs(sd - theory) < 0.3f, "galton", "spread should match the binomial", sd);
    expect(escaped == 0, "galton", "no ball should leave the board", escaped);
    std::printf("  galton: %d balls landed, mean %.2f, sd %.2f (binomial predicts %.2f)\n", n, mean, sd, theory);
}

void testDrumStaysRound() {
    Scene s;
    loadScene(s, findScene("Tumbling Drum"));
    advance(s, 15.0f);
    int escaped = 0;
    for (const Body& b : s.world.bodies()) {
        if (!b.alive || !b.isDynamic()) continue;
        if (phys::length(b.pos - Vec2{0.0f, 11.8f}) > 12.5f) ++escaped;
    }
    expect(escaped == 0, "drum", "no ball should leave the drum", escaped);
    std::printf("  drum: %d escaped\n", escaped);
}

void testOrbitsStayBound() {
    Scene s;
    loadScene(s, findScene("Binary Stars"));
    advance(s, 60.0f);
    int lost = 0, total = 0;
    for (const Body& b : s.world.bodies()) {
        if (!b.alive || !b.isDynamic()) continue;
        ++total;
        if (phys::length(b.pos) > 90.0f) ++lost;
    }
    expect(lost < total / 20, "orbits", "most bodies should stay in orbit for a minute", lost);
    std::printf("  orbits: %d of %d flung out after 60 s\n", lost, total);
}

void testChaosDiverges() {
    Scene s;
    loadScene(s, findScene("Chaos"));
    std::vector<int> bobs;
    for (int i = 0; i < static_cast<int>(s.world.bodies().size()); ++i) {
        const Body& b = s.world.bodies()[i];
        if (b.alive && b.isDynamic() && b.trail) bobs.push_back(i);
    }
    const float energyStart = s.world.totalEnergy();
    auto spread = [&] {
        float most = 0.0f;
        for (const int id : bobs) {
            most = std::max(most, phys::length(s.world.bodies()[id].pos - s.world.bodies()[bobs[0]].pos));
        }
        return most;
    };
    advance(s, 3.0f);
    const float early = spread();
    advance(s, 22.0f);
    const float late = spread();
    const float energyDrift = std::abs(s.world.totalEnergy() - energyStart);
    expect(early < 1.5f, "chaos", "pendulums should agree at first", early);
    expect(late > 6.0f, "chaos", "pendulums should end up nowhere near each other", late);
    std::printf("  chaos: spread %.2f m at 3 s, %.2f m at 25 s; energy drift %.1f of %.1f\n", early, late, energyDrift,
                std::abs(energyStart));
}

void testRoverDrives() {
    Scene s;
    loadScene(s, findScene("Rover"));
    const int car = s.follow;
    float upside = 0.0f;
    advance(s, 2.0f);
    const float x0 = s.world.bodies()[car].pos.x;
    for (int i = 0; i < 60 * 70; ++i) {
        advance(s, kDt);
        upside = std::max(upside, std::abs(std::remainder(s.world.bodies()[car].angle, 2.0f * phys::kPi)));
    }
    const float travelled = s.world.bodies()[car].pos.x - x0;
    expect(travelled > 450.0f, "rover", "should keep driving over the hills", travelled);
    expect(upside < 2.0f, "rover", "should not end up on its roof", upside);
    std::printf("  rover: drove %.0f m in 70 s, worst tilt %.2f rad, %.0f m/s at the end\n", travelled, upside,
                phys::length(s.world.bodies()[car].vel));
}

void testJellyHoldsTogether() {
    Scene s;
    loadScene(s, findScene("Jelly"));
    advance(s, 14.0f);
    float worst = 0.0f;
    for (const Blob& blob : s.blobs) {
        const Vec2 c = s.world.bodies()[blob.centre].pos;
        for (const int id : blob.ring) worst = std::max(worst, phys::length(s.world.bodies()[id].pos - c));
    }
    expect(worst < 6.0f, "jelly", "blobs should not burst", worst);
    std::printf("  jelly: furthest ring particle %.2f m from its centre\n", worst);
}

}  // namespace

int main() {
    testEverySceneRuns();
    std::puts("");
    testCradle();
    testPyramidFalls();
    testDominoes();
    testGalton();
    testDrumStaysRound();
    testOrbitsStayBound();
    testChaosDiverges();
    testRoverDrives();
    testJellyHoldsTogether();
    std::printf("\n%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
