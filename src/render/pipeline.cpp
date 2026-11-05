#include "pipeline.hpp"

#include <algorithm>

#include "rlgl.h"
#include "shaders.hpp"

namespace gfx {
namespace {

// raylib's own render textures are 8-bit, which would clip every highlight.
RenderTexture2D makeFloatTarget(int width, int height) {
    RenderTexture2D target{};
    target.id = rlLoadFramebuffer();
    rlEnableFramebuffer(target.id);

    target.texture.id = rlLoadTexture(nullptr, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
    target.texture.width = width;
    target.texture.height = height;
    target.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    target.texture.mipmaps = 1;

    rlFramebufferAttach(target.id, target.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    if (!rlFramebufferComplete(target.id)) TraceLog(LOG_ERROR, "Float render target is incomplete");
    rlDisableFramebuffer();

    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
    return target;
}

// Render textures are stored upside down, hence the negative source height.
void blit(const Texture2D& source, float width, float height) {
    const Rectangle src{0.0f, 0.0f, static_cast<float>(source.width), -static_cast<float>(source.height)};
    DrawTexturePro(source, src, {0.0f, 0.0f, width, height}, {0.0f, 0.0f}, 0.0f, WHITE);
}

void setVec3(Shader shader, int location, const Col& c) {
    const float v[3] = {c.r, c.g, c.b};
    SetShaderValue(shader, location, v, SHADER_UNIFORM_VEC3);
}

}  // namespace

void Pipeline::load(int width, int height) {
    backdrop_ = LoadShaderFromMemory(nullptr, shaders::kBackground);
    boost_ = LoadShaderFromMemory(nullptr, shaders::kBoost);
    down_ = LoadShaderFromMemory(nullptr, shaders::kDownsample);
    up_ = LoadShaderFromMemory(nullptr, shaders::kUpsample);
    composite_ = LoadShaderFromMemory(nullptr, shaders::kComposite);
    frost_ = LoadShaderFromMemory(nullptr, shaders::kFrost);

    backdropResolution_ = GetShaderLocation(backdrop_, "uResolution");
    backdropCamera_ = GetShaderLocation(backdrop_, "uCamera");
    backdropScale_ = GetShaderLocation(backdrop_, "uPixelsPerMetre");
    backdropTime_ = GetShaderLocation(backdrop_, "uTime");
    backdropTop_ = GetShaderLocation(backdrop_, "uTop");
    backdropBottom_ = GetShaderLocation(backdrop_, "uBottom");
    backdropGlow_ = GetShaderLocation(backdrop_, "uGlow");
    boostAmount_ = GetShaderLocation(boost_, "uBoost");
    downTexel_ = GetShaderLocation(down_, "uTexel");
    upTexel_ = GetShaderLocation(up_, "uTexel");
    compositeBloom_ = GetShaderLocation(composite_, "uBloom");
    compositeAmount_ = GetShaderLocation(composite_, "uBloomAmount");
    compositeScale_ = GetShaderLocation(composite_, "uBloomScale");
    compositeFade_ = GetShaderLocation(composite_, "uFade");
    compositeVignette_ = GetShaderLocation(composite_, "uVignette");
    compositeTime_ = GetShaderLocation(composite_, "uTime");
    frostRect_ = GetShaderLocation(frost_, "uRect");
    frostRadius_ = GetShaderLocation(frost_, "uRadius");
    frostScale_ = GetShaderLocation(frost_, "uBloomScale");
    frostTint_ = GetShaderLocation(frost_, "uTint");
    frostAmount_ = GetShaderLocation(frost_, "uTintAmount");
    frostPixel_ = GetShaderLocation(frost_, "uPixel");

    width_ = width;
    height_ = height;
    createTargets();
}

void Pipeline::unload() {
    destroyTargets();
    UnloadShader(backdrop_);
    UnloadShader(boost_);
    UnloadShader(down_);
    UnloadShader(up_);
    UnloadShader(composite_);
    UnloadShader(frost_);
}

void Pipeline::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    if (width <= 0 || height <= 0) return;
    destroyTargets();
    width_ = width;
    height_ = height;
    createTargets();
}

void Pipeline::createTargets() {
    scene_ = makeFloatTarget(width_, height_);
    int w = width_, h = height_;
    for (RenderTexture2D& mip : mips_) {
        w = std::max(w / 2, 2);
        h = std::max(h / 2, 2);
        mip = makeFloatTarget(w, h);
    }
}

void Pipeline::destroyTargets() {
    UnloadRenderTexture(scene_);
    for (RenderTexture2D& mip : mips_) UnloadRenderTexture(mip);
}

void Pipeline::beginScene() {
    BeginTextureMode(scene_);
    ClearBackground(BLACK);
}

void Pipeline::endScene() { EndTextureMode(); }

void Pipeline::drawBackdrop(const BackdropParams& p) {
    BeginShaderMode(backdrop_);
    const float resolution[2] = {static_cast<float>(width_), static_cast<float>(height_)};
    // The world point at the middle of the screen, which the panel offset moves off `centre`.
    const Vec2 middle = p.view.toWorld({static_cast<float>(width_) * 0.5f, static_cast<float>(height_) * 0.5f});
    const float camera[2] = {middle.x, middle.y};
    SetShaderValue(backdrop_, backdropResolution_, resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(backdrop_, backdropCamera_, camera, SHADER_UNIFORM_VEC2);
    SetShaderValue(backdrop_, backdropScale_, &p.view.pixelsPerMetre, SHADER_UNIFORM_FLOAT);
    SetShaderValue(backdrop_, backdropTime_, &p.time, SHADER_UNIFORM_FLOAT);
    setVec3(backdrop_, backdropTop_, p.top);
    setVec3(backdrop_, backdropBottom_, p.bottom);
    setVec3(backdrop_, backdropGlow_, p.glow);
    DrawRectangle(0, 0, width_, height_, WHITE);
    EndShaderMode();
}

void Pipeline::beginEmissive(float boost) {
    BeginBlendMode(BLEND_ADDITIVE);
    BeginShaderMode(boost_);
    SetShaderValue(boost_, boostAmount_, &boost, SHADER_UNIFORM_FLOAT);
}

void Pipeline::endEmissive() {
    EndShaderMode();
    EndBlendMode();
}

void Pipeline::buildBloom() {
    const Texture2D* source = &scene_.texture;
    for (RenderTexture2D& mip : mips_) {
        BeginTextureMode(mip);
        BeginShaderMode(down_);
        const float texel[2] = {1.0f / static_cast<float>(source->width), 1.0f / static_cast<float>(source->height)};
        SetShaderValue(down_, downTexel_, texel, SHADER_UNIFORM_VEC2);
        blit(*source, static_cast<float>(mip.texture.width), static_cast<float>(mip.texture.height));
        EndShaderMode();
        EndTextureMode();
        source = &mip.texture;
    }

    for (int i = kMips - 1; i > 0; --i) {
        const Texture2D& small = mips_[i].texture;
        RenderTexture2D& large = mips_[i - 1];
        BeginTextureMode(large);
        BeginBlendMode(BLEND_ADDITIVE);
        BeginShaderMode(up_);
        const float texel[2] = {1.0f / static_cast<float>(small.width), 1.0f / static_cast<float>(small.height)};
        SetShaderValue(up_, upTexel_, texel, SHADER_UNIFORM_VEC2);
        blit(small, static_cast<float>(large.texture.width), static_cast<float>(large.texture.height));
        EndShaderMode();
        EndBlendMode();
        EndTextureMode();
    }
}

void Pipeline::present(const CompositeParams& p, float pointsWide, float pointsHigh) {
    BeginShaderMode(composite_);
    const float scale = bloomScale();
    SetShaderValueTexture(composite_, compositeBloom_, mips_[0].texture);
    SetShaderValue(composite_, compositeAmount_, &p.bloomAmount, SHADER_UNIFORM_FLOAT);
    SetShaderValue(composite_, compositeScale_, &scale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(composite_, compositeFade_, &p.fade, SHADER_UNIFORM_FLOAT);
    SetShaderValue(composite_, compositeVignette_, &p.vignette, SHADER_UNIFORM_FLOAT);
    SetShaderValue(composite_, compositeTime_, &p.time, SHADER_UNIFORM_FLOAT);
    blit(scene_.texture, pointsWide, pointsHigh);
    EndShaderMode();
}

void Pipeline::frostedPanel(float x, float y, float w, float h, float radius, float dpi, float pointsWide,
                            float pointsHigh, Col tint, float tintAmount) {
    const Texture2D& blur = mips_[1].texture;

    BeginShaderMode(frost_);
    const float rect[4] = {x * dpi, (pointsHigh - y - h) * dpi, w * dpi, h * dpi};
    const float radiusPixels = radius * dpi;
    const float scale = bloomScale() * 1.6f;
    SetShaderValue(frost_, frostRect_, rect, SHADER_UNIFORM_VEC4);
    SetShaderValue(frost_, frostRadius_, &radiusPixels, SHADER_UNIFORM_FLOAT);
    SetShaderValue(frost_, frostScale_, &scale, SHADER_UNIFORM_FLOAT);
    setVec3(frost_, frostTint_, tint);
    SetShaderValue(frost_, frostAmount_, &tintAmount, SHADER_UNIFORM_FLOAT);
    SetShaderValue(frost_, frostPixel_, &dpi, SHADER_UNIFORM_FLOAT);

    // Sample the same region of the screen from the blurred texture.
    const float fx = x / pointsWide;
    const float fy = y / pointsHigh;
    const float fw = w / pointsWide;
    const float fh = h / pointsHigh;
    const float tw = static_cast<float>(blur.width);
    const float th = static_cast<float>(blur.height);
    // Render textures have v = 1 at the top of the screen, and the negative height flips to match.
    const Rectangle src{fx * tw, (1.0f - fy - fh) * th, fw * tw, -fh * th};
    DrawTexturePro(blur, src, {x, y, w, h}, {0.0f, 0.0f}, 0.0f, WHITE);
    EndShaderMode();
}

}  // namespace gfx
