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

// ASCII, Latin-1, Greek, and the punctuation and maths symbols the lessons use.
std::vector<int> fontCodepoints() {
    std::vector<int> codes;
    for (int c = 32; c < 127; ++c) codes.push_back(c);
    for (int c = 0xA0; c <= 0xFF; ++c) codes.push_back(c);
    for (int c = 0x391; c <= 0x3C9; ++c) codes.push_back(c);
    for (int c = 0x2080; c <= 0x2089; ++c) codes.push_back(c);
    for (int c = 0x2074; c <= 0x2079; ++c) codes.push_back(c);
    for (const int c : {0x1D62, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2070, 0x207B,
                        0x207F, 0x2153, 0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x221A, 0x221E, 0x2248, 0x2260,
                        0x2264, 0x2265}) {
        codes.push_back(c);
    }
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
    loadFace(Face::Heading, "Inter-SemiBold.ttf", 20.0f, -0.2f);
    loadFace(Face::Equation, "Inter-Medium.ttf", 14.5f, 0.0f);
    loadFace(Face::Small, "Inter-SemiBold.ttf", 10.0f, 0.7f);
    loadFace(Face::Caption, "Inter-Medium.ttf", 11.5f, 0.0f);
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

void Ui::textOverScene(Face f, const char* string, float x, float y, Col colour) {
    const Col shade = uiHex(0x05060A, 0.55f * colour.a);
    for (const Vec2 d : {Vec2{-0.8f, 0.0f}, Vec2{0.8f, 0.0f}, Vec2{0.0f, -0.8f}, Vec2{0.0f, 0.8f}}) {
        text(f, string, x + d.x, y + d.y, shade);
    }
    text(f, string, x, y, colour);
}

float Ui::paragraph(Face f, const char* string, float x, float y, float width, Col colour, float leading, bool draw) {
    const TypeFace& t = face(f);
    const float step = std::round(t.size * leading);
    const float space = MeasureTextEx(t.font, " ", t.size, t.spacing).x + t.spacing;
    std::string line;
    float lineWidth = 0.0f;
    float cursor = y;
    const auto flush = [&] {
        const bool visible = !clipping_ || (cursor + step > clip_.y && cursor < clip_.y + clip_.h);
        if (draw && visible && !line.empty()) text(f, line.c_str(), x, cursor, colour);
        cursor += step;
        line.clear();
        lineWidth = 0.0f;
    };
    const char* p = string;
    for (;;) {
        const char* start = p;
        while (*p != '\0' && *p != ' ' && *p != '\n') ++p;
        if (p > start) {
            const std::string word(start, p);
            const float w = MeasureTextEx(t.font, word.c_str(), t.size, t.spacing).x;
            if (!line.empty() && lineWidth + space + w > width) flush();
            if (!line.empty()) {
                line += ' ';
                lineWidth += space;
            }
            line += word;
            lineWidth += w;
        }
        if (*p == '\n') {
            flush();
            ++p;
        } else if (*p == ' ') {
            ++p;
        } else {
            break;
        }
    }
    if (!line.empty()) flush();
    if (cursor <= y) return 0.0f;
    // The last line only needs the type's own height, not a full step below it.
    return cursor - y - (step - t.size * 1.25f);
}

void Ui::beginClip(Box2 box) {
    rlDrawRenderBatchActive();
    rlEnableScissorTest();
    const float framebuffer = static_cast<float>(GetRenderHeight());
    rlScissor(static_cast<int>(std::floor(box.x * dpi_)), static_cast<int>(std::floor(framebuffer - (box.y + box.h) * dpi_)),
              static_cast<int>(std::ceil(box.w * dpi_)), static_cast<int>(std::ceil(box.h * dpi_)));
    clip_ = box;
    clipping_ = true;
}

void Ui::endClip() {
    rlDrawRenderBatchActive();
    rlDisableScissorTest();
    clipping_ = false;
}

bool Ui::hit(Box2 box) const { return box.contains(mouse_) && (!clipping_ || clip_.contains(mouse_)); }

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
        case Icon::Inspect:
            k.ring(c + Vec2{-1.5f, -1.5f}, 5.6f, 1.5f, col, 0.8f);
            k.line(c + Vec2{2.6f, 2.6f}, c + Vec2{7.0f, 7.0f}, 2.2f, col, 0.8f);
            break;
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
        case Icon::Throw: {
            k.disc(c + Vec2{3.0f, -2.5f}, 4.6f, col);
            for (int i = 0; i < 3; ++i) {
                const float y = -1.0f + static_cast<float>(i) * 3.2f;
                k.line(c + Vec2{-8.0f + static_cast<float>(i) * 1.2f, y + 1.8f}, c + Vec2{-2.6f, y - 0.4f}, 1.2f,
                       withAlpha(col, col.a * (0.9f - 0.2f * static_cast<float>(i))), 0.8f);
            }
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
        case Icon::Slow: {
            // An hourglass.
            k.line(c + Vec2{-5.5f, -7.5f}, c + Vec2{5.5f, -7.5f}, 1.4f, col, 0.8f);
            k.line(c + Vec2{-5.5f, 7.5f}, c + Vec2{5.5f, 7.5f}, 1.4f, col, 0.8f);
            k.triangleFilled(c + Vec2{-4.5f, -6.0f}, c + Vec2{4.5f, -6.0f}, c + Vec2{0.0f, 0.0f}, withAlpha(col, col.a * 0.55f));
            k.triangleFilled(c + Vec2{0.0f, 0.0f}, c + Vec2{4.5f, 6.0f}, c + Vec2{-4.5f, 6.0f}, col);
            break;
        }
        case Icon::Vectors: {
            const Vec2 from = c + Vec2{-6.5f, 6.5f};
            const Vec2 to = c + Vec2{6.0f, -6.0f};
            k.line(from, to + Vec2{-2.8f, 2.8f}, 1.6f, col, 0.8f);
            k.triangleFilled(to, to + Vec2{-7.0f, 1.2f}, to + Vec2{-1.2f, 7.0f}, col);
            k.disc(from, 2.0f, col);
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
    const bool hovered = hit(box);
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
    const bool hovered = hit(box);
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
    const float vw = textWidth(Face::Label, valueText);
    text(Face::Label, valueText, box.x + box.w - vw, box.y, kInk);

    const float trackY = box.y + 26.0f;
    const bool hovered = hit({box.x - 4.0f, trackY - 9.0f, box.w + 8.0f, 18.0f});
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
    const bool hovered = hit(box);
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

bool Ui::tab(const void* id, Box2 box, const char* label, bool selected) {
    const bool hovered = hit(box);
    const float hover = hoverAmount(id, hovered);
    if (hovered && pressed_) active_ = id;
    const bool clicked = hovered && released_ && active_ == id;

    canvas_->begin();
    if (selected) {
        canvas_->roundedRect(box.x, box.y, box.w, box.h, 8.0f, withAlpha(accent_, 0.2f));
        canvas_->roundedRectStroke(box.x + 0.5f, box.y + 0.5f, box.w - 1.0f, box.h - 1.0f, 7.5f, 0.8f, withAlpha(accent_, 0.45f));
    } else if (hover > 0.01f) {
        canvas_->roundedRect(box.x, box.y, box.w, box.h, 8.0f, uiHex(0xFFFFFF, 0.07f * hover));
    }
    canvas_->end();
    const Face f = selected ? Face::Strong : Face::Label;
    const float w = textWidth(f, label);
    text(f, label, box.x + (box.w - w) * 0.5f, box.y + (box.h - lineHeight(f)) * 0.5f + 0.5f,
         selected ? kInk : withAlpha(kInk, 0.62f + 0.3f * hover));
    return clicked;
}

bool Ui::link(const void* id, float x, float y, const char* label) {
    const float w = textWidth(Face::Caption, label);
    const Box2 box{x - 3.0f, y - 2.0f, w + 6.0f, lineHeight(Face::Caption) + 4.0f};
    const bool hovered = hit(box);
    const float hover = hoverAmount(id, hovered);
    if (hovered && pressed_) active_ = id;
    const bool clicked = hovered && released_ && active_ == id;
    const Col colour = mix(accent_, kInk, 0.35f + 0.4f * hover);
    text(Face::Caption, label, x, y, colour);
    if (hover > 0.01f) {
        canvas_->begin();
        canvas_->line({x, y + lineHeight(Face::Caption) - 0.5f}, {x + w, y + lineHeight(Face::Caption) - 0.5f}, 0.6f,
                      withAlpha(colour, hover * colour.a), 0.6f);
        canvas_->end();
    }
    return clicked;
}

void Ui::divider(float x, float y, float height) {
    canvas_->begin();
    canvas_->line({x, y}, {x, y + height}, 0.5f, uiHex(0xFFFFFF, 0.16f), 0.6f);
    canvas_->end();
}

}  // namespace gfx
