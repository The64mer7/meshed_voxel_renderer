#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <vector>
#include <iostream>
#include <format>

#include <unordered_map>
#include <map>
#include <glad/gl.h>

#include <unordered_set>
#include <set>

#include <queue>
#include <filesystem>
#include <fstream>

#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>

#include "buffer.hpp"
//#include "gpu_utils.hpp"
//#include "chunk_manager.hpp"

//#include "GlQuery.hpp"

std::string VectorToName(glm::ivec3 key, char sep)
{
	return std::to_string(key.x) + sep + std::to_string(key.y) + sep + std::to_string(key.z);
}

using namespace std::chrono_literals;
bool g_DebugPrint = true;

template <typename T>
class ThreadSafeQueue
{
public:
	void NotifyAll()
	{
		m_Cv.notify_all();
	}

	void Enqueue(const T& value)
	{
		{
			std::lock_guard lock(m_Mtx);
			m_Queue.push(value);
		}
		m_Cv.notify_one();
	}

	bool TryDequeue(T& out)
	{
		std::lock_guard lock(m_Mtx);
		if (m_Queue.empty())
			return false;

		out = m_Queue.front();
		m_Queue.pop();

		return true;
	}

	bool TryFront(T& out)
	{
		std::lock_guard lock(m_Mtx);
		if (m_Queue.empty())
			return false;

		out = m_Queue.front();

		return true;
	}

	void WaitAndDequeue(T& out)
	{
		std::unique_lock lock(m_Mtx);
		m_Cv.wait(lock, [this] {return !m_Queue.empty(); });

		out = m_Queue.front();
		m_Queue.pop();
	}

	void Clear()
	{
		std::unique_lock lock(m_Mtx);
		m_Queue = std::queue<T>();
	}

	size_t Size()
	{
		std::unique_lock lock(m_Mtx);
		size_t size = m_Queue.size();
		return size;
	}
private:

	std::condition_variable m_Cv;
	std::mutex m_Mtx;
	std::queue<T> m_Queue;
};

using offset_t = size_t;

struct MemoryBlock
{
	offset_t offset;
	size_t size;
};


constexpr size_t Bytes(size_t bytes)
{
	return bytes;
}

constexpr size_t KB(size_t kilobytes)
{
	return kilobytes * 1024ull;
}

constexpr size_t MB(size_t megabytes)
{
	return megabytes * 1024ull * 1024ull;
}

constexpr size_t GB(size_t gigabites)
{
	return gigabites * 1024ull * 1024ull * 1024ull;
}

struct BufferRange
{
	size_t offset;
	size_t size;
};

struct BufferRange32
{
	size_t offset;
	size_t size;
};

enum class ChunkCommandInfo
{
	Load, Unload, Edit
};
struct ChunkCommand
{
	ChunkCommandInfo info;
	bool editRemove = false;
	glm::ivec3 key;
	glm::vec3 p;
	float radius;

	bool m_WasEdited = false;
};
namespace std
{
	template<>
	struct hash<glm::ivec3>
	{
		size_t operator()(const glm::ivec3& v) const noexcept
		{
			size_t h1 = std::hash<int>()(v.x);
			size_t h2 = std::hash<int>()(v.y);
			size_t h3 = std::hash<int>()(v.z);

			return h1 ^ (h2 << 1) ^ (h3 << 2);
		}
	};
};

class MemoryManager // change rec mutex to classic, lock just public API
{
	using Offsets = std::set<offset_t>;
public:
	size_t GetMaxSize()
	{
		std::lock_guard lock(m_RecMtx);
		return m_MaxSize;
	}

	void Init(size_t size)
	{
		m_MaxSize = size;
	}

	bool FindAndPopOffset(size_t desiredSize, offset_t& outOffset)
	{
		std::lock_guard lock(m_RecMtx);

		auto itSize = m_SizeToOffsets.lower_bound(desiredSize);

		if (itSize == m_SizeToOffsets.end())
			return false;

		size_t storedSize = itSize->first;

		outOffset = *itSize->second.begin();

		SplitMemoryBlockAndPopDesired(outOffset, storedSize, desiredSize);

		return true;
	}

	void Clear()
	{
		std::lock_guard lock(m_RecMtx);
		m_OffsetToSize.clear();
		m_SizeToOffsets.clear();
	}

