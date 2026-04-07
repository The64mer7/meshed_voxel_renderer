#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/vec_swizzle.hpp>

#define WORLD_SIZEUI (1ull << 20ull)
#define WORLD_SIZE float(WORLD_SIZEUI)
#define CHUNK_SIZE(lod) (WORLD_SIZEUI >> (lod))
#define BITMASK(n) ((1 << (n)) - 1)

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
#include <omp.h>

#include <stb_image.h>
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
#define MAKE_COLOR_OFFSET(r, g, b, a, offset) ( \
    ((uint32_t)((r) + (offset)) & 0xFFu)        | \
    (((uint32_t)((g) + (offset)) & 0xFFu) << 8)  | \
    (((uint32_t)((b) + (offset)) & 0xFFu) << 16) | \
    (((uint32_t)(a) & 0xFFu) << 24)                \
)

#define PROFILER_DECLARE(name) \
double _profiler_##name##_count = 0.0;\
double _profiler_##name##_sum = 0.0;

#define PROFILER_BEGIN(name)\
double _profiler_##name##_t0 = glfwGetTime();

#define PROFILER_END(name)\
double _profiler_##name##_t1 = glfwGetTime();\
_profiler_##name##_count += 1.0;\
_profiler_##name##_sum += _profiler_##name##_t1 - _profiler_##name##_t0;

#define PROFILER_GET_AVG_NESTED(name, parent) parent._profiler_##name##_sum / parent._profiler_##name##_count
#define PROFILER_GET_AVG(name) _profiler_##name##_sum / _profiler_##name##_count

std::atomic<double> meshing_time_total = 0.0;
std::atomic<double> meshing_time_total_cnt = 0.0;

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

static FastNoise::SmartNode<FastNoise::FractalFBm> terrainNode;
static FastNoise::SmartNode<FastNoise::Multiply> detailTerrain;
auto terrainAmpNode = FastNoise::New<FastNoise::Constant>();

static FastNoise::SmartNode<FastNoise::FractalFBm> caveNode;

static FastNoise::SmartNode<FastNoise::Add> finalTerrain;

static FastNoise::SmartNode<FastNoise::FractalFBm> megaSource;
static FastNoise::SmartNode<FastNoise::Perlin> megaTerrainNode;
static FastNoise::SmartNode<FastNoise::Multiply> megaTerrainAmplitude;
static FastNoise::SmartNode<FastNoise::Add> megaTerrain;
auto megaCurve = FastNoise::New<FastNoise::PowFloat>();

auto zeroNode = FastNoise::New<FastNoise::Constant>();
auto ampNode = FastNoise::New<FastNoise::Constant>();
auto biasNode = FastNoise::New<FastNoise::Constant>();


