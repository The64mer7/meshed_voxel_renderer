#version 460 core
#extension GL_ARB_gpu_shader_int64 : enable

struct greedy_face
{
    uint x;
    uint y;
    uint z;
    uint width;
    uint height;
    uint dir;
    uint custom;
};

#define bitmask(bits) ((1u << (bits)) - 1u)
greedy_face unpack_face(uint64_t packed_data)
{
    greedy_face face;
    face.x      = uint(packed_data >> 0)  & bitmask(6);
    face.y      = uint(packed_data >> 6)  & bitmask(6);
    face.z      = uint(packed_data >> 12) & bitmask(6);
    face.width  = (uint(packed_data >> 18) & bitmask(6)) + 1u;
    face.height = (uint(packed_data >> 24) & bitmask(6)) + 1u;
    face.dir    = uint(packed_data >> 30) & bitmask(3);
    face.custom = uint(packed_data >> 33) & bitmask(31);
    return face;
}

layout (std430, binding = 0) buffer vertex_buffer
{
	uint64_t faces[];
};

layout (std430, binding = 1) buffer aabbs_buffer
{
	ivec4 aabbs[];
};

vec4 triangle[3] = vec4[3](
    vec4(-0.5, -0.5, -1.0, 1.0), // Bottom Left
    vec4( 0.5, -0.5, -1.0, 1.0), // Bottom Right
    vec4( 0.0,  0.5, -1.0, 1.0)  // Top Center
);

uniform uint u_offset;
uniform uint u_render_triangle;
uniform uint u_render_cube;
uniform float u_world_size;
uniform vec3 u_world_origin;
uniform uint u_voxel_count;
uniform ivec3 u_camera_chunk_coord;
uniform float u_camera_chunk_size;

uniform mat4 u_view_matrix;
uniform mat4 u_proj_matrix;

out vec3 v_world_pos;

out flat vec3 vox_color;
out flat uint draw_id;
out flat uint vox_id;
out flat ivec4 chunk_aabb;
out flat uint material_id;


vec3 cube_vertex(uint id)
{
    uint edge = id / 2;
    bool start = (id % 2) == 0;

    uint v0 = 0u;
    uint v1 = 0u;
    switch(edge)
    {
        case 0: v0=0u; v1=1u; break;
        case 1: v0=1u; v1=3u; break;
        case 2: v0=3u; v1=2u; break;
        case 3: v0=2u; v1=0u; break;
        case 4: v0=4u; v1=5u; break;
        case 5: v0=5u; v1=7u; break;
        case 6: v0=7u; v1=6u; break;
        case 7: v0=6u; v1=4u; break;
        case 8: v0=0u; v1=4u; break;
        case 9: v0=1u; v1=5u; break;
        case 10: v0=2u; v1=6u; break;
        case 11: v0=3u; v1=7u; break;
    }

    uint corner = start ? v0 : v1;

    return vec3(corner % 2, (corner / 2) % 2, (corner / 4) % 2);
}

const int vert_id_to_face_id[2][6] = int[2][6](
    int[6](0, 1, 2, 2, 1, 3),
    int[6](0, 2, 1, 2, 3, 1) 
);

vec3 emit_face(uint face_dir, float voxel_size, ivec3 voxel_coord, uint face_vertex_id)
{
    vec3 delta = 
    {
        (float(face_dir == 0) - float(face_dir == 1)),
        (float(face_dir == 2) - float(face_dir == 3)),
        (float(face_dir == 4) - float(face_dir == 5))
    };


    int axis = (int(face_dir) / 2);
    bool positive = face_dir % 2 == 0;

    ivec2 axes = ivec2(
        (axis + 1) % 3,
        (axis + 2) % 3
    );

    vec3 voxel_origin = voxel_size * vec3(voxel_coord);
    vec3 curr_face_origin = voxel_origin + delta * voxel_size;
    
    vec3 face_vertex;


    int f = vert_id_to_face_id[int(positive)][face_vertex_id];
    {
        ivec2 mask = { f % 2, f / 2 };
        vec3 offset = vec3(0.f);

        offset[axes.x] = mask.x;
        offset[axes.y] = mask.y;
        
        face_vertex = vec3(curr_face_origin + voxel_size * offset);
    }

    return face_vertex;
}

