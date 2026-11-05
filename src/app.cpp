#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "raylib.h"
#include "rlgl.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#else
#include <unistd.h>

#include <csignal>
#endif

using gfx::Box2;
using gfx::Col;
using gfx::Face;
using gfx::Icon;
using gfx::Vec2;
using phys::Body;
using phys::BodyDef;
using scene::Tool;

namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr int kMaxStepsPerFrame = 3;
constexpr float kPanelWidth = 190.0f;
constexpr float kRowHeight = 27.0f;
constexpr int kMinWidth = 800;  // the smallest window the interface is laid out for, in UI points
constexpr int kMinHeight = 560;

const char* const kSceneKeys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="};
const int kSceneKeyCodes[12] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX,
                                KEY_SEVEN, KEY_EIGHT, KEY_NINE, KEY_ZERO, KEY_MINUS, KEY_EQUAL};

float randomUnit(unsigned int& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>((seed >> 8) & 0xFFFFFF) / 16777216.0f;
}

double secondsNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#if defined(__EMSCRIPTEN__)

struct CanvasSize {
    int width = 0;  // device pixels
    int height = 0;
    float pointScale = 1.0f;
};

// The page's CSS decides how big the canvas looks. Its pixel buffer has to follow by hand,
// because raylib's own web resizing ignores high-density screens and would blur the image.
CanvasSize measureCanvas() {
    double cssWidth = 0.0;
    double cssHeight = 0.0;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    const double ratio = emscripten_get_device_pixel_ratio();
    // Below the interface's minimum size, shrink it rather than let it overlap.
    const double fit = std::min({1.0, cssWidth / kMinWidth, cssHeight / kMinHeight});
    return {std::max(static_cast<int>(std::lround(cssWidth * ratio)), 1),
            std::max(static_cast<int>(std::lround(cssHeight * ratio)), 1),
            std::max(static_cast<float>(ratio * fit), 0.1f)};
}

void toggleFullscreen() {
    // The whole page goes full screen, so followCanvas sees the new size like any other resize.
    EM_ASM({
        const page = document.documentElement;
        if (document.fullscreenElement) document.exitFullscreen();
        else if (page.requestFullscreen) page.requestFullscreen();
    });
}

#else

void toggleFullscreen() { ToggleBorderlessWindowed(); }

// raylib 5.5's InitWindow ignores a failed window creation and goes on to call null OpenGL
// functions. That can happen for a moment while a display wakes or reconfigures, so during
// startup a segfault means no window. Only write and _exit are safe inside a signal handler.
void onWindowFailure(int) {
    static const char message[] = "inertia: could not open an OpenGL window. Is the display awake?\n";
    (void)!write(STDERR_FILENO, message, sizeof message - 1);
    _exit(1);
}

#endif

}  // namespace

App::App(const Options& options) : options_(options) {
    capture_ = options.frames > 0;
    showUi_ = !options.hideUi;
    showSettings_ = options.showSettings;

    SetTraceLogLevel(LOG_ERROR);
    openWindow();
    SetExitKey(KEY_NULL);

    dpi_ = GetWindowScaleDPI().x * pointScale_;
    if (options.renderScale > 0.0f) {
        renderScale_ = options.renderScale;
        autoResolution_ = false;
    }
    sceneDpi_ = dpi_ * renderScale_;
    pipeline_.load(static_cast<int>(static_cast<float>(GetRenderWidth()) * renderScale_),
                   static_cast<int>(static_cast<float>(GetRenderHeight()) * renderScale_));
    const std::string fonts = std::string(GetApplicationDirectory()) + "assets/fonts";
    ui_.load(fonts.c_str(), dpi_);

    sceneIndex_ = std::clamp(options.scene, 0, static_cast<int>(scene::sceneList().size()) - 1);
    loadScene(sceneIndex_);
    fade_ = capture_ ? 1.0f : 0.0f;

    parseScript(options.script);

    if (!options.toolName.empty()) {
        const std::string& t = options.toolName;
        if (t == "ball") tool_ = Tool::Ball;
        if (t == "box") tool_ = Tool::Box;
        if (t == "gem") tool_ = Tool::Gem;
        if (t == "bomb") tool_ = Tool::Bomb;
        if (t == "pull") tool_ = Tool::Pull;
    }
}

App::~App() {
    ui_.unload();
    pipeline_.unload();
    CloseWindow();
}

#if defined(__EMSCRIPTEN__)

void App::openWindow() {
    const CanvasSize canvas = measureCanvas();
    pointScale_ = canvas.pointScale;
    // No resize or high-DPI flags: followCanvas does that job, in device pixels.
    InitWindow(canvas.width, canvas.height, "Inertia");

    // WebGL keeps drawing into float textures switched off until it's asked for.
    const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_get_current_context();
    emscripten_webgl_enable_extension(context, "EXT_color_buffer_float");
    emscripten_webgl_enable_extension(context, "EXT_color_buffer_half_float");
}

