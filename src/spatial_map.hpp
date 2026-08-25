#pragma once
#include "sparse_set.hpp"


using Key = int;
using Value = int;
class SpatialMap
{
public:

	void insert(const Key& key, const Value& value, const glm::ivec3& coord)
	{
		m_data[coord].insert(key, value);
	}

	void remove(const Key& key, const glm::ivec3& coord)
	{
		m_data[coord].remove(key);
	}

private:
	std::unordered_map<glm::ivec3, SparseSet<Key, Value>> m_data;
};