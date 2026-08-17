#pragma once
#include <glm/glm.hpp>

struct ChunkKey
{
	union
	{
		glm::ivec4 vec;
		glm::ivec3 coord;
		struct
		{
			int x, y, z, lod;
		};
	};

	ChunkKey(int x, int y, int z, int lod)
		: x(x), y(y), z(z), lod(lod) {}
	ChunkKey(glm::ivec4 v)
		: vec(v) {}
};

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
};