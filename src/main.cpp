#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/vec_swizzle.hpp>

#define WORLD_SIZE (1024.f * 32*32)

#include <thread>
#include <mutex>
#include <condition_variable>
#include <shared_mutex>
#include <bitset>
#include <atomic>
#include <unordered_map>
#include <bit>
#include <queue>
#include <FastNoise/FastNoise.h>
#include <glm/gtc/matrix_access.hpp>

#include <stdio.h>

#include "engine.h"
#include "octree.h"
#include "camera.hpp"
#include "buffer.hpp"
#include "allocator.hpp"
#include "shader.h"

#include <atomic>
#include <format>

#define MAKE_COLOR(r,g,b,a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

template <glm::length_t L, typename T, glm::qualifier Q>
void print_vec(const std::string& label, const glm::vec<L, T, Q>& v) {
    std::cout << label << ": [ ";

    // glm::value_ptr returns a pointer to the first element
    const T* data = glm::value_ptr(v);

    for (glm::length_t i = 0; i < L; ++i) {
        std::cout << data[i] << (i < L - 1 ? ", " : "");
    }

    std::cout << " ]" << std::endl;
}

template<typename T>
class ThreadSafeStack
{
public:
    void Push(const T& value)
    {
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Stack.push(value);
        }
        m_Cv.notify_one();
    }

    void WaitAndPop(T& out)
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_Cv.wait(lock, [this] {return !m_Stack.empty(); });

        out = std::move(m_Stack.top());
        m_Stack.pop();
    }

    bool TryPop(T& out)
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        if (m_Stack.empty())
            return false;

        out = std::move(m_Stack.top());
        m_Stack.pop();

        return true;
    }

    bool IsEmpty()
    {
        std::lock_guard lock(m_Mutex);
        return m_Stack.empty();
    }

    size_t Size()
    {
        std::lock_guard lock(m_Mutex);
        return m_Stack.size();
    }

private:
    std::mutex m_Mutex;
    std::condition_variable m_Cv;
    std::stack<T> m_Stack;
};

struct DataResult
{
    void* addr;
    size_t size;
    int id;

    std::mutex mtx;
    std::condition_variable cv;
    bool consumed;

    glm::ivec4 key;

    void Consume()
    {
        {
            std::lock_guard lock(mtx);
            consumed = true;
        }
        cv.notify_one();
    }
};

std::mutex coutMtx;

struct DrawArraysIndirectCommand
{
    uint32_t count = 0u;
    uint32_t instanceCount = 1u;
    uint32_t first = 0u;
    uint32_t baseInstance = 0u;
};

enum world_preset
{
    Mountains, Desert, count
};

world_preset preset = world_preset::Mountains;

static FastNoise::SmartNode<FastNoise::Perlin> sourceNode;
static FastNoise::SmartNode<FastNoise::FractalFBm> noiseNode;

void SetupNoise() {
    sourceNode = FastNoise::New<FastNoise::Perlin>();

    noiseNode = FastNoise::New<FastNoise::FractalFBm>();
    

    switch (preset)
    {
    case world_preset::Mountains:
    {
        noiseNode->SetSource(sourceNode);
        noiseNode->SetOctaveCount(7);
        noiseNode->SetLacunarity(2.f);
        noiseNode->SetGain(0.5f);
    }
        break;
    case world_preset::Desert:
    {
        noiseNode->SetSource(sourceNode);
        noiseNode->SetOctaveCount(5);
        noiseNode->SetLacunarity(2.f);
        noiseNode->SetGain(0.2f);
    }
        break;
    case world_preset::count:
        break;
    default:
        break;
    }
}

float hash(float n) 
{
    return glm::fract(191122.518925 + glm::sin(n) * 43758.5453123);
}

enum FrustumResult { OUTSIDE, INTERSECT, INSIDE };

void extract_frustum(const glm::mat4& vp, glm::vec4* planes) {
    planes[0] = glm::row(vp, 3) + glm::row(vp, 0);
    planes[1] = glm::row(vp, 3) - glm::row(vp, 0);
    planes[2] = glm::row(vp, 3) + glm::row(vp, 1);
    planes[3] = glm::row(vp, 3) - glm::row(vp, 1);
    planes[4] = glm::row(vp, 3) + glm::row(vp, 2);
    planes[5] = glm::row(vp, 3) - glm::row(vp, 2);

    for (int i = 0; i < 6; ++i) {
        float length = glm::length(glm::vec3(planes[i]));
        planes[i] /= length;
    }
}
FrustumResult frustum_aabb(const glm::vec4* planes, const glm::vec3& box_min, const glm::vec3& box_max) {
    bool allInside = true;

    for (int i = 0; i < 6; ++i) {
        glm::vec3 p;
        p.x = (planes[i].x > 0) ? box_max.x : box_min.x;
        p.y = (planes[i].y > 0) ? box_max.y : box_min.y;
        p.z = (planes[i].z > 0) ? box_max.z : box_min.z;

        if (glm::dot(glm::vec3(planes[i]), p) + planes[i].w < 0)
            return OUTSIDE;

        glm::vec3 n;
        n.x = (planes[i].x > 0) ? box_min.x : box_max.x;
        n.y = (planes[i].y > 0) ? box_min.y : box_max.y;
        n.z = (planes[i].z > 0) ? box_min.z : box_max.z;

        if (glm::dot(glm::vec3(planes[i]), n) + planes[i].w < 0)
            allInside = false;
    }

    return allInside ? INSIDE : INTERSECT;
}

