#include "imgui_test.hpp"

#include <iostream>

#include "../../win32/timing.hpp"

#include "../../rendering/opengl_utils.hpp"
#include "../../rendering/gpu_buffers.hpp"
#include "../../rendering/cameras.hpp"
#include "../../rendering/camera_controllers.hpp"
#include "../../rendering/texture.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../win32/window.hpp"
#include "../../win32/process.hpp"
#include "../../utility/file_io.hpp"
#include "../../utility/random.hpp"
#include "../../rendering/renderer_2d.hpp"

#include "../../math/umath.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/hashset.hpp"
#include "../../utility/hash_functions.hpp"

#include "../../datastructures/block_allocator.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "../../win32/windows_helper_functions.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

struct ivec2
{
    ivec2() {}
    ivec2(int val) : x(val), y(val) {}
    ivec2(int x, int y) : x(x), y(y) {}

    int x;
    int y;
};

struct BBox
{
    BBox() {}
    BBox(ivec2 val) { min = val; max = val; };
    BBox(ivec2 min, ivec2 max) { this->min = min; this->max = max; };

    ivec2 min;
    ivec2 max;
};

struct Bitmap
{
    ivec2 size;
    u8* data;
    // Having a pitch that may differ from size allows 2D-Slices/Views of other data
    int pitch;
};

// Creates uninitialized bitmap data
Bitmap bitmap_create(ivec2 size)
{
    Bitmap result;
    result.size = size;
    result.data = new u8[size.x * size.y];
    result.pitch = size.x;
    return result;
}

void bitmap_destroy(Bitmap bitmap)
{
    delete[] bitmap.data;
    bitmap.data = nullptr;
}

Bitmap bitmap_create_static(ivec2 size, u8* data, int pitch)
{
    Bitmap bitmap;
    bitmap.size = size;
    bitmap.data = data;
    bitmap.pitch = pitch;
    return bitmap;
}

void cpu_mono_texture_block_transfer(Bitmap destination, Bitmap source, ivec2 position, bool mirror_y = false)
{
    if (position.x < 0 || position.y < 0 || position.x + source.size.x >= destination.size.x || position.y + source.size.y >= destination.size.y) {
        panic("Caller must make sure to not overdraw!\n");
        return;
    }

    for (int x = 0; x < source.size.x; x++)
    {
        for (int y = 0; y < source.size.y; y++)
        {
            int source_index = x + y * source.pitch;
            if (mirror_y) {
                source_index = x + (source.size.y - y - 1) * source.pitch;
            }

            int destination_x = x + position.x;
            int destination_y = y + position.y; 
            int destination_index = destination_x + destination_y * destination.pitch;
            destination.data[destination_index] = source.data[source_index];
        }
    }
}



struct Sub_Image
{
    ivec2 position; // Bottom left coordinates of bitmap-start
    ivec2 size;
};

struct Bitmap_Atlas
{
    Bitmap bitmap;
    Dynamic_Array<Sub_Image> sub_images;
    ivec2 write_pos;
    int max_subimage_height_in_current_line; // E.g. max height of su
};

Bitmap_Atlas bitmap_atlas_create(ivec2 size)
{
    Bitmap_Atlas result;
    result.bitmap = bitmap_create(size);
    result.max_subimage_height_in_current_line = 0;
    result.write_pos = ivec2(0);
    result.sub_images = dynamic_array_create<Sub_Image>();
    return result;
}

void bitmap_atlas_destroy(Bitmap_Atlas* atlas)
{
    bitmap_destroy(atlas->bitmap);
    dynamic_array_destroy(&atlas->sub_images);
}

// Returns index of subtexture, or -1 if atlas is full
int bitmap_atlas_add_sub_image(Bitmap_Atlas* atlas, Bitmap bitmap, bool mirror_y = false)
{
    auto& write_pos = atlas->write_pos;

    // Check if atlas-bitmap is large enough for given bitmap and position
    if (bitmap.size.x >= atlas->bitmap.size.x || bitmap.size.y >= atlas->bitmap.size.y) {
        return -1;
    }

    // Jump to next line in atlas if current line is full
    if (write_pos.x + bitmap.size.x >= atlas->bitmap.size.x) 
    {
        // Check if atlas is exhausted (No more free space)
        int next_write_y = write_pos.y + atlas->max_subimage_height_in_current_line;
        if (next_write_y + bitmap.size.y >= atlas->bitmap.size.y) {
            return -1;
        }

        write_pos.x = 0;
        write_pos.y = next_write_y;
        atlas->max_subimage_height_in_current_line = 0;
    }

    // Store information
    Sub_Image subtexture_info;
    subtexture_info.position = write_pos;
    subtexture_info.size = bitmap.size;
    dynamic_array_push_back(&atlas->sub_images, subtexture_info);

    // Write bitmap to atlas, advance cursor
    cpu_mono_texture_block_transfer(atlas->bitmap, bitmap, write_pos, mirror_y);
    atlas->max_subimage_height_in_current_line = math_maximum(atlas->max_subimage_height_in_current_line, bitmap.size.y);
    write_pos.x += bitmap.size.x;

    return atlas->sub_images.size - 1;
}



