#pragma once

#include "../datastructures/array.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../utility/utils.hpp"

#include "texture.hpp"

struct GlyphInformation
{
    unsigned char character;
    // The following measurements are in 23.3 pixels (1/64th of a pixel)
    int advance_x; 
    int bearing_x; 
    int bearing_y;
    int glyph_width;
    int glyph_height;
    // The following are normalized uv coordinates
    float atlas_fragcoords_bottom;
    float atlas_fragcoords_left;
    float atlas_fragcoords_top;
    float atlas_fragcoords_right;
};

struct GlyphAtlas
{
    Texture_Bitmap atlas_bitmap;
    Array<float> atlas_distance_field;
    int ascender;
    int descender;
    int cursor_advance;
    DynamicArray<GlyphInformation> glyph_informations;
    Array<int> character_to_glyph_map;
};

Optional<GlyphAtlas> glyph_atlas_create_from_font_file(
    const char* font_filepath,
    int max_character_pixel_size,
    int atlas_size,
    int padding,
    int character_margin,
    bool render_antialiased
);
Optional<GlyphAtlas> glyph_atlas_create_from_atlas_file(const char* altas_filepath);

void glyph_atlas_save_as_file(GlyphAtlas* altas, const char* filepath);
void glyph_atlas_print_glyph_information(GlyphAtlas* atlas);
void glyph_atlas_destroy(GlyphAtlas* atlas);

