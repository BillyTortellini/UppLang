#include "input.hpp"

#include "../utility/utils.hpp"
#include "../datastructures/string.hpp"

const char* key_code_to_string(KEY_CODE::ENUM code)
{
    switch (code)
    {
    case KEY_CODE::UNASSIGNED: return "UNASSIGNED";
    case KEY_CODE::A: return "A";
    case KEY_CODE::B: return "B";
    case KEY_CODE::C: return "C";
    case KEY_CODE::D: return "D";
    case KEY_CODE::E: return "E";
    case KEY_CODE::F: return "F";
    case KEY_CODE::G: return "G";
    case KEY_CODE::H: return "H";
    case KEY_CODE::I: return "I";
    case KEY_CODE::J: return "J";
    case KEY_CODE::K: return "K";
    case KEY_CODE::L: return "L";
    case KEY_CODE::M: return "M";
    case KEY_CODE::N: return "N";
    case KEY_CODE::O: return "O";
    case KEY_CODE::P: return "P";
    case KEY_CODE::Q: return "Q";
    case KEY_CODE::R: return "R";
    case KEY_CODE::S: return "S";
    case KEY_CODE::T: return "T";
    case KEY_CODE::U: return "U";
    case KEY_CODE::V: return "V";
    case KEY_CODE::W: return "W";
    case KEY_CODE::X: return "X";
    case KEY_CODE::Y: return "Y";
    case KEY_CODE::Z: return "Z";
    case KEY_CODE::NUM_1: return "NUM_1";
    case KEY_CODE::NUM_2: return "NUM_2";
    case KEY_CODE::NUM_3: return "NUM_3";
    case KEY_CODE::NUM_4: return "NUM_4";
    case KEY_CODE::NUM_5: return "NUM_5";
    case KEY_CODE::NUM_6: return "NUM_6";
    case KEY_CODE::NUM_7: return "NUM_7";
    case KEY_CODE::NUM_8: return "NUM_8";
    case KEY_CODE::NUM_9: return "NUM_9";
    case KEY_CODE::NUM_0: return "NUM_0";
    case KEY_CODE::RETURN: return "RETURN";
    case KEY_CODE::ESCAPE: return "ESCAPE";
    case KEY_CODE::BACKSPACE: return "BACKSPACE";
    case KEY_CODE::TAB: return "TAB";
    case KEY_CODE::SPACE: return "SPACE";
    case KEY_CODE::SHIFT: return "SHIFT";
    case KEY_CODE::CTRL: return "CTRL";
    case KEY_CODE::ALT: return "ALT";
    case KEY_CODE::F1: return "F1";
    case KEY_CODE::F2: return "F2";
    case KEY_CODE::F3: return "F3";
    case KEY_CODE::F4: return "F4";
    case KEY_CODE::F5: return "F5";
    case KEY_CODE::F6: return "F6";
    case KEY_CODE::F7: return "F7";
    case KEY_CODE::F8: return "F8";
    case KEY_CODE::F9: return "F9";
    case KEY_CODE::F10: return "F10";
    case KEY_CODE::F11: return "F11";
    case KEY_CODE::F12: return "F12";
    case KEY_CODE::LCTRL: return "LCTRL";
    case KEY_CODE::LSHIFT: return "LSHIFT";
    case KEY_CODE::LALT: return "LALT";
    case KEY_CODE::RCTRL: return "RCTRL";
    case KEY_CODE::RSHIFT: return "RSHIFT";
    case KEY_CODE::RALT: return "RALT";
    }
    panic("invalid enum value!");
    return "WRONG";
}

Mouse_Message mouse_message_make(MOUSE_KEY_CODE::ENUM key_code, int mouse_x, int mouse_y, bool key_down, bool shift_down, bool alt_down, bool ctrl_down)
{
    Mouse_Message result;
    result.key_code = key_code;
    result.key_down = key_down;
    result.alt_down = alt_down;
    result.shift_down = shift_down;
    result.ctrl_down = ctrl_down;
    result.mouse_x = mouse_x;
    result.mouse_y = mouse_y;
    return result;
}

Mouse_Message mouse_message_make(MOUSE_KEY_CODE::ENUM key_code, bool key_down, Input* input)
{
    return mouse_message_make(key_code, input->mouse_x, input->mouse_y, key_down,
        input->key_down[KEY_CODE::SHIFT], input->key_down[KEY_CODE::ALT], input->key_down[KEY_CODE::CTRL]
    );
}

Key_Message key_message_make(KEY_CODE::ENUM key_code, bool key_down, char c, bool shift_down, bool alt_down, bool ctrl_down)
{
    Key_Message result;
    result.key_code = key_code;
    result.key_down = key_down;
    result.character = c;
    result.shift_down = shift_down;
    result.ctrl_down = ctrl_down;
    result.alt_down = alt_down;
    return result;
}

void key_message_append_to_string(Key_Message* msg, String* string)
{
    if (msg->character == 0) {
        string_append_formated(string, "char: '\\0' ");
    }
    else {
        string_append_formated(string, "char: '%c'  ", (byte)msg->character);
    }
    string_append_formated(string, "key_code: %s ", key_code_to_string(msg->key_code));
    string_append_formated(string, "down: %s ", msg->key_down ? "TRUE" : "FALSE");
    string_append_formated(string, "shift: %s ", msg->shift_down ? "TRUE" : "FALSE");
    string_append_formated(string, "alt: %s ", msg->alt_down ? "TRUE" : "FALSE");
}


Input input_create()
{
    Input result;
    result.key_messages = dynamic_array_create_empty<Key_Message>(64);
    result.mouse_messages = dynamic_array_create_empty<Mouse_Message>(64);
    input_reset(&result);
    memory_set_bytes(result.key_down, KEYBOARD_KEY_COUNT, 0);
    memory_set_bytes(result.mouse_down, MOUSE_KEY_COUNT, 0);
    return result;
}

void input_destroy(Input* input) {
    dynamic_array_destroy(&input->key_messages);
    dynamic_array_destroy(&input->mouse_messages);
}

void input_add_key_message(Input* input, Key_Message message) {
    dynamic_array_push_back(&input->key_messages, message);
}
void input_add_mouse_message(Input* input, Mouse_Message message) {
    dynamic_array_push_back(&input->mouse_messages, message);
}

void input_reset(Input* input)
{
    memory_set_bytes(input->key_pressed, KEYBOARD_KEY_COUNT, 0);
    memory_set_bytes(input->mouse_pressed, MOUSE_KEY_COUNT, 0);
    memory_set_bytes(input->mouse_released, MOUSE_KEY_COUNT, 0);

    input->mouse_delta_y = 0;
    input->mouse_delta_x = 0;
    input->mouse_normalized_delta_x = 0.0f;
    input->mouse_normalized_delta_y = 0.0f;
    input->mouse_wheel_delta = 0;

    input->client_area_resized = false;
    input->close_request_issued = false;

    dynamic_array_reset(&input->key_messages);
    dynamic_array_reset(&input->mouse_messages);
}

