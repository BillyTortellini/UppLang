#ifdef VERTEX_SHADER

layout (location = 0) in vec3 a_position;
layout (location = 2) in vec2 a_uv;
layout (location = 9) in vec3 a_color;
layout (location = 11) in float a_thickness;
layout (location = 12) in float a_length;

out vec3 f_color;
out vec2 f_uv;
out float f_thickness;
out float f_length;

void main()
{
    gl_Position = vec4(a_position, 1.0);
    f_color = a_color;
    f_uv = a_uv;
    f_thickness = a_thickness;
    f_length = a_length;
}

#endif

#ifdef FRAGMENT_SHADER

in vec3 f_color;
in vec2 f_uv;
in float f_thickness;
in float f_length;
out vec4 o_color;

void main()
{
    o_color = vec4(f_color, 1.0);

    float l_alpha = abs(f_uv.x - 0.5) * 2.0f;
    l_alpha = smoothstep(1.0 - (4.0 / f_length), 1.0, l_alpha);
    //l_alpha = smoothstep(1.0 - (0.02), 1.0, l_alpha);
    l_alpha = 1.0 - l_alpha;
    //o_color = vec4(vec3(l_alpha), 1.0);

    float w_alpha = abs(f_uv.y - 0.5) * 2.0;
    w_alpha = smoothstep(1.0 - (2.0 / f_thickness), 1.0, w_alpha);
    w_alpha = 1.0 - w_alpha;

    float alpha = w_alpha * l_alpha;
    //alpha = l_alpha;
    alpha = pow(alpha, 1.0/2.2);
    o_color = vec4(f_color, alpha);

    //o_color = vec4(vec3(c), 1.0);
}

#endif
