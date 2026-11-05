#include "collision.hpp"

#include <limits>

namespace phys {
namespace {

bool collideCircles(const Shape& a, const Transform& xfA, const Shape& b, const Transform& xfB,
                    float margin, Manifold& m) {
    const Vec2 d = xfB.p - xfA.p;
    const float dist = length(d);
    const float sep = dist - a.radius - b.radius;
    if (sep > margin) return false;

    m.normal = dist > 1e-7f ? d / dist : Vec2{0.0f, 1.0f};
    m.count = 1;
    m.points[0].point = xfA.p + m.normal * (a.radius + 0.5f * sep);
    m.points[0].separation = sep;
    m.points[0].id = 0;
    return true;
}

// The returned normal points from the polygon towards the circle.
bool collidePolygonCircle(const Shape& poly, const Transform& xfP, const Shape& circle,
                          const Transform& xfC, float margin, Manifold& m) {
    const Vec2 c = applyInv(xfP, xfC.p);
    const float r = circle.radius;

    int edge = 0;
    float maxSep = -std::numeric_limits<float>::max();
    for (int i = 0; i < poly.count; ++i) {
        const float s = dot(poly.normals[i], c - poly.verts[i]);
        if (s > r + margin) return false;
        if (s > maxSep) {
            maxSep = s;
            edge = i;
        }
    }

    const Vec2 v1 = poly.verts[edge];
    const Vec2 v2 = poly.verts[(edge + 1) % poly.count];
    Vec2 normal;
    float sep;
    uint32_t id;

    if (maxSep < 1e-5f) {
        // Centre is inside the polygon: push out through the nearest face.
        normal = poly.normals[edge];
        sep = maxSep - r;
        id = static_cast<uint32_t>(edge);
    } else {
        const float u1 = dot(c - v1, v2 - v1);
        const float u2 = dot(c - v2, v1 - v2);
        if (u1 <= 0.0f || u2 <= 0.0f) {
            // Voronoi region of a vertex.
            const Vec2 v = u1 <= 0.0f ? v1 : v2;
            const Vec2 d = c - v;
            const float dist = length(d);
            sep = dist - r;
            if (sep > margin) return false;
            normal = dist > 1e-7f ? d / dist : poly.normals[edge];
            id = 0x100u + (u1 <= 0.0f ? static_cast<uint32_t>(edge)
                                      : static_cast<uint32_t>((edge + 1) % poly.count));
        } else {
            normal = poly.normals[edge];
            sep = maxSep - r;
            id = static_cast<uint32_t>(edge);
        }
    }

    const Vec2 localPoint = c - normal * (r + 0.5f * sep);
    m.normal = rotate(xfP.q, normal);
    m.count = 1;
    m.points[0].point = apply(xfP, localPoint);
    m.points[0].separation = sep;
    m.points[0].id = id;
    return true;
}

// Largest separation along the face normals of `a`.
float findMaxSeparation(const Shape& a, const Transform& xfA, const Shape& b, const Transform& xfB,
                        int& bestEdge) {
    float best = -std::numeric_limits<float>::max();
    bestEdge = 0;
    for (int i = 0; i < a.count; ++i) {
        const Vec2 n = invRotate(xfB.q, rotate(xfA.q, a.normals[i]));
        const Vec2 v = applyInv(xfB, apply(xfA, a.verts[i]));
        float sep = std::numeric_limits<float>::max();
        for (int j = 0; j < b.count; ++j) sep = std::min(sep, dot(n, b.verts[j] - v));
        if (sep > best) {
            best = sep;
            bestEdge = i;
        }
    }
    return best;
}

struct ClipVertex {
    Vec2 p;
    uint32_t id;
};

// Sutherland-Hodgman against one plane; keeps the side where dot(n, p) <= offset.
int clipSegment(ClipVertex out[2], const ClipVertex in[2], Vec2 n, float offset, uint32_t clipBit) {
    int count = 0;
    const float d0 = dot(n, in[0].p) - offset;
    const float d1 = dot(n, in[1].p) - offset;
    if (d0 <= 0.0f) out[count++] = in[0];
    if (d1 <= 0.0f) out[count++] = in[1];
    if (d0 * d1 < 0.0f) {
        const float t = d0 / (d0 - d1);
        out[count].p = in[0].p + (in[1].p - in[0].p) * t;
        out[count].id = (d0 > 0.0f ? in[0].id : in[1].id) | clipBit;
        ++count;
    }
    return count;
}

bool collidePolygons(const Shape& a, const Transform& xfA, const Shape& b, const Transform& xfB,
                     float margin, Manifold& m) {
    int edgeA = 0;
    const float sepA = findMaxSeparation(a, xfA, b, xfB, edgeA);
    if (sepA > margin) return false;

    int edgeB = 0;
    const float sepB = findMaxSeparation(b, xfB, a, xfA, edgeB);
    if (sepB > margin) return false;

    // Favour A as the reference shape so the manifold doesn't flicker between the two.
    constexpr float kFlipTolerance = 0.0005f;
    const bool flip = sepB > sepA + kFlipTolerance;
    const Shape& ref = flip ? b : a;
    const Shape& inc = flip ? a : b;
    const Transform& xfRef = flip ? xfB : xfA;
    const Transform& xfInc = flip ? xfA : xfB;
    const int refEdge = flip ? edgeB : edgeA;

    const Vec2 refNormal = rotate(xfRef.q, ref.normals[refEdge]);

    // Incident edge: the one facing most directly against the reference normal.
    const Vec2 localNormal = invRotate(xfInc.q, refNormal);
    int incEdge = 0;
    float minDot = std::numeric_limits<float>::max();
    for (int i = 0; i < inc.count; ++i) {
        const float d = dot(localNormal, inc.normals[i]);
        if (d < minDot) {
            minDot = d;
            incEdge = i;
        }
    }

    const uint32_t baseId = (flip ? 0x1000000u : 0u) | (static_cast<uint32_t>(refEdge) << 16);
    const int i1 = incEdge;
    const int i2 = (incEdge + 1) % inc.count;
    ClipVertex incident[2] = {
        {apply(xfInc, inc.verts[i1]), baseId | (static_cast<uint32_t>(i1) << 8)},
        {apply(xfInc, inc.verts[i2]), baseId | (static_cast<uint32_t>(i2) << 8)},
    };

    const Vec2 v1 = apply(xfRef, ref.verts[refEdge]);
    const Vec2 v2 = apply(xfRef, ref.verts[(refEdge + 1) % ref.count]);
    const Vec2 tangent = normalized(v2 - v1);

    ClipVertex clip1[2];
    ClipVertex clip2[2];
    if (clipSegment(clip1, incident, -tangent, -dot(tangent, v1), 1u) < 2) return false;
    if (clipSegment(clip2, clip1, tangent, dot(tangent, v2), 2u) < 2) return false;

    m.normal = flip ? -refNormal : refNormal;
    m.count = 0;
    for (const ClipVertex& v : clip2) {
        const float sep = dot(refNormal, v.p - v1);
        if (sep > margin) continue;
        ManifoldPoint& mp = m.points[m.count++];
        mp.point = v.p - refNormal * (0.5f * sep);
        mp.separation = sep;
        mp.id = v.id;
    }
    return m.count > 0;
}

}  // namespace

bool collide(const Shape& a, const Transform& xfA, const Shape& b, const Transform& xfB,
             float margin, Manifold& out) {
    const bool circleA = a.type == ShapeType::Circle;
    const bool circleB = b.type == ShapeType::Circle;
    if (circleA && circleB) return collideCircles(a, xfA, b, xfB, margin, out);
    if (!circleA && !circleB) return collidePolygons(a, xfA, b, xfB, margin, out);
    if (!circleA) return collidePolygonCircle(a, xfA, b, xfB, margin, out);

    // Circle vs polygon: solve the mirrored case, then flip the normal to stay A -> B.
    if (!collidePolygonCircle(b, xfB, a, xfA, margin, out)) return false;
    out.normal = -out.normal;
    return true;
}

}  // namespace phys
