#include "text_renderer.hpp"

#include "../datastructures/string.hpp"
#include "basic2D.hpp"
#include "../utility/utils.hpp"

Text_Renderer* text_renderer_create_from_font_atlas_file(const char* font_filepath)
{
    Text_Renderer* text_renderer = new Text_Renderer();
    text_renderer->glyph_atlas = optional_unwrap(glyph_atlas_create_from_atlas_file(font_filepath));

    // Create Font File
    //text_renderer->glyph_atlas = optional_unwrap(glyph_atlas_create_from_font_file("resources/fonts/consola.ttf", 256, 3200, 32, 16, false));
    //glyph_atlas_save_as_file(&text_renderer->glyph_atlas, "resources/fonts/glyph_atlas_new.atlas");
    //text_renderer->glyph_atlas = optional_unwrap(glyph_atlas_create_from_font_file("resources/cour.ttf", 128, 1600, 16, 8, true));
    //glyph_atlas_save_as_file(&text_renderer->glyph_atlas, "resources/glyph_atlas_cour.atlas");
    //glyph_atlas_print_glyph_information(&text_renderer->glyph_atlas);

    // Initialize textures
    text_renderer->atlas_bitmap_texture = texture_create_from_texture_bitmap(
        &text_renderer->glyph_atlas.atlas_bitmap, false
    );
    text_renderer->atlas_sdf_texture = texture_create_from_bytes(
        Texture_Type::RED_F32,
        array_as_bytes(&text_renderer->glyph_atlas.atlas_distance_field),
        text_renderer->glyph_atlas.atlas_bitmap.width,
        text_renderer->glyph_atlas.atlas_bitmap.height, 
        false
    );

    text_renderer->attrib_pixel_size = vertex_attribute_make<float>("Pixel_Size");
    auto& predef = rendering_core.predefined;
    text_renderer->text_mesh = rendering_core_query_mesh(
        "text rendering mesh",
        vertex_description_create({
            predef.position2D,
            predef.texture_coordinates,
            predef.color3,
            predef.index,
            text_renderer->attrib_pixel_size 
        }),
        true
    );

    return text_renderer;
}

void text_renderer_destroy(Text_Renderer* renderer)
{
    glyph_atlas_destroy(&renderer->glyph_atlas);
    delete renderer;
}

vec2 text_renderer_get_scaling_factor(Text_Renderer* renderer, float relative_height)
{
    // Glpyh information sizes (in 23.3 format) to normalized screen coordinates scaling factor
    Glyph_Atlas* atlas = &renderer->glyph_atlas;
    auto& info = rendering_core.render_information;
    float CHARACTER_HEIGHT_NORMALIZED = relative_height;
    const float scaling_factor_x = CHARACTER_HEIGHT_NORMALIZED / (atlas->ascender - atlas->descender) *
        ((float)info.backbuffer_height / info.backbuffer_width);
    const float scaling_factor_y = CHARACTER_HEIGHT_NORMALIZED / (atlas->ascender - atlas->descender);

    return vec2(scaling_factor_x, scaling_factor_y);
}

