#version 430

layout (location = 1) in vec2 attribute_position;
layout (location = 2) in vec2 texture_coordinates;

out vec2 uv_coords;

void main() 
{
	gl_Position = vec4(attribute_position, 0, 1);
	uv_coords = texture_coordinates;
}