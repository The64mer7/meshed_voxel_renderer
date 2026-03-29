#pragma once
#include <stdio.h>
#include <stack>
#include <unordered_set>
#include <functional>
#include "tree_node.h"
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

bool IntersectSphereAABB3D(
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

typedef glm::ivec4 packed_leaf3d_raw_t;

union packed_leaf3d_t
{
    packed_leaf3d_raw_t packed;
    struct
    {
        int32_t x :     32;
        int32_t y :     32;
        int32_t z :     32;
        int32_t lod :   32;
    };
};

inline bool operator==(const packed_leaf3d_t& lhs, const packed_leaf3d_t& rhs) {
    return lhs.packed == rhs.packed;
}

class Octree
{
    std::unordered_set<packed_leaf3d_raw_t> m_curr_leaves;
    std::unordered_set<packed_leaf3d_raw_t> m_prev_leaves;


public:

    using OctreeNode = TreeNode<8, int>;
    OctreeNode root;

    float px = 0.f, py = 0.f, pz = 0.f, s = 1024.f;

    void Generate(uint64_t min_depth, uint64_t max_depth, float circle_x, float circle_y, float circle_z, float radius, float further_radius, float intensity)
    {
        root.Clear();
        m_prev_leaves = std::move(m_curr_leaves);
        m_curr_leaves.clear();

        struct StackItem
        {
            OctreeNode* node;
            packed_leaf3d_t leaf_data;
            float px, py, pz, size;
            float depth;
        };
        std::stack<StackItem> stack;
        StackItem root_item = { &root, glm::ivec4(0), px, py, pz, s, 0 };
        stack.push(root_item);

        while (!stack.empty())
        {
            StackItem parent = stack.top();
            stack.pop();

            if (!parent.node || parent.depth >= max_depth)
            {
                m_curr_leaves.insert(parent.leaf_data.packed);
                continue;
            }

            float child_size = parent.size * 0.5f;
            for (int z = 0; z < 2; z++)
                for (int y = 0; y < 2; y++)
                    for (int x = 0; x < 2; x++)
                    {
                        float cx = parent.px + child_size * x;
                        float cy = parent.py + child_size * y;
                        float cz = parent.pz + child_size * z;
                        float dist2;
                        bool closer = IntersectSphereAABB3D(circle_x, circle_y, circle_z, radius, cx, cy, cz, cx + child_size, cy + child_size, cz + child_size, &dist2);
                        bool further = IntersectSphereAABB3D(circle_x, circle_y, circle_z, further_radius, cx, cy, cz, cx + child_size, cy + child_size, cz + child_size);

                        packed_leaf3d_t leaf;
                        leaf = parent.leaf_data;
                        leaf.lod += 1;
                        leaf.x = (leaf.x << 1) | x;
                        leaf.y = (leaf.y << 1) | y;
                        leaf.z = (leaf.z << 1) | z;

                        if (!further && !closer && parent.depth >= min_depth)
                        {
                            m_curr_leaves.insert(leaf.packed);
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

                            float log_t = std::log(1.0f + relative_dist * intensity) / std::log(1.0f + intensity);

                            float t = 1.f - log_t;

                            target_depth = t * (float)max_depth;
                        }



                        if (parent.depth < target_depth || parent.depth < min_depth)
                        {
                            OctreeNode*& child = parent.node->children[x + 2 * y + 4 * z];
                            if (child) delete child;
                            child = new OctreeNode;

                            stack.push({ child, leaf, cx, cy, cz, child_size, parent.depth + 1 });
                        }
                        else
                            m_curr_leaves.insert(leaf.packed);
                    }
        }
#if 1
        uint64_t unload_count = 0;
        uint64_t load_count = 0;
        for (packed_leaf3d_raw_t leaf_raw : m_prev_leaves)
        {
            packed_leaf3d_t leaf = { leaf_raw };
            if (m_curr_leaves.find(leaf_raw) == m_curr_leaves.end())
                unload_count++;
            //printf("unload %i, %i, lod: %i\n", leaf.x, leaf.y, leaf.lod);
        }
        for (packed_leaf3d_raw_t leaf_raw : m_curr_leaves)
        {
            packed_leaf3d_t leaf = { leaf_raw };
            if (m_prev_leaves.find(leaf_raw) == m_prev_leaves.end())
                load_count++;
            //printf("load %i, %i, lod: %i\n", leaf.x, leaf.y, leaf.lod);
        }
        //printf("load: %i, unload: %i\n", load_count, unload_count);
#endif


    }

    int ForEachNode(bool include_leafs, std::function<void(packed_leaf3d_t)> fn_for_each)
    {

        struct StackItem
        {
            OctreeNode* node;
            float px, py, pz, s;
            packed_leaf3d_raw_t leaf;
        };

        std::stack<StackItem> stack;
        stack.push({ &root, px, py, pz, s, glm::ivec4(0) });
        int drawCount = 0;
        packed_leaf3d_t root_leaf;
        root_leaf.packed = glm::ivec4(0);
        fn_for_each(root_leaf);
        while (!stack.empty())
        {
            StackItem item = stack.top();
            stack.pop();
            OctreeNode* node = item.node;

            if (!node)
                continue;
            if (!include_leafs)
                fn_for_each({ item.leaf });
            float cs = item.s * 0.5f;
            for (int z = 0; z < 2; z++)
                for (int y = 0; y < 2; y++)
                    for (int x = 0; x < 2; x++)
                    {
                        float cx = item.px + cs * x;
                        float cy = item.py + cs * y;
                        float cz = item.pz + cs * z;

                        packed_leaf3d_t leaf;
                        leaf.packed = item.leaf;
                        leaf.lod += 1;
                        leaf.x = (leaf.x << 1) | x;
                        leaf.y = (leaf.y << 1) | y;
                        leaf.z = (leaf.z << 1) | z;

                        if (include_leafs)
                        {
                            fn_for_each(leaf);
                        }
                        drawCount++;
                        OctreeNode* child = node->children[x + 2 * y + 4 * z];
                        if (!child)
                            continue;

                        stack.push({ child, cx, cy, cz, cs, leaf.packed });
                    }
        }
        return drawCount;
    }


    void ForEachLeafAdded(std::function<void(packed_leaf3d_t)> fn_for_each)
    {
        for (packed_leaf3d_raw_t leaf_raw : m_curr_leaves)
        {
            packed_leaf3d_t leaf = { leaf_raw };
            if (m_prev_leaves.find(leaf_raw) == m_prev_leaves.end())
            {
                //float size = powf(0.5f, leaf.lod) * s;
                //float x = px + size * leaf.x;
                //float y = py + size * leaf.y;
                //float z = pz + size * leaf.z;
                fn_for_each(leaf);
            }
        }
    }

    void ForEachLeafRemoved(std::function<void(packed_leaf3d_t)> fn_for_each)
    {
        for (packed_leaf3d_raw_t leaf_raw : m_prev_leaves)
        {
            packed_leaf3d_t leaf = { leaf_raw };
            if (m_curr_leaves.find(leaf_raw) == m_curr_leaves.end())
            {
                //float size = powf(0.5f, leaf.lod) * s;
                //float x = px + size * leaf.x;
                //float y = py + size * leaf.y;
                //float z = pz + size * leaf.z;
                fn_for_each(leaf);
            }
        }
    }
};
