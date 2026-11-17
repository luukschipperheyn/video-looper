#pragma once

#include <OpenGL/gl3.h>

class FullscreenQuad {
public:
	FullscreenQuad();
	~FullscreenQuad();
	FullscreenQuad(const FullscreenQuad&) = delete;
	FullscreenQuad& operator=(const FullscreenQuad&) = delete;

	void draw() const;

private:
	GLuint vao;
	GLuint vbo;
};


