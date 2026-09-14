#pragma once

#include <glm/glm.hpp>

#include "chunk_mesher.hpp"
#include "edit_octree.h"
#include "engine.h"
#include "octree.h"
#include "sparse_set.hpp"
#include "world_data.hpp"
#include "world_metrics.hpp"

#define VERTICES_PER_FACE 6

struct ChunkMesherTaskData
{
    DrawArraysIndirectCommand cmd;
    packed_aabb64 aabb;
    ChunkKey key;
    bool remesh;
};

struct ChunkMesherDeltaTaskData
{
    OctreeClipmap::LeavesVector* chunks;
    OctreeClipmap::LeavesVector* chunks_removed;
    ChunkDelta delta;
};

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
                chunk_data.aabb = voxel_data->packed_aabb;

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
