#pragma once

#include "../math/umath.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../datastructures/string.hpp"
#include "rendering_core.hpp"
#include "basic2D.hpp"

struct Shader_Program;
struct Text_Renderer;
struct Render_Pass;

struct Renderer_2D
{
    Text_Renderer* text_renderer;
    Pipeline_State pipeline_state;
    Mesh* mesh;
    String string_buffer;

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

void renderer_2D_add_rectangle(Renderer_2D* renderer, Bounding_Box2 box, vec3 color);
void renderer_2D_add_line(Renderer_2D* renderer, vec2 start, vec2 end, vec3 color, float thickness);
void renderer_2D_add_rect_outline(Renderer_2D* renderer, vec2 pos, vec2 size, vec3 color, float thickness);
