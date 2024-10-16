#include "syntax_editor.hpp"

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/renderer_2d.hpp"
#include "../../utility/file_io.hpp"
#include "../../utility/character_info.hpp"
#include "../../utility/fuzzy_search.hpp"

#include "../../win32/input.hpp"
#include "syntax_colors.hpp"
#include "compiler.hpp"
#include "ast.hpp"
#include "symbol_table.hpp"
#include "semantic_analyser.hpp"
#include "parser.hpp"
#include "source_code.hpp"
#include "code_history.hpp"

const int MIN_CURSOR_DISTANCE = 3;

// Prototypes
bool auto_format_line(int line_index);
void syntax_editor_load_text_file(const char* filename);



// Structures/Enums

// Motions/Movements and Commands
enum class Movement_Type
{
    SEARCH_FORWARDS_TO, // t
    SEARCH_FORWARDS_FOR, // f
    SEARCH_BACKWARDS_TO, // T
    SEARCH_BACKWARDS_FOR, // F
    REPEAT_LAST_SEARCH, // ;
    REPEAT_LAST_SEARCH_REVERSE_DIRECTION, // ,
    MOVE_LEFT, // h
    MOVE_RIGHT, // l
    MOVE_UP, // k
    MOVE_DOWN, //j
    TO_END_OF_LINE, // $
    TO_START_OF_LINE, // 0
    NEXT_WORD, // w
    NEXT_SPACE, // W
    PREVIOUS_WORD, // b
    PREVIOUS_SPACE, // B
    END_OF_WORD, // e
    END_OF_WORD_AFTER_SPACE, // E
    JUMP_ENCLOSURE, // %
    BLOCK_END, // }
    BLOCK_START, // {
    GOTO_END_OF_TEXT, // G
    GOTO_START_OF_TEXT, // gg
    GOTO_LINE_NUMBER, // g43
};

struct Movement
{
    Movement_Type type;
    int repeat_count;
    char search_char;
};

// Missing: <> ''
enum class Motion_Type
{
    MOVEMENT, // motion is a movement 
    WORD, // w
    SPACES, // W
    PARENTHESES, // ()
    BRACES, // {}
    BRACKETS, // []
    QUOTATION_MARKS, // ""
    // PARAGRAPH_WITH_INDENT, // p
    // PARAGRAPH_WITHOUT_INDENT, // P
    BLOCK, // b or B
    // 'Custom' motion used for dd, yy, Y, cc and also line-up/down movement motions
    // This is because yank/put handles lines different than other motions
    // Also deletes with up-down movement turn into line delets in Vim
    // The repeat count for this motion gives the line offset to the next line
    // E.g. repeat_count 0 means just this line, repeat count -1 this and previous line...
    LINE,
};

struct Motion
{
    Motion_Type motion_type;
    int repeat_count;
    Movement movement; // If type == MOVEMENT
    bool contains_edges; // In vim, this would be inner or around motions (iw or aw)
};

enum class Insert_Command_Type
{
    IDENTIFIER_LETTER,
    NUMBER_LETTER,
    DELIMITER_LETTER,
    SPACE,
    BACKSPACE,
    ENTER,
    ENTER_REMOVE_ONE_INDENT,

    EXIT_INSERT_MODE,
    ADD_INDENTATION,
    REMOVE_INDENTATION,
    MOVE_LEFT,
    MOVE_RIGHT,
    INSERT_CODE_COMPLETION,

    DELETE_LAST_WORD, // Ctrl-W or Ctrl-Backspace
    DELETE_TO_LINE_START, // Ctrl-U
};

struct Insert_Command
{
    Insert_Command_Type type;
    char letter;
};

enum class Normal_Mode_Command_Type
{
    MOVEMENT,
    ENTER_INSERT_MODE_AFTER_MOVEMENT, // i, a, I, A
    ENTER_INSERT_MODE_NEW_LINE_BELOW, // o
    ENTER_INSERT_MODE_NEW_LINE_ABOVE, // O
    DELETE_MOTION, // d [m] or D/dd
    CHANGE_MOTION, // c [m] or C/cc
    REPLACE_CHAR, // r
    REPLACE_MOTION_WITH_YANK, // R

    // Yank/Put
    YANK_MOTION,   // y [m] or Y
    PUT_AFTER_CURSOR,
    PUT_BEFORE_CURSOR,

    // History
    UNDO, // u
    REDO, // Ctrl-R
    REPEAT_LAST_COMMAND, // .

    // Camera
    SCROLL_DOWNWARDS_HALF_PAGE, // Ctrl-D
    SCROLL_UPWARDS_HALF_PAGE, // Ctrl-U
    MOVE_VIEWPORT_CURSOR_TOP, // zt
    MOVE_VIEWPORT_CURSOR_CENTER, // zz
    MOVE_VIEWPORT_CURSOR_BOTTOM, // zb
    MOVE_CURSOR_VIEWPORT_TOP, // 'Shift-H'
    MOVE_CURSOR_VIEWPORT_CENTER, // 'Shift-M'
    MOVE_CURSOR_VIEWPORT_BOTTOM, // 'Shift-L'

    // Others
    VISUALIZE_MOTION, // not sure
    GOTO_LAST_JUMP, // Ctrl-O
    GOTO_NEXT_JUMP, // Ctrl-I

    MAX_ENUM_VALUE
};

struct Normal_Mode_Command
{
    Normal_Mode_Command_Type type;
    int repeat_count;
    union {
        Motion motion;
        Movement movement;
        char character;
    } options;
};

struct Error_Display
{
    String message;
    Token_Range range;
    bool from_main_source;
    int semantic_error_index; // -1 if parsing error
};
Error_Display error_display_make(String msg, Token_Range range, bool from_main_source, int semantic_error_index);

enum class Editor_Mode
{
    NORMAL,
    INSERT,
};

struct Input_Replay
{
    String code_state_initial;
    String code_state_afterwards;
    Dynamic_Array<Key_Message> recorded_inputs;
    bool currently_recording;
    bool currently_replaying;
    Editor_Mode start_mode;
    Text_Index cursor_start;
};

struct Text_Style
{
    int char_start;
    vec3 text_color;
    bool has_bg;
    vec3 bg_color;
};

struct Context_Line
{
    String text;
    int indentation;
    Dynamic_Array<Text_Style> styles;
    bool is_seperator; // E.g. just a blank ---- line
};

struct Syntax_Editor
{
    // Editing
    Editor_Mode mode;
    Source_Code* code;
    Text_Index cursor;
    const char* file_path;

    Code_History history;
    History_Timestamp last_token_synchronized;
    bool code_changed_since_last_compile;
    bool last_compile_was_with_code_gen;

    Dynamic_Array<String> code_completion_suggestions;
    Input_Replay input_replay;
    String yank_string;
    bool yank_was_line;
    
    // Command repeating
    Normal_Mode_Command last_normal_command;
    Dynamic_Array<Insert_Command> last_insert_commands;
    bool record_insert_commands;
    String last_recorded_code_completion;

    // Movement
    String command_buffer;
    int last_line_x_pos; // Position with indentation * 4 for up/down movements
    char last_search_char;
    bool last_search_was_forward;
    bool last_search_was_to;

    // Rendering
    String context_text;
    Dynamic_Array<Context_Line> context_lines;

    Dynamic_Array<Error_Display> errors;
    Dynamic_Array<Token_Range> token_range_buffer;
    int camera_start_line;
    int last_visible_line;

    Input* input;
    Rendering_Core* rendering_core;
    Renderer_2D* renderer_2D;
    Text_Renderer* text_renderer;
    vec2 character_size;
};



// Editor
static Syntax_Editor syntax_editor;

void syntax_editor_initialize(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Input* input, Timer* timer)
{
    memory_zero(&syntax_editor);
    syntax_editor.context_text = string_create_empty(256);
    syntax_editor.context_lines = dynamic_array_create<Context_Line>();
    syntax_editor.errors = dynamic_array_create<Error_Display>(1);
    syntax_editor.token_range_buffer = dynamic_array_create<Token_Range>(1);
    syntax_editor.code_completion_suggestions = dynamic_array_create<String>();
    syntax_editor.command_buffer = string_create();

    syntax_editor.yank_string = string_create();
    syntax_editor.yank_was_line = false;

    syntax_editor.last_normal_command.type = Normal_Mode_Command_Type::MOVEMENT;
    syntax_editor.last_normal_command.options.movement.type = Movement_Type::MOVE_LEFT;
    syntax_editor.last_normal_command.options.movement.repeat_count = 0;
    syntax_editor.last_insert_commands = dynamic_array_create<Insert_Command>();
    syntax_editor.record_insert_commands = true;
    syntax_editor.last_recorded_code_completion = string_create();

    syntax_editor.code = source_code_create();
    syntax_editor.history = code_history_create(syntax_editor.code);
    syntax_editor.last_token_synchronized = history_get_timestamp(&syntax_editor.history);
    syntax_editor.code_changed_since_last_compile = true;

    syntax_editor.text_renderer = text_renderer;
    syntax_editor.rendering_core = rendering_core;
    syntax_editor.renderer_2D = renderer_2D;
    syntax_editor.input = input;
    syntax_editor.mode = Editor_Mode::NORMAL;
    syntax_editor.cursor = text_index_make(0, 0);
    syntax_editor.last_line_x_pos = 0;
    syntax_editor.camera_start_line = 0;
    syntax_editor.last_visible_line = 0;

    syntax_editor.last_search_char = '\0';
    syntax_editor.last_search_was_forward = false;
    syntax_editor.last_search_was_to = false;

    // Init input replay
    {
        auto replay = syntax_editor.input_replay;
        replay.code_state_initial = string_create_empty(1);
        replay.code_state_afterwards = string_create_empty(1);
        replay.recorded_inputs = dynamic_array_create<Key_Message>(1);
        replay.currently_recording = false;
        replay.currently_replaying = false;
    }

    compiler_initialize(timer);
    compiler_run_testcases(timer, false);
    syntax_editor_load_text_file("upp_code/editor_text.upp");
}

void syntax_editor_destroy()
{
    auto& editor = syntax_editor;
    source_code_destroy(editor.code);
    code_history_destroy(&editor.history);
    dynamic_array_destroy(&editor.code_completion_suggestions);
    dynamic_array_destroy(&editor.context_lines);
    string_destroy(&syntax_editor.context_text);
    string_destroy(&syntax_editor.command_buffer);
    string_destroy(&syntax_editor.yank_string);
    compiler_destroy();
    for (int i = 0; i < editor.errors.size; i++) {
        string_destroy(&editor.errors[i].message);
    }
    dynamic_array_destroy(&editor.errors);
    dynamic_array_destroy(&editor.token_range_buffer);

    dynamic_array_destroy(&editor.last_insert_commands);
    string_destroy(&editor.last_recorded_code_completion);

    {
        auto& replay = editor.input_replay;
        string_destroy(&replay.code_state_afterwards);
        string_destroy(&replay.code_state_initial);
        dynamic_array_destroy(&replay.recorded_inputs);
    }
}

void syntax_editor_set_text(String string)
{
    auto& editor = syntax_editor;
    editor.cursor = text_index_make(0, 0);
    source_code_fill_from_string(editor.code, string);
    source_code_tokenize(editor.code);
    for (int i = 0; i < editor.code->line_count; i++) {
        auto_format_line(i);
    }
    code_history_reset(&editor.history);
    editor.last_token_synchronized = history_get_timestamp(&editor.history);
    editor.code_changed_since_last_compile = true;
    // compiler_compile_clean(editor.code, Compile_Type::ANALYSIS_ONLY, string_create(syntax_editor.file_path));
}

