#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

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
constexpr float kListWidth = 190.0f;    // the stage starts this far from the left, in UI points
constexpr float kLessonWidth = 348.0f;  // and ends this far from the right
constexpr float kMargin = 14.0f;
constexpr float kRowHeight = 25.0f;
constexpr float kSlowMotion = 0.25f;
constexpr int kMinWidth = 1000;  // the smallest window the interface is laid out for, in UI points
constexpr int kMinHeight = 600;

const char* const kSceneKeys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="};
const int kSceneKeyCodes[12] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX,
                                KEY_SEVEN, KEY_EIGHT, KEY_NINE, KEY_ZERO, KEY_MINUS, KEY_EQUAL};

const Col kInk = gfx::uiHex(0xF3F5F9);
const Col kInkSoft = gfx::uiHex(0xF3F5F9, 0.8f);
const Col kInkDim = gfx::uiHex(0xF3F5F9, 0.6f);
const Col kInkFaint = gfx::uiHex(0xF3F5F9, 0.4f);

float randomUnit(unsigned int& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>((seed >> 8) & 0xFFFFFF) / 16777216.0f;
}

double secondsNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string formatNumber(float v, int decimals) {
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, static_cast<double>(v));
    std::string text = buffer;
    // "-0.00" reads as a measurement error; it is really zero.
    if (text[0] == '-' && text.find_first_not_of("-0.") == std::string::npos) text.erase(0, 1);
    return text;
}

// Degrees, percentages and multiples sit right against the number; other units get a space.
std::string withUnit(const std::string& number, const std::string& unit) {
    if (unit.empty()) return number;
    const bool tight = unit.rfind("°", 0) == 0 || unit[0] == '%' || unit.rfind("×", 0) == 0;
    return number + (tight ? "" : " ") + unit;
}

