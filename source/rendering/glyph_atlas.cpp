#include "glyph_atlas.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include "../utility/binary_parser.hpp"
#include "../datastructures/string.hpp"

void glyph_information_append_to_string(Glyph_Information* information, String* string) 
{
    string_append_formated(
        string, "Character '%c', advance: %5d, bearing x/y: %5d/%5d, width/height: %5d/%5d\n",
        information->character == 0 ? '?' : information->character, information->advance_x, 
        information->bearing_x, information->bearing_y,
        information->glyph_width, information->glyph_height
    );
}

void glyph_information_binary_parser_write(Glyph_Information* information, BinaryParser* parser)
{
    binary_parser_write_int(parser, information->character);
    binary_parser_write_int(parser, information->advance_x);
    binary_parser_write_int(parser, information->bearing_x);
    binary_parser_write_int(parser, information->bearing_y);
    binary_parser_write_int(parser, information->glyph_width);
    binary_parser_write_int(parser, information->glyph_height);
    binary_parser_write_float(parser, information->atlas_fragcoords_bottom);
    binary_parser_write_float(parser, information->atlas_fragcoords_left);
    binary_parser_write_float(parser, information->atlas_fragcoords_right);
    binary_parser_write_float(parser, information->atlas_fragcoords_top);
}

Glyph_Information glyph_information_binary_parser_read(BinaryParser* parser)
{
    Glyph_Information information;
    information.character = binary_parser_read_int(parser);
    information.advance_x = binary_parser_read_int(parser);
    information.bearing_x = binary_parser_read_int(parser);
    information.bearing_y = binary_parser_read_int(parser);
    information.glyph_width = binary_parser_read_int(parser);
    information.glyph_height = binary_parser_read_int(parser);
    information.atlas_fragcoords_bottom = binary_parser_read_float(parser);
    information.atlas_fragcoords_left = binary_parser_read_float(parser);
    information.atlas_fragcoords_right = binary_parser_read_float(parser);
    information.atlas_fragcoords_top = binary_parser_read_float(parser);
    return information;
}

