#ifdef VERTEX_SHADER

layout (location = 0) in vec3 a_position;

void main()
{
    gl_Position = vec4(a_position, 1.0);
}

#endif

#ifdef FRAGMENT_SHADER

out vec4 o_color;

void main()
{
    o_color = vec4(0.5, sin(0.0), 0.0, 0.0); 
}

#endif
