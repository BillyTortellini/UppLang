#pragma once

#include "../math/vectors.hpp"
#include "../utility/file_listener.hpp"
#include "glyph_atlas.hpp"
#include "../utility/bounding_box.hpp"

struct Character_Position
{
    BoundingBox2 bounding_box;
    GlyphInformation* glyph_info;
    vec3 color;
};

struct TextLayout
{
    DynamicArray<Character_Position> character_positions;
    vec2 size;
    float relative_height;
};

TextLayout text_layout_create();
void text_layout_destroy(TextLayout* info);

struct TextRenderer;
struct OpenGLState;
struct FileListener;
struct String;
TextRenderer* text_renderer_create_from_font_atlas_file(
    OpenGLState* state,
    FileListener* listener,
    const char* font_filepath,
    int window_width,
    int window_height
);
void text_renderer_destroy(TextRenderer* renderer);

float text_renderer_calculate_text_width(TextRenderer* renderer, int char_count, float relative_height);
TextLayout* text_renderer_calculate_text_layout(
    TextRenderer* renderer,
    String* text,
    float relative_height,
    float line_gap_percent);
void text_renderer_add_text_from_layout(
    TextRenderer* renderer,
    TextLayout* text_layout,
    vec2 position
);
void text_renderer_add_text(
    TextRenderer* renderer,
    String* text,
    vec2 position,
    float relative_height,
    float line_gap_percent
);
float text_renderer_get_cursor_advance(TextRenderer* renderer, float relative_height);
void text_renderer_set_color(TextRenderer* renderer, vec3 color);
void text_renderer_render(TextRenderer* renderer, OpenGLState* state);
void text_renderer_update_window_size(TextRenderer* renderer, int new_width, int new_height);
Texture* text_renderer_get_texture(TextRenderer* renderer);

/*
    TODO:
        - Create a text editor with current functions

    What do i want to do with this:
        - Create a text editor (On a per line basis) (On a per line basis)
        - Gui elements (Text input, number inputs, labels, buttons, drop-down stuff)

    In the text editor i also want to be able to highlight text (Put background colors behind words, ...)
    Also scrolling needs to be a thing

    What do i want when I render a gui:
    Whats information do i need?
        * Text position (Text is inherently 2D -> 2D rectangles, baseline has a vec2 position)
            -> Generally how does formatting/layout work
        * Text size 
            - Size relative to window
            - Relative to screen
            - With respect to physical size (screen height + dpi))
            - Fit inside box
        * Padding inside the text-box
        * Text color
        * Wrapping options/Handling newlines
            - Cutoff -> Stop rendering text when line space is not enough
            - Wrap at character or last space
        * Alignment (Right/Left/Center aligned/Block layout)
        * Calculating size before rendering (Maybe also for vertical scrolling)
*/