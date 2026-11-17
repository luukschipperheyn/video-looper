#version 410 core
out vec4 oColor;
in vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uFrame;
uniform vec2 uCenter;
uniform vec2 uHalf;
uniform float uRot;
uniform float uOutside;

void main() {
	vec2 p = vUv * uFrame - uCenter;
	float c = cos(uRot);
	float s = sin(uRot);
	p = vec2(c * p.x + s * p.y, -s * p.x + c * p.y);
	vec2 halfv = max(uHalf, vec2(1e-4));
	if (abs(p.x) > halfv.x || abs(p.y) > halfv.y) {
		oColor = vec4(0.0, 0.0, 0.0, uOutside);
		return;
	}
	vec2 uv = p / halfv * 0.5 + 0.5;
	oColor = vec4(texture(uTex, uv).rgb, 1.0);
}
