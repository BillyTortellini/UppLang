#ifdef VERTEX_SHADER

layout (location = 0) in vec3 a_position;
layout (location = 9) in vec3 a_color;

out vec3 f_color;

void main()
{
    gl_Position = vec4(a_position, 1.0);
    f_color = a_color;
}

#endif

#ifdef FRAGMENT_SHADER

in vec3 f_color;
out vec4 o_color;

void main()
{
    o_color = vec4(f_color, 1.0);
}

#endif
