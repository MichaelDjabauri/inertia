#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
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

void Plot::push(float t, std::initializer_list<float> values) {
    if (!times.empty() && t <= times.back()) return;
    times.push_back(t);
    size_t i = 0;
    for (const float v : values) {
        if (i < series.size()) series[i].values.push_back(v);
        ++i;
    }
    // Drop what has scrolled out of view, in chunks so this stays cheap.
    size_t drop = 0;
    while (drop < times.size() && t - times[drop] > span + 0.5f) ++drop;
    if (drop > 30) {
        times.erase(times.begin(), times.begin() + static_cast<long>(drop));
        for (Series& s : series) s.values.erase(s.values.begin(), s.values.begin() + static_cast<long>(std::min(drop, s.values.size())));
    }
}

int Scene::addParam(Param p) {
    const size_t index = params.size();
    p.initial = p.value;
    if (index < presets.size()) p.value = std::clamp(presets[index], p.lo, p.hi);
    params.push_back(std::move(p));
    return static_cast<int>(index);
}

void Scene::measure() {
    overlays.clear();
    arrows.clear();
    markers.clear();
    readouts.clear();
    if (observe) observe(*this);
}

void Scene::step(float stepDt, const Input& input) {
    dt = stepDt;
    if (update) update(*this, input, stepDt);
    world.step(stepDt);
    time += stepDt;
    measure();
}

const Readout* Scene::readout(const std::string& prefix) const {
    for (const Readout& r : readouts) {
        if (r.label.compare(0, prefix.size(), prefix) == 0) return &r;
    }
    return nullptr;
}

namespace {

constexpr float kDeg = kPi / 180.0f;

// ---------------------------------------------------------------------- styles

// Each lesson gets its own mood, all from the same dark family.
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

Style styleSpace() {
    Style s = styleGlacier();
    s.top = 0x070A14;
    s.bottom = 0x030409;
    s.glow = 0x0B1022;
    s.ramp = {0xFFD08A, 0xFF8FA3, 0xC48BFF, 0x6FB4FF, 0x6FF0E0};
    s.bloom = 0.2f;
    return s;
}

// --------------------------------------------------------------------- helpers

std::string format(const char* pattern, ...) {
    char buffer[192];
    va_list args;
    va_start(args, pattern);
    std::vsnprintf(buffer, sizeof buffer, pattern, args);
    va_end(args);
    return buffer;
}

Param setting(const char* label, const char* unit, float lo, float hi, float value, float step, int decimals,
              bool live = false) {
    Param p;
    p.label = label;
    p.unit = unit;
    p.lo = lo;
    p.hi = hi;
    p.value = value;
    p.step = step;
    p.decimals = decimals;
    p.live = live;
    return p;
}

float gravityOf(const Scene& s) { return std::abs(s.world.gravity.y); }

float kineticEnergy(const Body& b) {
    return 0.5f * b.mass * phys::lengthSq(b.vel) + 0.5f * b.inertia * b.angVel * b.angVel;
}

// Length of one substep, which is what the solver's accumulated impulses are per.
float substepLength(const Scene& s) { return s.dt / static_cast<float>(std::max(s.world.substeps, 1)); }

// Total contact force on a body during the last step, normal and friction together.
Vec2 contactForce(const Scene& s, int body) {
    Vec2 total;
    for (const phys::Contact& c : s.world.contacts()) {
        if (c.a != body && c.b != body) continue;
        const float sign = c.b == body ? 1.0f : -1.0f;
        const Vec2 t = phys::perpRight(c.normal);
        for (int k = 0; k < c.count; ++k) {
            total += (c.normal * c.points[k].normalImpulse + t * c.points[k].tangentImpulse) * sign;
        }
    }
    return total * (1.0f / substepLength(s));
}

// Force a joint exerted on its second body during the last step.
Vec2 jointForce(const Scene& s, int joint) {
    const phys::Joint& j = s.world.joints()[joint];
    const float h = substepLength(s);
    if (j.type == phys::JointType::Distance) {
        const Body& a = s.world.bodies()[j.a];
        const Body& b = s.world.bodies()[j.b];
        const Vec2 axis = normalized((b.pos + phys::rotate(b.q, j.localB)) - (a.pos + phys::rotate(a.q, j.localA)));
        return axis * (j.axialImpulse / h);
    }
    return j.impulse * (1.0f / h);
}

void reading(Scene& s, const std::string& label, float value, float expected, const std::string& unit, int decimals,
             uint32_t colour = 0) {
    Readout r;
    r.label = label;
    r.value = value;
    r.expected = expected;
    r.unit = unit;
    r.decimals = decimals;
    r.colour = colour;
    s.readouts.push_back(std::move(r));
}

void status(Scene& s, const std::string& label, const std::string& text, const std::string& expectedText = "",
            uint32_t colour = 0) {
    Readout r;
    r.label = label;
    r.text = text;
    r.expectedText = expectedText;
    r.colour = colour;
    s.readouts.push_back(std::move(r));
}

void arrow(Scene& s, int body, Vec2 vec, uint32_t colour, const std::string& label, Vec2 from = {}) {
    if (phys::lengthSq(vec) < 0.0025f) return;
    s.arrows.push_back({body, from, vec, colour, label});
}

// Text centred on a point, or following a body when `body` is set.
void label(Scene& s, int body, Vec2 at, const std::string& text, uint32_t colour = 0xF3F5F9) {
    Marker m;
    m.body = body;
    m.at = at;
    m.label = text;
    m.colour = colour;
    m.radius = 0.0f;
    s.markers.push_back(std::move(m));
}

void dot(Scene& s, Vec2 at, uint32_t colour, const std::string& text = "", float radius = 0.12f, bool ring = false) {
    Marker m;
    m.at = at;
    m.colour = colour;
    m.label = text;
    m.radius = radius;
    m.ring = ring;
    s.markers.push_back(std::move(m));
}

Guide line(std::vector<Vec2> points, uint32_t colour, float alpha, bool dashed) {
    Guide g;
    g.points = std::move(points);
    g.colour = colour;
    g.alpha = alpha;
    g.dashed = dashed;
    return g;
}

std::vector<Vec2> arc(Vec2 centre, float radius, float from, float to, int segments = 40) {
    std::vector<Vec2> points;
    for (int i = 0; i <= segments; ++i) {
        const float a = from + (to - from) * static_cast<float>(i) / static_cast<float>(segments);
        points.push_back(centre + Vec2{std::cos(a), std::sin(a)} * radius);
    }
    return points;
}

// Times at which a signal changes sign, interpolated between steps.
struct Crossings {
    float previous = NAN;
    float previousTime = 0.0f;
    std::vector<float> times;

    void feed(float value, float time) {
        if (!std::isnan(previous) && time > previousTime && (previous < 0.0f) != (value < 0.0f)) {
            const float f = previous / (previous - value);
            times.push_back(previousTime + f * (time - previousTime));
        }
        previous = value;
        previousTime = time;
    }

