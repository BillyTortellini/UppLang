#include "Test_Renderer.hpp"

#include "mesh_utils.hpp"

Mesh_CPU_Buffer_Dynamic mesh_cpu_buffer_dynamic_create(Rendering_Core* core, int expected_face_count, int vertex_byte_size, Array<Vertex_Attribute> attributes)
{
    Mesh_CPU_Buffer_Dynamic result;
    result.index_buffer = dynamic_array_create_empty<u32>(expected_face_count * 3);
    result.vertex_buffer = dynamic_array_create_empty<byte>(expected_face_count * 3 * vertex_byte_size);
    result.mesh = mesh_gpu_buffer_create_with_single_vertex_buffer(
        core,
        gpu_buffer_create_empty(expected_face_count * 3 * vertex_byte_size, GPU_Buffer_Type::VERTEX_BUFFER, GPU_Buffer_Usage::DYNAMIC),
        attributes,
        gpu_buffer_create_empty(expected_face_count * 3 * 4, GPU_Buffer_Type::INDEX_BUFFER, GPU_Buffer_Usage::DYNAMIC),
        Mesh_Topology::TRIANGLES,
        0
    );
    result.vertex_count = 0;
    return result;
}

void mesh_cpu_buffer_dynamic_dynamic_destroy(Mesh_CPU_Buffer_Dynamic* buffer)
{
    dynamic_array_destroy(&buffer->index_buffer);
    dynamic_array_destroy(&buffer->vertex_buffer);
    mesh_gpu_buffer_destroy(&buffer->mesh);
}

void mesh_cpu_buffer_dynamic_add_face(Mesh_CPU_Buffer_Dynamic* buffer, int index_offset_0, int index_offset_1, int index_offset_2)
{
    dynamic_array_push_back(&buffer->index_buffer, (u32)buffer->vertex_count + index_offset_0);
    dynamic_array_push_back(&buffer->index_buffer, (u32)buffer->vertex_count + index_offset_1);
    dynamic_array_push_back(&buffer->index_buffer, (u32)buffer->vertex_count + index_offset_2);
}

void mesh_cpu_buffer_dynamic_upload_data(Mesh_CPU_Buffer_Dynamic* buffer, Rendering_Core* core)
{
    gpu_buffer_update(&buffer->mesh.vertex_buffers[0].gpu_buffer, dynamic_array_as_bytes(&buffer->vertex_buffer));
    mesh_gpu_buffer_update_index_buffer(&buffer->mesh, core, dynamic_array_as_array(&buffer->index_buffer));
    dynamic_array_reset(&buffer->vertex_buffer);
    dynamic_array_reset(&buffer->index_buffer);
    buffer->vertex_count = 0;
}

vec2 anchor_to_direction(Anchor_2D anchor) 
{
    switch (anchor) {
    case Anchor_2D::TOP_LEFT: return vec2(-1.0f, 1.0f);
    case Anchor_2D::TOP_CENTER: return vec2(0.0f, 1.0f);
    case Anchor_2D::TOP_RIGHT: return vec2(1.0f, 1.0f);
    case Anchor_2D::CENTER_LEFT: return vec2(-1.0f, 0.0f);  
    case Anchor_2D::CENTER_CENTER: return vec2(0.0f, 0.0f);
    case Anchor_2D::CENTER_RIGHT: return vec2(1.0f, 0.0f);
    case Anchor_2D::BOTTOM_LEFT: return vec2(-1.0f, -1.0f);
    case Anchor_2D::BOTTOM_CENTER: return vec2(0.0f, -1.0f);
    case Anchor_2D::BOTTOM_RIGHT: return vec2(1.0f, -1.0f);
    }
    return vec2(0.0f);
}

Vertex_Rectangle vertex_rectangle_make(vec3 pos, vec3 color)
{
    Vertex_Rectangle rect;
    rect.position = pos;
    rect.color = color;
    return rect;
}

