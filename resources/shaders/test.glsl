#version 430 core
#ifdef VERTEX

in vec4 a_posSize; //@PosSize2D
in vec2 a_thicknessRadius; //@BorderThicknessEdgeRadius
in vec4 a_color; //@Color4
in vec4 a_borderColor; //@BorderColor

out vec4 g_posSize;
out vec2 g_thicknessRadius;
out vec4 g_color;
out vec4 g_borderColor;

void main()
{
    g_posSize = a_posSize;
    g_color = a_color;
    g_borderColor = a_borderColor;
    g_thicknessRadius = a_thicknessRadius;
}

#endif 



#ifdef GEOMETRY

layout (points) in;
in vec4 g_posSize[];
in vec4 g_color[];
in vec4 g_borderColor[];
in vec2 g_thicknessRadius[];

layout (triangle_strip, max_vertices = 4) out;
flat out vec4 f_color;
flat out vec4 f_borderColor;
flat out vec2 f_thicknessRadius;
flat out vec2 f_size;
out vec2 f_rectPos;

layout (std140, binding = 0) uniform Render_Information
{
    float backbuffer_width;
    float backbuffer_height;
    float monitor_dpi;
    float current_time_in_seconds;
} u_render_info;

void main()
{
    vec2 screen_size = vec2(u_render_info.backbuffer_width, u_render_info.backbuffer_height);
    vec2 pos = g_posSize[0].xy / screen_size * 2.0 - 1.0;
    vec2 size = g_posSize[0].zw / screen_size * 2.0;

    f_borderColor = g_borderColor[0];
    f_thicknessRadius = g_thicknessRadius[0];
    f_color = g_color[0];
    f_size = g_posSize[0].zw;

    gl_Position = vec4(pos.x + size.x, pos.y, 0.0, 1.0);
    f_rectPos = vec2(1.0, 0.0) * g_posSize[0].zw;
    EmitVertex();

    gl_Position = vec4(pos.x + size.x, pos.y + size.y, 0.0, 1.0);
    f_rectPos = vec2(1.0, 1.0) * g_posSize[0].zw;
    EmitVertex();

    gl_Position = vec4(pos.x, pos.y, 0.0, 1.0);
    f_rectPos = vec2(0.0, 0.0) * g_posSize[0].zw;
    EmitVertex();

    gl_Position = vec4(pos.x, pos.y + size.y, 0.0, 1.0);
    f_rectPos = vec2(0.0, 1.0) * g_posSize[0].zw;
    EmitVertex();
}


#endif



#ifdef FRAGMENT

flat in vec4 f_color;
flat in vec2 f_size;
flat in vec4 f_borderColor;
flat in vec2 f_thicknessRadius;
in vec2 f_rectPos;

out vec4 o_color;

void main()
{
    // Calcuate distance from border
    vec2 dist_to_mid = abs(f_rectPos - f_size / 2.0);

    float thickness = f_thicknessRadius.x;
    float radius = f_thicknessRadius.y;
    thickness = 16.0;
    radius = 15.0;

    float border_alpha; // How much percent we are on a border
    float outside_alpha; // How much we are outside of the rectangle (Rounded borders)

    // Check for rounded corners
    vec2 center = f_size / 2.0 - vec2(radius);
    if (dist_to_mid.x > center.x && dist_to_mid.y > center.y) {
        float dist = radius - distance(center, dist_to_mid);
        border_alpha = smoothstep(thickness - 0.5, thickness + 0.5, dist);
        outside_alpha = smoothstep(0.5, -0.5, dist);
    }
    else {
        vec2 dist2 = f_size / 2.0 - dist_to_mid;
        float dist = min(dist2.x, dist2.y);
        border_alpha = step(thickness, dist - 0.5);
        outside_alpha = 0.0;
    }
    
    // Calculate final color based on mix between color, border-color and outside
    vec4 border = vec4(1.0, 0.0, 0.0, 1.0);
    vec4 bg = vec4(0.0);
    o_color = mix(border, f_color, border_alpha);
    o_color = mix(o_color, bg, outside_alpha);
}

#endif
