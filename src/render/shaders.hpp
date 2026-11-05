// GLSL for the HDR pipeline. Everything targets the OpenGL 3.3 core profile.
#pragma once

namespace gfx::shaders {

// Tone mapping is shared by the final image and the frosted UI panels.
#define INERTIA_TONEMAP R"(
// Khronos PBR Neutral, without its toe. Keeps colours below 0.8 true to the palette and
// only rolls highlights towards white. The toe is dropped because it subtracts the
// smallest channel from dark colours, which turns muted backgrounds lurid.
vec3 tonemap(vec3 color) {
    const float startCompression = 0.76;
    const float desaturation = 0.15;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

vec3 toSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(12.92 * c, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}
)"

inline const char* kBackground = R"(#version 330
out vec4 finalColor;
uniform vec2 uResolution;
uniform vec2 uCamera;
uniform float uPixelsPerMetre;
uniform float uTime;
uniform vec3 uTop;
uniform vec3 uBottom;
uniform vec3 uGlow;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x),
               mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}

float clouds(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; ++i) {
        value += amplitude * noise(p);
        p = p * 2.03 + vec2(13.1, 7.7);
        amplitude *= 0.5;
    }
    return value;
}

void main() {
    vec2 uv = gl_FragCoord.xy / uResolution;
    float aspect = uResolution.x / uResolution.y;

    vec3 color = mix(uBottom, uTop, smoothstep(0.0, 1.0, uv.y));

    // A broad pool of light that sits behind the action.
    vec2 q = (uv - vec2(0.5, 0.58)) * vec2(aspect, 1.0);
    color += uGlow * exp(-dot(q, q) * 2.6) * 0.5;

    // Slow drifting haze, tied loosely to the camera so it reads as far away.
    vec2 w = vec2(uv.x * aspect, uv.y) * 2.2 + uCamera * 0.004;
    float haze = clouds(w + vec2(uTime * 0.012, -uTime * 0.007));
    haze = smoothstep(0.35, 0.8, clouds(w * 0.8 + haze * 1.4));
    color += uGlow * haze * 0.18;

    // Dot grid at two-metre spacing, which fades out as the view zooms away.
    float spacing = 2.0;
    vec2 world = (gl_FragCoord.xy - uResolution * 0.5) / uPixelsPerMetre + uCamera;
    vec2 cell = abs(fract(world / spacing + 0.5) - 0.5) * spacing * uPixelsPerMetre;
    float dotRadius = 1.1;
    float grid = 1.0 - smoothstep(dotRadius - 0.6, dotRadius + 0.6, length(cell));
    float gridFade = smoothstep(6.0, 18.0, uPixelsPerMetre);
    color += (uTop * 1.2 + vec3(0.003)) * grid * gridFade;

    finalColor = vec4(color, 1.0);
}
)";

// Vertex colours are 8-bit, so emissive layers scale them up here to exceed 1.0.
inline const char* kBoost = R"(#version 330
in vec4 fragColor;
out vec4 finalColor;
uniform float uBoost;
void main() {
    finalColor = vec4(fragColor.rgb * uBoost, fragColor.a);
}
)";

// 13-tap downsample from Jimenez, "Next Generation Post Processing in Call of
// Duty: Advanced Warfare". Avoids the shimmering of a plain box filter.
inline const char* kDownsample = R"(#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 uTexel;

vec3 tap(float x, float y) {
    return min(texture(texture0, fragTexCoord + uTexel * vec2(x, y)).rgb, vec3(16.0));
}

void main() {
    vec3 a = tap(-2, 2), b = tap(0, 2), c = tap(2, 2);
    vec3 d = tap(-2, 0), e = tap(0, 0), f = tap(2, 0);
    vec3 g = tap(-2, -2), h = tap(0, -2), i = tap(2, -2);
    vec3 j = tap(-1, 1), k = tap(1, 1), l = tap(-1, -1), m = tap(1, -1);
    vec3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    finalColor = vec4(color, 1.0);
}
)";

// 3x3 tent, the matching upsample. Each level is added on top of the one above it.
inline const char* kUpsample = R"(#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 uTexel;

vec3 tap(float x, float y) {
    return texture(texture0, fragTexCoord + uTexel * vec2(x, y)).rgb;
}

void main() {
    vec3 color = tap(0, 0) * 4.0;
    color += (tap(-1, 0) + tap(1, 0) + tap(0, -1) + tap(0, 1)) * 2.0;
    color += tap(-1, -1) + tap(1, -1) + tap(-1, 1) + tap(1, 1);
    finalColor = vec4(color / 16.0, 1.0);
}
)";

inline const char* kComposite = R"(#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D uBloom;
uniform float uBloomAmount;
uniform float uBloomScale;
uniform float uFade;
uniform float uVignette;
uniform float uTime;
)" INERTIA_TONEMAP R"(
float grain(vec2 p, float t) {
    return fract(sin(dot(p + t, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec3 scene = texture(texture0, fragTexCoord).rgb;
    vec3 bloom = texture(uBloom, fragTexCoord).rgb * uBloomScale;
    vec3 color = scene + bloom * uBloomAmount;

    vec2 c = fragTexCoord - 0.5;
    color *= 1.0 - uVignette * smoothstep(0.25, 0.95, dot(c, c) * 2.2);

    color = toSrgb(tonemap(color * uFade));

    // Dark gradients band badly in 8 bits, and a touch of noise hides it.
    float n = grain(gl_FragCoord.xy, fract(uTime)) + grain(gl_FragCoord.yx, fract(uTime * 1.3)) - 1.0;
    color += n / 255.0;

    finalColor = vec4(color, 1.0);
}
)";

// Frosted glass: the blurred scene behind a panel, darkened and cut to a rounded rectangle.
inline const char* kFrost = R"(#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec4 uRect;      // x, y, w, h in framebuffer pixels, origin bottom-left
uniform float uRadius;   // framebuffer pixels
uniform float uBloomScale;
uniform vec3 uTint;
uniform float uTintAmount;
uniform float uPixel;    // framebuffer pixels per UI point
)" INERTIA_TONEMAP R"(
void main() {
    vec2 half_ = uRect.zw * 0.5;
    vec2 p = gl_FragCoord.xy - (uRect.xy + half_);
    vec2 q = abs(p) - half_ + uRadius;
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - uRadius;
    float alpha = 1.0 - smoothstep(-0.75, 0.75, d);

    vec3 blurred = texture(texture0, fragTexCoord).rgb * uBloomScale;
    vec3 color = toSrgb(tonemap(blurred * 1.2));
    color = mix(color, uTint, uTintAmount);

    // A thin lit edge, brighter along the top, gives the panel some thickness.
    float edge = 1.0 - smoothstep(0.0, 1.1 * uPixel, abs(d + 0.6 * uPixel));
    float top = 0.5 + 0.5 * clamp(p.y / max(half_.y, 1.0), -1.0, 1.0);
    color += edge * (0.04 + 0.14 * top * top);

    finalColor = vec4(color, alpha);
}
)";

}  // namespace gfx::shaders
