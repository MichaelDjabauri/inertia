// Things that exist only to be looked at: sparks, shockwaves and light trails.
#pragma once

#include <vector>

#include "canvas.hpp"
#include "physics/world.hpp"

namespace gfx {

class Effects {
public:
    void clear();

    // Sparks spray out of an impact, biased along the contact normal.
    void sparks(Vec2 point, Vec2 normal, float speed, const Col& colour, int count);
    void shockwave(Vec2 point, float radius, const Col& colour, float duration = 0.45f);

    void update(float dt, Vec2 gravity);
    void draw(Canvas& canvas, const View& view, float dpi) const;

    int activeCount() const { return static_cast<int>(sparks_.size()); }

private:
    struct Spark {
        Vec2 pos;
        Vec2 vel;
        float age = 0.0f;
        float life = 0.5f;
        float size = 1.0f;
        Col colour;
    };
    struct Wave {
        Vec2 pos;
        float radius = 1.0f;
        float age = 0.0f;
        float life = 0.45f;
        Col colour;
    };

    std::vector<Spark> sparks_;
    std::vector<Wave> waves_;
    unsigned int seed_ = 1;
};

// A ring buffer of recent positions for every body, drawn as tapering ribbons.
class Trails {
public:
    void configure(int length, float minSpeed);
    void clear();
    void sample(const phys::World& world);
    void draw(Canvas& canvas, const phys::World& world, const View& view, float alpha, float dpi);

private:
    struct Sample {
        Vec2 pos;
        float energy = 0.0f;
    };
    struct Track {
        uint32_t serial = 0;
        int head = 0;
        int count = 0;
        std::vector<Sample> samples;
    };

    int length_ = 26;
    float minSpeed_ = 10.0f;
    std::vector<Track> tracks_;
    std::vector<Vec2> points_;
    std::vector<float> widths_;
    std::vector<Col> colours_;
};

}  // namespace gfx
