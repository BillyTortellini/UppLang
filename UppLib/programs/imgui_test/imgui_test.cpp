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



struct Container_Handle
{
    int container_index;
};

struct Widget_Handle
{
    int widget_index;
    bool created_this_frame;
};

enum class Layout_Type
{
    NORMAL, // Stack-Horizontal with option to combine lines
    STACK_HORIZONTAL, // All widgets are added in a single line
    LABELED_ITEMS // Collapsable label items
};

struct Widget;

struct UI_String
{
    int start_index;
    int length;
};

struct Widget_Container
{
    Layout_Type layout;
    union {
        struct 
        {
            bool allow_line_combination;
            bool scroll_bar_enabled;
            int min_line_count; // 0 for normal behavior
            int max_line_count; // 0 or -1 to disable, otherwise container won't grow
        } normal;
        UI_String label_text;
    } options;
    Dynamic_Array<int> widget_indices;

    int next_matching_index;
    bool visited_this_frame;
    bool matching_failed_this_frame;

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
    bool scroll_bar_drag_active;
    int drag_start_bar_offset;
    int scroll_bar_y_offset;
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
        UI_String label_text; 
        UI_String button_text;
        UI_String input_text;
        Container_Handle container;
    } options;

    Container_Handle parent_container;
    bool visited_this_frame;
    bool created_this_frame;

    Layout_Info layout_info;
};

struct UI_Window
{
    String title;
    ivec2 position;
    ivec2 size;
    Container_Handle container;
    Layout_Info container_layout_info;

    // General drag-and-drops for all widgets
    bool drag_active;
    ivec2 drag_start_mouse_pos;
    bool last_cursor_was_drag;
    int mouse_hover_widget_index; // -1 if no hover...
    bool mouse_hovers_over_clickable;

    // Window drag and drops
    bool window_drag_active;
    ivec2 window_pos_at_drag_start;
    bool window_resize_active;
    ivec2 window_size_at_resize_start;
};

struct UI_System
{
    UI_Window window;
    Dynamic_Array<Widget> widgets;
    Dynamic_Array<Widget_Container> containers;
    String string_buffer;

    int line_item_height;
    ivec2 char_size;
};

UI_System ui_system;

void ui_system_initialize(Glyph_Atlas_* glyph_atlas)
{
    ui_system.containers = dynamic_array_create<Widget_Container>();
    ui_system.widgets = dynamic_array_create<Widget>();
    ui_system.string_buffer = string_create();

    ui_system.char_size = glyph_atlas->char_box_size;
    ui_system.line_item_height = PAD_TOP + PAD_BOT + BORDER_SPACE + ui_system.char_size.y;

    // Initialize window
    ui_system.window.container.container_index = 0;
    ui_system.window.drag_active = false;
    ui_system.window.title = string_create_static("Main window");
    ui_system.window.size = ivec2(400, 300);
    ui_system.window.position = ivec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height) / 2;
    ui_system.window.container.container_index = 0;

    Widget_Container container;
    container.drag_start_bar_offset = 0;
    container.layout = Layout_Type::NORMAL;
    container.scroll_bar_drag_active = false;
    container.scroll_bar_was_added = false;
    container.scroll_bar_y_offset = 0;
    container.widget_indices = dynamic_array_create<int>();
    dynamic_array_push_back(&ui_system.containers, container);
}

void ui_system_shutdown()
{
    for (int i = 0; i < ui_system.containers.size; i++) {
        dynamic_array_destroy(&ui_system.containers[i].widget_indices);
    }
    dynamic_array_destroy(&ui_system.containers);
    dynamic_array_destroy(&ui_system.widgets);
    string_destroy(&ui_system.string_buffer);
}

UI_String ui_system_add_string(String string)
{
    UI_String result;
    result.start_index = ui_system.string_buffer.size;
    result.length = string.size;
    string_append_string(&ui_system.string_buffer, &string);
    return result;
}

Widget_Handle ui_system_push_widget(Container_Handle container_handle, Widget_Type widget_type)
{
    Widget_Container& container = ui_system.containers[container_handle.container_index];

    // Check if we can match widget to previous frame
    if (!container.matching_failed_this_frame && container.next_matching_index < container.widget_indices.size) 
    {
        int widget_index = container.widget_indices[container.next_matching_index];
        Widget& prev_widget = ui_system.widgets[widget_index];
        if (prev_widget.type == widget_type) {
            // Found match
            container.next_matching_index += 1;
            prev_widget.visited_this_frame = true;
            prev_widget.created_this_frame = false;

            Widget_Handle result;
            result.created_this_frame = prev_widget.created_this_frame;
            result.widget_index = widget_index;
            return result;
        }
    }
    container.matching_failed_this_frame = true;

    Widget new_widget;
    new_widget.type = widget_type;
    new_widget.created_this_frame = true;
    new_widget.visited_this_frame = true;
    new_widget.parent_container = container_handle;
    dynamic_array_push_back(&ui_system.widgets, new_widget);
    dynamic_array_push_back(&container.widget_indices, ui_system.widgets.size - 1);

    Widget_Handle result;
    result.created_this_frame = new_widget.created_this_frame;
    result.widget_index = ui_system.widgets.size - 1;
    return result;
}

Container_Handle ui_system_push_container_widget(Container_Handle parent, Layout_Type layout)
{
    Widget_Handle widget_handle = ui_system_push_widget(parent, Widget_Type::CONTAINER);
    Widget& widget = ui_system.widgets[widget_handle.widget_index];

    if (widget_handle.created_this_frame) 
    {
        Widget_Container container;
        container.widget_indices = dynamic_array_create<int>();
        container.scroll_bar_drag_active = false;
        container.scroll_bar_was_added = false;
        container.scroll_bar_y_offset = 0;
        dynamic_array_push_back(&ui_system.containers, container);
        widget.options.container.container_index = ui_system.containers.size - 1;
    }

    Widget_Container& container = ui_system.containers[widget.options.container.container_index];
    container.visited_this_frame = true;
    container.layout = layout;

    return widget.options.container;
}

