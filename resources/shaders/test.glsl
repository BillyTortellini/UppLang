#version 430 core
#ifdef VERTEX

in vec2 a_position; //@Position2D
out vec2 uvs;

uniform vec2 offset;
uniform float scale;

void main()
{
    gl_Position = vec4(a_position * scale + offset, 0.0, 1.0);
    uvs = a_position;
}

#endif 

#ifdef FRAGMENT
in vec2 uvs;
out vec4 o_color;

layout (location = 2) uniform sampler2D image;

void main()
{
    o_color = texture(image, (uvs+vec2(0.5)));
}

#endif