std::vector<glm::vec4> spheresLoaded;
class ThreadGenerator
{
public:
    std::vector<float> m_HeightMap;
    std::vector<float> m_DensityMap;
    void Init()
    {
        static bool once = [&]()
            {
                SetupNoise();
                return true;
            }();
        m_HeightMap.resize(glm::pow(2 + m_ChunkAxisCount, 2u));
        m_DensityMap.resize(glm::pow(2 + m_ChunkAxisCount, 3u));
    }

    auto ComputeHeightMap(glm::vec2 origin, int lod, int seed)
    {
        // A standard frequency for a large world
        float worldFrequency = 0.5f;

        // 1. Scale the origin so the noise 'matches' world coordinates
        float noiseX = origin.x * worldFrequency;
        float noiseY = origin.y * worldFrequency;

        // 2. Scale the step size so we don't skip over the mountains
        float step = VoxelSize(lod) * worldFrequency;
        origin -= step;

        return noiseNode->GenUniformGrid2D(
            m_HeightMap.data(),
            noiseX, noiseY,
            m_ChunkAxisCount + 2, m_ChunkAxisCount + 2,
            step, step,
            seed
        );
    }

    auto ComputeDensityMap(glm::vec3 origin, int lod, int seed)
    {
        // A standard frequency for a large world
        float worldFrequency = 0.5f;

        // 1. Scale the origin so the noise 'matches' world coordinates
        float noiseX = origin.x * worldFrequency;
        float noiseY = origin.y * worldFrequency;
        float noiseZ = origin.z * worldFrequency;

        // 2. Scale the step size so we don't skip over the mountains
        float step = VoxelSize(lod) * worldFrequency;
        origin -= step;

        return noiseNode->GenUniformGrid3D(
            m_DensityMap.data(),
            noiseX, noiseY, noiseZ,
            m_ChunkAxisCount + 2, m_ChunkAxisCount + 2, m_ChunkAxisCount + 2,
            step, step, step,
            seed
        );
    }

    float ApplyAmplitude(float v)
    {
        return 64.0f + (32.0f * v);
    }

    float GetHeight(int x, int y, uint32_t* outColor) // -1 to m_ChunkAxisCount
    {
        x += 1; y += 1;
        float h = ApplyAmplitude(m_HeightMap[x + (m_ChunkAxisCount+2) * y]);

        float rand0 = (hash(h) - 0.5f) * 8;
        float rand1 = (hash(rand0) - 0.5f) * 10;
        float rand2 = (hash(rand1) - 0.5f) * 4;
        
        switch (preset)
        {
        case Mountains:
        {
            if (h <= 20) *outColor = MAKE_COLOR(160 + rand0, 150 + rand0, 245 + rand0, 255);
            else if (h <= 64) *outColor = MAKE_COLOR(200 + rand0, 240 + rand0, 190 + rand0, 255);
            else *outColor = MAKE_COLOR(245 + rand1, 245 + rand1, 245 + rand1, 255);
        }
            break;
        case Desert:
        {
            *outColor = MAKE_COLOR(242 + rand0, 233 + rand0, 131 + rand0, 255);
        }
            break;
        case count:
            break;
        default:
            break;
        }

        return glm::max(20.f, h);
    }

    float GetDensity(int x, int y, int z, uint32_t* outColor) // -1 to m_ChunkAxisCount
    {
        x += 1; y += 1; z += 1;
        uint32_t stride = (m_ChunkAxisCount + 2);
        float d = m_DensityMap[x + stride * y + stride * stride * z];

        float rand0 = (hash(d) - 0.5f) * 8;

        *outColor = MAKE_COLOR(205 + rand0, 205 + rand0, 205 + rand0, 255);
        return d+0.5;
    }

