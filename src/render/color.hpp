// Colours. The renderer works in linear light; hex values are read as sRGB.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace gfx {

struct Col {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

inline float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline const std::array<float, 256>& srgbTable() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) t[i] = srgbToLinear(static_cast<float>(i) / 255.0f);
        return t;
    }();
    return table;
}

// sRGB hex to linear light, for anything that goes through the HDR scene.
inline Col linearHex(uint32_t rgb, float alpha = 1.0f) {
    const auto& t = srgbTable();
    return {t[(rgb >> 16) & 0xFF], t[(rgb >> 8) & 0xFF], t[rgb & 0xFF], alpha};
}

// sRGB hex passed through untouched, for the UI drawn after tone mapping.
inline Col uiHex(uint32_t rgb, float alpha = 1.0f) {
    return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
}

inline Col mix(Col a, Col b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

inline Col times(Col c, float k) { return {c.r * k, c.g * k, c.b * k, c.a}; }
inline Col withAlpha(Col c, float a) { return {c.r, c.g, c.b, a}; }

}  // namespace gfx
