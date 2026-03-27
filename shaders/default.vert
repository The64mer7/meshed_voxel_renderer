#version 460 core

layout (std430, binding = 0) buffer vertex_buffer
{
	vec4 vertices[];
};

vec4 triangle[3] = vec4[3](
    vec4(-0.5, -0.5, -1.0, 1.0), // Bottom Left
    vec4( 0.5, -0.5, -1.0, 1.0), // Bottom Right
    vec4( 0.0,  0.5, -1.0, 1.0)  // Top Center
);

uniform uint u_offset;
uniform uint u_render_triangle;
uniform uint u_render_cube;

uniform mat4 u_view_matrix;
uniform mat4 u_proj_matrix;

out vec3 v_world_pos;

out flat vec3 vox_color;
out flat uint draw_id;

vec3 cube_vertex(uint id)
{
    uint edge = id / 2;       // which edge (0..11)
    bool start = (id % 2) == 0; // first or second vertex of the edge

    uint v0 = 0u;
    uint v1 = 0u;
    switch(edge)
    {
        case 0: v0=0u; v1=1u; break; // bottom edges
        case 1: v0=1u; v1=3u; break;
        case 2: v0=3u; v1=2u; break;
        case 3: v0=2u; v1=0u; break;
        case 4: v0=4u; v1=5u; break; // top edges
        case 5: v0=5u; v1=7u; break;
        case 6: v0=7u; v1=6u; break;
        case 7: v0=6u; v1=4u; break;
        case 8: v0=0u; v1=4u; break; // vertical edges
        case 9: v0=1u; v1=5u; break;
        case 10: v0=2u; v1=6u; break;
        case 11: v0=3u; v1=7u; break;
    }

    uint corner = start ? v0 : v1;

    return vec3(corner % 2, (corner / 2) % 2, (corner / 4) % 2);
}

uniform vec4 u_cube_xyz_size;

void main()
{
    draw_id = gl_DrawID;

    if(u_render_cube == 1u)
    {
        v_world_pos = u_cube_xyz_size.xyz + cube_vertex(gl_VertexID) * u_cube_xyz_size.w;
    }
    else
    {
        if(u_render_triangle == 1u)
            v_world_pos = triangle[gl_VertexID].xyz;
        else
        {
            vec4 vert = vertices[gl_VertexID];
            v_world_pos = vert.xyz;
            uint bits = floatBitsToUint(vert.w);
            vox_color.r = (255u & (bits >> 0)) / 255.f;
            vox_color.g = (255u & (bits >> 8)) / 255.f;
            vox_color.b = (255u & (bits >> 16)) / 255.f;
        }
    }

    gl_Position = u_proj_matrix * u_view_matrix * vec4(v_world_pos, 1.0);
}