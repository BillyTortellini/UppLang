#include "gui.hpp"

#include "../win32/window.hpp"
#include "../win32/input.hpp"
#include "../rendering/rendering_core.hpp"
#include "../rendering/gpu_buffers.hpp"
#include "../rendering/renderer_2d.hpp"
#include "../rendering/text_renderer.hpp"
#include "../win32/timing.hpp"

vec2 anchor_to_direction(Anchor_2D anchor)
{
    switch (anchor) {
    case Anchor_2D::TOP_LEFT:   return vec2(-1, 1);
    case Anchor_2D::TOP_CENTER: return vec2(0, 1);
    case Anchor_2D::TOP_RIGHT:  return vec2(1, 1);
    case Anchor_2D::CENTER_LEFT: return vec2(-1, 0);
    case Anchor_2D::CENTER_CENTER: return vec2(0, 0);
    case Anchor_2D::CENTER_RIGHT: return vec2(1, 0);
    case Anchor_2D::BOTTOM_LEFT: return vec2(-1, -1);
    case Anchor_2D::BOTTOM_CENTER: return vec2(0, -1);
    case Anchor_2D::BOTTOM_RIGHT: return vec2(1, -1);
    default: panic("");
    }
    return vec2(0);
}

GUI gui_create(Renderer_2D* renderer_2d, Input* input, Timer* timer)
{
    GUI result;
    result.element_in_focus = false;
    result.draw_in_focus = false;

    result.renderer_2d = renderer_2d;
    result.input = input;
    result.timer = timer;

    result.mouse_down_this_frame = false;
    result.mouse_pos = vec2(0.0f);
    result.text_in_edit = string_create_empty(128);
    result.numeric_input_buffer = string_create_empty(128);

    return result;
}

void gui_destroy(GUI* gui)
{
    string_destroy(&gui->text_in_edit);
    string_destroy(&gui->numeric_input_buffer);
}

void gui_update(GUI* gui, Input* input, int backbuffer_width, int backbuffer_height)
{
    // Update mouse information
    gui->mouse_pos_last_frame = gui->mouse_pos;
    gui->mouse_down_last_frame = gui->mouse_down_this_frame;
    gui->mouse_down_this_frame = input->mouse_down[(int)Mouse_Key_Code::LEFT];
    gui->mouse_pos = vec2(input->mouse_x / (float)backbuffer_width, input->mouse_y / (float)backbuffer_height) * 2 - vec2(1);
    gui->mouse_pos.y *= -1;
    gui->mouse_pos = gui->mouse_pos / gui->renderer_2d->scaling_factor;
    gui->current_depth = 0.99f;

    if (input->mouse_pressed[(int)Mouse_Key_Code::LEFT]) {
        gui->element_in_focus = false;
    }
}

void gui_render(GUI* gui, Rendering_Core* core)
{
    if (gui->element_in_focus && gui->draw_in_focus) {
        renderer_2D_add_rectangle(gui->renderer_2d, gui->focused_pos,
            gui->focused_size + 0.1f, vec3(0.3f, 0.3f, 0.8f), 0.999f);
    }
    renderer_2D_draw(gui->renderer_2d, core->predefined.main_pass);
}

float gui_next_depth(GUI* gui) {
    gui->current_depth -= 1.0f / 1000.0f;
    return gui->current_depth;
}

void gui_set_focus(GUI* gui, vec2 pos, vec2 size) {
    gui->element_in_focus = true;
    gui->focused_pos = pos;
    gui->focused_size = size;
}

bool gui_is_in_focus(GUI* gui, vec2 pos, vec2 size) {
    if (!gui->element_in_focus) {
        return false;
    }
    if (vector_distance_between(pos, gui->focused_pos) < 0.01f &&
        vector_distance_between(size, gui->focused_size) < 0.01f) {
        return true;
    }
    return false;
}

vec2 gui_calculate_text_size(GUI* gui, int char_count, float height) {
    return vec2(text_renderer_calculate_text_width(gui->renderer_2d->text_renderer, char_count, height), height);
}

GUI_Position gui_position_make(vec2 pos, vec2 size) {
    GUI_Position result;
    result.pos = pos;
    result.size = size;
    return result;
}

GUI_Position gui_position_make_neighbour(GUI_Position origin, Anchor_2D anchor, vec2 size) {
    vec2 anchor_dir = anchor_to_direction(anchor);
    origin.pos = origin.pos - (origin.size/2.0f + size/2.0f) * anchor_dir;
    origin.size = size;
    return origin;
}

