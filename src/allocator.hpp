#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <vector>
#include <iostream>
#include <format>

#include <unordered_map>
#include <map>
#include <array>
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
#include "thread_safe.hpp"

static std::string VectorToName(glm::ivec3 key, char sep)
{
	return std::to_string(key.x) + sep + std::to_string(key.y) + sep + std::to_string(key.z);
}

static std::string VectorToName(glm::ivec4 key, char sep)
{
	return std::to_string(key.x) + sep + std::to_string(key.y) + sep + std::to_string(key.z) + sep + std::to_string(key.w);
}

using namespace std::chrono_literals;
inline static bool g_DebugPrint = true;

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


// TODO: change rec mutex to classic, lock just public API
class MemoryManager
{
	using Offsets = std::set<offset_t>;
public:
	uint64_t id = -1;
	MemoryManager()
	{
		static std::atomic_uint64_t id_gen = 0;
		this->id = id_gen++;
	}
	size_t get_max_size()
	{
		std::lock_guard lock(m_RecMtx);
		return m_MaxSize;
	}

	void create(size_t size)
	{
		m_MaxSize = size;
	}

	bool alloc(size_t desiredSize, offset_t& outOffset)
	{
		std::lock_guard lock(m_RecMtx);

		auto itSize = m_SizeToOffsets.lower_bound(desiredSize);

		if (itSize == m_SizeToOffsets.end())
			return false;

		size_t storedSize = itSize->first;

		outOffset = *itSize->second.begin();

		split_memory_blocks(outOffset, storedSize, desiredSize);
		return true;
	}

	bool can_alloc(size_t desiredSize)
	{
		std::lock_guard lock(m_RecMtx);

		auto itSize = m_SizeToOffsets.lower_bound(desiredSize);

		if (itSize == m_SizeToOffsets.end())
			return false;

		return true;
	}

	void free_all()
	{
		std::lock_guard lock(m_RecMtx);
		m_OffsetToSize.clear();
		m_SizeToOffsets.clear();
	}

	void defrag_free_list()
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

	void free(offset_t offset, size_t size)
	{
		std::lock_guard lock(m_RecMtx);

		if (size == 0)
			return;

		bool merged = true;
		while (merged)
		{
			merged = false;
			auto it = m_OffsetToSize.lower_bound(offset);
			if (it != m_OffsetToSize.begin())
			{
				auto prev = std::prev(it);
				size_t prev_offset = prev->first;
				size_t prev_size = prev->second;
				offset_t prev_end = prev_offset + prev_size;

				if (prev_end == offset)
				{
					offset = prev_offset;
					size += prev_size;
					alloc_at(prev_offset, prev_size);
					merged = true;
				}
			}
		}
		while (true)
		{

			auto next = m_OffsetToSize.lower_bound(offset);
			if (next == m_OffsetToSize.end())
				break;

			size_t next_offset = next->first;
			size_t next_size = next->second;
			offset_t curr_end = offset + size;
			
			if (curr_end != next_offset)
				break;
			
			size += next_size;
			alloc_at(next_offset, next_size);
		}

		assert(!m_OffsetToSize.contains(offset));
		assert(!m_SizeToOffsets[size].contains(offset));

		Offsets& offsets = m_SizeToOffsets[size];
		offsets.insert(offset);
		
		m_OffsetToSize[offset] = size;
	}

	void alloc_at(offset_t offset, size_t size)
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

	uint64_t get_blocks_size()
	{
		uint64_t total = 0;
		for (const auto& [offset, size] : m_OffsetToSize)
			total += size;
		return total;
	}