    // Successive crossings are half a period apart. Averages over the last few.
    float period() const {
        const int n = static_cast<int>(times.size());
        if (n < 2) return NAN;
        const int first = std::max(0, n - 7);
        return 2.0f * (times[n - 1] - times[first]) / static_cast<float>(n - 1 - first);
    }
};

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

// A static shape that is only there to be looked at.
int addProp(Scene& s, const Shape& shape, Vec2 pos, float angle = 0.0f) {
    BodyDef d;
    d.type = BodyType::Static;
    d.shape = shape;
    d.pos = pos;
    d.angle = angle;
    d.collides = false;
    d.color = s.style.solid;
    return s.world.createBody(d);
}

// ------------------------------------------------------------ projectile motion

constexpr float kShotRadius = 0.3f;

struct Shot {
    std::vector<Vec2> path;
    float angle = 0.0f;
    Vec2 landing;
};

// Earlier shots stay on screen as faint lines, so different settings can be compared.
std::vector<Shot>& pastShots() {
    static std::vector<Shot> shots;
    return shots;
}

Shot& latestShot() {
    static Shot shot;
    return shot;
}

void buildProjectile(Scene& s) {
    s.style = styleDusk();
    s.style.trailLength = 400;
    s.style.trailMinSpeed = 0.0f;
    s.viewHeight = 25.0f;
    s.viewWidth = 47.0f;
    s.camera = {19.0f, 10.5f};
    s.startTool = Tool::Inspect;
    addFloor(s);

    const int speedParam = s.addParam(setting("Launch speed v", "m/s", 5.0f, 20.0f, 15.0f, 0.5f, 1));
    const int angleParam = s.addParam(setting("Launch angle θ", "°", 5.0f, 85.0f, 45.0f, 1.0f, 0));
    const float speed = s.value(speedParam);
    const float angle = s.value(angleParam) * kDeg;
    const Vec2 dir{std::cos(angle), std::sin(angle)};
    const Vec2 start{0.0f, kShotRadius};

    // The last run's shot joins the comparison lines.
    if (!latestShot().path.empty()) {
        pastShots().push_back(latestShot());
        if (pastShots().size() > 4) pastShots().erase(pastShots().begin());
        latestShot() = {};
    }

    addProp(s, makeBox(0.75f, 0.22f), start + dir * 0.5f, angle);
    addProp(s, makeBox(0.5f, 0.28f), {-0.3f, 0.28f});

    struct State {
        int ball = -1;
        float launchTime = 0.0f;
        bool landed = false;
        float top = 0.0f;
        float topX = 0.0f;
        float landX = 0.0f;
        float landT = 0.0f;
        float vx = 0.0f;
        Vec2 prevPos, prevVel;
        float prevTime = 0.0f;
        std::vector<Vec2> path;
    };
    auto st = std::make_shared<State>();

    s.update = [st, speed, dir, start](Scene& sc, const Input&, float) {
        if (st->ball >= 0 || sc.time < 0.6f) return;
        BodyDef d;
        d.shape = makeCircle(kShotRadius);
        d.pos = start;
        d.vel = dir * speed;
        d.density = 2.0f;
        d.friction = 0.8f;
        d.restitution = 0.0f;
        d.angularDamping = 0.0f;
        d.color = 0xFFF3D6;
        d.glow = 0.6f;
        d.trail = true;
        st->ball = sc.world.createBody(d);
        st->launchTime = sc.time;
        st->prevTime = sc.time;
        st->prevPos = d.pos;
        st->prevVel = d.vel;
        st->vx = d.vel.x;
        st->path = {d.pos};
    };

    s.observe = [st, speed, angle, start](Scene& sc) {
        const float g = gravityOf(sc);
        const bool falls = g > 0.05f;
        const float vx0 = speed * std::cos(angle);
        const float vy0 = speed * std::sin(angle);
        const float flightTime = falls ? 2.0f * vy0 / g : NAN;
        const float topHeight = falls ? vy0 * vy0 / (2.0f * g) : NAN;
        const float range = falls ? vx0 * flightTime : NAN;

        const auto& shots = pastShots();
        for (size_t i = 0; i < shots.size(); ++i) {
            const uint32_t colour = sampleRamp(sc.style.ramp, 0.2f + 0.2f * static_cast<float>(i));
            sc.overlays.push_back(line(shots[i].path, colour, 0.35f, false));
            label(sc, -1, shots[i].landing + Vec2{0.0f, 0.9f}, format("%.0f°", shots[i].angle), colour);
        }

        if (falls) {
            std::vector<Vec2> path;
            for (int i = 0; i <= 64; ++i) {
                const float t = flightTime * static_cast<float>(i) / 64.0f;
                path.push_back(start + Vec2{vx0 * t, vy0 * t - 0.5f * g * t * t});
            }
            sc.overlays.push_back(line(std::move(path), 0xF3F5F9, 0.4f, true));
        }

        float measuredTime = NAN, measuredTop = NAN, measuredRange = NAN, vy = NAN, vyTheory = NAN;
        if (st->ball >= 0 && sc.world.bodies()[st->ball].alive) {
            Body& b = sc.world.bodies()[st->ball];
            const float t = sc.time - st->launchTime;
            if (!st->landed) {
                const float stepTime = sc.time - st->prevTime;
                // In free flight only gravity changes the velocity. Anything more means the ground.
                const bool pushed = stepTime > 0.0f && b.vel.y - st->prevVel.y > -0.5f * g * stepTime + 1e-3f;
                if (pushed && st->prevVel.y < 0.0f && t > 0.1f) {
                    // Finish the last bit of free flight exactly, from the state before the bounce.
                    const float v = st->prevVel.y;
                    const float height = st->prevPos.y - kShotRadius;
                    float tau = 0.0f;
                    if (falls) {
                        tau = (v + std::sqrt(std::max(v * v + 2.0f * g * height, 0.0f))) / g;
                    } else if (v < 0.0f) {
                        tau = height / -v;
                    }
                    st->landX = st->prevPos.x + st->prevVel.x * tau;
                    st->landT = (st->prevTime - st->launchTime) + tau;
                    st->landed = true;
                    b.linearDamping = 1.5f;
                    b.angularDamping = 3.0f;
                    st->path.push_back({st->landX, kShotRadius});
                    latestShot() = {st->path, angle / kDeg, {st->landX, 0.0f}};
                } else {
                    if (b.pos.y - kShotRadius > st->top) {
                        st->top = b.pos.y - kShotRadius;
                        st->topX = b.pos.x;
                    }
                    st->vx = b.vel.x;
                    st->path.push_back(b.pos);
                    vy = b.vel.y;
                    vyTheory = vy0 - g * t;
                }
                st->prevPos = b.pos;
                st->prevVel = b.vel;
                st->prevTime = sc.time;
            }
            measuredTime = st->landed ? st->landT : t;
            measuredTop = st->top;
            if (st->landed) measuredRange = st->landX;

            if (!st->landed) {
                constexpr float kScale = 0.2f;
                arrow(sc, st->ball, b.vel * kScale, ink::velocity, "v");
                arrow(sc, st->ball, {b.vel.x * kScale, 0.0f}, ink::normal, "vx");
                arrow(sc, st->ball, {0.0f, b.vel.y * kScale}, ink::gravity, "vy");
                sc.plot.push(sc.time, {b.vel.x, b.vel.y});
            }
            if (st->landed || b.vel.y < 0.0f) {
                dot(sc, {st->topX, st->top + kShotRadius}, 0xF3F5F9, format("top %.1f m", st->top), 0.08f);
            }
            if (st->landed) dot(sc, {st->landX, 0.0f}, 0xF3F5F9, format("lands at %.1f m", st->landX), 0.1f);
        }

        reading(sc, "Time of flight", measuredTime, flightTime, "s", 2);
        if (st->ball < 0) sc.readouts.back().text = "ready";
        reading(sc, "Highest point", measuredTop, topHeight, "m", 2);
        reading(sc, "Range", measuredRange, range, "m", 2);
        if (st->ball >= 0 && !st->landed) sc.readouts.back().text = "in flight";
        reading(sc, "Sideways speed vx", st->ball >= 0 ? st->vx : NAN, vx0, "m/s", 2, ink::normal);
        reading(sc, "Vertical speed vy", vy, vyTheory, "m/s", 2, ink::gravity);
    };

    s.plot.title = "Velocity components (m/s)";
    s.plot.span = 5.0f;
    s.plot.series = {{"vx", ink::normal, {}}, {"vy", ink::gravity, {}}};
}

// ------------------------------------------------------------ friction on a slope

void buildFriction(Scene& s) {
    s.style = styleLagoon();
    s.viewHeight = 16.0f;
    s.viewWidth = 31.0f;
    s.camera = {-4.0f, 6.0f};
    s.startTool = Tool::Inspect;

    const int angleParam = s.addParam(setting("Slope angle θ", "°", 5.0f, 45.0f, 18.0f, 1.0f, 0));
    const float theta = s.value(angleParam) * kDeg;
    const Vec2 upSlope{-std::cos(theta), std::sin(theta)};
    const Vec2 normal{std::sin(theta), std::cos(theta)};

    const int floor = addFloor(s);
    s.world.bodies()[floor].friction = 1.0f;

    // A wedge whose long side is the slope. Surfaces have friction 1, and contacts combine
    // the two as the square root of their product, so each block's own value is μ².
    constexpr float kLength = 18.0f;
    const Vec2 corners[3] = {{-kLength * std::cos(theta), 0.0f}, {0.0f, 0.0f}, {-kLength * std::cos(theta), kLength * std::sin(theta)}};
    const Vec2 centroid = (corners[0] + corners[1] + corners[2]) * (1.0f / 3.0f);
    const Vec2 local[3] = {corners[0] - centroid, corners[1] - centroid, corners[2] - centroid};
    BodyDef wedge;
    wedge.type = BodyType::Static;
    wedge.shape = phys::makePolygon(local, 3);
    wedge.pos = centroid;
    wedge.friction = 1.0f;
    wedge.color = s.style.solid;
    s.world.createBody(wedge);

    s.guides.push_back(line(arc({0.0f, 0.0f}, 2.4f, kPi - theta, kPi), 0xF3F5F9, 0.5f, false));

    struct Block {
        int id = -1;
        float mu = 0.0f;
        float accel = 0.0f;
    };
    auto blocks = std::make_shared<std::vector<Block>>();
    const float mus[3] = {0.2f, 0.4f, 0.6f};
    const float along[3] = {4.0f, 9.0f, 14.0f};
    const uint32_t colours[3] = {0xFFD86B, 0x5FF0C2, 0xC48BFF};
    constexpr float kHalfW = 0.7f, kHalfH = 0.35f;
    for (int i = 0; i < 3; ++i) {
        BodyDef d;
        d.shape = makeBox(kHalfW, kHalfH);
        d.pos = upSlope * along[i] + normal * kHalfH;
        d.angle = -theta;
        d.friction = mus[i] * mus[i];
        d.restitution = 0.0f;
        d.color = colours[i];
        blocks->push_back({s.world.createBody(d), mus[i], 0.0f});
    }

    s.observe = [blocks, theta, upSlope, normal](Scene& sc) {
        const float g = gravityOf(sc);
        constexpr float kForceScale = 0.2f;
        label(sc, -1, Vec2{-std::cos(theta * 0.5f), std::sin(theta * 0.5f)} * 3.1f, "θ");
        reading(sc, "tan θ  (compare with μ)", std::tan(theta), NAN, "", 2);

        std::vector<float> speeds;
        for (Block& block : *blocks) {
            Body& b = sc.world.bodies()[block.id];
            if (!b.alive) continue;
            const bool onSlope = b.pos.x < -0.3f;
            const float speed = phys::dot(b.vel, -upSlope);
            // Measured while the whole block is on the slope, before it reaches the bottom corner.
            if (phys::dot(b.pos, upSlope) > 1.0f && sc.time > 0.25f) block.accel = speed / sc.time;
            speeds.push_back(std::max(phys::length(b.vel), 0.0f));

            const bool slides = std::tan(theta) > block.mu;
            const float expected = slides ? g * (std::sin(theta) - block.mu * std::cos(theta)) : 0.0f;
            reading(sc, format("a, block μ = %.1f", block.mu), block.accel, expected, "m/s²", 2, b.color);

            // Free-body diagram: weight, and the contact force split into normal and friction.
            const Vec2 n = onSlope ? normal : Vec2{0.0f, 1.0f};
            const Vec2 contact = contactForce(sc, block.id);
            const float pressing = phys::dot(contact, n);
            // Weight acts at the centre; the surface pushes at the contact face, and friction
            // acts along it, drawn from the block's edge so it isn't hidden underneath.
            const Vec2 sliding = contact - n * pressing;
            const Vec2 base = n * -kHalfH;
            arrow(sc, block.id, {0.0f, -b.mass * g * kForceScale}, ink::gravity, "mg");
            arrow(sc, block.id, n * (pressing * kForceScale), ink::normal, "N", base);
            arrow(sc, block.id, sliding * kForceScale, ink::friction, "f", base + normalized(sliding) * kHalfW);
            label(sc, block.id, {}, format("μ %.1f", block.mu), 0x0B1A1C);
        }
        while (speeds.size() < 3) speeds.push_back(0.0f);
        sc.plot.push(sc.time, {speeds[0], speeds[1], speeds[2]});
    };

    s.plot.title = "Speed (m/s)";
    s.plot.span = 6.0f;
    for (int i = 0; i < 3; ++i) s.plot.series.push_back({format("μ %.1f", mus[i]), colours[i], {}});
}

// ------------------------------------------------------------ centre of mass

void buildBalance(Scene& s) {
    s.style = styleEmber();
    s.viewHeight = 12.0f;
    s.viewWidth = 18.0f;
    s.camera = {1.2f, 6.0f};
    // Stiffer contacts and more substeps than usual: a tall stack near its limit has little margin.
    s.world.substeps = 12;
    s.world.contactHertz = 60.0f;
    s.startTool = Tool::Inspect;

    const int countParam = s.addParam(setting("Number of blocks n", "", 2.0f, 10.0f, 6.0f, 1.0f, 0));
    const int reachParam = s.addParam(setting("Overhang", "% of the limit", 50.0f, 120.0f, 90.0f, 1.0f, 0));
    const int n = static_cast<int>(std::lround(s.value(countParam)));
    const float fraction = s.value(reachParam) / 100.0f;

    constexpr float kLength = 3.0f, kHeight = 0.6f, kTable = 4.0f;
    addFloor(s);
    const int table = addSolid(s, makeBox(9.0f, kTable * 0.5f), {-9.0f, kTable * 0.5f});
    s.world.bodies()[table].friction = 0.8f;

    // edge[k] is the right-hand end of block k, counting down from the top block at k = 1.
    // Block k sticks out past the one below it by L / 2k, scaled by the chosen fraction.
    std::vector<float> edge(n + 2, 0.0f);
    for (int k = n; k >= 1; --k) edge[k] = edge[k + 1] + fraction * kLength / (2.0f * static_cast<float>(k));

    struct State {
        std::vector<int> ids;  // ids[k] for k = 1..n
        std::vector<Vec2> start;
        bool toppled = false;
    };
    auto st = std::make_shared<State>();
    st->ids.assign(n + 1, -1);
    st->start.assign(n + 1, {});
    for (int k = n; k >= 1; --k) {
        BodyDef d;
        d.shape = makeBox(kLength * 0.5f, kHeight * 0.5f);
        d.pos = {edge[k] - kLength * 0.5f, kTable + kHeight * 0.5f + static_cast<float>(n - k) * kHeight};
        d.friction = 0.8f;
        d.restitution = 0.0f;
        d.color = sampleRamp(s.style.ramp, 0.1f + 0.75f * static_cast<float>(n - k) / static_cast<float>(std::max(n - 1, 1)));
        st->ids[k] = s.world.createBody(d);
        st->start[k] = d.pos;
    }

    float harmonic = 0.0f;
    for (int k = 1; k <= n; ++k) harmonic += 1.0f / static_cast<float>(k);
    const float planned = edge[1];

    s.observe = [st, n, harmonic, planned](Scene& sc) {
        const auto& bodies = sc.world.bodies();
        for (int k = 1; k <= n; ++k) {
            const Body& b = bodies[st->ids[k]];
            if (std::abs(b.angle) > 0.12f || length(b.pos - st->start[k]) > 0.3f) st->toppled = true;
        }

        // Everything above each edge must have its centre of mass inside that edge.
        float mass = 0.0f;
        Vec2 moment;
        for (int k = 1; k <= n; ++k) {
            const Body& b = bodies[st->ids[k]];
            mass += b.mass;
            moment += b.pos * b.mass;
            const Vec2 com = moment * (1.0f / mass);
            Vec2 support{0.0f, kTable};
            if (k < n) {
                const Body& below = bodies[st->ids[k + 1]];
                support = below.pos + phys::rotate(below.q, {kLength * 0.5f, kHeight * 0.5f});
            }
            const bool balanced = com.x <= support.x + 1e-3f;
            const uint32_t colour = balanced ? ink::good : ink::bad;
            sc.overlays.push_back(line({com, {com.x, support.y}}, colour, 0.75f, true));
            dot(sc, com, colour, k == n ? "centre of mass" : "", k == n ? 0.13f : 0.09f);
            dot(sc, support, 0xF3F5F9, "", 0.06f, true);
        }

        const Body& top = bodies[st->ids[1]];
        const Vec2 tip = top.pos + phys::rotate(top.q, {kLength * 0.5f, 0.0f});
        reading(sc, "Top block reaches", tip.x / kLength, planned / kLength, "L", 2);
        reading(sc, "Limit for n blocks", 0.5f * harmonic, NAN, "L", 2);
        reading(sc, "Top block's far end", tip.x - kLength, planned - kLength, "m", 2);
        const float fraction = planned / (0.5f * harmonic * kLength);
        status(sc, "Stack", st->toppled ? "toppled" : "standing", fraction <= 1.0f + 1e-4f ? "standing" : "topples");
    };
}

// ------------------------------------------------------------ hanging chain

// The catenary y = a cosh(x / a) through two points 2d apart with arc length S
// satisfies 2a sinh(d / a) = S. Solved for a by bisection.
double catenaryParameter(double halfSpan, double arcLength) {
    double lo = halfSpan / 40.0, hi = 1e5;
    for (int i = 0; i < 200; ++i) {
        const double mid = std::sqrt(lo * hi);
        if (2.0 * mid * std::sinh(halfSpan / mid) > arcLength) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return std::sqrt(lo * hi);
}

void buildChain(Scene& s) {
    s.style = styleLagoon();
    s.viewHeight = 20.0f;
    s.viewWidth = 33.0f;
    s.camera = {0.0f, 9.2f};
    s.startTool = Tool::Grab;
    // With the usual six substeps a long chain rings along its length for many seconds, and
    // the forces at the anchors swing with it.
    s.world.substeps = 12;

    const int lengthParam = s.addParam(setting("Chain length", "% of the gap", 102.0f, 150.0f, 120.0f, 1.0f, 0));
    const int loadParam = s.addParam(setting("Weight at the middle", "kg", 0.0f, 40.0f, 0.0f, 1.0f, 0));
    const float load = s.value(loadParam);

    constexpr float kHalfSpan = 12.0f, kAnchorY = 16.0f;
    constexpr int kLinks = 30;
    const float chain = 2.0f * kHalfSpan * s.value(lengthParam) / 100.0f;
    const float a = static_cast<float>(catenaryParameter(kHalfSpan, chain));
    const auto curve = [a](float x) { return kAnchorY + a * (std::cosh(x / a) - std::cosh(kHalfSpan / a)); };

    addFloor(s);
    for (const float side : {-1.0f, 1.0f}) {
        addProp(s, makeBox(0.35f, kAnchorY * 0.5f + 0.4f), {side * (kHalfSpan + 0.35f), kAnchorY * 0.5f + 0.4f});
    }

    // Start the links on the curve itself, spaced evenly along its length.
    std::vector<Vec2> points;
    for (int i = 0; i <= kLinks; ++i) {
        const float along = -0.5f * chain + chain * static_cast<float>(i) / kLinks;
        const float x = a * std::asinh(along / a);
        points.push_back({x, curve(x)});
    }
    std::vector<int> links;
    for (int i = 0; i < kLinks; ++i) {
        const Vec2 d = points[i + 1] - points[i];
        BodyDef b;
        b.shape = makeBox(length(d) * 0.5f - 0.01f, 0.14f);
        b.pos = (points[i] + points[i + 1]) * 0.5f;
        b.angle = std::atan2(d.y, d.x);
        b.density = 2.0f;
        b.friction = 0.6f;
        b.group = 1;
        b.linearDamping = 0.6f;
        b.angularDamping = 0.25f;
        links.push_back(s.world.createBody(b));
    }
    std::vector<int> joints;
    joints.push_back(s.world.createRevolute(s.world.ground(), links[0], points[0]));
    for (int i = 1; i < kLinks; ++i) joints.push_back(s.world.createRevolute(links[i - 1], links[i], points[i]));
    joints.push_back(s.world.createRevolute(links[kLinks - 1], s.world.ground(), points[kLinks]));

    float chainMass = 0.0f;
    for (const int id : links) chainMass += s.world.bodies()[id].mass;

    if (load > 0.0f) {
        const float radius = std::sqrt(load / (6.0f * kPi));
        BodyDef w;
        w.shape = makeCircle(radius);
        w.pos = points[kLinks / 2] - Vec2{0.0f, 1.4f + radius};
        w.density = 6.0f;
        w.group = 1;
        w.linearDamping = 2.0f;
        w.color = 0xFFD86B;
        const int weight = s.world.createBody(w);
        s.world.createDistance(links[kLinks / 2 - 1], weight, points[kLinks / 2], w.pos);
    }

    std::vector<Vec2> theory;
    for (int i = 0; i <= 60; ++i) {
        const float x = -kHalfSpan + 2.0f * kHalfSpan * static_cast<float>(i) / 60.0f;
        theory.push_back({x, curve(x)});
    }
    s.guides.push_back(line(theory, 0xF3F5F9, 0.35f, true));

    const float sag = a * (std::cosh(kHalfSpan / a) - 1.0f);
    s.observe = [links, joints, chainMass, load, chain, a, sag](Scene& sc) {
        const float g = gravityOf(sc);
        const float weight = (chainMass + load) * g;
        const float perMetre = chainMass * g / chain;
        const bool bare = load <= 0.0f;

        // Tension in each link, from the pins at its two ends.
        std::vector<float> pins;
        for (const int j : joints) pins.push_back(length(jointForce(sc, j)));
        float lo = 1e30f, hi = 0.0f;
        for (size_t i = 0; i < links.size(); ++i) {
            const float t = 0.5f * (pins[i] + pins[i + 1]);
            lo = std::min(lo, t);
            hi = std::max(hi, t);
        }
        static const std::vector<uint32_t> heat = {0x3F67F5, 0x2CCFC7, 0xFFD86B, 0xFF6F61};
        for (size_t i = 0; i < links.size(); ++i) {
            const float t = 0.5f * (pins[i] + pins[i + 1]);
            sc.world.bodies()[links[i]].color = sampleRamp(heat, hi > lo + 1e-3f ? (t - lo) / (hi - lo) : 0.0f);
        }

        // The pull of the chain on each anchor.
        const phys::Joint& leftPin = sc.world.joints()[joints.front()];
        const phys::Joint& rightPin = sc.world.joints()[joints.back()];
        const Vec2 left = -jointForce(sc, joints.front());
        const Vec2 right = jointForce(sc, joints.back());
        const Vec2 leftAnchor = sc.world.bodies()[leftPin.b].pos + phys::rotate(sc.world.bodies()[leftPin.b].q, leftPin.localB);
        const Vec2 rightAnchor = sc.world.bodies()[rightPin.a].pos + phys::rotate(sc.world.bodies()[rightPin.a].q, rightPin.localA);
        const float scale = 7.0f / std::max(weight, 1.0f);
        arrow(sc, -1, left * scale, ink::tension, "T", leftAnchor);
        arrow(sc, -1, right * scale, ink::tension, "T", rightAnchor);

        float lowest = 1e30f;
        Vec2 bottom;
        for (size_t i = 1; i + 1 < joints.size(); ++i) {
            const phys::Joint& j = sc.world.joints()[joints[i]];
            const Body& b = sc.world.bodies()[j.b];
            const Vec2 p = b.pos + phys::rotate(b.q, j.localB);
            if (p.y < lowest) {
                lowest = p.y;
                bottom = p;
            }
        }
        dot(sc, bottom, 0xF3F5F9, "lowest point", 0.08f);

        reading(sc, "Total weight W", weight, NAN, "N", 0);
        reading(sc, "Lift at each anchor", 0.5f * (std::abs(left.y) + std::abs(right.y)), 0.5f * weight, "N", 1, ink::tension);
        reading(sc, "Sideways pull H", 0.5f * (std::abs(left.x) + std::abs(right.x)), bare ? perMetre * a : NAN, "N", 1);
        reading(sc, "Tension at anchor", 0.5f * (length(left) + length(right)),
                bare ? perMetre * a * std::cosh(kHalfSpan / a) : NAN, "N", 1, 0xFF6F61);
        reading(sc, "Tension at bottom", lo, bare ? perMetre * a : NAN, "N", 1, 0x3F67F5);
        reading(sc, "Sag", kAnchorY - lowest, bare ? sag : NAN, "m", 2);
    };
}

// ------------------------------------------------------------ collisions

void buildCollisions(Scene& s) {
    s.style = styleGlacier();
    s.style.trailMinSpeed = 1000.0f;
    s.viewHeight = 13.0f;
    s.viewWidth = 31.0f;
    s.camera = {0.0f, 0.6f};
    s.baseGravity = {0.0f, 0.0f};
    s.world.gravity = {0.0f, 0.0f};
    s.world.allowSleep = false;
    s.startTool = Tool::Inspect;
    s.killY = -1000.0f;

    const int massAParam = s.addParam(setting("Mass of A", "kg", 1.0f, 10.0f, 2.0f, 0.5f, 1));
    const int massBParam = s.addParam(setting("Mass of B", "kg", 1.0f, 10.0f, 2.0f, 0.5f, 1));
    const int bounceParam = s.addParam(setting("Elasticity e", "", 0.0f, 1.0f, 1.0f, 0.05f, 2));
    const float massA = s.value(massAParam);
    const float massB = s.value(massBParam);
    const float e = s.value(bounceParam);

    constexpr float kSpeed = 6.0f;
    constexpr float kStartA = -9.0f;
    constexpr uint32_t colourA = 0xFFB25C, colourB = 0x6FB4FF;

    // An air track: no gravity in the direction of travel, no friction.
    addProp(s, makeBox(16.0f, 0.08f), {0.0f, -2.4f});

    // Discs of the same density, so their areas show their masses.
    const auto puck = [&](float mass, Vec2 pos, Vec2 vel, uint32_t colour) {
        BodyDef d;
        d.shape = makeCircle(std::sqrt(mass / kPi));
        d.pos = pos;
        d.vel = vel;
        d.density = 1.0f;
        d.friction = 0.0f;
        d.restitution = e;
        d.angularDamping = 0.0f;
        d.canSleep = false;
        d.color = colour;
        return s.world.createBody(d);
    };
    const int a = puck(massA, {kStartA, 0.0f}, {kSpeed, 0.0f}, colourA);
    const int b = puck(massB, {0.0f, 0.0f}, {}, colourB);

    struct State {
        bool collided = false;
        float collideTime = 0.0f;
        float runStart = 0.0f;
    };
    auto st = std::make_shared<State>();

    // Replays itself, so the collision can be watched again and again.
    s.update = [st, a, b](Scene& sc, const Input&, float) {
        const bool done = st->collided && sc.time - st->collideTime > 2.6f;
        if (!done && sc.time - st->runStart < 10.0f) return;
        Body& ba = sc.world.bodies()[a];
        Body& bb = sc.world.bodies()[b];
        ba.pos = {kStartA, 0.0f};
        ba.vel = {kSpeed, 0.0f};
        bb.pos = {0.0f, 0.0f};
        bb.vel = {};
        for (Body* body : {&ba, &bb}) {
            body->angle = 0.0f;
            body->q = phys::Rot(0.0f);
            body->angVel = 0.0f;
        }
        st->collided = false;
        st->runStart = sc.time;
    };

    s.observe = [st, a, b, e](Scene& sc) {
        const Body& ba = sc.world.bodies()[a];
        const Body& bb = sc.world.bodies()[b];
        const float mA = ba.mass, mB = bb.mass;
        bool touching = false;
        for (const phys::Contact& c : sc.world.contacts()) {
            if (!((c.a == a && c.b == b) || (c.a == b && c.b == a))) continue;
            for (int k = 0; k < c.count; ++k) touching = touching || c.points[k].normalImpulse > 0.0f;
        }
        if (!st->collided && touching) {
            st->collided = true;
            st->collideTime = sc.time;
        }
        const bool after = st->collided && sc.time - st->collideTime > 0.2f;

        const float p0 = mA * kSpeed;
        const float energy0 = 0.5f * mA * kSpeed * kSpeed;
        const float vA = (mA - e * mB) * kSpeed / (mA + mB);
        const float vB = (1.0f + e) * mA * kSpeed / (mA + mB);
        const float pA = mA * ba.vel.x, pB = mB * bb.vel.x;

        reading(sc, "Momentum before", p0, NAN, "kg·m/s", 1);
        reading(sc, "Momentum after", after ? pA + pB : NAN, p0, "kg·m/s", 1, ink::momentum);
        reading(sc, "Energy before", energy0, NAN, "J", 1);
        reading(sc, "Energy after", after ? kineticEnergy(ba) + kineticEnergy(bb) : NAN,
                0.5f * mA * vA * vA + 0.5f * mB * vB * vB, "J", 1);
        reading(sc, "A's velocity after", after ? ba.vel.x : NAN, vA, "m/s", 2, colourA);
        reading(sc, "B's velocity after", after ? bb.vel.x : NAN, vB, "m/s", 2, colourB);

        const float scale = 4.0f / p0;
        const float rA = ba.shape.radius, rB = bb.shape.radius;
        arrow(sc, a, {pA * scale, 0.0f}, ink::momentum, "p", {0.0f, rA + 0.55f});
        arrow(sc, b, {pB * scale, 0.0f}, ink::momentum, "p", {0.0f, rB + 0.55f});
        label(sc, a, {0.0f, -rA - 0.6f}, format("A  %.1f kg", mA), colourA);
        label(sc, b, {0.0f, -rB - 0.6f}, format("B  %.1f kg", mB), colourB);

        sc.plot.push(sc.time, {pA, pB, pA + pB});
    };

    s.plot.title = "Momentum (kg·m/s)";
    s.plot.span = 8.0f;
    s.plot.series = {{"A", colourA, {}}, {"B", colourB, {}}, {"total", ink::momentum, {}}};
}

// ------------------------------------------------------------ Newton's cradle

void buildCradle(Scene& s) {
    s.style = styleGlacier();
    s.viewHeight = 24.0f;
    s.camera = {0.0f, 10.8f};
    s.world.allowSleep = false;
    s.world.substeps = 8;
    s.startTool = Tool::Grab;
    addFloor(s);

    const int liftParam = s.addParam(setting("Balls lifted", "", 1.0f, 4.0f, 1.0f, 1.0f, 0));
    const int bounceParam = s.addParam(setting("Elasticity e", "", 0.5f, 1.0f, 1.0f, 0.01f, 2));
    const int lifted = static_cast<int>(std::lround(s.value(liftParam)));
    const float e = s.value(bounceParam);

    constexpr int kCount = 5;
    const float radius = 1.3f;
    const float gap = 0.1f;  // just wider than a ball travels in a step, so each hit gets its own step
    const float rod = 11.0f;
    const float barY = 19.5f;
    const float release = 0.85f;
    const float span = (static_cast<float>(kCount) - 1.0f) * (2.0f * radius + gap);

    addProp(s, makeBox(span * 0.5f + 3.0f, 0.16f), {0.0f, barY + 0.16f});
    addProp(s, makeBox(0.16f, barY * 0.5f), {-span * 0.5f - 3.0f, barY * 0.5f});
    addProp(s, makeBox(0.16f, barY * 0.5f), {span * 0.5f + 3.0f, barY * 0.5f});

    struct State {
        std::vector<int> balls;
        std::vector<float> restX;
        bool swinging = false;
        float peak = 0.0f;
        int out = 0;
        float lastPeak = NAN;
        float lastOut = NAN;
    };
    auto st = std::make_shared<State>();

    for (int i = 0; i < kCount; ++i) {
        const float x = -span * 0.5f + static_cast<float>(i) * (2.0f * radius + gap);
        BodyDef d;
        d.shape = makeCircle(radius);
        d.restitution = e;
        d.friction = 0.0f;
        d.angularDamping = 0.0f;
        d.pos = {x, barY - rod};
        if (i < lifted) d.pos = {x - rod * std::sin(release), barY - rod * std::cos(release)};
        d.color = sampleRamp(s.style.ramp, static_cast<float>(i) / static_cast<float>(kCount - 1) * 0.75f);
        const int ball = s.world.createBody(d);
        s.world.createDistance(s.world.ground(), ball, {x, barY}, d.pos);
        st->balls.push_back(ball);
        st->restX.push_back(x);
    }

    const float restY = barY - rod;
    const float drop = rod * (1.0f - std::cos(release));
    s.observe = [st, restY, drop, lifted, e, radius](Scene& sc) {
        const float g = gravityOf(sc);
        const auto& bodies = sc.world.bodies();
        float energy = 0.0f;
        for (const int id : st->balls) energy += kineticEnergy(bodies[id]) + bodies[id].mass * g * (bodies[id].pos.y - restY);

        // Each swing to the right: how high it goes and how many balls take part. It is
        // reported as soon as the far ball turns back, and reset once it is down again.
        const float right = bodies[st->balls.back()].pos.y - restY;
        if (right > 0.05f * drop && !st->swinging) {
            st->peak = std::max(st->peak, right);
            int out = 0;
            for (size_t i = 0; i < st->balls.size(); ++i) {
                const Body& b = bodies[st->balls[i]];
                if (b.pos.y - restY > 0.3f * drop && b.pos.x > st->restX[i]) ++out;
            }
            st->out = std::max(st->out, out);
            if (right < 0.97f * st->peak) {
                st->lastPeak = st->peak;
                st->lastOut = static_cast<float>(st->out);
                st->swinging = true;
            }
        } else if (right < 0.02f * drop) {
            st->swinging = false;
            st->peak = 0.0f;
            st->out = 0;
        }

        const bool elastic = e >= 0.99f;
        const float mass = bodies[st->balls[0]].mass;
        reading(sc, "Balls swinging out", st->lastOut, elastic ? static_cast<float>(lifted) : NAN, "", 0);
        reading(sc, "Rebound height", st->lastPeak, elastic ? drop : NAN, "m", 2);
        reading(sc, "Release height", drop, NAN, "m", 2);
        reading(sc, "Total energy", energy, elastic ? static_cast<float>(lifted) * mass * g * drop : NAN, "J", 1);

        const float scale = 2.6f / (mass * std::sqrt(2.0f * std::max(g, 0.1f) * drop));
        for (const int id : st->balls) {
            const Body& b = bodies[id];
            arrow(sc, id, {b.vel.x * b.mass * scale, 0.0f}, ink::momentum, std::abs(b.vel.x) > 1.0f ? "p" : "",
                  {0.0f, -radius - 0.5f});
        }
        sc.plot.push(sc.time, {bodies[st->balls.front()].pos.y - restY, bodies[st->balls.back()].pos.y - restY});
    };

    s.plot.title = "Height above rest (m)";
    s.plot.span = 8.0f;
    s.plot.series = {{"left ball", sampleRamp(s.style.ramp, 0.0f), {}}, {"right ball", sampleRamp(s.style.ramp, 0.75f), {}}};
}

// ------------------------------------------------------------ pendulum

double arithmeticGeometricMean(double a, double b) {
    for (int i = 0; i < 30; ++i) {
        const double next = 0.5 * (a + b);
        b = std::sqrt(a * b);
        a = next;
    }
    return a;
}

void buildPendulum(Scene& s) {
    s.style = styleGlacier();
    s.style.trailLength = 90;
    s.style.trailMinSpeed = 1.0f;
    s.viewHeight = 19.0f;
    s.viewWidth = 27.0f;
    s.camera = {0.0f, 8.6f};
    s.world.allowSleep = false;
    s.world.substeps = 8;
    s.startTool = Tool::Grab;
    addFloor(s);

    const int lengthParam = s.addParam(setting("Length L", "m", 1.0f, 12.0f, 8.0f, 0.5f, 1));
    const int massParam = s.addParam(setting("Mass m", "kg", 0.5f, 10.0f, 2.0f, 0.5f, 1));
    const int angleParam = s.addParam(setting("Release angle θ₀", "°", 5.0f, 80.0f, 30.0f, 1.0f, 0));
    const float rod = s.value(lengthParam);
    const float mass = s.value(massParam);
    const float release = s.value(angleParam) * kDeg;

    const Vec2 pivot{0.0f, 15.5f};
    addProp(s, makeBox(2.5f, 0.15f), pivot + Vec2{0.0f, 0.15f});

    const float radius = 0.3f + 0.2f * std::sqrt(mass);
    BodyDef d;
    d.shape = makeCircle(radius);
    d.density = mass / (kPi * radius * radius);
    d.pos = pivot + Vec2{std::sin(release), -std::cos(release)} * rod;
    d.collides = false;
    d.angularDamping = 0.0f;
    d.canSleep = false;
    d.color = 0x74BEFF;
    d.glow = 0.3f;
    d.trail = true;
    const int bob = s.world.createBody(d);
    const int rodJoint = s.world.createDistance(s.world.ground(), bob, pivot, d.pos);

    s.guides.push_back(line({pivot, pivot - Vec2{0.0f, rod + 1.2f}}, 0xF3F5F9, 0.3f, true));
    s.guides.push_back(line(arc(pivot, rod, -kPi * 0.5f - release, -kPi * 0.5f + release), 0xF3F5F9, 0.3f, true));

    auto crossings = std::make_shared<Crossings>();
    const float factor = static_cast<float>(1.0 / arithmeticGeometricMean(1.0, std::cos(release * 0.5)));
    const uint32_t kineticColour = 0x6FD8FF, potentialColour = 0xFFB25C;

    s.observe = [=](Scene& sc) {
        const float g = gravityOf(sc);
        const Body& b = sc.world.bodies()[bob];
        const Vec2 rel = b.pos - pivot;
        crossings->feed(std::atan2(rel.x, -rel.y), sc.time);

        const float smallSwing = g > 0.05f ? 2.0f * kPi * std::sqrt(rod / g) : NAN;
        const float kinetic = kineticEnergy(b);
        const float potential = b.mass * g * (b.pos.y - (pivot.y - rod));
        reading(sc, "Period T", crossings->period(), smallSwing * factor, "s", 2);
        reading(sc, "2π√(L/g)", smallSwing, NAN, "s", 2);
        reading(sc, "Kinetic ½mv²", kinetic, NAN, "J", 1, kineticColour);
        reading(sc, "Potential mgh", potential, NAN, "J", 1, potentialColour);
        reading(sc, "Total energy", kinetic + potential, b.mass * g * rod * (1.0f - std::cos(release)), "J", 1);

        const float forceScale = 2.2f / (b.mass * std::max(g, 0.1f));
        arrow(sc, bob, b.vel * 0.3f, ink::velocity, "v");
        arrow(sc, bob, {0.0f, -b.mass * g * forceScale}, ink::gravity, "mg");
        arrow(sc, bob, jointForce(sc, rodJoint) * forceScale, ink::tension, "T");
        sc.plot.push(sc.time, {kinetic, potential, kinetic + potential});
    };

    s.plot.title = "Energy (J)";
    s.plot.span = 12.0f;
    s.plot.series = {{"kinetic", kineticColour, {}}, {"potential", potentialColour, {}}, {"total", 0xF3F5F9, {}}};
}

// ------------------------------------------------------------ springs and damping

// Displacement of a damped oscillator released from rest at x0.
float dampedResponse(float t, float zeta, float omega, float x0) {
    if (zeta < 0.999f) {
        const float root = std::sqrt(1.0f - zeta * zeta);
        const float wd = omega * root;
        return x0 * std::exp(-zeta * omega * t) * (std::cos(wd * t) + zeta / root * std::sin(wd * t));
    }
    if (zeta < 1.001f) return x0 * (1.0f + omega * t) * std::exp(-omega * t);
    const float root = std::sqrt(zeta * zeta - 1.0f);
    const float s1 = omega * (zeta - root), s2 = omega * (zeta + root);
    return x0 * (s2 * std::exp(-s1 * t) - s1 * std::exp(-s2 * t)) / (s2 - s1);
}

// The last time the response is further than `band` from rest.
float settlingTime(float zeta, float omega, float x0, float band) {
    if (zeta <= 0.0f) return NAN;
    float last = 0.0f;
    for (float t = 0.0f; t < 200.0f; t += 0.001f) {
        if (std::abs(dampedResponse(t, zeta, omega, x0)) > band) last = t;
    }
    return last;
}

void buildSprings(Scene& s) {
    s.style = styleMoss();
    s.viewHeight = 15.5f;
    s.viewWidth = 24.0f;
    s.camera = {0.0f, 7.3f};
    s.world.allowSleep = false;
    s.world.substeps = 12;
    s.startTool = Tool::Grab;
    addFloor(s);

    const int stiffParam = s.addParam(setting("Stiffness k", "N/m", 10.0f, 80.0f, 20.0f, 1.0f, 0));
    const int massParam = s.addParam(setting("Mass m", "kg", 0.5f, 4.0f, 1.0f, 0.1f, 1));
    const int dampParam = s.addParam(setting("Light damping ζ", "", 0.0f, 0.5f, 0.1f, 0.01f, 2));
    const float k = s.value(stiffParam);
    const float mass = s.value(massParam);
    const float zetas[3] = {s.value(dampParam), 1.0f, 2.5f};

    constexpr float kBeamY = 13.0f, kRestY = 6.0f, kPull = 2.5f, kBand = 0.05f;
    const float omega = std::sqrt(k / mass);
    // Rest length chosen so the masses hang at kRestY under the usual gravity.
    const float restLength = (kBeamY - kRestY) - mass * 10.0f / k;
    addProp(s, makeBox(11.0f, 0.2f), {0.0f, kBeamY + 0.2f});

    const float xs[3] = {-7.0f, 0.0f, 7.0f};
    const char* kinds[3] = {"underdamped", "critically damped", "overdamped"};
    std::vector<int> ids;
    std::vector<float> settle;
    std::vector<uint32_t> colours;
    for (int i = 0; i < 3; ++i) {
        BodyDef d;
        d.shape = makeBox(0.55f, 0.55f);
        d.density = mass / (1.1f * 1.1f);
        d.pos = {xs[i], kRestY - kPull};
        d.collides = false;
        d.canSleep = false;
        d.angularDamping = 0.0f;
        d.color = sampleRamp(s.style.ramp, 0.15f + 0.35f * static_cast<float>(i));
        const int id = s.world.createBody(d);
        const int spring = s.world.createSpring(s.world.ground(), id, {xs[i], kBeamY}, d.pos, omega / (2.0f * kPi), zetas[i]);
        s.world.joints()[spring].length = restLength;
        ids.push_back(id);
        settle.push_back(settlingTime(zetas[i], omega, kPull, kBand));
        colours.push_back(d.color);
    }

    struct State {
        float lastOutside[3] = {0.0f, 0.0f, 0.0f};
        Crossings crossings;
    };
    auto st = std::make_shared<State>();
    const float natural = 2.0f * kPi / omega;

    s.observe = [=](Scene& sc) {
        const float g = gravityOf(sc);
        const float equilibrium = kBeamY - restLength - mass * g / k;
        float x[3];
        for (int i = 0; i < 3; ++i) {
            const Body& b = sc.world.bodies()[ids[i]];
            x[i] = b.pos.y - equilibrium;
            if (std::abs(x[i]) > kBand) st->lastOutside[i] = sc.time;
            arrow(sc, ids[i], {0.0f, -k * x[i] * 2.0f / (k * kPull)}, ink::tension, "−kx");
            label(sc, -1, {xs[i], 1.5f}, format("ζ %.2g  %s", zetas[i], kinds[i]), colours[i]);
        }
        st->crossings.feed(x[0], sc.time);
        sc.overlays.push_back(line({{-10.5f, equilibrium}, {10.5f, equilibrium}}, 0xF3F5F9, 0.3f, true));
        label(sc, -1, {-9.0f, equilibrium + 0.45f}, "equilibrium", 0xA0A8B8);

        reading(sc, "Natural period", natural, NAN, "s", 2);
        reading(sc, format("Period, ζ = %.2f", zetas[0]), st->crossings.period(),
                zetas[0] < 1.0f ? natural / std::sqrt(1.0f - zetas[0] * zetas[0]) : NAN, "s", 2, colours[0]);
        for (int i = 0; i < 3; ++i) {
            const bool settled = std::abs(x[i]) <= kBand && sc.time - st->lastOutside[i] > 0.6f * natural;
            reading(sc, format("Settles, ζ = %.2g", zetas[i]), settled ? st->lastOutside[i] : NAN, settle[i], "s", 2, colours[i]);
            if (!settled) sc.readouts.back().text = "moving";
            if (std::isnan(settle[i])) sc.readouts.back().expectedText = "never";
        }
        sc.plot.push(sc.time, {x[0], x[1], x[2]});
    };

    s.plot.title = "Displacement from equilibrium (m)";
    s.plot.span = 8.0f;
    s.plot.lo = -2.7f;
    s.plot.hi = 2.7f;
    for (int i = 0; i < 3; ++i) s.plot.series.push_back({format("ζ %.2g", zetas[i]), colours[i], {}});
}

// ------------------------------------------------------------ dominoes

void buildDominoes(Scene& s) {
    s.style = styleDusk();
    s.style.ramp = {0xFF8A5C, 0xFFB35C, 0xFFD97A, 0xFFF0B5};
    s.viewHeight = 23.0f;
    s.viewWidth = 40.0f;
    s.camera = {0.0f, 8.4f};
    s.startTool = Tool::Inspect;
    addFloor(s);

    const int ratioParam = s.addParam(setting("Size ratio", "×", 1.0f, 1.3f, 1.1f, 0.01f, 2));
    const float ratio = s.value(ratioParam);
    // Bigger steps need fewer dominoes to reach the same final size.
    const int count = ratio > 1.001f ? std::min(23, 1 + static_cast<int>(std::log(10.0f) / std::log(ratio))) : 23;

    struct Domino {
        int id;
        float height;
        float width;
        float startY;
    };
    auto dominoes = std::make_shared<std::vector<Domino>>();
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
        dominoes->push_back({s.world.createBody(d), height, width, d.pos.y});
        x += height * 0.55f;
        height *= ratio;
    }

    // The push: just enough spin, about the front corner, to carry the first domino past
    // its balance point, with a little to spare.
    const Domino& first = dominoes->front();
    const float firstMass = s.world.bodies()[first.id].mass;
    const float tipEnergy = firstMass * 10.0f *
                            (0.5f * std::sqrt(first.height * first.height + first.width * first.width) - 0.5f * first.height);
    const float push = 5.0f * tipEnergy;
    auto pushed = std::make_shared<bool>(false);
    s.update = [dominoes, pushed, push](Scene& sc, const Input&, float) {
        if (*pushed || sc.time < 0.8f) return;
        *pushed = true;
        const Domino& d = dominoes->front();
        Body& b = sc.world.bodies()[d.id];
        const float corner = b.mass * (d.height * d.height + d.width * d.width) / 3.0f;
        const float spin = std::sqrt(2.0f * push / corner);
        b.angVel = -spin;
        b.vel = phys::cross(-spin, Vec2{-0.5f * d.width, 0.5f * d.height});
        sc.world.wake(d.id);
    };

    s.observe = [dominoes, ratio, push](Scene& sc) {
        const float g = gravityOf(sc);
        float kinetic = 0.0f, released = 0.0f;
        int fallen = 0;
        for (const Domino& d : *dominoes) {
            const Body& b = sc.world.bodies()[d.id];
            if (!b.alive) continue;
            kinetic += kineticEnergy(b);
            released += b.mass * g * (d.startY - b.pos.y);
            if (std::abs(b.angle) > 0.6f) ++fallen;
        }
        const Domino& last = dominoes->back();
        const float lastMass = sc.world.bodies()[last.id].mass;

        reading(sc, "Push energy", push, NAN, "J", 3);
        reading(sc, "Released so far", released, NAN, "J", 2);
        reading(sc, "Amplification", released / push, NAN, "×", 0);
        reading(sc, "Last domino holds", lastMass * g * 0.5f * (last.height - last.width), NAN, "J", 1);
        reading(sc, "Dominoes fallen", static_cast<float>(fallen), static_cast<float>(dominoes->size()), "", 0);
        reading(sc, "Gain per domino, ratio³", ratio * ratio * ratio, NAN, "×", 2);
        sc.plot.push(sc.time, {std::max(kinetic, 1e-4f)});
    };

    s.plot.title = "Kinetic energy (J, log scale)";
    s.plot.span = 12.0f;
    s.plot.log = true;
    s.plot.lo = -3.0f;
    s.plot.hi = 3.0f;
    s.plot.series = {{"moving dominoes", 0xFFB35C, {}}};
}

// ------------------------------------------------------------ orbits

void buildOrbit(Scene& s) {
    s.style = styleSpace();
    s.style.trailLength = 900;
    s.style.trailMinSpeed = 0.0f;
    s.baseGravity = {0.0f, 0.0f};
    s.world.gravity = {0.0f, 0.0f};
    s.world.allowSleep = false;
    s.world.substeps = 8;
    s.killY = -100000.0f;
    s.startTool = Tool::Throw;

    const int speedParam = s.addParam(setting("Launch speed", "% of circular", 60.0f, 150.0f, 85.0f, 1.0f, 0));
    constexpr float kGM = 2000.0f, kR0 = 14.0f;
    const float circular = std::sqrt(kGM / kR0);
    const float fraction = s.value(speedParam) / 100.0f;
    const float speed = fraction * circular;

    BodyDef star;
    star.shape = makeCircle(2.0f);
    star.density = 100.0f;
    star.collides = false;
    star.gravityScale = 0.0f;
    star.angularDamping = 0.0f;
    star.canSleep = false;
    star.color = 0xFFE3B0;
    star.glow = 5.0f;
    const int sun = s.world.createBody(star);

    BodyDef planet;
    planet.shape = makeCircle(0.6f);
    planet.pos = {kR0, 0.0f};
    planet.vel = {0.0f, speed};
    planet.collides = false;
    planet.angularDamping = 0.0f;
    planet.canSleep = false;
    planet.color = 0x6FB4FF;
    planet.glow = 0.6f;
    planet.trail = true;
    const int planetId = s.world.createBody(planet);

    // Everything except the star feels its pull.
    s.world.forceField = [sun](World& w, float h) {
        const Vec2 centre = w.bodies()[sun].pos;
        for (int i = 0; i < static_cast<int>(w.bodies().size()); ++i) {
            Body& b = w.bodies()[i];
            if (i == sun || !b.alive || !b.isDynamic()) continue;
            const Vec2 d = centre - b.pos;
            const float distSq = phys::lengthSq(d) + 0.01f;
            b.vel += d * (kGM / (distSq * std::sqrt(distSq)) * h);
        }
    };

    // The orbit these starting conditions should give.
    const float angular = kR0 * speed;
    const float energy = 0.5f * speed * speed - kGM / kR0;
    const float ecc = std::sqrt(std::max(0.0f, 1.0f + 2.0f * energy * angular * angular / (kGM * kGM)));
    const float semiLatus = angular * angular / kGM;
    const float periapsis = fraction < 1.0f ? kPi : 0.0f;
    const bool bound = energy < 0.0f;
    const float semiMajor = bound ? -kGM / (2.0f * energy) : NAN;
    std::vector<Vec2> path;
    for (int i = 0; i <= 360; ++i) {
        const float phi = 2.0f * kPi * static_cast<float>(i) / 360.0f;
        const float denominator = 1.0f + ecc * std::cos(phi - periapsis);
        if (denominator < 0.06f) continue;
        const float r = semiLatus / denominator;
        if (r > 12.0f * kR0) continue;
        path.push_back(Vec2{std::cos(phi), std::sin(phi)} * r);
    }
    s.guides.push_back(line(path, 0xF3F5F9, 0.3f, true));

    // Frame the whole orbit when it fits; otherwise the near part of it.
    if (bound && 2.0f * semiMajor < 7.0f * kR0) {
        const float other = 2.0f * semiMajor - kR0;  // distance to the far end of the major axis
        const float minor = semiMajor * std::sqrt(std::max(0.0f, 1.0f - ecc * ecc));
        s.camera = {0.5f * (kR0 - other), 0.0f};
        s.viewWidth = 2.0f * semiMajor * 1.15f + 6.0f;
        s.viewHeight = 2.0f * minor * 1.15f + 6.0f;
    } else {
        s.camera = {-1.5f * kR0, 1.0f * kR0};
        s.viewWidth = 6.0f * kR0;
        s.viewHeight = 4.6f * kR0;
    }

    const float interval = 2.0f * kPi * kR0 / circular / 12.0f;
    struct Wedge {
        std::vector<Vec2> points;
        float area = 0.0f;
    };
    struct State {
        std::vector<Wedge> done;
        Wedge current;
        float started = 0.0f;
        float lastArea = NAN;
        float previousY = 0.0f;
        float previousTime = 0.0f;
        std::vector<float> laps = {0.0f};  // it starts on the x axis
        int count = 0;
    };
    auto st = std::make_shared<State>();

    s.observe = [=](Scene& sc) {
        const Body& star = sc.world.bodies()[sun];
        const Body& b = sc.world.bodies()[planetId];
        const Vec2 rel = b.pos - star.pos;
        const float r = length(rel);
        const float v = length(b.vel);

        // Equal areas in equal times: the region swept in each interval.
        if (st->current.points.empty()) st->current.points.push_back(rel);
        st->current.points.push_back(rel);
        if (sc.time - st->started >= interval) {
            Wedge& w = st->current;
            w.area = 0.0f;
            for (size_t i = 0; i + 1 < w.points.size(); ++i) w.area += 0.5f * phys::cross(w.points[i], w.points[i + 1]);
            st->lastArea = w.area;
            // Every other interval is shaded, so the wedges stand apart.
            if (st->count++ % 2 == 0) st->done.push_back(w);
            if (st->done.size() > 3) st->done.erase(st->done.begin());
            st->current = {{rel}, 0.0f};
            st->started = sc.time;
        }
        for (size_t i = 0; i < st->done.size(); ++i) {
            Guide g;
            g.fill = true;
            g.colour = 0x8FA8FF;
            g.alpha = 0.2f;
            g.points.push_back(star.pos);
            for (const Vec2 p : st->done[i].points) g.points.push_back(star.pos + p);
            sc.overlays.push_back(std::move(g));
        }

        // A lap is complete each time the planet crosses the positive x axis going up.
        if (st->previousY < 0.0f && rel.y >= 0.0f && rel.x > 0.0f && sc.time > st->previousTime) {
            const float f = st->previousY / (st->previousY - rel.y);
            st->laps.push_back(st->previousTime + f * (sc.time - st->previousTime));
        }
        st->previousY = rel.y;
        st->previousTime = sc.time;
        const float measuredPeriod = st->laps.size() >= 2 ? st->laps.back() - st->laps[st->laps.size() - 2] : NAN;

        reading(sc, "Distance r", r, NAN, "m", 1, 0xFFD08A);
        reading(sc, "Speed v", v, NAN, "m/s", 2, ink::velocity);
        reading(sc, "r × v  (per kg)", phys::cross(rel, b.vel), angular, "m²/s", 1);
        reading(sc, "Energy per kg", 0.5f * v * v - kGM / r, energy, "J/kg", 1);
        reading(sc, "Area per interval", st->lastArea, 0.5f * angular * interval, "m²", 1);
        reading(sc, "Period", measuredPeriod, bound ? 2.0f * kPi * std::sqrt(semiMajor * semiMajor * semiMajor / kGM) : NAN, "s", 2);
        if (!bound) {
            sc.readouts.back().text = "escapes";
            sc.readouts.back().expectedText = "escapes";
        }

        arrow(sc, planetId, b.vel * 0.25f, ink::velocity, "v");
        arrow(sc, planetId, normalized(-rel) * (kGM / (r * r) * 0.15f), ink::gravity, "F");
        sc.plot.push(sc.time, {r, v});
    };

    s.plot.title = "Distance (m) and speed (m/s)";
    s.plot.span = 16.0f;
    s.plot.series = {{"r", 0xFFD08A, {}}, {"v", ink::velocity, {}}};
}

// ------------------------------------------------------------ Galton board

void buildGalton(Scene& s) {
    s.style = styleGlacier();
    s.style.ramp = {0x9CF0FF, 0x74BEFF, 0x7C7CFF, 0xB66DFF};
    s.style.bloom = 0.14f;
    s.style.trailLength = 12;
    s.viewHeight = 34.0f;
    s.camera = {0.0f, 12.9f};
    s.startTool = Tool::Inspect;
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

    const auto addBeam = [&](Vec2 a, Vec2 b, float thickness) {
        const Vec2 d = b - a;
        return addSolid(s, makeBox(length(d) * 0.5f, thickness * 0.5f), (a + b) * 0.5f, std::atan2(d.y, d.x));
    };
    for (int i = 0; i < kRows; ++i) {
        const float x = -0.5f * static_cast<float>(kRows - 1) + static_cast<float>(i);
        addBeam({x, 0.0f}, {x, binTop}, 0.1f);
    }
    const Vec2 shoulder{half + 0.1f, binTop + 0.6f};
    const Vec2 neck{0.62f, topY + 0.75f};
    const Vec2 mouth{0.62f, topY + 2.5f};
    for (const float side : {-1.0f, 1.0f}) {
        const auto mirrored = [&](Vec2 v) { return Vec2{v.x * side, v.y}; };
        addBeam(mirrored({shoulder.x, 0.0f}), mirrored(shoulder), 0.2f);
        addBeam(mirrored(shoulder), mirrored(neck), 0.2f);
        addBeam(mirrored(neck), mirrored(mouth), 0.2f);
    }

    // The bell curve these balls should pile up into.
    const float sigma = std::sqrt(static_cast<float>(kRows)) * 0.5f;
    Guide bell;
    bell.colour = 0xCDE9FF;
    bell.alpha = 0.55f;
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

    s.observe = [balls, binTop, sigma](Scene& sc) {
        double sum = 0.0, sumSq = 0.0;
        int landed = 0;
        for (const int id : *balls) {
            const Body& b = sc.world.bodies()[id];
            if (!b.alive || b.pos.y > binTop - 0.3f) continue;
            sum += b.pos.x;
            sumSq += static_cast<double>(b.pos.x) * b.pos.x;
            ++landed;
        }
        const float mean = landed > 0 ? static_cast<float>(sum / landed) : NAN;
        const float spread = landed > 1 ? static_cast<float>(std::sqrt(std::max(0.0, sumSq / landed - (sum / landed) * (sum / landed)))) : NAN;
        int inside = 0;
        for (const int id : *balls) {
            const Body& b = sc.world.bodies()[id];
            if (b.alive && b.pos.y <= binTop - 0.3f && std::abs(b.pos.x - mean) <= sigma) ++inside;
        }
        reading(sc, "Balls landed", static_cast<float>(landed), NAN, "", 0);
        reading(sc, "Mean position", mean, 0.0f, "", 2);
        reading(sc, "Spread σ", spread, sigma, "", 2);
        reading(sc, "Within ±1σ", landed > 0 ? 100.0f * static_cast<float>(inside) / static_cast<float>(landed) : NAN, 68.3f, "%", 1);
    };
}

// ------------------------------------------------------------ mixing and entropy

void buildDrum(Scene& s) {
    s.style = styleOrchid();
    s.style.trailMinSpeed = 14.0f;
    s.viewHeight = 29.0f;
    s.camera = {0.0f, 11.8f};
    s.startTool = Tool::Inspect;
    addFloor(s, -3.0f);

    const int speedParam = s.addParam(setting("Drum speed ω", "rad/s", -1.0f, 1.0f, 0.55f, 0.05f, 2, true));

    const Vec2 centre{0.0f, 11.8f};
    const float radius = 12.0f;
    const int segments = 44;

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

    // Five colour bands, side to side. Each ball remembers which band it started in.
    constexpr int kBands = 5;
    struct Ball {
        int id;
        int band;
    };
    auto balls = std::make_shared<std::vector<Ball>>();
    const float ball = 0.32f;
    const float spacing = 2.05f * ball;
    int row = 0;
    for (float y = centre.y - radius + 1.0f; y < centre.y + 1.5f; y += spacing * 0.87f, ++row) {
        for (float x = -radius; x < radius; x += spacing) {
            const Vec2 p{x + (row % 2 == 0 ? 0.0f : spacing * 0.5f), y};
            if (length(p - centre) > radius - 1.0f) continue;
            const float t = (p.x + radius) / (2.0f * radius);
            BodyDef d;
            d.shape = makeCircle(ball);
            d.pos = p;
            d.restitution = 0.1f;
            d.friction = 0.2f;
            d.color = sampleRamp(s.style.ramp, t);
            balls->push_back({s.world.createBody(d), std::clamp(static_cast<int>(t * kBands), 0, kBands - 1)});
        }
    }

    auto turned = std::make_shared<float>(0.0f);
    s.update = [parts, centre, turned, speedParam](Scene& sc, const Input&, float dt) {
        const float omega = sc.value(speedParam);
        *turned += omega * dt;
        for (const Part& part : *parts) {
            Body& b = sc.world.bodies()[part.id];
            // Velocity is set from the exact next position, so the rim never drifts outward.
            const Vec2 target = centre + Vec2{part.radius * std::cos(part.angle + *turned),
                                              part.radius * std::sin(part.angle + *turned)};
            b.vel = (target - b.pos) * (1.0f / dt);
            b.angVel = omega;
        }
    };

    // Coarse-grained entropy: split the drum into cells and ask how evenly the bands are
    // shared out inside each one. 0 is perfectly sorted, 1 perfectly mixed.
    auto initial = std::make_shared<float>(NAN);
    s.observe = [balls, centre, radius, turned, initial](Scene& sc) {
        constexpr float kCell = 3.0f;
        constexpr int kSide = 9;
        int counts[kSide][kSide][kBands] = {};
        for (const Ball& b : *balls) {
            const Body& body = sc.world.bodies()[b.id];
            if (!body.alive) continue;
            const int cx = std::clamp(static_cast<int>((body.pos.x - centre.x + radius) / kCell), 0, kSide - 1);
            const int cy = std::clamp(static_cast<int>((body.pos.y - centre.y + radius) / kCell), 0, kSide - 1);
            ++counts[cx][cy][b.band];
        }
        double entropy = 0.0;
        int total = 0;
        for (auto& column : counts) {
            for (auto& cell : column) {
                int n = 0;
                for (const int c : cell) n += c;
                if (n == 0) continue;
                for (const int c : cell) {
                    if (c == 0) continue;
                    const double p = static_cast<double>(c) / n;
                    entropy -= n * p * std::log(p);
                }
                total += n;
            }
        }
        const float mixed = total > 0 ? static_cast<float>(entropy / (total * std::log(static_cast<double>(kBands)))) : 0.0f;
        if (std::isnan(*initial)) *initial = mixed;

        reading(sc, "Mixing entropy", mixed, NAN, "", 2, 0xFF8ADB);
        reading(sc, "At the start", *initial, NAN, "", 2);
        reading(sc, "Turns so far", *turned / (2.0f * kPi), NAN, "", 1);
        sc.plot.push(sc.time, {mixed});
    };

    s.plot.title = "Mixing entropy  (0 sorted, 1 mixed)";
    s.plot.span = 60.0f;
    s.plot.lo = 0.0f;
    s.plot.hi = 1.0f;
    s.plot.series = {{"entropy", 0xFF8ADB, {}}};
}

// ------------------------------------------------------------ chaos

std::string powerOfTen(float exponent) {
    static const char* const digits[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
    const int e = static_cast<int>(std::lround(exponent));
    std::string text = "10";
    if (e < 0) text += "⁻";
    text += digits[std::min(std::abs(e), 9)];
    return text + " rad";
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
    s.startTool = Tool::Inspect;

    const int angleParam = s.addParam(setting("Starting angle", "°", 10.0f, 170.0f, 143.0f, 1.0f, 0));
    Param gapParam = setting("Gap between neighbours", "", -6.0f, -1.0f, -3.0f, 1.0f, 0);
    gapParam.format = powerOfTen;
    const int gapIndex = s.addParam(gapParam);
    const float gap = std::pow(10.0f, s.value(gapIndex));

    // Twenty double pendulums, a tiny angle apart.
    const int count = 20;
    const Vec2 pivot{0.0f, 10.0f};
    const float arm = 4.2f;
    const float angle = s.value(angleParam) * kDeg;

    std::vector<int> lowers;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count - 1);
        const float a = angle + static_cast<float>(i) * gap;
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
        lowers.push_back(lower);

        s.world.createDistance(s.world.ground(), upper, pivot, p1);
        s.world.createDistance(upper, lower, p1, p2);
    }

