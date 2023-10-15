#include "renderer_2D.hpp"

#include "text_renderer.hpp"
#include "rendering_core.hpp"

Renderer_2D* renderer_2D_create(Text_Renderer* text_renderer)
{
    Renderer_2D* result = new Renderer_2D();
    result->text_renderer = text_renderer;
    result->string_buffer = string_create_empty(256);

    auto& predef = rendering_core.predefined;
    result->mesh = rendering_core_query_mesh(
        "renderer 2d mesh buffer",
        vertex_description_create({ predef.index, predef.position2D, predef.color3 }), 
        true
    );

    result->pipeline_state = pipeline_state_make_default();
    result->pipeline_state.blending_state.blending_enabled = true;
    result->pipeline_state.depth_state.test_type = Depth_Test_Type::IGNORE_DEPTH;
    result->pipeline_state.culling_state.culling_enabled = false;

    renderer_2D_reset(result);

    return result;
}

void renderer_2D_destroy(Renderer_2D* renderer)
{
    string_destroy(&renderer->string_buffer);
    delete renderer;
}

void renderer_2D_reset(Renderer_2D* renderer) {
    renderer->batch_start = 0;
    renderer->batch_size = 0; 
}

void renderer_2D_draw(Renderer_2D* renderer, Render_Pass* render_pass) {
    if (renderer->batch_size == 0) {
        return;
    }
    auto shader_2d = rendering_core_query_shader("core/geometry_2d.glsl");
    render_pass_draw_count(
        render_pass, shader_2d, renderer->mesh, Mesh_Topology::TRIANGLES, {}, renderer->batch_start, renderer->batch_size);

    renderer->batch_start += renderer->batch_size;
    renderer->batch_size = 0;
}

void renderer_2D_add_rectangle(Renderer_2D* renderer, Bounding_Box2 box, vec3 color) 
{
    box.min = convertPointFromTo(box.min, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
    box.max = convertPointFromTo(box.max, Unit::PIXELS, Unit::NORMALIZED_SCREEN);

    auto predef = rendering_core.predefined;
    mesh_push_indices(renderer->mesh, { 0, 1, 2, 0, 2, 3 }, true);
    mesh_push_attribute(renderer->mesh, predef.color3, { color, color, color, color });
    mesh_push_attribute(renderer->mesh, predef.position2D, {box.min, vec2(box.max.x, box.min.y), box.max, vec2(box.min.x, box.max.y)});
    renderer->batch_size += 6;
}

void renderer_2D_add_line(Renderer_2D* renderer, vec2 start, vec2 end, vec3 color, float thickness)
{
    vec2 a_to_b = vector_normalize_safe(end - start);
    vec2 normal = vector_rotate_90_degree_counter_clockwise(a_to_b);
    thickness /= 2.0f;
    vec2 p0 = (start + (-normal - a_to_b) * thickness);
    vec2 p1 = (end + (-normal + a_to_b) * thickness);
    vec2 p2 = (end + (normal + a_to_b) * thickness);
    vec2 p3 = (start + (normal - a_to_b) * thickness);
    
    p0 = convertPointFromTo(p0, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
    p1 = convertPointFromTo(p1, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
    p2 = convertPointFromTo(p2, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
    p3 = convertPointFromTo(p3, Unit::PIXELS, Unit::NORMALIZED_SCREEN);

    auto predef = rendering_core.predefined;
    mesh_push_indices(renderer->mesh, { 0, 1, 2, 0, 2, 3 }, true);
    mesh_push_attribute(renderer->mesh, predef.color3, { color, color, color, color });
    mesh_push_attribute(renderer->mesh, predef.position2D, {p0, p1, p2, p3});
    renderer->batch_size += 6;
}

void renderer_2D_add_rect_outline(Renderer_2D* renderer, vec2 pos, vec2 size, vec3 color, float thickness)
{
    vec2 p0 = pos + vec2(-size.x, -size.y) / 2.0f + vec2(-1,-1) * thickness / 2.0f;
    vec2 p1 = pos + vec2(size.x, -size.y) / 2.0f + vec2(1, -1) * thickness / 2.0f;
    vec2 p2 = pos + vec2(size.x, size.y) / 2.0f + vec2(1, 1) * thickness / 2.0f;
    vec2 p3 = pos + vec2(-size.x, size.y) / 2.0f + vec2(-1, 1) * thickness / 2.0f;

    p0 = convertPointFromTo(p0, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
    p1 = convertPointFromTo(p1, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
    p2 = convertPointFromTo(p2, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
    p3 = convertPointFromTo(p3, Unit::PIXELS, Unit::NORMALIZED_SCREEN);

    renderer_2D_add_line(renderer, p0, p1, color, thickness);
    renderer_2D_add_line(renderer, p1, p2, color, thickness);
    renderer_2D_add_line(renderer, p2, p3, color, thickness);
    renderer_2D_add_line(renderer, p3, p0, color, thickness);
}
