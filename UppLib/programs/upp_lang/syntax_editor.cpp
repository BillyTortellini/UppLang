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
#include "code_history.hpp"
#include "lexer.hpp"

// Editor
struct Error_Display
{
    String message;
    Token_Range range;
};

Error_Display error_display_make(String msg, Token_Range range) {
    Error_Display result;
    result.message = msg;
    result.range = range;
    return result;
}

enum class Editor_Mode
{
    NORMAL,
    INPUT,
};

struct Syntax_Editor
{
    // Editing
    Editor_Mode mode;
    Source_Code* code;
    Code_History history;
    Text_Index cursor;
    History_Timestamp last_token_synchronized;
    History_Timestamp last_hierarchy_synchronized;

    bool space_before_cursor;
    bool space_after_cursor;
    bool code_changed_since_last_compile;

    // Rendering
    String context_text;
    Dynamic_Array<Error_Display> errors;

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
    UNDO,
    REDO,
};

// Globals
static Syntax_Editor syntax_editor;



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
    return character_index_to_token(&index_value(c.line)->tokens, c.pos);
}

Token get_cursor_token()
{
    auto& c = syntax_editor.cursor;
    int tok_index = get_cursor_token_index();
    auto& tokens = index_value(c.line)->tokens;
    if (tok_index >= tokens.size) return token_make_dummy();
    return tokens[tok_index];
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

    source_code_fill_from_string(editor.code, result);
    source_code_tokenize_block(block_index_make_root(editor.code), true);
    editor.cursor = text_index_make(block_get_start_line(block_index_make_root(editor.code)), 0);
    editor.file_path = filename;

    code_history_reset(&editor.history);
    editor.last_token_synchronized = history_get_timestamp(&editor.history);
    editor.last_hierarchy_synchronized = history_get_timestamp(&editor.history);
}

void syntax_editor_save_text_file()
{
    String whole_text = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&whole_text));
    source_code_append_to_string(syntax_editor.code, &whole_text);
    auto success = file_io_write_file(syntax_editor.file_path, array_create_static((byte*)whole_text.characters, whole_text.size));
    if (!success) {
        logg("Saving file failed for path \"%s\"\n", syntax_editor.file_path);
    }
    else {
        logg("Saved file \"%s\"!\n", syntax_editor.file_path);
    }
}

void syntax_editor_sanitize_cursor()
{
    auto& editor = syntax_editor;
    auto& cursor = editor.cursor;
    index_sanitize(&cursor.line);
    auto& text = index_value(cursor.line)->text;
    cursor.pos = math_clamp(cursor.pos, 0, editor.mode == Editor_Mode::INPUT ? text.size : math_maximum(0, text.size - 1));

    if (string_test_char(text, cursor.pos, ' ')) {
        editor.space_after_cursor = false;
    }
    if (string_test_char(text, cursor.pos - 1, ' ')) {
        editor.space_before_cursor = false;
    }
}

bool syntax_editor_sanitize_line(Line_Index line_index)
{
    // Remove all Spaces except space critical ones
    auto& editor = syntax_editor;
    auto line = index_value(line_index);
    auto& text = line->text;
    auto pos = editor.cursor.pos;

    bool changed = false;
    int index = 0;
    while (index < text.size)
    {
        char curr = text[index];
        char next = index + 1 < text.size ? text[index + 1] : '!'; // Any non-space critical chars will do
        char prev = index - 1 >= 0 ? text[index - 1] : '!';
        if (prev == '/' && curr == '/') break; // Skip comments
        // Skip strings
        if (curr == '"')
        {
            index += 1;
            while (index < text.size)
            {
                curr = text[index];
                if (curr == '\\') {
                    index += 2;
                    continue;
                }
                if (curr == '"') {
                    index += 1;
                    prev = curr;
                    break;
                }
                index += 1;
                prev = curr;
            }
            continue;
        }

        if (curr == ' ' && !(char_is_space_critical(prev) && char_is_space_critical(next))) {
            history_delete_char(&editor.history, text_index_make(editor.cursor.line, index));
            changed = true;
            if (pos > index) {
                pos -= 1;
            }
        }
        else {
            index += 1;
        }
    }

    if (index_equal(editor.cursor.line, line_index)) 
    {
        editor.cursor.pos = pos;
        syntax_editor_sanitize_cursor();
    }
    return changed;
}