bool ui_system_push_button(Container_Handle container, const char* label_text) 
{
    Widget_Handle handle = ui_system_push_widget(container, Widget_Type::BUTTON);
    Widget& widget = ui_system.widgets[handle.widget_index];
    widget.options.button_text = ui_system_add_string(string_create_static(label_text));
    return false;
}

void ui_system_push_label(Container_Handle container, const char* text)
{
    Widget_Handle handle = ui_system_push_widget(container, Widget_Type::LABEL);
    Widget& widget = ui_system.widgets[handle.widget_index];
    widget.options.label_text = ui_system_add_string(string_create_static(text));
}

void ui_system_push_text_input(Container_Handle container, const char* label_text, String text)
{
    Container_Handle label_container = ui_system_push_container_widget(container, Layout_Type::LABELED_ITEMS);
    ui_system.containers[label_container.container_index].options.label_text = ui_system_add_string(string_create_static(label_text));

    Widget_Handle handle = ui_system_push_widget(label_container, Widget_Type::TEXT_INPUT);
    Widget& widget = ui_system.widgets[handle.widget_index];
    widget.options.input_text = ui_system_add_string(text);
}

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

void widget_container_gather_width_information_recursive(Container_Handle container_handle, Layout_Info* out_layout_info)
{
    const int TEXT_BORDER_SPACE = BORDER_SPACE * 2 + PAD_LEFT_RIGHT * 2;
    Widget_Container& container = ui_system.containers[container_handle.container_index];

    // Calculate child width infos
    container.max_child_min_width_collapsed = 0;
    container.max_child_min_width_without_collapse = 0;
    container.max_child_min_width_for_line_merge = 0;
    container.sum_child_min_width_collapsed = 0;
    container.sum_child_min_width_without_collapse = 0;
    container.sum_child_min_width_for_line_merge = 0;
    container.min_child_size_for_line_merge = container.widget_indices.size == 0 ? 0 : 1000000;
    container.scroll_bar_was_added = false;
    bool child_height_can_grow = false;
    bool has_child_that_cannot_combine_in_line = false;
    for (int i = 0; i < container.widget_indices.size; i++)
    {
        auto& widget = ui_system.widgets[container.widget_indices[i]];
        auto& layout = widget.layout_info;

        // Default height info for all widgets
        layout.height_can_grow      = false;
        widget.layout_info.min_height = ui_system.line_item_height;
        widget.layout_info.max_height = ui_system.line_item_height;

        switch (widget.type)
        {
        case Widget_Type::LABEL: {
            layout.min_width_collapsed        = widget.options.label_text.length * ui_system.char_size.x;
            layout.min_width_without_collapse = layout.min_width_collapsed;
            layout.min_width_for_line_merge   = layout.min_width_collapsed;
            layout.can_combine_in_lines = false;
            break;
        }
        case Widget_Type::BUTTON: {
            layout.min_width_collapsed        = BUTTON_MIN_CHAR_COUNT * ui_system.char_size.x + TEXT_BORDER_SPACE;
            layout.min_width_without_collapse = layout.min_width_collapsed;
            layout.min_width_for_line_merge   = BUTTON_WANTED_CHAR_COUNT * ui_system.char_size.x + TEXT_BORDER_SPACE;
            layout.can_combine_in_lines = true;
            break;
        }
        case Widget_Type::TEXT_INPUT: {
            layout.min_width_collapsed        = TEXT_INPUT_MIN_CHAR_COUNT * ui_system.char_size.x + TEXT_BORDER_SPACE;
            layout.min_width_without_collapse = layout.min_width_collapsed; 
            layout.min_width_for_line_merge   = TEXT_INPUT_MAX_CHAR_COUNT * ui_system.char_size.x + TEXT_BORDER_SPACE;;
            layout.can_combine_in_lines = true;
            break;
        }
        case Widget_Type::CONTAINER: {
            widget_container_gather_width_information_recursive(widget.options.container, &layout);
            break;
        }
        default: panic("");
        }

        layout.line_index = i;
        container.max_child_min_width_collapsed        = math_maximum(container.max_child_min_width_collapsed, layout.min_width_collapsed);
        container.max_child_min_width_without_collapse = math_maximum(container.max_child_min_width_without_collapse, layout.min_width_without_collapse);
        container.max_child_min_width_for_line_merge   = math_maximum(container.max_child_min_width_for_line_merge, layout.min_width_for_line_merge);
        container.sum_child_min_width_collapsed += layout.min_width_collapsed;
        container.sum_child_min_width_without_collapse += layout.min_width_without_collapse;
        container.sum_child_min_width_for_line_merge += layout.min_width_for_line_merge;
        container.min_child_size_for_line_merge = math_minimum(container.min_child_size_for_line_merge, layout.min_width_for_line_merge);
        child_height_can_grow = child_height_can_grow | layout.height_can_grow;
        has_child_that_cannot_combine_in_line = has_child_that_cannot_combine_in_line | !layout.can_combine_in_lines;
    }

    // Calculate container size-info from child sizes
    out_layout_info->height_can_grow = child_height_can_grow;
    switch (container.layout)
    {
    case Layout_Type::NORMAL:
    {
        out_layout_info->min_width_collapsed = container.max_child_min_width_collapsed;
        out_layout_info->min_width_without_collapse = container.max_child_min_width_without_collapse;
        out_layout_info->min_width_for_line_merge = container.max_child_min_width_for_line_merge;
        out_layout_info->can_combine_in_lines = false;
        break;
    }
    case Layout_Type::LABELED_ITEMS:
    {
        int label_length = LABEL_CHAR_COUNT_SIZE * ui_system.char_size.x;
        int widget_count = container.widget_indices.size;
        out_layout_info->min_width_collapsed = math_maximum(label_length, PAD_ADJACENT_LABLE_LINE_SPLIT + container.max_child_min_width_collapsed);
        // Note: There are multiple different behaviors we could implement here...
        out_layout_info->min_width_without_collapse = 
            label_length + PAD_LABEL_BOX + container.sum_child_min_width_without_collapse + (widget_count - 1) * PAD_LABEL_BOX;
        out_layout_info->min_width_for_line_merge = label_length + container.sum_child_min_width_for_line_merge + (widget_count - 1) * PAD_LABEL_BOX;
        out_layout_info->can_combine_in_lines = !has_child_that_cannot_combine_in_line;
        break;
    }
    case Layout_Type::STACK_HORIZONTAL:
    {
        int widget_count = container.widget_indices.size;
        out_layout_info->min_width_collapsed = container.sum_child_min_width_without_collapse;
        out_layout_info->min_width_without_collapse = container.sum_child_min_width_without_collapse;
        out_layout_info->min_width_for_line_merge = container.sum_child_min_width_for_line_merge + (widget_count - 1) * PAD_LABEL_BOX;
        out_layout_info->can_combine_in_lines = false;
        out_layout_info->can_combine_in_lines = !has_child_that_cannot_combine_in_line;
        break;
    }
    default: panic("");
    }
}

