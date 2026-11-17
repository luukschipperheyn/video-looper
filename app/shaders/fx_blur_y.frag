#version 410 core
out vec4 oColor;
in vec2 vUv;

uniform sampler2D uInput;  // input texture to blur
uniform vec2 uTexel;       // 1/width, 1/height
uniform float uRadius;     // 0..1 maps to tap distance

vec3 blur1D(sampler2D t, vec2 uv, vec2 step){
    float w0 = 0.227027;
    float w1 = 0.1945946;
    float w2 = 0.1216216;
    float w3 = 0.054054;
    float w4 = 0.016216;
    vec3 c = texture(t, uv).rgb * w0;
    for (int i=1;i<=4;i++){
        float w = (i==1? w1 : i==2? w2 : i==3? w3 : w4);
        vec2 off = step * float(i);
        c += texture(t, uv + off).rgb * w;
        c += texture(t, uv - off).rgb * w;
    }
    return c;
}

void main(){
    float d = mix(0.0, 4.0, clamp(uRadius, 0.0, 1.0));
    vec2 step = vec2(0.0, uTexel.y * d);
    vec3 c = blur1D(uInput, vUv, step);
    oColor = vec4(c, 1.0);
}


