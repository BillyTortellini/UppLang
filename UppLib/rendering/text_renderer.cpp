#include "text_renderer.hpp"

#include "../datastructures/string.hpp"
#include "../utility/utils.hpp"
#include "shader_program.hpp"

Text_Layout text_layout_create()
{
    Text_Layout info;
    info.character_positions = dynamic_array_create_empty<Character_Position>(512);
    return info;
}

void text_layout_destroy(Text_Layout* info) {
    dynamic_array_destroy(&info->character_positions);
}

void text_renderer_update_window_size(void* userdata, Rendering_Core* core)
{
    Text_Renderer* renderer = (Text_Renderer*) userdata;
    renderer->screen_width = core->render_information.viewport_width;
    renderer->screen_height = core->render_information.viewport_height;
}

Text_Renderer* text_renderer_create_from_font_atlas_file(
    Rendering_Core* core,
    const char* font_filepath
)
{
    Text_Renderer* text_renderer = new Text_Renderer();
    text_renderer->text_layout = text_layout_create();
    text_renderer->screen_width = core->render_information.window_width;
    text_renderer->screen_height = core->render_information.window_height;
    rendering_core_add_window_size_listener(core, &text_renderer_update_window_size, text_renderer);
    text_renderer->glyph_atlas = optional_unwrap(glyph_atlas_create_from_atlas_file(font_filepath));
    text_renderer->default_color = vec3(1.0f);
    text_renderer->pipeline_state = pipeline_state_make_default();
    text_renderer->pipeline_state.depth_state.test_type = Depth_Test_Type::IGNORE_DEPTH;
    text_renderer->pipeline_state.culling_state.culling_enabled = true;
    text_renderer->pipeline_state.blending_state.blending_enabled = true;

    // Create Font File
    //text_renderer->glyph_atlas = optional_unwrap(glyph_atlas_create_from_font_file("resources/fonts/consola.ttf", 256, 3200, 32, 16, false));
    //glyph_atlas_save_as_file(&text_renderer->glyph_atlas, "resources/fonts/glyph_atlas_new.atlas");
    //text_renderer->glyph_atlas = optional_unwrap(glyph_atlas_create_from_font_file("resources/cour.ttf", 128, 1600, 16, 8, true));
    //glyph_atlas_save_as_file(&text_renderer->glyph_atlas, "resources/glyph_atlas_cour.atlas");
    //glyph_atlas_print_glyph_information(&text_renderer->glyph_atlas);

    // Initialize shaders
    text_renderer->bitmap_shader = shader_program_create_from_multiple_sources(
        core, { "resources/shaders/font_bitmap.frag", "resources/shaders/font_bitmap.vert" }
    );
    text_renderer->sdf_shader = shader_program_create_from_multiple_sources(
        core, { "resources/shaders/font_sdf.frag", "resources/shaders/font_sdf.vert" }
    );

    // Initialize textures
    text_renderer->atlas_bitmap_texture = texture_2D_create_from_texture_bitmap(
        core, 
        &text_renderer->glyph_atlas.atlas_bitmap,
        texture_sampling_mode_make_bilinear()
    );
    text_renderer->atlas_sdf_texture = texture_2D_create_from_bytes(
        core,
        Texture_2D_Type::RED_F32,
        array_as_bytes(&text_renderer->glyph_atlas.atlas_distance_field),
        text_renderer->glyph_atlas.atlas_bitmap.width,
        text_renderer->glyph_atlas.atlas_bitmap.height,
        texture_sampling_mode_make_bilinear()
    );

    // Initialize GPU data
    Vertex_Attribute attribute_informations[] = {
        vertex_attribute_make(Vertex_Attribute_Type::POSITION_2D),
        vertex_attribute_make(Vertex_Attribute_Type::UV_COORDINATES_0),
        vertex_attribute_make(Vertex_Attribute_Type::COLOR3),
        vertex_attribute_make_custom(Vertex_Attribute_Data_Type::FLOAT, 11)
    };
    text_renderer->font_mesh = mesh_gpu_buffer_create_with_single_vertex_buffer(
        core,
        gpu_buffer_create_empty(sizeof(Font_Vertex)*1024, GPU_Buffer_Type::VERTEX_BUFFER, GPU_Buffer_Usage::DYNAMIC),
        array_create_static(attribute_informations, 4),
        gpu_buffer_create_empty(sizeof(GLuint) * 1024 * 4, GPU_Buffer_Type::INDEX_BUFFER, GPU_Buffer_Usage::DYNAMIC),
        Mesh_Topology::TRIANGLES,
        0
    );

    text_renderer->text_vertices = dynamic_array_create_empty<Font_Vertex>(1024);
    text_renderer->text_indices = dynamic_array_create_empty<GLuint>(1024);

    return text_renderer;
}

