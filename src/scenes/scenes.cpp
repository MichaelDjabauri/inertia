#include <algorithm>
#include <cmath>
#include <memory>
#include <random>

#include "scene.hpp"

namespace scene {

using phys::Body;
using phys::BodyDef;
using phys::BodyType;
using phys::kPi;
using phys::length;
using phys::makeBox;
using phys::makeCircle;
using phys::makeRegularPolygon;
using phys::normalized;
using phys::Shape;
using phys::World;

uint32_t sampleRamp(const std::vector<uint32_t>& ramp, float t) {
    if (ramp.empty()) return 0xFFFFFF;
    if (ramp.size() == 1) return ramp[0];
    const float scaled = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(ramp.size() - 1);
    const int i = std::min(static_cast<int>(scaled), static_cast<int>(ramp.size()) - 2);
    const float f = scaled - static_cast<float>(i);
    const uint32_t a = ramp[i], b = ramp[i + 1];
    auto channel = [&](int shift) {
        const float ca = static_cast<float>((a >> shift) & 0xFF);
        const float cb = static_cast<float>((b >> shift) & 0xFF);
        return static_cast<uint32_t>(ca + (cb - ca) * f + 0.5f);
    };
    return (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

namespace {

// Each scene gets its own mood, all from the same dark family.
Style styleDusk() {
    Style s;
    s.top = 0x1A1C33;
    s.bottom = 0x0C0D18;
    s.glow = 0x241E3A;
    s.ramp = {0xFFC15E, 0xFF8552, 0xF2545B, 0xC9417F, 0x7D4CDB};
    s.solid = 0x1D1E33;
    s.solidRim = 0xFFB98A;
    return s;
}

Style styleLagoon() {
    Style s;
    s.top = 0x0B2730;
    s.bottom = 0x061311;
    s.glow = 0x0D3338;
    s.ramp = {0xB6FFD8, 0x5FF0C2, 0x2CCFC7, 0x239BFF, 0x3F67F5};
    s.solid = 0x0F2429;
    s.solidRim = 0x7CF5D8;
    s.joint = 0xB7FFEA;
    return s;
}

Style styleEmber() {
    Style s;
    s.top = 0x241510;
    s.bottom = 0x0E0908;
    s.glow = 0x33180E;
    s.ramp = {0xFFE9A8, 0xFFC04D, 0xFF8A3D, 0xF2502F, 0xB9222F};
    s.solid = 0x241713;
    s.solidRim = 0xFFB070;
    s.joint = 0xFFD9B0;
    return s;
}

Style styleGlacier() {
    Style s;
    s.top = 0x101E33;
    s.bottom = 0x070C16;
    s.glow = 0x142B48;
    s.ramp = {0xF2FAFF, 0xB5E3FF, 0x74BEFF, 0x4A8DFF, 0x4C5CF0};
    s.solid = 0x121D2E;
    s.solidRim = 0xA8D8FF;
    s.joint = 0xCDE9FF;
    return s;
}

Style styleOrchid() {
    Style s;
    s.top = 0x1E1230;
    s.bottom = 0x0B0714;
    s.glow = 0x2C1646;
    s.ramp = {0xFFD9F4, 0xFF8ADB, 0xD36BFF, 0x8E5CFF, 0x4F63F7};
    s.solid = 0x1E142D;
    s.solidRim = 0xE7A8FF;
    s.joint = 0xF0D0FF;
    return s;
}

Style styleMoss() {
    Style s;
    s.top = 0x131F15;
    s.bottom = 0x080C07;
    s.glow = 0x1B2C1A;
    s.ramp = {0xF0FFA8, 0xB8EC5A, 0x5ED18A, 0x27B3A0, 0x1E8AA8};
    s.solid = 0x162117;
    s.solidRim = 0xBEEF82;
    s.joint = 0xDDF7B8;
    return s;
}

int addSolid(Scene& s, const Shape& shape, Vec2 pos, float angle = 0.0f) {
    BodyDef d;
    d.type = BodyType::Static;
    d.shape = shape;
    d.pos = pos;
    d.angle = angle;
    d.friction = 0.7f;
    d.color = s.style.solid;
    return s.world.createBody(d);
}

int addFloor(Scene& s, float y = 0.0f, float halfWidth = 120.0f) {
    return addSolid(s, makeBox(halfWidth, 12.0f), {0.0f, y - 12.0f});
}

// A static plank running between two points.
int addBeam(Scene& s, Vec2 a, Vec2 b, float thickness) {
    const Vec2 d = b - a;
    return addSolid(s, makeBox(length(d) * 0.5f, thickness * 0.5f), (a + b) * 0.5f, std::atan2(d.y, d.x));
}

// Pins a static post in place purely as decoration.
int addProp(Scene& s, const Shape& shape, Vec2 pos) {
    BodyDef d;
    d.type = BodyType::Static;
    d.shape = shape;
    d.pos = pos;
    d.collides = false;
    d.color = s.style.solid;
    return s.world.createBody(d);
}

// Vertex count must not exceed phys::kMaxPolygonVerts.
Shape randomGem(std::mt19937& rng, float radius) {
    std::uniform_int_distribution<int> sides(5, 8);
    std::uniform_real_distribution<float> start(0.0f, 2.0f * kPi);
    return makeRegularPolygon(sides(rng), radius, start(rng));
}

// ---------------------------------------------------------------------- scenes

void buildPlayground(Scene& s) {
    s.style = styleDusk();
    s.viewHeight = 26.0f;
    s.camera = {0.0f, 9.6f};
    s.startTool = Tool::Ball;

    addFloor(s);
    addBeam(s, {-20.0f, 13.0f}, {-6.0f, 7.5f}, 0.5f);
    addBeam(s, {20.0f, 9.0f}, {7.0f, 4.0f}, 0.5f);

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int i = 0; i < 90; ++i) {
        BodyDef d;
        d.pos = {-14.0f + unit(rng) * 28.0f, 18.0f + static_cast<float>(i) * 0.55f};
        d.angle = unit(rng) * 6.28f;
        d.restitution = 0.25f;
        d.color = sampleRamp(s.style.ramp, unit(rng));
        const float size = 0.4f + unit(rng) * 0.5f;
        switch (i % 3) {
            case 0: d.shape = makeCircle(size); break;
            case 1: d.shape = makeBox(size * 0.9f, size * 0.7f); break;
            default: d.shape = randomGem(rng, size * 1.1f); break;
        }
        s.world.createBody(d);
    }
}

void buildPyramid(Scene& s) {
    s.style = styleEmber();
    s.viewHeight = 21.0f;
    s.camera = {0.0f, 7.8f};
    s.startTool = Tool::Ball;
    addFloor(s);

    const int rows = 16;
    const float hw = 0.55f, hh = 0.42f;
    for (int row = 0; row < rows; ++row) {
        const int count = rows - row;
        for (int i = 0; i < count; ++i) {
            BodyDef d;
            d.shape = makeBox(hw, hh);
            d.pos = {(static_cast<float>(i) - 0.5f * static_cast<float>(count - 1)) * (2.0f * hw + 0.04f),
                     hh + static_cast<float>(row) * 2.0f * hh};
            d.friction = 0.6f;
            d.restitution = 0.0f;
            d.color = sampleRamp(s.style.ramp, 1.0f - static_cast<float>(row) / static_cast<float>(rows - 1));
            s.world.createBody(d);
        }
    }

    // After a moment, a cannonball arrives.
    auto fired = std::make_shared<bool>(false);
    s.update = [fired](Scene& sc, const Input&, float) {
        if (*fired || sc.time < 3.0f) return;
        *fired = true;
        BodyDef d;
        d.shape = makeCircle(1.1f);
        d.pos = {-28.0f, 4.0f};
        d.vel = {46.0f, 5.0f};
        d.density = 7.0f;
        d.restitution = 0.2f;
        d.color = 0xFFF3D6;
        d.glow = 0.7f;
        d.trail = true;
        sc.world.createBody(d);
    };
}

void buildDominoes(Scene& s) {
    s.style = styleDusk();
    s.style.ramp = {0xFF8A5C, 0xFFB35C, 0xFFD97A, 0xFFF0B5};
    s.viewHeight = 23.0f;
    s.viewWidth = 40.0f;
    s.camera = {0.0f, 8.4f};
    addFloor(s);

    // Each domino is about 10% taller than the last. A tiny push grows into a slam.
    const int count = 23;
    const float ratio = 1.1f;
    float height = 0.8f;
    float x = -17.0f;
    for (int i = 0; i < count; ++i) {
        BodyDef d;
        const float width = 0.2f * height;
        d.shape = makeBox(width * 0.5f, height * 0.5f);
        d.pos = {x, height * 0.5f};
        d.friction = 0.5f;
        d.restitution = 0.0f;
        d.color = sampleRamp(s.style.ramp, static_cast<float>(i) / static_cast<float>(count - 1));
        s.world.createBody(d);
        x += height * 0.55f;
        height *= ratio;
    }

    auto fired = std::make_shared<bool>(false);
    s.update = [fired](Scene& sc, const Input&, float) {
        if (*fired || sc.time < 1.2f) return;
        *fired = true;
        BodyDef d;
        d.shape = makeCircle(0.25f);
        d.pos = {-20.5f, 0.25f};
        d.vel = {9.0f, 0.0f};
        d.density = 2.0f;
        d.color = 0xFFF3D6;
        d.glow = 0.6f;
        d.trail = true;
        sc.world.createBody(d);
    };
}

void buildCradle(Scene& s) {
    s.style = styleGlacier();
    s.viewHeight = 27.0f;
    s.camera = {0.0f, 9.9f};
    s.world.allowSleep = false;
    s.world.substeps = 8;
    addFloor(s);

    const int count = 5;
    const float radius = 1.3f;
    const float gap = 0.1f;  // just wider than a ball travels in a step, so each hit gets its own step
    const float rod = 11.0f;
    const float barY = 19.5f;
    const float release = 0.85f;
    const float span = (static_cast<float>(count) - 1.0f) * (2.0f * radius + gap);

    addProp(s, makeBox(span * 0.5f + 3.0f, 0.16f), {0.0f, barY + 0.16f});
    addProp(s, makeBox(0.16f, barY * 0.5f), {-span * 0.5f - 3.0f, barY * 0.5f});
    addProp(s, makeBox(0.16f, barY * 0.5f), {span * 0.5f + 3.0f, barY * 0.5f});

    for (int i = 0; i < count; ++i) {
        const float x = -span * 0.5f + static_cast<float>(i) * (2.0f * radius + gap);
        BodyDef d;
        d.shape = makeCircle(radius);
        d.restitution = 1.0f;
        d.friction = 0.0f;
        d.angularDamping = 0.0f;
        d.pos = {x, barY - rod};
        if (i == 0) d.pos = {x - rod * std::sin(release), barY - rod * std::cos(release)};
        d.color = sampleRamp(s.style.ramp, static_cast<float>(i) / static_cast<float>(count - 1) * 0.75f);
        const int ball = s.world.createBody(d);
        s.world.createDistance(s.world.ground(), ball, {x, barY}, d.pos);
    }
}

void buildWreckingBall(Scene& s) {
    s.style = styleEmber();
    s.viewHeight = 29.0f;
    s.camera = {0.0f, 10.8f};
    addFloor(s);

    const float brickW = 1.45f, brickH = 0.72f;
    const int rows = 17, cols = 8;
    for (int row = 0; row < rows; ++row) {
        const float offset = row % 2 == 0 ? 0.0f : brickW * 0.5f;
        for (int col = 0; col < cols; ++col) {
            BodyDef d;
            d.shape = makeBox(brickW * 0.5f - 0.015f, brickH * 0.5f);
            d.pos = {5.0f + static_cast<float>(col) * brickW + offset, brickH * 0.5f + static_cast<float>(row) * brickH};
            d.friction = 0.7f;
            d.restitution = 0.0f;
            d.color = sampleRamp(s.style.ramp, 0.1f + 0.8f * static_cast<float>(row) / static_cast<float>(rows - 1));
            s.world.createBody(d);
        }
    }

    const Vec2 pivot{-5.0f, 24.0f};
    const Vec2 dir = normalized({-1.0f, 0.12f});
    const int links = 15;
    const float linkLength = 0.95f;
    addProp(s, makeBox(1.0f, 0.3f), pivot + Vec2{0.0f, 0.3f});

    int previous = s.world.ground();
    Vec2 anchor = pivot;
    const float angle = std::atan2(dir.y, dir.x);
    for (int i = 0; i < links; ++i) {
        BodyDef d;
        d.shape = makeBox(linkLength * 0.5f, 0.09f);
        d.pos = anchor + dir * (linkLength * 0.5f);
        d.angle = angle;
        d.density = 3.0f;
        d.group = 1;
        d.color = 0x8F8078;
        const int link = s.world.createBody(d);
        s.world.createRevolute(previous, link, anchor);
        previous = link;
        anchor += dir * linkLength;
    }

    const float radius = 2.3f;
    BodyDef ball;
    ball.shape = makeCircle(radius);
    ball.pos = anchor + dir * radius;
    ball.density = 3.0f;
    ball.friction = 0.4f;
    ball.group = 1;
    ball.color = 0xF2502F;
    ball.trail = true;
    const int ballId = s.world.createBody(ball);
    s.world.createRevolute(previous, ballId, anchor);

    // A tight rope from pivot to ball carries the load, so the light chain cannot stretch.
    const int rope = s.world.createRope(s.world.ground(), ballId, pivot, ball.pos, length(ball.pos - pivot));
    s.world.joints()[rope].visible = false;
}

void buildBridge(Scene& s) {
    s.style = styleLagoon();
    s.viewHeight = 26.0f;
    s.camera = {0.0f, 10.5f};
    addFloor(s, -1.0f);

    const float halfSpan = 15.0f;
    const float deckY = 10.0f;
    addSolid(s, makeBox(20.0f, 11.0f), {-halfSpan - 20.0f, deckY - 11.0f});
    addSolid(s, makeBox(20.0f, 11.0f), {halfSpan + 20.0f, deckY - 11.0f});

    const int planks = 30;
    const float plankLength = 2.0f * halfSpan / static_cast<float>(planks);
    int previous = s.world.ground();
    for (int i = 0; i < planks; ++i) {
        const float x = -halfSpan + (static_cast<float>(i) + 0.5f) * plankLength;
        BodyDef d;
        d.shape = makeBox(plankLength * 0.5f - 0.01f, 0.13f);
        d.pos = {x, deckY - 0.13f};
        d.density = 2.0f;
        d.friction = 0.8f;
        d.group = 1;
        d.color = sampleRamp(s.style.ramp, static_cast<float>(i) / static_cast<float>(planks - 1));
        const int plank = s.world.createBody(d);
        s.world.createRevolute(previous, plank, {x - plankLength * 0.5f, deckY - 0.13f});
        previous = plank;
    }
    s.world.createRevolute(previous, s.world.ground(), {halfSpan, deckY - 0.13f});

    auto rng = std::make_shared<std::mt19937>(11);
    auto dropped = std::make_shared<int>(0);
    s.update = [rng, dropped](Scene& sc, const Input&, float) {
        if (*dropped >= 24 || sc.time < 1.0f + static_cast<float>(*dropped) * 0.55f) return;
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        BodyDef d;
        d.pos = {-11.0f + 22.0f * unit(*rng), 24.0f};
        d.angle = unit(*rng) * 6.28f;
        d.density = 1.5f;
        d.restitution = 0.2f;
        d.color = sampleRamp(sc.style.ramp, unit(*rng));
        const float size = 0.5f + 0.5f * unit(*rng);
        d.shape = *dropped % 2 == 0 ? makeCircle(size) : makeBox(size, size * 0.8f);
        sc.world.createBody(d);
        ++*dropped;
    };
}

void buildGalton(Scene& s) {
    s.style = styleGlacier();
    s.style.ramp = {0x9CF0FF, 0x74BEFF, 0x7C7CFF, 0xB66DFF};
    s.style.bloom = 0.14f;
    s.style.trailLength = 12;
    s.viewHeight = 34.0f;
    s.camera = {0.0f, 12.9f};
    addFloor(s);

    constexpr int kRows = 15;
    constexpr int kBalls = 340;
    constexpr float kBallRadius = 0.17f;
    constexpr int kFlightGroup = 7;
    const float pegRadius = 0.14f;
    const float dy = 0.85f;
    const float topY = 24.5f;
    const float binTop = topY - static_cast<float>(kRows - 1) * dy - 0.25f;
    const float half = 0.5f * static_cast<float>(kRows);

    // Row i has i + 1 pegs, so every ball meets the single peg at the top.
    for (int i = 0; i < kRows; ++i) {
        for (int j = 0; j <= i; ++j) {
            const Vec2 p{static_cast<float>(j) - 0.5f * static_cast<float>(i), topY - static_cast<float>(i) * dy};
            const int id = addSolid(s, makeCircle(pegRadius), p);
            s.world.bodies()[id].color = 0x5D7CB5;
            s.world.bodies()[id].restitution = 0.4f;
            s.world.bodies()[id].friction = 0.2f;
        }
    }

    for (int i = 0; i < kRows; ++i) {
        const float x = -0.5f * static_cast<float>(kRows - 1) + static_cast<float>(i);
        addBeam(s, {x, 0.0f}, {x, binTop}, 0.1f);
    }
    const Vec2 shoulder{half + 0.1f, binTop + 0.6f};
    const Vec2 neck{0.62f, topY + 0.75f};
    const Vec2 mouth{0.62f, topY + 2.5f};
    for (const float side : {-1.0f, 1.0f}) {
        const auto mirrored = [&](Vec2 v) { return Vec2{v.x * side, v.y}; };
        addBeam(s, mirrored({shoulder.x, 0.0f}), mirrored(shoulder), 0.2f);
        addBeam(s, mirrored(shoulder), mirrored(neck), 0.2f);
        addBeam(s, mirrored(neck), mirrored(mouth), 0.2f);
    }

    // The bell curve these balls should pile up into.
    Guide bell;
    bell.colour = 0xCDE9FF;
    bell.alpha = 0.55f;
    const float sigma = std::sqrt(static_cast<float>(kRows)) * 0.5f;
    const float packedArea = kPi * kBallRadius * kBallRadius / 0.74f;
    for (int i = 0; i <= 120; ++i) {
        const float x = -half + 2.0f * half * static_cast<float>(i) / 120.0f;
        const float pdf = std::exp(-0.5f * x * x / (sigma * sigma)) / (sigma * std::sqrt(2.0f * kPi));
        bell.points.push_back({x, static_cast<float>(kBalls) * pdf * packedArea / 0.9f});
    }
    s.guides.push_back(bell);

    auto rng = std::make_shared<std::mt19937>(3);
    auto balls = std::make_shared<std::vector<int>>();
    s.update = [rng, balls, topY, binTop](Scene& sc, const Input&, float) {
        std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
        const auto spawned = static_cast<float>(balls->size());
        if (balls->size() < kBalls && sc.time >= spawned * 0.035f) {
            BodyDef d;
            d.shape = makeCircle(kBallRadius);
            d.pos = {jitter(*rng) * 0.3f, topY + 1.6f};
            d.vel = {0.0f, -3.0f};
            d.restitution = 0.4f;
            d.friction = 0.05f;
            d.density = 2.0f;
            d.linearDamping = 2.13f;  // tuned so a lone ball's walk matches the binomial spread
            d.group = kFlightGroup;
            d.color = sampleRamp(sc.style.ramp, spawned / static_cast<float>(kBalls));
            balls->push_back(sc.world.createBody(d));
        }
        // The binomial law describes independent balls, so they only start to
        // collide with each other once they have reached the bins.
        for (const int id : *balls) {
            Body& b = sc.world.bodies()[id];
            if (b.group != 0 && b.pos.y < binTop) b.group = 0;
        }
    };
}

void buildDrum(Scene& s) {
    s.style = styleOrchid();
    s.style.trailMinSpeed = 14.0f;
    s.viewHeight = 29.0f;
    s.camera = {0.0f, 11.8f};
    addFloor(s, -3.0f);

    const Vec2 centre{0.0f, 11.8f};
    const float radius = 12.0f;
    const int segments = 44;
    const float omega = 0.55f;

    struct Part {
        int id;
        float radius;
        float angle;
    };
    auto parts = std::make_shared<std::vector<Part>>();

    auto addPart = [&](const Shape& shape, float r, float a, float bodyAngle, uint32_t colour) {
        BodyDef d;
        d.type = BodyType::Kinematic;
        d.shape = shape;
        d.pos = centre + Vec2{r * std::cos(a), r * std::sin(a)};
        d.angle = bodyAngle;
        d.friction = 0.6f;
        d.color = colour;
        parts->push_back({s.world.createBody(d), r, a});
    };

    const float halfChord = kPi * radius / static_cast<float>(segments) * 1.15f;
    for (int i = 0; i < segments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        addPart(makeBox(0.3f, halfChord), radius, a, a, 0x5A3A8C);
    }
    for (int i = 0; i < 5; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / 5.0f;
        addPart(makeBox(1.9f, 0.16f), radius - 2.0f, a, a, 0x8558C4);
    }

    // Colour bands run side to side, so the tumbling shows how quickly they mix.
    const float ball = 0.32f;
    const float spacing = 2.05f * ball;
    int row = 0;
    for (float y = centre.y - radius + 1.0f; y < centre.y + 1.5f; y += spacing * 0.87f, ++row) {
        for (float x = -radius; x < radius; x += spacing) {
            const Vec2 p{x + (row % 2 == 0 ? 0.0f : spacing * 0.5f), y};
            if (length(p - centre) > radius - 1.0f) continue;
            BodyDef d;
            d.shape = makeCircle(ball);
            d.pos = p;
            d.restitution = 0.1f;
            d.friction = 0.2f;
            d.color = sampleRamp(s.style.ramp, (p.x + radius) / (2.0f * radius));
            s.world.createBody(d);
        }
    }

    s.update = [parts, centre, omega](Scene& sc, const Input&, float dt) {
        const float next = omega * (sc.time + dt);
        for (const Part& part : *parts) {
            Body& b = sc.world.bodies()[part.id];
            // Velocity is set from the exact next position, so the rim never drifts outward.
            const Vec2 target = centre + Vec2{part.radius * std::cos(part.angle + next),
                                              part.radius * std::sin(part.angle + next)};
            b.vel = (target - b.pos) * (1.0f / dt);
            b.angVel = omega;
        }
    };
}

void buildOrbits(Scene& s) {
    s.style = styleGlacier();
    s.style.top = 0x070A14;
    s.style.bottom = 0x030409;
    s.style.glow = 0x0B1022;
    s.style.ramp = {0xFFD08A, 0xFF8FA3, 0xC48BFF, 0x6FB4FF, 0x6FF0E0};
    s.style.bloom = 0.2f;
    s.style.trailLength = 220;
    s.style.trailMinSpeed = 2.0f;
    s.viewHeight = 92.0f;
    s.camera = {0.0f, 0.0f};
    s.baseGravity = {0.0f, 0.0f};
    s.world.gravity = {0.0f, 0.0f};
    s.world.allowSleep = false;
    s.world.substeps = 8;
    s.killY = -1000.0f;

    const float gmEach = 1500.0f;
    const float halfSeparation = 3.6f;
    const float starSpeed = std::sqrt(gmEach / (4.0f * halfSeparation));

    BodyDef star;
    star.shape = makeCircle(1.7f);
    star.collides = false;
    star.gravityScale = 0.0f;
    star.linearDamping = 0.0f;
    star.angularDamping = 0.0f;
    star.pos = {-halfSeparation, 0.0f};
    star.vel = {0.0f, -starSpeed};
    star.color = 0xFFE3B0;
    star.glow = 5.0f;
    star.trail = true;
    const int starA = s.world.createBody(star);
    star.pos = {halfSeparation, 0.0f};
    star.vel = {0.0f, starSpeed};
    star.color = 0xA9D2FF;
    const int starB = s.world.createBody(star);

    std::mt19937 rng(5);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float gmTotal = 2.0f * gmEach;

    auto addOrbiter = [&](float r, float angle, float size, uint32_t colour, bool trail, float spin) {
        BodyDef d;
        d.shape = makeCircle(size);
        d.collides = false;
        d.linearDamping = 0.0f;
        d.angularDamping = 0.0f;
        d.pos = {r * std::cos(angle), r * std::sin(angle)};
        const float v = std::sqrt(gmTotal / r) * spin;
        d.vel = {-std::sin(angle) * v, std::cos(angle) * v};
        d.color = colour;
        d.trail = trail;
        d.glow = trail ? 0.6f : 0.0f;
        s.world.createBody(d);
    };

    addOrbiter(19.0f, 0.4f, 0.75f, 0xFF8FA3, true, 1.0f);
    addOrbiter(35.0f, 2.3f, 0.95f, 0xFFD08A, true, 1.0f);
    addOrbiter(39.5f, 4.1f, 0.6f, 0x6FF0E0, true, 1.0f);
    addOrbiter(26.0f, 5.5f, 0.5f, 0xC48BFF, true, 1.26f);  // eccentric, swings out past the belt

    for (int i = 0; i < 180; ++i) {
        const float r = 24.0f + unit(rng) * 7.0f;
        addOrbiter(r, unit(rng) * 2.0f * kPi, 0.12f + unit(rng) * 0.16f,
                   sampleRamp(s.style.ramp, (r - 24.0f) / 7.0f), false, 1.0f);
    }

    // Every body feels both stars, and the stars feel each other.
    s.world.forceField = [starA, starB, gmEach](World& w, float h) {
        const Vec2 pa = w.bodies()[starA].pos;
        const Vec2 pb = w.bodies()[starB].pos;
        auto pull = [&](Vec2 from, Vec2 to) {
            const Vec2 d = to - from;
            const float distSq = phys::lengthSq(d) + 0.4f;
            return d * (gmEach / (distSq * std::sqrt(distSq)));
        };
        for (int i = 0; i < static_cast<int>(w.bodies().size()); ++i) {
            Body& b = w.bodies()[i];
            if (!b.alive || !b.isDynamic()) continue;
            if (i != starA) b.vel += pull(b.pos, pa) * h;
            if (i != starB) b.vel += pull(b.pos, pb) * h;
        }
    };
}

void buildChaos(Scene& s) {
    s.style = styleOrchid();
    s.style.top = 0x120C20;
    s.style.bottom = 0x08060F;
    s.style.glow = 0x1C1030;
    s.style.ramp = {0xFFB86B, 0xFF6F91, 0xD36BFF, 0x6C8CFF, 0x4FE3D0};
    s.style.bloom = 0.2f;
    s.style.trailLength = 460;
    s.style.trailMinSpeed = 0.0f;
    s.viewHeight = 21.0f;
    s.camera = {0.0f, 9.6f};
    s.world.allowSleep = false;
    s.world.substeps = 16;
    s.world.jointHertz = 240.0f;
    s.world.jointDamping = 5.0f;

    // Twenty double pendulums, a thousandth of a radian apart. They agree for a few
    // seconds, then have nothing to do with one another. That is chaos.
    const int count = 20;
    const Vec2 pivot{0.0f, 10.0f};
    const float arm = 4.2f;
    const float angle = 2.5f;

    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count - 1);
        const float a = angle + static_cast<float>(i) * 0.001f;
        const Vec2 p1 = pivot + Vec2{arm * std::sin(a), -arm * std::cos(a)};
        const Vec2 p2 = p1 + Vec2{arm * std::sin(a), -arm * std::cos(a)};

        BodyDef d;
        d.collides = false;
        d.linearDamping = 0.0f;
        d.angularDamping = 0.0f;
        d.canSleep = false;
        d.color = sampleRamp(s.style.ramp, t);
        d.shape = makeCircle(0.1f);
        d.pos = p1;
        const int upper = s.world.createBody(d);

        d.shape = makeCircle(0.15f);
        d.pos = p2;
        d.trail = true;
        d.glow = 1.0f;
        const int lower = s.world.createBody(d);

        s.world.createDistance(s.world.ground(), upper, pivot, p1);
        s.world.createDistance(upper, lower, p1, p2);
    }
}

void addBlob(Scene& s, Vec2 c, float radius, int count, uint32_t colour, int group) {
    Blob blob;
    blob.colour = colour;

    BodyDef core;
    core.shape = makeCircle(radius * 0.4f);
    core.pos = c;
    core.group = group;
    core.visible = false;
    core.density = 1.5f;
    core.friction = 0.4f;
    blob.centre = s.world.createBody(core);

    // Neighbours just touch, so no gap is left for another blob to slip through.
    const float particle = 1.04f * kPi * radius / static_cast<float>(count);
    for (int i = 0; i < count; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(count);
        BodyDef d;
        d.shape = makeCircle(particle);
        d.pos = c + Vec2{radius * std::cos(a), radius * std::sin(a)};
        d.group = group;
        d.visible = false;
        d.friction = 0.6f;
        d.density = 1.5f;
        blob.ring.push_back(s.world.createBody(d));
    }

    auto spring = [&](int a, int b, float hertz, float damping) {
        const int j = s.world.createSpring(a, b, s.world.bodies()[a].pos, s.world.bodies()[b].pos, hertz, damping);
        s.world.joints()[j].visible = false;
    };
    for (int i = 0; i < count; ++i) {
        spring(blob.ring[i], blob.ring[(i + 1) % count], 16.0f, 0.5f);
        spring(blob.ring[i], blob.ring[(i + 2) % count], 8.0f, 0.4f);
        spring(blob.ring[i], blob.centre, 4.0f, 0.25f);
    }
    s.blobs.push_back(std::move(blob));
}

void buildJelly(Scene& s) {
    s.style = styleLagoon();
    s.style.ramp = {0xFFD86B, 0xFF8FA3, 0x5FF0C2, 0x4FB6FF, 0xC48BFF};
    s.viewHeight = 27.0f;
    s.camera = {0.0f, 10.0f};
    s.world.substeps = 8;
    addFloor(s);
    addBeam(s, {-21.0f, 11.0f}, {-5.0f, 4.0f}, 0.6f);
    addBeam(s, {21.0f, 9.0f}, {7.0f, 3.0f}, 0.6f);
    addSolid(s, makeCircle(1.1f), {-1.0f, 12.0f});
    addSolid(s, makeCircle(1.1f), {6.5f, 14.0f});

    std::mt19937 rng(21);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int i = 0; i < 9; ++i) {
        const float x = -12.0f + static_cast<float>(i % 5) * 6.0f + unit(rng) * 2.0f;
        const float y = 20.0f + static_cast<float>(i) * 5.5f;
        addBlob(s, {x, y}, 1.7f + unit(rng) * 1.2f, 16, s.style.ramp[i % s.style.ramp.size()], i + 1);
    }
}