GUI_Position gui_position_make_on_window_border(GUI* gui, vec2 size, Anchor_2D anchor) {
    vec2 pos;
    vec2 extrema = vec2(1.0f) / gui->renderer_2d->scaling_factor;
    switch (anchor) {
    case Anchor_2D::TOP_LEFT: pos = vec2(-extrema.x, extrema.y) + vec2(size.x, -size.y) / 2.0f; break;
    case Anchor_2D::TOP_CENTER: pos = vec2(0.0f, extrema.y) + vec2(0.0f, -size.y) / 2.0f; break;
    case Anchor_2D::TOP_RIGHT: pos = vec2(extrema.x, extrema.y) + vec2(-size.x, -size.y) / 2.0f; break;
    case Anchor_2D::CENTER_LEFT: pos = vec2(-extrema.x, 0.0f) + vec2(size.x, -size.y) / 2.0f; break;
    case Anchor_2D::CENTER_CENTER: pos = vec2(0.0f); break;
    case Anchor_2D::CENTER_RIGHT: pos = vec2(extrema.x, 0.0f) + vec2(-size.x, 0.0f) / 2.0f; break;
    case Anchor_2D::BOTTOM_LEFT: pos = vec2(-extrema.x, -extrema.y) + vec2(size.x, size.y) / 2.0f; break;
    case Anchor_2D::BOTTOM_CENTER: pos = vec2(0.0f, -extrema.y) + vec2(0.0f, size.y) / 2.0f; break;
    case Anchor_2D::BOTTOM_RIGHT: pos = vec2(extrema.x, -extrema.y) + vec2(-size.x, size.y) / 2.0f; break;
    }
    return gui_position_make(pos, size);
}

GUI_Position gui_position_make_inside(GUI_Position parent, Anchor_2D anchor, vec2 size)
{
    vec2 anchor_direction = anchor_to_direction(anchor);
    vec2 position = parent.pos + parent.size / 2.0f * anchor_direction - anchor_direction * size / 2.0f;
    return gui_position_make(position, size);
}

bool gui_checkbox(GUI* gui, vec2 pos, vec2 size, bool* value)
{
    Bounding_Box2 bb = bounding_box_2_make_center_size(pos, size);
    bool hovered = false;
    bool clicked = false;
    if (bounding_box_2_is_point_inside(bb, gui->mouse_pos)) {
        if (gui->input->mouse_released[(int)Mouse_Key_Code::LEFT]) clicked = true;
        else hovered = true;
    }

    vec3 fill_color = hovered ? vec3(0.8f) : vec3(1.0f);
    renderer_2D_add_rectangle(gui->renderer_2d, pos, size, fill_color, gui_next_depth(gui));
    renderer_2D_add_rect_outline(gui->renderer_2d, pos, size, vec3(0.0f), 3.0f, gui_next_depth(gui));
    if (*value) {
        float smoll_factor = 0.8f;
        vec2 p0 = pos + vec2(-size.x, -size.y) / 2.0f * smoll_factor;
        vec2 p1 = pos + vec2(size.x, -size.y) / 2.0f * smoll_factor;
        vec2 p2 = pos + vec2(size.x, size.y) / 2.0f * smoll_factor;
        vec2 p3 = pos + vec2(-size.x, size.y) / 2.0f * smoll_factor;
        renderer_2D_add_line(gui->renderer_2d, p0, p2, vec3(1.0f, 0.2f, 0.2f), 2.0f, gui_next_depth(gui));
        renderer_2D_add_line(gui->renderer_2d, p1, p3, vec3(1.0f, 0.2f, 0.2f), 2.0f, gui_next_depth(gui));
    }

    if (clicked) *value = !*value;
    return clicked;
}

bool gui_checkbox(GUI* gui, GUI_Position pos, bool* value)
{
    return gui_checkbox(gui, pos.pos, pos.size, value);
}