    struct State {
        float start = NAN;
        float fourfold = NAN;  // when the gap first reached four times its start
        float sixtyfourfold = NAN;
        float oneMetre = NAN;
    };
    auto st = std::make_shared<State>();

    s.observe = [lowers, st](Scene& sc) {
        const auto& bodies = sc.world.bodies();
        // The gap between the first two neighbours, which start one step apart.
        const float spread = length(bodies[lowers[0]].pos - bodies[lowers[1]].pos);
        if (std::isnan(st->start)) st->start = spread;
        if (std::isnan(st->fourfold) && spread > 4.0f * st->start) st->fourfold = sc.time;
        if (std::isnan(st->sixtyfourfold) && spread > 64.0f * st->start) st->sixtyfourfold = sc.time;
        if (std::isnan(st->oneMetre) && spread > 1.0f) st->oneMetre = sc.time;
        // Four doublings separate 4× from 64×.
        const float doubling = (st->sixtyfourfold - st->fourfold) / 4.0f;

        const auto metres = [](float v) {
            if (v >= 1.0f) return format("%.2f m", v);
            if (v >= 0.01f) return format("%.1f cm", v * 100.0f);
            if (v >= 1e-5f) return format("%.2f mm", v * 1000.0f);
            return format("%.1f µm", v * 1e6f);
        };
        status(sc, "Gap, neighbours", metres(spread), "", 0xFFB86B);
        status(sc, "Gap at the start", metres(st->start));
        reading(sc, "Gap passes 1 m at", st->oneMetre, NAN, "s", 1);
        reading(sc, "Gap doubles every", doubling, NAN, "s", 2);
        sc.plot.push(sc.time, {std::max(spread, 1e-6f)});
    };