    bool LoadChunk(glm::ivec4 chunkLod, DataResult* outResult)
    {
        m_StagingVertices.clear();
        int32_t lod = chunkLod.w;

        float chunkSize = ChunkSize(lod);
        float voxelSize = VoxelSize(lod);
         
        glm::vec3 chunkOrigin = chunkSize * glm::vec3(glm::xyz(chunkLod));
        uint32_t maxIndex = glm::pow(m_ChunkAxisCount, 3);

        auto bounds = ComputeHeightMap(glm::xz(chunkOrigin), lod, 191122);

        if (ApplyAmplitude(bounds.max) < chunkOrigin.y)
            return false;

        for (uint32_t z = 0; z < m_ChunkAxisCount; z++)
            for (uint32_t x = 0; x < m_ChunkAxisCount; x++)
            {
                uint32_t col;
                float h = GetHeight(x, z, &col);
                glm::vec3 voxelOrigin = chunkOrigin + glm::vec3(x,0,z) * voxelSize;
                for (int i = 0; i < spheresLoaded.size(); ++i)
                {
                    glm::vec4 sp = spheresLoaded[i];
                    if (glm::distance(glm::xz(sp), glm::xz(voxelOrigin)) > sp.w)
                        continue;
                    float sp_h = glm::sqrt(glm::pow(sp.w, 2.f) - glm::dot(glm::xz(voxelOrigin), glm::xz(voxelOrigin)));

                    h = glm::max(sp_h, h);
                }
                int y_coord = glm::floor((h - chunkOrigin.y) / voxelSize);

                for (uint32_t i = 0; i < 3; i++)
                {
                    uint32_t y = y_coord - i;
                    glm::ivec3 xyz = { x,y,z };
                    voxelOrigin = chunkOrigin + glm::vec3(xyz) * voxelSize;

                    //if (voxelOrigin.y < chunkOrigin.y || voxelOrigin.y >= chunkOrigin.y + chunkSize)
                    //    break;
                    // X NX Y NY Z NZ
                    bool positive = true;
                    for (uint32_t i = 0b1; i < 0b1000000; i <<= 1, positive = !positive)
                    {
                        glm::ivec3 idelta;
                        idelta.x = ((i & 0b1) ? 1 : 0) - ((i & 0b10) ? 1 : 0);
                        idelta.y = ((i & 0b100) ? 1 : 0) - ((i & 0b1000) ? 1 : 0);
                        idelta.z = ((i & 0b10000) ? 1 : 0) - ((i & 0b100000) ? 1 : 0);
                        
                        glm::vec3 delta = idelta;;
                        delta *= voxelSize;
                        uint32_t currAxis = (std::bit_width(i) - 0b1) >> 1;

                        glm::vec3 vertDelta = glm::greaterThan(delta, glm::vec3(0.f));
                        glm::vec3 currFaceOrigin = voxelOrigin + vertDelta * voxelSize;
                        if (lod < 10) currFaceOrigin.y -= voxelSize;

                        glm::vec3 neighborOrigin = voxelOrigin + delta;

                        int32_t nx = x + idelta.x;
                        int32_t nz = z + idelta.z;

                        uint32_t ncol;
                        float nh = GetHeight(nx, nz, &ncol);
                        /*
                        for (int i = 0; i < spheresLoaded.size(); ++i)
                        {
                            glm::vec4 sp = spheresLoaded[i];
                            if (glm::distance(glm::xz(sp), glm::xz(neighborOrigin)) > sp.w)
                                continue;
                            float sp_h = glm::sqrt(glm::pow(sp.w, 2.f) - glm::dot(glm::xz(neighborOrigin), glm::xz(neighborOrigin)));

                            nh = glm::max(sp_h, nh);
                        }
                        */

                        if (nh < neighborOrigin.y) // should emit face
                        {
                            glm::vec4 face[4];
                            glm::ivec2 axes = glm::ivec2(
                                (currAxis + 1) % 3,
                                (currAxis + 2) % 3
                            );
                            for (int f = 0; f < 4; f++)
                            {
                                glm::ivec2 mask = { f % 2, f / 2 };
                                glm::vec3 offset(0.f);
                             
                                offset[axes.x] = mask.x;
                                offset[axes.y] = mask.y;

                                face[f] = glm::vec4(currFaceOrigin + voxelSize * offset - chunkOrigin, std::bit_cast<float>(col));
                            }
                            m_StagingVertices.push_back(face[0]);
                            m_StagingVertices.push_back(face[positive ? 1 : 2]);
                            m_StagingVertices.push_back(face[positive ? 2 : 1]);

                            m_StagingVertices.push_back(face[positive ? 2 : 1]);
                            m_StagingVertices.push_back(face[positive ? 1 : 2]);
                            m_StagingVertices.push_back(face[3]);
                        }
                    }
                }
            }
        if (m_StagingVertices.empty())
            return false;

        outResult->addr = m_StagingVertices.data();
        outResult->size = m_StagingVertices.size() * sizeof(glm::vec4);
        outResult->consumed = false;

        return true;
    }

