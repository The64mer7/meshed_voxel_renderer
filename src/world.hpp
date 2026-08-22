#pragma once
#include <stdint.h>
#include <atomic>

#include "engine.h"
#include "sparse_set.hpp"
#include "shader.h"
#include "octree.h"
#include "world_data.hpp"
#include "utils.hpp"
#include "chunk_mesher.hpp"
#include "thread_safe_stack.hpp"

#define VERTICES_PER_FACE 6

struct ChunkMesherTaskData
{
	DrawArraysIndirectCommand cmd;
	uint32_t aabb;
	ChunkKey key;
};

inline std::atomic<double> g_generator_time_sum = 0;
inline std::atomic<double> g_generator_count = 0;

struct UpdateMeshTask
{
	MemoryManager* manager;
	GpuBufferMapping* gpu_buffer_mapping;
	WorldData* world_data;

	OctreeClipmap::LeavesVector* leaves_to_add;
	size_t begin;
	size_t end;
	uint32_t voxels_per_axis;

	std::atomic_uint32_t* tasks_counter;

	ThreadSafeQueue<ChunkMesherTaskData>* chunk_meshed_queue;

	void operator()(ArenaAllocator* allocator)
	{
		size_t size_2d = glm::pow(world_data->voxels_per_chunk_axis + 2, 2u);
		size_t size_3d = glm::pow(world_data->voxels_per_chunk_axis + 2, 3u);

		uint32_t* faces_buffer = allocator->allocate<uint32_t>(voxels_per_axis * voxels_per_axis * voxels_per_axis * 3);
		float* height_map = allocator->allocate<float>(size_2d);
		float* density_map = allocator->allocate<float>(size_3d);
		uint32_t* solid_mask = allocator->allocate<uint32_t>(size_3d);
		uint32_t* bit_mask = allocator->allocate<uint32_t>((size_3d + 31) / 32);

		for (size_t i = begin; i < end; i++)
		{
			ChunkKey key = leaves_to_add->at(i);

			auto t0 = std::chrono::high_resolution_clock::now();

			ChunkMeshResult result = ChunkMesher::mesh_3d_packed(key, faces_buffer, *world_data, height_map, density_map, solid_mask, bit_mask);

			auto t1 = std::chrono::high_resolution_clock::now();
			double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

			if (result.face_count > 0)
			{
				g_generator_count += 1;
				g_generator_time_sum += elapsed_ms;

				size_t size_bytes = result.face_count * sizeof(ChunkMesher::voxel_face_t);
				offset_t offset_bytes;
				if (manager->Allocate(size_bytes, offset_bytes))
				{
					memcpy(static_cast<uint8_t*>(gpu_buffer_mapping->buffer) + offset_bytes, faces_buffer, size_bytes);

					DrawArraysIndirectCommand cmd;
					cmd.baseInstance = 0;
					cmd.first = (offset_bytes * VERTICES_PER_FACE) / sizeof(ChunkMesher::voxel_face_t);
					cmd.count = (size_bytes * VERTICES_PER_FACE) / sizeof(ChunkMesher::voxel_face_t);
					cmd.instanceCount = 1;

					ChunkMesherTaskData chunk_data;
					chunk_data.key = key;
					chunk_data.aabb = result.shrinkwrap_aabb;
					chunk_data.cmd = cmd;

					chunk_meshed_queue->Enqueue(chunk_data);
				}
				else
				{
					LOG("OUT OF MEMORY");
				}
			}
		}
		tasks_counter->fetch_sub(1);
	}
};

struct UpdateGreedyMeshTask
{
	MemoryManager* manager;
	GpuBufferMapping* gpu_buffer_mapping;
	WorldData* world_data;

	OctreeClipmap::LeavesVector* leaves_to_add;
	size_t begin;
	size_t end;
	uint32_t voxels_per_axis;

	std::atomic_uint32_t* tasks_counter;

	ThreadSafeQueue<ChunkMesherTaskData>* chunk_meshed_queue;

