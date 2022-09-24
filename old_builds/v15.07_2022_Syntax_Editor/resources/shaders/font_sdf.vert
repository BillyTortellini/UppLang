#version 430

layout (location = 1) in vec2 attribute_position;
layout (location = 2) in vec2 texture_coordinates;
layout (location = 3) in float pixel_size;
layout (location = 4) in vec3 color;

out vec2 uv_coords;
out float pixel_ratio;
out vec3 frag_color;

void main() 
{
	gl_Position = vec4(attribute_position, 0, 1);
	uv_coords = texture_coordinates;
	pixel_ratio = pixel_size;
	frag_color = color;
}