#include "syntax_editor.hpp"

#include "../../rendering/text_renderer.hpp"
#include "../../datastructures/string.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/renderer_2d.hpp"

#include "../../win32/input.hpp"
#include "syntax_colors.hpp"

// Structs
enum class Input_Command_Type
{
    IDENTIFIER_LETTER,
    DIGIT,
    DELIMITER,
    SPACE,
    ENTER,
    EXIT_INSERT_MODE,
    BACKSPACE,
};

struct Input_Command
{
    Input_Command_Type type;
    char letter;
    Delimiter_Type delimiter;
};

enum class Normal_Command
{
    MOVE_LEFT,
    MOVE_RIGHT,
    INSERT_BEFORE,
    INSERT_AFTER,
    CHANGE_TOKEN,
    DELETE_TOKEN,
};

// Helpers
String characters_get_valid_identifier_characters() {
    return string_create_static("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890_");
}

String characters_get_non_identifier_non_whitespace() {
    return string_create_static("!\"§$%&/()[]{}<>|=\\?´`+*~#'-.:,;^°");
}

String characters_get_whitespaces() {
    return string_create_static("\n \t");
}

String characters_get_all_letters() {
    return string_create_static("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

bool char_is_digit(int c) {
    return (c >= '0' && c <= '9');
}

bool char_is_letter(int c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

void syntax_editor_sanitze_cursor(Syntax_Editor* editor)
{
    if (editor->tokens.size == 0) {
        editor->cursor_index = 0;
        return;
    }
    if (editor->cursor_index > editor->tokens.size) {
        editor->cursor_index = editor->tokens.size;
    }
    if (editor->cursor_index < 0) {
        editor->cursor_index = 0;
    }
}

void syntax_editor_remove_token(Syntax_Editor* editor, int token_index)
{
    if (token_index >= editor->tokens.size || token_index < 0) return;
    Syntax_Token t = editor->tokens[token_index];
    switch (t.type)
    {
    case Syntax_Token_Type::IDENTIFIER:
        string_destroy(&t.options.identifier);
        break;
    case Syntax_Token_Type::NUMBER:
        string_destroy(&t.options.number.text);
        break;
    }
    dynamic_array_remove_ordered(&editor->tokens, token_index);
}

void normal_mode_handle_command(Syntax_Editor* editor, Normal_Command command)
{
    auto& tokens = editor->tokens;
    auto& cursor = editor->cursor_index;
    switch (command)
    {
    case Normal_Command::DELETE_TOKEN: {
        if (cursor == tokens.size) return;
        syntax_editor_remove_token(editor, cursor);
        break;
    }
    case Normal_Command::CHANGE_TOKEN: {
        if (tokens.size == 0) return;
        syntax_editor_remove_token(editor, cursor);
        editor->mode = Editor_Mode::INPUT;
        editor->insert_mode = Insert_Mode::BEFORE;
        break;
    }
    case Normal_Command::INSERT_AFTER: {
        editor->mode = Editor_Mode::INPUT;
        editor->insert_mode = Insert_Mode::APPEND;
        break;
    }
    case Normal_Command::INSERT_BEFORE: {
        editor->mode = Editor_Mode::INPUT;
        editor->insert_mode = Insert_Mode::BEFORE;
        break;
    }
    case Normal_Command::MOVE_LEFT: {
        editor->cursor_index -= 1;
        syntax_editor_sanitze_cursor(editor);
        break;
    }
    case Normal_Command::MOVE_RIGHT: {
        editor->cursor_index += 1;
        syntax_editor_sanitze_cursor(editor);
        break;
    }
    default: panic("");
    }
}

void insert_mode_handle_command(Syntax_Editor* editor, Input_Command input)
{
    auto& tokens = editor->tokens;
    auto& cursor = editor->cursor_index;
    assert(editor->mode == Editor_Mode::INPUT, "");
    syntax_editor_sanitze_cursor(editor);

    if (cursor == tokens.size) {
        editor->insert_mode = Insert_Mode::BEFORE;
    }

    // Handle Universal Inputs
    if (input.type == Input_Command_Type::EXIT_INSERT_MODE) {
        if (editor->insert_mode == Insert_Mode::APPEND) {
            cursor += 1;
            syntax_editor_sanitze_cursor(editor);
        }
        editor->mode = Editor_Mode::NORMAL;
        editor->insert_mode = Insert_Mode::APPEND;
        return;
    }
    if (input.type == Input_Command_Type::ENTER) return; // TODO: Line/Indentation handling
    if (input.type == Input_Command_Type::SPACE) {
        if (editor->insert_mode == Insert_Mode::APPEND) {
            editor->insert_mode = Insert_Mode::BEFORE;
            editor->cursor_index += 1;
            syntax_editor_sanitze_cursor(editor);
        }
        return;
    }

    // Handle the case of editing inside a token
    if (editor->insert_mode == Insert_Mode::APPEND)
    {
        auto& token = tokens[cursor];
        bool input_used = false;
        bool remove_token = false;
        switch (token.type)
        {
        case Syntax_Token_Type::DELIMITER: {
            if (input.type == Input_Command_Type::BACKSPACE) {
                remove_token = true;
                input_used = true;
            }
            break;
        }
        case Syntax_Token_Type::IDENTIFIER: {
            auto& id = token.options.identifier;
            input_used = true;
            switch (input.type)
            {
            case Input_Command_Type::BACKSPACE:
                if (id.size == 1) {
                    remove_token = true;
                    break;
                }
                string_truncate(&id, id.size - 1);
                break;
            case Input_Command_Type::IDENTIFIER_LETTER:
                string_append_character(&id, input.letter);
                break;
            case Input_Command_Type::DIGIT:
                string_append_character(&id, input.letter);
                break;
            default: input_used = false;
            }
            break;
        }
        case Syntax_Token_Type::NUMBER: {
            auto& text = token.options.number.text;
            input_used = true;
            switch (input.type)
            {
            case Input_Command_Type::BACKSPACE:
                if (text.size == 1) {
                    remove_token = true;
                    break;
                }
                string_truncate(&text, text.size - 1);
                break;
            case Input_Command_Type::DIGIT:
                string_append_character(&text, input.letter);
                break;
            default: input_used = false;
            }
            break;
        }
        }

        if (remove_token) {
            syntax_editor_remove_token(editor, editor->cursor_index);
            editor->insert_mode = Insert_Mode::BEFORE;
            return;
        }
        if (input_used) return;
    }
    else 
    {
        if (input.type == Input_Command_Type::BACKSPACE && cursor != 0) {
            cursor -= 1;
            editor->insert_mode = Insert_Mode::APPEND;
            return;
        }
    }

    // Insert new token if necessary (Mode could either be BEFORE or APPEND)
    Syntax_Token new_token;
    bool token_valid = false;
    switch (input.type)
    {
    case Input_Command_Type::IDENTIFIER_LETTER: {
        token_valid = true;
        new_token.type = Syntax_Token_Type::IDENTIFIER;
        new_token.options.identifier = string_create_empty(1);
        string_append_character(&new_token.options.identifier, input.letter);
        break;
    }
    case Input_Command_Type::DIGIT: {
        token_valid = true;
        new_token.type = Syntax_Token_Type::NUMBER;
        new_token.options.number.text = string_create_empty(1);
        string_append_character(&new_token.options.number.text, input.letter);
        break;
    }
    case Input_Command_Type::DELIMITER: {
        token_valid = true;
        new_token.type = Syntax_Token_Type::DELIMITER;
        new_token.options.delimiter = input.delimiter;
        break;
    }
    default: token_valid = false;
    }

    if (token_valid) {
        if (editor->insert_mode == Insert_Mode::APPEND) {
            cursor += 1;
        }
        dynamic_array_insert_ordered(&tokens, new_token, cursor);
        syntax_editor_sanitze_cursor(editor);
        editor->insert_mode = Insert_Mode::APPEND;
    }
}

void syntax_editor_update(Syntax_Editor* editor, Input* input)
{
    for (int i = 0; i < input->key_messages.size; i++)
    {
        Key_Message msg = input->key_messages[i];
        if (editor->mode == Editor_Mode::INPUT)
        {
            Input_Command input;
            if (msg.character == 32 && msg.key_down) {
                input.type = Input_Command_Type::SPACE;
            }
            else if (msg.key_code == Key_Code::L && msg.key_down && msg.ctrl_down) {
                input.type = Input_Command_Type::EXIT_INSERT_MODE;
            }
            else if (msg.key_code == Key_Code::BACKSPACE && msg.key_down) {
                input.type = Input_Command_Type::BACKSPACE;
            }
            else if (msg.key_code == Key_Code::RETURN && msg.key_down) {
                input.type = Input_Command_Type::ENTER;
            }
            else if (char_is_letter(msg.character) || msg.character == '_') {
                input.type = Input_Command_Type::IDENTIFIER_LETTER;
                input.letter = msg.character;
            }
            else if (char_is_digit(msg.character)) {
                input.type = Input_Command_Type::DIGIT;
                input.letter = msg.character;
            }
            else if (msg.character == '(') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::OPEN_PARENTHESIS;
            }
            else if (msg.character == ')') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::CLOSED_PARENTHESIS;
            }
            else if (msg.character == '+') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::PLUS;
            }
            else if (msg.character == '-') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::MINUS;
            }
            else if (msg.character == '*') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::STAR;
            }
            else if (msg.character == '/') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::SLASH;
            }
            else {
                continue;
            }
            insert_mode_handle_command(editor, input);
        }
        else
        {
            Normal_Command command;
            if (msg.key_code == Key_Code::L && msg.key_down) {
                command = Normal_Command::MOVE_RIGHT;
            }
            else if (msg.key_code == Key_Code::H && msg.key_down) {
                command = Normal_Command::MOVE_LEFT;
            }
            else if (msg.key_code == Key_Code::A && msg.key_down) {
                command = Normal_Command::INSERT_AFTER;
            }
            else if (msg.key_code == Key_Code::I && msg.key_down) {
                command = Normal_Command::INSERT_BEFORE;
            }
            else if (msg.key_code == Key_Code::C && msg.key_down) {
                command = Normal_Command::CHANGE_TOKEN;
            }
            else if (msg.key_code == Key_Code::X && msg.key_down) {
                command = Normal_Command::DELETE_TOKEN;
            }
            else {
                continue;
            }
            normal_mode_handle_command(editor, command);
        }
    }
}

