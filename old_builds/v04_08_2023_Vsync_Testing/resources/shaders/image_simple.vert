#version 430

layout (location = 1) in vec2 attribute_position;

out vec2 uv_coords;

void main() {
	gl_Position = vec4(attribute_position, 0.0, 1.0);
	uv_coords = attribute_position / 2.0 + vec2(0.5);
}