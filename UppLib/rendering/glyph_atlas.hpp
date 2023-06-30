#pragma once

#include "../datastructures/array.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../utility/utils.hpp"
#include "texture_bitmap.hpp"

struct Glyph_Information
{
    unsigned char character;
    // The following measurements are in 23.3 pixels (1/64th of a pixel on normal dpi)
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

struct Glyph_Atlas
{
    // Atlas Data
    Texture_Bitmap atlas_bitmap;
    Array<float> atlas_distance_field;

    // Glyph Information
    int ascender;
    int descender;
    int cursor_advance; // For fonts with uniform character width
    Dynamic_Array<Glyph_Information> glyph_informations;
    Array<int> character_to_glyph_map;
};

Optional<Glyph_Atlas> glyph_atlas_create_from_font_file(
    const char* font_filepath,
    int max_character_pixel_size,
    int atlas_size,
    int padding,
    int character_margin,
    bool render_antialiased
);
Optional<Glyph_Atlas> glyph_atlas_create_from_atlas_file(const char* altas_filepath);

void glyph_atlas_save_as_file(Glyph_Atlas* altas, const char* filepath);
void glyph_atlas_print_glyph_information(Glyph_Atlas* atlas);
void glyph_atlas_destroy(Glyph_Atlas* atlas);

