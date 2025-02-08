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
    explicit ivec2(int val) : x(val), y(val) {}
    explicit ivec2(int x, int y) : x(x), y(y) {}

    ivec2 operator+(ivec2& other) { return ivec2(this->x + other.x, this->y + other.y); }
    ivec2 operator-(ivec2& other) { return ivec2(this->x - other.x, this->y - other.y); }
    ivec2 operator*(ivec2& other) { return ivec2(this->x * other.x, this->y * other.y); }
    ivec2 operator/(ivec2& other) { return ivec2(this->x / other.x, this->y / other.y); }
    ivec2 operator+(int value) { return ivec2(this->x + value, this->y + value); }
    ivec2 operator-(int value) { return ivec2(this->x - value, this->y - value); }
    ivec2 operator*(int value) { return ivec2(this->x * value, this->y * value); }
    ivec2 operator/(int value) { return ivec2(this->x / value, this->y / value); }

    int x;
    int y;
};

struct BBox
{
    BBox() {}
    explicit BBox(ivec2 val) { min = val; max = val; };
    explicit BBox(ivec2 min, ivec2 max) { this->min = min; this->max = max; };

    ivec2 min;
    ivec2 max;
};

ivec2 bbox_get_corner(BBox box, ivec2 dir)
{
    ivec2 result;
    if (dir.x < 0)       { result.x = box.min.x; }
    else if (dir.x == 0) { result.x = box.min.x + box.max.x / 2; }
    else                 { result.x = box.max.x; }

    if (dir.y < 0)       { result.y = box.min.y; }
    else if (dir.y == 0) { result.y = box.min.y + box.max.y / 2; }
    else                 { result.y = box.max.y; }

    return result;
}

bool bbox_contains_point(BBox box, ivec2 point)
{
    return
        box.min.x <= point.x &&
        box.max.x > point.x &&
        box.min.y <= point.y &&
        box.max.y > point.y;
}

// Returns signed distance from point to border
float bbox_sdf_to_point(BBox box, ivec2 point_int)
{
    vec2 center = vec2(box.min.x + box.max.x, box.min.y + box.max.y) / 2.0f;
    vec2 half_size = vec2(box.max.x - box.min.x, box.max.y - box.min.y) / 2.0f;
    vec2 point = vec2(point_int.x, point_int.y);

    vec2 offset = point - center;
    // Handle mirror cases
    offset.x = math_absolute(offset.x);
    offset.y = math_absolute(offset.y);
    // Turn offset into offset to corner
    offset = offset - half_size;
    if (offset.x <= 0 && offset.y <= 0) {
        return math_maximum(offset.x, offset.y);
    }

    // Otherwise coordinate-wise clamp to 0 and take distance
    offset.x = math_maximum(0.0f, offset.x);
    offset.y = math_maximum(0.0f, offset.y);
    return vector_length(offset);
}

BBox bbox_intersection(BBox a, BBox b)
{
    BBox result;
    result.min.x = math_maximum(a.min.x, b.min.x);
    result.min.y = math_maximum(a.min.y, b.min.y);
    result.max.x = math_maximum(result.min.x, math_minimum(a.max.x, b.max.x));
    result.max.y = math_maximum(result.min.y, math_minimum(a.max.y, b.max.y));
    return result;
}

bool bbox_is_empty(BBox box) {
    return 
        box.max.x <= box.min.x ||
        box.max.y <= box.min.y;
}
bool bbox_equals(BBox a, BBox b) {
    return a.max.x == b.max.x && a.max.y == b.max.y && a.min.x == b.min.x && a.min.y == b.min.y;
}



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

Bitmap bitmap_create_static(ivec2 size, u8* data, int pitch)
{
    Bitmap bitmap;
    bitmap.size = size;
    bitmap.data = data;
    bitmap.pitch = pitch;
    return bitmap;
}

void bitmap_destroy(Bitmap bitmap)
{
    delete[] bitmap.data;
    bitmap.data = nullptr;
}