Vertex_Circle vertex_circle_make(vec3 pos, vec3 color, vec2 uvs, float radius)
{
    Vertex_Circle c;
    c.position = pos;
    c.color = color;
    c.uvs = uvs;
    c.radius = radius;
    return c;
}

Vertex_Line vertex_line_make(vec3 pos, vec3 color, vec2 uv, float thickness, float length)
{
    Vertex_Line vertex;
    vertex.position = pos;
    vertex.color = color;
    vertex.uv = uv;
    vertex.thickness = thickness;
    vertex.length = length;
    return vertex;
}

Primitive_Renderer_2D* primitive_renderer_2D_create(Rendering_Core* core)
{
    Primitive_Renderer_2D* result = new Primitive_Renderer_2D();
    result->render_info = &core->render_information;
    result->shader_rectangles = shader_program_create(core, { "resources/shaders/rectangle_2D.glsl" });
    result->shader_circles = shader_program_create(core, { "resources/shaders/circle_2D.glsl" });
    result->shader_lines = shader_program_create(core, { "resources/shaders/line_2D.glsl" });
    result->line_train_points = dynamic_array_create_empty<Line_Train_Point>(64);

    Vertex_Attribute rectangle_attributes[] = {
        vertex_attribute_make(Vertex_Attribute_Type::POSITION_3D),
        vertex_attribute_make(Vertex_Attribute_Type::COLOR3),
    };
    result->mesh_rectangles = mesh_cpu_buffer_dynamic_create(core, 256, sizeof(Vertex_Rectangle), array_create_static(rectangle_attributes, 2));

    Vertex_Attribute circle_attributes[] = {
        vertex_attribute_make(Vertex_Attribute_Type::POSITION_3D),
        vertex_attribute_make(Vertex_Attribute_Type::COLOR3),
        vertex_attribute_make(Vertex_Attribute_Type::UV_COORDINATES_0),
        vertex_attribute_make_custom(Vertex_Attribute_Data_Type::FLOAT, 11)
    };
    result->mesh_circles = mesh_cpu_buffer_dynamic_create(core, 256, sizeof(Vertex_Circle), array_create_static(circle_attributes, 4));

    Vertex_Attribute line_attributes[] = {
        vertex_attribute_make(Vertex_Attribute_Type::POSITION_3D),
        vertex_attribute_make(Vertex_Attribute_Type::COLOR3),
        vertex_attribute_make(Vertex_Attribute_Type::UV_COORDINATES_0),
        vertex_attribute_make_custom(Vertex_Attribute_Data_Type::FLOAT, 11),
        vertex_attribute_make_custom(Vertex_Attribute_Data_Type::FLOAT, 12)
    };
    result->mesh_lines = mesh_cpu_buffer_dynamic_create(core, 256, sizeof(Vertex_Line), array_create_static(line_attributes, 5));

    return result;
}

void primitive_renderer_2D_destroy(Primitive_Renderer_2D* renderer, Rendering_Core* core)
{
    shader_program_destroy(renderer->shader_rectangles);
    shader_program_destroy(renderer->shader_circles);
    shader_program_destroy(renderer->shader_lines);
    dynamic_array_destroy(&renderer->line_train_points);
    mesh_cpu_buffer_dynamic_dynamic_destroy(&renderer->mesh_rectangles);
    mesh_cpu_buffer_dynamic_dynamic_destroy(&renderer->mesh_circles);
    mesh_cpu_buffer_dynamic_dynamic_destroy(&renderer->mesh_lines);
    delete renderer;
}

