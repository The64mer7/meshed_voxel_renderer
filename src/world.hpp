#pragma once
#include <atomic>
#include <queue>
#include <stdint.h>

#include "engine.h"
#include "shader.h"
#include "sparse_set.hpp"

#include "edit_octree.h"
#include "octree.h"

#include "chunk_mesher.hpp"
#include "thread_safe.hpp"
#include "utils.hpp"
#include "world_data.hpp"

#define VERTICES_PER_FACE 6

struct ChunkMesherTaskData
{
    DrawArraysIndirectCommand cmd;
    uint32_t aabb;
    ChunkKey key;
    bool remesh;
};

struct ChunkMesherDeltaTaskData
{
    OctreeClipmap::LeavesVector* chunks;
    OctreeClipmap::LeavesVector* chunks_removed;
    ChunkDelta delta;
};

inline std::atomic<double> g_meshing_time_sum = 0;
inline std::atomic<double> g_meshing_count = 0;
inline std::atomic<double> g_generating_time_sum = 0;
inline std::atomic<double> g_generating_count = 0;
inline std::atomic_bool g_mesh_naive = false;

class UpdateGreedyMeshTask
{
public:
    MemoryManager* manager = nullptr;
    GpuBufferMapping* gpu_buffer_mapping = nullptr;
    WorldData* world_data = nullptr;
    WorldEdits* edits = nullptr;

    bool remesh;
    OctreeClipmap::LeavesVector* chunks = nullptr;
    OctreeClipmap::LeavesVector* chunks_removed = nullptr;
    size_t begin;
    size_t end;
    uint32_t voxels_per_axis;
    std::atomic_uint32_t* tasks_counter = nullptr;
    const OctreeClipmap::DeltasVector* deltas = nullptr;
    ThreadSafeQueue<ChunkMesherTaskData>* chunks_to_commit = nullptr;
    ThreadSafeQueue<ChunkMesherDeltaTaskData>* deltas_to_commit = nullptr;
    TerrainStorage* terrain_storage = nullptr;

    std::vector<ChunkMesherTaskData>* chunks_to_commit_vec = nullptr;

    void operator()(ArenaAllocator* allocator)
    {
        size_t size_2d = glm::pow(world_data->voxels_per_chunk_axis + 2, 2u);
        size_t size_3d = glm::pow(world_data->voxels_per_chunk_axis + 2, 3u);

        voxel_data = allocator->allocate<VoxelData>(1);
        faces_buffer = allocator->allocate<GreedyFace>(size_3d * 6);

        structures = allocator->allocate<WorldInstance>(max_structures);

        if (voxel_data == nullptr || faces_buffer == nullptr || structures == nullptr)
        {
            LOG("ALLOCATION FAIL");
            exit(1);
        }

        for (size_t i = begin; i < end; i++)
        {
            ChunkDelta delta = (*deltas)[i];
            for (size_t j = delta.created.begin; j < delta.created.end; j++)
            {
                process_chunk(j);
            }

            commit_delta(delta);
        }
        tasks_counter->fetch_sub(1);
    }

private:
    uint32_t max_structures = 16;

    VoxelData* voxel_data;
    GreedyFace* faces_buffer;
    WorldInstance* structures;

    void commit_delta(const ChunkDelta& delta)
    {
        ChunkMesherDeltaTaskData delta_data;
        delta_data.chunks = chunks;
        delta_data.chunks_removed = chunks_removed;
        delta_data.delta = delta;
        deltas_to_commit->Enqueue(delta_data);
    }

    bool generate_and_mesh(const ChunkKey& key, ChunkGreedyMesherResult* result)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        HeightMap& data = terrain_storage->get_heightmap(key, *world_data);

        if (!voxel_data->compute_terrain(key, data, *world_data, edits, structures, max_structures))
            return false;
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        auto tmesh0 = std::chrono::high_resolution_clock::now();
        if (g_mesh_naive)
            *result = mesh_naive(voxel_data, faces_buffer, voxels_per_axis);
        else
            *result = mesh_greedy(voxel_data, faces_buffer, voxels_per_axis);
        auto tmesh1 = std::chrono::high_resolution_clock::now();
        double elapsed_mesh_ms = std::chrono::duration<double, std::milli>(tmesh1 - tmesh0).count();