    bool LoadChunk3D(glm::ivec4 chunkLod, DataResult* outResult)
    {
        m_StagingVertices.clear();
        int32_t lod = chunkLod.w;

        float chunkSize = ChunkSize(lod);
        float voxelSize = VoxelSize(lod);

        glm::vec3 chunkOrigin = chunkSize * glm::vec3(glm::xyz(chunkLod));
        uint32_t maxIndex = glm::pow(m_ChunkAxisCount, 3);

        auto bounds_2d = ComputeHeightMap(glm::xz(chunkOrigin), lod, 191122);
        if (ApplyAmplitude(bounds_2d.max) < chunkOrigin.y)
            return false;

        auto bounds = ComputeDensityMap(chunkOrigin, lod, 191122);


        for (uint32_t z = 0; z < m_ChunkAxisCount; z++)
            for (uint32_t x = 0; x < m_ChunkAxisCount; x++)
                for (uint32_t y = 0; y < m_ChunkAxisCount; y++)
                {
                    glm::ivec3 xyz = { x,y,z };
                    glm::vec3 voxelOrigin = chunkOrigin + glm::vec3(xyz) * voxelSize;
                    
                    uint32_t col;
                    float d = GetDensity(x, y, z, &col);
                    float h = GetHeight(x, z, &col);
                    if (voxelOrigin.y > h)
                        break;

                    if (d <= 0.f)
                        continue;

                    // X NX Y NY Z NZ
                    bool positive = true;
                    for (uint32_t i = 0b1; i < 0b1000000; i <<= 1, positive = !positive)
                    {
                        glm::ivec3 idelta;
                        idelta.x = ((i & 0b1) ? 1 : 0) - ((i & 0b10) ? 1 : 0);
                        idelta.y = ((i & 0b100) ? 1 : 0) - ((i & 0b1000) ? 1 : 0);
                        idelta.z = ((i & 0b10000) ? 1 : 0) - ((i & 0b100000) ? 1 : 0);

                        glm::vec3 delta = idelta;;
                        delta *= voxelSize;
                        uint32_t currAxis = (std::bit_width(i) - 0b1) >> 1;

                        glm::vec3 vertDelta = glm::greaterThan(delta, glm::vec3(0.f));
                        glm::vec3 currFaceOrigin = voxelOrigin + vertDelta * voxelSize;

                        glm::vec3 neighborOrigin = voxelOrigin + delta;

                        int32_t nx = x + idelta.x;
                        int32_t ny = y + idelta.y;
                        int32_t nz = z + idelta.z;

                        uint32_t ncol;
                        float nd = GetDensity(nx, ny, nz, &ncol);
                        float nh = GetHeight(nx, nz, &ncol);

                        bool is_neighbor_empty = nd <= 0.f || nh < neighborOrigin.y;
                        if (is_neighbor_empty) // should emit face
                        {
                            glm::vec4 face[4];
                            glm::ivec2 axes = glm::ivec2(
                                (currAxis + 1) % 3,
                                (currAxis + 2) % 3
                            );
                            for (int f = 0; f < 4; f++)
                            {
                                glm::ivec2 mask = { f % 2, f / 2 };
                                glm::vec3 offset(0.f);

                                offset[axes.x] = mask.x;
                                offset[axes.y] = mask.y;

                                face[f] = glm::vec4(currFaceOrigin + voxelSize * offset - chunkOrigin, std::bit_cast<float>(col));
                            }
                            m_StagingVertices.push_back(face[0]);
                            m_StagingVertices.push_back(face[positive ? 1 : 2]);
                            m_StagingVertices.push_back(face[positive ? 2 : 1]);

                            m_StagingVertices.push_back(face[positive ? 2 : 1]);
                            m_StagingVertices.push_back(face[positive ? 1 : 2]);
                            m_StagingVertices.push_back(face[3]);
                        }
                    }
                }
        if (m_StagingVertices.empty())
            return false;

        outResult->addr = m_StagingVertices.data();
        outResult->size = m_StagingVertices.size() * sizeof(glm::vec4);
        outResult->consumed = false;

        return true;
    }

    float VoxelSize(int32_t lod)
    {
        return ChunkSize(lod) / m_ChunkAxisCount;
    }

    float ChunkSize(int32_t lod)
    {
        return m_RootSize / glm::pow(2, lod);
    }

    float m_RootSize = WORLD_SIZE;
    uint32_t m_ChunkAxisCount = 32;

    std::vector<glm::vec4> m_StagingVertices;
};

ThreadSafeQueue<glm::vec3> requestQueue;
ThreadSafeQueue<glm::ivec4> taskQueue;
ThreadSafeQueue<DataResult*> resultQueue;

struct builder_info
{
    glm::vec3 p;
    FlatOctree* tree;
};

uint8_t octree_generate_max_depth = 15;
void octree_generate(FlatOctree* octree, glm::vec3 p)
{
    //octree->Generate(4, octree_generate_max_depth, p.x, p.y, p.z, 0.1f, 512.f,2.f);
    octree->Generate(4, octree_generate_max_depth, p.x, p.y, p.z, 0.f, 1024.f * 64, 512.f);
}

