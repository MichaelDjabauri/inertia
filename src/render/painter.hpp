// Draws the world: lit bodies, soft shadows, joints, blobs and terrain.
#pragma once

#include <vector>

#include "canvas.hpp"
#include "physics/world.hpp"
#include "scenes/scene.hpp"

namespace gfx {

class Painter {
public:
    struct Options {
        bool heatView = false;
    };

    // Works out every outline once per frame; the draw passes below reuse them.
    void prepare(const phys::World& world, const scene::Style& style, const View& view, float alpha, float dpi,
                 const Options& options);

    void drawShadows(Canvas& canvas);
    void drawSolids(Canvas& canvas);
    void drawBodies(Canvas& canvas);
    void drawEmissive(Canvas& canvas);

    void drawJointLines(Canvas& canvas, const phys::World& world, const scene::Style& style);
    void drawPins(Canvas& canvas, const phys::World& world, const scene::Style& style);
    void drawBlobs(Canvas& canvas, const phys::World& world, const std::vector<scene::Blob>& blobs);
    void drawBlobEmissive(Canvas& canvas);
    void drawHorizon(Canvas& canvas, const std::vector<Vec2>& terrain, const scene::Style& style);
    void drawTerrain(Canvas& canvas, const std::vector<Vec2>& terrain, const scene::Style& style);
    void drawTerrainRim(Canvas& canvas, const std::vector<Vec2>& terrain, const scene::Style& style);
    void drawGuides(Canvas& canvas, const std::vector<scene::Guide>& guides);
    void drawDebug(Canvas& canvas, const phys::World& world);

private:
    struct Item {
        int body = -1;
        int first = 0;
        int count = 0;
        Vec2 centre;
        float angle = 0.0f;
        float radiusPixels = 0.0f;
        Col colour;
        float glow = 0.0f;
        float flash = 0.0f;
        bool solid = false;
        bool circle = false;
        bool smooth = false;  // lit by one gradient across the face, rather than in facets
    };
    struct SkinRange {
        int first = 0;
        int count = 0;
        Vec2 centre;
        Col colour;
    };

    void addCircle(Item& item, const phys::Body& body, Vec2 centre);
    void addPolygon(Item& item, const phys::Body& body, Vec2 centre, float angle);
    Vec2 interpolated(const phys::Body& body) const;
    float interpolatedAngle(const phys::Body& body) const;

    View view_;
    float dpi_ = 1.0f;
    float alpha_ = 1.0f;
    Col solidColour_, solidRim_;

    std::vector<Item> items_;
    std::vector<Vec2> points_;
    std::vector<Vec2> normals_;
    std::vector<Col> colours_;
    std::vector<Col> coloursB_;

    std::vector<SkinRange> skins_;
    std::vector<Vec2> skinPoints_;
    std::vector<Vec2> skinNormals_;
    std::vector<Vec2> scratch_;
};

}  // namespace gfx