void syntax_editor_load_text_file(const char* filename)
{
    auto& editor = syntax_editor;
    Optional<String> content = file_io_load_text_file(filename);
    SCOPE_EXIT(file_io_unload_text_file(&content););

    String result;
    if (content.available) {
        result = content.value;
        editor.file_path = filename;
        syntax_editor_set_text(result);
    }
    else {
        result = string_create("main :: (x : int) -> void \n{\n\n}");
        SCOPE_EXIT(string_destroy(&result));
        syntax_editor_set_text(result);
    }
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

void syntax_editor_synchronize_tokens()
{
    auto& editor = syntax_editor;
    // Get changes since last sync
    Dynamic_Array<Code_Change> changes = dynamic_array_create<Code_Change>(1);
    SCOPE_EXIT(dynamic_array_destroy(&changes));
    auto now = history_get_timestamp(&editor.history);
    history_get_changes_between(&editor.history, editor.last_token_synchronized, now, &changes);
    editor.last_token_synchronized = now;
    if (changes.size != 0) {
        editor.code_changed_since_last_compile = true;
    }

    // Find out which lines were line_changed
    auto line_changes = dynamic_array_create<int>();
    SCOPE_EXIT(dynamic_array_destroy(&line_changes));
    auto helper_add_delete_line_item = [&line_changes](int new_line_index, bool is_insert) -> void 
    {
        for (int i = 0; i < line_changes.size; i++) {
            auto& line_index = line_changes[i];
            if (is_insert) {
                if (line_index >= new_line_index) {
                    line_index += 1;
                }
            }
            else {
                if (line_index == new_line_index) {
                    dynamic_array_swap_remove(&line_changes, i);
                    i = i - 1;
                    continue;
                }
                if (line_index > new_line_index) {
                    line_index -= 1;
                }
            }
        }
        // Note: Only line_changed lines need to be added to line_changes, so we don't add additions here
    };

    for (int i = 0; i < changes.size; i++)
    {
        auto& change = changes[i];
        switch (change.type)
        {
        case Code_Change_Type::LINE_INSERT: {
            helper_add_delete_line_item(change.options.line_insert.line_index, change.apply_forwards);
            break;
        }
        case Code_Change_Type::CHAR_INSERT:
        case Code_Change_Type::TEXT_INSERT:
        {
            int changed_line = change.type == Code_Change_Type::CHAR_INSERT ? change.options.char_insert.index.line : change.options.text_insert.index.line;
            bool found = false;
            for (int j = 0; j < line_changes.size; j++) {
                if (line_changes[j] ==  changed_line) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                dynamic_array_push_back(&line_changes, changed_line);
            }
            break;
        }
        case Code_Change_Type::LINE_INDENTATION_CHANGE: break;
        default: panic("");
        }
    }

    // Update line_changed lines
    for (int i = 0; i < line_changes.size; i++)
    {
        auto& index = line_changes[i];
        bool changed = auto_format_line(index);
        //assert(!changed, "Syntax editor has to make sure that lines are sanitized after edits!");

        // Tokenization is done by auto_format_line
        //source_code_tokenize_line(syntax_editor.code, line_changes[i]);
        //logg("Synchronized: %d/%d\n", index.block_index.block_index, index.line_index);
    }
}

void syntax_editor_synchronize_with_compiler(bool generate_code)
{
    syntax_editor_synchronize_tokens();
    auto& editor = syntax_editor;
    
    if (!editor.code_changed_since_last_compile)
    {
        if (!generate_code) {
            return;
        }
        else if (editor.last_compile_was_with_code_gen) {
            return;
        }
    }
    syntax_editor.code_changed_since_last_compile = false;
    syntax_editor.last_compile_was_with_code_gen = generate_code;
    //compiler_compile_incremental(&syntax_editor.history, (generate_code ? Compile_Type::BUILD_CODE : Compile_Type::ANALYSIS_ONLY));
    compiler_compile_clean(syntax_editor.code, (generate_code ? Compile_Type::BUILD_CODE : Compile_Type::ANALYSIS_ONLY), string_create(syntax_editor.file_path));

    // Collect errors from all compiler stages
    {
        for (int i = 0; i < editor.errors.size; i++) {
            string_destroy(&editor.errors[i].message);
        }
        dynamic_array_reset(&editor.errors);

        // Parse Errors
        for (int i = 0; i < compiler.program_sources.size; i++)
        {
            auto& parse_errors = compiler.program_sources[i]->parsed_code->error_messages;
            for (int j = 0; j < parse_errors.size; j++) {
                auto& error = parse_errors[j];
                dynamic_array_push_back(
                    &editor.errors, 
                    error_display_make(
                        string_create_static(error.msg), error.range, 
                        compiler.program_sources[i]->origin == Code_Origin::MAIN_PROJECT, -1
                    )
                );
            }
        }

        auto error_ranges = dynamic_array_create<Token_Range>(1);
        SCOPE_EXIT(dynamic_array_destroy(&error_ranges));

        // Semantic Analysis Errors
        for (int i = 0; i < compiler.semantic_analyser->errors.size; i++)
        {
            auto& error = compiler.semantic_analyser->errors[i];
            auto& node = error.error_node;
            dynamic_array_reset(&error_ranges);
            if (node == 0) continue;
            if (compiler_find_ast_program_source(node) != compiler.main_source) continue;
            Parser::ast_base_get_section_token_range(editor.code, node, error.section, &error_ranges);
            for (int j = 0; j < error_ranges.size; j++) {
                auto& range = error_ranges[j];
                assert(token_index_compare(range.start, range.end) >= 0, "hey");
                dynamic_array_push_back(&editor.errors, error_display_make(string_create_static(error.msg), range, true, i));
            }
        }
    }
}



// Helpers
Error_Display error_display_make(String msg, Token_Range range, bool from_main_source, int semantic_error_index)
{
    Error_Display result;
    result.message = msg;
    result.range = range;
    result.from_main_source = from_main_source;
    result.semantic_error_index = semantic_error_index;
    return result;
}

Token token_make_dummy() {
    Token t;
    t.type = Token_Type::INVALID;
    t.start_index = 0;
    t.end_index = 0;
    return t;
}

int get_cursor_token_index(bool after_cursor) {
    auto& c = syntax_editor.cursor;
    auto line = source_code_get_line(syntax_editor.code, c.line);
    return character_index_to_token(&line->tokens, c.character, after_cursor);
}

Token get_cursor_token(bool after_cursor)
{
    auto& c = syntax_editor.cursor;
    int tok_index = get_cursor_token_index(after_cursor);
    auto tokens = source_code_get_line(syntax_editor.code, c.line)->tokens;
    if (tok_index >= tokens.size) return token_make_dummy();
    return tokens[tok_index];
}

char get_cursor_char(char dummy_char)
{
    auto& c = syntax_editor.cursor;
    Source_Line* line = source_code_get_line(syntax_editor.code, c.line);
    if (c.character >= line->text.size) return dummy_char;
    return line->text.characters[c.character];
}

Text_Index sanitize_index(Text_Index index) {
    auto& editor = syntax_editor;
    auto& code = editor.code;
    if (index.line < 0) index.line = 0;
    if (index.line >= code->line_count) {
        index.line = code->line_count - 1;
    }
    auto& text = source_code_get_line(code, index.line)->text;
    index.character = math_clamp(index.character, 0, text.size);
    return index;
}

void syntax_editor_sanitize_cursor() {
    auto& editor = syntax_editor;
    auto& code = editor.code;
    auto& index = editor.cursor;

    if (index.line < 0) index.line = 0;
    if (index.line >= code->line_count) {
        index.line = code->line_count - 1;
    }
    auto& text = source_code_get_line(code, index.line)->text;
    index.character = math_clamp(index.character, 0, editor.mode == Editor_Mode::INSERT ? text.size : math_maximum(0, text.size - 1));
}



// Auto formating
void token_expects_space_before_or_after(Source_Line* line, int token_index, bool& out_space_before, bool& out_space_after)
{
    out_space_after = false;
    out_space_before = false;
    if (token_index >= line->tokens.size) return;

    // Handle different token types
    const auto& token = line->tokens[token_index];
    switch (token.type)
    {
    case Token_Type::COMMENT: {
        out_space_before = true;
        out_space_after = false;
        return;
    }
    case Token_Type::INVALID:
    case Token_Type::KEYWORD: {
        out_space_before = true;
        out_space_after = true;
        return;
    }
    case Token_Type::OPERATOR: {
        // Rest of this function
        break;
    }
    default: {
        out_space_before = false;
        out_space_after = false;
        return;
    }
    }

    // Handle different operator types
    switch (token.options.op)
    {
    // Operators that always have spaces before and after
    case Operator::ADDITION: 
    case Operator::DIVISON:
    case Operator::LESS_THAN: 
    case Operator::GREATER_THAN:
    case Operator::LESS_EQUAL: 
    case Operator::GREATER_EQUAL:
    case Operator::EQUALS:
    case Operator::NOT_EQUALS: 
    case Operator::POINTER_EQUALS:
    case Operator::POINTER_NOT_EQUALS:
    case Operator::DEFINE_COMPTIME:
    case Operator::DEFINE_INFER:
    case Operator::DEFINE_INFER_POINTER: 
    case Operator::DEFINE_INFER_RAW: 
    case Operator::AND: 
    case Operator::OR: 
    case Operator::ARROW: 
    case Operator::ASSIGN:
    case Operator::ASSIGN_RAW:
    case Operator::ASSIGN_POINTER:
    case Operator::ASSIGN_ADD:
    case Operator::ASSIGN_SUB:
    case Operator::ASSIGN_DIV:
    case Operator::ASSIGN_MULT:
    case Operator::ASSIGN_MODULO:
    case Operator::MODULO: {
        out_space_after = true;
        out_space_before = true;
        break;
    }

    // No spaces before or after
    case Operator::DOT: 
    case Operator::TILDE: 
    case Operator::NOT:
    case Operator::AMPERSAND:
    case Operator::UNINITIALIZED:
    case Operator::DOLLAR: {
        out_space_after = false;
        out_space_before = false;
        break;
    }

    // Only space after
    case Operator::COMMA:
    case Operator::TILDE_STAR:
    case Operator::TILDE_STAR_STAR:
    case Operator::COLON:
    case Operator::SEMI_COLON: {
        out_space_after = true;
        out_space_before = false;
        break;
    }

    // Complicated cases, where it could either be a Unop (expects no space after) or a binop (expects a space afterwards)
    case Operator::MULTIPLY: // Could also be pointer dereference...
    case Operator::SUBTRACTION: 
    {
        // If we don't have a token before or after, assume that it's a Unop
        if (token_index <= 0 || token_index + 1 >= line->tokens.size) {
            out_space_after = false;
            out_space_before = false;
            break;
        }

        // Otherwise use heuristic to check if it is a binop
        // The heuristic currently used: Check if previous and next are 'values' (e.g. literals/identifiers or parenthesis), if both are, then binop
        bool prev_is_value = false;
        {
            const auto& t = line->tokens[token_index - 1];
            if (t.type == Token_Type::IDENTIFIER || t.type == Token_Type::LITERAL) {
                prev_is_value = true;
            }
            if (t.type == Token_Type::PARENTHESIS && !t.options.parenthesis.is_open && t.options.parenthesis.type != Parenthesis_Type::BRACKETS) {
                prev_is_value = true;
            }
        }
        bool next_is_value = false;
        {
            const auto& t = line->tokens[token_index + 1];
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

        if (prev_is_value && next_is_value) {
            out_space_after = true;
            out_space_before = true;
        }
        else {
            out_space_after = false;
            out_space_before = false;
        }

        break;
    }

    default: panic("");
    }

    return;
}

bool auto_format_line(int line_index)
{
    auto& editor = syntax_editor;
    auto code = editor.code;

    source_code_tokenize_line(code, line_index);
    Source_Line* line = source_code_get_line(code, line_index);
    if (line->is_comment) {
        return false;
    }

    auto& text = line->text;
    bool cursor_on_line = editor.cursor.line == line_index;
    bool respect_cursor_space = editor.mode == Editor_Mode::INSERT && cursor_on_line;
    auto& pos = editor.cursor.character;

    // The auto-formater does the following things
    //  * Removes whitespaces between non-space critical characters, e.g. x : int  -->  x: int
    //  * Adds whitespaces between specific tokens,                  e.g. 5+15     -->  5 + 15
    //  * For writing, spaces before/after cursor aren't removed or added
    bool line_changed = false;

    // Delete whitespaces before first token
    {
        int delete_until = line->text.size;
        if (line->tokens.size > 0) {
            delete_until = line->tokens[0].start_index;
        }
        for (int i = 0; i < delete_until; i++) {
            history_delete_char(&editor.history, text_index_make(line_index, 0));
            line_changed = true;
            pos = math_maximum(0, pos - 1);
        }
        // Update token ranges
        for (int i = 0; i < line->tokens.size; i++) {
            line->tokens[i].start_index -= delete_until;
            line->tokens[i].end_index -= delete_until;
        }
    }

    // Go through tokens and check whitespaces between
    for (int i = 0; i < line->tokens.size - 1; i++)
    {
        Token& curr = line->tokens[i];
        Token& next = line->tokens[i + 1];

        // Check if spacing is expected between tokens
        bool space_between_tokens_expected = false;
        {
            bool space_before, space_after;
            token_expects_space_before_or_after(line, i, space_before, space_after);
            if (space_after) space_between_tokens_expected = true;
            token_expects_space_before_or_after(line, i + 1, space_before, space_after);
            if (space_before) space_between_tokens_expected = true;

            // Special handling for closing parenthesis, as I want a space there
            if (curr.type == Token_Type::PARENTHESIS && !curr.options.parenthesis.is_open && curr.options.parenthesis.type != Parenthesis_Type::BRACKETS &&
                next.type != Token_Type::OPERATOR && next.type != Token_Type::PARENTHESIS) {
                space_between_tokens_expected = true;
            }
        }

        // Check if spacing is as expected
        int index_shift_for_tokens_after_current = 0;
        if (curr.end_index == next.start_index)
        {
            if (space_between_tokens_expected) {
                // Insert space between tokens
                history_insert_char(&editor.history, text_index_make(line_index, curr.end_index), ' ');
                index_shift_for_tokens_after_current = 1;
            }
        }
        else if (curr.end_index != next.start_index)
        {
            char end = line->text.characters[curr.end_index - 1];
            char start = line->text.characters[next.start_index];

            // Remove excessive spaces (More than 1 space)
            {
                int space_count = next.start_index - curr.end_index;
                int delete_count = space_count - 1;
                if (respect_cursor_space && pos == curr.end_index + 1) {
                    delete_count = 0;
                }
                for (int i = 0; i < delete_count; i++) {
                    history_delete_char(&editor.history, text_index_make(line_index, curr.end_index));
                    index_shift_for_tokens_after_current -= 1;
                }
            }

            // Check if the final space should be removed
            bool remove_space = !space_between_tokens_expected;

            // Don't remove space if it's critical
            if (remove_space && char_is_space_critical(start) && char_is_space_critical(end)) {
                remove_space = false;
            }

            // Don't remove space if it's just before the cursor token
            if (curr.end_index + 1 == pos && respect_cursor_space) {
                remove_space = false;
            }

            // Don't remove space if it could cause lexing to change
            // Therefore check all operators if they contain a substring of the two added letters
            for (int j = 0; j < (int)Operator::MAX_ENUM_VALUE && remove_space; j++) 
            {
                String op_str = operator_get_string((Operator)j);
                for (int k = 0; k + 1 < op_str.size; k++) {
                    if (op_str.characters[k] == end && op_str.characters[k + 1] == start) {
                        remove_space = false;
                        break;
                    }
                }
            }

            if (remove_space) {
                history_delete_char(&editor.history, text_index_make(line_index, curr.end_index));
                index_shift_for_tokens_after_current -= 1;
            }
        }

        // Update cursor + follow tokens in space was removed
        if (index_shift_for_tokens_after_current != 0) 
        {
            line_changed = true;
            if (pos > curr.end_index && cursor_on_line) {
                pos = math_maximum(curr.end_index, pos + index_shift_for_tokens_after_current);
                syntax_editor_sanitize_cursor();
            }
            for (int j = i + 1; j < line->tokens.size; j++) {
                line->tokens[j].start_index += index_shift_for_tokens_after_current;
                line->tokens[j].end_index += index_shift_for_tokens_after_current;
            }
        }
    }

    // Delete whitespaces after last token
    if (line->tokens.size > 0)
    {
        const auto& last = line->tokens[line->tokens.size - 1];
        if (last.type == Token_Type::COMMENT) {
            return line_changed;
        }

        int delete_count = line->text.size - last.end_index; // Line text size changes, so store this first
        bool keep_cursor_space = cursor_on_line && pos > last.end_index;
        if (keep_cursor_space) delete_count -= 1;
        for (int i = 0; i < delete_count; i++) {
            history_delete_char(&editor.history, text_index_make(line_index, line->text.size - 1));
            line_changed = true;
        }

        if (keep_cursor_space) {
            pos = last.end_index + 1;
        }
    }

    return line_changed;
}



// Code Queries
Analysis_Pass* code_query_get_analysis_pass(AST::Node* base);
Symbol* code_query_get_ast_node_symbol(AST::Node* base)
{
    if (base->type != AST::Node_Type::DEFINITION_SYMBOL &&
        base->type != AST::Node_Type::PATH_LOOKUP &&
        base->type != AST::Node_Type::SYMBOL_LOOKUP &&
        base->type != AST::Node_Type::PARAMETER) {
        return 0;
    }

    auto pass = code_query_get_analysis_pass(base);
    if (pass == 0) return 0;
    switch (base->type)
    {
    case AST::Node_Type::DEFINITION_SYMBOL: {
        auto info = pass_get_node_info(pass, AST::downcast<AST::Definition_Symbol>(base), Info_Query::TRY_READ);
        return info == 0 ? 0 : info->symbol;
    }
    case AST::Node_Type::SYMBOL_LOOKUP: {
        auto info = pass_get_node_info(pass, AST::downcast<AST::Symbol_Lookup>(base), Info_Query::TRY_READ);
        return info == 0 ? 0 : info->symbol;
    }
    case AST::Node_Type::PATH_LOOKUP: {
        auto info = pass_get_node_info(pass, AST::downcast<AST::Path_Lookup>(base), Info_Query::TRY_READ);
        return info == 0 ? 0 : info->symbol;
    }
    case AST::Node_Type::PARAMETER: {
        auto info = pass_get_node_info(pass, AST::downcast<AST::Parameter>(base), Info_Query::TRY_READ);
        return info == 0 ? 0 : info->symbol;
    }
    }
    panic("");
    return 0;
}

Analysis_Pass* code_query_get_analysis_pass(AST::Node* base)
{
    auto& mappings = compiler.semantic_analyser->ast_to_pass_mapping;
    while (base != 0)
    {
        auto passes = hashtable_find_element(&mappings, base);
        if (passes != 0) {
            assert(passes->passes.size != 0, "");
            return passes->passes[0];
        }
        base = base->parent;
    }
    return 0;
}

Symbol_Table* code_query_get_ast_node_symbol_table(AST::Node* base)
{
    /*
    The three Nodes that have Symbol tables are:
        - Module
        - Function (Parameter symbol are here
        - Code-Block
    */
    Symbol_Table* table = compiler.semantic_analyser->root_symbol_table;
    Analysis_Pass* pass = code_query_get_analysis_pass(base);
    if (pass == 0) return table;
    while (base != 0)
    {
        switch (base->type)
        {
        case AST::Node_Type::MODULE: {
            auto info = pass_get_node_info(pass, AST::downcast<AST::Module>(base), Info_Query::TRY_READ);
            if (info == 0) { break; }
            return info->symbol_table;
        }
        case AST::Node_Type::CODE_BLOCK: {
            auto block = pass_get_node_info(pass, AST::downcast<AST::Code_Block>(base), Info_Query::TRY_READ);
            if (block == 0) { break; }
            return block->symbol_table;
        }
        case AST::Node_Type::EXPRESSION: {
            auto expr = AST::downcast<AST::Expression>(base);
            if (expr->type == AST::Expression_Type::FUNCTION) {
                if (pass->origin_workload->type == Analysis_Workload_Type::FUNCTION_HEADER) {
                    return ((Workload_Function_Header*)(pass->origin_workload))->progress->function->options.normal.parameter_table;
                }
            }
            break;
        }
        }
        base = base->parent;
    }
    return table;
}



// Code Completion
String code_completion_get_partially_typed_word()
{
    syntax_editor_synchronize_tokens();
    auto token = get_cursor_token(false);
    String result;
    if (token.type == Token_Type::IDENTIFIER) {
        result = *token.options.identifier;
    }
    else {
        return string_create_static("");
    }

    auto pos = syntax_editor.cursor.character;
    if (pos < token.end_index) {
        result = string_create_substring_static(&result, 0, pos - token.start_index);
    }
    return result;
}

void code_completion_find_suggestions()
{
    auto& editor = syntax_editor;
    auto& suggestions = editor.code_completion_suggestions;
    dynamic_array_reset(&suggestions);
    if (editor.mode != Editor_Mode::INSERT || editor.cursor.character == 0) {
        return;
    }

    if (editor.input_replay.currently_recording || editor.input_replay.currently_replaying) {
        return;
    }

    // Exit early if we are not in a completion context (Comments etc...)
    {
        syntax_editor_synchronize_tokens();
        auto current_token = get_cursor_token(false);
        if (current_token.type == Token_Type::COMMENT) {
            return;
        }
    }
    auto partially_typed = code_completion_get_partially_typed_word();
    fuzzy_search_start_search(partially_typed);

    // Check if we are on special node
    syntax_editor_synchronize_with_compiler(false);
    Token_Index cursor_token_index = token_index_make(syntax_editor.cursor.line, get_cursor_token_index(false));
    auto node = Parser::find_smallest_enclosing_node(&compiler.main_source->parsed_code->root->base, cursor_token_index);

    Symbol_Table* specific_table = 0;

    // Check for specific contexts where we can fill the suggestions more smartly (e.g. when typing struct member, module-path, auto-enum, ...)
    if (node->type == AST::Node_Type::EXPRESSION)
    {
        auto expr = AST::downcast<AST::Expression>(node);
        auto pass = code_query_get_analysis_pass(node);

        // Check if Expression is Member-Access
        Datatype* type = 0;
        if (expr->type == AST::Expression_Type::MEMBER_ACCESS && pass != 0) {
            auto info = pass_get_node_info(pass, expr->options.member_access.expr, Info_Query::TRY_READ);
            if (info != 0) {
                type = expression_info_get_type(info, false);
                if (info->result_type == Expression_Result_Type::TYPE) {
                    type = info->options.type;
                }
            }
        }
        else if (expr->type == AST::Expression_Type::AUTO_ENUM && pass != 0) {
            auto info = pass_get_node_info(pass, expr, Info_Query::TRY_READ);
            if (info != 0) {
                type = expression_info_get_type(info, false);
            }
        }

        if (type != 0)
        {
            auto original = type;
            type = type->base_type;
            switch (type->type)
            {
            case Datatype_Type::ARRAY:
            case Datatype_Type::SLICE: {
                fuzzy_search_add_item(string_create_static("data"));
                fuzzy_search_add_item(string_create_static("size"));
                break;
            }
            case Datatype_Type::STRUCT_INSTANCE_TEMPLATE:
            case Datatype_Type::STRUCT: 
            {
                Datatype_Struct* structure = 0;
                if (type->type == Datatype_Type::STRUCT) {
                    structure = downcast<Datatype_Struct>(type);
                }
                else if (type->type == Datatype_Type::STRUCT_INSTANCE_TEMPLATE) {
                    structure = downcast<Datatype_Struct_Instance_Template>(type)->struct_base->body_workload->struct_type;
                }

                Struct_Content* content = type_mods_get_subtype(structure, original->mods);
                auto& members = content->members;
                for (int i = 0; i < members.size; i++) {
                    auto& mem = members[i];
                    fuzzy_search_add_item(*mem.id);
                }
                if (content->subtypes.size > 0) {
                    fuzzy_search_add_item(*compiler.predefined_ids.tag);
                }
                for (int i = 0; i < content->subtypes.size; i++) {
                    auto sub = content->subtypes[i];
                    fuzzy_search_add_item(*sub->name);
                }
                // Add base name if available
                if (original->mods.subtype_index->indices.size > 0) {
                    Struct_Content* content = type_mods_get_subtype(structure, type->mods, type->mods.subtype_index->indices.size - 1);
                    fuzzy_search_add_item(*content->name);
                }
                break;
            }
            case Datatype_Type::ENUM:
                auto& members = downcast<Datatype_Enum>(type)->members;
                for (int i = 0; i < members.size; i++) {
                    auto& mem = members[i];
                    fuzzy_search_add_item(*mem.name);
                }
                break;
            }

            // Search for dot-calls
            Datatype* poly_base_type = compiler.type_system.predefined_types.unknown_type;
            if (type->type == Datatype_Type::STRUCT) {
                auto struct_type = downcast<Datatype_Struct>(type);
                if (struct_type->workload != 0 && struct_type->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
                    poly_base_type = upcast(struct_type->workload->polymorphic.instance.parent->body_workload->struct_type);
                }
            }

            auto context = code_query_get_ast_node_symbol_table(node)->operator_context;
            auto iter = hashtable_iterator_create(&context->custom_operators);
            while (hashtable_iterator_has_next(&iter))
            {
                SCOPE_EXIT(hashtable_iterator_next(&iter));

                Custom_Operator_Key key = *iter.key;
                Custom_Operator overload = *iter.value;

                if (key.type != Custom_Operator_Type::DOT_CALL) {
                    continue;
                }
                if (!(types_are_equal(type, key.options.dot_call.datatype) || types_are_equal(poly_base_type, key.options.dot_call.datatype))) {
                    continue;
                }
                fuzzy_search_add_item(*key.options.dot_call.id);
            }
        }

        // Check for function-call...
        if (type == 0)
        {
            // TODO!
            // AST::Node* parent = node;
            // while (parent != 0)
            // {
            //     if (parent->type == AST::Node_Type::EXPRESSION) {
            //         AST::Expression* expr = AST::downcast<AST::Expression>(parent);
            //         if (expr->type == AST::Expression_Type::FUNCTION_CALL) {

            //         }
            //         else if (expr->type == AST::Expression_Type::)
            //     }
            //     parent = node->parent;
            // }

        }
    }
    else if (node->type == AST::Node_Type::PATH_LOOKUP) {
        // This probably only happens if the last token was ~
        int cursor_token_index = get_cursor_token_index(false);
        auto prev_token = get_cursor_token(false);
        if (prev_token.type == Token_Type::OPERATOR && prev_token.options.op == Operator::TILDE && cursor_token_index > 0) {
            // Try to get token before ~, which should be an identifier...
            Token_Index prev = token_index_make(editor.cursor.line, cursor_token_index - 1);
            auto node = Parser::find_smallest_enclosing_node(&compiler.main_source->parsed_code->root->base, prev);
            auto pass = code_query_get_analysis_pass(node);
            if (node->type == AST::Node_Type::SYMBOL_LOOKUP && pass != 0) {
                auto info = pass_get_node_info(pass, AST::downcast<AST::Symbol_Lookup>(node), Info_Query::TRY_READ);
                if (info != 0) {
                    auto symbol = info->symbol;
                    if (symbol->type == Symbol_Type::MODULE) {
                        specific_table = symbol->options.module_progress->module_analysis->symbol_table;
                    }
                }
            }
        }
    }
    else if (node->type == AST::Node_Type::SYMBOL_LOOKUP)
    {
        auto lookup = AST::downcast<AST::Symbol_Lookup>(node);
        auto path = AST::downcast<AST::Path_Lookup>(node->parent);
        auto pass = code_query_get_analysis_pass(node);
        if (pass != 0)
        {
            // Find previous part of the current path
            int prev_index = -1;
            for (int i = 0; i < path->parts.size; i++) {
                if (path->parts[i] == lookup) {
                    prev_index = i - 1;
                    break;
                }
            }
            // Try to get symbol table from there
            if (prev_index != -1) {
                auto info = pass_get_node_info(pass, path->parts[prev_index], Info_Query::TRY_READ);
                if (info != 0) {
                    auto symbol = info->symbol;
                    if (symbol->type == Symbol_Type::MODULE) {
                        specific_table = symbol->options.module_progress->module_analysis->symbol_table;
                    }
                }
            }
        }
    }
    else {
        // Check if we should fill context options
        bool fill_context_options = false;
        auto& tokens = source_code_get_line(editor.code, editor.cursor.line)->tokens;
        if (tokens.size != 0 && tokens[0].type == Token_Type::KEYWORD && tokens[0].options.keyword == Keyword::CONTEXT) { 
            fill_context_options = get_cursor_char('!') == ' ';
        }

        if (fill_context_options) {
            auto& ids = compiler.predefined_ids;
            fuzzy_search_add_item(*ids.set_cast_option);
            fuzzy_search_add_item(*ids.id_import);
            fuzzy_search_add_item(*ids.add_binop);
            fuzzy_search_add_item(*ids.add_unop);
            fuzzy_search_add_item(*ids.add_cast);
            fuzzy_search_add_item(*ids.add_dot_call);
            fuzzy_search_add_item(*ids.add_array_access);
            fuzzy_search_add_item(*ids.add_iterator);
        }
    }

    // If no text has been written yet and we aren't on some special context (e.g. no suggestions), return
    if (specific_table == nullptr && fuzzy_search_get_item_count() == 0 && partially_typed.size == 0) {
        return;
    }

    // Search symbols
    if (fuzzy_search_get_item_count() == 0)
    {
        bool search_includes = specific_table == 0;
        if (specific_table == 0) {
            specific_table = code_query_get_ast_node_symbol_table(node);
        }
        if (specific_table != 0) {
            auto results = dynamic_array_create<Symbol*>(1);
            SCOPE_EXIT(dynamic_array_destroy(&results));
            symbol_table_query_id(specific_table, 0, search_includes, Symbol_Access_Level::INTERNAL, &results);
            for (int i = 0; i < results.size; i++) {
                fuzzy_search_add_item(*results[i]->id);
            }
        }
    }

    auto results = fuzzy_search_rank_results(true, 3);
    for (int i = 0; i < results.size; i++) {
        dynamic_array_push_back(&syntax_editor.code_completion_suggestions, results[i].item_name);
    }
}

void code_completion_insert_suggestion()
{
    auto& editor = syntax_editor;
    auto& suggestions = editor.code_completion_suggestions;

    string_reset(&editor.last_recorded_code_completion);

    if (suggestions.size == 0) return;
    if (editor.cursor.character == 0) return;
    String replace_string = suggestions[0];
    auto line = source_code_get_line(editor.code, editor.cursor.line);

    string_append_string(&editor.last_recorded_code_completion, &replace_string);

    // Remove current token
    int token_index = get_cursor_token_index(false);
    int start_pos = line->tokens[token_index].start_index;
    history_delete_text(&editor.history, text_index_make(editor.cursor.line, start_pos), editor.cursor.character);
    // Insert suggestion instead
    editor.cursor.character = start_pos;
    history_insert_text(&editor.history, editor.cursor, replace_string);
    editor.cursor.character += replace_string.size;
    auto_format_line(editor.cursor.line);
}



// Editor update
void editor_enter_insert_mode()
{
    if (syntax_editor.mode == Editor_Mode::INSERT) {
        return;
    }
    if (syntax_editor.record_insert_commands) {
        dynamic_array_reset(&syntax_editor.last_insert_commands);
    }
    syntax_editor.mode = Editor_Mode::INSERT;
    history_start_complex_command(&syntax_editor.history);
}

void editor_leave_insert_mode()
{
    if (syntax_editor.mode != Editor_Mode::INSERT) {
        return;
    }
    syntax_editor.mode = Editor_Mode::NORMAL;
    history_stop_complex_command(&syntax_editor.history);
    history_set_cursor_pos(&syntax_editor.history, syntax_editor.cursor);
    dynamic_array_reset(&syntax_editor.code_completion_suggestions);
    auto_format_line(syntax_editor.cursor.line);
}

void editor_split_line_at_cursor(int indentation_offset)
{
    auto& mode = syntax_editor.mode;
    auto& cursor = syntax_editor.cursor;
    Source_Line* line = source_code_get_line(syntax_editor.code, cursor.line);
    auto& text = line->text;
    int line_size = text.size; // Text may be invalid after applying history changes
    String cutout = string_create_substring_static(&text, cursor.character, text.size);

    history_start_complex_command(&syntax_editor.history);
    SCOPE_EXIT(history_stop_complex_command(&syntax_editor.history));

    int new_line_index = cursor.line + 1;
    history_insert_line(&syntax_editor.history, new_line_index, math_maximum(0, line->indentation + indentation_offset));

    if (cursor.character != line_size) {
        history_insert_text(&syntax_editor.history, text_index_make(new_line_index, 0), cutout);
        history_delete_text(&syntax_editor.history, cursor, line_size);
    }
    cursor = text_index_make(new_line_index, 0);
}

namespace Motions
{
    Source_Line* get_line(const Text_Index& pos) {
        auto code = syntax_editor.code;
        if (pos.line >= code->line_count || pos.line < 0) return nullptr;
        auto line = source_code_get_line(code, pos.line);
        if (pos.character > line->text.size || pos.character < 0) return nullptr;
        return line;
    }

    // Advances horizontally in current line. Returns true if index has changed
    bool move(Text_Index& pos, int value) {
        auto line = get_line(pos);
        if (line == nullptr) return false;
        int prev = pos.character;
        pos.character += value;
        if (pos.character < 0) pos.character = 0;
        if (pos.character > line->text.size) pos.character = line->text.size;
        return prev != pos.character;
    }

    void move_forwards_over_line(Text_Index& pos) {
        auto line = get_line(pos);
        if (line == nullptr) return;

        if (pos.character < line->text.size) {
            pos.character += 1;
            return;
        }

        if (pos.line + 1 >= syntax_editor.code->line_count) {
            return;
        }

        pos.character = 0;
        pos.line += 1;
    }

    void move_backwards_over_line(Text_Index& pos) 
    {
        if (pos.character > 0) {
            pos.character -= 1;
            return;
        }

        if (pos.line == 0) return;
        pos = text_index_make_line_end(syntax_editor.code, pos.line - 1);
    }
    
    char get_char(Text_Index& pos, int offset = 0, char invalid_char = '\0') {
        Source_Line* line = get_line(pos);
        if (line == nullptr) return invalid_char;
        int p = pos.character + offset;
        if (p < 0 || p >= line->text.size) return invalid_char;
        return line->text.characters[p];
    }

    // Returns true if a character was found that matched the test_fn
    bool goto_next_in_set(Text_Index& pos, char_test_fn test_fn, void* userdata = nullptr, bool forward = true, bool skip_current_char = false) 
    {
        Source_Line* line = get_line(pos);
        if (line == nullptr) return false;

        int dir = forward ? 1 : -1;
        for (int i = pos.character + (skip_current_char ? dir : 0); i < line->text.size && i >= 0; i += dir)
        {
            char c = line->text.characters[i];
            if (test_fn(c, userdata)) {
                pos.character = i;
                return true;
            }
        }

        return false;
    }

    bool move_while_in_set(Text_Index& pos, char_test_fn test_fn, void* userdata = nullptr, bool invert_set = false, bool forward = true) 
    {
        Source_Line* line = get_line(pos);
        if (line == nullptr) return false;

        int dir = forward ? 1 : -1;
        int last_valid = pos.character;
        for (int i = last_valid; i < line->text.size && i >= 0; i += dir)
        {
            char c = line->text.characters[i];
            bool result = test_fn(c, userdata);
            if (invert_set) result = !result;
            if (result) {
                last_valid = i;
            }
            else {
                break;
            }
        }

        pos.character = last_valid;
        return false;
    }

    void skip_in_set(Text_Index& pos, char_test_fn test_fn, void* userdata = nullptr, bool invert_set = false, bool forward = true)
    {
        Source_Line* line = get_line(pos);
        if (line == nullptr) return;

        int dir = forward ? 1 : -1;
        int index = pos.character;
        while (index >= 0 && index < line->text.size) {
            char c = line->text.characters[index];
            bool result = test_fn(c, userdata);
            if (invert_set) result = !result;
            if (!result) break;
            index += dir;
        }

        pos.character = math_maximum(0, index);
    }

    Text_Range text_range_get_island(Text_Index pos, char_test_fn test_fn, void* userdata = nullptr, bool invert_set = false)
    {
        Text_Index start = pos;
        move_while_in_set(start, test_fn, userdata, invert_set, false); // Go back to island start
        Text_Index end = pos;
        skip_in_set(end, test_fn, userdata, invert_set);
        return text_range_make(start, end);
    }

    Text_Range text_range_get_word(Text_Index pos)
    {
        auto line = get_line(pos);
        if (line == nullptr) return text_range_make(pos, pos);
        auto text = line->text;

        // If char is whitespace, return the whole whitespace as the word
        char c = get_char(pos);
        if (char_is_whitespace(c)) {
            return text_range_get_island(pos, char_is_whitespace);
        }
        
        // If we are on a identifier word
        if (char_is_valid_identifier(c)) {
            return text_range_get_island(pos, char_is_valid_identifier);
        }
        return text_range_get_island(pos, char_is_operator);
    }

    Text_Range text_range_get_parenthesis(Text_Index pos, char start_char, char end_char)
    {
        auto code = syntax_editor.code;

        // 1. Go back to previous occurance of text...
        Text_Index start = pos;
        Source_Line* line = source_code_get_line(code, start.line);

        // Special handling for string literals ""
        if (start_char == '\"' && end_char == '\"') 
        {
            int index = 0;
            bool inside_string = false;
            int string_start = -1;
            while (index < line->text.size)
            {
                char c = line->text.characters[index];
                if (c == '\"') {
                    inside_string = !inside_string;
                    if (!inside_string && pos.character >= string_start && pos.character <= index) {
                        return text_range_make(text_index_make(pos.line, string_start), text_index_make(pos.line, index + 1));
                    }
                    string_start = index;
                }
                else if (c == '\\') {
                    index += 2; // Skip this + escaped 
                    continue;
                }
                index += 1;
            }

            if (inside_string && pos.character >= string_start) {
                return text_range_make(text_index_make(pos.line, string_start), text_index_make(pos.line, line->text.size));
            }
        }

        bool found = false;
        bool is_block_parenthesis = false;
        // Try to find start parenthesis on current line
        {
            auto text = line->text;
            int depth = 0;
            for (int i = start.character; i >= 0; i -= 1) {
                char c = text.characters[i];
                if (c == start_char) {
                    if (depth == 0) {
                        found = true;
                        start.character = i;
                        is_block_parenthesis = i == text.size - 1;
                        break;
                    }
                    else {
                        depth -= 1;
                    }
                }
                else if (c == end_char && i != start.character) {
                    depth += 1;
                }
            }
        }

        // Try to find start parenthesis on previous block end...
        if (!found)
        {
            int start_indent = line->indentation;
            for (int i = start.line - 1; i >= 0; i -= 1) 
            {
                line = source_code_get_line(code, i);
                if (line->indentation == start_indent - 1) 
                {
                    if (line->text.size != 0 && line->text.characters[line->text.size-1] == start_char) {
                        found = true;
                        start.line = i;
                        start.character = line->text.size - 1;
                        is_block_parenthesis = true;
                    }
                    break;
                }
            }
        }

        if (!found) {
            return text_range_make(pos, pos);
        }

        // Now find parenthesis end...
        Text_Index end = start;
        found = false;
        if (!is_block_parenthesis)
        {
            // Try to find end parenthesis on current line...
            int depth = 0;
            for (int i = end.character + 1; i < line->text.size; i++) 
            {
                char c = line->text.characters[i];
                if (c == end_char) {
                    if (depth == 0) {
                        end.character = i + 1;
                        found = true;
                        break;
                    }
                    else {
                        depth -= 1;
                    }
                }
                else if (c == start_char) {
                    depth += 1;
                }
            }
        }
        else
        {
            // Find end parenthesis at end of block
            int start_indent = line->indentation;
            for (int i = start.line + 1; i < code->line_count; i++) 
            {
                line = source_code_get_line(code, i);
                if (line->indentation == start_indent) {
                    if (line->text.size != 0 && line->text.characters[0] == end_char) {
                        end.character = 1;
                        end.line = i;
                        found = true;
                        break;
                    }
                    break;
                }
                else if (line->indentation < start_indent) {
                    break;
                }
            }
        }

        // Return result if available
        if (!found) {
            return text_range_make(pos, pos);
        }
        return text_range_make(start, end);
    }
}

namespace Parsing
{
    Movement movement_make(Movement_Type movement_type, int repeat_count, char search_char = '\0') {
        Movement result;
        result.type = movement_type;
        result.repeat_count = repeat_count;
        result.search_char = search_char;
        return result;
    }

    Motion motion_make(Motion_Type motion_type, int repeat_count, bool contains_edges) {
        Motion result;
        result.motion_type = motion_type;
        result.repeat_count = repeat_count;
        result.contains_edges = contains_edges;
        return result;
    }
    
    Motion motion_make_from_movement(Movement movement) 
    {
        Motion result;
        result.motion_type = Motion_Type::MOVEMENT;
        result.movement = movement;
        result.repeat_count = 1;
        result.contains_edges = false;

        // Handle up/down movements as whole line movements, like vim does
        if (movement.type == Movement_Type::MOVE_UP || movement.type == Movement_Type::MOVE_DOWN) {
            int dir = movement.type == Movement_Type::MOVE_UP ? -1 : 1;
            int repeat_count = movement.repeat_count;
            return Parsing::motion_make(Motion_Type::LINE, movement.repeat_count * dir, true);
        }

        return result;
    }

    Normal_Mode_Command normal_mode_command_make(Normal_Mode_Command_Type command_type, int repeat_count) {
        Normal_Mode_Command result;
        result.type = command_type;
        result.repeat_count = repeat_count;
        return result;
    }

    Normal_Mode_Command normal_mode_command_make_char(Normal_Mode_Command_Type command_type, int repeat_count, char character) {
        Normal_Mode_Command result;
        result.type = command_type;
        result.repeat_count = repeat_count;
        result.options.character = character;
        return result;
    }
    
    Normal_Mode_Command normal_mode_command_make_motion(Normal_Mode_Command_Type command_type, int repeat_count, Motion motion) {
        Normal_Mode_Command result;
        result.type = command_type;
        result.repeat_count = repeat_count;
        result.options.motion = motion;
        return result;
    }
    
    Normal_Mode_Command normal_mode_command_make_movement(Normal_Mode_Command_Type command_type, int repeat_count, Movement movement) {
        Normal_Mode_Command result;
        result.type = command_type;
        result.repeat_count = 1;
        result.options.movement = movement;
        return result;
    }

    enum class Parse_Result_Type
    {
        SUCCESS,
        COMPLETABLE,
        FAILURE
    };

    template<typename T>
    struct Parse_Result
    {
        Parse_Result_Type type;
        T result;
    };

    template<typename T>
    Parse_Result<T> parse_result_success(T t) {
        Parse_Result<T> result;
        result.type = Parse_Result_Type::SUCCESS;
        result.result = t;
        return result;
    }

    template<typename T>
    Parse_Result<T> parse_result_failure() {
        Parse_Result<T> result;
        result.type = Parse_Result_Type::FAILURE;
        return result;
    }

    template<typename T>
    Parse_Result<T> parse_result_completable() {
        Parse_Result<T> result;
        result.type = Parse_Result_Type::COMPLETABLE;
        return result;
    }

    // Note: Parses a repeat-count, so 0 is not counted here, as this is a movement
    int parse_repeat_count(int& index, int return_value_if_invalid)
    {
        auto& buffer = syntax_editor.command_buffer;
        int result = 0;
        // Parse repeat count if existing
        if (index >= buffer.size) return return_value_if_invalid;
    
        int start_index = index;
    
        char c = buffer[index];
        if (c == '0') return return_value_if_invalid;
        while (char_is_digit(c) && index < buffer.size) {
            result = result * 10 + char_digit_value(c);
            index += 1;
            c = buffer[index];
        }
    
        if (index != start_index) return result;
        return return_value_if_invalid;
    }

    Parse_Result<Movement> parse_movement(int& index, int prev_repeat_count_parse = -1)
    {
        auto cmd = syntax_editor.command_buffer;

        bool repeat_count_exists = true;
        int repeat_count = prev_repeat_count_parse;
        if (repeat_count == -1) {
            int prev_index = index;
            repeat_count = parse_repeat_count(index, 1);
            repeat_count_exists = prev_index != index;
        }

        if (index >= cmd.size) return parse_result_completable<Movement>();

        switch (cmd.characters[index])
        {
        // Single character movements
        case 'h': index += 1; return parse_result_success(movement_make(Movement_Type::MOVE_LEFT, repeat_count));
        case 'l': index += 1; return parse_result_success(movement_make(Movement_Type::MOVE_RIGHT, repeat_count));
        case 'j': index += 1; return parse_result_success(movement_make(Movement_Type::MOVE_DOWN, repeat_count));
        case 'k': index += 1; return parse_result_success(movement_make(Movement_Type::MOVE_UP, repeat_count));
        case '0': index += 1; return parse_result_success(movement_make(Movement_Type::TO_START_OF_LINE, repeat_count));
        case '$': index += 1; return parse_result_success(movement_make(Movement_Type::TO_END_OF_LINE, repeat_count));
        case 'w': index += 1; return parse_result_success(movement_make(Movement_Type::NEXT_WORD, repeat_count));
        case 'W': index += 1; return parse_result_success(movement_make(Movement_Type::NEXT_SPACE, repeat_count));
        case 'b': index += 1; return parse_result_success(movement_make(Movement_Type::PREVIOUS_WORD, repeat_count));
        case 'B': index += 1; return parse_result_success(movement_make(Movement_Type::PREVIOUS_SPACE, repeat_count));
        case 'e': index += 1; return parse_result_success(movement_make(Movement_Type::END_OF_WORD, repeat_count));
        case 'E': index += 1; return parse_result_success(movement_make(Movement_Type::END_OF_WORD_AFTER_SPACE, repeat_count));
        case '%': index += 1; return parse_result_success(movement_make(Movement_Type::JUMP_ENCLOSURE, repeat_count));
        case ';': index += 1; return parse_result_success(movement_make(Movement_Type::REPEAT_LAST_SEARCH , repeat_count));
        case ',': index += 1; return parse_result_success(movement_make(Movement_Type::REPEAT_LAST_SEARCH_REVERSE_DIRECTION, repeat_count));
        case '}': index += 1; return parse_result_success(movement_make(Movement_Type::BLOCK_END, repeat_count));
        case '{': index += 1; return parse_result_success(movement_make(Movement_Type::BLOCK_START, repeat_count));

        // Character search movements:
        case 'f':
        case 'F':
        case 't':
        case 'T': {
            Movement_Type type;
            if (cmd.characters[index] == 'f') {
                type = Movement_Type::SEARCH_FORWARDS_FOR;
            }
            else if (cmd.characters[index] == 'F') {
                type = Movement_Type::SEARCH_BACKWARDS_FOR;
            }
            else if (cmd.characters[index] == 't') {
                type = Movement_Type::SEARCH_FORWARDS_TO;
            }
            else if (cmd.characters[index] == 'T') {
                type = Movement_Type::SEARCH_BACKWARDS_TO;
            }
            else {
                type = (Movement_Type)-1;
                panic("");
            }

            if (index + 1 >= cmd.size) {
                return parse_result_completable<Movement>();
            }

            auto result = parse_result_success(movement_make(type, repeat_count, cmd.characters[index + 1]));
            index += 2;
            return result;
        }

        // Line index movements
        case 'G': {
                if (repeat_count_exists) {
                    index += 1;
                    return parse_result_success(movement_make(Movement_Type::GOTO_LINE_NUMBER, repeat_count));
                }
                else {
                    index += 1;
                    return parse_result_success(movement_make(Movement_Type::GOTO_END_OF_TEXT, repeat_count));
                }
        }
        case 'g': {
            if (repeat_count_exists) {
                // Note: This is a non-vim shortcut, so that 123g is the same as 123G
                index += 1;
                return parse_result_success(movement_make(Movement_Type::GOTO_LINE_NUMBER, repeat_count));
            }

            if (index + 1 >= cmd.size) {
                return parse_result_completable<Movement>();
            }
            if (cmd[index + 1] == 'g') {
                index += 2;
                return parse_result_success(movement_make(Movement_Type::GOTO_START_OF_TEXT, 1));
            }
            return parse_result_failure<Movement>();
        }
        }
    
        return parse_result_failure<Movement>();
    }

    Parse_Result<Motion> parse_motion(int& index)
    {
        auto& cmd = syntax_editor.command_buffer;

        // Parse repeat count
        int prev_index = index;
        int repeat_count = parse_repeat_count(index, 1);
        bool repeat_count_exists = prev_index != index;
    
        // Motions may also be movements, so we check if we can parse a movement first
        Parse_Result<Movement> movement_parse = parse_movement(index, repeat_count_exists ? repeat_count : -1);
        if (movement_parse.type == Parse_Result_Type::SUCCESS) {
            return parse_result_success(motion_make_from_movement(movement_parse.result));
        }
        else if (movement_parse.type == Parse_Result_Type::COMPLETABLE) {
            return parse_result_completable<Motion>();
        }
    
        if (index >= cmd.size) return parse_result_completable<Motion>();

        // Now we need to check for real motions, which may start with a repeat count
        if (cmd[index] != 'i' && cmd[index] != 'a') return parse_result_failure<Motion>();
        bool contains_edges = cmd[index] == 'a';

        index += 1;
        if (index >= cmd.size) return parse_result_completable<Motion>();
    
        // Now we need to determine the motion
        char c = cmd[index];
        index += 1;
        switch (c)
        {
        case 'w': return parse_result_success(motion_make(Motion_Type::WORD, repeat_count, contains_edges));
        case 'W': return parse_result_success(motion_make(Motion_Type::SPACES, repeat_count, contains_edges));
        case ')': 
        case '(': return parse_result_success(motion_make(Motion_Type::PARENTHESES, repeat_count, contains_edges));
        case '{': 
        case '}': return parse_result_success(motion_make(Motion_Type::BRACES, repeat_count, contains_edges));
        case '[':
        case ']': return parse_result_success(motion_make(Motion_Type::BRACKETS, repeat_count, contains_edges));
        case '"': return parse_result_success(motion_make(Motion_Type::QUOTATION_MARKS, repeat_count, contains_edges));
        // case 'p': return parse_result_success(motion_make(Motion_Type::PARAGRAPH_WITHOUT_INDENT, repeat_count, contains_edges));
        // case 'P': return parse_result_success(motion_make(Motion_Type::PARAGRAPH_WITH_INDENT, repeat_count, contains_edges));
        case 'b':
        case 'B': return parse_result_success(motion_make(Motion_Type::BLOCK, repeat_count, contains_edges));
        }
    
        index -= 1;
        return parse_result_failure<Motion>();
    }

    Parse_Result<Insert_Command> parse_insert_command(const Key_Message& msg)
    {
        Insert_Command input;
        if ((msg.key_code == Key_Code::P || msg.key_code == Key_Code::N) && msg.ctrl_down && msg.key_down) {
            input.type = Insert_Command_Type::INSERT_CODE_COMPLETION;
        }
        else if ((msg.key_code == Key_Code::W) && msg.ctrl_down && msg.key_down) {
            input.type = Insert_Command_Type::DELETE_LAST_WORD;
        }
        else if ((msg.key_code == Key_Code::U) && msg.ctrl_down && msg.key_down) {
            input.type = Insert_Command_Type::DELETE_TO_LINE_START;
        }
        else if (msg.key_code == Key_Code::SPACE && msg.key_down) {
            if (msg.shift_down && syntax_editor.code_completion_suggestions.size > 0) {
                input.type = Insert_Command_Type::INSERT_CODE_COMPLETION;
            }
            else {
                input.type = Insert_Command_Type::SPACE;
            }
        }
        else if (msg.key_code == Key_Code::L && msg.key_down && msg.ctrl_down) {
            input.type = Insert_Command_Type::EXIT_INSERT_MODE;
        }
        else if (msg.key_code == Key_Code::ARROW_LEFT && msg.key_down) {
            input.type = Insert_Command_Type::MOVE_LEFT;
        }
        else if (msg.key_code == Key_Code::ARROW_RIGHT && msg.key_down) {
            input.type = Insert_Command_Type::MOVE_RIGHT;
        }
        else if (msg.key_code == Key_Code::BACKSPACE && msg.key_down) {
            input.type = Insert_Command_Type::BACKSPACE;
        }
        else if (msg.key_code == Key_Code::RETURN && msg.key_down) {
            if (msg.shift_down) {
                input.type = Insert_Command_Type::ENTER_REMOVE_ONE_INDENT;
            }
            else {
                input.type = Insert_Command_Type::ENTER;
            }
        }
        else if (char_is_letter(msg.character) || msg.character == '_') {
            input.type = Insert_Command_Type::IDENTIFIER_LETTER;
            input.letter = msg.character;
        }
        else if (char_is_digit(msg.character)) {
            input.type = Insert_Command_Type::NUMBER_LETTER;
            input.letter = msg.character;
        }
        else if (msg.key_code == Key_Code::TAB && msg.key_down) {
            if (msg.shift_down) {
                input.type = Insert_Command_Type::REMOVE_INDENTATION;
            }
            else {
                input.type = Insert_Command_Type::ADD_INDENTATION;
            }
        }
        else if (msg.key_down && msg.character != -1) {
            if (string_contains_character(characters_get_non_identifier_non_whitespace(), msg.character)) {
                input.type = Insert_Command_Type::DELIMITER_LETTER;
                input.letter = msg.character;
            }
            else {
                return parse_result_failure<Insert_Command>();
            }
        }
        else {
            return parse_result_failure<Insert_Command>();
        }

        return parse_result_success(input);
    }

    Parse_Result<Normal_Mode_Command> parse_normal_command(int& index)
    {
        auto& cmd = syntax_editor.command_buffer;

        int prev_index = index;
        int repeat_count = parse_repeat_count(index, 1);
        bool repeat_count_exists = prev_index != index;
        if (index >= cmd.size) return parse_result_completable<Normal_Mode_Command>();

        // Check if it is a movement
        Parse_Result<Movement> movement_parse = parse_movement(index, repeat_count_exists ? repeat_count : -1);
        if (movement_parse.type == Parse_Result_Type::SUCCESS) {
            return parse_result_success(normal_mode_command_make_movement(Normal_Mode_Command_Type::MOVEMENT, 1, movement_parse.result));
        }
        else if (movement_parse.type == Parse_Result_Type::COMPLETABLE) {
            return parse_result_completable<Normal_Mode_Command>();
        }

        if (index >= cmd.size) return parse_result_completable<Normal_Mode_Command>();
    
        // Check character
        Normal_Mode_Command_Type command_type = Normal_Mode_Command_Type::MAX_ENUM_VALUE;
        bool parse_motion_afterwards = false;
        char curr_char = cmd[index];
        char follow_char = index + 1 < cmd.size ? cmd[index + 1] : '?';
        bool follow_char_valid = index + 1 < cmd.size;
        index += 1;
        switch (curr_char)
        {
        case 'x': 
            return parse_result_success(
                    normal_mode_command_make_motion(
                        Normal_Mode_Command_Type::DELETE_MOTION, repeat_count, 
                        motion_make_from_movement(movement_make(Movement_Type::MOVE_RIGHT, 1))
                    )
            );
        case 'i':
            return parse_result_success(normal_mode_command_make_movement(
                    Normal_Mode_Command_Type::ENTER_INSERT_MODE_AFTER_MOVEMENT, 1,
                    movement_make(Movement_Type::MOVE_LEFT, 0))
            );
        case 'I':
            return parse_result_success(normal_mode_command_make_movement(
                    Normal_Mode_Command_Type::ENTER_INSERT_MODE_AFTER_MOVEMENT, 1,
                    movement_make(Movement_Type::TO_START_OF_LINE, 1))
            );
        case 'a':
            return parse_result_success(normal_mode_command_make_movement(
                    Normal_Mode_Command_Type::ENTER_INSERT_MODE_AFTER_MOVEMENT, 1,
                    movement_make(Movement_Type::MOVE_RIGHT, 1))
            );
        case 'A':
            return parse_result_success(normal_mode_command_make_movement(
                    Normal_Mode_Command_Type::ENTER_INSERT_MODE_AFTER_MOVEMENT, 1,
                    movement_make(Movement_Type::TO_END_OF_LINE, 1))
            );
        case 'o': return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::ENTER_INSERT_MODE_NEW_LINE_BELOW, 1));
        case 'O': return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::ENTER_INSERT_MODE_NEW_LINE_ABOVE, 1));
        case '.': return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::REPEAT_LAST_COMMAND, repeat_count));
        case 'D':
            return parse_result_success(
                    normal_mode_command_make_motion(
                        Normal_Mode_Command_Type::DELETE_MOTION, repeat_count, 
                        motion_make_from_movement(movement_make(Movement_Type::TO_END_OF_LINE, 1))
                    )
            );
        case 'C':
            return parse_result_success(
                    normal_mode_command_make_motion(
                        Normal_Mode_Command_Type::CHANGE_MOTION, repeat_count, 
                        motion_make_from_movement(movement_make(Movement_Type::TO_END_OF_LINE, 1))
                    )
            );
        case 'Y': 
            return parse_result_success(
                    normal_mode_command_make_motion(
                        Normal_Mode_Command_Type::YANK_MOTION, 0,
                        motion_make(Motion_Type::LINE, repeat_count - 1, false)
                    )
            );
        case 'L': return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_BOTTOM, 1));
        case 'M': return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_CENTER, 1));
        case 'H': return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_TOP, 1));
        case 'p': return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::PUT_AFTER_CURSOR, repeat_count));
        case 'P': return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::PUT_BEFORE_CURSOR, repeat_count));
        case 'u': return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::UNDO, repeat_count));

        case 'r': {
            if (!follow_char_valid) {
                return parse_result_completable<Normal_Mode_Command>();
            }
            return parse_result_success(normal_mode_command_make_char(Normal_Mode_Command_Type::REPLACE_CHAR, 1, follow_char));
        }
        case 'd': 
        case 'c':
        case 'y': 
        {
            if (!follow_char_valid) {
                return parse_result_completable<Normal_Mode_Command>();
            }
            bool include_edges = false;
            if (curr_char == 'd') {
                command_type = Normal_Mode_Command_Type::DELETE_MOTION;
                include_edges = true;
            }
            else if (curr_char == 'c') {
                command_type = Normal_Mode_Command_Type::CHANGE_MOTION;
            }
            else if (curr_char == 'y') {
                command_type = Normal_Mode_Command_Type::YANK_MOTION;
            }
            parse_motion_afterwards = true;

            if (follow_char == curr_char) { // dd, yy or cc
                index += 1;
                return parse_result_success(
                    normal_mode_command_make_motion(
                        command_type, 1,
                        motion_make(Motion_Type::LINE, repeat_count - 1, include_edges)
                    )
                );
            }
            break;
        }
        case 'v': command_type = Normal_Mode_Command_Type::VISUALIZE_MOTION; parse_motion_afterwards = true; break;
        case 'R': command_type = Normal_Mode_Command_Type::REPLACE_MOTION_WITH_YANK; parse_motion_afterwards = true; break;
        case 'z': {
            if (!follow_char_valid) {
                return parse_result_completable<Normal_Mode_Command>();
            }

            if (follow_char == 't') {
                return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_TOP, 1));
            }
            else if (follow_char == 'z') {
                return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_CENTER, 1));
            }
            else if (follow_char == 'b') {
                return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_BOTTOM, 1));
            }
            return parse_result_failure<Normal_Mode_Command>();
        }
        }

        // Check if further parsing is required
        if (command_type == Normal_Mode_Command_Type::MAX_ENUM_VALUE) {
            return parse_result_failure<Normal_Mode_Command>();
        }

        if (!parse_motion_afterwards) {
            return parse_result_success<Normal_Mode_Command>(normal_mode_command_make(command_type, repeat_count));
        }
        else
        {
            Parse_Result<Motion> motion = parse_motion(index);
            if (motion.type == Parse_Result_Type::SUCCESS) {
                return parse_result_success<Normal_Mode_Command>(normal_mode_command_make_motion(command_type, repeat_count, motion.result));
            }
            else if (motion.type == Parse_Result_Type::COMPLETABLE) {
                return parse_result_completable<Normal_Mode_Command>();
            }
            else {
                return parse_result_failure<Normal_Mode_Command>();
            }
        }

        return parse_result_failure<Normal_Mode_Command>();
    }
};


