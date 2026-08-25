#pragma once
#include <mutex>
#include <condition_variable>
#include <glm/gtx/vec_swizzle.hpp>
#include <omp.h>
#include <glm/gtx/bit.hpp>

#include "profiler.hpp"
#include "utils.hpp"
#include "edit_octree.h"
#include "terrain_noise.hpp"
#include "world_data.hpp"
#include "edit_octree.h"

struct ChunkMeshResult
{
    uint32_t face_count = 0;
    uint32_t shrinkwrap_aabb = 0;
};

enum FaceDirection
{
    PositiveX,
    NegativeX,
    PositiveY,
    NegativeY,
    PositiveZ,
    NegativeZ
};

enum Axis
{
    X, Y, Z
};

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

inline uint32_t pack_face(uint32_t x, uint32_t y, uint32_t z, uint32_t dir, uint32_t material)
{
    return (x << 0) | (y << 6) | (z << 12) | (dir << 18) | (material << 21);
}

inline void unpack_face(uint32_t face, uint32_t* x, uint32_t* y, uint32_t* z, uint32_t* dir, uint32_t* material)
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

class ChunkMesher
{
public:
    using voxel_face_t = uint32_t;

    inline static float s_get_height(int x, int z, int32_t stride, const float* height_map)
    {
        x += 1; z += 1;
        float h = height_map[x + stride * z];
        return h;
    }

    inline static float s_get_density(int x, int y, int z, int32_t stride, int32_t stride_sq, const float* density_map)
    {
        x += 1; y += 1; z += 1;
        float d = density_map[x + stride * y + stride * stride * z];
        return d;
    }

    inline static uint32_t s_is_solid_get(int x, int y, int z, const uint32_t* solid_mask, const WorldData& world_data)
    {
        return solid_mask[x + (world_data.voxels_per_chunk_axis + 2) * y + (world_data.voxels_per_chunk_axis + 2) * (world_data.voxels_per_chunk_axis + 2) * z];
    }

    inline static void solid_set(uint32_t value, bool is_solid, int x, int y, int z, uint32_t stride, uint32_t stride_sq,
        uint32_t* solid_mask,
        uint32_t* bit_mask,
        const WorldData& world_data)
    {
        x++; y++; z++;
        solid_mask[x + (world_data.voxels_per_chunk_axis + 2) * y + (world_data.voxels_per_chunk_axis + 2) * (world_data.voxels_per_chunk_axis + 2) * z] = value;

        uint32_t idx = x + y * stride + z * stride_sq;
        uint32_t mask = 1u << x;
        uint32_t idx32 = idx / (sizeof(uint32_t) * 8);
        bit_mask[idx32] = (bit_mask[idx32] & ~mask) | (-int(is_solid) & mask);
    }