void text_renderer_destroy(Text_Renderer* renderer, Rendering_Core* core)
{
    rendering_core_remove_window_size_listener(core, renderer);
    text_layout_destroy(&renderer->text_layout);
    shader_program_destroy(renderer->bitmap_shader);
    shader_program_destroy(renderer->sdf_shader);
    mesh_gpu_buffer_destroy(&renderer->font_mesh);
    glyph_atlas_destroy(&renderer->glyph_atlas);
    dynamic_array_destroy(&renderer->text_vertices);
    dynamic_array_destroy(&renderer->text_indices);
    delete renderer;
}

vec2 text_renderer_get_scaling_factor(Text_Renderer* renderer, float relative_height)
{
    // Glpyh information sizes (in 23.3 format) to normalized screen coordinates scaling factor
    Glyph_Atlas* atlas = &renderer->glyph_atlas;
    float CHARACTER_HEIGHT_NORMALIZED = relative_height;
    const float scaling_factor_x = CHARACTER_HEIGHT_NORMALIZED / (atlas->ascender - atlas->descender) *
        ((float)renderer->screen_height / renderer->screen_width);
    const float scaling_factor_y = CHARACTER_HEIGHT_NORMALIZED / (atlas->ascender - atlas->descender);

    return vec2(scaling_factor_x, scaling_factor_y);
}

void text_renderer_add_text_from_layout(
    Text_Renderer* renderer,
    Text_Layout* layout,
    vec2 position
)
{
    Glyph_Atlas* atlas = &renderer->glyph_atlas;
    vec2 scaling_factor = text_renderer_get_scaling_factor(renderer, layout->relative_height);
    float descender = atlas->descender * scaling_factor.y;
    float distance_field_scaling;
    {
        float line_pixel_size_in_atlas = ((atlas->ascender - atlas->descender) / 64.0f); // In pixel per line
        float line_size_on_screen = layout->relative_height * renderer->screen_height; // In pixel per line
        distance_field_scaling = line_size_on_screen / line_pixel_size_in_atlas;
    }

    for (int i = 0; i < layout->character_positions.size; i++)
    {
        Character_Position* char_pos = &renderer->text_layout.character_positions[i];
        Glyph_Information* glyph_info = char_pos->glyph_info;

        BoundingBox2 char_box;
        char_box.min.x =
            char_pos->bounding_box.min.x + position.x +
            glyph_info->bearing_x * scaling_factor.x;
        char_box.min.y =
            char_pos->bounding_box.min.y + position.y - descender +
            (glyph_info->bearing_y - glyph_info->glyph_height) * scaling_factor.y;
        char_box.max.x = char_box.min.x +
            glyph_info->glyph_width * scaling_factor.x;
        char_box.max.y = char_box.min.y +
            glyph_info->glyph_height * scaling_factor.y;

        // Push back 4 vertices for each glyph
        Font_Vertex bb_bottom_left(
            vec2(char_box.min.x, char_box.min.y),
            vec2(glyph_info->atlas_fragcoords_left, glyph_info->atlas_fragcoords_bottom),
            char_pos->color,
            distance_field_scaling
        );
        Font_Vertex bb_bottom_right(
            vec2(char_box.max.x, char_box.min.y),
            vec2(glyph_info->atlas_fragcoords_right, glyph_info->atlas_fragcoords_bottom),
            char_pos->color,
            distance_field_scaling
        );
        Font_Vertex bb_top_left(
            vec2(char_box.min.x, char_box.max.y),
            vec2(glyph_info->atlas_fragcoords_left, glyph_info->atlas_fragcoords_top),
            char_pos->color,
            distance_field_scaling
        );
        Font_Vertex bb_top_right(
            vec2(char_box.max.x, char_box.max.y),
            vec2(glyph_info->atlas_fragcoords_right, glyph_info->atlas_fragcoords_top),
            char_pos->color,
            distance_field_scaling
        );
        int quad_start_index = renderer->text_vertices.size;
        dynamic_array_push_back(&renderer->text_vertices, bb_bottom_left);
        dynamic_array_push_back(&renderer->text_vertices, bb_bottom_right);
        dynamic_array_push_back(&renderer->text_vertices, bb_top_right);
        dynamic_array_push_back(&renderer->text_vertices, bb_top_left);
        // Push back 6 indices for each glyph
        dynamic_array_push_back(&renderer->text_indices, (GLuint)quad_start_index + 0);
        dynamic_array_push_back(&renderer->text_indices, (GLuint)quad_start_index + 1);
        dynamic_array_push_back(&renderer->text_indices, (GLuint)quad_start_index + 2);
        dynamic_array_push_back(&renderer->text_indices, (GLuint)quad_start_index + 0);
        dynamic_array_push_back(&renderer->text_indices, (GLuint)quad_start_index + 2);
        dynamic_array_push_back(&renderer->text_indices, (GLuint)quad_start_index + 3);
    }
}

void text_renderer_add_text(
    Text_Renderer* renderer,
    String* text,
    vec2 position,
    float relative_height,
    float line_gap_percent)
{
    // Loop over all glyphs
    Text_Layout* layout = text_renderer_calculate_text_layout(renderer, text, relative_height, line_gap_percent);
    text_renderer_add_text_from_layout(renderer, layout, position);
}

