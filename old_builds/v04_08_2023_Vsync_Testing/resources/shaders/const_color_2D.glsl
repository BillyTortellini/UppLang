
#ifdef VERTEX

in vec2 a_position; //@Position2D
in vec2 a_uvs; //@TextureCoordinates

out vec2 f_uvs;

void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
    f_uvs = a_uvs;
}

#endif

#ifdef FRAGMENT

in vec2 f_uvs;
out vec4 o_color;

uniform vec4 u_color;

void main()
{
    o_color = u_color;
    o_color.xy = (f_uvs + 1) / 2.0;
}

#endif
