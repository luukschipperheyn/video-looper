#version 410 core
out vec4 oColor;
in vec2 vUv;

uniform sampler2D uTex0;   // ring front idx0
uniform sampler2D uTex1;   // ring front idx1
uniform float uBaseMix;    // mix factor for current read (mixf * gain)
uniform sampler2D uPrev;   // previous post-processed output

uniform float uDecay;      // 0..1 fade of previous frame
uniform float uMix;        // 0..1 how much previous contributes
uniform float uZoom;       // small zoom per frame (e.g. 0.001)
uniform float uRotate;     // radians per frame
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
    centered = rot(centered, uRotate);
    centered *= 1.0 + uZoom;
    vec2 uvPrev = centered + 0.5 + uOffset;

    // NOTE: set uPrev texture wrap to REPEAT
    vec3 prev = texture(uPrev, uvPrev).rgb * uDecay;

    vec3 outc = mix(base, prev, clamp(uMix, 0.0, 1.0));
    oColor = vec4(outc, 1.0);
}


