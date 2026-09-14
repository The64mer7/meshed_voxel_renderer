#pragma once

#include "FastNoise/FastNoise.h"
#include "utils.hpp"
#include "world_data.hpp"
#include <cassert>
#include <mutex>

enum world_preset
{
    mountains,
    desert,
    count
};

inline static const char* world_preset_names[] = {"mountains", "desert", "count"};

class TerrainNoise
{
public:
    inline static int seed = 0;
    inline static world_preset preset = world_preset::mountains;
    inline static float frequency = 0.25f;

    inline static FastNoise::SmartNode<FastNoise::Perlin> source_node;
    inline static FastNoise::SmartNode<> terrain_node;
    inline static FastNoise::SmartNode<FastNoise::Multiply> detail_terrain;
    inline static FastNoise::SmartNode<FastNoise::Constant> terrain_amp_node;
    inline static FastNoise::SmartNode<FastNoise::Perlin> mega_node;
    inline static FastNoise::SmartNode<FastNoise::Multiply> mega_amp;
    inline static FastNoise::SmartNode<FastNoise::Max> mega_clamp;
    inline static FastNoise::SmartNode<FastNoise::Add> root_node;
    inline static FastNoise::SmartNode<FastNoise::Constant> bias_node;
    inline static FastNoise::SmartNode<FastNoise::Constant> amp_node;

    inline static void init()
    {
        source_node = FastNoise::New<FastNoise::Perlin>();
        mega_node = FastNoise::New<FastNoise::Perlin>();
        mega_amp = FastNoise::New<FastNoise::Multiply>();
        mega_clamp = FastNoise::New<FastNoise::Max>();
        detail_terrain = FastNoise::New<FastNoise::Multiply>();
        root_node = FastNoise::New<FastNoise::Add>();
        terrain_amp_node = FastNoise::New<FastNoise::Constant>();
        amp_node = FastNoise::New<FastNoise::Constant>();
        bias_node = FastNoise::New<FastNoise::Constant>();

        zero_node_values();

        mega_node->SetScale(2048.f);
        mega_clamp->SetLHS(mega_node);
        mega_clamp->SetRHS(0.0f);
        mega_amp->SetLHS(amp_node);
        mega_amp->SetRHS(mega_clamp);

        update_preset_parameters();
    }

    inline static void zero_node_values()
    {
        if (amp_node)
            amp_node->SetValue(1.0f);
        if (bias_node)
            bias_node->SetValue(20.0f);
        if (terrain_amp_node)
            terrain_amp_node->SetValue(1.0f);
    }

    inline static void update_preset_parameters()
    {
        if (!terrain_amp_node || !bias_node)
            return;

        LOG("terrain_preset: {}", world_preset_names[int(preset)]);

        switch (preset)
        {
        case world_preset::desert:
        {
            auto fbm = FastNoise::New<FastNoise::FractalFBm>();
            fbm->SetSource(source_node);
            fbm->SetOctaveCount(3);
            fbm->SetLacunarity(2.0f);
            fbm->SetGain(0.5f);
            terrain_node = fbm;

            detail_terrain->SetLHS(terrain_node);
            detail_terrain->SetRHS(terrain_amp_node);

            auto base_add = FastNoise::New<FastNoise::Add>();
            base_add->SetLHS(bias_node);
            base_add->SetRHS(detail_terrain);
            root_node->SetLHS(base_add);
            root_node->SetRHS(FastNoise::New<FastNoise::Constant>());

            source_node->SetScale(250.0f);
            terrain_amp_node->SetValue(15.0f);
            bias_node->SetValue(60.0f);
        }
        break;
        case world_preset::mountains:
        {
            auto ridged = FastNoise::New<FastNoise::FractalRidged>();
            ridged->SetSource(source_node);
            ridged->SetOctaveCount(4);
            ridged->SetLacunarity(2.0f);
            ridged->SetGain(0.6f);
            terrain_node = ridged;

            auto lowland_detail = FastNoise::New<FastNoise::Multiply>();
            lowland_detail->SetLHS(terrain_node);
            lowland_detail->SetRHS(terrain_amp_node);

            auto base_add = FastNoise::New<FastNoise::Add>();
            base_add->SetLHS(bias_node);
            base_add->SetRHS(lowland_detail);

            auto spike_detail = FastNoise::New<FastNoise::Multiply>();
            spike_detail->SetLHS(terrain_node);
            spike_detail->SetRHS(mega_amp);

            root_node->SetLHS(base_add);
            root_node->SetRHS(spike_detail);

            source_node->SetScale(200.0f);
            bias_node->SetValue(60.0f);
            terrain_amp_node->SetValue(12.0f);
            amp_node->SetValue(500.0f);
        }
        break;
        default:
            break;
        }
    }

    static auto generate_2d(float* height_map, glm::vec2 origin, int voxels_per_chunk_axis,
                            float voxel_size)
    {
        assert(root_node && "TerrainNoise::init() was not called before generate_2d!");

        glm::vec2 noise = (origin - voxel_size) * frequency;
        return root_node->GenUniformGrid2D(height_map, noise.x, noise.y, voxels_per_chunk_axis + 2,
                                           voxels_per_chunk_axis + 2, voxel_size * frequency,
                                           voxel_size * frequency, seed);
    }

    static auto generate_3d(float* density_map, glm::vec3 origin, int voxels_per_chunk_axis,
                            float voxel_size)
    {
        assert(root_node && "TerrainNoise::init() was not called before generate_3d!");

        glm::vec3 noise = origin * frequency;
        return root_node->GenUniformGrid3D(density_map, noise.x, noise.y, noise.z,
                                           voxels_per_chunk_axis + 2, voxels_per_chunk_axis + 2,
                                           voxels_per_chunk_axis + 2, voxel_size * frequency,
                                           voxel_size * frequency, voxel_size * frequency, seed);
    }
};

struct HeightMap
{
    FastNoise::OutputMinMax bounds;
    float data[64][64];
};

class TerrainStorage
{

public:
    HeightMap& get_heightmap(const ChunkKey& key, const WorldData& world_data)
    {
        uint32_t index = std::hash<ChunkKey>()(key) % NUM_REGIONS;
        auto& chunk = m_heightmap_regions[index];
        glm::ivec3 horizontal_key = glm::xzw(key.raw_vec);

        std::lock_guard lock(locks[index]);

        auto it = chunk.find(horizontal_key);
        if (it == chunk.end())
        {
            HeightMapData& new_data = chunk[horizontal_key];
            new_data.heightmap.bounds = TerrainNoise::generate_2d(
                &new_data.heightmap.data[0][0], glm::xz(world_data.chunk_origin(key)),
                world_data.voxels_per_chunk_axis, world_data.voxel_size(key.lod));
            return new_data.heightmap;
        }
        return it->second.heightmap;
    }

private:
    struct HeightMapData
    {
        HeightMap heightmap;
    };

    inline static constexpr const uint32_t NUM_REGIONS = 16;
    std::unordered_map<glm::ivec3, HeightMapData> m_heightmap_regions[NUM_REGIONS];
    std::mutex locks[NUM_REGIONS];
};