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

add_executable(${PROJECT_NAME_VAR} ${SOURCES} ${SHADER_SOURCES})

# 1. Define the destination
set(SHADER_DEST_DIR $<TARGET_FILE_DIR:${PROJECT_NAME_VAR}>/shaders)

# 2. Create a custom target for shaders
add_custom_target(update_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DEST_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders" 
            ${SHADER_DEST_DIR}
    SOURCES ${SHADER_SOURCES}
    COMMENT "Checking/Copying shaders..."
)

# 3. Make the main project depend on the shader target
add_dependencies(${PROJECT_NAME_VAR} update_shaders)


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