        if (result->face_count > 0)
        {
            g_meshing_count += 1;
            g_meshing_time_sum += elapsed_mesh_ms;

            g_generating_count += 1;
            g_generating_time_sum += elapsed_ms;
        }
        return true;
    }

    void process_chunk(uint32_t index)
    {
        ChunkKey& key = (*chunks)[index];
        ChunkMesherTaskData chunk_data = {};
        chunk_data.key = key;

        ChunkGreedyMesherResult result;
        if (!generate_and_mesh(key, &result))
        {
            (*chunks_to_commit_vec)[index] = chunk_data;
            return;
        }

        if (result.face_count > 0)
        {
            size_t size_bytes = result.face_count * sizeof(GreedyFace);
            offset_t offset_bytes;
            if (manager->alloc(size_bytes, offset_bytes))
            {
                memcpy(static_cast<uint8_t*>(gpu_buffer_mapping->buffer) + offset_bytes,
                       faces_buffer, size_bytes);

                DrawArraysIndirectCommand cmd;
                cmd.baseInstance = 0;
                cmd.first = (offset_bytes * VERTICES_PER_FACE) / sizeof(GreedyFace);
                cmd.count = (size_bytes * VERTICES_PER_FACE) / sizeof(GreedyFace);
                cmd.instanceCount = 1;

                chunk_data.key = key;
                chunk_data.aabb = make_aabb(glm::ivec4(0),
                                            glm::ivec4(64)); // FIX: incorrect
                chunk_data.cmd = cmd;
                chunk_data.remesh = remesh;

                chunks_to_commit->Enqueue(chunk_data);
                (*chunks_to_commit_vec)[index] = chunk_data;
            }
            else
            {
                LOG("OUT OF MEMORY (tried to alloc {}B)", size_bytes);
            }
        }
        else if (remesh)
        {
            DrawArraysIndirectCommand cmd;
            cmd.baseInstance = 0;
            cmd.first = 0;
            cmd.count = 0;
            cmd.instanceCount = 1;

            ChunkMesherTaskData remesh_chunk_data;
            remesh_chunk_data.key = key;
            remesh_chunk_data.aabb = 0;
            remesh_chunk_data.cmd = cmd;
            remesh_chunk_data.remesh = remesh;

            chunks_to_commit->Enqueue(remesh_chunk_data);
        }
        (*chunks_to_commit_vec)[index] = chunk_data;
    }
};

struct TaskGen
{
    UpdateGreedyMeshTask task;
    uint64_t data_count;
    uint32_t batch_size;

    UpdateGreedyMeshTask operator()(uint32_t i)
    {
        task.begin = i * batch_size;
        task.end = glm::min(task.begin + batch_size, data_count);

        return task;
    }
};

struct DeferredFree
{
    offset_t offset_bytes;
    size_t size_bytes;
    uint64_t timestamp;
};

struct BatchedTasks
{
    bool try_submit_batch(ThreadPool& tp, uint32_t task_count, std::atomic_uint32_t* counter,
                          std::function<UpdateGreedyMeshTask(uint32_t i)> generator)
    {
        if (!all_tasks_completed(counter))
            return false;

        submit_batch(tp, task_count, counter, generator);
        return true;
    }

    void submit_batch(ThreadPool& tp, uint32_t task_count, std::atomic_uint32_t* counter,
                      std::function<UpdateGreedyMeshTask(uint32_t i)> generator)
    {
        counter->fetch_add(task_count);
        for (uint32_t i = 0; i < task_count; i++)
        {
            tp.submit(generator(i));
        }
    }

    static bool all_tasks_completed(std::atomic_uint32_t* counter) { return counter->load() == 0; }
};

