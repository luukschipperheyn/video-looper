#pragma once

#include <OpenGL/gl3.h>
#include <string>

class ShaderProgram {
public:
	ShaderProgram();
	~ShaderProgram();
	ShaderProgram(const ShaderProgram&) = delete;
	ShaderProgram& operator=(const ShaderProgram&) = delete;

	bool loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath);
	void use() const;
	GLuint id() const { return programId; }

	// Uniform helpers
	void setUniform(const char* name, int value) const;
	void setUniform(const char* name, float value) const;
	void setUniform(const char* name, float x, float y) const;

private:
	GLuint compile(GLenum type, const std::string& source);
	std::string readFile(const std::string& path);
	GLint getUniformLocation(const char* name) const;
	void destroy();

	GLuint programId;
};


