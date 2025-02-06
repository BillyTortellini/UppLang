
#ifdef VERTEX

in vec2 a_position; //@Position2D
in vec2 a_uvs;      //@TextureCoordinates
in vec4 a_color;    //@Color4

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

// FRAGyB
in vec2 f_uvs;
in vec4 f_color;

out vec4 o_color;

uniform sampler2D u_sampler;

void main()
{
    float texture_value = texture(u_sampler, f_uvs).x;
    o_color.xyz = f_color.xyz;
    o_color.w = f_color.w * pow(texture_value, 1/2.2);
}

#endif
