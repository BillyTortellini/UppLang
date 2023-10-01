#version 430 core

#ifdef VERTEX

in vec4 v_posSize; //@PosSize2D
in vec4 v_clippedBB; //@ClippedBoundingBox
in vec2 v_thicknessRadius; //@BorderThicknessEdgeRadius
in vec4 v_color; //@Color4
in vec4 v_borderColor; //@BorderColor

out vec4 g_posSize;
out vec4 g_clippedBB;
out vec2 g_thicknessRadius; 
out vec4 g_color;
out vec4 g_borderColor; 

void main(){
    g_posSize = v_posSize;
    g_clippedBB = v_clippedBB;
    g_thicknessRadius = v_thicknessRadius;
    g_color = v_color;
    g_borderColor = v_borderColor;
}

#endif 



#ifdef GEOMETRY

layout (points) in;
in vec4 g_posSize[];
in vec4 g_clippedBB[];
in vec4 g_color[];
in vec4 g_borderColor[];
in vec2 g_thicknessRadius[];

layout (triangle_strip, max_vertices = 4) out;
flat out vec4 f_color;
flat out vec4 f_borderColor;
flat out vec2 f_thicknessRadius;
flat out vec4 f_posSize;
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

    f_borderColor = g_borderColor[0];
    f_thicknessRadius = g_thicknessRadius[0];
    f_color = g_color[0];
    f_posSize = g_posSize[0];

    f_rectPos = vec2(g_clippedBB[0].z, g_clippedBB[0].y);
    gl_Position = vec4(f_rectPos.x / screen_size.x * 2.0 - 1.0, f_rectPos.y / screen_size.y * 2.0 - 1.0, 0.0, 1.0);
    EmitVertex();

    f_rectPos = vec2(g_clippedBB[0].z, g_clippedBB[0].w);
    gl_Position = vec4(f_rectPos.x / screen_size.x * 2.0 - 1.0, f_rectPos.y / screen_size.y * 2.0 - 1.0, 0.0, 1.0);
    EmitVertex();

    f_rectPos = vec2(g_clippedBB[0].x, g_clippedBB[0].y);
    gl_Position = vec4(f_rectPos.x / screen_size.x * 2.0 - 1.0, f_rectPos.y / screen_size.y * 2.0 - 1.0, 0.0, 1.0);
    EmitVertex();

    f_rectPos = vec2(g_clippedBB[0].x, g_clippedBB[0].w);
    gl_Position = vec4(f_rectPos.x / screen_size.x * 2.0 - 1.0, f_rectPos.y / screen_size.y * 2.0 - 1.0, 0.0, 1.0);
    EmitVertex();
}


#endif



#ifdef FRAGMENT

flat in vec4 f_color;
flat in vec4 f_posSize;
flat in vec4 f_borderColor;
flat in vec2 f_thicknessRadius;
in vec2 f_rectPos;

out vec4 o_color;

void main()
{
    // Calcuate distance from border
    vec2 size = f_posSize.zw;
    vec2 dist_to_mid = abs(f_rectPos - (f_posSize.xy + (size / 2.0)));

    float thickness = f_thicknessRadius.x;
    float radius = f_thicknessRadius.y;

    float border_alpha; // How much percent we are on a border
    float outside_alpha; // How much we are outside of the rectangle (Rounded borders)

    // Check for rounded corners
    vec2 center = size / 2.0 - vec2(radius);
    if (dist_to_mid.x > center.x && dist_to_mid.y > center.y) {
        float dist = radius - distance(center, dist_to_mid);
        border_alpha = smoothstep(thickness - 0.5, thickness + 0.5, dist);
        outside_alpha = smoothstep(0.5, -0.5, dist);
    }
    else {
        vec2 dist2 = size / 2.0 - dist_to_mid;
        float dist = min(dist2.x, dist2.y);
        border_alpha = step(thickness, dist);
        outside_alpha = 0.0;
    }
    
    // Calculate final color based on mix between color, border-color and outside
    o_color = mix(f_borderColor, f_color, border_alpha);
    o_color.w *= 1.0 - outside_alpha;
}

#endif