    s.plot.title = "Gap between neighbours (m, log scale)";
    s.plot.span = 20.0f;
    s.plot.log = true;
    s.plot.lo = -6.0f;
    s.plot.hi = 1.5f;
    s.plot.series = {{"gap", 0xFFB86B, {}}};
}

}  // namespace

// ---------------------------------------------------------------------- lessons

const std::vector<SceneInfo>& sceneList() {
    static const std::vector<SceneInfo> list = {
        {"Motion", "Projectile Motion",
         "A thrown object moves sideways at a steady speed while gravity steadily changes its "
         "vertical speed. Together they trace a parabola.",
         "x = v cos θ · t\n"
         "y = v sin θ · t − ½ g t²\n"
         "range R = v² sin 2θ / g",
         "Once the ball leaves the launcher, the only force on it is gravity, and gravity only pulls "
         "down. Nothing changes the sideways speed, so the ball covers equal distances across in "
         "equal times. The cyan arrow stays the same length the whole way.\n\n"
         "Vertically it is a different story. The ball slows by g every second on the way up, stops "
         "for an instant at the top, and speeds up by g every second on the way down. The orange "
         "arrow shrinks, flips, and grows.\n\n"
         "Steady motion across plus steadily accelerating motion down makes a parabola. 45° goes "
         "farthest, and angles that add up to 90°, such as 30° and 60°, land in the same place. "
         "Real projectiles also feel air drag, which shortens the flight; here there is none.",
         "Sport: a basketball shot, a long jump and a football kick all follow this curve, give or take air drag.\n"
         "Fountains and sprinklers aim their jets along parabolas.\n"
         "Firefighters judge the angle of a hose to reach a high window.\n"
         "The first artillery tables were worked out from these equations.\n"
         "Aircraft fly the same parabola to give astronauts half a minute of weightlessness.",
         "Find the angle that sends the ball farthest.\n"
         "Fire at 30°, then at 60°. Why do they land in the same place?\n"
         "Watch the cyan arrow: the sideways speed never changes in flight.\n"
         "Open Settings, set g to 1.6 m/s² (the Moon) and fire again.",
         buildProjectile},
        {"Forces", "Friction on a Slope",
         "Friction can hold a block still on a slope, but only up to a limit. Once the slope is "
         "steeper than tan θ = μ, the block slides.",
         "slides when tan θ > μ\n"
         "friction  f ≤ μ N\n"
         "a = g (sin θ − μ cos θ)",
         "Gravity pulls each block straight down with its weight, mg. Split that pull in two: one "
         "part, mg sin θ, runs along the slope and tries to slide the block; the other, mg cos θ, "
         "presses it into the slope. The surface pushes back against that with the normal force N.\n\n"
         "Friction opposes sliding, but it can only grow as large as μN. The block stays put as "
         "long as mg sin θ ≤ μ mg cos θ, which simplifies to tan θ ≤ μ. The mass cancels out: a "
         "heavy block and a light one with the same μ start to slide at exactly the same angle.\n\n"
         "Past that angle, friction falls short and the rest of the downhill pull accelerates the "
         "block. Real surfaces usually grip a little harder at rest than when sliding (static versus "
         "kinetic friction); this simulation uses one μ for both.",
         "Brakes and tyres: the grip limit sets how quickly a car can stop or corner.\n"
         "Roads and railways are kept below the steepest slope wheels can climb.\n"
         "Landslides and avalanches start when a slope passes its angle of repose.\n"
         "Screws, nails, knots and wedges all hold because of friction.\n"
         "Conveyor belts carry goods up inclines only while the slope stays within their grip.",
         "Raise the angle one degree at a time. When does each block start to slide?\n"
         "The μ 0.4 block should start at tan θ = 0.4, about 22°. Check it.\n"
         "Watch the friction arrow f: on a held block it matches the downhill pull exactly.\n"
         "Compare each block's measured acceleration with g (sin θ − μ cos θ).",
         buildFriction},
        {"Forces", "Centre of Mass",
         "An object balances as long as its centre of mass is above whatever supports it. Stack "
         "blocks so each pile's centre of mass sits just inside the edge below, and the top block "
         "can reach entirely past the table.",
         "centre of mass x = Σ mᵢ xᵢ / Σ mᵢ\n"
         "block k overhangs the one below by L / 2k\n"
         "total reach = ½ L (1 + ½ + ⅓ + … + 1/n)",
         "The centre of mass is the average position of all of a body's mass. Gravity acts as if it "
         "pulled at that one point. If the point lies over the support, the support can push up "
         "right beneath it and nothing tips. Once it passes the edge, gravity turns the body over "
         "the edge.\n\n"
         "In a stack, this has to hold at every level: all the blocks above any edge must have their "
         "combined centre of mass inside that edge. The dots show these centres, green when they "
         "are inside their edge and red when they are not.\n\n"
         "The best stack puts each block L/2, L/4, L/6 … past the one below. These add up to the "
         "harmonic series, which never stops growing: with enough blocks the top one can reach any "
         "distance. But it grows slowly. Two block lengths take 31 blocks; three take 227.",
         "Cranes carry counterweights to keep their centre of mass over their base.\n"
         "Tightrope walkers carry long poles to adjust their balance.\n"
         "Racing cars are built low and wide so they don't roll over in corners.\n"
         "Cantilevered balconies, diving boards and bridges are anchored so the load stays balanced.\n"
         "When you lean forward to pick something up, your hips move back to keep you over your feet.",
         "Push the overhang past 100% and watch which level fails first.\n"
         "With 5 or more blocks, the top block is entirely beyond the table. Check \"far end\".\n"
         "Add blocks one at a time. Each one adds less reach than the last.\n"
         "Grab the top block and slide it a little outward.",
         buildBalance},
        {"Forces", "Hanging Chain",
         "A chain hanging under its own weight takes the shape of a catenary. The tension is "
         "smallest at the bottom and largest at the anchors, and each anchor carries half the weight.",
         "y = a cosh(x / a)\n"
         "horizontal pull H = w a  (same all along)\n"
         "tension T = w a cosh(x / a)",
         "Each link hangs from its neighbours, so the tension at any point has to hold up all of the "
         "chain below it. The links are coloured by tension, from blue (low) to red (high).\n\n"
         "Nothing pushes the chain sideways, so the horizontal part of the tension is the same "
         "everywhere. Only the vertical part changes, growing by the weight of each link as you go "
         "up. Where the vertical part is large, the chain is steep; at the bottom it is zero, and the "
         "chain is level. Balancing those forces link by link gives the catenary, y = a cosh(x/a), "
         "drawn as a dashed line.\n\n"
         "However the chain is loaded, the two anchors together must hold up its whole weight. By "
         "symmetry each holds half, and the measurements show it once the chain is still. While it is "
         "bouncing, the anchors carry more or less than the weight, and the difference is exactly what "
         "accelerates the chain (F = ma). A chain can never be pulled perfectly straight: the tension "
         "needed would be infinite.",
         "Power lines and telephone wires hang in catenaries between poles.\n"
         "Suspension bridge cables carry an evenly spread deck, which pulls them into a parabola instead.\n"
         "Turn a catenary upside down and it becomes the ideal arch, in pure compression. Gaudí designed with hanging chains, and the Gateway Arch in St. Louis is a catenary.\n"
         "Anchor chains, ski lifts and zip lines all sag this way.",
         "Shorten the chain towards 102%: the sag shrinks, but the tension at the ends shoots up.\n"
         "Hang a weight from the middle and watch the curve turn into a V.\n"
         "Whatever you do, each anchor's lift stays at half the total weight.\n"
         "Grab a link and pull. Where does the tension go?",
         buildChain},
        {"Momentum", "Collisions",
         "When two bodies collide, the momentum one gains is exactly the momentum the other loses. "
         "Total momentum is conserved; kinetic energy is conserved only if the collision is perfectly "
         "elastic.",
         "p = m v\n"
         "total p before = total p after\n"
         "e = speed apart / speed together",
         "During the collision the two pucks push on each other with equal and opposite forces "
         "(Newton's third law) for exactly the same short time. So whatever momentum A loses, B "
         "gains, and the total p = mv doesn't change. The white line on the plot stays flat while the "
         "two coloured ones swap.\n\n"
         "Energy is a different matter. In a perfectly elastic collision (e = 1) the kinetic energy "
         "is conserved too. Real collisions lose some to heat, sound and permanent dents. The "
         "coefficient of restitution e measures how much bounce is left: the speed at which the "
         "bodies separate divided by the speed at which they approached. At e = 0 they stick "
         "together.\n\n"
         "Momentum is conserved whatever e is. Only the share of energy that survives changes.",
         "Crash investigators work back from where vehicles ended up to their speeds before impact.\n"
         "Rockets throw exhaust backwards to gain equal momentum forwards.\n"
         "Snooker and curling rely on near-elastic collisions between equal masses.\n"
         "Crumple zones make car crashes deliberately inelastic, turning kinetic energy into bent metal instead of injuries.\n"
         "Particle physicists detect unseen particles, such as neutrinos, from momentum missing after a collision.",
         "Make the masses equal with e = 1: A stops dead and B leaves with all its speed.\n"
         "Set e = 0. The pucks stick together, momentum is still conserved, and energy is lost.\n"
         "Make B five times heavier than A: A bounces back and B barely moves.\n"
         "Find a setting where A keeps going forwards after the collision.",
         buildCollisions},
        {"Momentum", "Newton's Cradle",
         "Lift one ball and one swings out on the other side. Lift two and two swing out. Only that "
         "outcome conserves both momentum and kinetic energy.",
         "momentum:  Σ m v is unchanged\n"
         "energy:  Σ ½ m v² is unchanged\n"
         "two balls at v/2 carry the same p, but only ½ the energy",
         "When the lifted ball strikes the row, the push passes through the middle balls as a quick "
         "compression wave. They pass it on almost without moving, and the last ball leaves with the "
         "speed the first one arrived with.\n\n"
         "Why one ball, and not two at half speed? Both carry the same momentum. But two balls at "
         "v/2 have kinetic energy 2 · ½ m (v/2)² = ¼ m v², only half of what came in. Conserving "
         "both momentum and energy leaves one answer: as many balls leave as arrived, at the same "
         "speed.\n\n"
         "Real cradles slowly wind down as energy is lost in each collision. Lower the elasticity and "
         "the balls soon swing together as one lump.",
         "An office desk toy, and a classic physics demonstration.\n"
         "The same push passes along a line of railway wagons when a locomotive bumps them.\n"
         "Pile drivers and impact hammers send a blow through a solid column.\n"
         "Sound and seismic waves travel the same way, each particle passing momentum on to the next.",
         "Lift two balls, then three, then four.\n"
         "Lower the elasticity to 0.9 and watch how fast the pattern breaks down.\n"
         "Grab the far-right ball and swing it while the cradle is running.\n"
         "Check that the rebound height matches the release height.",
         buildCradle},
        {"Energy", "Pendulum",
         "A pendulum trades potential energy for kinetic energy and back again. For small swings, its "
         "period depends only on its length and on gravity, not on its mass.",
         "T ≈ 2π √(L / g)\n"
         "½ m v² + m g h = constant\n"
         "exact: T = 2π √(L/g) / AGM(1, cos ½θ₀)",
         "At the ends of the swing the bob stops for an instant: all its energy is potential, mgh. At "
         "the bottom it moves fastest: all kinetic, ½mv². The total stays the same, and the plot shows "
         "the two trading places twice per swing.\n\n"
         "Gravity pulls the bob back toward the middle with a force that grows with the displacement. "
         "For small swings that makes it a simple harmonic oscillator with period T = 2π√(L/g). "
         "Longer pendulums swing more slowly. The mass cancels: a heavier bob needs more force to "
         "move, and gravity pulls it harder in exactly the same proportion.\n\n"
         "For bigger swings the restoring force grows more slowly than the angle, so the period gets "
         "longer: about 2% at 30° and 14% at 80°. The theory column uses the exact formula; compare it "
         "with the small-swing one.",
         "Pendulum clocks kept the world's time for nearly 300 years, because the period hardly depends on the swing.\n"
         "Swings, wrecking balls and loads hanging from cranes all move like pendulums.\n"
         "Foucault's pendulum shows the Earth turning beneath it.\n"
         "Skyscrapers such as Taipei 101 hang huge pendulums inside to damp their swaying.\n"
         "Precise pendulums were used to measure small changes in g, revealing what lies underground.",
         "Change the mass. The period doesn't change.\n"
         "Make the pendulum four times longer: the period only doubles.\n"
         "Release it at 80° and compare the measured period with 2π√(L/g).\n"
         "Open Settings and halve g. What happens to the period?",
         buildPendulum},
        {"Energy", "Springs & Damping",
         "A spring pulls back in proportion to how far it is stretched, so a mass on a spring "
         "oscillates. Damping drains the energy, and just the right amount stops it fastest.",
         "F = −k x\n"
         "T = 2π √(m / k)\n"
         "damping ratio ζ = c / 2√(k m)",
         "Stretch a spring by x and it pulls back with force kx (Hooke's law). The mass accelerates "
         "back toward equilibrium, overshoots, and is pulled back again: simple harmonic motion, with "
         "a period 2π√(m/k) that doesn't depend on how far you pulled it.\n\n"
         "Damping is a force against the motion, like the oil in a shock absorber. Its strength is "
         "measured by the damping ratio ζ. With a little damping (ζ < 1, left), the mass still "
         "oscillates but each swing is smaller. Critical damping (ζ = 1, middle) is the least damping "
         "that prevents any overshoot, and it returns to rest the fastest. More than that (ζ > 1, "
         "right) and the mass creeps back sluggishly.\n\n"
         "The plot shows all three. It is the same picture engineers use to tune anything that has "
         "to settle quickly.",
         "Car suspension: springs absorb bumps, and shock absorbers add damping close to critical so the car settles after one bounce.\n"
         "Door closers are damped so the door shuts without slamming or bouncing.\n"
         "Earthquake-resistant buildings and tall towers use dampers to stop them swaying.\n"
         "Scales, galvanometers and seismometers are damped so their readings settle quickly.\n"
         "Guitar strings, tuning forks and quartz watch crystals are lightly damped oscillators.",
         "Quadruple the stiffness: the period halves. Quadruple the mass: it doubles.\n"
         "Set the light damping to 0. The left mass bounces almost for ever.\n"
         "Compare the middle and right masses: more damping is not faster.\n"
         "Grab a mass, pull it, and let go.",
         buildSprings},
        {"Energy", "Dominoes",
         "A standing domino stores potential energy, and a small push releases it. Make each domino "
         "bigger than the last and the energy grows exponentially along the line.",
         "energy stored  E = m g (h − w) / 2\n"
         "energy gain per step = ratio³  (2D)\n"
         "after n steps: (ratio³)ⁿ times the push",
         "A domino standing on end has its centre of mass high up. Falling flat lowers it, releasing "
         "potential energy. But first it has to be tipped past its balance point, which takes only a "
         "tiny push. Once past, it falls on its own and hits the next one far harder than it was hit.\n\n"
         "Each domino here is bigger than the last by the size ratio. A bigger domino is heavier and "
         "its centre of mass falls further, so it releases more energy, yet a smaller domino can still "
         "tip it over. In this 2D world mass grows with area, so the energy grows by ratio³ at each "
         "step (ratio⁴ for real dominoes).\n\n"
         "Multiplying by the same factor at every step is exponential growth. On the log-scale plot it "
         "shows up as a line climbing steadily.\n\n"
         "There is a limit. A falling domino hits the next one lower down, relative to its height, the "
         "bigger the next one is. Make the step too large and the push can no longer tip it: here the "
         "chain stops somewhere above a ratio of 1.2.",
         "Chain reactions: in nuclear fission, each split releases neutrons that trigger more splits.\n"
         "Amplifiers and transistors use a small signal to release a much larger store of energy.\n"
         "Avalanches and rockfalls: a small slip releases huge amounts of stored potential energy.\n"
         "Lorne Whitehead showed in 1983 that 13 dominoes, each 1.5 times bigger, can amplify a push about two billion times.",
         "Set the ratio to 1.00, so every domino is the same, and compare the energy released.\n"
         "Push the ratio up. There are fewer dominoes, but each step multiplies the energy more.\n"
         "Find the largest ratio at which the whole chain still falls.\n"
         "Read the amplification once they have all fallen.",
         buildDominoes},
        {"Gravity", "Orbits",
         "A planet in orbit is always falling toward its star, but moving sideways fast enough to keep "
         "missing it. Gravity's 1/r² pull gives Kepler's laws.",
         "F = G M m / r²\n"
         "equal areas in equal times\n"
         "T² = 4π² a³ / G M\n"
         "escape speed = √2 × circular speed",
         "Gravity pulls the planet toward the star with a force that falls off with the square of "
         "the distance. Launch the planet sideways at just the right speed and it falls around the "
         "star in a circle. Slower, and it falls inward into an ellipse; faster, and it swings out.\n\n"
         "Kepler found three rules in the planets' motion, and all of them follow from this force. "
         "Orbits are ellipses with the star at one focus. The line from star to planet sweeps out "
         "equal areas in equal times, so the planet speeds up close to the star: that is conservation "
         "of angular momentum, r × v. And the square of the period grows with the cube of the orbit's "
         "size.\n\n"
         "At √2 (141%) times the circular speed, the planet has enough energy to escape completely and "
         "never comes back.",
         "Every satellite, from the Moon to GPS and the International Space Station, follows these laws.\n"
         "Geostationary satellites sit at the one height where an orbit takes exactly a day.\n"
         "Mission planners use transfer orbits to move between planets with the least fuel.\n"
         "Astronomers weigh stars and find exoplanets from the periods of their orbits.\n"
         "Gravity assists borrow a planet's orbital energy to speed spacecraft up.",
         "Set the speed to 100% for a circular orbit.\n"
         "Try 70%: check that every shaded wedge has the same area.\n"
         "Set 141% or more and watch the planet escape.\n"
         "Throw a moon into orbit around the star with the Throw tool.",
         buildOrbit},
        {"Order & chaos", "Galton Board",
         "Each ball bounces left or right at random, but hundreds of them pile up into a predictable "
         "bell curve. Many small random effects add up to a normal distribution.",
         "P(k) = C(n, k) / 2ⁿ\n"
         "mean = 0,   σ = √n / 2\n"
         "68% land within ±1σ",
         "At each of the 15 rows of pegs, a ball goes left or right, roughly 50/50. Its final "
         "position is the sum of 15 random steps. Many different paths lead to the middle, but only "
         "one path leads to each edge, so the middle fills up first.\n\n"
         "Counting the paths gives the binomial distribution, and with many rows it becomes the "
         "normal, or bell, curve drawn as a line, with spread σ = √n / 2.\n\n"
         "This is the central limit theorem: add up many small independent random effects and the "
         "total follows a bell curve, whatever the individual effects look like. No single ball is "
         "predictable, but the crowd is.",
         "Manufacturing: many small independent variations add up to a bell curve of part sizes.\n"
         "Measurement errors average out the same way, which is why scientists repeat measurements.\n"
         "Diffusion: a drop of ink spreads because each molecule takes a random walk.\n"
         "Heights, test scores and blood pressure are roughly normal because each has many small causes.\n"
         "Opinion polls, insurance and risk models are built on the central limit theorem.",
         "Follow a single ball: its path is unpredictable.\n"
         "Then look at the pile: it closely matches the curve.\n"
         "Compare the measured spread σ with √15 / 2 ≈ 1.94.\n"
         "Check what fraction of balls land within one σ of the middle.",
         buildGalton},
        {"Order & chaos", "Mixing & Entropy",
         "Mixed-up arrangements vastly outnumber sorted ones, so shuffling only ever goes one way. "
         "Entropy measures that, and the second law says it never decreases.",
         "S = −k Σ pᵢ ln pᵢ\n"
         "ΔS ≥ 0  for an isolated system",
         "There is only a handful of ways to arrange these balls in neat colour bands, and an "
         "astronomical number of ways to arrange them mixed up. Every tumble of the drum shuffles them "
         "into an arrangement more or less at random, so it almost always moves toward mixed and, in "
         "practice, never back.\n\n"
         "Entropy counts those arrangements. The meter here splits the drum into cells and measures "
         "how evenly the colours are shared out in each: 0 means perfectly sorted, 1 perfectly mixed.\n\n"
         "Reversing the drum doesn't unmix it. Undoing the mixing would require every collision to be "
         "undone exactly, and the tiniest difference grows at every bounce. This is the second law of "
         "thermodynamics, and the reason time seems to have a direction.",
         "Concrete mixers, cement kilns and tumble dryers.\n"
         "Pharmaceutical blenders must spread a tiny dose evenly through every tablet.\n"
         "The second law explains why heat flows from hot to cold, and why no engine can be 100% efficient.\n"
         "It is why milk never un-stirs from coffee.\n"
         "Some granular mixtures surprise: shaken mixed nuts sort themselves, with the big ones rising to the top.",
         "Let it mix for half a minute, then drag the speed below zero. Does it unmix?\n"
         "Stop the drum (speed 0). The entropy stops rising, but it doesn't fall.\n"
         "Restart and watch the curve: fast at first, then levelling off.\n"
         "Spin it fast. Does that mix it faster?",
         buildDrum},
        {"Order & chaos", "Chaos",
         "A double pendulum follows exact laws, yet two that start almost identically soon do "
         "completely different things. Tiny differences grow exponentially: that is chaos.",
         "gap δ(t) ≈ δ₀ e^(λt)\n"
         "predictable for about  (1/λ) ln(tolerance / δ₀)",
         "These 20 double pendulums start a tiny angle apart, a thousandth of a radian by default, "
         "closer than you could ever set them up by hand. For a few seconds they move as one. Then the gap becomes visible, and soon they have "
         "nothing in common.\n\n"
         "The motion is deterministic: the same start always gives the same result. But at large "
         "swings it is chaotic. Any difference between two starts keeps doubling, again and again. "
         "On the log-scale plot that shows up as a straight climb.\n\n"
         "Making the starting gap ten times smaller only buys a fixed few extra seconds before the "
         "pendulums part. That is why chaotic systems are predictable in principle but not in practice. "
         "At small angles, though, the double pendulum is not chaotic at all: try it.",
         "Weather: small measurement errors double every few days, so detailed forecasts fail after about two weeks.\n"
         "The orbits of the planets are chaotic over many millions of years.\n"
         "Turbulence, heart rhythms and animal populations all show chaotic behaviour.\n"
         "Spacecraft trajectories use the same sensitivity to change course with very little fuel.",
         "Lower the starting angle to 20°: the pendulums stay together.\n"
         "Make the gap 10 times smaller. How much longer do they stay together?\n"
         "Watch the plot: a straight climb on a log scale means exponential growth.\n"
         "Read off the doubling time and compare angles.",
         buildChaos},
    };
    return list;
}

void loadScene(Scene& s, int index, const std::vector<float>& presets) {
    s.world.clear();
    s.world.gravity = {0.0f, -10.0f};
    s.world.substeps = 6;
    s.world.jointHertz = 120.0f;
    s.world.jointDamping = 2.0f;
    s.world.allowSleep = true;
    s.world.minRestitution = 0.0f;
    s.world.contactHertz = 30.0f;
    s.world.forceField = nullptr;
    s.style = Style{};
    s.camera = {0.0f, 9.0f};
    s.viewHeight = 26.0f;
    s.viewWidth = 0.0f;
    s.follow = -1;
    s.followLead = {};
    s.blobs.clear();
    s.guides.clear();
    s.terrain.clear();
    s.overlays.clear();
    s.arrows.clear();
    s.markers.clear();
    s.readouts.clear();
    s.plot = Plot{};
    s.params.clear();
    s.presets = presets;
    s.baseGravity = {0.0f, -10.0f};
    s.startTool = Tool::Grab;
    s.time = 0.0f;
    s.dt = 1.0f / 60.0f;
    s.killY = -60.0f;
    s.update = nullptr;
    s.observe = nullptr;
    sceneList()[index].build(s);
    s.measure();
}

}  // namespace scene
