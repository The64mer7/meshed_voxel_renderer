#pragma once
#include <glm/glm.hpp>

typedef struct Renderer
{
    glm::ivec2 viewport;
    glm::vec4 clear_color;
    uint32_t framebuffer;
    uint32_t screen_texture;
    uint32_t depth_texture;
} Renderer;


void renderer_set_viewport(Renderer* renderer, glm::ivec2 new_viewport)
{
    renderer->viewport = new_viewport;
}

void renderer_set_clear_color(Renderer* renderer, glm::vec4 new_clear_color)
{
    renderer->clear_color = new_clear_color;
}

void renderer_create(Renderer* renderer)
{
    glCreateFramebuffers(1, &renderer->framebuffer);

    glCreateTextures(GL_TEXTURE_2D, 1, &renderer->screen_texture);
    glCreateTextures(GL_TEXTURE_2D, 1, &renderer->depth_texture);

    glTextureStorage2D(renderer->screen_texture, 1, GL_RGBA8, renderer->viewport.x, renderer->viewport.y);
    glTextureStorage2D(renderer->depth_texture, 1, GL_DEPTH_COMPONENT32F, renderer->viewport.x, renderer->viewport.y);

    glTextureParameteri(renderer->depth_texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(renderer->depth_texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(renderer->depth_texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(renderer->depth_texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTextureParameteri(renderer->screen_texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(renderer->screen_texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(renderer->screen_texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(renderer->screen_texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glNamedFramebufferTexture(renderer->framebuffer, GL_COLOR_ATTACHMENT0, renderer->screen_texture, 0);
    glNamedFramebufferTexture(renderer->framebuffer, GL_DEPTH_ATTACHMENT, renderer->depth_texture, 0);

    if (glCheckNamedFramebufferStatus(renderer->framebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        printf("error: framebuffer not complete!\n");
    }

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

    renderer_set_viewport(&engine->renderer, { width, height });
    renderer_create(&engine->renderer);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDepthFunc(GL_GREATER);
    glClearDepth(0.f);
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

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

        

        int result = engine->pfn_update_frame(engine->user_data);
        if (result != 0)
        {
            engine->is_running = false;
            return result;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, engine->renderer.framebuffer);
        glViewport(0, 0, engine->renderer.viewport.x, engine->renderer.viewport.y);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        result = engine->pfn_render_frame(&engine->renderer, engine->user_data);
        if (result != 0)
        {
            engine->is_running = false;
            return result;
        }
        if (glfwWindowShouldClose(engine->window))
            break;

        glBlitNamedFramebuffer(engine->renderer.framebuffer, 0,
            0, 0, 
            engine->renderer.viewport.x, engine->renderer.viewport.y, 
            0, 0, 
            engine->renderer.viewport.x, engine->renderer.viewport.y, 
            GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glfwSwapBuffers(engine->window);
        last_time = current_time;
    }
    return 0;
}

void engine_shutdown(Engine* engine)
{
    glfwTerminate();
}
