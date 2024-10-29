
#ifdef VERTEX

in vec2 a_position; //@Position2D
in vec2 a_uvs; //@TextureCoordinates
in vec4 a_color; //@Color4

out vec2 f_uvs;
out vec4 f_color;

void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
    f_uvs = a_uvs;
    f_color = a_color;
}

#endif

#ifdef FRAGMENT

in vec2 f_uvs;
in vec4 f_color;
out vec4 o_color;

void main()
{
    float d = max(0.0, (0.5f - length(f_uvs - 0.5f)) * 2.0f);
    vec3 color = f_color.xyz;
    color = color * d * f_color.w;

    o_color.xyz = color;
    o_color.w = d * f_color.w;
}

#endif

