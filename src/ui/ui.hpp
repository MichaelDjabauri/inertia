// A small immediate-mode UI: frosted panels, wrapped text, tabs, icon buttons, sliders and toggles.
#pragma once

#include <unordered_map>
#include <vector>

#include "raylib.h"
#include "render/canvas.hpp"
#include "render/pipeline.hpp"

namespace gfx {

enum class Icon { Inspect, Grab, Throw, Play, Pause, Step, Reset, Slow, Vectors, Settings };

enum class Face { Body, Label, Strong, Title, Mono, Heading, Equation, Small, Caption };

// One frame of pointer state, from the real mouse or from a test script.
struct Pointer {
    Vec2 position;
    float wheel = 0.0f;
    bool leftPressed = false;
    bool leftDown = false;
    bool leftReleased = false;
    bool rightPressed = false;
    bool rightDown = false;
    bool middlePressed = false;
    bool middleDown = false;
};

struct Box2 {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool contains(Vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
};

class Ui {
public:
    void load(const char* fontDir, float dpi);
    void unload();

    // `screen` is the window size in UI points.
    void beginFrame(Pipeline& pipeline, Canvas& canvas, float dt, float dpi, Vec2 screen, Col accent,
                    const Pointer& pointer);

    // True if the pointer is over any panel from the last frame, so the world should ignore it.
    bool wantsMouse(Vec2 mouse) const;

    void panel(Box2 box, float radius);
    void text(Face face, const char* string, float x, float y, Col colour);
    // Text with a soft dark outline, for labels drawn straight over the scene.
    void textOverScene(Face face, const char* string, float x, float y, Col colour);
    float textWidth(Face face, const char* string) const;
    float lineHeight(Face face) const;

    // Word-wrapped text; a newline starts a new line. Returns the height used. With `draw`
    // false it only measures.
    float paragraph(Face face, const char* string, float x, float y, float width, Col colour,
                    float leading = 1.5f, bool draw = true);

    // Everything drawn between these is cut off outside `box`, and doesn't respond to the
    // pointer there either.
    void beginClip(Box2 box);
    void endClip();

    bool iconButton(const void* id, Box2 box, Icon icon, bool active, const char* tooltip);
    bool listItem(const void* id, Box2 box, const char* label, const char* key, bool selected);
    bool slider(const void* id, Box2 box, const char* label, const char* valueText, float& value, float lo, float hi);
    bool toggle(const void* id, Box2 box, const char* label, const char* key, bool& on);
    bool tab(const void* id, Box2 box, const char* label, bool selected);
    // A small text button. Returns true when clicked.
    bool link(const void* id, float x, float y, const char* label);
    void divider(float x, float y, float height);

    void endFrame();

private:
    struct TypeFace {
        Font font{};
        float size = 13.0f;
        float spacing = 0.0f;
    };

    float hoverAmount(const void* id, bool hovered);
    bool hit(Box2 box) const;
    void drawIcon(Icon icon, Vec2 c, const Col& colour);
    const TypeFace& face(Face f) const { return faces_[static_cast<int>(f)]; }

    Pipeline* pipeline_ = nullptr;
    Canvas* canvas_ = nullptr;
    float dt_ = 0.0f;
    float dpi_ = 1.0f;
    Vec2 screen_;
    Col accent_;
    Vec2 mouse_;
    bool pressed_ = false;
    bool down_ = false;
    bool released_ = false;
    const void* active_ = nullptr;

    TypeFace faces_[9];
    Box2 clip_;
    bool clipping_ = false;
    std::unordered_map<const void*, float> hover_;
    std::unordered_map<const void*, float> slide_;
    std::vector<Box2> panels_;
    std::vector<Box2> panelsThisFrame_;

    const char* tooltip_ = nullptr;
    Vec2 tooltipAnchor_;
};

}  // namespace gfx
