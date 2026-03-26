#pragma once 
#include "glad/gl.h"

#include <memory>
#include <iostream>


static uint32_t MemoryExponent(uint64_t size)
{
	return glm::log2(float(size)) / glm::log2(1024.f);
}

static float MemoryNormalized(uint64_t size, uint32_t exp)
{
	return size / glm::pow(1024.f, float(exp));
}


class GpuBuffer
{
public:
	void Name(const std::string& name)
	{
		glObjectLabel(GL_BUFFER, m_Handle, name.size(), name.c_str());
	}
	void Create()
	{
		glCreateBuffers(1, &m_Handle);
		if (!glIsBuffer(m_Handle))
		{
			printf("Not a valid buffer generated!\n");
		}
	}
	void Create(const std::string& name)
	{
		Create();
		Name(name);
	}
	void AllocateStorage(size_t size, void* data, GLbitfield flags)
	{
		while (glGetError() != GL_NO_ERROR);
		glNamedBufferStorage(m_Handle, size, data, flags);
		GLenum err = glGetError();
		if (err == GL_OUT_OF_MEMORY)
		{
			printf("GPU out of memory\n");
			return;
		}
		uint32_t sizeExp = MemoryExponent(size);
		float sizeDisp = MemoryNormalized(size, sizeExp);
		static const char* units[4] = { "B", "KB", "MB", "GB" };
		totalBytes += size;
		uint32_t totalExp = MemoryExponent(totalBytes);
		float totalDisp = MemoryNormalized(totalBytes, totalExp);
		std::cout << "Allocated: " << sizeDisp << units[sizeExp] << "\t\t\ttotal = " << totalDisp << units[totalExp] << "\n";

		m_Size = size;
	}
	void Allocate(size_t size, void* data, GLenum usage)
	{
		while (glGetError() != GL_NO_ERROR);
		glNamedBufferData(m_Handle, size, data, usage);
		GLenum err = glGetError();
		if (err == GL_OUT_OF_MEMORY)
		{
			printf("GPU out of memory\n");
			return;
		}
		uint32_t sizeExp = MemoryExponent(size);
		float sizeDisp = MemoryNormalized(size, sizeExp);
		static const char* units[4] = { "B", "KB", "MB", "GB" };
		totalBytes += size;
		uint32_t totalExp = MemoryExponent(totalBytes);
		float totalDisp = MemoryNormalized(totalBytes, totalExp);
		std::cout << "Allocated: " << sizeDisp << units[sizeExp] << "\t\t\ttotal = " << totalDisp << units[totalExp] << "\n";

		m_Size = size;
	}
	void Upload(GLintptr offset, GLsizeiptr size, const void* data)
	{
		glNamedBufferSubData(m_Handle, offset, size, data);
	}
	void ShiftData(size_t writeOffset, size_t readOffset, size_t size)
	{
		void* ptr = glMapNamedBuffer(m_Handle, GL_READ_WRITE);

		if (!ptr)
			return;

		char* bytesPtr = reinterpret_cast<char*>(ptr);
		memmove(bytesPtr + writeOffset, bytesPtr + readOffset, size);

		glUnmapNamedBuffer(m_Handle);
	}
	GLuint Handle() const
	{
		return m_Handle;
	}
	void Delete()
	{
		if (m_Handle != 0)
			glDeleteBuffers(1, &m_Handle);
	}
	uint64_t SizeInBytes()
	{
		return m_Size;
	}
	~GpuBuffer()
	{
		Delete();
	}
private:
	GLuint m_Handle = 0;
	size_t m_Size = 0;
	inline static uint64_t totalBytes = 0;

};