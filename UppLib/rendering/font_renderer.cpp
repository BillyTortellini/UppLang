#include "font_renderer.hpp"

#include "../rendering/texture.hpp"
#include "../rendering/rendering_core.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

Texture_Buffer texture_buffer_create(ivec2 size, Arena* arena)
{
	Texture_Buffer result;
	result.size = size;
	result.data = arena->allocate_array<u8>(size.x * size.y).data;
	result.stride = size.x;
    // Initialize with some pattern for error handling
    for (int y = 0; y < size.y; y++) {
        for (int x = 0; x < size.x; x++) {
            result.data[x + y * size.x] = (x + y) % 2 == 0 ? 200 : 50;
        }
    }
	return result;
}

Texture_Buffer texture_buffer_make_view(u8* data, ivec2 size, int stride)
{
	Texture_Buffer result;
	result.size = size;
    result.data = data;
	result.stride = stride;
	return result;
}

void texture_buffer_inpaint_mirror_y(Texture_Buffer target, Texture_Buffer source, ivec2 pos)
{
    for (int x = 0; x < source.size.x && x + pos.x < target.size.x; x++)
    {
        for (int y = 0; y < source.size.y && y + pos.y < target.size.y; y++)
        {
            *target.get(ivec2(x, y) + pos) = *source.get(ivec2(x, source.size.y - 1 - y));
        }
    }
}

void texture_buffer_inpaint(Texture_Buffer target, Texture_Buffer source, ivec2 pos)
{
    for (int x = 0; x < source.size.x && x + pos.x < target.size.x; x++)
    {
        for (int y = 0; y < source.size.y && y + pos.y < target.size.y; y++)
        {
            *target.get(ivec2(x, y) + pos) = *source.get(ivec2(x, y));
        }
    }
}

Font_Renderer* Font_Renderer::create(int atlas_size)
{
	Font_Renderer* result = new Font_Renderer;
	result->arena = Arena::create();
	result->buffer = texture_buffer_create(ivec2(atlas_size, atlas_size), &result->arena);
	result->font_mesh = nullptr;
	result->texture = nullptr;
    result->pos = ivec2(0);
    result->max_char_height_in_line = 0;
    result->reset();
	return result;
}

void Font_Renderer::destroy()
{
	arena.destroy();
	if (texture != nullptr) {
		texture_destroy(texture);
		texture = nullptr;
	}
	delete this;
}

