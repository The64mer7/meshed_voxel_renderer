#pragma once
#include <atomic>
#include <queue>
#include <stdint.h>

#include "world_tasks.hpp"

#include "engine.h"
#include "shader.h"
#include "sparse_set.hpp"

#include "edit_octree.h"
#include "octree.h"

#include "chunk_mesher.hpp"
#include "thread_safe.hpp"
#include "utils.hpp"

#include "world_data.hpp"
#include "world_metrics.hpp"

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

        ImGui::Text("noise calls %u", TerrainNoise::noise_calls.load());
        ImGui::Text("potential noise calls %u", TerrainNoise::potential_noise_calls.load());

        if (ImGui::Button("clear noise calls"))
        {
            TerrainNoise::noise_calls.store(0);
            TerrainNoise::potential_noise_calls.store(0);
        }

        ImGui::Text("new_algorithm_vs_prev_speedup %f",
                    float(TerrainNoise::potential_noise_calls.load()) /
                        TerrainNoise::noise_calls.load());
        ImGui::Checkbox("pause update", &m_pause_update);
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

    std::vector<uint32_t> m_delta_chunks_remaining;
    std::vector<uint32_t> m_chunk_to_delta;

    ThreadSafeQueue<ChunkMesherTaskData> m_chunks_to_commit;
    ThreadSafeQueue<ChunkMesherDeltaTaskData> m_deltas_to_commit;
    std::vector<ChunkMesherTaskData> m_chunks_to_commit_vec;

    // TODO: Combine parallel sparse sets
    GpuBuffer m_chunk_aabbs_buffer;
    SparseSet<ChunkKey, packed_aabb64> m_chunk_aabbs;

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

    bool m_pause_update = false;

    ChunkGenDebugContext m_debug_context;
};