Text_Index movement_evaluate(const Movement& movement, Text_Index pos)
{
    auto code = syntax_editor.code;
    auto& editor = syntax_editor;

    pos = sanitize_index(pos);
    Source_Line* line = source_code_get_line(code, pos.line);

    auto do_char_search = [&](char c, bool forward, bool search_towards) {
        auto char_equals = [](char c, void* test_char) -> bool {
            char other = *(char*)test_char;
            return c == other;
        };
        Text_Index start = pos;
        bool found = Motions::goto_next_in_set(start, char_equals, (void*) &c, forward, true);
        if (found) {
            pos = start;
            if (search_towards) {
                pos.character += forward ? -1 : 1;
            }
        }
    };

    bool repeat_movement = true;
    bool set_horizontal_pos = true;
    for (int i = 0; i < movement.repeat_count && repeat_movement; i++)
    {
        switch (movement.type)
        {
        case Movement_Type::MOVE_DOWN: 
        case Movement_Type::MOVE_UP: {
            int dir = movement.type == Movement_Type::MOVE_UP ? -1 : 1;
            pos.line += dir * movement.repeat_count;
            pos = sanitize_index(pos);
            line = source_code_get_line(code, pos.line);

            // Set position on line
            pos.character = syntax_editor.last_line_x_pos - line->indentation * 4;
            set_horizontal_pos = false;
            repeat_movement = false;
            break;
        }
        case Movement_Type::MOVE_LEFT: {
            pos.character -= movement.repeat_count;
            repeat_movement = false;
            break;
        }
        case Movement_Type::MOVE_RIGHT: {
            pos.character += movement.repeat_count;
            repeat_movement = false;
            break;
        }
        case Movement_Type::TO_END_OF_LINE: {
            pos.character = line->text.size;
            editor.last_line_x_pos = 10000; // Look at jk movements after $ to understand this
            set_horizontal_pos = false;
            repeat_movement = false;
            break;
        }
        case Movement_Type::TO_START_OF_LINE: {
            pos.character = 0;
            editor.last_line_x_pos = 0; // Look at jk movements after $ to understand this
            set_horizontal_pos = false;
            repeat_movement = false;
            break;
        }
        case Movement_Type::GOTO_END_OF_TEXT: {
            pos = text_index_make(code->line_count - 1, 0);
            repeat_movement = false;
            break;
        }
        case Movement_Type::GOTO_START_OF_TEXT: {
            pos = text_index_make(0, 0);
            repeat_movement = false;
            break;
        }
        case Movement_Type::GOTO_LINE_NUMBER: {
            pos = text_index_make(movement.repeat_count, 0);
            repeat_movement = false;
            break;
        }
        case Movement_Type::NEXT_WORD:
        {
            Text_Range range = Motions::text_range_get_word(pos);
            pos = range.end;
            Motions::skip_in_set(pos, char_is_whitespace);
            break;
        }
        case Movement_Type::NEXT_SPACE: {
            Motions::skip_in_set(pos, char_is_whitespace, nullptr, true); // Skip current non-whitespaces
            Motions::skip_in_set(pos, char_is_whitespace); // Skip current whitespaces
            break;
        }
        case Movement_Type::END_OF_WORD: 
        {
            if (char_is_whitespace(Motions::get_char(pos))) {
                Motions::skip_in_set(pos, char_is_whitespace); // Skip current whitespaces
                Text_Range range = Motions::text_range_get_word(pos);
                pos.character = math_maximum(range.start.character, range.end.character - 1);
                break;
            }

            Text_Range range = Motions::text_range_get_word(pos);
            // Check if we are currently at end of word
            if (pos.character == range.end.character - 1) {
                Motions::move(pos, 1);
                Motions::skip_in_set(pos, char_is_whitespace); // Skip current whitespaces
                range = Motions::text_range_get_word(pos);
            }
            pos.character = math_maximum(range.start.character, range.end.character - 1);
            break;
        }
        case Movement_Type::END_OF_WORD_AFTER_SPACE: 
        {
            if (char_is_whitespace(Motions::get_char(pos))) {
                Motions::skip_in_set(pos, char_is_whitespace); // Skip current whitespaces
                Text_Range range = Motions::text_range_get_island(pos, char_is_whitespace, nullptr, true);
                pos.character = math_maximum(range.start.character, range.end.character - 1);
                break;
            }

            Text_Range range = Motions::text_range_get_island(pos, char_is_whitespace, nullptr, true);
            // Check if we are currently at end of word
            if (pos.character == range.end.character - 1) {
                Motions::move(pos, 1);
                Motions::skip_in_set(pos, char_is_whitespace); // Skip current whitespaces
                range = Motions::text_range_get_island(pos, char_is_whitespace, nullptr, true);
            }
            pos.character = math_maximum(range.start.character, range.end.character - 1);
            break;
        }
        case Movement_Type::PREVIOUS_SPACE: {
            int prev = pos.character;
            Motions::move_while_in_set(pos, char_is_whitespace, nullptr, true, false); 
            pos = Motions::text_range_get_island(pos, char_is_whitespace, nullptr, true).start;
            if (pos.character != prev) break;

            Motions::move(pos, -1);
            Motions::skip_in_set(pos, char_is_whitespace, nullptr, false, false); // Skip whitespaces
            Motions::move_while_in_set(pos, char_is_whitespace, nullptr, true, false); // Move backwards until start of word
            break;
        }
        case Movement_Type::PREVIOUS_WORD: {
            int prev = pos.character;
            Motions::move_while_in_set(pos, char_is_whitespace, nullptr, true, false); // Move backwards until start of word
            pos = Motions::text_range_get_word(pos).start;
            if (pos.character != prev) break;

            Motions::move(pos, -1);
            Motions::skip_in_set(pos, char_is_whitespace, nullptr, false, false); // Skip whitespaces
            pos = Motions::text_range_get_word(pos).start;
            break;
        }
        case Movement_Type::JUMP_ENCLOSURE: 
        {
            char c = Motions::get_char(pos);

            // If on some type of parenthesis its quite logical () {} [] <>, just search next thing
            // The question is what to do when not on such a thing
            char open_parenthesis = '\0';
            char closed_parenthesis = '\0';
            switch (c) {
            case '(': open_parenthesis = '('; closed_parenthesis = ')'; break;
            case ')': open_parenthesis = '('; closed_parenthesis = ')'; break;
            case '{': open_parenthesis = '{'; closed_parenthesis = '}'; break;
            case '}': open_parenthesis = '{'; closed_parenthesis = '}'; break;
            case '[': open_parenthesis = '['; closed_parenthesis = ']';  break;
            case ']': open_parenthesis = '['; closed_parenthesis = ']'; break;
            case '\"': open_parenthesis = '\"'; closed_parenthesis = '\"'; break;
            default: break;
            }
            if (open_parenthesis == '\0') {
                break;
            }

            Text_Range range = Motions::text_range_get_parenthesis(pos, open_parenthesis, closed_parenthesis);
            if (!text_index_equal(range.start, range.end)) {
                if (text_index_equal(pos, range.start)) {
                    pos = range.end;
                    pos.character -= 1;
                }
                else {
                    pos = range.start;
                }
            }
            break;
        }
        case Movement_Type::BLOCK_START: 
        {
            int line_index = pos.line;
            auto line = source_code_get_line(code, line_index);
            int start_indent = line->indentation;

            // Go up lines while indent is smaller
            // Skip current block of lines
            while (line_index >= 0) {
                auto line = source_code_get_line(code, line_index);
                if (line->indentation < start_indent) {
                    line_index = line_index + 1; // e.g. the previous line was the last of the block
                    break;
                }
                line_index -= 1;
            }
            line_index = math_maximum(0, line_index);

            pos.line = line_index;
            pos.character = 0;
            break;
        }
        case Movement_Type::BLOCK_END: 
        {
            int line_index = pos.line;
            auto line = source_code_get_line(code, line_index);
            int start_indent = line->indentation;

            // Go up lines while indent is smaller
            // Skip current block of lines
            while (line_index < code->line_count) {
                auto line = source_code_get_line(code, line_index);
                if (line->indentation < start_indent) {
                    line_index = line_index - 1; // e.g. the previous line was the last of the block
                    break;
                }
                line_index += 1;
            }
            line_index = math_minimum(code->line_count - 1, line_index);

            pos.line = line_index;
            line = source_code_get_line(code, line_index);
            pos.character = line->text.size;
            break;
        }
        case Movement_Type::SEARCH_FORWARDS_FOR:
        case Movement_Type::SEARCH_FORWARDS_TO:
        case Movement_Type::SEARCH_BACKWARDS_FOR: 
        case Movement_Type::SEARCH_BACKWARDS_TO: 
        {
            editor.last_search_was_forward = movement.type == Movement_Type::SEARCH_FORWARDS_FOR || movement.type == Movement_Type::SEARCH_FORWARDS_TO;
            editor.last_search_was_to = movement.type == Movement_Type::SEARCH_BACKWARDS_TO || movement.type == Movement_Type::SEARCH_FORWARDS_TO;
            editor.last_search_char = movement.search_char;
            do_char_search(editor.last_search_char, editor.last_search_was_forward, editor.last_search_was_to);
            break;
        }
        case Movement_Type::REPEAT_LAST_SEARCH: {
            do_char_search(editor.last_search_char, editor.last_search_was_forward, editor.last_search_was_to);
            break;
        }
        case Movement_Type::REPEAT_LAST_SEARCH_REVERSE_DIRECTION: {
            do_char_search(editor.last_search_char, !editor.last_search_was_forward, editor.last_search_was_to);
            break;
        }
        default: panic("");
        }

        pos = sanitize_index(pos);
        Source_Line* line = source_code_get_line(code, pos.line);
        if (set_horizontal_pos) {
            syntax_editor.last_line_x_pos = pos.character + 4 * line->indentation; 
        }
    }

    return pos;
}

