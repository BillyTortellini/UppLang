#include "syntax_editor.hpp"

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/renderer_2d.hpp"
#include "../../utility/file_io.hpp"
#include "../../utility/character_info.hpp"

#include "../../win32/input.hpp"
#include "syntax_colors.hpp"
#include "compiler.hpp"
#include "ast.hpp"
#include "dependency_analyser.hpp"
#include "semantic_analyser.hpp"
#include "parser.hpp"
#include "source_code.hpp"

// Datatypes
// Text
struct Render_Info
{
    int pos;  // x position in line including line indentation
    int line;
    int size; // length of token
};

struct Editor_Line
{
    String text;
    Dynamic_Array<Token> tokens;
    Dynamic_Array<Render_Info> infos;
    int indentation;
};

struct Editor_Text
{
    Dynamic_Array<Editor_Line> lines;
};

struct Text_Position
{
    int line_index;
    int char_index;
};



// Editor
enum class Editor_Mode
{
    NORMAL,
    INPUT,
};

struct Syntax_Editor
{
    // Editing
    Editor_Mode mode;
    Editor_Text text;
    Text_Position cursor;

    String context_text;
    bool space_before_cursor;
    bool space_after_cursor;

    // Rendering
    Input* input;
    Rendering_Core* rendering_core;
    Window* window;
    Renderer_2D* renderer_2D;
    Text_Renderer* text_renderer;
    vec2 character_size;
    const char* file_path;
};

enum class Input_Command_Type
{
    IDENTIFIER_LETTER,
    NUMBER_LETTER,
    DELIMITER_LETTER,
    SPACE,
    ENTER,
    ENTER_REMOVE_ONE_INDENT,
    EXIT_INSERT_MODE,
    BACKSPACE,
    ADD_INDENTATION,
    REMOVE_INDENTATION,
    MOVE_LEFT,
    MOVE_RIGHT,
};

struct Input_Command
{
    Input_Command_Type type;
    char letter;
};

enum class Normal_Command
{
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_UP,
    MOVE_DOWN,
    MOVE_LINE_START,
    MOVE_LINE_END,
    ADD_LINE_ABOVE,
    ADD_LINE_BELOW,
    INSERT_BEFORE,
    INSERT_AFTER,
    INSERT_AT_LINE_START,
    INSERT_AT_LINE_END,
    CHANGE_TOKEN,
    DELETE_TOKEN,
};

// Globals
static Syntax_Editor syntax_editor;



void editor_line_destroy(Editor_Line* line) {
    string_destroy(&line->text);
    dynamic_array_destroy(&line->tokens);
    dynamic_array_destroy(&line->infos);
}

void editor_text_remove_line(int line_index)
{
    auto& lines = syntax_editor.text.lines;
    assert(line_index > 0 && line_index < lines.size, "");
    auto& line = lines[line_index];
    editor_line_destroy(&line);
    dynamic_array_remove_ordered(&lines, line_index);
}

void editor_text_add_line(int line_index, int indentation, String initial_text)
{
    auto& lines = syntax_editor.text.lines;
    Editor_Line line;
    line.indentation = indentation;
    line.text = string_create_empty(initial_text.size);
    string_append_string(&line.text, &initial_text);
    line.tokens = dynamic_array_create_empty<Token>(1);
    line.infos = dynamic_array_create_empty<Render_Info>(1);
    dynamic_array_insert_ordered(&lines, line, line_index);
}

void editor_text_add_line_empty(int line_index, int indentation) {
    editor_text_add_line(line_index, indentation, string_create_static(""));
}

void editor_text_create()
{
    auto& result = syntax_editor.text;
    result.lines = dynamic_array_create_empty<Editor_Line>(1);
    editor_text_add_line_empty(0, 0);
}

void editor_text_destroy(Editor_Text* text)
{
    for (int i = 0; i < text->lines.size; i++) {
        editor_line_destroy(&text->lines[i]);
    }
    dynamic_array_destroy(&text->lines);
}

// Helpers
Token token_make_dummy() {
    Token t;
    t.type = Token_Type::INVALID;
    t.start_index = 0;
    t.end_index = 0;
    return t;
}

int get_cursor_token_index() {
    auto& c = syntax_editor.cursor;
    auto& line = syntax_editor.text.lines[syntax_editor.cursor.line_index];
    return character_index_to_token(&line.tokens, syntax_editor.cursor.char_index);
}

Token get_cursor_token()
{
    auto& line = syntax_editor.text.lines[syntax_editor.cursor.line_index];
    int tok_index = get_cursor_token_index();
    if (tok_index >= line.tokens.size) return token_make_dummy();
    return line.tokens[tok_index];
}

Array<Token> get_tokens() {
    auto& c = syntax_editor.cursor;
    auto& line = syntax_editor.text.lines[syntax_editor.cursor.line_index];
    return dynamic_array_as_array(&line.tokens);
}

void syntax_editor_sanitize_cursor()
{
    auto& editor = syntax_editor;
    auto& lines = editor.text.lines;
    auto& cursor = editor.cursor;
    cursor.line_index = math_clamp(cursor.line_index, 0, editor.mode == Editor_Mode::INPUT ? lines.size : math_maximum(0, lines.size - 1));
    auto& line = lines[cursor.line_index];
    cursor.char_index = math_clamp(cursor.char_index, 0, line.text.size);
    if (editor.mode == Editor_Mode::NORMAL) {
        //cursor.char_index = get_cursor_token().start_index;
    }
}