// Polled every frame, because browser zoom can change the scale without changing the pixels.
void App::followCanvas() {
    const CanvasSize canvas = measureCanvas();
    const bool rescaled = canvas.pointScale != pointScale_;
    pointScale_ = canvas.pointScale;
    if (canvas.width != GetRenderWidth() || canvas.height != GetRenderHeight()) {
        SetWindowSize(canvas.width, canvas.height);
    } else if (rescaled) {
        resizeTargets();
    }
}

#else

void App::openWindow() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | (capture_ ? 0 : FLAG_VSYNC_HINT));
    const auto previousHandler = std::signal(SIGSEGV, onWindowFailure);
    InitWindow(options_.width > 0 ? options_.width : 1280, options_.height > 0 ? options_.height : 800, "Inertia");
    std::signal(SIGSEGV, previousHandler);
    if (!IsWindowReady()) onWindowFailure(0);

    if (options_.width == 0) {
        // Fit comfortably on whatever display this opened on.
        const int monitor = GetCurrentMonitor();
        const int w = std::min(1440, static_cast<int>(static_cast<float>(GetMonitorWidth(monitor)) * 0.86f));
        const int h = std::min(900, static_cast<int>(static_cast<float>(GetMonitorHeight(monitor)) * 0.86f));
        SetWindowSize(w, h);
        SetWindowPosition((GetMonitorWidth(monitor) - w) / 2, (GetMonitorHeight(monitor) - h) / 2);
    }
    SetWindowMinSize(kMinWidth, kMinHeight);
}

void App::followCanvas() {}

#endif

Vec2 App::screenPoints() const {
    return {static_cast<float>(GetScreenWidth()) / pointScale_, static_cast<float>(GetScreenHeight()) / pointScale_};
}

// Scripted input lets captures exercise the real input paths. Events are separated by
// semicolons, in the form frame:action, with x,y in UI points where one is needed:
//   60:press:400,300   80:move:500,200   90:release   95:rpress:300,300   96:rrelease
//   100:wheel:-3       120:key:R          130:scale:0.6
void App::parseScript(const std::string& text) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        const std::string item = text.substr(start, end - start);
        start = end + 1;

        const size_t first = item.find(':');
        if (first == std::string::npos) continue;
        ScriptEvent event;
        event.frame = std::atoi(item.c_str());
        const size_t second = item.find(':', first + 1);
        event.action = item.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1);
        if (second != std::string::npos) {
            const std::string args = item.substr(second + 1);
            if (event.action == "key") {
                event.key = args == "SPACE" ? KEY_SPACE : args == "TAB" ? KEY_TAB : static_cast<int>(args[0]);
            } else {
                std::sscanf(args.c_str(), "%f,%f", &event.x, &event.y);
            }
        }
        script_.push_back(event);
    }
}

void App::readScript() {
    pointer_.leftPressed = false;
    pointer_.leftReleased = false;
    pointer_.rightPressed = false;
    pointer_.middlePressed = false;
    pointer_.wheel = 0.0f;
    scriptKeys_.clear();

    for (const ScriptEvent& e : script_) {
        if (e.frame != frameIndex_) continue;
        if (e.action == "press") {
            pointer_.position = {e.x, e.y};
            pointer_.leftPressed = true;
            pointer_.leftDown = true;
        } else if (e.action == "release") {
            pointer_.leftReleased = true;
            pointer_.leftDown = false;
        } else if (e.action == "move") {
            pointer_.position = {e.x, e.y};
        } else if (e.action == "rpress") {
            pointer_.position = {e.x, e.y};
            pointer_.rightPressed = true;
            pointer_.rightDown = true;
        } else if (e.action == "rrelease") {
            pointer_.rightDown = false;
        } else if (e.action == "wheel") {
            pointer_.wheel = e.x;
        } else if (e.action == "scale") {
            // The same change that adaptResolution makes when frames run long.
            renderScale_ = e.x;
            resizeTargets();
        } else if (e.action == "key") {
            scriptKeys_.push_back(e.key);
        }
    }
}

bool App::keyHit(int key) const {
    if (capture_) return std::find(scriptKeys_.begin(), scriptKeys_.end(), key) != scriptKeys_.end();
    return IsKeyPressed(key);
}

void App::readPointer() {
    if (capture_) {
        // A capture has to be repeatable, so a stray scroll over the window must not reach it.
        readScript();
        return;
    }
    const Vector2 mouse = GetMousePosition();
    pointer_.position = {mouse.x / pointScale_, mouse.y / pointScale_};
    pointer_.wheel = GetMouseWheelMove();
    pointer_.leftPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    pointer_.leftDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    pointer_.leftReleased = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    pointer_.rightPressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    pointer_.rightDown = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    pointer_.middlePressed = IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE);
    pointer_.middleDown = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
}

