#pragma once
#include "sparse_set.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

struct SpatialKey
{
	glm::ivec3 coord;
	uint32_t level;
};

namespace std
{
	template <>
	struct hash<SpatialKey>
	{
		size_t operator()(const SpatialKey& key) const noexcept
		{
			size_t h1 = std::hash<uint32_t>{}(key.level);
			size_t h2 = std::hash<glm::ivec3>{}(key.coord);

			return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
		}
	};
}

using Key = int;
using Value = int;
class SpatialMap
{
public:
	void create(uint32_t split_factor)
	{
		m_split_factor = split_factor;
	}

	void insert(const Key& key, const Value& value, const glm::ivec3& coord)
	{
		m_data[coord].insert(key, value);
	}

	void remove(const Key& key, const glm::ivec3& coord)
	{
		m_data[coord].remove(key);
	}

private:
	uint32_t m_split_factor;
	std::unordered_map<glm::ivec3, SparseSet<Key, Value>> m_data;
};