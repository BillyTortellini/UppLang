#version 430

uniform float time;

out vec4 output_color;

void main() {
	output_color = vec4(0.7, sin(time*0.3)*.5+.5, 0.2, 1.0);
}
