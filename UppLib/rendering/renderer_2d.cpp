#include "renderer_2d.hpp"

void renderer_2d_update_window_size(Renderer_2D* renderer, int window_width, int window_height) {
    int smallest_dim = 0;
    vec2& scaling_factor = renderer->scaling_factor;
    if (window_width > window_height) {
        scaling_factor.x = (float)window_height / window_width;
        scaling_factor.y = 1.0f;
        smallest_dim = window_height;
    }
    else {
        scaling_factor.y = (float)window_width / window_height;
        scaling_factor.x = 1.0f;
        smallest_dim = window_width;
    }
    renderer->to_pixel_scaling = 2.0f / (float)smallest_dim;
}

Renderer_2D renderer_2d_create(OpenGLState* state, FileListener* listener, TextRenderer* text_renderer, int window_width, int window_height)
{
    Renderer_2D result;
    result.text_renderer = text_renderer;
    result.geometry_data = dynamic_array_create_empty<Geometry_2D_Vertex>(64);
    result.index_data = dynamic_array_create_empty<uint32>(64);
    result.shader_2d = optional_unwrap(shader_program_create(listener, "resources/shaders/geometry_2d.glsl"));
    vertex_attribute_information_maker_reset();
    vertex_attribute_information_maker_add(0, VertexAttributeInformationType::VEC3);
    vertex_attribute_information_maker_add(1, VertexAttributeInformationType::VEC3);
    result.geometry = mesh_gpu_data_create(
        state,
        gpu_buffer_create_empty(sizeof(Geometry_2D_Vertex) * 128, GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW),
        vertex_attribute_information_maker_make(),
        gpu_buffer_create_empty(sizeof(uint32) * 128, GL_ELEMENT_ARRAY_BUFFER, GL_DYNAMIC_DRAW),
        GL_TRIANGLES,
        0
    );
    result.string_buffer = string_create_empty(256);
    renderer_2d_update_window_size(&result, window_width, window_height);
    return result;
}

void renderer_2d_destroy(Renderer_2D* renderer)
{
    dynamic_array_destroy(&renderer->geometry_data);
    dynamic_array_destroy(&renderer->index_data);
    shader_program_destroy(renderer->shader_2d);
    mesh_gpu_data_destroy(&renderer->geometry);
    string_destroy(&renderer->string_buffer);
}

Geometry_2D_Vertex geometry_2d_vertex_make(vec3 pos, vec3 color) {
    Geometry_2D_Vertex v;
    v.pos = pos;
    v.color = color;
    return v;
}

void renderer_2d_draw_rectangle(Renderer_2D* renderer, vec2 pos, vec2 size, vec3 color, float depth) 
{
    vec2 p0 = (pos + vec2(-size.x, -size.y) / 2.0f) * renderer->scaling_factor;
    vec2 p1 = (pos + vec2(size.x, -size.y) / 2.0f) * renderer->scaling_factor;
    vec2 p2 = (pos + vec2(size.x, size.y) / 2.0f) * renderer->scaling_factor;
    vec2 p3 = (pos + vec2(-size.x, size.y) / 2.0f) * renderer->scaling_factor;

    dynamic_array_push_back(&renderer->geometry_data, geometry_2d_vertex_make(vec3(p0, depth), color));
    dynamic_array_push_back(&renderer->geometry_data, geometry_2d_vertex_make(vec3(p1, depth), color));
    dynamic_array_push_back(&renderer->geometry_data, geometry_2d_vertex_make(vec3(p2, depth), color));
    dynamic_array_push_back(&renderer->geometry_data, geometry_2d_vertex_make(vec3(p3, depth), color));

    dynamic_array_push_back(&renderer->index_data, (uint32)(renderer->geometry_data.size - 4));
    dynamic_array_push_back(&renderer->index_data, (uint32)(renderer->geometry_data.size - 3));
    dynamic_array_push_back(&renderer->index_data, (uint32)(renderer->geometry_data.size - 2));
    dynamic_array_push_back(&renderer->index_data, (uint32)(renderer->geometry_data.size - 4));
    dynamic_array_push_back(&renderer->index_data, (uint32)(renderer->geometry_data.size - 2));
    dynamic_array_push_back(&renderer->index_data, (uint32)(renderer->geometry_data.size - 1));
}

void renderer_2d_draw_line(Renderer_2D* renderer, vec2 start, vec2 end, vec3 color, float thickness, float depth)
{
    vec2 a_to_b = vector_normalize_safe(end - start);
    vec2 normal = vector_rotate_90_degree_counter_clockwise(a_to_b);
    thickness = thickness * renderer->to_pixel_scaling / 2.0f;
    vec2 p0 = (start + (-normal - a_to_b) * thickness) * renderer->scaling_factor;
    vec2 p1 = (end + (-normal + a_to_b) * thickness) * renderer->scaling_factor;
    vec2 p2 = (end + (normal + a_to_b) * thickness) * renderer->scaling_factor;
    vec2 p3 = (start + (normal - a_to_b) * thickness) * renderer->scaling_factor;

    dynamic_array_push_back(&renderer->geometry_data, geometry_2d_vertex_make(vec3(p0, depth), color));
    dynamic_array_push_back(&renderer->geometry_data, geometry_2d_vertex_make(vec3(p1, depth), color));
    dynamic_array_push_back(&renderer->geometry_data, geometry_2d_vertex_make(vec3(p2, depth), color));
    dynamic_array_push_back(&renderer->geometry_data, geometry_2d_vertex_make(vec3(p3, depth), color));

    dynamic_array_push_back(&renderer->index_data, (uint32)(renderer->geometry_data.size - 4));
    dynamic_array_push_back(&renderer->index_data, (uint32)(renderer->geometry_data.size - 3));
    dynamic_array_push_back(&renderer->index_data, (uint32)(renderer->geometry_data.size - 2));
    dynamic_array_push_back(&renderer->index_data, (uint32)(renderer->geometry_data.size - 4));
    dynamic_array_push_back(&renderer->index_data, (uint32)(renderer->geometry_data.size - 2));
    dynamic_array_push_back(&renderer->index_data, (uint32)(renderer->geometry_data.size - 1));
}

