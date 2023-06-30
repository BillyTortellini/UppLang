#include "renderer_2D.hpp"

#include "text_renderer.hpp"
#include "rendering_core.hpp"

void renderer_2D_update_window_size(void* userdata) 
{
    Renderer_2D* renderer = (Renderer_2D*) userdata;
    int smallest_dim = 0;
    vec2& scaling_factor = renderer->scaling_factor;
    auto width = rendering_core.render_information.backbuffer_width;
    auto height = rendering_core.render_information.backbuffer_height;
    if (width > height) {
        scaling_factor.x = (float)height / width;
        scaling_factor.y = 1.0f;
        smallest_dim = height;
    }
    else {
        scaling_factor.y = (float)width / height;
        scaling_factor.x = 1.0f;
        smallest_dim = width;
    }
    renderer->to_pixel_scaling = 2.0f / (float)smallest_dim;
}

Renderer_2D* renderer_2D_create(Text_Renderer* text_renderer)
{
    Renderer_2D* result = new Renderer_2D();
    result->text_renderer = text_renderer;
    result->string_buffer = string_create_empty(256);

    renderer_2D_update_window_size(&result);
    rendering_core_add_window_size_listener(&renderer_2D_update_window_size, result);

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
    rendering_core_remove_window_size_listener(renderer);
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

void renderer_2D_add_rectangle(Renderer_2D* renderer, vec2 pos, vec2 size, vec3 color, float depth) 
{
    vec2 p0 = (pos + vec2(-size.x, -size.y) / 2.0f) * renderer->scaling_factor;
    vec2 p1 = (pos + vec2(size.x, -size.y) / 2.0f) * renderer->scaling_factor;
    vec2 p2 = (pos + vec2(size.x, size.y) / 2.0f) * renderer->scaling_factor;
    vec2 p3 = (pos + vec2(-size.x, size.y) / 2.0f) * renderer->scaling_factor;

    auto predef = rendering_core.predefined;
    mesh_push_indices(renderer->mesh, { 0, 1, 2, 0, 2, 3 }, true);
    mesh_push_attribute(renderer->mesh, predef.color3, { color, color, color, color });
    mesh_push_attribute(renderer->mesh, predef.position2D, {p0, p1, p2, p3});
    renderer->batch_size += 6;
}

void renderer_2D_add_line(Renderer_2D* renderer, vec2 start, vec2 end, vec3 color, float thickness, float depth)
{
    vec2 a_to_b = vector_normalize_safe(end - start);
    vec2 normal = vector_rotate_90_degree_counter_clockwise(a_to_b);
    thickness = thickness * renderer->to_pixel_scaling / 2.0f;
    vec2 p0 = (start + (-normal - a_to_b) * thickness) * renderer->scaling_factor;
    vec2 p1 = (end + (-normal + a_to_b) * thickness) * renderer->scaling_factor;
    vec2 p2 = (end + (normal + a_to_b) * thickness) * renderer->scaling_factor;
    vec2 p3 = (start + (normal - a_to_b) * thickness) * renderer->scaling_factor;

    auto predef = rendering_core.predefined;
    mesh_push_indices(renderer->mesh, { 0, 1, 2, 0, 2, 3 }, true);
    mesh_push_attribute(renderer->mesh, predef.color3, { color, color, color, color });
    mesh_push_attribute(renderer->mesh, predef.position2D, {p0, p1, p2, p3});
    renderer->batch_size += 6;
}

void renderer_2D_add_rect_outline(Renderer_2D* renderer, vec2 pos, vec2 size, vec3 color, float thickness, float depth)
{
    vec2 p0 = pos + vec2(-size.x, -size.y) / 2.0f + vec2(-1,-1) * thickness * renderer->to_pixel_scaling / 2.0f;
    vec2 p1 = pos + vec2(size.x, -size.y) / 2.0f + vec2(1, -1) * thickness * renderer->to_pixel_scaling / 2.0f;
    vec2 p2 = pos + vec2(size.x, size.y) / 2.0f + vec2(1, 1) * thickness * renderer->to_pixel_scaling / 2.0f;
    vec2 p3 = pos + vec2(-size.x, size.y) / 2.0f + vec2(-1, 1) * thickness * renderer->to_pixel_scaling / 2.0f;
    renderer_2D_add_line(renderer, p0, p1, color, thickness, depth);
    renderer_2D_add_line(renderer, p1, p2, color, thickness, depth);
    renderer_2D_add_line(renderer, p2, p3, color, thickness, depth);
    renderer_2D_add_line(renderer, p3, p0, color, thickness, depth);
}

void renderer_2D_add_text_in_box(Renderer_2D* renderer, String* text, float text_height, vec3 color, vec2 pos, vec2 size,
    Text_Alignment_Horizontal align_h, Text_Alignment_Vertical align_v, Text_Wrapping_Mode wrapping_mode)
{
    // Do text wrapping if necessary
    //text_height *= renderer->text_height_scaling;
    vec2 text_size(text_renderer_line_width(renderer->text_renderer, text->size, text_height), text_height);
    String* result = text; // Required for CUTOFF to set size to another value
    if (wrapping_mode == Text_Wrapping_Mode::SCALE_DOWN) {
        float required_scaling_y = size.y / text_height;
        float required_scaling_x = size.x / text_size.x;
        float scaling = math_minimum(1.0f, math_minimum(required_scaling_x, required_scaling_y));
        text_size = text_size * scaling;
    }
    else if (wrapping_mode == Text_Wrapping_Mode::CUTOFF) {
        if (text_size.x > size.x) {
            float char_width = text_renderer_line_width(renderer->text_renderer, 1, text_size.y);
            int fitting_char_count = (int)(size.x / char_width);
            string_set_characters(&renderer->string_buffer, text->characters);
            result = &renderer->string_buffer;
            result->size = fitting_char_count;
            for (int i = math_maximum(0, result->size - 2); i < result->size; i++) {
                result->characters[i] = '.';
            }
            text_size.x = char_width * fitting_char_count;
        }
    }
    else if (wrapping_mode == Text_Wrapping_Mode::OVERDRAW) {
        // Do nothing on overdraw :)
    }
    else {
        panic("Wrong wrapping mode");
    }

    // Calculate text pos
    vec2 text_pos;
    if (align_v == Text_Alignment_Vertical::BOTTOM) text_pos.y = pos.y - size.y / 2.0f;
    else if (align_v == Text_Alignment_Vertical::CENTER) text_pos.y = pos.y - text_size.y / 2.0f;
    else text_pos.y = pos.y + size.y / 2.0f - text_size.y;

    if (align_h == Text_Alignment_Horizontal::LEFT) text_pos.x = pos.x - size.x / 2.0f;
    else if (align_h == Text_Alignment_Horizontal::CENTER) text_pos.x = pos.x - text_size.x / 2.0f;
    else text_pos.x = pos.x + size.x / 2.0f - text_size.x;

    panic("TODO");
    // text_renderer_set_color(renderer->text_renderer, color);
    // text_renderer_add_text(renderer->text_renderer, result, text_pos * renderer->scaling_factor, text_size.y * renderer->scaling_factor.y, 0.0f);
}