void bitmap_block_transfer_(Bitmap destination, Bitmap source, ivec2 position, bool mirror_y = false)
{
    if (position.x < 0 || position.y < 0 || position.x + source.size.x >= destination.size.x || position.y + source.size.y >= destination.size.y) {
        panic("Caller must make sure to not overdraw!\n");
        return;
    }

    // Note: You can probably do something more efficient for mirror_y by using negative pitch and changing source.data pointer
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



struct Bitmap_Atlas_Writer
{
    Bitmap* bitmap;
    ivec2 write_pos;
    int max_subimage_height_in_current_line; // E.g. max height of su
};

Bitmap_Atlas_Writer bitmap_atlas_writer_make(Bitmap* bitmap)
{
    Bitmap_Atlas_Writer result;
    result.bitmap = bitmap;
    result.max_subimage_height_in_current_line = 0;
    result.write_pos = ivec2(0);
    return result;
}

BBox bitmap_atlas_add_sub_image(Bitmap_Atlas_Writer* atlas, Bitmap bitmap, bool mirror_y = false)
{
    auto& write_pos = atlas->write_pos;
    auto& atlas_size = atlas->bitmap->size;

    // Check if atlas-bitmap is large enough for given bitmap and position
    if (bitmap.size.x >= atlas_size.x || bitmap.size.y >= atlas_size.y) {
        return BBox(ivec2(0));
    }

    // Jump to next line in atlas if current line is full
    if (write_pos.x + bitmap.size.x >= atlas_size.x) 
    {
        // Check if atlas is exhausted (No more free space)
        int next_write_y = write_pos.y + atlas->max_subimage_height_in_current_line;
        if (next_write_y + bitmap.size.y >= atlas_size.y) {
            return BBox(ivec2(0));
        }

        write_pos.x = 0;
        write_pos.y = next_write_y;
        atlas->max_subimage_height_in_current_line = 0;
    }

    // Store information
    BBox result_box = BBox(write_pos, write_pos + bitmap.size);
    write_pos.x += bitmap.size.x;
    bitmap_block_transfer_(*atlas->bitmap, bitmap, result_box.min, mirror_y);
    atlas->max_subimage_height_in_current_line = math_maximum(atlas->max_subimage_height_in_current_line, bitmap.size.y);

    return result_box;
}



struct Glyph_Information_
{
    unsigned char character;
    BBox atlas_box;
    ivec2 placement_offset; // Where to place the bitmap-quad with respect to the current line cursor (Positive)
};

struct Glyph_Atlas_
{
    ivec2 char_box_size;
    ivec2 bitmap_atlas_size;
    Dynamic_Array<Glyph_Information_> glyph_informations;
    Array<int> character_to_glyph_map; // Maps 0-255 to a glyph information index (But we aren't using >127, so all upper values map to the error glyph!
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

void glyph_atlas_rasterize_font(Glyph_Atlas_* glyph_atlas, Bitmap_Atlas_Writer* atlas_writer, const char* font_filepath, u32 pixel_height)
{
    glyph_atlas->char_box_size = ivec2(0);
    dynamic_array_reset(&glyph_atlas->glyph_informations);
    for (int i = 0; i < glyph_atlas->character_to_glyph_map.size; i++) {
        glyph_atlas->character_to_glyph_map[i] = 0;
    }
    glyph_atlas->bitmap_atlas_size = atlas_writer->bitmap->size;

    u8 value_zero = 0;
    BBox empty_pixel_box = bitmap_atlas_add_sub_image(atlas_writer, bitmap_create_static(ivec2(1, 1), &value_zero, 1));

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
    int max_y_index = -1;
    int max_advance = 0;

    // Start with first printable ascii character (Space = 32) until end of ASCII (up to 126, #127 is non-printable)
    for (int i = 31; i < 127; i++)
    {
        // Get Glyph index
        // Note: We start with 31, to assert that the 'unknown-glyph' gets added as glyph_info index 0
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

        // Write glyph bitmap into atlas-bitmap
        BBox atlas_position = empty_pixel_box;
        ivec2 pixel_size = ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows);
        if (pixel_size.x != 0 && pixel_size.y != 0) 
        {
            // Special handling for Space ' ', or other characters without any pixels (Which use the empty pixel box)
            Bitmap glyph_bitmap = bitmap_create_static(pixel_size, face->glyph->bitmap.buffer, face->glyph->bitmap.pitch);
            atlas_position = bitmap_atlas_add_sub_image(atlas_writer, glyph_bitmap, true);
            if (bbox_is_empty(atlas_position)) {
                logg("Bitmap atlas size is too small for further glyphs\n");
                continue;
            }
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
        if (max_y < (int)face->glyph->metrics.horiBearingY / 64) {
            max_y = (int)face->glyph->metrics.horiBearingY / 64;
            max_y_index = i;
        }
        // max_y = math_maximum(max_y, (int)face->glyph->metrics.horiBearingY / 64);

        // Create Glyph information
        Glyph_Information_ information;
        information.character = current_character;
        information.atlas_box = atlas_position;
        information.placement_offset.x = face->glyph->metrics.horiBearingX / 64;
        information.placement_offset.y = face->glyph->metrics.horiBearingY / 64 - pixel_size.y; // Note: Usually negative/0

        // Add glyph information into information array
        dynamic_array_push_back(&glyph_atlas->glyph_informations, information);
        glyph_atlas->character_to_glyph_map[current_character] = glyph_atlas->glyph_informations.size-1;
    }

    printf("Max-Y character: '%c' (#%d)\n", max_y_index, max_y_index);

    // Adjust placement offsets so we only deal with 
    for (int i = 0; i < glyph_atlas->glyph_informations.size; i++) {
        auto& glyph = glyph_atlas->glyph_informations[i];
        glyph.placement_offset.y += -min_y;
    }
    glyph_atlas->char_box_size.x = max_advance;
    glyph_atlas->char_box_size.y = max_y - min_y;
}



void mesh_push_text(Mesh* mesh, Glyph_Atlas_* atlas, String text, ivec2 position)
{
    vec2 screen_size = vec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);
    vec2 bitmap_size = vec2(atlas->bitmap_atlas_size.x, atlas->bitmap_atlas_size.y);

    u32 start_vertex_count = mesh->vertex_count;
    auto& predef = rendering_core.predefined;
    Array<vec2> pos_data =   mesh_push_attribute_slice(mesh, predef.position2D,          4 * text.size);
    Array<vec4> color_data = mesh_push_attribute_slice(mesh, predef.color4,              4 * text.size);
    Array<vec2> uv_data =    mesh_push_attribute_slice(mesh, predef.texture_coordinates, 4 * text.size);
    Array<u32> indices =     mesh_push_attribute_slice(mesh, predef.index,               6 * text.size);

    for (int i = 0; i < text.size; i++)
    {
        char c = text.characters[i];
        Glyph_Information_& glyph = atlas->glyph_informations[atlas->character_to_glyph_map[(int)c]];

        ivec2 screen_pos = ivec2(
            position.x + atlas->char_box_size.x * i + glyph.placement_offset.x, 
            position.y + glyph.placement_offset.y
        );
        ivec2& glyph_size = glyph.atlas_box.max - glyph.atlas_box.min;

        vec2 normalized_pos = 2.0f * vec2(screen_pos.x, screen_pos.y) / screen_size - 1.0f;
        vec2 normalized_size = 2.0f * vec2(glyph_size.x, glyph_size.y) / screen_size;

        pos_data[i * 4 + 0] = normalized_pos + normalized_size * vec2(0, 0);
        pos_data[i * 4 + 1] = normalized_pos + normalized_size * vec2(1, 0);
        pos_data[i * 4 + 2] = normalized_pos + normalized_size * vec2(1, 1);
        pos_data[i * 4 + 3] = normalized_pos + normalized_size * vec2(0, 1);

        vec4 color(1.0f);
        color_data[i * 4 + 0] = color;
        color_data[i * 4 + 1] = color;
        color_data[i * 4 + 2] = color;
        color_data[i * 4 + 3] = color;

        vec2 uv_min = vec2(glyph.atlas_box.min.x, glyph.atlas_box.min.y) / bitmap_size;
        vec2 uv_max = vec2(glyph.atlas_box.max.x, glyph.atlas_box.max.y) / bitmap_size;
        uv_data[i * 4 + 0] = uv_min; 
        uv_data[i * 4 + 1] = vec2(uv_max.x, uv_min.y);
        uv_data[i * 4 + 2] = uv_max;
        uv_data[i * 4 + 3] = vec2(uv_min.x, uv_max.y);

        indices[i * 6 + 0] = start_vertex_count + i * 4 + 0;
        indices[i * 6 + 1] = start_vertex_count + i * 4 + 1;
        indices[i * 6 + 2] = start_vertex_count + i * 4 + 2;
        indices[i * 6 + 3] = start_vertex_count + i * 4 + 0;
        indices[i * 6 + 4] = start_vertex_count + i * 4 + 2;
        indices[i * 6 + 5] = start_vertex_count + i * 4 + 3;
    }
}

void mesh_push_text_clipped(Mesh* mesh, Glyph_Atlas_* atlas, String text, ivec2 position, BBox clipping_box)
{
    if (text.size == 0) return;
    BBox text_box = BBox(position, position + atlas->char_box_size * ivec2(text.size, 1));
    BBox intersection = bbox_intersection(text_box, clipping_box);
    if (bbox_is_empty(intersection)) {
        return;
    }
    if (bbox_equals(intersection, text_box)) {
        mesh_push_text(mesh, atlas, text, position);
        return;
    }

    vec2 screen_size = vec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);
    vec2 bitmap_size = vec2(atlas->bitmap_atlas_size.x, atlas->bitmap_atlas_size.y);

    // Figure out how many characters are going to be visible
    int char_start_index = 0;
    int char_end_index = text.size; // Exclusive index
    {
        int start_clip = math_maximum(clipping_box.min.x - position.x, 0);
        char_start_index = start_clip / atlas->char_box_size.x;
        int end_clip = math_maximum(text_box.max.x - clipping_box.max.x, 0);
        char_end_index = text.size - end_clip / atlas->char_box_size.x;
    }
    int char_count = char_end_index - char_start_index;

    auto& predef = rendering_core.predefined;
    Attribute_Buffer* pos_buffer   = mesh_get_raw_attribute_buffer(mesh, predef.position2D);
    Attribute_Buffer* color_buffer = mesh_get_raw_attribute_buffer(mesh, predef.color4);
    Attribute_Buffer* uv_buffer    = mesh_get_raw_attribute_buffer(mesh, predef.texture_coordinates);
    Attribute_Buffer* index_buffer = mesh_get_raw_attribute_buffer(mesh, predef.index);

    for (int i = char_start_index; i < char_end_index; i++)
    {
        char c = text.characters[i];
        if (c == ' ') continue;
        Glyph_Information_& glyph = atlas->glyph_informations[atlas->character_to_glyph_map[(int)c]];

        // Calculate and Clip Glyph-BBox
        ivec2 screen_pos = ivec2(
            position.x + atlas->char_box_size.x * i + glyph.placement_offset.x, 
            position.y + glyph.placement_offset.y
        );
        BBox glyph_box = BBox(screen_pos, screen_pos + glyph.atlas_box.max - glyph.atlas_box.min);
        BBox clip_box = bbox_intersection(glyph_box, clipping_box);
        if (bbox_is_empty(clip_box)) continue;

        // Generate Vertex-Data
        u32 start_vertex_count = mesh->vertex_count;
        Array<vec2> pos_data   = attribute_buffer_allocate_slice<vec2>(pos_buffer, 4);
        Array<vec4> color_data = attribute_buffer_allocate_slice<vec4>(color_buffer, 4);
        Array<vec2> uv_data    = attribute_buffer_allocate_slice<vec2>(uv_buffer, 4);
        Array<u32>  indices    = attribute_buffer_allocate_slice<u32> (index_buffer, 6);

        ivec2& pixel_size = clip_box.max - clip_box.min;
        vec2 min_pos = 2.0f * vec2(clip_box.min.x, clip_box.min.y) / screen_size - 1.0f;
        vec2 max_pos = 2.0f * vec2(clip_box.max.x, clip_box.max.y) / screen_size - 1.0f;
        pos_data[0] = min_pos;
        pos_data[1] = vec2(max_pos.x, min_pos.y);
        pos_data[2] = max_pos;
        pos_data[3] = vec2(min_pos.x, max_pos.y);

        BBox uv_box = glyph.atlas_box;
        uv_box.min = glyph.atlas_box.min + clip_box.min - glyph_box.min;
        uv_box.max = glyph.atlas_box.max + clip_box.max - glyph_box.max;
        vec2 uv_min = vec2(uv_box.min.x, uv_box.min.y) / bitmap_size;
        vec2 uv_max = vec2(uv_box.max.x, uv_box.max.y) / bitmap_size;
        uv_data[0] = uv_min; 
        uv_data[1] = vec2(uv_max.x, uv_min.y);
        uv_data[2] = uv_max;
        uv_data[3] = vec2(uv_min.x, uv_max.y);

        vec4 color(1.0f);
        color_data[0] = color;
        color_data[1] = color;
        color_data[2] = color;
        color_data[3] = color;

        indices[0] = start_vertex_count + 0;
        indices[1] = start_vertex_count + 1;
        indices[2] = start_vertex_count + 2;
        indices[3] = start_vertex_count + 0;
        indices[4] = start_vertex_count + 2;
        indices[5] = start_vertex_count + 3;
    }
}

