#pragma once
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/gl.h>

enum class Status
{
	SUCCESS,
	FAILURE
};

class Shader
{
public:
	Status createShader(std::string filepath, int shaderType)
	{
		m_handle = glCreateShader(shaderType);

		std::ifstream file(filepath);
		if (!file.is_open())
		{
			std::cerr << "FILE: " << filepath << " NOT FOUND";
			return Status::FAILURE;
		}

		std::string source;
		file.seekg(0, std::ios::end);
		source.resize(file.tellg());
		file.seekg(0, std::ios::beg);
		file.read(&source[0], source.size());
		file.close();

		setShaderSource(source);
		return createShader();
	}

	void setShaderSource(const std::string& source)
	{
		m_source = source;
	}
	void cleanup()
	{
		if (m_handle != 0)
			glDeleteShader(m_handle);
	}

	Status createShader()
	{
		const char* sourceCStr = m_source.c_str();
		glShaderSource(m_handle, 1, &sourceCStr, nullptr);

		glCompileShader(m_handle);

		return checkErrors();
	}


	uint32_t getHandle() { return m_handle; }
private:

	Status checkErrors()
	{

		int success = 0;
		glGetShaderiv(m_handle, GL_COMPILE_STATUS, &success);

		if (!success)
		{
			int infoLogLength = 0;
			glGetShaderiv(m_handle, GL_INFO_LOG_LENGTH, &infoLogLength);
			std::vector<char> infoLog(infoLogLength);
			glGetShaderInfoLog(m_handle, infoLogLength, nullptr, infoLog.data());
			std::cout << "Shader compile log:\n" << infoLog.data() << '\n';
			return Status::FAILURE;
		}
		return Status::SUCCESS;
	}

	uint32_t m_handle = 0;
	std::string m_source = "";
};

class ShaderProgramBase
{
public:
	void bind()
	{
		glUseProgram(m_handle);
	}

	void unbind()
	{
		glUseProgram(0);
	}

	uint32_t getHandle() const { return m_handle; }

	void cleanup()
	{
		if (m_handle)
			glDeleteProgram(m_handle);
	}

	~ShaderProgramBase()
	{
		cleanup();
	}

	uint32_t getUniformLoc(const char* name)
	{
		return glGetUniformLocation(m_handle, name);
	}

	void uniform1d(const char* name, double value)
	{
		glProgramUniform1d(m_handle, getUniformLoc(name), value);
	}

	void uniform2d(const char* name, const glm::dvec2& value)
	{
		glProgramUniform2d(m_handle, getUniformLoc(name), value.x, value.y);
	}

	void uniform3d(const char* name, const glm::dvec3& value)
	{
		glProgramUniform3d(m_handle, getUniformLoc(name), value.x, value.y, value.z);
	}

	void uniform4d(const char* name, const glm::dvec4& value)
	{
		glProgramUniform4d(m_handle, getUniformLoc(name), value.x, value.y, value.z, value.w);
	}

	void uniform1f(const char* name, float value)
	{
		glProgramUniform1f(m_handle, getUniformLoc(name), value);
	}

	void uniform2f(const char* name, const glm::vec2& value)
	{
		glProgramUniform2f(m_handle, getUniformLoc(name), value.x, value.y);
	}

	void uniform3f(const char* name, const glm::vec3& value)
	{
		glProgramUniform3f(m_handle, getUniformLoc(name), value.x, value.y, value.z);
	}

	void uniform4f(const char* name, const glm::vec4& value)
	{
		glProgramUniform4f(m_handle, getUniformLoc(name), value.x, value.y, value.z, value.w);
	}

	void uniform1ui(const char* name, uint32_t value)
	{
		glProgramUniform1ui(m_handle, getUniformLoc(name), value);
	}

	void uniform2ui(const char* name, const glm::uvec2& value)
	{
		glProgramUniform2ui(m_handle, getUniformLoc(name), value.x, value.y);
	}

	void uniform3ui(const char* name, const glm::uvec3& value)
	{
		glProgramUniform3ui(m_handle, getUniformLoc(name), value.x, value.y, value.z);
	}

