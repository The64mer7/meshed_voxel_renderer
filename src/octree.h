#pragma once

#include <stdio.h>
#include <stack>
#include <unordered_set>
#include <functional>
#include "tree_node.h"
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtx/norm.hpp>
#include <set>

#include "world_data.hpp"
#include "edit_octree.h"

static bool intersect_sphere_aabb3d(
    float sX, float sY, float sZ, float radius,
    float minX, float minY, float minZ,
    float maxX, float maxY, float maxZ,
    float* outDistSquared = nullptr)
{
    float closestX = (sX < minX) ? minX : (sX > maxX) ? maxX : sX;
    float closestY = (sY < minY) ? minY : (sY > maxY) ? maxY : sY;
    float closestZ = (sZ < minZ) ? minZ : (sZ > maxZ) ? maxZ : sZ;

    float dx = sX - closestX;
    float dy = sY - closestY;
    float dz = sZ - closestZ;

    float distToPointSq = (dx * dx) + (dy * dy) + (dz * dz);

    float distanceSquared = distToPointSq - (radius * radius);

    if (outDistSquared)
        *outDistSquared = distanceSquared;

    return distanceSquared < 0;
}

struct OctreeClipmapGenerateSettings
{
    uint32_t min_depth;
    uint32_t max_depth;
    float radius;
    float further_radius;
    uint32_t chunks_per_lod;
};

constexpr uint64_t invalid_id = UINT64_MAX;
struct FlatOctreeNode
{
    uint64_t children[8] = { invalid_id ,invalid_id ,invalid_id ,invalid_id ,invalid_id ,invalid_id ,invalid_id ,invalid_id };
    bool is_leaf = true;
};

class OctreeClipmap
{
public:
    using LeavesSet = std::unordered_set<ChunkKeyRaw>;
    using LeavesVector = std::vector<ChunkKey>;

    LeavesVector& get_leaves()
    {
        return m_leaves_curr;
    }

    LeavesVector& get_leaves_created()
    {
        return m_leaves_created;
    }

    std::vector<FlatOctreeNode> nodes;

    float origin_x = 0.f, origin_y = 0.f, origin_z = 0.f, world_scale = 0.f;
    
    ChunkKey find_leaf(const glm::vec3& position)
    {

        for (size_t i = 0; i < m_leaves_curr.size(); i++)
        {
            ChunkKey key = m_leaves_curr[i];
            aabb3d aabb;
            float chunk_size = world_scale / glm::pow(2, key.lod);
            aabb.min = glm::vec3(key.coord) * chunk_size;
            aabb.max = aabb.min + chunk_size;
            if (aabb.contains_point(position))
                return key;
        }
        return ChunkKey(0, 0, 0, 0);
        ChunkKey key = ChunkKey{ 0,0,0,0 };
        
        uint32_t i = 0;
        glm::vec3 origin = { origin_x, origin_y, origin_z };
        float chunk_size = world_scale;
        
        while (i < nodes.size())
        {
            FlatOctreeNode node = nodes[i];
            if (node.is_leaf)
                return key;

            float children_size = chunk_size * 0.5f;

            glm::vec3 offset = position - origin;
            glm::ivec3 indices = glm::floor(offset / children_size);
            if (!glm::all(glm::greaterThanEqual(indices,  glm::ivec3(0))) ||
                !glm::all(glm::lessThan(indices,          glm::ivec3(2))))
            {
                return key;
            }
            int idx = indices.x + 2 * indices.y + 4 * indices.z;
            if (node.children[idx] == invalid_id)
                return key;

            i = node.children[idx];

            key = key.get_octree_child(indices.x, indices.y, indices.z);
            origin = origin + children_size * glm::vec3(indices);
        }
        return key;
    }