vec3 emit_greedy_face(greedy_face face, float voxel_size, uint face_vertex_id)
{
    int axis = int(face.dir) / 2;
    bool positive = (face.dir % 2u) == 0u;

    ivec2 axes = ivec2(
        (axis + 1) % 3,
        (axis + 2) % 3
    );

    int negative_indices[6] = int[6](0, 2, 1, 1, 2, 3);
    int positive_indices[6] = int[6](0, 1, 2, 2, 1, 3);
    int corner_idx = positive ? positive_indices[face_vertex_id] : negative_indices[face_vertex_id];

    vec2 mask = vec2(corner_idx % 2, corner_idx / 2);

    vec3 offset = vec3(0.0);
    if (positive) offset[axis] = 1.0;

    offset[axes.x] += mask.x * face.width;
    offset[axes.y] += mask.y * face.height;

    return (vec3(face.x, face.y, face.z) + offset) * voxel_size;
}

uniform vec3 u_cube_min;
uniform vec3 u_cube_size;

#define BITMASK(n) ((1 << (n)) - 1)


void main()
{
    draw_id = gl_DrawID;
    
    if(u_render_cube == 1u)
    {
        v_world_pos = u_cube_min + cube_vertex(gl_VertexID) * u_cube_size;
    }
    else if(u_render_triangle == 1u)
    {
        v_world_pos = triangle[gl_VertexID].xyz;
        gl_Position = u_proj_matrix * u_view_matrix * vec4(v_world_pos, 1.0);
        return;
    }
    else
    {
        // #if 0
        // chunk_aabb = aabbs[gl_DrawID];
        // uint face_id = gl_VertexID / 6;
        // uint vertex_id = gl_VertexID % 6;
        // uint packed_face = faces[face_id];
        // 
        // uint x = (packed_face >> 0) & BITMASK(6);
        // uint y = (packed_face >> 6) & BITMASK(6);
        // uint z = (packed_face >> 12) & BITMASK(6);
        // uint dir = (packed_face >> 18) & BITMASK(3);
        // uint material = (packed_face >> 21) & BITMASK(11);
        // material_id = material;
        // 
        // vox_color = vec3(1.f);
        // 
        // float lod_factor = float(1 << chunk_aabb.w);
        // ivec3 chunk_offset = chunk_aabb.xyz; //offset in lod coordinates <0, 2^lod)
        // float chunk_size = (u_world_size / lod_factor);
        // float voxel_size = chunk_size/float(u_voxel_count);
        // 
        // vec3 vert = emit_face(dir, voxel_size, ivec3(x, y, z), vertex_id);
        // 
        // float cam_to_lod = (u_camera_chunk_size / chunk_size);
        // vec3 cam_chunk_lod_offset = u_camera_chunk_coord * cam_to_lod;
        // 
        // vec3 relative_offset = vec3(chunk_offset) - cam_chunk_lod_offset;
        // v_world_pos = vert.xyz + relative_offset * chunk_size;
        // 
        // gl_Position = u_proj_matrix * u_view_matrix * vec4(v_world_pos, 1.0);
        // return;
        // #endif // _____________________

        chunk_aabb = aabbs[gl_DrawID];

        uint face_id = gl_VertexID / 6;
        uint vertex_id = gl_VertexID % 6;
        uint64_t packed_face = faces[face_id];
        greedy_face face = unpack_face(packed_face);

        material_id = face.custom;

        float lod_factor = float(1 << chunk_aabb.w);
        ivec3 chunk_offset = chunk_aabb.xyz; //offset in lod coordinates <0, 2^lod)
        float chunk_size = (u_world_size / lod_factor);
        float voxel_size = chunk_size / float(u_voxel_count);

        float cam_to_lod = (u_camera_chunk_size / chunk_size);
        vec3 cam_chunk_lod_offset = u_camera_chunk_coord * cam_to_lod;
        vec3 relative_offset = vec3(chunk_offset) - cam_chunk_lod_offset;

        vec3 vert = emit_greedy_face(face, voxel_size, vertex_id);
        v_world_pos = vert + relative_offset * chunk_size;
        gl_Position = u_proj_matrix * u_view_matrix * vec4(v_world_pos, 1.0);
        return;
    }

    gl_Position = u_proj_matrix * u_view_matrix * vec4(v_world_pos - u_camera_chunk_coord * 8.f, 1.0);
}