void syntax_editor_synchronize_tokens()
{
    auto& editor = syntax_editor;
    // Get changes since last sync
    Dynamic_Array<Code_Change> changes = dynamic_array_create_empty<Code_Change>(1);
    SCOPE_EXIT(dynamic_array_destroy(&changes));
    auto now = history_get_timestamp(&editor.history);
    history_get_changes_between(&editor.history, editor.last_token_synchronized, now, &changes);
    editor.last_token_synchronized = now;
    if (changes.size != 0) {
        editor.code_changed_since_last_compile = true;
    }

    // Find out which lines were changed
    auto line_changes = dynamic_array_create_empty<Line_Index>(1);
    SCOPE_EXIT(dynamic_array_destroy(&line_changes));
    for (int i = 0; i < changes.size; i++)
    {
        auto& change = changes[i];
        switch (change.type)
        {
        case Code_Change_Type::BLOCK_CREATE: 
        {
            if (change.apply_forwards) break;
            for (int j = 0; j < line_changes.size; j++) {
                auto& line = line_changes[j];
                if (index_equal(line.block, change.options.block_create.new_block_index)) {
                    dynamic_array_swap_remove(&line_changes, j);
                    j -= 1;
                }
            }
            break;
        }
        case Code_Change_Type::BLOCK_INDEX_CHANGED: break;
        case Code_Change_Type::BLOCK_MERGE:
        {
            auto& merge = change.options.block_merge;
            for (int j = 0; j < line_changes.size; j++)
            {
                auto& line = line_changes[j];
                if (change.apply_forwards) 
                {
                    // Merge
                    if (index_equal(merge.merge_other, line.block)) {
                        int new_index = line.line;
                        if (new_index > merge.split_index) {
                            new_index -= merge.split_index;
                        }
                        line = line_index_make(merge.index, new_index);
                    }
                }
                else 
                { 
                    // Split
                    if (index_equal(merge.index, line.block) && line.line >= merge.split_index) {
                        line = line_index_make(merge.merge_other, line.line - merge.split_index);
                    }
                }
            }
            break;
        }
        case Code_Change_Type::LINE_INSERT:
        {
            // Update line indices in corresponding blocks
            auto& insert = change.options.line_insert;
            for (int j = 0; j < line_changes.size; j++)
            {
                auto& line = line_changes[j];
                if (!index_equal(line.block, insert.block)) continue;
                if (change.apply_forwards)
                {
                    // Line insertion
                    if (line.line >= insert.line) {
                        line.line += 1;
                    }
                }
                else
                {
                    // Line deletion
                    if (line.line > insert.line) {
                        line.line -= 1;
                    }
                    else if (line.line == insert.line) {
                        dynamic_array_swap_remove(&line_changes, j);
                        j -= 1;
                    }
                }
            }
            break;
        }
        case Code_Change_Type::TEXT_INSERT:
        {
            auto& changed_line = change.options.text_insert.index.line;
            bool found = false;
            for (int j = 0; j < line_changes.size; j++) {
                if (index_equal(line_changes[j], changed_line)) {
                    found = true;
                }
            }
            if (!found) {
                dynamic_array_push_back(&line_changes, changed_line);
            }
            break;
        }
        default: panic("");
        }
    }

    // Update changed lines
    for (int i = 0; i < line_changes.size; i++)
    {
        auto& index = line_changes[i];
        bool changed = syntax_editor_sanitize_line(index);
        assert(!changed, "Syntax editor has to make sure that lines are sanitized after edits!");

        auto line = index_value(line_changes[i]);
        lexer_tokenize_text(line->text, &line->tokens);
        logg("Synchronized: %d/%d\n", index.block.block, index.line);
    }
}




// Commands
void editor_enter_input_mode()
{
    syntax_editor.mode = Editor_Mode::INPUT;
    history_start_complex_command(&syntax_editor.history);
}

void editor_leave_input_mode()
{
    syntax_editor.mode = Editor_Mode::NORMAL;
    history_stop_complex_command(&syntax_editor.history);
    history_set_cursor_pos(&syntax_editor.history, syntax_editor.cursor);
}

