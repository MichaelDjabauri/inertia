// Maps between world metres (y up) and pixels (y down).
#pragma once

#include "physics/math.hpp"

namespace gfx {

using phys::Vec2;

struct View {
    Vec2 centre;
    float pixelsPerMetre = 40.0f;
    float width = 1280.0f;
    float height = 800.0f;
    float originX = 640.0f;  // pixel column `centre` maps to, off-centre to clear the side panel

    Vec2 toPixels(Vec2 w) const {
        return {originX + (w.x - centre.x) * pixelsPerMetre, height * 0.5f - (w.y - centre.y) * pixelsPerMetre};
    }
    Vec2 toWorld(Vec2 p) const {
        return {centre.x + (p.x - originX) / pixelsPerMetre, centre.y - (p.y - height * 0.5f) / pixelsPerMetre};
    }
    // World direction to pixel direction.
    static Vec2 flip(Vec2 v) { return {v.x, -v.y}; }
};

}  // namespace gfx