struct Glyph_Information_
{
    unsigned char character;
    int sub_image_index;
    ivec2 placement_offset; // Where to place the bitmap-quad with respect to the current line cursor (Positive)
};

struct Glyph_Atlas_
{
    ivec2 char_box_size;
    Dynamic_Array<Glyph_Information_> glyph_informations;
    Array<int> character_to_glyph_map; // Maps 0-255 to a glyph information index
};

Glyph_Atlas_ glyph_atlas_create()
{
    Glyph_Atlas_ result;
    result.char_box_size = ivec2(0);
    result.glyph_informations = dynamic_array_create<Glyph_Information_>(128);
    result.character_to_glyph_map = array_create<int>(256);
    return result;
}

void glyph_atlas_destroy(Glyph_Atlas_* atlas)
{
    dynamic_array_destroy(&atlas->glyph_informations);
    array_destroy(&atlas->character_to_glyph_map);
}

void glyph_atlas_rasterize_font(Glyph_Atlas_* glyph_atlas, Bitmap_Atlas* bitmap_atlas, const char* font_filepath, u32 pixel_height)
{
    glyph_atlas->char_box_size = 0;
    dynamic_array_reset(&glyph_atlas->glyph_informations);
    for (int i = 0; i < glyph_atlas->character_to_glyph_map.size; i++) {
        glyph_atlas->character_to_glyph_map[i] = 0;
    }

    // Initialize freetype
    FT_Library library;
    u32 ft_error = FT_Init_FreeType(&library);
    if (ft_error != 0) {
        logg("Could not initialize freetype, error: %s\n", FT_Error_String(ft_error));
        return;
    }
    SCOPE_EXIT(FT_Done_FreeType(library));

    FT_Face face; // A FreeType face is a font
    ft_error = FT_New_Face(library, font_filepath, 0, &face);
    if (ft_error != 0) {
        logg("Could not create face for \"%s\", error: %s\n", font_filepath, FT_Error_String(ft_error));
        return;
    }
    SCOPE_EXIT(FT_Done_Face(face));

    // Set pixel size
    ft_error = FT_Set_Pixel_Sizes(face, 0, pixel_height);
    if (ft_error != 0) {
        logg("FT_Set_Pixel_Size failed, error: %s\n", FT_Error_String(ft_error));
        return;
    }

    int min_y = 100000;
    int max_y = -100000;
    int max_advance = 0;

    for (int i = 31; i < glyph_atlas->character_to_glyph_map.size; i++) // Start with first printable ascii character (Space = 32)
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
        ft_error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        if (ft_error != 0) {
            logg("FT_Render_Glyph failed for '%c' (%d): %s\n", current_character, i, FT_Error_String(ft_error));
            continue;
        }

        // Store glyph bitmap in atlas
        ivec2 pixel_size = ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows);
        Bitmap glyph_bitmap = bitmap_create_static(pixel_size, face->glyph->bitmap.buffer, face->glyph->bitmap.pitch);
        int sub_image_index = bitmap_atlas_add_sub_image(bitmap_atlas, glyph_bitmap, true);
        if (sub_image_index == -1) {
            logg("Bitmap atlas size is too small for further glyphs\n");
            continue;
        }

        // Sanity check metrics
        assert(face->glyph->metrics.horiAdvance % 64 == 0, "I expect TrueType to make scalable fonts exactly pixel-sized!\n");
        assert(face->glyph->metrics.horiBearingX % 64 == 0, "");
        assert(face->glyph->metrics.horiBearingY % 64 == 0, "");
        assert(face->glyph->metrics.width / 64 == pixel_size.x, "");
        assert(face->glyph->metrics.height / 64 == pixel_size.y, "");

        // Store size metrics
        max_advance = math_maximum(max_advance, (int) face->glyph->metrics.horiAdvance / 64);
        min_y = math_minimum(min_y, (int)face->glyph->metrics.horiBearingY / 64 - pixel_size.y);
        max_y = math_maximum(max_y, (int)face->glyph->metrics.horiBearingY / 64);

        // Create Glyph information
        Glyph_Information_ information;
        information.character = current_character;
        information.sub_image_index = sub_image_index;
        information.placement_offset.x = face->glyph->metrics.horiBearingX / 64;
        information.placement_offset.y = face->glyph->metrics.horiBearingY / 64 - pixel_size.y; // Note: Usually negative/0

        // Add glyph information into information array
        dynamic_array_push_back(&glyph_atlas->glyph_informations, information);
        glyph_atlas->character_to_glyph_map[current_character] = glyph_atlas->glyph_informations.size-1;
    }

    // Adjust placement offsets so we only deal with 
    for (int i = 0; i < glyph_atlas->glyph_informations.size; i++) {
        auto& glyph = glyph_atlas->glyph_informations[i];
        glyph.placement_offset.y += -min_y;
    }
    glyph_atlas->char_box_size.x = max_advance;
    glyph_atlas->char_box_size.y = max_y - min_y;
}