void normal_mode_handle_command(Normal_Command command)
{
    auto& editor = syntax_editor;
    auto& cursor = editor.cursor;
    auto line = index_value(cursor.line);
    auto& tokens = line->tokens;

    SCOPE_EXIT(history_set_cursor_pos(&editor.history, editor.cursor));
    SCOPE_EXIT(syntax_editor_sanitize_cursor());

    editor.space_before_cursor = false;
    switch (command)
    {
    case Normal_Command::INSERT_AFTER: {
        syntax_editor_synchronize_tokens();
        editor_enter_input_mode();
        cursor.pos = get_cursor_token().end_index;
        break;
    }
    case Normal_Command::INSERT_BEFORE: {
        syntax_editor_synchronize_tokens();
        editor_enter_input_mode();
        cursor.pos = get_cursor_token().start_index;
        break;
    }
    case Normal_Command::MOVE_LEFT: {
        syntax_editor_synchronize_tokens();
        auto index = get_cursor_token_index() - 1;
        if (index < 0) break;
        cursor.pos = tokens[index].start_index;
        break;
    }
    case Normal_Command::MOVE_RIGHT: {
        syntax_editor_synchronize_tokens();
        auto index = get_cursor_token_index() + 1;
        if (index >= tokens.size) break;
        cursor.pos = tokens[index].start_index;
        break;
    }
    case Normal_Command::INSERT_AT_LINE_END: {
        cursor.pos = line->text.size;
        editor_enter_input_mode();
        break;
    }
    case Normal_Command::INSERT_AT_LINE_START: {
        cursor.pos = 0;
        editor_enter_input_mode();
        break;
    }
    case Normal_Command::UNDO: {
        history_undo(&editor.history);
        auto cursor_history = history_get_cursor_pos(&editor.history);
        if (cursor_history.available) {
            cursor = cursor_history.value;
        }
        break;
    }
    case Normal_Command::REDO: {
        history_redo(&editor.history);
        auto cursor_history = history_get_cursor_pos(&editor.history);
        if (cursor_history.available) {
            cursor = cursor_history.value;
        }
        break;
    }
    case Normal_Command::DELETE_TOKEN:
    case Normal_Command::CHANGE_TOKEN:
    {
        syntax_editor_synchronize_tokens();
        if (tokens.size == 0) {
            if (command == Normal_Command::CHANGE_TOKEN) {
                editor_enter_input_mode();
            }
            break;
        }

        auto index = get_cursor_token_index();

        bool critical_before = false;
        if (index > 0) {
            critical_before = char_is_space_critical(line->text[tokens[index - 1].end_index - 1]);
        }
        bool critical_after = false;
        if (index + 1 < tokens.size) {
            critical_after = char_is_space_critical(line->text[tokens[index + 1].start_index]);
        }

        history_start_complex_command(&syntax_editor.history);
        SCOPE_EXIT(history_stop_complex_command(&syntax_editor.history));

        // Delete
        int delete_start = index > 0 ? tokens[index - 1].end_index : 0;
        int delete_end = index + 1 < tokens.size ? tokens[index + 1].start_index : line->text.size;
        cursor.pos = delete_start;
        history_delete_text(&editor.history, text_index_make(cursor.line, delete_start), delete_end);

        // Re-Enter critical spaces
        if (critical_before && critical_after) {
            history_insert_char(&editor.history, text_index_make(cursor.line, delete_start), ' ');
            cursor.pos += 1;
        }
        if (command == Normal_Command::CHANGE_TOKEN) {
            if (!(critical_before && critical_after)) {
                editor.space_before_cursor = critical_before;
            }
            editor.space_after_cursor = critical_after;
            editor_enter_input_mode();
        }
        break;
    }
    case Normal_Command::MOVE_LINE_START: {
        cursor.pos = 0;
        break;
    }
    case Normal_Command::MOVE_LINE_END: {
        cursor.pos = line->text.size;
        break;
    }
    case Normal_Command::ADD_LINE_ABOVE:
    case Normal_Command::ADD_LINE_BELOW:
    {
        history_start_complex_command(&editor.history);
        SCOPE_EXIT(history_stop_complex_command(&editor.history));

        bool below = command == Normal_Command::ADD_LINE_BELOW;
        auto new_line = line_index_make(cursor.line.block, below ? cursor.line.line + 1 : cursor.line.line);
        history_insert_line(&editor.history, new_line, below);
        cursor = text_index_make(new_line, 0);
        editor_enter_input_mode();
        break;
    }
    case Normal_Command::MOVE_UP: {
        // FUTURE: Use token render positions to move up/down
        cursor.line = line_index_prev(cursor.line);
        break;
    }
    case Normal_Command::MOVE_DOWN: {
        cursor.line = line_index_next(cursor.line);
        break;
    }
    default: panic("");
    }
}

void split_line_at_cursor(int indentation_offset)
{
    auto& mode = syntax_editor.mode;
    auto& cursor = syntax_editor.cursor;
    auto line = index_value(cursor.line);
    auto& text = line->text;
    int cutof_index = text.size; // Text may be invalid after applying history changes
    String cutout = string_create_substring_static(&text, cursor.pos, text.size);

    history_start_complex_command(&syntax_editor.history);
    SCOPE_EXIT(history_stop_complex_command(&syntax_editor.history));

    Line_Index new_line_index = line_index_make(cursor.line.block, cursor.line.line + 1);
    history_insert_line(&syntax_editor.history, new_line_index, true);
    if (indentation_offset == 1) {
        new_line_index = history_add_line_indent(&syntax_editor.history, new_line_index);
    }
    else if (indentation_offset == -1) {
        new_line_index = history_remove_line_indent(&syntax_editor.history, new_line_index);
    }
    else if (indentation_offset != 0) {
        panic("Should not happen!");
    }

    if (cursor.pos != cutof_index) {
        history_insert_text(&syntax_editor.history, text_index_make(new_line_index, 0), cutout);
        history_delete_text(&syntax_editor.history, cursor, cutof_index);
    }
    syntax_editor_sanitize_line(cursor.line);
    syntax_editor_sanitize_line(new_line_index);
    cursor = text_index_make(new_line_index, 0);
}

