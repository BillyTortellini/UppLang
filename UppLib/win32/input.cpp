#include "input.hpp"

#include "../utility/utils.hpp"
#include "../datastructures/string.hpp"

const char* key_code_to_string(Key_Code code)
{
    switch (code)
    {
    case Key_Code::UNASSIGNED: return "UNASSIGNED";
    case Key_Code::A: return "A";
    case Key_Code::B: return "B";
    case Key_Code::C: return "C";
    case Key_Code::D: return "D";
    case Key_Code::E: return "E";
    case Key_Code::F: return "F";
    case Key_Code::G: return "G";
    case Key_Code::H: return "H";
    case Key_Code::I: return "I";
    case Key_Code::J: return "J";
    case Key_Code::K: return "K";
    case Key_Code::L: return "L";
    case Key_Code::M: return "M";
    case Key_Code::N: return "N";
    case Key_Code::O: return "O";
    case Key_Code::P: return "P";
    case Key_Code::Q: return "Q";
    case Key_Code::R: return "R";
    case Key_Code::S: return "S";
    case Key_Code::T: return "T";
    case Key_Code::U: return "U";
    case Key_Code::V: return "V";
    case Key_Code::W: return "W";
    case Key_Code::X: return "X";
    case Key_Code::Y: return "Y";
    case Key_Code::Z: return "Z";
    case Key_Code::NUM_1: return "NUM_1";
    case Key_Code::NUM_2: return "NUM_2";
    case Key_Code::NUM_3: return "NUM_3";
    case Key_Code::NUM_4: return "NUM_4";
    case Key_Code::NUM_5: return "NUM_5";
    case Key_Code::NUM_6: return "NUM_6";
    case Key_Code::NUM_7: return "NUM_7";
    case Key_Code::NUM_8: return "NUM_8";
    case Key_Code::NUM_9: return "NUM_9";
    case Key_Code::NUM_0: return "NUM_0";
    case Key_Code::RETURN: return "RETURN";
    case Key_Code::ESCAPE: return "ESCAPE";
    case Key_Code::BACKSPACE: return "BACKSPACE";
    case Key_Code::TAB: return "TAB";
    case Key_Code::SPACE: return "SPACE";
    case Key_Code::SHIFT: return "SHIFT";
    case Key_Code::CTRL: return "CTRL";
    case Key_Code::ALT: return "ALT";
    case Key_Code::F1: return "F1";
    case Key_Code::F2: return "F2";
    case Key_Code::F3: return "F3";
    case Key_Code::F4: return "F4";
    case Key_Code::F5: return "F5";
    case Key_Code::F6: return "F6";
    case Key_Code::F7: return "F7";
    case Key_Code::F8: return "F8";
    case Key_Code::F9: return "F9";
    case Key_Code::F10: return "F10";
    case Key_Code::F11: return "F11";
    case Key_Code::F12: return "F12";
    case Key_Code::LCTRL: return "LCTRL";
    case Key_Code::LSHIFT: return "LSHIFT";
    case Key_Code::LALT: return "LALT";
    case Key_Code::RCTRL: return "RCTRL";
    case Key_Code::RSHIFT: return "RSHIFT";
    case Key_Code::RALT: return "RALT";
    case Key_Code::ARROW_DOWN: return "ARROW_DOWN";
    case Key_Code::ARROW_LEFT: return "ARROW_LEFT";
    case Key_Code::ARROW_RIGHT: return "ARROW_RIGHT";
    case Key_Code::ARROW_UP: return "ARROW_UP";
    }
    panic("invalid enum value!");
    return "WRONG";
}

Mouse_Message mouse_message_make(Mouse_Key_Code key_code, int mouse_x, int mouse_y, bool key_down, bool shift_down, bool alt_down, bool ctrl_down)
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

Mouse_Message mouse_message_make(Mouse_Key_Code key_code, bool key_down, Input* input)
{
    return mouse_message_make(key_code, input->mouse_x, input->mouse_y, key_down,
        input->key_down[(int)Key_Code::SHIFT], input->key_down[(int)Key_Code::ALT], input->key_down[(int)Key_Code::CTRL]
    );
}

Key_Message key_message_make(Key_Code key_code, bool key_down, char c, bool shift_down, bool alt_down, bool ctrl_down)
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
    // ! Don't reset key_down here, since this function is supposed to be once per frame
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

void input_on_focus_lost(Input* input)
{
    input_reset(input);
    memory_set_bytes(input->key_down, KEYBOARD_KEY_COUNT, 0);
    memory_set_bytes(input->mouse_down, MOUSE_KEY_COUNT, 0);
}


