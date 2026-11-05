#include "painter.hpp"

#include <algorithm>
#include <cmath>

namespace gfx {
namespace {

using phys::Body;
using phys::Joint;
using phys::JointType;
using phys::Rot;

constexpr float kPi = 3.14159265358979f;

// Light comes from the upper left of the screen, and stays there as bodies turn.
// This one detail is most of what makes flat shapes read as solid objects.
const Vec2 kLightDir = phys::normalized({-0.5f, -0.87f});

float litAmount(Vec2 pixelNormal) {
    const float l = std::max(phys::dot(pixelNormal, kLightDir), 0.0f);
    return l * l;
}

Col heatColour(float speed) {
    static const Col stops[5] = {linearHex(0x27306B), linearHex(0x7A2E9E), linearHex(0xF0504F), linearHex(0xFFB547),
                                 linearHex(0xFFF7D6)};
    const float t = std::clamp(speed / 24.0f, 0.0f, 0.999f) * 4.0f;
    const int i = static_cast<int>(t);
    return mix(stops[i], stops[i + 1], t - static_cast<float>(i));
}

}  // namespace

Vec2 Painter::interpolated(const Body& b) const {
    if (b.type == phys::BodyType::Static) return b.pos;
    return phys::lerp(b.prevPos, b.pos, alpha_);
}

float Painter::interpolatedAngle(const Body& b) const { return phys::lerp(b.prevAngle, b.angle, alpha_); }

void Painter::addCircle(Item& item, const Body& b, Vec2 centre) {
    const float radius = b.shape.radius * view_.pixelsPerMetre;
    const int count = circleSegments(radius);
    item.circle = true;
    const float step = 2.0f * kPi / static_cast<float>(count);
    const float cs = std::cos(step), sn = std::sin(step);
    Vec2 dir{1.0f, 0.0f};
    for (int i = 0; i < count; ++i) {
        points_.push_back(centre + dir * radius);
        normals_.push_back(dir);
        dir = {dir.x * cs - dir.y * sn, dir.x * sn + dir.y * cs};
    }
    item.count = count;
}

void Painter::addPolygon(Item& item, const Body& b, Vec2 centre, float angle) {
    const phys::Shape& shape = b.shape;
    const Rot q(angle);

    float apothem = 1e9f;
    for (int i = 0; i < shape.count; ++i) {
        apothem = std::min(apothem, phys::dot(shape.normals[i], shape.verts[i]));
    }
    // Static geometry stays sharp, since its pieces have to butt up against each other.
    const float rounding = item.solid ? 0.0f : std::min(0.09f, 0.4f * apothem);
    const int arc = rounding * view_.pixelsPerMetre > 3.0f ? 3 : 1;

    int emitted = 0;
    for (int i = 0; i < shape.count; ++i) {
        const Vec2 before = shape.normals[(i + shape.count - 1) % shape.count];
        const Vec2 after = shape.normals[i];
        const Vec2 v = shape.verts[i];
        const Vec2 arcCentre = v - (before + after) * (rounding / (1.0f + phys::dot(before, after)));
        const int steps = rounding > 0.0f ? arc : 0;
        for (int k = 0; k <= steps; ++k) {
            const float t = steps > 0 ? static_cast<float>(k) / static_cast<float>(steps) : 0.0f;
            const Vec2 n = steps > 0 ? phys::normalized(phys::lerp(before, after, t)) : before;
            const Vec2 local = rounding > 0.0f ? arcCentre + n * rounding : v;
            const Vec2 world = phys::rotate(q, local);
            points_.push_back(centre + View::flip(world) * view_.pixelsPerMetre);
            normals_.push_back(View::flip(phys::rotate(q, n)));
            ++emitted;
            if (rounding <= 0.0f) {
                // Hard corner: repeat the vertex so each edge keeps its own normal.
                points_.push_back(centre + View::flip(world) * view_.pixelsPerMetre);
                normals_.push_back(View::flip(phys::rotate(q, after)));
                ++emitted;
            }
        }
    }
    item.count = emitted;
}

void Painter::prepare(const phys::World& world, const scene::Style& style, const View& view, float alpha, float dpi,
                      const Options& options) {
    view_ = view;
    dpi_ = dpi;
    alpha_ = alpha;
    solidColour_ = linearHex(style.solid);
    solidRim_ = linearHex(style.solidRim);

    items_.clear();
    points_.clear();
    normals_.clear();

    const auto& bodies = world.bodies();
    for (int i = 0; i < static_cast<int>(bodies.size()); ++i) {
        const Body& b = bodies[i];
        if (!b.alive || !b.visible) continue;

        const Vec2 worldPos = interpolated(b);
        const Vec2 centre = view.toPixels(worldPos);
        const float reach = b.shape.boundRadius * view.pixelsPerMetre + 40.0f;
        if (centre.x < -reach || centre.y < -reach || centre.x > view.width + reach || centre.y > view.height + reach) {
            continue;
        }

        Item item;
        item.body = i;
        item.first = static_cast<int>(points_.size());
        item.centre = centre;
        item.angle = interpolatedAngle(b);
        item.solid = b.type == phys::BodyType::Static;
        item.radiusPixels = b.shape.boundRadius * view.pixelsPerMetre;
        item.glow = b.glow;
        item.flash = b.flash;
        item.colour = linearHex(b.color);
        if (options.heatView && b.isDynamic()) item.colour = heatColour(phys::length(b.vel));

        if (b.shape.type == phys::ShapeType::Circle) {
            addCircle(item, b, centre);
        } else {
            addPolygon(item, b, centre, item.angle);
            // Gems keep their facets. On a box they'd cross the face in a big X.
            item.smooth = b.shape.count <= 4;
        }
        items_.push_back(item);
    }
}

void Painter::drawShadows(Canvas& canvas) {
    const float px = view_.pixelsPerMetre;
    const Vec2 offset = Vec2{0.05f, 0.11f} * px;
    const float soft = 0.22f * px;
    const Col dark{0.0f, 0.0f, 0.0f, 0.34f};
    const Col clear{0.0f, 0.0f, 0.0f, 0.0f};

    // Hundreds of tiny shadows cost more than they add.
    const bool busy = items_.size() > 500;

    for (const Item& item : items_) {
        if (item.solid || item.glow > 2.0f) continue;
        if (busy && item.radiusPixels < 12.0f * dpi_) continue;
        scratch_.assign(points_.begin() + item.first, points_.begin() + item.first + item.count);
        for (Vec2& p : scratch_) p += offset;
        colours_.assign(item.count, dark);
        canvas.fan(item.centre + offset, dark, scratch_.data(), colours_.data(), item.count);
        canvas.band(scratch_.data(), normals_.data() + item.first, item.count, 0.0f, dark, soft, clear);
    }
}

void Painter::drawSolids(Canvas& canvas) {
    for (const Item& item : items_) {
        if (!item.solid) continue;
        const Vec2* p = points_.data() + item.first;
        const Vec2* n = normals_.data() + item.first;
        colours_.resize(item.count);
        for (int i = 0; i < item.count; ++i) {
            // The top of a solid catches the light, its body sinks into the dark.
            const float top = std::max(-n[i].y, 0.0f);
            colours_[i] = times(item.colour, 0.75f + 0.5f * top);
        }
        const Col centre = times(item.colour, 0.7f);
        canvas.fan(item.centre, centre, p, colours_.data(), item.count);
        canvas.band(p, n, item.count, 0.0f, item.colour, dpi_, withAlpha(item.colour, 0.0f));
    }
}

void Painter::drawBodies(Canvas& canvas) {
    const Col white{1.0f, 1.0f, 1.0f, 1.0f};

    for (const Item& item : items_) {
        if (item.solid) continue;
        const Vec2* p = points_.data() + item.first;
        const Vec2* n = normals_.data() + item.first;
        const Col base = mix(item.colour, white, std::min(item.flash * 0.45f, 0.45f));

        colours_.resize(item.count);
        coloursB_.resize(item.count);
        for (int i = 0; i < item.count; ++i) {
            float shade, lit;
            if (item.smooth) {
                const float along = phys::dot(p[i] - item.centre, kLightDir) / item.radiusPixels;
                shade = 0.5f + 0.5f * along;
                lit = 0.5f * shade * shade;
            } else {
                shade = 0.5f + 0.5f * phys::dot(n[i], kLightDir);
                lit = litAmount(n[i]);
            }
            colours_[i] = times(base, 0.34f + 0.42f * shade + 0.5f * lit);
            coloursB_[i] = withAlpha(colours_[i], 0.0f);
        }
        const Col centre = times(base, item.smooth ? 0.58f : 0.62f);

        canvas.fan(item.centre, centre, p, colours_.data(), item.count);
        canvas.band(p, n, colours_.data(), coloursB_.data(), item.count, 0.0f, dpi_);

        if (item.circle && item.radiusPixels > 5.0f * dpi_) {
            // A mark off-centre shows spin, which a plain disc cannot.
            const Vec2 dir = View::flip(Vec2{std::cos(item.angle), std::sin(item.angle)});
            const float r = item.radiusPixels;
            canvas.disc(item.centre + dir * (r * 0.58f), r * 0.13f, times(base, 0.28f), dpi_);
        }
    }
}

void Painter::drawEmissive(Canvas& canvas) {
    for (const Item& item : items_) {
        const Vec2* p = points_.data() + item.first;
        const Vec2* n = normals_.data() + item.first;

        if (item.solid) {
            if (item.circle) {
                // Round solids get the whole rim, brightest where it faces the light.
                colours_.resize(item.count);
                coloursB_.resize(item.count);
                for (int i = 0; i < item.count; ++i) {
                    colours_[i] = withAlpha(solidRim_, 0.04f + 0.2f * litAmount(n[i]));
                    coloursB_[i] = withAlpha(solidRim_, 0.0f);
                }
                canvas.band(p, n, coloursB_.data(), colours_.data(), item.count, -1.5f * dpi_, -0.25f * dpi_);
                canvas.band(p, n, colours_.data(), coloursB_.data(), item.count, -0.25f * dpi_, 0.75f * dpi_);
                continue;
            }
            // A thin bright line along every upward face.
            for (int i = 0; i < item.count; ++i) {
                const Vec2 q = p[(i + 1) % item.count];
                const float top = std::max(-n[i].y, 0.0f);
                if (top < 0.3f || phys::lengthSq(q - p[i]) < 0.01f) continue;
                canvas.line(p[i], q, 0.6f * dpi_, withAlpha(solidRim_, 0.2f * top * top), 1.1f * dpi_);
            }
            continue;
        }

        const float heat = std::min(item.flash, 1.0f);
        colours_.resize(item.count);
        coloursB_.resize(item.count);
        for (int i = 0; i < item.count; ++i) {
            const float lit = litAmount(n[i]);
            colours_[i] = withAlpha(item.colour, 0.05f + 0.34f * lit + heat * 0.4f);
            coloursB_[i] = withAlpha(item.colour, 0.0f);
        }
        const float rim = 1.6f * dpi_;
        canvas.band(p, n, coloursB_.data(), colours_.data(), item.count, -rim, -0.25f * dpi_);
        canvas.band(p, n, colours_.data(), coloursB_.data(), item.count, -0.25f * dpi_, 0.75f * dpi_);

        // Specular highlight, for round things with enough size to carry one.
        if (item.circle && item.radiusPixels > 7.0f * dpi_) {
            const float r = item.radiusPixels;
            const Vec2 spot = item.centre + kLightDir * (r * 0.42f);
            const Col core = withAlpha(mix(item.colour, {1.0f, 1.0f, 1.0f, 1.0f}, 0.6f), 0.32f);
            canvas.softDisc(spot, r * 0.42f, core, withAlpha(core, 0.0f));
        }

        if (item.glow > 0.0f || heat > 0.05f) {
            const float size = item.radiusPixels;
            const float amount = std::min(item.glow + heat, 6.0f);
            const float reach = size + 3.0f * dpi_;
            canvas.glow(item.centre, reach * (2.2f + amount * 0.5f), withAlpha(item.colour, std::min(0.16f * amount, 0.5f)));
            if (item.glow > 2.0f) canvas.glow(item.centre, reach * 11.0f, withAlpha(item.colour, 0.07f));
            if (item.glow > 2.0f) {
                canvas.disc(item.centre, size * 1.02f, mix(item.colour, {1.0f, 1.0f, 1.0f, 1.0f}, 0.75f), dpi_);
            }
        }
    }
}

void Painter::drawJointLines(Canvas& canvas, const phys::World& world, const scene::Style& style) {
    const Col colour = withAlpha(linearHex(style.joint), 0.3f);
    const auto& bodies = world.bodies();

    for (const Joint& j : world.joints()) {
        if (!j.alive || !j.visible || j.type != JointType::Distance) continue;
        const Body& a = bodies[j.a];
        const Body& b = bodies[j.b];
        const Vec2 pa = interpolated(a) + phys::rotate(Rot(interpolatedAngle(a)), j.localA);
        const Vec2 pb = interpolated(b) + phys::rotate(Rot(interpolatedAngle(b)), j.localB);
        const Vec2 sa = view_.toPixels(pa);
        const Vec2 sb = view_.toPixels(pb);

        if (j.hertz <= 0.0f) {
            canvas.line(sa, sb, 0.8f * dpi_, colour, dpi_);
            continue;
        }

        // Coil for a spring, compressing as it shortens.
        constexpr int kTurns = 8;
        const Vec2 along = sb - sa;
        const Vec2 side = phys::normalized(phys::perpLeft(along)) * (0.1f * view_.pixelsPerMetre);
        scratch_.clear();
        scratch_.push_back(sa);
        for (int i = 1; i < kTurns * 2; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kTurns * 2);
            scratch_.push_back(sa + along * t + side * (i % 2 == 0 ? -1.0f : 1.0f));
        }
        scratch_.push_back(sb);
        canvas.polyline(scratch_.data(), static_cast<int>(scratch_.size()), 0.8f * dpi_, colour, dpi_);
    }
}

