#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>

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