void widget_container_calculate_x_bounds_and_height(Container_Handle container_handle, Layout_Info* out_layout_info, int x_pos, int available_width)
{
    const int TEXT_BORDER_SPACE = BORDER_SPACE * 2 + PAD_LEFT_RIGHT * 2;

    // Calculate x-bounds for each widget
    Widget_Container& container = ui_system.containers[container_handle.container_index];
    container.line_count = 0;

    auto distribute_width_to_widgets_in_line = [&](
        int line_start_index, int line_end_index, int sum_min_width_per_widget, int start_x_offset, bool use_min_width_without_collapse
    )
    {
        int count = line_end_index - line_start_index;
        if (count == 0) return;

        // Handle single widget line (Common case) first
        if (count == 1) {
            auto& widget_layout = ui_system.widgets[container.widget_indices[line_start_index]].layout_info;
            widget_layout.box.min.x = x_pos + start_x_offset;
            widget_layout.box.max.x = x_pos + available_width;
            widget_layout.line_index = container.line_count;
            return;
        }

        int padding_space = (count - 1) * PAD_WIDGETS_ON_LINE;
        int overflow_budget = available_width - padding_space - sum_min_width_per_widget - start_x_offset;
        int extra_per_widget = math_maximum(0, overflow_budget / count);
        int remaining_pixels = math_maximum(0, overflow_budget % count);

        int cursor_x = x_pos + start_x_offset;
        for (int i = line_start_index; i < line_end_index; i++)
        {
            auto& widget_layout = ui_system.widgets[container.widget_indices[i]].layout_info;
            int width = use_min_width_without_collapse ? widget_layout.min_width_without_collapse : widget_layout.min_width_for_line_merge;
            width += extra_per_widget;
            if (i == 0) { width += remaining_pixels; }

            widget_layout.box.min.x = cursor_x;
            widget_layout.box.max.x = cursor_x + width;
            widget_layout.line_index = container.line_count;
            cursor_x += width + PAD_WIDGETS_ON_LINE;
        }
    };

    switch (container.layout)
    {
    case Layout_Type::NORMAL:
    {
        // int child_index = 0;
        // while (child_index < container.widget_indices.size)
        // {
        //     // Find end of line
        //     int line_start_index = child_index;
        //     bool last_can_combine = true;
        //     int remaining_width = available_width;
        //     int line_sum_min_width_merge = 0;
        //     while (child_index < container.widget_indices.size)
        //     {
        //         auto& widget_layout = ui_system.widgets[container.widget_indices[child_index]].layout_info;
        //         bool add_widget_to_line = remaining_width >= widget_layout.min_width_for_line_merge && widget_layout.can_combine_in_lines && last_can_combine;
        //         if (!add_widget_to_line) {
        //             break;
        //         }
        //         line_sum_min_width_merge += widget_layout.min_width_for_line_merge;
        //         remaining_width -= widget_layout.min_width_for_line_merge + PAD_WIDGETS_ON_LINE;
        //         child_index += 1;
        //     }

        //     // Special case if no widget has enough space in line
        //     if (line_start_index == child_index) {
        //         auto& widget = ui_system.widgets[container.widget_indices[child_index]];
        //         auto& widget_layout = widget.layout_info;
        //         line_sum_min_width_merge += widget.layout_info.min_width_for_line_merge;
        //         child_index += 1;
        //     }

        //     distribute_width_to_widgets_in_line(line_start_index, child_index, line_sum_min_width_merge, 0, false);
        //     container.line_count += 1;
        // }

        int child_index = 0;
        const int BOX_WIDTH = ui_system.char_size.x * 12;
        const int box_count = math_maximum(1, available_width / BOX_WIDTH);
        while (child_index < container.widget_indices.size)
        {
            // Find end of line
            int line_start_index = child_index;
            bool last_can_combine = true;
            int remaining_boxes = box_count;
            while (child_index < container.widget_indices.size)
            {
                auto& widget_layout = ui_system.widgets[container.widget_indices[child_index]].layout_info;
                int required_boxes = (widget_layout.min_width_for_line_merge + PAD_WIDGETS_ON_LINE) / BOX_WIDTH;
                if (required_boxes * BOX_WIDTH < widget_layout.min_width_for_line_merge + PAD_WIDGETS_ON_LINE) {
                    required_boxes += 1;
                }
                bool add_widget_to_line = required_boxes <= remaining_boxes && widget_layout.can_combine_in_lines && last_can_combine;
                if (!add_widget_to_line) {
                    break;
                }
                remaining_boxes -= required_boxes;
                child_index += 1;
            }

            // Special case if no widget has enough space in line
            if (line_start_index == child_index || line_start_index +1 == child_index) {
                auto& widget = ui_system.widgets[container.widget_indices[line_start_index]];
                auto& widget_layout = widget.layout_info;

                widget_layout.box.min.x = x_pos;
                widget_layout.box.max.x = x_pos + available_width;
                widget_layout.line_index = container.line_count;
                container.line_count += 1;
                if (line_start_index == child_index) {
                    child_index += 1;
                }
                continue;
            }

            // Otherwise distribute boxes onto widgets
            int count = child_index - line_start_index;
            int extra_boxes_per_widget = remaining_boxes / count;
            int box_remainder          = remaining_boxes % count;
            const int last_box_extra = available_width - box_count * BOX_WIDTH;

            int cursor_x = x_pos;
            for (int i = line_start_index; i < child_index; i++)
            {
                auto& widget_layout = ui_system.widgets[container.widget_indices[i]].layout_info;

                int widget_boxes = (widget_layout.min_width_for_line_merge + PAD_WIDGETS_ON_LINE) / BOX_WIDTH;
                if (widget_boxes * BOX_WIDTH < widget_layout.min_width_for_line_merge + PAD_WIDGETS_ON_LINE) {
                    widget_boxes += 1;
                }
                widget_boxes += extra_boxes_per_widget;
                if (i - line_start_index < box_remainder) {
                    widget_boxes += 1;
                }

                int width = BOX_WIDTH * widget_boxes;
                if (i == child_index - 1) {
                    width += last_box_extra;
                }
                else {
                    width -= PAD_WIDGETS_ON_LINE;
                }

                widget_layout.box.min.x = cursor_x;
                widget_layout.box.max.x = cursor_x + width;
                widget_layout.line_index = container.line_count;
                cursor_x += width + PAD_WIDGETS_ON_LINE;
            }

            container.line_count += 1;
        }
        break;
    }
    case Layout_Type::LABELED_ITEMS:
    {
        // Distribute x_pos if the label is collapsed or not
        if (available_width < out_layout_info->min_width_without_collapse)
        {
            for (int i = 0; i < container.widget_indices.size; i++) {
                auto& layout = ui_system.widgets[container.widget_indices[i]].layout_info;
                layout.box.min.x = x_pos + PAD_ADJACENT_LABLE_LINE_SPLIT;
                layout.box.max.x = x_pos + available_width;
                layout.line_index = i; // Note: Not i + 1 (If we count label), because this is used later for calculating height (Grouping lines)
            }
            container.line_count = container.widget_indices.size;
        }
        else {
            // Distribute space to all widgets on line
            int label_width = LABEL_CHAR_COUNT_SIZE * ui_system.char_size.x + PAD_LABEL_BOX;
            distribute_width_to_widgets_in_line(0, container.widget_indices.size, container.sum_child_min_width_without_collapse, label_width, true);
        }
        break;
    }
    case Layout_Type::STACK_HORIZONTAL:
    {
        // Distribute space to all widgets on line
        distribute_width_to_widgets_in_line(0, container.widget_indices.size, container.sum_child_min_width_without_collapse, 0, true);
        break;
    }
    default: panic("");
    }
    container.line_count = math_maximum(container.line_count, 1);

    // Calculate Height per line
    container.sum_line_min_heights = 0;
    container.sum_line_max_heights = 0;
    container.growable_line_count = 0;

    int max_last_line_min_height = 0;
    int max_last_line_max_height = 0;
    bool last_line_can_grow = false;
    int last_line_index = 0;
    for (int i = 0; i < container.widget_indices.size; i++)
    {
        auto& widget = ui_system.widgets[container.widget_indices[i]];
        if (widget.type == Widget_Type::CONTAINER) {
            auto& box = widget.layout_info.box;
            widget_container_calculate_x_bounds_and_height(widget.options.container, &widget.layout_info, box.min.x, box.max.x - box.min.x);
        }

        if (widget.layout_info.line_index != last_line_index)
        {
            container.sum_line_min_heights += max_last_line_min_height;
            container.sum_line_max_heights += max_last_line_max_height;
            container.growable_line_count += last_line_can_grow ? 1 : 0;

            last_line_index = widget.layout_info.line_index;
            max_last_line_min_height = 0;
            max_last_line_max_height = 0;
            last_line_can_grow = false;
        }

        max_last_line_min_height = math_maximum(max_last_line_min_height, widget.layout_info.min_height);
        max_last_line_max_height = math_maximum(max_last_line_max_height, widget.layout_info.max_height);
        last_line_can_grow = last_line_can_grow | widget.layout_info.height_can_grow;
    }
    container.sum_line_min_heights += max_last_line_min_height;
    container.sum_line_max_heights += max_last_line_max_height;
    container.growable_line_count += last_line_can_grow ? 1 : 0;

    // Set Container height infos
    out_layout_info->min_height = container.sum_line_min_heights + (container.line_count - 1) * PAD_WIDGETS_BETWEEN_LINES;
    out_layout_info->max_height = container.sum_line_max_heights + (container.line_count - 1) * PAD_WIDGETS_BETWEEN_LINES;
    out_layout_info->height_can_grow = container.growable_line_count > 0;
    switch (container.layout)
    {
    case Layout_Type::NORMAL:
    {
        int min_line_count = container.options.normal.min_line_count;
        int max_line_count = container.options.normal.max_line_count;

        int min_height = min_line_count * ui_system.line_item_height + PAD_WIDGETS_BETWEEN_LINES * math_maximum(0, (min_line_count - 1));
        out_layout_info->min_height = math_maximum(out_layout_info->min_height, min_height);

        int max_height = max_line_count * ui_system.line_item_height + PAD_WIDGETS_BETWEEN_LINES * math_maximum(0, (max_line_count - 1));
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
            out_layout_info->min_height += ui_system.line_item_height + PAD_WIDGETS_BETWEEN_LINES;
            out_layout_info->max_height += ui_system.line_item_height + PAD_WIDGETS_BETWEEN_LINES;
        }
        break;
    }
    default: panic("");
    }
}

