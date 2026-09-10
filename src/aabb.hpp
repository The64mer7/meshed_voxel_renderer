#pragma once
#include <glm/glm.hpp>
struct aabb3d
{
    glm::vec3 min;
    glm::vec3 max;

    inline bool fully_contains(const aabb3d& aabb) const
    {
        if (aabb.min.x < min.x || aabb.max.x > max.x)
            return false;
        if (aabb.min.y < min.y || aabb.max.y > max.y)
            return false;
        if (aabb.min.z < min.z || aabb.max.z > max.z)
            return false;
        return true;
    }

    inline bool is_subset_of(const aabb3d& aabb) const { return aabb.fully_contains(*this); }

    inline bool intersects(const aabb3d& aabb) const
    {
        if (aabb.max.x < min.x || max.x < aabb.min.x)
            return false;
        if (aabb.max.y < min.y || max.y < aabb.min.y)
            return false;
        if (aabb.max.z < min.z || max.z < aabb.min.z)
            return false;
        return true;
    }

    inline bool intersects_triangle(const glm::vec3& v0, const glm::vec3& v1,
                                    const glm::vec3& v2) const
    {
        glm::vec3 c = (min + max) * 0.5f;
        glm::vec3 e = (max - min) * 0.5f;

        glm::vec3 v0l = v0 - c;
        glm::vec3 v1l = v1 - c;
        glm::vec3 v2l = v2 - c;

        glm::vec3 f0 = v1l - v0l;
        glm::vec3 f1 = v2l - v1l;
        glm::vec3 f2 = v0l - v2l;

        auto axis_test = [&](glm::vec3 axis) -> bool
        {
            float len2 = glm::dot(axis, axis);
            if (len2 < 1e-12f)
                return true;

            axis = glm::normalize(axis);

            auto proj = [&](const glm::vec3& v) { return glm::dot(v, axis); };

            float p0 = proj(v0l);
            float p1 = proj(v1l);
            float p2 = proj(v2l);

            float r = e.x * std::abs(axis.x) + e.y * std::abs(axis.y) + e.z * std::abs(axis.z);

            float min_p = std::min({p0, p1, p2});
            float max_p = std::max({p0, p1, p2});

            return !(min_p > r || max_p < -r);
        };

        if (!axis_test({1, 0, 0}))
            return false;
        if (!axis_test({0, 1, 0}))
            return false;
        if (!axis_test({0, 0, 1}))
            return false;

        glm::vec3 tri_normal = glm::cross(f0, f1);
        if (!axis_test(tri_normal))
            return false;

        glm::vec3 axes[9] = {
            glm::cross(f0, {1, 0, 0}), glm::cross(f0, {0, 1, 0}), glm::cross(f0, {0, 0, 1}),
            glm::cross(f1, {1, 0, 0}), glm::cross(f1, {0, 1, 0}), glm::cross(f1, {0, 0, 1}),
            glm::cross(f2, {1, 0, 0}), glm::cross(f2, {0, 1, 0}), glm::cross(f2, {0, 0, 1}),
        };

        for (auto& a : axes)
            if (!axis_test(a))
                return false;

        return true;
    }

    inline bool contains_point(const glm::vec3& p) const
    {
        for (int i = 0; i < 3; i++)
        {
            if (p[i] < min[i] || p[i] > max[i])
                return false;
        }
        return true;
    }

    inline glm::vec3 center() const { return (min + max) * 0.5f; }

    inline glm::vec3 size() const { return max - min; }

    void extend(glm::vec3 p)
    {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }

    aabb3d octree_child(uint32_t index) const
    {
        aabb3d child;
        glm::vec3 child_size = size() * 0.5f;
        child.min.x = min.x + child_size.x * ((index >> 0) & 1);
        child.min.y = min.y + child_size.y * ((index >> 1) & 1);
        child.min.z = min.z + child_size.z * ((index >> 2) & 1);
        child.max.x = child.min.x + child_size.x;
        child.max.y = child.min.y + child_size.y;
        child.max.z = child.min.z + child_size.z;

        return child;
    }
};

static bool intersect_sphere_aabb3d(float sX, float sY, float sZ, float radius, float minX,
                                    float minY, float minZ, float maxX, float maxY, float maxZ,
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
