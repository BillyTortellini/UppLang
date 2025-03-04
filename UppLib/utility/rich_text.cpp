#include "rich_text.hpp"

#include "../rendering/rendering_core.hpp"
#include "../rendering/text_renderer.hpp"
#include "../rendering/renderer_2d.hpp"

// Includes for C varargs
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

namespace Rich_Text
{
    // Style and Changes
    Text_Style text_style_make(vec3 text_color = vec3(1.0f), bool has_bg = false, vec3 bg_color = vec3(0.0f), bool has_underline = false, vec3 underline_color = vec3(0.0f)) {
        Text_Style style;
        style.text_color = text_color;
        style.has_bg = has_bg;
        style.bg_color = bg_color;
        style.has_underline = has_underline;
        style.underline_color = underline_color;
        return style;
    }

    bool color_equals(const vec3& a, const vec3& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    bool text_style_equals(const Text_Style& a, const Text_Style& b) {
        if (!(a.has_bg == b.has_bg && a.has_underline == b.has_underline && color_equals(a.text_color, b.text_color))) {
            return false;
        }
        if (a.has_bg && !color_equals(a.bg_color, b.bg_color)) {
            return false;
        }
        if (a.has_underline && !color_equals(a.underline_color, b.underline_color)) {
            return false;
        }
        return true;
    }



    // Rich Text
    Rich_Text create(vec3 default_text_color) 
    {
        Rich_Text result;
        result.lines = dynamic_array_create<Rich_Line>();
        result.style = text_style_make(default_text_color);
        result.default_text_color = default_text_color;
        return result;
    };

    void rich_line_destroy(Rich_Line* line) {
        dynamic_array_destroy(&line->style_changes);
        string_destroy(&line->text);
    }

    void destroy(Rich_Text* text) {
        dynamic_array_for_each(text->lines, rich_line_destroy);
        dynamic_array_destroy(&text->lines);
    }

    void reset(Rich_Text* text) {
        dynamic_array_for_each(text->lines, rich_line_destroy);
        dynamic_array_reset(&text->lines);
        text->style = text_style_make();
    }

    // Text-Pushing functions
    void add_line(Rich_Text* text, bool keep_style, int indentation) 
    {
        Rich_Line line;
        line.indentation = indentation;
        line.is_seperator = false;
        line.style_changes = dynamic_array_create<Style_Change>();
        line.text = string_create();
        line.default_style = keep_style ? text->style : text_style_make();
        line.has_bg = false;
        line.bg_color = vec3(0.0f);
        dynamic_array_push_back(&text->lines, line);

        text->style = line.default_style;
    }

    void set_line_bg(Rich_Text* text, vec3 color, int line_index) {
        if (text->lines.size == 0) return;
        if (line_index == -1) {
            line_index = text->lines.size - 1;
        }
        if (line_index < -1 || line_index >= text->lines.size) return;
        auto& line = text->lines[line_index];
        line.has_bg = true;
        line.bg_color = color;
    }

    void add_seperator_line(Rich_Text* text, bool skip_if_last_was_seperator_or_first) 
    {
        auto& lines = text->lines;

        if (lines.size == 0 && skip_if_last_was_seperator_or_first) return;
        auto& current = lines[lines.size - 1];
        if (current.is_seperator && skip_if_last_was_seperator_or_first) return;

        add_line(text);
        text->lines[lines.size - 1].is_seperator = true;
    }

    void append(Rich_Text* rich_text, String string) 
    {
        auto& lines = rich_text->lines;
        if (lines.size == 0) return;
        auto& line = lines[lines.size - 1];
        if (line.is_seperator) return;

        string_append_string(&line.text, &string);
    }

    void append(Rich_Text* rich_text, const char* msg) {
        append(rich_text, string_create_static(msg));
    }

    void append_character(Rich_Text* rich_text, char c) {
        auto& lines = rich_text->lines;
        if (lines.size == 0) return;
        auto& line = lines[lines.size - 1];
        if (line.is_seperator) return;

        string_append_character(&line.text, c);
    }

    // Copy-paste from string_append_formated
    void append_formated(Rich_Text* rich_text, const char* format, ...) 
    {
        auto& lines = rich_text->lines;
        if (lines.size == 0) return;
        auto& line = lines[lines.size - 1];
        if (line.is_seperator) return;
        String* string = &line.text;

        // Copy paste starts here
        va_list args;
        va_start(args, format);
        int message_length = vsnprintf(0, 0, format, args);
        string_reserve(string, string->size + message_length + 1);
        int ret_val = vsnprintf(string->characters + string->size, string->capacity - string->size, format, args);
        if (ret_val < 0) {
            panic("Shouldn't happen");
        }
        string->size = string->size + message_length;
        va_end(args);
    }

    String* start_line_manipulation(Rich_Text* rich_text) {
        auto& lines = rich_text->lines;
        if (lines.size == 0) return nullptr;
        auto& line = lines[lines.size - 1];
        if (line.is_seperator) return nullptr;

        return &line.text;
    }
    
    void stop_line_manipulation(Rich_Text* rich_text) {
        auto& lines = rich_text->lines;
        if (lines.size == 0) return;
        auto& line = lines[lines.size - 1];
        if (line.is_seperator) return;
    }



    // If end == -1, then this change goes from start to end of line
    // If start == -1, then take start as current text pos
    // If line_index == -1, then take current line
    void line_add_style_change(Rich_Text* text, Mark_Type mark_type, bool deactivate, vec3 color, int line_index, int start, int end)
    {
        if (line_index < 0) {
            line_index = text->lines.size - 1;
        }
        if (line_index < 0 || line_index >= text->lines.size) return;
        auto& line = text->lines[line_index];
        if (line.is_seperator) return;
        if (start == -1) { start = math_maximum(0, line.text.size); }
        if (end != -1 && end <= start) return;


        Style_Change change;
        change.type = mark_type;
        change.is_deactivate = deactivate;
        change.char_start = start < 0 ? line.text.size : start;
        change.char_end = end;
        change.color = color;
        dynamic_array_push_back(&line.style_changes, change);
    }

    void set_text_color(Rich_Text* text, vec3 color) {
        text->style.text_color = color;
        line_add_style_change(text, Mark_Type::TEXT_COLOR, false, color, -1, -1, -1);
    }

    void set_text_color(Rich_Text* text) {
        set_text_color(text, text->default_text_color);
    }

    void set_bg(Rich_Text* text, vec3 color)
    {
        text->style.has_bg = true;
        text->style.bg_color = color;
        line_add_style_change(text, Mark_Type::BACKGROUND_COLOR, false, color, -1, -1, -1);
    }

    void stop_bg(Rich_Text* text) {
        text->style.has_bg = false;
        line_add_style_change(text, Mark_Type::BACKGROUND_COLOR, true, vec3(1), -1, -1, -1);
    }

    void set_underline(Rich_Text* text, vec3 color)
    {
        text->style.has_underline = true;
        text->style.underline_color = color;
        line_add_style_change(text, Mark_Type::UNDERLINE, false, color, -1, -1, -1);
    }

    void stop_underline(Rich_Text* text) {
        text->style.has_underline = false;
        line_add_style_change(text, Mark_Type::UNDERLINE, true, vec3(1), -1, -1, -1);
    }
    
    // Update format functions
    void mark_line(Rich_Text* text, Mark_Type mark_type, vec3 color, int line, int char_start, int char_end) {
        line_add_style_change(text, mark_type, false, color, line, char_start, char_end);
    }

    void append_to_string(Rich_Text* text, String* string, int indentation_spaces)
    {
        for (int i = 0; i < text->lines.size; i++)
        {
            auto& line = text->lines[i];
            for (int i = 0; i < line.indentation * indentation_spaces; i += 1) {
                string_append_character(string, ' ');
            }
            string_append_string(string, &line.text);
            if (i != text->lines.size - 1) {
                string_append_character(string, '\n');
            }
        }
    }
};

namespace Text_Display
{
    Text_Display make(Rich_Text::Rich_Text* text, Renderer_2D* renderer_2D, Text_Renderer* text_renderer, vec2 char_size, int indentation_spaces) 
    {
        Text_Display display;
        display.text = text;
        display.renderer_2D = renderer_2D;
        display.text_renderer = text_renderer;
        display.indentation_spaces = indentation_spaces;
        display.char_size = char_size;

        // Initialize other members
        display.frame_anchor = Anchor::BOTTOM_LEFT;
        display.frame_size = vec2(0.0f);
        display.frame_pos = vec2(0.0f);

        // Set other options to 0
        display.padding = 0;
        display.draw_bg = false;
        display.draw_border = false;
        display.draw_block_outline = false;

        display.border_thickness = 0;
        display.block_outline_thickness = 0;

        return display;
    }

