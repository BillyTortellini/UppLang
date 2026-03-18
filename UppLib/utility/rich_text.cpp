#include "rich_text.hpp"

#include "../datastructures/allocators.hpp"
#include "../rendering/rendering_core.hpp"
#include "../rendering/text_renderer.hpp"
#include "../rendering/renderer_2d.hpp"
#include "../rendering/font_renderer.hpp"
#include "../utility/character_info.hpp"

// Includes for C varargs
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

bool style_code_has_color(Style_Code code)
{
    return 
        code == Style_Code::PUSH_TEXT_COLOR ||
        code == Style_Code::PUSH_BACKGROUND_COLOR ||
        code == Style_Code::PUSH_UNDERLINE;
}

void string_style_push(String* str, Mark_Type type, Palette_Color color)
{
    string_append_character(str, '\0');
    string_append_character(str, (char)type);
    string_append_character(str, (char)color);
}

void string_style_push(String* str, Mark_Type type, Syntax_Color color)
{
    string_style_push(str, type, syntax_color_to_palette_color(color));
}

void string_style_pop(String* str)
{
    string_append_character(str, '\0');
    string_append_character(str, (char)Style_Code::POP);
}

void string_style_remove_codes(String* str)
{
    int next = 0;
    for (int i = 0; i < str->size; i++)
    {
        char c = (*str)[i];
        if (c == '\0' && i + 1 < str->size) {
            Style_Code code = (Style_Code)str->characters[i+1];
            i += 1 + (style_code_has_color(code) ? 1 : 0); // - 1 because continue also increments i
            continue;
        }
        str->characters[next] = c;
        next += 1;
    }
    str->size = next;
}

ivec2 string_style_calculated_2D_size(String string)
{
    ivec2 result = ivec2(0, 1);
    int x = 0;
    for (int i = 0; i < string.size; i++)
    {
        char c = string[i];

        // Skip escape codes
        if (c == '\0' && i + 1 < string.size) {
            Style_Code code = (Style_Code)string.characters[i+1];
            i += 1 + (style_code_has_color(code) ? 1 : 0); // - 1 because continue also increments i
            continue;
        }

        if (c == '\n') {
            result.y += 1;
            result.x = math_maximum(result.x, x);
            x = 0;
            continue;
        }
        x += 1;
    }

    result.x = math_maximum(result.x, x);
    return result;
}

// RICH TEXT AREA
Rich_Char rich_char_make(
    char c, Palette_Color text_color = Palette_Color::WHITE, Palette_Color bg_color = Palette_Color::NONE, Palette_Color underline_color = Palette_Color::NONE)
{
    Rich_Char result;
    result.character = c;
    result.text_color = text_color;
    result.background_color = bg_color;
    result.underline_color = underline_color;
    return result;
}

Rich_Text_Area Rich_Text_Area::create(Arena* arena, ivec2 size)
{
    Rich_Text_Area result;
    result.arena = arena;
    result.size = size;
    result.buffer = array_create<Rich_Char>(size.x * size.y);
    memory_set_bytes(result.buffer.data, result.buffer.size * sizeof(Rich_Char), 0);
    return result;
}

struct Style_Change
{
    Mark_Type type;
    Palette_Color color;
};

Style_Change style_change_make(Mark_Type type, Palette_Color color) {
    Style_Change result;
    result.type = type;
    result.color = color;
    return result;
}

struct Text_Style
{
    Palette_Color text;
    Palette_Color background;
    Palette_Color underline;
};

Text_Style text_style_make(Palette_Color text, Palette_Color background, Palette_Color underline) {
    Text_Style result;
    result.text = text;
    result.background = background;
    result.underline = underline;
    return result;
}

