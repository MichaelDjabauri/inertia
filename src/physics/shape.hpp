// Collision shapes: circles and convex polygons.
#pragma once

#include "math.hpp"

namespace phys {

constexpr int kMaxPolygonVerts = 8;

enum class ShapeType : uint8_t { Circle, Polygon };

struct Shape {
    ShapeType type = ShapeType::Circle;
    float radius = 0.5f;             // circle radius
    int count = 0;                   // polygon vertex count
    Vec2 verts[kMaxPolygonVerts];    // counter-clockwise, centred on the centroid
    Vec2 normals[kMaxPolygonVerts];  // outward normal of edge i -> i+1
    float boundRadius = 0.5f;        // distance from centroid to the furthest point
};

struct MassData {
    float mass = 0.0f;
    float inertia = 0.0f;
};

Shape makeCircle(float radius);
Shape makeBox(float halfWidth, float halfHeight);
// `points` must describe a convex outline. Winding is corrected and the shape
// is re-centred so the body origin sits on the centre of mass.
Shape makePolygon(const Vec2* points, int count);
Shape makeRegularPolygon(int sides, float radius, float startAngle = 0.0f);

MassData computeMass(const Shape& shape, float density);
AABB computeAABB(const Shape& shape, const Transform& xf);
bool containsPoint(const Shape& shape, const Transform& xf, Vec2 worldPoint);

}  // namespace phys