    void set_background_color(Text_Display* display, vec3 color) {
        display->draw_bg = true;
        display->bg_color = color;
    }

    void set_padding(Text_Display* display, int padding) {
        display->padding = padding;
    }

    void set_border(Text_Display* display, int border_thickness, vec3 color) {
        display->draw_border = true;
        display->border_thickness = border_thickness;
        display->border_color = color;
    }

    void set_block_outline(Text_Display* display, int thickness, vec3 color) {
        display->draw_block_outline = true;
        display->block_outline_thickness = thickness;
        display->outline_color = color;
    }

    void set_frame(Text_Display* display, vec2 position, Anchor anchor, vec2 size) {
        display->frame_pos = position;
        display->frame_anchor = anchor;
        display->frame_size = size;
    }

    vec2 get_char_position(Text_Display* display, int line, int char_index, Anchor anchor, bool with_indentation) 
    {
        Rich_Text::Rich_Text* rich_text = display->text;
        vec2 top_left = anchor_switch(display->frame_pos, display->frame_size, display->frame_anchor, Anchor::TOP_LEFT);
        vec2 char_pos = top_left + display->char_size * vec2(char_index, -line); // Lines go downwards
        char_pos = anchor_switch(char_pos, display->char_size, Anchor::TOP_LEFT, anchor);
        float padding_border = display->padding + display->border_thickness;
        char_pos = char_pos + vec2(padding_border, -padding_border);
        if (line >= 0 && line < rich_text->lines.size && with_indentation) {
            int indent = rich_text->lines[line].indentation;
            char_pos.x += indent * display->indentation_spaces * display->char_size.x;
        }
        return char_pos;
    }

