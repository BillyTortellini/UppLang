#include "ui_system.hpp"

#include "../../utility/line_edit.hpp"
#include "../../rendering/texture.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../win32/window.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

UI_Icon ui_icon_make(Icon_Type type, Icon_Rotation rotation, vec3 color)
{
	UI_Icon icon;
	icon.type = type;
	icon.rotation = rotation;
	icon.color = color;
	return icon;
}

// Constants
const float WINDOW_RESIZE_RADIUS = 5;
const float WINDOW_RESIZE_RADIUS_INSIDE_HEADER = 2;

// Widget Sizes and Paddings
const int PAD_TOP = 2;
const int PAD_BOT = 1;
const int PAD_LEFT_RIGHT = 2;
const int BORDER_SPACE = 1;
const int TEXT_BORDER_SPACE = BORDER_SPACE * 2 + PAD_LEFT_RIGHT * 2;

const int PAD_LABEL_BOX = 1;
const int PAD_ADJACENT_LABLE_LINE_SPLIT = 6;

const int PAD_WIDGETS_ON_LINE = 6;
const int PAD_WIDGETS_BETWEEN_LINES = 1;

const int SCROLL_BAR_WIDTH = 10;
const int MIN_SCROLL_BAR_HEIGHT = 10;
const int SCROLL_BAR_PADDING = 1; // Top/Bot/Left/Right padding
const int MOUSE_WHEEL_SENSITIVITY = 15;

// Widget min sizes
const int LABEL_CHAR_COUNT_SIZE = 12;
const int TEXT_INPUT_MIN_CHAR_COUNT = 10;
const int TEXT_INPUT_MAX_CHAR_COUNT = 20;
const int BUTTON_MIN_CHAR_COUNT = 6;
const int BUTTON_WANTED_CHAR_COUNT = 10;
const int LIST_CONTAINER_MIN_CHAR_COUNT = 16;

const float ICON_SIZE_TO_LINE_SIZE = 0.8f;
const int ICON_PADDING = 2;

const int MIN_WINDOW_WIDTH = 60;
const int MIN_WINDOW_HEIGHT = 40;

// Colors
const vec4 COLOR_WINDOW_BG = vec4_color_from_rgb(0x16, 0x85, 0x5B);
const vec4 COLOR_WINDOW_BG_HEADER = vec4_color_from_rgb(0x62, 0xA1, 0x99);
const vec4 COLOR_WINDOW_BORDER = vec4_color_from_rgb(0, 0, 0);
const vec4 COLOR_LIST_BG = vec4_color_from_rgb(0x05, 0x50, 0x50);
const vec4 COLOR_SCROLL_BG = vec4_color_from_rgb(0xCE, 0xCE, 0xCE);
const vec4 COLOR_SCROLL_BAR = vec4_color_from_rgb(0x9D, 0x9D, 0x9D);
const vec4 COLOR_BUTTON_BORDER = vec4_color_from_rgb(0x19, 0x75, 0xD0);
const vec4 COLOR_BUTTON_BG = vec4_color_from_rgb(0x0F, 0x47, 0x7E);
const vec4 COLOR_BUTTON_BG_HOVER = vec4_color_from_rgb(0x71, 0xA9, 0xE2);

const vec4 COLOR_INPUT_BG = vec4_color_from_code("#A7A7A7");
const vec4 COLOR_INPUT_BG_NUMBER = vec4_color_from_code("#878787");
const vec4 COLOR_INPUT_BG_HOVER = vec4_color_from_code("#699EB6");
const vec4 COLOR_FOCUSED_BG = vec4_color_from_code("#808080");
const vec4 COLOR_INPUT_BORDER = vec4_color_from_code("#696969");
const vec4 COLOR_INPUT_BORDER_FOCUSED = vec4_color_from_code("#FF8F00");

const vec4 COLOR_LIST_LINE_EVEN = vec4_color_from_rgb(0xFE, 0xCB, 0xA3);
const vec4 COLOR_LIST_LINE_ODD = vec4_color_from_rgb(0xB6, 0xB1, 0xAC);

const vec4 COLOR_DROPDOWN_BG = vec4_color_from_rgb(100, 100, 100);
const vec4 COLOR_DROPDOWN_HOVER = vec4_color_from_rgb(130, 130, 130);



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

float distance_point_to_line_segment(vec2 p, vec2 a, vec2 b)
{
    vec2 a_to_b = b - a;
    float t = vector_dot(p - a, a_to_b) / vector_dot(a_to_b, a_to_b);
    t = math_clamp(t, 0.0f, 1.0f);
    vec2 closest = a + t * a_to_b;
    return vector_length(p - closest);
}

float sdf_triangle(vec2 pos, vec2 a, vec2 b, vec2 c)
{
	// Point inside triangle check (Note: The distances are squared here)
	vec2 normal_a_b = vector_rotate_90_degree_counter_clockwise(vector_normalize_safe(b - a));
	vec2 normal_b_c = vector_rotate_90_degree_counter_clockwise(vector_normalize_safe(c - b));
	vec2 normal_c_a = vector_rotate_90_degree_counter_clockwise(vector_normalize_safe(a - c));
	float dist_a_b = vector_dot(normal_a_b, pos - a);
	float dist_b_c = vector_dot(normal_b_c, pos - b);
	float dist_c_a = vector_dot(normal_c_a, pos - c);

	float sdf = -math_minimum(dist_c_a, math_minimum(dist_a_b, dist_b_c));
	if (sdf <= 0.0f) {
		// Inside triangle
		return sdf;
	}

	// Position outside triangle, here we need to also check distances to points
	float to_a = vector_length_squared(pos - a);
	float to_b = vector_length_squared(pos - b);
	float to_c = vector_length_squared(pos - c);
	float min_to_vertices = math_square_root(math_minimum(to_a, math_minimum(to_b, to_c)));
	return math_minimum(min_to_vertices, sdf);
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

BBox bitmap_atlas_make_space_for_sub_image(Bitmap_Atlas_Writer* atlas, ivec2 size)
{
	auto& write_pos = atlas->write_pos;
	auto& atlas_size = atlas->bitmap->size;

	// Check if atlas-bitmap is large enough for given bitmap and position
	if (size.x >= atlas_size.x || size.y >= atlas_size.y) {
		return BBox(ivec2(0));
	}

	// Jump to next line in atlas if current line is full
	if (write_pos.x + size.x >= atlas_size.x)
	{
		// Check if atlas is exhausted (No more free space)
		int next_write_y = write_pos.y + atlas->max_subimage_height_in_current_line;
		if (next_write_y + size.y >= atlas_size.y) {
			return BBox(ivec2(0));
		}

		write_pos.x = 0;
		write_pos.y = next_write_y;
		atlas->max_subimage_height_in_current_line = 0;
	}

	// Store information
	BBox result_box = BBox(write_pos, write_pos + size);
	write_pos.x += size.x;
	atlas->max_subimage_height_in_current_line = math_maximum(atlas->max_subimage_height_in_current_line, size.y);

	return result_box;
}

BBox bitmap_atlas_add_sub_image(Bitmap_Atlas_Writer* atlas, Bitmap bitmap, bool mirror_y = false)
{
	BBox result = bitmap_atlas_make_space_for_sub_image(atlas, bitmap.size);
	if (result.min.x == result.max.x) return result;
	bitmap_block_transfer_(*atlas->bitmap, bitmap, result.min, mirror_y);
	return result;
}

typedef float (*bitmap_atlas_sdf_function)(vec2 pos, int pixel_size);
BBox bitmap_atlas_writer_add_sdf_symbol(Bitmap_Atlas_Writer* writer, int size, bitmap_atlas_sdf_function sdf_function)
{
	BBox symbol_box = bitmap_atlas_make_space_for_sub_image(writer, ivec2(size));
	for (int x_pixel = 0; x_pixel < size; x_pixel++)
	{
		for (int y_pixel = 0; y_pixel < size; y_pixel++)
		{
			ivec2 pixel_pos = symbol_box.min + ivec2(x_pixel, y_pixel);
			u8* pixel_data = &writer->bitmap->data[pixel_pos.x + pixel_pos.y * writer->bitmap->pitch];
			float pixel_width = 2.0f / size;

			// Normalized pos (-1 to 1), Note: Samples at the pixel center
			vec2 pos = vec2((float)(x_pixel + 0.5f) / (float)size, (y_pixel + 0.5f) / (float)size);
			pos = pos * 2 - 1;

			float sdf = sdf_function(pos, size);
			float value = 0.0f;
			sdf += pixel_width / 2.0f;
			if (sdf < 0.0f) {
				value = 0.0f;
			}
			else if (sdf >= pixel_width) {
				value = 1.0f;
			}
			else {
				value = sdf / pixel_width;
			}
			value = 1.0f - value;
			*pixel_data = math_clamp((int)(value * 255), 0, 255);
		}
	}
	return symbol_box;
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
	int max_descender_height;
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
	result.max_descender_height = 0;
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
		max_advance = math_maximum(max_advance, (int)face->glyph->metrics.horiAdvance / 64);
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
		glyph_atlas->character_to_glyph_map[current_character] = glyph_atlas->glyph_informations.size - 1;
	}

	//printf("Max-Y character: '%c' (#%d)\n", max_y_index, max_y_index);

	// Adjust placement offsets so we only deal with 
	for (int i = 0; i < glyph_atlas->glyph_informations.size; i++) {
		auto& glyph = glyph_atlas->glyph_informations[i];
		glyph.placement_offset.y += -min_y;
	}
	glyph_atlas->char_box_size.x = max_advance;
	glyph_atlas->char_box_size.y = max_y - min_y;
	glyph_atlas->max_descender_height = -min_y;
}



void mesh_push_text(Mesh* mesh, Glyph_Atlas_* atlas, String text, ivec2 position)
{
	vec2 screen_size = vec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);
	vec2 bitmap_size = vec2(atlas->bitmap_atlas_size.x, atlas->bitmap_atlas_size.y);

	u32 start_vertex_count = mesh->vertex_count;
	auto& predef = rendering_core.predefined;
	Array<vec2> pos_data = mesh_push_attribute_slice(mesh, predef.position2D, 4 * text.size);
	Array<vec4> color_data = mesh_push_attribute_slice(mesh, predef.color4, 4 * text.size);
	Array<vec2> uv_data = mesh_push_attribute_slice(mesh, predef.texture_coordinates, 4 * text.size);
	Array<u32> indices = mesh_push_attribute_slice(mesh, predef.index, 6 * text.size);

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
	Attribute_Buffer* pos_buffer = mesh_get_raw_attribute_buffer(mesh, predef.position2D);
	Attribute_Buffer* color_buffer = mesh_get_raw_attribute_buffer(mesh, predef.color4);
	Attribute_Buffer* uv_buffer = mesh_get_raw_attribute_buffer(mesh, predef.texture_coordinates);
	Attribute_Buffer* index_buffer = mesh_get_raw_attribute_buffer(mesh, predef.index);

	for (int i = char_start_index; i < char_end_index; i++)
	{
		unsigned char c = text.characters[i];
		if (c == ' ') continue;
		Glyph_Information_& glyph = atlas->glyph_informations[atlas->character_to_glyph_map[c]];

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
		Array<vec2> pos_data = attribute_buffer_allocate_slice<vec2>(pos_buffer, 4);
		Array<vec4> color_data = attribute_buffer_allocate_slice<vec4>(color_buffer, 4);
		Array<vec2> uv_data = attribute_buffer_allocate_slice<vec2>(uv_buffer, 4);
		Array<u32>  indices = attribute_buffer_allocate_slice<u32>(index_buffer, 6);

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

void mesh_push_icon(Mesh* mesh, ivec2 position, BBox subimage, ivec2 atlas_bitmap_size, Icon_Rotation rotation, vec4 color)
{
	vec2 screen_size = vec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);
	vec2 bitmap_size = vec2(atlas_bitmap_size.x, atlas_bitmap_size.y);

	u32 start_vertex_count = mesh->vertex_count;
	auto& predef = rendering_core.predefined;
	Array<vec2> pos_data = mesh_push_attribute_slice(mesh, predef.position2D, 4);
	Array<vec4> color_data = mesh_push_attribute_slice(mesh, predef.color4, 4);
	Array<vec2> uv_data = mesh_push_attribute_slice(mesh, predef.texture_coordinates, 4);
	Array<u32> indices = mesh_push_attribute_slice(mesh, predef.index, 6);

	{
		ivec2& glyph_size = subimage.max - subimage.min;

		vec2 normalized_pos = 2.0f * vec2(position.x, position.y) / screen_size - 1.0f;
		vec2 normalized_size = 2.0f * vec2(glyph_size.x, glyph_size.y) / screen_size;

		pos_data[0] = normalized_pos + normalized_size * vec2(0, 0);
		pos_data[1] = normalized_pos + normalized_size * vec2(1, 0);
		pos_data[2] = normalized_pos + normalized_size * vec2(1, 1);
		pos_data[3] = normalized_pos + normalized_size * vec2(0, 1);

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

		switch (rotation) 
		{
		case Icon_Rotation::NONE: break;
		case Icon_Rotation::ROT_90: {
			vec2 swap = uv_data[0];
			uv_data[0] = uv_data[1];
			uv_data[1] = uv_data[2];
			uv_data[2] = uv_data[3];
			uv_data[3] = swap;
			break;
		}
		case Icon_Rotation::ROT_180: {
			vec2 swap = uv_data[0];
			uv_data[0] = uv_data[2];
			uv_data[2] = swap;
			swap = uv_data[1];
			uv_data[1] = uv_data[3];
			uv_data[3] = swap;
			break;
		}
		case Icon_Rotation::ROT_270: {
			vec2 swap = uv_data[3];
			uv_data[3] = uv_data[2];
			uv_data[2] = uv_data[1];
			uv_data[1] = uv_data[0];
			uv_data[0] = swap;
			break;
		}
		default: panic("");
		}

		indices[0] = start_vertex_count + 0;
		indices[1] = start_vertex_count + 1;
		indices[2] = start_vertex_count + 2;
		indices[3] = start_vertex_count + 0;
		indices[4] = start_vertex_count + 2;
		indices[5] = start_vertex_count + 3;
	}
}

