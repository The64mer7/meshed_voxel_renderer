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

void main()
{
	if(u_render_triangle == 1u)
	{
		FragColor = vec4(1,0,0,1);
		return;
	}
	vec3 dx = dFdx(v_world_pos);
	vec3 dy = dFdy(v_world_pos);

	vec3 normal = normalize(cross(dx, dy));
	
	float side_shading = 1.0;
	if (abs(normal.x) > 0.9) side_shading = 0.8;
	if (abs(normal.z) > 0.9) side_shading = 0.7;
	if (normal.y < -0.9)     side_shading = 0.6;
	float lambert =  max(0.1f, dot(normal, sun_dir));
#if 1
	float pixels_per_voxel = 16.f;
	vec2 triplanar = get_triplanar_uv(pixels_per_voxel*v_world_pos * 62.f/64.f, normal);

	triplanar = remap_texture(material_id, triplanar);
	vec3 color = texture(u_texture_atlas, triplanar).rgb * mix(vec3(1.f), vec3(95,159,63)/200.f, float(material_id == 5));
#else
	vec3 color = vox_color;
#endif
	vec3 diffuse = side_shading * color;

	FragColor = vec4(diffuse, 1);
	if(u_render_cube == 1u)
		FragColor = vec4(1,0,0,1);
	//FragColor = mod(vec4(chunk_aabb), 2.f) / vec4(vec3(2), 2.f);
}
