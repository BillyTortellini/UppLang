#pragma once

#include "../math/umath.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../datastructures/string.hpp"
#include "rendering_core.hpp"

struct Shader_Program;
struct Text_Renderer;
struct Render_Pass;

struct Renderer_2D
{
    Text_Renderer* text_renderer;
    Pipeline_State pipeline_state;
    Mesh* mesh;
    String string_buffer;
    vec2 scaling_factor;
    float to_pixel_scaling;

    int batch_start;
    int batch_size;
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

Renderer_2D* renderer_2D_create(Text_Renderer* text_renderer);
void renderer_2D_destroy(Renderer_2D* renderer);
void renderer_2D_draw(Renderer_2D* renderer, Render_Pass* render_pass);
void renderer_2D_reset(Renderer_2D* renderer);

void renderer_2D_add_rectangle(Renderer_2D* renderer, vec2 pos, vec2 size, vec3 color, float depth);
void renderer_2D_add_rect_outline(Renderer_2D* renderer, vec2 pos, vec2 size, vec3 color, float thickness, float depth);
void renderer_2D_add_line(Renderer_2D* renderer, vec2 start, vec2 end, vec3 color, float thickness, float depth);
void renderer_2D_add_text_in_box(Renderer_2D* renderer, String* text, float text_height, vec3 color, vec2 pos, vec2 size,
    Text_Alignment_Horizontal align_h, Text_Alignment_Vertical align_v, Text_Wrapping_Mode wrapping_mode);