void App::loadScene(int index) {
    sceneIndex_ = index;
    scene::loadScene(scene_, index);
    effects_.clear();
    trails_.configure(scene_.style.trailLength, scene_.style.trailMinSpeed);
    tool_ = scene_.startTool;
    drag_ = Drag::None;
    mouseJoint_ = -1;
    grabbedBody_ = -1;
    accumulator_ = 0.0f;
    renderAlpha_ = 1.0f;
    cameraPan_ = {};
    userZoom_ = 1.0f;
    input_ = {};
    lastRingTime_ = -1.0f;
    substepSetting_ = static_cast<float>(scene_.world.substeps);
    applyGravity();
    scene_.world.minRestitution = bounce_;

    // Snap the camera, so a new scene doesn't begin with a long pan.
    view_.width = static_cast<float>(pipeline_.width());
    view_.height = static_cast<float>(pipeline_.height());
    insetAnim_ = showUi_ ? 1.0f : 0.0f;
    view_.originX = (view_.width + stageInset()) * 0.5f;
    view_.pixelsPerMetre = fitScale();
    cameraFollow_ = scene_.camera;
    if (scene_.follow >= 0) cameraFollow_ = scene_.world.bodies()[scene_.follow].pos + scene_.followLead;
    view_.centre = cameraFollow_;
}

void App::reset() {
    pendingScene_ = sceneIndex_;
    fadingOut_ = true;
}

void App::applyGravity() {
    scene_.world.gravity = scene_.baseGravity * gravityScale_;
    scene_.world.wakeAll();
}

float App::stageInset() const { return kPanelWidth * sceneDpi_ * insetAnim_; }

void App::resizeTargets() {
    dpi_ = GetWindowScaleDPI().x * pointScale_;
    sceneDpi_ = dpi_ * renderScale_;
    pipeline_.resize(static_cast<int>(static_cast<float>(GetRenderWidth()) * renderScale_),
                     static_cast<int>(static_cast<float>(GetRenderHeight()) * renderScale_));
}

// A Retina panel on integrated graphics can't always fill every pixel with HDR bloom.
// When frames run long, trade a little sharpness for smooth motion.
void App::adaptResolution(float dt) {
    if (!autoResolution_ || capture_ || wallTime_ < 2.0f) return;
    slowTime_ += dt;
    slowFrames_ += 1.0f;
    if (slowTime_ < 1.5f) return;
    const float averageMs = 1000.0f * slowTime_ / slowFrames_;
    slowTime_ = 0.0f;
    slowFrames_ = 0.0f;
    if (averageMs > 21.0f && renderScale_ > 0.5f) {
        renderScale_ = std::max(renderScale_ * 0.8f, 0.5f);
        resizeTargets();
    }
}

float App::fitScale() const {
    float scale = view_.height / scene_.viewHeight;
    if (scene_.viewWidth > 0.0f) scale = std::min(scale, (view_.width - stageInset()) / scene_.viewWidth);
    return scale;
}

int App::run() {
#if defined(__EMSCRIPTEN__)
    // The browser owns the loop and calls back once per display refresh. Passing true leaves
    // main() suspended rather than returned, so the App on its stack stays alive.
    emscripten_set_main_loop_arg([](void* app) { static_cast<App*>(app)->frame(); }, this, 0, true);
    return 0;
#else
    while (!WindowShouldClose()) {
        const double start = secondsNow();
        frame();
        const double elapsed = secondsNow() - start;
        if (frameIndex_ > 30) {
            benchRender_ += elapsed;
            benchWorst_ = std::max(benchWorst_, elapsed);
            ++benchFrames_;
        }
        ++frameIndex_;
        if (capture_ && frameIndex_ >= options_.frames) break;
    }
    if (options_.benchmark && benchFrames_ > 0) {
        std::printf("%-18s %5d bodies  %6.2f ms/frame avg  %6.2f worst  (physics %.2f)\n",
                    scene::sceneList()[sceneIndex_].name, scene_.world.stats().bodies,
                    1000.0 * benchRender_ / benchFrames_, 1000.0 * benchWorst_,
                    1000.0 * benchPhysics_ / benchFrames_);
    }
    if (options_.report) {
        const phys::WorldStats& s = scene_.world.stats();
        std::printf("scene=%s tool=%d bodies=%d joints=%d paused=%d settings=%d gravity=%.2f time=%.2f ppm=%.1f ui=%d heat=%d\n",
                    scene::sceneList()[sceneIndex_].name, static_cast<int>(tool_), s.bodies, s.joints, paused_ ? 1 : 0,
                    showSettings_ ? 1 : 0, static_cast<double>(gravityScale_), static_cast<double>(timeScale_),
                    static_cast<double>(view_.pixelsPerMetre), showUi_ ? 1 : 0, heatView_ ? 1 : 0);
    }
    return 0;
#endif
}

