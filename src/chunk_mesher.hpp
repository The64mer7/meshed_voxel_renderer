#pragma once
#include <mutex>
#include <condition_variable>
#include <glm/gtx/vec_swizzle.hpp>

#include "profiler.hpp"
#include "utils.hpp"
#include "edit_octree.h"
#include "terrain_noise.hpp"
#include "world_data.hpp"

struct ChunkMeshResult
{
    uint32_t face_count = 0;
    uint32_t shrinkwrap_aabb = 0;
};

class ChunkMesher
{
public:
	void create(const WorldData& world_data)
	{
		m_world_data = world_data;

        size_t size_2d = glm::pow(world_data.voxels_per_chunk_axis + 2, 2u);
        size_t size_3d = glm::pow(world_data.voxels_per_chunk_axis + 2, 3u);

        m_height_map.resize(size_2d);
        m_density_map.resize(size_3d);

        m_solid_mask.resize(size_3d);
        m_bit_mask.resize((size_3d + 31) / 32);
	}
    
    ChunkMeshResult mesh_3d_packed(const ChunkKey& chunk_key, uint32_t* write_buffer)
    {
        glm::vec3 chunk_origin = m_world_data.chunk_origin(chunk_key);
        float chunk_size = m_world_data.chunk_size(chunk_key.lod);
        float voxel_size = m_world_data.voxel_size(chunk_key.lod);

        ChunkMeshResult result;

        auto bounds_2d = TerrainNoise::generate_2d(m_height_map.data(), glm::xz(chunk_origin), m_world_data.voxels_per_chunk_axis, voxel_size);

        float max_height = bounds_2d.max;
        float min_height = bounds_2d.min;
        float chunk_top = chunk_origin.y + chunk_size;
        float chunk_bot = chunk_origin.y;

        if (chunk_bot > max_height)
            return result;

        auto bounds_3d = TerrainNoise::generate_3d(m_density_map.data(), chunk_origin, m_world_data.voxels_per_chunk_axis, voxel_size);
        
        if (bounds_3d.max <= 0.f || chunk_top < min_height && bounds_3d.min > 0.f)
            return result;

        uint64_t stride = 2 + m_world_data.voxels_per_chunk_axis;
        uint64_t stride_sq = glm::pow(stride, 2);

        glm::ivec3 aabb_min(stride - 1);
        glm::ivec3 aabb_max(0);

        for (int32_t z = -1; z < m_world_data.voxels_per_chunk_axis + 1; z++)
        {
            for (int32_t y = -1; y < m_world_data.voxels_per_chunk_axis + 1; y++)
            {
                for (int32_t x = -1; x < m_world_data.voxels_per_chunk_axis + 1; x++)
                {
                    glm::vec3 voxel_origin = chunk_origin + glm::vec3(x, y, z) * voxel_size;
                    float d = get_density(x, y, z, stride, stride_sq);
                    float h = get_height(x, z, stride);
                    bool is_solid = (voxel_origin.y <= h && d > 0.f);


                    //TODO: make it modular
                    uint32_t material = 5;
                    if (is_solid)
                    {
                        if (voxel_origin.y < h)
                            material = 3;
                    }
                    else
                    {
                        material = 0;
                    }

                    solid_set(material, is_solid, x, y, z, stride, stride_sq);

                    if (is_solid &&
                        x != -1 && x != m_world_data.voxels_per_chunk_axis &&
                        y != -1 && y != m_world_data.voxels_per_chunk_axis &&
                        z != -1 && z != m_world_data.voxels_per_chunk_axis)
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

        result.shrinkwrap_aabb = make_aabb(aabb_min, aabb_max);
        uint64_t write_idx = 0;
        mesh_dir_packed<PositiveX>(voxel_size, chunk_origin, write_buffer, &write_idx);
        mesh_dir_packed<NegativeX>(voxel_size, chunk_origin, write_buffer, &write_idx);

        mesh_dir_packed<PositiveY>(voxel_size, chunk_origin, write_buffer, &write_idx);
        mesh_dir_packed<NegativeY>(voxel_size, chunk_origin, write_buffer, &write_idx);

        mesh_dir_packed<PositiveZ>(voxel_size, chunk_origin, write_buffer, &write_idx);
        mesh_dir_packed<NegativeZ>(voxel_size, chunk_origin, write_buffer, &write_idx);

        result.face_count = write_idx;
    }
    using voxel_face_t = uint32_t;
private:
    std::vector<float> m_height_map;
    
    std::vector<float> m_density_map;
    std::vector<uint32_t> m_solid_mask;
    std::vector<uint32_t> m_bit_mask;

    WorldData m_world_data;

    inline float get_height(int x, int z, int32_t stride) // -1 to VOXELS_PER_AXIS
    {
        x += 1; z += 1;
        float h = m_height_map[x + stride * z];
        return h;

    }

    inline float get_density(int x, int y, int z, int32_t stride, int32_t stride_sq) // -1 to VOXELS_PER_AXIS
    {
        x += 1; y += 1; z += 1;
        float d = m_density_map[x + stride * y + stride * stride * z];
        return d;
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
            (min.x << 0) |
            (min.y << 5) |
            (min.z << 10) |
            (max.x << 15) |
            (max.y << 20) |
            (max.z << 25);
    }
    PROFILER_DECLARE(structures_query);


    inline uint32_t is_solid_get(int x, int y, int z)
    {
        return m_solid_mask[x + (m_world_data.voxels_per_chunk_axis + 2) * y + (m_world_data.voxels_per_chunk_axis + 2) * (m_world_data.voxels_per_chunk_axis + 2) * z];
    }

    inline void solid_set(uint32_t value, bool is_solid, int x, int y, int z, uint32_t stride, uint32_t stride_sq)
    {
        x++; y++; z++;
        m_solid_mask[x + (m_world_data.voxels_per_chunk_axis + 2) * y + (m_world_data.voxels_per_chunk_axis + 2) * (m_world_data.voxels_per_chunk_axis + 2) * z] = value;

        uint32_t idx = x + y * stride + z * stride_sq;
        uint32_t mask = 1u << x;
        uint32_t idx32 = idx / (sizeof(uint32_t) * 8);
        m_bit_mask[idx32] = (m_bit_mask[idx32] & ~mask) | (-int(is_solid) & mask);
    }


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
    voxel_face_t pack_face(uint32_t x, uint32_t y, uint32_t z, uint32_t dir, uint32_t material)
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
    void unpack_face(voxel_face_t face, uint32_t* x, uint32_t* y, uint32_t* z, uint32_t* dir, uint32_t* material)
    {
        *x = (face >> 0) & BITMASK(6);
        *y = (face >> 6) & BITMASK(6);
        *z = (face >> 12) & BITMASK(6);
        *dir = (face >> 18) & BITMASK(3);
        *material = (face >> 21) & BITMASK(11);
    }

    inline void emit_face_packed(uint32_t* out_buffer, uint64_t* offset, uint32_t x, uint32_t y, uint32_t z, uint32_t dir, uint32_t material)
    {
        out_buffer[*offset] = pack_face(x, y, z, dir, material);
        (*offset)++;
    }

    template<FaceDirection face_dir>
    inline void mesh_dir_packed(float voxel_size, glm::vec3 chunk_origin, uint32_t* write_buffer, uint64_t* offset)
    {
        constexpr int32_t dx = face_dir == PositiveX ? 1 : face_dir == NegativeX ? -1 : 0;
        constexpr int32_t dy = face_dir == PositiveY ? 1 : face_dir == NegativeY ? -1 : 0;
        constexpr int32_t dz = face_dir == PositiveZ ? 1 : face_dir == NegativeZ ? -1 : 0;

        constexpr Axis axis = Axis(face_dir / 2);

        if constexpr (axis == Axis::Z)
        {
            for (uint32_t z = 0; z < m_world_data.voxels_per_chunk_axis; z++)
            {
                uint32_t face_mask[32] = { 0 };
                for (uint32_t y = 0; y < m_world_data.voxels_per_chunk_axis; y++)
                    for (uint32_t x = 0; x < m_world_data.voxels_per_chunk_axis; x++)
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
            for (uint32_t y = 0; y < m_world_data.voxels_per_chunk_axis; y++)
            {
                uint32_t face_mask[32] = { 0 };
                for (uint32_t z = 0; z < m_world_data.voxels_per_chunk_axis; z++)
                    for (uint32_t x = 0; x < m_world_data.voxels_per_chunk_axis; x++)
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
            for (uint32_t x = 0; x < m_world_data.voxels_per_chunk_axis; x++)
            {
                uint32_t face_mask[32] = { 0 };
                for (uint32_t z = 0; z < m_world_data.voxels_per_chunk_axis; z++)
                    for (uint32_t y = 0; y < m_world_data.voxels_per_chunk_axis; y++)
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
};