void mesh_push_subimage(Mesh* mesh, ivec2 position, BBox subimage, ivec2 atlas_bitmap_size)
{
    vec2 screen_size = vec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);
    vec2 bitmap_size = vec2(atlas_bitmap_size.x, atlas_bitmap_size.y);

    u32 start_vertex_count = mesh->vertex_count;
    auto& predef = rendering_core.predefined;
    Array<vec2> pos_data =   mesh_push_attribute_slice(mesh, predef.position2D,          4);
    Array<vec4> color_data = mesh_push_attribute_slice(mesh, predef.color4,              4);
    Array<vec2> uv_data =    mesh_push_attribute_slice(mesh, predef.texture_coordinates, 4);
    Array<u32> indices =     mesh_push_attribute_slice(mesh, predef.index,               6);

    {
        ivec2& glyph_size = subimage.max - subimage.min;

        vec2 normalized_pos = 2.0f * vec2(position.x, position.y) / screen_size - 1.0f;
        vec2 normalized_size = 2.0f * vec2(glyph_size.x, glyph_size.y) / screen_size;

        pos_data[0] = normalized_pos + normalized_size * vec2(0, 0);
        pos_data[1] = normalized_pos + normalized_size * vec2(1, 0);
        pos_data[2] = normalized_pos + normalized_size * vec2(1, 1);
        pos_data[3] = normalized_pos + normalized_size * vec2(0, 1);

        vec4 color(1.0f);
        color_data[0] = color;
        color_data[1] = color;
        color_data[2] = color;
        color_data[3] = color;

        vec2 uv_min = vec2(subimage.min.x, subimage.min.y) / bitmap_size;
        vec2 uv_max = vec2(subimage.max.x, subimage.max.y) / bitmap_size;
        uv_data[0] = uv_min; 
        uv_data[1] = vec2(uv_max.x, uv_min.y);
        uv_data[2] = uv_max;
        uv_data[3] = vec2(uv_min.x, uv_max.y);

        indices[0] = start_vertex_count + 0;
        indices[1] = start_vertex_count + 1;
        indices[2] = start_vertex_count + 2;
        indices[3] = start_vertex_count + 0;
        indices[4] = start_vertex_count + 2;
        indices[5] = start_vertex_count + 3;
    }
}

void mesh_push_box(Mesh* mesh, BBox box, vec4 color)
{
    if (bbox_is_empty(box)) return;

    u32 start_vertex_count = mesh->vertex_count;
    auto& predef = rendering_core.predefined;
    Array<vec2> pos_data   = mesh_push_attribute_slice(mesh, predef.position2D,          4);
    Array<vec4> color_data = mesh_push_attribute_slice(mesh, predef.color4,              4);
    Array<vec2> uv_data    = mesh_push_attribute_slice(mesh, predef.texture_coordinates, 4);
    Array<u32>  indices    = mesh_push_attribute_slice(mesh, predef.index,               6);

    {
        vec2 min = vec2(box.min.x, box.min.y);
        vec2 max = vec2(box.max.x, box.max.y);
        vec2 screen_size = vec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);

        // Normalize to screen-coordinates
        min = 2 * min / screen_size - 1.0f;
        max = 2 * max / screen_size - 1.0f;

        pos_data[0] = min; 
        pos_data[1] = vec2(max.x, min.y);
        pos_data[2] = max;
        pos_data[3] = vec2(min.x, max.y);

        color_data[0] = color;
        color_data[1] = color;
        color_data[2] = color;
        color_data[3] = color;

        // Note: We set the pixel at 0 0 to 1, so we can use this for colored rectangles
        vec2 uv_pos = vec2(0.0f);
        uv_data[0] = uv_pos;
        uv_data[1] = uv_pos;
        uv_data[2] = uv_pos;
        uv_data[3] = uv_pos;

        indices[0] = start_vertex_count + 0;
        indices[1] = start_vertex_count + 1;
        indices[2] = start_vertex_count + 2;
        indices[3] = start_vertex_count + 0;
        indices[4] = start_vertex_count + 2;
        indices[5] = start_vertex_count + 3;
    }
}

void mesh_push_box_clipped(Mesh* mesh, BBox box, BBox clipping_box, vec4 color) {
    mesh_push_box(mesh, bbox_intersection(box, clipping_box), color);
}

// Pushes a border inside the given box
void mesh_push_inner_border_clipped(Mesh* mesh, BBox box, BBox clipping_box, vec4 border_color, int border_thickness)
{
    if (border_thickness <= 0) {
        return;
    }
    ivec2 size = box.max - box.min;
    if (size.x <= border_thickness * 2 || size.y <= border_thickness * 2) {
        mesh_push_box_clipped(mesh, box, clipping_box, border_color);
        return;
    }

    // Left/Right borders
    mesh_push_box_clipped(mesh, BBox(box.min, ivec2(box.min.x + border_thickness, box.max.y)), clipping_box, border_color);
    mesh_push_box_clipped(mesh, BBox(ivec2(box.max.x - border_thickness, box.min.y), box.max), clipping_box, border_color);
    // Top/Bottom borders
    mesh_push_box_clipped(mesh, BBox(ivec2(box.min.x + border_thickness, box.min.y), ivec2(box.max.x - border_thickness, box.min.y + border_thickness)), clipping_box, border_color);
    mesh_push_box_clipped(mesh, BBox(ivec2(box.min.x + border_thickness, box.max.y - border_thickness), ivec2(box.max.x - border_thickness, box.max.y)), clipping_box, border_color);
}

void mesh_push_box_with_border_clipped(Mesh* mesh, BBox box, BBox clipping_box, vec4 color, int border_thickness, vec4 border_color)
{
    if (border_thickness <= 0) {
        mesh_push_box_clipped(mesh, box, clipping_box, color);
        return;
    }
    // Handle case where border is larger than 'client'
    ivec2 size = box.max - box.min;
    if (size.x <= border_thickness * 2 || size.y <= border_thickness * 2) {
        mesh_push_box_clipped(mesh, box, clipping_box, border_color);
        return;
    }

    mesh_push_inner_border_clipped(mesh, box, clipping_box, border_color, border_thickness);
    mesh_push_box_clipped(mesh, BBox(box.min + border_thickness, box.max - border_thickness), clipping_box, color);
}




// Constants
const int PAD_TOP = 2;
const int PAD_BOT = 1;
const int PAD_LEFT_RIGHT = 2;
const int BORDER_SPACE = 1;

const int PAD_LABEL_BOX = 1;
const int PAD_ADJACENT_LABLE_LINE_SPLIT = 6;

const int PAD_WIDGETS_ON_LINE = 6;
const int PAD_WIDGETS_BETWEEN_LINES = 1;

