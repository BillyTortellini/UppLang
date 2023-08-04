#ifdef VERTEX

layout (location = 0) in vec3 a_position;
layout (location = 2) in vec2 a_uv;
layout (location = 9) in vec3 a_color;
layout (location = 11) in float a_radius;

out vec3 f_color;
out vec2 f_uv;
out float f_radius;

void main()
{
    gl_Position = vec4(a_position, 1.0);
    f_color = a_color;
    f_uv = a_uv;
    f_radius = a_radius;
}

#endif

#ifdef FRAGMENT_SHADER

in vec3 f_color;
in vec2 f_uv;
in float f_radius;
out vec4 o_color;

void main()
{
    float dist = length(f_uv - vec2(0.5));
    float s = 1.0 / f_radius / 2.0;
    float alpha = step(0.5, dist);
    alpha = smoothstep(0.5 - s, 0.5 + s, dist);
    alpha = 1.0 - alpha;
    //alpha = pow(alpha, 2.2);
    if (alpha == 0) discard;
    o_color = vec4(vec3(f_color), alpha);
}

#endif
