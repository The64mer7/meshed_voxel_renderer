#pragma once
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class Renderer
{
public:
    glm::ivec2 viewport;
    glm::vec4 clear_color;
    uint32_t framebuffer;
    uint32_t screen_texture;
    uint32_t depth_texture;
};

struct DrawArraysIndirectCommand
{
    uint32_t count = 0u;
    uint32_t instanceCount = 1u;
    uint32_t first = 0u;
    uint32_t baseInstance = 0u;
};


void renderer_set_viewport(Renderer* renderer, glm::ivec2 new_viewport);

void renderer_set_clear_color(Renderer* renderer, glm::vec4 new_clear_color);

void renderer_create(Renderer* renderer);