void renderer_2d_draw_rect_outline(Renderer_2D* renderer, vec2 pos, vec2 size, vec3 color, float thickness, float depth)
{
    vec2 p0 = pos + vec2(-size.x, -size.y) / 2.0f + vec2(-1,-1) * thickness * renderer->to_pixel_scaling / 2.0f;
    vec2 p1 = pos + vec2(size.x, -size.y) / 2.0f + vec2(1, -1) * thickness * renderer->to_pixel_scaling / 2.0f;
    vec2 p2 = pos + vec2(size.x, size.y) / 2.0f + vec2(1, 1) * thickness * renderer->to_pixel_scaling / 2.0f;
    vec2 p3 = pos + vec2(-size.x, size.y) / 2.0f + vec2(-1, 1) * thickness * renderer->to_pixel_scaling / 2.0f;
    renderer_2d_draw_line(renderer, p0, p1, color, thickness, depth);
    renderer_2d_draw_line(renderer, p1, p2, color, thickness, depth);
    renderer_2d_draw_line(renderer, p2, p3, color, thickness, depth);
    renderer_2d_draw_line(renderer, p3, p0, color, thickness, depth);
}

void renderer_2d_draw(Renderer_2D* renderer, OpenGLState* state)
{
    // Set state
    opengl_state_set_depth_testing(state, true, true, GL_LESS);
    glClear(GL_DEPTH_BUFFER_BIT);
    opengl_state_set_blending_state(state, false, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_FUNC_ADD);
    // Upload data
    gpu_buffer_update(&renderer->geometry.vertex_buffers[0].vertex_buffer, array_as_bytes(&dynamic_array_to_array(&renderer->geometry_data)));
    mesh_gpu_data_update_index_buffer(&renderer->geometry, dynamic_array_to_array(&renderer->index_data), state);
    dynamic_array_reset(&renderer->geometry_data);
    dynamic_array_reset(&renderer->index_data);
    // Draw
    mesh_gpu_data_draw_with_shader_program(&renderer->geometry, renderer->shader_2d, state);
    text_renderer_render(renderer->text_renderer, state);
}

void renderer_2d_draw_text_in_box(Renderer_2D* renderer, String* text, float text_height, vec3 color, vec2 pos, vec2 size,
    ALIGNMENT_HORIZONTAL::ENUM align_h, ALIGNMENT_VERTICAL::ENUM align_v, TEXT_WRAPPING_MODE::ENUM wrapping_mode)
{
    // Do text wrapping if necessary
    //text_height *= renderer->text_height_scaling;
    vec2 text_size(text_renderer_calculate_text_width(renderer->text_renderer, text->size, text_height), text_height);
    String* result = text; // Required for CUTOFF to set size to another value
    if (wrapping_mode == TEXT_WRAPPING_MODE::SCALE_DOWN) {
        float required_scaling_y = size.y / text_height;
        float required_scaling_x = size.x / text_size.x;
        float scaling = math_minimum(1.0f, math_minimum(required_scaling_x, required_scaling_y));
        text_size = text_size * scaling;
    }
    else if (wrapping_mode == TEXT_WRAPPING_MODE::CUTOFF) {
        if (text_size.x > size.x) {
            float char_width = text_renderer_calculate_text_width(renderer->text_renderer, 1, text_size.y);
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
    else if (wrapping_mode == TEXT_WRAPPING_MODE::OVERDRAW) {
        // Do nothing on overdraw :)
    }
    else {
        panic("Wrong wrapping mode");
    }

    // Calculate text pos
    vec2 text_pos;
    if (align_v == ALIGNMENT_VERTICAL::BOTTOM) text_pos.y = pos.y - size.y / 2.0f;
    else if (align_v == ALIGNMENT_VERTICAL::CENTER) text_pos.y = pos.y - text_size.y / 2.0f;
    else text_pos.y = pos.y + size.y / 2.0f - text_size.y;

    if (align_h == ALIGNMENT_HORIZONTAL::LEFT) text_pos.x = pos.x - size.x / 2.0f;
    else if (align_h == ALIGNMENT_HORIZONTAL::CENTER) text_pos.x = pos.x - text_size.x / 2.0f;
    else text_pos.x = pos.x + size.x / 2.0f - text_size.x;

    text_renderer_set_color(renderer->text_renderer, color);
    text_renderer_add_text(renderer->text_renderer, result, text_pos * renderer->scaling_factor, text_size.y * renderer->scaling_factor.y, 0.0f);
}