    template <FaceDirection face_dir>
    inline static void mesh_dir_packed(float voxel_size, glm::vec3 chunk_origin, uint32_t* write_buffer, uint64_t* offset, const WorldData& world_data, const uint32_t* solid_mask)
    {
        constexpr int32_t dx = face_dir == PositiveX ? 1 : face_dir == NegativeX ? -1 : 0;
        constexpr int32_t dy = face_dir == PositiveY ? 1 : face_dir == NegativeY ? -1 : 0;
        constexpr int32_t dz = face_dir == PositiveZ ? 1 : face_dir == NegativeZ ? -1 : 0;

        constexpr Axis axis = Axis(face_dir / 2);

        if constexpr (axis == Axis::Z)
        {
            for (uint32_t z = 0; z < world_data.voxels_per_chunk_axis; z++)
            {
                //uint32_t face_mask[32] = { 0 };

                for (uint32_t y = 0; y < world_data.voxels_per_chunk_axis; y++)
                    for (uint32_t x = 0; x < world_data.voxels_per_chunk_axis; x++)
                    {
                        if (s_is_solid_get(x + 1, y + 1, z + 1, solid_mask, world_data) && s_is_solid_get(x + 1 + dx, y + 1 + dy, z + 1 + dz, solid_mask, world_data) == 0u)
                        {
                            //face_mask[y] |= (1u << x);
                            emit_face_packed(write_buffer, offset, x, y, z, uint32_t(face_dir), s_is_solid_get(x + 1, y + 1, z + 1, solid_mask, world_data) & BITMASK(11));
                        }
                    }
            }
        }
        else if constexpr (axis == Axis::Y)
        {
            for (uint32_t y = 0; y < world_data.voxels_per_chunk_axis; y++)
            {
                //uint32_t face_mask[32] = { 0 };

                for (uint32_t z = 0; z < world_data.voxels_per_chunk_axis; z++)
                    for (uint32_t x = 0; x < world_data.voxels_per_chunk_axis; x++)
                    {
                        if (s_is_solid_get(x + 1, y + 1, z + 1, solid_mask, world_data) && s_is_solid_get(x + 1 + dx, y + 1 + dy, z + 1 + dz, solid_mask, world_data) == 0u)
                        {
                            //face_mask[z] |= (1u << x);
                            emit_face_packed(write_buffer, offset, x, y, z, uint32_t(face_dir), s_is_solid_get(x + 1, y + 1, z + 1, solid_mask, world_data) & BITMASK(11));
                        }
                    }
            }
        }
        else if constexpr (axis == Axis::X)
        {
            for (uint32_t x = 0; x < world_data.voxels_per_chunk_axis; x++)
            {
                //uint32_t face_mask[32] = { 0 };
                
                for (uint32_t z = 0; z < world_data.voxels_per_chunk_axis; z++)
                    for (uint32_t y = 0; y < world_data.voxels_per_chunk_axis; y++)
                    {
                        if (s_is_solid_get(x + 1, y + 1, z + 1, solid_mask, world_data) && s_is_solid_get(x + 1 + dx, y + 1 + dy, z + 1 + dz, solid_mask, world_data) == 0u)
                        {
                            //face_mask[z] |= (1u << y);
                            emit_face_packed(write_buffer, offset, x, y, z, uint32_t(face_dir), s_is_solid_get(x + 1, y + 1, z + 1, solid_mask, world_data) & BITMASK(11));
                        }
                    }
            }
        }
    }

