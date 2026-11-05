// The frame's journey: scene into a float target, bloom chain, tone-mapped composite.
#pragma once

#include <array>

#include "color.hpp"
#include "raylib.h"
#include "view.hpp"

namespace gfx {

struct BackdropParams {
    View view;
    float time = 0.0f;
    Col top, bottom, glow;
};

struct CompositeParams {
    float bloomAmount = 0.1f;
    float fade = 1.0f;
    float vignette = 0.4f;
    float time = 0.0f;
};

class Pipeline {
public:
    void load(int width, int height);
    void unload();
    void resize(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    void beginScene();
    void endScene();
    void drawBackdrop(const BackdropParams& params);

    // Emissive layer: additive blending, with colours scaled past 1.0.
    void beginEmissive(float boost);
    void endEmissive();

    // Builds the blur chain from the finished scene. Call outside BeginDrawing.
    void buildBloom();

    // Draws the final image to the current target, sized in UI points.
    void present(const CompositeParams& params, float pointsWide, float pointsHigh);

    // A frosted panel. Coordinates are in UI points; `dpi` is window pixels per point.
    void frostedPanel(float x, float y, float w, float h, float radius, float dpi, float pointsWide, float pointsHigh,
                      Col tint, float tintAmount);

private:
    static constexpr int kMips = 6;

    void createTargets();
    void destroyTargets();
    float bloomScale() const { return 1.0f / static_cast<float>(kMips); }

    int width_ = 0;
    int height_ = 0;
    RenderTexture2D scene_{};
    std::array<RenderTexture2D, kMips> mips_{};

    Shader backdrop_{}, boost_{}, down_{}, up_{}, composite_{}, frost_{};

    int backdropResolution_ = -1, backdropCamera_ = -1, backdropScale_ = -1, backdropTime_ = -1;
    int backdropTop_ = -1, backdropBottom_ = -1, backdropGlow_ = -1;
    int boostAmount_ = -1, downTexel_ = -1, upTexel_ = -1;
    int compositeBloom_ = -1, compositeAmount_ = -1, compositeScale_ = -1, compositeFade_ = -1;
    int compositeVignette_ = -1, compositeTime_ = -1;
    int frostRect_ = -1, frostRadius_ = -1, frostScale_ = -1, frostTint_ = -1, frostAmount_ = -1, frostPixel_ = -1;
};

}  // namespace gfx