void SetupNoise() {
    sourceNode = FastNoise::New<FastNoise::Perlin>();
    terrainNode = FastNoise::New<FastNoise::FractalFBm>();
    caveNode = FastNoise::New<FastNoise::FractalFBm>();
    finalTerrain = FastNoise::New<FastNoise::Add>();
    megaTerrainNode = FastNoise::New<FastNoise::Perlin>();
    zeroNode->SetValue(0.f);
    ampNode->SetValue(512.f);
    biasNode->SetValue(1024.f);
    terrainAmpNode->SetValue(32.f);

    megaTerrainAmplitude = FastNoise::New<FastNoise::Multiply>();
    megaTerrain = FastNoise::New<FastNoise::Add>();
    detailTerrain = FastNoise::New<FastNoise::Multiply>();

    switch (preset)
    {
    case world_preset::Mountains:
    {
        terrainNode->SetSource(sourceNode);
        terrainNode->SetOctaveCount(4);
        terrainNode->SetLacunarity(2.2f);
        terrainNode->SetGain(0.5f);

        detailTerrain->SetLHS(terrainNode);
        detailTerrain->SetRHS(terrainAmpNode);

        megaTerrainNode->SetScale(1024.f);
        megaTerrainAmplitude->SetLHS(ampNode);
        megaTerrainAmplitude->SetRHS(megaTerrainNode);


        megaTerrain->SetLHS(biasNode);
        megaTerrain->SetRHS(megaTerrainAmplitude);

        finalTerrain->SetLHS(megaTerrain);
        finalTerrain->SetRHS(detailTerrain);


        caveNode->SetSource(sourceNode);
        caveNode->SetOctaveCount(3);
        caveNode->SetLacunarity(2.5f);
        caveNode->SetGain(0.5f);
    }
        break;
    case world_preset::Desert:
    {
        terrainNode->SetSource(sourceNode);
        terrainNode->SetOctaveCount(5);
        terrainNode->SetLacunarity(2.f);
        terrainNode->SetGain(0.2f);

        caveNode->SetSource(sourceNode);
        caveNode->SetOctaveCount(3);
        caveNode->SetLacunarity(2.f);
        caveNode->SetGain(0.2f);
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

    for (int i = 0; i < 6; ++i) 
    {
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

std::vector<glm::vec4> edit_spheres_stack;
class ThreadGenerator
{
private:
    std::vector<float> m_HeightMap;
    std::vector<float> m_DensityMap;

public:

    void Init()
    {
        static bool once = [&]()
            {
                SetupNoise();
                return true;
            }();
        m_HeightMap.resize(glm::pow(2 + m_ChunkAxisCount, 2u));
        m_DensityMap.resize(glm::pow(2 + m_ChunkAxisCount, 3u));

        m_StagingVertices.resize(glm::pow(m_ChunkAxisCount, 3) / 2 * 6 * 4);

        m_solid_mask = new uint32_t[glm::pow(m_ChunkAxisCount + 2, 3)];
        m_bit_mask = new uint32_t[(glm::pow(m_ChunkAxisCount + 2, 3) + 31) / 32];
    }

    PROFILER_DECLARE(noise2d);
    auto ComputeHeightMap(glm::vec2 origin, int lod, int seed)
    {
        PROFILER_BEGIN(noise2d);

        float worldFrequency = 0.25f;

        float noiseX = origin.x * worldFrequency;
        float noiseY = origin.y * worldFrequency;

        float step = VoxelSize(lod) * worldFrequency;
        origin -= step;
        
        auto bounds = finalTerrain->GenUniformGrid2D(
            m_HeightMap.data(),
            noiseX, noiseY,
            m_ChunkAxisCount + 2, m_ChunkAxisCount + 2,
            step, step,
            seed
        );

        PROFILER_END(noise2d);

        return bounds;
    }

    inline float ApplyAmplitude(float v)
    {
        return v;
    }

    PROFILER_DECLARE(noise3d);
    auto ComputeDensityMap(glm::vec3 origin, int lod, int seed)
    {
        PROFILER_BEGIN(noise3d);

        float worldFrequency = 0.5f;

        float noiseX = origin.x * worldFrequency;
        float noiseY = origin.y * worldFrequency;
        float noiseZ = origin.z * worldFrequency;

        float step = VoxelSize(lod) * worldFrequency;
        origin -= step;

        auto bounds = caveNode->GenUniformGrid3D(
            m_DensityMap.data(),
            noiseX, noiseY, noiseZ,
            m_ChunkAxisCount + 2, m_ChunkAxisCount + 2, m_ChunkAxisCount + 2,
            step, step, step,
            seed
        );

        PROFILER_END(noise3d);

        return bounds;
    }

    inline float GetHeightOnly(int x, int z) // -1 to m_ChunkAxisCount
    {
        x += 1; z += 1;
        float h = ApplyAmplitude(m_HeightMap[x + (m_ChunkAxisCount + 2) * z]);
        return h;
    }
    inline float GetDensityOnly(int x, int y, int z) // -1 to m_ChunkAxisCount
    {
        x += 1; y += 1; z += 1;
        uint32_t stride = (m_ChunkAxisCount + 2);
        float d = m_DensityMap[x + stride * y + stride * stride * z];
        return d;
    }

    float GetHeight(int x, int z, uint32_t* outColor, float y, float vox_size, float d) // -1 to m_ChunkAxisCount
    {
        x += 1; z += 1;
        //if (x > 0 && x < 16) x = 1;
        //if (x >= 16) x = 16;
        //if (z > 0 && z < 16) z = 1;
        //if (z >= 16) z = 16;
        float h = ApplyAmplitude(m_HeightMap[x + (m_ChunkAxisCount+2) * z]);

        float rand0 = (hash(h) - 0.5f) * 8;
        float rand1 = (hash(rand0) - 0.5f) * 10;
        float rand2 = (hash(rand1) - 0.5f) * 4;
        

        switch (preset)
        {
        case Mountains:
        {
            uint32_t stone_color = MAKE_COLOR(160 + rand0, 150 + rand0, 145 + rand0, 255);

            uint32_t dirt_color = MAKE_COLOR_OFFSET(90, 85, 75, 255, rand0);
            uint32_t grass_color = MAKE_COLOR_OFFSET(75, 101, 50, 255, rand0);

            if (y < h - 8 * vox_size)
            {
                *outColor = stone_color;

                uint32_t diamond_color = MAKE_COLOR_OFFSET(84, 224, 240, 255, rand0);
            }
            else if(y < h - 2 * vox_size) *outColor = dirt_color;
            else *outColor = grass_color;
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
        return h;
    }

    float GetDensity(int x, int y, int z, uint32_t* outColor) // -1 to m_ChunkAxisCount
    {
        x += 1; y += 1; z += 1;
        uint32_t stride = (m_ChunkAxisCount + 2);
        float d = m_DensityMap[x + stride * y + stride * stride * z];

        float rand0 = (hash(d) - 0.5f) * 8;

        *outColor = MAKE_COLOR(205 + rand0, 205 + rand0, 205 + rand0, 255);
        return d;
    }

    float ApplyDensity(float v)
    {
        return v + 0.5f;
    }

    PROFILER_DECLARE(meshing);
    bool MeshChunk(glm::ivec4 chunkLod, DataResult* outResult)
    {
        m_StagingVertices.clear();
        int32_t lod = chunkLod.w;

        float chunk_size = ChunkSize(lod);
        float voxelSize = VoxelSize(lod);
         
        glm::vec3 chunk_origin = chunk_size * glm::vec3(glm::xyz(chunkLod));
        uint32_t maxIndex = glm::pow(m_ChunkAxisCount, 3);

        auto bounds = ComputeHeightMap(glm::xz(chunk_origin), lod, 191122);

        if (ApplyAmplitude(bounds.max) < chunk_origin.y)
            return false;
        PROFILER_BEGIN(meshing);
        for (uint32_t z = 0; z < m_ChunkAxisCount; z++)
            for (uint32_t x = 0; x < m_ChunkAxisCount; x++)
            {
                uint32_t col;
                float h = GetHeight(x, z, &col, 0, voxelSize,0.f);
                glm::vec3 voxel_origin = chunk_origin + glm::vec3(x,0,z) * voxelSize;
                
                int y_coord = glm::floor((h - chunk_origin.y) / voxelSize);

                for (uint32_t i = 0; i < 3; i++)
                {
                    uint32_t y = y_coord - i;
                    glm::ivec3 xyz = { x,y,z };
                    voxel_origin = chunk_origin + glm::vec3(xyz) * voxelSize;

                    if (voxel_origin.y < chunk_origin.y || voxel_origin.y >= chunk_origin.y + chunk_size)
                        break;
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
                        glm::vec3 currFaceOrigin = voxel_origin + vertDelta * voxelSize;
                        if (lod < 10) currFaceOrigin.y -= voxelSize;

                        glm::vec3 neighborOrigin = voxel_origin + delta;

                        int32_t nx = x + idelta.x;
                        int32_t nz = z + idelta.z;

                        uint32_t ncol;
                        float nh = GetHeight(nx, nz, &ncol, 0, voxelSize,0.f);

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

                                face[f] = glm::vec4(currFaceOrigin + voxelSize * offset - chunk_origin, std::bit_cast<float>(col));
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
        PROFILER_END(meshing);
        
        if (m_StagingVertices.empty())
            return false;

        outResult->addr = m_StagingVertices.data();
        outResult->size = m_StagingVertices.size() * sizeof(glm::vec4);
        outResult->consumed = false;

        return true;
    }

    bool MeshChunk3D(glm::ivec4 chunkLod, DataResult* outResult)
    {
        int32_t lod = chunkLod.w;

        float chunk_size = ChunkSize(lod);
        float voxelSize = VoxelSize(lod);

        glm::vec3 chunk_origin = chunk_size * glm::vec3(glm::xyz(chunkLod));
        uint32_t maxIndex = glm::pow(m_ChunkAxisCount, 3);

        auto bounds_2d = ComputeHeightMap(glm::xz(chunk_origin), lod, 191122);

        m_spheres_in_chunk.clear();
        for (int i = edit_spheres_stack.size() - 1; i >= 0; i--)
        {
            glm::vec4 s = edit_spheres_stack[i];
            if (IntersectSphereAABB3D(s.x, s.y, s.z, glm::abs(s.w),
                chunk_origin.x, 
                chunk_origin.y, 
                chunk_origin.z,
                chunk_origin.x + chunk_size,
                chunk_origin.y + chunk_size,
                chunk_origin.z + chunk_size,
                nullptr))
            {
                m_spheres_in_chunk.push_back(i);
            }
        }

        float maxHeight = ApplyAmplitude(bounds_2d.max);
        float minHeight = ApplyAmplitude(bounds_2d.min);
        float chunk_top = chunk_origin.y + chunk_size;

        auto bounds = ComputeDensityMap(chunk_origin, lod, 191122);
        if (m_spheres_in_chunk.empty())
        {
            if (maxHeight < chunk_origin.y)
                return false;

            if (ApplyDensity(bounds.max) <= 0.f || chunk_top < minHeight && ApplyDensity(bounds.min) > 0.f)
                return false;
        }
        


        glm::vec3 offset_y(0.f);
        if (lod < 10)
        {
            offset_y.y = voxelSize;
        }
        uint64_t write_idx = 0;
        PROFILER_BEGIN(meshing);

        bool positive = true;
        int idx = 0;

        double t0 = glfwGetTime();
        for (uint32_t face_id = 0b1; face_id < 0b1000000; face_id <<= 1, positive = !positive)
        {
            glm::ivec3 idelta;
            idelta.x = ((face_id & 0b1) ? 1 : 0) - ((face_id & 0b10) ? 1 : 0);
            idelta.y = ((face_id & 0b100) ? 1 : 0) - ((face_id & 0b1000) ? 1 : 0);
            idelta.z = ((face_id & 0b10000) ? 1 : 0) - ((face_id & 0b100000) ? 1 : 0);

            glm::vec3 delta = idelta;;
            delta *= voxelSize;
            uint32_t currAxis = (idx++) >> 1;

            glm::vec3 vertDelta = glm::greaterThan(delta, glm::vec3(0.f));
            glm::vec4 face[4];
            glm::ivec2 axes = glm::ivec2(
                (currAxis + 1) % 3,
                (currAxis + 2) % 3
            );

            for (uint32_t z = 0; z < m_ChunkAxisCount; z++)
            for (uint32_t y = 0; y < m_ChunkAxisCount; y++)
                for (uint32_t x = 0; x < m_ChunkAxisCount; x++)
                {
                    glm::ivec3 xyz = { x,y,z };
                    glm::vec3 voxel_origin = chunk_origin + glm::vec3(xyz) * voxelSize;
                    
                    uint32_t col;
                    bool sphere_overrides = false;
                    {
                        int remove = -1;
                        for (int i = 0; i < m_spheres_in_chunk.size(); i++)
                        {
                            glm::vec4 s = edit_spheres_stack[m_spheres_in_chunk[i]];

                            if (IntersectSphereAABB3D(s.x, s.y, s.z, glm::abs(s.w),
                                voxel_origin.x, voxel_origin.y, voxel_origin.z,
                                voxel_origin.x + voxelSize,
                                voxel_origin.y + voxelSize,
                                voxel_origin.z + voxelSize,
                                nullptr))
                            {
                                remove = (s.w < 0.f);
                                sphere_overrides = true;
                                break;
                            }
                        }
                        if (remove == 1)
                            continue;
                        if (remove == 0)
                        {
                            col = MAKE_COLOR(232, 155, 177, 255);
                            uint8_t rand_color = uint64_t(16*hash(float(x^y^z) + voxel_origin.x));
                            col = MAKE_COLOR_OFFSET(90, 85, 75, 255, rand_color);
                        }
                    }
                    if (!sphere_overrides)
                    {
                        float d = ApplyDensity(GetDensity(x, y, z, &col));
                        float h = GetHeight(x, z, &col, voxel_origin.y, voxelSize, d);

                        if (voxel_origin.y > h)
                            continue;

                        if (d <= 0.f)
                            continue;
                    }

                    // X NX Y NY Z NZ
                    {
                        

                        glm::vec3 neighborOrigin = voxel_origin + delta;

                        int32_t nx = x + idelta.x;
                        int32_t ny = y + idelta.y;
                        int32_t nz = z + idelta.z;

                        uint32_t ncol;
                        float nd = ApplyDensity(GetDensity(nx, ny, nz, &ncol));
                        float nh = GetHeight(nx, nz, &ncol, 0, voxelSize, nd);


                        bool is_neighbor_empty = (nd <= 0.f || nh < neighborOrigin.y);

                        for (int i = 0; i < m_spheres_in_chunk.size(); i++)
                        {
                            glm::vec4 s = edit_spheres_stack[m_spheres_in_chunk[i]];

                            if (IntersectSphereAABB3D(s.x, s.y, s.z, glm::abs(s.w),
                                neighborOrigin.x, neighborOrigin.y, neighborOrigin.z,
                                neighborOrigin.x + voxelSize, 
                                neighborOrigin.y + voxelSize, 
                                neighborOrigin.z + voxelSize,
                                nullptr))
                            {
                                is_neighbor_empty = (s.w < 0.f);
                                break;
                            }
                        }

                        if (is_neighbor_empty) // should emit face
                        {
                            glm::vec3 currFaceOrigin = voxel_origin + vertDelta * voxelSize;
                            for (int f = 0; f < 4; f++)
                            {
                                glm::ivec2 mask = { f % 2, f / 2 };
                                glm::vec3 offset(0.f);

                                offset[axes.x] = mask.x;
                                offset[axes.y] = mask.y;

                                face[f] = glm::vec4(currFaceOrigin + voxelSize * offset - chunk_origin - offset_y, std::bit_cast<float>(col));
                            }
                            m_StagingVertices[write_idx++] = (face[0]);
                            m_StagingVertices[write_idx++] = (face[positive ? 1 : 2]);
                            m_StagingVertices[write_idx++] = (face[positive ? 2 : 1]);

                            m_StagingVertices[write_idx++] = (face[positive ? 2 : 1]);
                            m_StagingVertices[write_idx++] = (face[positive ? 1 : 2]);
                            m_StagingVertices[write_idx++] = (face[3]);
                        }
                    }
                }
        }
        double t1 = glfwGetTime();
        meshing_time_total += t1 - t0;
        meshing_time_total_cnt += 1;
                PROFILER_END(meshing);
        if (write_idx == 0)
            return false;

        outResult->addr = m_StagingVertices.data();
        outResult->size = write_idx * sizeof(glm::vec4);
        outResult->consumed = false;

        return true;
    }

    enum FaceDirection
    {
        PositiveX,
        NegativeX,
        PositiveY,
        NegativeY,
        PositiveZ,
        NegativeZ,
    };

    enum Axis
    {
        X, Y, Z
    };

    /**
     * @brief Packs voxel face data into a single 32-bit integer.
     *
     * This function encodes the voxel coordinates (x, y, z),
     * the face direction, and material into one uint32_t.
     *
     * @param x        X coordinate of the voxel (0-63)
     * @param y        Y coordinate of the voxel (0-63)
     * @param z        Z coordinate of the voxel (0-63)
     * @param dir      Face direction (0-5)
     * @param material Material or color ID (0-2047)
     * @return uint32_t Packed representation of the face
     */
    uint32_t PackFace(uint32_t x, uint32_t y, uint32_t z, uint32_t dir, uint32_t material)
    {
        //assert(x <= BITMASK(6));
        //assert(y <= BITMASK(6));
        //assert(z <= BITMASK(6));
        //assert(dir <= BITMASK(3));
        //assert(material <= BITMASK(11));
        return (x << 0) | (y << 6) | (z << 12) | (dir << 18) | (material << 21);
    }
    /**
    * @brief Inverse operation of PackFace
    * 
    * @param must be non null
    * @return Unpacked representation of the face
    */
    void UnpackFace(uint32_t face, uint32_t* x, uint32_t* y, uint32_t* z, uint32_t* dir, uint32_t* material)
    {
        *x = (face >> 0) & BITMASK(6);
        *y = (face >> 6) & BITMASK(6);
        *z = (face >> 12) & BITMASK(6);
        *dir = (face >> 18) & BITMASK(3);
        *material = (face >> 21) & BITMASK(11);
    }

    template<FaceDirection face_dir>
    inline void emit_face(float voxel_size, glm::vec3 chunk_origin, glm::ivec3 voxelCoord, std::vector<glm::vec4>& out, uint64_t& write_idx, uint32_t voxel_color)
    {
        glm::ivec3 idelta =
        {
            (((1 << face_dir) & 0b000001) ? 1 : 0) - 
            (((1 << face_dir) & 0b000010) ? 1 : 0),
            (((1 << face_dir) & 0b000100) ? 1 : 0) - 
            (((1 << face_dir) & 0b001000) ? 1 : 0),
            (((1 << face_dir) & 0b010000) ? 1 : 0) - 
            (((1 << face_dir) & 0b100000) ? 1 : 0)
        };

        glm::vec3 delta = idelta;

        constexpr Axis axis = Axis(int(face_dir) / 2);
        constexpr bool positive = face_dir % 2 == 0;

        glm::vec3 vertDelta = glm::greaterThan(delta * voxel_size, glm::vec3(0.f));
        glm::vec4 face[4];
        glm::ivec2 axes = glm::ivec2(
            (axis + 1) % 3,
            (axis + 2) % 3
        );

        glm::vec3 voxel_origin = chunk_origin + voxel_size * glm::vec3(voxelCoord);
        glm::vec3 currFaceOrigin = voxel_origin + vertDelta * voxel_size;

        for (int f = 0; f < 4; f++)
        {
            glm::ivec2 mask = { f % 2, f / 2 };
            glm::vec3 offset(0.f);

            offset[axes.x] = mask.x;
            offset[axes.y] = mask.y;
            
            face[f] = glm::vec4(currFaceOrigin + voxel_size * offset - chunk_origin, std::bit_cast<float>(voxel_color));
        }

        m_StagingVertices[write_idx++] = (face[0]);
        m_StagingVertices[write_idx++] = (face[positive ? 1 : 2]);
        m_StagingVertices[write_idx++] = (face[positive ? 2 : 1]);
        
        m_StagingVertices[write_idx++] = (face[positive ? 2 : 1]);
        m_StagingVertices[write_idx++] = (face[positive ? 1 : 2]);
        m_StagingVertices[write_idx++] = (face[3]);
    }
    
    inline void emit_face_packed(uint32_t* out_buffer, uint64_t* offset, uint32_t x, uint32_t y, uint32_t z, uint32_t dir, uint32_t material)
    {
        out_buffer[*offset] = PackFace(x, y, z, dir, material);
        (*offset)++;
    }


    uint32_t* m_solid_mask;
    uint32_t* m_bit_mask;
    inline uint32_t is_solid_get(int x, int y, int z)
    {
        return m_solid_mask[x + (m_ChunkAxisCount + 2) * y + (m_ChunkAxisCount + 2) * (m_ChunkAxisCount + 2) * z];
    }

    inline void solid_set(uint32_t value, int x, int y, int z)
    {
        m_solid_mask[x + (m_ChunkAxisCount + 2) * y + (m_ChunkAxisCount + 2) * (m_ChunkAxisCount + 2) * z] = value;
    }

    template<FaceDirection face_dir>
    inline uint64_t mesh_dir(float voxelSize, glm::vec3 chunk_origin, std::vector<glm::vec4>& out, uint64_t write_idx)
    {
        constexpr int32_t dx = face_dir == PositiveX ? 1 : face_dir == NegativeX ? -1 : 0;
        constexpr int32_t dy = face_dir == PositiveY ? 1 : face_dir == NegativeY ? -1 : 0;
        constexpr int32_t dz = face_dir == PositiveZ ? 1 : face_dir == NegativeZ ? -1 : 0;

        constexpr Axis axis = Axis(face_dir / 2);

        if constexpr (axis == Axis::Z)
        {
            for (uint32_t z = 0; z < m_ChunkAxisCount; z++)
            {
                uint32_t face_mask[32] = { 0 };
                for (uint32_t y = 0; y < m_ChunkAxisCount; y++)
                    for (uint32_t x = 0; x < m_ChunkAxisCount; x++)
                    {
                        if (is_solid_get(x + 1, y + 1, z + 1) && is_solid_get(x + 1 + dx, y + 1 + dy, z + 1 + dz) == 0u)
                        {
                            face_mask[y] |= (1u << x);
                            emit_face<face_dir>(voxelSize, chunk_origin, { x,y,z }, m_StagingVertices, write_idx, is_solid_get(x + 1, y + 1, z + 1));
                        }
                    }
            }
        }
        else if constexpr (axis == Axis::Y)
        {
            for (uint32_t y = 0; y < m_ChunkAxisCount; y++)
            {
                uint32_t face_mask[32] = { 0 };
                for (uint32_t z = 0; z < m_ChunkAxisCount; z++)
                    for (uint32_t x = 0; x < m_ChunkAxisCount; x++)
                    {
                        if (is_solid_get(x + 1, y + 1, z + 1) && is_solid_get(x + 1 + dx, y + 1 + dy, z + 1 + dz) == 0u)
                        {
                            face_mask[z] |= (1u << x);
                            emit_face<face_dir>(voxelSize, chunk_origin, { x,y,z }, m_StagingVertices, write_idx, is_solid_get(x + 1, y + 1, z + 1));
                        }
                    }
            }
        }
        else if constexpr (axis == Axis::X)
        {
            for (uint32_t x = 0; x < m_ChunkAxisCount; x++)
            {
                uint32_t face_mask[32] = { 0 };
                for (uint32_t z = 0; z < m_ChunkAxisCount; z++)
                    for (uint32_t y = 0; y < m_ChunkAxisCount; y++)
                    {
                        if (is_solid_get(x + 1, y + 1, z + 1) && is_solid_get(x + 1 + dx, y + 1 + dy, z + 1 + dz) == 0u)
                        {
                            face_mask[z] |= (1u << y);
                            emit_face<face_dir>(voxelSize, chunk_origin, { x,y,z }, m_StagingVertices, write_idx, is_solid_get(x + 1, y + 1, z + 1));
                        }
                    }
            }
        }

        return write_idx;
    }

    template<FaceDirection face_dir>
    inline void mesh_dir_packed(float voxelSize, glm::vec3 chunk_origin, uint32_t* write_buffer, uint64_t* offset)
    {
        constexpr int32_t dx = face_dir == PositiveX ? 1 : face_dir == NegativeX ? -1 : 0;
        constexpr int32_t dy = face_dir == PositiveY ? 1 : face_dir == NegativeY ? -1 : 0;
        constexpr int32_t dz = face_dir == PositiveZ ? 1 : face_dir == NegativeZ ? -1 : 0;

        constexpr Axis axis = Axis(face_dir / 2);

        if constexpr (axis == Axis::Z)
        {
            for (uint32_t z = 0; z < m_ChunkAxisCount; z++)
            {
                uint32_t face_mask[32] = { 0 };
                for (uint32_t y = 0; y < m_ChunkAxisCount; y++)
                    for (uint32_t x = 0; x < m_ChunkAxisCount; x++)
                    {
                        if (is_solid_get(x + 1, y + 1, z + 1) && is_solid_get(x + 1 + dx, y + 1 + dy, z + 1 + dz) == 0u)
                        {
                            face_mask[y] |= (1u << x);
                            emit_face_packed(write_buffer, offset, x, y, z, uint32_t(face_dir), is_solid_get(x + 1, y + 1, z + 1) & BITMASK(11));
                        }
                    }
            }
        }
        else if constexpr (axis == Axis::Y)
        {
            for (uint32_t y = 0; y < m_ChunkAxisCount; y++)
            {
                uint32_t face_mask[32] = { 0 };
                for (uint32_t z = 0; z < m_ChunkAxisCount; z++)
                    for (uint32_t x = 0; x < m_ChunkAxisCount; x++)
                    {
                        if (is_solid_get(x + 1, y + 1, z + 1) && is_solid_get(x + 1 + dx, y + 1 + dy, z + 1 + dz) == 0u)
                        {
                            face_mask[z] |= (1u << x);
                            emit_face_packed(write_buffer, offset, x, y, z, uint32_t(face_dir), is_solid_get(x + 1, y + 1, z + 1) & BITMASK(11));
                        }
                    }
            }
        }
        else if constexpr (axis == Axis::X)
        {
            for (uint32_t x = 0; x < m_ChunkAxisCount; x++)
            {
                uint32_t face_mask[32] = { 0 };
                for (uint32_t z = 0; z < m_ChunkAxisCount; z++)
                    for (uint32_t y = 0; y < m_ChunkAxisCount; y++)
                    {
                        if (is_solid_get(x + 1, y + 1, z + 1) && is_solid_get(x + 1 + dx, y + 1 + dy, z + 1 + dz) == 0u)
                        {
                            face_mask[z] |= (1u << y);
                            emit_face_packed(write_buffer, offset, x, y, z, uint32_t(face_dir), is_solid_get(x + 1, y + 1, z + 1) & BITMASK(11));
                        }
                    }
            }
        }

    }


#define MESH_EDITS
    bool MeshChunk3DFast(glm::ivec4 chunkLod, DataResult* out_result)
    {
        int32_t lod = chunkLod.w;

        float chunk_size = ChunkSize(lod);
        float voxel_size = VoxelSize(lod);

        glm::vec3 chunk_origin = chunk_size * glm::vec3(glm::xyz(chunkLod));

        //PROFILER_BEGIN(meshing);
        auto bounds_2d = ComputeHeightMap(glm::xz(chunk_origin), lod, 191122);

        m_spheres_in_chunk.clear();
#ifdef MESH_EDITS
        for (int i = edit_spheres_stack.size() - 1; i >= 0; i--)
        {
            glm::vec4 s = edit_spheres_stack[i];
            if (IntersectSphereAABB3D(s.x, s.y, s.z, glm::abs(s.w),
                chunk_origin.x,
                chunk_origin.y,
                chunk_origin.z,
                chunk_origin.x + chunk_size,
                chunk_origin.y + chunk_size,
                chunk_origin.z + chunk_size,
                nullptr))
            {
                m_spheres_in_chunk.push_back(i);
            }
        }
#endif

        float max_height = ApplyAmplitude(bounds_2d.max);
        float min_height = ApplyAmplitude(bounds_2d.min);
        float chunk_top = chunk_origin.y + chunk_size;
        float chunk_bot = chunk_origin.y;

        if (m_spheres_in_chunk.empty())
        {
            if(chunk_bot > max_height)
                return false;
        }

        auto bounds = ComputeDensityMap(chunk_origin, lod, 191122);
        if (m_spheres_in_chunk.empty())
        {
            if (ApplyDensity(bounds.max) <= 0.f || chunk_top < min_height && ApplyDensity(bounds.min) > 0.f)
                return false;
        }
        //PROFILER_END(meshing);

        uint64_t stride = 2 + m_ChunkAxisCount;
        uint64_t stride_sq = stride * stride;


        for (int32_t z = -1; z < m_ChunkAxisCount + 1; z++)
        {

            for (int32_t y = -1; y < m_ChunkAxisCount + 1; y++)
            {
#pragma omp simd
                for (int32_t x = -1; x < m_ChunkAxisCount + 1; x++)
                {
                    glm::vec3 voxel_origin = chunk_origin + glm::vec3(x, y, z) * voxel_size;

                    uint32_t voxel_color = 0;
#ifdef MESH_EDITS
                    int remove = -1;
                    for (int i = 0; i < m_spheres_in_chunk.size(); i++)
                    {
                        glm::vec4 s = edit_spheres_stack[m_spheres_in_chunk[i]];

                        if (IntersectSphereAABB3D(s.x, s.y, s.z, glm::abs(s.w),
                            voxel_origin.x, voxel_origin.y, voxel_origin.z,
                            voxel_origin.x + voxel_size,
                            voxel_origin.y + voxel_size,
                            voxel_origin.z + voxel_size,
                            nullptr))
                        {
                            remove = (s.w < 0.f);
                            break;
                        }
                    }
                    if (remove == 0)
                    {
                        uint32_t rand_color = (x * 73856093 ^ y * 19349663 ^ z * 8349279);
                        voxel_color = MAKE_COLOR_OFFSET(90, 85, 75, 255, rand_color);
                        solid_set(voxel_color | 1, x + 1, y + 1, z + 1);
                    }
                    else if (remove == 1)
                    {
                        solid_set(0, x + 1, y + 1, z + 1);
                    }
                    else if (remove == -1)
#endif
                    {
                        float d = ApplyDensity(GetDensityOnly(x, y, z));
                        //float h = GetHeight(x, z, &voxel_color, voxel_origin.y, voxel_size, d);
                        float h = GetHeightOnly(x, z);
                        bool is_solid = (voxel_origin.y <= h && d > 0.f);
                        solid_set(is_solid ? MAKE_COLOR(0,255,0,255) : 0u, x + 1, y + 1, z + 1);

                        uint32_t idx = (x + 1) + (y + 1) * stride + (z + 1) * stride_sq;
                        uint32_t mask = 1u << x;
                        uint32_t idx32 = idx >> 5;
                        m_bit_mask[idx32] = (m_bit_mask[idx32] & ~mask) | (-int(is_solid) & mask);
                    }
                }
            }
        }

        uint64_t write_idx = 0;
        
        PROFILER_BEGIN(meshing);
        write_idx = mesh_dir<PositiveX>(voxel_size, chunk_origin, m_StagingVertices, write_idx);
        write_idx = mesh_dir<NegativeX>(voxel_size, chunk_origin, m_StagingVertices, write_idx);

        write_idx = mesh_dir<PositiveY>(voxel_size, chunk_origin, m_StagingVertices, write_idx);
        write_idx = mesh_dir<NegativeY>(voxel_size, chunk_origin, m_StagingVertices, write_idx);

        write_idx = mesh_dir<PositiveZ>(voxel_size, chunk_origin, m_StagingVertices, write_idx);
        write_idx = mesh_dir<NegativeZ>(voxel_size, chunk_origin, m_StagingVertices, write_idx);
        PROFILER_END(meshing);

        if (write_idx == 0)
            return false;

        out_result->addr = m_StagingVertices.data();
        out_result->size = write_idx * sizeof(glm::vec4);
        out_result->consumed = false;

        return true;
    }

    void update_aabb_packed(uint32_t* aabb, int x, int y, int z)
    {
        uint32_t bitmask = BITMASK(5);
        for (int offset = 0; offset < 15; offset += 5)
        {
            int coord = (*aabb) & bitmask;
            coord = glm::min(coord >> offset, x);
            *aabb &= ~bitmask;
            *aabb |= coord << offset;

            bitmask <<= 5;
        }
        for (int offset = 15; offset < 30; offset += 5)
        {
            int coord = (*aabb) & bitmask;
            coord = glm::max(coord >> offset, x);
            *aabb &= ~bitmask;
            *aabb |= coord << offset;

            bitmask <<= 5;
        }
    }

    inline uint32_t make_aabb(glm::ivec3 min, glm::ivec3 max)
    {
        return
            (min.x <<  0) |
            (min.y <<  5) |
            (min.z << 10) |
            (max.x << 15) |
            (max.y << 20) |
            (max.z << 25);
    }

    uint32_t MeshChunk3DFastPacked(glm::ivec4 chunkLod, uint32_t* write_buffer, uint32_t* out_aabb)
    {
        int32_t lod = chunkLod.w;

        float chunk_size = ChunkSize(lod);
        float voxel_size = VoxelSize(lod);

        glm::vec3 chunk_origin = chunk_size * glm::vec3(glm::xyz(chunkLod));

        //PROFILER_BEGIN(meshing);
        auto bounds_2d = ComputeHeightMap(glm::xz(chunk_origin), lod, 191122);

        m_spheres_in_chunk.clear();
#ifdef MESH_EDITS
        for (int i = edit_spheres_stack.size() - 1; i >= 0; i--)
        {
            glm::vec4 s = edit_spheres_stack[i];
            if (IntersectSphereAABB3D(s.x, s.y, s.z, glm::abs(s.w),
                chunk_origin.x,
                chunk_origin.y,
                chunk_origin.z,
                chunk_origin.x + chunk_size,
                chunk_origin.y + chunk_size,
                chunk_origin.z + chunk_size,
                nullptr))
            {
                m_spheres_in_chunk.push_back(i);
            }
        }
#endif

        float max_height = ApplyAmplitude(bounds_2d.max);
        float min_height = ApplyAmplitude(bounds_2d.min);
        float chunk_top = chunk_origin.y + chunk_size;
        float chunk_bot = chunk_origin.y;

        if (m_spheres_in_chunk.empty())
        {
            if (chunk_bot > max_height)
                return 0;
        }

        auto bounds = ComputeDensityMap(chunk_origin, lod, 191122);
        if (m_spheres_in_chunk.empty())
        {
            if (ApplyDensity(bounds.max) <= 0.f || chunk_top < min_height && ApplyDensity(bounds.min) > 0.f)
                return 0;
        }
        //PROFILER_END(meshing);

        uint64_t stride = 2 + m_ChunkAxisCount;
        uint64_t stride_sq = stride * stride;

        float mul = lod < 11 ? voxel_size : 1.f;

        glm::ivec3 aabb_min(m_ChunkAxisCount - 1);
        glm::ivec3 aabb_max(0);

        for (int32_t z = -1; z < m_ChunkAxisCount + 1; z++)
        {

            for (int32_t y = -1; y < m_ChunkAxisCount + 1; y++)
            {
#pragma omp simd
                for (int32_t x = -1; x < m_ChunkAxisCount + 1; x++)
                {
                    glm::vec3 voxel_origin = chunk_origin + glm::vec3(x, y, z) * voxel_size;

                    uint32_t voxel_color = 0;
#ifdef MESH_EDITS
                    int remove = -1;
                    bool is_solid = false;
                    for (int i = 0; i < m_spheres_in_chunk.size(); i++)
                    {
                        glm::vec4 s = edit_spheres_stack[m_spheres_in_chunk[i]];

                        if (glm::distance(glm::xyz(s), voxel_origin + voxel_size*0.5f) < glm::abs(s.w) || 
                            false && IntersectSphereAABB3D(s.x, s.y, s.z, glm::abs(s.w),
                            voxel_origin.x, voxel_origin.y, voxel_origin.z,
                            voxel_origin.x + voxel_size,
                            voxel_origin.y + voxel_size,
                            voxel_origin.z + voxel_size,
                            nullptr))
                        {
                            remove = (s.w < 0.f);
                            break;
                        }
                    }
                    if (remove == 0)
                    {
                        uint32_t rand_color = (x * 73856093 ^ y * 19349663 ^ z * 8349279);
                        solid_set(4, x + 1, y + 1, z + 1);
                        is_solid = true;
                    }
                    else if (remove == 1)
                    {
                        solid_set(0, x + 1, y + 1, z + 1);
                    }
                    else if (remove == -1)
#endif
                    {
                        float d = ApplyDensity(GetDensityOnly(x, y, z));
                        //float h = GetHeight(x, z, &voxel_color, voxel_origin.y, voxel_size, d);
                        float h = GetHeightOnly(x, z);
                        is_solid = (voxel_origin.y <= h && d > 0.f);

                        uint32_t material = 5;
                        if (voxel_origin.y < h - 64.f * mul)
                            material = 3;
                        else if (voxel_origin.y < h - 16.f * mul)
                            material = 2;
                        solid_set(is_solid ? material : 0u, x + 1, y + 1, z + 1);

                        uint32_t idx = (x + 1) + (y + 1) * stride + (z + 1) * stride_sq;
                        uint32_t mask = 1u << x;
                        uint32_t idx32 = idx >> 5;
                        m_bit_mask[idx32] = (m_bit_mask[idx32] & ~mask) | (-int(is_solid) & mask);
                    }
                    if (is_solid && 
                        x != -1 && x != m_ChunkAxisCount && 
                        y != -1 && y != m_ChunkAxisCount && 
                        z != -1 && z != m_ChunkAxisCount)
                    {
                        aabb_min.x = glm::min(aabb_min.x, x);
                        aabb_min.y = glm::min(aabb_min.y, y);
                        aabb_min.z = glm::min(aabb_min.z, z);
                        aabb_max.x = glm::max(aabb_max.x, x);
                        aabb_max.y = glm::max(aabb_max.y, y);
                        aabb_max.z = glm::max(aabb_max.z, z);
                    }
                }
            }
        }

        *out_aabb = make_aabb(aabb_min, aabb_max);
        uint64_t write_idx = 0;

        PROFILER_BEGIN(meshing);
        mesh_dir_packed<PositiveX>(voxel_size, chunk_origin, write_buffer, &write_idx);
        mesh_dir_packed<NegativeX>(voxel_size, chunk_origin, write_buffer, &write_idx);

        mesh_dir_packed<PositiveY>(voxel_size, chunk_origin, write_buffer, &write_idx);
        mesh_dir_packed<NegativeY>(voxel_size, chunk_origin, write_buffer, &write_idx);

        mesh_dir_packed<PositiveZ>(voxel_size, chunk_origin, write_buffer, &write_idx);
        mesh_dir_packed<NegativeZ>(voxel_size, chunk_origin, write_buffer, &write_idx);
        PROFILER_END(meshing);

        if (write_idx == 0)
            return 0;
        return write_idx;
    }

    float VoxelSize(int32_t lod)
    {
        return ChunkSize(lod) / m_ChunkAxisCount;
    }

    float ChunkSize(int32_t lod)
    {
        return CHUNK_SIZE(lod);
    }

    int32_t m_ChunkAxisCount = 32;
    std::vector<uint64_t> m_spheres_in_chunk;
    std::vector<glm::vec4> m_StagingVertices;
};

ThreadSafePriorityQueue<packed_leaf3d_raw_t, 4> request_chunks_queue;
ThreadSafeQueue<DataResult*> load_chunks_queue;

struct ChunkLoadInfo
{
    glm::ivec4 chunk_key;
    uint32_t* buffer;
    uint32_t face_count;
    std::condition_variable* cv_signal_consumed;
    uint32_t packed_aabb;

    uint32_t buffer_slice_offset;
    MemoryManager* m_manager;
    void NotifyConsumed()
    {
        cv_signal_consumed->notify_one();
        m_manager->AddMemoryBlock(buffer_slice_offset, face_count);
    }
};

ThreadSafeQueue<ChunkLoadInfo> load_chunks_queue_packed;
ThreadSafeQueue<packed_leaf3d_t> unload_chunks_queue;

struct builder_info
{
    glm::vec3 p;
    FlatOctree* tree;
    glm::vec4 reload_sphere;
};

uint8_t octree_generate_max_depth = 17;
float dist = 1024*512;

PROFILER_DECLARE(octree_generation);
void octree_generate(FlatOctree* octree, glm::vec3 p)
{
    //octree->Generate(4, octree_generate_max_depth, p.x, p.y, p.z, 0.1f, 512.f,2.f);
    PROFILER_BEGIN(octree_generation);
    octree->GenerateByChunks(4, octree_generate_max_depth, p.x, p.y, p.z, 0.f, dist, 6);
    PROFILER_END(octree_generation);
}

void octree_reload_leafs(FlatOctree* octree, glm::vec4 sphere)
{
    sphere.w = glm::abs(sphere.w);

    octree->RegenerateLeafsInSphere(sphere);
}

std::mutex tree_builder_mtx;
std::condition_variable tree_builder_cv;
builder_info tree_builder_info;

enum builder_state
{
    IDLE,
    REQUEST,
    READY,
    REQUEST_RELOAD
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


    uint32_t texture_atlas;
    uint32_t dummy_vao;
    FlatOctree octree;
    float octree_rebuild_distance_treshold;
    glm::vec3 octree_build_pos;
    GpuBuffer voxel_face_buffer;
    MemoryManager vertex_free_list;

    std::unordered_map<packed_leaf3d_raw_t, size_t> loaded_chunk_to_cmd_idx_map;
    std::unordered_map<size_t, packed_leaf3d_raw_t> loaded_cmd_idx_to_chunk_map;

    std::vector<DrawArraysIndirectCommand> terrain_draw_cmds;
    GpuBuffer terrain_draw_cmds_buffer;

    std::vector<uint32_t> chunk_aabbs;
    std::vector<glm::ivec4> chunk_positions_cmds;

    std::unordered_set<glm::ivec4> chunk_positions_cmds_set;
    GpuBuffer chunk_positions_cmds_buffer;
    
    ShaderProgram render_shader_program;
};

static bool remove_leaf(app_data_t* data, packed_leaf3d_t leaf)
{
    if (data->loaded_chunk_to_cmd_idx_map.find(leaf.packed) == data->loaded_chunk_to_cmd_idx_map.end())
    {
        return false;
    }
    size_t cmd_offset = data->loaded_chunk_to_cmd_idx_map[leaf.packed];

    DrawArraysIndirectCommand cmd = data->terrain_draw_cmds[cmd_offset];
    size_t swap_cmd_offset = data->terrain_draw_cmds.size() - 1;

    packed_leaf3d_raw_t swap_leaf = data->loaded_cmd_idx_to_chunk_map[swap_cmd_offset];

    data->terrain_draw_cmds[cmd_offset] = data->terrain_draw_cmds[swap_cmd_offset];
    data->chunk_positions_cmds[cmd_offset] = data->chunk_positions_cmds[swap_cmd_offset];
    data->chunk_aabbs[cmd_offset] = data->chunk_aabbs[swap_cmd_offset];

    data->terrain_draw_cmds.pop_back();
    data->chunk_positions_cmds.pop_back();
    data->chunk_aabbs.pop_back();
    data->chunk_positions_cmds_set.erase(leaf.packed);

    data->loaded_chunk_to_cmd_idx_map.erase(leaf.packed);
    data->loaded_cmd_idx_to_chunk_map.erase(swap_cmd_offset);

    data->loaded_chunk_to_cmd_idx_map[swap_leaf] = cmd_offset;
    data->loaded_cmd_idx_to_chunk_map[cmd_offset] = swap_leaf;

    data->vertex_free_list.AddMemoryBlock(cmd.first * sizeof(uint32_t), cmd.count * sizeof(uint32_t));
    return true;
}


static void tree_builder(app_data_t* data)
{
    while (true)
    {
        std::unique_lock lock(tree_builder_mtx);
        float t0 = glfwGetTime();
        tree_builder_cv.wait(lock, [&]() {return tree_builder_state == builder_state::REQUEST || tree_builder_state == builder_state::REQUEST_RELOAD; });
        float t1 = glfwGetTime();
        //printf("slept for %fms\n", (t1 - t0) * 1000);
        
        if (tree_builder_state == builder_state::REQUEST)
        {
            octree_generate(&data->octree, tree_builder_info.p);
            tree_builder_state = builder_state::READY;
            
            data->octree.ForEachLeafRemoved(
                [&](packed_leaf3d_t leaf)
                {
                    unload_chunks_queue.Enqueue(leaf);
                    //remove_leaf(data, leaf);
                }
            );
            data->vertex_free_list.Defragment();
            data->octree.ForEachLeafAdded(
                [&](packed_leaf3d_t leaf)
                {
                    float norm_priority = 1.f - (leaf.lod / float(octree_generate_max_depth));
                    uint8_t priority = glm::clamp(uint8_t(norm_priority * request_chunks_queue.GetMaxPriority()), (uint8_t)0, request_chunks_queue.GetMaxPriority());
                    request_chunks_queue.Enqueue(leaf.packed, priority);
                }
            );
            
        }
        if (tree_builder_state == builder_state::REQUEST_RELOAD)
        {
            data->octree.ForEachLeaf(
                [&](packed_leaf3d_t leaf)
                {
                    glm::vec4 chunk;
                    chunk.w = WORLD_SIZE / glm::pow(2, leaf.lod);
                    chunk.x = leaf.x * chunk.w;
                    chunk.y = leaf.y * chunk.w;
                    chunk.z = leaf.z * chunk.w;

                    if (IntersectSphereAABB3D(
                        tree_builder_info.reload_sphere.x,
                        tree_builder_info.reload_sphere.y,
                        tree_builder_info.reload_sphere.z,
                        glm::abs(tree_builder_info.reload_sphere.w),
                        chunk.x, chunk.y, chunk.z, chunk.x + chunk.w, chunk.y + chunk.w, chunk.z + chunk.w, nullptr))
                    {
                        if (data->chunk_positions_cmds_set.contains(leaf.packed))
                            remove_leaf(data, leaf);

                        float norm_priority = 1.f - (leaf.lod / float(octree_generate_max_depth));
                        uint8_t priority = glm::clamp(uint8_t(norm_priority * request_chunks_queue.GetMaxPriority()), (uint8_t)0, request_chunks_queue.GetMaxPriority());
                        request_chunks_queue.Enqueue(leaf.packed, priority);
                    }
                }
            );


        }
        tree_builder_state = builder_state::IDLE;
    }
}
uint64_t completed = 0;
uint64_t requested = 0;

std::atomic_uint64_t working_threads = 0u;
static void data_generator()
{
    static std::atomic_int idGenerator = 0;
    int id = idGenerator++;

    ThreadGenerator generator;
    generator.Init();
    uint64_t thread_bit = 1ull << uint64_t(id);

    double total_time = 0.0;
    double total_count = 0.0;
    
    double only_loaded_time = 0.0;
    double only_loaded_count = 0.0;
    
    double empty_time = 0.0;
    double empty_count = 0.0;

    DataResult dataResult;
    while (true)
    {
        glm::ivec4 chunkKey;
        request_chunks_queue.WaitAndDequeue(chunkKey);
        working_threads.fetch_xor(thread_bit);

        double t0 = glfwGetTime();
        bool loaded = generator.MeshChunk3DFast(chunkKey, &dataResult);
        double t1 = glfwGetTime();
        double dt = t1 - t0; dt *= 1000;

        total_time += dt;
        total_count++;
        if (loaded)
        {
            only_loaded_time += dt;
            only_loaded_count++;
        }
        else
        {
            empty_time += dt;
            empty_count++;
        }

        printf("meshing: avg time %s %fms\n", loaded ? "for loaded chunk" : "for early exit chunk", 1000.0 * PROFILER_GET_AVG_NESTED(meshing, generator));
        
        if (loaded)
        {
            dataResult.key = chunkKey;
            //printf("chunk loaded %i: %i, %i, %i\n", chunkKey.w, chunkKey.x, chunkKey.y, chunkKey.z);
            load_chunks_queue.Enqueue(&dataResult);

            std::unique_lock lock(dataResult.mtx);
            requested++;
            //printf("meshing + terrain: %i avg times load: %fms noload: %fms total: %fms\n", id, only_loaded_time / only_loaded_count, empty_time / empty_count, total_time / total_count);
            //printf("terrain: avg times 2D: %fms 3D: %fms\n\n",1000.0 * PROFILER_GET_AVG_NESTED(noise2d, generator), 1000.0 * PROFILER_GET_AVG_NESTED(noise3d, generator));
            dataResult.cv.wait(lock, [&dataResult] {return dataResult.consumed; });
        }

        working_threads.fetch_xor(thread_bit);
    }
}

static void data_generator_packed()
{
    //rework logic:
    /*
    * use memory bookkeeper (thread safe by default)
    * use cv and wait until bookkeeper can deliver buffer
    * if fails, go back to sleep
    * remove m_StagingVertices from ThreadGenerator class and make it take a buffer with MAX_VERTICES_SIZE
    */
    static std::atomic_int idGenerator = 0;
    int id = idGenerator++;

    ThreadGenerator generator;
    generator.Init();
    uint64_t thread_bit = 1ull << uint64_t(id);

    uint32_t face_count_max = (glm::pow(generator.m_ChunkAxisCount, 3) * 6) / 2; // checkerboard
    std::vector<uint32_t> faces(face_count_max * 8);
    MemoryManager faces_free_list;
    faces_free_list.Init(faces.size());
    faces_free_list.AddMemoryBlock(0, faces.size());

    double total_time = 0.0;
    double total_count = 0.0;

    double only_loaded_time = 0.0;
    double only_loaded_count = 0.0;

    double empty_time = 0.0;
    double empty_count = 0.0;

    std::mutex mtx_wait_for_memory;
    std::condition_variable cv_is_ready;

    packed_leaf3d_raw_t chunk_key;
    while (true)
    {
        request_chunks_queue.WaitAndDequeue(chunk_key);

        uint64_t offset;
        
        if (faces_free_list.FindAndPopOffset(face_count_max, offset))
        {
            uint32_t* buffer = faces.data() + offset;
            uint32_t packed_aabb;
            uint32_t face_count = generator.MeshChunk3DFastPacked(chunk_key, buffer, &packed_aabb); // guaranteed to have maxFacesPerChunk count

            uint32_t remaining_offset = offset + face_count;
            uint32_t remaining_size = face_count_max - face_count;
            
            if (face_count)
            {
                ChunkLoadInfo info;
                info.buffer = buffer;
                info.buffer_slice_offset = offset;
                info.face_count = face_count;
                info.chunk_key = chunk_key;
                info.packed_aabb = packed_aabb;
                info.cv_signal_consumed = &cv_is_ready; // for main thread to notify when its done using the memory
                info.m_manager = &faces_free_list;
                load_chunks_queue_packed.Enqueue(info);
            }

            faces_free_list.AddMemoryBlock(remaining_offset, remaining_size);
            faces_free_list.Defragment();
        }
        else
        {
            request_chunks_queue.Enqueue(chunk_key, request_chunks_queue.GetMaxPriority());
            std::unique_lock lock(mtx_wait_for_memory);
            printf("not enough staging memory %u\n", id);
            cv_is_ready.wait(lock, 
                [&]() 
                {
                    faces_free_list.Defragment();
                    return faces_free_list.FindOffset(face_count_max);
                }
            );
            printf("staging memory available! %u\n", id);
        }
        
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
    bool get()
    {
        return prev_predicate;
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
    static OnceGuard click_lod;
    static bool remesh_lods = true;
    static OnceGuard click_guard;
    static OnceGuard edit_guard;
    static OnceGuard edit_guard_remove;
    static OnceGuard add_guard;
    static OnceGuard sub_guard;
    static OnceGuard chunk_guard;
    static uint64_t frame_index = 0;
    static float cam_speed = 1.f;

    static glm::vec4 sphere_request;
    static bool did_edit = false;

    if (click_lod.is_first(glfwGetKey(data->engine->window, GLFW_KEY_P) == GLFW_PRESS))
        remesh_lods ^= 1;

    if (false && frame_index % 1024 == 0)
    {
        system("cls");
        //data->vertex_free_list.DebugPrint(1024*1024*64);
        printf("tasks: %i\n", request_chunks_queue.Size());
        printf("results: %i\n", load_chunks_queue.Size());
        printf("requested: %i\n", requested);
        printf("completed: %i\n", completed);
        std::cout << "working threads: " << std::bitset<32>(working_threads.load()) << '\n';
        printf("generation time: %fms\n", 1000*PROFILER_GET_AVG(octree_generation));
    }

    //ScopedTimer timer("update");
    glm::vec3 cam_pos = cam.GetPosition();
    glm::vec3 world_pos = cam_pos + glm::vec3(data->camera_chunk_pos) * 8.f;
    frame_index++;
    if(frame_index % 128 == 0) glfwSetWindowTitle(data->engine->window, std::format("chunks: {} pos: ({:.3f}, {:.3f}, {:.3f}) vel: {:.3f}m/s dt: {:.3f}ms\t FPS: {:.0f}", data->terrain_draw_cmds.size(), world_pos.x, world_pos.y, world_pos.z, cam_speed, 1000 * data->engine->delta_time, 1 / data->engine->delta_time).c_str());
    

    //if (glm::distance(data->octree_build_pos, cam_pos) > data->octree_rebuild_distance_treshold)

    if (glm::any(glm::greaterThanEqual(glm::abs(cam_pos), glm::vec3(8.f))))
    {
        glm::ivec3 chunk_offset = (cam_pos / 8.f);
        data->camera_chunk_pos += chunk_offset;
        data->camera.Translate(-glm::vec3(chunk_offset) * 8.f);
    }

    if (tree_builder_state == builder_state::IDLE)
    {
        int i = 0;
        ChunkLoadInfo info;
        constexpr uint32_t vertices_per_quad = 6u;
        while (i++ < 64 && load_chunks_queue_packed.TryDequeueNonBlocking(info))
        {
            offset_t offset;
            size_t size = info.face_count * sizeof(uint32_t) * vertices_per_quad;
            if (data->vertex_free_list.FindAndPopOffset(size, offset))
            {
                data->voxel_face_buffer.Upload(offset / vertices_per_quad, info.face_count * sizeof(uint32_t), info.buffer);
                completed++;
                DrawArraysIndirectCommand cmd;
                cmd.baseInstance = 0;
                cmd.count = size / sizeof(uint32_t);
                cmd.first = offset / sizeof(uint32_t);

                //printf("saved: %u - %u\n", offset, offset + size - 1);
                //printf("cmd  : %u - %u\n", cmd.first, cmd.first + info.face_count - 1);
                cmd.instanceCount = 1;

                packed_leaf3d_t leaf_data;
                leaf_data.x = info.chunk_key.x;
                leaf_data.y = info.chunk_key.y;
                leaf_data.z = info.chunk_key.z;
                leaf_data.lod = info.chunk_key.w;

                packed_leaf3d_t parent_leaf = leaf_data;
                parent_leaf.packed = parent_leaf.packed >> glm::ivec4(1, 1, 1, 0);
                parent_leaf.lod -= 1;

                size_t cmd_idx = data->terrain_draw_cmds.size();

                data->loaded_chunk_to_cmd_idx_map[leaf_data.packed] = cmd_idx;
                data->loaded_cmd_idx_to_chunk_map[cmd_idx] = leaf_data.packed;

                data->terrain_draw_cmds.push_back(cmd);
                data->chunk_aabbs.push_back(info.packed_aabb);

                data->chunk_positions_cmds.push_back(leaf_data.packed);
                data->chunk_positions_cmds_set.insert(leaf_data.packed);
                //printf("saved chunk %i: %i, %i, %i in offset %i, size %i\n", leaf_data.lod, leaf_data.x, leaf_data.y, leaf_data.z, offset, size);
            }
            else
            {
                printf("error: not enough mem\n");
                load_chunks_queue_packed.Enqueue(info);
                break;
            }
            info.NotifyConsumed();
        }
        /*
        DataResult* result;
        while (false && i++ < 128 && load_chunks_queue.TryDequeueNonBlocking(result))
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

                packed_leaf3d_t parent_leaf = leaf_data;
                parent_leaf.packed = parent_leaf.packed >> glm::ivec4(1, 1, 1, 0);
                parent_leaf.lod -= 1;

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
                printf("error: not enough mem\n");
            }
            result->Consume();
        }*/
    }
    
    bool is_busy = !(working_threads.load() == 0u && request_chunks_queue.Size() == 0 && load_chunks_queue.Size() == 0 && tree_builder_state == builder_state::IDLE);

    if (!is_busy)
    {
        packed_leaf3d_t to_remove;
        int i = 0;
        while (i++ < 128 && unload_chunks_queue.TryDequeueNonBlocking(to_remove))
        {
            remove_leaf(data, to_remove);
        }
        data->vertex_free_list.Defragment();
    }

    if (did_edit)
    {
        tree_builder_mtx.lock();
        tree_builder_info = { .tree = &data->octree, .reload_sphere = sphere_request };
        tree_builder_state = builder_state::REQUEST_RELOAD;
        tree_builder_mtx.unlock();
        tree_builder_cv.notify_one();
        did_edit = false;
    }
    else
    {
        if (remesh_lods && !is_busy && unload_chunks_queue.Size() == 0)
        {
            if (glm::distance(cam.GetPosition(), data->octree_build_pos) > data->octree_rebuild_distance_treshold ||
                click_guard.is_first(glfwGetKey(data->engine->window, GLFW_KEY_B) == GLFW_PRESS))
            {
                if (tree_builder_mtx.try_lock())
                {
                    tree_builder_info = { .p = cam_pos + glm::vec3(data->camera_chunk_pos) * 8.f, .tree = &data->octree };
                    tree_builder_state = builder_state::REQUEST;
                    tree_builder_mtx.unlock();
                    data->octree_build_pos = cam_pos;

                    tree_builder_cv.notify_one();
                }
            }
        }
    }


    double xpos = 0, ypos = 0;
    glfwGetCursorPos(data->engine->window, &xpos, &ypos);
    static double lastX = xpos, lastY = ypos;

    float deltaX = (float)(xpos - lastX);
    float deltaY = (float)(lastY - ypos);

    lastX = xpos;
    lastY = ypos;
    
    float speed = data->engine->delta_time * cam_speed;
    if (glfwGetKey(data->engine->window, GLFW_KEY_W) == GLFW_PRESS)
        cam.Translate(speed * cam.GetForwardVector());
    if (glfwGetKey(data->engine->window, GLFW_KEY_S) == GLFW_PRESS)
        cam.Translate(-speed * cam.GetForwardVector());
    if (glfwGetKey(data->engine->window, GLFW_KEY_A) == GLFW_PRESS)
        cam.Translate(speed * glm::normalize(glm::cross(WorldDirection::Up, cam.GetForwardVector())));
    if (glfwGetKey(data->engine->window, GLFW_KEY_D) == GLFW_PRESS)
        cam.Translate(-speed * glm::normalize(glm::cross(WorldDirection::Up, cam.GetForwardVector())));


    static float edit_sphere_radius = 16.f;
    if (glfwGetKey(data->engine->window, GLFW_KEY_UP) == GLFW_PRESS)
    {
        edit_sphere_radius *= glm::pow(16.f, data->engine->delta_time);
        edit_sphere_radius = glm::clamp(edit_sphere_radius,0.125f, WORLD_SIZE);
    }
    if (glfwGetKey(data->engine->window, GLFW_KEY_DOWN) == GLFW_PRESS)
    {
        edit_sphere_radius /= glm::pow(16.f, data->engine->delta_time);
        edit_sphere_radius = glm::clamp(edit_sphere_radius,0.125f, WORLD_SIZE);
    }

    bool left_pressed = glfwGetMouseButton(data->engine->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool right_pressed = glfwGetMouseButton(data->engine->window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (
        
        (glfwGetKey(data->engine->window, GLFW_KEY_Q) && (left_pressed || right_pressed) && frame_index % 100 == 0) ||
        edit_guard.is_first(left_pressed) ||
        edit_guard_remove.is_first(right_pressed))
    {
        float depth = 0.0f;

        glBindFramebuffer(GL_READ_FRAMEBUFFER, data->engine->renderer.framebuffer);

        double xmouse, ymouse;
        glfwGetCursorPos(data->engine->window, &xmouse, &ymouse);
        glReadPixels(
            glm::min(static_cast<int>(xmouse), data->engine->renderer.viewport.x), 
            glm::min(data->engine->renderer.viewport.y - static_cast<int>(ymouse), data->engine->renderer.viewport.y),
            1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
        float x_ndc = (2.0f * xmouse) / data->engine->renderer.viewport.x - 1.0f;
        float y_ndc = 1.f - (2.0f * ymouse) / data->engine->renderer.viewport.y;

        glm::vec4 ray_clip = glm::vec4(x_ndc, y_ndc, 1.0f, 1.0f);

        glm::vec4 ray_view = glm::inverse(cam.GetProjectionMatrix()) * ray_clip;
        ray_view = glm::vec4(ray_view.x, ray_view.y, -1.0f, 0.0f);

        glm::vec3 ray_world = glm::vec3(glm::inverse(cam.GetViewMatrix()) * ray_view);
        ray_world = glm::normalize(ray_world);

        float n = WORLD_SIZE * 4.0f;
        float f = 0.125f;

        float distance = (n * f) / (depth * (n - f) + f);


        sphere_request = glm::vec4(cam.GetPosition() + glm::vec3(data->camera_chunk_pos) * 8.f + distance * ray_world, right_pressed ? edit_sphere_radius : -edit_sphere_radius);
        edit_spheres_stack.push_back(sphere_request);
        did_edit = true;
    }
    


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
            data->chunk_aabbs.clear();
            data->chunk_positions_cmds.clear();
            data->chunk_positions_cmds_set.clear();
            data->terrain_draw_cmds.clear();
            load_chunks_queue.Clear();
            request_chunks_queue.Clear();
        }
    }

    //if (add_guard.is_first(glfwGetKey(data->engine->window, GLFW_KEY_KP_ADD) == GLFW_PRESS))
    //    octree_generate_max_depth++;
    //if (sub_guard.is_first(glfwGetKey(data->engine->window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS))
    //    octree_generate_max_depth--;

    if (glfwGetKey(data->engine->window, GLFW_KEY_Z) == GLFW_PRESS)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    if (glfwGetKey(data->engine->window, GLFW_KEY_Y) == GLFW_PRESS)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    if (glfwGetKey(data->engine->window, GLFW_KEY_SPACE) == GLFW_PRESS)
        cam.Translate(speed * WorldDirection::Up*1.0f);
    if (glfwGetKey(data->engine->window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        cam.Translate(-speed * WorldDirection::Up);

    if (glfwGetKey(data->engine->window, GLFW_KEY_F) == GLFW_PRESS)
        cam_speed *= glm::pow(8.0, data->engine->delta_time);
    if (glfwGetKey(data->engine->window, GLFW_KEY_R) == GLFW_PRESS)
        cam_speed /= glm::pow(8.0, data->engine->delta_time);
    cam_speed = glm::max(cam_speed, 0.01f);

    float sensitivity = 0.01f;
    //if (glfwGetMouseButton(data->engine->window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
    {
        cam.Rotate(sensitivity * deltaX, sensitivity * deltaY);
    }

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
        if (glfwGetKey(data->engine->window, GLFW_KEY_C) == GLFW_PRESS)
            sp.uniformMat4("u_proj_matrix", data->camera.GetOrthoProjectionMatrix());
        else
            sp.uniformMat4("u_proj_matrix", data->camera.GetProjectionMatrix());

        glBindVertexArray(data->dummy_vao);

        sp.uniform1f("u_world_size", WORLD_SIZE);

        
        sp.uniform1ui("u_render_cube", 0u);
        sp.uniformTex2D("u_texture_atlas", data->texture_atlas, 0);
        sp.uniform3i("u_camera_chunk_pos", data->camera_chunk_pos);
#if 1
       

        //glNamedBufferSubData(data->terrain_draw_cmds_buffer.Handle(),
        //    0, data->terrain_draw_cmds.size() * sizeof(DrawArraysIndirectCommand),
        //    data->terrain_draw_cmds.data());


        glm::vec4 planes[6];
        extract_frustum(data->camera.GetProjectionMatrix() * glm::translate(data->camera.GetViewMatrix(), -glm::vec3(data->camera_chunk_pos) * 8.f), planes);
        static std::vector<DrawArraysIndirectCommand> frustum_terrain_draw_cmds;
        static std::vector<glm::ivec4> frustum_chunk_positions_cmds;
        static std::vector<uint32_t> frustum_packed_aabbs;
        static bool freeze_frustum = false;
        static OnceGuard freeze_frust;
        if (freeze_frust.is_first(glfwGetKey(data->engine->window, GLFW_KEY_G) == GLFW_PRESS))
            freeze_frustum ^= 1;

        if (!freeze_frustum)
        {
            frustum_chunk_positions_cmds.clear();
            frustum_terrain_draw_cmds.clear();
            frustum_packed_aabbs.clear();
            for (uint64_t i = 0; i < data->chunk_aabbs.size(); i++)
            {
                glm::ivec4 leaf = data->chunk_positions_cmds[i];
                uint32_t packed_aabb = data->chunk_aabbs[i];
                float chunk_size = WORLD_SIZE / glm::pow(2, leaf.w);
                float voxel_size = chunk_size / 32.f;


                glm::vec3 aabb_origin;
                aabb_origin = glm::xyz(leaf);
                aabb_origin *= chunk_size;

                glm::vec3 aabb_min;
                glm::vec3 aabb_max;

                aabb_min.x = ((packed_aabb >> 0) & BITMASK(5)) * voxel_size;
                aabb_min.y = ((packed_aabb >> 5) & BITMASK(5)) * voxel_size;
                aabb_min.z = ((packed_aabb >> 10) & BITMASK(5)) * voxel_size;
                aabb_max.x = (1 + ((packed_aabb >> 15) & BITMASK(5))) * voxel_size;
                aabb_max.y = (1 + ((packed_aabb >> 20) & BITMASK(5))) * voxel_size;
                aabb_max.z = (1 + ((packed_aabb >> 25) & BITMASK(5))) * voxel_size;
                FrustumResult result = frustum_aabb(planes, aabb_origin+ aabb_min, aabb_origin+aabb_max);
                if (result != FrustumResult::OUTSIDE)
                {
                    frustum_chunk_positions_cmds.push_back(leaf);
                    frustum_terrain_draw_cmds.push_back(data->terrain_draw_cmds[i]);
                    frustum_packed_aabbs.push_back(data->chunk_aabbs[i]);
                }
            }
        }


        glBindBuffer(GL_SHADER_STORAGE_BUFFER, data->chunk_positions_cmds_buffer.Handle());
        glBufferData(GL_SHADER_STORAGE_BUFFER,
            frustum_chunk_positions_cmds.size() * sizeof(glm::ivec4),
            frustum_chunk_positions_cmds.data(),
            GL_STREAM_DRAW);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, data->terrain_draw_cmds_buffer.Handle());
        glBufferData(GL_SHADER_STORAGE_BUFFER,
            frustum_terrain_draw_cmds.size() * sizeof(DrawArraysIndirectCommand),
            frustum_terrain_draw_cmds.data(),
            GL_STREAM_DRAW);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, data->voxel_face_buffer.Handle());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, data->chunk_positions_cmds_buffer.Handle());
        
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, data->terrain_draw_cmds_buffer.Handle());
        glMultiDrawArraysIndirect(GL_TRIANGLES, 0, frustum_terrain_draw_cmds.size(), sizeof(DrawArraysIndirectCommand));

        if (glfwGetKey(data->engine->window, GLFW_KEY_TAB) == GLFW_PRESS)
        {
            sp.uniform1ui("u_render_cube", 1u);
            for (int i = 0; i < frustum_packed_aabbs.size(); i++)
            {
                packed_leaf3d_raw_t leaf = frustum_chunk_positions_cmds[i];
                uint32_t packed_aabb = frustum_packed_aabbs[i];
                float chunk_size = WORLD_SIZE / glm::pow(2, leaf.w);
                float voxel_size = chunk_size / 32.f;


                glm::vec3 aabb_origin;
                aabb_origin = glm::xyz(leaf);
                aabb_origin *= chunk_size;

                glm::vec3 aabb_min;
                glm::vec3 aabb_max;

                aabb_min.x = ((packed_aabb >> 0) & BITMASK(5)) * voxel_size;
                aabb_min.y = ((packed_aabb >> 5) & BITMASK(5)) * voxel_size;
                aabb_min.z = ((packed_aabb >> 10) & BITMASK(5)) * voxel_size;
                aabb_max.x = (1+((packed_aabb >> 15) & BITMASK(5))) * voxel_size;
                aabb_max.y = (1+((packed_aabb >> 20) & BITMASK(5))) * voxel_size;
                aabb_max.z = (1+((packed_aabb >> 25) & BITMASK(5))) * voxel_size;

                sp.uniform3f("u_cube_min", aabb_origin + aabb_min);
                sp.uniform3f("u_cube_size", (aabb_max - aabb_min));
                glDrawArrays(GL_LINES, 0, 24);
            }
        }


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
    
    for(int i = 0; i < std::thread::hardware_concurrency() - 2; i++)
        app->data->generator_workers.emplace_back(data_generator_packed);
    
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
    

    {
        int w, h, ch;
        stbi_set_flip_vertically_on_load(true);
        void* atlas_data = stbi_load("resources/textures/atlas.png", &w, &h, &ch, 4);
        if (!atlas_data)
        {
            return 1;
        }

        glCreateTextures(GL_TEXTURE_2D, 1, &data->texture_atlas);
        glTextureStorage2D(data->texture_atlas, 1, GL_RGBA8, 256, 256);
        glTextureParameteri(data->texture_atlas, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(data->texture_atlas, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(data->texture_atlas, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(data->texture_atlas, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTextureSubImage2D(data->texture_atlas, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, atlas_data);
        
        stbi_image_free(atlas_data);
    }

    {
        FirstPersonCameraSettings camera_settings;
        camera_settings.position = { 0,0,0 };
        camera_settings.direction = { 0,0,-1 };
        camera_settings.nearPlane = 4 * WORLD_SIZE;
        camera_settings.farPlane = 0.125f;
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

    data->voxel_face_buffer.Create();
    data->voxel_face_buffer.Allocate(GB(2), nullptr, GL_STREAM_DRAW);
    data->vertex_free_list.Init(data->voxel_face_buffer.SizeInBytes());
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

#include <xsimd/xsimd.hpp>
namespace xs = xsimd;

int main()
{
    
    using batch_type = xs::batch<float>;
    
    alignas(xs::default_arch::alignment()) float a_raw[] = { 1,2,3,4,5,6,7,8 };
    alignas(xs::default_arch::alignment()) float b_raw[] = { 1,2,3,4,5,6,7,8 };
    float res_raw[8];

    auto a = xs::load_aligned(a_raw);
    auto b = xs::load_aligned(b_raw);

    auto res = xs::add(a, b);
    res.store_aligned(res_raw);
    for (int i = 0; i < 8; ++i) {
        std::cout << res_raw[i] << " ";
    }

    App app;
    app_create(&app);
    app_run(&app);
    app_cleanup(&app);
}