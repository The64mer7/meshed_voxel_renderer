#pragma once
#include <glm/glm.hpp>

#include "thread_pool.hpp"

typedef glm::ivec4 ChunkKeyRaw;
typedef glm::ivec3 ChunkCoord;
struct ChunkKey
{
	union
	{
		ChunkKeyRaw raw;
		glm::ivec4 raw_vec;
		glm::ivec3 coord;
		struct
		{
			int x, y, z, lod;
		};
	};
	ChunkKey() = default;
	ChunkKey(int x, int y, int z, int lod)
		: x(x), y(y), z(z), lod(lod) {}
	ChunkKey(glm::ivec4 v)
		: raw(v) {}

	bool operator<(const ChunkKey& rhs) const
	{
		if (x != rhs.x) return x < rhs.x;
		if (y != rhs.y) return y < rhs.y;
		if (z != rhs.z) return z < rhs.z;
		return lod < rhs.lod;
	}

	inline ChunkKey get_octree_child(int cx, int cy, int cz)
	{
		ChunkKey child;
		child.lod = lod + 1;
		child.x = (x << 1) | cx;
		child.y = (y << 1) | cy;
		child.z = (z << 1) | cz;
		return child;
	}
};

inline bool operator==(const ChunkKey& lhs, const ChunkKey& rhs) {
	return lhs.raw == rhs.raw;
}

namespace std
{
	template <>
	struct hash<ChunkKey>
	{
		std::size_t operator()(const ChunkKey& key) const noexcept
		{
			return std::hash<glm::ivec4>{}(key.raw_vec);
		}
	};
}

struct WorldData
{
	int world_size_exp;
	float world_size() const
	{
		return 1 << world_size_exp;
	}

	float chunk_size(int32_t lod) const
	{
		return glm::ldexp(1.f, world_size_exp - lod);
	}

	float voxel_size(int32_t lod) const
	{
		return chunk_size(lod) / voxels_per_chunk_axis;
	}

	glm::vec3 chunk_origin(const ChunkKey& chunk_key) const
	{
		return chunk_size(chunk_key.lod) * glm::vec3(chunk_key.coord);
	}

	int voxels_per_chunk_axis;
	float update_distance;
	size_t world_vram;
	ThreadPool* thread_pool;
};