Optional<Glyph_Atlas> glyph_atlas_create_from_font_file(
    const char* font_filepath,
    int max_character_pixel_size,
    int atlas_size,
    int padding,
    int character_margin,
    bool render_antialiased)
{
    Glyph_Atlas result;
    result.character_to_glyph_map = array_create_empty<int>(256);
    result.cursor_advance = 0;
    result.glyph_informations = dynamic_array_create_empty<Glyph_Information>(128);

    // Initialize freetype
    FT_Library library;
    u32 ft_error = FT_Init_FreeType(&library);
    if (ft_error != 0) {
        logg("Could not initialize freetype, error: %s\n", FT_Error_String(ft_error));
        return optional_make_failure<Glyph_Atlas>();
    }
    SCOPE_EXIT(FT_Done_FreeType(library));

    FT_Face face;
    ft_error = FT_New_Face(library, font_filepath, 0, &face);
    if (ft_error != 0) {
        logg("Could not create face for \"%font_filepath\", error: %s\n", font_filepath, FT_Error_String(ft_error));
        return optional_make_failure<Glyph_Atlas>();
    }
    SCOPE_EXIT(FT_Done_Face(face));

    // Set pixel size
    ft_error = FT_Set_Pixel_Sizes(face, 0, max_character_pixel_size);
    if (ft_error != 0) {
        logg("FT_Set_Pixel_Size failed, error: %s\n", FT_Error_String(ft_error));
        return optional_make_failure<Glyph_Atlas>();
    }
    result.ascender = face->size->metrics.ascender;
    result.descender = face->size->metrics.descender;

    // Render all glyphs into bitmap
    Texture_Bitmap atlas_bitmap = texture_bitmap_create_empty_mono(atlas_size, atlas_size, 0);

    for (int i = 0; i < result.character_to_glyph_map.size; i++) {
        // Set all glyph indices to error glyph
        result.character_to_glyph_map.data[i] = 0;
    }

    int atlas_cursor_x = padding;
    int atlas_cursor_y = padding;
    int atlas_max_line_height = 0;
    for (int i = 31; i < result.character_to_glyph_map.size; i++) // Start with first printable ascii character (Space = 32)
    {
        // Get Glyph index
        // WATCH OUT: This is a hack so that 0 is always the unknown glyph index
        unsigned char current_character = i;
        u32 glyph_index;
        if (i == 31) {
            glyph_index = 0;
            current_character = 0;
        }
        else {
            glyph_index = FT_Get_Char_Index(face, current_character);
            if (glyph_index == 0) {
                logg("Glyph %c (#%d) does not exist\n", current_character, i);
                continue;
            }
        }

        // Use FreeType to render glyph
        ft_error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
        if (ft_error != 0) {
            logg("FT_Load_Glyph failed for '%c' (%d): %s\n", current_character, i, FT_Error_String(ft_error));
            continue;
        }

        FT_Render_Mode render_mode = render_antialiased ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO;
        ft_error = FT_Render_Glyph(face->glyph, render_mode);
        if (ft_error != 0) {
            logg("FT_Render_Glyph failed for '%c' (%d): %s\n", current_character, i, FT_Error_String(ft_error));
            continue;
        }

        // Create bitmap from freetype bitmap
        Texture_Bitmap glyph_bitmap;
        if (render_antialiased) {
            glyph_bitmap = texture_bitmap_create_from_data_with_pitch(
                face->glyph->bitmap.width,
                face->glyph->bitmap.rows,
                face->glyph->bitmap.pitch,
                face->glyph->bitmap.buffer
            );
        }
        else {
            glyph_bitmap = texture_bitmap_create_from_bitmap_with_pitch(
                face->glyph->bitmap.width,
                face->glyph->bitmap.rows,
                face->glyph->bitmap.pitch,
                face->glyph->bitmap.buffer
            );
        }
        SCOPE_EXIT(texture_bitmap_destroy(&glyph_bitmap));

        // Create Glyph information
        Glyph_Information information;
        information.character = current_character;
        information.advance_x = face->glyph->metrics.horiAdvance;
        information.bearing_x = face->glyph->metrics.horiBearingX - character_margin*64;
        information.bearing_y = face->glyph->metrics.horiBearingY + character_margin*64;
        information.glyph_width = face->glyph->metrics.width + character_margin*128;
        information.glyph_height = face->glyph->metrics.height + character_margin*128;

        // Calculate position in glyph_atlas
        if (atlas_cursor_x + glyph_bitmap.width + padding >= atlas_bitmap.width) {// Jump to next line if not enough space is in current line
            atlas_cursor_x = padding; // Sets it back to the leftmost pos
            atlas_cursor_y += atlas_max_line_height + padding;
            atlas_max_line_height = 0;
        }
        if (atlas_cursor_y + glyph_bitmap.height + padding > atlas_bitmap.height) {
            logg("Texture atlas is too small", font_filepath, FT_Error_String(ft_error));
            return optional_make_failure<Glyph_Atlas>();
        }
        information.atlas_fragcoords_left = (atlas_cursor_x - character_margin) / (float)atlas_bitmap.width;
        information.atlas_fragcoords_right = (atlas_cursor_x + glyph_bitmap.width + character_margin) / (float)atlas_bitmap.width;
        information.atlas_fragcoords_bottom = (atlas_cursor_y - character_margin) / (float)atlas_bitmap.height;
        information.atlas_fragcoords_top = (atlas_cursor_y + glyph_bitmap.height + character_margin) / (float)atlas_bitmap.height;
        // Inpaint data
        texture_bitmap_inpaint_complete(&atlas_bitmap, &glyph_bitmap, atlas_cursor_x, atlas_cursor_y);
        // Advance atlas_cursor
        atlas_cursor_x += glyph_bitmap.width + padding;
        if (atlas_max_line_height < glyph_bitmap.height) {
            atlas_max_line_height = glyph_bitmap.height;
        }

        // Save highest cursor_advance
        if (information.advance_x > result.cursor_advance) {
            result.cursor_advance = information.advance_x;
        }

        // Add glyph information into information array
        dynamic_array_push_back(&result.glyph_informations, information);
        result.character_to_glyph_map[current_character] = result.glyph_informations.size-1;
    }

    // Create character atlas texture
    result.atlas_distance_field = texture_bitmap_create_distance_field(&atlas_bitmap);
    result.atlas_bitmap = atlas_bitmap;

    return optional_make_success(result);
}

