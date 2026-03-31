#version 460 core

out vec4 FragColor;

in vec3 v_world_pos;
in flat uint draw_id;
uniform uint u_render_cube;
in flat vec3 vox_color;
in flat ivec4 chunk_data;


vec3 sun_dir = normalize(vec3(0.2,0.6,0.7));
uniform uint u_render_triangle;

void main()
{
	if(u_render_triangle == 1u)
	{
		FragColor = vec4(1,0,0,1);
		return;
	}
	vec3 dx = dFdx(v_world_pos);
	vec3 dy = dFdy(v_world_pos);

	vec3 dz = normalize(cross(dx, dy));
	
	float side_shading = 1.0;
	if (abs(dz.x) > 0.9) side_shading = 0.8;
	if (abs(dz.z) > 0.9) side_shading = 0.6;
	if (dz.y < -0.9)     side_shading = 0.4;
	float lambert =  max(0.1f, dot(dz, sun_dir));
	vec3 diffuse = side_shading * vox_color;
	//FragColor = vec4(vec3(float(draw_id)/512.f), 1);
	//FragColor = vec4(dz*0.5+0.5, 1);



	FragColor = vec4(diffuse, 1);
	if(u_render_cube == 1u)
		FragColor = vec4(1,0,0,1);
	//FragColor = mod(vec4(chunk_data), 2.f) / vec4(vec3(2), 2.f);
}