bool gui_slider(GUI* gui, GUI_Position pos, float* value, float min, float max) 
{
    float normalized = math_clamp((*value - min) / (max - min), 0.0f, 1.0f);
    vec2 slider_pos = pos.pos - vec2(pos.size.x / 2.0f, 0.0f) + vec2(normalized * pos.size.x, 0.0f);
    vec2 slider_size = vec2(0.05f, pos.size.y);
    Bounding_Box2 slider_bb = bounding_box_2_make_center_size(slider_pos, slider_size);
    
    // TODO: Check if slider was in focus last frame, and drop focus if mouse is not pressed anymore
    bool in_focus = gui_is_in_focus(gui, slider_pos, slider_size);
    if (!in_focus) {
        if (bounding_box_2_is_point_inside(slider_bb, gui->mouse_pos) && gui->mouse_down_this_frame) {
            gui->draw_in_focus = false;
            gui_set_focus(gui, slider_pos, slider_size);
        }
    }
    else if (!gui->mouse_down_this_frame) {
        gui->element_in_focus = false; // If mouse was moved out of slider, and mouse key was released, we want to lose focus
        in_focus = false;
        return true;
    }

    bool value_changed = false;
    if (in_focus) // If the slider is in focus, follow it with mouse 
    {
        float slider_min = pos.pos.x - pos.size.x / 2.0f;
        float slider_max = pos.pos.x + pos.size.x / 2.0f;
        slider_pos.x = math_clamp(gui->mouse_pos.x, slider_min, slider_max);
        float slider_value = (slider_pos.x - slider_min) / (slider_max - slider_min);
        float old_value = *value;
        *value = slider_value * (max - min) + min;
        value_changed = *value != old_value;
        gui_set_focus(gui, slider_pos, slider_size);
    }

    renderer_2D_add_line(
        gui->renderer_2d,
        pos.pos - vec2(pos.size.x / 2.0f, 0.0f),
        pos.pos + vec2(pos.size.x / 2.0f, 0.0f),
        vec3(0.0f), 3.0f, gui_next_depth(gui)
    );
    renderer_2D_add_rectangle(gui->renderer_2d, slider_pos, slider_size, vec3(1.0f), gui_next_depth(gui));
    renderer_2D_add_rect_outline(gui->renderer_2d, slider_pos, slider_size, vec3(1.0f), 3.0f, gui_next_depth(gui));
    return false;
}

void gui_label(GUI* gui, GUI_Position pos, const char* text) {
    renderer_2D_add_rectangle(gui->renderer_2d, pos.pos, pos.size, vec3(1.0f), gui_next_depth(gui));
    renderer_2D_add_text_in_box(gui->renderer_2d, &string_create_static(text),
        pos.size.y, vec3(0.0f), pos.pos, pos.size, Text_Alignment_Horizontal::LEFT, Text_Alignment_Vertical::CENTER,
        Text_Wrapping_Mode::SCALE_DOWN);
}

void gui_label_float(GUI* gui, GUI_Position pos, float f) {
    string_clear(&gui->numeric_input_buffer);
    string_append_formated(&gui->numeric_input_buffer, "%3.2f", f);
    gui_label(gui, pos, gui->numeric_input_buffer.characters);
}

// Return value indicates if string was changed
bool gui_text_input_string(GUI* gui, String* to_fill, vec2 pos, vec2 size, bool only_write_on_enter, bool clear_on_focus)
{
    // Check if user presses on text input if its not in focus
    bool in_focus = gui_is_in_focus(gui, pos, size);
    bool text_was_edited = false;
    String* edit_string = only_write_on_enter ? &gui->text_in_edit : to_fill;
    if (!in_focus) {
        if (gui->input->mouse_pressed[(int)Mouse_Key_Code::LEFT]) {
            if (bounding_box_2_is_point_inside(bounding_box_2_make_center_size(pos, size), gui->mouse_pos)) {
                gui_set_focus(gui, pos, size);
                if (only_write_on_enter) {
                    if (clear_on_focus) {
                        string_clear(&gui->text_in_edit);
                    }
                    else {
                        string_set_characters(&gui->text_in_edit, to_fill->characters);
                    }
                }
                else if (clear_on_focus) {
                    string_clear(to_fill);
                    text_was_edited = true;
                }
            }
        }
    }

    String* display_string = to_fill;
    if (in_focus)
    {
        gui->draw_in_focus = true;
        display_string = edit_string;
        if (gui->input->key_down[(int)Key_Code::BACKSPACE])
        {
            if (!gui->backspace_was_down) {
                gui->backspace_was_down = true;
                gui->backspace_down_time = timer_current_time_in_seconds(gui->timer);
            }
            else
            {
                double now = timer_current_time_in_seconds(gui->timer);
                double diff = now - gui->backspace_down_time;
                const int ticks_per_second = 10;
                while (diff > 1 / (float)ticks_per_second) {
                    text_was_edited = true;
                    string_remove_character(edit_string, edit_string->size - 1);
                    diff -= 1.0f / ticks_per_second;
                }
                gui->backspace_down_time = now;
            }
        }
        else {
            gui->backspace_was_down = false;
        }

        for (int i = 0; i < gui->input->key_messages.size; i++) {
            Key_Message& msg = gui->input->key_messages[i];
            if (msg.key_down && msg.character >= 32) {
                string_append_character(edit_string, msg.character);
                text_was_edited = true;
            }
            else if (msg.key_down && msg.key_code == Key_Code::BACKSPACE) {
                string_remove_character(edit_string, edit_string->size - 1);
                text_was_edited = true;
            }
        }
    }

    // Draw
    renderer_2D_add_rectangle(gui->renderer_2d, pos, size, vec3(0.3f), gui_next_depth(gui));
    renderer_2D_add_text_in_box(gui->renderer_2d, display_string, size.y, vec3(1.0f),
        pos, size, Text_Alignment_Horizontal::LEFT, Text_Alignment_Vertical::CENTER,
        Text_Wrapping_Mode::SCALE_DOWN);
    renderer_2D_add_rect_outline(gui->renderer_2d, pos, size, vec3(0.0f), 3.0f, gui_next_depth(gui));

    if (only_write_on_enter) {
        text_was_edited = false;
    }

    if (gui->input->key_pressed[(int)Key_Code::RETURN] && in_focus) {
        if (only_write_on_enter) {
            string_set_characters(to_fill, gui->text_in_edit.characters);
            string_clear(&gui->text_in_edit);
            text_was_edited = true;
        }
        gui->element_in_focus = false;
        return text_was_edited;
    }

    return text_was_edited;
}

