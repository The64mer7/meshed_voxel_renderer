#version 460 core

out vec4 FragColor;

in vec3 v_world_pos;
in flat uint draw_id;
uniform uint u_render_cube;

vec3 sun_dir = normalize(vec3(0.2,0.6,0.7));
vec3 terrain_color = vec3(0.6,0.9,0.7);

void main()
{
	vec3 dx = dFdx(v_world_pos);
	vec3 dy = dFdy(v_world_pos);

	vec3 dz = normalize(cross(dx, dy));
	vec3 diffuse = max(0.1f, dot(dz, sun_dir)) * terrain_color;
	//FragColor = vec4(vec3(float(draw_id)/512.f), 1);
	//FragColor = vec4(dz*0.5+0.5, 1);

	FragColor = vec4(diffuse, 1);
	if(u_render_cube == 1u)
		FragColor = vec4(1,0,0,1);

}