void App::frame() {
    const float dt = capture_ ? kStep : std::min(GetFrameTime(), 0.05f);
    frameDt_ = dt;
    wallTime_ += dt;

    // Transition: fade down, swap the scene while it's dark, fade back up.
    if (fadingOut_) {
        fade_ -= dt / 0.11f;
        if (fade_ <= 0.0f) {
            fade_ = 0.0f;
            fadingOut_ = false;
            if (pendingScene_ >= 0) loadScene(pendingScene_);
            pendingScene_ = -1;
        }
    } else if (fade_ < 1.0f) {
        fade_ = std::min(fade_ + dt / 0.28f, 1.0f);
    }

    followCanvas();
    if (IsWindowResized()) resizeTargets();
    adaptResolution(dt);

    view_.width = static_cast<float>(pipeline_.width());
    view_.height = static_cast<float>(pipeline_.height());

    readPointer();
    handleKeys();
    handlePointer();

    const double physicsStart = secondsNow();
    if (!paused_ || stepOnce_) {
        accumulator_ += stepOnce_ && paused_ ? kStep : dt * timeScale_;
        int steps = 0;
        while (accumulator_ >= kStep && steps < kMaxStepsPerFrame) {
            stepPhysics(kStep);
            accumulator_ -= kStep;
            ++steps;
        }
        // If the machine can't keep up, slow down rather than spiral.
        if (steps == kMaxStepsPerFrame) accumulator_ = 0.0f;
        renderAlpha_ = accumulator_ / kStep;
        effects_.update(dt * timeScale_, scene_.world.gravity);
    }
    stepOnce_ = false;
    benchPhysics_ += frameIndex_ > 30 ? secondsNow() - physicsStart : 0.0;

    for (Body& b : scene_.world.bodies()) {
        if (b.flash > 0.0f) b.flash *= std::exp(-dt * 7.0f);
    }

    updateCamera(dt);
    render();

    statTimer_ += dt;
    if (statTimer_ > 0.3f) {
        statTimer_ = 0.0f;
        statFps_ = static_cast<float>(GetFPS());
        statPhysicsMs_ = scene_.world.stats().collideMs + scene_.world.stats().solveMs;
    }
}

void App::handleKeys() {
    if (keyHit(KEY_SPACE)) paused_ = !paused_;
    if (keyHit(KEY_PERIOD)) {
        paused_ = true;
        stepOnce_ = true;
    }
    if (keyHit(KEY_R)) reset();
    if (keyHit(KEY_H)) showUi_ = !showUi_;
    if (keyHit(KEY_TAB)) showSettings_ = !showSettings_;
    if (keyHit(KEY_T)) trailsOn_ = !trailsOn_;
    if (keyHit(KEY_B)) bloomOn_ = !bloomOn_;
    if (keyHit(KEY_C)) debugOn_ = !debugOn_;
    if (keyHit(KEY_V)) heatView_ = !heatView_;
    if (keyHit(KEY_F)) toggleFullscreen();
    if (keyHit(KEY_Q)) tool_ = Tool::Grab;

    const int count = static_cast<int>(scene::sceneList().size());
    for (int i = 0; i < count; ++i) {
        if (keyHit(kSceneKeyCodes[i]) && i != sceneIndex_) {
            pendingScene_ = i;
            fadingOut_ = true;
        }
    }
    if (keyHit(KEY_RIGHT_BRACKET)) {
        pendingScene_ = (sceneIndex_ + 1) % count;
        fadingOut_ = true;
    }
    if (keyHit(KEY_LEFT_BRACKET)) {
        pendingScene_ = (sceneIndex_ + count - 1) % count;
        fadingOut_ = true;
    }

    const bool left = !capture_ && (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A));
    const bool right = !capture_ && (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D));
    input_.drive = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
    if (left || right) input_.driving = true;
}

void App::handlePointer() {
    const Vec2 pixels = pointer_.position * sceneDpi_;
    mouseWorld_ = view_.toWorld(pixels);
    phys::World& world = scene_.world;

    const bool overUi = showUi_ && ui_.wantsMouse(pointer_.position);

    const float wheel = pointer_.wheel;
    if (wheel != 0.0f && !overUi) {
        const Vec2 before = view_.toWorld(pixels);
        userZoom_ = std::clamp(userZoom_ * std::exp(wheel * 0.09f), 0.25f, 6.0f);
        view_.pixelsPerMetre = fitScale() * userZoom_;
        const Vec2 after = view_.toWorld(pixels);
        cameraPan_ += before - after;
        view_.centre += before - after;
    }

    if (pointer_.leftPressed && !overUi) {
        switch (tool_) {
            case Tool::Grab: {
                const int id = world.bodyAt(mouseWorld_);
                if (id >= 0 && world.bodies()[id].isDynamic()) {
                    grabbedBody_ = id;
                    mouseJoint_ = world.createMouse(id, mouseWorld_, 1400.0f * world.bodies()[id].mass, 7.0f, 1.0f);
                    drag_ = Drag::Body;
                } else {
                    drag_ = Drag::Pan;
                    panStartWorld_ = mouseWorld_;
                }
                break;
            }
            case Tool::Ball:
            case Tool::Box:
            case Tool::Gem:
                drag_ = Drag::Aim;
                aimStart_ = mouseWorld_;
                break;
            case Tool::Bomb:
                world.explode(mouseWorld_, 11.0f, 34.0f);
                effects_.shockwave(mouseWorld_, 11.0f, gfx::linearHex(0xFFD9A8), 0.55f);
                if (sparksOn_) effects_.sparks(mouseWorld_, {0.0f, 1.0f}, 34.0f, gfx::linearHex(0xFFC27A), 90);
                break;
            case Tool::Pull:
                drag_ = Drag::Pull;
                break;
        }
    }

    if (pointer_.rightPressed && !overUi) {
        const int id = world.bodyAt(mouseWorld_);
        if (id >= 0 && world.bodies()[id].isDynamic()) {
            if (id == grabbedBody_) {
                drag_ = Drag::None;
                mouseJoint_ = -1;
                grabbedBody_ = -1;
            }
            const Body& b = world.bodies()[id];
            const Col colour = gfx::linearHex(b.color);
            effects_.shockwave(b.pos, b.shape.boundRadius * 2.4f, colour, 0.35f);
            if (sparksOn_) effects_.sparks(b.pos, {0.0f, 1.0f}, 14.0f, colour, 26);
            world.destroyBody(id);
        } else {
            drag_ = Drag::Pan;
            panStartWorld_ = mouseWorld_;
        }
    }
    if (pointer_.middlePressed) {
        drag_ = Drag::Pan;
        panStartWorld_ = mouseWorld_;
    }

    if (drag_ == Drag::Pan) {
        const Vec2 delta = panStartWorld_ - mouseWorld_;
        cameraPan_ += delta;
        view_.centre += delta;
    }

    const bool anyDown = pointer_.leftDown || pointer_.rightDown || pointer_.middleDown;
    if (!anyDown && drag_ != Drag::None) {
        if (drag_ == Drag::Body) {
            world.destroyJoint(mouseJoint_);
            mouseJoint_ = -1;
            grabbedBody_ = -1;
        } else if (drag_ == Drag::Aim) {
            // A click drops the body; a drag throws it towards the cursor.
            spawnFromTool(aimStart_, (mouseWorld_ - aimStart_) * 2.6f);
        }
        drag_ = Drag::None;
    }
}