void insert_mode_handle_command(Input_Command input)
{
    auto& mode = syntax_editor.mode;
    auto& cursor = syntax_editor.cursor;
    auto line = index_value(cursor.line);
    auto& text = line->text;
    auto& pos = cursor.pos;

    history_start_complex_command(&syntax_editor.history);
    SCOPE_EXIT(history_stop_complex_command(&syntax_editor.history));

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
        editor_leave_input_mode();
        syntax_editor_sanitize_cursor();
        return;
    }
    if (input.type == Input_Command_Type::ENTER)
    {
        split_line_at_cursor(0);
        return;
    }
    if (input.type == Input_Command_Type::ENTER_REMOVE_ONE_INDENT)
    {
        split_line_at_cursor(-1);
        return;
    }
    if (input.type == Input_Command_Type::ADD_INDENTATION)
    {
        if (pos == 0) {
            cursor.line = history_add_line_indent(&syntax_editor.history, cursor.line);
            return;
        }
        split_line_at_cursor(1);
        return;
    }
    if (input.type == Input_Command_Type::REMOVE_INDENTATION)
    {
        cursor.line = history_remove_line_indent(&syntax_editor.history, cursor.line);
        return;
    }
    if (input.type == Input_Command_Type::MOVE_LEFT) {
        pos = math_maximum(0, pos - 1);
        return;
    }
    if (input.type == Input_Command_Type::MOVE_RIGHT) {
        pos = math_minimum(line->text.size, pos + 1);
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

        if (skip_auto_input) {
            pos += 1;
            break;
        }
        if (insert_double_after) {
            history_insert_char(&syntax_editor.history, cursor, double_char);
        }
        history_insert_char(&syntax_editor.history, cursor, input.letter);
        pos += 1;
        // Inserting delimiters between space critical tokens can lead to spaces beeing removed
        syntax_editor_sanitize_line(cursor.line);
        break;
    }
    case Input_Command_Type::SPACE:
    {
        if (pos == 0) break;
        syntax_editor_synchronize_tokens();
        auto token = get_cursor_token();
        if (token.type == Token_Type::COMMENT) {
            if (pos > token.start_index + 1) {
                history_insert_char(&syntax_editor.history, cursor, ' ');
                pos += 1;
            }
            break;
        }
        if (token.type == Token_Type::LITERAL && token.options.literal_value.type == Literal_Type::STRING) {
            if (pos > token.start_index && pos < token.end_index) {
                history_insert_char(&syntax_editor.history, cursor, ' ');
                pos += 1;
            }
            break;
        }

        char c = text[pos - 1];
        if (pos < text.size) {
            if (char_is_space_critical(c) && char_is_space_critical(text[pos])) {
                history_insert_char(&syntax_editor.history, cursor, ' ');
                pos += 1;
                break;
            }
        }

        if (char_is_space_critical(c) && !syntax_editor.space_before_cursor) {
            syntax_editor.space_before_cursor = true;
        }
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

        if (pos == 0)
        {
            auto prev_line = line_index_prev(cursor.line);
            if (index_equal(prev_line, cursor.line)) {
                // We are at the first line in the code
                break;
            }
            // Merge this line with previous one
            Text_Index insert_index = text_index_make(prev_line, index_value(prev_line)->text.size);
            history_insert_text(&syntax_editor.history, insert_index, text);
            history_remove_line(&syntax_editor.history, cursor.line);
            cursor = insert_index;
            syntax_editor_sanitize_line(cursor.line);
            break;
        }

        syntax_editor.space_after_cursor = string_test_char(text, pos, ' ') || syntax_editor.space_after_cursor;
        history_delete_char(&syntax_editor.history, text_index_make(cursor.line, pos - 1));
        pos -= 1;
        syntax_editor.space_before_cursor = string_test_char(text, pos - 1, ' ');
        syntax_editor_sanitize_line(cursor.line);
        break;
    }
    case Input_Command_Type::NUMBER_LETTER:
    case Input_Command_Type::IDENTIFIER_LETTER:
    {
        int insert_pos = pos;
        if (syntax_editor.space_before_cursor) {
            syntax_editor.space_before_cursor = false;
            history_insert_char(&syntax_editor.history, cursor, ' ');
            pos += 1;
        }
        history_insert_char(&syntax_editor.history, cursor, input.letter);
        pos += 1;
        if (syntax_editor.space_after_cursor) {
            syntax_editor.space_after_cursor = false;
            history_insert_char(&syntax_editor.history, cursor, ' ');
        }
        break;
    }
    default: break;
    }
    syntax_editor_sanitize_cursor();
}



