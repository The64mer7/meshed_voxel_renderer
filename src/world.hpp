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
#include "thread_safe.hpp"
#include "edit_octree.h"

#define VERTICES_PER_FACE 6

struct ChunkMesherTaskData
{
	DrawArraysIndirectCommand cmd;
	uint32_t aabb;
	ChunkKey key;
	bool remesh;
};

inline std::atomic<double> g_meshing_time_sum = 0;
inline std::atomic<double> g_meshing_count = 0;
inline std::atomic<double> g_generating_time_sum = 0;
inline std::atomic<double> g_generating_count = 0;
inline std::atomic_bool g_mesh_naive = false;

struct UpdateGreedyMeshTask
{
	MemoryManager* manager = nullptr;
	GpuBufferMapping* gpu_buffer_mapping = nullptr;
	WorldData* world_data = nullptr;
	WorldEdits* edits = nullptr;

	bool remesh;
	OctreeClipmap::LeavesVector* chunks = nullptr;
	size_t begin;
	size_t end;
	uint32_t voxels_per_axis;
	std::atomic_uint32_t* tasks_counter = nullptr;

	ThreadSafeQueue<ChunkMesherTaskData>* chunks_to_commit = nullptr;

	void operator()(ArenaAllocator* allocator)
	{
		size_t size_2d = glm::pow(world_data->voxels_per_chunk_axis + 2, 2u);
		size_t size_3d = glm::pow(world_data->voxels_per_chunk_axis + 2, 3u);

		VoxelData* voxel_data = allocator->allocate<VoxelData>(1);
		GreedyFace* faces_buffer = allocator->allocate<GreedyFace>(size_3d*6);

		uint32_t max_structures = 16;
		WorldInstance* structures = allocator->allocate<WorldInstance>(max_structures);
		
		if (voxel_data == nullptr || faces_buffer == nullptr || structures == nullptr)
		{
			LOG("ALLOCATION FAIL");
			exit(1);
		}

		for (size_t i = begin; i < end; i++)
		{
			ChunkKey key = (*chunks)[i];

			auto t0 = std::chrono::high_resolution_clock::now();
			if (!voxel_data->compute_terrain(key, *world_data, edits, structures, max_structures)) continue;
			auto t1 = std::chrono::high_resolution_clock::now();
			double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

			auto tmesh0 = std::chrono::high_resolution_clock::now();
			ChunkGreedyMesherResult result;
			if (g_mesh_naive)
				result = mesh_naive(voxel_data, faces_buffer, voxels_per_axis);
			else
				result = mesh_greedy(voxel_data, faces_buffer, voxels_per_axis);
			auto tmesh1 = std::chrono::high_resolution_clock::now();
			double elapsed_mesh_ms = std::chrono::duration<double, std::milli>(tmesh1 - tmesh0).count();



			if (result.face_count > 0)
			{
				g_meshing_count += 1;
				g_meshing_time_sum += elapsed_mesh_ms;
				
				g_generating_count += 1;
				g_generating_time_sum += elapsed_ms;


				size_t size_bytes = result.face_count * sizeof(GreedyFace);
				offset_t offset_bytes;
				if (manager->alloc(size_bytes, offset_bytes))
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
					chunk_data.remesh = remesh;

					chunks_to_commit->Enqueue(chunk_data);
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
	OctreeClipmap::LeavesVector* chunks;
	uint32_t batch_size;
	
	UpdateGreedyMeshTask operator()(uint32_t i)
	{
		task.begin = i * batch_size;
		task.end = glm::min(task.begin + batch_size, chunks->size());

		return task;
	}
};

struct BatchedTasks
{
	bool try_submit_batch(ThreadPool& tp, uint32_t task_count, std::atomic_uint32_t* counter, std::function<UpdateGreedyMeshTask(uint32_t i)> generator)
	{
		if (!all_tasks_completed(counter))
			return false;

		submit_batch(tp, task_count, counter, generator);
		return true;
	}

	void submit_batch(ThreadPool& tp, uint32_t task_count, std::atomic_uint32_t* counter, std::function<UpdateGreedyMeshTask(uint32_t i)> generator)
	{
		counter->fetch_add(task_count);
		for (uint32_t i = 0; i < task_count; i++)
		{
			tp.submit(generator(i));
		}
	}

	bool all_tasks_completed(std::atomic_uint32_t* counter)
	{
		return counter->load() == 0;
	}
};

class World
{
public:
	void create(const WorldData& data, const OctreeClipmapGenerateSettings& settings);
	void update(const glm::vec3& player_position, float fov);
	void render(const glm::vec3& world_origin, const FirstPersonCamera& camera, const glm::ivec3& camera_chunk_coord, float camera_chunk_size, ChunkKey& out_key);
	void destroy();
	structure_id create_structure(OctreeStructure* structure);
	void place_structure(structure_id handle, const glm::vec3& position);

	void update_settings(const OctreeClipmapGenerateSettings& settings);
	void regenerate_chunks(const glm::vec3& player_position);
	void get_loaded_chunks_in_area(std::vector<ChunkKey>* out_chunks, const aabb3d& bounds);
	void get_chunks_in_area(std::vector<ChunkKey>* out_chunks, const aabb3d& bounds);

	void debug_ui()
	{
		ImGui::Separator();
		ImGui::Text("world_debug");
		ImGui::Text("to mesh: %u\nto remesh: %u", m_chunks_to_mesh_counter.load(), m_chunks_to_remesh_counter.load());
	}

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

	uint64_t get_tree_node_size()
	{
		return m_clipmap.nodes.size();
	}

	const WorldData& get_data()
	{
		return m_data;
	}

	WorldEdits& get_edits()
	{
		return m_edits;
	}


private:
	bool erase_chunk(const ChunkKey& key);

	void submit_tasks(OctreeClipmap::LeavesVector* chunks, bool remesh, std::atomic_uint32_t* counter);
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

	WorldEdits m_edits;

	OctreeClipmap::LeavesVector m_chunks_to_remesh;

	std::atomic_uint32_t m_chunks_to_mesh_counter = 0;
	std::atomic_uint32_t m_chunks_to_remesh_counter = 0;

	std::queue<WorldInstance> m_placed_instances;

	ThreadSafeQueue<ChunkMesherTaskData> m_chunks_to_submit;

	ThreadSafeQueue<ChunkMesherTaskData> m_chunks_to_commit;

	//TODO: Combine parallel sparse sets
	GpuBuffer m_chunk_aabbs_buffer;
	SparseSet<ChunkKey, uint32_t> m_chunk_aabbs;
	
	GpuBuffer m_chunk_draw_cmds_buffer;
	SparseSet<ChunkKey, DrawArraysIndirectCommand> m_chunk_draw_cmds;
	
	ShaderProgram m_sp;
};