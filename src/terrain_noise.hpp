#pragma once

#include <glm/glm.hpp>
#include "FastNoise/FastNoise.h"
#include <cassert>

enum world_preset
{
    Mountains, Desert, count
};

class TerrainNoise
{
public:
    inline static int seed = 0;
    inline static world_preset preset = world_preset::Mountains;
    inline static float frequency = 0.25f;

    inline static FastNoise::SmartNode<FastNoise::Perlin> source_node;
    inline static FastNoise::SmartNode<FastNoise::FractalFBm> terrain_node;
    inline static FastNoise::SmartNode<FastNoise::Multiply> detail_terrain;
    inline static FastNoise::SmartNode<FastNoise::Constant> terrain_amp_node;
    inline static FastNoise::SmartNode<FastNoise::Add> final_terrain;
    inline static FastNoise::SmartNode<FastNoise::Perlin> mega_terrain_node;
    inline static FastNoise::SmartNode<FastNoise::Multiply> mega_terrain_amplitude;
    inline static FastNoise::SmartNode<FastNoise::Add> mega_terrain;
    inline static FastNoise::SmartNode<FastNoise::Constant> amp_node;
    inline static FastNoise::SmartNode<FastNoise::Constant> bias_node;

    inline static void init()
    {
        source_node = FastNoise::New<FastNoise::Perlin>();
        terrain_node = FastNoise::New<FastNoise::FractalFBm>();
        final_terrain = FastNoise::New<FastNoise::Add>();
        mega_terrain_node = FastNoise::New<FastNoise::Perlin>();
        mega_terrain_amplitude = FastNoise::New<FastNoise::Multiply>();
        mega_terrain = FastNoise::New<FastNoise::Add>();
        detail_terrain = FastNoise::New<FastNoise::Multiply>();

        terrain_amp_node = FastNoise::New<FastNoise::Constant>();
        amp_node = FastNoise::New<FastNoise::Constant>();
        bias_node = FastNoise::New<FastNoise::Constant>();

        zero_node_values();

        terrain_node->SetSource(source_node);

        detail_terrain->SetLHS(terrain_node);
        detail_terrain->SetRHS(terrain_amp_node);

        mega_terrain_node->SetScale(1024.f);
        mega_terrain_amplitude->SetLHS(amp_node);
        mega_terrain_amplitude->SetRHS(mega_terrain_node);

        mega_terrain->SetLHS(bias_node);
        mega_terrain->SetRHS(mega_terrain_amplitude);

        final_terrain->SetLHS(mega_terrain);
        final_terrain->SetRHS(detail_terrain);

        update_preset_parameters();
    }

    inline static void update_preset_parameters()
    {
        if (!terrain_node || !terrain_amp_node || !bias_node) return;

        switch (preset)
        {
        case world_preset::Mountains:
        {
            terrain_node->SetOctaveCount(4);
            terrain_node->SetLacunarity(2.2f);
            terrain_node->SetGain(0.5f);

            terrain_amp_node->SetValue(32.f);
            bias_node->SetValue(1024.f);
        }
        break;
        case world_preset::Desert:
        {
            terrain_node->SetOctaveCount(5);
            terrain_node->SetLacunarity(2.0f);
            terrain_node->SetGain(0.2f);

            terrain_amp_node->SetValue(16.f);
            bias_node->SetValue(512.f);
        }
        break;
        default:
            break;
        }
    }

    inline static void zero_node_values()
    {
        if (amp_node) amp_node->SetValue(8 * 512.f);
        if (bias_node) bias_node->SetValue(1024.f);
        if (terrain_amp_node) terrain_amp_node->SetValue(32.f);
    }

    static auto generate_2d(float* height_map, glm::vec2 origin, int voxels_per_chunk_axis, float voxel_size)
    {
        assert(final_terrain && "TerrainNoise::init() was not called before generate_2d!");

        glm::vec2 noise = (origin - voxel_size) * frequency;
        return final_terrain->GenUniformGrid2D(
            height_map,
            noise.x, noise.y,
            voxels_per_chunk_axis + 2, voxels_per_chunk_axis + 2,
            voxel_size * frequency, voxel_size * frequency,
            seed
        );
    }

    static auto generate_3d(float* density_map, glm::vec3 origin, int voxels_per_chunk_axis, float voxel_size)
    {
        assert(final_terrain && "TerrainNoise::init() was not called before generate_3d!");

        glm::vec3 noise = origin * frequency;
        return final_terrain->GenUniformGrid3D(
            density_map,
            noise.x, noise.y, noise.z,
            voxels_per_chunk_axis + 2, voxels_per_chunk_axis + 2, voxels_per_chunk_axis + 2,
            voxel_size * frequency, voxel_size * frequency, voxel_size * frequency,
            seed
        );
    }
};