Text_Range motion_evaluate(const Motion& motion, Text_Index pos)
{
    auto code = syntax_editor.code;
    Text_Range result;
    switch (motion.motion_type)
    {
    case Motion_Type::MOVEMENT: 
    {
        // Note: Repeat count is stored in movement, and motion should always have 1 as repeat count here
        assert(motion.repeat_count == 1, "");
        auto& mov = motion.movement;

        Text_Index end_pos = movement_evaluate(motion.movement, pos);
        if (text_index_in_order(pos, end_pos)) {
            result = text_range_make(pos, end_pos);
        }
        else {
            Motions::move(pos, 1);
            result = text_range_make(end_pos, pos);
        }

        // Check for special movements
        bool add_one_char = false;
        auto mov_type = motion.movement.type;
        if (mov_type == Movement_Type::SEARCH_FORWARDS_FOR || mov_type == Movement_Type::SEARCH_FORWARDS_TO) {
            add_one_char = true;
        }
        else if (mov_type == Movement_Type::REPEAT_LAST_SEARCH) {
            add_one_char = syntax_editor.last_search_was_forward;
        }
        else if (mov_type == Movement_Type::REPEAT_LAST_SEARCH_REVERSE_DIRECTION) {
            add_one_char = !syntax_editor.last_search_was_forward;
        }

        if (add_one_char) {
            Motions::move(result.end, 1);
        }

        break;
    }
    case Motion_Type::WORD: {
        result = Motions::text_range_get_word(pos);

        if (motion.contains_edges && !text_index_equal(result.start, result.end)) {
            if (char_is_whitespace(Motions::get_char(result.start, -1))) {
                Motions::move(result.start, -1);
                Motions::skip_in_set(result.start, char_is_whitespace, nullptr, false, false);
            }
            if (char_is_whitespace(Motions::get_char(result.end))) {
                Motions::skip_in_set(result.end, char_is_whitespace);
            }
        }
        break;
    }
    case Motion_Type::SPACES: 
    {
        if (char_is_whitespace(Motions::get_char(pos))) {
            result = Motions::text_range_get_island(pos, char_is_whitespace);
        }
        else {
            result = Motions::text_range_get_island(pos, char_is_whitespace, nullptr, true);
        }

        if (motion.contains_edges && !text_index_equal(result.start, result.end)) {
            if (char_is_whitespace(Motions::get_char(result.start, -1))) {
                Motions::move(result.start, -1);
                Motions::skip_in_set(result.start, char_is_whitespace, nullptr, false, false);
            }
            if (char_is_whitespace(Motions::get_char(result.end))) {
                Motions::skip_in_set(result.end, char_is_whitespace);
            }
        }
        break;
    }
    case Motion_Type::BRACES: 
    case Motion_Type::BRACKETS: 
    case Motion_Type::PARENTHESES: 
    case Motion_Type::QUOTATION_MARKS: 
    {
        char start = '\0';
        char end = '\0';
        switch (motion.motion_type) {
        case Motion_Type::PARENTHESES:     start = '('; end = ')'; break;
        case Motion_Type::BRACES:          start = '{'; end = '}'; break;
        case Motion_Type::BRACKETS:        start = '['; end = ']'; break;
        case Motion_Type::QUOTATION_MARKS: start = '\"'; end = '\"'; break;
        }

        for (int i = 0; i < motion.repeat_count; i++) 
        {
            result = Motions::text_range_get_parenthesis(pos, start, end);
            pos = result.start;
            if (pos.character == 0) {
                if (pos.line == 0) {
                    break;
                }
                pos.line -= 1;
            }
            else {
                pos.character -= 1;
            }
        }

        if (!text_index_equal(result.start, result.end) && !motion.contains_edges) {
            Motions::move_forwards_over_line(result.start);
            Motions::move_backwards_over_line(result.end);
        }

        break;
    }
    case Motion_Type::BLOCK: 
    {
        int line_start = pos.line;
        int start_indentation = source_code_get_line(code, line_start)->indentation - (motion.repeat_count - 1);
        while (line_start > 0) {
            auto line = source_code_get_line(code, line_start);
            if (line->indentation < start_indentation) {
                line_start = line_start + 1;
                break;
            }
            line_start -= 1;
        }
        line_start = math_maximum(0, line_start);

        int line_end = pos.line;
        while (line_end < code->line_count) {
            auto line = source_code_get_line(code, line_end);
            if (line->indentation < start_indentation) {
                line_end = line_end - 1;
                break;
            }
            line_end += 1;
        }
        line_end = math_minimum(code->line_count - 1, line_end);

        result.start = text_index_make(line_start, 0);
        result.end = text_index_make_line_end(code, line_end);
        if (motion.contains_edges) {
            if (result.start.line > 0) {
                result.start = text_index_make_line_end(code, result.start.line - 1);
            }
            if (result.end.line + 1 < code->line_count) {
                result.end = text_index_make(result.end.line + 1, 0);
            }
        }

        break;
    }
    case Motion_Type::LINE: 
    {
        // Note: The repeat count for line gives the final line offset
        if (motion.repeat_count >= 0) {
            result.start = text_index_make(pos.line, 0);
            result.end = text_index_make_line_end(code, pos.line + motion.repeat_count);
        }
        else {
            result.start = text_index_make(pos.line + motion.repeat_count, 0);
            result.end = text_index_make_line_end(code, pos.line);
        }
        result.start = sanitize_index(result.start);
        result.end = sanitize_index(result.end);

        if (motion.contains_edges) {
            Motions::move_forwards_over_line(result.end);
            // if (motion.repeat_count > 0) {
            // }
            // else {
            //     Motions::move_backwards_over_line(result.start);
            // }
        }
        break;
    }
    default:
        panic("Invalid motion type");
        result = text_range_make(pos, pos);
        break;
    }

    return result;
}

