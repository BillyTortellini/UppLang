#pragma once

#include "../math/vectors.hpp"
#include "../utility/bounding_box.hpp"

#include "glyph_atlas.hpp"
#include "texture_2D.hpp"
#include "gpu_buffers.hpp"
#include "rendering_core.hpp"

struct Shader_Program;
struct String;

struct Character_Position
{
    BoundingBox2 bounding_box;
    Glyph_Information* glyph_info;
    vec3 color;
};

struct Text_Layout
{
    Dynamic_Array<Character_Position> character_positions;
    vec2 size;
    float relative_height;
};

Text_Layout text_layout_create();
void text_layout_destroy(Text_Layout* info);



struct Font_Vertex
{
    vec2 position;
    vec2 uv_coords;
    vec3 color;
    float pixel_size;
    Font_Vertex(const vec2& pos, const vec2& uv_coords, vec3 color, float pixel_size)
        : position(pos), uv_coords(uv_coords), pixel_size(pixel_size), color(color) {};
    Font_Vertex() {};
};

struct Text_Renderer
{
    // Atlas data
    Glyph_Atlas glyph_atlas;
    Texture_2D atlas_bitmap_texture;
    Texture_2D atlas_sdf_texture;

    // Shaders
    Shader_Program* bitmap_shader;
    Shader_Program* sdf_shader;

    // Gpu data
    Mesh_GPU_Buffer font_mesh;
    Dynamic_Array<Font_Vertex> text_vertices;
    Dynamic_Array<GLuint> text_indices;
    Pipeline_State pipeline_state;

    // Text positioning cache
    Text_Layout text_layout;
    vec3 default_color;

    // Window data
    int screen_width;
    int screen_height;
};

Text_Renderer* text_renderer_create_from_font_atlas_file(
    Rendering_Core* core,
    const char* font_filepath
);
void text_renderer_destroy(Text_Renderer* renderer, Rendering_Core* core);

void text_renderer_render(Text_Renderer* renderer, Rendering_Core* core);
void text_renderer_add_text(Text_Renderer* renderer, String* text, vec2 position, float relative_height, float line_gap_percent);
Text_Layout* text_renderer_calculate_text_layout(Text_Renderer* renderer, String* text, float relative_height, float line_gap_percent);
void text_renderer_add_text_from_layout(Text_Renderer* renderer, Text_Layout* text_layout, vec2 position);

float text_renderer_calculate_text_width(Text_Renderer* renderer, int char_count, float relative_height);
float text_renderer_get_cursor_advance(Text_Renderer* renderer, float relative_height);
void text_renderer_set_color(Text_Renderer* renderer, vec3 color);
Texture_2D* text_renderer_get_texture(Text_Renderer* renderer);
