#pragma once

#include "../math/umath.hpp"
#include "mesh_gpu_data.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "text_renderer.hpp"

struct Geometry_2D_Vertex {
    vec3 pos;
    vec3 color; // RGB
};

Geometry_2D_Vertex geometry_2d_vertex_make(vec3 pos, vec3 color);

struct Renderer_2D
{
    Mesh_GPU_Data geometry;
    DynamicArray<Geometry_2D_Vertex> geometry_data;
    DynamicArray<uint32> index_data;
    ShaderProgram* shader_2d;
    TextRenderer* text_renderer;
    String string_buffer;
    vec2 scaling_factor;
    float to_pixel_scaling;
};

namespace ALIGNMENT_HORIZONTAL
{
    enum ENUM {
        LEFT, RIGHT, CENTER
    };
}

namespace ALIGNMENT_VERTICAL
{
    enum ENUM {
        BOTTOM, TOP, CENTER
    };
}

namespace TEXT_WRAPPING_MODE
{
    enum ENUM {
        CUTOFF, OVERDRAW, SCALE_DOWN
    };
}

Renderer_2D renderer_2d_create(OpenGLState* state, FileListener* listener, TextRenderer* text_renderer, int window_width, int window_height);
void renderer_2d_destroy(Renderer_2D* renderer);

void renderer_2d_draw(Renderer_2D* renderer, OpenGLState* state);
void renderer_2d_update_window_size(Renderer_2D* renderer, int window_width, int window_height);

void renderer_2d_draw_rectangle(Renderer_2D* renderer, vec2 pos, vec2 size, vec3 color, float depth);
void renderer_2d_draw_rect_outline(Renderer_2D* renderer, vec2 pos, vec2 size, vec3 color, float thickness, float depth);
void renderer_2d_draw_line(Renderer_2D* renderer, vec2 start, vec2 end, vec3 color, float thickness, float depth);
void renderer_2d_draw_text_in_box(Renderer_2D* renderer, String* text, float text_height, vec3 color, vec2 pos, vec2 size,
    ALIGNMENT_HORIZONTAL::ENUM align_h, ALIGNMENT_VERTICAL::ENUM align_v, TEXT_WRAPPING_MODE::ENUM wrapping_mode);