void text_range_delete(Text_Range range)
{
    auto code = syntax_editor.code;

    history_start_complex_command(&syntax_editor.history);
    SCOPE_EXIT(history_stop_complex_command(&syntax_editor.history));

    // Handle single line case
    if (range.start.line == range.end.line) {
        history_delete_text(&syntax_editor.history, range.start, range.end.character);
        return;
    }

    // Delete text in first line
    auto line = Motions::get_line(range.start);
    auto end_line = Motions::get_line(range.end);
    if (end_line == nullptr || line == nullptr) return;
    history_delete_text(&syntax_editor.history, range.start, line->text.size); 

    // Append remaining text of last-line into first line
    String remainder = string_create_substring_static(&end_line->text, range.end.character, end_line->text.size);
    history_insert_text(&syntax_editor.history, range.start, remainder);

    // Delete all lines inbetween
    for (int i = range.start.line + 1; range.start.line + 1 < code->line_count && i <= range.end.line; i++) {
        history_remove_line(&syntax_editor.history, range.start.line + 1);
    }
}

void text_range_append_to_string(Text_Range range, String* str)
{
    auto code = syntax_editor.code;

    // Handle single line case first
    if (range.start.line == range.end.line) {
        auto line = source_code_get_line(code, range.start.line);
        for (int j = 0; j < line->indentation; j++) {
            string_append_character(str, '\t');
        }
        String substring = string_create_substring_static(&line->text, range.start.character, range.end.character);
        string_append_string(str, &substring);
        return;
    }

    // Append first line
    auto start_line = source_code_get_line(code, range.start.line);
    for (int j = 0; j < start_line->indentation; j++) {
        string_append_character(str, '\t');
    }
    String substring = string_create_substring_static(&start_line->text, range.start.character, start_line->text.size);
    string_append_string(str, &substring);

    // Append lines until end
    for (int i = range.start.line + 1; i <= range.end.line && i < code->line_count; i++) 
    {
        auto line = source_code_get_line(code, i);
        string_append_character(str, '\n');
        // Append indentation
        for (int j = 0; j < line->indentation; j++) {
            string_append_character(str, '\t');
        }

        // Append line content
        if (i == range.end.line) {
            String substring = string_create_substring_static(&line->text, 0, range.end.character);
            string_append_string(str, &substring);
        }
        else {
            string_append_string(str, &line->text);
        }
    }
}

struct Yank_Line
{
    String text;
    int indentation;
};

