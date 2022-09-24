#ifdef VERTEX_SHADER

uniform vec2 u_pos;
uniform vec2 u_size;

layout (location = 1) in vec2 a_pos;

void main() {
	gl_Position = vec4(a_pos*u_size + u_pos, 0.0, 1.0);
}

#endif



#ifdef FRAGMENT_SHADER

out vec4 output_color;

uniform vec3 u_color;

void main() {
	output_color = vec4(u_color, 1.0);
}

#endif