void widget_container_calculate_y_bounds(Container_Handle container_handle, Layout_Info* out_layout_info, int y_pos, int available_height)
{
    Widget_Container& container = ui_system.containers[container_handle.container_index];
    const int TEXT_BORDER_SPACE = BORDER_SPACE * 2 + PAD_LEFT_RIGHT * 2;

    // Check if we want to add scroll-bar
    bool overflow_detected = available_height < out_layout_info->min_height;
    int available_width = out_layout_info->box.max.x - out_layout_info->box.min.x;
    container.scroll_bar_was_added = false;
    if (overflow_detected && container.layout == Layout_Type::NORMAL && container.options.normal.scroll_bar_enabled)
    {
        auto& box = out_layout_info->box;
        available_width -= SCROLL_BAR_WIDTH;
        // Re-Calculate Child layout
        widget_container_calculate_x_bounds_and_height(container_handle, out_layout_info, out_layout_info->box.min.x, available_width);
        container.scroll_bar_was_added = true;
    }

    // Offset y_pos if we are collapsed
    if (container.layout == Layout_Type::LABELED_ITEMS && available_width < out_layout_info->min_width_without_collapse) {
        y_pos -= ui_system.line_item_height + PAD_WIDGETS_BETWEEN_LINES;
    }

    if (available_width < out_layout_info->min_width_for_line_merge) {
        out_layout_info->min_height += ui_system.line_item_height + PAD_WIDGETS_BETWEEN_LINES;
        out_layout_info->max_height += ui_system.line_item_height + PAD_WIDGETS_BETWEEN_LINES;
    }

    // Do Y-Layout (Basically all layouts do the same thing, using line-index to calculate height)
    available_height = available_height - PAD_WIDGETS_BETWEEN_LINES * (container.line_count - 1);
    int last_line_index = 0;
    int last_line_height = 0;
    int height_buffer = available_height - container.sum_line_min_heights;
    int max_subtracted_from_height_buffer_in_line = 0;
    int first_growing_line_index = -1;
    for (int i = 0; i < container.widget_indices.size; i++)
    {
        auto& widget = ui_system.widgets[container.widget_indices[i]];
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
        if (available_height <= container.sum_line_min_heights)
        {
            widget_height = layout.min_height;
        }
        else if (available_height <= container.sum_line_max_heights)
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
                int extra_height = (available_height - container.sum_line_max_heights) / container.growable_line_count;
                int pixel_remainder = (available_height - container.sum_line_max_heights) % container.growable_line_count;
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
            widget_container_calculate_y_bounds(widget.options.container, &widget.layout_info, box.max.y, box.max.y - box.min.y);
        }
    }
    y_pos -= last_line_height;
}

