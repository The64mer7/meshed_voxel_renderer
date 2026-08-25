#include "application.hpp"

Application::Application(int width, int height)
    : width(width), height(height) {}

Application::~Application() {}

void Application::init()
{
    engine.init(update_trampoline, render_trampoline, render_ui_trampoline, this, width, height);
    thread_pool.init(std::thread::hardware_concurrency(), MB(4));
    glfwSwapInterval(0);

    player.position = glm::vec3(0);
    player.direction = glm::vec3(0);
    player.velocity = glm::vec3(0);

    glCreateVertexArrays(1, &dummy_vao);
    {
        int w, h, ch;
        stbi_set_flip_vertically_on_load(true);
        void* atlas_data = stbi_load("resources/textures/atlas.png", &w, &h, &ch, 4);
        if (!atlas_data)
            return;

        glCreateTextures(GL_TEXTURE_2D, 1, &texture_atlas);
        glTextureStorage2D(texture_atlas, 1, GL_RGBA8, 256, 256);
        glTextureParameteri(texture_atlas, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(texture_atlas, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(texture_atlas, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(texture_atlas, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTextureSubImage2D(texture_atlas, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, atlas_data);

        stbi_image_free(atlas_data);
    }

    WorldData world_data;
    world_data.update_distance = 2.f;
    world_data.voxels_per_chunk_axis = 62;
    world_data.world_size_exp = 23;
    world_data.world_vram = GB(2);
    world_data.thread_pool = &thread_pool;


    clipmap_settings.radius = 0;
    clipmap_settings.further_radius = 1024*64;
    clipmap_settings.min_depth = 4;
    clipmap_settings.max_depth = 19;
    clipmap_settings.chunks_per_lod = 3;

    world.create(world_data, clipmap_settings);

    {
        camera_settings.position = { 0,0,0 };
        camera_settings.direction = { 0,0,-1 };
        camera_settings.nearPlane = world_data.world_size() * 2;
        camera_settings.farPlane = 0.125f;
        camera_settings.fov = 45.f;
        camera_settings.width = width;
        camera_settings.height = height;
        camera.Init(camera_settings);
    }
}

void Application::run()
{
    engine.run();
}

void Application::cleanup()
{
    thread_pool.shutdown();
    engine.shutdown();
}

glm::vec3 Application::camera_world_pos() const
{
    return glm::vec3(camera_chunk_coord) * camera_chunk_size + camera.GetPosition();
}

void Application::handle_input()
{
    input.update(engine.window);

    float speed = camera_speed * engine.delta_time;

    if (input.get_key(GLFW_KEY_W))
        camera.Translate(speed * camera.GetForwardVector());
    if (input.get_key(GLFW_KEY_S))
        camera.Translate(-speed * camera.GetForwardVector());
    if (input.get_key(GLFW_KEY_A))
        camera.Translate(speed * glm::normalize(glm::cross(WorldDirection::Up, camera.GetForwardVector())));
    if (input.get_key(GLFW_KEY_D))
        camera.Translate(-speed * glm::normalize(glm::cross(WorldDirection::Up, camera.GetForwardVector())));


    if (input.get_key(GLFW_KEY_Z))
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    if (input.get_key(GLFW_KEY_Y))
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    if (input.get_key(GLFW_KEY_SPACE))
        camera.Translate(speed * WorldDirection::Up * 1.0f);
    if (input.get_key(GLFW_KEY_LEFT_CONTROL))
        camera.Translate(-speed * WorldDirection::Up);

    if (input.get_key(GLFW_KEY_F))
        camera_speed *= glm::pow(8.0, engine.delta_time);
    if (input.get_key(GLFW_KEY_R))
    {
        camera_speed /= glm::pow(8.0, engine.delta_time);
        camera_speed = glm::max(camera_speed, 0.01f);
    }

    float sensitivity = 0.01f;

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse)
    {
        if (input.get_button(GLFW_MOUSE_BUTTON_RIGHT))
            camera.Rotate(sensitivity * input.get_mouse_dx(), -sensitivity * input.get_mouse_dy());
        if (input.is_button_clicked(GLFW_MOUSE_BUTTON_LEFT))
        {
            float depth = 0.0f;

            glBindFramebuffer(GL_READ_FRAMEBUFFER, engine.renderer.framebuffer);

            double xmouse, ymouse;
            glfwGetCursorPos(engine.window, &xmouse, &ymouse);
            glReadPixels(
                glm::min(static_cast<int>(xmouse), engine.renderer.viewport.x),
                glm::min(engine.renderer.viewport.y - static_cast<int>(ymouse), engine.renderer.viewport.y),
                1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
            float x_ndc = (2.0f * xmouse) / engine.renderer.viewport.x - 1.0f;
            float y_ndc = 1.f - (2.0f * ymouse) / engine.renderer.viewport.y;

            glm::vec4 ray_clip = glm::vec4(x_ndc, y_ndc, 1.0f, 1.0f);

            glm::vec4 ray_view = glm::inverse(camera.GetProjectionMatrix()) * ray_clip;
            ray_view = glm::vec4(ray_view.x, ray_view.y, -1.0f, 0.0f);

            glm::vec3 ray_world = glm::vec3(glm::inverse(camera.GetViewMatrix()) * ray_view);
            ray_world = glm::normalize(ray_world);

            float n = world.get_data().world_size() * 4.0f;
            float f = 0.125f;

            float distance = (n * f) / (depth * (n - f) + f);


            SphereStructure* structure = new SphereStructure();
            structure->radius = sphere_radius;

            structure->position = camera_world_pos() + ray_world * distance;



            structure_id handle = world.create_structure(structure);
            world.place_structure(handle);
        }
    }

    if (glm::any(glm::greaterThanEqual(glm::abs(camera.GetPosition()), glm::vec3(camera_chunk_size))))
    {
        glm::ivec3 chunk_offset = (camera.GetPosition() / camera_chunk_size);
        camera_chunk_coord += chunk_offset;
        camera.Translate(-glm::vec3(chunk_offset) * camera_chunk_size);
    }
}

int Application::frame_update()
{
    handle_input();
    
    world.update(camera.GetPosition() + glm::vec3(camera_chunk_coord) * camera_chunk_size);

    frame_index++;
    return 0;
}

int Application::frame_render()
{
    glClearColor(0.7f, 0.7f, 0.9f, 1.f);

    world.render({ 0.f,0.f,0.f }, camera, camera_chunk_coord, camera_chunk_size);

    return 0;
}

int Application::frame_render_ui()
{
    ImGui::Begin("Debug");

    static bool display_allocator = false;
    ImGui::Checkbox("display_allocator", &display_allocator);
    if (display_allocator)
    {
        std::string string = "";
        world.get_memory_allocator().debug_log(string, MB(4));
        ImGui::TextWrapped(string.c_str());
    }

    static bool display_metrics = true;
    ImGui::Checkbox("display_metrics", &display_metrics);
    if (display_metrics)
    {
        ImGui::Text("chunks_allocated: %u", world.get_chunks_allocated());
        ImGui::Text("nodes_created: %u", world.get_tree_node_size());
        ImGui::SliderFloat("sphere_radius: %f", &sphere_radius, 1.f, 1024 * 1024);
        glm::vec3 cam_rel = camera.GetPosition();
        glm::vec3 cam_world = cam_rel + glm::vec3(camera_chunk_coord) * camera_chunk_size;
        ImGui::Text("dt: %fms", 1000 * engine.delta_time);
        ImGui::Text("camera_position: [%f, %f, %f]", cam_world.x, cam_world.y, cam_world.z);
        ImGui::Text("camera_relative_position: [%f, %f, %f]", cam_rel.x, cam_rel.y, cam_rel.z);
        ImGui::Text("camera_chunk_coord: [%u, %u, %u]", camera_chunk_coord.x, camera_chunk_coord.y, camera_chunk_coord.z);
        ImGui::Text("camera_speed: %f u/s", camera_speed);
        ImGui::Separator();

        {
            int chunks_per_lod = clipmap_settings.chunks_per_lod;
            int min_depth = clipmap_settings.min_depth;
            int max_depth = clipmap_settings.max_depth;
            bool update = false;
            update = update || ImGui::SliderInt("chunks_per_lod", &chunks_per_lod, 0, 8);
            update = update || ImGui::SliderInt("min_depth", &min_depth, 0, clipmap_settings.max_depth);
            update = update || ImGui::SliderInt("max_depth", &max_depth, 0, 25);
            update = update || ImGui::SliderFloat("further_radius", &clipmap_settings.further_radius, 0, 1024 * 1024);
            update = update || ImGui::SliderFloat("radius", &clipmap_settings.radius, 0, clipmap_settings.further_radius);
            clipmap_settings.chunks_per_lod = chunks_per_lod;
            clipmap_settings.min_depth = min_depth;
            clipmap_settings.max_depth = max_depth;
            ImGui::Text("average_chunk_meshing_time %fms", float(g_generator_time_sum / g_generator_count));
            if (update)
                world.update_settings(clipmap_settings);
        }
        ImGui::Separator();
        ImGui::Text("tasks_remaining: %u", thread_pool.get_task_count());
        
        world.debug_ui();

        bool naive = g_mesh_naive._Storage._Value;
        if (ImGui::Button(naive ? "set greedy" : "set naive", ImVec2{64,24}))
        {
            g_mesh_naive.store(!naive);
            world.regenerate_chunks(camera.GetPosition() + glm::vec3(camera_chunk_coord) * camera_chunk_size);
        }
    }

    ImGui::End();
    return 0;
}

int Application::update_trampoline(void* user_data)
{
    return reinterpret_cast<Application*>(user_data)->frame_update();
}

int Application::render_trampoline(void* user_data)
{
    return reinterpret_cast<Application*>(user_data)->frame_render();
}

int Application::render_ui_trampoline(void* user_data)
{
    return reinterpret_cast<Application*>(user_data)->frame_render_ui();
}
