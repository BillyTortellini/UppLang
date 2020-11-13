#version 430

uniform mat4 uniform_mvp;

layout (location = 0) in vec3 attribute_position;

void main() {
	gl_Position = uniform_mvp * vec4(attribute_position, 1.0);
}