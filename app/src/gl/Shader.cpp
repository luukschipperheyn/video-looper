#include "gl/Shader.hpp"

#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>

ShaderProgram::ShaderProgram() : programId(0) {}

ShaderProgram::~ShaderProgram() {
	destroy();
}

void ShaderProgram::destroy() {
	if (programId != 0) {
		glDeleteProgram(programId);
		programId = 0;
	}
}

std::string ShaderProgram::readFile(const std::string& path) {
	std::ifstream file(path, std::ios::in | std::ios::binary);
	if (!file) {
		return {};
	}
	std::ostringstream ss;
	ss << file.rdbuf();
	return ss.str();
}

GLuint ShaderProgram::compile(GLenum type, const std::string& source) {
	GLuint sh = glCreateShader(type);
	const char* src = source.c_str();
	glShaderSource(sh, 1, &src, nullptr);
	glCompileShader(sh);
	GLint ok = GL_FALSE;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		GLint len = 0;
		glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
		std::vector<char> log(len);
		glGetShaderInfoLog(sh, len, nullptr, log.data());
		std::cerr << "Shader compile error: " << log.data() << "\n";
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

bool ShaderProgram::loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath) {
	destroy();
	std::string vsrc = readFile(vertexPath);
	std::string fsrc = readFile(fragmentPath);
	if (vsrc.empty() || fsrc.empty()) {
		std::cerr << "Failed to read shader files: " << vertexPath << " or " << fragmentPath << "\n";
		return false;
	}
	GLuint vs = compile(GL_VERTEX_SHADER, vsrc);
	GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc);
	if (vs == 0 || fs == 0) {
		if (vs) glDeleteShader(vs);
		if (fs) glDeleteShader(fs);
		return false;
	}
	programId = glCreateProgram();
	glAttachShader(programId, vs);
	glAttachShader(programId, fs);
	glLinkProgram(programId);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = GL_FALSE;
	glGetProgramiv(programId, GL_LINK_STATUS, &ok);
	if (!ok) {
		GLint len = 0;
		glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &len);
		std::vector<char> log(len);
		glGetProgramInfoLog(programId, len, nullptr, log.data());
		std::cerr << "Program link error: " << log.data() << "\n";
		glDeleteProgram(programId);
		programId = 0;
		return false;
	}
	return true;
}

void ShaderProgram::use() const {
	glUseProgram(programId);
}

GLint ShaderProgram::getUniformLocation(const char* name) const {
	return glGetUniformLocation(programId, name);
}

void ShaderProgram::setUniform(const char* name, int value) const {
	glUniform1i(getUniformLocation(name), value);
}

void ShaderProgram::setUniform(const char* name, float value) const {
	glUniform1f(getUniformLocation(name), value);
}

void ShaderProgram::setUniform(const char* name, float x, float y) const {
	glUniform2f(getUniformLocation(name), x, y);
}