    void draw_block_outline(Text_Display* display, int line_start, int line_end, int indentation)
    {
        if (indentation == 0) return;
        if (display->block_outline_thickness <= 0) return;
        int t = display->block_outline_thickness;
        vec2 start = get_char_position(display, line_start, 0, Anchor::TOP_LEFT, false);
        vec2 end = get_char_position(display, line_end, 0, Anchor::BOTTOM_LEFT, false);

        int min_x = start.x + (((indentation - 1) * display->indentation_spaces) * display->char_size.x);
        min_x += 4;
        // min_x -= (display->indentation_spaces * display->char_size.x) / 2.0f - display->block_outline_thickness;
        int max_y = start.y - display->char_size.y * 0.1f;
        int min_y = end.y   + display->char_size.y * 0.1f;
        int stub_length = (display->char_size.x * 2) / 3;

        // Vertical line
        renderer_2D_add_rectangle(display->renderer_2D, bounding_box_2_make_min_max(vec2(min_x, min_y + t), vec2(min_x + t, max_y)), display->outline_color);
        // Stub
        renderer_2D_add_rectangle(display->renderer_2D, bounding_box_2_make_min_max(vec2(min_x, min_y), vec2(min_x + t + stub_length, min_y + t)), display->outline_color);
    }

    // Returns position after this block has ended (Or on final line?)
    int draw_block_outlines_recursive(Text_Display* display, int line_index, int indentation)
    {
        auto& lines = display->text->lines;
        int block_start = line_index;
    
        // Find end of block
        int block_end = lines.size - 1;
        while (line_index < lines.size)
        {
            auto& line = lines[line_index];
            if (line.indentation > indentation) {
                line_index = draw_block_outlines_recursive(display, line_index, indentation + 1) + 1;
            }
            else if (line.indentation == indentation) {
                line_index += 1;
            }
            else { // line->indentation < indentation
                block_end = line_index - 1;
                break;
            }
        }
    
        draw_block_outline(display, block_start, block_end, indentation);
        return block_end;
    }