float hillHeight(float x) {
    const float ramp = std::clamp((x - 12.0f) / 50.0f, 0.0f, 1.0f);
    // Amplitude times frequency bounds the slope; these sum to about 33 degrees at worst.
    return ramp * (2.2f * std::sin(x * 0.11f) + 1.0f * std::sin(x * 0.27f + 1.0f) +
                   0.25f * std::sin(x * 0.61f + 2.0f));
}

void buildRover(Scene& s) {
    s.style = styleMoss();
    s.style.ramp = {0xB8EC5A, 0x5ED18A, 0x27B3A0};
    s.viewHeight = 23.0f;
    s.camera = {0.0f, 5.0f};
    s.killY = -80.0f;

    // The surface is one long strip of invisible quads, painted as a single landscape.
    const float step = 1.0f;
    for (float x = -44.0f; x < 900.0f; x += step) {
        const float x0 = x, x1 = x + step;
        const float y0 = hillHeight(x0), y1 = hillHeight(x1);
        s.terrain.push_back({x0, y0});
        const Vec2 pts[4] = {{x0, y0}, {x0, -24.0f}, {x1, -24.0f}, {x1, y1}};
        BodyDef d;
        d.type = BodyType::Static;
        d.shape = phys::makePolygon(pts, 4);
        // makePolygon recentres on the centroid, so put it back where it was drawn.
        Vec2 centroid;
        {
            const float a0 = (y0 + 24.0f), a1 = (y1 + 24.0f);
            const float cx = step * (a0 + 2.0f * a1) / (3.0f * (a0 + a1));
            const float cy = -24.0f + (a0 * a0 + a0 * a1 + a1 * a1) / (3.0f * (a0 + a1));
            centroid = {x0 + cx, cy};
        }
        d.pos = centroid;
        d.friction = 0.9f;
        d.visible = false;
        s.world.createBody(d);
    }
    s.terrain.push_back({900.0f, hillHeight(900.0f)});

    addSolid(s, makeBox(2.0f, 30.0f), {-44.0f, 15.0f});

    // Chassis.
    const Vec2 start{0.0f, 2.2f};
    const Vec2 outline[6] = {{-2.2f, -0.35f}, {2.2f, -0.35f}, {2.2f, 0.05f}, {0.9f, 0.7f}, {-1.0f, 0.7f}, {-2.2f, 0.25f}};
    BodyDef chassis;
    chassis.shape = phys::makePolygon(outline, 6);
    chassis.pos = start;
    chassis.density = 1.1f;
    chassis.group = 1;
    chassis.friction = 0.4f;
    chassis.color = 0xD6F27A;
    chassis.canSleep = false;
    chassis.angularDamping = 0.4f;
    const int body = s.world.createBody(chassis);
    s.follow = body;
    s.followLead = {6.0f, 3.5f};

    const float wheelRadius = 0.85f;
    auto addWheel = [&](float x) {
        BodyDef w;
        w.shape = makeCircle(wheelRadius);
        w.pos = start + Vec2{x, -1.1f};
        w.density = 0.9f;
        w.friction = 1.6f;
        w.restitution = 0.05f;
        w.group = 1;
        w.color = 0x66795A;
        w.canSleep = false;
        w.angularDamping = 0.2f;
        const int id = s.world.createBody(w);
        // Two springs meeting at the axle make a V: suspension that also holds the wheel in line.
        s.world.createSpring(body, id, start + Vec2{x - 0.9f, -0.1f}, w.pos, 5.0f, 0.75f);
        s.world.createSpring(body, id, start + Vec2{x + 0.9f, -0.1f}, w.pos, 5.0f, 0.75f);
        return id;
    };
    const int rear = addWheel(-1.6f);
    const int front = addWheel(1.6f);

    // Crates scattered along the way, for the rover to plough through.
    std::mt19937 rng(9);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (float x = 28.0f; x < 860.0f; x += 26.0f + unit(rng) * 14.0f) {
        const int high = 2 + static_cast<int>(unit(rng) * 2.0f);
        const float ground = hillHeight(x);
        for (int i = 0; i < high; ++i) {
            BodyDef d;
            d.shape = makeBox(0.4f, 0.4f);
            d.pos = {x, ground + 0.45f + static_cast<float>(i) * 0.82f};
            d.density = 0.15f;
            d.color = sampleRamp(s.style.ramp, unit(rng));
            s.world.createBody(d);
        }
    }

    s.update = [body, rear, front](Scene& sc, const Input& in, float) {
        // Rolls on its own until somebody takes the wheel.
        const float drive = in.driving ? in.drive : (sc.time > 0.8f ? 0.85f : 0.0f);
        const float torque = -48.0f * drive;
        const float topSpin = 20.0f;
        Body& chassisBody = sc.world.bodies()[body];
        for (const int id : {rear, front}) {
            Body& wheel = sc.world.bodies()[id];
            const float spin = -wheel.angVel * (drive >= 0.0f ? 1.0f : -1.0f);
            if (drive != 0.0f && spin < topSpin) {
                wheel.torque += torque;
                chassisBody.torque -= torque * 0.3f;  // a little of the reaction, for a lively nose
            }
        }
        // A gyro keeps it mostly upright and rolls it back over after a crash.
        chassisBody.torque -= 90.0f * std::sin(chassisBody.angle) + 16.0f * chassisBody.angVel;
    };
}

}  // namespace