void mesh_push_text(Mesh* mesh, Glyph_Atlas_* atlas, Bitmap_Atlas* bitmap_atlas, const char* text, ivec2 position, ivec2 screen_size)
{
    int char_count = strlen(text);
    {
        if (char_count <= 0) return;
        int non_special_char_count = 0;
        for (int i = 0; i < char_count; i++) {
            char c = text[i];
            if (c <= 31) continue;
            non_special_char_count += 1;
        }
        char_count = non_special_char_count;
    }

    u32 start_vertex_count = mesh->vertex_count;
    auto& predef = rendering_core.predefined;
    Array<vec2> pos_data =   mesh_push_attribute_slice(mesh, predef.position2D,          4 * char_count);
    Array<vec4> color_data = mesh_push_attribute_slice(mesh, predef.color4,              4 * char_count);
    Array<vec2> uv_data =    mesh_push_attribute_slice(mesh, predef.texture_coordinates, 4 * char_count);
    Array<u32> indices =     mesh_push_attribute_slice(mesh, predef.index,               6 * char_count);

    int non_special_char_index = -1;
    for (int i = 0; i < char_count; i++)
    {
        char c = text[i];
        if (c <= 31) continue;
        non_special_char_index += 1;
       
        Glyph_Information_& glyph = atlas->glyph_informations[atlas->character_to_glyph_map[(int)c]];
        Sub_Image& sub_image = bitmap_atlas->sub_images[glyph.sub_image_index];

        ivec2 screen_pos = ivec2(
            position.x + atlas->char_box_size.x * non_special_char_index + glyph.placement_offset.x, 
            position.y + glyph.placement_offset.y
        );
        ivec2& bitmap_size = sub_image.size;

        vec2 normalized_pos(0.0f);
        normalized_pos.x = 2.0f * (screen_pos.x / (float)screen_size.x) - 1.0f;
        normalized_pos.y = 2.0f * (screen_pos.y / (float)screen_size.y) - 1.0f;
        vec2 normalized_size = vec2(bitmap_size.x / (float)screen_size.x * 2.0f, bitmap_size.y / (float)screen_size.y * 2.0f);

        pos_data[non_special_char_index * 4 + 0] = normalized_pos + normalized_size * vec2(0, 0);
        pos_data[non_special_char_index * 4 + 1] = normalized_pos + normalized_size * vec2(1, 0);
        pos_data[non_special_char_index * 4 + 2] = normalized_pos + normalized_size * vec2(1, 1);
        pos_data[non_special_char_index * 4 + 3] = normalized_pos + normalized_size * vec2(0, 1);

        vec4 color(1.0f);
        color_data[non_special_char_index * 4 + 0] = color;
        color_data[non_special_char_index * 4 + 1] = color;
        color_data[non_special_char_index * 4 + 2] = color;
        color_data[non_special_char_index * 4 + 3] = color;

        vec2 uv_pos = vec2(sub_image.position.x / (float)bitmap_atlas->bitmap.size.x, sub_image.position.y / (float)bitmap_atlas->bitmap.size.y);
        vec2 uv_size = vec2(sub_image.size.x / (float)bitmap_atlas->bitmap.size.x, sub_image.size.y / (float)bitmap_atlas->bitmap.size.y);
        uv_data[non_special_char_index * 4 + 0] = uv_pos + uv_size * vec2(0, 0);
        uv_data[non_special_char_index * 4 + 1] = uv_pos + uv_size * vec2(1, 0);
        uv_data[non_special_char_index * 4 + 2] = uv_pos + uv_size * vec2(1, 1);
        uv_data[non_special_char_index * 4 + 3] = uv_pos + uv_size * vec2(0, 1);

        indices[non_special_char_index * 6 + 0] = start_vertex_count + non_special_char_index * 4 + 0;
        indices[non_special_char_index * 6 + 1] = start_vertex_count + non_special_char_index * 4 + 1;
        indices[non_special_char_index * 6 + 2] = start_vertex_count + non_special_char_index * 4 + 2;
        indices[non_special_char_index * 6 + 3] = start_vertex_count + non_special_char_index * 4 + 0;
        indices[non_special_char_index * 6 + 4] = start_vertex_count + non_special_char_index * 4 + 2;
        indices[non_special_char_index * 6 + 5] = start_vertex_count + non_special_char_index * 4 + 3;
    }
}