void text_renderer_add_text(Text_Renderer* renderer, String text, vec2 position, Anchor anchor, float line_height, vec3 color)
{
    const int vertexCount = text.size * 4;
    auto positions = mesh_push_attribute_slice(renderer->text_mesh, rendering_core.predefined.position2D, vertexCount);
    auto uvs = mesh_push_attribute_slice(renderer->text_mesh, rendering_core.predefined.texture_coordinates, vertexCount);
    auto colors = mesh_push_attribute_slice(renderer->text_mesh, rendering_core.predefined.color3, vertexCount);
    auto pixelSizes = mesh_push_attribute_slice(renderer->text_mesh, renderer->attrib_pixel_size, vertexCount);
    auto indices = mesh_push_attribute_slice(renderer->text_mesh, rendering_core.predefined.index, text.size * 6);

    Glyph_Atlas* atlas = &renderer->glyph_atlas;
    vec2 char_size_normalized = convertSizeFromTo(
        vec2(((float)atlas->cursor_advance / (float)(atlas->ascender - atlas->descender)), 1.0f) * line_height, Unit::PIXELS, Unit::NORMALIZED_SCREEN
    );
    vec2 offset = anchor_switch(
        convertPointFromTo(position, Unit::PIXELS, Unit::NORMALIZED_SCREEN), vec2(char_size_normalized.x * text.size, char_size_normalized.y),
        anchor, Anchor::BOTTOM_LEFT
    );
    vec2 font_scaling = convertSizeFromTo(vec2(line_height) / (float)(atlas->ascender - atlas->descender), Unit::PIXELS, Unit::NORMALIZED_SCREEN);
    float distance_field_scaling;
    {
        float line_pixel_size_in_atlas = ((atlas->ascender - atlas->descender) / 64.0f); // In pixel per line_index
        float line_size_on_screen = line_height; // In pixel per line_index
        distance_field_scaling = line_size_on_screen / line_pixel_size_in_atlas;
    }


    for (int i = 0; i < text.size; i++) 
    {
        char character = text.characters[i];
        Glyph_Information* glyph_info = &atlas->glyph_informations[atlas->character_to_glyph_map[character]];;

        Bounding_Box2 char_box;
        char_box.min.x = i * char_size_normalized.x + glyph_info->bearing_x * font_scaling.x;
        char_box.min.y = (-atlas->descender + glyph_info->bearing_y - glyph_info->glyph_height) * font_scaling.y;
        char_box.max.x = char_box.min.x + glyph_info->glyph_width * font_scaling.x;
        char_box.max.y = char_box.min.y + glyph_info->glyph_height * font_scaling.y;

        // Push back 4 vertices for each glyph
        positions[i * 4 + 0] = vec2(char_box.min.x, char_box.min.y) + offset;
        positions[i * 4 + 1] = vec2(char_box.max.x, char_box.min.y) + offset;
        positions[i * 4 + 2] = vec2(char_box.min.x, char_box.max.y) + offset;
        positions[i * 4 + 3] = vec2(char_box.max.x, char_box.max.y) + offset;
        uvs[i * 4 + 0] = vec2(glyph_info->atlas_fragcoords_left, glyph_info->atlas_fragcoords_bottom);
        uvs[i * 4 + 1] = vec2(glyph_info->atlas_fragcoords_right, glyph_info->atlas_fragcoords_bottom);
        uvs[i * 4 + 2] = vec2(glyph_info->atlas_fragcoords_left, glyph_info->atlas_fragcoords_top);
        uvs[i * 4 + 3] = vec2(glyph_info->atlas_fragcoords_right, glyph_info->atlas_fragcoords_top);
        colors[i * 4 + 0] = color;
        colors[i * 4 + 1] = color;
        colors[i * 4 + 2] = color;
        colors[i * 4 + 3] = color;
        pixelSizes[i * 4 + 0] = distance_field_scaling;
        pixelSizes[i * 4 + 1] = distance_field_scaling;
        pixelSizes[i * 4 + 2] = distance_field_scaling;
        pixelSizes[i * 4 + 3] = distance_field_scaling;

        // Push 6 indices for each character quad
        indices[i * 6 + 0] = (renderer->current_batch_end + i) * 4 + 0;
        indices[i * 6 + 1] = (renderer->current_batch_end + i) * 4 + 1;
        indices[i * 6 + 2] = (renderer->current_batch_end + i) * 4 + 2;
        indices[i * 6 + 3] = (renderer->current_batch_end + i) * 4 + 1;
        indices[i * 6 + 4] = (renderer->current_batch_end + i) * 4 + 3;
        indices[i * 6 + 5] = (renderer->current_batch_end + i) * 4 + 2;
    }
    renderer->current_batch_end += text.size;
}

float text_renderer_line_width(Text_Renderer* renderer, float line_height, int char_count) {
    auto& atlas = renderer->glyph_atlas;
    return line_height * char_count * atlas.cursor_advance / (float)(atlas.ascender - atlas.descender);
}

void text_renderer_reset(Text_Renderer* renderer) {
    renderer->current_batch_end = 0;
    renderer->last_batch_end = 0;
}

