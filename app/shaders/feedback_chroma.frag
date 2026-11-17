#version 410 core
out vec4 oColor;
in vec2 vUv;

uniform sampler2D uTex0;   // ring front idx0
uniform sampler2D uTex1;   // ring front idx1
uniform float uBaseMix;    // mix factor for current read (mixf * gain)
uniform sampler2D uPrev;   // previous post-processed output

uniform float uDecay;      // 0..1 fade of previous frame
uniform float uChroma;     // 0..1 chromatic amount (also used as mix)
uniform float uZoom;       // small zoom per frame
uniform float uRotate;     // unused here
uniform vec2  uOffset;     // subtle drift

vec2 rot(vec2 p, float a){
    float c = cos(a), s = sin(a);
    return mat2(c,-s,s,c)*p;
}

void main(){
    vec3 a = texture(uTex0, vUv).rgb;
    vec3 b = texture(uTex1, vUv).rgb;
    vec3 base = mix(a, b, clamp(uBaseMix, 0.0, 1.0));

    vec2 centered = vUv - 0.5;
    centered = rot(centered, 0.0);
    centered *= 1.0 + uZoom;
    vec2 uvPrev = centered + 0.5 + uOffset;

    float shift = uChroma * 0.01; // UV shift magnitude
    float d = clamp(uDecay, 0.0, 1.0);
    float m = clamp(uChroma, 0.0, 1.0);
    vec3 prev;
    prev.r = texture(uPrev, uvPrev + vec2( shift, 0.0)).r;
    prev.g = texture(uPrev, uvPrev).g;
    prev.b = texture(uPrev, uvPrev + vec2(-shift, 0.0)).b;
    prev *= d;

    vec3 outc = mix(base, prev, m);
    oColor = vec4(outc, 1.0);
}