Raster_Font* Font_Renderer::add_raster_font(const char* filename, int line_height)
{
    Raster_Font* result = arena.allocate<Raster_Font>();
    result->renderer = this;
    result->char_to_glyph_index = arena.allocate_array<u8>(256);
    result->glyph_infos = arena.allocate_array<Font_Glyph_Info>(256); // Will get resized later
    result->base_line_offset = 0;
    int next_free_glyph_info = 0;
    SCOPE_EXIT(
        arena.rewind_to_address(&result->glyph_infos[next_free_glyph_info]);
        result->glyph_infos.size = next_free_glyph_info;
    );



    // Initialize freetype
    FT_Library library;
    u32 ft_error = FT_Init_FreeType(&library);
    if (ft_error != 0) {
        logg("Could not initialize freetype, error: %s\n", FT_Error_String(ft_error));
        return nullptr;
    }
    SCOPE_EXIT(FT_Done_FreeType(library));

    FT_Face face;
    ft_error = FT_New_Face(library, filename, 0, &face);
    if (ft_error != 0) {
        logg("Could not create face for \"%s\", error: %s\n", filename, FT_Error_String(ft_error));
        return nullptr;
    }
    SCOPE_EXIT(FT_Done_Face(face));

    ft_error = FT_Set_Pixel_Sizes(face, 0, line_height);
    if (ft_error != 0) {
        logg("FT_Set_Pixel_Size failed, error: %s\n", FT_Error_String(ft_error));
        return nullptr;
    }



    // Rasterize each character and add to texture-buffer
    DynTable<u32, int> freetype_to_our_mapping = DynTable<u32, int>::create(&arena, hash_u32, equals_u32);
    result->char_size = ivec2(0, 0);
    int min_coord = INT_MAX;
    for (int i = 0; i < 32; i++) {
        result->char_to_glyph_index[i] = 0; // Glyph 0 is error-glyph
    }
    for (int i = 31; i < result->char_to_glyph_index.size; i++) // Start with first printable ascii character (Space = 32)
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
                result->char_to_glyph_index[i] = 0;
                // logg("Glyph %c (#%d) does not exist\n", current_character, i);
                continue;
            }
        }

        // Check if this glyph was already renderer
        int* internal_glyph_index = freetype_to_our_mapping.find(glyph_index);
        if (internal_glyph_index != nullptr) {
            result->char_to_glyph_index[i] = *internal_glyph_index;
            assert(*internal_glyph_index < next_free_glyph_info, "");
            continue;
        }
        Font_Glyph_Info& glyph_info = result->glyph_infos[next_free_glyph_info];
        glyph_info.offset_from_line = ivec2(0);
        glyph_info.atlas_box = ibox2(ivec2(0, 0), ivec2(1, 1));
        freetype_to_our_mapping.insert(glyph_index, next_free_glyph_info);
        result->char_to_glyph_index[i] = next_free_glyph_info;
        next_free_glyph_info += 1;

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
        assert(face->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY, "");

        // Figure out offset ("Bearing")
        ivec2 size = ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows);
        glyph_info.offset_from_line = ivec2(
            face->glyph->bitmap_left, // Hinted 'bearing' in pixel
            face->glyph->bitmap_top - size.y
        );

        // Handle advance and min/max char size
        int advance = face->glyph->advance.x / 64; // Note: Glyph->advance is the hinted advance in 26.6 pixel
        if (face->glyph->advance.x % 64 != 0) {
            panic("Maybe this is fine");
            advance += 1;
        }
        result->char_size.x = math_maximum(result->char_size.x, advance);
        min_coord = math_minimum(min_coord, glyph_info.offset_from_line.y);
        result->char_size.y = math_maximum(result->char_size.y, glyph_info.offset_from_line.y + size.y);

        // Calculate position in glyph_atlas
        if (pos.x + size.x > buffer.size.x) { // Jump to next line_index if not enough space is in current line
            pos.x = 0;
            pos.y += max_char_height_in_line;
            max_char_height_in_line = 0;
        }
        if (pos.y + size.y > buffer.size.y) {
            logg("Texture atlas is too small");
            panic("");
            return nullptr;
        }
        glyph_info.atlas_box = ibox2(pos, pos + size);
        pos.x += size.x; // Advance cursor
        max_char_height_in_line = math_maximum(max_char_height_in_line, size.y);

        // Inpaint into buffer
        Texture_Buffer glyph_slice = texture_buffer_make_view(
            face->glyph->bitmap.buffer, size, face->glyph->bitmap.pitch
        );
        texture_buffer_inpaint_mirror_y(buffer, glyph_slice, glyph_info.atlas_box.min);
    }

    min_coord = math_minimum(0, min_coord); // I don't think it makes sense that this is ever positive
    for (int i = 0; i < next_free_glyph_info; i++) {
        Font_Glyph_Info& info = result->glyph_infos[i];
        info.offset_from_line.y -= min_coord;
    }
    result->char_size.y += -min_coord;
    result->base_line_offset = -min_coord;

    return result;
}

void Font_Renderer::finish_atlas() 
{
    texture = texture_create_empty(Texture_Type::RED_U8, buffer.size.x, buffer.size.y);
    texture_update_texture_data(texture, array_create_static(buffer.data, buffer.size.x * buffer.size.y), false);

    auto& predef = rendering_core.predefined;
    font_mesh = rendering_core_query_mesh(
        "font_mesh for fixed size text rendering",
        vertex_description_create({
            predef.position2D,
            predef.texture_coordinates,
            predef.rgba_int,
            predef.index,
        }),
        true
    );
}

void Font_Renderer::reset() {
    batch_end = 0;
    batch_start = 0;
}

void Font_Renderer::add_draw_call(Render_Pass* pass)
{
    if (batch_end <= batch_start) {
        return;
    }

    render_pass_draw_count(
        pass,
        rendering_core_query_shader("core/raster_font_new.glsl"),
        font_mesh,
        Mesh_Topology::TRIANGLES,
        { uniform_make("sampler", texture, sampling_mode_nearest()) },
        batch_start * 6,
        (batch_end - batch_start) * 6
    );
    batch_start = batch_end;
}