    static ChunkMeshResult mesh_3d_packed(
        const ChunkKey& chunk_key,
        uint32_t* write_buffer,
        WorldData& world_data,
        float* height_map,
        float* density_map,
        uint32_t* solid_mask,
        uint32_t* bit_mask
    )
    {
        glm::vec3 chunk_origin = world_data.chunk_origin(chunk_key);
        float chunk_size = world_data.chunk_size(chunk_key.lod);
        float voxel_size = world_data.voxel_size(chunk_key.lod);

        ChunkMeshResult result;

        auto bounds_2d = TerrainNoise::generate_2d(height_map, glm::xz(chunk_origin), world_data.voxels_per_chunk_axis, voxel_size);

        float max_height = bounds_2d.max;
        float min_height = bounds_2d.min;
        float chunk_top = chunk_origin.y + chunk_size;
        float chunk_bot = chunk_origin.y;

        if (chunk_bot > max_height)
            return result;

        auto bounds_3d = TerrainNoise::generate_3d(density_map, chunk_origin, world_data.voxels_per_chunk_axis, voxel_size);

        if (bounds_3d.max <= 0.f || chunk_top < min_height && bounds_3d.min > 0.f)
            return result;

        uint64_t stride = 2 + world_data.voxels_per_chunk_axis;
        uint64_t stride_sq = glm::pow(stride, 2);

        glm::ivec3 aabb_min(stride - 1);
        glm::ivec3 aabb_max(0);

        for (int32_t z = -1; z < world_data.voxels_per_chunk_axis + 1; z++)
        {
            for (int32_t y = -1; y < world_data.voxels_per_chunk_axis + 1; y++)
            {
                for (int32_t x = -1; x < world_data.voxels_per_chunk_axis + 1; x++)
                {
                    glm::vec3 voxel_origin = chunk_origin + glm::vec3(x, y, z) * voxel_size;
                    float d = s_get_density(x, y, z, stride, stride_sq, density_map);
                    float h = s_get_height(x, z, stride, height_map);
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

                    solid_set(material, is_solid, x, y, z, stride, stride_sq, solid_mask, bit_mask, world_data);

                    if (is_solid &&
                        x != -1 && x != world_data.voxels_per_chunk_axis &&
                        y != -1 && y != world_data.voxels_per_chunk_axis &&
                        z != -1 && z != world_data.voxels_per_chunk_axis)
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
        mesh_dir_packed<PositiveX>(voxel_size, chunk_origin, write_buffer, &write_idx, world_data, solid_mask);
        mesh_dir_packed<NegativeX>(voxel_size, chunk_origin, write_buffer, &write_idx, world_data, solid_mask);

        mesh_dir_packed<PositiveY>(voxel_size, chunk_origin, write_buffer, &write_idx, world_data, solid_mask);
        mesh_dir_packed<NegativeY>(voxel_size, chunk_origin, write_buffer, &write_idx, world_data, solid_mask);

        mesh_dir_packed<PositiveZ>(voxel_size, chunk_origin, write_buffer, &write_idx, world_data, solid_mask);
        mesh_dir_packed<NegativeZ>(voxel_size, chunk_origin, write_buffer, &write_idx, world_data, solid_mask);

        result.face_count = write_idx;

        return result;
    }
};

struct GreedyFace
{
    uint64_t packed;

    void pack(uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t dir, uint32_t custom)
    {
        packed =
            (static_cast<uint64_t>(x & BITMASK(6))              << 0) |
            (static_cast<uint64_t>(y & BITMASK(6))              << 6) |
            (static_cast<uint64_t>(z & BITMASK(6))              << 12) |
            (static_cast<uint64_t>((width - 1) & BITMASK(6))    << 18) |
            (static_cast<uint64_t>((height - 1) & BITMASK(6))   << 24) |
            (static_cast<uint64_t>(dir & BITMASK(3))            << 30) |
            (static_cast<uint64_t>(custom & BITMASK(31))        << 33);
    };

    void unpack(uint32_t* x, uint32_t* y, uint32_t* z, uint32_t* width, uint32_t* height, uint32_t* dir, uint32_t* custom)
    {
        *x =        static_cast<uint32_t>(packed >> 0) & BITMASK(6);
        *y =        static_cast<uint32_t>(packed >> 6) & BITMASK(6);
        *z =        static_cast<uint32_t>(packed >> 12) & BITMASK(6);
        *width =    (static_cast<uint32_t>(packed >> 18) & BITMASK(6)) + 1;
        *height =   (static_cast<uint32_t>(packed >> 24) & BITMASK(6)) + 1;
        *dir =      static_cast<uint32_t>(packed >> 30) & BITMASK(3);
        *custom =   static_cast<uint32_t>(packed >> 33) & BITMASK(31);
    }
};

struct ChunkGreedyMesherResult
{
    uint64_t face_count;
};
struct VoxelCoord
{
    int x, y, z;
};
struct VoxelData
{

    float height_map[64][64];
    float density_map[64][64][64];
    uint16_t material_map[64][64][64];
    uint64_t solid_mask[64][64];
    union
    {
        uint64_t face_mask_3d[64][64];
        uint64_t face_mask[64];
    };

    uint16_t calculate_material(uint32_t x, uint32_t y, uint32_t z, glm::vec3 chunk_origin, float voxel_size)
    {
        glm::vec3 voxel_origin = chunk_origin + glm::vec3(x-1, y-1, z-1) * voxel_size;
        float d = 1; density_map[z][y][x];
        float h = height_map[z][x];
        bool is_solid = (voxel_origin.y <= h && d > 0.f);
        
        uint16_t material = 0;
        if (is_solid)
        {

            if (voxel_origin.y > 1024.f)
                material = 1;
            else
            {
                material = 1+uint16_t(voxel_origin.x + voxel_origin.y + voxel_origin.z) % 7;
            }
            if (voxel_origin.y > 3000)
                material = 0;
        }
        
        return material;

    }

    uint16_t get_structure_material(WorldEdits* edits, const std::vector<structure_id>* structures, glm::vec3 voxel_origin, float voxel_size)
    {
        for (int i = structures->size() - 1; i >= 0; i--)
        {
            structure_id id = (*structures)[i];
            OctreeStructure* structure = edits->get_structure(id);
            uint16_t material = structure->get_voxel(voxel_origin, voxel_size);
            
            if (material)
                return material;
        }

        return 0;
    }

    bool compute_terrain(const ChunkKey& key, const WorldData& world_data, WorldEdits* edits)
    {
        float voxel_size = world_data.voxel_size(key.lod);
        float chunk_size = world_data.chunk_size(key.lod);
        int voxels_per_chunk_axis = world_data.voxels_per_chunk_axis;
        glm::vec3 chunk_origin = world_data.chunk_origin(key);

        auto bounds_2d = TerrainNoise::generate_2d(&height_map[0][0], glm::xz(chunk_origin), voxels_per_chunk_axis, voxel_size);

        float max_height = bounds_2d.max;
        float min_height = bounds_2d.min;
        float chunk_top = chunk_origin.y + chunk_size;
        float chunk_bot = chunk_origin.y;

        aabb3d aabb;
        aabb.min = world_data.chunk_origin(key);
        aabb.max = aabb.min + world_data.chunk_size(key.lod);
        auto structures = edits->find_structures_in_region(aabb);
        
        if (structures->size() == 0 && (chunk_bot > max_height || chunk_top < min_height))
            return false;


        //auto bounds_3d = TerrainNoise::generate_3d(&density_map[0][0][0], chunk_origin, world_data.voxels_per_chunk_axis, voxel_size);
        //
        //if (bounds_3d.max <= 0.f || chunk_top < min_height && bounds_3d.min > 0.f)
        //    return false;

        glm::ivec3 aabb_min(world_data.voxels_per_chunk_axis);
        glm::ivec3 aabb_max(0);
        for (int z = 0; z < voxels_per_chunk_axis + 2; z++)
            for (int y = 0; y < voxels_per_chunk_axis + 2; y++)
            {
                solid_mask[z][y] = 0;
                for (int x = 0; x < voxels_per_chunk_axis + 2; x++)
                {
                    glm::vec3 voxel_origin = glm::vec3(x, y, z) * voxel_size + chunk_origin;

                    uint16_t material = 0;

                    if (structures)
                        material = get_structure_material(edits, structures, voxel_origin, voxel_size);
                   
                    if (material == 0)
                        material = calculate_material(x, y, z, chunk_origin, voxel_size);
                    
                    bool is_solid = material != 0;
                    material_map[z][y][x] = material;
                    solid_mask[z][y] |= is_solid ? (1ull << x) : 0;
                    
                    if (is_solid &&
                        x > 0 && x <= world_data.voxels_per_chunk_axis &&
                        y > 0 && y <= world_data.voxels_per_chunk_axis &&
                        z > 0 && z <= world_data.voxels_per_chunk_axis)
                    {
                        aabb_min.x = glm::min(aabb_min.x, x - 1);
                        aabb_min.y = glm::min(aabb_min.y, y - 1);
                        aabb_min.z = glm::min(aabb_min.z, z - 1);
                        aabb_max.x = glm::max(aabb_max.x, x - 1);
                        aabb_max.y = glm::max(aabb_max.y, y - 1);
                        aabb_max.z = glm::max(aabb_max.z, z - 1);
                    }
                    
                }
            }

        return true;
    }

    // Fills face_mask slice at pos with data, if dir uses axis X, then face_mask_3d is populated and param pos is unused
    void compute_face_mask(FaceDirection dir, int pos, int voxels_per_chunk_axis)
    {
        Axis axis = Axis(dir / 2);

        if (axis == Axis::Z)
        {
            int z = pos;
            int32_t dz = dir == PositiveZ ? 1 : dir == NegativeZ ? -1 : 0;
            for (int y = 0; y < 64; y++)
            {
                face_mask[y] = solid_mask[z][y] & (~solid_mask[z + dz][y]); // YX
            }
        }
        else if (axis == Axis::Y)
        {
            int y = pos;
            int32_t dy = dir == PositiveY ? 1 : dir == NegativeY ? -1 : 0;
            for (int z = 0; z < voxels_per_chunk_axis + 2; z++)
            {
                face_mask[z] = solid_mask[z][y] & (~solid_mask[z][y + dy]); // ZX
            }
        }
        else if (axis == Axis::X)
        {
            int32_t dx = dir == PositiveX ? 1 : (dir == NegativeX ? -1 : 0);
            if (dx == 1)
            {
                for (int z = 0; z < voxels_per_chunk_axis + 2; z++)
                {
                    for (int y = 0; y < voxels_per_chunk_axis + 2; y++)
                    {
                        uint64_t row = solid_mask[z][y];
                        face_mask_3d[z][y] = row & ~static_cast<uint64_t>(row >> 1); // ZYX
                    }
                }
            }
            else
            {
                for (int z = 0; z < voxels_per_chunk_axis + 2; z++)
                {
                    for (int y = 0; y < voxels_per_chunk_axis + 2; y++)
                    {
                        uint64_t row = solid_mask[z][y];
                        face_mask_3d[z][y] = row & ~static_cast<uint64_t>(row << 1); // ZYX
                    }
                }
            }
        }
    }

    VoxelCoord tbn_to_coord(int t, int b, int n, FaceDirection dir)
    {
        switch (dir)
        {
        case PositiveX:
        case NegativeX:
        {
            return { n,t,b };
        }
        case PositiveY:
        case NegativeY:
        {
            return { t,n,b };
        }
        case PositiveZ:
        case NegativeZ:
        {
            return { t,b,n };
        }
        default:
            return { 0,0,0 };
        }
        return { 0,0,0 };
    }

    uint64_t& get_face_mask(uint64_t n, uint64_t b, FaceDirection dir)
    {
        if (dir == PositiveX || dir == NegativeX)
            return face_mask_3d[b][n];
        return face_mask[b];
    }

    uint16_t get_material(int x, int y, int z)
    {
        return material_map[z][y][x];
    }

    inline uint16_t get_material_tbn(int t, int b, int n, FaceDirection dir)
    {
        VoxelCoord c = tbn_to_coord(t, b, n, dir);
        return material_map[c.z][c.y][c.x];
    }

    uint16_t get_material(const VoxelCoord& coord)
    {
        return material_map[coord.z][coord.y][coord.x];
    }

    inline void mesh_slice(FaceDirection dir, int n, int voxels_per_chunk_axis, GreedyFace* out_buffer, ChunkGreedyMesherResult& result)
    {
        // z == b : X
        for (int b = 1; b <= voxels_per_chunk_axis; b++)
        {
            uint64_t& row = get_face_mask(n, b, dir);
            row = row & ~((1ull) | (1ull << 63)); // clear 0th and last bit (since its neighbor chunk data)

            while (row)
            {
                //y == t : X
                int t0 = glm::findLSB(row); // 1 to 62 can only be returned (or -1)

                if (t0 == -1)
                    break;

                int t = t0;
                uint16_t material = get_material_tbn(t, b, n, dir);

                while (t <= voxels_per_chunk_axis && get_material_tbn(t, b, n, dir) == material && (row & (1ull << t)) != 0)
                    t++;
                // b <= 62 -> w <= 62

                int w = t - t0;

                if (!(w <= voxels_per_chunk_axis)) exit(1);
                if (!(t0 <= voxels_per_chunk_axis)) exit(1);
                uint64_t strip_mask = BITMASK(w) << t0;

                int b1 = b + 1;

                while (b1 <= voxels_per_chunk_axis)
                {
                    uint64_t& next_row = get_face_mask(n, b1, dir);
                    next_row = next_row & ~((1ull) | (1ull << 63));

                    bool is_same_solid = strip_mask == (strip_mask & next_row); // check if neighbor row contains same adjacent solid voxels
                    if (!is_same_solid)
                        break;

                    bool all_equal = true;
                    for (int ti = t0; ti < t; ti++) // check if their material is same
                    {
                        if (get_material_tbn(ti, b1, n, dir) != material)
                        {
                            all_equal = false;
                            break;
                        }
                    }
                    if (!all_equal)
                        break;

                    b1++; // merge the face strip
                    next_row &= ~strip_mask; // erase the row since we merged the face
                }


                int h = b1 - b;

                GreedyFace face;
                VoxelCoord c = tbn_to_coord(t0 - 1, b - 1, n - 1, dir);

                if (dir == PositiveY or dir == NegativeY)
                    face.pack(c.x, c.y, c.z, h, w, dir, material);
                else
                    face.pack(c.x, c.y, c.z, w, h, dir, material);
                out_buffer[result.face_count++] = face;

                row &= (~0ull << t);
            }
        }
    }
};

static ChunkGreedyMesherResult mesh_naive(VoxelData* data, GreedyFace* out_buffer, int voxels_per_chunk_axis)
{
    ChunkGreedyMesherResult result;
    result.face_count = 0;
    for (int z = 1; z <= voxels_per_chunk_axis; z++)
        for (int y = 1; y <= voxels_per_chunk_axis; y++)
            for (int x = 1; x <= voxels_per_chunk_axis; x++)
            {
                for (int i = 0; i < 6; i++)
                {
                    glm::ivec3 d =
                    {
                        (i == 0) - (i == 1),
                        (i == 2) - (i == 3),
                        (i == 4) - (i == 5)
                    };
                    if (data->material_map[z][y][x] != 0 && data->material_map[z + d.z][y + d.y][x + d.x] == 0)
                    {
                        GreedyFace face;
                        face.pack(x-1, y-1, z-1, 1, 1, i, data->material_map[z][y][x]);
                        out_buffer[result.face_count++] = face;
                    }
                }
            }
    return result;
}

static ChunkGreedyMesherResult mesh_greedy(VoxelData* data, GreedyFace* out_buffer, int voxels_per_chunk_axis)
{
    ChunkGreedyMesherResult result;
    result.face_count = 0;

    data->compute_face_mask(PositiveX, -1, voxels_per_chunk_axis);
    for (int i = 1; i <= voxels_per_chunk_axis; i++)
        transpose_matrix(data->face_mask_3d[i]); // ZYX to ZXY order

    for (int x = 1; x <= voxels_per_chunk_axis; x++)
        data->mesh_slice(PositiveX, x, voxels_per_chunk_axis, out_buffer, result);

    data->compute_face_mask(NegativeX, -1, voxels_per_chunk_axis);
    for (int i = 1; i <= voxels_per_chunk_axis; i++)
        transpose_matrix(data->face_mask_3d[i]); // ZYX to ZXY order

    for (int x = 1; x <= voxels_per_chunk_axis; x++)
        data->mesh_slice(NegativeX, x, voxels_per_chunk_axis, out_buffer, result);

    for (int i = 1; i <= voxels_per_chunk_axis; i++)
    {
        data->compute_face_mask(PositiveZ, i, voxels_per_chunk_axis);
        data->mesh_slice(PositiveZ, i, voxels_per_chunk_axis, out_buffer, result);
        data->compute_face_mask(NegativeZ, i, voxels_per_chunk_axis);
        data->mesh_slice(NegativeZ, i, voxels_per_chunk_axis, out_buffer, result);

        data->compute_face_mask(PositiveY, i, voxels_per_chunk_axis);
        data->mesh_slice(PositiveY, i, voxels_per_chunk_axis, out_buffer, result);
        data->compute_face_mask(NegativeY, i, voxels_per_chunk_axis);
        data->mesh_slice(NegativeY, i, voxels_per_chunk_axis, out_buffer, result);
    }

    return result;
}