	void Defragment()
	{
		std::lock_guard lock(m_RecMtx);
		auto it = m_OffsetToSize.begin();
		auto nextIt = std::next(it);

		while (it != m_OffsetToSize.end() && nextIt != m_OffsetToSize.end())
		{
			if (it->second + it->first == nextIt->first)
			{
				it->second += nextIt->second;
				nextIt = m_OffsetToSize.erase(nextIt);
			}
			else
			{
				it = nextIt;
				++nextIt;
			}

		}

		m_SizeToOffsets.clear();

		for (auto& [offset, size] : m_OffsetToSize)
		{
			m_SizeToOffsets[size].insert(offset);
		}
	}

	void AddMemoryBlock(offset_t offset, size_t size)
	{
		std::lock_guard lock(m_RecMtx);

		if (size == 0)
			return;
		Offsets& offsets = m_SizeToOffsets[size];
		offsets.insert(offset);

		m_OffsetToSize[offset] = size;
	}

	void RemoveMemoryBlock(offset_t offset, size_t size)
	{
		std::lock_guard lock(m_RecMtx);

		auto it = m_SizeToOffsets.find(size);
		if (it != m_SizeToOffsets.end())
		{
			Offsets& offsets = it->second;
			offsets.erase(offset);
			if (offsets.empty())
				m_SizeToOffsets.erase(size);

		}
		m_OffsetToSize.erase(offset);
	}

	uint64_t GetBlocksSize()
	{
		uint64_t total = 0;
		for (const auto& [offset, size] : m_OffsetToSize)
			total += size;
		return total;
	}

	void DebugPrint(uint64_t blockSize = KB(4))
	{
		std::lock_guard lock(m_RecMtx);
		std::cout << "MEM: [\n";
		if (m_OffsetToSize.empty())
			return;

		auto it = m_OffsetToSize.begin();

		char markedSlot = '-';
		char freeSlot = '#';
		char sep = 'I';


		auto nextIt = std::next(it);

		uint64_t memRowSize = 128;
		uint64_t currRowSize = 0;
		std::function<void()> FnTryNewLine = [&]
			{
				if (currRowSize++ >= memRowSize)
				{
					std::cout << '\n';
					currRowSize = 0;
				}
			};

		for (uint64_t i = 0; i < it->first / blockSize; i++)
		{
			std::cout << freeSlot; FnTryNewLine();
		}
		std::cout << sep; FnTryNewLine();
		while (it != m_OffsetToSize.end() && nextIt != m_OffsetToSize.end())
		{
			std::cout << sep; FnTryNewLine();
			for (uint64_t i = 0; i < (it->second / blockSize); i++)
			{
				std::cout << markedSlot; FnTryNewLine();
			}
			std::cout << sep; FnTryNewLine();

			if (!(it->second + it->first == nextIt->first))
			{
				for (uint64_t i = 0; i < ((nextIt->first - (it->first + it->second)) / blockSize); i++)
				{
					std::cout << freeSlot; FnTryNewLine();
				}
				std::cout << sep; FnTryNewLine();
			}
			std::cout << sep; FnTryNewLine();
			it = nextIt;
			nextIt++;
		}
		if (it != m_OffsetToSize.end())
		{
			for (uint64_t i = 0; i < (it->second / blockSize); i++)
			{
				std::cout << markedSlot; FnTryNewLine();
			}
			std::cout << sep; FnTryNewLine();
		}
		std::cout << "]\n";

		if (true)
		{
			if (g_DebugPrint) std::cout << '\n';

			for (auto& [offset, size] : m_OffsetToSize)
			{
				if (g_DebugPrint) std::cout << std::format("O: {} --> S: {}\n", offset, size);
			}
			if (g_DebugPrint) std::cout << '\n';
			for (auto& [size, offsets] : m_SizeToOffsets)
			{
				if (g_DebugPrint) std::cout << std::format("S: {}\n", size);
				for (auto& offset : offsets)
					if (g_DebugPrint) std::cout << std::format("  --> O: {}\n", offset);
			}
		}

		uint64_t totalBlocksSize = GetBlocksSize();
		double ratio = totalBlocksSize / double(GetMaxSize());
		std::cout << std::format("marked: {} ({})\n", totalBlocksSize, ratio);
		std::cout << std::format("unmarked: {} ({})\n", GetMaxSize() - totalBlocksSize, 1.0 - ratio);

	}

