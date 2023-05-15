//@VERTEX
in vec2 a_position; //@Position2D

uniform vec2 offset;
uniform float scale;

void main_vert()
{
    gl_Position = vec4(a_position * scale + offset, 0.0, 1.0);
}

//@FRAGMENT
out vec4 o_color;

void main_frag()
{
    o_color = vec4(1.0);
}
