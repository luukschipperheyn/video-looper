#version 410 core
out vec4 oColor;
in vec2 vUv;
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform float uMix; // 0..1
uniform int uMode; // 0: Mix, 1: Add, 2: Multiply, 3: Screen, 4: Overlay, 5: Difference, 6: Lighten, 7: Darken, 8: HardLight, 9: SoftLight, 10: ColorDodge, 11: ColorBurn
// Blend helpers operate on RGB; alpha is set to 1.0
vec3 blendAdd(vec3 a, vec3 b) {
    return clamp(a + b, 0.0, 1.0);
}

vec3 blendMultiply(vec3 a, vec3 b) {
    return a * b;
}

vec3 blendScreen(vec3 a, vec3 b) {
    return 1.0 - (1.0 - a) * (1.0 - b);
}

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

vec3 blendDifference(vec3 a, vec3 b) {
    return abs(a - b);
}

vec3 blendLighten(vec3 a, vec3 b) {
    return max(a, b);
}

vec3 blendDarken(vec3 a, vec3 b) {
    return min(a, b);
}

vec3 blendColorDodge(vec3 a, vec3 b) {
    vec3 denom = max(vec3(1e-5), 1.0 - b);
    return clamp(a / denom, 0.0, 1.0);
}

vec3 blendColorBurn(vec3 a, vec3 b) {
    vec3 denom = max(vec3(1e-5), b);
    return clamp(1.0 - (1.0 - a) / denom, 0.0, 1.0);
}

void main(){
    vec4 base = texture(uTex0, vUv);
    vec4 over = texture(uTex1, vUv);
    float t = clamp(uMix, 0.0, 1.0);

    vec3 result;
    if (uMode == 1) {
        // Additive: interpolate toward additive result
        result = mix(base.rgb, blendAdd(base.rgb, over.rgb), t);
    } else if (uMode == 2) {
        // Multiply
        result = mix(base.rgb, blendMultiply(base.rgb, over.rgb), t);
    } else if (uMode == 3) {
        // Screen
        result = mix(base.rgb, blendScreen(base.rgb, over.rgb), t);
    } else if (uMode == 4) {
        // Overlay
        result = mix(base.rgb, blendOverlay(base.rgb, over.rgb), t);
    } else if (uMode == 5) {
        // Difference
        result = mix(base.rgb, blendDifference(base.rgb, over.rgb), t);
    } else if (uMode == 6) {
        // Lighten
        result = mix(base.rgb, blendLighten(base.rgb, over.rgb), t);
    } else if (uMode == 7) {
        // Darken
        result = mix(base.rgb, blendDarken(base.rgb, over.rgb), t);
    } else if (uMode == 8) {
        // Hard Light
        result = mix(base.rgb, blendHardLight(base.rgb, over.rgb), t);
    } else if (uMode == 9) {
        // Soft Light
        result = mix(base.rgb, blendSoftLight(base.rgb, over.rgb), t);
    } else if (uMode == 10) {
        // Color Dodge
        result = mix(base.rgb, blendColorDodge(base.rgb, over.rgb), t);
    } else if (uMode == 11) {
        // Color Burn
        result = mix(base.rgb, blendColorBurn(base.rgb, over.rgb), t);
    } else {
        // 0: Simple mix (crossfade)
        result = mix(base.rgb, over.rgb, t);
    }

    oColor = vec4(result, 1.0);
}


