#ifdef VERTEX_SHADER

layout (std140, binding = 0) uniform Render_Information
{
    float viewport_width;
    float viewport_height;
    float window_width;
    float window_height;
    float monitor_dpi;
    float current_time_in_seconds;
} u_render_info;

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

#endif