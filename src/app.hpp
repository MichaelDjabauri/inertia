// Ties it together: input, the fixed-step loop, the render passes and the HUD.
#pragma once

#include <string>
#include <vector>

#include "render/canvas.hpp"
#include "render/effects.hpp"
#include "render/painter.hpp"
#include "render/pipeline.hpp"
#include "scenes/scene.hpp"
#include "ui/ui.hpp"

struct Options {
    int scene = 0;
    int width = 0;
    int height = 0;
    int frames = 0;           // when set, run this many fixed frames and exit
    std::string shotPath;     // screenshot taken on the last frame
    bool hideUi = false;
    bool showSettings = false;
    std::string toolName;
    bool benchmark = false;
    float renderScale = 0.0f;  // scene pixels per window pixel; 0 means automatic
    std::string script;        // scripted input for captures, see parseScript in app.cpp
    bool report = false;       // print the final state on exit
};

class App {
public:
    explicit App(const Options& options);
    ~App();

    int run();

private:
    enum class Drag { None, Body, Pan, Aim, Pull };

    void openWindow();
    void followCanvas();
    gfx::Vec2 screenPoints() const;
    void loadScene(int index);
    void reset();
    void frame();
    void parseScript(const std::string& text);
    void readScript();
    bool keyHit(int key) const;
    void readPointer();
    void handleKeys();
    void handlePointer();
    void applyGravity();
    void stepPhysics(float dt);
    void pollImpacts();
    void updateCamera(float dt);
    void render();
    void drawOverlay();
    void drawHud();
    void spawnFromTool(gfx::Vec2 at, gfx::Vec2 velocity);
    float fitScale() const;
    float stageInset() const;
    void resizeTargets();
    void adaptResolution(float dt);

    Options options_;
    bool capture_ = false;
    int frameIndex_ = 0;

    scene::Scene scene_;
    int sceneIndex_ = 0;
    int pendingScene_ = -1;
    float fade_ = 0.0f;
    bool fadingOut_ = false;

    gfx::Pipeline pipeline_;
    gfx::Canvas canvas_;
    gfx::Painter painter_;
    gfx::Effects effects_;
    gfx::Trails trails_;
    gfx::Ui ui_;
    gfx::View view_;
    float pointScale_ = 1.0f;   // raylib screen units per UI point; only differs from 1 on the web
    float dpi_ = 1.0f;          // window pixels per UI point
    float renderScale_ = 1.0f;  // scene pixels per window pixel
    float sceneDpi_ = 1.0f;     // scene pixels per UI point
    bool autoResolution_ = true;
    float slowTime_ = 0.0f;
    float slowFrames_ = 0.0f;

    float accumulator_ = 0.0f;
    float renderAlpha_ = 1.0f;
    float wallTime_ = 0.0f;
    float frameDt_ = 1.0f / 60.0f;

    bool paused_ = false;
    bool stepOnce_ = false;
    bool showUi_ = true;
    bool showSettings_ = false;
    bool trailsOn_ = true;
    bool sparksOn_ = true;
    bool bloomOn_ = true;
    bool debugOn_ = false;
    bool heatView_ = false;
    float timeScale_ = 1.0f;
    float gravityScale_ = 1.0f;
    float bounce_ = 0.0f;
    float substepSetting_ = 6.0f;

    scene::Tool tool_ = scene::Tool::Grab;
    Drag drag_ = Drag::None;
    int mouseJoint_ = -1;
    int grabbedBody_ = -1;
    gfx::Vec2 mouseWorld_;
    gfx::Vec2 aimStart_;
    gfx::Vec2 panStartWorld_;
    float pullPulse_ = 0.0f;
    float lastRingTime_ = -1.0f;

    gfx::Vec2 cameraPan_;
    float userZoom_ = 1.0f;
    gfx::Vec2 cameraFollow_;
    float insetAnim_ = 1.0f;
    scene::Input input_;
    gfx::Pointer pointer_;

    struct ScriptEvent {
        int frame = 0;
        std::string action;
        float x = 0.0f;
        float y = 0.0f;
        int key = 0;
    };
    std::vector<ScriptEvent> script_;
    std::vector<int> scriptKeys_;

    unsigned int spawnSeed_ = 12345;
    float statFps_ = 60.0f;
    float statFrameMs_ = 16.0f;
    float statPhysicsMs_ = 0.0f;
    float statTimer_ = 0.0f;
    double benchPhysics_ = 0.0;
    double benchRender_ = 0.0;
    double benchWorst_ = 0.0;
    int benchFrames_ = 0;
};