void mesh_push_subimage(Mesh* mesh, Bitmap_Atlas* bitmap_atlas, Sub_Image sub_image, ivec2 position, ivec2 screen_size)
{
    u32 start_vertex_count = mesh->vertex_count;
    auto& predef = rendering_core.predefined;
    Array<vec2> pos_data =   mesh_push_attribute_slice(mesh, predef.position2D,          4);
    Array<vec4> color_data = mesh_push_attribute_slice(mesh, predef.color4,              4);
    Array<vec2> uv_data =    mesh_push_attribute_slice(mesh, predef.texture_coordinates, 4);
    Array<u32> indices =     mesh_push_attribute_slice(mesh, predef.index,               6);

    {
        vec2 normalized_pos(0.0f);
        normalized_pos.x = 2.0f * (position.x / (float)screen_size.x) - 1.0f;
        normalized_pos.y = 2.0f * (position.y / (float)screen_size.y) - 1.0f;
        vec2 normalized_size = vec2(sub_image.size.x / (float)screen_size.x * 2.0f, sub_image.size.y / (float)screen_size.y * 2.0f);

        pos_data[0] = normalized_pos + normalized_size * vec2(0, 0);
        pos_data[1] = normalized_pos + normalized_size * vec2(1, 0);
        pos_data[2] = normalized_pos + normalized_size * vec2(1, 1);
        pos_data[3] = normalized_pos + normalized_size * vec2(0, 1);

        vec4 color(1.0f);
        color_data[0] = color;
        color_data[1] = color;
        color_data[2] = color;
        color_data[3] = color;

        vec2 uv_pos = vec2(sub_image.position.x / (float)bitmap_atlas->bitmap.size.x, sub_image.position.y / (float)bitmap_atlas->bitmap.size.y);
        vec2 uv_size = vec2(sub_image.size.x / (float)bitmap_atlas->bitmap.size.x, sub_image.size.y / (float)bitmap_atlas->bitmap.size.y);
        uv_data[0] = uv_pos + uv_size * vec2(0, 0);
        uv_data[1] = uv_pos + uv_size * vec2(1, 0);
        uv_data[2] = uv_pos + uv_size * vec2(1, 1);
        uv_data[3] = uv_pos + uv_size * vec2(0, 1);

        indices[0] = start_vertex_count + 0;
        indices[1] = start_vertex_count + 1;
        indices[2] = start_vertex_count + 2;
        indices[3] = start_vertex_count + 0;
        indices[4] = start_vertex_count + 2;
        indices[5] = start_vertex_count + 3;
    }
}

