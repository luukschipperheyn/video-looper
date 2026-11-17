#version 410 core
out vec4 oColor;
in vec2 vUv;
uniform float uTime;
uniform vec2 uResolution;
void main(){
	vec2 uv = vUv;
	float grid = step(0.98, fract(uv.x*10.0)) + step(0.98, fract(uv.y*10.0));
	float wave = 0.5 + 0.5 * sin(10.0*uv.x + uTime*2.0) * cos(10.0*uv.y - uTime*1.7);
	vec3 col = mix(vec3(0.1,0.1,0.12), vec3(0.9,0.9,0.9), wave);
    col += grid * 0.2;
	oColor = vec4(col, 1.0);
}