	void debug_print(uint64_t blockSize = KB(4))
	{
		std::lock_guard lock(m_RecMtx);
		std::cout << id << "id )" <<"MEM: [\n";
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
					std::cout << id << "id )" <<'\n';
					currRowSize = 0;
				}
			};

		for (uint64_t i = 0; i < it->first / blockSize; i++)
		{
			std::cout << id << "id )" <<freeSlot; FnTryNewLine();
		}
		std::cout << id << "id )" <<sep; FnTryNewLine();
		while (it != m_OffsetToSize.end() && nextIt != m_OffsetToSize.end())
		{
			std::cout << id << "id )" <<sep; FnTryNewLine();
			for (uint64_t i = 0; i < (it->second / blockSize); i++)
			{
				std::cout << id << "id )" <<markedSlot; FnTryNewLine();
			}
			std::cout << id << "id )" <<sep; FnTryNewLine();

			if (!(it->second + it->first == nextIt->first))
			{
				for (uint64_t i = 0; i < ((nextIt->first - (it->first + it->second)) / blockSize); i++)
				{
					std::cout << id << "id )" <<freeSlot; FnTryNewLine();
				}
				std::cout << id << "id )" <<sep; FnTryNewLine();
			}
			std::cout << id << "id )" <<sep; FnTryNewLine();
			it = nextIt;
			nextIt++;
		}
		if (it != m_OffsetToSize.end())
		{
			for (uint64_t i = 0; i < (it->second / blockSize); i++)
			{
				std::cout << id << "id )" <<markedSlot; FnTryNewLine();
			}
			std::cout << id << "id )" <<sep; FnTryNewLine();
		}
		std::cout << id << "id )" <<"]\n";

		if (true)
		{
			if (g_DebugPrint) std::cout << id << "id )" <<'\n';

			for (auto& [offset, size] : m_OffsetToSize)
			{
				if (g_DebugPrint) std::cout << std::format("O: {} --> S: {}\n", offset, size);
			}
			if (g_DebugPrint) std::cout << id << "id )" <<'\n';
			for (auto& [size, offsets] : m_SizeToOffsets)
			{
				if (g_DebugPrint) std::cout << std::format("S: {}\n", size);
				for (auto& offset : offsets)
					if (g_DebugPrint) std::cout << std::format("  --> O: {}\n", offset);
			}
		}

		uint64_t totalBlocksSize = get_blocks_size();
		double ratio = totalBlocksSize / double(get_max_size());
		std::cout << std::format("marked: {} ({})\n", totalBlocksSize, ratio);
		std::cout << std::format("unmarked: {} ({})\n", get_max_size() - totalBlocksSize, 1.0 - ratio);

	}

	void debug_log(std::string& out, uint64_t blockSize)
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

			if (blockSize > 0 && nextIt->first > (it->first + it->second))
			{
				uint64_t free_diff = nextIt->first - (it->first + it->second);
				for (uint64_t i = 0; i < (free_diff / blockSize); i++)
				{
					out += freeSlot; FnTryNewLine();
				}
				out += sep; FnTryNewLine();
			}
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

	uint64_t block_count()
	{
		std::lock_guard lock(m_RecMtx);
		return m_OffsetToSize.size();
	}


	void for_each_memory_block(std::function<void(offset_t offset, size_t size, bool is_allocated)> for_each)
	{
		std::lock_guard lock(m_RecMtx);
		auto it = m_OffsetToSize.begin();
		if (it == m_OffsetToSize.end())
			return;

		if (it->first > 0)
		{
			for_each(0, it->first, false);
		}

		auto nextIt = std::next(it);

		while (nextIt != m_OffsetToSize.end())
		{
			for_each(it->first, it->second, true);

			offset_t current_end = it->first + it->second;
			if (current_end < nextIt->first)
			{
				size_t gap_size = nextIt->first - current_end;
				for_each(current_end, gap_size, false);
			}

			it = nextIt;
			++nextIt;
		}

		if (it != m_OffsetToSize.end())
		{
			for_each(it->first, it->second, true);
		}
	}

private:
	const std::map<offset_t, size_t>& get_offsets_to_size_map()
	{
		return m_OffsetToSize;
	}

	const std::map<size_t, Offsets>& get_size_to_offsets_map()
	{
		return m_SizeToOffsets;
	}

	void split_memory_blocks(offset_t offset, size_t size, size_t desiredSize)
	{
		std::lock_guard lock(m_RecMtx);

		alloc_at(offset, size);
		size_t remainingSize = size - desiredSize;

		free(offset + desiredSize, remainingSize);
	}


	size_t m_MaxSize;
	std::recursive_mutex m_RecMtx;
	std::map<size_t, Offsets> m_SizeToOffsets;
	std::map<offset_t, size_t> m_OffsetToSize;
};