void insert_command_execute(Insert_Command input);

void syntax_editor_insert_yank(bool before_cursor)
{
    auto& editor = syntax_editor;
    auto code = editor.code;
    auto& cursor = editor.cursor;

    history_start_complex_command(&editor.history);
    SCOPE_EXIT(history_stop_complex_command(&editor.history));

    // Parse yanked text into individual lines
    Dynamic_Array<Yank_Line> yank_lines = dynamic_array_create<Yank_Line>();
    SCOPE_EXIT(dynamic_array_destroy(&yank_lines));
    {
        int index = 0;
        int last_line_start = 0;
        int last_indentation = 0;
        bool tabs_valid = true;
        while (index < editor.yank_string.size)
        {
            SCOPE_EXIT(index += 1);

            char c = editor.yank_string.characters[index];
            if (c == '\n')
            {
                Yank_Line yank;
                yank.text = string_create_substring_static(&editor.yank_string, last_line_start, index);
                yank.indentation = last_indentation;
                dynamic_array_push_back(&yank_lines, yank);

                tabs_valid = true;
                last_indentation = 0;
                last_line_start = index + 1;
            }
            else if (c == '\t') {
                if (tabs_valid) {
                    last_indentation += 1;
                    last_line_start = index + 1;
                }
            }
            else {
                tabs_valid = false;
            }
        }

        // Insert last yank line
        Yank_Line yank;
        yank.text = string_create_substring_static(&editor.yank_string, last_line_start, editor.yank_string.size);
        yank.indentation = last_indentation;
        dynamic_array_push_back(&yank_lines, yank);
    }

    // Insert text
    if (editor.yank_was_line)
    {
        int line_insert_index = cursor.line + (before_cursor ? 0 : 1);
        for (int i = 0; i < yank_lines.size; i++) {
            auto& yank_line = yank_lines[i];
            history_insert_line_with_text(&editor.history, line_insert_index + i, yank_line.indentation, yank_line.text);
        }
        cursor = text_index_make(line_insert_index + yank_lines.size - 1, 0);
    }
    else
    {
        // Insert first line
        const Yank_Line& first_line = yank_lines[0];
        auto& pos = cursor;
        pos.character += before_cursor ? 0 : 1;
        pos = sanitize_index(pos);
        history_insert_text(&editor.history, pos, first_line.text);

        if (yank_lines.size > 1)
        {
            // Insert the rest of the lines
            for (int i = 1; i < yank_lines.size; i++) {
                const Yank_Line& yank_line = yank_lines[i];
                history_insert_line_with_text(&editor.history, pos.line + i, yank_line.indentation, yank_line.text);
            }

            // Append first line cutoff to last line, e.g. asdf|what and then put needs to add what to the end
            Source_Line* first = source_code_get_line(editor.code, pos.line);
            int cutoff_start = pos.character + first_line.text.size;
            String substring = string_create_substring_static(&first->text, cutoff_start, first->text.size);
            history_insert_text(&editor.history, text_index_make_line_end(editor.code, pos.line + yank_lines.size - 1), substring);
            history_delete_text(&editor.history, text_index_make(pos.line, cutoff_start), first->text.size);
        }
    }
}

void normal_command_execute(Normal_Mode_Command& command)
{
    auto& editor = syntax_editor;
    auto& cursor = editor.cursor;
    auto line = source_code_get_line(editor.code, cursor.line);
    auto& tokens = line->tokens;

    // Save cursor pos
    history_set_cursor_pos(&editor.history, editor.cursor);

    // Record command for repeat
    if (command.type == Normal_Mode_Command_Type::ENTER_INSERT_MODE_AFTER_MOVEMENT ||
        command.type == Normal_Mode_Command_Type::ENTER_INSERT_MODE_NEW_LINE_ABOVE ||
        command.type == Normal_Mode_Command_Type::ENTER_INSERT_MODE_NEW_LINE_BELOW ||
        command.type == Normal_Mode_Command_Type::DELETE_MOTION ||
        command.type == Normal_Mode_Command_Type::CHANGE_MOTION ||
        command.type == Normal_Mode_Command_Type::PUT_AFTER_CURSOR ||
        command.type == Normal_Mode_Command_Type::PUT_BEFORE_CURSOR)
    {
        editor.last_normal_command = command;
    }

    // Start complex command for non-history commands
    bool execute_as_complex = command.type != Normal_Mode_Command_Type::UNDO && command.type != Normal_Mode_Command_Type::REDO;
    if (execute_as_complex) {
        history_start_complex_command(&editor.history);
    }

    SCOPE_EXIT(
        syntax_editor_sanitize_cursor();
        history_set_cursor_pos(&editor.history, editor.cursor);
        auto_format_line(cursor.line);
        if (execute_as_complex) {
            history_stop_complex_command(&editor.history);
        }
    );

    switch (command.type)
    {
    case Normal_Mode_Command_Type::MOVEMENT: {
        cursor = movement_evaluate(command.options.movement, cursor);
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Mode_Command_Type::ENTER_INSERT_MODE_AFTER_MOVEMENT: {
        editor_enter_insert_mode();
        cursor = movement_evaluate(command.options.movement, cursor);
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Mode_Command_Type::ENTER_INSERT_MODE_NEW_LINE_BELOW:
    case Normal_Mode_Command_Type::ENTER_INSERT_MODE_NEW_LINE_ABOVE: {
        bool below = command.type == Normal_Mode_Command_Type::ENTER_INSERT_MODE_NEW_LINE_BELOW;
        int new_line_index = cursor.line + (below ? 1 : 0);
        history_insert_line(&editor.history, new_line_index, line->indentation);
        cursor = text_index_make(new_line_index, 0);
        editor_enter_insert_mode();
        break;
    }
    case Normal_Mode_Command_Type::YANK_MOTION:
    case Normal_Mode_Command_Type::DELETE_MOTION:
    {
        // First yank deleted text
        const auto& motion = command.options.motion;
        if (motion.motion_type == Motion_Type::LINE)
        {
            editor.yank_was_line = true;
            int start_line = cursor.line;
            int end_line = cursor.line;
            if (motion.repeat_count < 0) {
                start_line += motion.repeat_count;
                start_line = math_maximum(start_line, 0);
            }
            else {
                end_line += motion.repeat_count;
                start_line = math_minimum(editor.code->line_count - 1, start_line);
            }

            string_reset(&editor.yank_string);
            for (int i = start_line; i <= end_line; i += 1) {
                auto line = source_code_get_line(editor.code, i);
                for (int j = 0; j < line->indentation; j++) {
                    string_append_character(&editor.yank_string, '\t');
                }
                string_append_string(&editor.yank_string, &line->text);
                if (i != end_line) {
                    string_append_character(&editor.yank_string, '\n');
                }
            }
        }
        else {
            editor.yank_was_line = false;
            auto range = motion_evaluate(command.options.motion, cursor);
            string_reset(&editor.yank_string);
            text_range_append_to_string(range, &editor.yank_string);
        }
        printf("Yanked: was_line = %s ----\n%s\n----\n", (editor.yank_was_line ? "true" : "false"), editor.yank_string.characters);

        // Delete if necessary
        if (command.type == Normal_Mode_Command_Type::DELETE_MOTION) {
            auto range = motion_evaluate(command.options.motion, cursor);
            text_range_delete(range);
            cursor = range.start;
        }
        break;
    }
    case Normal_Mode_Command_Type::CHANGE_MOTION: {
        auto range = motion_evaluate(command.options.motion, cursor);
        cursor = range.start;
        editor_enter_insert_mode();
        text_range_delete(range);
        break;
    }
    case Normal_Mode_Command_Type::PUT_AFTER_CURSOR:
    case Normal_Mode_Command_Type::PUT_BEFORE_CURSOR: {
        syntax_editor_insert_yank((command.type == Normal_Mode_Command_Type::PUT_BEFORE_CURSOR));
        break;
    }
    case Normal_Mode_Command_Type::REPLACE_CHAR: {
        auto curr_char = Motions::get_char(cursor);
        if (curr_char == '\0' || curr_char == command.options.character) break;
        history_delete_char(&editor.history, cursor);
        history_insert_char(&editor.history, cursor, command.options.character);
        break;
    }
    case Normal_Mode_Command_Type::REPLACE_MOTION_WITH_YANK: {
        auto range = motion_evaluate(command.options.motion, cursor);
        cursor = range.start;
        if (text_index_equal(range.start, range.end)) {
            break;
        }
        text_range_delete(range);
        syntax_editor_insert_yank(true);
        break;
    }
    case Normal_Mode_Command_Type::UNDO: {
        history_undo(&editor.history);
        auto cursor_history = history_get_cursor_pos(&editor.history);
        if (cursor_history.available) {
            cursor = cursor_history.value;
        }
        break;
    }
    case Normal_Mode_Command_Type::REDO: {
        history_redo(&editor.history);
        auto cursor_history = history_get_cursor_pos(&editor.history);
        if (cursor_history.available) {
            cursor = cursor_history.value;
        }
        break;
    }

    case Normal_Mode_Command_Type::SCROLL_DOWNWARDS_HALF_PAGE:
    case Normal_Mode_Command_Type::SCROLL_UPWARDS_HALF_PAGE: {
        int dir = command.type == Normal_Mode_Command_Type::SCROLL_DOWNWARDS_HALF_PAGE ? 1 : -1;
        editor.camera_start_line += (editor.camera_start_line - editor.last_visible_line) / 2 * dir;
        break;
    }
    case Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_TOP: {
        editor.camera_start_line = editor.cursor.line - MIN_CURSOR_DISTANCE;
        break;
    }
    case Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_CENTER: {
        editor.camera_start_line = editor.cursor.line - (editor.last_visible_line - editor.camera_start_line) / 2;
        break;
    }
    case Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_BOTTOM: {
        editor.camera_start_line = editor.cursor.line - (editor.last_visible_line - editor.camera_start_line) + MIN_CURSOR_DISTANCE;
        break;
    }
    case Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_TOP: {
        cursor.line = editor.camera_start_line + MIN_CURSOR_DISTANCE;
        cursor.character = 0;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_CENTER: {
        cursor.line = editor.camera_start_line + (editor.last_visible_line - editor.camera_start_line) / 2;
        cursor.character = 0;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_BOTTOM: {
        cursor.line = editor.last_visible_line - MIN_CURSOR_DISTANCE;
        cursor.character = 0;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Mode_Command_Type::REPEAT_LAST_COMMAND:
    {
        editor.record_insert_commands = false;
        normal_command_execute(editor.last_normal_command);
        if (editor.mode == Editor_Mode::INSERT) {
            for (int i = 0; i < editor.last_insert_commands.size; i++) {
                insert_command_execute(editor.last_insert_commands[i]);
            }
        }
        assert(editor.mode == Editor_Mode::NORMAL, "");
        editor.record_insert_commands = true;
        break;
    }
    case Normal_Mode_Command_Type::VISUALIZE_MOTION:
    case Normal_Mode_Command_Type::GOTO_LAST_JUMP:
    case Normal_Mode_Command_Type::GOTO_NEXT_JUMP:
        break;
    default: panic("");
    }
}

void insert_command_execute(Insert_Command input)
{
    auto& editor = syntax_editor;
    auto& mode = editor.mode;
    auto& cursor = editor.cursor;
    auto line = source_code_get_line(editor.code, cursor.line);
    auto& text = line->text;
    auto& pos = cursor.character;

    history_start_complex_command(&editor.history);
    SCOPE_EXIT(history_stop_complex_command(&editor.history));

    assert(mode == Editor_Mode::INSERT, "");
    syntax_editor_sanitize_cursor();
    SCOPE_EXIT(syntax_editor_sanitize_cursor());
    SCOPE_EXIT(code_completion_find_suggestions());

    if (editor.record_insert_commands) {
        dynamic_array_push_back(&editor.last_insert_commands, input);
    }

    // Experimental: Automatically complete identifier if we are about to enter some seperation afterwards
    if (false)
    {
        bool complete_current = false;
        switch (input.type)
        {
        case Insert_Command_Type::ADD_INDENTATION:
        case Insert_Command_Type::ENTER:
        case Insert_Command_Type::ENTER_REMOVE_ONE_INDENT:
        case Insert_Command_Type::SPACE:
        {
            syntax_editor_synchronize_tokens();
            auto after_token = get_cursor_token(true);
            complete_current = line->tokens.size != 1;
            if (!complete_current) {
                complete_current =
                    !(after_token.type == Token_Type::OPERATOR &&
                        (after_token.options.op == Operator::DEFINE_COMPTIME ||
                            after_token.options.op == Operator::DEFINE_INFER ||
                            after_token.options.op == Operator::COLON));
            }
            break;
        }
        case Insert_Command_Type::DELIMITER_LETTER:
            complete_current = input.letter != ':';
            break;
        }

        if (complete_current) {
            code_completion_insert_suggestion();
        }
    }

    // Handle Universal Inputs
    switch (input.type)
    {
    case Insert_Command_Type::INSERT_CODE_COMPLETION: {
        if (editor.record_insert_commands) {
            code_completion_insert_suggestion();
        }
        else {
            history_insert_text(&editor.history, cursor, editor.last_recorded_code_completion);
            auto_format_line(cursor.line);
        }
        break;
    }
    case Insert_Command_Type::EXIT_INSERT_MODE: {
        //code_completion_insert_suggestion();
        editor_leave_insert_mode();
        break;
    }
    case Insert_Command_Type::ENTER: {
        editor_split_line_at_cursor(0);
        break;
    }
    case Insert_Command_Type::ENTER_REMOVE_ONE_INDENT: {
        editor_split_line_at_cursor(-1);
        break;
    }
    case Insert_Command_Type::ADD_INDENTATION: {
        if (pos == 0) {
            history_change_indent(&syntax_editor.history, cursor.line, line->indentation + 1);
            break;
        }
        editor_split_line_at_cursor(1);
        break;
    }
    case Insert_Command_Type::REMOVE_INDENTATION: {
        if (line->indentation > 0) {
            history_change_indent(&syntax_editor.history, cursor.line, line->indentation - 1);
        }
        break;
    }
    case Insert_Command_Type::MOVE_LEFT: {
        pos = math_maximum(0, pos - 1);
        break;
    }
    case Insert_Command_Type::MOVE_RIGHT: {
        pos = math_minimum(line->text.size, pos + 1);
        break;
    }
    case Insert_Command_Type::DELETE_LAST_WORD: {
        Movement movement = Parsing::movement_make(Movement_Type::PREVIOUS_WORD, 1);
        Text_Index start = movement_evaluate(movement, cursor);
        if (!text_index_equal(start, cursor)) {
            Text_Index end = cursor;
            cursor = start;
            text_range_delete(text_range_make(start, end));
        }
        break;
    }
    case Insert_Command_Type::DELETE_TO_LINE_START: {
        if (cursor.character == 0) break;
        auto to = cursor;
        cursor.character = 0;
        text_range_delete(text_range_make(cursor, to));
        break;
    }

    // Letters
    case Insert_Command_Type::DELIMITER_LETTER:
    {
        bool insert_double_after = false;
        bool skip_auto_input = false;
        char double_char = ' ';
        if (char_is_parenthesis(input.letter))
        {
            Parenthesis p = char_to_parenthesis(input.letter);
            // Check if line_index is properly parenthesised with regards to the one token I currently am on
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
        auto_format_line(cursor.line);
        break;
    }
    case Insert_Command_Type::SPACE:
    {
        // Handle strings and comments, where we always just add a space
        if (line->is_comment) {
            history_insert_char(&syntax_editor.history, cursor, ' ');
            pos += 1;
            break;
        }
        if (pos == 0) break;
        syntax_editor_synchronize_tokens();
        auto token = get_cursor_token(false);
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

        char prev = text[pos - 1];
        if ((char_is_space_critical(prev)) || (pos == text.size && prev != ' ')) {
            history_insert_char(&syntax_editor.history, cursor, ' ');
            pos += 1;
            auto_format_line(cursor.line);
        }

        break;
    }
    case Insert_Command_Type::BACKSPACE:
    {
        if (pos == 0)
        {
            if (cursor.line == 0) {
                // We are at the first line_index in the code
                break;
            }
            auto prev_line = source_code_get_line(editor.code, cursor.line - 1);
            // Merge this line_index with previous one
            Text_Index insert_index = text_index_make(cursor.line - 1, prev_line->text.size);
            history_insert_text(&syntax_editor.history, insert_index, text);
            history_remove_line(&syntax_editor.history, cursor.line);
            cursor = insert_index;
            auto_format_line(cursor.line);
            break;
        }

        history_delete_char(&syntax_editor.history, text_index_make(cursor.line, pos - 1));
        pos -= 1;
        auto_format_line(cursor.line);
        break;
    }
    case Insert_Command_Type::NUMBER_LETTER:
    case Insert_Command_Type::IDENTIFIER_LETTER:
    {
        history_insert_char(&syntax_editor.history, cursor, input.letter);
        pos += 1;
        auto_format_line(cursor.line);
        break;
    }
    default: panic("");
    }
}

void syntax_editor_process_key_message(Key_Message& msg)
{
    auto& mode = syntax_editor.mode;
    auto& editor = syntax_editor;

    using Parsing::Parse_Result;
    using Parsing::Parse_Result_Type;
    using Parsing::parse_insert_command;
    using Parsing::parse_normal_command;
    using Parsing::parse_repeat_count;

    if (mode == Editor_Mode::INSERT)
    {
        Parse_Result<Insert_Command> result = parse_insert_command(msg);
        if (result.type == Parse_Result_Type::SUCCESS) {
            insert_command_execute(result.result);
        }
    }
    else
    {
        auto& cmd_buffer = editor.command_buffer;

        // Filter out special/unnecessary messages
        {
            if (msg.key_code == Key_Code::L && msg.ctrl_down && msg.key_down) {
                string_reset(&editor.command_buffer);
                logg("Command canceled: \"%s\"!\n", editor.command_buffer.characters);
                return;
            }
            // Filter out messages (Key Up messages + random shift or alt or ctrl clicks)
            if ((msg.character == 0 && !(msg.ctrl_down && msg.key_down)) || !msg.key_down || msg.key_code == Key_Code::ALT) {
                //logg("message filtered\n");
                return;
            }
        }

        // Parse CTRL messages first
        if (msg.ctrl_down)
        {
            Normal_Mode_Command_Type command_type = Normal_Mode_Command_Type::MAX_ENUM_VALUE;
            switch (msg.key_code)
            {
            case Key_Code::R: command_type = Normal_Mode_Command_Type::REDO; break;
            case Key_Code::U: command_type = Normal_Mode_Command_Type::SCROLL_UPWARDS_HALF_PAGE; break;
            case Key_Code::D: command_type = Normal_Mode_Command_Type::SCROLL_DOWNWARDS_HALF_PAGE; break;
            case Key_Code::O: command_type = Normal_Mode_Command_Type::GOTO_LAST_JUMP; break;
            case Key_Code::I: command_type = Normal_Mode_Command_Type::GOTO_NEXT_JUMP; break;
            }

            if (command_type != Normal_Mode_Command_Type::MAX_ENUM_VALUE)
            {
                int index = 0;
                int repeat_count = parse_repeat_count(index, 1);
                Normal_Mode_Command cmd = Parsing::normal_mode_command_make(command_type, repeat_count);
                normal_command_execute(cmd);
                string_reset(&editor.command_buffer);
                return;
            }
        }

        // Check that character is valid
        if (msg.character < ' ' || msg.character > 128) {
            string_reset(&editor.command_buffer);
            return;
        }

        // Otherwise try to parse command
        int index = 0;
        string_append_character(&cmd_buffer, msg.character);;

        Parse_Result<Normal_Mode_Command> result = parse_normal_command(index);
        if (result.type == Parse_Result_Type::SUCCESS) {
            normal_command_execute(result.result);
            string_reset(&cmd_buffer);
        }
        else if (result.type == Parse_Result_Type::FAILURE) {
            logg("Command parsing failed: \"%s\"!\n", editor.command_buffer.characters);
            string_reset(&cmd_buffer);
        }
    }
}

void syntax_editor_update()
{
    auto& editor = syntax_editor;
    auto& input = syntax_editor.input;
    auto& mode = syntax_editor.mode;

    SCOPE_EXIT(source_code_sanity_check(editor.code));

    // Handle input replay
    {
        auto& replay = editor.input_replay;
        if (editor.input->key_pressed[(int)Key_Code::F9]) {
            if (replay.currently_recording) {
                logg("Ending recording of inputs, captures: %d messages!\n", replay.recorded_inputs.size);
                replay.currently_recording = false;
                string_reset(&replay.code_state_afterwards);
                source_code_append_to_string(editor.code, &replay.code_state_afterwards);
            }
            else {
                logg("Started recording keyboard inputs!", replay.recorded_inputs.size);
                replay.currently_recording = true;
                replay.cursor_start = editor.cursor;
                replay.start_mode = editor.mode;
                string_reset(&replay.code_state_initial);
                source_code_append_to_string(editor.code, &replay.code_state_initial);
                dynamic_array_reset(&replay.recorded_inputs);
            }
        }
        if (editor.input->key_pressed[(int)Key_Code::F10]) {
            if (replay.currently_recording) {
                logg("Cannot replay recorded inputs, since we are CURRENTLY RECORDING!\n");
            }
            else {
                replay.currently_replaying = true;
                SCOPE_EXIT(replay.currently_replaying = false);
                // Set new state
                syntax_editor_set_text(replay.code_state_initial);
                editor.cursor = replay.cursor_start;
                editor.mode = replay.start_mode;
                for (int i = 0; i < replay.recorded_inputs.size; i++) {
                    syntax_editor_process_key_message(replay.recorded_inputs[i]);
                }
                auto text_afterwards = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&text_afterwards));
                source_code_append_to_string(editor.code, &text_afterwards);
                if (!string_equals(&text_afterwards, &replay.code_state_afterwards)) {
                    logg("Replaying the events did not end in the same output!\n");
                }
                else {
                    logg("Replay successfull, everything happened as expected");
                }
            }
        }
        if (replay.currently_recording) {
            dynamic_array_append_other(&replay.recorded_inputs, &input->key_messages);
        }
    }

    // Check shortcuts pressed
    if (syntax_editor.input->key_pressed[(int)Key_Code::O] && syntax_editor.input->key_down[(int)Key_Code::CTRL]) {
        auto open_file = file_io_open_file_selection_dialog();
        if (open_file.available) {
            syntax_editor_load_text_file(open_file.value.characters);
        }
    }
    else if (syntax_editor.input->key_pressed[(int)Key_Code::S] && syntax_editor.input->key_down[(int)Key_Code::CTRL]) {
        syntax_editor_save_text_file();
    }
    else if (syntax_editor.input->key_pressed[(int)Key_Code::F8]) {
        compiler_run_testcases(compiler.timer, true);
        syntax_editor_load_text_file(editor.file_path);
    }

    // Handle Editor inputs
    for (int i = 0; i < input->key_messages.size; i++) {
        syntax_editor_process_key_message(input->key_messages[i]);
    }

    bool build_and_run = syntax_editor.input->key_pressed[(int)Key_Code::F5];
    syntax_editor_synchronize_with_compiler(build_and_run);
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
            String tmp = string_create();
            SCOPE_EXIT(string_destroy(&tmp));

            for (int i = 0; i < editor.errors.size; i++) {
                auto error = editor.errors[i];
                if (error.semantic_error_index != -1) {
                    continue;
                }
                string_append_formated(&tmp, "\t%s\n", error.message.characters);
            }

            semantic_analyser_append_all_errors_to_string(&tmp, 1);
            logg(tmp.characters);
        }
    }
}



// RENDERING
// Draw Commands

// Returns bottom left vector
vec2 syntax_editor_position_to_pixel(int line_index, int character)
{
    float height = rendering_core.render_information.backbuffer_height;
    return vec2(0.0f, height) + syntax_editor.character_size * vec2(character, -line_index - 1);
}

void syntax_editor_draw_underline(int line_index, int character, int length, vec3 color)
{
    vec2 pos = syntax_editor_position_to_pixel(line_index, character);
    vec2 size = vec2((float)length, 1.0f / 8.f) * syntax_editor.character_size;
    renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_anchor(pos, size, Anchor::BOTTOM_LEFT), color);
}

void syntax_editor_draw_cursor_line(int line_index, int character, vec3 color)
{
    vec2 pos = syntax_editor_position_to_pixel(line_index, character);
    vec2 size = vec2(0.1f, 1.0) * syntax_editor.character_size;
    renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_anchor(pos, size, Anchor::BOTTOM_LEFT), color);
}