std::vector<std::string> splitLines(const char* text) {
    std::vector<std::string> lines;
    std::string line;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == '\n') {
            lines.push_back(line);
            line.clear();
        } else {
            line += *p;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// What each arrow label in the lessons stands for.
const char* arrowMeaning(const std::string& label) {
    if (label == "v") return "velocity";
    if (label == "vx") return "sideways part of the velocity";
    if (label == "vy") return "vertical part of the velocity";
    if (label == "mg") return "weight, the pull of gravity";
    if (label == "N") return "normal force from the surface";
    if (label == "f") return "friction";
    if (label == "T") return "tension";
    if (label == "p") return "momentum, mass × velocity";
    if (label == "F") return "the star's gravitational pull";
    if (label == "−kx") return "spring force";
    return "";
}

bool isDark(uint32_t rgb) {
    const float r = static_cast<float>((rgb >> 16) & 0xFF), g = static_cast<float>((rgb >> 8) & 0xFF),
                b = static_cast<float>(rgb & 0xFF);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b < 90.0f;
}

// 1, 2 or 5 times a power of ten, close to `rough`.
float niceStep(float rough) {
    const float power = std::pow(10.0f, std::floor(std::log10(std::max(rough, 1e-9f))));
    const float f = rough / power;
    return (f < 1.5f ? 1.0f : f < 3.5f ? 2.0f : f < 7.5f ? 5.0f : 10.0f) * power;
}

std::string powerLabel(int exponent) {
    static const char* const digits[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
    if (exponent == 0) return "1";
    if (exponent == 1) return "10";
    std::string text = "10";
    if (exponent < 0) text += "⁻";
    text += digits[std::min(std::abs(exponent), 9)];
    return text;
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

#else

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
    lessonTab_ = options.experimentTab ? 1 : 0;

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
    loadFonts();

    lessonValues_.resize(scene::sceneList().size());
    sceneIndex_ = std::clamp(options.scene, 0, static_cast<int>(scene::sceneList().size()) - 1);
    loadScene(sceneIndex_);
    fade_ = capture_ ? 1.0f : 0.0f;

    parseScript(options.script);

    if (!options.toolName.empty()) {
        const std::string& t = options.toolName;
        if (t == "inspect") tool_ = Tool::Inspect;
        if (t == "grab") tool_ = Tool::Grab;
        if (t == "throw") tool_ = Tool::Throw;
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

void App::loadFonts() {
    if (fontDpi_ > 0.0f) ui_.unload();
    fontDpi_ = dpi_;
    const std::string fonts = std::string(GetApplicationDirectory()) + "assets/fonts";
    ui_.load(fonts.c_str(), dpi_);
}

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
    const bool newLesson = index != sceneIndex_ || frameIndex_ == 0;
    sceneIndex_ = index;
    scene::loadScene(scene_, index, lessonValues_[index]);
    lessonValues_[index].clear();
    for (const scene::Param& p : scene_.params) lessonValues_[index].push_back(p.value);
    paramsDirty_ = false;

    effects_.clear();
    trails_.configure(scene_.style.trailLength, scene_.style.trailMinSpeed);
    if (newLesson) {
        tool_ = scene_.startTool;
        lessonScroll_ = 0.0f;
        cameraPan_ = {};
        userZoom_ = 1.0f;
    }
    drag_ = Drag::None;
    mouseJoint_ = -1;
    grabbedBody_ = -1;
    selected_ = -1;
    accumulator_ = 0.0f;
    renderAlpha_ = 1.0f;
    substepSetting_ = static_cast<float>(scene_.world.substeps);
    applyGravity();
    scene_.measure();
    plotLo_ = scene_.plot.lo;
    plotHi_ = scene_.plot.hi;

    // Snap the camera, so a new scene doesn't begin with a long pan.
    view_.width = static_cast<float>(pipeline_.width());
    view_.height = static_cast<float>(pipeline_.height());
    insetAnim_ = showUi_ ? 1.0f : 0.0f;
    view_.originX = (view_.width + stageLeft() - stageRight()) * 0.5f;
    view_.pixelsPerMetre = fitScale() * userZoom_;
    cameraFollow_ = scene_.camera;
    if (scene_.follow >= 0) cameraFollow_ = scene_.world.bodies()[scene_.follow].pos + scene_.followLead;
    view_.centre = cameraFollow_ + cameraPan_;
}

void App::reset() {
    pendingScene_ = sceneIndex_;
    fadingOut_ = true;
}

void App::changeScene(int index) {
    if (index == sceneIndex_) return;
    pendingScene_ = index;
    fadingOut_ = true;
}

// Gravity points the way the lesson set it up, at the strength chosen in Settings.
void App::applyGravity() {
    const float base = phys::length(scene_.baseGravity);
    scene_.world.gravity = base > 0.0f ? scene_.baseGravity * (gravity_ / base) : Vec2{};
    scene_.world.wakeAll();
}

float App::stageLeft() const { return kListWidth * sceneDpi_ * insetAnim_; }
float App::stageRight() const { return kLessonWidth * sceneDpi_ * insetAnim_; }

void App::resizeTargets() {
    dpi_ = GetWindowScaleDPI().x * pointScale_;
    sceneDpi_ = dpi_ * renderScale_;
    pipeline_.resize(static_cast<int>(static_cast<float>(GetRenderWidth()) * renderScale_),
                     static_cast<int>(static_cast<float>(GetRenderHeight()) * renderScale_));
    // Fonts are baked for one scale, and text goes soft if the window lands on another. The
    // margin stops a slow browser resize from rebuilding them every frame.
    if (std::abs(dpi_ / fontDpi_ - 1.0f) > 0.1f) loadFonts();
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
    // Scenes without a set width are framed for a landscape stage.
    const float viewWidth = scene_.viewWidth > 0.0f ? scene_.viewWidth : scene_.viewHeight * 1.1f;
    const float stageWidth = std::max(view_.width - stageLeft() - stageRight(), view_.width * 0.25f);
    return std::min(view_.height / scene_.viewHeight, stageWidth / viewWidth);
}

const Body* App::selectedBody() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(scene_.world.bodies().size())) return nullptr;
    const Body& b = scene_.world.bodies()[selected_];
    return b.alive && b.serial == selectedSerial_ ? &b : nullptr;
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
        std::string values;
        for (const scene::Param& p : scene_.params) values += (values.empty() ? "" : ",") + formatNumber(p.value, p.decimals);
        std::printf("scene=%s tool=%d bodies=%d joints=%d paused=%d settings=%d gravity=%.2f time=%.2f zoom=%.2f ui=%d "
                    "heat=%d tab=%d vectors=%d slow=%d selected=%d params=%s\n",
                    scene::sceneList()[sceneIndex_].name, static_cast<int>(tool_), s.bodies, s.joints, paused_ ? 1 : 0,
                    showSettings_ ? 1 : 0, static_cast<double>(gravity_), static_cast<double>(timeScale_),
                    static_cast<double>(userZoom_), showUi_ ? 1 : 0, heatView_ ? 1 : 0, lessonTab_, vectorsOn_ ? 1 : 0,
                    slowMotion_ ? 1 : 0, selectedBody() != nullptr ? 1 : 0, values.c_str());
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
    // A setting that restarts the experiment does so once the slider is let go.
    if (paramsDirty_ && !pointer_.leftDown) {
        paramsDirty_ = false;
        reset();
    }
    handleKeys();
    handlePointer();

    const double physicsStart = secondsNow();
    if (!paused_ || stepOnce_) {
        const float speed = timeScale_ * (slowMotion_ ? kSlowMotion : 1.0f);
        accumulator_ += stepOnce_ && paused_ ? kStep : dt * speed;
        int steps = 0;
        while (accumulator_ >= kStep && steps < kMaxStepsPerFrame) {
            stepPhysics(kStep);
            accumulator_ -= kStep;
            ++steps;
        }
        // If the machine can't keep up, slow down rather than spiral.
        if (steps == kMaxStepsPerFrame) accumulator_ = 0.0f;
        renderAlpha_ = accumulator_ / kStep;
        effects_.update(dt * speed, scene_.world.gravity);
    }
    stepOnce_ = false;
    benchPhysics_ += frameIndex_ > 30 ? secondsNow() - physicsStart : 0.0;

    for (Body& b : scene_.world.bodies()) {
        if (b.flash > 0.0f) b.flash *= std::exp(-dt * 7.0f);
    }

    updateCamera(dt);
    render();

    statTimer_ += GetFrameTime();
    ++statFrames_;
    if (statTimer_ > 0.3f) {
        statFps_ = static_cast<float>(statFrames_) / statTimer_;
        statTimer_ = 0.0f;
        statFrames_ = 0;
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
    if (keyHit(KEY_K)) heatView_ = !heatView_;
    if (keyHit(KEY_V)) vectorsOn_ = !vectorsOn_;
    if (keyHit(KEY_S)) slowMotion_ = !slowMotion_;
#if !defined(__EMSCRIPTEN__)
    if (keyHit(KEY_F)) ToggleBorderlessWindowed();  // the page handles this key on the web
#endif
    if (keyHit(KEY_Q)) tool_ = Tool::Inspect;
    if (keyHit(KEY_W)) tool_ = Tool::Grab;
    if (keyHit(KEY_E)) tool_ = Tool::Throw;

    const int count = static_cast<int>(scene::sceneList().size());
    for (int i = 0; i < std::min(count, 12); ++i) {
        if (keyHit(kSceneKeyCodes[i])) changeScene(i);
    }
    if (keyHit(KEY_RIGHT_BRACKET)) changeScene((sceneIndex_ + 1) % count);
    if (keyHit(KEY_LEFT_BRACKET)) changeScene((sceneIndex_ + count - 1) % count);
}

void App::handlePointer() {
    const Vec2 pixels = pointer_.position * sceneDpi_;
    mouseWorld_ = view_.toWorld(pixels);
    phys::World& world = scene_.world;

    const bool overUi = showUi_ && ui_.wantsMouse(pointer_.position);

    const float wheel = pointer_.wheel;
    if (wheel != 0.0f && showUi_ && lessonScrollBox_.contains(pointer_.position)) {
        lessonScroll_ -= wheel * 32.0f;  // clamped when the panel is drawn
    } else if (wheel != 0.0f && !overUi) {
        const Vec2 before = view_.toWorld(pixels);
        userZoom_ = std::clamp(userZoom_ * std::exp(wheel * 0.09f), 0.25f, 6.0f);
        view_.pixelsPerMetre = fitScale() * userZoom_;
        const Vec2 after = view_.toWorld(pixels);
        cameraPan_ += before - after;
        view_.centre += before - after;
    }

    if (pointer_.leftPressed && !overUi) {
        const int id = world.bodyAt(mouseWorld_);
        const bool onBody = id >= 0 && world.bodies()[id].isDynamic();
        switch (tool_) {
            case Tool::Inspect:
                if (onBody) {
                    selected_ = id;
                    selectedSerial_ = world.bodies()[id].serial;
                } else {
                    selected_ = -1;
                    drag_ = Drag::Pan;
                    panStartWorld_ = mouseWorld_;
                }
                break;
            case Tool::Grab:
                if (onBody) {
                    grabbedBody_ = id;
                    mouseJoint_ = world.createMouse(id, mouseWorld_, 1400.0f * world.bodies()[id].mass, 7.0f, 1.0f);
                    drag_ = Drag::Body;
                } else {
                    drag_ = Drag::Pan;
                    panStartWorld_ = mouseWorld_;
                }
                break;
            case Tool::Throw:
                drag_ = Drag::Aim;
                aimStart_ = mouseWorld_;
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
            if (id == selected_) selected_ = -1;
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
            // A click drops a ball; a drag throws it towards the cursor.
            throwBody(aimStart_, (mouseWorld_ - aimStart_) * 2.6f);
        }
        drag_ = Drag::None;
    }
}

void App::throwBody(Vec2 at, Vec2 velocity) {
    BodyDef d;
    d.shape = phys::makeCircle(0.4f + randomUnit(spawnSeed_) * 0.3f);
    d.pos = at;
    d.vel = velocity;
    d.density = 1.4f;
    d.restitution = 0.25f;
    d.color = scene::sampleRamp(scene_.style.ramp, randomUnit(spawnSeed_));
    d.trail = phys::length(velocity) > 8.0f;
    const int id = scene_.world.createBody(d);
    scene_.world.bodies()[id].flash = 0.5f;
}

void App::stepPhysics(float dt) {
    phys::World& world = scene_.world;
    if (mouseJoint_ >= 0) world.joints()[mouseJoint_].target = mouseWorld_;

    scene_.step(dt);

    pollImpacts();
    trails_.sample(world);

    // Anything thrown off the edge of the world goes away.
    for (int i = 0; i < static_cast<int>(world.bodies().size()); ++i) {
        const Body& b = world.bodies()[i];
        if (!b.alive || !b.isDynamic()) continue;
        if (b.pos.y < scene_.killY || std::abs(b.pos.y) > 2000.0f) {
            if (i == grabbedBody_) continue;
            if (i == selected_) selected_ = -1;
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
    }
}

void App::updateCamera(float dt) {
    const float inset = showUi_ ? 1.0f : 0.0f;
    insetAnim_ += (inset - insetAnim_) * (1.0f - std::exp(-dt * 10.0f));
    view_.originX = (view_.width + stageLeft() - stageRight()) * 0.5f;

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
        // Dots along the path the thrown body will take: a parabola, or a straight line without gravity.
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
}

void App::render() {
    rlDisableBackfaceCulling();
    const phys::World& world = scene_.world;

    gfx::Painter::Options paintOptions;
    paintOptions.heatView = heatView_;
    painter_.prepare(world, scene_.style, view_, renderAlpha_, sceneDpi_, paintOptions);

    // The selected body gets a ring and its velocity, on top of whatever the lesson draws.
    std::vector<scene::Marker> selection;
    std::vector<scene::Arrow> selectionArrows;
    if (const Body* b = selectedBody()) {
        scene::Marker ring;
        ring.body = selected_;
        ring.radius = b->shape.boundRadius + 0.18f;
        ring.ring = true;
        ring.colour = 0xF3F5F9;
        selection.push_back(ring);
        selectionArrows.push_back({selected_, {}, b->vel * 0.25f, scene::ink::velocity, "v"});
    }

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
        painter_.drawGuides(canvas_, scene_.overlays, true);
        painter_.drawJointLines(canvas_, world, scene_.style);
        painter_.drawTerrain(canvas_, scene_.terrain, scene_.style);
        painter_.drawSolids(canvas_);
        painter_.drawShadows(canvas_);
        canvas_.end();

        if (trailsOn_) {
            pipeline_.beginEmissive(1.8f);
            canvas_.begin();
            trails_.draw(canvas_, world, view_, renderAlpha_, sceneDpi_);
            canvas_.end();
            pipeline_.endEmissive();
        }

        canvas_.begin();
        painter_.drawBlobs(canvas_, world, scene_.blobs);
        painter_.drawBodies(canvas_);
        canvas_.end();

        pipeline_.beginEmissive(2.6f);
        canvas_.begin();
        painter_.drawEmissive(canvas_);
        painter_.drawBlobEmissive(canvas_);
        painter_.drawTerrainRim(canvas_, scene_.terrain, scene_.style);
        painter_.drawPins(canvas_, world, scene_.style);
        painter_.drawGuides(canvas_, scene_.guides);
        painter_.drawGuides(canvas_, scene_.overlays);
        painter_.drawMarkers(canvas_, world, scene_.markers);
        painter_.drawMarkers(canvas_, world, selection);
        if (vectorsOn_) painter_.drawArrows(canvas_, world, scene_.arrows);
        painter_.drawArrows(canvas_, world, selectionArrows);
        effects_.draw(canvas_, view_, sceneDpi_);
        drawOverlay();
        if (debugOn_) painter_.drawDebug(canvas_, world);
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

    drawHud();

    const bool lastFrame = capture_ && frameIndex_ == options_.frames - 1;
    if (lastFrame && !options_.shotPath.empty()) {
        rlDrawRenderBatchActive();
        Image image = LoadImageFromScreen();
        ExportImage(image, options_.shotPath.c_str());
        UnloadImage(image);
    }
    EndDrawing();
}

// ------------------------------------------------------------------- interface

Vec2 App::toScreen(Vec2 world) const { return view_.toPixels(world) * (1.0f / sceneDpi_); }

float App::sectionHeading(const char* label, float x, float y) {
    ui_.text(Face::Small, label, x, y, kInkFaint);
    return y + 21.0f;
}

void App::drawHud() {
    const Col accent = gfx::uiHex(scene::sampleRamp(scene_.style.ramp, 0.3f));
    const Vec2 screen = screenPoints();
    ui_.beginFrame(pipeline_, canvas_, frameDt_, dpi_, screen, accent, pointer_);

    // Labels belong to the scene, so they go under every panel. They show even with the UI hidden.
    drawSceneLabels();

    if (showUi_) {
        drawCurriculum(screen.y);

        const float columnX = screen.x - kLessonWidth + 12.0f;
        const float columnW = kLessonWidth - 26.0f;
        const float instruments = instrumentsHeight();
        const float lessonH = screen.y - 2.0f * kMargin - (instruments > 0.0f ? instruments + 10.0f : 0.0f);
        drawLesson(columnX, kMargin, columnW, lessonH);
        if (instruments > 0.0f) drawInstruments(columnX, screen.y - kMargin - instruments, columnW, instruments);

        // The clock, top right of the stage.
        const float stageRightEdge = screen.x - kLessonWidth - 6.0f;
        char clock[48];
        std::snprintf(clock, sizeof clock, "t = %.2f s", static_cast<double>(scene_.time));
        ui_.text(Face::Mono, clock, stageRightEdge - ui_.textWidth(Face::Mono, clock), 20.0f, kInkDim);
        float flagY = 38.0f;
        if (paused_) {
            ui_.text(Face::Mono, "PAUSED", stageRightEdge - ui_.textWidth(Face::Mono, "PAUSED"), flagY, accent);
            flagY += 17.0f;
        }
        if (slowMotion_) {
            const char* label = "SLOW MOTION ¼×";
            ui_.text(Face::Caption, label, stageRightEdge - ui_.textWidth(Face::Caption, label), flagY, accent);
        }

        drawInspector(kListWidth + 4.0f, kMargin);
        drawDock(screen.x, screen.y);
    }
    ui_.endFrame();
}

void App::drawSceneLabels() {
    const phys::World& world = scene_.world;
    if (vectorsOn_) {
        for (const scene::Arrow& a : scene_.arrows) {
            if (a.label.empty()) continue;
            const Vec2 base = painter_.anchor(world, a.body, a.from);
            const Vec2 from = toScreen(base);
            const Vec2 to = toScreen(base + a.vec);
            if (phys::length(to - from) < 6.0f) continue;
            const Vec2 at = to + phys::normalized(to - from) * 10.0f;
            const float w = ui_.textWidth(Face::Caption, a.label.c_str());
            ui_.textOverScene(Face::Caption, a.label.c_str(), at.x - w * 0.5f, at.y - 7.5f, gfx::uiHex(a.colour));
        }
    }
    for (const scene::Marker& m : scene_.markers) {
        if (m.label.empty()) continue;
        const Vec2 p = toScreen(painter_.anchor(world, m.body, m.at));
        const float w = ui_.textWidth(Face::Caption, m.label.c_str());
        const Col colour = gfx::uiHex(m.colour);
        if (m.radius <= 0.0f) {
            if (isDark(m.colour)) {
                ui_.text(Face::Caption, m.label.c_str(), p.x - w * 0.5f, p.y - 7.5f, colour);
            } else {
                ui_.textOverScene(Face::Caption, m.label.c_str(), p.x - w * 0.5f, p.y - 7.5f, colour);
            }
        } else {
            const float r = std::max(m.radius * view_.pixelsPerMetre / sceneDpi_, 2.5f);
            ui_.textOverScene(Face::Caption, m.label.c_str(), p.x + r + 6.0f, p.y - 7.5f, colour);
        }
    }
}

void App::drawCurriculum(float screenH) {
    const auto& lessons = scene::sceneList();
    const int count = static_cast<int>(lessons.size());

    float height = 14.0f;
    for (int i = 0; i < count; ++i) {
        if (i == 0 || std::string_view(lessons[i].topic) != lessons[i - 1].topic) height += i == 0 ? 20.0f : 27.0f;
        height += kRowHeight;
    }
    const float top = std::max((screenH - height - 70.0f) * 0.5f, kMargin);
    ui_.panel({kMargin, top, kListWidth - 24.0f, height}, 14.0f);

    float y = top + 10.0f;
    for (int i = 0; i < count; ++i) {
        if (i == 0 || std::string_view(lessons[i].topic) != lessons[i - 1].topic) {
            if (i > 0) y += 7.0f;
            std::string topic = lessons[i].topic;
            for (char& c : topic) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            ui_.text(Face::Small, topic.c_str(), kMargin + 17.0f, y + 4.0f, kInkFaint);
            y += 20.0f;
        }
        const Box2 row{kMargin + 7.0f, y, kListWidth - 38.0f, kRowHeight};
        if (ui_.listItem(&lessons[i], row, lessons[i].name, i < 12 ? kSceneKeys[i] : "", i == sceneIndex_)) changeScene(i);
        y += kRowHeight;
    }
}

void App::drawLesson(float x, float y, float w, float h) {
    const auto& lessons = scene::sceneList();
    const scene::SceneInfo& info = lessons[sceneIndex_];
    const Col accent = gfx::mix(gfx::uiHex(scene::sampleRamp(scene_.style.ramp, 0.3f)), kInk, 0.25f);
    ui_.panel({x, y, w, h}, 16.0f);

    const float left = x + 20.0f;
    const float width = w - 40.0f;
    float cursor = y + 18.0f;

    std::string topic = info.topic;
    for (char& c : topic) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    char header[96];
    std::snprintf(header, sizeof header, "%s  ·  %d OF %d", topic.c_str(), sceneIndex_ + 1, static_cast<int>(lessons.size()));
    ui_.text(Face::Small, header, left, cursor, accent);
    cursor += 16.0f;
    ui_.text(Face::Heading, info.name, left, cursor, kInk);
    cursor += 34.0f;

    const float tabW = width * 0.5f;
    if (ui_.tab(&lessonTab_, {left, cursor, tabW - 2.0f, 28.0f}, "Learn", lessonTab_ == 0) && lessonTab_ != 0) {
        lessonTab_ = 0;
        lessonScroll_ = 0.0f;
    }
    if (ui_.tab(&lessonContent_, {left + tabW + 2.0f, cursor, tabW - 2.0f, 28.0f}, "Experiment", lessonTab_ == 1) &&
        lessonTab_ != 1) {
        lessonTab_ = 1;
        lessonScroll_ = 0.0f;
    }
    cursor += 28.0f + 12.0f;

    const Box2 area{x + 2.0f, cursor, w - 4.0f, std::max(y + h - cursor - 8.0f, 20.0f)};
    lessonScrollBox_ = area;
    const float maxScroll = std::max(0.0f, lessonContent_ - area.h + 10.0f);
    lessonScroll_ = std::clamp(lessonScroll_, 0.0f, maxScroll);

    // The principle leads both tabs, and scrolls with them so short windows keep room to read.
    ui_.beginClip(area);
    const float start = cursor + 8.0f - lessonScroll_;
    const float body = start + ui_.paragraph(Face::Body, info.principle, left, start, width, kInk, 1.5f) + 20.0f;
    const float end = lessonTab_ == 0 ? drawLearn(left, body, width) : drawExperiment(left, body, width);
    ui_.endClip();
    lessonContent_ = end - start + 8.0f;

    // Soft edges where the text runs on beyond the visible part.
    const auto fade = [&](float edgeY, float towardY) {
        const Col solid = gfx::uiHex(0x101219, 0.92f);
        const Col clear = gfx::uiHex(0x101219, 0.0f);
        canvas_.quad({area.x, edgeY}, solid, {area.x + area.w, edgeY}, solid, {area.x + area.w, towardY}, clear, {area.x, towardY}, clear);
    };
    canvas_.begin();
    if (lessonScroll_ < maxScroll - 1.0f) fade(area.y + area.h, area.y + area.h - 26.0f);
    if (lessonScroll_ > 1.0f) fade(area.y, area.y + 18.0f);
    canvas_.end();

    if (maxScroll > 0.0f) {
        const float track = area.h - 8.0f;
        const float thumb = std::max(24.0f, track * area.h / lessonContent_);
        const float thumbY = area.y + 4.0f + (track - thumb) * (lessonScroll_ / maxScroll);
        canvas_.begin();
        canvas_.roundedRect(x + w - 8.0f, thumbY, 3.0f, thumb, 1.5f, gfx::uiHex(0xFFFFFF, 0.22f));
        canvas_.end();
    }
}

float App::drawLearn(float x, float y, float w) {
    const scene::SceneInfo& info = scene::sceneList()[sceneIndex_];
    const Col accent = gfx::uiHex(scene::sampleRamp(scene_.style.ramp, 0.3f));

    // The maths, set apart in a box of its own.
    const std::vector<std::string> lines = splitLines(info.equation);
    constexpr float kLine = 23.0f;
    const float boxH = 14.0f + static_cast<float>(lines.size()) * kLine + 4.0f;
    canvas_.begin();
    canvas_.roundedRect(x - 6.0f, y, w + 12.0f, boxH, 10.0f, gfx::uiHex(0xFFFFFF, 0.05f));
    canvas_.roundedRect(x - 6.0f, y + 10.0f, 2.5f, boxH - 20.0f, 1.25f, gfx::withAlpha(accent, 0.9f));
    canvas_.end();
    float ly = y + 12.0f;
    for (const std::string& line : lines) {
        ui_.text(Face::Equation, line.c_str(), x + 8.0f, ly, kInk);
        ly += kLine;
    }
    y += boxH + 22.0f;

    y = sectionHeading("HOW IT WORKS", x, y);
    y += ui_.paragraph(Face::Body, info.how, x, y, w, kInkSoft, 1.55f) + 24.0f;

    y = sectionHeading("WHERE IT'S USED", x, y);
    for (const std::string& use : splitLines(info.uses)) {
        canvas_.begin();
        canvas_.disc({x + 2.5f, y + 8.5f}, 2.2f, accent);
        canvas_.end();
        y += ui_.paragraph(Face::Body, use.c_str(), x + 14.0f, y, w - 14.0f, kInkSoft, 1.5f) + 9.0f;
    }
    return y + 4.0f;
}

float App::drawExperiment(float x, float y, float w) {
    const scene::SceneInfo& info = scene::sceneList()[sceneIndex_];
    const Col accent = gfx::uiHex(scene::sampleRamp(scene_.style.ramp, 0.3f));
    auto& params = scene_.params;

    if (!params.empty()) {
        y = sectionHeading("SET UP", x, y);
        bool restarts = false;
        bool changed = false;
        for (size_t i = 0; i < params.size(); ++i) {
            scene::Param& p = params[i];
            const std::string text = p.format ? p.format(p.value) : withUnit(formatNumber(p.value, p.decimals), p.unit);
            float v = p.value;
            if (ui_.slider(&p, {x, y, w, 40.0f}, p.label.c_str(), text.c_str(), v, p.lo, p.hi)) {
                if (p.step > 0.0f) v = std::clamp(std::round(v / p.step) * p.step, p.lo, p.hi);
                if (v != p.value) {
                    p.value = v;
                    lessonValues_[sceneIndex_][i] = v;
                    if (!p.live) paramsDirty_ = true;
                }
            }
            restarts = restarts || !p.live;
            changed = changed || std::abs(p.value - p.initial) > 1e-5f;
            y += 46.0f;
        }
        if (restarts) {
            ui_.text(Face::Caption, "Changing a setting restarts the experiment.", x, y - 4.0f, kInkFaint);
            y += 18.0f;
        }
        if (changed) {
            if (ui_.link(&lessonValues_, x, y, "Restore defaults")) {
                lessonValues_[sceneIndex_].clear();
                reset();
            }
            y += 20.0f;
        }
        y += 14.0f;
    }

    y = sectionHeading("TRY THIS", x, y);
    int number = 1;
    for (const std::string& idea : splitLines(info.tryThis)) {
        char label[8];
        std::snprintf(label, sizeof label, "%d", number++);
        ui_.text(Face::Strong, label, x, y, accent);
        y += ui_.paragraph(Face::Body, idea.c_str(), x + 16.0f, y, w - 16.0f, kInkSoft, 1.5f) + 10.0f;
    }

    // A key to the arrows currently on screen.
    std::vector<const scene::Arrow*> kinds;
    for (const scene::Arrow& a : scene_.arrows) {
        if (a.label.empty() || arrowMeaning(a.label)[0] == '\0') continue;
        const bool seen = std::any_of(kinds.begin(), kinds.end(), [&](const scene::Arrow* k) { return k->label == a.label; });
        if (!seen) kinds.push_back(&a);
    }
    if (!kinds.empty() && vectorsOn_) {
        y += 12.0f;
        y = sectionHeading("ARROWS", x, y);
        for (const scene::Arrow* a : kinds) {
            const Col colour = gfx::uiHex(a->colour);
            canvas_.begin();
            canvas_.line({x, y + 8.0f}, {x + 15.0f, y + 8.0f}, 1.5f, colour, 0.8f);
            canvas_.triangleFilled({x + 21.0f, y + 8.0f}, {x + 14.0f, y + 4.0f}, {x + 14.0f, y + 12.0f}, colour);
            canvas_.end();
            ui_.text(Face::Strong, a->label.c_str(), x + 30.0f, y, colour);
            ui_.text(Face::Body, arrowMeaning(a->label), x + 62.0f, y, kInkSoft);
            y += 21.0f;
        }
    }

    y += 14.0f;
    y += ui_.paragraph(Face::Caption,
                       "Use the Inspect tool (Q) to read any body's mass, speed, momentum and energy. "
                       "S slows time down, Space pauses, and . steps one frame at a time.",
                       x, y, w, kInkFaint, 1.5f);
    return y + 4.0f;
}

// The plot gives up height first on short screens, leaving room for the lesson text.
float App::plotHeight() const { return std::clamp((screenPoints().y - 600.0f) * 0.25f + 64.0f, 64.0f, 96.0f); }

float App::instrumentsHeight() const {
    const int rows = static_cast<int>(scene_.readouts.size());
    const bool plot = scene_.plot.active();
    if (rows == 0 && !plot) return 0.0f;
    float h = 16.0f + 21.0f + static_cast<float>(rows) * 20.0f + 10.0f;
    if (plot) h += 8.0f + 22.0f + plotHeight();
    return h;
}

void App::drawInstruments(float x, float y, float w, float h) {
    ui_.panel({x, y, w, h}, 16.0f);
    const float left = x + 18.0f;
    const float right = x + w - 18.0f;
    const bool anyTheory = std::any_of(scene_.readouts.begin(), scene_.readouts.end(), [](const scene::Readout& r) {
        return std::isfinite(r.expected) || !r.expectedText.empty();
    });
    const float measuredRight = anyTheory ? right - 66.0f : right;

    float cursor = y + 16.0f;
    ui_.text(Face::Small, "MEASUREMENTS", left, cursor, kInkFaint);
    if (anyTheory) {
        ui_.text(Face::Small, "MEASURED", measuredRight - ui_.textWidth(Face::Small, "MEASURED"), cursor, kInkFaint);
        ui_.text(Face::Small, "THEORY", right - ui_.textWidth(Face::Small, "THEORY"), cursor, kInkFaint);
    }
    cursor += 21.0f;

    for (const scene::Readout& r : scene_.readouts) {
        std::string measured = r.text;
        if (measured.empty()) measured = std::isfinite(r.value) ? withUnit(formatNumber(r.value, r.decimals), r.unit) : "–";
        std::string theory = r.expectedText;
        if (theory.empty() && std::isfinite(r.expected)) theory = formatNumber(r.expected, r.decimals);

        const float labelX = left + (r.colour != 0 ? 13.0f : 0.0f);
        if (r.colour != 0) {
            canvas_.begin();
            canvas_.disc({left + 3.5f, cursor + 8.5f}, 3.5f, gfx::uiHex(r.colour));
            canvas_.end();
        }
        const float measuredW = ui_.textWidth(Face::Mono, measured.c_str());
        // Long labels lose their end rather than run into the numbers.
        std::string label = r.label;
        const float room = measuredRight - measuredW - 10.0f - labelX;
        if (ui_.textWidth(Face::Body, label.c_str()) > room) {
            while (!label.empty() && ui_.textWidth(Face::Body, (label + "…").c_str()) > room) {
                do label.pop_back();
                while (!label.empty() && (static_cast<unsigned char>(label.back()) & 0xC0) == 0x80);
                if (!label.empty()) label.pop_back();
            }
            label += "…";
        }
        ui_.text(Face::Body, label.c_str(), labelX, cursor, kInkSoft);
        ui_.text(Face::Mono, measured.c_str(), measuredRight - measuredW, cursor + 1.5f, kInk);
        if (!theory.empty()) {
            ui_.text(Face::Mono, theory.c_str(), right - ui_.textWidth(Face::Mono, theory.c_str()), cursor + 1.5f, kInkDim);
        }
        cursor += 20.0f;
    }

    if (scene_.plot.active()) drawPlot({left, cursor + 8.0f, right - left, 22.0f + plotHeight()});
}

void App::drawPlot(Box2 box) {
    const scene::Plot& plot = scene_.plot;
    ui_.text(Face::Caption, plot.title.c_str(), box.x, box.y, kInkDim);
    if (plot.series.size() > 1) {
        float lx = box.x + box.w;
        for (auto it = plot.series.rbegin(); it != plot.series.rend(); ++it) {
            const float tw = ui_.textWidth(Face::Caption, it->name.c_str());
            lx -= tw;
            ui_.text(Face::Caption, it->name.c_str(), lx, box.y, kInkDim);
            canvas_.begin();
            canvas_.disc({lx - 7.0f, box.y + 7.5f}, 3.0f, gfx::uiHex(it->colour));
            canvas_.end();
            lx -= 18.0f;
        }
    }
    const Box2 area{box.x, box.y + 22.0f, box.w, box.h - 22.0f};
    canvas_.begin();
    canvas_.roundedRect(area.x, area.y, area.w, area.h, 8.0f, gfx::uiHex(0xFFFFFF, 0.035f));
    canvas_.end();
    if (plot.times.empty()) return;

    const auto transform = [&](float v) { return plot.log ? std::log10(std::max(v, 1e-12f)) : v; };
    const float end = plot.times.back();
    const float start = end - plot.span;

    // Fixed range, or one that fits what is on screen, eased so it doesn't twitch.
    float lo = plot.lo, hi = plot.hi;
    if (!(lo < hi)) {
        lo = 1e30f;
        hi = -1e30f;
        for (const auto& s : plot.series) {
            for (size_t i = 0; i < s.values.size() && i < plot.times.size(); ++i) {
                if (plot.times[i] < start) continue;
                const float v = transform(s.values[i]);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        }
        if (plot.includeZero && !plot.log) {
            lo = std::min(lo, 0.0f);
            hi = std::max(hi, 0.0f);
        }
        if (hi - lo < 1e-4f) {
            lo -= 1.0f;
            hi += 1.0f;
        }
        const float pad = 0.1f * (hi - lo);
        lo -= pad;
        hi += pad;
        if (!(plotLo_ < plotHi_)) {
            plotLo_ = lo;
            plotHi_ = hi;
        }
        const float ease = 1.0f - std::exp(-frameDt_ * 5.0f);
        plotLo_ += (lo - plotLo_) * ease;
        plotHi_ += (hi - plotHi_) * ease;
        lo = plotLo_;
        hi = plotHi_;
    }
    const auto toY = [&](float v) { return area.y + area.h - 6.0f - (v - lo) / (hi - lo) * (area.h - 12.0f); };

    // Grid lines and their values. Counted in whole steps, so rounding can't stall the loop.
    const float divisions = std::max(2.0f, area.h / 24.0f);
    const float step = plot.log ? std::max(1.0f, std::round((hi - lo) / divisions)) : niceStep((hi - lo) / divisions);
    const float firstTick = std::ceil(lo / step);
    const int ticks = std::clamp(static_cast<int>(std::floor(hi / step) - firstTick) + 1, 0, 12);
    canvas_.begin();
    for (int i = 0; i < ticks; ++i) {
        const float t = (firstTick + static_cast<float>(i)) * step;
        const float gy = toY(t);
        if (gy < area.y + 2.0f || gy > area.y + area.h - 2.0f) continue;
        const bool zero = !plot.log && std::abs(t) < step * 0.01f;
        canvas_.line({area.x + 4.0f, gy}, {area.x + area.w - 4.0f, gy}, 0.5f, gfx::uiHex(0xFFFFFF, zero ? 0.18f : 0.07f), 0.6f);
    }
    canvas_.end();
    for (int i = 0; i < ticks; ++i) {
        const float t = (firstTick + static_cast<float>(i)) * step;
        const float gy = toY(t);
        if (gy < area.y + 12.0f || gy > area.y + area.h - 2.0f) continue;
        const std::string label = plot.log ? powerLabel(static_cast<int>(std::lround(t)))
                                           : formatNumber(t, std::max(0, -static_cast<int>(std::floor(std::log10(step)))));
        ui_.text(Face::Caption, label.c_str(), area.x + 6.0f, gy - 14.0f, kInkFaint);
    }

    // A tick each second along the bottom (every five on long plots), scrolling with the data.
    const float tick = plot.span > 20.0f ? 5.0f : 1.0f;
    const float firstSecond = std::ceil(start / tick);
    canvas_.begin();
    for (int i = 0; i <= static_cast<int>(plot.span / tick); ++i) {
        const float t = (firstSecond + static_cast<float>(i)) * tick;
        if (t > end) break;
        const float tx = area.x + (t - start) / plot.span * area.w;
        canvas_.line({tx, area.y + area.h - 4.0f}, {tx, area.y + area.h - 1.0f}, 0.6f, gfx::uiHex(0xFFFFFF, 0.25f), 0.6f);
    }
    canvas_.end();

    // The lines themselves, newest on the right.
    std::vector<Vec2> points;
    canvas_.begin();
    for (const auto& s : plot.series) {
        points.clear();
        for (size_t i = 0; i < s.values.size() && i < plot.times.size(); ++i) {
            if (plot.times[i] < start) continue;
            const float px = area.x + (plot.times[i] - start) / plot.span * area.w;
            const float py = std::clamp(toY(transform(s.values[i])), area.y + 1.0f, area.y + area.h - 1.0f);
            points.push_back({px, py});
        }
        const Col colour = gfx::uiHex(s.colour);
        if (points.size() >= 2) canvas_.polyline(points.data(), static_cast<int>(points.size()), 1.3f, colour, 0.8f);
        if (!points.empty()) canvas_.disc(points.back(), 2.6f, colour);
    }
    canvas_.end();

}

void App::drawInspector(float x, float y) {
    const Body* b = selectedBody();
    constexpr float kWidth = 226.0f;
    if (b == nullptr) {
        if (tool_ != Tool::Inspect) return;
        const char* hint = "Click any moving body to read its mass, speed, momentum and energy.";
        const float h = 16.0f + ui_.paragraph(Face::Caption, hint, 0.0f, 0.0f, kWidth - 32.0f, kInk, 1.45f, false) + 14.0f;
        ui_.panel({x, y, kWidth, h}, 12.0f);
        ui_.paragraph(Face::Caption, hint, x + 16.0f, y + 13.0f, kWidth - 32.0f, kInkDim, 1.45f);
        return;
    }

    const float g = phys::length(scene_.world.gravity);
    const float speed = phys::length(b->vel);
    struct Row {
        const char* label;
        std::string value;
    };
    char buffer[64];
    const auto number = [&](const char* pattern, float a, float c = 0.0f) {
        std::snprintf(buffer, sizeof buffer, pattern, static_cast<double>(a), static_cast<double>(c));
        return std::string(buffer);
    };
    std::vector<Row> rows = {
        {"Mass m", number("%.2f kg", b->mass)},
        {"Speed v", number("%.2f m/s", speed)},
        {"Velocity", number("(%.2f, %.2f)", b->vel.x, b->vel.y)},
        {"Momentum mv", number("%.2f kg·m/s", b->mass * speed)},
        {"Kinetic ½mv²", number("%.2f J", 0.5f * b->mass * speed * speed)},
        {"Spin ω", number("%.2f rad/s", b->angVel)},
    };
    if (g > 0.0f) {
        rows.push_back({"Height y", number("%.2f m", b->pos.y)});
        rows.push_back({"Potential mgy", number("%.2f J", b->mass * g * b->pos.y)});
    }

    const float h = 16.0f + 21.0f + static_cast<float>(rows.size()) * 19.0f + 10.0f;
    ui_.panel({x, y, kWidth, h}, 12.0f);
    ui_.text(Face::Small, "SELECTED BODY", x + 16.0f, y + 15.0f, kInkFaint);
    float cursor = y + 37.0f;
    for (const Row& row : rows) {
        ui_.text(Face::Body, row.label, x + 16.0f, cursor, kInkSoft);
        ui_.text(Face::Mono, row.value.c_str(), x + kWidth - 16.0f - ui_.textWidth(Face::Mono, row.value.c_str()), cursor + 1.5f, kInk);
        cursor += 19.0f;
    }
}

void App::drawDock(float screenW, float screenH) {
    constexpr float kButton = 40.0f;
    constexpr float kGap = 3.0f;
    constexpr float kPad = 6.0f;
    struct ToolItem {
        Icon icon;
        Tool tool;
        const char* tip;
    };
    const ToolItem tools[3] = {{Icon::Inspect, Tool::Inspect, "Inspect   Q"},
                               {Icon::Grab, Tool::Grab, "Grab   W"},
                               {Icon::Throw, Tool::Throw, "Throw   E"}};

    const float dockW = kPad * 2.0f + kButton * 9.0f + kGap * 6.0f + 22.0f;
    const float stageCentre = kListWidth + (screenW - kListWidth - kLessonWidth) * 0.5f;
    const float dockX = stageCentre - dockW * 0.5f;
    const float dockY = screenH - kButton - kPad * 2.0f - kMargin;
    ui_.panel({dockX, dockY, dockW, kButton + kPad * 2.0f}, 16.0f);

    float x = dockX + kPad;
    const float y = dockY + kPad;
    for (const ToolItem& item : tools) {
        const auto id = reinterpret_cast<const void*>(static_cast<size_t>(item.tool) + 1);
        if (ui_.iconButton(id, {x, y, kButton, kButton}, item.icon, tool_ == item.tool, item.tip)) tool_ = item.tool;
        x += kButton + kGap;
    }
    ui_.divider(x + 4.0f, y + 9.0f, kButton - 18.0f);
    x += 11.0f;
    if (ui_.iconButton(&paused_, {x, y, kButton, kButton}, paused_ ? Icon::Play : Icon::Pause, false,
                       paused_ ? "Play   Space" : "Pause   Space")) {
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
    ui_.divider(x - 4.0f, y + 9.0f, kButton - 18.0f);
    if (ui_.iconButton(&slowMotion_, {x, y, kButton, kButton}, Icon::Slow, slowMotion_, "Slow motion   S")) {
        slowMotion_ = !slowMotion_;
    }
    x += kButton + kGap;
    if (ui_.iconButton(&vectorsOn_, {x, y, kButton, kButton}, Icon::Vectors, vectorsOn_, "Vectors   V")) {
        vectorsOn_ = !vectorsOn_;
    }
    x += kButton + kGap;
    const Box2 settingsButton{x, y, kButton, kButton};
    if (ui_.iconButton(&showSettings_, settingsButton, Icon::Settings, showSettings_, "Settings   Tab")) {
        showSettings_ = !showSettings_;
    }
    if (showSettings_) drawSettings({dockX, dockY, dockW, kButton + kPad * 2.0f});
}

void App::drawSettings(Box2 dock) {
    constexpr float kWidth = 264.0f;
    constexpr float kHeight = 392.0f;
    const float px = dock.x + dock.w - kWidth;
    const float py = std::max(dock.y - kHeight - 10.0f, 8.0f);
    ui_.panel({px, py, kWidth, kHeight}, 16.0f);
    const float left = px + 18.0f;
    const float width = kWidth - 36.0f;
    float cursor = py + 16.0f;
    char value[48];

    if (phys::length(scene_.baseGravity) > 0.0f) {
        std::snprintf(value, sizeof value, "%.1f m/s²", static_cast<double>(gravity_));
        if (ui_.slider(&gravity_, {left, cursor, width, 40.0f}, "Gravity g", value, gravity_, 0.0f, 25.0f)) {
            gravity_ = std::round(gravity_ * 10.0f) / 10.0f;
            applyGravity();
        }
        cursor += 42.0f;
        // Real places to try. The lessons start at 10, Earth's 9.81 rounded.
        const struct {
            const char* name;
            float g;
        } places[4] = {{"Moon", 1.6f}, {"Mars", 3.7f}, {"Earth", 9.8f}, {"Jupiter", 24.8f}};
        float lx = left;
        for (const auto& place : places) {
            if (ui_.link(&place, lx, cursor, place.name)) {
                gravity_ = place.g;
                applyGravity();
            }
            lx += ui_.textWidth(Face::Caption, place.name) + 16.0f;
        }
        cursor += 26.0f;
    } else {
        ui_.text(Face::Label, "Gravity g", left, cursor, kInkDim);
        ui_.paragraph(Face::Caption, "This lesson has no uniform gravity.", left, cursor + 20.0f, width, kInkFaint);
        cursor += 68.0f;
    }

    std::snprintf(value, sizeof value, "%.2f×", static_cast<double>(timeScale_));
    if (ui_.slider(&timeScale_, {left, cursor, width, 40.0f}, "Time", value, timeScale_, 0.05f, 2.0f)) {
        if (std::abs(timeScale_ - 1.0f) < 0.03f) timeScale_ = 1.0f;
    }
    cursor += 46.0f;

    std::snprintf(value, sizeof value, "%d", static_cast<int>(std::round(substepSetting_)));
    if (ui_.slider(&substepSetting_, {left, cursor, width, 40.0f}, "Solver substeps", value, substepSetting_, 1.0f, 20.0f)) {
        scene_.world.substeps = static_cast<int>(std::round(substepSetting_));
    }
    cursor += 50.0f;

    constexpr float kToggle = 27.0f;
    ui_.toggle(&trailsOn_, {left, cursor, width, kToggle}, "Trails", "T", trailsOn_);
    cursor += kToggle;
    ui_.toggle(&bloomOn_, {left, cursor, width, kToggle}, "Bloom", "B", bloomOn_);
    cursor += kToggle;
    ui_.toggle(&sparksOn_, {left, cursor, width, kToggle}, "Sparks on impacts", "", sparksOn_);
    cursor += kToggle;
    ui_.toggle(&heatView_, {left, cursor, width, kToggle}, "Colour by speed", "K", heatView_);
    cursor += kToggle;
    ui_.toggle(&debugOn_, {left, cursor, width, kToggle}, "Contact points", "C", debugOn_);
    cursor += kToggle + 12.0f;

    const phys::WorldStats& s = scene_.world.stats();
    std::snprintf(value, sizeof value, "%.0f fps · %d bodies · %.1f ms", static_cast<double>(statFps_), s.bodies,
                  static_cast<double>(statPhysicsMs_));
    ui_.text(Face::Mono, value, left, cursor, kInkFaint);
}
