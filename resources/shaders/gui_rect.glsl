#version 430 core

#ifdef VERTEX

in vec2 a_position; //@Position2D
in vec4 a_color; //@Color4

out vec4 f_color;

void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
    f_color = a_color;
}

#endif

#ifdef FRAGMENT

in vec4 f_color;
out vec4 o_color;

void main()
{
    o_color = f_color;
}

#endif