vec4 vec4_color_from_rgb(u8 r, u8 g, u8 b) {
    return vec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

const vec4 COLOR_WINDOW_BG        = vec4_color_from_rgb(0x16, 0x85, 0x5B);
const vec4 COLOR_WINDOW_BG_HEADER = vec4_color_from_rgb(0x62, 0xA1, 0x99);
const vec4 COLOR_SCROLL_BG        = vec4_color_from_rgb(0xCE, 0xCE, 0xCE);
const vec4 COLOR_SCROLL_BAR       = vec4_color_from_rgb(0x9D, 0x9D, 0x9D);
const vec4 COLOR_BUTTON_BORDER    = vec4_color_from_rgb(0x19, 0x75, 0xD0);
const vec4 COLOR_BUTTON_BG        = vec4_color_from_rgb(0x0F, 0x47, 0x7E);
const vec4 COLOR_BUTTON_BG_HOVER  = vec4_color_from_rgb(0x71, 0xA9, 0xE2);
const vec4 COLOR_INPUT_BG         = vec4_color_from_rgb(0x9C, 0xA3, 0xAC);
const vec4 COLOR_INPUT_BORDER     = vec4_color_from_rgb(0x70, 0x73, 0x76);
const vec4 COLOR_LIST_LINE_EVEN   = vec4_color_from_rgb(0xFE, 0xCB, 0xA3);
const vec4 COLOR_LIST_LINE_ODD    = vec4_color_from_rgb(0xB6, 0xB1, 0xAC);



enum class Layout_Type
{
    NORMAL, // Stack-Horizontal with option to combine lines
    STACK_HORIZONTAL, // All widgets are added in a single line
    LABELED_ITEMS // Collapsable label items
};

struct Widget;
struct Widget_Container
{
    Layout_Type layout;
    union {
        struct 
        {
            bool allow_line_combination;
            bool scroll_bar_enabled;
            int min_line_count; // 0 for normal behavior
            int max_line_count; // 0 or -1 to disable
        } normal;
        String label_text;
    } options;
    Dynamic_Array<Widget> widgets;

    // Intermediate layout data
    int max_child_min_width_collapsed;
    int max_child_min_width_without_collapse;
    int max_child_min_width_for_line_merge;
    int min_child_size_for_line_merge;
    int sum_child_min_width_collapsed;
    int sum_child_min_width_without_collapse;
    int sum_child_min_width_for_line_merge;

    int line_count;
    int sum_line_min_heights;
    int sum_line_max_heights;
    int growable_line_count;
    bool scroll_bar_was_added;
};

struct Layout_Info
{
    // Layout information (Needs to be reported by widget)
    bool can_combine_in_lines;
    int min_width_collapsed; 
    int min_width_without_collapse;
    int min_width_for_line_merge;

    // Given the available width, widgets can calculate their min, and max height
    int min_height;
    int max_height;
    bool height_can_grow; // For widgets that want to grow in y (Lists or others)

    // Calculated by container
    BBox box;
    int line_index;
};

enum class Widget_Type
{
    LABEL,
    TEXT_INPUT,
    BUTTON,
    CONTAINER
};

struct Widget
{
    // Widget Data
    Widget_Type type;
    union {
        String label; 
        String button_text;
        String input_text;
        Widget_Container container;
    } options;

    Layout_Info layout_info;
};

struct UI_Window
{
    String title;
    ivec2 position;
    ivec2 size;
    Widget_Container container;
};

const int LABEL_CHAR_COUNT_SIZE = 12;
const int TEXT_INPUT_MIN_CHAR_COUNT = 10;
const int TEXT_INPUT_MAX_CHAR_COUNT = 20;
const int BUTTON_MIN_CHAR_COUNT = 6;
const int BUTTON_WANTED_CHAR_COUNT = 10;
const int LIST_CONTAINER_MIN_CHAR_COUNT = 16;

const int SCROLL_BAR_WIDTH = 10;
const int MIN_SCROLL_BAR_HEIGHT = 10;
const int SCROLL_BAR_PADDING = 1; // Top/Bot/Left/Right padding
const int MOUSE_WHEEL_SENSITIVITY = 15;

void widget_container_gather_width_information_recursive(Widget_Container* container, Layout_Info* out_layout_info, ivec2 char_size)
{
    const int LINE_ITEM_HEIGHT = PAD_TOP + PAD_BOT + BORDER_SPACE + char_size.y;
    const int TEXT_BORDER_SPACE = BORDER_SPACE * 2 + PAD_LEFT_RIGHT * 2;
    auto& widgets = container->widgets;

    // Calculate child width infos
    Widget_Container* c = container;
    c->max_child_min_width_collapsed = 0;
    c->max_child_min_width_without_collapse = 0;
    c->max_child_min_width_for_line_merge = 0;
    c->sum_child_min_width_collapsed = 0;
    c->sum_child_min_width_without_collapse = 0;
    c->sum_child_min_width_for_line_merge = 0;
    c->min_child_size_for_line_merge = widgets.size == 0 ? 0 : 1000000;
    c->scroll_bar_was_added = false;
    bool child_height_can_grow = false;
    bool has_child_that_cannot_combine_in_line = false;
    for (int i = 0; i < widgets.size; i++)
    {
        auto& widget = widgets[i];
        auto& layout = widget.layout_info;
        switch (widget.type)
        {
        case Widget_Type::LABEL: {
            layout.min_width_collapsed = widget.options.label.size * char_size.x;
            layout.min_width_without_collapse = layout.min_width_collapsed;
            layout.min_width_for_line_merge = layout.min_width_collapsed;
            layout.height_can_grow = false;
            layout.can_combine_in_lines = false;
            break;
        }
        case Widget_Type::BUTTON: {
            layout.min_width_collapsed = BUTTON_MIN_CHAR_COUNT * char_size.x + TEXT_BORDER_SPACE;
            layout.min_width_without_collapse = layout.min_width_collapsed;
            layout.min_width_for_line_merge = BUTTON_WANTED_CHAR_COUNT * char_size.x + TEXT_BORDER_SPACE;
            layout.height_can_grow = false;
            layout.can_combine_in_lines = true;
            break;
        }
        case Widget_Type::TEXT_INPUT: {
            layout.min_width_collapsed = TEXT_INPUT_MIN_CHAR_COUNT * char_size.x + TEXT_BORDER_SPACE;
            layout.min_width_without_collapse = TEXT_INPUT_MIN_CHAR_COUNT * char_size.x + TEXT_BORDER_SPACE;
            layout.min_width_for_line_merge = TEXT_INPUT_MAX_CHAR_COUNT * char_size.x + TEXT_BORDER_SPACE;;
            layout.height_can_grow = false;
            layout.can_combine_in_lines = true;
            break;
        }
        case Widget_Type::CONTAINER: {
            widget_container_gather_width_information_recursive(&widget.options.container, &layout, char_size);
            layout.min_width_collapsed = math_maximum(layout.min_width_collapsed, 4 * char_size.x);
            layout.min_width_without_collapse = math_maximum(layout.min_width_without_collapse, LIST_CONTAINER_MIN_CHAR_COUNT * char_size.x);
            layout.min_width_for_line_merge = math_maximum(layout.min_width_for_line_merge, LIST_CONTAINER_MIN_CHAR_COUNT * char_size.x);
            break;
        }
        default: panic("");
        }

        layout.line_index = i;
        c->max_child_min_width_collapsed = math_maximum(c->max_child_min_width_collapsed, layout.min_width_collapsed);
        c->max_child_min_width_without_collapse = math_maximum(c->max_child_min_width_without_collapse, layout.min_width_without_collapse);
        c->max_child_min_width_for_line_merge = math_maximum(c->max_child_min_width_for_line_merge, layout.min_width_for_line_merge);
        c->sum_child_min_width_collapsed += layout.min_width_collapsed;
        c->sum_child_min_width_without_collapse += layout.min_width_without_collapse;
        c->sum_child_min_width_for_line_merge += layout.min_width_for_line_merge;
        c->min_child_size_for_line_merge = math_minimum(c->min_child_size_for_line_merge, layout.min_width_for_line_merge);
        child_height_can_grow = child_height_can_grow | layout.height_can_grow;
        has_child_that_cannot_combine_in_line = has_child_that_cannot_combine_in_line | !layout.can_combine_in_lines;
    }

    // Calculate container-layout from child sizes
    out_layout_info->height_can_grow = child_height_can_grow;
    switch (container->layout)
    {
    case Layout_Type::NORMAL:
    {
        out_layout_info->min_width_collapsed = c->max_child_min_width_collapsed;
        out_layout_info->min_width_without_collapse = c->max_child_min_width_without_collapse;
        out_layout_info->min_width_for_line_merge = c->sum_child_min_width_for_line_merge + (widgets.size - 1) * PAD_WIDGETS_ON_LINE;
        out_layout_info->can_combine_in_lines = !has_child_that_cannot_combine_in_line;
        break;
    }
    case Layout_Type::LABELED_ITEMS:
    {
        int label_length = LABEL_CHAR_COUNT_SIZE * char_size.x;
        out_layout_info->min_width_collapsed = math_maximum(label_length, PAD_ADJACENT_LABLE_LINE_SPLIT + c->max_child_min_width_collapsed);
        // Note: There are multiple different behaviors we could implement here...
        out_layout_info->min_width_without_collapse = label_length + PAD_LABEL_BOX + c->sum_child_min_width_without_collapse + (widgets.size - 1) * PAD_LABEL_BOX;
        out_layout_info->min_width_for_line_merge = label_length + c->sum_child_min_width_for_line_merge + (widgets.size - 1) * PAD_LABEL_BOX;
        out_layout_info->can_combine_in_lines = !has_child_that_cannot_combine_in_line;
        break;
    }
    case Layout_Type::STACK_HORIZONTAL:
    {
        out_layout_info->min_width_collapsed = c->sum_child_min_width_without_collapse;
        out_layout_info->min_width_without_collapse = c->sum_child_min_width_without_collapse;
        out_layout_info->min_width_for_line_merge = c->sum_child_min_width_for_line_merge + (widgets.size - 1) * PAD_LABEL_BOX;
        out_layout_info->can_combine_in_lines = false;
        out_layout_info->can_combine_in_lines = !has_child_that_cannot_combine_in_line;
        break;
    }
    default: panic("");
    }
}

void widget_container_calculate_x_bounds_and_height(Widget_Container* container, Layout_Info* out_layout_info, int x_pos, int available_width, ivec2 char_size)
{
    const int LINE_ITEM_HEIGHT = PAD_TOP + PAD_BOT + BORDER_SPACE + char_size.y;
    const int TEXT_BORDER_SPACE = BORDER_SPACE * 2 + PAD_LEFT_RIGHT * 2;

    // Calculate x-bounds for each widget
    auto& widgets = container->widgets;
    container->line_count = 0;
    switch (container->layout)
    {
    case Layout_Type::NORMAL:
    {
        // Combine lines if enough space is available
        bool lines_were_combined = false;
        if (available_width >= container->min_child_size_for_line_merge * 2 && container->options.normal.allow_line_combination)
        {
            // Combine multiple widgets into lines
            int remaining_width = available_width;
            int max_widgets_per_line = 0;
            int line_widget_count = 0;
            bool last_can_combine = true;
            for (int i = 0; i < widgets.size; i++)
            {
                auto& widget_layout = widgets[i].layout_info;
                if (remaining_width >= widget_layout.min_width_for_line_merge && widget_layout.can_combine_in_lines && last_can_combine) {
                    remaining_width -= widget_layout.min_width_for_line_merge + PAD_WIDGETS_ON_LINE;
                    line_widget_count += 1;
                }
                else
                {
                    // Not enough space in line for widget
                    if (i != 0) {
                        container->line_count += 1;
                        max_widgets_per_line = math_maximum(max_widgets_per_line, line_widget_count);
                        line_widget_count = 1;
                    }
                    else {
                        max_widgets_per_line = math_maximum(max_widgets_per_line, 1);
                    }
                    remaining_width = available_width - (widget_layout.min_width_for_line_merge + PAD_WIDGETS_ON_LINE);
                }
                widget_layout.line_index = container->line_count;
                last_can_combine = widget_layout.can_combine_in_lines;
            }
            max_widgets_per_line = math_maximum(max_widgets_per_line, line_widget_count);
            container->line_count += 1;

            lines_were_combined = max_widgets_per_line > 1;
        }
        else {
            container->line_count = widgets.size;
            lines_were_combined = false;
        }

        // Distribute Width to widgets (based on lines)
        if (lines_were_combined)
        {
            int line_start_index = 0;
            while (line_start_index < widgets.size)
            {
                // Find end on line
                int start_widget_line = widgets[line_start_index].layout_info.line_index;
                int line_end_index = line_start_index;
                int fixed_allocated_size = 0;
                while (line_end_index < widgets.size) {
                    auto& widget_layout = widgets[line_end_index].layout_info;
                    if (widget_layout.line_index != start_widget_line) {
                        break;
                    }
                    fixed_allocated_size += widget_layout.min_width_for_line_merge;
                    line_end_index += 1;
                }

                // Early exit if only single widget on line
                int count = line_end_index - line_start_index;
                if (count == 1) {
                    auto& widget_layout = widgets[line_start_index];
                    widget_layout.layout_info.box.min.x = x_pos;
                    widget_layout.layout_info.box.max.x = x_pos + available_width;
                    line_start_index = line_end_index;
                    continue;
                }

                // Distribute space to all widgets on line
                int padding_space = (count - 1) * PAD_WIDGETS_ON_LINE;
                int overflow_budget = available_width - padding_space - fixed_allocated_size;
                int extra_per_widget = overflow_budget / count;
                int remaining_pixels = overflow_budget % count;

                int cursor_x = x_pos;
                for (int i = line_start_index; i < line_end_index; i++)
                {
                    auto& widget = widgets[i];
                    int width = widget.layout_info.min_width_for_line_merge + extra_per_widget;
                    if (i == 0) { width += remaining_pixels; }

                    widget.layout_info.box.min.x = cursor_x;
                    widget.layout_info.box.max.x = cursor_x + width;
                    cursor_x += width + PAD_WIDGETS_ON_LINE;
                }

                line_start_index = line_end_index;
            }
        }
        else
        {
            // Otherwise it's a simple horizontal stack for each widget
            for (int i = 0; i < widgets.size; i++) {
                auto& widget = widgets[i];
                widget.layout_info.box.min.x = x_pos;
                widget.layout_info.box.max.x = x_pos + available_width;
                widget.layout_info.line_index = i;
            }
        }

        break;
    }
    case Layout_Type::LABELED_ITEMS:
    {
        if (available_width < out_layout_info->min_width_without_collapse)
        {
            for (int i = 0; i < widgets.size; i++) {
                auto& widget = widgets[i];
                widget.layout_info.box.min.x = x_pos + PAD_ADJACENT_LABLE_LINE_SPLIT;
                widget.layout_info.box.max.x = x_pos + available_width;
                widget.layout_info.line_index = i; // Note: Not i + 1, because this is used later for calculating height (Grouping lines)
            }
        }
        else
        {
            // Distribute space to all widgets on line
            int padding_space = widgets.size * PAD_LABEL_BOX; // Note: Not minus one because we have a label
            int overflow_budget = available_width - LABEL_CHAR_COUNT_SIZE * char_size.x - padding_space - container->sum_child_min_width_without_collapse;
            int extra_per_widget = overflow_budget / widgets.size;
            int remaining_pixels = overflow_budget % widgets.size;

            int cursor_x = x_pos;
            cursor_x += LABEL_CHAR_COUNT_SIZE * char_size.x + PAD_LABEL_BOX;
            for (int i = 0; i < widgets.size; i++)
            {
                auto& widget = widgets[i];
                int width = widget.layout_info.min_width_without_collapse + extra_per_widget;
                if (i == 0) { width += remaining_pixels; }

                widget.layout_info.box.min.x = cursor_x;
                widget.layout_info.box.max.x = cursor_x + width;
                widget.layout_info.line_index = 0;
                cursor_x += width + PAD_LABEL_BOX;
            }
        }
        break;
    }
    case Layout_Type::STACK_HORIZONTAL:
    {
        // Distribute space to all widgets on line
        int padding_space = (widgets.size - 1) * PAD_LABEL_BOX;
        int overflow_budget = available_width - padding_space - container->sum_child_min_width_without_collapse;
        int extra_per_widget = overflow_budget / widgets.size;
        int remaining_pixels = overflow_budget % widgets.size;

        int cursor_x = x_pos;
        for (int i = 0; i < widgets.size; i++)
        {
            auto& widget = widgets[i];
            int width = widget.layout_info.min_width_without_collapse + extra_per_widget;
            if (i == 0) { width += remaining_pixels; }

            widget.layout_info.box.min.x = cursor_x;
            widget.layout_info.box.max.x = cursor_x + width;
            widget.layout_info.line_index = 0;
            cursor_x += width + PAD_LABEL_BOX;
        }

        break;
    }
    default: panic("");
    }
    container->line_count = math_maximum(container->line_count, 1);

    // Calculate Height per line
    container->sum_line_min_heights = 0;
    container->sum_line_max_heights = 0;
    container->growable_line_count = 0;

    int max_last_line_min_height = 0;
    int max_last_line_max_height = 0;
    bool last_line_can_grow = false;
    int last_line_index = 0;
    for (int i = 0; i < widgets.size; i++)
    {
        auto& widget = widgets[i];
        switch (widget.type)
        {
        case Widget_Type::LABEL:
        case Widget_Type::BUTTON:
        case Widget_Type::TEXT_INPUT:
        {
            widget.layout_info.min_height = LINE_ITEM_HEIGHT;
            widget.layout_info.max_height = LINE_ITEM_HEIGHT;
            widget.layout_info.height_can_grow = false;
            break;
        }
        case Widget_Type::CONTAINER: 
        {
            auto& box = widget.layout_info.box;
            widget_container_calculate_x_bounds_and_height(&widget.options.container, &widget.layout_info, box.min.x, box.max.x - box.min.x, char_size);
            break;
        }
        default: panic("");
        }

        if (widget.layout_info.line_index != last_line_index)
        {
            container->sum_line_min_heights += max_last_line_min_height;
            container->sum_line_max_heights += max_last_line_max_height;
            container->growable_line_count += last_line_can_grow ? 1 : 0;

            last_line_index = widget.layout_info.line_index;
            max_last_line_min_height = 0;
            max_last_line_max_height = 0;
            last_line_can_grow = false;
        }

        max_last_line_min_height = math_maximum(max_last_line_min_height, widget.layout_info.min_height);
        max_last_line_max_height = math_maximum(max_last_line_max_height, widget.layout_info.max_height);
        last_line_can_grow = last_line_can_grow | widget.layout_info.height_can_grow;
    }
    container->sum_line_min_heights += max_last_line_min_height;
    container->sum_line_max_heights += max_last_line_max_height;
    container->growable_line_count += last_line_can_grow ? 1 : 0;

    // Set Container height infos
    out_layout_info->min_height = container->sum_line_min_heights + (container->line_count - 1) * PAD_WIDGETS_BETWEEN_LINES;
    out_layout_info->max_height = container->sum_line_max_heights + (container->line_count - 1) * PAD_WIDGETS_BETWEEN_LINES;
    out_layout_info->height_can_grow = container->growable_line_count > 0;
    switch (container->layout)
    {
    case Layout_Type::NORMAL: 
    {
        int min_line_count = container->options.normal.min_line_count;
        int max_line_count = container->options.normal.max_line_count;

        int min_height = min_line_count * LINE_ITEM_HEIGHT + PAD_WIDGETS_BETWEEN_LINES * math_maximum(0, (min_line_count - 1));
        out_layout_info->min_height = math_maximum(out_layout_info->min_height, min_height);

        int max_height = max_line_count * LINE_ITEM_HEIGHT + PAD_WIDGETS_BETWEEN_LINES * math_maximum(0, (max_line_count - 1));
        if (max_line_count > 0) {
            out_layout_info->max_height = math_minimum(out_layout_info->max_height, max_height);
            out_layout_info->height_can_grow = false;
        }
        break;
    }
    case Layout_Type::STACK_HORIZONTAL:
        break;
    case Layout_Type::LABELED_ITEMS: {
        if (available_width < out_layout_info->min_width_without_collapse) {
            out_layout_info->min_height += LINE_ITEM_HEIGHT + PAD_WIDGETS_BETWEEN_LINES;
            out_layout_info->max_height += LINE_ITEM_HEIGHT + PAD_WIDGETS_BETWEEN_LINES;
        }
        break;
    }
    default: panic("");
    }
}

void widget_container_calculate_y_bounds(Widget_Container* container, Layout_Info* out_layout_info, int y_pos, int available_height, ivec2 char_size)
{
    auto& widgets = container->widgets;
    const int LINE_ITEM_HEIGHT = PAD_TOP + PAD_BOT + BORDER_SPACE + char_size.y;
    const int TEXT_BORDER_SPACE = BORDER_SPACE * 2 + PAD_LEFT_RIGHT * 2;

    // Check if we want to add scroll-bar
    bool overflow_detected = available_height < out_layout_info->min_height;
    int available_width = out_layout_info->box.max.x - out_layout_info->box.min.x;
    if (overflow_detected && container->layout == Layout_Type::NORMAL && container->options.normal.scroll_bar_enabled)
    {
        auto& box = out_layout_info->box;
        available_width -= SCROLL_BAR_WIDTH;
        // Re-Calculate Child layout
        widget_container_calculate_x_bounds_and_height(container, out_layout_info, out_layout_info->box.min.x, available_width, char_size);
        container->scroll_bar_was_added = true;
    }

    // Offset y_pos if we are collapsed
    if (container->layout == Layout_Type::LABELED_ITEMS && available_width < out_layout_info->min_width_without_collapse) {
        y_pos -= LINE_ITEM_HEIGHT + PAD_WIDGETS_BETWEEN_LINES;
    }

    if (available_width < out_layout_info->min_width_for_line_merge) {
        out_layout_info->min_height += LINE_ITEM_HEIGHT + PAD_WIDGETS_BETWEEN_LINES;
        out_layout_info->max_height += LINE_ITEM_HEIGHT + PAD_WIDGETS_BETWEEN_LINES;
    }

    // Do Y-Layout (Basically all layout do the same thing, using line-index to calculate height)
    available_height = available_height - PAD_WIDGETS_BETWEEN_LINES * (container->line_count - 1);
    int last_line_index = 0;
    int last_line_height = 0;
    int height_buffer = available_height - container->sum_line_min_heights;
    int max_subtracted_from_height_buffer_in_line = 0;
    int first_growing_line_index = -1;
    for (int i = 0; i < widgets.size; i++)
    {
        auto& widget = widgets[i];
        auto& layout = widget.layout_info;

        // Check if we moved to new line
        if (layout.line_index != last_line_index)
        {
            y_pos -= last_line_height + PAD_WIDGETS_BETWEEN_LINES;
            height_buffer = math_maximum(0, height_buffer - max_subtracted_from_height_buffer_in_line);
            max_subtracted_from_height_buffer_in_line = 0;
            last_line_index = layout.line_index;
            last_line_height = 0;
        }

        // Figure out widget height
        int widget_height = 0;
        bool line_grows = false;
        if (available_height <= container->sum_line_min_heights)
        {
            widget_height = layout.min_height;
        }
        else if (available_height <= container->sum_line_max_heights)
        {
            widget_height = layout.min_height;
            int remaining_to_max = layout.max_height - layout.min_height;
            int subtract_count = math_minimum(height_buffer, remaining_to_max);
            widget_height += subtract_count;
            max_subtracted_from_height_buffer_in_line = math_maximum(max_subtracted_from_height_buffer_in_line, subtract_count);
        }
        else
        {
            widget_height = layout.max_height;
            if (layout.height_can_grow)
            {
                int extra_height = (available_height - container->sum_line_max_heights) / container->growable_line_count;
                int pixel_remainder = (available_height - container->sum_line_max_heights) % container->growable_line_count;
                widget_height += extra_height;
                if (first_growing_line_index == layout.line_index || first_growing_line_index == -1) {
                    first_growing_line_index = layout.line_index;
                    widget_height += pixel_remainder;
                }
            }
        }

        // Set widget position
        last_line_height = math_maximum(last_line_height, widget_height);
        layout.box.max.y = y_pos;
        layout.box.min.y = y_pos - widget_height;

        // Recurse to children
        if (widget.type == Widget_Type::CONTAINER) {
            auto& box = widget.layout_info.box;
            widget_container_calculate_y_bounds(&widget.options.container, &widget.layout_info, box.max.y, box.max.y - box.min.y, char_size);
        }
    }
    y_pos -= last_line_height;
}

void widget_container_render_widgets_recursive(
    Widget_Container* container, const Layout_Info* container_layout, BBox clipping_box, Mesh* mesh, Glyph_Atlas_* glyph_atlas
)
{
    ivec2 char_size = glyph_atlas->char_box_size;
    const int LINE_ITEM_HEIGHT = PAD_TOP + PAD_BOT + BORDER_SPACE + char_size.y;
    const int TEXT_BORDER_SPACE = BORDER_SPACE * 2 + PAD_LEFT_RIGHT * 2;

    // Draw scroll bar
    if (container->scroll_bar_was_added)
    {
        // Draw scroll area
        BBox scroll_area = container_layout->box;
        scroll_area.min.x = scroll_area.max.x - SCROLL_BAR_WIDTH;
        mesh_push_box_clipped(mesh, scroll_area, clipping_box, COLOR_SCROLL_BG);

        // Draw bar (Currently on top of everything, because it's easier)
        BBox bar_box = scroll_area;
        bar_box.min.y = bar_box.max.y - MIN_SCROLL_BAR_HEIGHT;
        bar_box.max = bar_box.max - ivec2(SCROLL_BAR_PADDING);
        bar_box.min = bar_box.min + ivec2(SCROLL_BAR_PADDING);
        mesh_push_box_clipped(mesh, bar_box, clipping_box, COLOR_SCROLL_BAR);
    }

    auto box_draw_label = [&](BBox box, String text, BBox clipping_box) {
        mesh_push_text_clipped(mesh, glyph_atlas, text, box.min + ivec2(0, BORDER_SPACE + PAD_BOT), bbox_intersection(box, clipping_box));
    };
    auto box_draw_text_in_box = [&](BBox box, String text, bool center_text, BBox clipping_area, vec4 BG_COLOR, vec4 BORDER_COLOR)
    {
        mesh_push_box_with_border_clipped(mesh, box, clipping_area, BG_COLOR, BORDER_SPACE, BORDER_COLOR);

        BBox text_clip_area = box;
        text_clip_area.min = text_clip_area.min + ivec2(PAD_LEFT_RIGHT + BORDER_SPACE, BORDER_SPACE + PAD_BOT);
        text_clip_area.max = text_clip_area.max - ivec2(PAD_LEFT_RIGHT + BORDER_SPACE, BORDER_SPACE + PAD_TOP);
        ivec2 text_pos = text_clip_area.min;
        int available_text_space = text_clip_area.max.x - text_clip_area.min.x;
        text_clip_area = bbox_intersection(text_clip_area, clipping_area);

        // Center button text if enough space is available
        int required_text_space = char_size.x * text.size;
        int text_offset = 0;
        if (available_text_space > required_text_space && center_text) {
            text_offset = (available_text_space - required_text_space) / 2; // Without division this would be right-align
        }
        mesh_push_text_clipped(mesh, glyph_atlas, text, text_pos + ivec2(text_offset, 0), text_clip_area);
    };

    // Render widgets
    auto& widgets = container->widgets;
    for (int i = 0; i < widgets.size; i++)
    {
        auto& widget = widgets[i];
        auto& box = widget.layout_info.box;
        switch (widget.type)
        {
        case Widget_Type::LABEL: {
            box_draw_label(box, widget.options.label, clipping_box);
            break;
        }
        case Widget_Type::BUTTON: {
            box_draw_text_in_box(box, widget.options.button_text, true, clipping_box, COLOR_BUTTON_BG, COLOR_BUTTON_BORDER);
            break;
        }
        case Widget_Type::TEXT_INPUT: {
            box_draw_text_in_box(box, widget.options.input_text, false, clipping_box, COLOR_INPUT_BG, COLOR_INPUT_BORDER);
            break;
        }
        case Widget_Type::CONTAINER:
        {
            auto& list_container = widget.options.container;
            if (list_container.layout == Layout_Type::LABELED_ITEMS) {
                BBox label_box = box;
                label_box.min.y = label_box.max.y - LINE_ITEM_HEIGHT;
                label_box.max.x = label_box.min.x + LABEL_CHAR_COUNT_SIZE * char_size.x;
                box_draw_label(label_box, widget.options.container.options.label_text, clipping_box);
            }
            widget_container_render_widgets_recursive(&list_container, &widget.layout_info, bbox_intersection(box, clipping_box), mesh, glyph_atlas);
            break;
        }
        default: panic("");
        }
    }
}

void ui_window_new_rendering(UI_Window* window, BBox client_area, Mesh* mesh, Glyph_Atlas_* glyph_atlas)
{
    Layout_Info container_layout_info;
    ivec2 char_size = glyph_atlas->char_box_size;

    // Note: Here we could apply window width to e.g. fit exactly one line
    widget_container_gather_width_information_recursive(&window->container, &container_layout_info, char_size);
    container_layout_info.box.min.x = client_area.min.x;
    container_layout_info.box.max.x = client_area.max.x;

    // Note: Here we could apply window height to e.g. fit the required height perfectly, or add some more space for lists if growable
    widget_container_calculate_x_bounds_and_height(
        &window->container, &container_layout_info, client_area.min.x, client_area.max.x - client_area.min.x, char_size
    );
    container_layout_info.box.min.y = client_area.min.y;
    container_layout_info.box.max.y = client_area.max.y;

    widget_container_calculate_y_bounds(&window->container, &container_layout_info, client_area.max.y, client_area.max.y - client_area.min.y, char_size);

    // Handle inputs after layout calculations (Note: Scroll-Bar input was not calculated yet!)
    // Old scroll bar code
    /*{
        // Re-calculate widget layout, leaving space for scroll-bar
        BBox original_client_area = client_area;
        client_area.max.x -= SCROLL_BAR_WIDTH + 2;
        used_height = ui_layout_widgets_in_area(window, client_area, char_size);

        // Draw scroll-background box
        BBox scroll_box = BBox(ivec2(client_area.max.x + 2, client_area.min.y), ivec2(client_area.max.x + SCROLL_BAR_WIDTH + 2, client_area.max.y));
        mesh_push_box(mesh, scroll_box, COLOR_SCROLL_BG);

        // Figure out bar-height
        int available_bar_space = scroll_box.max.y - scroll_box.min.y - 2 * SCROLL_BAR_PADDING;
        int bar_height = math_maximum(MIN_SCROLL_BAR_HEIGHT, available_bar_space * max_height / math_maximum(1, used_height));

        // Figure out bar-positioning
        static int pixel_scroll_offset = 0;
        const int max_bar_offset = available_bar_space - bar_height;
        const int max_pixel_scroll_offset = used_height - max_height;

        // Handle Input
        {
            ivec2 window_size = ivec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);
            ivec2 mouse = ivec2(input->mouse_x, window_size.y - input->mouse_y);

            // Handle mouse-wheel
            if (bbox_contains_point(original_client_area, mouse)) {
                pixel_scroll_offset -= input->mouse_wheel_delta * MOUSE_WHEEL_SENSITIVITY;
            }

            // Calculate current bar position
            int bar_offset = max_bar_offset * pixel_scroll_offset / math_maximum(max_pixel_scroll_offset, 1);
            BBox bar_box = BBox(
                ivec2(scroll_box.min.x + SCROLL_BAR_PADDING, scroll_box.max.y - SCROLL_BAR_PADDING - bar_height - bar_offset),
                ivec2(scroll_box.max.x - SCROLL_BAR_PADDING, scroll_box.max.y - SCROLL_BAR_PADDING - bar_offset)
            );

            // Drag-and-Drop logic
            static bool drag_start = false;
            static ivec2 drag_start_mouse = ivec2(0);
            static int drag_start_bar_offset = 0;

            if (drag_start)
            {
                if (input->mouse_down[(int)Mouse_Key_Code::LEFT]) {
                    bar_offset = drag_start_bar_offset - (mouse.y - drag_start_mouse.y); // Minus because bar-offset is given in negative y
                    // Set pixel-scroll offset
                    pixel_scroll_offset = bar_offset * max_pixel_scroll_offset / math_maximum(max_bar_offset, 1);
                }
                else {
                    drag_start = false;
                }
            }
            else
            {
                if (bbox_contains_point(bar_box, mouse) && input->mouse_down[(int)Mouse_Key_Code::LEFT]) {
                    drag_start = true;
                    drag_start_mouse = mouse;
                    drag_start_bar_offset = bar_offset;
                }
            }

        }

        // Draw scroll-bar
        pixel_scroll_offset = math_clamp(pixel_scroll_offset, 0, max_pixel_scroll_offset);
        int bar_offset = max_bar_offset * pixel_scroll_offset / math_maximum(max_pixel_scroll_offset, 1);
        BBox bar_box = BBox(
            ivec2(scroll_box.min.x + SCROLL_BAR_PADDING, scroll_box.max.y - SCROLL_BAR_PADDING - bar_height - bar_offset),
            ivec2(scroll_box.max.x - SCROLL_BAR_PADDING, scroll_box.max.y - SCROLL_BAR_PADDING - bar_offset)
        );
        mesh_push_box(mesh, bar_box, COLOR_SCROLL_BAR);

        // Apply offset to all widgets
        for (int i = 0; i < window->widgets.size; i++) {
            auto& widget = window->widgets[i];
            widget.widget_box.min.y += pixel_scroll_offset;
            widget.widget_box.max.y += pixel_scroll_offset;
        }

    }*/

    // Now we can render widgets
    widget_container_render_widgets_recursive(&window->container, &container_layout_info, client_area, mesh, glyph_atlas);
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
    Bitmap bitmap_atlas = bitmap_create(ivec2(256));
    SCOPE_EXIT(bitmap_destroy(bitmap_atlas));
    Bitmap_Atlas_Writer atlas_writer = bitmap_atlas_writer_make(&bitmap_atlas);

    // Initialize atlas data with pattern for error recognition
    {
        for (int i = 0; i < bitmap_atlas.size.x * bitmap_atlas.size.y; i++) {
            bitmap_atlas.data[i] = 255;
        }
        for (int x = 0; x < bitmap_atlas.size.x; x++) {
            for (int y = 0; y < bitmap_atlas.size.y; y++) {
                u8 value = 0;
                if (x / 4 % 2 == 0) {
                    value = 255;
                }
                value = (int)(value * (float)y / bitmap_atlas.size.y);
                bitmap_atlas.data[x + y * bitmap_atlas.pitch] = value;
            }
        }
    }
    {
        // Note: Set pixel at (0, 0) to value 255, because this is used by rectangles with solid colors
        u8 value = 255;
        Bitmap bmp = bitmap_create_static(ivec2(1, 1), &value, 1);
        BBox pixel_box = bitmap_atlas_add_sub_image(&atlas_writer, bmp);
        ivec2 pos = pixel_box.min;
        assert(pos.x == 0 && pos.y == 0, "");
    }

    Glyph_Atlas_ glyph_atlas = glyph_atlas_create();
    SCOPE_EXIT(glyph_atlas_destroy(&glyph_atlas));
    glyph_atlas_rasterize_font(&glyph_atlas, &atlas_writer, "resources/fonts/mona_neon.ttf", 14);

    Glyph_Atlas_ smoll_atlas = glyph_atlas_create();
    SCOPE_EXIT(glyph_atlas_destroy(&smoll_atlas));
    glyph_atlas_rasterize_font(&smoll_atlas, &atlas_writer, "resources/fonts/consola.ttf", 14);

    // Create GPU texture
    Texture* texture = texture_create_from_bytes(
        Texture_Type::RED_U8,
        array_create_static((byte*)bitmap_atlas.data, bitmap_atlas.size.x * bitmap_atlas.size.y),
        bitmap_atlas.size.x, bitmap_atlas.size.y,
        false
    );
    SCOPE_EXIT(texture_destroy(texture));

    auto& predef = rendering_core.predefined;
    Vertex_Description* vertex_desc = vertex_description_create({ predef.position2D, predef.texture_coordinates, predef.color4, predef.index });
    Mesh* mesh = rendering_core_query_mesh("Mono_Render_Mesh", vertex_desc, true);
    Shader* shader = rendering_core_query_shader("mono_texture.glsl");

    ivec2 screen_size = ivec2(window_state->width, window_state->height);
    UI_Window ui_window;
    ui_window.size = ivec2(400, 300);
    ui_window.position = screen_size / 2 - ui_window.size / 2 - BORDER_SPACE;
    ui_window.title = string_create_static("Test-Window!");
    ui_window.container.layout = Layout_Type::NORMAL;
    ui_window.container.options.normal.allow_line_combination = true;
    ui_window.container.options.normal.scroll_bar_enabled = true;
    ui_window.container.options.normal.min_line_count = 0;
    ui_window.container.options.normal.max_line_count = 0;
    ui_window.container.widgets = dynamic_array_create<Widget>();
    SCOPE_EXIT(dynamic_array_destroy(&ui_window.container.widgets));

    auto& widgets = ui_window.container.widgets;
    Widget label;
    label.type = Widget_Type::LABEL;
    label.options.label = string_create_static("Test label YaY");
    dynamic_array_push_back(&widgets, label);

    Widget text_input;
    text_input.type = Widget_Type::TEXT_INPUT;
    text_input.options.input_text = string_create_static("Some text yay nay jay in the ocean?");

    Widget labeled_container;
    labeled_container.type = Widget_Type::CONTAINER;
    labeled_container.options.container.widgets = dynamic_array_create<Widget>();
    labeled_container.options.container.layout = Layout_Type::LABELED_ITEMS;
    labeled_container.options.container.options.label_text = string_create_static("Input:");
    SCOPE_EXIT(dynamic_array_destroy(&labeled_container.options.container.widgets));

    dynamic_array_push_back(&labeled_container.options.container.widgets, text_input);
    dynamic_array_push_back(&widgets, labeled_container);

    for (int i = 0; i < 4; i++)
    {
        const char* names[3] = { "Test 1", "What", "Other" };
        const char* texts[3] = { "Well this is somethign", "Lorem ipsum ", "What did you just say you little..." };

        labeled_container.type = Widget_Type::CONTAINER;
        labeled_container.options.container.widgets = dynamic_array_create<Widget>();
        labeled_container.options.container.options.label_text = string_create_static(names[i % 3]);

        text_input.options.input_text = string_create_static(texts[i % 3]);
        dynamic_array_push_back(&labeled_container.options.container.widgets, text_input);
        dynamic_array_push_back(&widgets, labeled_container);
    }

    // Widget list;
    // list.type = Widget_Type::CONTAINER;
    // list.options.list_container_can_grow = false;
    // list.can_combine_in_lines = false;
    // dynamic_array_push_back(&widgets, list);

    Widget button;
    button.type = Widget_Type::BUTTON;
    button.options.button_text = string_create_static("Click Me!");
    dynamic_array_push_back(&widgets, button);

    // Window resize drag and drop
    bool drag_active = false;
    bool resize_active = false;
    ivec2 drag_start_mouse_pos(0);
    ivec2 drag_start_window_pos(0);
    ivec2 resize_start_size(0);
    bool last_cursor_was_drag = false;
    const int LINE_ITEM_SIZE = glyph_atlas.char_box_size.y + PAD_TOP + PAD_BOT + BORDER_SPACE * 2;

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
        rendering_core_prepare_frame(timer_current_time_in_seconds(), window_state->width, window_state->height);

        screen_size = ivec2(window_state->width, window_state->height);
        ivec2 mouse = ivec2(input->mouse_x, window_state->height - input->mouse_y);

        if (!input->mouse_down[(int)Mouse_Key_Code::LEFT]) {
            drag_active = false;
            resize_active = false;
        }
        if (drag_active && input->mouse_down[(int)Mouse_Key_Code::LEFT]) {
            ui_window.position = drag_start_window_pos + mouse - drag_start_mouse_pos;
        }

        if (drag_active) {
            resize_active = false;
        }
        if (resize_active) {
            ivec2 new_size = resize_start_size + (mouse - drag_start_mouse_pos) * ivec2(1, -1);
            new_size.x = math_maximum(new_size.x, 50);
            new_size.y = math_maximum(new_size.y, 50);
            ivec2 top_left = ui_window.position + ui_window.size * ivec2(0, 1);
            ui_window.size = new_size;
            ui_window.position = top_left - new_size * ivec2(0, 1);
        }

        // Calculate UI-Window sizes
        BBox bbox = BBox(ui_window.position, ui_window.position + ui_window.size);
        BBox header_box = bbox;
        header_box.min.y = bbox.max.y - LINE_ITEM_SIZE;
        BBox client_box = bbox;
        client_box.max.y = header_box.min.y;

        bool header_hover = bbox_sdf_to_point(header_box, mouse) - 4.0f <= 0.0f;
        if (!drag_active && header_hover && input->mouse_pressed[(int)Mouse_Key_Code::LEFT])
        {
            drag_active = true;
            drag_start_mouse_pos = mouse;
            drag_start_window_pos = ui_window.position;
        }

        bool resize_hover = vector_length(vec2((float)mouse.x, (float)mouse.y) - vec2(ui_window.position.x + ui_window.size.x, ui_window.position.y)) <= 8.0f;
        if (!drag_active && !resize_active && resize_hover && input->mouse_pressed[(int)Mouse_Key_Code::LEFT])
        {
            resize_active = true;
            drag_start_mouse_pos = mouse;
            drag_start_window_pos = ui_window.position;
            resize_start_size = ui_window.size;
        }

        if (resize_hover || header_hover || drag_active || resize_active) {
            last_cursor_was_drag = true;
            window_set_cursor_icon(window, Cursor_Icon_Type::HAND);
        }
        else if (last_cursor_was_drag) {
            last_cursor_was_drag = false;
            window_set_cursor_icon(window, Cursor_Icon_Type::ARROW);
        }

        // Render Window
        mesh_push_box(mesh, header_box, COLOR_WINDOW_BG_HEADER);
        mesh_push_box(mesh, client_box, COLOR_WINDOW_BG);
        mesh_push_text_clipped(mesh, &glyph_atlas, ui_window.title, header_box.min + ivec2(BORDER_SPACE) + ivec2(PAD_LEFT_RIGHT, PAD_BOT), header_box);

        // Render Widgets
        BBox widget_box = client_box;
        widget_box.min = widget_box.min + ivec2(2);
        widget_box.max = widget_box.max - ivec2(2);
        ui_window_new_rendering(&ui_window, widget_box, mesh, &glyph_atlas);

        // Tests for Text-Rendering
        if (false)
        {
            // Render mesh with bitmap at the center of the screen
            ivec2 center = ivec2(screen_size.x / 2, screen_size.y / 2);
            if (input->mouse_down[(int)Mouse_Key_Code::LEFT]) {
                center = mouse;
            }

            // mesh_push_text(mesh, &glyph_atlas, &bitmap_atlas, "Hello WORLDyyj!", center, screen_size);
            // center.y -= glyph_atlas.char_box_size.y;
            // mesh_push_text(mesh, &glyph_atlas, &bitmap_atlas, "Hello WORLDyyj!", center, screen_size);

            mesh_push_subimage(mesh, center, BBox(ivec2(0), bitmap_atlas.size), bitmap_atlas.size);

            center.y -= glyph_atlas.char_box_size.y;
            String msg = string_create_static("Hello wjoejyLD!|$()");
            center.y -= 3;
            int border = 1;

            int pad_left_right = 1;
            int pad_top = 1;
            int pad_bot = 1;

            float b_col = 0.4f;
            float col = 0.2f;
            mesh_push_text(mesh, &glyph_atlas, msg, center);

            center.y -= 30;
            mesh_push_text(mesh, &glyph_atlas, msg, center);

            center.y -= smoll_atlas.char_box_size.y;
            center.y -= 3;
            // mesh_push_text(mesh, &smoll_atlas, &bitmap_atlas, "Smoller hello World", center);
        }

        Render_Pass* pass_2d = rendering_core_query_renderpass("2D-Pass", pipeline_state_make_alpha_blending(), nullptr);
        render_pass_draw(pass_2d, shader, mesh, Mesh_Topology::TRIANGLES, { uniform_make("u_sampler", texture, sampling_mode_nearest()) });

        // End of frame handling
        {
            rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
            window_swap_buffers(window);
            input_reset(input); // Clear input for next frame

            // Sleep
            const int TARGET_FPS = 60;
            const double SECONDS_PER_FRAME = 1.0 / TARGET_FPS;
            timer_sleep_until(time_frame_start + SECONDS_PER_FRAME);
        }
    }
}
