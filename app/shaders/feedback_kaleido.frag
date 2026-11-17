#version 410 core
out vec4 oColor;
in vec2 vUv;

uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform float uBaseMix;
uniform sampler2D uPrev;

uniform float uDecay;
uniform float uSegments;   // segments as float; will round
uniform float uZoom;
uniform float uRotate;
uniform vec2  uOffset;

const float PI = 3.14159265359;

vec2 polarFold(vec2 p, float seg){
    // Convert to polar
    float r = length(p);
    float a = atan(p.y, p.x);
    // fold angle into [0, 2pi/seg]
    float sector = 2.0 * PI / seg;
    a = mod(a, sector);
    float halfSector = 0.5 * sector;
    if (a > halfSector) a = sector - a;
    // back to cartesian
    return vec2(cos(a), sin(a)) * r;
}

vec2 rot2(vec2 p, float a){
    float c = cos(a), s = sin(a);
    return mat2(c,-s,s,c)*p;
}

void main(){
    vec3 a = texture(uTex0, vUv).rgb;
    vec3 b = texture(uTex1, vUv).rgb;
    vec3 base = mix(a, b, clamp(uBaseMix, 0.0, 1.0));

    float seg = max(1.0, floor(uSegments + 0.5));
    vec2 centered = vUv - 0.5;
    centered = rot2(centered, uRotate);
    centered *= 1.0 + uZoom;
    centered = polarFold(centered, seg);
    vec2 uvPrev = centered + 0.5 + uOffset;

    vec3 prev = texture(uPrev, uvPrev).rgb * clamp(uDecay, 0.0, 1.0);
    // Mix amount: use a soft function of segments so higher segments favor prev a bit
    float m = clamp((seg - 1.0) / 8.0, 0.0, 1.0);
    vec3 outc = mix(base, prev, m);
    oColor = vec4(outc, 1.0);
}


