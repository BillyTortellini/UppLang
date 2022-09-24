#version 430

layout (location = 1) in vec2 position;
out vec2 uv_coords;

void main() {
	gl_Position = vec4(position, 0.0, 1.0);
	uv_coords = position * 0.5 + 0.5;
}