void App::spawnFromTool(Vec2 at, Vec2 velocity) {
    const float size = 0.42f + randomUnit(spawnSeed_) * 0.42f;
    BodyDef d;
    d.pos = at;
    d.vel = velocity;
    d.angle = randomUnit(spawnSeed_) * 6.28f;
    d.density = 1.4f;
    d.restitution = 0.25f;
    d.color = scene::sampleRamp(scene_.style.ramp, randomUnit(spawnSeed_));
    d.trail = phys::length(velocity) > 8.0f;

    switch (tool_) {
        case Tool::Box: d.shape = phys::makeBox(size, size * 0.8f); break;
        case Tool::Gem: d.shape = phys::makeRegularPolygon(5 + static_cast<int>(randomUnit(spawnSeed_) * 3.5f), size * 1.2f); break;
        default: d.shape = phys::makeCircle(size); break;
    }
    const int id = scene_.world.createBody(d);
    effects_.shockwave(at, size * 3.0f, gfx::linearHex(d.color), 0.3f);
    scene_.world.bodies()[id].flash = 0.7f;
}

void App::stepPhysics(float dt) {
    phys::World& world = scene_.world;

    if (scene_.update) scene_.update(scene_, input_, dt);

    if (mouseJoint_ >= 0) world.joints()[mouseJoint_].target = mouseWorld_;

    if (drag_ == Drag::Pull) {
        constexpr float kRadius = 16.0f;
        constexpr float kStrength = 90.0f;
        for (int i = 0; i < static_cast<int>(world.bodies().size()); ++i) {
            Body& b = world.bodies()[i];
            if (!b.alive || !b.isDynamic()) continue;
            const Vec2 d = mouseWorld_ - b.pos;
            const float dist = phys::length(d);
            if (dist > kRadius || dist < 0.05f) continue;
            const float falloff = 1.0f - dist / kRadius;
            world.wake(i);
            b.vel += d * (kStrength * falloff * dt / dist);
            b.vel *= 1.0f - 1.6f * falloff * dt;
        }
    }

    world.step(dt);
    scene_.time += dt;

    pollImpacts();
    trails_.sample(world);

    // Anything thrown off the edge of the world goes away.
    for (int i = 0; i < static_cast<int>(world.bodies().size()); ++i) {
        const Body& b = world.bodies()[i];
        if (!b.alive || !b.isDynamic()) continue;
        if (b.pos.y < scene_.killY || std::abs(b.pos.y) > 2000.0f) {
            if (i == grabbedBody_) continue;
            world.destroyBody(i);
        }
    }
}

void App::pollImpacts() {
    phys::World& world = scene_.world;
    int budget = 36;
    for (const phys::Impact& hit : world.impacts()) {
        Body& a = world.bodies()[hit.a];
        Body& b = world.bodies()[hit.b];
        const float amount = std::clamp((hit.speed - 3.0f) / 12.0f, 0.0f, 1.0f);
        if (a.isDynamic()) a.flash = std::max(a.flash, amount);
        if (b.isDynamic()) b.flash = std::max(b.flash, amount);

        if (!sparksOn_ || hit.speed < 6.0f || budget <= 0) continue;
        const Body& lit = a.isDynamic() ? a : b;
        const int count = std::clamp(static_cast<int>(hit.speed * 0.35f), 2, 12);
        effects_.sparks(hit.point, hit.normal, hit.speed, gfx::linearHex(lit.color), count);
        budget -= count;

        const float reduced = std::min(a.mass > 0.0f ? a.mass : 1e9f, b.mass > 0.0f ? b.mass : 1e9f);
        // Rings are for the big moments, and a rain of debris shouldn't trigger a wall of them.
        if (hit.speed > 20.0f && reduced > 2.5f && scene_.time - lastRingTime_ > 0.25f) {
            lastRingTime_ = scene_.time;
            effects_.shockwave(hit.point, 1.5f + hit.speed * 0.09f, gfx::linearHex(lit.color), 0.4f);
        }
    }
}