void imgui_test_entry()
{
    timer_initialize();

    // Create window
    Window* window = window_create("Test", 0);
    SCOPE_EXIT(window_destroy(window));
    window_load_position(window, "window_pos.set");
    opengl_state_set_clear_color(vec4(0.0f));
    window_set_vsync(window, true);

    // Prepare rendering core
    Window_State* window_state = window_get_window_state(window);
    rendering_core_initialize(window_state->width, window_state->height, window_state->dpi);
    SCOPE_EXIT(rendering_core_destroy());
    Camera_3D* camera = camera_3D_create(math_degree_to_radians(90), 0.1f, 100.0f);
    SCOPE_EXIT(camera_3D_destroy(camera));

    // Create our data
    Bitmap_Atlas bitmap_atlas = bitmap_atlas_create(ivec2(256));
    SCOPE_EXIT(bitmap_atlas_destroy(&bitmap_atlas));

    // Initialize atlas data
    {
        Bitmap& bitmap = bitmap_atlas.bitmap;
        for (int i = 0; i < bitmap.size.x * bitmap.size.y; i++) {
            bitmap.data[i] = 255;
        }
        for (int x = 0; x < bitmap.size.x; x++) {
            for (int y = 0; y < bitmap.size.y; y++) {
                u8 value = 0;
                if (x / 4 % 2 == 0) {
                    value = 255;
                }
                value = (int)(value * (float)y / bitmap.size.y);
                bitmap.data[x + y * bitmap.pitch] = value;
            }
        }
    }

    Glyph_Atlas_ glyph_atlas = glyph_atlas_create();
    SCOPE_EXIT(glyph_atlas_destroy(&glyph_atlas));
    glyph_atlas_rasterize_font(&glyph_atlas, &bitmap_atlas, "resources/fonts/consola.ttf", 20);

    Glyph_Atlas_ smoll_atlas = glyph_atlas_create();
    SCOPE_EXIT(glyph_atlas_destroy(&smoll_atlas));
    glyph_atlas_rasterize_font(&smoll_atlas, &bitmap_atlas, "resources/fonts/consola.ttf", 14);


    // Create GPU texture
    Texture* texture = texture_create_from_bytes(
        Texture_Type::RED_U8,
        array_create_static((byte*)bitmap_atlas.bitmap.data, bitmap_atlas.bitmap.size.x * bitmap_atlas.bitmap.size.y),
        bitmap_atlas.bitmap.size.x, bitmap_atlas.bitmap.size.y,
        false
    );
    SCOPE_EXIT(texture_destroy(texture));

    auto& predef = rendering_core.predefined;
    Vertex_Description* vertex_desc = vertex_description_create({ predef.position2D, predef.texture_coordinates, predef.color4, predef.index });
    Mesh* mesh = rendering_core_query_mesh("Mono_Render_Mesh", vertex_desc, true);
    Shader* shader = rendering_core_query_shader("mono_texture.glsl");

    // Window Loop
    double time_last_update_start = timer_current_time_in_seconds();
    while (true)
    {
        double time_frame_start = timer_current_time_in_seconds();
        float time_since_last_update = (float)(time_frame_start - time_last_update_start);
        time_last_update_start = time_frame_start;

        // Input Handling
        Input* input = window_get_input(window);
        {
            int msg_count = 0;
            if (!window_handle_messages(window, true, &msg_count)) {
                break;
            }

            if (input->close_request_issued ||
                (input->key_pressed[(int)Key_Code::ESCAPE] && (input->key_down[(int)Key_Code::SHIFT] || input->key_down[(int)Key_Code::CTRL])))
            {
                window_save_position(window, "window_pos.set");
                window_close(window);
                break;
            }
            if (input->key_pressed[(int)Key_Code::F11]) {
                Window_State* state = window_get_window_state(window);
                window_set_fullscreen(window, !state->fullscreen);
            }
        }

        // Rendering
        {
            rendering_core_prepare_frame(timer_current_time_in_seconds(), window_state->width, window_state->height);

            // Render mesh with bitmap at the center of the screen
            ivec2 screen_size = ivec2(window_state->width, window_state->height);
            ivec2 center = ivec2(screen_size.x / 2, screen_size.y / 2);
            ivec2 mouse = ivec2(input->mouse_x, window_state->height - input->mouse_y);
            if (input->mouse_down[(int)Mouse_Key_Code::LEFT]) {
                center = mouse;
            }

            // mesh_push_text(mesh, &glyph_atlas, &bitmap_atlas, "Hello WORLDyyj!", center, screen_size);
            // center.y -= glyph_atlas.char_box_size.y;
            // mesh_push_text(mesh, &glyph_atlas, &bitmap_atlas, "Hello WORLDyyj!", center, screen_size);

            Sub_Image sub_image;
            sub_image.position = ivec2(0);
            sub_image.size = bitmap_atlas.bitmap.size;
            mesh_push_subimage(mesh, &bitmap_atlas, sub_image, center, screen_size);

            Render_Pass* pass_2d = rendering_core_query_renderpass("2D-Pass", pipeline_state_make_alpha_blending(), nullptr);
            render_pass_draw(pass_2d, shader, mesh, Mesh_Topology::TRIANGLES, { uniform_make("u_sampler", texture, sampling_mode_nearest()) });

            rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
            window_swap_buffers(window);
        }

        // Sleep
        {
            input_reset(input); // Clear input for next frame
            const int TARGET_FPS = 60;
            const double SECONDS_PER_FRAME = 1.0 / TARGET_FPS;
            timer_sleep_until(time_frame_start + SECONDS_PER_FRAME);
        }
    }
}
