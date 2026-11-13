#version 430 core

//@VERTEX

layout (std430, binding = 1) uniform Render_Info_UBO
{
    mat4 view;
    mat4 projection;
} ubo_render_info;

layout (std430, binding = 1) buffer GenericInputData
{
    vec2 positions[];
} vertex_data;

vec2 vertices[3] = {
    vec2(-0.5, -0.5),
    vec2( 0.5, -0.5),
    vec2( 0.0,  0.5)
}; 

void main()
{
    vec2 pos = vec2(0.0, 0.0);
    if (gl_VertexID < 3) {
        pos = vertex_data.positions[gl_VertexID];
        // pos = vertices[gl_VertexID];
    }
    gl_Position = vec4(pos.x, pos.y, 0.0, 1.0);
}

//@END


//@FRAGMENT

out vec4 o_color;

void main()
{
    o_color = vec4(1.0, 0.0, 0.0, 1.0);
}

//@END