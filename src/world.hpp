#pragma once
#include <stdint.h>

#include "engine.h"
#include "sparse_set.hpp"
#include "shader.h"
#include "octree.h"
#include "world_data.hpp"
#include "utils.hpp"
#include "chunk_mesher.hpp"

#define VERTICES_PER_FACE 6

struct ChunkData
{
	DrawArraysIndirectCommand cmd;
	glm::ivec4 position;
	uint32_t aabb;
};


class World
{
public:
	void create(const WorldData& data, const OctreeClipmapGenerateSettings& settings);
	void update(const glm::vec3& player_position);
	void render(const glm::vec3& world_origin, const FirstPersonCamera& camera, const glm::ivec3& camera_chunk_coord, float camera_chunk_size);

	OctreeClipmapGenerateSettings& get_settings()
	{
		return m_settings;
	}
	MemoryManager& get_memory_allocator()
	{
		return m_world_buffer_manager;
	}

	uint64_t get_chunks_allocated()
	{
		return m_chunk_draw_cmds.get_keys().size();
	}

private:
	OctreeClipmapGenerateSettings m_settings;
	glm::vec3 m_last_update_pos;
	ChunkMesher m_mesher;

	uint32_t m_dummy_vao;
	WorldData m_data;

	uint32_t texture_atlas;
	GpuBuffer m_world_buffer;
	MemoryManager m_world_buffer_manager;
	OctreeClipmap m_world_chunk_clipmap;

	//TODO: Combine parallel sparse sets
	GpuBuffer m_chunk_aabbs_buffer;
	SparseSet<glm::ivec4, uint32_t> m_chunk_aabbs;
	
	GpuBuffer m_chunk_draw_cmds_buffer;
	SparseSet<glm::ivec4, DrawArraysIndirectCommand> m_chunk_draw_cmds;
	
	ShaderProgram m_sp;
};