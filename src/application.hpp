#pragma once
#include "engine.h"

#include "world.hpp"

class Application
{
public:
    Application(int width, int height);
    ~Application();

    void init();
    void run();
    void cleanup();

    int width, height;
    Engine engine;
    ThreadPool thread_pool;
    InputState input;
    
    World world;
    OctreeClipmapGenerateSettings clipmap_settings;

    Entity player;
    FirstPersonCamera camera;
    glm::ivec3 camera_chunk_coord = { 32,128,32 };
    float camera_chunk_size = 8.f;
    float camera_speed = 1.f;

    uint32_t texture_atlas;
    uint32_t dummy_vao;

    uint64_t frame_index = 0;

    struct DisplaySettings
    {
        bool chunk_aabb = false;
    } display_settings;

private:

    void handle_input();

    int frame_update();
    int frame_render();
    int frame_render_ui();

    static int update_trampoline(void* user_data);
    static int render_trampoline(void* user_data);
    static int render_ui_trampoline(void* user_data);
};
