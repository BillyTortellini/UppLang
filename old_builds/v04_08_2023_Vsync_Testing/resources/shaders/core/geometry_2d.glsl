#ifdef VERTEX

in vec2 a_pos; //@Position2D
in vec3 a_color; //@Color3

out vec3 f_color;

void main()
{
    gl_Position = vec4(a_pos, 0.0, 1.0);
    f_color = a_color;
}

#endif

#ifdef FRAGMENT

in vec3 f_color;
out vec4 o_color;

void main() {
    o_color = vec4(f_color, 1.0);
}


#endif