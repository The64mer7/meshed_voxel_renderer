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

void APIENTRY OpenGLDebugCallback(GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar* message,
    const void* userParam)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
        return;

    printf("[OpenGL Debug] source=%u type=%u id=%u severity=%u\n%s\n",
        source, type, id, severity, message);

    if (severity == GL_DEBUG_SEVERITY_HIGH)
        __debugbreak();
}

int engine_init(Engine* engine,
    pfn_update pfn_frame_update,
    pfn_render pfn_frame_render,
    void* user_data,
    int width,
    int height)
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
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

    // Create GLFW window
    engine->window = glfwCreateWindow(width, height, "voxel engine", NULL, NULL);
    if (!engine->window)
    {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(engine->window);

    // Load OpenGL functions with GLAD
    if (!gladLoadGL(glfwGetProcAddress))
    {
        printf("Failed to initialize GLAD\n");
        return 1;
    }

    if (GLAD_GL_VERSION_4_3)
    {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

        glDebugMessageCallback(
            OpenGLDebugCallback,
            nullptr
        );

        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE,
            0, nullptr, GL_TRUE);
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

        if (glfwWindowShouldClose(engine->window)) {
            engine->is_running = false;
            break;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, engine->renderer.viewport.x, engine->renderer.viewport.y);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