void Painter::drawPins(Canvas& canvas, const phys::World& world, const scene::Style& style) {
    const Col colour = linearHex(style.joint);
    const auto& bodies = world.bodies();
    const float radius = std::clamp(0.05f * view_.pixelsPerMetre, 1.5f * dpi_, 3.5f * dpi_);

    for (const Joint& j : world.joints()) {
        if (!j.alive || !j.visible || j.type != JointType::Revolute) continue;
        const Body& b = bodies[j.b];
        const Vec2 pin = interpolated(b) + phys::rotate(Rot(interpolatedAngle(b)), j.localB);
        canvas.disc(view_.toPixels(pin), radius, withAlpha(colour, 0.5f), dpi_);
    }
}

void Painter::drawBlobs(Canvas& canvas, const phys::World& world, const std::vector<scene::Blob>& blobs) {
    const auto& bodies = world.bodies();
    constexpr int kSubdivisions = 5;

    skins_.clear();
    skinPoints_.clear();
    skinNormals_.clear();

    for (const scene::Blob& blob : blobs) {
        const int n = static_cast<int>(blob.ring.size());
        if (n < 3) continue;

        Vec2 centre;
        scratch_.resize(n);
        for (int i = 0; i < n; ++i) {
            const Body& b = bodies[blob.ring[i]];
            scratch_[i] = interpolated(b);
            centre += scratch_[i];
        }
        centre = centre * (1.0f / static_cast<float>(n));

        // Push the outline out by the particle radius, so the skin covers the particles.
        const float skin = bodies[blob.ring[0]].shape.radius;

        SkinRange range;
        range.first = static_cast<int>(skinPoints_.size());
        range.centre = view_.toPixels(centre);
        range.colour = linearHex(blob.colour);

        for (int i = 0; i < n; ++i) {
            const Vec2 p0 = scratch_[(i + n - 1) % n];
            const Vec2 p1 = scratch_[i];
            const Vec2 p2 = scratch_[(i + 1) % n];
            const Vec2 p3 = scratch_[(i + 2) % n];
            for (int k = 0; k < kSubdivisions; ++k) {
                // Catmull-Rom through the ring, which turns the polygon into a smooth curve.
                const float t = static_cast<float>(k) / static_cast<float>(kSubdivisions);
                const float t2 = t * t, t3 = t2 * t;
                const Vec2 point = (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 +
                                    (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) *
                                   0.5f;
                const Vec2 tangent = ((p2 - p0) + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * (2.0f * t) +
                                      (p1 * 3.0f - p0 - p2 * 3.0f + p3) * (3.0f * t2)) *
                                     0.5f;
                const Vec2 normal = phys::normalized(phys::perpRight(tangent));
                const Vec2 pixelNormal = View::flip(normal);
                skinPoints_.push_back(view_.toPixels(point + normal * skin));
                skinNormals_.push_back(pixelNormal);
            }
        }
        range.count = static_cast<int>(skinPoints_.size()) - range.first;
        skins_.push_back(range);

        const Vec2* p = skinPoints_.data() + range.first;
        const Vec2* nrm = skinNormals_.data() + range.first;
        colours_.resize(range.count);
        coloursB_.resize(range.count);
        for (int i = 0; i < range.count; ++i) {
            const float lit = litAmount(nrm[i]);
            const float shade = 0.5f + 0.5f * phys::dot(nrm[i], kLightDir);
            colours_[i] = times(range.colour, 0.3f + 0.4f * shade + 0.55f * lit);
            coloursB_[i] = withAlpha(colours_[i], 0.0f);
        }
        canvas.fan(range.centre, times(range.colour, 0.55f), p, colours_.data(), range.count);
        canvas.band(p, nrm, colours_.data(), coloursB_.data(), range.count, 0.0f, dpi_);
    }
}

void Painter::drawBlobEmissive(Canvas& canvas) {
    for (const SkinRange& range : skins_) {
        const Vec2* p = skinPoints_.data() + range.first;
        const Vec2* n = skinNormals_.data() + range.first;
        colours_.resize(range.count);
        coloursB_.resize(range.count);
        for (int i = 0; i < range.count; ++i) {
            colours_[i] = withAlpha(range.colour, 0.06f + 0.42f * litAmount(n[i]));
            coloursB_[i] = withAlpha(range.colour, 0.0f);
        }
        canvas.band(p, n, coloursB_.data(), colours_.data(), range.count, -2.5f * dpi_, -0.25f * dpi_);
        canvas.band(p, n, colours_.data(), coloursB_.data(), range.count, -0.25f * dpi_, 0.75f * dpi_);
    }
}

void Painter::drawHorizon(Canvas& canvas, const std::vector<Vec2>& terrain, const scene::Style& style) {
    if (terrain.empty()) return;

    // Two ranges of far hills, sliding slower than the foreground to give the scene some depth.
    struct Layer {
        float parallax, height, base, frequency, seed, shade;
    };
    const Layer layers[2] = {{0.1f, 2.6f, 0.46f, 0.10f, 1.3f, 0.75f}, {0.28f, 3.4f, 0.58f, 0.075f, 4.1f, 1.05f}};
    const Col solid = linearHex(style.solid);
    const float step = 10.0f * dpi_;
    const float ppm = view_.pixelsPerMetre;

    for (const Layer& layer : layers) {
        const Col top = times(solid, layer.shade);
        const Col fade = times(solid, layer.shade * 0.55f);
        const float baseline = view_.height * layer.base + view_.centre.y * layer.parallax * ppm;

        const auto ridge = [&](float px) {
            const float x = (px - view_.originX) / ppm + view_.centre.x * layer.parallax;
            const float h = std::sin(x * layer.frequency + layer.seed) * 1.0f +
                            std::sin(x * layer.frequency * 2.3f + layer.seed * 2.0f) * 0.45f +
                            std::sin(x * layer.frequency * 5.1f + layer.seed * 3.0f) * 0.14f;
            return baseline - h * layer.height * ppm;
        };

        float y0 = ridge(0.0f);
        for (float px = 0.0f; px < view_.width; px += step) {
            const float next = px + step;
            const float y1 = ridge(next);
            canvas.quad({px, y0}, top, {next, y1}, top, {next, view_.height}, fade, {px, view_.height}, fade);
            y0 = y1;
        }
    }
}

void Painter::drawTerrain(Canvas& canvas, const std::vector<Vec2>& terrain, const scene::Style& style) {
    if (terrain.size() < 2) return;
    const Col top = times(linearHex(style.solid), 1.5f);
    const Col bottom = times(linearHex(style.solid), 0.35f);
    const float depth = view_.height + 20.0f;

    for (size_t i = 0; i + 1 < terrain.size(); ++i) {
        const Vec2 a = view_.toPixels(terrain[i]);
        const Vec2 b = view_.toPixels(terrain[i + 1]);
        if (b.x < -2.0f || a.x > view_.width + 2.0f) continue;
        const float aDepth = a.y + depth * 0.5f, bDepth = b.y + depth * 0.5f;
        canvas.quad(a, top, b, top, {b.x, bDepth}, bottom, {a.x, aDepth}, bottom);
        canvas.quad({a.x, aDepth}, bottom, {b.x, bDepth}, bottom, {b.x, view_.height + depth}, bottom,
                    {a.x, view_.height + depth}, bottom);
    }
}

void Painter::drawTerrainRim(Canvas& canvas, const std::vector<Vec2>& terrain, const scene::Style& style) {
    if (terrain.size() < 2) return;
    const Col rim = withAlpha(linearHex(style.solidRim), 0.34f);
    for (size_t i = 0; i + 1 < terrain.size(); ++i) {
        const Vec2 a = view_.toPixels(terrain[i]);
        const Vec2 b = view_.toPixels(terrain[i + 1]);
        if (b.x < -2.0f || a.x > view_.width + 2.0f) continue;
        canvas.line(a, b, 0.8f * dpi_, rim, 1.2f * dpi_);
    }
}

void Painter::drawGuides(Canvas& canvas, const std::vector<scene::Guide>& guides) {
    for (const scene::Guide& guide : guides) {
        scratch_.clear();
        for (const Vec2 p : guide.points) scratch_.push_back(view_.toPixels(p));
        canvas.polyline(scratch_.data(), static_cast<int>(scratch_.size()), 0.8f * dpi_,
                        linearHex(guide.colour, guide.alpha), dpi_);
    }
}

void Painter::drawDebug(Canvas& canvas, const phys::World& world) {
    const Col contactColour = linearHex(0xFF5D73);
    const Col normalColour = linearHex(0xFFE27A, 0.8f);
    const Col sleepColour = linearHex(0x6FA8FF, 0.5f);
    const float dot = 2.2f * dpi_;

    for (const phys::Contact& c : world.contacts()) {
        for (int k = 0; k < c.count; ++k) {
            const Vec2 p = view_.toPixels(c.points[k].point);
            const Vec2 tip = view_.toPixels(c.points[k].point + c.normal * 0.35f);
            canvas.line(p, tip, 0.5f * dpi_, normalColour, dpi_);
            canvas.disc(p, dot, contactColour, dpi_);
        }
    }
    for (const Item& item : items_) {
        const Body& b = world.bodies()[item.body];
        if (b.isDynamic() && !b.awake) {
            canvas.band(points_.data() + item.first, normals_.data() + item.first, item.count, 0.0f, sleepColour,
                        1.5f * dpi_, sleepColour);
        }
    }
}

}  // namespace gfx