void App::updateCamera(float dt) {
    const float inset = showUi_ ? 1.0f : 0.0f;
    insetAnim_ += (inset - insetAnim_) * (1.0f - std::exp(-dt * 10.0f));
    view_.originX = (view_.width + stageInset()) * 0.5f;

    Vec2 target = scene_.camera;
    if (scene_.follow >= 0 && scene_.world.bodies()[scene_.follow].alive) {
        const Body& b = scene_.world.bodies()[scene_.follow];
        cameraFollow_ += (b.pos + scene_.followLead - cameraFollow_) * (1.0f - std::exp(-dt * 3.5f));
        target = cameraFollow_;
    }

    if (drag_ != Drag::Pan) {
        view_.centre += (target + cameraPan_ - view_.centre) * (1.0f - std::exp(-dt * 12.0f));
    }

    const float scale = fitScale() * userZoom_;
    view_.pixelsPerMetre += (scale - view_.pixelsPerMetre) * (1.0f - std::exp(-dt * 14.0f));
}

void App::drawOverlay() {
    const float dpi = sceneDpi_;
    const Col accent = gfx::linearHex(scene::sampleRamp(scene_.style.ramp, 0.3f));

    if (drag_ == Drag::Body && grabbedBody_ >= 0) {
        const Body& b = scene_.world.bodies()[grabbedBody_];
        const phys::Joint& j = scene_.world.joints()[mouseJoint_];
        const Vec2 anchor = view_.toPixels(b.pos + phys::rotate(b.q, j.localB));
        const Vec2 cursor = view_.toPixels(mouseWorld_);
        canvas_.line(anchor, cursor, 0.8f * dpi, gfx::withAlpha(accent, 0.5f), dpi);
        canvas_.disc(anchor, 2.5f * dpi, gfx::withAlpha(accent, 0.8f), dpi);
        canvas_.ring(cursor, 5.0f * dpi, 0.8f * dpi, gfx::withAlpha(accent, 0.6f), dpi);
    }

    if (drag_ == Drag::Aim) {
        // Dots along the path the thrown body will take.
        const Vec2 velocity = (mouseWorld_ - aimStart_) * 2.6f;
        Vec2 p = aimStart_;
        Vec2 v = velocity;
        const float dt = 0.045f;
        for (int i = 0; i < 26; ++i) {
            v += scene_.world.gravity * dt;
            p += v * dt;
            const float fade = 1.0f - static_cast<float>(i) / 26.0f;
            canvas_.disc(view_.toPixels(p), (1.5f + 1.6f * fade) * dpi, gfx::withAlpha(accent, 0.2f + 0.8f * fade), dpi);
        }
        canvas_.ring(view_.toPixels(aimStart_), 7.0f * dpi, 0.8f * dpi, gfx::withAlpha(accent, 0.8f), dpi);
    }

    if (drag_ == Drag::Pull) {
        pullPulse_ = std::fmod(pullPulse_ + frameDt_ * 1.5f, 1.0f);
        const Vec2 c = view_.toPixels(mouseWorld_);
        for (int i = 0; i < 3; ++i) {
            const float t = std::fmod(pullPulse_ + static_cast<float>(i) / 3.0f, 1.0f);
            const float radius = (1.0f - t) * 12.0f * view_.pixelsPerMetre;
            canvas_.ring(c, radius, 0.9f * dpi, gfx::withAlpha(accent, 0.34f * t), dpi);
        }
    }
}

