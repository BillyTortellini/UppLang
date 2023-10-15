#pragma once

#include "../math/vectors.hpp"

#include "glyph_atlas.hpp"
#include "texture.hpp"
#include "rendering_core.hpp"
#include "basic2D.hpp"

struct Text_Renderer
{
    Glyph_Atlas glyph_atlas;
    Texture* atlas_bitmap_texture;
    Texture* atlas_sdf_texture;
    Vertex_Attribute<float>* attrib_pixel_size;
    Mesh* text_mesh;
    int last_batch_end; // Note, in character-count, so to get index you need * 6, and for vertex * 4 
    int current_batch_end;
};

Text_Renderer* text_renderer_create_from_font_atlas_file(const char* font_filepath);
void text_renderer_destroy(Text_Renderer* renderer);

void text_renderer_reset(Text_Renderer* renderer);
void text_renderer_add_text(
    Text_Renderer* renderer, 
    String text, 
    vec2 position, 
    Anchor anchor, 
    float line_height,
    vec3 color,
    Optional<Bounding_Box2> clip_box = optional_make_failure<Bounding_Box2>()
);
void text_renderer_draw(Text_Renderer* renderer, Render_Pass* render_pass);
float text_renderer_character_width(Text_Renderer* renderer, float line_height);