    struct Style_Change_Index
    {
        int char_index;
        int change_index;
        bool is_end;
    };
    struct Style_Change_Index_Comparator {
        bool operator()(const Style_Change_Index a, const Style_Change_Index b) {
            if (a.char_index == b.char_index) {
                if (a.is_end && !b.is_end) return true;
                if (b.is_end && !a.is_end) return false;
            }
            return a.char_index < b.char_index;
        }
    };

    void render(Text_Display* display, Render_Pass* render_pass)
    {
        Rich_Text::Rich_Text* text = display->text;
        Renderer_2D* renderer_2D = display->renderer_2D;
        Text_Renderer* text_renderer = display->text_renderer;
        auto& char_size = display->char_size;

        // Draw border and background and block_outlines
        Bounding_Box2 bb = bounding_box_2_make_anchor(display->frame_pos, display->frame_size, display->frame_anchor);
        {
            int t = display->draw_border ? display->border_thickness : 0;
            // Draw bg
            if (display->draw_bg) {
                renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(bb.min + t, bb.max - t), display->bg_color);
            }
            // Draw border
            if (display->draw_border && t > 0) {
                vec3 bc = display->border_color;
                renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(bb.min, vec2(bb.min.x + t, bb.max.y)), bc);
                renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(vec2(bb.max.x - t, bb.min.y), vec2(bb.max.x, bb.max.y)), bc);
                renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(vec2(bb.min.x + t, bb.min.y), vec2(bb.max.x - t, bb.min.y + t)), bc);
                renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(vec2(bb.min.x + t, bb.max.y - t), vec2(bb.max.x - t, bb.max.y)), bc);
            }
            if (display->draw_block_outline) {
                draw_block_outlines_recursive(display, 0, 0);
            }

            if (display->draw_bg || display->draw_border || display->draw_block_outline) {
                renderer_2D_draw(renderer_2D, render_pass);
            }
        }

        // Render line backgrounds
        auto& lines = text->lines;
        {
            int t = display->draw_border ? display->border_thickness : 0;
            bool has_line_bgs = false;
            for (int i = 0; i < lines.size; i++) 
            {
                auto& line = text->lines[i];
                if (!line.has_bg) {
                    continue;
                }

                has_line_bgs = true;
                vec2 min = vec2(
                    bb.min.x + t + line.indentation * display->char_size.x * display->indentation_spaces, 
                    bb.max.y - t - display->char_size.y * (i + 1)
                );
                vec2 max = vec2(bb.max.x - t, bb.max.y - t - display->char_size.y * i);
                renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(min, max), line.bg_color);
            }
        }

        // Render lines
        int max_char_count = (bb.max.x - bb.min.x) / display->char_size.x;
        Dynamic_Array<Style_Change_Index> change_indices = dynamic_array_create<Style_Change_Index>();
        Dynamic_Array<Rich_Text::Text_Style> style_stack = dynamic_array_create<Rich_Text::Text_Style>();
        SCOPE_EXIT(dynamic_array_destroy(&change_indices));
        SCOPE_EXIT(dynamic_array_destroy(&style_stack));
        for (int line_index = 0; line_index < lines.size; line_index++)
        {
            auto& line = lines[line_index];
            vec2 line_start_pos = get_char_position(display, line_index, 0, Anchor::BOTTOM_LEFT);
            Bounding_Box2 line_bb = bounding_box_2_make_min_max(vec2(bb.min.x, line_start_pos.y), vec2(bb.max.x, line_start_pos.y + char_size.y));

            if (line.is_seperator) {
                int t = 1;
                vec2 pos = get_char_position(display, line_index, 0, Anchor::CENTER_LEFT);
                auto sep_bb = bounding_box_2_union(line_bb, bounding_box_2_make_min_max(vec2(bb.min.x + display->border_thickness, (pos.y)), vec2(bb.max.x - display->border_thickness, pos.y + t)));
                if (sep_bb.available) {
                    renderer_2D_add_rectangle(renderer_2D, sep_bb.value, display->border_color * 0.8f);
                }
                continue;
            }

            auto render_substring_in_style = [&](int start_char, int end_char, Rich_Text::Text_Style style)
            {
                // Skip if range is empty
                if (end_char <= start_char) return;
                vec2 text_start_pos = get_char_position(display, line_index, start_char, Anchor::BOTTOM_LEFT);

                // Render background and underline
                vec2 min = text_start_pos;
                vec2 max = min + char_size * vec2(end_char - start_char, 1);
                max.x = math_minimum(max.x, line_bb.max.x);
                if (max.x - min.x >= 0.5f)
                {
                    if (style.has_underline) {
                        renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(min, vec2(max.x, min.y + 2)), style.underline_color);
                        min.y += 2; // Background should not draw over underline
                    }

                    if (style.has_bg) {
                        renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(min, max), style.bg_color);
                    }
                }

                // Draw text
                String substring = string_create_substring_static(&line.text, start_char, end_char);
                if (substring.size == 0) return;
                text_renderer_add_text(
                    text_renderer, substring, text_start_pos, Anchor::BOTTOM_LEFT, display->char_size, style.text_color, optional_make_success(line_bb)
                );
            };

            // Generate and sort change-indices
            dynamic_array_reset(&change_indices);
            dynamic_array_reset(&style_stack);
            for (int i = 0; i < line.style_changes.size; i++) 
            {
                auto& change = line.style_changes[i];
                if (change.char_end == -1) {
                    change.char_end = line.text.size;
                }
                if (change.char_end <= change.char_start) continue;

                Style_Change_Index index;
                index.char_index = change.char_start;
                index.change_index = i;
                index.is_end = false;
                dynamic_array_push_back(&change_indices, index);

                index.char_index = change.char_end;
                index.is_end = true;
                dynamic_array_push_back(&change_indices, index);
            }
            dynamic_array_stable_sort(&change_indices, Style_Change_Index_Comparator());

            Rich_Text::Text_Style style = line.default_style;
            int last_change_index = 0;
            for (int j = 0; j < change_indices.size; j++)
            {
                auto& change_index = change_indices[j];
                auto& change = line.style_changes[change_index.change_index];

                // Render line up to this point in old style
                render_substring_in_style(last_change_index, change_index.char_index, style);
                last_change_index = change_index.char_index;

                if (change_index.is_end) {
                    style = style_stack[style_stack.size - 1];
                    style_stack.size -= 1;
                    continue;
                }
                else {
                    dynamic_array_push_back(&style_stack, style);
                }

                // Apply
                switch (change.type)
                {
                case Rich_Text::Mark_Type::BACKGROUND_COLOR: {
                    style.has_bg = !change.is_deactivate;
                    style.bg_color = change.color;
                    break;
                }
                case Rich_Text::Mark_Type::UNDERLINE: {
                    style.has_underline = !change.is_deactivate;
                    style.underline_color = change.color;
                    break;
                }
                case Rich_Text::Mark_Type::TEXT_COLOR: {
                    if (change.is_deactivate) {
                        style.text_color = line.default_style.text_color;
                    }
                    else {
                        style.text_color = change.color;
                    }
                    break;
                }
				default: panic("");
                }
            }
            // Render end of line (From last style change to end of line)
            render_substring_in_style(last_change_index, line.text.size, style);
        }

        renderer_2D_draw(renderer_2D, render_pass);
        text_renderer_draw(text_renderer, render_pass);
    }
};