void Rich_Text_Area::fill_from_string(String string, Palette_Color default_text, Palette_Color default_bg, Palette_Color default_underline)
{
    auto checkpoint = arena->make_checkpoint();
    SCOPE_EXIT(checkpoint.rewind());

    DynArray<Style_Change> style_stack = DynArray<Style_Change>::create(arena);
    Text_Style style = text_style_make(default_text, default_bg, default_underline);
    ivec2 pos = ivec2(0, 0);

    for (int i = 0; i < string.size && pos.y < size.y; i++)
    {
        char c = string[i];

        // Handle new-line
        if (c == '\n') {
            pos.y += 1;
            pos.x = 0;
            continue;
        }

        // Handle style-codes
        if (c == '\0' && i + 1 < string.size) 
        {
            Style_Code code = (Style_Code)string[i + 1];
            Palette_Color* target = nullptr;
            switch (code)
            {
            case Style_Code::PUSH_TEXT_COLOR: {
                style_stack.push_back(style_change_make(Mark_Type::TEXT_COLOR, style.text));
                target = &style.text;
                break;
            }
            case Style_Code::PUSH_BACKGROUND_COLOR: {
                style_stack.push_back(style_change_make(Mark_Type::BACKGROUND_COLOR, style.background));
                target = &style.background;
                break;
            }
            case Style_Code::PUSH_UNDERLINE: {
                style_stack.push_back(style_change_make(Mark_Type::UNDERLINE, style.underline));
                target = &style.underline;
                break;
            }
            case Style_Code::POP: 
            {
                if (style_stack.size == 0) {
                    break;
                }

                auto change = style_stack.last();
                style_stack.size -= 1;
                switch (change.type)
                {
                case Mark_Type::TEXT_COLOR: style.text = change.color; break;
                case Mark_Type::BACKGROUND_COLOR: style.background = change.color; break;
                case Mark_Type::UNDERLINE: style.underline = change.color; break;
                default: panic("");
                }
                break;
            }
            default: panic("Encountered invalid style sequence in string");
            }

            if (target != nullptr && i + 2 < string.size) {
                *target = (Palette_Color)string[i + 2];
                i += 1;
            }
            i += 1;
            continue;
        }

        if (pos.x >= size.x) {
            continue;
        }
        *get_char(pos) = rich_char_make(c, style.text, style.background, style.underline);
        pos.x += 1;
    }
}

void Rich_Text_Area::fill_from_string(String string, Syntax_Color default_text, Syntax_Color default_bg, Syntax_Color default_underline)
{
    fill_from_string(
        string, 
        syntax_color_to_palette_color(default_text), 
        syntax_color_to_palette_color(default_bg), 
        syntax_color_to_palette_color(default_underline)
    );
}

Rich_Char* Rich_Text_Area::get_char(ivec2 pos)
{
    if (pos.x < 0 || pos.x >= size.x || pos.y < 0 || pos.y >= size.y) return &dummy;
    return &buffer[pos.x + size.x * pos.y];
}

void Rich_Text_Area::set_text(ivec2 pos, String string)
{
    for (int i = 0; i < string.size; i += 1) {
        get_char(ivec2(pos.x + i, pos.y))->character = string[i];
    }
}

void Rich_Text_Area::mark(ivec2 pos, int length, Mark_Type type, Palette_Color color)
{
    for (int i = 0; i < length; i += 1)
    {
        Rich_Char* target = get_char(ivec2(pos.x + i, pos.y));
        switch (type)
        {
        case Mark_Type::BACKGROUND_COLOR: target->background_color = color; break;
        case Mark_Type::TEXT_COLOR: target->text_color = color; break;
        case Mark_Type::UNDERLINE: target->underline_color = color; break;
        default: panic("");
        }
    }
}

void Rich_Text_Area::mark(ivec2 pos, int length, Mark_Type type, Syntax_Color color) {
    mark(pos, length, type, syntax_color_to_palette_color(color));
}

