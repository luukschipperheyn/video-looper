#version 410 core
out vec4 oColor;
in vec2 vUv;
uniform sampler2D uTex;
void main(){
	oColor = texture(uTex, vUv);
}