bool gui_text_input_string(GUI* gui, String* to_fill, GUI_Position pos, bool only_write_on_enter, bool clear_on_focus) {
    return gui_text_input_string(gui, to_fill, pos.pos, pos.size, only_write_on_enter, clear_on_focus);
}

bool gui_text_input_int(GUI* gui, vec2 pos, vec2 size, int* value)
{
    string_clear(&gui->numeric_input_buffer);
    string_append_formated(&gui->numeric_input_buffer, "%d", *value);
    if (gui_text_input_string(gui, &gui->numeric_input_buffer, pos, size, true, true)) {
        Optional<int> input = string_parse_int(&gui->numeric_input_buffer);
        if (!input.available) {
            return false;
        }
        else {
            *value = input.value;
            return true;
        }
    }
    return false;
}

bool gui_text_input_int(GUI* gui, GUI_Position pos, int* value) {
    return gui_text_input_int(gui, pos.pos, pos.size, value);
}

bool gui_text_input_float(GUI* gui, vec2 pos, vec2 size, float* value)
{
    string_clear(&gui->numeric_input_buffer);
    string_append_formated(&gui->numeric_input_buffer, "%3.2f", *value);
    if (gui_text_input_string(gui, &gui->numeric_input_buffer, pos, size, true, true)) {
        Optional<float> input = string_parse_float(&gui->numeric_input_buffer);
        if (!input.available) {
            return false;
        }
        else {
            *value = input.value;
            return true;
        }
    }
    return false;
}

bool gui_text_input_float(GUI* gui, GUI_Position pos, float* value) {
    return gui_text_input_float(gui, pos.pos, pos.size, value);
}

bool gui_button(GUI* gui, vec2 pos, vec2 size, const char* text)
{
    // Add button to button list
    bool clicked = false, hovered = false;

    // Check if clicked
    Bounding_Box2 bb = bounding_box_2_make_center_size(pos, size);
    if (bounding_box_2_is_point_inside(bb, gui->mouse_pos)) {
        if (gui->input->mouse_released[(int)Mouse_Key_Code::LEFT]) {
            clicked = true;
        }
        else {
            hovered = true;
        }
    }

    // Draw
    vec3 color(0.0f, 0.3f, 0.9f);
    if (hovered) color = color * 0.7f;
    renderer_2D_add_rectangle(gui->renderer_2d, pos, size, color, gui_next_depth(gui));
    renderer_2D_add_text_in_box(gui->renderer_2d, &string_create_static(text), size.y, vec3(1.0f),
        pos, size, Text_Alignment_Horizontal::CENTER, Text_Alignment_Vertical::CENTER,
        Text_Wrapping_Mode::SCALE_DOWN);
    renderer_2D_add_rect_outline(gui->renderer_2d, pos, size, color * 0.2f, 3.0f, gui_next_depth(gui));

    return clicked;
}

bool gui_button(GUI* gui, GUI_Position pos, const char* text) {
    return gui_button(gui, pos.pos, pos.size, text);
}

