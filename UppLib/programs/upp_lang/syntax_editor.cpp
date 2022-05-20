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
#include "rc_analyser.hpp"
#include "semantic_analyser.hpp"
#include "parser.hpp"
#include "source_code.hpp"
#include "../../win32/windows_helper_functions.hpp"

// Datatypes
enum class Editor_Mode
{
    NORMAL,
    INPUT,
};

struct Syntax_Editor
{
    // Editing
    Editor_Mode mode;
    Syntax_Block* root_block;
    int cursor_index; 
    Syntax_Line* cursor_line;

    String context_text;

    // Rendering
    Input* input;
    Rendering_Core* rendering_core;
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

// Prototypes
void syntax_editor_layout_block(Syntax_Block* block, int indentation, int* line_index);

// Helpers
int get_cursor_token_index() {
    return syntax_line_character_to_token_index(syntax_editor.cursor_line, syntax_editor.cursor_index);
}

Syntax_Token get_cursor_token()
{
    auto& cursor = syntax_editor.cursor_index;
    auto& tokens = syntax_editor.cursor_line->tokens;
    return tokens[get_cursor_token_index()];
}

void syntax_editor_sanitize_cursor()
{
    auto& editor = syntax_editor;
    assert(editor.cursor_line != 0, "");
    auto& text = editor.cursor_line->text;
    auto& cursor = editor.cursor_index;
    cursor = math_clamp(cursor, 0, editor.mode == Editor_Mode::INPUT ? text.size : math_maximum(0, text.size - 1));
    if (editor.mode == Editor_Mode::NORMAL) {
        cursor = get_cursor_token().info.char_start;
    }
}

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



// Commands
void normal_mode_handle_command(Normal_Command command)
{
    auto& editor = syntax_editor;
    auto& line = editor.cursor_line;
    auto& block = line->parent_block;
    auto& cursor = editor.cursor_index;
    auto& mode = editor.mode;
    auto& tokens = line->tokens;

    bool tokens_changed = false;
    switch (command)
    {
    case Normal_Command::INSERT_AFTER: {
        mode = Editor_Mode::INPUT;
        cursor = get_cursor_token().info.char_end;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::INSERT_BEFORE: {
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::MOVE_LEFT: {
        int cursor_token = math_maximum(get_cursor_token_index() - 1, 0);
        if (cursor_token < tokens.size) {
            cursor = tokens[cursor_token].info.char_start;
        }
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::MOVE_RIGHT: {
        int cursor_token = math_minimum(get_cursor_token_index() + 1, tokens.size);
        if (cursor_token < tokens.size) {
            cursor = tokens[cursor_token].info.char_start;
        }
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::INSERT_AT_LINE_END: {
        cursor = line->text.size;
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::INSERT_AT_LINE_START: {
        cursor = 0;
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::DELETE_TOKEN: {
        auto index = get_cursor_token_index();
        syntax_line_remove_token(line, index);
        if (index > 0) {
            cursor = tokens[index - 1].info.char_end + 1;
        }
        tokens_changed = true;
        break;
    }
    case Normal_Command::CHANGE_TOKEN: {
        syntax_line_remove_token(line, get_cursor_token_index());
        tokens_changed = true;
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::MOVE_LINE_START: {
        cursor = 0;
        break;
    }
    case Normal_Command::MOVE_LINE_END: {
        cursor = tokens[tokens.size - 1].info.char_start;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::ADD_LINE_ABOVE:
    case Normal_Command::ADD_LINE_BELOW: {
        bool below = command == Normal_Command::ADD_LINE_BELOW;
        line = syntax_line_create(line->parent_block, syntax_line_index(line) + (below ? 1 : 0));
        cursor = 0;
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::MOVE_UP: {
        line = syntax_line_prev_line(line);
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::MOVE_DOWN: {
        line = syntax_line_next_line(line);
        syntax_editor_sanitize_cursor();
        break;
    }
    default: panic("");
    }

    if (tokens_changed) {
        lexer_reconstruct_line_text(line, 0);
        lexer_tokenize_syntax_line(line); // To rejoin operators, like ": int =" -> ":="
        syntax_editor_sanitize_cursor();
    }
}

void insert_mode_handle_command(Input_Command input)
{
    auto& mode = syntax_editor.mode;
    auto& line = syntax_editor.cursor_line;
    auto& text = line->text;
    auto& cursor = syntax_editor.cursor_index;

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
        auto old_line = line;
        auto& old_text = old_line->text;
        line = syntax_line_create(line->parent_block, syntax_line_index(line) + 1);
        auto cutout = string_create_substring_static(&old_text, cursor, old_text.size);
        string_append_string(&line->text, &cutout);
        string_truncate(&old_text, cursor);

        if (old_line->follow_block != 0) {
            line->follow_block = old_line->follow_block;
            old_line->follow_block = 0;
            line->follow_block->parent_line = line;
        }

        cursor = 0;
        return;
    }
    if (input.type == Input_Command_Type::ADD_INDENTATION)
    {
        auto old_line = line;
        auto& old_text = old_line->text;
        auto cutout = string_create_substring_static(&old_text, cursor, old_text.size);
        int line_index = syntax_line_index(line);

        if (cursor == 0 && line_index > 0)
        {
            Syntax_Line* add_to_line = line->parent_block->lines[line_index - 1];
            if (add_to_line->follow_block == 0)
            {
                Syntax_Block* block = syntax_block_create(add_to_line);
                syntax_line_destroy(block->lines[0]);
                block->lines[0] = line;
                auto old_block = line->parent_block;
                line->parent_block = block;
                dynamic_array_remove_ordered(&old_block->lines, line_index);
            }
            else {
                syntax_line_move(line, add_to_line->follow_block, add_to_line->follow_block->lines.size);
            }
            return;
        }

        if (line->follow_block == 0) syntax_block_create(line);
        else if (line->follow_block->lines[0]->text.size != 0) {
            syntax_line_create(line->follow_block, 0);
        }
        line = line->follow_block->lines[0];

        string_insert_string(&line->text, &cutout, 0);
        string_truncate(&old_text, cursor);
        cursor = 0;
        return;
    }
    if (input.type == Input_Command_Type::ENTER_REMOVE_ONE_INDENT)
    {
        auto old_line = line;
        auto& old_text = old_line->text;

        auto cutout = string_create_substring_static(&old_text, cursor, old_text.size);
        if (line->parent_block->parent_line == 0) return;
        line = line->parent_block->parent_line;
        line = syntax_line_create(line->parent_block, syntax_line_index(line) + 1);

        string_insert_string(&line->text, &cutout, 0);
        string_truncate(&old_text, cursor);
        cursor = 0;

        if (old_line->follow_block != 0) {
            line->follow_block = old_line->follow_block;
            old_line->follow_block = 0;
            line->follow_block->parent_line = line;
        }

        return;
    }
    if (input.type == Input_Command_Type::REMOVE_INDENTATION)
    {
        auto old_block = line->parent_block;
        auto& block = line->parent_block;
        if (block->parent_line == 0) return;

        int parent_line_index = syntax_line_index(block->parent_line);
        dynamic_array_remove_ordered(&block->lines, syntax_line_index(line));
        block = block->parent_line->parent_block;
        dynamic_array_insert_ordered(&block->lines, line, parent_line_index + 1);

        if (old_block->lines.size == 0) {
            old_block->parent_line->follow_block = 0;
            syntax_block_destroy(old_block);
        }
        return;
    }

    if (input.type == Input_Command_Type::MOVE_LEFT) {
        cursor = math_maximum(0, cursor - 1);
        return;
    }
    if (input.type == Input_Command_Type::MOVE_RIGHT) {
        cursor = math_minimum(line->text.size, cursor + 1);
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
                skip_auto_input = cursor < text.size&& text[cursor] == input.letter;
            }
        }
        if (input.letter == '"') {
            if (cursor < text.size && text[cursor] == '"') {
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
        if (skip_auto_input) {
            cursor += 1;
            break;
        }
        if (insert_double_after) {
            string_insert_character_before(&text, double_char, cursor);
        }
        string_insert_character_before(&text, input.letter, cursor);
        cursor += 1;
        break;
    }
    case Input_Command_Type::SPACE:
        string_insert_character_before(&text, ' ', cursor);
        cursor += 1;
        break;
    case Input_Command_Type::BACKSPACE:
    {
        if (cursor > 0) {
            string_remove_character(&text, cursor - 1);
            cursor -= 1;
            break;
        }
        // Cases: First line in a block or not
        auto line_index = syntax_line_index(line);
        auto block = line->parent_block;
        Syntax_Line* combine_with = syntax_line_prev_line(line);
        if (combine_with == line) {
            break;
        }

        cursor = combine_with->text.size;
        string_append_string(&combine_with->text, &line->text);
        string_reset(&line->text);

        if (line->follow_block != 0)
        {
            if (line_index == 0 && block->lines.size > 1) {

            }
            else {
                auto follow = line->follow_block;
                line->follow_block = 0;
                combine_with->follow_block = follow;
                follow->parent_line = combine_with;
            }
        }
        else {
            if (block->lines.size > 1) {
                syntax_line_destroy(line);
                dynamic_array_remove_ordered(&block->lines, line_index);
            }
            else {
                syntax_block_destroy(block);
                combine_with->follow_block = 0;
            }
        }

        line = combine_with;
        //line.indentation_level = math_maximum(0, line.indentation_level - 1);
        // TODO: Merge this line with last one...
        break;
    }
    case Input_Command_Type::IDENTIFIER_LETTER:
        string_insert_character_before(&text, input.letter, cursor);
        cursor += 1;
        break;
    case Input_Command_Type::NUMBER_LETTER:
        string_insert_character_before(&text, input.letter, cursor);
        cursor += 1;
        break;
    default: break;
    }
    syntax_editor_sanitize_cursor();
}

void syntax_editor_update()
{
    auto& input = syntax_editor.input;
    auto& mode = syntax_editor.mode;

    /*
    if (syntax_editor.input->key_pressed[(int)Key_Code::O]) {
        logg("O Pressed m8\n");
    }
    if (syntax_editor.input->key_down[(int)Key_Code::O]) {
        logg("O DOWN m8\n");
    }
    */

    if (syntax_editor.input->key_pressed[(int)Key_Code::O] && syntax_editor.input->key_down[(int)Key_Code::CTRL]) {
        auto open_file = open_file_selection_dialog();
        if (open_file.available) {
            syntax_editor_load_text_file(open_file.value.characters);
            compiler_compile(syntax_editor.root_block, false, string_create(syntax_editor.file_path));
        }
        return;
    }
    if (syntax_editor.input->key_pressed[(int)Key_Code::S] && syntax_editor.input->key_down[(int)Key_Code::CTRL]) {
        syntax_editor_save_text_file();
    }

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
    syntax_block_sanity_check(syntax_editor.root_block);

    int index = 0;
    syntax_editor_layout_block(syntax_editor.root_block, 0, &index);

    if (syntax_editor.input->key_pressed[(int)Key_Code::F5])
    {
        auto ast_errors = Parser::get_error_messages();
        for (int i = 0; i < ast_errors.size; i++) {
            auto& error = ast_errors[i];
            logg("AST_Error: %s\n", error.msg);
        }

        compiler_compile(syntax_editor.root_block, true, string_create(syntax_editor.file_path));

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
        compiler_compile(syntax_editor.root_block, false, string_create(syntax_editor.file_path));
    }
}

void syntax_editor_initialize(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Input* input, Timer* timer)
{
    memory_zero(&syntax_editor);
    syntax_editor.cursor_index = 0;
    syntax_editor.mode = Editor_Mode::INPUT;
    syntax_editor.root_block = syntax_block_create(0);
    syntax_editor.cursor_line = syntax_editor.root_block->lines[0];
    syntax_editor.context_text = string_create_empty(256);

    syntax_editor.text_renderer = text_renderer;
    syntax_editor.rendering_core = rendering_core;
    syntax_editor.renderer_2D = renderer_2D;
    syntax_editor.input = input;

    compiler_initialize(timer);
    compiler_run_testcases(timer);
    syntax_editor_load_text_file("upp_code/editor_text.upp");
}

void syntax_editor_destroy()
{
    auto& editor = syntax_editor;
    syntax_block_destroy(editor.root_block);
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



void syntax_editor_layout_line(Syntax_Line* line, int line_index)
{
    // Layout Tokens
    auto& tokens = line->tokens;
    int pos = line->parent_block->info.indentation_level * 4;
    line->info.index = line_index;
    for (int i = 0; i < tokens.size; i++)
    {
        auto& token = tokens[i];
        auto& info = token.info;
        String str = syntax_token_as_string(token);

        pos += info.format_space_before ? 1 : 0;
        info.screen_pos = pos;
        info.screen_size = str.size;
        vec3 color = Syntax_Color::TEXT;

        // This has to be the first if case, since multi-line comments may not start with ||
        if (token.type == Syntax_Token_Type::COMMENT || syntax_line_is_comment(line)) {
            color = Syntax_Color::COMMENT;
        }
        else if (token.type == Syntax_Token_Type::KEYWORD) {
            color = Syntax_Color::KEYWORD;
        }
        else if (token.type == Syntax_Token_Type::LITERAL_STRING) {
            color = Syntax_Color::STRING;
        }
        else if (token.type == Syntax_Token_Type::LITERAL_NUMBER) {
            color = Syntax_Color::LITERAL_NUMBER;
        }
        info.screen_color = color;

        pos += str.size + (info.format_space_after ? 1 : 0);
    }
    line->info.line_end = pos;
}

void syntax_editor_layout_block(Syntax_Block* block, int indentation, int* line_index)
{
    auto& info = block->info;
    info.line_start = *line_index;
    info.indentation_level = indentation;
    for (int i = 0; i < block->lines.size; i++)
    {
        auto& line = block->lines[i];
        lexer_tokenize_syntax_line(line);
        if (syntax_editor.mode == Editor_Mode::INPUT && syntax_editor.cursor_line == line) {
            lexer_reconstruct_line_text(line, &syntax_editor.cursor_index);
        }
        else {
            lexer_reconstruct_line_text(line, 0);
        }

        syntax_editor_layout_line(line, *line_index);
        *line_index += 1;
        if (line->follow_block != 0) {
            syntax_editor_layout_block(line->follow_block, indentation + 1, line_index);
        }
    }
    info.line_end = *line_index;
}

void syntax_line_render(Syntax_Line* line)
{
    auto& tokens = line->tokens;
    for (int i = 0; i < tokens.size; i++)
    {
        auto& token = tokens[i];
        String str = syntax_token_as_string(token);
        text_renderer_set_color(syntax_editor.text_renderer, token.info.screen_color);
        syntax_editor_draw_string(str, token.info.screen_color, line->info.index, token.info.screen_pos);
    }
}

void syntax_block_render(Syntax_Block* block)
{
    for (int i = 0; i < block->lines.size; i++)
    {
        auto& line = block->lines[i];
        syntax_line_render(line);
        if (line->follow_block != 0) {
            syntax_block_render(line->follow_block);
        }
    }

    auto& info = block->info;
    if (info.indentation_level != 0)
    {
        auto offset = syntax_editor.character_size * vec2(0.5f, 1.0f);
        vec2 start = text_to_screen_coord(info.line_start, (info.indentation_level - 1) * 4) + offset;
        vec2 end = text_to_screen_coord(info.line_end, (info.indentation_level - 1) * 4) + offset;
        start.y -= syntax_editor.character_size.y * 0.1;
        end.y += syntax_editor.character_size.y * 0.1;
        renderer_2D_add_line(syntax_editor.renderer_2D, start, end, vec3(0.4f), 3, 0.0f);
        vec2 l_end = end + vec2(syntax_editor.character_size.x * 0.5f, 0.0f);
        renderer_2D_add_line(syntax_editor.renderer_2D, end, l_end, vec3(0.4f), 3, 0.0f);
    }
}

void syntax_editor_base_set_section_color(AST::Base* base, Parser::Section section, vec3 color)
{
    assert(base != 0, "");
    auto ranges = dynamic_array_create_empty<Syntax_Range>(1);
    SCOPE_EXIT(dynamic_array_destroy(&ranges));

    auto& node = base;
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
}

void syntax_editor_underline_syntax_range(Syntax_Range range)
{
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

void syntax_editor_render()
{
    auto& editor = syntax_editor;
    auto& cursor = syntax_editor.cursor_index;

    // Prepare Render
    editor.character_size.y = text_renderer_cm_to_relative_height(editor.text_renderer, editor.rendering_core, 0.65f);
    editor.character_size.x = text_renderer_get_cursor_advance(editor.text_renderer, editor.character_size.y);

    // Layout Source Code
    int line_index = 0;
    syntax_editor_layout_block(editor.root_block, 0, &line_index);
    syntax_editor_sanitize_cursor();

    // Draw Cursor
    {
        Syntax_Line* line = syntax_editor.cursor_line;
        auto& info = get_cursor_token().info;
        int cursor_pos = info.screen_pos + (cursor - info.char_start);
        if (editor.mode == Editor_Mode::NORMAL)
        {
            int box_start = info.screen_pos;
            int box_end = math_maximum(info.screen_pos + info.screen_size, box_start + 1);
            for (int i = box_start; i < box_end; i++) {
                syntax_editor_draw_character_box(vec3(0.2f), line->info.index, i);
            }
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line->info.index, box_start);
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line->info.index, box_end);
        }
        else {
            if (info.format_space_before && cursor == info.char_start) {
                cursor_pos -= 1;
            }
            if (info.format_space_after && cursor > info.char_end) {
                cursor_pos = info.screen_pos + info.screen_size + 1;
            }
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line->info.index, cursor_pos);
        }

        // Draw Text-Representation @ the bottom of the screen 
        if (true)
        {
            int line_index = 2.0f / editor.character_size.y - 1;
            syntax_editor_draw_string(line->text, Syntax_Color::TEXT, line_index, 0);
            if (editor.mode == Editor_Mode::NORMAL) {
                syntax_editor_draw_character_box(Syntax_Color::COMMENT, line_index, cursor);
            }
            else {
                syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line_index, cursor);
            }
        }
    }

    bool show_context_info = false;
    auto& context = syntax_editor.context_text;
    string_reset(&context);

    // Draw error messages
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

    // Draw context
    if (show_context_info || true)
    {
        auto& info = get_cursor_token().info;
        int cursor_char = info.screen_pos + (cursor - info.char_start);
        int cursor_line = editor.cursor_line->info.index;
        syntax_editor_draw_string_in_box(context, vec3(1.0f), vec3(0.2f), cursor_line + 1, cursor_char, 0.8f);
    }

    // Syntax Highlighting
    syntax_editor_find_syntax_highlights(&compiler.main_source->ast->base);

    // Render Source Code
    syntax_block_render(editor.root_block);

    // Render Primitives
    renderer_2D_render(editor.renderer_2D, editor.rendering_core);
    text_renderer_render(editor.text_renderer, editor.rendering_core);
}