	void operator()(ArenaAllocator* allocator)
	{
		size_t size_2d = glm::pow(world_data->voxels_per_chunk_axis + 2, 2u);
		size_t size_3d = glm::pow(world_data->voxels_per_chunk_axis + 2, 3u);

		VoxelData* voxel_data = allocator->allocate<VoxelData>(1);
		GreedyFace* faces_buffer = allocator->allocate<GreedyFace>(size_3d*6);
		if (faces_buffer == nullptr)
		{
			LOG("ALLOCATION FAIL");
			exit(1);
		}
		for (size_t i = begin; i < end; i++)
		{
			ChunkKey key = leaves_to_add->at(i);

			auto t0 = std::chrono::high_resolution_clock::now();
			if (!voxel_data->compute_terrain(key, *world_data)) continue;

			ChunkGreedyMesherResult result = mesh_greedy(voxel_data, faces_buffer, voxels_per_axis);

			auto t1 = std::chrono::high_resolution_clock::now();
			double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

			if (result.face_count > 0)
			{
				g_generator_count += 1;
				g_generator_time_sum += elapsed_ms;

				size_t size_bytes = result.face_count * sizeof(GreedyFace);
				offset_t offset_bytes;
				if (manager->Allocate(size_bytes, offset_bytes))
				{
					if(!(size_bytes % sizeof(GreedyFace) == 0)) exit(1);
					if(!(offset_bytes % sizeof(GreedyFace) == 0)) exit(1);
					memcpy(static_cast<uint8_t*>(gpu_buffer_mapping->buffer) + offset_bytes, faces_buffer, size_bytes);

					DrawArraysIndirectCommand cmd;
					cmd.baseInstance = 0;
					cmd.first = (offset_bytes * VERTICES_PER_FACE) / sizeof(GreedyFace);
					cmd.count = (size_bytes * VERTICES_PER_FACE) / sizeof(GreedyFace);
					cmd.instanceCount = 1;

					ChunkMesherTaskData chunk_data;
					chunk_data.key = key;
					chunk_data.aabb = make_aabb(glm::ivec4(0), glm::ivec4(64)); // FIX: incorrect
					chunk_data.cmd = cmd;

					chunk_meshed_queue->Enqueue(chunk_data);
				}
				else
				{
					LOG("OUT OF MEMORY (tried to alloc {}B)", size_bytes);
				}
			}
		}
		tasks_counter->fetch_sub(1);
	}
};


struct TaskGen
{
	UpdateGreedyMeshTask task;
	OctreeClipmap* clipmap;
	uint32_t batch_size;
	
	UpdateGreedyMeshTask operator()(uint32_t i)
	{
		task.begin = i * batch_size;
		task.end = glm::min(task.begin + batch_size, clipmap->get_leaves_created().size());

		return task;
	}
};

struct BatchedTasks
{
	bool try_submit_batch(ThreadPool& tp, uint32_t task_count, std::function<UpdateGreedyMeshTask(uint32_t i)> generator)
	{
		if (!all_tasks_completed())
			return false;

		tasks_counter.fetch_add(task_count);

		for (uint32_t i = 0; i < task_count; i++)
			tp.submit(generator(i));

		return true;
	}

	bool all_tasks_completed()
	{
		return tasks_counter.load() == 0;
	}

	std::atomic_uint32_t tasks_counter = 0;
};

class World
{
public:
	void create(const WorldData& data, const OctreeClipmapGenerateSettings& settings);
	void update(const glm::vec3& player_position);
	void render(const glm::vec3& world_origin, const FirstPersonCamera& camera, const glm::ivec3& camera_chunk_coord, float camera_chunk_size);
	
	void update_settings(const OctreeClipmapGenerateSettings& settings);
	
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

	void update_world_mesh(const glm::vec3& player_position);
	OctreeClipmapGenerateSettings m_settings;
	glm::vec3 m_last_update_pos;

	uint32_t m_dummy_vao;
	WorldData m_data;

	BatchedTasks m_tasks;

	uint32_t texture_atlas;
	GpuBuffer m_world_buffer;
	GpuBufferMapping m_world_buffer_mapping;
	MemoryManager m_world_buffer_manager;
	OctreeClipmap m_clipmap;

	ThreadSafeQueue<ChunkMesherTaskData> m_chunk_meshed_queue;

	//TODO: Combine parallel sparse sets
	GpuBuffer m_chunk_aabbs_buffer;
	SparseSet<ChunkKey, uint32_t> m_chunk_aabbs;
	
	GpuBuffer m_chunk_draw_cmds_buffer;
	SparseSet<ChunkKey, DrawArraysIndirectCommand> m_chunk_draw_cmds;
	
	ShaderProgram m_sp;
};