void primitive_renderer_2D_add_rectangle(Primitive_Renderer_2D* renderer, vec2 anchor_pos, vec2 size, float depth, Anchor_2D anchor, vec3 color)
{
    vec2 dir = anchor_to_direction(anchor);
    vec2 hs = size / 2.0f;
    vec2 center = anchor_pos - (dir * hs);

    vec2 vp_size = vec2(renderer->render_info->viewport_width, renderer->render_info->viewport_height);
    vec2 p0 = (center + vec2(-hs.x, -hs.y)) / vp_size * vec2(2) - vec2(1);
    vec2 p1 = (center + vec2(hs.x, -hs.y))  / vp_size * vec2(2) - vec2(1);
    vec2 p2 = (center + vec2(hs.x, hs.y))   / vp_size * vec2(2) - vec2(1) ;
    vec2 p3 = (center + vec2(-hs.x, hs.y))  / vp_size * vec2(2) - vec2(1) ;

    mesh_cpu_buffer_dynamic_add_face(&renderer->mesh_rectangles, 0, 1, 2);
    mesh_cpu_buffer_dynamic_add_face(&renderer->mesh_rectangles, 0, 2, 3);
    mesh_cpu_buffer_dynamic_add_vertex(&renderer->mesh_rectangles, vertex_rectangle_make(vec3(p0, depth), color));
    mesh_cpu_buffer_dynamic_add_vertex(&renderer->mesh_rectangles, vertex_rectangle_make(vec3(p1, depth), color));
    mesh_cpu_buffer_dynamic_add_vertex(&renderer->mesh_rectangles, vertex_rectangle_make(vec3(p2, depth), color));
    mesh_cpu_buffer_dynamic_add_vertex(&renderer->mesh_rectangles, vertex_rectangle_make(vec3(p3, depth), color));
}

void primitive_renderer_2D_add_circle(Primitive_Renderer_2D* renderer, vec2 center, float radius, float depth, vec3 color)
{
    vec2 hs = vec2(radius);
    vec2 vp_size = vec2(renderer->render_info->viewport_width, renderer->render_info->viewport_height);
    vec2 p0 = (center + vec2(-hs.x, -hs.y)) / vp_size * vec2(2) - vec2(1);
    vec2 p1 = (center + vec2(hs.x, -hs.y))  / vp_size * vec2(2) - vec2(1);
    vec2 p2 = (center + vec2(hs.x, hs.y))   / vp_size * vec2(2) - vec2(1);
    vec2 p3 = (center + vec2(-hs.x, hs.y))  / vp_size * vec2(2) - vec2(1);

    mesh_cpu_buffer_dynamic_add_face(&renderer->mesh_circles, 0, 1, 2);
    mesh_cpu_buffer_dynamic_add_face(&renderer->mesh_circles, 0, 2, 3);
    mesh_cpu_buffer_dynamic_add_vertex(&renderer->mesh_circles, vertex_circle_make(vec3(p0, depth), color, vec2(0.0f, 0.0f), radius));
    mesh_cpu_buffer_dynamic_add_vertex(&renderer->mesh_circles, vertex_circle_make(vec3(p1, depth), color, vec2(1.0f, 0.0f), radius));
    mesh_cpu_buffer_dynamic_add_vertex(&renderer->mesh_circles, vertex_circle_make(vec3(p2, depth), color, vec2(1.0f, 1.0f), radius));
    mesh_cpu_buffer_dynamic_add_vertex(&renderer->mesh_circles, vertex_circle_make(vec3(p3, depth), color, vec2(0.0f, 1.0f), radius));
}