class World
{
public:
    void create(const WorldData& data, const OctreeClipmapGenerateSettings& settings);
    void update(const glm::vec3& player_position, float fov);
    void render(const glm::vec3& world_origin, const FirstPersonCamera& camera,
                const glm::ivec3& camera_chunk_coord, float camera_chunk_size);
    void destroy();
    structure_id create_structure(OctreeStructure* structure);
    void place_structure(structure_id handle, const glm::vec3& position);

    void update_settings(const OctreeClipmapGenerateSettings& settings);
    void regenerate_chunks(const glm::vec3& player_position);
    void get_loaded_chunks_in_area(std::vector<ChunkKey>* out_chunks, const aabb3d& bounds);
    void get_chunks_in_area(std::vector<ChunkKey>* out_chunks, const aabb3d& bounds);
    bool display_chunks = false;

    void debug_ui()
    {
        ImGui::Separator();
        ImGui::Text("world_debug");
        ImGui::Text("to mesh: %u\nto remesh: %u", m_chunks_to_mesh_counter.load(),
                    m_chunks_to_remesh_counter.load());
        ImGui::Text("chunks_allocated: %u", get_chunks_allocated());
        ImGui::Text("nodes_created: %u", get_tree_node_size());
        ImGui::Text("chunks_with_meshes_troughput: %f/s", get_meshed_chunks_troughput());
        ImGui::Text("chunks_all_troughput: %f/s", get_all_chunks_troughput());
    }

    OctreeClipmapGenerateSettings& get_settings() { return m_settings; }
    MemoryManager& get_memory_allocator() { return m_world_buffer_manager; }
    uint64_t get_chunks_allocated() { return m_chunk_draw_cmds.get_keys().size(); }
    uint64_t get_tree_node_size() { return m_clipmap.nodes.size(); }
    const WorldData& get_data() { return m_data; }
    WorldEdits& get_edits() { return m_edits; }
    double get_meshed_chunks_troughput() { return m_nonempty_chunks_count / m_total_chunks_time_s; }
    double get_all_chunks_troughput() { return m_total_chunks_count / m_total_chunks_time_s; }

private:
    bool erase_chunk(const ChunkKey& key);

    void submit_tasks(OctreeClipmap::LeavesVector* chunks,
                      OctreeClipmap::LeavesVector* chunks_removed,
                      const OctreeClipmap::DeltasVector* deltas, bool remesh,
                      std::atomic_uint32_t* counter);
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
    OctreeClipmap::DeltasVector m_chunk_deltas;

    std::atomic_uint32_t m_chunks_to_mesh_counter = 0;
    std::atomic_uint32_t m_chunks_to_remesh_counter = 0;

    std::queue<WorldInstance> m_placed_instances;

    ThreadSafeQueue<ChunkMesherTaskData> m_chunks_to_submit;

    ThreadSafeQueue<ChunkMesherTaskData> m_chunks_to_commit;
    ThreadSafeQueue<ChunkMesherDeltaTaskData> m_deltas_to_commit;
    std::vector<ChunkMesherTaskData> m_chunks_to_commit_vec;

    // TODO: Combine parallel sparse sets
    GpuBuffer m_chunk_aabbs_buffer;
    SparseSet<ChunkKey, uint32_t> m_chunk_aabbs;

    GpuBuffer m_chunk_draw_cmds_buffer;
    SparseSet<ChunkKey, DrawArraysIndirectCommand> m_chunk_draw_cmds;
    std::queue<DeferredFree> m_deferred_frees;
    uint64_t m_current_frame = 0;
    ShaderProgram m_sp;

    bool m_chunks_dirty = false;
    bool is_dispatched = false;
    std::chrono::high_resolution_clock::time_point m_chunks_start_time;
    std::chrono::high_resolution_clock::time_point m_chunks_end_time;
    double m_total_chunks_time_s = 0.0;
    uint32_t m_total_chunks_count = 0;
    uint32_t m_nonempty_chunks_count = 0;
    uint32_t m_empty_chunks_count = 0;

    TerrainStorage m_terrain_storage;
};