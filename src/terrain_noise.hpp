#pragma once

#include <glm/glm.hpp>
#include "FastNoise/FastNoise.h"

enum world_preset
{
    Mountains, Desert, count
};

class TerrainNoise
{
public:
    inline static int seed = 0;
    inline static world_preset preset = world_preset::Mountains;
    inline static FastNoise::SmartNode<FastNoise::Perlin> sourceNode;
    inline static FastNoise::SmartNode<FastNoise::FractalFBm> terrainNode;
    inline static FastNoise::SmartNode<FastNoise::Multiply> detailTerrain;
    inline static auto terrainAmpNode = FastNoise::New<FastNoise::Constant>();
    inline static FastNoise::SmartNode<FastNoise::FractalFBm> caveNode;
    inline static FastNoise::SmartNode<FastNoise::Add> finalTerrain;
    inline static FastNoise::SmartNode<FastNoise::FractalFBm> megaSource;
    inline static FastNoise::SmartNode<FastNoise::Perlin> megaTerrainNode;
    inline static FastNoise::SmartNode<FastNoise::Multiply> megaTerrainAmplitude;
    inline static FastNoise::SmartNode<FastNoise::Add> megaTerrain;
    inline static auto megaCurve = FastNoise::New<FastNoise::PowFloat>();
    inline static auto zeroNode = FastNoise::New<FastNoise::Constant>();
    inline static auto ampNode = FastNoise::New<FastNoise::Constant>();
    inline static auto biasNode = FastNoise::New<FastNoise::Constant>();


    inline static void init()
    {
        sourceNode = FastNoise::New<FastNoise::Perlin>();
        terrainNode = FastNoise::New<FastNoise::FractalFBm>();
        caveNode = FastNoise::New<FastNoise::FractalFBm>();
        finalTerrain = FastNoise::New<FastNoise::Add>();
        megaTerrainNode = FastNoise::New<FastNoise::Perlin>();
        zeroNode->SetValue(0.f);
        ampNode->SetValue(8*512.f);
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
    
    inline static float frequency = 0.25f;

    static auto generate_2d(float* height_map, glm::vec2 origin, int voxels_per_chunk_axis, float voxel_size)
    {
        glm::vec2 noise = (origin - voxel_size) * frequency;
        return finalTerrain->GenUniformGrid2D(
            height_map,
            noise.x, noise.y,
            voxels_per_chunk_axis + 2, voxels_per_chunk_axis + 2,
            voxel_size * frequency, voxel_size * frequency,
            seed
        );
    }

    static auto generate_3d(float* density_map, glm::vec3 origin, int voxels_per_chunk_axis, float voxel_size)
    {
        glm::vec3 noise = origin * frequency;
        return finalTerrain->GenUniformGrid3D(
            density_map,
            noise.x, noise.y, noise.z,
            voxels_per_chunk_axis + 2, voxels_per_chunk_axis + 2, voxels_per_chunk_axis + 2,
            voxel_size * frequency, voxel_size * frequency, voxel_size * frequency,
            seed
        );
    }
};