std::mutex tree_builder_mtx;
std::condition_variable tree_builder_cv;
builder_info tree_builder_info;

enum builder_state
{
    IDLE,
    REQUEST,
    READY
};
std::atomic<builder_state> tree_builder_state = builder_state::IDLE;

typedef struct Entity
{
    glm::vec3 position;
    glm::vec3 direction;
    glm::vec3 velocity;
} Entity;

struct app_data_t
{
    Entity player;
    Engine* engine;
    FirstPersonCamera camera;
    glm::ivec3 camera_chunk_pos = glm::ivec3(0);

    std::vector<std::thread> generator_workers;
    std::thread tree_builder_worker;

    uint32_t dummy_vao;
    FlatOctree octree;
    float octree_rebuild_distance_treshold;
    glm::vec3 octree_build_pos;
    GpuBuffer vertex_buffer;
    MemoryManager vertex_free_list;

    std::unordered_map<packed_leaf3d_raw_t, size_t> loaded_chunk_to_cmd_idx_map;
    std::unordered_map<size_t, packed_leaf3d_raw_t> loaded_cmd_idx_to_chunk_map;

    std::vector<DrawArraysIndirectCommand> terrain_draw_cmds;
    GpuBuffer terrain_draw_cmds_buffer;
    std::vector<glm::ivec4> chunk_positions_cmds;
    std::unordered_set<glm::ivec4> chunk_positions_cmds_set;
    GpuBuffer chunk_positions_cmds_buffer;

    ShaderProgram render_shader_program;
};

static void tree_builder(app_data_t* data)
{
    while (true)
    {
        std::unique_lock lock(tree_builder_mtx);
        tree_builder_cv.wait(lock, [&]() {return tree_builder_state == builder_state::REQUEST; });
        
        octree_generate(&data->octree, tree_builder_info.p);
        tree_builder_state = builder_state::READY;

        data->octree.ForEachLeafRemoved(
            [&](packed_leaf3d_t leaf)
            {
                if (data->loaded_chunk_to_cmd_idx_map.find(leaf.packed) == data->loaded_chunk_to_cmd_idx_map.end())
                {
                    return;
                }
                size_t cmd_offset = data->loaded_chunk_to_cmd_idx_map[leaf.packed];

                DrawArraysIndirectCommand cmd = data->terrain_draw_cmds[cmd_offset];
                size_t swap_cmd_offset = data->terrain_draw_cmds.size() - 1;

                packed_leaf3d_raw_t swap_leaf = data->loaded_cmd_idx_to_chunk_map[swap_cmd_offset];

                data->terrain_draw_cmds[cmd_offset] = data->terrain_draw_cmds[swap_cmd_offset];
                data->chunk_positions_cmds[cmd_offset] = data->chunk_positions_cmds[swap_cmd_offset];

                data->terrain_draw_cmds.pop_back();
                data->chunk_positions_cmds.pop_back();
                data->chunk_positions_cmds_set.erase(leaf.packed);

                data->loaded_chunk_to_cmd_idx_map.erase(leaf.packed);
                data->loaded_cmd_idx_to_chunk_map.erase(swap_cmd_offset);

                data->loaded_chunk_to_cmd_idx_map[swap_leaf] = cmd_offset;
                data->loaded_cmd_idx_to_chunk_map[cmd_offset] = swap_leaf;

                data->vertex_free_list.AddMemoryBlock(cmd.first * sizeof(glm::vec4), cmd.count * sizeof(glm::vec4));
            }
        );
        data->vertex_free_list.Defragment();

        data->octree.ForEachLeafAdded(
            [&](packed_leaf3d_t leaf)
            {
                //printf("chunk request %i: %i, %i, %i\n", leaf.lod, leaf.x, leaf.y, leaf.z);
                taskQueue.Enqueue({ leaf.x, leaf.y, leaf.z, leaf.lod });
            }
        );

        tree_builder_state = builder_state::IDLE;
    }
}
uint64_t completed = 0;
uint64_t requested = 0;

std::atomic_uint64_t working_threads = 0u;
static void data_generator()
{
    static int idGenerator = 0;
    int id = idGenerator++;

    ThreadGenerator generator;
    generator.Init();
    uint64_t thread_bit = 1ull << uint64_t(id);

    while (true)
    {
        DataResult dataResult;
        glm::ivec4 chunkKey;
        taskQueue.WaitAndDequeue(chunkKey);
        working_threads.fetch_xor(thread_bit);

        if (generator.LoadChunk3D(chunkKey, &dataResult))
        {
            dataResult.key = chunkKey;
            //printf("chunk loaded %i: %i, %i, %i\n", chunkKey.w, chunkKey.x, chunkKey.y, chunkKey.z);
            resultQueue.Enqueue(&dataResult);

            std::unique_lock lock(dataResult.mtx);
            requested++;
            dataResult.cv.wait(lock, [&dataResult] {return dataResult.consumed; });
        }

        working_threads.fetch_xor(thread_bit);
    }
}

