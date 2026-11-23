#version 410 core
out vec4 oColor;
in vec2 vUv;

uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform float uBaseMix;
uniform vec2 uStep;    // UV step from app/src/util/Repeat.hpp
uniform vec2 uStep2;   // perpendicular step; used when uGrid is 1
uniform float uCount;  // extra copies; the original is sample 0. Grid: per axis, max 6
uniform int uBlend;    // same modes as an Overdub
uniform int uGrid;     // 0 = one axis, 1 = both axes

vec3 tap(vec2 uv) {
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return vec3(0.0);
    vec3 a = texture(uTex0, uv).rgb;
    vec3 b = texture(uTex1, uv).rgb;
    return mix(a, b, clamp(uBaseMix, 0.0, 1.0));
}

float coverOf(vec3 c) {
    return max(c.r, max(c.g, c.b));
}

vec3 blendAdd(vec3 a, vec3 b) { return clamp(a + b, 0.0, 1.0); }
vec3 blendMultiply(vec3 a, vec3 b) { return a * b; }
vec3 blendScreen(vec3 a, vec3 b) { return 1.0 - (1.0 - a) * (1.0 - b); }
vec3 blendOverlay(vec3 a, vec3 b) {
    vec3 lt = 2.0 * a * b;
    vec3 gt = 1.0 - 2.0 * (1.0 - a) * (1.0 - b);
    return mix(lt, gt, step(0.5, a));
}
vec3 blendHardLight(vec3 a, vec3 b) {
    vec3 lt = 2.0 * a * b;
    vec3 gt = 1.0 - 2.0 * (1.0 - a) * (1.0 - b);
    return mix(lt, gt, step(0.5, b));
}
vec3 softlightG(vec3 a) {
    vec3 g1 = ((16.0 * a - 12.0) * a + 4.0) * a;
    vec3 g2 = sqrt(a);
    return mix(g1, g2, step(0.25, a));
}
vec3 blendSoftLight(vec3 a, vec3 b) {
    vec3 d = a - (1.0 - 2.0 * b) * a * (1.0 - a);
    vec3 e = a + (2.0 * b - 1.0) * (softlightG(a) - a);
    return mix(d, e, step(0.5, b));
}
vec3 blendDifference(vec3 a, vec3 b) { return abs(a - b); }
vec3 blendLighten(vec3 a, vec3 b) { return max(a, b); }
vec3 blendDarken(vec3 a, vec3 b) { return min(a, b); }
vec3 blendColorDodge(vec3 a, vec3 b) { return clamp(a / max(vec3(1e-5), 1.0 - b), 0.0, 1.0); }
vec3 blendColorBurn(vec3 a, vec3 b) { return clamp(1.0 - (1.0 - a) / max(vec3(1e-5), b), 0.0, 1.0); }

vec3 blendMode(vec3 base, vec3 over, int mode) {
    if (mode == 1) return blendAdd(base, over);
    if (mode == 2) return blendMultiply(base, over);
    if (mode == 3) return blendScreen(base, over);
    if (mode == 4) return blendOverlay(base, over);
    if (mode == 5) return blendDifference(base, over);
    if (mode == 6) return blendLighten(base, over);
    if (mode == 7) return blendDarken(base, over);
    if (mode == 8) return blendHardLight(base, over);
    if (mode == 9) return blendSoftLight(base, over);
    if (mode == 10) return blendColorDodge(base, over);
    if (mode == 11) return blendColorBurn(base, over);
    return over;
}

// A colored shape on black: empty pixels stay out of the way, and a copy landing on empty black is placed as-is.
vec3 lay(vec3 base, vec3 over, int mode) {
    float cover = coverOf(over);
    if (coverOf(base) < 1.0 / 255.0) return over;
    return mix(base, blendMode(base, over, mode), clamp(cover, 0.0, 1.0));
}

void main() {
    vec3 acc = vec3(0.0);
    if (uGrid == 1) {
        int copies = int(clamp(uCount, 0.0, 6.0) + 0.5);
        for (int j = 0; j <= 6; ++j) {
            if (j <= copies) {
                for (int i = 0; i <= 6; ++i) {
                    if (i <= copies) acc = lay(acc, tap(vUv - uStep * float(i) - uStep2 * float(j)), uBlend);
                }
            }
        }
    } else {
        int copies = int(clamp(uCount, 0.0, 16.0) + 0.5);
        for (int i = 0; i <= 16; ++i) {
            if (i <= copies) acc = lay(acc, tap(vUv - uStep * float(i)), uBlend);
        }
    }
    oColor = vec4(acc, 1.0);
}