void text_renderer_draw(Text_Renderer* renderer, Render_Pass* render_pass)
{
    if (renderer->last_batch_end == renderer->current_batch_end) {
        return;
    }

    // Get shaders
    auto& core = rendering_core;
    auto bitmap_shader = rendering_core_query_shader("core/font_bitmap.glsl");
    auto sdf_shader = rendering_core_query_shader("core/font_sdf.glsl");

    // Render the last pushed indices
    render_pass_draw_count(
        render_pass,
        sdf_shader,
        renderer->text_mesh,
        Mesh_Topology::TRIANGLES,
        { uniform_make("sampler", renderer->atlas_sdf_texture, sampling_mode_bilinear()) },
        renderer->last_batch_end * 6, 
        (renderer->current_batch_end - renderer->last_batch_end) * 6
    );
    renderer->last_batch_end = renderer->current_batch_end;
}



/*
    Typography information/vocabulary:
        Font        ... Set of character images used to print/display text
        Font Family ... Contains multiple font faces of the same template
        Font Face   ... Contains an image for each character
            Example: Font family: "Arial", Font Face: "Arial Bold", "Arial italic"...

    Files:
        Digital font    ... File containing one or more font faces
        Font collection ... same as above

    Character and mapping:
        Glyph       ... A image of a single character
                        One character can have multiple glyphs (multiple styles, different faces) and multiple characters can share the same glyph
        Charmap     ... Convert character codes into glyph indices inside a font file
                        Depends on encoding, files may contain ASCII, Unicode or other encodings

    Sizing, dimensions in printing:
        Text sizes are not given in pixels, since pixels are not PHYSICAL units
        (They depend on monitor size --> specified by DPI/PPI)
        This is why they are given in points (1/72 of an inch).
        Points are called points because printers methods used points (Point array printers)

        BUT when designing a text, designers do not think about logical sizes, only the proportions of each glyph to another.
        This dimensionless space is the EM-SQUARE, and glyph information is usually stored in this em square
        To convert the em units to inches, metrics are provided in the font file.

        So, to go from the file to pixels, you do the following
            EM-Space        -->   Physical Size in inches     (Font metrics in file)
            Physical Size   -->   Pixel size                  (dpi)

    Vector representation:
        A glphy is represented by quadratic or cubic bezier curves in the em-space, and
        the coordinates are on a fixed grid, typically given by signed 16-bit integers.

    Hinting: To get from the vector representation to a bitmap (Grid-fitting)

    Text positioning, and size metrics:
    Per Font Face information:
        Baseline        ...     Horizontal or vertical line_index on which characters are placed
        Pen/Origin      ...     Current position on the baseline (Is always on baseline)
        Ascent          ...     Per character information on height of the character from the baseline (Always positive)
        Descent         ...     Per character information on the lowest part from the baseline (Always negative)
        Linespace       ...     How much the baseline should shift in a new line_index, should be linespace = ascent - descent + linegap
        Linegap         ...     Per font information, additional space that should exist between lines
        BoundingBox     ...     Bounding box of all glyphs
    Per Glyph informations
        Advance width   ...     How much the pen should advance horizontally after drawing
        Advance height  ...     How much the pen should advance verticaly after drawing (0 for horizontal languages)
        Bearing left    ...     Distance from pen to left side of the bounding box
        Bearing right   ...     Distance from pen to top side of the bounding box
        Glyph width     ...     Width  (of bounding box)
        Glyph height    ...     Height (of bounding box)

    Kerning
        Because each single glyph contains an ascent, and because of a glpyhs form, sometimes unwanted whitespace may appear.
        As an example, AV usually has a lot of whitespace, and when determining the glyphs position, we want to avoid this.
        This is what kerning does. (Usually contained in the GPOS and kern tables of the font)
*/

/*
    What are my requirements:
        - ANSI-Code (8 bit instead of 7 bit ASCII code, unicode is not compatible)
        - Transforms/Scale invariance -> Distance field rendering
        - Kerning would also be cool, but i generally only need unispaced fonts

    Interesting stuff to implement:
        - Distance field form raster image
        - Rectangle packing in texture atlas
        - Font rasterization (When loading fonts myself) from bezier curves
        - Kerning and text placement with other metrics
*/

/*
    RESEARCH:
    ---------
    Libraries for text rendering:
        - DirectWrite (DirectX/Microsoft)
        - Some Apple stuff
        - FreeType (Font rasterizer)
        - Slug (Does rendering directly in pixel shader with bezier curves, interesting paper)
        - Some other

    Font file formats:
        - TrueType (Old, made by apple)
        - OpenType (Based on TrueType, trademark Microsoft)
*/

