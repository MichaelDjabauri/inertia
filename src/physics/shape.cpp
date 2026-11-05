#include "shape.hpp"

#include <cassert>
#include <limits>

namespace phys {

Shape makeCircle(float radius) {
    Shape s;
    s.type = ShapeType::Circle;
    s.radius = radius;
    s.boundRadius = radius;
    return s;
}

Shape makeBox(float hw, float hh) {
    const Vec2 pts[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
    return makePolygon(pts, 4);
}

Shape makePolygon(const Vec2* points, int count) {
    assert(count >= 3 && count <= kMaxPolygonVerts);

    Shape s;
    s.type = ShapeType::Polygon;
    s.count = count;

    float signedArea = 0.0f;
    for (int i = 0; i < count; ++i) signedArea += cross(points[i], points[(i + 1) % count]);

    // Enforce counter-clockwise winding.
    for (int i = 0; i < count; ++i) {
        s.verts[i] = signedArea >= 0.0f ? points[i] : points[count - 1 - i];
    }

    // Centroid, relative to the first vertex for numerical accuracy.
    const Vec2 origin = s.verts[0];
    float area = 0.0f;
    Vec2 centroid;
    for (int i = 1; i + 1 < count; ++i) {
        const Vec2 e1 = s.verts[i] - origin;
        const Vec2 e2 = s.verts[i + 1] - origin;
        const float a = 0.5f * cross(e1, e2);
        area += a;
        centroid += (e1 + e2) * (a / 3.0f);
    }
    centroid = centroid / area + origin;

    s.boundRadius = 0.0f;
    for (int i = 0; i < count; ++i) {
        s.verts[i] -= centroid;
        s.boundRadius = std::max(s.boundRadius, length(s.verts[i]));
    }
    for (int i = 0; i < count; ++i) {
        const Vec2 edge = s.verts[(i + 1) % count] - s.verts[i];
        s.normals[i] = normalized(perpRight(edge));
    }
    s.radius = 0.0f;
    return s;
}

Shape makeRegularPolygon(int sides, float radius, float startAngle) {
    assert(sides >= 3 && sides <= kMaxPolygonVerts);
    Vec2 pts[kMaxPolygonVerts];
    for (int i = 0; i < sides; ++i) {
        const float a = startAngle + 2.0f * kPi * static_cast<float>(i) / static_cast<float>(sides);
        pts[i] = {radius * std::cos(a), radius * std::sin(a)};
    }
    return makePolygon(pts, sides);
}

MassData computeMass(const Shape& shape, float density) {
    MassData m;
    if (shape.type == ShapeType::Circle) {
        m.mass = density * kPi * shape.radius * shape.radius;
        m.inertia = 0.5f * m.mass * shape.radius * shape.radius;
        return m;
    }

    // Vertices are already centred on the centroid, so the fan is taken about the origin.
    float area = 0.0f;
    float inertia = 0.0f;
    for (int i = 0; i < shape.count; ++i) {
        const Vec2 e1 = shape.verts[i];
        const Vec2 e2 = shape.verts[(i + 1) % shape.count];
        const float d = cross(e1, e2);
        area += 0.5f * d;
        const float ix = e1.x * e1.x + e2.x * e1.x + e2.x * e2.x;
        const float iy = e1.y * e1.y + e2.y * e1.y + e2.y * e2.y;
        inertia += (0.25f / 3.0f) * d * (ix + iy);
    }
    m.mass = density * area;
    m.inertia = density * inertia;
    return m;
}

AABB computeAABB(const Shape& shape, const Transform& xf) {
    if (shape.type == ShapeType::Circle) {
        const Vec2 r{shape.radius, shape.radius};
        return {xf.p - r, xf.p + r};
    }
    constexpr float inf = std::numeric_limits<float>::max();
    AABB box{{inf, inf}, {-inf, -inf}};
    for (int i = 0; i < shape.count; ++i) {
        const Vec2 v = apply(xf, shape.verts[i]);
        box.min = {std::min(box.min.x, v.x), std::min(box.min.y, v.y)};
        box.max = {std::max(box.max.x, v.x), std::max(box.max.y, v.y)};
    }
    return box;
}

bool containsPoint(const Shape& shape, const Transform& xf, Vec2 worldPoint) {
    const Vec2 p = applyInv(xf, worldPoint);
    if (shape.type == ShapeType::Circle) return lengthSq(p) <= shape.radius * shape.radius;
    for (int i = 0; i < shape.count; ++i) {
        if (dot(shape.normals[i], p - shape.verts[i]) > 0.0f) return false;
    }
    return true;
}

}  // namespace phys