// Syntax Editor
void syntax_editor_update()
{
    auto& editor = syntax_editor;
    auto& input = syntax_editor.input;
    auto& mode = syntax_editor.mode;

    if (syntax_editor.input->key_pressed[(int)Key_Code::O] && syntax_editor.input->key_down[(int)Key_Code::CTRL]) {
        auto open_file = file_io_open_file_selection_dialog();
        if (open_file.available) {
            syntax_editor_load_text_file(open_file.value.characters);
            //compiler_compile(syntax_editor.root_block, false, string_create(syntax_editor.file_path));
        }
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
                if (msg.ctrl_down) {
                    command = Normal_Command::REDO;
                }
                else {
                    command = Normal_Command::CHANGE_TOKEN;
                }
            }
            else if (msg.key_code == Key_Code::X && msg.key_down) {
                command = Normal_Command::DELETE_TOKEN;
            }
            else if (msg.key_code == Key_Code::U && msg.key_down) {
                command = Normal_Command::UNDO;
            }
            else {
                continue;
            }
            normal_mode_handle_command(command);
        }
    }

    syntax_editor_synchronize_tokens();
    bool build_and_run = syntax_editor.input->key_pressed[(int)Key_Code::F5];
    if (syntax_editor.code_changed_since_last_compile || build_and_run) 
    {
        compiler_compile(syntax_editor.code, build_and_run, string_create(syntax_editor.file_path));
        syntax_editor.code_changed_since_last_compile = false;

        // Collect errors from all compiler stages
        {
            for (int i = 0; i < editor.errors.size; i++) {
                string_destroy(&editor.errors[i].message);
            }
            dynamic_array_reset(&editor.errors);

            // Parse Errors
            auto parse_errors = Parser::get_error_messages();
            for (int i = 0; i < parse_errors.size; i++) {
                auto& error = parse_errors[i];
                dynamic_array_push_back(&editor.errors, error_display_make(string_create_static(error.msg), error.range));
            }

            auto error_ranges = dynamic_array_create_empty<Token_Range>(1);
            SCOPE_EXIT(dynamic_array_destroy(&error_ranges));

            // Dependency Errors
            for (int i = 0; i < compiler.dependency_analyser->errors.size; i++)
            {
                auto& error = compiler.dependency_analyser->errors[i];
                auto& node = error.error_node;
                if (node == 0) continue;
                if (code_source_from_ast(node) != compiler.main_source) continue;
                dynamic_array_reset(&error_ranges);
                Parser::ast_base_get_section_token_range(node, Parser::Section::IDENTIFIER, &error_ranges);
                for (int j = 0; j < error_ranges.size; j++) {
                    auto& range = error_ranges[j];
                    dynamic_array_push_back(&editor.errors, error_display_make(string_create_static("Symbol already exists"), range));
                }
            }

            // Semantic Analysis Errors
            for (int i = 0; i < compiler.semantic_analyser->errors.size; i++)
            {
                auto& error = compiler.semantic_analyser->errors[i];
                auto& node = error.error_node;
                dynamic_array_reset(&error_ranges);
                if (node == 0) continue;
                if (code_source_from_ast(node) != compiler.main_source) continue;
                Parser::ast_base_get_section_token_range(node, semantic_error_get_section(error), &error_ranges);
                for (int j = 0; j < error_ranges.size; j++) {
                    auto& range = error_ranges[j];
                    String string = string_create_empty(4);
                    semantic_error_append_to_string(error, &string);
                    dynamic_array_push_back(&editor.errors, error_display_make(string, range));
                }
            }
        }
    }
    if (build_and_run)
    {
        // Display error messages or run the program
        if (!compiler_errors_occured()) {
            auto exit_code = compiler_execute();
            String output = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&output));
            exit_code_append_to_string(&output, exit_code);
            logg("\nProgram Exit with Code: %s\n", output.characters);
        }
        else {
            // Print errors
            logg("Could not run program, there were errors:\n");
            for (int i = 0; i < editor.errors.size; i++) {
                auto error = editor.errors[i];
                logg("\t%s\n", error.message.characters);
            }
        }
    }
}

void syntax_editor_initialize(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Input* input, Timer* timer)
{
    memory_zero(&syntax_editor);
    syntax_editor.context_text = string_create_empty(256);
    syntax_editor.errors = dynamic_array_create_empty<Error_Display>(1);

    syntax_editor.code = source_code_create();
    syntax_editor.history = code_history_create(syntax_editor.code);
    syntax_editor.last_token_synchronized = history_get_timestamp(&syntax_editor.history);
    syntax_editor.last_hierarchy_synchronized = history_get_timestamp(&syntax_editor.history);
    syntax_editor.code_changed_since_last_compile = true;

    syntax_editor.text_renderer = text_renderer;
    syntax_editor.rendering_core = rendering_core;
    syntax_editor.renderer_2D = renderer_2D;
    syntax_editor.input = input;
    syntax_editor.mode = Editor_Mode::NORMAL;
    syntax_editor.cursor = text_index_make(line_index_make(block_index_make(syntax_editor.code, 0), 0), 0);

    compiler_initialize(timer);
    compiler_run_testcases(timer);
    syntax_editor_load_text_file("upp_code/editor_text.upp");
}

void syntax_editor_destroy()
{
    auto& editor = syntax_editor;
    source_code_destroy(editor.code);
    code_history_destroy(&editor.history);
    string_destroy(&syntax_editor.context_text);
    compiler_destroy();
    for (int i = 0; i < editor.errors.size; i++) {
        string_destroy(&editor.errors[i].message);
    }
    dynamic_array_destroy(&editor.errors);
}



// RENDERING
// Draw Commands
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

void syntax_editor_draw_cursor_line(int line, int character, vec3 color)
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