class OnceGuard
{
public:
    bool is_first(bool predicate)
    {
        bool is_active = predicate && !prev_predicate;
        prev_predicate = predicate;
        return is_active;
    }
private:
    bool prev_predicate = false;
};

typedef struct App
{
    Engine engine;
    app_data_t* data;
} App;

class ScopedTimer
{
public:
    ScopedTimer(std::string label)
    {
        m_label = label;
        m_start_time = glfwGetTime();
    }
    ~ScopedTimer()
    {
        m_end_time = glfwGetTime();
        printf("%s: %f\n", m_label.c_str(), (m_end_time - m_start_time) * 1000.f);
    }
private:
    double m_end_time;
    double m_start_time;
    std::string m_label;
};
int frame_update(void* user_data)
{

    static uint64_t total_added = 0;
    static uint64_t total_removed = 0;
    app_data_t* data = reinterpret_cast<app_data_t*>(user_data);
    FirstPersonCamera& cam = data->camera;
    static OnceGuard click_guard;
    static OnceGuard add_guard;
    static OnceGuard sub_guard;
    static OnceGuard treshold_guard;
    static uint64_t frame_index = 0;
    if (frame_index % 1024 == 0)
    {
        //data->vertex_free_list.DebugPrint(1024*1024*64);
        printf("tasks: %i\n", taskQueue.Size());
        printf("results: %i\n", resultQueue.Size());
        printf("requested: %i\n", requested);
        printf("completed: %i\n", completed);
        std::cout << "working threads: " << std::bitset<32>(working_threads.load()) << '\n';
    }

    //ScopedTimer timer("update");

    if (glfwGetKey(data->engine->window, GLFW_KEY_TAB) == GLFW_PRESS)
        printf("add: %u, rem: %u, diff: %u\n", total_added, total_removed, total_added - total_removed);
    frame_index++;
    if(frame_index % 16 == 0) glfwSetWindowTitle(data->engine->window, std::format("{:.3f}ms\t FPS: {:.0f}", 1000 * data->engine->delta_time, 1 / data->engine->delta_time).c_str());
    

    glm::vec3 cam_pos = cam.GetPosition();
    //if (glm::distance(data->octree_build_pos, cam_pos) > data->octree_rebuild_distance_treshold)

    if (glm::any(glm::greaterThanEqual(glm::abs(cam_pos), glm::vec3(8.f))))
    {
        glm::ivec3 chunk_offset = (cam_pos / 8.f);
        data->camera_chunk_pos += chunk_offset;
        data->camera.Translate(-glm::vec3(chunk_offset) * 8.f);
    }

    if(tree_builder_state == builder_state::IDLE)
        for (int i = 0; i < 32; i++)
        {
            DataResult* result;
            if (resultQueue.TryDequeue(result))
            {
                offset_t offset;
                size_t size = result->size;
                if (data->vertex_free_list.FindAndPopOffset(size, offset))
                {
                    data->vertex_buffer.Upload(offset, size, result->addr);
                    completed++;
                    DrawArraysIndirectCommand cmd;
                    cmd.baseInstance = 0;
                    cmd.count = size / sizeof(glm::vec4);
                    cmd.first = offset / sizeof(glm::vec4);
                    cmd.instanceCount = 1;

                    packed_leaf3d_t leaf_data;
                    leaf_data.x = result->key.x;
                    leaf_data.y = result->key.y;
                    leaf_data.z = result->key.z;
                    leaf_data.lod = result->key.w;

                    size_t cmd_idx = data->terrain_draw_cmds.size();

                    data->loaded_chunk_to_cmd_idx_map[leaf_data.packed] = cmd_idx;
                    data->loaded_cmd_idx_to_chunk_map[cmd_idx] = leaf_data.packed;

                    data->terrain_draw_cmds.push_back(cmd);
                    data->chunk_positions_cmds.push_back(leaf_data.packed);
                    data->chunk_positions_cmds_set.insert(leaf_data.packed);
                    //printf("saved chunk %i: %i, %i, %i in offset %i, size %i\n", leaf_data.lod, leaf_data.x, leaf_data.y, leaf_data.z, offset, size);
                }
                else
                {
                    //printf("not enough mem\n");
                }
                result->Consume();
            }
            else
                break;
        }

    if (
        (glm::distance(cam.GetPosition(), data->octree_build_pos) > data->octree_rebuild_distance_treshold ||
            click_guard.is_first(glfwGetKey(data->engine->window, GLFW_KEY_B) == GLFW_PRESS)
        ) &&
        working_threads.load() == 0u && taskQueue.Size() == 0 && 
        resultQueue.Size() == 0)
    {
        if (tree_builder_mtx.try_lock())
        {
            if (tree_builder_state == builder_state::IDLE)
            {
                tree_builder_info = { .p = cam_pos + glm::vec3(data->camera_chunk_pos)*8.f, .tree = &data->octree };
                tree_builder_state = builder_state::REQUEST;
                tree_builder_mtx.unlock();
                data->octree_build_pos = cam_pos;
                
                tree_builder_cv.notify_one();
            }
            else
                tree_builder_mtx.unlock();
        }
    }

    double xpos = 0, ypos = 0;
    glfwGetCursorPos(data->engine->window, &xpos, &ypos);
    static double lastX = xpos, lastY = ypos;

    float deltaX = (float)(xpos - lastX);
    float deltaY = (float)(lastY - ypos);

    lastX = xpos;
    lastY = ypos;
    
    static float camSpeed = 1.f;
    float speed = data->engine->delta_time * camSpeed;
    if (glfwGetKey(data->engine->window, GLFW_KEY_W) == GLFW_PRESS)
        cam.Translate(speed *cam.GetForwardVector());
    if (glfwGetKey(data->engine->window, GLFW_KEY_S) == GLFW_PRESS)
        cam.Translate(-speed *cam.GetForwardVector());
    if (glfwGetKey(data->engine->window, GLFW_KEY_A) == GLFW_PRESS)
        cam.Translate(speed * glm::normalize(glm::cross(WorldDirection::Up, cam.GetForwardVector())));
    if (glfwGetKey(data->engine->window, GLFW_KEY_D) == GLFW_PRESS)
        cam.Translate(-speed * glm::normalize(glm::cross(WorldDirection::Up, cam.GetForwardVector())));

    for (int i = 0; i < world_preset::count; i++)
    {
        if (glfwGetKey(data->engine->window, GLFW_KEY_1 + i) == GLFW_PRESS)
        {
            preset = world_preset(i);
            SetupNoise();
            data->octree_build_pos = data->player.position + 2 * data->octree_rebuild_distance_treshold; // FIX
            data->octree = FlatOctree();
            data->octree.s = WORLD_SIZE;
            data->loaded_chunk_to_cmd_idx_map.clear();
            data->loaded_cmd_idx_to_chunk_map.clear();
            data->chunk_positions_cmds.clear();
            data->chunk_positions_cmds_set.clear();
            data->terrain_draw_cmds.clear();
            resultQueue.Clear();
            taskQueue.Clear();
        }
    }

    if (add_guard.is_first(glfwGetKey(data->engine->window, GLFW_KEY_KP_ADD) == GLFW_PRESS))
        octree_generate_max_depth++;
    if (sub_guard.is_first(glfwGetKey(data->engine->window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS))
        octree_generate_max_depth--;

    if (glfwGetKey(data->engine->window, GLFW_KEY_SPACE) == GLFW_PRESS)
        cam.Translate(speed * WorldDirection::Up*1.0f);
    if (glfwGetKey(data->engine->window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        cam.Translate(-speed * WorldDirection::Up);

    if (glfwGetKey(data->engine->window, GLFW_KEY_F) == GLFW_PRESS)
        camSpeed *= glm::pow(8.0, data->engine->delta_time);
    if (glfwGetKey(data->engine->window, GLFW_KEY_R) == GLFW_PRESS)
        camSpeed /= glm::pow(8.0, data->engine->delta_time);
    camSpeed = glm::max(camSpeed, 0.01f);

    float sensitivity = 0.01f;
    cam.Rotate(sensitivity * deltaX, sensitivity * deltaY);

    ////printf("pos: %f, %f, %f dir: %f, %f, %f\n", 
    //    cam.GetPosition().x, cam.GetPosition().y, cam.GetPosition().z, 
    //    cam.GetForwardVector().x, cam.GetForwardVector().y, cam.GetForwardVector().z);


    return 0;
}

int frame_render(Renderer* renderer, void* user_data)
{
    //ScopedTimer timer("render");
    app_data_t* data = reinterpret_cast<app_data_t*>(user_data);
    glClearColor(0.7f, 0.7f, 0.9f, 1.f);
    {
        ShaderProgram& sp = data->render_shader_program;
        sp.bind();
        sp.uniform1ui("u_offset", 0u);
        sp.uniformMat4("u_view_matrix", data->camera.GetViewMatrix());
        if(glfwGetKey(data->engine->window, GLFW_KEY_C) == GLFW_PRESS)
            sp.uniformMat4("u_proj_matrix", data->camera.GetOrthoProjectionMatrix());
        else
            sp.uniformMat4("u_proj_matrix", data->camera.GetProjectionMatrix());
        
        glBindVertexArray(data->dummy_vao);

        sp.uniform1ui("u_render_cube", 1u);
        sp.uniform1f("u_world_size", WORLD_SIZE);

        if (glfwGetKey(data->engine->window, GLFW_KEY_TAB) == GLFW_PRESS)
            for (auto& leaf : data->chunk_positions_cmds)
            {
                glm::vec4 xyz_size;
                xyz_size.w = WORLD_SIZE / glm::pow(2, leaf.w);
                xyz_size.x = leaf.x * xyz_size.w;
                xyz_size.y = leaf.y * xyz_size.w;
                xyz_size.z = leaf.z * xyz_size.w;
                sp.uniform4f("u_cube_xyz_size", xyz_size);
                glDrawArrays(GL_LINES, 0, 24);
            }
        sp.uniform1ui("u_render_cube", 0u);
        
        sp.uniform1ui("u_render_triangle", 1u);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        sp.uniform1ui("u_render_triangle", 0u);
        sp.uniform3i("u_camera_chunk_pos", data->camera_chunk_pos);
#if 1
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, data->terrain_draw_cmds_buffer.Handle());
        glBufferData(GL_DRAW_INDIRECT_BUFFER,
            data->terrain_draw_cmds.size() * sizeof(DrawArraysIndirectCommand),
            data->terrain_draw_cmds.data(),
            GL_STREAM_DRAW);

        //glNamedBufferSubData(data->terrain_draw_cmds_buffer.Handle(),
        //    0, data->terrain_draw_cmds.size() * sizeof(DrawArraysIndirectCommand),
        //    data->terrain_draw_cmds.data());

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, data->chunk_positions_cmds_buffer.Handle());
        glBufferData(GL_SHADER_STORAGE_BUFFER,
            data->chunk_positions_cmds.size() * sizeof(glm::ivec4),
            data->chunk_positions_cmds.data(),
            GL_STREAM_DRAW);
        //glNamedBufferSubData(data->chunk_positions_cmds_buffer.Handle(),
        //    0, data->chunk_positions_cmds.size() * sizeof(glm::ivec4),
        //    data->terrain_draw_cmds.data());

        
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, data->vertex_buffer.Handle());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, data->chunk_positions_cmds_buffer.Handle());
        
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, data->terrain_draw_cmds_buffer.Handle());
        glMultiDrawArraysIndirect(GL_TRIANGLES, 0, data->terrain_draw_cmds.size(), sizeof(DrawArraysIndirectCommand));
        sp.unbind();
