#ifdef VERTEX_SHADER

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec3 a_normal;
layout (location = 2) in vec2 a_uv;

out vec3 f_normal;
out vec3 f_position;
out vec2 f_uv;

layout (std140, binding = 0) uniform Camera
{
    mat4 view;
    mat4 projection;
    mat4 view_projection;
    mat4 view_inverse;
    vec4 position; // As Homogeneous vectors, w = 1.0f
    vec4 direction; // As Homogeneous vector
    vec4 parameters; // Packed data: x = near, y = far, z = time, w = unused
} u_camera;

uniform mat4 u_model;

void main()
{
    f_uv = a_uv;
    f_normal = a_normal;
    f_position = vec3(u_model * vec4(a_position, 1.0));
    gl_Position = u_camera.view_projection * vec4(f_position, 1.0);
}

#endif

#ifdef FRAGMENT_SHADER

in vec3 f_normal;
in vec3 f_position;
in vec2 f_uv;

uniform sampler2D u_diffuse_map;

layout (std140, binding = 0) uniform Camera
{
    mat4 view;
    mat4 projection;
    mat4 view_projection;
    mat4 view_inverse;
    vec4 position; // As Homogeneous vectors, w = 1.0f
    vec4 direction; // As Homogeneous vector
    vec4 parameters; // Packed data: x = near, y = far, z = time, w = unused
} u_camera;

out vec4 o_color;

void main()
{
    o_color = vec4(f_normal * 0.5 + 0.5, 1.0); 
    //o_color = vec4(1.0);
}

#endif