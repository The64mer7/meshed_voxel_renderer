#version 460 core

out vec4 FragColor;

in vec3 v_world_pos;
in flat uint draw_id;
uniform uint u_render_cube;
in flat vec3 vox_color;
in flat uint vox_id;
in flat ivec4 chunk_aabb;
in flat uint material_id;

uniform sampler2D u_texture_atlas;


vec3 sun_dir = normalize(vec3(0.2,0.6,0.7));
uniform uint u_render_triangle;

vec2 get_triplanar_uv(vec3 pos, vec3 normal) {
    vec3 abs_norm = abs(normal);
    
    vec2 uv;
    
    if (abs_norm.x > abs_norm.y && abs_norm.x > abs_norm.z) {
        uv = pos.zy;
    } else if (abs_norm.z > abs_norm.x && abs_norm.z > abs_norm.y) {
        uv = pos.xy;
    } else {
        uv = pos.xz;
    }

    return fract(uv);
}

vec2 remap_texture(uint id, vec2 uv)
{
	ivec2 size = textureSize(u_texture_atlas, 0);
	ivec2 blocks = size / 16;
	vec2 iuv = 16.f * uv;
	id--;
	iuv.x += float(id % blocks.x) * 16.f;
	iuv.y += float(blocks.y - id / blocks.x - 1) * 16.f;

	vec2 final = iuv / size;

	return final;
}

vec4 calculate_triangle_fragment()
{
    return vec4(1.0, 0.0, 0.0, 1.0);
}

vec4 calculate_cube_fragment()
{
    return vec4(1.0, 0.0, 0.0, 1.0);
}

vec3 calculate_surface_normal()
{
    vec3 dx = dFdx(v_world_pos);
    vec3 dy = dFdy(v_world_pos);
    return normalize(cross(dx, dy));
}

float calculate_side_shading(vec3 normal)
{
    float shading = 1.0;
    if (abs(normal.x) > 0.9) shading = 0.8;
    if (abs(normal.z) > 0.9) shading = 0.7;
    if (normal.y < -0.9)     shading = 0.6;
    return shading;
}

vec3 sample_material_color(vec3 normal)
{
    float pixels_per_voxel = 1.0;
    vec2 triplanar = get_triplanar_uv(pixels_per_voxel * v_world_pos * (62.0 / 64.0), normal);
    triplanar = remap_texture(material_id, triplanar);
    
    vec3 base_color = texture(u_texture_atlas, triplanar).rgb;
    vec3 tint = mix(vec3(1.0), vec3(95.0, 159.0, 63.0) / 200.0, float(material_id == 5u));
    
    return base_color * tint;
}

#define RENDER_TEXTURE 0
vec4 calculate_voxel_fragment()
{
    vec3 normal = calculate_surface_normal();
    float side_shading = calculate_side_shading(normal);
#if RENDER_TEXTURE
    vec3 color = sample_material_color(normal);
    vec3 diffuse = side_shading * color;

    return vec4(diffuse, 1.0);
#else
    return vec4(vox_color * vec3(side_shading), 1.0);
#endif
}


void main()
{
    if (u_render_triangle == 1u)
    {
        FragColor = calculate_triangle_fragment();
        return;
    }

    if (u_render_cube == 1u)
    {
        FragColor = calculate_cube_fragment();
        return;
    }

    FragColor = calculate_voxel_fragment();
}