void glyph_atlas_save_as_file(Glyph_Atlas* atlas, const char* filepath)
{
    BinaryParser parser = binary_parser_create_empty(1024 * 1024 * 4);
    SCOPE_EXIT(binary_parser_destroy(&parser));
    binary_parser_write_int(&parser, atlas->atlas_bitmap.width);
    binary_parser_write_int(&parser, atlas->atlas_bitmap.height);
    binary_parser_write_int(&parser, atlas->ascender);
    binary_parser_write_int(&parser, atlas->descender);
    binary_parser_write_int(&parser, atlas->cursor_advance);
    texture_bitmap_binary_parser_write(&atlas->atlas_bitmap, &parser);
    binary_parser_write_bytes(&parser, array_as_bytes(&atlas->atlas_distance_field));
    binary_parser_write_int(&parser, atlas->glyph_informations.size);
    for (int i = 0; i < atlas->glyph_informations.size; i++) {
        glyph_information_binary_parser_write(&atlas->glyph_informations[i], &parser);
    }
    binary_parser_write_int(&parser, atlas->character_to_glyph_map.size);
    binary_parser_write_bytes(&parser, array_as_bytes(&atlas->character_to_glyph_map));

    binary_parser_write_to_file(&parser, filepath);
}

Optional<Glyph_Atlas> glyph_atlas_create_from_atlas_file(const char* atlas_filepath)
{
    Optional<BinaryParser> optional_parser = binary_parser_create_from_file(atlas_filepath);
    if (!optional_parser.available) {
        return optional_make_failure<Glyph_Atlas>();
    }
    BinaryParser* parser = &optional_parser.value;
    SCOPE_EXIT(binary_parser_destroy(parser));

    Glyph_Atlas result;
    int width = binary_parser_read_int(parser);
    int height = binary_parser_read_int(parser);
    result.ascender = binary_parser_read_int(parser);
    result.descender = binary_parser_read_int(parser);
    result.cursor_advance = binary_parser_read_int(parser);
    result.atlas_bitmap = texture_bitmap_binary_parser_read(parser);
    result.atlas_distance_field = array_create_empty<float>(width*height);
    binary_parser_read_bytes(parser, array_as_bytes(&result.atlas_distance_field));
    int glyph_information_count = binary_parser_read_int(parser);
    result.glyph_informations = dynamic_array_create_empty<Glyph_Information>(glyph_information_count);
    for (int i = 0; i < glyph_information_count; i++) {
        Glyph_Information info = glyph_information_binary_parser_read(parser);
        dynamic_array_push_back(&result.glyph_informations, info);
    }
    int character_to_glyph_map_size = binary_parser_read_int(parser);
    result.character_to_glyph_map = array_create_empty<int>(character_to_glyph_map_size);
    binary_parser_read_bytes(parser, array_as_bytes(&result.character_to_glyph_map));

    return optional_make_success(result);
}

void glyph_atlas_print_glyph_information(Glyph_Atlas* atlas)
{
    String message = string_create_empty(4096);
    SCOPE_EXIT(string_destroy(&message));
    string_append(&message, "\nGlyphAtlas Informations:\n");
    string_append_formated(&message, "\tAscender:         %d\n", atlas->ascender);
    string_append_formated(&message, "\tDescender:        %d\n", atlas->descender);
    string_append_formated(&message, "Glphys (#%d):\n", atlas->glyph_informations.size);
    for (int i = 0; i < atlas->glyph_informations.size; i++) {
        string_append(&message, "\t");
        glyph_information_append_to_string(&atlas->glyph_informations.data[i], &message);
    }
    logg("\n%s\n", message.characters);
}

void glyph_atlas_destroy(Glyph_Atlas* atlas)
{
    dynamic_array_destroy(&atlas->glyph_informations);
    array_destroy(&atlas->character_to_glyph_map);
    texture_bitmap_destroy(&atlas->atlas_bitmap);
    array_destroy(&atlas->atlas_distance_field);
}