	void uniform4ui(const char* name, const glm::uvec4& value)
	{
		glProgramUniform4ui(m_handle, getUniformLoc(name), value.x, value.y, value.z, value.w);
	}

	void uniform1i(const char* name, int32_t value)
	{
		glProgramUniform1i(m_handle, getUniformLoc(name), value);
	}

	void uniform2i(const char* name, const glm::ivec2& value)
	{
		glProgramUniform2i(m_handle, getUniformLoc(name), value.x, value.y);
	}

	void uniform3i(const char* name, const glm::ivec3& value)
	{
		glProgramUniform3i(m_handle, getUniformLoc(name), value.x, value.y, value.z);
	}

	void uniform4i(const char* name, const glm::ivec4& value)
	{
		glProgramUniform4i(m_handle, getUniformLoc(name), value.x, value.y, value.z, value.w);
	}

	void uniformMat4(const char* name, const glm::mat4& value)
	{
		glProgramUniformMatrix4fv(m_handle, getUniformLoc(name), 1, false, glm::value_ptr(value));
	}

	void uniformTex2D(const char* name, unsigned int texture_id, int tex_slot)
	{
		glActiveTexture(GL_TEXTURE0 + tex_slot);
		glBindTexture(GL_TEXTURE_2D, texture_id);
		glUniform1i(getUniformLoc(name), tex_slot);
	}

	void uniformTex3D(const char* name, unsigned int texture_id, int tex_slot)
	{
		glActiveTexture(GL_TEXTURE0 + tex_slot);
		glBindTexture(GL_TEXTURE_3D, texture_id);
		glUniform1i(getUniformLoc(name), tex_slot);
	}

	void uniformImg2D(const char* name, unsigned int texture_id, int tex_slot, unsigned int access, unsigned int format)
	{
		glBindImageTexture(tex_slot, texture_id, 0, GL_FALSE, 0, access, format);
		glUniform1i(getUniformLoc(name), tex_slot);
	}

	void uniformImg3D(const char* name, unsigned int texture_id, int tex_slot, unsigned int access, unsigned int format)
	{
		glBindImageTexture(tex_slot, texture_id, 0, GL_FALSE, 0, access, format);
		glUniform1i(getUniformLoc(name), tex_slot);
	}

protected:
	void checkErrors()
	{
		int infoLogLength = 0;
		glGetProgramiv(m_handle, GL_INFO_LOG_LENGTH, &infoLogLength);
		if (infoLogLength > 0)
		{
			std::vector<char> infoLog(infoLogLength);
			glGetProgramInfoLog(m_handle, infoLogLength, nullptr, infoLog.data());
			std::cout << "Program link log:\n" << infoLog.data() << '\n';
		}
	}
	uint32_t m_handle = 0;
};

class ShaderProgram : public ShaderProgramBase
{
public:

	Status createProgram(Shader& vertexShader, Shader& fragmentShader)
	{
		m_handle = glCreateProgram();

		if (!m_handle)
			return Status::FAILURE;

		glAttachShader(m_handle, vertexShader.getHandle());
		glAttachShader(m_handle, fragmentShader.getHandle());

		glLinkProgram(m_handle);

		int linked = 0;
		glGetProgramiv(m_handle, GL_LINK_STATUS, &linked);
		if (!linked)
		{
			checkErrors();
			return Status::FAILURE;
		}
		return Status::SUCCESS;
	}

};

class ShaderProgramCompute : public ShaderProgramBase
{
public:
	Status createProgram(Shader& computeShader)
	{
		m_handle = glCreateProgram();

		if (!m_handle)
			return Status::FAILURE;

		glAttachShader(m_handle, computeShader.getHandle());

		glLinkProgram(m_handle);

		int linked = 0;
		glGetProgramiv(m_handle, GL_LINK_STATUS, &linked);
		if (!linked)
		{
			checkErrors();
			return Status::FAILURE;
		}
		return Status::SUCCESS;
	}
};

void DispatchCompute(ShaderProgramCompute& computeProgram, glm::ivec3 groups);
void DispatchCompute(ShaderProgramCompute& computeProgram, uint32_t x, uint32_t y, uint32_t z);
void DispatchComputeIndirect(ShaderProgramCompute& computeProgram, GLintptr indirectByteOffset);