void Raster_Font::push_line(ivec2 pos, String text, vec3 color)
{
    if (text.size == 0) {
        return;
    }

    u32 color_int = 0;
    {
        color_int = color_int | (math_clamp((int)(color.x * 255.0f + .5f), 0, 255) << 24);
        color_int = color_int | (math_clamp((int)(color.y * 255.0f + .5f), 0, 255) << 16);
        color_int = color_int | (math_clamp((int)(color.z * 255.0f + .5f), 0, 255) << 8);
        color_int = color_int | (math_clamp((int)(   1.0f * 255.0f + .5f), 0, 255) << 0); // Alpha stays at zero for now
    }

    auto pixel_to_screen = [](ivec2 pos) {
        vec2 screen_size = vec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);
        return vec2(pos.x, pos.y) / screen_size * 2.0f - 1.0f;
    };
    auto pixel_to_uv = [&](ivec2 pos) {
        return vec2(pos.x, pos.y) / vec2(renderer->buffer.size.x, renderer->buffer.size.y);
    };

    // Allocate attributes in mesh
    const int vertexCount = text.size * 4;
    auto positions = mesh_push_attribute_slice(renderer->font_mesh, rendering_core.predefined.position2D, vertexCount);
    auto uvs = mesh_push_attribute_slice(renderer->font_mesh, rendering_core.predefined.texture_coordinates, vertexCount);
    auto colors = mesh_push_attribute_slice(renderer->font_mesh, rendering_core.predefined.rgba_int, vertexCount);
    auto indices = mesh_push_attribute_slice(renderer->font_mesh, rendering_core.predefined.index, text.size * 6);

    for (int i = 0; i < text.size; i++)
    {
        unsigned char character = text.characters[i];
        if (character >= char_to_glyph_index.size) continue;
        const Font_Glyph_Info& glyph_info = glyph_infos[char_to_glyph_index[character]];

        const ibox2& uv_box = glyph_info.atlas_box;
        ibox2 char_box;
        char_box.min = pos + char_size * ivec2(i, 0) + glyph_info.offset_from_line;
        char_box.max = char_box.min + (uv_box.max - uv_box.min);

        // Push back 4 vertices for each glyph
        positions[i * 4 + 0] = pixel_to_screen(ivec2(char_box.min.x, char_box.min.y));
        positions[i * 4 + 1] = pixel_to_screen(ivec2(char_box.max.x, char_box.min.y));
        positions[i * 4 + 2] = pixel_to_screen(ivec2(char_box.min.x, char_box.max.y));
        positions[i * 4 + 3] = pixel_to_screen(ivec2(char_box.max.x, char_box.max.y));
        uvs[i * 4 + 0] = pixel_to_uv(ivec2(uv_box.min.x, uv_box.min.y));
        uvs[i * 4 + 1] = pixel_to_uv(ivec2(uv_box.max.x, uv_box.min.y));
        uvs[i * 4 + 2] = pixel_to_uv(ivec2(uv_box.min.x, uv_box.max.y));
        uvs[i * 4 + 3] = pixel_to_uv(ivec2(uv_box.max.x, uv_box.max.y));
        colors[i * 4 + 0] = color_int;
        colors[i * 4 + 1] = color_int;
        colors[i * 4 + 2] = color_int;
        colors[i * 4 + 3] = color_int;

        // Push 6 indices for each character quad
        indices[i * 6 + 0] = (renderer->batch_end + i) * 4 + 0;
        indices[i * 6 + 1] = (renderer->batch_end + i) * 4 + 1;
        indices[i * 6 + 2] = (renderer->batch_end + i) * 4 + 2;
        indices[i * 6 + 3] = (renderer->batch_end + i) * 4 + 1;
        indices[i * 6 + 4] = (renderer->batch_end + i) * 4 + 3;
        indices[i * 6 + 5] = (renderer->batch_end + i) * 4 + 2;
    }
    renderer->batch_end += text.size;
}

void Raster_Font::add_draw_call(Render_Pass* pass) {
    renderer->add_draw_call(pass);
    renderer->batch_start = renderer->batch_end;
}
