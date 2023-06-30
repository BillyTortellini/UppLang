#version 430 core

#ifdef VERTEX

in vec3 a_position; //@Position3D
in vec4 a_color; //@Color4

out vec4 f_color;

void main()
{
    gl_Position = vec4(a_position, 1.0);
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
