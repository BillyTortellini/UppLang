#pragma once

#include "../math/vectors.hpp"
#include "../datastructures/string.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../rendering/basic2D.hpp"

struct Text_Renderer;
struct Renderer_2D;
struct Render_Pass;

namespace Rich_Text
{
    enum class Mark_Type
    {
        TEXT_COLOR,
        BACKGROUND_COLOR,
        UNDERLINE
    };

    struct Text_Style
    {
        vec3 text_color;
        vec3 bg_color;
        vec3 underline_color;
        bool has_bg;
        bool has_underline;
    };

    struct Style_Change
    {
        int char_start;
        Text_Style style;
    };

    struct Rich_Line 
    {
        String text;
        Dynamic_Array<Style_Change> style_changes;
        Text_Style default_style; 
        int indentation;
        bool is_seperator; // E.g. just a blank ---- line
        bool has_bg;
        vec3 bg_color;
    };

    struct Rich_Text
    {
        Dynamic_Array<Rich_Line> lines;
        int max_line_char_count;
        Text_Style style;
        vec3 default_text_color;
    };

    Rich_Text create(vec3 default_text_color);
    void destroy(Rich_Text* text);
    void reset(Rich_Text* text);

    void add_line(Rich_Text* text, bool keep_style = false, int indentation = 0);
    void set_line_bg(Rich_Text* text, vec3 color, int line_index = -1);
    void add_seperator_line(Rich_Text* text, bool skip_if_last_was_seperator_or_first = true);
    void append(Rich_Text* rich_text, String string);
    void append(Rich_Text* rich_text, const char* msg);
    void append_character(Rich_Text* rich_text, char c);
    void append_formated(Rich_Text* rich_text, const char* format, ...);

    String* start_line_manipulation(Rich_Text* rich_text);
    void stop_line_manipulation(Rich_Text* rich_text);

    void set_text_color(Rich_Text* text, vec3 color);
    void set_text_color(Rich_Text* text); // Resets to default text-color
    void set_bg(Rich_Text* text, vec3 color);
    void stop_bg(Rich_Text* text);
    void set_underline(Rich_Text* text, vec3 color);
    void stop_underline(Rich_Text* text);

    void line_set_underline_range(Rich_Text* text, vec3 color, int line, int char_start, int char_end);
    void line_set_bg_color_range(Rich_Text* text, vec3 color, int line, int char_start, int char_end);
    void line_set_text_color_range(Rich_Text* text, vec3 color, int line, int char_start, int char_end);
    void mark_line(Rich_Text* text, Mark_Type mark_type, vec3 color, int line, int char_start, int char_end);

    void append_to_string(Rich_Text* text, String* string, int indentation_spaces);
};

namespace Text_Display
{
    struct Text_Display
    {
        Rich_Text::Rich_Text* text;

        // Render info
        Renderer_2D* renderer_2D;
        Text_Renderer* text_renderer;
        vec2 char_size; // Note: this is to pixel aligned, so it cannot be used as text-renderer height without conversion
        int indentation_spaces;

        // Render_Position
        vec2 frame_size;
        vec2 frame_pos;
        Anchor frame_anchor;

        // Decorations
        int padding; // In pixels

        bool draw_border;
        int border_thickness; // In pixels 
        vec3 border_color;

        bool draw_bg;
        vec3 bg_color;

        bool draw_block_outline;
        int block_outline_thickness;
        vec3 outline_color;
    };

    Text_Display make(Rich_Text::Rich_Text* text, Renderer_2D* renderer_2D, Text_Renderer* text_renderer, vec2 char_size, int indentation_spaces);
    void set_background_color(Text_Display* display, vec3 color);
    void set_padding(Text_Display* display, int padding);
    void set_border(Text_Display* display, int border_thickness, vec3 color);
    void set_block_outline(Text_Display* display, int thickness, vec3 color);

    void set_frame(Text_Display* display, vec2 position, Anchor anchor, vec2 size);
    vec2 get_char_position(Text_Display* display, int line, int char_index, Anchor anchor, bool with_indentation = true);
    void render(Text_Display* display, Render_Pass* render_pass);
};