#version 430 core

#ifdef VERTEX

in vec2 a_pos; //@Position2D
in vec4 a_line_start_end; //@Line_Start_End
in float a_line_width; //@Line_Width
in vec4 a_color; //@Color4

out vec2 v_pos; // In pixel coordinates
out vec4 v_line_start_end;
out vec4 v_color;
out float v_line_width;

layout (std140, binding = 0) uniform Render_Information
{
    float backbuffer_width;
    float backbuffer_height;
    float monitor_dpi;
    float current_time_in_seconds;
} u_render_info;

void main()
{
    vec2 window_size = vec2(u_render_info.backbuffer_width, u_render_info.backbuffer_height);
    vec2 pos = a_pos / window_size * 2.0f - 1.0f;
    gl_Position = vec4(pos.x, pos.y, 0.0, 1.0);

    v_pos = a_pos;
    v_line_start_end = a_line_start_end;
    v_line_width = a_line_width;
    v_color = a_color;
}

#endif

#ifdef FRAGMENT

in vec2 v_pos;
in vec4 v_line_start_end;
in vec4 v_color;
in float v_line_width;

out vec4 o_color;

void main()
{
    // Get line sdf
    // vec4 red = vec4(1.0f, 0.0f, 0.0f, 1.0f);
    // vec4 green = vec4(0.0f, 1.0f, 0.0f, 1.0f);
    // vec4 blue = vec4(0.0f, 0.0f, 1.0f, 1.0f);
    // vec4 white = vec4(1.0f, 1.0f, 1.0f, 1.0f);
    // o_color = white;
    // return;

    float sdf = 0.0f;
    {
        // SDF of line segment
        vec2 p = v_pos;
        vec2 a = vec2(v_line_start_end.x, v_line_start_end.y);
        vec2 b = vec2(v_line_start_end.z, v_line_start_end.w);

        vec2 ab = b - a;
        float t = dot(p - a, ab) / max(0.00001, ab.x * ab.x + ab.y * ab.y);
        t = min(1.0f, max(0.0f, t));
        sdf = length(p - (a + ab * t)) - v_line_width;
        //sdf = length(p - a) - u_width;
    }

    float radius = 1.0f;
    float scalar;
    scalar = smoothstep(-radius, radius, -sdf);
    //scalar = -sdf / 10.0f;
    // scalar = v_line_size.w / 20.0f;
    //scalar = 1.0f - abs(sdf * 0.1f);
    // o_color = vec4(v_color.x * scalar, v_color.y * scalar, v_color.z * scalar, 1.0f); // Constant color
    o_color = vec4(v_color.x, v_color.y, v_color.z, scalar); // Alpha blending
    // o_color = v_color; // Alpha blending
}

#endif