void syntax_editor_draw_text_background(int line, int character, int length, vec3 color)
{
    auto& editor = syntax_editor;
    auto offset = editor.character_size * vec2(0.0f, 0.5f);

    float w = editor.rendering_core->render_information.viewport_width;
    float h = editor.rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }

    vec2 char_size = editor.character_size * scaling_factor;
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * char_size.x, -line * char_size.y);

    vec2 size = char_size * vec2((float)length, 1.0f);
    cursor = cursor + size * vec2(0.5f, -0.5f);
    renderer_2D_add_rectangle(editor.renderer_2D, cursor, size, color, 0.0f);
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

void syntax_editor_draw_block_outline(int line_start, int line_end, int indentation)
{
    if (indentation == 0) return;
    auto offset = syntax_editor.character_size * vec2(0.5f, 1.0f);
    vec2 start = text_to_screen_coord(line_start, (indentation - 1) * 4) + offset;
    vec2 end = text_to_screen_coord(line_end, (indentation - 1) * 4) + offset;
    start.y -= syntax_editor.character_size.y * 0.1;
    end.y += syntax_editor.character_size.y * 0.1;
    renderer_2D_add_line(syntax_editor.renderer_2D, start, end, vec3(0.4f), 3, 0.0f);
    vec2 l_end = end + vec2(syntax_editor.character_size.x * 0.5f, 0.0f);
    renderer_2D_add_line(syntax_editor.renderer_2D, end, l_end, vec3(0.4f), 3, 0.0f);
}



// Syntax Highlighting
void syntax_highlighting_set_section_text_color(AST::Base* base, Parser::Section section, vec3 color)
{
    assert(base != 0, "");
    auto ranges = dynamic_array_create_empty<Token_Range>(1);
    SCOPE_EXIT(dynamic_array_destroy(&ranges));

    auto& node = base;
    Parser::ast_base_get_section_token_range(node, section, &ranges);
    for (int i = 0; i < ranges.size; i++)
    {
        const auto& range = ranges[i];
        auto iter = range.start;
        while (!index_equal(iter, range.end)) {
            if (token_index_is_last_in_line(iter)) {
                iter = token_index_make(line_index_next(iter.line), 0);
                continue;
            }
            auto line = index_value(iter.line);
            line->infos[iter.token].color = color;
            iter = token_index_next(iter);
        }
    }
}

