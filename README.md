# meshed_voxel_renderer

Multithreaded voxel engine renderer made in c++ and OpenGL with massive scale world edits

To build this project:

cmake -B build/release -DCMAKE_BUILD_TYPE=Release -G Ninja

cmake --build build/release

Controls:

RMB Hold - Rotate camera

WASD - Camera movement

Space - Move camera up

Left Ctrl - Move camera down

F - increase camera speed

R - decrease camera speed

LMB - Place structure at cursor position