void syntax_editor_draw_text_background(int line_index, int character, int length, vec3 color)
{
    vec2 pos = syntax_editor_position_to_pixel(line_index, character);
    vec2 size = vec2(length, 1) * syntax_editor.character_size;
    renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_anchor(pos, size, Anchor::BOTTOM_LEFT), color);
}

void syntax_editor_draw_string(String string, vec3 color, int line_index, int character)
{
    vec2 pos = syntax_editor_position_to_pixel(line_index, character);
    text_renderer_add_text(syntax_editor.text_renderer, string, pos, Anchor::BOTTOM_LEFT, syntax_editor.character_size.y, color);
}

// Note: Always takes Anchor::TOP_LEFT, because string may grow downwards...
void syntax_editor_draw_string_in_box(String string, vec3 text_color, vec3 box_color, vec2 position, float text_height)
{
    vec2 char_size = vec2(text_renderer_character_width(syntax_editor.text_renderer, text_height), text_height);

    int line_pos = 0;
    int max_line_pos = 0;
    int current_char_pos = 0;
    int last_draw_character_index = 0;
    int next_draw_char_pos = 0;
    for (int i = 0; i <= string.size; i++) {
        char c = i == string.size ? 0 : string[i];
        bool draw_until_this_character = i == string.size;
        const int draw_line_index = line_pos;

        if (c == '\n') {
            draw_until_this_character = true;
            line_pos += 1;
            current_char_pos = 0;
        }
        else if (c == '\t') {
            draw_until_this_character = true;
            current_char_pos += 4;
        }
        else if (c < 32) {
            // Ignore other ascii control sequences
        }
        else {
            // Normal character
            current_char_pos += 1;
        }
        max_line_pos = math_maximum(max_line_pos, current_char_pos);

        if (draw_until_this_character) {
            String line = string_create_substring_static(&string, last_draw_character_index, i);
            last_draw_character_index = i + 1;
            vec2 text_line_pos = position + char_size * vec2(next_draw_char_pos, -draw_line_index);
            text_renderer_add_text(syntax_editor.text_renderer, line, text_line_pos, Anchor::TOP_LEFT, text_height, text_color);
            next_draw_char_pos = current_char_pos;
        }
    }

    // Draw surrounding retangle
    vec2 text_size = char_size * vec2(max_line_pos, line_pos + 1);
    renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_anchor(position, text_size, Anchor::TOP_LEFT), box_color);
}

void syntax_editor_draw_block_outline(int line_start, int line_end, int indentation)
{
    if (indentation == 0) return;
    auto offset = syntax_editor.character_size * vec2(0.5f, 1.0f);
    vec2 start = syntax_editor_position_to_pixel(line_start, (indentation - 1) * 4) + offset;
    vec2 end = syntax_editor_position_to_pixel(line_end, (indentation - 1) * 4) + offset;
    start.y -= syntax_editor.character_size.y * 0.1;
    end.y += syntax_editor.character_size.y * 0.1;
    renderer_2D_add_line(syntax_editor.renderer_2D, start, end, vec3(0.4f), 3);
    vec2 l_end = end + vec2(syntax_editor.character_size.x * 0.5f, 0.0f);
    renderer_2D_add_line(syntax_editor.renderer_2D, end, l_end, vec3(0.4f), 3);
}



// Syntax Highlighting
void syntax_highlighting_mark_range(Token_Range range, vec3 normal_color, vec3 empty_range_color, bool underline)
{
    auto index = range.start;
    auto end = range.end;

    typedef void (*draw_fn)(int line_index, int character, int length, vec3 color);
    draw_fn draw_mark = underline ? syntax_editor_draw_underline : syntax_editor_draw_text_background;

    // Handle empty ranges
    assert(token_index_compare(index, end) >= 0, "hey");
    if (token_index_equal(index, end))
    {
        auto line = source_code_get_line(syntax_editor.code, index.line);
        int render_end_pos = line->indentation * 4;
        if (line->tokens.size > 0) {
            auto& last = line->tokens[line->tokens.size - 1];
            render_end_pos = line->indentation * 4 + last.end_index;
        }

        if (index.token >= line->tokens.size) {
            draw_mark(line->screen_index, render_end_pos, 1, empty_range_color);
        }
        else {
            auto& info = line->tokens[index.token];
            int start = line->indentation * 4 + info.start_index;
            draw_mark(line->screen_index, start, info.end_index - info.start_index, normal_color);
        }
        return;
    }

    // Otherwise draw mark on all affected lines
    bool quit_loop = false;
    while (true)
    {
        auto line = source_code_get_line(syntax_editor.code, index.line);

        // Figure out start and end of highlight in line
        int highlight_start = line->indentation * 4;
        int highlight_end = line->indentation * 4;
        if (line->tokens.size > 0)
        {
            const auto& last = line->tokens[line->tokens.size - 1];
            highlight_end += last.end_index; // Set end to line end

            if (index.token < line->tokens.size) {
                highlight_start += line->tokens[index.token].start_index;
            }
        }

        if (index.line == end.line) {
            if (end.token > 0 && end.token - 1 < line->infos.size) {
                const auto& end_token = line->tokens[end.token - 1];
                highlight_end = line->indentation * 4 + end_token.end_index;
            }
        }

        // Draw
        if (highlight_start != highlight_end) {
            draw_mark(line->screen_index, highlight_start, highlight_end - highlight_start, normal_color);
        }

        // Break loop or go to next line
        if (index.line == end.line) {
            break;
        }
        index = token_index_make(index.line + 1, 0);
    }
}

void syntax_highlighting_mark_section(AST::Node* base, Parser::Section section, vec3 normal_color, vec3 empty_range_color, bool underline)
{
    assert(base != 0, "");
    auto ranges = syntax_editor.token_range_buffer;
    dynamic_array_reset(&ranges);

    auto& node = base;
    Parser::ast_base_get_section_token_range(syntax_editor.code, node, section, &ranges);
    for (int i = 0; i < ranges.size; i++) {
        syntax_highlighting_mark_range(ranges[i], normal_color, empty_range_color, underline);
    }
}

void syntax_highlighting_set_section_text_color(AST::Node* base, Parser::Section section, vec3 color)
{
    assert(base != 0, "");
    auto ranges = syntax_editor.token_range_buffer;
    dynamic_array_reset(&ranges);

    auto& node = base;
    Parser::ast_base_get_section_token_range(syntax_editor.code, node, section, &ranges);
    for (int r = 0; r < ranges.size; r++)
    {
        Token_Range range = ranges[r];
        for (int line_index = range.start.line; line_index <= range.end.line; line_index += 1)
        {
            Source_Line* line = source_code_get_line(syntax_editor.code, line_index);

            int start_index = line_index == range.start.line ? range.start.token : 0;
            int end_index = line_index == range.end.line ? range.end.token : line->tokens.size;
            for (int token = start_index; token < end_index && token < line->tokens.size; token += 1) {
                line->infos[token].color = color;
            }
        }
    }
}

void syntax_highlighting_highlight_identifiers_recursive(AST::Node* base)
{
    Symbol* symbol = code_query_get_ast_node_symbol(base);
    if (symbol != 0) {
        syntax_highlighting_set_section_text_color(base, Parser::Section::IDENTIFIER, symbol_type_to_color(symbol->type));
    }

    // Highlight dot-calls
    if (base->type == AST::Node_Type::EXPRESSION)
    {
        auto expr = downcast<AST::Expression>(base);
        if (expr->type == AST::Expression_Type::MEMBER_ACCESS)
        {
            auto pass = code_query_get_analysis_pass(base);
            if (pass != 0)
            {
                auto info = pass_get_node_info(pass, expr, Info_Query::TRY_READ);
                if (info != nullptr) {
                    if (info->result_type == Expression_Result_Type::DOT_CALL || info->specifics.member_access.options.dot_call_function != nullptr) {
                        syntax_highlighting_set_section_text_color(base, Parser::Section::END_TOKEN, Syntax_Color::FUNCTION);
                    }
                }
            }
        }
    }

    // Do syntax highlighting for children
    int index = 0;
    auto child = AST::base_get_child(base, index);
    while (child != 0)
    {
        syntax_highlighting_highlight_identifiers_recursive(child);
        index += 1;
        child = AST::base_get_child(base, index);
    }
}



// Code Layout 

// Rendering
int syntax_editor_draw_block_outlines_recursive(int line_index, int indentation)
{
    auto code = syntax_editor.code;
    int block_start = line_index;

    // Find end of block
    int block_end = code->line_count - 1;
    while (line_index < code->line_count)
    {
        if (line_index >= code->line_count) {
            block_end = code->line_count - 1;
            break;
        }

        auto line = source_code_get_line(code, line_index);
        if (line->indentation > indentation) {
            line_index = syntax_editor_draw_block_outlines_recursive(line_index, indentation + 1) + 1;
        }
        else if (line->indentation == indentation) {
            line_index += 1;
        }
        else { // line->indentation < indentation
            block_end = line_index - 1;
            break;
        }
    }

    syntax_editor_draw_block_outline(block_start - syntax_editor.camera_start_line, block_end - syntax_editor.camera_start_line + 1, indentation);
    return block_end;
}

namespace Context
{
    const vec3 COLOR_BG = vec3(0.2f);
    const vec3 COLOR_TEXT = vec3(1.0f);
    const vec3 COLOR_ERROR_TEXT = vec3(1.0f, 0.5f, 0.5f);

    const int BORDER_SIZE = 2;
    const int PADDING = 2;
    const vec3 BORDER_COLOR = vec3(0.5f, 0.0f, 1.0f);

    void reset() 
    {
        auto& lines = syntax_editor.context_lines;
        for (int i = 0; i < lines.size; i++) {
            auto& line = lines[i];
            dynamic_array_destroy(&line.styles);
            string_destroy(&line.text);
        }
        dynamic_array_reset(&lines);
    }

    void add_line(bool keep_style = false, int indentation = 0) 
    {
        auto& lines = syntax_editor.context_lines;

        Context_Line line;
        line.indentation = indentation;
        line.is_seperator = false;
        line.styles = dynamic_array_create<Text_Style>();
        line.text = string_create();

        if (keep_style && lines.size != 0) {
            auto& prev_line = lines[lines.size - 1];
            if (!prev_line.is_seperator && prev_line.styles.size != 0) {
                Text_Style style = prev_line.styles[prev_line.styles.size - 1];
                style.char_start = 0;
                dynamic_array_push_back(&line.styles, style);
            }
        }

        dynamic_array_push_back(&lines, line);
    }

    void add_seperator(bool skip_if_last_was_seperator_or_first = true) 
    {
        auto& lines = syntax_editor.context_lines;

        if (lines.size == 0 && skip_if_last_was_seperator_or_first) return;
        auto& current = lines[lines.size - 1];
        if (current.is_seperator && skip_if_last_was_seperator_or_first) return;

        Context_Line line;
        line.is_seperator = true;
        line.indentation = 0;
        line.styles = dynamic_array_create<Text_Style>();
        line.text = string_create();
        dynamic_array_push_back(&syntax_editor.context_lines, line);
    }

    void add_text(String string) 
    {
        auto& lines = syntax_editor.context_lines;

        if (lines.size == 0) return;
        auto& line = lines[lines.size - 1];
        if (line.is_seperator) return;
        string_append_string(&line.text, &string);
    }

    void add_text(const char* text) {
        add_text(string_create_static(text));
    }