void App::render() {
    rlDisableBackfaceCulling();

    gfx::Painter::Options paintOptions;
    paintOptions.heatView = heatView_;
    painter_.prepare(scene_.world, scene_.style, view_, renderAlpha_, sceneDpi_, paintOptions);

    pipeline_.beginScene();
    {
        gfx::BackdropParams backdrop;
        backdrop.view = view_;
        backdrop.time = wallTime_;
        backdrop.top = gfx::linearHex(scene_.style.top);
        backdrop.bottom = gfx::linearHex(scene_.style.bottom);
        backdrop.glow = gfx::linearHex(scene_.style.glow);
        pipeline_.drawBackdrop(backdrop);

        canvas_.begin();
        painter_.drawHorizon(canvas_, scene_.terrain, scene_.style);
        painter_.drawJointLines(canvas_, scene_.world, scene_.style);
        painter_.drawTerrain(canvas_, scene_.terrain, scene_.style);
        painter_.drawSolids(canvas_);
        painter_.drawShadows(canvas_);
        canvas_.end();

        if (trailsOn_) {
            pipeline_.beginEmissive(1.8f);
            canvas_.begin();
            trails_.draw(canvas_, scene_.world, view_, renderAlpha_, sceneDpi_);
            canvas_.end();
            pipeline_.endEmissive();
        }

        canvas_.begin();
        painter_.drawBlobs(canvas_, scene_.world, scene_.blobs);
        painter_.drawBodies(canvas_);
        canvas_.end();

        pipeline_.beginEmissive(2.6f);
        canvas_.begin();
        painter_.drawEmissive(canvas_);
        painter_.drawBlobEmissive(canvas_);
        painter_.drawTerrainRim(canvas_, scene_.terrain, scene_.style);
        painter_.drawPins(canvas_, scene_.world, scene_.style);
        painter_.drawGuides(canvas_, scene_.guides);
        effects_.draw(canvas_, view_, sceneDpi_);
        drawOverlay();
        if (debugOn_) painter_.drawDebug(canvas_, scene_.world);
        canvas_.end();
        pipeline_.endEmissive();
    }
    pipeline_.endScene();
    pipeline_.buildBloom();

    BeginDrawing();
    ClearBackground(BLACK);
    rlScalef(pointScale_, pointScale_, 1.0f);  // everything from here on is in UI points

    gfx::CompositeParams composite;
    composite.bloomAmount = bloomOn_ ? scene_.style.bloom : 0.0f;
    composite.fade = fade_;
    composite.vignette = 0.5f;
    composite.time = wallTime_;
    const Vec2 screen = screenPoints();
    pipeline_.present(composite, screen.x, screen.y);

    if (showUi_) drawHud();

    const bool lastFrame = capture_ && frameIndex_ == options_.frames - 1;
    if (lastFrame && !options_.shotPath.empty()) {
        rlDrawRenderBatchActive();
        Image image = LoadImageFromScreen();
        ExportImage(image, options_.shotPath.c_str());
        UnloadImage(image);
    }
    EndDrawing();
}