Syntax_Token token_make_identifier(const char* id) {
    Syntax_Token t;
    t.type = Syntax_Token_Type::IDENTIFIER;
    t.options.identifier = string_create(id);
    return t;
}

Syntax_Editor* syntax_editor_create(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D)
{
    Syntax_Editor* result = new Syntax_Editor;
    result->cursor_index = 0;
    result->mode = Editor_Mode::INPUT;
    result->insert_mode = Insert_Mode::APPEND;

    result->text_renderer = text_renderer;
    result->rendering_core = rendering_core;
    result->renderer_2D = renderer_2D;
    result->tokens = dynamic_array_create_empty<Syntax_Token>(1);
    return result;
}

void syntax_editor_destroy(Syntax_Editor* editor)
{
    delete editor;
}



// Rendering
void syntax_editor_draw_underline(Syntax_Editor* editor, int line, int character, int length, vec3 color)
{
    float w = editor->rendering_core->render_information.viewport_width;
    float h = editor->rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 char_size = editor->character_size * scaling_factor;
    vec2 size = char_size * vec2((float)length, 1 / 8.0f);
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * char_size.x, -(line + 1) * char_size.y);
    cursor = cursor + size * vec2(0.5f, 0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_cursor_line(Syntax_Editor* editor, vec3 color, int line, int character)
{
    float w = editor->rendering_core->render_information.viewport_width;
    float h = editor->rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 char_size = editor->character_size * scaling_factor;
    vec2 size = char_size * vec2(0.1f, 1.0f);
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * char_size.x, -(line + 1) * char_size.y);
    cursor = cursor + size * vec2(0.5f, 0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_character_box(Syntax_Editor* editor, vec3 color, int line, int character)
{
    float w = editor->rendering_core->render_information.viewport_width;
    float h = editor->rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 size = editor->character_size * scaling_factor;
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * size.x, -line * size.y);
    cursor = cursor + size * vec2(0.5f, -0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_string(Syntax_Editor* editor, String string, vec3 color, int line, int character)
{
    vec2 pos = vec2(-1.0f, 1.0f) + vec2(character, -(line + 1)) * editor->character_size;
    text_renderer_set_color(editor->text_renderer, color);
    text_renderer_add_text(editor->text_renderer, &string, pos, editor->character_size.y, 1.0f);
}

String delimiter_type_to_string(Delimiter_Type type)
{
    switch (type)
    {
    case Delimiter_Type::CLOSED_PARENTHESIS: return string_create_static(")");
    case Delimiter_Type::OPEN_PARENTHESIS:return string_create_static("(");
    case Delimiter_Type::MINUS: return string_create_static("-");
    case Delimiter_Type::PLUS: return string_create_static("+");
    case Delimiter_Type::SLASH: return string_create_static("/");
    case Delimiter_Type::STAR: return string_create_static("*");
    default: panic("");
    }
    return string_create_static("ERROR");
}

void syntax_editor_render(Syntax_Editor* editor)
{
    auto& tokens = editor->tokens;
    auto& cursor = editor->cursor_index;

    // Prepare Render
    editor->character_size.y = text_renderer_cm_to_relative_height(editor->text_renderer, editor->rendering_core, 0.8f);
    editor->character_size.x = text_renderer_get_cursor_advance(editor->text_renderer, editor->character_size.y);

    // Calculate Token Sizes
    for (int i = 0; i < tokens.size; i++) {
        auto& token = tokens[i];
        switch (token.type)
        {
        case Syntax_Token_Type::DELIMITER:
            token.size = 1;
            break;
        case Syntax_Token_Type::IDENTIFIER:
            token.size = token.options.identifier.size;
            break;
        case Syntax_Token_Type::NUMBER:
            token.size = token.options.number.text.size;
            break;
        default: panic("");
        }
    }

    // Calculate Token positioning
    int offset = 0;
    for (int i = 0; i < editor->tokens.size; i++) {
        Syntax_Token* token = &tokens[i];
        if (editor->mode == Editor_Mode::INPUT && editor->insert_mode == Insert_Mode::BEFORE && cursor == i) {
            offset += 1;
        }
        token->x_pos = offset;
        offset += token->size + 1;
    }

    // Render Tokens
    for (int i = 0; i < editor->tokens.size; i++) {
        Syntax_Token* token = &editor->tokens[i];
        switch (token->type)
        {
        case Syntax_Token_Type::DELIMITER:
            syntax_editor_draw_string(editor, delimiter_type_to_string(token->options.delimiter), Syntax_Color::TEXT, 0, token->x_pos);
            break;
        case Syntax_Token_Type::IDENTIFIER:
            syntax_editor_draw_string(editor, token->options.identifier, Syntax_Color::IDENTIFIER_FALLBACK, 0, token->x_pos);
            break;
        case Syntax_Token_Type::NUMBER:
            syntax_editor_draw_string(editor, token->options.number.text, Syntax_Color::LITERAL, 0, token->x_pos);
            break;
        default: panic("");
        }
    }

    // Render Cursor
    if (editor->mode == Editor_Mode::NORMAL) {
        int pos = 0;
        if (cursor == 0) {
            pos = 0;
        }
        else if (cursor == tokens.size) {
            pos = offset;
        }
        else {
            pos = tokens[cursor].x_pos;
        }
        syntax_editor_draw_character_box(editor, Syntax_Color::COMMENT, 0, pos);
    }
    else
    {
        int pos = 0;
        if (cursor != tokens.size) {
            pos = tokens[cursor].x_pos;
            if (editor->insert_mode == Insert_Mode::BEFORE) {
                pos -= 1;
            }
            else {
                pos += tokens[cursor].size;
            }
        }
        else {
            pos = offset;
        }
        syntax_editor_draw_cursor_line(editor, Syntax_Color::COMMENT, 0, pos);
    }

    renderer_2D_render(editor->renderer_2D, editor->rendering_core);
    text_renderer_render(editor->text_renderer, editor->rendering_core);
}