/*
void syntax_editor_load_text_file(const char* filename)
{
    auto& editor = syntax_editor;
    Optional<String> content = file_io_load_text_file(filename);
    SCOPE_EXIT(file_io_unload_text_file(&content););

    String result;
    if (content.available) {
        result = content.value;
    }
    else {
        result = string_create_static("main :: (x : int) -> void \n{\n\n}");
    }

    syntax_block_destroy(editor.root_block);
    editor.root_block = syntax_block_create_from_string(result);
    lexer_tokenize_block(editor.root_block, 0);
    editor.cursor_line = editor.root_block->lines[0];
    editor.file_path = filename;
}

void syntax_editor_save_text_file()
{
    String whole_text = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&whole_text));
    syntax_block_append_to_string(syntax_editor.root_block, &whole_text, 0);
    auto success = file_io_write_file(syntax_editor.file_path, array_create_static((byte*)whole_text.characters, whole_text.size));
    if (!success) {
        logg("Saving file failed for path \"%s\"\n", syntax_editor.file_path);
    }
    else {
        logg("Saved file \"%s\"!\n", syntax_editor.file_path);
    }
}
*/



// Commands
void normal_mode_handle_command(Normal_Command command)
{
    auto& editor = syntax_editor;
    auto& cursor = editor.cursor;
    auto& mode = editor.mode;
    auto& line = editor.text.lines[cursor.line_index];
    auto& tokens = line.tokens;

    editor.space_before_cursor = false;
    bool tokens_changed = false;
    switch (command)
    {
    case Normal_Command::INSERT_AFTER: {
        mode = Editor_Mode::INPUT;
        cursor.char_index = get_cursor_token().end_index;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::INSERT_BEFORE: {
        mode = Editor_Mode::INPUT;
        cursor.char_index = get_cursor_token().start_index;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::MOVE_LEFT: {
        auto index = get_cursor_token_index() - 1;
        if (index < 0) break;
        cursor.char_index = tokens[index].start_index;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::MOVE_RIGHT: {
        auto index = get_cursor_token_index() + 1;
        if (index >= tokens.size) break;
        cursor.char_index = tokens[index].start_index;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::INSERT_AT_LINE_END: {
        cursor.char_index = line.text.size;
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::INSERT_AT_LINE_START: {
        cursor.char_index = 0;
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::DELETE_TOKEN: {
        auto index = get_cursor_token_index();
        if (tokens.size == 0) {
            break;
        }
        dynamic_array_remove_ordered(&tokens, index);
        if (index > 0) {
            cursor.char_index = tokens[index - 1].end_index;
        }
        else {
            cursor.char_index = 0;
        }
        tokens_changed = true;
        break;
    }
    case Normal_Command::CHANGE_TOKEN: {
        auto index = get_cursor_token_index();
        if (tokens.size == 0) {
            mode = Editor_Mode::INPUT;
            break;
        }
        dynamic_array_remove_ordered(&tokens, index);
        if (index > 0) {
            cursor.char_index = tokens[index - 1].end_index;
        }
        else {
            cursor.char_index = 0;
        }
        tokens_changed = true;
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::MOVE_LINE_START: {
        cursor.char_index = 0;
        break;
    }
    case Normal_Command::MOVE_LINE_END: {
        cursor.char_index = line.text.size;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::ADD_LINE_ABOVE:
    case Normal_Command::ADD_LINE_BELOW: {
        bool below = command == Normal_Command::ADD_LINE_BELOW;
        int new_indent = line.indentation;
        if (below) {
            if (cursor.line_index + 1 < editor.text.lines.size) {
                new_indent = editor.text.lines[cursor.line_index + 1].indentation;
            }
        }
        editor_text_add_line_empty(cursor.line_index + (below ? 1 : 0), new_indent);
        cursor.char_index = 0;
        if (below) {
            cursor.line_index += 1;
        }
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::MOVE_UP: {
        cursor.line_index -= 1;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::MOVE_DOWN: {
        cursor.line_index += 1;
        syntax_editor_sanitize_cursor();
        break;
    }
    default: panic("");
    }

    if (tokens_changed) {
        lexer_tokens_to_text(&line.tokens, &line.text);
        lexer_tokenize_text(line.text, &line.tokens); // To rejoin operators, like ": int =" -> ":="
        syntax_editor_sanitize_cursor();
    }
}

void insert_mode_handle_command(Input_Command input)
{
    auto& mode = syntax_editor.mode;
    auto& cursor = syntax_editor.cursor;
    auto& line = syntax_editor.text.lines[cursor.line_index];
    auto& text = line.text;
    auto& pos = cursor.char_index;

    if (input.type != Input_Command_Type::IDENTIFIER_LETTER && input.type != Input_Command_Type::BACKSPACE &&
        input.type != Input_Command_Type::NUMBER_LETTER) {
        syntax_editor.space_before_cursor = false;
        if (input.type != Input_Command_Type::DELIMITER_LETTER) {
            syntax_editor.space_after_cursor = false;
        }
    }

    assert(mode == Editor_Mode::INPUT, "");
    syntax_editor_sanitize_cursor();

    // Handle Universal Inputs
    if (input.type == Input_Command_Type::EXIT_INSERT_MODE) {
        mode = Editor_Mode::NORMAL;
        //cursor -= 1;
        syntax_editor_sanitize_cursor();
        return;
    }
    if (input.type == Input_Command_Type::ENTER)
    {
        editor_text_add_line(cursor.line_index + 1, line.indentation, string_create_substring_static(&text, pos, text.size));
        string_truncate(&syntax_editor.text.lines[cursor.line_index].text, pos);
        cursor.line_index += 1;
        pos = 0;
        return;
    }
    if (input.type == Input_Command_Type::ADD_INDENTATION)
    {
        if (pos == 0) {
            line.indentation += 1;
            return;
        }
        editor_text_add_line(cursor.line_index + 1, line.indentation + 1, string_create_substring_static(&text, pos, text.size));
        string_truncate(&syntax_editor.text.lines[cursor.line_index].text, pos);
        cursor.line_index += 1;
        pos = 0;
        return;
    }
    if (input.type == Input_Command_Type::ENTER_REMOVE_ONE_INDENT)
    {
        editor_text_add_line(cursor.line_index + 1, math_maximum(0, line.indentation - 1), string_create_substring_static(&text, pos, text.size));
        string_truncate(&syntax_editor.text.lines[cursor.line_index].text, pos);
        cursor.line_index += 1;
        pos = 0;
        return;
    }
    if (input.type == Input_Command_Type::REMOVE_INDENTATION)
    {
        line.indentation = math_maximum(line.indentation-1, 0);
        return;
    }

    if (input.type == Input_Command_Type::MOVE_LEFT) {
        pos = math_maximum(0, pos - 1);
        return;
    }
    if (input.type == Input_Command_Type::MOVE_RIGHT) {
        pos = math_minimum(line.text.size, pos + 1);
        return;
    }

    switch (input.type)
    {
    case Input_Command_Type::DELIMITER_LETTER:
    {
        bool insert_double_after = false;
        bool skip_auto_input = false;
        char double_char = ' ';
        if (char_is_parenthesis(input.letter))
        {
            Parenthesis p = char_to_parenthesis(input.letter);
            // Check if line is properly parenthesised with regards to the one token I currently am on
            // This is easy to check, but design wise I feel like I only want to check the token I am on
            if (p.is_open)
            {
                int open_count = 0;
                int closed_count = 0;
                for (int i = 0; i < text.size; i++) {
                    char c = text[i];
                    if (!char_is_parenthesis(c)) continue;
                    Parenthesis found = char_to_parenthesis(c);
                    if (found.type == p.type) {
                        if (found.is_open) open_count += 1;
                        else closed_count += 1;
                    }
                }
                insert_double_after = open_count == closed_count;
                if (insert_double_after) {
                    p.is_open = false;
                    double_char = parenthesis_to_char(p);
                }
            }
            else {
                skip_auto_input = pos < text.size&& text[pos] == input.letter;
            }
        }
        if (input.letter == '"')
        {
            if (pos < text.size && text[pos] == '"') {
                skip_auto_input = true;
            }
            else {
                int count = 0;
                for (int i = 0; i < text.size; i++) {
                    if (text[i] == '"') count += 1;
                }
                if (count % 2 == 0) {
                    insert_double_after = true;
                    double_char = '"';
                }
            }
        }

        bool skip_move = string_test_char(text, pos - 1, ' ');
        if (skip_auto_input) {
            pos += skip_move ? 0 : 1;
            break;
        }
        if (insert_double_after) {
            string_insert_character_before(&text, double_char, pos);
        }
        string_insert_character_before(&text, input.letter, pos);
        pos += skip_move ? 0 : 1;
        break;
    }
    case Input_Command_Type::SPACE:
    {
        if (line.tokens.size == 0) {
            break;
        }

        auto token = get_cursor_token();
        if (token.type == Token_Type::COMMENT) {
            string_insert_character_before(&text, ' ', pos);
            pos += 1;
            break;
        }

        if (pos == token.start_index) {
            break;
        }
        if (pos >= token.start_index && pos < token.end_index)
        {
            if (token.type == Token_Type::OPERATOR) {
                break;
            }
            string_insert_character_before(&text, ' ', pos);
            pos += 1;
            break;
        }

        // At the end of token
        if (!is_space_critical(&token)) {
            break;
        }
        syntax_editor.space_before_cursor = true;
        break;
    }
    case Input_Command_Type::BACKSPACE:
    {
        if (syntax_editor.space_before_cursor) {
            syntax_editor.space_before_cursor = false;
            break;
        }
        if (string_test_char(text, pos - 1, ' ') && syntax_editor.space_after_cursor) {
            syntax_editor.space_after_cursor = false;
            pos -= 1;
            break;
        }
        if (pos == 0) {
            if (cursor.line_index == 0) {
                line.indentation = math_maximum(0, line.indentation - 1);
                break;
            }
            // Merge this line with previous one
            auto& prev_text = syntax_editor.text.lines[cursor.line_index - 1].text;
            int new_char = prev_text.size;
            string_append_string(&prev_text, &text);
            editor_text_remove_line(cursor.line_index);
            cursor.line_index -= 1;
            cursor.char_index = new_char;
            break;
        }

        syntax_editor.space_after_cursor = string_test_char(text, pos, ' ') || syntax_editor.space_after_cursor;
        string_remove_character(&text, pos - 1);
        pos -= 1;
        if (pos == text.size) {
            syntax_editor.space_before_cursor = string_test_char(text, pos - 1, ' ');
        }
        break;
    }
    case Input_Command_Type::NUMBER_LETTER:
    case Input_Command_Type::IDENTIFIER_LETTER:
    {
        if (syntax_editor.space_before_cursor) {
            syntax_editor.space_before_cursor = false;
            string_insert_character_before(&text, ' ', pos);
            pos += 1;
        }
        string_insert_character_before(&text, input.letter, pos);
        pos += 1;
        if (syntax_editor.space_after_cursor) {
            syntax_editor.space_after_cursor = false;
            string_insert_character_before(&text, ' ', pos);
        }
        break;
    }
    default: break;
    }
    syntax_editor_sanitize_cursor();
}

void syntax_editor_update()
{
    auto& input = syntax_editor.input;
    auto& mode = syntax_editor.mode;

    /*
    if (syntax_editor.input->key_pressed[(int)Key_Code::O] && syntax_editor.input->key_down[(int)Key_Code::CTRL]) {
        auto open_file = file_io_open_file_selection_dialog();
        if (open_file.available) {
            syntax_editor_load_text_file(open_file.value.characters);
            //compiler_compile(syntax_editor.root_block, false, string_create(syntax_editor.file_path));
        }
        return;
    }
    if (syntax_editor.input->key_pressed[(int)Key_Code::S] && syntax_editor.input->key_down[(int)Key_Code::CTRL]) {
        syntax_editor_save_text_file();
    }
    */

    for (int i = 0; i < input->key_messages.size; i++)
    {
        Key_Message msg = input->key_messages[i];
        if (mode == Editor_Mode::INPUT)
        {
            Input_Command input;
            if (msg.character == 32 && msg.key_down) {
                input.type = Input_Command_Type::SPACE;
            }
            else if (msg.key_code == Key_Code::L && msg.key_down && msg.ctrl_down) {
                input.type = Input_Command_Type::EXIT_INSERT_MODE;
            }
            else if (msg.key_code == Key_Code::ARROW_LEFT && msg.key_down) {
                input.type = Input_Command_Type::MOVE_LEFT;
            }
            else if (msg.key_code == Key_Code::ARROW_RIGHT && msg.key_down) {
                input.type = Input_Command_Type::MOVE_RIGHT;
            }
            else if (msg.key_code == Key_Code::BACKSPACE && msg.key_down) {
                input.type = Input_Command_Type::BACKSPACE;
            }
            else if (msg.key_code == Key_Code::RETURN && msg.key_down) {
                if (msg.shift_down) {
                    input.type = Input_Command_Type::ENTER_REMOVE_ONE_INDENT;
                }
                else {
                    input.type = Input_Command_Type::ENTER;
                }
            }
            else if (char_is_letter(msg.character) || msg.character == '_') {
                input.type = Input_Command_Type::IDENTIFIER_LETTER;
                input.letter = msg.character;
            }
            else if (char_is_digit(msg.character)) {
                input.type = Input_Command_Type::NUMBER_LETTER;
                input.letter = msg.character;
            }
            else if (msg.key_code == Key_Code::TAB && msg.key_down) {
                if (msg.shift_down) {
                    input.type = Input_Command_Type::REMOVE_INDENTATION;
                }
                else {
                    input.type = Input_Command_Type::ADD_INDENTATION;
                }
            }
            else if (msg.key_down && msg.character != -1) {
                if (string_contains_character(characters_get_non_identifier_non_whitespace(), msg.character)) {
                    input.type = Input_Command_Type::DELIMITER_LETTER;
                    input.letter = msg.character;
                }
                else {
                    continue;
                }
            }
            else {
                continue;
            }
            insert_mode_handle_command(input);
        }
        else
        {
            syntax_editor.space_before_cursor = false;
            syntax_editor.space_after_cursor = false;
            Normal_Command command;
            if (msg.key_code == Key_Code::L && msg.key_down) {
                command = Normal_Command::MOVE_RIGHT;
            }
            else if (msg.key_code == Key_Code::H && msg.key_down) {
                command = Normal_Command::MOVE_LEFT;
            }
            else if (msg.key_code == Key_Code::J && msg.key_down) {
                command = Normal_Command::MOVE_DOWN;
            }
            else if (msg.key_code == Key_Code::K && msg.key_down) {
                command = Normal_Command::MOVE_UP;
            }
            else if (msg.key_code == Key_Code::O && msg.key_down) {
                if (msg.shift_down) {
                    command = Normal_Command::ADD_LINE_ABOVE;
                }
                else {
                    command = Normal_Command::ADD_LINE_BELOW;
                }
            }
            else if (msg.key_code == Key_Code::NUM_0 && msg.key_down) {
                command = Normal_Command::MOVE_LINE_START;
            }
            else if (msg.key_code == Key_Code::NUM_4 && msg.key_down && msg.shift_down) {
                command = Normal_Command::MOVE_LINE_END;
            }
            else if (msg.key_code == Key_Code::A && msg.key_down) {
                if (msg.shift_down) {
                    command = Normal_Command::INSERT_AT_LINE_END;
                }
                else {
                    command = Normal_Command::INSERT_AFTER;
                }
            }
            else if (msg.key_code == Key_Code::I && msg.key_down) {
                if (msg.shift_down) {
                    command = Normal_Command::INSERT_AT_LINE_START;
                }
                else {
                    command = Normal_Command::INSERT_BEFORE;
                }
            }
            else if (msg.key_code == Key_Code::R && msg.key_down) {
                command = Normal_Command::CHANGE_TOKEN;
            }
            else if (msg.key_code == Key_Code::X && msg.key_down) {
                command = Normal_Command::DELETE_TOKEN;
            }
            else {
                continue;
            }
            normal_mode_handle_command(command);
        }
    }
    //syntax_block_sanity_check(syntax_editor.root_block);

    // Tokenize all lines for now
    for (int i = 0; i < syntax_editor.text.lines.size; i++) {
        auto& line = syntax_editor.text.lines[i];
        lexer_tokenize_text(line.text, &line.tokens);
        lexer_tokens_to_text(&line.tokens, &line.text);
    }

    if (syntax_editor.input->key_pressed[(int)Key_Code::F5] && false)
    {
        auto ast_errors = Parser::get_error_messages();
        for (int i = 0; i < ast_errors.size; i++) {
            auto& error = ast_errors[i];
            logg("AST_Error: %s\n", error.msg);
        }

        //compiler_compile(syntax_editor.root_block, true, string_create(syntax_editor.file_path));

        if (!compiler_errors_occured()) {
            auto exit_code = compiler_execute();
            String output = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&output));
            exit_code_append_to_string(&output, exit_code);
            logg("\nProgram Exit with Code: %s\n", output.characters);
        }
        else
        {
            // Print errors
            auto parse_errors = Parser::get_error_messages();
            for (int i = 0; i < parse_errors.size; i++) {
                logg("Parse Error: \"%s\"\n", parse_errors[i].msg);
            }
            for (int i = 0; i < compiler.dependency_analyser->errors.size; i++) {
                Symbol_Error error = compiler.dependency_analyser->errors[i];
                logg("Symbol error: Redefinition of \"%s\"\n", error.existing_symbol->id->characters);
            }
            {
                String tmp = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&tmp));
                for (int i = 0; i < compiler.semantic_analyser->errors.size; i++)
                {
                    Semantic_Error e = compiler.semantic_analyser->errors[i];
                    semantic_error_append_to_string(e, &tmp);
                    logg("Semantic Error: %s\n", tmp.characters);
                    string_reset(&tmp);
                }
            }
        }
    }
    else {
        //compiler_compile(syntax_editor.root_block, false, string_create(syntax_editor.file_path));
    }
}

void syntax_editor_initialize(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Input* input, Timer* timer)
{
    memory_zero(&syntax_editor);
    editor_text_create();
    syntax_editor.cursor.char_index = 0;
    syntax_editor.cursor.line_index = 0;
    syntax_editor.mode = Editor_Mode::INPUT;
    syntax_editor.context_text = string_create_empty(256);

    syntax_editor.text_renderer = text_renderer;
    syntax_editor.rendering_core = rendering_core;
    syntax_editor.renderer_2D = renderer_2D;
    syntax_editor.input = input;

    compiler_initialize(timer);
    compiler_run_testcases(timer);
    //syntax_editor_load_text_file("upp_code/editor_text.upp");
}

void syntax_editor_destroy()
{
    auto& editor = syntax_editor;
    editor_text_destroy(&editor.text);
    compiler_destroy();
    string_destroy(&syntax_editor.context_text);
}



// Rendering
vec2 text_to_screen_coord(int line, int character)
{
    auto editor = &syntax_editor;
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
    vec2 line_start = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = line_start + vec2(character * char_size.x, -(line + 1) * char_size.y);
    return cursor;
}

void syntax_editor_draw_underline(int line, int character, int length, vec3 color)
{
    float w = syntax_editor.rendering_core->render_information.viewport_width;
    float h = syntax_editor.rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 char_size = syntax_editor.character_size * scaling_factor;
    vec2 size = char_size * vec2((float)length, 1 / 8.0f);
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * char_size.x, -(line + 1) * char_size.y);
    cursor = cursor + size * vec2(0.5f, 0.5f);
    renderer_2D_add_rectangle(syntax_editor.renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_cursor_line(vec3 color, int line, int character)
{
    auto editor = &syntax_editor;
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

void syntax_editor_draw_character_box(vec3 color, int line, int character)
{
    auto editor = &syntax_editor;
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

void syntax_editor_draw_string(String string, vec3 color, int line, int character)
{
    auto editor = &syntax_editor;
    vec2 pos = vec2(-1.0f, 1.0f) + vec2(character, -(line + 1)) * editor->character_size;
    text_renderer_set_color(editor->text_renderer, color);
    text_renderer_add_text(editor->text_renderer, &string, pos, editor->character_size.y, 1.0f);
}

void syntax_editor_draw_string_in_box(String string, vec3 text_color, vec3 box_color, int line, int character, float text_size)
{
    int text_width = 0;
    int max_text_width = 0;
    int text_height = 1;
    for (int i = 0; i < string.size; i++) {
        auto c = string[i];
        if (c == '\n') {
            text_width = 0;
            text_height += 1;
        }
        else {
            text_width += 1;
            max_text_width = math_maximum(max_text_width, text_width);
        }
    }

    auto editor = &syntax_editor;
    vec2 pos = vec2(-1.0f, 1.0f) + vec2(character, -line) * editor->character_size + vec2(0.0f, -editor->character_size.y * text_size * text_height);
    text_renderer_set_color(editor->text_renderer, text_color);
    text_renderer_add_text(editor->text_renderer, &string, pos, editor->character_size.y * text_size, 1.0f);

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
    size = size * vec2(max_text_width, text_height) * text_size;
    cursor = cursor + size * vec2(0.5f, -0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, box_color, 0.0f);
}



void syntax_editor_base_set_section_color(AST::Base* base, Parser::Section section, vec3 color)
{
    assert(base != 0, "");
    auto ranges = dynamic_array_create_empty<Token_Range>(1);
    SCOPE_EXIT(dynamic_array_destroy(&ranges));

    auto& node = base;
    /*
    Parser::ast_base_get_section_token_range(node, section, &ranges);
    for (int i = 0; i < ranges.size; i++)
    {
        auto range = ranges[i];
        // Set end to previous line if its at start of next line
        if (range.end.token_index == 0 && range.end.line_index > 0)
        {
            range.end.line_index -= 1;
            range.end.token_index = syntax_position_get_line(range.end)->tokens.size;
        }
        // TODO: Currently looping over all tokens in a range only works if start and end is in one line
        if (range.start.block == range.end.block && range.start.line_index == range.end.line_index)
        {
            Syntax_Position iter = range.start;
            while (syntax_position_on_token(iter) && iter.token_index < range.end.token_index) {
                syntax_position_get_token(iter)->info.screen_color = color;
                iter.token_index += 1;
            }
        }
    }
    */
}

void syntax_editor_underline_syntax_range(Token_Range range)
{
    /*
    auto index = syntax_position_sanitize(range.start);
    auto end = syntax_position_sanitize(range.end);

    assert(syntax_position_in_order(index, end), "hey");
    if (syntax_position_equal(index, end)) {
        auto line_info = syntax_position_get_line(index)->info;
        if (syntax_position_on_token(index)) {
            auto& info = syntax_position_get_token(index)->info;
            syntax_editor_draw_underline(line_info.index, info.screen_pos, info.screen_size, vec3(1.0f, 0.0f, 0.0f));
        }
        else {
            syntax_editor_draw_underline(line_info.index, line_info.line_end, 1, vec3(1.0f, 0.0f, 0.0f));
        }
        return;
    }

    auto line_start = index;
    while (true)
    {
        auto line = syntax_position_get_line(line_start);
        auto next = syntax_position_advance_one_token(index);
        if (next.line_index != index.line_index)
        {
            // Draw underline from start token to end of line
            if (syntax_position_on_token(line_start)) {
                int start_char = syntax_position_get_token(line_start)->info.screen_pos;
                int end_char = line->info.line_end;
                syntax_editor_draw_underline(line->info.index, start_char, end_char - start_char, vec3(1.0f, 0.0f, 0.0f));
            }
            line_start = next;
        }

        if (syntax_position_equal(next, end))
        {
            int start_char = line->parent_block->info.indentation_level * 4;
            if (syntax_position_on_token(line_start)) {
                start_char = syntax_position_get_token(line_start)->info.screen_pos;
            }
            int end_char = line->info.line_end;
            if (syntax_position_on_token(end)) {
                auto& info = syntax_position_get_token(end)->info;
                end_char = info.screen_pos + info.screen_size;
            }
            syntax_editor_draw_underline(line->info.index, start_char, end_char - start_char, vec3(1.0f, 0.0f, 0.0f));
            break;
        }

        if (syntax_position_equal(next, index)) {
            logg("SHouldn't happen");
            break;
        }
        index = next;
    }
    */
}

void syntax_editor_find_syntax_highlights(AST::Base* base)
{
    switch (base->type)
    {
    case AST::Base_Type::DEFINITION: {
        auto definition = (AST::Definition*) base;
        if (definition->symbol != 0) {
            auto color = symbol_type_to_color(definition->symbol->type);
            syntax_editor_base_set_section_color(base, Parser::Section::IDENTIFIER, color);
        }
        break;
    }
    case AST::Base_Type::SYMBOL_READ: {
        auto read = (AST::Symbol_Read*) base;
        if (read->resolved_symbol != 0) {
            auto color = symbol_type_to_color(read->resolved_symbol->type);
            syntax_editor_base_set_section_color(base, Parser::Section::IDENTIFIER, color);
        }
        break;
    }
    }

    // Do syntax highlighting for children
    int index = 0;
    auto child = AST::base_get_child(base, index);
    while (child != 0)
    {
        syntax_editor_find_syntax_highlights(child);
        index += 1;
        child = AST::base_get_child(base, index);
    }
}


void operator_space_before_after(int line_index, int token_index, bool& space_before, bool& space_after)
{
    auto& tokens = syntax_editor.text.lines[line_index].tokens;
    assert(tokens[token_index].type == Token_Type::OPERATOR, "");
    auto& op_info = syntax_operator_info(tokens[token_index].options.op);
    space_before = false;
    space_after = false;

    bool use_info = true;
    // Approximate if Operator is binop or not (Sometimes this cannot be detected with parsing alone)
    if (op_info.type == Operator_Type::BOTH)
    {
        // Current approximation for is_binop: The current and previous type has to be a value
        use_info = false;
        if (token_index > 0 && token_index + 1 < tokens.size)
        {
            bool prev_is_value = false;
            {
                const auto& t = tokens[token_index - 1];
                if (t.type == Token_Type::IDENTIFIER || t.type == Token_Type::LITERAL) {
                    prev_is_value = true;
                }
                if (t.type == Token_Type::PARENTHESIS && !t.options.parenthesis.is_open) {
                    prev_is_value = true;
                }
            }
            bool next_is_value = false;
            {
                const auto& t = tokens[token_index + 1];
                if (t.type == Token_Type::IDENTIFIER || t.type == Token_Type::LITERAL) {
                    next_is_value = true;
                }
                if (t.type == Token_Type::PARENTHESIS && t.options.parenthesis.is_open) {
                    next_is_value = true;
                }
                if (t.type == Token_Type::OPERATOR) { // Operators usually indicate values, but this is just an approximation
                    next_is_value = true;
                }
            }
            use_info = prev_is_value && next_is_value;
        }
        /*
        // OLD IS_BINOP APPROXIMATION
        use_info = !(prev_type == Token_Type::OPERATOR ||
            (prev_type == Token_Type::PARENTHESIS && tokens[token_index - 1].options.parenthesis.is_open) ||
            (prev_type == Token_Type::PARENTHESIS && tokens[token_index - 1].options.parenthesis.type == Parenthesis_Type::BRACKETS) ||
            (prev_type == Token_Type::KEYWORD));
        */
    }

    if (use_info) {
        space_before = op_info.space_before;
        space_after = op_info.space_after;
    }
}

bool display_space_between_token(int line_index, int token_index)
{
    auto& line = syntax_editor.text.lines[line_index];
    auto& tokens = line.tokens;
    auto& text = line.text;
    if (token_index + 1 >= tokens.size) return false;

    auto& a = tokens[token_index];
    auto& b = tokens[token_index + 1];

    // Space critical tokens
    if (string_test_char(text, a.end_index, ' ')) {
        return true;
    }

    // End of line comments
    if (b.type == Token_Type::COMMENT) {
        return true;
    }

    // Invalids should always have space before and after
    if (a.type == Token_Type::INVALID || b.type == Token_Type::INVALID) {
        return true;
    }

    // Special keyword handling
    if (a.type == Token_Type::KEYWORD && !is_space_critical(&b)) {
        return true;
    }

    // Ops that have spaces after
    if (a.type == Token_Type::OPERATOR)
    {
        bool unused, space_after;
        operator_space_before_after(line_index, token_index, unused, space_after);
        if (space_after) {
            return true;
        }
    }

    // Ops that have spaces before
    if (b.type == Token_Type::OPERATOR)
    {
        bool unused, space_before;
        operator_space_before_after(line_index, token_index + 1, space_before, unused);
        if (space_before) {
            return true;
        }
    }

    if (a.type == Token_Type::PARENTHESIS) {
        // Closed paranethesis should have a space after if it isn't used as an array type
        if (!a.options.parenthesis.is_open && is_space_critical(&b) && a.options.parenthesis.type != Parenthesis_Type::BRACKETS) {
            return true;
        }
    }

    return false;
}

void syntax_editor_render()
{
    auto& editor = syntax_editor;
    auto& cursor = syntax_editor.cursor;

    // Prepare Render
    editor.character_size.y = text_renderer_cm_to_relative_height(editor.text_renderer, editor.rendering_core, 0.55f);
    editor.character_size.x = text_renderer_get_cursor_advance(editor.text_renderer, editor.character_size.y);

    // Layout Source Code
    syntax_editor_sanitize_cursor();
    for (int i = 0; i < editor.text.lines.size; i++)
    {
        auto& line = editor.text.lines[i];
        dynamic_array_reset(&line.infos);
        int pos = line.indentation * 4;
        for (int j = 0; j < line.tokens.size; j++)
        {
            auto& token = line.tokens[j];
            bool on_token = cursor.line_index == i && get_cursor_token_index() == j;
            if (on_token && syntax_editor.space_after_cursor && token.start_index == cursor.char_index) {
                pos += 1;
            }

            Render_Info info;
            info.line = i;
            info.pos = pos;
            info.size = token.end_index - token.start_index;
            pos += info.size;

            if (on_token && syntax_editor.space_before_cursor) {
                pos += 1;
            }

            // Check for space critical tokens
            if (j + 1 < line.tokens.size) {
                auto& next_token = line.tokens[j + 1];
                if (display_space_between_token(i, j)) {
                    pos += 1;
                }
            }
            dynamic_array_push_back(&line.infos, info);
        }
    }

    // Draw Cursor
    {
        auto& line = syntax_editor.text.lines[cursor.line_index];
        auto token_index = get_cursor_token_index();
        Render_Info info;
        if (token_index < line.infos.size) {
            info = line.infos[token_index];
        }
        else {
            info.pos = line.indentation*4;
            info.size = 1;
            info.line = cursor.line_index;
        }

        if (editor.mode == Editor_Mode::NORMAL)
        {
            int box_start = info.pos;
            int box_end = math_maximum(info.pos + info.size, box_start + 1);
            //int box_end = box_start + 1;
            for (int i = box_start; i < box_end; i++) {
                syntax_editor_draw_character_box(vec3(0.2f), cursor.line_index, i);
            }
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, cursor.line_index, box_start);
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, cursor.line_index, box_end);
        }
        else
        {
            int cursor_pos = line.indentation*4;
            if (line.tokens.size != 0) {
                auto tok_start = line.tokens[token_index].start_index;
                int cursor_offset = cursor.char_index - tok_start;
                cursor_pos = info.pos + cursor_offset;

                if (editor.space_after_cursor && cursor_offset == 0) {
                    cursor_pos = info.pos - 1;
                }
            }
            if (editor.space_before_cursor && !editor.space_after_cursor) {
                cursor_pos += 1;
            }
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, cursor.line_index, cursor_pos);
        }
    }

    // Draw Text-Representation @ the bottom of the screen 
    if (true)
    {
        int line_index = 2.0f / editor.character_size.y - 1;
        syntax_editor_draw_string(editor.text.lines[cursor.line_index].text, Syntax_Color::TEXT, line_index, 0);
        if (editor.mode == Editor_Mode::NORMAL) {
            syntax_editor_draw_character_box(Syntax_Color::COMMENT, line_index, cursor.char_index);
        }
        else {
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line_index, cursor.char_index);
        }
    }

    bool show_context_info = false;
    auto& context = syntax_editor.context_text;
    string_reset(&context);

    // Draw error messages
    /*
    {
        Syntax_Position cursor_pos;
        cursor_pos.block = syntax_editor.cursor_line->parent_block;
        cursor_pos.line_index = syntax_line_index(syntax_editor.cursor_line);
        cursor_pos.token_index = get_cursor_token_index();

        // Parser errors
        auto parse_errors = Parser::get_error_messages();
        for (int i = 0; i < parse_errors.size; i++)
        {
            auto& error = parse_errors[i];
            syntax_editor_underline_syntax_range(error.range);
            if (syntax_range_contains(error.range, cursor_pos)) {
                show_context_info = true;
                if (context.size != 0) {
                    string_append(&context, "\n\n");
                }
                string_append_formated(&context, error.msg);
            }
        }

        // Semantic Analysis Errors
        auto error_ranges = dynamic_array_create_empty<Syntax_Range>(1);
        SCOPE_EXIT(dynamic_array_destroy(&error_ranges));

        for (int i = 0; i < compiler.dependency_analyser->errors.size; i++)
        {
            auto& error = compiler.dependency_analyser->errors[i];
            auto& node = error.error_node;
            dynamic_array_reset(&error_ranges);
            if (node == 0) continue;
            if (code_source_from_ast(node) != compiler.main_source) continue;
            Parser::ast_base_get_section_token_range(node, Parser::Section::IDENTIFIER, &error_ranges);
            for (int j = 0; j < error_ranges.size; j++)
            {
                auto& range = error_ranges[j];
                syntax_editor_underline_syntax_range(range);
                if (syntax_range_contains(range, cursor_pos)) {
                    show_context_info = true;
                    if (context.size != 0) {
                        string_append(&context, "\n\n");
                    }
                    string_append_formated(&context, "Symbol already exists");
                }
            }
        }

        for (int i = 0; i < compiler.semantic_analyser->errors.size; i++)
        {
            auto& error = compiler.semantic_analyser->errors[i];
            auto& node = error.error_node;
            dynamic_array_reset(&error_ranges);
            if (node == 0) continue;
            if (code_source_from_ast(node) != compiler.main_source) continue;
            Parser::ast_base_get_section_token_range(node, semantic_error_get_section(error), &error_ranges);
            for (int j = 0; j < error_ranges.size; j++)
            {
                auto& range = error_ranges[j];
                syntax_editor_underline_syntax_range(range);
                if (syntax_range_contains(range, cursor_pos)) {
                    show_context_info = true;
                    if (context.size != 0) {
                        string_append(&context, "\n\n");
                    }
                    semantic_error_append_to_string(error, &context);
                }
            }
        }
    }
    */

    // Draw context
    /*
    if (show_context_info || true)
    {
        auto& info = get_cursor_token().info;
        int cursor_char = info.screen_pos + (cursor - info.char_start);
        int cursor_line = editor.cursor_line->info.index;
        syntax_editor_draw_string_in_box(context, vec3(1.0f), vec3(0.2f), cursor_line + 1, cursor_char, 0.8f);
    }
    */

    // Syntax Highlighting
    //syntax_editor_find_syntax_highlights(&compiler.main_source->ast->base);

    // Render Source Code
    for (int i = 0; i < editor.text.lines.size; i++)
    {
        auto& line = editor.text.lines[i];
        for (int j = 0; j < line.tokens.size; j++)
        {
            auto& token = line.tokens[j];
            auto& info = line.infos[j];
            auto str = token_get_string(token, line.text);
            syntax_editor_draw_string(str, Syntax_Color::TEXT, info.line, info.pos);
        }
    }

    // Render Primitives
    renderer_2D_render(editor.renderer_2D, editor.rendering_core);
    text_renderer_render(editor.text_renderer, editor.rendering_core);
}