void App::drawHud() {
    const Col accent = gfx::uiHex(scene::sampleRamp(scene_.style.ramp, 0.3f));
    const Vec2 screen = screenPoints();
    ui_.beginFrame(pipeline_, canvas_, frameDt_, dpi_, screen, accent, pointer_);

    const float screenW = screen.x;
    const float screenH = screen.y;
    const Col ink = gfx::uiHex(0xF3F5F9);
    const Col inkDim = gfx::uiHex(0xF3F5F9, 0.6f);
    const Col inkFaint = gfx::uiHex(0xF3F5F9, 0.4f);

    const auto& scenes = scene::sceneList();
    const scene::SceneInfo& info = scenes[sceneIndex_];

    // Scene list.
    const float listH = 14.0f + kRowHeight * static_cast<float>(scenes.size());
    const float listY = std::max((screenH - listH - 60.0f) * 0.5f, 16.0f);
    ui_.panel({14.0f, listY, kPanelWidth - 24.0f, listH}, 14.0f);
    for (int i = 0; i < static_cast<int>(scenes.size()); ++i) {
        const Box2 row{21.0f, listY + 7.0f + kRowHeight * static_cast<float>(i), kPanelWidth - 38.0f, kRowHeight};
        if (ui_.listItem(&scenes[i], row, scenes[i].name, kSceneKeys[i], i == sceneIndex_) && i != sceneIndex_) {
            pendingScene_ = i;
            fadingOut_ = true;
        }
    }

    // Title and caption.
    const float titleX = kPanelWidth + 14.0f;
    ui_.text(Face::Title, info.name, titleX, 18.0f, ink);
    ui_.text(Face::Body, info.blurb, titleX, 53.0f, inkDim);

    // A suggestion of what to try, under the caption while the scene is new.
    const float hintAlpha = std::clamp(9.0f - scene_.time, 0.0f, 1.0f);
    if (hintAlpha > 0.0f) {
        ui_.text(Face::Body, info.hint, titleX, 72.0f, gfx::uiHex(0xF3F5F9, 0.4f * hintAlpha));
    }

    // Live numbers, top right.
    char stats[160];
    const phys::WorldStats& s = scene_.world.stats();
    std::snprintf(stats, sizeof stats, "%3.0f fps   %d bodies   %d contacts   %.1f ms", statFps_, s.bodies, s.contacts, statPhysicsMs_);
    ui_.text(Face::Mono, stats, screenW - ui_.textWidth(Face::Mono, stats) - 20.0f, 22.0f, inkFaint);

    // The dock.
    constexpr float kButton = 42.0f;
    constexpr float kGap = 3.0f;
    constexpr float kPad = 7.0f;
    struct Item {
        Icon icon;
        Tool tool;
        const char* tip;
    };
    const Item tools[6] = {{Icon::Grab, Tool::Grab, "Grab"},  {Icon::Ball, Tool::Ball, "Ball"}, {Icon::Box, Tool::Box, "Box"},
                           {Icon::Gem, Tool::Gem, "Gem"},    {Icon::Bomb, Tool::Bomb, "Blast"}, {Icon::Pull, Tool::Pull, "Pull"}};

    const float dockW = kPad * 2.0f + kButton * 10.0f + kGap * 7.0f + 22.0f;
    const float stageCentre = kPanelWidth + (screenW - kPanelWidth) * 0.5f;
    const float dockX = stageCentre - dockW * 0.5f;
    const float dockY = screenH - kButton - kPad * 2.0f - 16.0f;
    ui_.panel({dockX, dockY, dockW, kButton + kPad * 2.0f}, 17.0f);

    float x = dockX + kPad;
    const float y = dockY + kPad;
    for (const Item& item : tools) {
        const auto id = reinterpret_cast<const void*>(static_cast<size_t>(item.tool) + 1);
        if (ui_.iconButton(id, {x, y, kButton, kButton}, item.icon, tool_ == item.tool, item.tip)) {
            tool_ = item.tool;
        }
        x += kButton + kGap;
    }
    ui_.divider(x + 5.0f, y + 9.0f, kButton - 18.0f);
    x += 11.0f;
    if (ui_.iconButton(&paused_, {x, y, kButton, kButton}, paused_ ? Icon::Play : Icon::Pause, false, paused_ ? "Play   Space" : "Pause   Space")) {
        paused_ = !paused_;
    }
    x += kButton + kGap;
    if (ui_.iconButton(&stepOnce_, {x, y, kButton, kButton}, Icon::Step, false, "Step one frame   .")) {
        paused_ = true;
        stepOnce_ = true;
    }
    x += kButton + kGap;
    if (ui_.iconButton(&sceneIndex_, {x, y, kButton, kButton}, Icon::Reset, false, "Restart   R")) reset();
    x += kButton + 11.0f;
    ui_.divider(x - 5.0f, y + 9.0f, kButton - 18.0f);
    if (ui_.iconButton(&showSettings_, {x, y, kButton, kButton}, Icon::Settings, showSettings_, "Settings   Tab")) {
        showSettings_ = !showSettings_;
    }


    if (paused_) {
        const char* label = "PAUSED";
        const float w = ui_.textWidth(Face::Mono, label);
        ui_.text(Face::Mono, label, screenW - w - 20.0f, 40.0f, accent);
    }

    if (showSettings_) {
        constexpr float kWidth = 244.0f;
        constexpr float kHeight = 416.0f;
        const float px = screenW - kWidth - 16.0f;
        const float py = std::max(dockY - kHeight - 10.0f, 8.0f);  // above the dock, never over it
        ui_.panel({px, py, kWidth, kHeight}, 16.0f);
        const float left = px + 18.0f;
        const float width = kWidth - 36.0f;
        float cursor = py + 16.0f;
        char value[32];

        std::snprintf(value, sizeof value, "%.2f\xC3\x97", static_cast<double>(gravityScale_));
        if (ui_.slider(&gravityScale_, {left, cursor, width, 40.0f}, "Gravity", value, gravityScale_, -1.0f, 3.0f)) {
            if (std::abs(gravityScale_ - 1.0f) < 0.04f) gravityScale_ = 1.0f;
            if (std::abs(gravityScale_) < 0.04f) gravityScale_ = 0.0f;
            applyGravity();
        }
        cursor += 46.0f;

        std::snprintf(value, sizeof value, "%.2f\xC3\x97", static_cast<double>(timeScale_));
        if (ui_.slider(&timeScale_, {left, cursor, width, 40.0f}, "Time", value, timeScale_, 0.05f, 2.0f)) {
            if (std::abs(timeScale_ - 1.0f) < 0.03f) timeScale_ = 1.0f;
        }
        cursor += 46.0f;

        std::snprintf(value, sizeof value, "%.2f", static_cast<double>(bounce_));
        if (ui_.slider(&bounce_, {left, cursor, width, 40.0f}, "Extra bounce", value, bounce_, 0.0f, 1.0f)) {
            scene_.world.minRestitution = bounce_;
        }
        cursor += 46.0f;

        std::snprintf(value, sizeof value, "%d", static_cast<int>(std::round(substepSetting_)));
        if (ui_.slider(&substepSetting_, {left, cursor, width, 40.0f}, "Substeps", value, substepSetting_, 1.0f, 20.0f)) {
            scene_.world.substeps = static_cast<int>(std::round(substepSetting_));
        }
        cursor += 50.0f;

        constexpr float kToggle = 27.0f;
        ui_.toggle(&trailsOn_, {left, cursor, width, kToggle}, "Trails", "T", trailsOn_);
        cursor += kToggle;
        ui_.toggle(&sparksOn_, {left, cursor, width, kToggle}, "Sparks", "", sparksOn_);
        cursor += kToggle;
        ui_.toggle(&bloomOn_, {left, cursor, width, kToggle}, "Bloom", "B", bloomOn_);
        cursor += kToggle;
        ui_.toggle(&heatView_, {left, cursor, width, kToggle}, "Colour by speed", "V", heatView_);
        cursor += kToggle;
        ui_.toggle(&debugOn_, {left, cursor, width, kToggle}, "Show contacts", "C", debugOn_);
        cursor += kToggle + 10.0f;

        ui_.text(Face::Body, "Scroll zooms. H hides all of this.", left, cursor, inkFaint);
    }

    ui_.endFrame();
}
