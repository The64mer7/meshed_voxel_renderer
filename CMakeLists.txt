cmake_minimum_required(VERSION 3.25)

set(PROJECT_NAME_VAR meshed_voxel_renderer)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.h" "src/*.hpp")

file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS "shaders/*.frag" "shaders/*.vert" "shaders/*.comp")


project(${PROJECT_NAME_VAR} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)

add_subdirectory(dependencies/glfw)

include(FetchContent)
FetchContent_Declare(
    glm 
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW true
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
    fastnoise2
    GIT_REPOSITORY https://github.com/Auburn/FastNoise2.git
    GIT_TAG v1.1.1
    GIT_SHALLOW true
)
FetchContent_MakeAvailable(fastnoise2)

#[[
# --- ImGui FetchContent (Modern v1.90+) ---
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.4  # Latest stable
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_DIR ${imgui_SOURCE_DIR})

# Define the source files (Core + Backends)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})

# FIX: Tell ImGui exactly how to find Glad 2
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
target_compile_definitions(imgui PUBLIC "IMGUI_IMPL_OPENGL_LOADER_CUSTOM=\"glad/gl.h\"")

# Include paths for the imgui library itself
target_include_directories(imgui PUBLIC 
    ${IMGUI_DIR} 
    ${IMGUI_DIR}/backends
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencies" # Folder containing 'glad/gl.h'
)

# Link GLFW to the imgui library so the backend can find glfw3.h
target_link_libraries(imgui PRIVATE glfw)
]]

add_executable(${PROJECT_NAME_VAR} ${SOURCES} ${SHADER_SOURCES})

set(SHADER_DEST_DIR $<TARGET_FILE_DIR:${PROJECT_NAME_VAR}>/shaders)
add_custom_command(
    TARGET ${PROJECT_NAME_VAR} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DEST_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            ${SHADER_DEST_DIR}
    COMMENT "Copying shaders to executable directory..."
)


target_include_directories(${PROJECT_NAME_VAR} PRIVATE 
    dependencies
    dependencies/glfw/include
    #${IMGUI_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME_VAR} PRIVATE 
glfw
OpenGL::GL
${CMAKE_DL_LIBS}
glm::glm
FastNoise
#imgui
)