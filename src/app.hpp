// Ties it together: input, the fixed-step loop, the render passes and the lesson interface.
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
    bool experimentTab = false;  // open the lesson on its Experiment tab
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
    enum class Drag { None, Body, Pan, Aim };

    void openWindow();
    void followCanvas();
    void loadFonts();
    gfx::Vec2 screenPoints() const;
    void loadScene(int index);
    void reset();
    void changeScene(int index);
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
    void throwBody(gfx::Vec2 at, gfx::Vec2 velocity);
    float fitScale() const;
    float stageLeft() const;
    float stageRight() const;
    void resizeTargets();
    void adaptResolution(float dt);
    const phys::Body* selectedBody() const;

    // The interface, drawn in UI points over the finished scene.
    void drawHud();
    void drawCurriculum(float screenH);
    void drawLesson(float x, float y, float w, float h);
    float drawLearn(float x, float y, float w);
    float drawExperiment(float x, float y, float w);
    float plotHeight() const;
    float instrumentsHeight() const;
    void drawInstruments(float x, float y, float w, float h);
    void drawPlot(gfx::Box2 box);
    void drawSceneLabels();
    void drawInspector(float x, float y);
    void drawDock(float screenW, float screenH);
    void drawSettings(gfx::Box2 anchor);
    float sectionHeading(const char* label, float x, float y);
    gfx::Vec2 toScreen(gfx::Vec2 world) const;

    Options options_;
    bool capture_ = false;
    int frameIndex_ = 0;

    scene::Scene scene_;
    int sceneIndex_ = 0;
    int pendingScene_ = -1;
    float fade_ = 0.0f;
    bool fadingOut_ = false;

    // What the learner has set each lesson's controls to, so they survive restarts.
    std::vector<std::vector<float>> lessonValues_;
    bool paramsDirty_ = false;
    int lessonTab_ = 0;  // 0 Learn, 1 Experiment
    float lessonScroll_ = 0.0f;
    float lessonContent_ = 0.0f;
    gfx::Box2 lessonScrollBox_;

    gfx::Pipeline pipeline_;
    gfx::Canvas canvas_;
    gfx::Painter painter_;
    gfx::Effects effects_;
    gfx::Trails trails_;
    gfx::Ui ui_;
    gfx::View view_;
    float pointScale_ = 1.0f;   // raylib screen units per UI point; only differs from 1 on the web
    float dpi_ = 1.0f;          // window pixels per UI point
    float fontDpi_ = 0.0f;      // the scale the fonts are baked at
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
    bool sparksOn_ = false;
    bool bloomOn_ = true;
    bool debugOn_ = false;
    bool heatView_ = false;
    bool vectorsOn_ = true;
    bool slowMotion_ = false;
    float timeScale_ = 1.0f;
    float gravity_ = 10.0f;  // m/s², for lessons that have gravity
    float substepSetting_ = 6.0f;

    scene::Tool tool_ = scene::Tool::Grab;
    Drag drag_ = Drag::None;
    int mouseJoint_ = -1;
    int grabbedBody_ = -1;
    int selected_ = -1;
    uint32_t selectedSerial_ = 0;
    gfx::Vec2 mouseWorld_;
    gfx::Vec2 aimStart_;
    gfx::Vec2 panStartWorld_;

    gfx::Vec2 cameraPan_;
    float userZoom_ = 1.0f;
    gfx::Vec2 cameraFollow_;
    float insetAnim_ = 1.0f;
    float plotLo_ = 0.0f;  // the plot's vertical range, eased so it doesn't jump
    float plotHi_ = 1.0f;
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
    float statPhysicsMs_ = 0.0f;
    float statTimer_ = 0.0f;
    int statFrames_ = 0;
    double benchPhysics_ = 0.0;
#if !defined(__EMSCRIPTEN__)
    double benchRender_ = 0.0;
    double benchWorst_ = 0.0;
    int benchFrames_ = 0;
#endif
};