    void regenerate(const aabb3d& bounds)
    {
        m_leaves_created.clear();
        m_leaves_removed.clear();

        struct StackItem
        {
            uint64_t index;
            aabb3d bounds;
            ChunkKey key;
        };
        aabb3d root_bounds;
        root_bounds.min = { origin_x, origin_y, origin_z };
        root_bounds.max = root_bounds.min + world_scale;

        StackItem root = { 0, root_bounds, glm::ivec4(0) };
        
        std::stack<StackItem> stack;
        stack.push(root);

        while (!stack.empty())
        {
            StackItem item = stack.top();
            stack.pop();
            if (item.index == invalid_id || item.index >= nodes.size())
                continue;

            if (!item.bounds.intersects(bounds))
                continue;

            FlatOctreeNode& node = nodes[item.index];

            if (node.is_leaf)
            {
                m_leaves_created.push_back(item.key);
                m_leaves_removed.push_back(item.key);
            }
            else
            {
                for (int i = 0; i < 8; i++)
                {
                    int x = i % 2;
                    int y = (i / 2) % 2;
                    int z = (i / 4) % 2;

                    uint64_t child_index = node.children[i];

                    if (child_index == invalid_id)
                        continue;

                    ChunkKey child_key = item.key.get_octree_child(x, y, z);

                    StackItem child_item;
                    child_item.index = child_index;
                    child_item.key = child_key;
                    child_item.bounds = item.bounds.octree_child(i);

                    stack.push(child_item);
                }
            }
        }
    }

    void generate_by_chunks(const OctreeClipmapGenerateSettings& settings, const glm::vec3& position)
    {
        generate(
            settings.min_depth, 
            settings.max_depth, 
            position.x, 
            position.y, 
            position.z, 
            settings.radius, 
            settings.further_radius, 
            settings.further_radius * glm::pow(0.5f, settings.chunks_per_lod)
        );
    }
    void generate_by_perspective(uint32_t min_depth, uint32_t max_depth, const glm::vec3& position, float fov, float radius)
    {
        nodes.clear();

        m_leaves_prev = std::move(m_leaves_curr);

        struct StackItem
        {
            uint64_t parent_idx;
            uint8_t parent_spatial_idx;
            ChunkKey leaf_data;
            float px, py, pz, size;
            float depth;
        };
        std::stack<StackItem> stack;

        StackItem root;
        root.parent_idx = invalid_id;
        root.parent_spatial_idx = 0;
        root.leaf_data = glm::ivec4(0);
        root.px = origin_x;
        root.py = origin_y;
        root.pz = origin_z;
        root.size = world_scale;
        root.depth = 0;

        stack.push(root);
        float focal_length = glm::tan(fov);

        while (!stack.empty())
        {
            StackItem parent = stack.top();
            stack.pop();

            uint64_t node_idx = nodes.size();
            nodes.push_back(FlatOctreeNode());

            if (parent.depth >= max_depth)
            {
                m_leaves_curr.push_back(parent.leaf_data.raw);
                continue;
            }

            float child_size = parent.size * 0.5f;

            bool has_children = false;
            for (int z = 0; z < 2; z++)
                for (int y = 0; y < 2; y++)
                    for (int x = 0; x < 2; x++)
                    {
                        float cx = parent.px + child_size * x;
                        float cy = parent.py + child_size * y;
                        float cz = parent.pz + child_size * z;
                        float dist = glm::distance(glm::vec3{ cx,cy, cz } + child_size * 0.5f, position);

                        ChunkKey leaf;
                        leaf = parent.leaf_data;
                        leaf.lod += 1;
                        leaf.x = (leaf.x << 1) | x;
                        leaf.y = (leaf.y << 1) | y;
                        leaf.z = (leaf.z << 1) | z;

                        float dist_ratio = glm::max(1.0f, dist / radius);
                        float target_depth = max_depth - glm::log2(dist_ratio);
                        target_depth = glm::max(0.f, target_depth);

                        if (parent.depth < target_depth || parent.depth < min_depth)
                        {

                            has_children = true;
                            StackItem child_item;
                            child_item.depth = parent.depth + 1;
                            child_item.size = child_size;
                            child_item.px = cx;
                            child_item.py = cy;
                            child_item.pz = cz;
                            child_item.leaf_data = leaf;
                            child_item.parent_spatial_idx = x + 2 * y + 4 * z;
                            child_item.parent_idx = node_idx;
                            stack.push(child_item);
                        }
                        else
                        {
#if 0 // include leaves
                            uint64_t leaf_node_idx = nodes.size();
                            nodes.push_back(FlatOctreeNode());
                            nodes[node_idx].children[x + 2 * y + 4 * z] = leaf_node_idx;
#endif
                            m_leaves_curr.push_back(leaf.raw);
                        }
                    }

            if (has_children && parent.parent_idx != invalid_id)
            {
                nodes[parent.parent_idx].is_leaf = false;
                nodes[parent.parent_idx].children[parent.parent_spatial_idx] = node_idx;
            }
        }
        compute_leaves_delta();
    }

