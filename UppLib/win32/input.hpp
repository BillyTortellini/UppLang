#pragma once

#include "../datastructures/dynamic_array.hpp"

enum class Key_Code
{
    UNASSIGNED = 0,
    A = 4,
    B = 5,
    C = 6,
    D = 7,
    E = 8,
    F = 9,
    G = 10,
    H = 11,
    I = 12,
    J = 13,
    K = 14,
    L = 15,
    M = 16,
    N = 17,
    O = 18,
    P = 19,
    Q = 20,
    R = 21,
    S = 22,
    T = 23,
    U = 24,
    V = 25,
    W = 26,
    X = 27,
    Y = 28,
    Z = 29,

    NUM_1 = 30,
    NUM_2 = 31,
    NUM_3 = 32,
    NUM_4 = 33,
    NUM_5 = 34,
    NUM_6 = 35,
    NUM_7 = 36,
    NUM_8 = 37,
    NUM_9 = 38,
    NUM_0 = 39,

    RETURN = 40,
    ESCAPE = 41,
    BACKSPACE = 42,
    TAB = 43,
    SPACE = 44,
    SHIFT = 45,
    CTRL = 46,
    ALT = 47,

    F1 = 58,
    F2 = 59,
    F3 = 60,
    F4 = 61,
    F5 = 62,
    F6 = 63,
    F7 = 64,
    F8 = 65,
    F9 = 66,
    F10 = 67,
    F11 = 68,
    F12 = 69,

    ARROW_LEFT = 80,
    ARROW_RIGHT = 81,
    ARROW_UP = 82,
    ARROW_DOWN = 83,

    LCTRL = 224,
    LSHIFT = 225,
    LALT = 226, /**< alt, option */
    RCTRL = 228,
    RSHIFT = 229,
    RALT = 230, /**< alt gr, option */
};

const char* key_code_to_string(Key_Code code);

enum class Mouse_Key_Code
{
    LEFT = 0,
    RIGHT = 1,
    MIDDLE = 2,
};

struct Mouse_Message
{
    Mouse_Key_Code key_code;
    int mouse_x;
    int mouse_y;
    bool key_down; // If false, key was up this frame
    bool shift_down;
    bool alt_down;
    bool ctrl_down;
};
struct Input; // Forward declaration
Mouse_Message mouse_message_make(Mouse_Key_Code key_code, int mouse_x, int mouse_y, bool key_down, bool shift_down, bool alt_down, bool ctrl_down);
Mouse_Message mouse_message_make(Mouse_Key_Code key_code, bool key_down, Input* input);

struct Key_Message
{
    Key_Code key_code;
    bool key_down; // If false, key was up this frame
    bool shift_down;
    bool alt_down;
    bool ctrl_down;
    byte character;
};
Key_Message key_message_make(Key_Code key_code, bool key_down, char c, bool shift_down, bool alt_down, bool ctrl_down);
struct String;
void key_message_append_to_string(Key_Message* msg, String* string);

#define KEYBOARD_KEY_COUNT 256
#define MOUSE_KEY_COUNT 3
struct Input
{
    // Keyboard input
    bool key_down[KEYBOARD_KEY_COUNT];    // Indicating if the key is currently down
    bool key_pressed[KEYBOARD_KEY_COUNT]; // If the key was pressed since last input reset
    Dynamic_Array<Key_Message> key_messages;
    Dynamic_Array<Mouse_Message> mouse_messages;

    // Mouse input
    bool mouse_down[MOUSE_KEY_COUNT];
    bool mouse_pressed[MOUSE_KEY_COUNT];// If mouse was pressed this frame (last_frame_down = false, this_frame_down = true)
    bool mouse_released[MOUSE_KEY_COUNT]; // If mouse was released this frame (last_frame_down = true, this_frame_down = false)
    float mouse_wheel_delta;
    int mouse_x, mouse_y;
    int mouse_delta_x, mouse_delta_y;
    float mouse_normalized_delta_x, mouse_normalized_delta_y;

    // Window input
    bool close_request_issued;         // Either X was pressed or ALT-F4 was sent to the window
    bool client_area_resized;   // True when the client area (Framebuffer of window) size changed
};

Input input_create();
void input_destroy(Input* input);
void input_add_key_message(Input* input, Key_Message message);
void input_add_mouse_message(Input* input, Mouse_Message message);
void input_reset(Input* input);