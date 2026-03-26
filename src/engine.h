#pragma once
#include <glm/glm.hpp>

typedef struct Renderer
{
    glm::ivec2 viewport;
    glm::vec4 clear_color;
    uint32_t framebuffer;
} Renderer;

void renderer_set_viewport(Renderer* renderer, glm::ivec2 new_viewport)
{
    renderer->viewport = new_viewport;
}

void renderer_set_clear_color(Renderer* renderer, glm::vec4 new_clear_color)
{
    renderer->clear_color = new_clear_color;
}

typedef int(*pfn_update)(void*);
typedef int(*pfn_render)(Renderer*, void*);

typedef struct Engine
{
    Renderer renderer;
    bool is_running;
    pfn_update pfn_update_frame;
    pfn_render pfn_render_frame;
    GLFWwindow* window;
    void* user_data;
    double delta_time;
} Engine;

int engine_init(Engine* engine, pfn_update pfn_frame_update, pfn_render pfn_frame_render, void* user_data, int width, int height)
{
    engine->pfn_update_frame = pfn_frame_update;
    engine->pfn_render_frame = pfn_frame_render;
    engine->user_data = user_data;
    engine->is_running = true;
    if (!glfwInit())
        return 1;

    renderer_set_viewport(&engine->renderer, { width, height });
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

    engine->window = glfwCreateWindow(width, height, "voxel engine", NULL, NULL);
    if (!engine->window)
    {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(engine->window);

    if (!gladLoadGL(glfwGetProcAddress))
    {
        printf("Failed to initialize GLAD\n");
        return 1;
    }
    return 0;
}

int engine_run(Engine* engine)
{
    double last_time = glfwGetTime();
    while (engine->is_running)
    {
        double current_time = glfwGetTime();
        engine->delta_time = current_time - last_time;

        glfwPollEvents();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, engine->renderer.viewport.x, engine->renderer.viewport.y);

        if (glfwWindowShouldClose(engine->window)) {
            engine->is_running = false;
            break;
        }

        int result = engine->pfn_update_frame(engine->user_data);
        if (result != 0)
        {
            engine->is_running = false;
            return result;
        }
        result = engine->pfn_render_frame(&engine->renderer, engine->user_data);
        if (result != 0)
        {
            engine->is_running = false;
            return result;
        }
        if (glfwWindowShouldClose(engine->window))
            break;

        glfwSwapBuffers(engine->window);
        last_time = current_time;
    }
    return 0;
}

void engine_shutdown(Engine* engine)
{
    glfwTerminate();
}