    void generate(uint32_t min_depth, uint32_t max_depth, float circle_x, float circle_y, float circle_z, float radius, float further_radius, float intensity)
    {
        nodes.clear();

        m_leaves_prev = std::move(m_leaves_curr);

        struct StackItem
        {
            uint64_t parent_idx;
            uint8_t parent_spatial_idx;
            ChunkKey leaf_data;
            float px, py, pz, size;
            float depth;
        };
        std::stack<StackItem> stack;
        StackItem root_item = { invalid_id, 0, glm::ivec4(0), origin_x, origin_y, origin_z, world_scale, 0};
        stack.push(root_item);

        while (!stack.empty())
        {
            StackItem parent = stack.top();
            stack.pop();

            uint64_t node_idx = nodes.size();
            nodes.push_back(FlatOctreeNode());

            if (parent.depth >= max_depth)
            {
                m_leaves_curr.push_back(parent.leaf_data.raw);
                continue;
            }

            float child_size = parent.size * 0.5f;
            
            bool has_children = false;
            for (int z = 0; z < 2; z++)
                for (int y = 0; y < 2; y++)
                    for (int x = 0; x < 2; x++)
                    {
                        float cx = parent.px + child_size * x;
                        float cy = parent.py + child_size * y;
                        float cz = parent.pz + child_size * z;
                        float dist2;
                        bool closer = intersect_sphere_aabb3d(circle_x, circle_y, circle_z, radius, cx, cy, cz, cx + child_size, cy + child_size, cz + child_size, &dist2);
                        bool further = intersect_sphere_aabb3d(circle_x, circle_y, circle_z, further_radius, cx, cy, cz, cx + child_size, cy + child_size, cz + child_size);

                        ChunkKey leaf;
                        leaf = parent.leaf_data;
                        leaf.lod += 1;
                        leaf.x = (leaf.x << 1) | x;
                        leaf.y = (leaf.y << 1) | y;
                        leaf.z = (leaf.z << 1) | z;

                        if (!further && !closer && parent.depth >= min_depth)
                        {
                            m_leaves_curr.push_back(leaf.raw);
                            continue;
                        }

                        float target_depth;
                        if (closer)
                        {
                            target_depth = (float)max_depth;
                        }
                        else
                        {
                            float max_dist = further_radius - radius;
                            float current_dist = sqrtf(dist2);
                            float relative_dist = (current_dist - radius) / (further_radius - radius);

                            float log_t = glm::log2(1.0f + relative_dist * intensity) / glm::log2(1.0f + intensity);
                            float t = 1.f - log_t;

                            target_depth = t * (float)max_depth;
                        }



                        if (parent.depth < target_depth || parent.depth < min_depth)
                        {

                            has_children = true;
                            StackItem child_item;
                            child_item.depth = parent.depth + 1;
                            child_item.size = child_size;
                            child_item.px = cx;
                            child_item.py = cy;
                            child_item.pz = cz;
                            child_item.leaf_data = leaf;
                            child_item.parent_spatial_idx = x + 2 * y + 4 * z;
                            child_item.parent_idx = node_idx;
                            stack.push(child_item);
                        }
                        else
                        {
                            m_leaves_curr.push_back(leaf.raw);
                        }
                    }

            if (has_children && parent.parent_idx != invalid_id)
            {
                nodes[parent.parent_idx].is_leaf = false;
                nodes[parent.parent_idx].children[parent.parent_spatial_idx] = node_idx;
            }

        }
        
        compute_leaves_delta();
    }

