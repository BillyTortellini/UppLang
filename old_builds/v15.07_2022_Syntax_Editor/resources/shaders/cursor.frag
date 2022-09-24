#version 430

out vec4 output_color;

uniform vec3 color;

void main() {
	output_color = vec4(color, 1.0);
}
