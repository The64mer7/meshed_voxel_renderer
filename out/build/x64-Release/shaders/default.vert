#version 460 core

//layout (std430, binding = 0) buffer vertex_buffer
//{
//	vec4 vertices[];
//};

layout (std430, binding = 0) buffer vertex_buffer
{
	uint vertices[];
};

layout (std430, binding = 1) buffer chunk_buffer
{
	ivec4 chunks[];
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
uniform ivec3 u_camera_chunk_pos;

uniform mat4 u_view_matrix;
uniform mat4 u_proj_matrix;

out vec3 v_world_pos;

out flat vec3 vox_color;
out flat uint draw_id;
out flat uint vox_id;
out flat ivec4 chunk_data;
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

vec3 emit_face(uint face_dir, float voxel_size, ivec3 voxel_coord, uint face_vertex)
{
    ivec3 idelta =
    {
        ((face_dir == 0) ? 1 : 0) - 
        ((face_dir == 1) ? 1 : 0),
        ((face_dir == 2) ? 1 : 0) - 
        ((face_dir == 3) ? 1 : 0),
        ((face_dir == 4) ? 1 : 0) - 
        ((face_dir == 5) ? 1 : 0)
    };

    vec3 delta = idelta;

    int axis = (int(face_dir) / 2);
    bool positive = face_dir % 2 == 0;

    vec3 vertDelta = step(vec3(0.0001f), delta);
    vec3 face[4];
    ivec2 axes = ivec2(
        (axis + 1) % 3,
        (axis + 2) % 3
    );

    vec3 voxel_origin = voxel_size * vec3(voxel_coord);
    vec3 currFaceOrigin = voxel_origin + vertDelta * voxel_size;

    for (int f = 0; f < 4; f++)
    {
        ivec2 mask = { f % 2, f / 2 };
        vec3 offset = vec3(0.f);

        offset[axes.x] = mask.x;
        offset[axes.y] = mask.y;
        
        face[f] = vec3(currFaceOrigin + voxel_size * offset);
    }

    if(face_vertex == 0) return (face[0]);
    if(face_vertex == 1) return (face[positive ? 1 : 2]);
    if(face_vertex == 2) return (face[positive ? 2 : 1]);
    if(face_vertex == 3) return (face[positive ? 2 : 1]);
    if(face_vertex == 4) return (face[positive ? 1 : 2]);
    if(face_vertex == 5) return (face[3]);

    while(true);
    return vec3(0.f);
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
    else
    {
        if(u_render_triangle == 1u)
        {
            v_world_pos = triangle[gl_VertexID].xyz;
            gl_Position = u_proj_matrix * u_view_matrix * vec4(v_world_pos, 1.0);
            return;
        }
        else
        {
            chunk_data = chunks[gl_DrawID];

            #if 0
            vec4 vert = vertices[gl_VertexID];
            uint bits = floatBitsToUint(vert.w);
            vox_color.r = (255u & (bits >> 0)) / 255.f;
            vox_color.g = (255u & (bits >> 8)) / 255.f;
            vox_color.b = (255u & (bits >> 16)) / 255.f;
            vox_id = (bits >> 24) & 255u;
            #else

            uint face_id = gl_VertexID / 6;
            uint vertex_id = gl_VertexID % 6;
            uint packed_face = vertices[face_id];

            uint x = (packed_face >> 0) & BITMASK(6);
            uint y = (packed_face >> 6) & BITMASK(6);
            uint z = (packed_face >> 12) & BITMASK(6);
            uint dir = (packed_face >> 18) & BITMASK(3);
            uint material = (packed_face >> 21) & BITMASK(11);
            material_id = material;

            vox_color = vec3(1.f);
            #endif

            float lod_factor = float(1 << chunk_data.w);
            ivec3 chunk_offset = chunk_data.xyz; //offset in lod coordinates <0, 2^lod)
            float chunk_size = (u_world_size / lod_factor);
            float voxel_size = chunk_size/32.f;

            vec3 vert = emit_face(dir, voxel_size, ivec3(x, y, z), vertex_id);

            float cam_to_lod = (8.f / chunk_size);
            vec3 cam_chunk_lod_offset = u_camera_chunk_pos * cam_to_lod;

            vec3 relative_offset = vec3(chunk_offset) - cam_chunk_lod_offset;
            v_world_pos = vert.xyz + relative_offset * chunk_size;

            gl_Position = u_proj_matrix * u_view_matrix * vec4(v_world_pos, 1.0);
            return;
        }
    }

    gl_Position = u_proj_matrix * u_view_matrix * vec4(v_world_pos - u_camera_chunk_pos * 8.f, 1.0);
}