    Text_Style* get_or_push_current_style() 
    {
        auto& lines = syntax_editor.context_lines;
        if (lines.size == 0) return 0;
        auto& line = lines[lines.size - 1];

        bool add_new_style = true;
        if (line.styles.size != 0 && line.styles[line.styles.size - 1].char_start == line.text.size) {
            add_new_style = false;
        }
        
        if (add_new_style) {
            Text_Style style;
            if (line.styles.size != 0) {
                style = line.styles[line.styles.size - 1];
            }
            else {
                style.has_bg = false;
                style.bg_color = COLOR_BG;
                style.text_color = COLOR_TEXT;
            }
            style.char_start = line.text.size;
            dynamic_array_push_back(&line.styles, style);
        }
        return &line.styles[line.styles.size - 1];
    }

    void set_text_color(vec3 text_color = COLOR_TEXT) {
        get_or_push_current_style()->text_color = text_color;
    }

    void set_bg(vec3 bg_color) {
        auto style = get_or_push_current_style();
        style->has_bg = true;
        style->bg_color = bg_color;
    }

    void stop_bg() {
        get_or_push_current_style()->has_bg = false;
    }

    void add_datatype(Datatype* datatype) 
    {
        auto& lines = syntax_editor.context_lines;

        if (lines.size == 0) return;
        auto& line = lines[lines.size - 1];
        if (line.is_seperator) return;

        auto highlight_start_fn = []() {
            Context::set_bg(vec3(COLOR_BG * 2.0f));
        };
        auto highlight_stop_fn = []() {
            Context::stop_bg();
        };
        auto set_color_fn = [](vec3 color, void* userdata) {
            set_text_color(color);
        };

        Datatype_Format format = datatype_format_make_default();
        format.color_fn = set_color_fn;
        format.highlight_parameter_index = 0;
        format.highlight_start = highlight_start_fn;
        format.highlight_stop = highlight_stop_fn;
        datatype_append_to_string(&line.text, datatype, format);
    }

    void add_error(Error_Display error)
    {
        Context::add_line();
        Context::set_bg(vec3(1.0f, 0.0f, 0.0f));
        Context::set_text_color(vec3(1.0f));
        Context::add_text("Error:");
        Context::set_text_color();
        Context::stop_bg();
        Context::add_text(" ");
        Context::add_text(error.message);

        auto set_color_fn = [](vec3 color, void* userdata) {
            set_text_color(color);
        };
        Datatype_Format format = datatype_format_make_default();
        format.color_fn = set_color_fn;

        // Add error infos
        if (error.semantic_error_index != -1)
        {
            auto& semantic_error = compiler.semantic_analyser->errors[error.semantic_error_index];
            for (int j = 0; j < semantic_error.information.size; j++) 
            {
                auto& error_info = semantic_error.information[j];
                Context::add_line(false, 1);

                auto& lines = syntax_editor.context_lines;
                auto& line = lines[lines.size - 1];

                error_information_append_to_string(error_info, &line.text, format);
            }
        }
    }
}

void syntax_editor_render()
{
    auto& editor = syntax_editor;
    auto& cursor = syntax_editor.cursor;
    auto& code = syntax_editor.code;

    // Prepare Render
    editor.character_size.y = math_floor(convertHeight(0.55f, Unit::CENTIMETER));
    editor.character_size.x = text_renderer_character_width(editor.text_renderer, editor.character_size.y);
    syntax_editor_sanitize_cursor();

    auto state_2D = pipeline_state_make_default();
    state_2D.blending_state.blending_enabled = true;
    state_2D.blending_state.source = Blend_Operand::SOURCE_ALPHA;
    state_2D.blending_state.destination = Blend_Operand::ONE_MINUS_SOURCE_ALPHA;
    state_2D.blending_state.equation = Blend_Equation::ADDITION;
    state_2D.depth_state.test_type = Depth_Test_Type::IGNORE_DEPTH;
    auto pass_context = rendering_core_query_renderpass("Context pass", state_2D, 0);
    auto pass_2D = rendering_core_query_renderpass("2D state", state_2D, 0);
    render_pass_add_dependency(pass_2D, rendering_core.predefined.main_pass);
    render_pass_add_dependency(pass_context, pass_2D);

    // Calculate camera start
    {
        int line_count = rendering_core.render_information.backbuffer_height / editor.character_size.y;
        auto& cam_start = editor.camera_start_line;
        auto& cam_end = editor.last_visible_line;
        cam_end = cam_start + line_count;

        auto cursor_line = cursor.line;
        if (cursor_line - MIN_CURSOR_DISTANCE < cam_start) {
            cam_start = cursor_line - MIN_CURSOR_DISTANCE;
        }
        else if (cursor_line + MIN_CURSOR_DISTANCE > cam_end) {
            cam_start = cursor_line - line_count + MIN_CURSOR_DISTANCE;
        }

        editor.camera_start_line = math_clamp(editor.camera_start_line, 0, editor.code->line_count - 1);
        cam_end = cam_start + line_count;
    }

    // Layout Source Code (Set line screen-indices + token colors)
    for (int i = 0; i < code->line_count; i++)
    {
        Source_Line* line = source_code_get_line(code, i);
        line->screen_index = i - editor.camera_start_line;

        dynamic_array_reset(&line->infos);
        for (int j = 0; j < line->tokens.size; j++)
        {
            auto& token = line->tokens[j];

            Render_Info info;
            info.color = Syntax_Color::TEXT;
            info.bg_color = vec3(0.0f);
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

            dynamic_array_push_back(&line->infos, info);
        }
    }

    // Find objects under cursor
    Token_Index cursor_token_index = token_index_make(syntax_editor.cursor.line, get_cursor_token_index(true));
    AST::Node* node = Parser::find_smallest_enclosing_node(&compiler.main_source->parsed_code->root->base, cursor_token_index);
    Analysis_Pass* pass = code_query_get_analysis_pass(node); // Note: May be null?
    Symbol* symbol = code_query_get_ast_node_symbol(node); // May be null
    // AST::Node* node = nullptr;
    // Analysis_Pass* pass = nullptr;
    // Symbol* symbol = nullptr;

    // Syntax Highlighting
    if (true)
    {
        syntax_highlighting_highlight_identifiers_recursive(&compiler.main_source->parsed_code->root->base);

        // Highlight selected symbol occurances
        if (symbol != 0)
        {
            // Highlight all instances of the symbol
            vec3 color = vec3(1.0f, 1.0f, 0.3f) * 0.3f;
            for (int i = 0; i < symbol->references.size; i++) {
                auto node = &symbol->references[i]->base;
                if (compiler_find_ast_source_code(node) != editor.code)
                    continue;
                syntax_highlighting_mark_section(node, Parser::Section::IDENTIFIER, color, color, false);
            }

            // Highlight Definition
            if (symbol->definition_node != 0 && compiler_find_ast_source_code(symbol->definition_node) == editor.code) {
                syntax_highlighting_mark_section(symbol->definition_node, Parser::Section::IDENTIFIER, color, color, false);
            }
        }

        // Error messages 
        for (int i = 0; i < editor.errors.size; i++)
        {
            auto& error = editor.errors[i];
            if (!error.from_main_source) continue;
            syntax_highlighting_mark_range(error.range, vec3(1.0f, 0.0f, 0.0f), vec3(1.0f, 1.0f, 0.0f), true);
        }
    }

    // Render Block outlines
    syntax_editor_draw_block_outlines_recursive(0, 0);

    // Render Code
    for (int i = 0; i < code->line_count; i++)
    {
        Source_Line* line = source_code_get_line(code, i);
        for (int j = 0; j < line->tokens.size; j++)
        {
            auto& token = line->tokens[j];
            auto& info = line->infos[j];
            auto str = token_get_string(token, line->text);
            syntax_editor_draw_string(str, info.color, line->screen_index, line->indentation * 4 + token.start_index);
        }
    }

    // Draw Cursor
    if (true)
    {
        auto line = source_code_get_line(editor.code, cursor.line);
        auto& text = line->text;
        auto& tokens = line->tokens;
        auto& infos = line->infos;
        auto& pos = cursor.character;
        auto token_index = get_cursor_token_index(true);

        if (editor.mode == Editor_Mode::NORMAL)
        {
            int box_start = line->indentation * 4 + cursor.character;
            int box_end = box_start + 1;
            syntax_editor_draw_text_background(line->screen_index, box_start, box_end - box_start, vec3(0.2f));
            syntax_editor_draw_cursor_line(line->screen_index, box_start, Syntax_Color::COMMENT);
            syntax_editor_draw_cursor_line(line->screen_index, box_end, Syntax_Color::COMMENT);
        }
        else {
            syntax_editor_draw_cursor_line(line->screen_index, line->indentation * 4 + cursor.character, Syntax_Color::COMMENT);
        }
    }

    renderer_2D_draw(editor.renderer_2D, pass_2D);
    text_renderer_draw(editor.text_renderer, pass_2D);


    // Calculate context
    Context::reset();
    {
        // If we are in Insert-Mode, prioritize code-completion
        bool show_normal_mode_context = true;
        if (editor.code_completion_suggestions.size != 0 && editor.mode == Editor_Mode::INSERT)
        {
            Context::add_seperator();
            show_normal_mode_context = false;
            auto& suggestions = syntax_editor.code_completion_suggestions;
            for (int i = 0; i < editor.code_completion_suggestions.size; i++) {
                auto sugg = editor.code_completion_suggestions[i];
                Context::add_line();
                Context::add_text(sugg);
            }
        }

        // Error messages (If cursor is directly on error)
        if (editor.errors.size > 0 && show_normal_mode_context && editor.mode == Editor_Mode::NORMAL) 
        {
            Context::add_seperator();
            for (int i = 0; i < editor.errors.size; i++)
            {
                auto& error = editor.errors[i];
                if (!error.from_main_source) continue;

                auto line = source_code_get_line(editor.code, cursor.line);
                bool inside_error = token_range_contains(error.range, cursor_token_index);
                // Special handling for empty error-ranges
                if (token_index_equal(error.range.start, error.range.end) && token_index_equal(cursor_token_index, error.range.start)) {
                    inside_error = true;
                }
                // Special handling for error on last token
                if (editor.mode == Editor_Mode::NORMAL &&
                    cursor_token_index.token == math_maximum(0, line->tokens.size - 1) &&
                    error.range.start.token == line->tokens.size && cursor_token_index.line == error.range.start.line)
                {
                    inside_error = true;
                }

                if (inside_error) {
                    Context::add_error(error);
                }
            }
        }

        // Symbol Info
        if (show_normal_mode_context && symbol != 0 && pass != 0)
        {
            Context::add_seperator();
            Context::add_line();

            Datatype* type = 0;
            const char* after_text = nullptr;
            switch (symbol->type)
            {
            case Symbol_Type::COMPTIME_VALUE:
                after_text = "Comptime";
                type = symbol->options.constant.type;
                break;
            case Symbol_Type::HARDCODED_FUNCTION:
                break;
            case Symbol_Type::FUNCTION:
                after_text = "Function";
                type = upcast(symbol->options.function->signature);
                break;
            case Symbol_Type::PARAMETER: {
                auto progress = analysis_workload_try_get_function_progress(pass->origin_workload);
                type = progress->function->signature->parameters[symbol->options.parameter.index_in_non_polymorphic_signature].type;
                after_text = "Parameter";
                break;
            }
            case Symbol_Type::POLYMORPHIC_VALUE: {
                assert(pass->origin_workload->polymorphic_values.data != nullptr, "");
                const auto& value = pass->origin_workload->polymorphic_values[symbol->options.polymorphic_value.access_index];
                if (value.only_datatype_known) {
                    type = value.options.type;
                }
                else {
                    type = value.options.value.type;
                }
                break;
            }
            case Symbol_Type::TYPE:
                type = symbol->options.type;
                break;
            case Symbol_Type::VARIABLE:
                type = symbol->options.variable_type;
                after_text = "Variable";
                break;
            default: break;
            }

            if (type != 0) {
                Context::add_datatype(type);
                Context::set_text_color();
            }

            if (symbol->type != Symbol_Type::TYPE)
            {
                Context::add_line(false, 2);
                Context::set_text_color(symbol_type_to_color(symbol->type));
                Context::add_text(*symbol->id);

                if (after_text != 0) {
                    Context::set_text_color();
                    Context::add_text(": ");
                    Context::add_text(after_text);
                }
            }
        }

        // Analysis-Info
        if (node->type == AST::Node_Type::EXPRESSION && pass != 0)
        {
            auto expr = AST::downcast<AST::Expression>(node);
            auto expression_info = pass_get_node_info(pass, expr, Info_Query::TRY_READ);
            if (expression_info != 0 && !expression_info->contains_errors)
            {
                Context::add_seperator();
                Context::add_line();
                Context::add_text("Expr: ");
                Context::add_datatype(expression_info_get_type(expression_info, false));
            }
        }
    }

    // Push some example text for testing
    if (false)
    {
        Context::add_seperator();
        Context::add_line();
        Context::set_text_color(Context::COLOR_ERROR_TEXT);
        Context::add_text(string_create_static("Error: "));
        Context::set_text_color(Context::COLOR_TEXT);
        Context::set_bg(vec3(0.8f, 0.0f, 0.0f));
        Context::add_text("This is the error");
        Context::stop_bg();
        Context::add_text(" afterwards...");
    }

    // Render new context
    auto& context_lines = editor.context_lines;
    if (context_lines.size > 0)
    {
        auto& char_size = editor.character_size * 0.8f;

        // Figure out size of box
        int max_line_size = 0;
        int line_count = context_lines.size;
        {
            for (int i = 0; i < context_lines.size; i++) {
                auto& line = context_lines[i];
                int length = line.text.size + 2 * line.indentation;
                max_line_size = math_maximum(max_line_size, length);
            }
            max_line_size = math_maximum(max_line_size, 40); // Min box size
        }
        vec2 content_size = vec2(max_line_size * char_size.x, line_count * char_size.y);
        vec2 box_size = content_size + 2 * (Context::BORDER_SIZE + Context::PADDING);

        // Figure out box position
        vec2 box_top_left = vec2(0.0f);
        {
            auto line = source_code_get_line(editor.code, cursor.line);
            vec2 cursor_pos = syntax_editor_position_to_pixel(line->screen_index, line->indentation * 4 + cursor.character);

            int width = rendering_core.render_information.backbuffer_width;
            int height = rendering_core.render_information.backbuffer_height;

            // Move box above cursor if more space is available there
            int pixels_below = cursor_pos.y;
            int pixels_above = height - cursor_pos.y - editor.character_size.y;
            if (pixels_below >= box_size.y || pixels_below > pixels_above) {
                box_top_left.y = cursor_pos.y;
            }
            else {
                box_top_left.y = cursor_pos.y + editor.character_size.y + box_size.y;
            }

            box_top_left.x = cursor_pos.x;
        }
        using Context::BORDER_COLOR;
        using Context::BORDER_SIZE;
        using Context::PADDING;
        vec2 box_content_start = box_top_left + vec2(BORDER_SIZE + PADDING, -BORDER_SIZE - PADDING);

        // Render
        renderer_2D_add_rectangle(editor.renderer_2D, bounding_box_2_make_anchor(box_top_left, box_size, Anchor::TOP_LEFT), BORDER_COLOR);
        renderer_2D_draw(editor.renderer_2D, pass_context);
        renderer_2D_add_rectangle(editor.renderer_2D, bounding_box_2_make_anchor(box_top_left + vec2(BORDER_SIZE, -BORDER_SIZE), content_size + 2 * PADDING, Anchor::TOP_LEFT), Context::COLOR_BG);
        renderer_2D_draw(editor.renderer_2D, pass_context);

        for (int i = 0; i < context_lines.size; i++)
        {
            auto& line = context_lines[i];
            vec2 line_pos = box_content_start + vec2(0.0f, -char_size.y * (i + 1));

            if (line.is_seperator) {
                vec2 seperator_size = vec2(box_size.y - 4, 2.0f);
                vec2 start = vec2(box_content_start.x, char_size.y * (i + 0.5f));
                renderer_2D_add_rectangle(editor.renderer_2D,
                    bounding_box_2_make_anchor(line_pos + vec2(0.0f, char_size.y / 2.0f), vec2(char_size.x * max_line_size, 1.0f), Anchor::CENTER_LEFT),
                    BORDER_COLOR * 0.8f
                );
                continue;
            }

            line_pos.x += line.indentation * 2 * char_size.x;
            if (line.styles.size == 0) {
                text_renderer_add_text(
                    editor.text_renderer, line.text, line_pos, Anchor::BOTTOM_LEFT, char_size.y, vec3(1.0f),
                    optional_make_success(bounding_box_2_make_anchor(box_content_start, content_size, Anchor::TOP_LEFT))
                );
                continue;
            }

            Text_Style default_style;
            default_style.char_start = 0;
            default_style.text_color = vec3(1.0f);
            default_style.has_bg = false;
            default_style.bg_color = Context::COLOR_BG;

            Text_Style last_style = default_style;
            int last_start = 0;
            for (int i = 0; i <= line.styles.size; i++)
            {
                Text_Style style;
                SCOPE_EXIT(last_style = style; last_start = style.char_start;);
                if (i < line.styles.size) {
                    style = line.styles[i];
                }
                else {
                    style = default_style;
                    style.char_start = line.text.size;
                }

                String substring = string_create_substring_static(&line.text, last_start, style.char_start);
                if (substring.size == 0) {
                    continue;
                }

                vec2 pos = line_pos + vec2(last_start * char_size.x, 0.0f);
                if (last_style.has_bg) {
                    renderer_2D_add_rectangle(editor.renderer_2D,
                        bounding_box_2_make_anchor(pos, vec2(char_size.x * substring.size, char_size.y), Anchor::BOTTOM_LEFT),
                        last_style.bg_color
                    );
                }

                text_renderer_add_text(
                    editor.text_renderer, substring, line_pos + vec2(last_start * char_size.x, 0.0f), Anchor::BOTTOM_LEFT, char_size.y, last_style.text_color,
                    optional_make_success(bounding_box_2_make_anchor(box_content_start, content_size, Anchor::TOP_LEFT))
                );
            }
        }

        renderer_2D_draw(editor.renderer_2D, pass_context);
        text_renderer_draw(editor.text_renderer, pass_context);
    }

    // Render Primitives
    renderer_2D_draw(editor.renderer_2D, pass_context);
    text_renderer_draw(editor.text_renderer, pass_context);
}