#endif
    }

    return 0;
}

int app_create(App* app)
{
    int width = 1920;
    int height = 1080;
    
    app->data = new app_data_t;
    if (!app->data)
        return 1;

    int engine_status = engine_init(&app->engine, frame_update, frame_render, app->data, width, height);
    
    for(int i = 0; i < 12; i++)
        app->data->generator_workers.emplace_back(data_generator);
    
    glfwSwapInterval(0);

    app_data_t* data = reinterpret_cast<app_data_t*>(app->data);
    data->engine = &app->engine;
    data->player.position = glm::vec3(0);
    data->player.direction = glm::vec3(0);
    data->player.velocity = glm::vec3(0);

    data->octree.s = WORLD_SIZE;

    data->octree_rebuild_distance_treshold = 0.25f;
    data->octree_build_pos = data->player.position + 2 * data->octree_rebuild_distance_treshold;
    tree_builder_info.tree = &app->data->octree;
    tree_builder_info.p = data->player.position;

    app->data->tree_builder_worker = std::thread(tree_builder, data);
    data->terrain_draw_cmds_buffer.Create();
    data->chunk_positions_cmds_buffer.Create();
    
    glCreateVertexArrays(1, &data->dummy_vao);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);

    {
        FirstPersonCameraSettings camera_settings;
        camera_settings.position = { 0,0,0 };
        camera_settings.direction = { 0,0,-1 };
        camera_settings.farPlane = 4*WORLD_SIZE;
        camera_settings.nearPlane = 0.125f;
        camera_settings.fov = 45.f;
        camera_settings.width = width;
        camera_settings.height = height;
        data->camera.Init(camera_settings);
    }

    {
        Shader vert;
        Shader frag;
        vert.createShader("shaders/default.vert", GL_VERTEX_SHADER);
        frag.createShader("shaders/default.frag", GL_FRAGMENT_SHADER);
        data->render_shader_program.createProgram(vert, frag);
        vert.cleanup();
        frag.cleanup();
    }

    data->vertex_buffer.Create();
    data->vertex_buffer.Allocate(GB(2), nullptr, GL_STREAM_DRAW);
    data->vertex_free_list.Init(data->vertex_buffer.SizeInBytes());
    data->vertex_free_list.AddMemoryBlock(0, data->vertex_free_list.GetMaxSize());

    return engine_status;
}

void app_run(App* app)
{
    engine_run(&app->engine);
}

void app_cleanup(App* app)
{
    engine_shutdown(&app->engine);

    delete app->data;
}

int main(void)
{
    App app;
    app_create(&app);
    app_run(&app);
    app_cleanup(&app);
}