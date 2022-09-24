#ifdef VERTEX_SHADER

layout (std140, binding = 0) uniform Camera
{
    mat4 view;
    mat4 projection;
    mat4 view_projection;
    mat4 view_inverse;
    vec4 position; // As Homogeneous vectors, w = 1.0f
    vec4 direction; // As Homogeneous vector
    vec4 parameters; // Packed data: x = near, y = far, z = time, w = unused
} camera;

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_color;

out vec3 f_color;

uniform mat4 u_model;

void main()
{
    gl_Position = camera.view_projection * u_model * vec4(a_pos, 1.0);
    f_color = a_color;
}

#endif



#ifdef FRAGMENT_SHADER

in vec3 f_color;
out vec4 o_color;

void main() {
    o_color = vec4(f_color, 1.0);
}

#endif
