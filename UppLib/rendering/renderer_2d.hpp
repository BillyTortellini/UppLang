#pragma once

#include "../math/umath.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../datastructures/string.hpp"
#include "gpu_buffers.hpp"
#include "rendering_core.hpp"

struct Shader_Program;
struct Text_Renderer;

struct Geometry_2D_Vertex {
    vec3 pos;
    vec3 color; // RGB
};

Geometry_2D_Vertex geometry_2d_vertex_make(vec3 pos, vec3 color);

struct Renderer_2D
{
    Mesh_GPU_Buffer geometry;
    Dynamic_Array<Geometry_2D_Vertex> geometry_data;
    Dynamic_Array<uint32> index_data;
    Shader_Program* shader_2d;
    Text_Renderer* text_renderer;
    Pipeline_State pipeline_state;
    String string_buffer;
    vec2 scaling_factor;
    float to_pixel_scaling;
};

enum class Text_Alignment_Horizontal
{
    LEFT, 
    RIGHT, 
    CENTER
};

enum class Text_Alignment_Vertical
{
    BOTTOM, 
    TOP, 
    CENTER
};

enum class Text_Wrapping_Mode
{
    CUTOFF, 
    OVERDRAW, 
    SCALE_DOWN
};

Renderer_2D* renderer_2D_create(Rendering_Core* core, Text_Renderer* text_renderer);
void renderer_2D_destroy(Renderer_2D* renderer, Rendering_Core* core);

void renderer_2D_render(Renderer_2D* renderer, Rendering_Core* core);
void renderer_2D_add_rectangle(Renderer_2D* renderer, vec2 pos, vec2 size, vec3 color, float depth);
void renderer_2D_add_rect_outline(Renderer_2D* renderer, vec2 pos, vec2 size, vec3 color, float thickness, float depth);
void renderer_2D_add_line(Renderer_2D* renderer, vec2 start, vec2 end, vec3 color, float thickness, float depth);
void renderer_2D_add_text_in_box(Renderer_2D* renderer, String* text, float text_height, vec3 color, vec2 pos, vec2 size,
    Text_Alignment_Horizontal align_h, Text_Alignment_Vertical align_v, Text_Wrapping_Mode wrapping_mode);
