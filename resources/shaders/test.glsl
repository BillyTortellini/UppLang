#ifdef VERTEX_SHADER

layout (std140, binding = 1) uniform Camera
{
    mat4 view;
    mat4 inverse_view;
    mat4 projection;
    mat4 view_projection;

    vec4 camera_position; // Packed, w = 1.0
    vec4 camera_direction; // Packed, w = 1.0
    vec4 camera_up;        // Packed w = 1.0
    float near_distance;
    float far_distance;
    float field_of_view_x;
    float field_of_view_y;
} u_camera;

layout (location = 0) in vec3 a_position;

out vec2 f_uv;

void main()
{
    gl_Position = u_camera.view_projection * vec4(a_position, 1.0);
    f_uv = a_position.xy + 0.5;
}

#endif

#ifdef FRAGMENT_SHADER

layout (std140, binding = 0) uniform Render_Information
{
    float viewport_width;
    float viewport_height;
    float window_width;
    float window_height;
    float monitor_dpi;
    float current_time_in_seconds;
} u_render_info;

in vec2 f_uv;
out vec4 o_color;

uniform sampler2D texture_fb;

void main()
{
    o_color = vec4(0.5, sin(u_render_info.current_time_in_seconds), 0.0, 0.0); 
    o_color = 0.5 * o_color + 0.5 * texture(texture_fb, f_uv).rgba;
}

#endif
