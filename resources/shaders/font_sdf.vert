#version 430

layout (location = 1) in vec2 a_position;
layout (location = 2) in vec2 a_uvs;
layout (location = 9) in vec3 a_color;
layout (location = 11) in float a_pixel_size;

out vec2 uv_coords;
out float pixel_ratio;
out vec3 frag_color;

void main() 
{
	gl_Position = vec4(a_position, 0, 1);
	uv_coords = a_uvs;
	pixel_ratio = a_pixel_size;
	frag_color = a_color;
}