void mesh_push_icon_clipped(Mesh* mesh, ivec2 position, BBox subimage, ivec2 atlas_bitmap_size, BBox clipping_box, Icon_Rotation rotation, vec4 color)
{
	BBox box = BBox(position, position + subimage.max - subimage.min);
	BBox clipped_box = bbox_intersection(box, clipping_box);
	if (bbox_is_empty(clipped_box)) {
		return;
	}
	if (bbox_equals(clipped_box, box)) { // No clipping
		mesh_push_icon(mesh, position, subimage, atlas_bitmap_size, rotation, color);
		return;
	}

	vec2 screen_size = vec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);
	vec2 bitmap_size = vec2(atlas_bitmap_size.x, atlas_bitmap_size.y);

	u32 start_vertex_count = mesh->vertex_count;
	auto& predef = rendering_core.predefined;
	Array<vec2> pos_data = mesh_push_attribute_slice(mesh, predef.position2D, 4);
	Array<vec4> color_data = mesh_push_attribute_slice(mesh, predef.color4, 4);
	Array<vec2> uv_data = mesh_push_attribute_slice(mesh, predef.texture_coordinates, 4);
	Array<u32> indices = mesh_push_attribute_slice(mesh, predef.index, 6);

	{
		ivec2& glyph_size = subimage.max - subimage.min;

		vec2 normalized_pos = 2.0f * vec2(position.x, position.y) / screen_size - 1.0f;
		vec2 normalized_size = 2.0f * vec2(glyph_size.x, glyph_size.y) / screen_size;

		ivec2& pixel_size = clipped_box.max - clipped_box.min;
		vec2 min_pos = 2.0f * vec2(clipped_box.min.x, clipped_box.min.y) / screen_size - 1.0f;
		vec2 max_pos = 2.0f * vec2(clipped_box.max.x, clipped_box.max.y) / screen_size - 1.0f;
		pos_data[0] = min_pos;
		pos_data[1] = vec2(max_pos.x, min_pos.y);
		pos_data[2] = max_pos;
		pos_data[3] = vec2(min_pos.x, max_pos.y);

		BBox uv_box = subimage;
		uv_box.min = subimage.min + clipped_box.min - box.min;
		uv_box.max = subimage.max + clipped_box.max - box.max;
		vec2 uv_min = vec2(uv_box.min.x, uv_box.min.y) / bitmap_size;
		vec2 uv_max = vec2(uv_box.max.x, uv_box.max.y) / bitmap_size;
		uv_data[0] = uv_min;
		uv_data[1] = vec2(uv_max.x, uv_min.y);
		uv_data[2] = uv_max;
		uv_data[3] = vec2(uv_min.x, uv_max.y);

		switch (rotation) 
		{
		case Icon_Rotation::NONE: break;
		case Icon_Rotation::ROT_90: {
			vec2 swap = uv_data[0];
			uv_data[0] = uv_data[1];
			uv_data[1] = uv_data[2];
			uv_data[2] = uv_data[3];
			uv_data[3] = swap;
			break;
		}
		case Icon_Rotation::ROT_180: {
			vec2 swap = uv_data[0];
			uv_data[0] = uv_data[2];
			uv_data[2] = swap;
			swap = uv_data[1];
			uv_data[1] = uv_data[3];
			uv_data[3] = swap;
			break;
		}
		case Icon_Rotation::ROT_270: {
			vec2 swap = uv_data[3];
			uv_data[3] = uv_data[2];
			uv_data[2] = uv_data[1];
			uv_data[1] = uv_data[0];
			uv_data[0] = swap;
			break;
		}
		default: panic("");
		}

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

void mesh_push_box(Mesh* mesh, BBox box, vec4 color)
{
	if (bbox_is_empty(box)) return;

	u32 start_vertex_count = mesh->vertex_count;
	auto& predef = rendering_core.predefined;
	Array<vec2> pos_data = mesh_push_attribute_slice(mesh, predef.position2D, 4);
	Array<vec4> color_data = mesh_push_attribute_slice(mesh, predef.color4, 4);
	Array<vec2> uv_data = mesh_push_attribute_slice(mesh, predef.texture_coordinates, 4);
	Array<u32>  indices = mesh_push_attribute_slice(mesh, predef.index, 6);

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



// UI_SYSTEM START
enum class Layout_Type
{
	NORMAL, // Stack-Horizontal with option to combine lines
	STACK_HORIZONTAL, // All widgets are added in a single line
};

struct Container_Element
{
	bool is_widget;
	int element_index; // Either widget or container index

	// Layout information
	bool can_combine_in_lines;
	int min_width_collapsed;
	int min_width_without_collapse;
	int min_width_for_line_merge;
	bool width_can_grow_beyond_max;

	// Given the available width, containers can calculate their height
	int min_height;
	int wanted_height; // This value should be used to check if a container needs a scroll-bar
	bool height_can_grow; // For widgets that want to grow in y (Lists or others)

	// Calculated by container
	BBox box;
	int line_index;
};

struct UI_Matching_Info
{
	bool is_widget;
	int element_index;
	bool is_hidden;
	// TODO: Some other things for comparing matching, like substring or something other...
};

struct Container_Layout
{
	Layout_Type type;
	union {
		struct
		{
			bool allow_line_combination;
			int indentation;
			bool scroll_bar_enabled; // If scroll bar is enabled, then the widget automatically fills it's height
			int min_height_empty;
			int min_height_restrained; // Only valid if scroll bar is enabled 
		} normal;
		bool horizontal_allow_collapse;
	} options;

	bool draw_background;
	vec4 background_color;
	int padding;
};

struct Scroll_Bar_Info
{
	bool has_scroll_bar;
	int pixel_scroll_y;

	int bar_offset; // Bar offset from top of scroll-area
	int bar_height;
	int max_bar_offset; // Maximum bar offset 
	int max_pixel_scroll_offset; // Maximum pixel_scroll_offset
};

struct Widget_Container
{
	Container_Layout layout;

	Dynamic_Array<Container_Element> elements;
	Dynamic_Array<UI_Matching_Info> matching_infos; // Includes hidden containers (For matching)
	Container_Handle parent_container; // -1 if root of a window

	int next_matching_index;
	bool visited_this_frame;
	bool matching_failed_this_frame;

	// Intermediate layout data
	bool collaps_allowed;
	int sum_child_min_width_without_collapse;
	int sum_child_min_width_for_line_merge;

	int line_count;
	int sum_line_min_heights;
	int sum_line_wanted_heights;
	int growable_line_count;
	int elements_with_growable_width_count;

	Scroll_Bar_Info scroll_bar_info;
};

enum class Text_Input_Type
{
	TEXT,
	INT,
	FLOAT
};

enum class Text_Alignment
{
	LEFT,
	CENTER,
	RIGHT
};

struct Widget_Style
{
	// Layout information
	int min_width;
	int max_width;
	bool can_grow_beyond_max_width;
	int height;
	bool can_combine_in_lines;

	// Input information
	bool is_clickable;
	bool can_obtain_text_input;
	Text_Input_Type text_input_type;

	// BG-Color
	bool draw_background;
	vec4 background_color;
	vec4 hover_color;
	vec4 focused_color;
	bool has_border;
	vec4 border_color;

	// Text
	UI_String text_display; // Text to display when rendering
	Text_Alignment text_alignment;

	// Icons and offsets
	bool draw_icon;
	bool icon_left_aligned;
	UI_Icon icon;
};

struct Widget
{
	Widget_Style style;

	// Matching information
	Container_Handle parent_container;
	int element_index_in_container; // Note: This is only valid after layout was done!
	bool visited_this_frame;
	bool created_this_frame;
};

struct UI_Window
{
	Window_Style style;
	BBox window_box;
	int z_index;
	bool visited_this_frame;
	bool created_this_frame;
	Container_Element root;
};

enum class Drag_Status
{
	SCROLL_BAR,
	WINDOW_MOVE,
	WINDOW_RESIZE,
	NONE,
};

struct UI_System
{
	// Data-Structures
	Dynamic_Array<UI_Window> windows;
	Dynamic_Array<int> window_z_sorting; // From smallest to largest z
	Dynamic_Array<Widget> widgets;
	Dynamic_Array<Widget_Container> containers;
	Dynamic_Array<Container_Handle> container_stack;
	String string_buffer;

	// Misc
	bool pop_container_after_next_push;
	int next_window_index;
	int max_window_z_index;
	int new_windows_this_frame_count;

	// Layout info
	int line_item_height;
	ivec2 char_size;
	int max_descender_height;
	int icon_size;

	// Hover infos (-1 if no hover)
	int mouse_hover_window_index;
	int mouse_hover_closest_window_index; // Note: May not be window index, as resize is also active a little bit outside of window
	int mouse_hover_container_index;
	int mouse_hover_widget_index;
	Drag_Status mouse_hover_drag_status; // If mouse hovers over draggable
	ivec2 mouse_hover_resize_direction;

	// Input Data
	Drag_Status drag_status;
	int drag_index;
	Cursor_Icon_Type last_cursor_icon_type;
	ivec2 resize_direction;
	ivec2 drag_start_mouse_pos;
	BBox drag_start_window_box;
	int drag_start_bar_offset;

	int focused_widget_index; // -1 if not available
	bool mouse_was_clicked;
	int text_changed_widget_index;
	UI_String changed_text;

	Line_Editor line_editor;
	String input_string;
	int input_x_offset;

	// Rendering Data
	Bitmap atlas_bitmap;
	Glyph_Atlas_ glyph_atlas;
	Mesh* mesh;
	Shader* shader;
	Texture* texture;

	BBox icon_boxes[(int)Icon_Type::MAX_ENUM_VALUE];
	BBox atlas_box_text_clipping;
};

static UI_System ui_system;

Container_Layout container_layout_make_default();

void ui_system_initialize()
{
	// Create rendering objects
	{
		ui_system.atlas_bitmap = bitmap_create(ivec2(256));
		auto& bitmap = ui_system.atlas_bitmap;
		Bitmap_Atlas_Writer atlas_writer = bitmap_atlas_writer_make(&bitmap);
		// Initialize atlas data with pattern for error recognition
		{
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
		{
			// Note: Set pixel at (0, 0) to value 255, because this is used by rectangles with solid colors
			u8 value = 255;
			Bitmap bmp = bitmap_create_static(ivec2(1, 1), &value, 1);
			BBox pixel_box = bitmap_atlas_add_sub_image(&atlas_writer, bmp);
			ivec2 pos = pixel_box.min;
			assert(pos.x == 0 && pos.y == 0, "");
		}

		ui_system.glyph_atlas = glyph_atlas_create();
		glyph_atlas_rasterize_font(&ui_system.glyph_atlas, &atlas_writer, "resources/fonts/mona_neon.ttf", 14);
		//glyph_atlas_rasterize_font(&ui_system.glyph_atlas, &atlas_writer, "resources/fonts/consola.ttf", 14);

		// Note: Storing height infos needs to be done before creating symbols
		ui_system.char_size = ui_system.glyph_atlas.char_box_size;
		ui_system.line_item_height = PAD_TOP + PAD_BOT + 2 * BORDER_SPACE + ui_system.char_size.y;
		ui_system.max_descender_height = ui_system.glyph_atlas.max_descender_height;
		ui_system.icon_size = (int) (ui_system.line_item_height * ICON_SIZE_TO_LINE_SIZE);

		// Draw symbols into glyph-atlas
		for (int i = 0; i < (int)Icon_Type::MAX_ENUM_VALUE; i++)
		{
			Icon_Type type = (Icon_Type)i;

			auto sdf_check_mark = [](vec2 pos, int pixel_size) -> float {
				float pixel_width = 1.0f / pixel_size;
				float thickness = 5 * pixel_width;
				float r = thickness / 2.0f;
				float border_spacing = r + pixel_width;
				float max = 1.0f - border_spacing;
				vec2 a = vec2(-max, 0.0f);
				vec2 b = vec2(-1.0f + 2.0f / 3.0f, -max);
				vec2 c = vec2(max);
				float sdf = distance_point_to_line_segment(pos, a, b);
				sdf = math_minimum(sdf, distance_point_to_line_segment(pos, b, c));
				// Add thickness
				sdf -= r;
				return sdf;
			};

			auto sdf_close_symbol = [](vec2 pos, int pixel_size) -> float {
				float pixel_width = 1.0f / pixel_size;
				float thickness = 5 * pixel_width;
				float r = thickness / 2.0f;
				float border_spacing = r + pixel_width;
				float max = 1.0f - border_spacing;
				vec2 a = vec2(-max, -max);
				vec2 b = vec2(max, -max);
				vec2 c = vec2(max, max);
				vec2 d = vec2(-max, max);
				float sdf = distance_point_to_line_segment(pos, a, c);
				sdf = math_minimum(sdf, distance_point_to_line_segment(pos, b, d));
				// Add thickness
				sdf -= r;
				return sdf;
			};

			auto sdf_left_arrow = [](vec2 pos, int pixel_size) -> float {
				float pixel_width = 1.0f / pixel_size;
				float thickness = 5 * pixel_width;
				float r = thickness / 2.0f;
				float border_spacing = r + pixel_width;
				float max = 1.0f - border_spacing;
				vec2 a = vec2(-max, 0.0f);
				vec2 b = vec2(max, 0.0f);
				vec2 c = vec2(0.0f, max);
				vec2 d = vec2(0.0f, -max);
				float sdf = distance_point_to_line_segment(pos, a, b);
				sdf = math_minimum(sdf, distance_point_to_line_segment(pos, b, c));
				sdf = math_minimum(sdf, distance_point_to_line_segment(pos, b, d));
				// Add thickness
				sdf -= r;
				return sdf;
			};

			auto sdf_left_triangle = [](vec2 pos, int pixel_size) -> float {
				float pixel_width = 1.0f / pixel_size;
				float r = pixel_width * 2; // Radius doesn't really mean much for triangle sdf
				float max = 1.0f - (r + pixel_width); // Keep outline 1 pixel width away from border (For anti-aliasing)
				vec2 a = vec2(-max);
				vec2 b = vec2(max, 0.0f);
				vec2 c = vec2(-max, max);

				return sdf_triangle(pos, a, b, c) - r;
			};

			auto sdf_left_triangle_small = [](vec2 pos, int pixel_size) -> float {
				float pixel_width = 1.0f / pixel_size;
				float r = pixel_width * 2; // Radius doesn't really mean much for triangle sdf
				float max = 1.0f - (r + pixel_width); // Keep outline 1 pixel width away from border (For anti-aliasing)
				float scale = 0.3;
				vec2 a = vec2(-max) * scale;
				vec2 b = vec2(max, 0.0f) * scale;
				vec2 c = vec2(-max, max) * scale;

				return sdf_triangle(pos, a, b, c) - r;
				};

			auto sdf_none = [](vec2 pos, int pixel_size) { return 1000.0f; };

			bitmap_atlas_sdf_function sdf = nullptr;
			switch (type)
			{
			case Icon_Type::TRIANGLE_LEFT: sdf = sdf_left_triangle; break;
			case Icon_Type::TRIANGLE_LEFT_SMALL: sdf = sdf_left_triangle_small; break;
			case Icon_Type::CHECK_MARK: sdf = sdf_check_mark; break;
			case Icon_Type::X_MARK: sdf = sdf_close_symbol; break;
			case Icon_Type::ARROW_LEFT: sdf = sdf_left_arrow; break;
			case Icon_Type::NONE: sdf = sdf_none; break;
			default: panic("");
			}
			ui_system.icon_boxes[i] = bitmap_atlas_writer_add_sdf_symbol(&atlas_writer, ui_system.icon_size, sdf);
		}

		ui_system.atlas_box_text_clipping = bitmap_atlas_make_space_for_sub_image(&atlas_writer, ui_system.char_size);
		for (int x_pixel = 0; x_pixel < ui_system.char_size.x; x_pixel++) {
			for (int y_pixel = 0; y_pixel < ui_system.char_size.y; y_pixel++) {
				ivec2 pixel_pos = ui_system.atlas_box_text_clipping.min + ivec2(x_pixel, y_pixel);
				u8* pixel_data = &atlas_writer.bitmap->data[pixel_pos.x + pixel_pos.y * atlas_writer.bitmap->pitch];
				*pixel_data = 0;
			}
		}
		{
			int available_size = ui_system.char_size.x;
			int dot_size = 2;
			int spacing = 1;
			int x = 0;
			while (x + dot_size <= available_size)
			{
				// Draw single dot
				for (int i = 0; i < dot_size && i + x < available_size; i++) {
					for (int y = 0; y < dot_size && y + ui_system.glyph_atlas.max_descender_height < ui_system.char_size.y; y++) {
						ivec2 pixel_pos = ui_system.atlas_box_text_clipping.min + ivec2(x + i, y + ui_system.glyph_atlas.max_descender_height);
						u8* pixel_data = &atlas_writer.bitmap->data[pixel_pos.x + pixel_pos.y * atlas_writer.bitmap->pitch];
						*pixel_data = 255;
					}
				}

				x += dot_size + spacing;
			}
		}



		// Create Rendering objects
		ui_system.texture = texture_create_from_bytes(
			Texture_Type::RED_U8,
			array_create_static((byte*)bitmap.data, bitmap.size.x * bitmap.size.y),
			bitmap.size.x, bitmap.size.y,
			false
		);

		auto& predef = rendering_core.predefined;
		Vertex_Description* vertex_desc = vertex_description_create({ predef.position2D, predef.texture_coordinates, predef.color4, predef.index });
		ui_system.mesh = rendering_core_query_mesh("UI_Render_Mesh", vertex_desc, true);
		ui_system.shader = rendering_core_query_shader("mono_texture.glsl");
	}

	ui_system.windows = dynamic_array_create<UI_Window>();
	ui_system.window_z_sorting = dynamic_array_create<int>();
	ui_system.containers = dynamic_array_create<Widget_Container>();
	ui_system.widgets = dynamic_array_create<Widget>();
	ui_system.string_buffer = string_create();
	ui_system.container_stack = dynamic_array_create<Container_Handle>();

	ui_system.max_window_z_index = 0;
	ui_system.next_window_index = 0;

	// Input 
	ui_system.drag_status = Drag_Status::NONE;
	ui_system.mouse_was_clicked = false;
	ui_system.mouse_hover_widget_index = -1;
	ui_system.focused_widget_index = -1;
	ui_system.last_cursor_icon_type = Cursor_Icon_Type::ARROW;

	ui_system.line_editor = line_editor_make();
	ui_system.input_string = string_create();
}

void ui_system_shutdown()
{
	dynamic_array_destroy(&ui_system.windows);
	dynamic_array_destroy(&ui_system.window_z_sorting);
	for (int i = 0; i < ui_system.containers.size; i++) {
		dynamic_array_destroy(&ui_system.containers[i].elements);
		dynamic_array_destroy(&ui_system.containers[i].matching_infos);
	}
	dynamic_array_destroy(&ui_system.containers);
	dynamic_array_destroy(&ui_system.widgets);
	dynamic_array_destroy(&ui_system.container_stack);
	string_destroy(&ui_system.string_buffer);
	string_destroy(&ui_system.input_string);

	bitmap_destroy(ui_system.atlas_bitmap);
	texture_destroy(ui_system.texture);
	glyph_atlas_destroy(&ui_system.glyph_atlas);
}

UI_String ui_system_add_string(String string)
{
	UI_String result;
	result.start_index = ui_system.string_buffer.size;
	result.length = string.size;
	string_append_string(&ui_system.string_buffer, &string);
	return result;
}

String ui_string_to_string(UI_String string) {
	String result;
	result.capacity = 0;
	result.characters = &ui_system.string_buffer[string.start_index];
	result.size = string.length;
	return result;
};

void ui_system_push_active_container(Container_Handle handle, bool pop_after_next_push) {
	dynamic_array_push_back(&ui_system.container_stack, handle);
	ui_system.pop_container_after_next_push = pop_after_next_push;
}

void ui_system_pop_active_container() {
	ui_system.container_stack.size = math_maximum(0, ui_system.container_stack.size - 1);
	ui_system.pop_container_after_next_push = false;
}

Widget_Handle ui_system_add_widget(Widget_Style style, bool is_hidden = false)
{
	assert(ui_system.container_stack.size > 0, "");
	Container_Handle container_handle;
	container_handle.container_index = ui_system.container_stack[ui_system.container_stack.size - 1].container_index;
	if (ui_system.pop_container_after_next_push) {
		ui_system.container_stack.size -= 1;
		ui_system.pop_container_after_next_push = false;
	}

	Widget_Container& container = ui_system.containers[container_handle.container_index];

	// Check if we can match widget to previous frame
	if (!container.matching_failed_this_frame && container.next_matching_index < container.matching_infos.size)
	{
		UI_Matching_Info& next_match = container.matching_infos[container.next_matching_index];
		if (next_match.is_widget)
		{
			Widget& widget = ui_system.widgets[next_match.element_index];

			// Found match
			next_match.is_hidden = is_hidden;
			container.next_matching_index += 1;
			widget.visited_this_frame = true;
			widget.created_this_frame = false;
			widget.element_index_in_container = -1;
			widget.style = style;
			widget.parent_container = container_handle;

			Widget_Handle result;
			result.created_this_frame = widget.created_this_frame;
			result.widget_index = next_match.element_index;
			return result;

		}
	}
	container.matching_failed_this_frame = true;

	// Otherwise create new data for widget
	Widget new_widget;
	new_widget.created_this_frame = true;
	new_widget.visited_this_frame = true;
	new_widget.parent_container = container_handle;
	new_widget.element_index_in_container = -1;
	new_widget.style = style;
	dynamic_array_push_back(&ui_system.widgets, new_widget);

	UI_Matching_Info matching_info;
	matching_info.is_widget = true;
	matching_info.element_index = ui_system.widgets.size - 1;
	matching_info.is_hidden = is_hidden;
	dynamic_array_push_back(&container.matching_infos, matching_info);

	Widget_Handle result;
	result.created_this_frame = new_widget.created_this_frame;
	result.widget_index = ui_system.widgets.size - 1;
	return result;
}

Container_Handle ui_system_add_container(Container_Layout layout, bool is_hidden = false)
{
	Container_Handle parent_handle;
	parent_handle.container_index = ui_system.container_stack[ui_system.container_stack.size - 1].container_index;
	if (ui_system.pop_container_after_next_push) {
		ui_system.container_stack.size -= 1;
		ui_system.pop_container_after_next_push = false;
	}

	Widget_Container& parent = ui_system.containers[parent_handle.container_index];

	// Check if we can match widget to previous frame
	if (!parent.matching_failed_this_frame && parent.next_matching_index < parent.matching_infos.size)
	{
		UI_Matching_Info& next_match = parent.matching_infos[parent.next_matching_index];
		if (!next_match.is_widget)
		{
			Widget_Container& container = ui_system.containers[next_match.element_index];

			// Found match
			parent.next_matching_index += 1;
			next_match.is_hidden = is_hidden;
			container.visited_this_frame = true;
			container.layout = layout;
			container.matching_failed_this_frame = false;
			container.next_matching_index = 0;
			container.parent_container = parent_handle;

			Container_Handle result;
			result.container_index = next_match.element_index;
			return result;
		}
	}
	parent.matching_failed_this_frame = true;

	// Note: Add matching info to parent first, because adding to containers may invalidate parent pointer
	UI_Matching_Info matching_info;
	matching_info.element_index = ui_system.containers.size;
	matching_info.is_hidden = is_hidden;
	matching_info.is_widget = false;
	dynamic_array_push_back(&parent.matching_infos, matching_info);

	Widget_Container container;
	container.parent_container = parent_handle;
	container.elements = dynamic_array_create<Container_Element>();
	container.matching_infos = dynamic_array_create<UI_Matching_Info>();
	container.scroll_bar_info.has_scroll_bar = false;
	container.scroll_bar_info.pixel_scroll_y = 0;
	container.matching_failed_this_frame = false;
	container.next_matching_index = 0;
	container.visited_this_frame = true;
	container.layout = layout;
	dynamic_array_push_back(&ui_system.containers, container);

	Container_Handle result;
	result.container_index = matching_info.element_index;
	return result;
}

Window_Handle ui_system_add_window(Window_Style style)
{
	Window_Handle window_handle;
	if (ui_system.next_window_index < ui_system.windows.size) 
	{
		window_handle.window_index = ui_system.next_window_index;
		ui_system.next_window_index += 1;
		window_handle.created_this_frame = false;
	}
	else 
	{
		window_handle.created_this_frame = true;

		// Add main container
		Widget_Container container;
		container.elements = dynamic_array_create<Container_Element>();
		container.matching_infos = dynamic_array_create<UI_Matching_Info>();
		container.scroll_bar_info.has_scroll_bar = false;
		container.scroll_bar_info.pixel_scroll_y = 0;
		dynamic_array_push_back(&ui_system.containers, container);

		UI_Window window;
		window.root.is_widget = false;
		window.root.element_index = ui_system.containers.size - 1;
		window.z_index = ui_system.max_window_z_index + 1;
		ui_system.max_window_z_index += 1;

		ivec2 screen_size = ivec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);
		ivec2 default_size = ivec2(400, 300);
		ivec2 offset = ivec2(1, -1) * ui_system.new_windows_this_frame_count * 20;
		ui_system.new_windows_this_frame_count += 1;
		window.window_box.min = screen_size / 2 - default_size / 2 + offset;
		window.window_box.max = window.window_box.min + default_size + offset;

		dynamic_array_push_back(&ui_system.windows, window);
		window_handle.window_index = ui_system.windows.size - 1;
		ui_system.next_window_index += 1; // Because we don't want any more matchings
	}

	UI_Window* window = &ui_system.windows[window_handle.window_index];
	window->visited_this_frame = true;
	window->created_this_frame = window_handle.created_this_frame;
	window->style = style;
	window_handle.container.container_index = window->root.element_index;

	auto& container = ui_system.containers[window->root.element_index];
	container.visited_this_frame = true;
	container.matching_failed_this_frame = false;
	container.next_matching_index = 0;
	container.parent_container.container_index = -1;
	container.layout = container_layout_make_default();
	container.layout.options.normal.scroll_bar_enabled = true;
	container.layout.padding = 2;

	return window_handle;
}

void ui_system_set_window_topmost(Window_Handle window_handle)
{
	auto& window = ui_system.windows[window_handle.window_index];
	window.z_index = ui_system.max_window_z_index + 1;
	ui_system.max_window_z_index += 1;
}



BBox ui_window_get_title_area(int window_index)
{
	auto& window = ui_system.windows[window_index];
	BBox box = ui_system.windows[window_index].window_box;
	if (!window.style.has_title_bar) {
		box.min.y = box.max.y;
		return box;
	}

	box.max.y -= BORDER_SPACE;
	box.min.y = box.max.y - ui_system.line_item_height;
	box.min.x += BORDER_SPACE;
	box.max.x -= BORDER_SPACE;
	return box;
}

BBox ui_window_get_client_area(int window_index)
{
	auto& window = ui_system.windows[window_index];
	BBox box = ui_system.windows[window_index].window_box;
	box.max.y -= BORDER_SPACE;
	if (window.style.has_title_bar) {
		box.max.y -= ui_system.line_item_height;
	}
	box.min.y += BORDER_SPACE;
	box.min.x += BORDER_SPACE;
	box.max.x -= BORDER_SPACE;
	return box;
}

Window_Handle window_handle_create_from_index(int index) {
	auto& window = ui_system.windows[index];
	Window_Handle handle;
	handle.window_index = index;
	handle.created_this_frame = false;
	handle.container.container_index = window.root.element_index;
	return handle;
}

void ui_system_draw_text_with_clipping_indicator(
	Mesh* mesh, Glyph_Atlas_* glyph_atlas, ivec2 position, String text, Text_Alignment alignment, BBox clipping_box)
{
	if (text.size == 0) return;

	int available_text_space = clipping_box.max.x - clipping_box.min.x;
	int required_text_space = text.size * ui_system.char_size.x;
	ivec2 text_pos = position;

	// Align text
	switch (alignment) {
	case Text_Alignment::LEFT: break;
	case Text_Alignment::RIGHT: text_pos.x = clipping_box.max.x - required_text_space; break;
	case Text_Alignment::CENTER: {
		// Center button text if enough space is available
		if (available_text_space > required_text_space) {
			text_pos.x = clipping_box.min.x + (available_text_space - required_text_space) / 2;
		}
		break;
	}
	default: panic("");
	}

	// Check if text is clipped
	int first_fully_visible = 0;
	if (text_pos.x < clipping_box.min.x) {
		first_fully_visible = (clipping_box.min.x - text_pos.x) / ui_system.char_size.x + 1;
	}
	int last_fully_visible = text.size - 1;
	if (text_pos.x + required_text_space > clipping_box.max.x) {
		last_fully_visible = math_clamp((clipping_box.max.x - text_pos.x) / ui_system.char_size.x - 1, 0, text.size - 1);
	}

	if ((first_fully_visible == 0 && last_fully_visible == text.size - 1) || last_fully_visible <= first_fully_visible) {
		mesh_push_text_clipped(mesh, glyph_atlas, text, text_pos, clipping_box);
		return;
	}

	// Draw clipping symbols '...'
	int start_draw_char = 0;
	int end_draw_char = text.size;
	if (first_fully_visible != 0)
	{
		text_pos.x += first_fully_visible * ui_system.char_size.x;
		mesh_push_icon_clipped(
			mesh, text_pos, ui_system.atlas_box_text_clipping, glyph_atlas->bitmap_atlas_size, clipping_box, Icon_Rotation::NONE, vec4(1.0f));
		text_pos.x += ui_system.char_size.x;
		start_draw_char = first_fully_visible + 1;
	}
	if (last_fully_visible != text.size - 1) {
		end_draw_char = last_fully_visible;
		ivec2 dot_pos = text_pos + ivec2((last_fully_visible - start_draw_char) * ui_system.char_size.x, 0);
		mesh_push_icon_clipped(
			mesh, dot_pos, ui_system.atlas_box_text_clipping, glyph_atlas->bitmap_atlas_size, clipping_box, Icon_Rotation::NONE, vec4(1.0f));
	}

	String substring = string_create_substring_static(&text, start_draw_char, end_draw_char);
	mesh_push_text_clipped(mesh, glyph_atlas, substring, text_pos, clipping_box);
}

void container_element_gather_width_information_recursive(Container_Element* element, bool collapse_allowed)
{
	const int TEXT_BORDER_SPACE = BORDER_SPACE * 2 + PAD_LEFT_RIGHT * 2;

	if (element->is_widget)
	{
		auto& style = ui_system.widgets[element->element_index].style;
		element->can_combine_in_lines = style.can_combine_in_lines;
		element->min_width_collapsed = style.min_width;
		element->min_width_without_collapse = style.min_width;
		element->min_width_for_line_merge = style.max_width;
		element->width_can_grow_beyond_max = style.can_grow_beyond_max_width;
		element->height_can_grow = false;
		element->min_height = style.height;
		element->wanted_height = style.height;
		element->line_index = 0;
		return;
	}

	// Calculate child width infos
	Widget_Container& container = ui_system.containers[element->element_index];
	int max_child_min_width_collapsed = 0;
	int max_child_min_width_without_collapse = 0;
	int sum_child_min_width_collpsed = 0;
	int max_child_min_width_for_line_merge = 0;
	container.sum_child_min_width_without_collapse = 0;
	container.sum_child_min_width_for_line_merge = 0;
	container.collaps_allowed = collapse_allowed;
	container.elements_with_growable_width_count = 0;
	bool child_height_can_grow = false;
	bool has_child_that_cannot_combine_in_line = false;
	for (int i = 0; i < container.elements.size; i++)
	{
		Container_Element& child = container.elements[i];
		auto& layout = container.layout;
		bool child_collapse_allowed = !(layout.type == Layout_Type::STACK_HORIZONTAL && !layout.options.horizontal_allow_collapse);
		container_element_gather_width_information_recursive(&child, collapse_allowed && child_collapse_allowed);

		child.line_index = i;
		max_child_min_width_collapsed = math_maximum(max_child_min_width_collapsed, child.min_width_collapsed);
		max_child_min_width_without_collapse = math_maximum(max_child_min_width_without_collapse, child.min_width_without_collapse);
		max_child_min_width_for_line_merge = math_maximum(max_child_min_width_for_line_merge, child.min_width_for_line_merge);
		sum_child_min_width_collpsed += child.min_width_collapsed;
		container.sum_child_min_width_without_collapse += child.min_width_without_collapse;
		container.sum_child_min_width_for_line_merge += child.min_width_for_line_merge;
		container.elements_with_growable_width_count += child.width_can_grow_beyond_max ? 1 : 0;
		child_height_can_grow = child_height_can_grow || child.height_can_grow;
		has_child_that_cannot_combine_in_line = has_child_that_cannot_combine_in_line || !child.can_combine_in_lines;
	}

	// Calculate container size-info from child sizes
	element->height_can_grow = child_height_can_grow;
	element->width_can_grow_beyond_max = container.elements_with_growable_width_count > 0;
	switch (container.layout.type)
	{
	case Layout_Type::NORMAL:
	{
		int indent = container.layout.options.normal.indentation;
		element->min_width_collapsed = max_child_min_width_collapsed + indent;
		element->min_width_without_collapse = max_child_min_width_without_collapse + indent;
		element->min_width_for_line_merge = max_child_min_width_for_line_merge + (container.elements.size - 1) * PAD_WIDGETS_ON_LINE + indent;
		element->can_combine_in_lines = false;
		break;
	}
	case Layout_Type::STACK_HORIZONTAL:
	{
		int padding = (container.elements.size - 1) * PAD_LABEL_BOX;
		if (container.elements.size >= 1 && container.layout.options.horizontal_allow_collapse && collapse_allowed)
		{
			auto& first_element = container.elements[0];
			int padding = (container.elements.size - 1) * PAD_LABEL_BOX;

			element->min_width_collapsed = PAD_ADJACENT_LABLE_LINE_SPLIT + max_child_min_width_collapsed;
			element->min_width_without_collapse = container.sum_child_min_width_without_collapse + padding;
			element->min_width_for_line_merge = container.sum_child_min_width_for_line_merge + padding;
			element->can_combine_in_lines = !has_child_that_cannot_combine_in_line;

		}
		else {
			element->min_width_collapsed = sum_child_min_width_collpsed + padding;
			element->min_width_without_collapse = sum_child_min_width_collpsed + padding;
			element->min_width_for_line_merge = container.sum_child_min_width_for_line_merge + padding;
			element->can_combine_in_lines = !has_child_that_cannot_combine_in_line;
		}
		break;
	}
	default: panic("");
	}

	element->min_width_collapsed += container.layout.padding * 2;
	element->min_width_for_line_merge += container.layout.padding * 2;
	element->min_width_without_collapse += container.layout.padding * 2;
}

struct Max_Width_Child
{
	int index;
	int max_width;
};

struct Max_Width_Child_Comparator
{
	bool operator()(const Max_Width_Child& a, const Max_Width_Child& b) {
		return a.max_width < b.max_width;
	}
};

void container_element_do_horizontal_layout_and_find_height(Container_Element* element, int x_pos, int available_width)
{
	if (element->is_widget) return;
	Widget_Container& container = ui_system.containers[element->element_index];

	available_width -= container.layout.padding * 2;
	x_pos += container.layout.padding;

	// Calculate x-bounds for each widget
	container.line_count = 0;
	switch (container.layout.type)
	{
	case Layout_Type::NORMAL:
	{
		available_width -= container.layout.options.normal.indentation;
		x_pos += container.layout.options.normal.indentation;
		int child_index = 0;
		const int BOX_WIDTH = ui_system.char_size.x * 8;
		const int box_count = math_maximum(1, available_width / BOX_WIDTH);
		while (child_index < container.elements.size)
		{
			// Find end of line
			int line_start_index = child_index;
			bool last_can_combine = true;
			int remaining_boxes = box_count;
			while (child_index < container.elements.size)
			{
				auto& child = container.elements[child_index];
				int required_boxes = (child.min_width_for_line_merge + PAD_WIDGETS_ON_LINE) / BOX_WIDTH;
				if (required_boxes * BOX_WIDTH < child.min_width_for_line_merge + PAD_WIDGETS_ON_LINE) {
					required_boxes += 1;
				}
				bool add_widget_to_line = required_boxes <= remaining_boxes && child.can_combine_in_lines && last_can_combine;
				if (!add_widget_to_line) {
					break;
				}
				remaining_boxes -= required_boxes;
				child_index += 1;
			}

			// Special case if no widget has enough space in line
			if (line_start_index == child_index || line_start_index + 1 == child_index) {
				auto& child = container.elements[line_start_index];
				child.box.min.x = x_pos;
				child.box.max.x = x_pos + available_width;
				child.line_index = container.line_count;
				container.line_count += 1;
				if (line_start_index == child_index) {
					child_index += 1;
				}
				continue;
			}

			// Otherwise distribute boxes onto widgets
			int count = child_index - line_start_index;
			int extra_boxes_per_widget = remaining_boxes / count;
			int box_remainder = remaining_boxes % count;
			const int first_box_extra = available_width - box_count * BOX_WIDTH;

			int cursor_x = x_pos;
			for (int i = line_start_index; i < child_index; i++)
			{
				auto& child = container.elements[i];

				int widget_boxes = (child.min_width_for_line_merge + PAD_WIDGETS_ON_LINE) / BOX_WIDTH;
				if (widget_boxes * BOX_WIDTH < child.min_width_for_line_merge + PAD_WIDGETS_ON_LINE) {
					widget_boxes += 1;
				}
				widget_boxes += extra_boxes_per_widget;
				if (i - line_start_index < box_remainder) {
					widget_boxes += 1;
				}

				int width = BOX_WIDTH * widget_boxes;
				if (i != child_index - 1) {
					width -= PAD_WIDGETS_ON_LINE;
				}
				if (i == line_start_index) {
					width += first_box_extra;
				}

				child.box.min.x = cursor_x;
				child.box.max.x = cursor_x + width;
				child.line_index = container.line_count;
				cursor_x += width + PAD_WIDGETS_ON_LINE;
			}

			container.line_count += 1;
		}
		break;
	}
	case Layout_Type::STACK_HORIZONTAL:
	{
		// Distribute space to all widgets on line
		if (container.layout.options.horizontal_allow_collapse && container.collaps_allowed && available_width < element->min_width_without_collapse)
		{
			for (int i = 0; i < container.elements.size; i++) {
				auto& child = container.elements[i];
				child.box.min.x = x_pos + (i != 0 ? PAD_ADJACENT_LABLE_LINE_SPLIT : 0);
				child.box.max.x = x_pos + available_width;
				child.line_index = i;
			}
			container.line_count = container.elements.size;
		}
		else
		{
			// Handle single widget line (Common case) first
			if (container.elements.size == 0) break;
			if (container.elements.size == 1) {
				auto& child = container.elements[0];
				child.box.min.x = x_pos;
				child.box.max.x = x_pos + available_width;
				child.line_index = container.line_count;
				break;
			}

			// If not enough space is available, elements will start to clip
			if (available_width < element->min_width_without_collapse)
			{
				for (int i = 0; i < container.elements.size; i++) {
					auto& child = container.elements[i];
					child.box.min.x = x_pos;
					child.box.max.x = x_pos + child.min_width_collapsed;
					child.line_index = 0;
					x_pos += child.min_width_collapsed + PAD_LABEL_BOX;
				}
				break;
			}

			// Find elements with fixed maximum width
			Dynamic_Array<Max_Width_Child> max_width_children = dynamic_array_create<Max_Width_Child>();
			SCOPE_EXIT(dynamic_array_destroy(&max_width_children));
			for (int i = 0; i < container.elements.size; i++) {
				auto& child = container.elements[i];
				if (!child.width_can_grow_beyond_max) {
					Max_Width_Child max_width;
					max_width.index = i;
					max_width.max_width = child.min_width_for_line_merge;
					dynamic_array_push_back(&max_width_children, max_width);
				}
			}
			// Sort max-width elements by size
			dynamic_array_sort(&max_width_children, Max_Width_Child_Comparator());

			int growing_element_count = container.elements.size;
			int overflow_budget = available_width - element->min_width_without_collapse;
			int extra_per_widget = overflow_budget / math_maximum(1, growing_element_count);
			for (int i = 0; i < max_width_children.size; i++)
			{
				Max_Width_Child& max_width_child = max_width_children[i];
				auto child = container.elements[max_width_child.index];

				int child_space = child.min_width_without_collapse + extra_per_widget;
				// Once we cannot fill child, we have found our extra_per_widget value
				if (child_space < max_width_child.max_width) { break; }

				// Otherwise we need to adjust extra_per_widget_value
				overflow_budget = overflow_budget + child.min_width_collapsed - child.min_width_for_line_merge;
				growing_element_count -= 1;
				if (growing_element_count == 0) {
					extra_per_widget = available_width - element->min_width_without_collapse;
					break;
				}
				int new_extra_per_widget = overflow_budget / math_maximum(1, growing_element_count);
				assert(new_extra_per_widget >= extra_per_widget, "This should always grow when we hit the maximum for a child");
				extra_per_widget = new_extra_per_widget;
			}

			int remaining_pixels = overflow_budget % math_maximum(1, growing_element_count);
			if (growing_element_count == 0) {
				remaining_pixels = 0;
			}

			int cursor_x = x_pos;
			for (int i = 0; i < container.elements.size; i++)
			{
				auto& child = container.elements[i];
				int child_space = child.min_width_without_collapse + extra_per_widget;
				if (i == container.elements.size - 1) { child_space += remaining_pixels; }
				if (child_space > child.min_width_for_line_merge && !child.width_can_grow_beyond_max) {
					child_space = child.min_width_for_line_merge;
				}

				child.box.min.x = cursor_x;
				child.box.max.x = cursor_x + child_space;
				child.line_index = 0;
				cursor_x += child_space + PAD_LABEL_BOX;
			}
			container.line_count = 1;
		}
		break;
	}
	default: panic("");
	}
	container.line_count = math_maximum(container.line_count, 1);

	// Calculate Height per line
	container.sum_line_min_heights = 0;
	container.sum_line_wanted_heights = 0;
	container.growable_line_count = 0;

	int max_last_line_min_height = 0;
	int max_last_line_wanted_height = 0;
	bool last_line_can_grow = false;
	int last_line_index = 0;
	for (int i = 0; i < container.elements.size; i++)
	{
		auto& child = container.elements[i];
		if (!child.is_widget) {
			container_element_do_horizontal_layout_and_find_height(&child, child.box.min.x, child.box.max.x - child.box.min.x);
		}

		if (child.line_index != last_line_index)
		{
			container.sum_line_min_heights += max_last_line_min_height;
			container.sum_line_wanted_heights += max_last_line_wanted_height;
			container.growable_line_count += last_line_can_grow ? 1 : 0;

			last_line_index = child.line_index;
			max_last_line_min_height = 0;
			max_last_line_wanted_height = 0;
			last_line_can_grow = false;
		}

		max_last_line_min_height = math_maximum(max_last_line_min_height, child.min_height);
		max_last_line_wanted_height = math_maximum(max_last_line_wanted_height, child.wanted_height);
		last_line_can_grow = last_line_can_grow || child.height_can_grow;
	}
	container.sum_line_min_heights += max_last_line_min_height;
	container.sum_line_wanted_heights += max_last_line_wanted_height;
	container.growable_line_count += last_line_can_grow ? 1 : 0;

	// Set Container height infos
	element->min_height = container.sum_line_min_heights + (container.line_count - 1) * PAD_WIDGETS_BETWEEN_LINES + container.layout.padding * 2;
	element->wanted_height = container.sum_line_wanted_heights + (container.line_count - 1) * PAD_WIDGETS_BETWEEN_LINES + container.layout.padding * 2;
	element->height_can_grow = container.growable_line_count > 0;
	switch (container.layout.type)
	{
	case Layout_Type::NORMAL:
	{
		auto& settings = container.layout.options.normal;
		// Apply min and max height if set
		element->min_height = math_maximum(element->min_height, settings.min_height_empty);
		if (settings.scroll_bar_enabled && settings.min_height_restrained >= 0) {
			// This line is confusing, because min_height_with_widgets is basically a max height if we are constrained!
			element->min_height = math_minimum(element->min_height, settings.min_height_restrained);
		}
		break;
	}
	case Layout_Type::STACK_HORIZONTAL: {
		break;
	}
	default: panic("");
	}
}

struct Int_Comparator {
	bool operator()(int a, int b) {
		return a < b;
	}
};

void container_element_do_vertical_layout(Container_Element* element, int y_pos, int available_height)
{
	if (element->is_widget) return;
	Widget_Container& container = ui_system.containers[element->element_index];
	if (container.elements.size == 0) return;

	y_pos -= container.layout.padding;
	available_height -= container.layout.padding * 2;

	// Check if we want to add scroll-bar
	int child_required_height = container.sum_line_min_heights + (container.line_count - 1) * PAD_WIDGETS_BETWEEN_LINES;
	bool overflow_detected = available_height < child_required_height;
	Scroll_Bar_Info& scroll_info = container.scroll_bar_info;
	if (overflow_detected && container.layout.type == Layout_Type::NORMAL && container.layout.options.normal.scroll_bar_enabled)
	{
		auto& box = element->box;
		int available_width = element->box.max.x - element->box.min.x - SCROLL_BAR_WIDTH;

		// Re-Calculate Child layout after width change
		container_element_do_horizontal_layout_and_find_height(element, element->box.min.x, available_width);
		if (!scroll_info.has_scroll_bar) {
			scroll_info.pixel_scroll_y = 0; // Only reset if we didn't have scroll-bar before
		}
		scroll_info.has_scroll_bar = true;

		// Calculate scroll-bar metrics
		const int max_height = box.max.y - box.min.y;
		const int used_height =
			container.sum_line_min_heights +
			math_maximum(0, container.line_count - 1) * PAD_WIDGETS_BETWEEN_LINES +
			container.layout.padding * 2;
		const int available_bar_space = max_height - 2 * SCROLL_BAR_PADDING;
		scroll_info.bar_height = math_maximum(MIN_SCROLL_BAR_HEIGHT, available_bar_space * max_height / math_maximum(1, used_height));
		scroll_info.max_bar_offset = math_maximum(available_bar_space - scroll_info.bar_height, 1);
		scroll_info.max_pixel_scroll_offset = math_maximum(used_height - max_height, 1);
		scroll_info.pixel_scroll_y = math_clamp(scroll_info.pixel_scroll_y, 0, scroll_info.max_pixel_scroll_offset);
		scroll_info.bar_offset = scroll_info.max_bar_offset * scroll_info.pixel_scroll_y / scroll_info.max_pixel_scroll_offset;
	}
	else {
		scroll_info.has_scroll_bar = false;
		scroll_info.pixel_scroll_y = 0;
	}

	// Do Y-Layout (Basically all layouts do the same thing, using line-index to calculate height)
	available_height = available_height - PAD_WIDGETS_BETWEEN_LINES * (container.line_count - 1);

	// Special algorithm to distribute heights
	int extra_height_per_growable = 0;
	int remaining_pixel = 0;
	if (available_height <= container.sum_line_min_heights) {
		extra_height_per_growable = 0;
		remaining_pixel = 0;
	}
	else if (available_height < container.sum_line_wanted_heights)
	{
		// Find line infos
		Dynamic_Array<int> max_extra_heights = dynamic_array_create<int>(container.line_count);
		SCOPE_EXIT(dynamic_array_destroy(&max_extra_heights));
		int i = 0;
		while (i < container.elements.size)
		{
			auto& start_element = container.elements[i];
			int max_growable = start_element.wanted_height - start_element.min_height;
			i += 1;
			while (i < container.elements.size) {
				auto& element = container.elements[i];
				if (element.line_index != start_element.line_index) {
					break;
				}
				max_growable = math_maximum(max_growable, element.wanted_height - element.min_height);
				i += 1;
			}
			dynamic_array_push_back(&max_extra_heights, max_growable);
		}

		// Sort line infos by wanted height
		dynamic_array_sort(&max_extra_heights, Int_Comparator());

		int height_buffer = available_height - container.sum_line_min_heights;
		int growable_count = max_extra_heights.size;
		int extra_height = height_buffer / math_maximum(1, growable_count);
		for (int i = 0; i < max_extra_heights.size; i++)
		{
			int max_extra_height = max_extra_heights[i];
			if (extra_height >= max_extra_height) {
				height_buffer = height_buffer - max_extra_height;
				growable_count -= 1;
				int new_extra_height = height_buffer / math_maximum(1, growable_count);
				assert(new_extra_height >= extra_height, "");
				extra_height = new_extra_height;
			}
			else {
				break;
			}
		}

		extra_height_per_growable = extra_height;
		remaining_pixel = height_buffer % math_maximum(1, growable_count);
	}
	else {
		int height_buffer = math_maximum(available_height - container.sum_line_wanted_heights, 0);
		extra_height_per_growable = height_buffer / math_maximum(1, container.growable_line_count);
		remaining_pixel = height_buffer % math_maximum(1, container.growable_line_count);
	}

	int last_line_index = 0;
	int last_line_height = 0;
	bool line_took_pixel = false;
	for (int i = 0; i < container.elements.size; i++)
	{
		auto& child = container.elements[i];

		// Check if we moved to new line
		if (child.line_index != last_line_index)
		{
			last_line_index = child.line_index;
			y_pos -= last_line_height + PAD_WIDGETS_BETWEEN_LINES;
			last_line_height = 0;
			if (line_took_pixel) {
				line_took_pixel = false;
				remaining_pixel -= 1;
			}
		}

		// Figure out widget height
		int widget_height = child.min_height;
		if (available_height <= container.sum_line_min_heights) {
			// Nothing more to do here
		}
		else if (available_height < container.sum_line_wanted_heights) {
			widget_height += extra_height_per_growable;
			if (widget_height > child.wanted_height) {
				widget_height = child.wanted_height;
			}
			else if (remaining_pixel > 0) {
				widget_height += 1;
				line_took_pixel = true;
			}
		}
		else {
			widget_height = child.wanted_height;
			if (child.height_can_grow) {
				widget_height += extra_height_per_growable;
				if (remaining_pixel > 0) {
					widget_height += 1;
					line_took_pixel = true;
				}
			}
		}

		// Set widget position
		last_line_height = math_maximum(last_line_height, widget_height);
		child.box.max.y = y_pos;
		child.box.min.y = y_pos - widget_height;

		// Recurse to children
		if (!child.is_widget) {
			container_element_do_vertical_layout(&child, child.box.max.y, child.box.max.y - child.box.min.y);
		}
	}
}

void container_element_render(
	Container_Element* element, BBox clipping_box, int y_offset, Mesh* mesh, Glyph_Atlas_* glyph_atlas
)
{
	ivec2 char_size = glyph_atlas->char_box_size;
	const int LINE_ITEM_HEIGHT = PAD_TOP + PAD_BOT + 2 * BORDER_SPACE + char_size.y;
	const int TEXT_BORDER_SPACE = BORDER_SPACE * 2 + PAD_LEFT_RIGHT * 2;

	auto box_draw_label = [&](BBox box, String text, BBox clipping_box) {
		ui_system_draw_text_with_clipping_indicator(
			mesh, glyph_atlas, box.min + ivec2(0, BORDER_SPACE + PAD_BOT), text, Text_Alignment::LEFT, clipping_box
		);
		};
	auto box_draw_text_in_box = [&](BBox box, String text, Text_Alignment alignment, BBox clipping_area, vec4 BG_COLOR, vec4 BORDER_COLOR)
		{
			// Draw Background
			mesh_push_box_with_border_clipped(mesh, box, clipping_area, BG_COLOR, BORDER_SPACE, BORDER_COLOR);

			// Draw text
			BBox text_clip_area = box;
			text_clip_area.min = text_clip_area.min + ivec2(PAD_LEFT_RIGHT + BORDER_SPACE, BORDER_SPACE + PAD_BOT);
			text_clip_area.max = text_clip_area.max - ivec2(PAD_LEFT_RIGHT + BORDER_SPACE, BORDER_SPACE + PAD_TOP);
			ivec2 text_pos = text_clip_area.min;
			text_clip_area = bbox_intersection(text_clip_area, clipping_area);

			ui_system_draw_text_with_clipping_indicator(mesh, glyph_atlas, text_pos, text, alignment, text_clip_area);
		};

	if (element->is_widget)
	{
		Widget_Style& widget_style = ui_system.widgets[element->element_index].style;
		BBox box = element->box;
		box.min.y += y_offset;
		box.max.y += y_offset;
		if (!widget_style.can_grow_beyond_max_width) {
			box.max.x = box.min.x + widget_style.max_width;
		}

		// Draw background
		ivec2 text_pos = box.min;
		text_pos.y += PAD_BOT + BORDER_SPACE;
		text_pos.x += widget_style.has_border ? (PAD_LEFT_RIGHT + BORDER_SPACE) : 0;
		vec4 bg_color = widget_style.background_color;
		if (widget_style.draw_background)
		{
			if (widget_style.can_obtain_text_input && element->element_index == ui_system.focused_widget_index) {
				bg_color = widget_style.focused_color;
			}
			else if (widget_style.is_clickable && element->element_index == ui_system.mouse_hover_widget_index) {
				bg_color = widget_style.hover_color;
			}
			vec4 border_color = widget_style.border_color;
			if (widget_style.can_obtain_text_input && element->element_index == ui_system.focused_widget_index) {
				border_color = COLOR_INPUT_BORDER_FOCUSED;
			}
			mesh_push_box_with_border_clipped(mesh, box, clipping_box, bg_color, (widget_style.has_border ? BORDER_SPACE : 0), border_color);
		}

		// Draw icon
		if (widget_style.draw_icon) 
		{
			const int icon_size = ui_system.icon_size;
			ivec2 icon_pos = box.min;
			int padding = ICON_PADDING + widget_style.has_border ? BORDER_SPACE : 0;
			// Icon-Y always centers it inside box
			icon_pos.y = box.min.y + (box.max.y - box.min.y - icon_size) / 2;
			BBox box_after = box;
			if (widget_style.text_display.length > 0) 
			{
				if (widget_style.icon_left_aligned) {
					icon_pos.x = box.min.x + padding;
					text_pos.x = box.min.x + icon_size + padding * 2;
				}
				else {
					icon_pos.x = box.max.x - icon_size - padding;
					box_after.max.x -= icon_size + padding * 2;
				}
			}
			else {
				icon_pos.x = box.min.x + (box.max.x - box.min.x - icon_size) / 2 + padding;
			}
			mesh_push_icon_clipped(
				mesh, icon_pos, ui_system.icon_boxes[(int)widget_style.icon.type], ui_system.atlas_bitmap.size,
				bbox_intersection(clipping_box, box), widget_style.icon.rotation, vec4(widget_style.icon.color, 1.0f)
			);
			box = box_after;
		}

		// Draw text
		if (element->element_index == ui_system.focused_widget_index)
		{
			// Draw edit text inside box!
			BBox text_area = box;
			text_area.min = text_pos;
			text_area.max = box.max - ivec2(PAD_LEFT_RIGHT, PAD_TOP) - BORDER_SPACE;
			BBox text_clip_box = bbox_intersection(text_area, clipping_box);

			// Calculate Text-X offset inside box
			auto& line_editor = ui_system.line_editor;
			auto& x_offset = ui_system.input_x_offset;

			// Adjust x-offset
			{
				int available_text_space = text_area.max.x - text_area.min.x;
				int required_text_size = ui_system.char_size.x * ui_system.input_string.size;
				int cursor_pos = ui_system.char_size.x * line_editor.pos;
				if (available_text_space < required_text_size)
				{
					int start = -x_offset;
					int end = -x_offset + available_text_space;
					if (cursor_pos < start) {
						x_offset = -cursor_pos;
					}
					else if (cursor_pos >= end) {
						x_offset -= cursor_pos - end + 1;
					}
				}
				else {
					x_offset = 0;
				}
			}

			// Draw selection
			if (line_editor.pos != line_editor.select_start)
			{
				int start = math_minimum(line_editor.pos, line_editor.select_start);
				int end = math_maximum(line_editor.pos, line_editor.select_start);
				BBox selection = BBox(
					text_area.min + ivec2(start * ui_system.char_size.x + x_offset, 0),
					text_area.min + ivec2(end * ui_system.char_size.x + x_offset, ui_system.char_size.y)
				);
				vec4 selection_color = bg_color;
				selection_color.x *= 0.7f;
				selection_color.y *= 0.7f;
				selection_color.z *= 0.7f;
				mesh_push_box_clipped(mesh, selection, text_clip_box, selection_color);
			}

			// Draw Text
			mesh_push_text_clipped(mesh, glyph_atlas, ui_system.input_string, text_area.min + ivec2(x_offset, 0), text_clip_box);

			// Draw cursor 
			ivec2 cursor_pos = text_area.min + ivec2(line_editor.pos * ui_system.char_size.x + x_offset, 0);
			BBox cursor = BBox(cursor_pos, cursor_pos + ivec2(1, ui_system.char_size.y));
			mesh_push_box_clipped(mesh, cursor, text_clip_box, vec4(0, 0, 0, 1));
		}
		else if (widget_style.text_display.length > 0)
		{
			if (widget_style.draw_background) {
				box.max.x -= BORDER_SPACE + PAD_LEFT_RIGHT;
			}
			ui_system_draw_text_with_clipping_indicator(
				mesh, glyph_atlas, text_pos, ui_string_to_string(widget_style.text_display),
				widget_style.text_alignment, bbox_intersection(box, clipping_box)
			);
		}
	}
	else
	{
		auto& container = ui_system.containers[element->element_index];
		BBox box = element->box;
		box.min.y += y_offset;
		box.max.y += y_offset;

		// Draw scroll bar
		Scroll_Bar_Info& scroll_info = container.scroll_bar_info;
		if (scroll_info.has_scroll_bar)
		{
			BBox scroll_area = box;
			scroll_area.min.x = scroll_area.max.x - SCROLL_BAR_WIDTH;
			BBox scroll_box = scroll_area;
			scroll_box.max.x -= SCROLL_BAR_PADDING;
			scroll_box.min.x += SCROLL_BAR_PADDING;
			scroll_box.max.y = scroll_area.max.y - scroll_info.bar_offset - SCROLL_BAR_PADDING;
			scroll_box.min.y = scroll_box.max.y - scroll_info.bar_height;

			// Draw
			mesh_push_box_clipped(mesh, scroll_area, clipping_box, COLOR_SCROLL_BG);
			mesh_push_box_clipped(mesh, scroll_box, clipping_box, COLOR_SCROLL_BAR);

			box.max.x -= SCROLL_BAR_WIDTH;
			clipping_box = bbox_intersection(clipping_box, box);
		}

		// Draw background
		if (container.layout.draw_background) {
			BBox box = element->box;
			box.min.x += container.layout.type == Layout_Type::NORMAL ? container.layout.options.normal.indentation : 0;
			mesh_push_box_clipped(mesh, box, clipping_box, container.layout.background_color);
		}

		// Render elements
		box.max = box.max - ivec2(container.layout.padding);
		box.min = box.min + ivec2(container.layout.padding);
		clipping_box = bbox_intersection(clipping_box, box); // Clip at padding border!
		for (int i = 0; i < container.elements.size; i++) {
			container_element_render(&container.elements[i], clipping_box, y_offset + container.scroll_bar_info.pixel_scroll_y, mesh, glyph_atlas);
		}
	}
}

void ui_element_find_mouse_hover_infos_recursive(Container_Element* element, ivec2 mouse_pos, BBox clipping_box, int y_offset)
{
	BBox box = element->box;
	box.max.y += y_offset;
	box.min.y += y_offset;
	clipping_box = bbox_intersection(box, clipping_box);
	bool has_mouse_hover = bbox_contains_point(clipping_box, mouse_pos);
	if (!has_mouse_hover) { return; }

	if (element->is_widget) {
		ui_system.mouse_hover_widget_index = element->element_index;
		return;
	}

	Widget_Container* container = &ui_system.containers[element->element_index];
	ui_system.mouse_hover_container_index = element->element_index;

	// Check if we hover over scroll bar
	Scroll_Bar_Info scroll_info = container->scroll_bar_info;
	if (scroll_info.has_scroll_bar)
	{
		BBox scroll_area = box;
		scroll_area.min.x = scroll_area.max.x - SCROLL_BAR_WIDTH;
		BBox scroll_box = scroll_area;
		scroll_box.max.x -= SCROLL_BAR_PADDING;
		scroll_box.min.x += SCROLL_BAR_PADDING;
		scroll_box.max.y = scroll_area.max.y - scroll_info.bar_offset - SCROLL_BAR_PADDING;
		scroll_box.min.y = scroll_box.max.y - scroll_info.bar_height;

		if (bbox_contains_point(bbox_intersection(clipping_box, scroll_box), mouse_pos)) {
			ui_system.mouse_hover_drag_status = Drag_Status::SCROLL_BAR;
			return;
		}
	}

	// Check if we hover over children
	y_offset += container->scroll_bar_info.pixel_scroll_y;
	for (int i = 0; i < container->elements.size; i++) {
		ui_element_find_mouse_hover_infos_recursive(&container->elements[i], mouse_pos, clipping_box, y_offset);
	}
}

void ui_system_find_mouse_hover_infos(ivec2 mouse_pos)
{
	ui_system.mouse_hover_window_index = -1;
	ui_system.mouse_hover_closest_window_index = -1;
	ui_system.mouse_hover_container_index = -1;
	ui_system.mouse_hover_widget_index = -1;
	ui_system.mouse_hover_drag_status = Drag_Status::NONE;
	ui_system.mouse_hover_resize_direction = ivec2(0);

	// Find which window we are hovering over
	float min_resize_distance = 1000000.0f;
	for (int i = ui_system.window_z_sorting.size - 1; i >= 0; i--)
	{
		int window_index = ui_system.window_z_sorting[i];
		UI_Window& window = ui_system.windows[window_index];
		if (window.style.is_hidden) continue;

		// Check for resize (Hovers over border)
		auto box = window.window_box;
		float distance_left = distance_point_to_line_segment(vec2(mouse_pos.x, mouse_pos.y), vec2(box.min.x, box.min.y), vec2(box.min.x, box.max.y));
		float distance_right = distance_point_to_line_segment(vec2(mouse_pos.x, mouse_pos.y), vec2(box.max.x, box.min.y), vec2(box.max.x, box.max.y));
		float distance_top = distance_point_to_line_segment(vec2(mouse_pos.x, mouse_pos.y), vec2(box.min.x, box.max.y), vec2(box.max.x, box.max.y));
		float distance_bot = distance_point_to_line_segment(vec2(mouse_pos.x, mouse_pos.y), vec2(box.min.x, box.min.y), vec2(box.max.x, box.min.y));

		ivec2 resize_direction = ivec2(0);
		if (distance_left <= distance_right && distance_left <= WINDOW_RESIZE_RADIUS) {
			resize_direction.x = -1;
		}
		else if (distance_right < distance_left && distance_right <= WINDOW_RESIZE_RADIUS) {
			resize_direction.x = 1;
		}
		if (distance_top <= distance_bot && distance_top <= WINDOW_RESIZE_RADIUS) {
			resize_direction.y = 1;
		}
		else if (distance_bot < distance_top && distance_bot <= WINDOW_RESIZE_RADIUS) {
			resize_direction.y = -1;
		}

		// Filter valid resize options
		switch (window.style.layout)
		{
		case Window_Layout::FLOAT: break;
		case Window_Layout::DROPDOWN: resize_direction = ivec2(0); break;
		case Window_Layout::ANCHOR_RIGHT: {
			resize_direction.y = 0;
			if (resize_direction.x != -1) {
				resize_direction.x = 0;
			}
			break;
		}
		default: panic("");
		}

		float min_dist = math_minimum(math_minimum(distance_left, distance_right), math_minimum(distance_top, distance_bot));
		if (min_dist < min_resize_distance && (resize_direction.x != 0 || resize_direction.y != 0)) {
			ui_system.mouse_hover_closest_window_index = window_index;
			ui_system.mouse_hover_resize_direction = resize_direction;
			min_resize_distance = min_dist;
		}

		// Terminate if mouse is completely inside window
		if (bbox_contains_point(window.window_box, mouse_pos))
		{
			min_resize_distance = min_dist;
			ui_system.mouse_hover_window_index = window_index;
			ui_system.mouse_hover_closest_window_index = window_index; // Note: closest_window_index also respects z-order


			// Check for window move
			if (window.style.has_title_bar && window.style.layout == Window_Layout::FLOAT)
			{
				BBox header_box = ui_window_get_title_area(window_index);
				// Window hove has priority over Window-Resize at a specific distance
				if (bbox_contains_point(header_box, mouse_pos) && min_dist > WINDOW_RESIZE_RADIUS_INSIDE_HEADER) {
					ui_system.mouse_hover_drag_status = Drag_Status::WINDOW_MOVE;
				}
			}
			break;
		}
	}

	if (min_resize_distance <= WINDOW_RESIZE_RADIUS &&
		(ui_system.mouse_hover_resize_direction.x != 0 || ui_system.mouse_hover_resize_direction.y != 0))
	{
		ui_system.mouse_hover_drag_status = Drag_Status::WINDOW_RESIZE;
	}
	else {
		ui_system.mouse_hover_resize_direction = ivec2(0);
	}

	if (ui_system.mouse_hover_window_index == -1) return;

	// Search window children for further hover info
	auto& hover_window = ui_system.windows[ui_system.mouse_hover_window_index];
	ui_element_find_mouse_hover_infos_recursive(
		&hover_window.root, mouse_pos, ui_window_get_client_area(ui_system.mouse_hover_window_index), 0
	);
}

struct Window_Z_Comparator
{
	bool operator()(int a, int b) {
		return ui_system.windows[a].z_index <= ui_system.windows[b].z_index;
	}
};

UI_Input_Info ui_system_start_frame(Input* input)
{
	auto& info = rendering_core.render_information;
	ivec2 screen_size = ivec2(info.backbuffer_width, info.backbuffer_height);
	ivec2 mouse = ivec2(input->mouse_x, screen_size.y - input->mouse_y);
	bool mouse_down = input->mouse_down[(int)Mouse_Key_Code::LEFT];
	bool mouse_pressed = input->mouse_pressed[(int)Mouse_Key_Code::LEFT];

	dynamic_array_reset(&ui_system.container_stack);
	string_reset(&ui_system.string_buffer);

	ui_system.text_changed_widget_index = -1;
	ui_system.mouse_was_clicked = mouse_pressed;
	ui_system.next_window_index = 0;
	ui_system.new_windows_this_frame_count = 0;

	if (!mouse_down) {
		ui_system.drag_status = Drag_Status::NONE;
	}

	// Find mouse hover 
	ui_system_find_mouse_hover_infos(mouse);

	// Move window abover all others if mouse is pressed on it
	if (ui_system.mouse_hover_window_index != -1 && mouse_pressed) {
		auto& hover_window = ui_system.windows[ui_system.mouse_hover_window_index];
		hover_window.z_index = ui_system.max_window_z_index + 1;
		ui_system.max_window_z_index += 1;
		dynamic_array_sort(&ui_system.window_z_sorting, Window_Z_Comparator());
		ui_system_find_mouse_hover_infos(mouse);
	}

	// Handle mouse-wheel input
	if (ui_system.mouse_hover_container_index != -1 && ui_system.drag_status == Drag_Status::NONE)
	{
		int container_index = ui_system.mouse_hover_container_index;
		int pixel_scroll_value = input->mouse_wheel_delta * MOUSE_WHEEL_SENSITIVITY;
		bool scroll_down = pixel_scroll_value < 0;
		pixel_scroll_value = math_absolute(pixel_scroll_value);

		while (pixel_scroll_value > 0 && container_index != -1)
		{
			auto& container = ui_system.containers[container_index];
			auto& scroll_info = container.scroll_bar_info;

			if (scroll_info.has_scroll_bar)
			{
				if (scroll_down)
				{
					int movable = scroll_info.max_pixel_scroll_offset - scroll_info.pixel_scroll_y;
					scroll_info.pixel_scroll_y += math_minimum(pixel_scroll_value, movable);
					pixel_scroll_value = math_maximum(0, pixel_scroll_value - movable);
				}
				else
				{
					int movable = scroll_info.pixel_scroll_y;
					scroll_info.pixel_scroll_y -= math_minimum(pixel_scroll_value, movable);
					pixel_scroll_value = math_maximum(0, pixel_scroll_value - movable);
				}
			}
			scroll_info.pixel_scroll_y = math_clamp(scroll_info.pixel_scroll_y, 0, scroll_info.max_pixel_scroll_offset);

			container_index = container.parent_container.container_index;
		}
	}

	// Check for Drag-Initiation
	if (ui_system.drag_status == Drag_Status::NONE && ui_system.mouse_hover_drag_status != Drag_Status::NONE && mouse_pressed)
	{
		ui_system.drag_start_mouse_pos = mouse;
		ui_system.drag_status = ui_system.mouse_hover_drag_status;
		ui_system.drag_start_window_box = ui_system.windows[ui_system.mouse_hover_closest_window_index].window_box;
		switch (ui_system.drag_status)
		{
		case Drag_Status::WINDOW_MOVE: ui_system.drag_index = ui_system.mouse_hover_window_index; break;
		case Drag_Status::WINDOW_RESIZE: {
			ui_system.drag_index = ui_system.mouse_hover_closest_window_index;
			ui_system.resize_direction = ui_system.mouse_hover_resize_direction;
			break;
		}
		case Drag_Status::SCROLL_BAR: {
			ui_system.drag_index = ui_system.mouse_hover_container_index;
			ui_system.drag_start_bar_offset = ui_system.containers[ui_system.mouse_hover_container_index].scroll_bar_info.bar_offset;
			break;
		}
		default: panic("");
		}
	}

	// Handle active Drags
	switch (ui_system.drag_status)
	{
	case Drag_Status::SCROLL_BAR:
	{
		// Move scroll bar
		Scroll_Bar_Info& scroll_info = ui_system.containers[ui_system.drag_index].scroll_bar_info;
		scroll_info.bar_offset = ui_system.drag_start_bar_offset - (mouse.y - ui_system.drag_start_mouse_pos.y); // Minus because bar-offset is given in negative y

		// Calculate pixel scroll from scroll bar position
		scroll_info.pixel_scroll_y = scroll_info.bar_offset * scroll_info.max_pixel_scroll_offset / scroll_info.max_bar_offset;
		scroll_info.pixel_scroll_y = math_clamp(scroll_info.pixel_scroll_y, 0, scroll_info.max_pixel_scroll_offset);
		break;
	}
	case Drag_Status::WINDOW_MOVE:
	{
		ivec2 offset = mouse - ui_system.drag_start_mouse_pos;
		BBox& box = ui_system.windows[ui_system.drag_index].window_box;
		box = ui_system.drag_start_window_box;
		box.min = box.min + offset;
		box.max = box.max + offset;
		break;
	}
	case Drag_Status::WINDOW_RESIZE:
	{
		BBox& box = ui_system.windows[ui_system.drag_index].window_box;
		box = ui_system.drag_start_window_box;
		int width = box.max.x - box.min.x;
		int height = box.max.y - box.min.y;

		ivec2 offset = mouse - ui_system.drag_start_mouse_pos;
		ivec2 dir = ui_system.resize_direction;
		if (dir.x == 1) {
			box.max.x = box.min.x + math_maximum(width + offset.x, MIN_WINDOW_WIDTH);
		}
		else if (dir.x == -1) {
			box.min.x = box.max.x - math_maximum(width - offset.x, MIN_WINDOW_WIDTH);
		}
		if (dir.y == 1) {
			box.max.y = box.min.y + math_maximum(height + offset.y, MIN_WINDOW_HEIGHT);
		}
		else if (dir.y == -1) {
			box.min.y = box.max.y - math_maximum(height - offset.y, MIN_WINDOW_HEIGHT);
		}
		break;
	}
	case Drag_Status::NONE: break;
	default: panic("");
	}

	UI_Input_Info input_info;
	input_info.has_mouse_hover = 
		ui_system.mouse_hover_window_index != -1 || 
		ui_system.drag_status != Drag_Status::NONE || 
		ui_system.mouse_hover_drag_status != Drag_Status::NONE;
	input_info.has_keyboard_input = ui_system.focused_widget_index != -1;

	// Debug window shortcut system (X, Y, C, V for sizing)
	if (ui_system.mouse_hover_window_index != -1)
	{
		auto& window = ui_system.windows[ui_system.mouse_hover_window_index];
		BBox header_box = ui_window_get_title_area(ui_system.mouse_hover_window_index);
		if (bbox_contains_point(header_box, mouse))
		{
			int width = window.window_box.max.x - window.window_box.min.x;
			int height = window.window_box.max.y - window.window_box.min.y;
			if (input->key_pressed[(int)Key_Code::X]) {
				width = window.root.min_width_without_collapse + 2 * BORDER_SPACE;
			}
			else if (input->key_pressed[(int)Key_Code::C]) {
				width = window.root.min_width_collapsed + 2 * BORDER_SPACE;
			}
			else if (input->key_pressed[(int)Key_Code::V]) {
				width = window.root.min_width_for_line_merge + 2 * BORDER_SPACE;
			}
			if (input->key_pressed[(int)Key_Code::Y]) {
				height = window.root.min_height + ui_system.line_item_height + 2 * BORDER_SPACE;
			}
			if (input->key_pressed[(int)Key_Code::B]) {
				height = window.root.wanted_height + ui_system.line_item_height + 2 * BORDER_SPACE;
			}

			window.window_box.max.x = window.window_box.min.x + width;
			window.window_box.min.y = window.window_box.max.y - height;
		}
	}

	// Handle Mouse-Clicks on Widgets (Focusable Widgets, e.g. Text-Input)
	if (ui_system.mouse_hover_widget_index != -1 && ui_system.drag_status == Drag_Status::NONE && mouse_pressed)
	{
		auto& widget = ui_system.widgets[ui_system.mouse_hover_widget_index];
		if (widget.style.can_obtain_text_input && ui_system.focused_widget_index != ui_system.mouse_hover_widget_index)
		{
			ui_system.focused_widget_index = ui_system.mouse_hover_widget_index;
			string_reset(&ui_system.input_string);
			String text = ui_string_to_string(widget.style.text_display);
			string_append_string(&ui_system.input_string, &text);
			ui_system.line_editor = line_editor_make();
			ui_system.line_editor.select_start = 0;
			ui_system.line_editor.pos = ui_system.input_string.size;
			ui_system.input_x_offset = 0;
		}
	}
	// Reset 
	if (mouse_pressed && ui_system.mouse_hover_widget_index != ui_system.focused_widget_index) {
		ui_system.focused_widget_index = -1;
	}

	// Handle keyboard-messages
	if (ui_system.focused_widget_index != -1)
	{
		auto& widget = ui_system.widgets[ui_system.focused_widget_index];
		Text_Input_Type input_type = widget.style.text_input_type;
		for (int i = 0; i < input->key_messages.size; i++)
		{
			auto msg = input->key_messages[i];
			if (msg.key_down && msg.key_code == Key_Code::RETURN) {
				ui_system.text_changed_widget_index = ui_system.focused_widget_index;
				ui_system.changed_text = ui_system_add_string(ui_system.input_string);
				ui_system.focused_widget_index = -1;
				break;
			}

			// Filter messages for number inputs
			if (input_type != Text_Input_Type::TEXT)
			{
				bool filtered = false;
				if (msg.character >= 31 && msg.character < 128)
				{
					filtered = true;
					char c = msg.character;
					if (c >= '0' && c <= '9') {
						filtered = false;
					}
					else if (c == '.' && input_type == Text_Input_Type::FLOAT) {
						filtered = false;
					}
				}

				if (filtered) {
					continue;
				}
			}

			line_editor_feed_key_message(ui_system.line_editor, &ui_system.input_string, input->key_messages[i]);
		}
	}

	input_info.has_keyboard_input = input_info.has_keyboard_input || ui_system.focused_widget_index != -1;
	return input_info;
}

void ui_system_end_frame_and_render(Window* whole_window, Input* input, Render_Pass* render_pass_alpha_blended)
{
	bool mouse_down = input->mouse_down[(int)Mouse_Key_Code::LEFT];
	auto& info = rendering_core.render_information;
	ivec2 screen_size = ivec2(info.backbuffer_width, info.backbuffer_height);
	ivec2 mouse = ivec2(input->mouse_x, screen_size.y - input->mouse_y);

	if (!mouse_down) {
		ui_system.drag_status = Drag_Status::NONE;
	}

	// Compact widgets and container arrays, and reset data for next frame
	{
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
				dynamic_array_destroy(&container.elements);
				dynamic_array_destroy(&container.matching_infos);
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
			widget.element_index_in_container = -1;
			if (widget.visited_this_frame) {
				widget.visited_this_frame = false;
				widget.parent_container.container_index = moved_container_indices[widget.parent_container.container_index];
				assert(widget.parent_container.container_index != -1, "");
				ui_system.widgets[next_widget_index] = widget;
				moved_widget_indices[i] = next_widget_index;
				next_widget_index += 1;
			}
			else {
				moved_widget_indices[i] = -1;
			}
		}
		dynamic_array_rollback_to_size(&ui_system.widgets, next_widget_index);

		// Remove windows that weren't used this frame
		int next_window_index = 0;
		int min_z_index = 1000000;
		int max_z_index = 0;
		Array<int> moved_window_indices = array_create<int>(ui_system.windows.size);
		SCOPE_EXIT(array_destroy(&moved_window_indices));
		dynamic_array_reset(&ui_system.window_z_sorting);
		for (int i = 0; i < ui_system.windows.size; i++)
		{
			auto& window = ui_system.windows[i];

			if (window.visited_this_frame)
			{
				window.visited_this_frame = false;
				assert(!window.root.is_widget, "");
				window.root.element_index = moved_container_indices[window.root.element_index];
				dynamic_array_push_back(&ui_system.window_z_sorting, next_window_index);
				min_z_index = math_minimum(min_z_index, window.z_index);
				max_z_index = math_maximum(max_z_index, window.z_index);
				if (window.style.layout == Window_Layout::DROPDOWN) {
					window.style.options.dropdown_parent_widget.widget_index =
						moved_widget_indices[window.style.options.dropdown_parent_widget.widget_index];
				}

				ui_system.windows[next_window_index] = window;
				moved_window_indices[i] = next_window_index;
				next_window_index += 1;
			}
			else {
				moved_window_indices[i] = -1;
			}
		}
		dynamic_array_rollback_to_size(&ui_system.windows, next_window_index);

		// Recalculate Z-Sorting
		dynamic_array_sort(&ui_system.window_z_sorting, Window_Z_Comparator());
		for (int i = 0; i < ui_system.windows.size; i++) {
			ui_system.windows[i].z_index = ui_system.windows[i].z_index - min_z_index;
		}
		ui_system.max_window_z_index = max_z_index - min_z_index;

		// Update container data (Element-Indices, Scroll-Data, and visible elements)
		for (int i = 0; i < ui_system.containers.size; i++)
		{
			auto& container = ui_system.containers[i];
			container.matching_failed_this_frame = false;
			container.next_matching_index = 0;

			if (container.parent_container.container_index != -1) {
				container.parent_container.container_index = moved_container_indices[container.parent_container.container_index];
			}

			// Update matching infos + create visible elements array
			int next_child_index = 0;
			dynamic_array_reset(&container.elements);
			for (int j = 0; j < container.matching_infos.size; j++)
			{
				UI_Matching_Info& matching_info = container.matching_infos[j];

				int new_index = -1;
				if (matching_info.is_widget) {
					new_index = moved_widget_indices[matching_info.element_index];
				}
				else {
					new_index = moved_container_indices[matching_info.element_index];
				}

				// Compact
				if (new_index != -1)
				{
					matching_info.element_index = new_index;
					container.matching_infos[next_child_index] = matching_info;
					next_child_index += 1;

					if (matching_info.is_hidden) continue;
					Container_Element element;
					element.is_widget = matching_info.is_widget;
					element.element_index = matching_info.element_index;
					dynamic_array_push_back(&container.elements, element);

					if (element.is_widget) {
						ui_system.widgets[element.element_index].element_index_in_container = container.elements.size - 1;
					}
				}
			}
			dynamic_array_rollback_to_size(&container.matching_infos, next_child_index);
		}

		// Update drag infos
		int new_drag_index = ui_system.drag_index;
		switch (ui_system.drag_status)
		{
		case Drag_Status::SCROLL_BAR: new_drag_index = moved_container_indices[ui_system.drag_index]; break;
		case Drag_Status::WINDOW_MOVE:
		case Drag_Status::WINDOW_RESIZE: new_drag_index = moved_window_indices[ui_system.drag_index]; break;
		case Drag_Status::NONE: break;
		default: panic("");
		}
		ui_system.drag_index = new_drag_index;
		if (new_drag_index == -1) {
			ui_system.drag_status = Drag_Status::NONE;
		}

		// Reset text-input data
		ui_system.text_changed_widget_index = -1;
		ui_system.changed_text.length = 0;
		ui_system.changed_text.start_index = 0;
	}

	// Do layout
	for (int window_index = 0; window_index < ui_system.windows.size; window_index++)
	{
		UI_Window* window = &ui_system.windows[window_index];
		if (window->style.is_hidden) continue;

		switch (window->style.layout)
		{
		case Window_Layout::FLOAT: break;
		case Window_Layout::DROPDOWN: {
			continue;
		}
		case Window_Layout::ANCHOR_RIGHT:
		{
			BBox& box = window->window_box;
			int width = box.max.x - box.min.x;
			box = BBox(ivec2(0), screen_size);
			box.min.x = box.max.x - width;
			break;
		}
		default: panic("");
		}

		Container_Element& root_element = window->root;
		BBox client_box = ui_window_get_client_area(window_index);
		window->root.box = client_box;

		container_element_gather_width_information_recursive(&root_element, true);
		container_element_do_horizontal_layout_and_find_height(&root_element, client_box.min.x, client_box.max.x - client_box.min.x);
		container_element_do_vertical_layout(&root_element, client_box.max.y, client_box.max.y - client_box.min.y);
	}

	// Layout windows with DROPDOWN style (Require widget positions of other windows)
	for (int window_index = 0; window_index < ui_system.windows.size; window_index++)
	{
		UI_Window* window = &ui_system.windows[window_index];
		if (window->style.layout != Window_Layout::DROPDOWN) continue;
		if (window->style.is_hidden) continue;

		Container_Element& root_element = window->root;
		container_element_gather_width_information_recursive(&root_element, true);

		// Set window position + size
		Widget& parent_widget = ui_system.widgets[window->style.options.dropdown_parent_widget.widget_index];
		Container_Element& element = ui_system.containers[
			parent_widget.parent_container.container_index
		].elements[parent_widget.element_index_in_container];
		assert(element.element_index == window->style.options.dropdown_parent_widget.widget_index, "");
		BBox& window_box = window->window_box;

		// Figure out width and x-pos
		{
			// Width is the max of window-minimum, required_size of children and parent element width
			int width = math_maximum(
				root_element.min_width_without_collapse + 2 * BORDER_SPACE, 
				math_maximum(window->style.min_size.x, math_minimum(screen_size.x, element.box.max.x) - element.box.min.x)
			);
			width = math_minimum(screen_size.x, width);

			// Move dropdown so it best fits on screen
			int x_pos = element.box.min.x;
			if (x_pos + width > screen_size.x) {
				x_pos = screen_size.x - width;
			}
			if (x_pos < 0) {
				x_pos = 0;
			}

			window_box.min.x = x_pos;
			window_box.max.x = x_pos + width;
			container_element_do_horizontal_layout_and_find_height(&root_element, x_pos + BORDER_SPACE, width - 2 * BORDER_SPACE);
		}

		// Figure out height and y-pos
		{
			// Width is the max of window-minimum, required_size of children and parent element width
			int height = math_maximum(root_element.min_height + 2 * BORDER_SPACE, window->style.min_size.y);
			int available_height_below = element.box.min.y;
			int available_height_above = screen_size.y - element.box.max.y;
			int y_pos = element.box.min.y;
			if (y_pos - height < 0 && available_height_above > available_height_below) {
				height = math_minimum(height, available_height_above);
				y_pos = element.box.max.y + height;
			}
			else {
				height = math_minimum(height, available_height_below);
			}

			window_box.max.y = y_pos;
			window_box.min.y = y_pos - height;
			container_element_do_vertical_layout(&root_element, y_pos - BORDER_SPACE, height - 2 * BORDER_SPACE);
		}
		window->root.box = ui_window_get_client_area(window_index);
	}

	// Update hover infos (For hover colors and mouse cursor)
	ui_system_find_mouse_hover_infos(mouse);

	// Update mouse cursor
	{
		Cursor_Icon_Type icon = Cursor_Icon_Type::ARROW;

		Drag_Status drag_status = ui_system.drag_status;
		ivec2 resize_dir = ui_system.resize_direction;
		if (ui_system.drag_status == Drag_Status::NONE)
		{
			if (ui_system.mouse_hover_widget_index != -1) {
				Widget& widget = ui_system.widgets[ui_system.mouse_hover_widget_index];
				if (widget.style.is_clickable) {
					icon = Cursor_Icon_Type::HAND;
				}
				if (widget.style.can_obtain_text_input) {
					icon = Cursor_Icon_Type::IBEAM;
				}
			}
			else if (ui_system.mouse_hover_drag_status != Drag_Status::NONE) {
				drag_status = ui_system.mouse_hover_drag_status;
				resize_dir = ui_system.mouse_hover_resize_direction;
			}
		}

		if (drag_status != Drag_Status::NONE)
		{
			if (drag_status == Drag_Status::WINDOW_RESIZE)
			{
				ivec2 dir = resize_dir;
				if (dir.x == 0 && dir.y != 0) {
					icon = Cursor_Icon_Type::SIZE_VERTICAL;
				}
				else if (dir.y == 0 && dir.x != 0) {
					icon = Cursor_Icon_Type::SIZE_HORIZONTAL;
				}
				else if (dir.x != 0 && dir.y != 0 && dir.x + dir.y == 0) {
					icon = Cursor_Icon_Type::SIZE_SOUTHEAST;
				}
				else {
					icon = Cursor_Icon_Type::SIZE_NORTHEAST;
				}
			}
			else {
				icon = Cursor_Icon_Type::HAND;
			}
		}

		if (icon != ui_system.last_cursor_icon_type) {
			window_set_cursor_icon(whole_window, icon);
			ui_system.last_cursor_icon_type = icon;
		}
	}

	// Render
	for (int i = 0; i < ui_system.window_z_sorting.size; i++)
	{
		int window_index = ui_system.window_z_sorting[i];
		UI_Window& window = ui_system.windows[window_index];
		if (window.style.is_hidden) continue;

		BBox window_box = window.window_box;
		BBox client_box = ui_window_get_client_area(window_index);

		// Render Window + widgets
		mesh_push_inner_border_clipped(ui_system.mesh, window_box, window_box, COLOR_WINDOW_BORDER, BORDER_SPACE);
		if (window.style.has_title_bar) {
			BBox header_box = ui_window_get_title_area(window_index);
			mesh_push_box(ui_system.mesh, header_box, COLOR_WINDOW_BG_HEADER);
			mesh_push_text_clipped(
				ui_system.mesh, &ui_system.glyph_atlas, ui_string_to_string(window.style.title),
				header_box.min + ivec2(BORDER_SPACE) + ivec2(PAD_LEFT_RIGHT, PAD_BOT), header_box
			);
		}
		mesh_push_box(ui_system.mesh, client_box, window.style.bg_color);
		container_element_render(&window.root, window.root.box, 0, ui_system.mesh, &ui_system.glyph_atlas);
	}

	render_pass_draw(
		render_pass_alpha_blended, ui_system.shader, ui_system.mesh, Mesh_Topology::TRIANGLES, 
		{ uniform_make("u_sampler", ui_system.texture, sampling_mode_nearest()) }
	);
}



// BUILDER CODE
Widget_Style widget_style_make_empty()
{
	Widget_Style style;

	// Set rendering options
	style.draw_background = false;
	style.background_color = COLOR_BUTTON_BG;
	style.hover_color = COLOR_BUTTON_BG_HOVER;
	style.focused_color = COLOR_BUTTON_BG_HOVER;
	style.has_border = false;
	style.border_color = COLOR_BUTTON_BORDER;
	style.text_alignment = Text_Alignment::LEFT;
	style.text_display.length = 0;
	style.text_display.start_index = 0;
	style.draw_icon = false;
	style.icon_left_aligned = true;
	style.icon.type = Icon_Type::TRIANGLE_LEFT;
	style.icon.color = vec3(1.0f, 0.0f, 0.0f);
	style.icon.rotation = Icon_Rotation::NONE;

	// Size options
	style.min_width = 0;
	style.max_width = 0;
	style.height = ui_system.line_item_height;
	style.can_grow_beyond_max_width = true;
	style.can_combine_in_lines = true;

	// Input options
	style.is_clickable = false;
	style.can_obtain_text_input = false;
	style.text_input_type = Text_Input_Type::TEXT;

	return style;
}

Widget_Style widget_style_make_text_in_box(String text, vec4 bg_color, vec4 hover_color, vec4 border_color, Text_Alignment text_alignment)
{
	Widget_Style style = widget_style_make_empty();
	style.draw_background = true;
	style.background_color = bg_color;
	style.hover_color = hover_color;
	style.focused_color = hover_color;
	style.has_border = true;
	style.border_color = border_color;
	style.text_alignment = text_alignment;
	style.text_display = ui_system_add_string(text);

	return style;
}

Window_Style window_style_make_floating(const char* title)
{
	Window_Style result;
	result.has_title_bar = true;
	result.title = ui_system_add_string(string_create_static(title));
	result.layout = Window_Layout::FLOAT;
	result.min_size = ivec2(60, 40);
	result.bg_color = COLOR_WINDOW_BG;
	result.is_hidden = false;
	return result;
}

Window_Style window_style_make_anchored(const char* title)
{
	Window_Style result;
	result.has_title_bar = true;
	result.title = ui_system_add_string(string_create_static(title));
	result.layout = Window_Layout::ANCHOR_RIGHT;
	result.min_size = ivec2(60, 40);
	result.bg_color = COLOR_WINDOW_BG;
	result.is_hidden = false;
	return result;
}

Window_Style window_style_make_dropdown(Widget_Handle parent_widget, int min_width)
{
	Window_Style result;
	result.has_title_bar = false;
	result.title.length = 0;
	result.title.start_index = 0;
	result.layout = Window_Layout::DROPDOWN;
	result.options.dropdown_parent_widget = parent_widget;
	result.min_size = ivec2(min_width, 0);
	result.bg_color = COLOR_DROPDOWN_BG;
	result.is_hidden = false;
	return result;
}

Container_Layout container_layout_make_default()
{
	Container_Layout layout;
	layout.draw_background = false;
	layout.padding = 0;
	layout.type = Layout_Type::NORMAL;
	layout.options.normal.allow_line_combination = true;
	layout.options.normal.indentation = 0;
	layout.options.normal.min_height_empty = 0;
	layout.options.normal.min_height_restrained = -1;
	layout.options.normal.scroll_bar_enabled = false;
	return layout;
}

Container_Layout container_layout_make_horizontal(bool allow_collapse)
{
	Container_Layout layout;
	layout.draw_background = false;
	layout.padding = 0;
	layout.type = Layout_Type::STACK_HORIZONTAL;
	layout.options.horizontal_allow_collapse = allow_collapse;
	return layout;
}

Container_Layout container_layout_make_list(int min_lines_to_display)
{
	Container_Layout layout;
	layout.draw_background = true;
	layout.background_color = COLOR_LIST_BG;
	layout.padding = 1;
	layout.type = Layout_Type::NORMAL;
	layout.options.normal.allow_line_combination = false;
	layout.options.normal.indentation = 0;
	layout.options.normal.min_height_empty = ui_system.line_item_height;;
	layout.options.normal.min_height_restrained = 
		ui_system.line_item_height * min_lines_to_display + 
		math_maximum(0, min_lines_to_display - 1) * PAD_WIDGETS_BETWEEN_LINES;
	layout.options.normal.scroll_bar_enabled = false;
	return layout;
}



Button_Input ui_system_push_button(const char* label_text)
{
	Widget_Style style = widget_style_make_text_in_box(
		string_create_static(label_text), COLOR_BUTTON_BG, COLOR_BUTTON_BG_HOVER, COLOR_BUTTON_BORDER, Text_Alignment::CENTER
	);
	style.is_clickable = true;
	style.min_width = BUTTON_MIN_CHAR_COUNT * ui_system.char_size.x + TEXT_BORDER_SPACE;
	style.max_width = BUTTON_WANTED_CHAR_COUNT * ui_system.char_size.x + TEXT_BORDER_SPACE;
	style.can_grow_beyond_max_width = true;

	Button_Input result;
	result.widget = ui_system_add_widget(style);
	result.was_pressed = ui_system.mouse_hover_widget_index == result.widget.widget_index && ui_system.mouse_was_clicked;
	return result;
}

Widget_Handle ui_system_push_label(String text, bool restrain_label_size)
{
	Widget_Style style = widget_style_make_empty();
	style.text_display = ui_system_add_string(text);
	style.can_grow_beyond_max_width = false;
	if (restrain_label_size) {
		style.min_width = LABEL_CHAR_COUNT_SIZE * ui_system.char_size.x;
		style.max_width = LABEL_CHAR_COUNT_SIZE * ui_system.char_size.x;
		style.can_combine_in_lines = true;
	}
	else {
		style.max_width = style.text_display.length * ui_system.char_size.x;
		style.min_width = math_minimum(style.text_display.length, 8) * ui_system.char_size.x;
		style.can_combine_in_lines = false;
	}

	return ui_system_add_widget(style);
}

Widget_Handle ui_system_push_label(const char* text, bool restrain_label_size) {
	return ui_system_push_label(string_create_static(text), restrain_label_size);
}

Text_Input_State ui_system_push_text_input(String text)
{
	Widget_Style style = widget_style_make_text_in_box(text, COLOR_INPUT_BG, COLOR_INPUT_BG_HOVER, COLOR_INPUT_BORDER, Text_Alignment::LEFT);

	style.min_width = TEXT_INPUT_MIN_CHAR_COUNT * ui_system.char_size.x + TEXT_BORDER_SPACE;
	style.max_width = TEXT_INPUT_MAX_CHAR_COUNT * ui_system.char_size.x + TEXT_BORDER_SPACE;
	style.height = ui_system.line_item_height;
	style.can_grow_beyond_max_width = true;
	style.is_clickable = true;
	style.can_obtain_text_input = true;
	style.text_input_type = Text_Input_Type::TEXT;

	Widget_Handle handle = ui_system_add_widget(style);
	Widget& widget = ui_system.widgets[handle.widget_index];

	Text_Input_State result;
	result.text_was_changed = handle.widget_index == ui_system.text_changed_widget_index;
	result.new_text = string_create_static("");
	result.handle = handle;
	if (result.text_was_changed) {
		result.new_text = ui_system.input_string;
		widget.style.text_display = ui_system_add_string(ui_system.input_string);
	}
	return result;
}

int ui_system_push_int_input(int value)
{
	String tmp = string_create();
	SCOPE_EXIT(string_destroy(&tmp));
	string_append_formated(&tmp, "%d", value);
	Text_Input_State update_state = ui_system_push_text_input(tmp);

	Widget& widget = ui_system.widgets[update_state.handle.widget_index];
	widget.style.text_input_type = Text_Input_Type::INT;
	widget.style.text_alignment = Text_Alignment::RIGHT;
	widget.style.background_color = COLOR_INPUT_BG_NUMBER;
	if (update_state.text_was_changed)
	{
		String text = update_state.new_text;
		Optional<int> parsed_value = string_parse_int(&text);
		if (parsed_value.available) {
			value = parsed_value.value;
			string_reset(&tmp);
			string_append_formated(&tmp, "%d", value);
		}

		widget.style.text_display = ui_system_add_string(tmp);
	}

	return value;
}

float ui_system_push_float_input(float value)
{
	String tmp = string_create();
	SCOPE_EXIT(string_destroy(&tmp));
	string_append_formated(&tmp, "%.3f", value);
	Text_Input_State update_state = ui_system_push_text_input(tmp);

	Widget& widget = ui_system.widgets[update_state.handle.widget_index];
	widget.style.text_input_type = Text_Input_Type::FLOAT;
	widget.style.background_color = COLOR_INPUT_BG_NUMBER;
	if (update_state.text_was_changed)
	{
		String text = update_state.new_text;
		Optional<float> parsed_value = string_parse_float(&text);
		if (parsed_value.available) {
			value = parsed_value.value;
			string_reset(&tmp);
			string_append_formated(&tmp, "%.3f", value);
		}

		// Don't show non-parsable text
		widget.style.text_display = ui_system_add_string(tmp);
	}

	return value;
}

void ui_system_push_next_component_label(const char* label_text)
{
	Container_Handle container_handle = ui_system_add_container(container_layout_make_horizontal(true));
	ui_system_push_active_container(container_handle, true);
	ui_system_push_label(string_create_static(label_text), true);
	ui_system_push_active_container(container_handle, true);
}

bool ui_system_push_icon_button(UI_Icon icon, bool draw_border, Widget_Handle* out_widget_handle)
{
	Widget_Style style = widget_style_make_empty();
	style.draw_background = true;
	style.background_color = COLOR_BUTTON_BG;
	style.hover_color = COLOR_BUTTON_BG_HOVER;
	style.has_border = draw_border;
	style.border_color = COLOR_BUTTON_BORDER;

	int padding = ICON_PADDING + draw_border ? BORDER_SPACE : 0;
	style.min_width = ui_system.line_item_height;
	style.max_width = style.min_width;
	style.can_grow_beyond_max_width = false;
	style.is_clickable = true;
	style.draw_icon = true;
	style.icon = icon;

	Widget_Handle handle = ui_system_add_widget(style);
	if (out_widget_handle != nullptr) {
		*out_widget_handle = handle;
	}
	return ui_system.mouse_hover_widget_index == handle.widget_index && ui_system.mouse_was_clicked;
}

bool ui_system_push_checkbox_style(bool enabled, UI_Icon enabled_icon, UI_Icon disabled_icon, bool draw_background)
{
	Widget_Handle handle;
	bool pressed = ui_system_push_icon_button(enabled_icon, draw_background, &handle);
	if (pressed) {
		enabled = !enabled;
	}
	
	Widget& widget = ui_system.widgets[handle.widget_index];
	widget.style.icon = enabled ? enabled_icon : disabled_icon;
	return enabled;
}

bool ui_system_push_checkbox(bool enabled)
{
	return ui_system_push_checkbox_style(
		enabled,
		ui_icon_make(Icon_Type::CHECK_MARK, Icon_Rotation::NONE, vec3(1.0f)),
		ui_icon_make(Icon_Type::NONE, Icon_Rotation::NONE, vec3(1.0f)),
		true
	);
}

UI_Subsection_Info ui_system_push_subsection(bool enabled, const char* section_name, bool own_scrollbar)
{
	Widget_Style style = widget_style_make_text_in_box(
		string_create_static(section_name), COLOR_BUTTON_BG, COLOR_BUTTON_BG_HOVER, COLOR_BUTTON_BORDER, Text_Alignment::LEFT
	);
	style.draw_icon = true;
	style.icon = ui_icon_make(Icon_Type::TRIANGLE_LEFT_SMALL, enabled ? Icon_Rotation::ROT_90 : Icon_Rotation::NONE, vec3(1.0f));
	style.can_combine_in_lines = false;
	style.is_clickable = true;

	Widget_Handle handle = ui_system_add_widget(style);
	Widget& widget = ui_system.widgets[handle.widget_index];
	if (ui_system.mouse_hover_widget_index == handle.widget_index && ui_system.mouse_was_clicked) {
		enabled = !enabled;
		widget.style.icon.rotation = enabled ? Icon_Rotation::ROT_90 : Icon_Rotation::NONE;
	}

	Container_Layout layout = container_layout_make_default();
	layout.options.normal.indentation = ui_system.char_size.x * 2;
	layout.options.normal.scroll_bar_enabled = own_scrollbar;
	layout.draw_background = false;
	if (own_scrollbar) {
		int min_line_count = 3;
		layout.options.normal.min_height_restrained = ui_system.line_item_height * min_line_count + (min_line_count - 1) * PAD_WIDGETS_BETWEEN_LINES;
	}

	UI_Subsection_Info subsection;
	subsection.enabled = enabled;
	subsection.container = ui_system_add_container(layout, !enabled);
	return subsection;
}

void ui_system_push_dropdown(Dropdown_State& state, Array<String> possible_values)
{
	state.value_was_changed = false;
	state.value = math_clamp(state.value, 0, (int)possible_values.size);
	String text =  possible_values.size == 0 ? string_create_static("NO_OPTIONS_PROVIDED!") : possible_values[state.value];

	Widget_Style style = widget_style_make_text_in_box(text, COLOR_DROPDOWN_BG, COLOR_DROPDOWN_HOVER, COLOR_INPUT_BORDER, Text_Alignment::LEFT);
	style.is_clickable = true;
	style.min_width = ui_system.char_size.x * 6 + PAD_LEFT_RIGHT + 2 * BORDER_SPACE;
	style.max_width = ui_system.char_size.x * 24 + PAD_LEFT_RIGHT + 2 * BORDER_SPACE;
	style.can_grow_beyond_max_width = true;
	style.icon = ui_icon_make(Icon_Type::TRIANGLE_LEFT_SMALL, Icon_Rotation::ROT_90, vec3(1.0f));
	style.draw_icon = true;
	style.icon_left_aligned = false;
	style.can_combine_in_lines = false;

	Widget_Handle widget_handle = ui_system_add_widget(style);
	if (widget_handle.created_this_frame) {
		state.is_open = false;
		state.value = 0;
	}

	bool pressed_button = ui_system.mouse_hover_widget_index == widget_handle.widget_index && ui_system.mouse_was_clicked;
	bool opened_this_frame = !state.is_open && pressed_button;
	state.is_open = state.is_open || pressed_button;
	if (state.is_open)
	{
		Window_Handle dropdown = ui_system_add_window(window_style_make_dropdown(widget_handle, style.min_width));
		ui_system_set_window_topmost(dropdown);

		// Check if any labels were pressed
		ui_system_push_active_container(dropdown.container, false);
		int pressed_label_index = -1;
		for (int i = 0; i < possible_values.size; i++)
		{
			String text = possible_values[i];
			Widget_Style style = widget_style_make_text_in_box(text, COLOR_DROPDOWN_BG, COLOR_DROPDOWN_HOVER, COLOR_INPUT_BORDER, Text_Alignment::LEFT);
			style.has_border = false;
			style.is_clickable = true;
			style.min_width = ui_system.char_size.x * text.size + PAD_LEFT_RIGHT + 2 * BORDER_SPACE;
			style.max_width = ui_system.char_size.x * text.size + PAD_LEFT_RIGHT + 2 * BORDER_SPACE;
			style.can_grow_beyond_max_width = true;
			style.can_combine_in_lines = false;

			Widget_Handle handle = ui_system_add_widget(style);
			if (ui_system.mouse_hover_widget_index == handle.widget_index && ui_system.mouse_was_clicked) {
				pressed_label_index = i;
				break;
			}
		}
		ui_system_pop_active_container();

		if (pressed_label_index != -1) {
			state.value_was_changed = true;
			state.value = pressed_label_index;
			state.is_open = false;
		}

		// Check if we clicked away from dropdown
		if (!opened_this_frame && ui_system.mouse_was_clicked && ui_system.mouse_hover_window_index != dropdown.window_index) {
			state.is_open = false;
		}

		if (!state.is_open) {
			ui_system.windows[dropdown.window_index].style.is_hidden = true;
		}
	}
}

Container_Handle ui_system_push_line_container() {
	return ui_system_add_container(container_layout_make_horizontal(false));
}

void ui_system_push_test_windows()
{
	static bool test_initialized = false;
	static String texts[3];
	if (!test_initialized)
	{
		test_initialized = true;

		for (int i = 0; i < 3; i++) {
			const char* initial[3] = {
				"Something that you soundlt ",
				"Dont you carrera about me",
				"Wellerman",
			};
			texts[i] = string_create(initial[i % 3]);
		}
	}

	static bool check_box_enabled = false;
	static int int_value = 0;
	static float float_value = 0.0f;
	static bool subsection_status = true;
	static bool subsection_breakpoints = true;
	static bool subsection_watch_values = true;
	static bool drop_down_open = false;

	if (false)
	{
		Window_Handle handle = ui_system_add_window(window_style_make_anchored("Debugger_Info"));
		ui_system_push_active_container(handle.container, false);
		SCOPE_EXIT(ui_system_pop_active_container());

		ui_system_push_checkbox(true);
		ui_system_push_checkbox(false);
		return;

		// auto subsection_info = ui_system_push_subsection(true, "Watch_Window", true);
		// ui_system_push_active_container(subsection_info.container, false);
		// SCOPE_EXIT(ui_system_pop_active_container());

		ui_system_push_active_container(ui_system_push_line_container(), false);
		SCOPE_EXIT(ui_system_pop_active_container());

		ui_system_push_button("Frick dis");
		ui_system_push_icon_button(ui_icon_make(Icon_Type::X_MARK, Icon_Rotation::NONE, vec3(1.0f)), true);
		ui_system_push_label("Henlo", false);

		return;
	}


	Window_Handle window_handle = ui_system_add_window(window_style_make_anchored("Test-Window"));
	ui_system_push_active_container(window_handle.container, false);

	UI_Subsection_Info info = ui_system_push_subsection(subsection_status, "Status", false);
	subsection_status = info.enabled;
	if (subsection_status) {
		ui_system_push_active_container(info.container, false);
		SCOPE_EXIT(ui_system_pop_active_container());
		ui_system_push_next_component_label("Stack:");
		ui_system_push_text_input(string_create_static("upp_main"));
	}

	info = ui_system_push_subsection(subsection_breakpoints, "Breakpoints", true);
	subsection_breakpoints = info.enabled;
	if (subsection_breakpoints) {
		ui_system_push_active_container(info.container, false);
		SCOPE_EXIT(ui_system_pop_active_container());

		ui_system_push_label(string_create_static("Bp 1 at line #15"), false);
		ui_system_push_label(string_create_static("Bp 2 at line #105"), false);
		ui_system_push_label(string_create_static("Bp 3 at line #1"), false);
		ui_system_push_label(string_create_static("Bp 4 at line #32"), false);
		ui_system_push_label(string_create_static("Bp 5 at line #23"), false);
		ui_system_push_label(string_create_static("Bp 5 at line #23"), false);
		ui_system_push_label(string_create_static("Bp 5 at line #23"), false);
		ui_system_push_label(string_create_static("Bp 9 at line #1027"), false);
	}

	info = ui_system_push_subsection(subsection_watch_values, "Watch-Values", true);
	subsection_watch_values = info.enabled;
	if (subsection_watch_values) {
		ui_system_push_active_container(info.container, false);
		SCOPE_EXIT(ui_system_pop_active_container());

		ui_system_push_active_container(ui_system_push_line_container(), false);
		ui_system_push_text_input(string_create_static(""));
		ui_system_push_label("Hello there", true);
		ui_system_pop_active_container();
	}
	ui_system_push_button("Test, lol");

	ui_system_push_label(string_create_static("Hello IMGUI world!"), false);
	ui_system_push_label(string_create_static("Test label to check if render works"), false);
	ui_system_push_next_component_label("Click for test");
	ui_system_push_button("Click me!");
	for (int i = 0; i < 4; i++)
	{
		const char* labels[3] = {
			"Name",
			"Surname",
			"Address",
		};
		ui_system_push_next_component_label(labels[i % 3]);
		String& text = texts[i % 3];
		auto update = ui_system_push_text_input(text);
		if (update.text_was_changed) {
			string_reset(&text);
			string_append_string(&text, &update.new_text);
		}
	}
	bool pressed = ui_system_push_button("Frick me").was_pressed;
	if (pressed) {
		printf("Frick me was pressed!\n");
	}
	pressed = ui_system_push_button("Frick me").was_pressed;
	if (pressed) {
		printf("Another one was pressed!\n");
	}

	ui_system_push_active_container(ui_system_push_line_container(), false);
	ui_system_push_checkbox(true);
	ui_system_push_checkbox(false);
	ui_system_push_icon_button(ui_icon_make(Icon_Type::CHECK_MARK, Icon_Rotation::NONE, vec3(1, 0, 0)), true);
	ui_system_push_icon_button(ui_icon_make(Icon_Type::ARROW_LEFT, Icon_Rotation::NONE, vec3(1, 1, 0)), true);
	ui_system_push_icon_button(ui_icon_make(Icon_Type::TRIANGLE_LEFT, Icon_Rotation::NONE, vec3(0, 1, 0)), true);
	ui_system_push_icon_button(ui_icon_make(Icon_Type::TRIANGLE_LEFT_SMALL, Icon_Rotation::NONE, vec3(1, 1, 1)), true);
	ui_system_push_icon_button(ui_icon_make(Icon_Type::X_MARK, Icon_Rotation::NONE, vec3(1, 1, 1)), false);

	ui_system_pop_active_container();

	Window_Handle new_window = ui_system_add_window(window_style_make_floating("Dropdown parent window"));
	ui_system_push_active_container(new_window.container, false);

	String values[] = {
		string_create_static("Hello"),
		string_create_static("There"),
		string_create_static("Another one")
	};
	static Dropdown_State dropdown_state;
	ui_system_push_dropdown(dropdown_state, array_create_static(&values[0], 3));
}

