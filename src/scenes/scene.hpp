// Lessons. Each one builds a small experiment in the physics world, describes the principle
// behind it, and measures what happens so the result can be checked against the theory.
// They depend only on the physics core, so every lesson is also run headless in
// tests/scene_tests.cpp.
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "physics/world.hpp"

namespace scene {

using phys::Vec2;

enum class Tool { Inspect, Grab, Throw };

// Colours are 0xRRGGBB in sRGB.
struct Style {
    uint32_t top = 0x141A2E;
    uint32_t bottom = 0x0B0D18;
    uint32_t glow = 0x1A2238;
    std::vector<uint32_t> ramp = {0xFFB347, 0xFF6F61, 0xE0528D, 0x8A5CF6};
    uint32_t solid = 0x1B2133;
    uint32_t solidRim = 0x9FB2D9;
    uint32_t joint = 0xB9C6E4;
    float bloom = 0.16f;
    int trailLength = 26;
    float trailMinSpeed = 10.0f;
};

uint32_t sampleRamp(const std::vector<uint32_t>& ramp, float t);

// Colours shared by every lesson, so a quantity looks the same wherever it appears.
namespace ink {
constexpr uint32_t velocity = 0xF3F5F9;
constexpr uint32_t gravity = 0xFFB25C;
constexpr uint32_t normal = 0x6FD8FF;
constexpr uint32_t friction = 0xFF6F91;
constexpr uint32_t tension = 0x8CF0B4;
constexpr uint32_t momentum = 0xC59BFF;
constexpr uint32_t good = 0x7CF0A0;
constexpr uint32_t bad = 0xFF6F6F;
}  // namespace ink

// A soft body, drawn as one smooth skin around a ring of hidden particles.
struct Blob {
    std::vector<int> ring;
    int centre = -1;
    uint32_t colour = 0xFFFFFF;
};

// A line drawn over the scene. Filled guides are drawn as a fan from the first point.
struct Guide {
    std::vector<Vec2> points;
    uint32_t colour = 0xFFFFFF;
    float alpha = 0.6f;
    bool dashed = false;
    bool fill = false;
};

// A vector drawn on the scene, such as a velocity or a force.
struct Arrow {
    int body = -1;    // starts at this body's centre plus `from`; -1 means `from` is a world point
    Vec2 from;
    Vec2 vec;         // metres on screen, already scaled
    uint32_t colour = ink::velocity;
    std::string label;
};

// A labelled point, such as a centre of mass. With a body it follows that body.
struct Marker {
    int body = -1;
    Vec2 at;
    uint32_t colour = 0xFFFFFF;
    std::string label;
    float radius = 0.12f;  // metres; 0 draws only the label
    bool ring = false;
};

// A setting the learner can change. Most restart the experiment; live ones apply at once.
struct Param {
    std::string label;
    std::string unit;
    float lo = 0.0f;
    float hi = 1.0f;
    float value = 0.0f;
    float step = 0.0f;  // snap to multiples of this; 0 is continuous
    int decimals = 0;
    bool live = false;
    float initial = 0.0f;  // the default, before any preset was applied
    std::function<std::string(float)> format;  // replaces the plain number when set
};

// One line of the measurements table: what the simulation measured, beside what theory predicts.
struct Readout {
    std::string label;
    float value = NAN;
    float expected = NAN;   // NAN when there is no prediction
    std::string unit;
    int decimals = 2;
    uint32_t colour = 0;    // swatch matching a plot line; 0 for none
    std::string text;       // shown instead of the value when set, e.g. "holds"
    std::string expectedText;
};

// A strip chart of the last few seconds of up to four quantities.
struct Plot {
    struct Series {
        std::string name;
        uint32_t colour = 0xFFFFFF;
        std::vector<float> values;
    };
    std::string title;
    float span = 10.0f;     // seconds shown
    float lo = 0.0f;        // fixed range when lo < hi; otherwise it fits the data
    float hi = 0.0f;
    bool log = false;       // values are plotted on a log10 axis
    bool includeZero = true;
    std::vector<float> times;
    std::vector<Series> series;

    bool active() const { return !series.empty(); }
    void push(float time, std::initializer_list<float> values);
};

struct Input {
    float drive = 0.0f;  // -1 to 1, from the arrow keys
    bool driving = false;
};

struct Scene {
    phys::World world;
    Style style;

    Vec2 camera{0.0f, 9.0f};
    float viewHeight = 26.0f;  // metres visible top to bottom
    float viewWidth = 0.0f;    // minimum metres visible across, when non-zero
    int follow = -1;           // body the camera tracks
    Vec2 followLead;

    std::vector<Blob> blobs;
    std::vector<Guide> guides;    // fixed for the whole experiment
    std::vector<Vec2> terrain;    // surface line of a continuous landscape

    // Rebuilt by `observe` after every step.
    std::vector<Guide> overlays;
    std::vector<Arrow> arrows;
    std::vector<Marker> markers;
    std::vector<Readout> readouts;
    Plot plot;

    std::vector<Param> params;
    std::vector<float> presets;   // values to start params at, carried over from a previous run

    Vec2 baseGravity{0.0f, -10.0f};
    Tool startTool = Tool::Grab;
    float time = 0.0f;
    float dt = 1.0f / 60.0f;  // length of the last step
    float killY = -60.0f;

    // Runs once per physics step, before the world advances.
    std::function<void(Scene&, const Input&, float dt)> update;
    // Runs once per physics step, after the world advances: measures and annotates.
    std::function<void(Scene&)> observe;

    // Declares a setting and returns its index. The value comes from `presets` when one is there.
    int addParam(Param p);
    float value(int param) const { return params[param].value; }

    // Advances the experiment one step: update, world step, then measure.
    void step(float stepDt, const Input& input = {});

    // Clears the annotations and readouts, then runs `observe` to rebuild them.
    void measure();

    // The first measurement whose label starts with `prefix`, or nullptr.
    const Readout* readout(const std::string& prefix) const;
};

struct SceneInfo {
    const char* topic;      // the part of physics it belongs to
    const char* name;
    const char* principle;  // the idea, in a sentence or two
    const char* equation;   // the maths, one relation per line
    const char* how;        // how it works; paragraphs separated by blank lines
    const char* uses;       // where it matters in the real world; one per line
    const char* tryThis;    // experiments to run; one per line
    void (*build)(Scene&);
};

const std::vector<SceneInfo>& sceneList();

// Returns the scene to its starting state and builds scene `index`. Params start at
// `presets` where given, and at their defaults otherwise.
void loadScene(Scene& scene, int index, const std::vector<float>& presets = {});

}  // namespace scene