void primitive_renderer_2D_add_line(Primitive_Renderer_2D* renderer, vec2 start, vec2 end, Line_Cap start_cap, Line_Cap end_cap, float thickness, float depth, vec3 color)
{
    vec2 direction = vector_normalize_safe(end - start);
    vec2 normal = vector_rotate_90_degree_counter_clockwise(direction);
    vec2 vp_size = vec2(renderer->render_info->viewport_width, renderer->render_info->viewport_height);
    if (start_cap == Line_Cap::SQUARE) {
        start = start - direction * thickness / 2.0f;
    }
    if (end_cap == Line_Cap::SQUARE) {
        end = end + direction * thickness / 2.0f;
    }
    float length = vector_length(end - start);

    float t = math_maximum(thickness, 1.0f);
    thickness = thickness * 2.0f;
    t = thickness;
    float uv_multiplier = 1.0f;

    vec2 p0 = (start - normal * t / 2.0f) / vp_size * vec2(2) - vec2(1);
    vec2 p1 = (end   - normal * t / 2.0f) / vp_size * vec2(2) - vec2(1);
    vec2 p2 = (end   + normal * t / 2.0f) / vp_size * vec2(2) - vec2(1);
    vec2 p3 = (start + normal * t / 2.0f) / vp_size * vec2(2) - vec2(1);

    mesh_cpu_buffer_dynamic_add_face(&renderer->mesh_lines, 0, 1, 2);
    mesh_cpu_buffer_dynamic_add_face(&renderer->mesh_lines, 0, 2, 3);
    mesh_cpu_buffer_dynamic_add_vertex(&renderer->mesh_lines, vertex_line_make(vec3(p0, depth), color, vec2(0.0f, 0.0f) * uv_multiplier, thickness, length));
    mesh_cpu_buffer_dynamic_add_vertex(&renderer->mesh_lines, vertex_line_make(vec3(p1, depth), color, vec2(1.0f, 0.0f) * uv_multiplier, thickness, length));
    mesh_cpu_buffer_dynamic_add_vertex(&renderer->mesh_lines, vertex_line_make(vec3(p2, depth), color, vec2(1.0f, 1.0f) * uv_multiplier, thickness, length));
    mesh_cpu_buffer_dynamic_add_vertex(&renderer->mesh_lines, vertex_line_make(vec3(p3, depth), color, vec2(0.0f, 1.0f) * uv_multiplier, thickness, length));

    if (start_cap == Line_Cap::ROUND) {
        primitive_renderer_2D_add_circle(renderer, start, thickness/2.0f - 1.0f, depth, color);
    }
    if (end_cap == Line_Cap::ROUND) {
        primitive_renderer_2D_add_circle(renderer, end, thickness/2.0f - 0.5f, depth, color);
    }
}

void primitive_renderer_2D_render(Primitive_Renderer_2D* renderer, Rendering_Core* core)
{
    mesh_cpu_buffer_dynamic_upload_data(&renderer->mesh_rectangles, core);
    shader_program_draw_mesh(renderer->shader_rectangles, &renderer->mesh_rectangles.mesh, core, {});
    mesh_cpu_buffer_dynamic_upload_data(&renderer->mesh_circles, core);
    shader_program_draw_mesh(renderer->shader_circles, &renderer->mesh_circles.mesh, core, {});
    mesh_cpu_buffer_dynamic_upload_data(&renderer->mesh_lines, core);
    shader_program_draw_mesh(renderer->shader_lines, &renderer->mesh_lines.mesh, core, {});
}

void primitive_renderer_2D_start_line_train(Primitive_Renderer_2D* renderer, vec3 color, float depth)
{
    renderer->line_train_color = color;
    renderer->line_train_depth = depth;
    dynamic_array_reset(&renderer->line_train_points);
}

void primitive_renderer_2D_add_line_train_point(Primitive_Renderer_2D* renderer, vec2 position, float thickness)
{
    Line_Train_Point point;
    point.position = position;
    point.thickness = thickness;
    dynamic_array_push_back(&renderer->line_train_points, point);
}

void primitive_renderer_2D_end_line_train(Primitive_Renderer_2D* renderer)
{
    for (int i = 0; i < renderer->line_train_points.size-1; i++) {
        primitive_renderer_2D_add_line(renderer, renderer->line_train_points[i].position, renderer->line_train_points[i + 1].position,
            Line_Cap::ROUND, Line_Cap::ROUND,
            renderer->line_train_points[i].thickness, renderer->line_train_depth, renderer->line_train_color);
    }
    dynamic_array_reset(&renderer->line_train_points);
}