	void DebugLog(std::string& out, uint64_t blockSize)
	{
		out.clear();
		std::lock_guard lock(m_RecMtx);
		out += "MEM: [\n";
		if (m_OffsetToSize.empty())
			return;

		auto it = m_OffsetToSize.begin();

		char markedSlot = '-';
		char freeSlot = '#';
		char sep = 'I';


		auto nextIt = std::next(it);

		uint64_t memRowSize = 64;
		uint64_t currRowSize = 0;
		std::function<void()> FnTryNewLine = [&]
			{
				if (currRowSize++ >= memRowSize)
				{
					currRowSize = 0;
				}
			};

		size_t totalSize = 0;
		for (uint64_t i = 0; i < it->first / blockSize; i++)
		{
			out += freeSlot; FnTryNewLine();
		}
		out += sep; FnTryNewLine();
		while (it != m_OffsetToSize.end() && nextIt != m_OffsetToSize.end())
		{
			totalSize += it->second;
			out += sep; FnTryNewLine();
			for (uint64_t i = 0; i < (it->second / blockSize); i++)
			{
				out += markedSlot; FnTryNewLine();
			}
			out += sep; FnTryNewLine();

			if (!(it->second + it->first == nextIt->first))
			{
				for (uint64_t i = 0; i < ((nextIt->first - (it->first + it->second)) / blockSize); i++)
				{
					out += freeSlot; FnTryNewLine();
				}
				out += sep; FnTryNewLine();
			}
			out += sep; FnTryNewLine();
			it = nextIt;
			nextIt++;
		}
		if (it != m_OffsetToSize.end())
		{
			totalSize += it->second;

			for (uint64_t i = 0; i < (it->second / blockSize); i++)
			{
				out += markedSlot; FnTryNewLine();
			}
			out += sep; FnTryNewLine();
		}
		out += "]\n";
		totalSize = m_MaxSize - totalSize;
		if (totalSize < KB(4))
			out += std::format("Memory usage: {} / {}B", totalSize, m_MaxSize);
		else if (totalSize < MB(4))
			out += std::format("Memory usage: {} / {}KB", totalSize / 1024, m_MaxSize / 1024);
		else
			out += std::format("Memory usage: {} / {}MB", totalSize / 1024 / 1024, m_MaxSize / 1024 / 1024);

		if (true)
		{
			out += '\n';

			for (auto& [offset, size] : m_OffsetToSize)
			{
				out += std::format("O: {} --> S: {}\n", offset, size);
			}
			out += '\n';
			for (auto& [size, offsets] : m_SizeToOffsets)
			{
				out += std::format("S: {}\n", size);
				for (auto& offset : offsets)
					out += std::format("  --> O: {}\n", offset);
			}
		}
	}

	uint64_t BlocksCount()
	{
		std::lock_guard lock(m_RecMtx);
		return m_OffsetToSize.size();
	}

private:
	const std::map<offset_t, size_t>& GetOffsetToSizeMap()
	{
		return m_OffsetToSize;
	}

	const std::map<size_t, Offsets>& GetSizeToOffsetsMap()
	{
		return m_SizeToOffsets;
	}

	void SplitMemoryBlock(offset_t offset, size_t size, size_t desiredSize)
	{
		std::lock_guard lock(m_RecMtx);

		RemoveMemoryBlock(offset, size);
		AddMemoryBlock(offset, desiredSize);

		size_t remainingSize = size - desiredSize;
		AddMemoryBlock(offset + desiredSize, remainingSize);
	}

	void SplitMemoryBlockAndPopDesired(offset_t offset, size_t size, size_t desiredSize)
	{
		std::lock_guard lock(m_RecMtx);

		RemoveMemoryBlock(offset, size);

		size_t remainingSize = size - desiredSize;
		AddMemoryBlock(offset + desiredSize, remainingSize);
	}


	size_t m_MaxSize;
	std::recursive_mutex m_RecMtx;
	std::map<size_t, Offsets> m_SizeToOffsets;
	std::map<offset_t, size_t> m_OffsetToSize;
};

