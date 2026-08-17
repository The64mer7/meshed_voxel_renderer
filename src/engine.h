#pragma once

#include <atomic>
#include <bit>
#include <bitset>
#include <condition_variable>
#include <format>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <stdio.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/vec_swizzle.hpp>

#include <FastNoise/FastNoise.h>
#include <stb_image.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "camera.hpp"
#include "buffer.hpp"
#include "allocator.hpp"
#include "shader.h"
#include "utils.hpp"
#include "thread_pool.hpp"
#include "renderer.hpp"
#include "input.hpp"

struct Entity
{
    glm::vec3 position;
    glm::vec3 direction;
    glm::vec3 velocity;
};

typedef int(*pfn_update)(void*);
typedef int(*pfn_render)(void*);

static void APIENTRY OpenGLDebugCallback(GLenum source,
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

class Engine
{
public:
    Renderer renderer;
    bool is_running;
    pfn_update pfn_update_frame;
    pfn_render pfn_render_frame;
    pfn_render pfn_render_ui;
    GLFWwindow* window;
    void* user_data;
    double delta_time;

    int init(
        pfn_update pfn_frame_update,
        pfn_render pfn_frame_render,
        pfn_render pfn_frame_render_ui,
        void* user_data,
        int width,
        int height)
    {
        this->pfn_update_frame = pfn_frame_update;
        this->pfn_render_frame = pfn_frame_render;
        this->pfn_render_ui = pfn_frame_render_ui;
        this->user_data = user_data;
        this->is_running = true;

        if (!glfwInit())
            return 1;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

        this->window = glfwCreateWindow(width, height, "voxel renderer", NULL, NULL);
        if (!this->window)
        {
            glfwTerminate();
            return 1;
        }

        glfwMakeContextCurrent(this->window);

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

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(this->window, true);
        ImGui_ImplOpenGL3_Init("#version 460 core");

        renderer_set_viewport(&this->renderer, { width, height });
        renderer_create(&this->renderer);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glDepthFunc(GL_GREATER);
        glClearDepth(0.f);
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

        return 0;
    }

    int run()
    {
        double last_time = glfwGetTime();
        while (is_running)
        {
            double current_time = glfwGetTime();
            delta_time = current_time - last_time;

            glfwPollEvents();

            if (glfwWindowShouldClose(window)) {
                is_running = false;
                break;
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            int result = pfn_update_frame(user_data);
            if (result != 0)
            {
                is_running = false;
                return result;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, renderer.framebuffer);
            glViewport(0, 0, renderer.viewport.x, renderer.viewport.y);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            result = pfn_render_frame(user_data);
            if (result != 0)
            {
                is_running = false;
                return result;
            }
            if (glfwWindowShouldClose(window))
                break;

            glBlitNamedFramebuffer(renderer.framebuffer, 0,
                0, 0,
                renderer.viewport.x, renderer.viewport.y,
                0, 0,
                renderer.viewport.x, renderer.viewport.y,
                GL_COLOR_BUFFER_BIT, GL_NEAREST);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            result = pfn_render_ui(user_data);
            if (result != 0)
            {
                is_running = false;
                return result;
            }

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window);
            last_time = current_time;
        }
        return 0;
    }

    void shutdown()
    {
        glfwTerminate();
    }
};
