#pragma once

#include "../math/vectors.hpp"
#include "../datastructures/string.hpp"
#include "../datastructures/allocators.hpp"

struct Mesh;
struct Render_Pass;
struct Texture;
struct Font_Renderer;

struct Texture_Buffer
{
	ivec2 size;
	u8* data; // Starts at bottom-left, fills up upwards
	int stride; // Usefull for sub-images

	u8* get(ivec2 pos) {
		assert(pos.x >= 0 && pos.x < size.x && pos.y >= 0 && pos.y < size.y, "");
		return &data[pos.x + stride * pos.y];
	}
};

struct Font_Glyph_Info
{
	ibox2 atlas_box;
	ivec2 offset_from_line;
};

struct Raster_Font
{
	Font_Renderer* renderer;
	ivec2 char_size; // Maximum size of a characters bitmap
	int base_line_offset; // Number of pixels for descender
	Array<Font_Glyph_Info> glyph_infos; // Glyph 0 is always 'error' glyph
	Array<u8> char_to_glyph_index;

	// Position is bottom-left of line
	void push_line(ivec2 pos, String text, vec3 color);
	void add_draw_call(Render_Pass* pass);
};

struct Font_Renderer
{
	Arena arena;
	Texture_Buffer buffer;
	ivec2 pos; // Write pos in texture-buffer
    int max_char_height_in_line;

	// Rendering stuff
	Texture* texture;
	Mesh* font_mesh;
	int batch_start;
	int batch_end;

	static Font_Renderer* create(int atlas_size);
	void destroy();

	// Note: Because of hinting and all that line-height is just a suggestion, and we probably get a value close to it
	//  Also some glyphs may not be used, so we cannot determine this beforehand...
	Raster_Font* add_raster_font(const char* filename, int line_height); // Crashes if filename does not exist or not enough space
	void finish_atlas(); // Creates texture on GPU
	void reset();
	void add_draw_call(Render_Pass* pass);
};

