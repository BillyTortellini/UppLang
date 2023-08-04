#version 430

uniform vec2 position;
uniform vec2 size;

layout (location = 1) in vec2 a_pos;

void main() {
	gl_Position = vec4((a_pos/2.0+0.5)*size + position, 0.0, 1.0);
}