    int for_each_node(bool include_leaves, std::function<void(ChunkKey)> fn_for_each)
    {

        struct StackItem
        {
            uint64_t index;
            float px, py, pz, s;
            ChunkKeyRaw leaf;
        };

        std::stack<StackItem> stack;
        stack.push({ 0, origin_x, origin_y, origin_z, world_scale, glm::ivec4(0) });
        int drawCount = 0;
        ChunkKey root_leaf;
        root_leaf.raw = glm::ivec4(0);
        fn_for_each(root_leaf);
        while (!stack.empty())
        {
            StackItem item = stack.top();
            stack.pop();
            if (item.index == -1)
                continue;

            if (!include_leaves)
                fn_for_each({ item.leaf });
            float cs = item.s * 0.5f;
            for (int z = 0; z < 2; z++)
                for (int y = 0; y < 2; y++)
                    for (int x = 0; x < 2; x++)
                    {
                        float cx = item.px + cs * x;
                        float cy = item.py + cs * y;
                        float cz = item.pz + cs * z;

                        ChunkKey leaf;
                        leaf.raw = item.leaf;
                        leaf.lod += 1;
                        leaf.x = (leaf.x << 1) | x;
                        leaf.y = (leaf.y << 1) | y;
                        leaf.z = (leaf.z << 1) | z;

                        if (include_leaves)
                        {
                            fn_for_each(leaf);
                        }
                        drawCount++;
                        uint64_t child_index = nodes[item.index].children[x + 2 * y + 4 * z];

                        stack.push({ child_index, cx, cy, cz, cs, leaf.raw });
                    }
        }
        return drawCount;
    }

    template <typename Func>
    void for_each_chunk_created(Func&& fn_for_each)
    {
        for (ChunkKey leaf : m_leaves_created)
            fn_for_each(leaf);
    }

    template <typename Func>
    void for_each_chunk_created(Func&& fn_for_each, LeavesVector::iterator& it, int iteration_count)
    {
        while (it != m_leaves_created.end() && iteration_count > 0)
        {
            ChunkKey leaf = *it;
            fn_for_each(leaf);
            --iteration_count;
            ++it;
        }
    }

    template <typename Func>
    void for_each_chunk(Func&& fn_for_each)
    {
        for (ChunkKey leaf : m_leaves_curr)
            fn_for_each(leaf);
    }

    template <typename Func>
    void for_each_chunk_removed(Func&& fn_for_each)
    {
        for (ChunkKey leaf : m_leaves_removed)
        {
            fn_for_each(leaf);
        }
    }
private:

    void compute_leaves_delta()
    {
        std::sort(m_leaves_curr.begin(), m_leaves_curr.end());

        m_leaves_created.clear();
        m_leaves_removed.clear();

        auto it_curr = m_leaves_curr.begin();
        auto it_prev = m_leaves_prev.begin();

        while (it_prev != m_leaves_prev.end() && it_curr != m_leaves_curr.end())
        {
            if (*it_curr == *it_prev)
            {
                ++it_curr;
                ++it_prev;
            }
            else if (*it_curr < *it_prev)
            {
                m_leaves_created.push_back(*it_curr);
                ++it_curr;
            }
            else
            {
                m_leaves_removed.push_back(*it_prev);
                ++it_prev;
            }
        }

        while (it_curr != m_leaves_curr.end())
        {
            m_leaves_created.push_back(*it_curr);
            ++it_curr;
        }

        while (it_prev != m_leaves_prev.end())
        {
            m_leaves_removed.push_back(*it_prev);
            ++it_prev;
        }

    }
    LeavesVector m_leaves_created;
    LeavesVector m_leaves_removed;

    LeavesVector m_leaves_curr;
    LeavesVector m_leaves_prev;

};
