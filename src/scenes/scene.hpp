// Scene descriptions. These depend only on the physics core, so every scene can
// also be built and checked headless in tests/scene_tests.cpp.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "physics/world.hpp"

namespace scene {

using phys::Vec2;

enum class Tool { Grab, Ball, Box, Gem, Bomb, Pull };

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

// A soft body, drawn as one smooth skin around a ring of hidden particles.
struct Blob {
    std::vector<int> ring;
    int centre = -1;
    uint32_t colour = 0xFFFFFF;
};

// A line drawn over the scene.
struct Guide {
    std::vector<Vec2> points;
    uint32_t colour = 0xFFFFFF;
    float alpha = 0.6f;
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
    std::vector<Guide> guides;
    std::vector<Vec2> terrain;  // surface line of a continuous landscape

    Vec2 baseGravity{0.0f, -10.0f};
    Tool startTool = Tool::Grab;
    float time = 0.0f;
    float killY = -60.0f;

    // Runs once per physics step, before the world advances.
    std::function<void(Scene&, const Input&, float dt)> update;
};

struct SceneInfo {
    const char* name;
    const char* blurb;
    const char* hint;
    void (*build)(Scene&);
};

const std::vector<SceneInfo>& sceneList();

// Returns the scene to its starting state and builds scene `index`.
void loadScene(Scene& scene, int index);

}  // namespace scene