const std::vector<SceneInfo>& sceneList() {
    static const std::vector<SceneInfo> list = {
        {"Playground", "A loose pile of shapes. Throw things at it.", "Drag to throw. Right-click removes a body.", buildPlayground},
        {"Pyramid", "136 boxes, all resting on each other. Then a cannonball.", "It stands until something makes it fall.", buildPyramid},
        {"Dominoes", "Each domino is 10% bigger, so a small push grows into a slam.", "Watch the energy grow along the line.", buildDominoes},
        {"Newton's Cradle", "Momentum passes through the middle balls and they barely move.", "Grab a second ball and pull it back too.", buildCradle},
        {"Wrecking Ball", "A heavy ball on a light chain against a wall of bricks.", "Grab the ball and swing it again.", buildWreckingBall},
        {"Suspension Bridge", "Thirty planks, pinned end to end, sagging under the load.", "Drop something heavy on it.", buildBridge},
        {"Galton Board", "Random bounces, predictable total: the line is the theory.", "340 balls, 15 rows, one bell curve.", buildGalton},
        {"Tumbling Drum", "A rotating drum stirs bands of colour together.", "Mixing is easy. Unmixing never happens.", buildDrum},
        {"Binary Stars", "Two suns orbit each other with planets and a belt around them.", "Throw something and see if it stays in orbit.", buildOrbits},
        {"Chaos", "Twenty pendulums, started 0.001 radians apart.", "They agree at first, then they don't.", buildChaos},
        {"Jelly", "Soft bodies: rings of particles held together by springs.", "Grab one and pull it.", buildJelly},
        {"Rover", "A sprung buggy on endless hills.", "Left and right arrows to drive.", buildRover},
    };
    return list;
}

void loadScene(Scene& s, int index) {
    s.world.clear();
    s.world.gravity = {0.0f, -10.0f};
    s.world.substeps = 6;
    s.world.jointHertz = 120.0f;
    s.world.jointDamping = 2.0f;
    s.world.allowSleep = true;
    s.world.minRestitution = 0.0f;
    s.style = Style{};
    s.camera = {0.0f, 9.0f};
    s.viewHeight = 26.0f;
    s.viewWidth = 0.0f;
    s.follow = -1;
    s.followLead = {};
    s.blobs.clear();
    s.guides.clear();
    s.terrain.clear();
    s.baseGravity = {0.0f, -10.0f};
    s.startTool = Tool::Grab;
    s.time = 0.0f;
    s.killY = -60.0f;
    s.update = nullptr;
    sceneList()[index].build(s);
}

}  // namespace scene