void syntax_highlighting_mark_range(Token_Range range, vec3 normal_color, vec3 empty_range_color, bool underline)
{
    auto index = range.start;
    auto end = range.end;

    typedef void (*draw_fn)(int line, int character, int length, vec3 color);
    draw_fn draw_mark = underline ? syntax_editor_draw_underline : syntax_editor_draw_text_background;

    assert(index_compare(index, end) >= 0, "hey");
    if (index_equal(index, end))
    {
        auto line = index_value(index.line);
        if (token_index_is_last_in_line(index)) {
            draw_mark(line->render_index, line->render_end_pos, 1, empty_range_color);
        }
        else {
            auto& info = line->infos[index.token];
            draw_mark(line->render_index, info.pos, info.size, normal_color);
        }
        return;
    }

    bool quit_loop = false;
    while (true)
    {
        auto line = index_value(index.line);

        int draw_start = line->render_start_pos;
        int draw_end = line->render_end_pos;
        if (!token_index_is_last_in_line(index)) {
            draw_start = line->infos[index.token].pos;
        }
        if (index_equal(index.line, end.line)) {
            if (end.token - 1 >= 0) {
                const auto& info = line->infos[end.token - 1];
                draw_end = info.pos + info.size;
            }
        }

        if (draw_start != draw_end) {
            draw_mark(line->render_index, draw_start, draw_end - draw_start, normal_color);
        }
        if (index_equal(index.line, end.line)) {
            break;
        }
        index = token_index_make(line_index_next(index.line), 0);
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
            syntax_highlighting_set_section_text_color(base, Parser::Section::IDENTIFIER, color);
        }
        break;
    }
    case AST::Base_Type::SYMBOL_READ: {
        auto read = (AST::Symbol_Read*) base;
        if (read->resolved_symbol != 0) {
            auto color = symbol_type_to_color(read->resolved_symbol->type);
            syntax_highlighting_set_section_text_color(base, Parser::Section::IDENTIFIER, color);
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



// Code Layout 
void operator_space_before_after(Token_Index index, bool& space_before, bool& space_after)
{
    auto& tokens = index_value(index.line)->tokens;
    assert(tokens[index.token].type == Token_Type::OPERATOR, "");
    auto op_info = syntax_operator_info(tokens[index.token].options.op);
    space_before = false;
    space_after = false;

    bool use_info = true;
    // Approximate if Operator is binop or not (Sometimes this cannot be detected with parsing alone)
    if (op_info.type == Operator_Type::BOTH)
    {
        // Current approximation for is_binop: The current and previous type has to be a value
        use_info = false;
        if (index.token > 0 && index.token + 1 < tokens.size)
        {
            bool prev_is_value = false;
            {
                const auto& t = tokens[index.token - 1];
                if (t.type == Token_Type::IDENTIFIER || t.type == Token_Type::LITERAL) {
                    prev_is_value = true;
                }
                if (t.type == Token_Type::PARENTHESIS && !t.options.parenthesis.is_open && t.options.parenthesis.type != Parenthesis_Type::BRACKETS) {
                    prev_is_value = true;
                }
            }
            bool next_is_value = false;
            {
                const auto& t = tokens[index.token + 1];
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

bool display_space_after_token(Token_Index index)
{
    auto line = index_value(index.line);
    auto& text = line->text;
    auto& tokens = line->tokens;

    // No space after final line token
    if (index.token + 1 >= tokens.size) return false;

    auto& a = tokens[index.token];
    auto& b = tokens[index.token + 1];

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
    if (a.type == Token_Type::KEYWORD) {
        return true;
    }

    // Ops that have spaces after
    if (a.type == Token_Type::OPERATOR)
    {
        bool unused, space_after;
        operator_space_before_after(index, unused, space_after);
        if (space_after) {
            return true;
        }
    }

    // Ops that have spaces before
    if (b.type == Token_Type::OPERATOR)
    {
        bool unused, space_before;
        operator_space_before_after(token_index_make(index.line, index.token + 1), space_before, unused);
        if (space_before) {
            return true;
        }
    }

    if (a.type == Token_Type::PARENTHESIS) {
        // Closed paranethesis should have a space after if it isn't used as an array type
        if (!a.options.parenthesis.is_open && char_is_space_critical(text[b.start_index]) && a.options.parenthesis.type != Parenthesis_Type::BRACKETS) {
            return true;
        }
    }

    return false;
}

void syntax_editor_layout_line(Line_Index line_index, int screen_index, int indentation)
{
    auto& editor = syntax_editor;
    auto& cursor = editor.cursor;
    auto line = index_value(line_index);
    line->render_index = screen_index;
    line->render_indent = indentation;
    line->render_start_pos = indentation * 4;

    dynamic_array_reset(&line->infos);
    int pos = indentation * 4;
    for (int i = 0; i < line->tokens.size; i++)
    {
        auto& token = line->tokens[i];
        bool on_token = index_equal(cursor.line, line_index) && get_cursor_token_index() == i;
        if (on_token && token.start_index == cursor.pos) {
            pos += editor.space_after_cursor ? 1 : 0;
            pos += editor.space_before_cursor ? 1 : 0;
        }

        Render_Info info;
        info.line = line->render_index;
        info.pos = pos;
        info.size = token.end_index - token.start_index;
        info.color = Syntax_Color::TEXT;
        switch (token.type)
        {
        case Token_Type::COMMENT: info.color = Syntax_Color::COMMENT; break;
        case Token_Type::INVALID: info.color = vec3(1.0f, 0.8f, 0.8f); break;
        case Token_Type::KEYWORD: info.color = Syntax_Color::KEYWORD; break;
        case Token_Type::IDENTIFIER: info.color = Syntax_Color::IDENTIFIER_FALLBACK; break;
        case Token_Type::LITERAL: {
            switch (token.options.literal_value.type)
            {
            case Literal_Type::BOOLEAN: info.color = vec3(0.5f, 0.5f, 1.0f); break;
            case Literal_Type::STRING: info.color = Syntax_Color::STRING; break;
            case Literal_Type::INTEGER:
            case Literal_Type::FLOAT_VAL:
            case Literal_Type::NULL_VAL:
                info.color = Syntax_Color::LITERAL_NUMBER; break;
            default: panic("");
            }
            break;
        }
        }
        pos += info.size;

        if (on_token && syntax_editor.space_before_cursor) {
            pos += 1;
        }

        if (display_space_after_token(token_index_make(line_index, i)) && i != line->tokens.size - 1) {
            pos += 1;
        }
        dynamic_array_push_back(&line->infos, info);
    }
    line->render_end_pos = pos;
}

void syntax_editor_layout_block(Block_Index block_index, int* screen_index, int indentation)
{
    auto block = index_value(block_index);
    block->render_start = *screen_index;
    block->render_indent = indentation;

    int child_index = 0;
    for (int i = 0; i < block->lines.size; i++)
    {
        while (child_index < block->children.size)
        {
            auto next_child = index_value(block->children[child_index]);
            if (next_child->line_index == i) {
                syntax_editor_layout_block(block->children[child_index], screen_index, indentation + 1);
                child_index++;
            }
            else {
                break;
            }
        }
        syntax_editor_layout_line(line_index_make(block_index, i), *screen_index, indentation);
        *screen_index += 1;
    }
    if (child_index < block->children.size) {
        syntax_editor_layout_block(block->children[child_index], screen_index, indentation + 1);
        assert(child_index + 1 >= block->children.size, "All must be iterated by now");
    }
    block->render_end = *screen_index;
}


// Rendering
void syntax_editor_render_block(Block_Index block_index)
{
    auto block = index_value(block_index);
    // Render lines
    for (int i = 0; i < block->lines.size; i++)
    {
        auto line = block->lines[i];
        for (int j = 0; j < line.tokens.size; j++)
        {
            auto& token = line.tokens[j];
            auto& info = line.infos[j];
            auto str = token_get_string(token, line.text);
            syntax_editor_draw_string(str, info.color, info.line, info.pos);
        }
    }

    // Render child-blocks
    for (int i = 0; i < block->children.size; i++) {
        syntax_editor_render_block(block->children[i]);
    }

    // Render block outline
    if (block_index.block != 0) {
        syntax_editor_draw_block_outline(block->render_start, block->render_end, block->render_indent);
    }
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
    int screen_index = 0;
    syntax_editor_layout_block(block_index_make_root(editor.code), &screen_index, 0);

    // Draw Text-Representation @ the bottom of the screen 
    if (true)
    {
        int line_index = 2.0f / editor.character_size.y - 1;
        syntax_editor_draw_string(index_value(cursor.line)->text, Syntax_Color::TEXT, line_index, 0);
        if (editor.mode == Editor_Mode::NORMAL) {
            syntax_editor_draw_text_background(line_index, cursor.pos, 1, Syntax_Color::COMMENT);
        }
        else {
            syntax_editor_draw_cursor_line(line_index, cursor.pos, Syntax_Color::COMMENT);
        }
    }

    bool show_context_info = false;
    auto& context = syntax_editor.context_text;
    string_reset(&context);

    // Draw error messages
    Token_Index cursor_token_index = token_index_make(syntax_editor.cursor.line, get_cursor_token_index());
    for (int i = 0; i < editor.errors.size; i++)
    {
        auto& error = editor.errors[i];
        syntax_highlighting_mark_range(error.range, vec3(1.0f, 0.0f, 0.0f), vec3(1.0f, 1.0f, 0.0f), true);
        if (token_range_contains(error.range, cursor_token_index)) {
            show_context_info = true;
            if (context.size != 0) {
                string_append(&context, "\n\n");
            }
            string_append_string(&context, &error.message);
        }
    }

    // Highlight smallest enclosing node
    {
        auto node = Parser::find_smallest_enclosing_node(&compiler.main_source->ast->base, cursor_token_index);
        auto info = Parser::get_parse_info(node);
        syntax_highlighting_mark_range(info->range, vec3(0.3f), vec3(0.3f), false);
        if (!show_context_info)
        {
            show_context_info = true;
            AST::base_append_to_string(node, &context);
        }
    }

    // Draw context
    if (show_context_info)
    {
        auto line = index_value(cursor_token_index.line);
        int char_index = 0;
        if (!token_index_is_last_in_line(cursor_token_index)) {
            auto& info = line->infos[cursor_token_index.token];
            char_index = info.pos + (cursor.pos - index_value(cursor_token_index)->start_index);
        }
        syntax_editor_draw_string_in_box(context, vec3(1.0f), vec3(0.2f), line->render_index + 1, char_index, 0.8f);
    }

    // Syntax Highlighting
    syntax_editor_find_syntax_highlights(&compiler.main_source->ast->base);

    // Draw Cursor
    {
        auto line = index_value(cursor.line);
        auto& text = line->text;
        auto& tokens = line->tokens;
        auto& infos = line->infos;
        auto& pos = cursor.pos;
        auto token_index = get_cursor_token_index();

        Render_Info info;
        if (token_index < infos.size) {
            info = infos[token_index];
        }
        else {
            info.pos = line->render_indent * 4;
            info.size = 1;
            info.line = line->render_index;
        }

        if (editor.mode == Editor_Mode::NORMAL)
        {
            int box_start = info.pos;
            int box_end = math_maximum(info.pos + info.size, box_start + 1);
            syntax_editor_draw_text_background(info.line, box_start, box_end - box_start, vec3(0.2f));
            syntax_editor_draw_cursor_line(info.line, box_start, Syntax_Color::COMMENT);
            syntax_editor_draw_cursor_line(info.line, box_end, Syntax_Color::COMMENT);
        }
        else
        {
            // Adjust token index if we are inbetween tokens
            if (pos > 0 && pos < text.size)
            {
                Token* token = &tokens[token_index];
                if (pos == token->start_index && token_index > 0) {
                    if (char_is_space_critical(text[pos - 1]) && !char_is_space_critical(text[pos])) {
                        token_index -= 1;
                        info = infos[token_index];
                    }
                }
            }

            int cursor_pos = line->render_indent * 4;
            if (tokens.size != 0)
            {
                auto tok_start = tokens[token_index].start_index;
                int cursor_offset = cursor.pos - tok_start;
                cursor_pos = info.pos + cursor_offset;

                if (editor.space_after_cursor && cursor_offset == 0) {
                    cursor_pos = info.pos - 1;
                }
            }
            if (editor.space_before_cursor && !editor.space_after_cursor) {
                cursor_pos += 1;
            }
            syntax_editor_draw_cursor_line(line->render_index, cursor_pos, Syntax_Color::COMMENT);
        }
    }

    // Render Source Code
    syntax_editor_render_block(block_index_make_root(editor.code));

    // Render Primitives
    renderer_2D_render(editor.renderer_2D, editor.rendering_core);
    text_renderer_render(editor.text_renderer, editor.rendering_core);
}