Text_Layout* text_renderer_calculate_text_layout(
    Text_Renderer* renderer,
    String* text,
    float relative_height,
    float line_gap_percent)
{
    Glyph_Atlas* atlas = &renderer->glyph_atlas;
    vec2 scaling_factor = text_renderer_get_scaling_factor(renderer, relative_height);
    renderer->text_layout.relative_height = relative_height;

    float max_cursor_x = 0.0f;
    float cursor_x = 0.0f;
    //float cursor_y = -atlas->descender * scaling_factor.y;
    float cursor_y = 0.0f;

    dynamic_array_reset(&renderer->text_layout.character_positions);
    for (int i = 0; i < text->size; i++)
    {
        byte current_character = (*text)[i];
        if (current_character == '\n') {
            cursor_y -= relative_height * line_gap_percent;
            if (cursor_x > max_cursor_x) {
                max_cursor_x = cursor_x;
            }
            cursor_x = 0.0f;
            continue;
        }

        // Get Glyph info
        Glyph_Information* info = &atlas->glyph_informations[atlas->character_to_glyph_map[current_character]];;

        // Add character information
        {
            Character_Position pos;
            pos.glyph_info = info;
            pos.bounding_box.min = vec2(cursor_x, cursor_y);
            pos.bounding_box.max = vec2(cursor_x + info->advance_x * scaling_factor.x,
                cursor_y + (atlas->ascender - atlas->descender) * scaling_factor.y);
            pos.color = renderer->default_color;
            dynamic_array_push_back(&renderer->text_layout.character_positions, pos);
        }

        // Advance to next character
        cursor_x += info->advance_x * scaling_factor.x;
    }
    if (cursor_x > max_cursor_x) {
        max_cursor_x = cursor_x;
    }

    // Push up all character, so that all y coordinates are > 0
    for (int i = 0; i < renderer->text_layout.character_positions.size; i++) {
        renderer->text_layout.character_positions[i].bounding_box.min.y += cursor_y;
        renderer->text_layout.character_positions[i].bounding_box.max.y += cursor_y;
    }

    renderer->text_layout.size = vec2(max_cursor_x, -cursor_y);
    return &renderer->text_layout;
}

void text_renderer_render(Text_Renderer* renderer, Rendering_Core* core)
{
    // Update font_mesh
    gpu_buffer_update(
        &renderer->font_mesh.vertex_buffers[0].gpu_buffer,
        dynamic_array_as_bytes(&renderer->text_vertices)
    );
    mesh_gpu_buffer_update_index_buffer(
        &renderer->font_mesh,
        core,
        dynamic_array_as_array(&renderer->text_indices)
    );
    renderer->font_mesh.index_count = renderer->text_indices.size;

    // Reset buffers
    dynamic_array_reset(&renderer->text_vertices);
    dynamic_array_reset(&renderer->text_indices);

    // Render
    rendering_core_update_pipeline_state(core, renderer->pipeline_state);
    shader_program_set_uniform(renderer->sdf_shader, core, "sampler", texture_2D_bind_to_next_free_unit(&renderer->atlas_sdf_texture, core));
    mesh_gpu_buffer_draw_with_shader_program(&renderer->font_mesh, renderer->sdf_shader, core);
}

float text_renderer_get_cursor_advance(Text_Renderer* renderer, float relative_height)
{
    vec2 scaling_factor = text_renderer_get_scaling_factor(renderer, relative_height);
    return renderer->glyph_atlas.cursor_advance * scaling_factor.x;
}

Texture_2D* text_renderer_get_texture(Text_Renderer* renderer)
{
    return &renderer->atlas_sdf_texture;
}

void text_renderer_set_color(Text_Renderer* renderer, vec3 color) {
    renderer->default_color = color;
}

float text_renderer_calculate_text_width(Text_Renderer* renderer, int char_count, float relative_height) {
    return (float)renderer->glyph_atlas.cursor_advance / (renderer->glyph_atlas.ascender - renderer->glyph_atlas.descender) * relative_height * char_count
        * ((float)renderer->screen_height / renderer->screen_width);
}









/*
    How to implement: either
        - Use a tool to create distance fields onto images, load those
        - Use a library to load TrueType font types, and render those
           |--> Create distance field from rasterized images/create distance fields directly from those images
        - Load TrueType/OpenType font data manually, do bezier stuff, would probably be lots of fun :)
*/

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
        Baseline        ...     Horizontal or vertical line on which characters are placed
        Pen/Origin      ...     Current position on the baseline (Is always on baseline)
        Ascent          ...     Per character information on height of the character from the baseline (Always positive)
        Descent         ...     Per character information on the lowest part from the baseline (Always negative)
        Linespace       ...     How much the baseline should shift in a new line, should be linespace = ascent - descent + linegap
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

