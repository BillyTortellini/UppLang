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
        code == Style_Code::TEXT_COLOR ||
        code == Style_Code::BACKGROUND_COLOR ||
        code == Style_Code::UNDERLINE;
}

void string_style_add_code(String* str, Style_Code code, Syntax_Color color)
{
    string_append_character(str, '\0');
    string_append_character(str, (char)code);
    if (style_code_has_color(code)) {
        string_append_character(str, (char)color);
    }
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
    char c, Syntax_Color text_color = Syntax_Color::TEXT, Syntax_Color bg_color = Syntax_Color::NONE, Syntax_Color underline_color = Syntax_Color::NONE)
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

struct Text_Style
{
    Syntax_Color text;
    Syntax_Color background;
    Syntax_Color underline;
};

Text_Style text_style_make(Syntax_Color text, Syntax_Color background, Syntax_Color underline) {
    Text_Style result;
    result.text = text;
    result.background = background;
    result.underline = underline;
    return result;
}

void Rich_Text_Area::fill_from_string(String string, Syntax_Color default_text, Syntax_Color default_bg, Syntax_Color default_underline)
{
    auto checkpoint = arena->make_checkpoint();
    SCOPE_EXIT(checkpoint.rewind());

    DynArray<Text_Style> style_stack = DynArray<Text_Style>::create(arena);
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
            Syntax_Color* target = nullptr;
            switch (code)
            {
            case Style_Code::TEXT_COLOR: target = &style.text; break;
            case Style_Code::UNDERLINE:  target = &style.underline; break;
            case Style_Code::BACKGROUND_COLOR: target = &style.background; break;
            case Style_Code::PUSH_STYLE: {
                style_stack.push_back(style);
                break;
            }
            case Style_Code::POP_STYLE: 
            {
                if (style_stack.size > 0) {
                    style = style_stack.last();
                    style_stack.size -= 1;
                }
                else {
                    // Shouldn't happen, but we reset to default style for now
                    style = text_style_make(default_text, default_bg, default_underline);
                }
                break;
            }
            default: panic("Encountered invalid style sequence in string");
            }

            if (target != nullptr && i + 2 < string.size) {
                *target = (Syntax_Color)string[i + 2];
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

void Rich_Text_Area::mark(ivec2 pos, int length, Mark_Type type, Syntax_Color color)
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



// void draw_block_outline(int line_start, int line_end, int indentation, Text_Display* display, Renderer_2D* renderer_2D)
// {
//     if (indentation == 0) return;
//     if (display->block_outline_thickness <= 0) return;
//     int t = display->block_outline_thickness;
//     vec2 start = display->get_char_position(ivec2(line_start display, line_start, 0, Anchor::TOP_LEFT, false);
//     vec2 end = get_char_position(display, line_end, 0, Anchor::BOTTOM_LEFT, false);
// 
//     int min_x = start.x + (((indentation - 1) * display->indentation_spaces) * display->char_size.x);
//     min_x += 4;
//     // min_x -= (display->indentation_spaces * display->char_size.x) / 2.0f - display->block_outline_thickness;
//     int max_y = start.y - display->char_size.y * 0.1f;
//     int min_y = end.y   + display->char_size.y * 0.1f;
//     int stub_length = (display->char_size.x * 2) / 3;
// 
//     // Vertical line
//     renderer_2D_add_rectangle(display->renderer_2D, bounding_box_2_make_min_max(vec2(min_x, min_y + t), vec2(min_x + t, max_y)), display->outline_color);
//     // Stub
//     renderer_2D_add_rectangle(display->renderer_2D, bounding_box_2_make_min_max(vec2(min_x, min_y), vec2(min_x + t + stub_length, min_y + t)), display->outline_color);
// }
// 
// // Returns position after this block has ended (Or on final line?)
// int draw_block_outlines_recursive(Text_Display* display, int line_index, int indentation)
// {
//     auto& lines = display->text->lines;
//     int block_start = line_index;
// 
//     // Find end of block
//     int block_end = lines.size - 1;
//     while (line_index < lines.size)
//     {
//         auto& line = lines[line_index];
//         if (line.indentation > indentation) {
//             line_index = draw_block_outlines_recursive(display, line_index, indentation + 1) + 1;
//         }
//         else if (line.indentation == indentation) {
//             line_index += 1;
//         }
//         else { // line->indentation < indentation
//             block_end = line_index - 1;
//             break;
//         }
//     }
// 
//     draw_block_outline(display, block_start, block_end, indentation);
//     return block_end;
// }


void Rich_Text_Area::render(ibox2 box, Raster_Font* font, Renderer_2D* renderer_2D, Render_Pass* render_pass)
{
    String text_buffer = string_create_empty(64);
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
        if (rich_char.background_color != Syntax_Color::NONE) {
            ibox2 bg_box = rect_box;
            if (rich_char.underline_color != Syntax_Color::NONE) {
                bg_box.min.y += UNDERLINE_THICKNESS;
            }
            bg_box = bg_box.intersect(box);
            if (!bg_box.is_empty()) {
                renderer_2D_add_rectangle(
                    renderer_2D, 
                    bounding_box_2_make_min_max(vec2(bg_box.min.x, bg_box.min.y), vec2(bg_box.max.x, bg_box.max.y)), 
                    syntax_color_to_vec3(rich_char.background_color)
                );
            }
        }

        // Draw underline
        if (rich_char.underline_color != Syntax_Color::NONE) {
            ibox2 underline_box = rect_box;
            underline_box.max.y = underline_box.min.y + UNDERLINE_THICKNESS;
            underline_box = underline_box.intersect(box);
            if (!underline_box.is_empty()) {
                renderer_2D_add_rectangle(
                    renderer_2D,
                    bounding_box_2_make_min_max(vec2(underline_box.min.x, underline_box.min.y), vec2(underline_box.max.x, underline_box.max.y)), 
                    syntax_color_to_vec3(rich_char.underline_color)
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
        font->push_line(min, text_buffer, syntax_color_to_vec3(rich_char.text_color));
    };

    for (int line = 0; line < size.y; line += 1)
    {
        Rich_Char& start_char = buffer[line * size.x];
        Syntax_Color background_color = start_char.background_color;
        Syntax_Color underline_color = start_char.underline_color;

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
        Syntax_Color text_color = start_char.text_color;
        x = 0;
        same_style_start = -1;
        string_reset(&text_buffer);
        while (x < size.x)
        {
            SCOPE_EXIT(x += 1);
            Rich_Char& current = buffer[line * size.x + x];
            bool is_whitespace = 
                current.character == '\0' || current.character == '\t' || current.character == ' ' ||
                current.text_color == Syntax_Color::NONE;
            
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
