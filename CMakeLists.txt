cmake_minimum_required(VERSION 3.25)

# Add RelWithDebInfo if not already present
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel" CACHE STRING "" FORCE)
set(PROJECT_NAME_VAR meshed_voxel_renderer)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.h" "src/*.hpp")

file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS "shaders/*.frag" "shaders/*.vert" "shaders/*.comp")
file(GLOB_RECURSE RESOURCES_SOURCES CONFIGURE_DEPENDS "resources/*")


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

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    SOURCE_SUBDIR  "MADE_UP_DIR"
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(
  xsimd
  GIT_REPOSITORY https://github.com/xtensor-stack/xsimd.git
  GIT_TAG        13.0.0
)

FetchContent_MakeAvailable(xsimd)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${stb_SOURCE_DIR})

add_executable(${PROJECT_NAME_VAR} ${SOURCES} ${SHADER_SOURCES})

if(MSVC)
    add_compile_options(/arch:AVX2) # For Visual Studio
else()
    add_compile_options(-msse4.1 -msse4.2 -mavx) # For GCC/Clang
endif()

# 1. Define the destination
set(SHADER_DEST_DIR $<TARGET_FILE_DIR:${PROJECT_NAME_VAR}>/shaders)
set(RESOURCES_DEST_DIR $<TARGET_FILE_DIR:${PROJECT_NAME_VAR}>/resources)

# 2. Create a custom target for shaders
add_custom_target(update_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DEST_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders" 
            ${SHADER_DEST_DIR}
    SOURCES ${SHADER_SOURCES}
    COMMENT "Checking/Copying shaders..."
)

add_custom_target(update_resources ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RESOURCES_DEST_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
            "${CMAKE_CURRENT_SOURCE_DIR}/resources" 
            ${RESOURCES_DEST_DIR}
    SOURCES ${RESOURCES_SOURCES}
    COMMENT "Checking/Copying resources..."
)

# 3. Make the main project depend on the shader target
add_dependencies(${PROJECT_NAME_VAR} update_shaders)
add_dependencies(${PROJECT_NAME_VAR} update_resources)

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
stb_image
xsimd
#imgui
)

if(MSVC)
    target_compile_options(${PROJECT_NAME_VAR} PRIVATE /arch:AVX2) 
else()
    target_compile_options(${PROJECT_NAME_VAR} PRIVATE -march=native)
endif()