#version 460 core

vec3 cube_vertex(uint id)
{
	return vec3(id % 2, (id / 2) % 2, (id / 4) % 2);
}

void main()
{

}