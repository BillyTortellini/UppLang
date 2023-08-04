#ifdef VERTEX

uniform vec2 position;
uniform vec2 size;

in vec2 a_pos; //@Position2D

void main() {
	gl_Position = vec4((a_pos/2.0+0.5)*size + position, 0.0, 1.0);
}

#endif

#ifdef FRAGMENT

out vec4 output_color;

uniform vec4 color;

void main() {
	output_color = vec4(color);
}

#endif
