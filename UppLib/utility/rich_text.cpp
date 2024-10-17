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

    Style_Change style_change_make(int char_start, Text_Style style) {
        return { char_start, style };
    }
    


    // Rich Text
    Rich_Text create(vec3 default_text_color) 
    {
        Rich_Text result;
        result.lines = dynamic_array_create<Rich_Line>();
        result.max_line_char_count = 0;
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
        text->max_line_char_count = 0;
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
        dynamic_array_push_back(&text->lines, line);

        text->style = line.default_style;
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
        rich_text->max_line_char_count = math_maximum(rich_text->max_line_char_count, line.text.size);
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
        rich_text->max_line_char_count = math_maximum(rich_text->max_line_char_count, line.text.size);
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

        rich_text->max_line_char_count = math_maximum(rich_text->max_line_char_count, line.text.size);
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

        rich_text->max_line_char_count = math_maximum(rich_text->max_line_char_count, line.text.size);
    }

    void push_current_style(Rich_Text* text) 
    {
        auto& lines = text->lines;
        if (lines.size == 0) return;
        auto& line = lines[lines.size - 1];

        // Add new change if non-exist currently
        if (line.style_changes.size == 0) 
        {
            if (line.text.size == 0) {
                line.default_style = text->style;
                return;
            }
            dynamic_array_push_back(&line.style_changes, style_change_make(line.text.size, text->style));
            return;
        }

        auto& last_change = line.style_changes[line.style_changes.size - 1];
        if (last_change.char_start == line.text.size) {
            last_change.style = text->style;
            return;
        }

        if (text_style_equals(last_change.style, text->style)) {
            return;
        }
        dynamic_array_push_back(&line.style_changes, style_change_make(line.text.size, text->style));
    }

    void set_text_color(Rich_Text* text, vec3 color) {
        text->style.text_color = color;
        push_current_style(text);
    }

    void set_text_color(Rich_Text* text) {
        set_text_color(text, text->default_text_color);
    }

    void set_bg(Rich_Text* text, vec3 color)
    {
        text->style.has_bg = true;
        text->style.bg_color = color;
        push_current_style(text);
    }

    void stop_bg(Rich_Text* text) {
        text->style.has_bg = false;
        push_current_style(text);
    }

    void set_underline(Rich_Text* text, vec3 color)
    {
        text->style.has_underline = true;
        text->style.underline_color = color;
        push_current_style(text);
    }

    void stop_underline(Rich_Text* text) {
        text->style.has_underline = false;
        push_current_style(text);
    }


    
    // Update format functions
    typedef void (*update_style_fn) (Text_Style& style, void* userdata);
    void line_update_style_range(Rich_Text* text, update_style_fn update_fn, void* userdata, int line_index, int char_start, int char_end)
    {
        // Sanitize indices
        if (line_index < 0 || line_index >= text->lines.size) return;
        if (char_start >= char_end) return;
        auto& line = text->lines[line_index];
        if (line.text.size == 0 || line.is_seperator) return;
        char_end = math_clamp(char_end, 0, line.text.size);
        char_start = math_clamp(char_start, 0, math_maximum(0, char_end-1));
        if (char_start >= char_end) return;

        // Normalize line style changes (Style changes != 0, first style change has char_start 0)
        dynamic_array_insert_ordered(&line.style_changes, style_change_make(0, line.default_style), 0);

        // Helper
        auto find_last_change_index = [&](int char_index) -> int {
            int last_change_index = 0;
            for (int i = 0; i < line.style_changes.size; i++) {
                auto& change = line.style_changes[i];
                if (change.char_start <= char_start) {
                    last_change_index = i;
                }
                else {
                    break;
                }
            }
            return last_change_index;
        };

        // Insert changes at range start and end
        int last_change_before_start_index = find_last_change_index(char_start);
        Text_Style last_style = line.style_changes[last_change_before_start_index].style;
        dynamic_array_insert_ordered(&line.style_changes, style_change_make(char_start, last_style), last_change_before_start_index + 1);
        int last_change_before_end = find_last_change_index(char_end-1); // Last change at end of range
        dynamic_array_insert_ordered(&line.style_changes, style_change_make(char_end, last_style), last_change_before_end + 1);

        // Run update style function on all styles in range
        for (int i = 0; i < line.style_changes.size; i++) {
            auto& change = line.style_changes[i];
            if (change.char_start >= char_start && change.char_start < char_end) {
                update_fn(change.style, userdata);
            }
        }

        // Remove all duplicated styles that may have been generated
        for (int i = 0; i < line.style_changes.size; i++) 
        {
            auto& change = line.style_changes[i];
            Style_Change prev_change;
            if (i == 0) {
                prev_change = style_change_make(0, line.default_style);
            }
            else {
                prev_change = line.style_changes[i - 1];
            }

            // Remove previous change if both have same char_start
            if (prev_change.char_start == change.char_start && i != 0) {
                dynamic_array_remove_ordered(&line.style_changes, i - 1);
                i -= 1;
                continue;
            }

            // Remove change if previous change has same style
            if (text_style_equals(change.style, prev_change.style)) {
                dynamic_array_remove_ordered(&line.style_changes, i);
                i -= 1;
                continue;
            }
        }
    }

    void line_set_underline_range(Rich_Text* text, vec3 color, int line, int char_start, int char_end) {
        auto set_underline = [](Text_Style& style, void* userdata) {
            style.has_underline = true;
            style.underline_color = *(vec3*)userdata;
        };
        line_update_style_range(text, set_underline, (void*)&color, line, char_start, char_end);
    }

    void line_set_bg_color_range(Rich_Text* text, vec3 color, int line, int char_start, int char_end) {
        auto set_bg_color = [](Text_Style& style, void* userdata) {
            style.has_bg = true;
            style.bg_color = *(vec3*)userdata;
        };
        line_update_style_range(text, set_bg_color, (void*)&color, line, char_start, char_end);
    }

    void line_set_text_color_range(Rich_Text* text, vec3 color, int line, int char_start, int char_end) {
        auto set_text_color = [](Text_Style& style, void* userdata) {
            style.text_color = *(vec3*)userdata;
        };
        line_update_style_range(text, set_text_color, (void*)&color, line, char_start, char_end);
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

namespace Rich_Text_Renderer
{
    Rich_Text_Renderer make(Rich_Text::Rich_Text* text, Renderer_2D* renderer_2D, Text_Renderer* text_renderer, float text_height, int indentation_spaces) 
    {
        Rich_Text_Renderer renderer;
        renderer.text = text;
        renderer.renderer_2D = renderer_2D;
        renderer.text_renderer = text_renderer;
        renderer.indentation_spaces = indentation_spaces;

        // Calculate char size (Align char width to pixel size)
        {
            float width_to_height_ratio = text_renderer_get_char_width_to_height_ratio(text_renderer);
            float width = math_ceil(text_height * width_to_height_ratio); // Align to pixel size
            float height = math_ceil(width / width_to_height_ratio); // Also align to pixel size
            renderer.char_size = vec2(width, height);
        }

        // Set other options to 0
        renderer.bg_color = vec3(0.0f);
        renderer.border_color = vec3(0.0f);
        renderer.draw_bg_and_border = false;
        renderer.border_thickness = 0;
        renderer.padding = 0;
        renderer.frame_anchor = Anchor::BOTTOM_LEFT;
        renderer.frame_size = vec2(0.0f);
        renderer.frame_pos = vec2(0.0f);

        return renderer;
    }

    void set_decorations(Rich_Text_Renderer* renderer, int padding, int border_thickness, bool draw_bg_and_border, vec3 bg_color, vec3 border_color)
    {
        renderer->padding = padding;
        renderer->border_thickness = border_thickness;
        renderer->draw_bg_and_border = draw_bg_and_border;
        if (renderer->draw_bg_and_border) {
            renderer->bg_color = bg_color;
            renderer->border_color = border_color;
        }
    }

    void set_frame(Rich_Text_Renderer* renderer, vec2 position, Anchor anchor, vec2 size) {
        renderer->frame_pos = position;
        renderer->frame_anchor = anchor;
        renderer->frame_size = size;
    }

    vec2 get_char_position(Rich_Text_Renderer* renderer, int line, int char_index, Anchor anchor) 
    {
        Rich_Text::Rich_Text* rich_text = renderer->text;
        vec2 top_left = anchor_switch(renderer->frame_pos, renderer->frame_size, renderer->frame_anchor, Anchor::TOP_LEFT);
        vec2 char_pos = top_left + renderer->char_size * vec2(char_index, -line); // Lines go downwards
        char_pos = anchor_switch(char_pos, renderer->char_size, Anchor::TOP_LEFT, anchor);
        float padding_border = renderer->padding + renderer->border_thickness;
        char_pos = char_pos + vec2(padding_border, -padding_border);
        if (line >= 0 && line < rich_text->lines.size) {
            int indent = rich_text->lines[line].indentation;
            char_pos.x += indent * renderer->indentation_spaces * renderer->char_size.x;
        }
        return char_pos;
    }

    void render(Rich_Text_Renderer* renderer, Render_Pass* render_pass)
    {
        Rich_Text::Rich_Text* text = renderer->text;
        Renderer_2D* renderer_2D = renderer->renderer_2D;
        Text_Renderer* text_renderer = renderer->text_renderer;
        auto& char_size = renderer->char_size;

        Bounding_Box2 bb = bounding_box_2_make_anchor(renderer->frame_pos, renderer->frame_size, renderer->frame_anchor);
        if (renderer->draw_bg_and_border)
        {
            int t = renderer->border_thickness;
            // Draw bg
            renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(bb.min + t, bb.max - t), renderer->bg_color);
            // Draw border
            if (renderer->border_thickness > 0) {
                vec3 bc = renderer->border_color;
                renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(bb.min, vec2(bb.min.x + t, bb.max.y)), bc);
                renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(vec2(bb.max.x - t, bb.min.y), vec2(bb.max.x, bb.max.y)), bc);
                renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(vec2(bb.min.x + t, bb.min.y), vec2(bb.max.x - t, bb.min.y + t)), bc);
                renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(vec2(bb.min.x + t, bb.max.y - t), vec2(bb.max.x - t, bb.max.y)), bc);
            }
            renderer_2D_draw(renderer_2D, render_pass);
        }

        const float text_render_height = char_size.x / text_renderer_get_char_width_to_height_ratio(text_renderer);

        // Render lines
        auto& lines = text->lines;
        for (int line_index = 0; line_index < lines.size; line_index++)
        {
            auto& line = lines[line_index];
            if (line.is_seperator) {
                int t = 1;
                vec2 pos = get_char_position(renderer, line_index, 0, Anchor::CENTER_LEFT);
                renderer_2D_add_rectangle(renderer_2D,
                    bounding_box_2_make_min_max(vec2(bb.min.x + renderer->border_thickness, (pos.y)), vec2(bb.max.x - renderer->border_thickness, pos.y + t)),
                    renderer->border_color * 0.8f
                );
                continue;
            }

            vec2 line_start_pos = get_char_position(renderer, line_index, 0, Anchor::BOTTOM_LEFT);
            Bounding_Box2 line_bb = bounding_box_2_make_min_max(line_start_pos, line_start_pos + char_size * vec2(line.text.size, 1));
            for (int i = 0; i <= line.style_changes.size; i++)
            {
                // Get current and last change
                Rich_Text::Style_Change change;
                Rich_Text::Style_Change last_change;
                if (i < line.style_changes.size) {
                    change = line.style_changes[i];
                }
                else {
                    change = style_change_make(line.text.size, line.default_style);
                }
                if (i == 0) {
                    last_change = style_change_make(0, line.default_style);
                }
                else {
                    last_change = line.style_changes[i - 1];
                }

                // Skip if range is empty
                if (last_change.char_start == change.char_start) continue;
                vec2 text_start_pos = get_char_position(renderer, line_index, last_change.char_start, Anchor::BOTTOM_LEFT);

                // Render background and underline
                {
                    vec2 min = text_start_pos;
                    vec2 max = min + char_size * vec2(change.char_start - last_change.char_start, 1);
                    if (last_change.style.has_underline) {
                        renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(min, vec2(max.x, min.y + 2)), last_change.style.underline_color);
                        min.y += 2; // Background should not draw over underline
                    }

                    if (last_change.style.has_bg) {
                        renderer_2D_add_rectangle(renderer_2D, bounding_box_2_make_min_max(min, max), last_change.style.bg_color);
                    }
                }

                // Draw text
                String substring = string_create_substring_static(&line.text, last_change.char_start, change.char_start);
                text_renderer_add_text(
                    text_renderer, substring, text_start_pos, Anchor::BOTTOM_LEFT, text_render_height, last_change.style.text_color, optional_make_success(line_bb)
                );
            }
        }

        renderer_2D_draw(renderer_2D, render_pass);
        text_renderer_draw(text_renderer, render_pass);
    }
};