void widget_container_render_widgets_recursive(
    Container_Handle container_handle, const Layout_Info* container_layout, BBox clipping_box, int y_offset, Mesh* mesh, Glyph_Atlas_* glyph_atlas
)
{
    ivec2 char_size = glyph_atlas->char_box_size;
    const int LINE_ITEM_HEIGHT = PAD_TOP + PAD_BOT + 2 * BORDER_SPACE + char_size.y;
    const int TEXT_BORDER_SPACE = BORDER_SPACE * 2 + PAD_LEFT_RIGHT * 2;

    auto& container = ui_system.containers[container_handle.container_index];

    // Draw scroll bar
    if (container.scroll_bar_was_added)
    {
        BBox box = container_layout->box;
        box.min.y += y_offset;
        box.max.y += y_offset;

        // Calculate scroll-bar metrics
        const int max_height = box.max.y - box.min.y;
        const int used_height = container.sum_line_min_heights + math_maximum(0, container.line_count - 1) * PAD_WIDGETS_BETWEEN_LINES;
        const int available_bar_space = max_height - 2 * SCROLL_BAR_PADDING;
        const int bar_height = math_maximum(MIN_SCROLL_BAR_HEIGHT, available_bar_space * max_height / math_maximum(1, used_height));
        const int max_bar_offset = available_bar_space - bar_height;
        const int max_pixel_scroll_offset = used_height - max_height;

        // Calculate current bar position
        BBox scroll_box = BBox(ivec2(box.max.x - SCROLL_BAR_WIDTH, box.min.y), box.max);
        int bar_offset = max_bar_offset * container.scroll_bar_y_offset / math_maximum(max_pixel_scroll_offset, 1);
        BBox bar_box = BBox(
            ivec2(scroll_box.min.x + SCROLL_BAR_PADDING, scroll_box.max.y - SCROLL_BAR_PADDING - bar_height - bar_offset),
            ivec2(scroll_box.max.x - SCROLL_BAR_PADDING, scroll_box.max.y - SCROLL_BAR_PADDING - bar_offset)
        );

        // Draw
        mesh_push_box_clipped(mesh, scroll_box, clipping_box, COLOR_SCROLL_BG);
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
    auto ui_string_to_string = [](UI_String string) -> String {
        String result;
        result.capacity = 0;
        result.characters = &ui_system.string_buffer[string.start_index];
        result.size = string.length;
        return result;
    };

    // Render widgets
    y_offset += container.scroll_bar_y_offset;
    for (int i = 0; i < container.widget_indices.size; i++)
    {
        int widget_index = container.widget_indices[i];
        bool has_hover = widget_index == ui_system.window.mouse_hover_widget_index;
        auto& widget = ui_system.widgets[widget_index];
        auto& box = widget.layout_info.box;
        box.max.y += y_offset;
        box.min.y += y_offset;
        switch (widget.type)
        {
        case Widget_Type::LABEL: {
            box_draw_label(box, ui_string_to_string(widget.options.label_text), clipping_box);
            break;
        }
        case Widget_Type::BUTTON: {
            if (has_hover) {
                box_draw_text_in_box(box, ui_string_to_string(widget.options.button_text), true, clipping_box, COLOR_BUTTON_BG_HOVER, COLOR_BUTTON_BORDER);
            }
            else {
                box_draw_text_in_box(box, ui_string_to_string(widget.options.button_text), true, clipping_box, COLOR_BUTTON_BG, COLOR_BUTTON_BORDER);
            }
            break;
        }
        case Widget_Type::TEXT_INPUT: {
            box_draw_text_in_box(box, ui_string_to_string(widget.options.input_text), false, clipping_box, COLOR_INPUT_BG, COLOR_INPUT_BORDER);
            break;
        }
        case Widget_Type::CONTAINER:
        {
            auto& container = ui_system.containers[widget.options.container.container_index];
            if (container.layout == Layout_Type::LABELED_ITEMS) {
                BBox label_box = box;
                label_box.min.y = label_box.max.y - LINE_ITEM_HEIGHT;
                label_box.max.x = label_box.min.x + LABEL_CHAR_COUNT_SIZE * char_size.x;
                box_draw_label(label_box, ui_string_to_string(container.options.label_text), clipping_box);
            }
            widget_container_render_widgets_recursive(
                widget.options.container, &widget.layout_info, bbox_intersection(box, clipping_box), y_offset, mesh, glyph_atlas
            );
            break;
        }
        default: panic("");
        }
    }
}

void widget_container_handle_scroll_bar_input(
    UI_Window* window, Widget_Container* container, Layout_Info* layout_info, int y_offset, BBox clipping_box,
    ivec2 mouse_pos, bool mouse_down, bool mouse_clicked, int mouse_wheel_delta
)
{
    BBox box = layout_info->box;
    box.max.y += y_offset;
    box.min.y += y_offset;
    bool mouse_inside_container = bbox_contains_point(bbox_intersection(clipping_box, box), mouse_pos);
    if (!mouse_inside_container) { return; }

    if (!container->scroll_bar_was_added) {
        container->scroll_bar_y_offset = 0;
        container->scroll_bar_drag_active = false;
    }
    if (!mouse_down) {
        window->drag_active = false;
        container->scroll_bar_drag_active = false;
    }

    // Handle scroll-bar
    if (container->scroll_bar_was_added)
    {
        // Calculate scroll-bar metrics
        const int max_height = box.max.y - box.min.y;
        const int used_height = container->sum_line_min_heights + math_maximum(0, container->line_count - 1) * PAD_WIDGETS_BETWEEN_LINES;
        const int available_bar_space = max_height - 2 * SCROLL_BAR_PADDING;
        const int bar_height = math_maximum(MIN_SCROLL_BAR_HEIGHT, available_bar_space * max_height / math_maximum(1, used_height));
        const int max_bar_offset = available_bar_space - bar_height;
        const int max_pixel_scroll_offset = used_height - max_height;

        // Handle mouse-wheel input
        if (mouse_inside_container) {
            container->scroll_bar_y_offset -= mouse_wheel_delta * MOUSE_WHEEL_SENSITIVITY;
        }

        // Calculate current bar position
        BBox scroll_box = BBox(ivec2(box.max.x - SCROLL_BAR_WIDTH, box.min.y), box.max);
        int bar_offset = max_bar_offset * container->scroll_bar_y_offset / math_maximum(max_pixel_scroll_offset, 1);
        BBox bar_box = BBox(
            ivec2(scroll_box.min.x + SCROLL_BAR_PADDING, scroll_box.max.y - SCROLL_BAR_PADDING - bar_height - bar_offset),
            ivec2(scroll_box.max.x - SCROLL_BAR_PADDING, scroll_box.max.y - SCROLL_BAR_PADDING - bar_offset)
        );

        // Handle scroll-bar drag and drop
        if (container->scroll_bar_drag_active)
        {
            if (mouse_down) {
                bar_offset = container->drag_start_bar_offset - (mouse_pos.y - window->drag_start_mouse_pos.y); // Minus because bar-offset is given in negative y
                // Set pixel-scroll offset
                container->scroll_bar_y_offset = bar_offset * max_pixel_scroll_offset / math_maximum(max_bar_offset, 1);
            }
            else {
                container->scroll_bar_drag_active = false;
                window->drag_active = false;
            }
        }
        else if (bbox_contains_point(bar_box, mouse_pos))
        {
            window->mouse_hovers_over_clickable = true;
            if (!window->drag_active && mouse_clicked) {
                container->scroll_bar_drag_active = true;
                window->drag_active = true;
                window->drag_start_mouse_pos = mouse_pos;
                container->drag_start_bar_offset = bar_offset;
            }
        }
        container->scroll_bar_y_offset = math_clamp(container->scroll_bar_y_offset, 0, max_pixel_scroll_offset);
    }

    // Recurse to children
    for (int i = 0; i < container->widget_indices.size; i++)
    {
        auto& widget = ui_system.widgets[container->widget_indices[i]];
        BBox widget_box = widget.layout_info.box;
        widget_box.min.y += y_offset;
        widget_box.max.y += y_offset;
        if (!bbox_contains_point(bbox_intersection(widget_box, bbox_intersection(box, clipping_box)), mouse_pos)) continue;
        window->mouse_hover_widget_index = container->widget_indices[i];
        if (widget.type == Widget_Type::CONTAINER) {
            widget_container_handle_scroll_bar_input(
                window, &ui_system.containers[widget.options.container.container_index],
                &widget.layout_info, y_offset + container->scroll_bar_y_offset, bbox_intersection(clipping_box, box),
                mouse_pos, mouse_down, mouse_clicked, mouse_wheel_delta
            );
        }
    }
}

void widget_container_find_mouse_hover_widget(
    UI_Window* window, Widget_Container* container, Layout_Info* layout_info, int y_offset, BBox clipping_box, ivec2 mouse_pos
)
{
    BBox box = layout_info->box;
    box.max.y += y_offset;
    box.min.y += y_offset;
    bool mouse_inside_container = bbox_contains_point(bbox_intersection(clipping_box, box), mouse_pos);
    if (!mouse_inside_container) { return; }

    // Recurse to children
    for (int i = 0; i < container->widget_indices.size; i++)
    {
        auto& widget = ui_system.widgets[container->widget_indices[i]];
        BBox widget_box = widget.layout_info.box;
        widget_box.min.y += y_offset;
        widget_box.max.y += y_offset;
        if (!bbox_contains_point(bbox_intersection(widget_box, bbox_intersection(box, clipping_box)), mouse_pos)) continue;
        window->mouse_hover_widget_index = container->widget_indices[i];
        if (widget.type == Widget_Type::CONTAINER) {
            widget_container_find_mouse_hover_widget(
                window, &ui_system.containers[widget.options.container.container_index],
                &widget.layout_info, y_offset + container->scroll_bar_y_offset, bbox_intersection(clipping_box, box),
                mouse_pos
            );
        }
    }
}

void ui_system_start_frame(Input* input)
{
    auto& info = rendering_core.render_information;
    ivec2 screen_size = ivec2(info.backbuffer_width, info.backbuffer_height);
    ivec2 mouse = ivec2(input->mouse_x, screen_size.y - input->mouse_y);
    bool mouse_down = input->mouse_down[(int)Mouse_Key_Code::LEFT];
    bool mouse_pressed = input->mouse_pressed[(int)Mouse_Key_Code::LEFT];

    UI_Window* window = &ui_system.window;
    window->mouse_hovers_over_clickable = false;

    // Handle window resize/move
    {
        if (!mouse_down) {
            window->drag_active = false;
            window->window_drag_active = false;
            window->window_resize_active = false;
        }
        if (window->window_drag_active) {
            window->position = window->window_pos_at_drag_start + mouse - window->drag_start_mouse_pos;
            window->window_resize_active = false;
        }
        if (window->window_resize_active)
        {
            ivec2 new_size = window->window_size_at_resize_start + (mouse - window->drag_start_mouse_pos) * ivec2(1, -1);
            new_size.x = math_maximum(new_size.x, 50);
            new_size.y = math_maximum(new_size.y, 50);
            ivec2 top_left = window->position + window->size * ivec2(0, 1);
            window->size = new_size;
            window->position = top_left - new_size * ivec2(0, 1);
        }
    }

    BBox window_box = BBox(window->position, window->position + window->size);
    BBox header_box = window_box;
    header_box.min.y = window_box.max.y - ui_system.line_item_height;
    BBox widget_box = BBox(window_box.min + 2, window_box.max - ivec2(2, 2 + ui_system.line_item_height));

    // Check window Resize + Drag and Drop
    {
        bool header_hover = bbox_sdf_to_point(header_box, mouse) - 4.0f <= 0.0f;
        if (!window->drag_active && header_hover && mouse_pressed)
        {
            window->drag_active = true;
            window->drag_start_mouse_pos = mouse;
            window->window_drag_active = true;
            window->window_pos_at_drag_start = window->position;
            window->window_size_at_resize_start = window->size;
        }

        bool resize_hover = vector_length(vec2((float)mouse.x, (float)mouse.y) - vec2(window->position.x + window->size.x, window->position.y)) <= 8.0f;
        if (!window->drag_active && resize_hover && mouse_pressed)
        {
            window->drag_active = true;
            window->drag_start_mouse_pos = mouse;
            window->window_resize_active = true;
            window->window_pos_at_drag_start = window->position;
            window->window_size_at_resize_start = window->size;
        }
        window->mouse_hovers_over_clickable = header_hover || resize_hover;
    }

    widget_container_handle_scroll_bar_input(
        window, &ui_system.containers[window->container.container_index], &window->container_layout_info, 0, widget_box,
        mouse, mouse_down, mouse_pressed, input->mouse_wheel_delta
    );

    window->mouse_hover_widget_index = -1;
    widget_container_find_mouse_hover_widget(
        window, &ui_system.containers[window->container.container_index], &window->container_layout_info, 0, widget_box, mouse
    );
    if (window->drag_active) {
        window->mouse_hover_widget_index = -1;
    }
}

void ui_system_end_frame_and_render(Window* whole_window, Mesh* mesh, Glyph_Atlas_* glyph_atlas, Input* input)
{
    bool mouse_down = input->mouse_down[(int)Mouse_Key_Code::LEFT];
    UI_Window* window = &ui_system.window;

    // Compact widgets and container arrays, and reset data for next frame
    {
        if (!mouse_down) {
            window->drag_active = false;
            window->window_drag_active = false;
            window->window_resize_active = false;
        }

        // Remove containers that weren't used this frame
        Array<int> moved_container_indices = array_create<int>(ui_system.containers.size);
        SCOPE_EXIT(array_destroy(&moved_container_indices));
        int next_container_index = 0;
        for (int i = 0; i < ui_system.containers.size; i++)
        {
            auto& container = ui_system.containers[i];
            if (container.visited_this_frame) {
                container.visited_this_frame = false;
                ui_system.containers[next_container_index] = container;
                moved_container_indices[i] = next_container_index;
                next_container_index += 1;
            }
            else {
                moved_container_indices[i] = -1;
                dynamic_array_destroy(&container.widget_indices);
            }
        }
        dynamic_array_rollback_to_size(&ui_system.containers, next_container_index);

        // Remove widgets that weren't used this frame
        Array<int> moved_widget_indices = array_create<int>(ui_system.widgets.size);
        SCOPE_EXIT(array_destroy(&moved_widget_indices));
        int next_widget_index = 0;
        for (int i = 0; i < ui_system.widgets.size; i++)
        {
            auto& widget = ui_system.widgets[i];
            widget.created_this_frame = false;
            if (widget.visited_this_frame) {
                widget.visited_this_frame = false;
                ui_system.widgets[next_widget_index] = widget;
                moved_widget_indices[i] = next_widget_index;
                next_widget_index += 1;
            }
            else {
                moved_widget_indices[i] = -1;
            }
        }
        dynamic_array_rollback_to_size(&ui_system.widgets, next_widget_index);

        // Update indices in containers
        for (int i = 0; i < ui_system.containers.size; i++)
        {
            auto& container = ui_system.containers[i];
            container.matching_failed_this_frame = false;
            container.next_matching_index = 0;

            if (!container.scroll_bar_was_added) {
                container.drag_start_bar_offset = 0;
                container.scroll_bar_drag_active = false;
            }
            if (!mouse_down) {
                container.scroll_bar_drag_active = false;
            }

            int next_child_index = 0;
            for (int j = 0; j < container.widget_indices.size; j++) {
                int index = container.widget_indices[j];
                int new_index = moved_widget_indices[index];
                if (new_index != -1) {
                    container.widget_indices[next_child_index] = new_index;
                    next_child_index += 1;
                }
            }
            dynamic_array_rollback_to_size(&container.widget_indices, next_child_index);
        }

        // Update indices in Widgets
        for (int i = 0; i < ui_system.widgets.size; i++) {
            auto& widget = ui_system.widgets[i];
            if (widget.type == Widget_Type::CONTAINER) {
                int new_container_index = moved_container_indices[widget.options.container.container_index];
                assert(new_container_index != -1, "Shouldn't happen");
                widget.options.container.container_index = new_container_index;
            }
        }

        ui_system.window.container.container_index = moved_container_indices[ui_system.window.container.container_index];
    }

    // Do layout
    Layout_Info& window_layout = window->container_layout_info;
    BBox window_box = BBox(window->position, window->position + window->size);
    BBox header_box = window_box;
    BBox client_box = window_box;
    BBox widget_box = window_box;
    {
        ui_system.containers[window->container.container_index].visited_this_frame = true;
        widget_container_gather_width_information_recursive(window->container, &window_layout);
        if (input->key_pressed[(int)Key_Code::X]) {
            window->size.x = window_layout.min_width_without_collapse + 4;
        }
        else if (input->key_pressed[(int)Key_Code::C]) {
            window->size.x = window_layout.min_width_collapsed + 4;
        }
        else if (input->key_pressed[(int)Key_Code::V]) {
            window->size.x = window_layout.min_width_for_line_merge + 4;
        }

        window_box = BBox(window->position, window->position + window->size);
        header_box = window_box;
        header_box.min.y = window_box.max.y - ui_system.line_item_height;
        client_box = window_box;
        client_box.max.y = header_box.min.y;
        widget_box = client_box;
        widget_box.max = widget_box.max - ivec2(2);
        widget_box.min = widget_box.min + ivec2(2);
        window_layout.box = widget_box;

        widget_container_calculate_x_bounds_and_height(
            window->container, &window_layout, widget_box.min.x, widget_box.max.x - widget_box.min.x
        );
        if (input->key_pressed[(int)Key_Code::Y]) {
            window->size.y = window_layout.min_height + 4 + ui_system.line_item_height;
        }
        {
            window_box = BBox(window->position, window->position + window->size);
            header_box = window_box;
            header_box.min.y = window_box.max.y - ui_system.line_item_height;
            client_box = window_box;
            client_box.max.y = header_box.min.y;
            widget_box = client_box;
            widget_box.max = widget_box.max - ivec2(2);
            widget_box.min = widget_box.min + ivec2(2);
            window_layout.box = widget_box;
        }

        widget_container_calculate_y_bounds(window->container, &window_layout, widget_box.max.y, widget_box.max.y - widget_box.min.y);
    }

    // Render
    {
        auto& info = rendering_core.render_information;
        ivec2 screen_size = ivec2(info.backbuffer_width, info.backbuffer_height);
        ivec2 mouse = ivec2(input->mouse_x, screen_size.y - input->mouse_y);
        window->mouse_hover_widget_index = -1;
        widget_container_find_mouse_hover_widget(
            window, &ui_system.containers[window->container.container_index], &window->container_layout_info, 0, widget_box, mouse
        );
        if (window->mouse_hover_widget_index != -1 && !window->drag_active) {
            Widget& widget = ui_system.widgets[window->mouse_hover_widget_index];
            if (widget.type == Widget_Type::BUTTON || widget.type == Widget_Type::TEXT_INPUT) {
                window->mouse_hovers_over_clickable = true;
            }
        }

        // Set mouse cursor
        if (window->mouse_hovers_over_clickable) {
            window->last_cursor_was_drag = true;
            window_set_cursor_icon(whole_window, Cursor_Icon_Type::HAND);
        }
        else {
            if (window->last_cursor_was_drag) {
                window->last_cursor_was_drag = false;
                window_set_cursor_icon(whole_window, Cursor_Icon_Type::ARROW);
            }
        }

        // Render Window + widgets
        mesh_push_box(mesh, header_box, COLOR_WINDOW_BG_HEADER);
        mesh_push_box(mesh, client_box, COLOR_WINDOW_BG);
        mesh_push_text_clipped(mesh, glyph_atlas, window->title, header_box.min + ivec2(BORDER_SPACE) + ivec2(PAD_LEFT_RIGHT, PAD_BOT), header_box);
        widget_container_render_widgets_recursive(window->container, &window_layout, widget_box, 0, mesh, glyph_atlas);
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
    ui_system_initialize(&glyph_atlas);

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
        ui_system_start_frame(input);

        ui_system_push_label(ui_system.window.container, "Hello IMGUI world!");
        for (int i = 0; i < 4; i++)
        {
            const char* labels[3] = {
                "Name",
                "Surname",
                "Address",
            };
            const char* texts[3] = {
                "Something that you soundlt ",
                "Dont you carrera about me",
                "Wellerman",
            };
            ui_system_push_text_input(ui_system.window.container, labels[i % 3], string_create_static(texts[i % 3]));
        }
        ui_system_push_button(ui_system.window.container, "Frick me");
        ui_system_push_text_input(ui_system.window.container, "Name:", string_create_static("Frank Woo"));
        ui_system_push_button(ui_system.window.container, "Frick me");
        ui_system_push_text_input(ui_system.window.container, "Name:", string_create_static("Frank What why where"));

        ui_system_end_frame_and_render(window, mesh, &glyph_atlas, input);

        // Tests for Text-Rendering
        if (false)
        {
            // Render mesh with bitmap at the center of the screen
            auto& info = rendering_core.render_information;
            ivec2 screen_size = ivec2(info.backbuffer_width, info.backbuffer_height);
            ivec2 center = ivec2(screen_size.x / 2, screen_size.y / 2);
            if (input->mouse_down[(int)Mouse_Key_Code::LEFT]) {
                auto& info = rendering_core.render_information;
                ivec2 screen_size = ivec2(info.backbuffer_width, info.backbuffer_height);
                ivec2 mouse = ivec2(input->mouse_x, screen_size.y - input->mouse_y);
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
