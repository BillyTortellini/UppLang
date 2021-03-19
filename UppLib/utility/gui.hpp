#pragma once

#include "../math/umath.hpp"
#include "../datastructures/string.hpp"

namespace ANCHOR {
    enum ENUM {
        TOP_LEFT, TOP_CENTER, TOP_RIGHT,
        BOTTOM_LEFT, BOTTOM_CENTER, BOTTOM_RIGHT,
        CENTER_LEFT, CENTER_CENTER, CENTER_RIGHT
    };
}

// Forward Declarations
struct Input;
struct WindowState;
struct Renderer_2D;
struct FileListener;
struct OpenGLState;

struct GUI
{
    vec2 mouse_pos; // Normalized
    vec2 mouse_pos_last_frame;
    bool mouse_down_last_frame;
    bool mouse_down_this_frame;

    bool element_in_focus;
    bool draw_in_focus;
    vec2 focused_size;
    vec2 focused_pos;

    bool backspace_was_down = false;
    double backspace_down_time;
    String text_in_edit;
    String numeric_input_buffer;
    float current_depth = 0.99f;

    Input* input;
    WindowState* window_state;
    Renderer_2D* renderer_2d;
};

GUI gui_create(OpenGLState* state, FileListener* listener, Renderer_2D* renderer_2d, WindowState* window_state, Input* input);
void gui_destroy(GUI* gui);
void gui_update(GUI* gui, Input* input, WindowState* window_state);
void gui_draw(GUI* gui, OpenGLState* state);
float gui_next_depth(GUI* gui);
void gui_set_focus(GUI* gui, vec2 pos, vec2 size);
bool gui_is_in_focus(GUI* gui, vec2 pos, vec2 size);
vec2 gui_calculate_text_size(GUI* gui, int char_count, float height);

struct GUI_Position {
    vec2 pos;
    vec2 size;
};

GUI_Position gui_position_make(vec2 pos, vec2 size);
GUI_Position gui_position_make_neighbour(GUI_Position origin, ANCHOR::ENUM anchor, vec2 size);
GUI_Position gui_position_make_on_window_border(GUI* gui, vec2 size, ANCHOR::ENUM anchor);
GUI_Position gui_position_make_inside(GUI_Position parent, ANCHOR::ENUM anchor, vec2 size);

bool gui_checkbox(GUI* gui, vec2 pos, vec2 size, bool* value);
bool gui_checkbox(GUI* gui, GUI_Position pos, bool* value);
bool gui_slider(GUI* gui, GUI_Position pos, float* value, float min, float max) ;
void gui_label(GUI* gui, GUI_Position pos, const char* text);
void gui_label_float(GUI* gui, GUI_Position pos, float f);
bool gui_text_input_string(GUI* gui, String* to_fill, vec2 pos, vec2 size, bool only_write_on_enter, bool clear_on_focus);
bool gui_text_input_string(GUI* gui, String* to_fill, GUI_Position pos, bool only_write_on_enter, bool clear_on_focus);
bool gui_text_input_int(GUI* gui, vec2 pos, vec2 size, int* value);
bool gui_text_input_int(GUI* gui, GUI_Position pos, int* value);
bool gui_text_input_float(GUI* gui, vec2 pos, vec2 size, float* value);
bool gui_text_input_float(GUI* gui, GUI_Position pos, float* value);
bool gui_button(GUI* gui, vec2 pos, vec2 size, const char* text);
bool gui_button(GUI* gui, GUI_Position pos, const char* text);
