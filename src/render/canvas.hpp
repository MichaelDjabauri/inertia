// Anti-aliased 2D geometry built from coloured triangles.
//
// Render textures have no MSAA, so edges are feathered instead: every outline
// gets a one-pixel band whose alpha fades to zero. The same band primitive also
// gives soft shadows, rims and glows, without a shader per shape.
#pragma once

#include <vector>

#include "color.hpp"
#include "view.hpp"

namespace gfx {

class Canvas {
public:
    void begin();
    void end();

    void vertex(Vec2 p, const Col& c);
    void triangle(Vec2 a, const Col& ca, Vec2 b, const Col& cb, Vec2 c, const Col& cc);
    void quad(Vec2 a, const Col& ca, Vec2 b, const Col& cb, Vec2 c, const Col& cc, Vec2 d, const Col& cd);

    // Fan from `centre` out to `points`, with one colour per outline point.
    void fan(Vec2 centre, const Col& centreColour, const Vec2* points, const Col* colours, int count);

    // Band around a closed outline, between two offsets along its normals.
    void band(const Vec2* points, const Vec2* normals, int count, float offsetA, const Col& colourA,
              float offsetB, const Col& colourB);
    void band(const Vec2* points, const Vec2* normals, const Col* coloursA, const Col* coloursB, int count,
              float offsetA, float offsetB);

    // Soft-edged disc and ring.
    void disc(Vec2 centre, float radius, const Col& colour, float feather = 1.0f);
    void ring(Vec2 centre, float radius, float width, const Col& colour, float feather = 1.0f);
    void softDisc(Vec2 centre, float radius, const Col& inner, const Col& outer);
    // Light falling away smoothly to nothing, with no visible edge.
    void glow(Vec2 centre, float radius, const Col& colour);

    // Anti-aliased line; width is the solid core, feather the fade either side.
    void line(Vec2 a, Vec2 b, float width, const Col& colour, float feather = 1.0f);
    void polyline(const Vec2* points, int count, float width, const Col& colour, float feather = 1.0f);

    // Tapering ribbon. Widths and colours are per point.
    void ribbon(const Vec2* points, const float* widths, const Col* colours, int count);

    void roundedRect(float x, float y, float w, float h, float radius, const Col& colour);
    void roundedRectStroke(float x, float y, float w, float h, float radius, float width, const Col& colour);
    void triangleFilled(Vec2 a, Vec2 b, Vec2 c, const Col& colour);

private:
    void buildCircle(Vec2 centre, float radius);
    void buildRoundedRect(float x, float y, float w, float h, float radius);

    std::vector<Vec2> points_;
    std::vector<Vec2> normals_;
    std::vector<Col> colours_;
    bool open_ = false;
};

// Number of segments for a smooth circle of this pixel radius.
int circleSegments(float radiusPixels);

}  // namespace gfx
