#pragma once

#include "input.hpp"

struct Window;

struct Window_State
{
    // Position and size
    int x, y;
    int width, height; // Note: client size
    int dpi;

    bool fullscreen, minimized, vsync;
    bool cursor_visible, cursor_constrained, cursor_reset_into_center;
    bool in_focus;
};

Window* window_create(const char* window_title, int multisample_count);
void window_close(Window* window); // Causes the window to exit the handle message loop
void window_destroy(Window* window); // Deletes the window object

bool window_handle_messages(Window* window, bool block_until_next_message);
void window_swap_buffers(Window* window);
void window_activate_context(Window* window);

Window_State* window_get_window_state(Window* window);
Input* window_get_input(Window* window);

void window_set_fullscreen(Window* window, bool fullscreen);
void window_set_cursor_visibility(Window* window, bool visible);
void window_set_cursor_constrain(Window* window, bool constrain);
void window_set_cursor_reset_into_center(Window* window, bool reset);
void window_set_vsync(Window* window, bool vsync);
void window_set_minimized(Window* window, bool minimized);
void window_set_position(Window* window, int x, int y);
void window_set_size(Window* window, int width, int height);

void window_save_position(Window* window, const char* filename);
void window_load_position(Window* window, const char* filename);


