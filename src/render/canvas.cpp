#include "canvas.hpp"

#include <algorithm>
#include <cmath>

#include "rlgl.h"

namespace gfx {
namespace {

unsigned char toByte(float v) {
    return static_cast<unsigned char>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

constexpr float kPi = 3.14159265358979f;

}  // namespace

int circleSegments(float radiusPixels) {
    return std::clamp(static_cast<int>(6.0f + radiusPixels * 0.9f), 10, 96);
}

void Canvas::begin() { open_ = false; }

void Canvas::end() {
    if (open_) rlEnd();
    open_ = false;
}

void Canvas::vertex(Vec2 p, const Col& c) {
    // Opened lazily: an empty rlBegin/rlEnd pair makes rlgl drop the texture set for the
    // next draw, which turns the first glyph of the following text into a white box.
    if (!open_) {
        rlBegin(RL_TRIANGLES);
        open_ = true;
    }
    rlColor4ub(toByte(c.r), toByte(c.g), toByte(c.b), toByte(c.a));
    rlVertex2f(p.x, p.y);
}

void Canvas::triangle(Vec2 a, const Col& ca, Vec2 b, const Col& cb, Vec2 c, const Col& cc) {
    vertex(a, ca);
    vertex(b, cb);
    vertex(c, cc);
}

void Canvas::quad(Vec2 a, const Col& ca, Vec2 b, const Col& cb, Vec2 c, const Col& cc, Vec2 d, const Col& cd) {
    triangle(a, ca, b, cb, c, cc);
    triangle(a, ca, c, cc, d, cd);
}

void Canvas::fan(Vec2 centre, const Col& centreColour, const Vec2* points, const Col* colours, int count) {
    for (int i = 0; i < count; ++i) {
        const int j = i + 1 == count ? 0 : i + 1;
        triangle(centre, centreColour, points[i], colours[i], points[j], colours[j]);
    }
}

void Canvas::band(const Vec2* p, const Vec2* n, int count, float offA, const Col& colA, float offB, const Col& colB) {
    Vec2 a0 = p[count - 1] + n[count - 1] * offA;
    Vec2 b0 = p[count - 1] + n[count - 1] * offB;
    for (int i = 0; i < count; ++i) {
        const Vec2 a1 = p[i] + n[i] * offA;
        const Vec2 b1 = p[i] + n[i] * offB;
        quad(a0, colA, a1, colA, b1, colB, b0, colB);
        a0 = a1;
        b0 = b1;
    }
}

void Canvas::band(const Vec2* p, const Vec2* n, const Col* colA, const Col* colB, int count, float offA, float offB) {
    int prev = count - 1;
    for (int i = 0; i < count; ++i) {
        quad(p[prev] + n[prev] * offA, colA[prev], p[i] + n[i] * offA, colA[i], p[i] + n[i] * offB, colB[i],
             p[prev] + n[prev] * offB, colB[prev]);
        prev = i;
    }
}

void Canvas::buildCircle(Vec2 centre, float radius) {
    const int count = circleSegments(radius);
    points_.resize(count);
    normals_.resize(count);
    const float step = 2.0f * kPi / static_cast<float>(count);
    const float cs = std::cos(step), sn = std::sin(step);
    Vec2 dir{1.0f, 0.0f};
    for (int i = 0; i < count; ++i) {
        normals_[i] = dir;
        points_[i] = centre + dir * radius;
        dir = {dir.x * cs - dir.y * sn, dir.x * sn + dir.y * cs};
    }
}

void Canvas::disc(Vec2 centre, float radius, const Col& colour, float feather) {
    buildCircle(centre, std::max(radius - feather * 0.5f, 0.0f));
    const int n = static_cast<int>(points_.size());
    for (int i = 0; i < n; ++i) {
        const int j = i + 1 == n ? 0 : i + 1;
        triangle(centre, colour, points_[i], colour, points_[j], colour);
    }
    band(points_.data(), normals_.data(), n, 0.0f, colour, feather, withAlpha(colour, 0.0f));
}

void Canvas::ring(Vec2 centre, float radius, float width, const Col& colour, float feather) {
    buildCircle(centre, radius);
    const int n = static_cast<int>(points_.size());
    const float half = width * 0.5f;
    const Col clear = withAlpha(colour, 0.0f);
    band(points_.data(), normals_.data(), n, -half, colour, half, colour);
    band(points_.data(), normals_.data(), n, half, colour, half + feather, clear);
    band(points_.data(), normals_.data(), n, -half - feather, clear, -half, colour);
}

void Canvas::softDisc(Vec2 centre, float radius, const Col& inner, const Col& outer) {
    buildCircle(centre, radius);
    const int n = static_cast<int>(points_.size());
    for (int i = 0; i < n; ++i) {
        const int j = i + 1 == n ? 0 : i + 1;
        triangle(centre, inner, points_[i], outer, points_[j], outer);
    }
}

void Canvas::glow(Vec2 centre, float radius, const Col& colour) {
    constexpr int kRings = 7;
    buildCircle(centre, radius);
    const int n = static_cast<int>(points_.size());
    const auto strength = [&](float t) {
        const float f = 1.0f - t;
        return colour.a * f * f * f;  // cubic, so the slope reaches zero exactly at the rim
    };
    for (int i = 0; i < n; ++i) {
        const int j = i + 1 == n ? 0 : i + 1;
        for (int ring = 0; ring < kRings; ++ring) {
            const float t0 = static_cast<float>(ring) / kRings;
            const float t1 = static_cast<float>(ring + 1) / kRings;
            const Col c0 = withAlpha(colour, strength(t0));
            const Col c1 = withAlpha(colour, strength(t1));
            const Vec2 a0 = centre + normals_[i] * (radius * t0);
            const Vec2 b0 = centre + normals_[j] * (radius * t0);
            const Vec2 a1 = centre + normals_[i] * (radius * t1);
            const Vec2 b1 = centre + normals_[j] * (radius * t1);
            quad(a0, c0, a1, c1, b1, c1, b0, c0);
        }
    }
}

void Canvas::line(Vec2 a, Vec2 b, float width, const Col& colour, float feather) {
    const Vec2 dir = phys::normalized(b - a, {1.0f, 0.0f});
    const Vec2 n = phys::perpLeft(dir);
    const float half = width * 0.5f;
    const Col clear = withAlpha(colour, 0.0f);
    const Vec2 solid = n * half;
    const Vec2 edge = n * (half + feather);
    quad(a - solid, colour, b - solid, colour, b + solid, colour, a + solid, colour);
    quad(a + solid, colour, b + solid, colour, b + edge, clear, a + edge, clear);
    quad(a - edge, clear, b - edge, clear, b - solid, colour, a - solid, colour);
}

void Canvas::polyline(const Vec2* points, int count, float width, const Col& colour, float feather) {
    for (int i = 0; i + 1 < count; ++i) line(points[i], points[i + 1], width, colour, feather);
}

void Canvas::ribbon(const Vec2* p, const float* w, const Col* c, int count) {
    if (count < 2) return;
    Vec2 prevNormal;
    for (int i = 0; i < count; ++i) {
        const Vec2 tangent = phys::normalized(p[std::min(i + 1, count - 1)] - p[std::max(i - 1, 0)], {1.0f, 0.0f});
        const Vec2 normal = phys::perpLeft(tangent);
        if (i > 0) {
            const Col clearA = withAlpha(c[i - 1], 0.0f);
            const Col clearB = withAlpha(c[i], 0.0f);
            const Vec2 pa = p[i - 1], pb = p[i];
            const Vec2 na = prevNormal * w[i - 1], nb = normal * w[i];
            quad(pa - na, clearA, pb - nb, clearB, pb, c[i], pa, c[i - 1]);
            quad(pa, c[i - 1], pb, c[i], pb + nb, clearB, pa + na, clearA);
        }
        prevNormal = normal;
    }
}

void Canvas::buildRoundedRect(float x, float y, float w, float h, float radius) {
    constexpr int kArc = 8;
    radius = std::min({radius, w * 0.5f, h * 0.5f});
    points_.clear();
    normals_.clear();
    const Vec2 corners[4] = {{x + w - radius, y + radius},
                             {x + w - radius, y + h - radius},
                             {x + radius, y + h - radius},
                             {x + radius, y + radius}};
    for (int c = 0; c < 4; ++c) {
        const float start = -0.5f * kPi + static_cast<float>(c) * 0.5f * kPi;
        for (int i = 0; i <= kArc; ++i) {
            const float a = start + 0.5f * kPi * static_cast<float>(i) / static_cast<float>(kArc);
            const Vec2 dir{std::cos(a), std::sin(a)};
            normals_.push_back(dir);
            points_.push_back(corners[c] + dir * radius);
        }
    }
}

void Canvas::roundedRect(float x, float y, float w, float h, float radius, const Col& colour) {
    buildRoundedRect(x, y, w, h, radius);
    const int n = static_cast<int>(points_.size());
    const Vec2 centre{x + w * 0.5f, y + h * 0.5f};
    for (int i = 0; i < n; ++i) {
        const int j = i + 1 == n ? 0 : i + 1;
        triangle(centre, colour, points_[i] - normals_[i] * 0.5f, colour, points_[j] - normals_[j] * 0.5f, colour);
    }
    band(points_.data(), normals_.data(), n, -0.5f, colour, 0.5f, withAlpha(colour, 0.0f));
}

void Canvas::roundedRectStroke(float x, float y, float w, float h, float radius, float width, const Col& colour) {
    buildRoundedRect(x, y, w, h, radius);
    const int n = static_cast<int>(points_.size());
    const Col clear = withAlpha(colour, 0.0f);
    const float half = width * 0.5f;
    band(points_.data(), normals_.data(), n, -half, colour, half, colour);
    band(points_.data(), normals_.data(), n, half, colour, half + 0.75f, clear);
    band(points_.data(), normals_.data(), n, -half - 0.75f, clear, -half, colour);
}

void Canvas::triangleFilled(Vec2 a, Vec2 b, Vec2 c, const Col& colour) {
    triangle(a, colour, b, colour, c, colour);
    line(a, b, 0.0f, colour, 0.8f);
    line(b, c, 0.0f, colour, 0.8f);
    line(c, a, 0.0f, colour, 0.8f);
}

}  // namespace gfx
