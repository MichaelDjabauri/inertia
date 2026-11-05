#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "rlgl.h"

namespace gfx {
namespace {

constexpr float kPi = 3.14159265358979f;

const Col kInk = uiHex(0xF3F5F9);
const Col kInkDim = uiHex(0xF3F5F9, 0.62f);
const Col kInkFaint = uiHex(0xF3F5F9, 0.38f);

Color toColor(const Col& c) {
    const auto byte = [](float v) { return static_cast<unsigned char>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return {byte(c.r), byte(c.g), byte(c.b), byte(c.a)};
}

std::vector<int> fontCodepoints() {
    std::vector<int> codes;
    for (int c = 32; c < 127; ++c) codes.push_back(c);
    for (const int c : {0x00B7, 0x00D7, 0x2013, 0x2014, 0x2190, 0x2191, 0x2192, 0x2193}) codes.push_back(c);
    return codes;
}

}  // namespace

void Ui::load(const char* fontDir, float dpi) {
    auto codes = fontCodepoints();
    const auto loadFace = [&](Face f, const char* file, float size, float spacing) {
        const std::string path = std::string(fontDir) + "/" + file;
        TypeFace& t = faces_[static_cast<int>(f)];
        t.size = size;
        t.spacing = spacing;
        t.font = LoadFontEx(path.c_str(), static_cast<int>(std::round(size * dpi)), codes.data(), static_cast<int>(codes.size()));
        SetTextureFilter(t.font.texture, TEXTURE_FILTER_BILINEAR);
    };
    loadFace(Face::Body, "Inter-Regular.ttf", 13.0f, 0.0f);
    loadFace(Face::Label, "Inter-Medium.ttf", 13.0f, 0.0f);
    loadFace(Face::Strong, "Inter-SemiBold.ttf", 13.0f, 0.0f);
    loadFace(Face::Title, "Inter-SemiBold.ttf", 26.0f, -0.3f);
    loadFace(Face::Mono, "JetBrainsMono-Regular.ttf", 11.5f, 0.0f);
}

void Ui::unload() {
    for (TypeFace& t : faces_) UnloadFont(t.font);
}

void Ui::beginFrame(Pipeline& pipeline, Canvas& canvas, float dt, float dpi, Vec2 screen, Col accent,
                    const Pointer& pointer) {
    pipeline_ = &pipeline;
    canvas_ = &canvas;
    dt_ = dt;
    dpi_ = dpi;
    screen_ = screen;
    accent_ = accent;
    mouse_ = pointer.position;
    pressed_ = pointer.leftPressed;
    down_ = pointer.leftDown;
    released_ = pointer.leftReleased;
    panelsThisFrame_.clear();
    tooltip_ = nullptr;
}

void Ui::endFrame() {
    if (tooltip_ != nullptr) {
        const float w = textWidth(Face::Label, tooltip_) + 18.0f;
        const float x = tooltipAnchor_.x - w * 0.5f;
        const float y = tooltipAnchor_.y - 32.0f;
        canvas_->begin();
        canvas_->roundedRect(x, y, w, 24.0f, 7.0f, uiHex(0x101218, 0.92f));
        canvas_->end();
        text(Face::Label, tooltip_, x + 9.0f, y + 5.0f, kInk);
    }
    panels_ = panelsThisFrame_;
    if (released_) active_ = nullptr;
}

bool Ui::wantsMouse(Vec2 mouse) const {
    if (active_ != nullptr) return true;
    return std::any_of(panels_.begin(), panels_.end(), [&](const Box2& b) { return b.contains(mouse); });
}

void Ui::panel(Box2 box, float radius) {
    pipeline_->frostedPanel(box.x, box.y, box.w, box.h, radius, dpi_, screen_.x, screen_.y, uiHex(0x0C0E14), 0.62f);
    panelsThisFrame_.push_back(box);
}

void Ui::text(Face f, const char* string, float x, float y, Col colour) {
    const TypeFace& t = face(f);
    // Landing on whole pixels keeps the glyphs from smearing across two rows.
    const Vector2 at{std::round(x * dpi_) / dpi_, std::round(y * dpi_) / dpi_};
    DrawTextEx(t.font, string, at, t.size, t.spacing, toColor(colour));
}

float Ui::textWidth(Face f, const char* string) const {
    const TypeFace& t = face(f);
    return MeasureTextEx(t.font, string, t.size, t.spacing).x;
}

float Ui::lineHeight(Face f) const { return face(f).size * 1.3f; }

float Ui::hoverAmount(const void* id, bool hovered) {
    float& value = hover_[id];
    const float target = hovered ? 1.0f : 0.0f;
    value += (target - value) * (1.0f - std::exp(-dt_ * 18.0f));
    return value;
}

void Ui::drawIcon(Icon icon, Vec2 c, const Col& col) {
    Canvas& k = *canvas_;
    switch (icon) {
        case Icon::Grab: {
            const Vec2 tip = c + Vec2{-4.5f, -7.5f};
            const Vec2 left = c + Vec2{-4.5f, 5.5f};
            const Vec2 notch = c + Vec2{-1.2f, 2.6f};
            const Vec2 right = c + Vec2{4.8f, 2.2f};
            k.triangleFilled(tip, left, notch, col);
            k.triangleFilled(tip, notch, right, col);
            k.line(notch, c + Vec2{2.0f, 7.5f}, 2.0f, col);
            break;
        }
        case Icon::Ball: k.disc(c, 7.5f, col); break;
        case Icon::Box: k.roundedRect(c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f, 2.5f, col); break;
        case Icon::Gem: {
            Vec2 pts[6];
            for (int i = 0; i < 6; ++i) {
                const float a = kPi / 6.0f + static_cast<float>(i) * kPi / 3.0f;
                pts[i] = c + Vec2{std::cos(a), std::sin(a)} * 8.2f;
            }
            for (int i = 0; i < 6; ++i) k.triangleFilled(c, pts[i], pts[(i + 1) % 6], col);
            break;
        }
        case Icon::Bomb: {
            k.disc(c, 3.2f, col);
            for (int i = 0; i < 8; ++i) {
                const float a = static_cast<float>(i) * kPi / 4.0f;
                const Vec2 d{std::cos(a), std::sin(a)};
                const float outer = i % 2 == 0 ? 9.0f : 7.0f;
                k.line(c + d * 5.2f, c + d * outer, 1.4f, col, 0.8f);
            }
            break;
        }
        case Icon::Pull: {
            k.disc(c, 2.4f, col);
            k.ring(c, 5.4f, 1.2f, withAlpha(col, col.a * 0.8f), 0.8f);
            k.ring(c, 8.6f, 1.2f, withAlpha(col, col.a * 0.45f), 0.8f);
            break;
        }
        case Icon::Play:
            k.triangleFilled(c + Vec2{-4.5f, -7.0f}, c + Vec2{7.0f, 0.0f}, c + Vec2{-4.5f, 7.0f}, col);
            break;
        case Icon::Pause:
            k.roundedRect(c.x - 5.5f, c.y - 7.0f, 4.0f, 14.0f, 1.2f, col);
            k.roundedRect(c.x + 1.5f, c.y - 7.0f, 4.0f, 14.0f, 1.2f, col);
            break;
        case Icon::Step:
            k.triangleFilled(c + Vec2{-6.5f, -6.5f}, c + Vec2{3.0f, 0.0f}, c + Vec2{-6.5f, 6.5f}, col);
            k.roundedRect(c.x + 3.5f, c.y - 6.5f, 2.8f, 13.0f, 1.0f, col);
            break;
        case Icon::Reset: {
            constexpr int kSegments = 22;
            Vec2 pts[kSegments + 1];
            const float start = -0.3f * kPi;
            for (int i = 0; i <= kSegments; ++i) {
                const float a = start + 1.55f * kPi * static_cast<float>(i) / kSegments;
                pts[i] = c + Vec2{std::cos(a), std::sin(a)} * 7.0f;
            }
            k.polyline(pts, kSegments + 1, 1.5f, col, 0.8f);
            const Vec2 head = pts[0];
            k.triangleFilled(head + Vec2{4.5f, 1.0f}, head + Vec2{-3.0f, -3.0f}, head + Vec2{-2.0f, 3.2f}, col);
            break;
        }
        case Icon::Settings: {
            const float rows[3] = {-5.5f, 0.0f, 5.5f};
            const float knobs[3] = {3.0f, -3.5f, 1.0f};
            for (int i = 0; i < 3; ++i) {
                k.line(c + Vec2{-8.0f, rows[i]}, c + Vec2{8.0f, rows[i]}, 1.3f, withAlpha(col, col.a * 0.7f), 0.8f);
                k.disc(c + Vec2{knobs[i], rows[i]}, 2.5f, col);
            }
            break;
        }
    }
}

bool Ui::iconButton(const void* id, Box2 box, Icon icon, bool isActive, const char* tooltip) {
    const bool hovered = box.contains(mouse_);
    const float hover = hoverAmount(id, hovered);
    if (hovered && pressed_) active_ = id;
    const bool clicked = hovered && released_ && active_ == id;
    if (hovered) {
        tooltip_ = tooltip;
        tooltipAnchor_ = {box.x + box.w * 0.5f, box.y};
    }

    canvas_->begin();
    if (isActive) {
        canvas_->roundedRect(box.x, box.y, box.w, box.h, 11.0f, withAlpha(accent_, 0.22f));
        canvas_->roundedRectStroke(box.x + 0.5f, box.y + 0.5f, box.w - 1.0f, box.h - 1.0f, 10.5f, 0.8f, withAlpha(accent_, 0.5f));
    } else if (hover > 0.01f) {
        canvas_->roundedRect(box.x, box.y, box.w, box.h, 11.0f, uiHex(0xFFFFFF, 0.075f * hover));
    }
    const Col colour = isActive ? mix(accent_, kInk, 0.45f) : withAlpha(kInk, 0.72f + 0.28f * hover);
    drawIcon(icon, {box.x + box.w * 0.5f, box.y + box.h * 0.5f}, colour);
    canvas_->end();
    return clicked;
}

bool Ui::listItem(const void* id, Box2 box, const char* label, const char* key, bool selected) {
    const bool hovered = box.contains(mouse_);
    const float hover = hoverAmount(id, hovered);
    if (hovered && pressed_) active_ = id;
    const bool clicked = hovered && released_ && active_ == id;

    canvas_->begin();
    if (selected) {
        canvas_->roundedRect(box.x, box.y, box.w, box.h, 8.0f, withAlpha(accent_, 0.17f));
        canvas_->disc({box.x + 11.0f, box.y + box.h * 0.5f}, 2.6f, mix(accent_, kInk, 0.3f));
    } else if (hover > 0.01f) {
        canvas_->roundedRect(box.x, box.y, box.w, box.h, 8.0f, uiHex(0xFFFFFF, 0.07f * hover));
    }
    canvas_->end();

    const float textY = box.y + (box.h - lineHeight(Face::Label)) * 0.5f + 0.5f;
    text(selected ? Face::Strong : Face::Label, label, box.x + 22.0f, textY, selected ? kInk : withAlpha(kInk, 0.68f + 0.32f * hover));
    if (key[0] != '\0') {
        const float w = textWidth(Face::Mono, key);
        text(Face::Mono, key, box.x + box.w - w - 10.0f, textY + 1.5f, kInkFaint);
    }
    return clicked;
}

bool Ui::slider(const void* id, Box2 box, const char* label, const char* valueText, float& value, float lo, float hi) {
    text(Face::Label, label, box.x, box.y, kInkDim);
    const float vw = textWidth(Face::Mono, valueText);
    text(Face::Mono, valueText, box.x + box.w - vw, box.y + 1.5f, kInk);

    const float trackY = box.y + 26.0f;
    const Box2 hit{box.x - 4.0f, trackY - 9.0f, box.w + 8.0f, 18.0f};
    const bool hovered = hit.contains(mouse_);
    const float hover = hoverAmount(id, hovered || active_ == id);
    if (hovered && pressed_) active_ = id;

    bool changed = false;
    if (active_ == id && down_) {
        const float t = std::clamp((mouse_.x - box.x) / box.w, 0.0f, 1.0f);
        const float next = lo + (hi - lo) * t;
        changed = next != value;
        value = next;
    }

    const float t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
    const float knobX = box.x + box.w * t;
    canvas_->begin();
    canvas_->roundedRect(box.x, trackY - 1.5f, box.w, 3.0f, 1.5f, uiHex(0xFFFFFF, 0.14f));
    canvas_->roundedRect(box.x, trackY - 1.5f, std::max(box.w * t, 3.0f), 3.0f, 1.5f, accent_);
    canvas_->disc({knobX, trackY}, 6.5f + hover * 1.3f, uiHex(0xFFFFFF));
    canvas_->end();
    return changed;
}

bool Ui::toggle(const void* id, Box2 box, const char* label, const char* key, bool& on) {
    const bool hovered = box.contains(mouse_);
    const float hover = hoverAmount(id, hovered);
    float& slide = slide_[id];
    slide += ((on ? 1.0f : 0.0f) - slide) * (1.0f - std::exp(-dt_ * 20.0f));
    if (hovered && pressed_) active_ = id;
    const bool clicked = hovered && released_ && active_ == id;
    if (clicked) on = !on;

    text(Face::Label, label, box.x, box.y + (box.h - lineHeight(Face::Label)) * 0.5f, withAlpha(kInk, 0.68f + 0.32f * hover));
    const float w = 28.0f, h = 16.0f;
    const float x = box.x + box.w - w;
    const float y = box.y + (box.h - h) * 0.5f;
    if (key[0] != '\0') {
        text(Face::Mono, key, x - textWidth(Face::Mono, key) - 8.0f, box.y + (box.h - lineHeight(Face::Mono)) * 0.5f, kInkFaint);
    }
    canvas_->begin();
    canvas_->roundedRect(x, y, w, h, 8.0f, mix(uiHex(0xFFFFFF, 0.14f), accent_, slide));
    canvas_->disc({x + 8.0f + (w - 16.0f) * slide, y + h * 0.5f}, 6.0f, uiHex(0xFFFFFF));
    canvas_->end();
    return clicked;
}

void Ui::divider(float x, float y, float height) {
    canvas_->begin();
    canvas_->line({x, y}, {x, y + height}, 0.5f, uiHex(0xFFFFFF, 0.16f), 0.6f);
    canvas_->end();
}

}  // namespace gfx