void Rich_Text_Area::render(ibox2 box, Raster_Font* font, Renderer_2D* renderer_2D, Render_Pass* render_pass)
{
    String text_buffer = string_create(64);
    SCOPE_EXIT(string_destroy(&text_buffer));

    ivec2 char_size = font->char_size;

    auto helper_draw_background = [&](int line, int start, int end)
    {
        if (end <= start) return;

        const int UNDERLINE_THICKNESS = 2;

        // Figure out bounding-box on screen
        ibox2 rect_box;
        rect_box.min.x = box.min.x + char_size.x * start;
        rect_box.max.x = box.min.x + char_size.x * end;
        rect_box.min.y = box.max.y - char_size.y * (line + 1);
        rect_box.max.y = box.max.y - char_size.y * (line);

        // Draw background
        Rich_Char& rich_char = buffer[line * size.x + start];
        if (rich_char.background_color != Palette_Color::NONE) {
            ibox2 bg_box = rect_box;
            if (rich_char.underline_color != Palette_Color::NONE) {
                bg_box.min.y += UNDERLINE_THICKNESS;
            }
            bg_box = bg_box.intersect(box);
            if (!bg_box.is_empty()) {
                renderer_2D_add_rectangle(
                    renderer_2D, 
                    box2_make_min_max(vec2(bg_box.min.x, bg_box.min.y), vec2(bg_box.max.x, bg_box.max.y)), 
                    palette_color_to_vec3(rich_char.background_color)
                );
            }
        }

        // Draw underline
        if (rich_char.underline_color != Palette_Color::NONE) {
            ibox2 underline_box = rect_box;
            underline_box.max.y = underline_box.min.y + UNDERLINE_THICKNESS;
            underline_box = underline_box.intersect(box);
            if (!underline_box.is_empty()) {
                renderer_2D_add_rectangle(
                    renderer_2D,
                    box2_make_min_max(vec2(underline_box.min.x, underline_box.min.y), vec2(underline_box.max.x, underline_box.max.y)), 
                    palette_color_to_vec3(rich_char.underline_color)
                );
            }
        }
    };

    auto helper_draw_text = [&](int line, int start, int end)
    {
        if (end <= start) return;
        assert(line >= 0 && line < size.y && start <= size.x && end <= size.x, "");

        // Create string from buffer
        string_reset(&text_buffer);
        for (int i = start; i < end; i += 1) {
            string_append_character(
                &text_buffer, buffer[line * size.x + i].character
            );
        }

        ivec2 min = ivec2(box.min.x, box.max.y) + char_size * ivec2(start, -line - 1);
        Rich_Char& rich_char = buffer[line * size.x + start];
        font->push_line(min, text_buffer, palette_color_to_vec3(rich_char.text_color));
    };

    for (int line = 0; line < size.y; line += 1)
    {
        Rich_Char& start_char = buffer[line * size.x];
        Palette_Color background_color = start_char.background_color;
        Palette_Color underline_color = start_char.underline_color;

        // Draw background
        int x = 0;
        int same_style_start = 0;
        while (x < size.x)
        {
            SCOPE_EXIT(x += 1);
            Rich_Char& current = buffer[line * size.x + x];
            if (current.background_color != background_color || current.underline_color != underline_color) {
                helper_draw_background(line, same_style_start, x);
                same_style_start = x;
                background_color = current.background_color;
                underline_color = current.underline_color;
            }
        }
        helper_draw_background(line, same_style_start, size.x);

        // Draw text
        Palette_Color text_color = start_char.text_color;
        x = 0;
        same_style_start = -1;
        string_reset(&text_buffer);
        while (x < size.x)
        {
            SCOPE_EXIT(x += 1);
            Rich_Char& current = buffer[line * size.x + x];
            bool is_whitespace = 
                current.character == '\0' || current.character == '\t' || current.character == ' ' ||
                current.text_color == Palette_Color::NONE;
            
            if (same_style_start == -1)
            {
                if (is_whitespace) {
                    continue;
                }
                same_style_start = x;
                text_color = current.text_color;
                continue;
            }

            if (is_whitespace) 
            {
                helper_draw_text(line, same_style_start, x);
                same_style_start = -1;
                text_color = current.text_color;
            }
            else if (current.text_color != text_color) {
                helper_draw_text(line, same_style_start, x);
                same_style_start = x;
                text_color = current.text_color;
            }
        }
        if (same_style_start != -1) {
            helper_draw_text(line, same_style_start, size.x);
        }
    }

    renderer_2D_draw(renderer_2D, render_pass);
    font->add_draw_call(render_pass);
}

