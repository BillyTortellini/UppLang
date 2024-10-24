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

float text_renderer_character_width(Text_Renderer* renderer, float line_height) {
    auto& atlas = renderer->glyph_atlas;
    const float width_to_height_ratio = atlas.cursor_advance / (float)(atlas.ascender - atlas.descender);
    return line_height * width_to_height_ratio;
}

vec2 text_renderer_get_aligned_char_size(Text_Renderer* renderer, float text_height)
{
    float width_to_height_ratio = text_renderer_get_char_width_to_height_ratio(renderer);
    float width = math_ceil(text_height * width_to_height_ratio); // Align to pixel size
    float height = math_ceil(width / width_to_height_ratio); // Also align to pixel size
    return vec2(width, height);
}

float text_renderer_get_char_width_to_height_ratio(Text_Renderer* renderer) {
    auto& atlas = renderer->glyph_atlas;
    return atlas.cursor_advance / (float)(atlas.ascender - atlas.descender);
}

void text_renderer_add_text(Text_Renderer* renderer, String text, vec2 position, Anchor anchor, vec2 char_size, vec3 color, Optional<Bounding_Box2> clip_box)
{
    if (text.size == 0) {
        return;
    }

    Glyph_Atlas* atlas = &renderer->glyph_atlas;
    float text_height = char_size.x / text_renderer_get_char_width_to_height_ratio(renderer);
    const vec2 char_size_normalized = convertSizeFromTo(char_size, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
    vec2 offset = anchor_switch(
        convertPointFromTo(position, Unit::PIXELS, Unit::NORMALIZED_SCREEN), vec2(char_size_normalized.x * text.size, char_size_normalized.y),
        anchor, Anchor::BOTTOM_LEFT
    );
    vec2 font_scaling = convertSizeFromTo(vec2(text_height) / (float)(atlas->ascender - atlas->descender), Unit::PIXELS, Unit::NORMALIZED_SCREEN);
    float distance_field_scaling;
    {
        float line_pixel_size_in_atlas = ((atlas->ascender - atlas->descender) / 64.0f); // In pixel per line_index
        float line_size_on_screen = text_height; // In pixel per line_index
        distance_field_scaling = line_size_on_screen / line_pixel_size_in_atlas;
    }

    // Two simple cases: completely inside clip box, or completely outside
    bool clipping = false;
    if (clip_box.available) {
        auto clip = clip_box.value;
        Bounding_Box2 text_box = bounding_box_2_make_anchor(position, vec2(char_size.x * text.size, text_height), anchor);
        if (!bounding_box_2_overlap(clip, text_box)) { // Return if everything is clipped
            return;
        }
        clipping = !bounding_box_2_is_other_box_inside(clip, text_box);
    }

    // Clip text, so that we don't produce too many vertices
    if (clipping) {
        int char_start = math_maximum(0, (int)((clip_box.value.min.x - position.x) / char_size.x));
        int char_end = math_minimum(text.size, (int)((clip_box.value.max.x - position.x + char_size.x) / char_size.x));
        offset.x = offset.x + char_size_normalized.x * char_start;
        text = string_create_substring_static(&text, char_start, char_end);
        if (char_start > char_end) {
            return;
        }
    }

    const int vertexCount = text.size * 4;
    auto positions = mesh_push_attribute_slice(renderer->text_mesh, rendering_core.predefined.position2D, vertexCount);
    auto uvs = mesh_push_attribute_slice(renderer->text_mesh, rendering_core.predefined.texture_coordinates, vertexCount);
    auto colors = mesh_push_attribute_slice(renderer->text_mesh, rendering_core.predefined.color3, vertexCount);
    auto pixelSizes = mesh_push_attribute_slice(renderer->text_mesh, renderer->attrib_pixel_size, vertexCount);
    auto indices = mesh_push_attribute_slice(renderer->text_mesh, rendering_core.predefined.index, text.size * 6);

    for (int i = 0; i < text.size; i++)
    {
        char character = text.characters[i];
        Glyph_Information* glyph_info = &atlas->glyph_informations[atlas->character_to_glyph_map[character]];;

        Bounding_Box2 char_box;
        char_box.min.x = i * char_size_normalized.x + glyph_info->bearing_x * font_scaling.x;
        char_box.min.y = (-atlas->descender + glyph_info->bearing_y - glyph_info->glyph_height) * font_scaling.y;
        char_box.max.x = char_box.min.x + glyph_info->glyph_width * font_scaling.x;
        char_box.max.y = char_box.min.y + glyph_info->glyph_height * font_scaling.y;
        char_box.min += offset;
        char_box.max += offset;

        Bounding_Box2 uv_box = bounding_box_2_make_min_max(
            vec2(glyph_info->atlas_fragcoords_left, glyph_info->atlas_fragcoords_bottom),
            vec2(glyph_info->atlas_fragcoords_right, glyph_info->atlas_fragcoords_top)
        );

        if (clipping) 
        {
            auto clip = clip_box.value;
            clip.min = convertPointFromTo(clip.min, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
            clip.max = convertPointFromTo(clip.max, Unit::PIXELS, Unit::NORMALIZED_SCREEN);

            Bounding_Box2 clipped_pos = char_box;
            Bounding_Box2 clipped_uvs = uv_box;
            if (char_box.min.y > clip.max.y || char_box.max.y < clip.min.y ||
                char_box.min.x > clip.max.x || char_box.max.x < clip.min.x) {
                /*
                Even though we already trim as much as possible, some characters may still get completely clipped
                because of how they are positioned on the screen (e.g. ascender/descender and general text positioning)
                */
                clipped_pos.min = vec2(-10.0f);
                clipped_pos.max = vec2(-10.0f);
            }
            else 
            {
                // Normal clipping with interpolation
                if (char_box.max.y > clip.max.y) {
                    float blend = (clip.max.y - char_box.min.y) / (char_box.max.y - char_box.min.y);
                    assert(blend >= 0 && blend <= 1, "Blend must be valid");
                    clipped_pos.max.y = math_interpolate_linear(char_box.min.y, char_box.max.y, blend);
                    clipped_uvs.max.y = math_interpolate_linear(uv_box.min.y, uv_box.max.y, blend);
                }
                if (char_box.min.y < clip.min.y) {
                    float blend = (clip.min.y - char_box.min.y) / (char_box.max.y - char_box.min.y);
                    assert(blend >= 0 && blend <= 1, "Blend must be valid");
                    clipped_pos.min.y = math_interpolate_linear(char_box.min.y, char_box.max.y, blend);
                    clipped_uvs.min.y = math_interpolate_linear(uv_box.min.y, uv_box.max.y, blend);
                }
                if (char_box.max.x > clip.max.x) {
                    float blend = (clip.max.x - char_box.min.x) / (char_box.max.x - char_box.min.x);
                    assert(blend >= 0 && blend <= 1, "Blend must be valid");
                    clipped_pos.max.x = math_interpolate_linear(char_box.min.x, char_box.max.x, blend);
                    clipped_uvs.max.x = math_interpolate_linear(uv_box.min.x, uv_box.max.x, blend);
                }
                if (char_box.min.x < clip.min.x) {
                    float blend = (clip.min.x - char_box.min.x) / (char_box.max.x - char_box.min.x);
                    assert(blend >= 0 && blend <= 1, "Blend must be valid");
                    clipped_pos.min.x = math_interpolate_linear(char_box.min.x, char_box.max.x, blend);
                    clipped_uvs.min.x = math_interpolate_linear(uv_box.min.x, uv_box.max.x, blend);
                }
            }
            char_box = clipped_pos;
            uv_box = clipped_uvs;
        }

        // Push back 4 vertices for each glyph
        positions[i * 4 + 0] = vec2(char_box.min.x, char_box.min.y);
        positions[i * 4 + 1] = vec2(char_box.max.x, char_box.min.y);
        positions[i * 4 + 2] = vec2(char_box.min.x, char_box.max.y);
        positions[i * 4 + 3] = vec2(char_box.max.x, char_box.max.y);
        uvs[i * 4 + 0] = vec2(uv_box.min.x, uv_box.min.y);
        uvs[i * 4 + 1] = vec2(uv_box.max.x, uv_box.min.y);
        uvs[i * 4 + 2] = vec2(uv_box.min.x, uv_box.max.y);
        uvs[i * 4 + 3] = vec2(uv_box.max.x, uv_box.max.y);
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

