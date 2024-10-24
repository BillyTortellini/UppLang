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
#include "../../utility/gui.hpp"

#include "../../win32/input.hpp"
#include "syntax_colors.hpp"
#include "compiler.hpp"
#include "ast.hpp"
#include "symbol_table.hpp"
#include "semantic_analyser.hpp"
#include "parser.hpp"
#include "source_code.hpp"
#include "code_history.hpp"

#include "../../utility/rich_text.hpp"
#include "../../utility/line_edit.hpp"

const int MIN_CURSOR_DISTANCE = 3;
using Rich_Text::Mark_Type;

// Prototypes
bool auto_format_line(int line_index, int tab_index = -1);
void syntax_editor_synchronize_with_compiler(bool generate_code);
void syntax_editor_set_text(String string);

struct Error_Display
{
    String message;
    Token_Range range;

    Source_Code* code;
    bool is_token_range_duplicate;
    int semantic_error_index; // -1 if parsing error
};

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

    // Tabs
    GOTO_NEXT_TAB,
    GOTO_PREV_TAB,

    // Others
    ENTER_FUZZY_FIND_DEFINITION,
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

Error_Display error_display_make(String msg, Token_Range range, Source_Code* code, bool is_token_range_duplicate, int semantic_error_index);

enum class Editor_Mode
{
    NORMAL,
    INSERT,
    FUZZY_FIND_DEFINITION
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

struct Editor_Tab
{
    Source_Code* code;
    Code_History history;
    History_Timestamp last_token_synchronized;

    // Cursor and camera
    Text_Index cursor;
    int last_line_x_pos; // Position with indentation * 4 for up/down movements

    int cam_start;
    int cam_end;
    History_Timestamp last_render_timestamp;
    Text_Index last_render_cursor_pos;
};

struct Syntax_Editor
{
    // Editing
    Editor_Mode mode;
    Dynamic_Array<Editor_Tab> tabs;
    int open_tab_index;
    int main_tab_index; // If -1, use the currently open tab for compiling
    float normal_text_size_pixel;

    bool last_compile_was_with_code_gen;
    bool code_changed_since_last_compile;

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
    char last_search_char;
    bool last_search_was_forward;
    bool last_search_was_to;

    // Text
    Rich_Text::Rich_Text editor_text;
    Text_Display::Text_Display text_display;

    // Fuzzy-Find definition
    String fuzzy_find_search;
    Line_Editor fuzzy_line_edit;
    Dynamic_Array<Symbol*> fuzzy_find_suggestions;

    // Rendering
    Dynamic_Array<Error_Display> errors;
    Dynamic_Array<Token_Range> token_range_buffer;

    Bounding_Box2 code_box;
    Input* input;
    Rendering_Core* rendering_core;
    Renderer_2D* renderer_2D;
    Text_Renderer* text_renderer;
};



// Editor
static Syntax_Editor syntax_editor;

// Returns new tab index
int syntax_editor_add_tab(String file_path)
{
    auto& editor = syntax_editor;

    Source_Code* code = compiler_add_source(file_path, true, false);
    if (code == 0) { // Could not load file
        return editor.open_tab_index;
    }

    // Check if code is already opened in tab
    for (int i = 0; i < editor.tabs.size; i++) {
        auto& tab = editor.tabs[i];
        if (tab.code == code) {
            return i;
        }
    }

    // Otherwise create new tab
    Editor_Tab tab;
    tab.code = code;
    tab.history = code_history_create(tab.code);
    tab.last_token_synchronized = history_get_timestamp(&tab.history);
    tab.last_render_timestamp = history_get_timestamp(&tab.history);
    tab.last_render_cursor_pos = text_index_make(0, 0);
    tab.cursor = text_index_make(0, 0);
    tab.last_line_x_pos = 0;
    tab.cam_start = 0;
    tab.cam_end = 0;
    dynamic_array_push_back(&syntax_editor.tabs, tab);

    editor.code_changed_since_last_compile = true;
    syntax_editor_synchronize_with_compiler(false);
    return syntax_editor.tabs.size - 1;
}

void editor_tab_destroy(Editor_Tab* tab) {
    code_history_destroy(&tab->history);
}

void syntax_editor_switch_tab(int new_tab_index)
{
    auto& editor = syntax_editor;
    if (editor.open_tab_index == new_tab_index) return;
    if (new_tab_index < 0 || new_tab_index >= editor.tabs.size) return;

    editor.open_tab_index = new_tab_index;
    if (editor.main_tab_index == -1) {
        editor.code_changed_since_last_compile = true;
        syntax_editor_synchronize_with_compiler(false);
    }
}

void syntax_editor_close_tab(int tab_index)
{
    auto& editor = syntax_editor;
    if (editor.tabs.size <= 1) return;
    if (tab_index < 0 || tab_index >= editor.tabs.size) return;

    Editor_Tab* tab = &editor.tabs[tab_index];
    tab->code->open_in_editor = false;
    editor_tab_destroy(&editor.tabs[tab_index]);
    dynamic_array_remove_ordered(&editor.tabs, tab_index);

    editor.open_tab_index = math_minimum(editor.tabs.size - 1, editor.open_tab_index);
    if (tab_index == editor.main_tab_index) {
        editor.main_tab_index = -1;
        editor.code_changed_since_last_compile = true;
        syntax_editor_synchronize_with_compiler(false);
    }
}

void syntax_editor_initialize(Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Window* window, Input* input, Timer* timer)
{
    memory_zero(&syntax_editor);
    gui_initialize(text_renderer, window);

    syntax_editor.last_compile_was_with_code_gen = false;
    syntax_editor.code_changed_since_last_compile = true;
    syntax_editor.editor_text = Rich_Text::create(vec3(1.0f));
    syntax_editor.normal_text_size_pixel = convertHeight(0.48f, Unit::CENTIMETER);
    syntax_editor.text_display = Text_Display::make(
        &syntax_editor.editor_text, renderer_2D, text_renderer, text_renderer_get_aligned_char_size(text_renderer, syntax_editor.normal_text_size_pixel), 4
    );
    Text_Display::set_padding(&syntax_editor.text_display, 2);
    Text_Display::set_block_outline(&syntax_editor.text_display, 3, vec3(0.5f));

    syntax_editor.errors = dynamic_array_create<Error_Display>(1);
    syntax_editor.token_range_buffer = dynamic_array_create<Token_Range>(1);
    syntax_editor.code_completion_suggestions = dynamic_array_create<String>();
    syntax_editor.command_buffer = string_create();

    syntax_editor.fuzzy_find_search = string_create();
    syntax_editor.fuzzy_find_suggestions = dynamic_array_create<Symbol*>();

    syntax_editor.yank_string = string_create();
    syntax_editor.yank_was_line = false;

    syntax_editor.last_normal_command.type = Normal_Mode_Command_Type::MOVEMENT;
    syntax_editor.last_normal_command.options.movement.type = Movement_Type::MOVE_LEFT;
    syntax_editor.last_normal_command.options.movement.repeat_count = 0;
    syntax_editor.last_insert_commands = dynamic_array_create<Insert_Command>();
    syntax_editor.record_insert_commands = true;
    syntax_editor.last_recorded_code_completion = string_create();

    syntax_editor.text_renderer = text_renderer;
    syntax_editor.renderer_2D = renderer_2D;
    syntax_editor.input = input;
    syntax_editor.mode = Editor_Mode::NORMAL;

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

    String default_filename = string_create_static("upp_code/editor_text.upp");
    int tab_index = syntax_editor_add_tab(default_filename);
    assert(tab_index != -1, "");
    syntax_editor.open_tab_index = 0;
    syntax_editor.main_tab_index = 0;
}

void syntax_editor_destroy()
{
    auto& editor = syntax_editor;
    dynamic_array_destroy(&editor.code_completion_suggestions);
    dynamic_array_destroy(&editor.fuzzy_find_suggestions);
    Rich_Text::destroy(&editor.editor_text);
    string_destroy(&syntax_editor.command_buffer);
    string_destroy(&syntax_editor.yank_string);
    string_destroy(&syntax_editor.fuzzy_find_search);
    compiler_destroy();
    for (int i = 0; i < editor.errors.size; i++) {
        string_destroy(&editor.errors[i].message);
    }
    dynamic_array_destroy(&editor.errors);
    dynamic_array_destroy(&editor.token_range_buffer);

    dynamic_array_destroy(&editor.last_insert_commands);
    string_destroy(&editor.last_recorded_code_completion);

    dynamic_array_for_each(editor.tabs, editor_tab_destroy);
    dynamic_array_destroy(&editor.tabs);

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
    auto& tab = editor.tabs[editor.open_tab_index];

    tab.cursor = text_index_make(0, 0);
    source_code_fill_from_string(tab.code, string);
    source_code_tokenize(tab.code);
    for (int i = 0; i < tab.code->line_count; i++) {
        auto_format_line(i);
    }
    code_history_reset(&tab.history);
    tab.last_token_synchronized = history_get_timestamp(&tab.history);
    tab.code->code_changed_since_last_compile = true;
    editor.code_changed_since_last_compile = true;
    tab.last_render_cursor_pos = tab.cursor;
    tab.last_render_timestamp = tab.last_token_synchronized;
    // compiler_compile_clean(editor.code, Compile_Type::ANALYSIS_ONLY, string_create(syntax_editor.file_path));
}

void syntax_editor_save_text_file()
{
    auto& editor = syntax_editor;
    for (int i = 0; i < editor.tabs.size; i++) 
    {
        auto& tab = editor.tabs[i];
        String whole_text = string_create_empty(256);
        SCOPE_EXIT(string_destroy(&whole_text));
        source_code_append_to_string(tab.code, &whole_text);
        auto path = tab.code->file_path;
        auto success = file_io_write_file(path.characters, array_create_static((byte*)whole_text.characters, whole_text.size));
        if (!success) {
            logg("Saving file failed for path \"%s\"\n", path.characters);
        }
        else {
            logg("Saved file \"%s\"!\n", path.characters);
        }
    }
}

void syntax_editor_synchronize_tokens()
{
    auto& editor = syntax_editor;

    for (int tab_index = 0; tab_index < editor.tabs.size; tab_index++)
    {
        auto& tab = editor.tabs[tab_index];

        // Get changes since last sync
        Dynamic_Array<Code_Change> changes = dynamic_array_create<Code_Change>(1);
        SCOPE_EXIT(dynamic_array_destroy(&changes));
        auto now = history_get_timestamp(&tab.history);
        history_get_changes_between(&tab.history, tab.last_token_synchronized, now, &changes);
        tab.last_token_synchronized = now;
        if (changes.size != 0) {
            tab.code->code_changed_since_last_compile = true;
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
                    if (line_changes[j] == changed_line) {
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
            bool changed = auto_format_line(index, tab_index);
            //assert(!changed, "Syntax editor has to make sure that lines are sanitized after edits!");

            // Tokenization is done by auto_format_line
            //source_code_tokenize_line(syntax_editor.code, line_changes[i]);
            //logg("Synchronized: %d/%d\n", index.block_index.block_index, index.line_index);
        }
    }
}

void syntax_editor_synchronize_with_compiler(bool generate_code)
{
    syntax_editor_synchronize_tokens();
    auto& editor = syntax_editor;

    if (!editor.code_changed_since_last_compile)
    {
        if (generate_code) {
            if (editor.last_compile_was_with_code_gen) {
                return;
            }
        }
        else {
            return;
        }
    }
    syntax_editor.code_changed_since_last_compile = false;
    syntax_editor.last_compile_was_with_code_gen = generate_code;

    auto& main_tab = syntax_editor.tabs[editor.main_tab_index == -1 ? editor.open_tab_index : editor.main_tab_index];
    compiler_compile_clean(main_tab.code, (generate_code ? Compile_Type::BUILD_CODE : Compile_Type::ANALYSIS_ONLY));

    // Collect errors from all compiler stages
    {
        for (int i = 0; i < editor.errors.size; i++) {
            string_destroy(&editor.errors[i].message);
        }
        dynamic_array_reset(&editor.errors);

        // Parse Errors
        for (int i = 0; i < compiler.program_sources.size; i++)
        {
            auto code = compiler.program_sources[i];
            auto& parse_errors = code->error_messages;
            for (int j = 0; j < parse_errors.size; j++) {
                auto& error = parse_errors[j];
                dynamic_array_push_back(
                    &editor.errors,
                    error_display_make(string_create_static(error.msg), error.range, code, false, -1)
                );
            }
        }

        auto error_ranges = dynamic_array_create<Token_Range>(1);
        SCOPE_EXIT(dynamic_array_destroy(&error_ranges));

        // Semantic Analysis Errors
        for (int i = 0; i < compiler.semantic_analyser->errors.size; i++)
        {
            auto& error = compiler.semantic_analyser->errors[i];
            auto node = error.error_node;
            assert(node != 0, "");
            auto code = compiler_find_ast_source_code(node);

            dynamic_array_reset(&error_ranges);
            Parser::ast_base_get_section_token_range(code, node, error.section, &error_ranges);
            for (int j = 0; j < error_ranges.size; j++) {
                auto& range = error_ranges[j];
                assert(token_index_compare(range.start, range.end) >= 0, "hey");
                dynamic_array_push_back(&editor.errors, error_display_make(string_create_static(error.msg), range, code, j != 0, i));
            }
        }
    }
}



// Helpers
Error_Display error_display_make(String msg, Token_Range range, Source_Code* code, bool is_token_range_duplicate, int semantic_error_index)
{
    Error_Display result;
    result.message = msg;
    result.range = range;
    result.code = code;
    result.is_token_range_duplicate = is_token_range_duplicate;
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
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto& c = tab.cursor;

    auto line = source_code_get_line(tab.code, c.line);
    return character_index_to_token(&line->tokens, c.character, after_cursor);
}

Token get_cursor_token(bool after_cursor)
{
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto& c = tab.cursor;

    int tok_index = get_cursor_token_index(after_cursor);
    auto tokens = source_code_get_line(tab.code, c.line)->tokens;
    if (tok_index >= tokens.size) return token_make_dummy();
    return tokens[tok_index];
}

char get_cursor_char(char dummy_char)
{
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto& c = tab.cursor;

    Source_Line* line = source_code_get_line(tab.code, c.line);
    if (c.character >= line->text.size) return dummy_char;
    return line->text.characters[c.character];
}

Text_Index sanitize_index(Text_Index index) {
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto code = tab.code;

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
    auto& tab = editor.tabs[editor.open_tab_index];
    auto& code = tab.code;
    auto& index = tab.cursor;

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

// If tab index == -1, then take current tab
bool auto_format_line(int line_index, int tab_index)
{
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[tab_index == -1 ? editor.open_tab_index : tab_index];
    auto code = tab.code;

    source_code_tokenize_line(code, line_index);
    Source_Line* line = source_code_get_line(code, line_index);
    if (line->is_comment) {
        return false;
    }

    auto& text = line->text;
    bool cursor_on_line = tab.cursor.line == line_index;
    bool respect_cursor_space = editor.mode == Editor_Mode::INSERT && cursor_on_line;
    auto& pos = tab.cursor.character;

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
            history_delete_char(&tab.history, text_index_make(line_index, 0));
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
                history_insert_char(&tab.history, text_index_make(line_index, curr.end_index), ' ');
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
                    history_delete_char(&tab.history, text_index_make(line_index, curr.end_index));
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
                history_delete_char(&tab.history, text_index_make(line_index, curr.end_index));
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
            history_delete_char(&tab.history, text_index_make(line_index, line->text.size - 1));
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
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];

    syntax_editor_synchronize_tokens();
    auto token = get_cursor_token(false);
    String result;
    if (token.type == Token_Type::IDENTIFIER) {
        result = *token.options.identifier;
    }
    else {
        return string_create_static("");
    }

    auto pos = tab.cursor.character;
    if (pos < token.end_index) {
        result = string_create_substring_static(&result, 0, pos - token.start_index);
    }
    return result;
}

void code_completion_find_suggestions()
{
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto& suggestions = editor.code_completion_suggestions;
    auto& cursor = tab.cursor;

    dynamic_array_reset(&suggestions);
    if (editor.mode != Editor_Mode::INSERT || cursor.character == 0) {
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
    Token_Index cursor_token_index = token_index_make(cursor.line, get_cursor_token_index(false));
    auto node = Parser::find_smallest_enclosing_node(upcast(tab.code->root), cursor_token_index);

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
            Token_Index prev = token_index_make(cursor.line, cursor_token_index - 1);
            auto node = Parser::find_smallest_enclosing_node(upcast(tab.code->root), prev);
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
        auto& tokens = source_code_get_line(tab.code, cursor.line)->tokens;
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
    auto& tab = editor.tabs[editor.open_tab_index];
    auto& suggestions = editor.code_completion_suggestions;

    string_reset(&editor.last_recorded_code_completion);

    if (suggestions.size == 0) return;
    if (tab.cursor.character == 0) return;
    String replace_string = suggestions[0];
    auto line = source_code_get_line(tab.code, tab.cursor.line);

    string_append_string(&editor.last_recorded_code_completion, &replace_string);

    // Remove current token
    int token_index = get_cursor_token_index(false);
    int start_pos = line->tokens[token_index].start_index;
    history_delete_text(&tab.history, text_index_make(tab.cursor.line, start_pos), tab.cursor.character);
    // Insert suggestion instead
    tab.cursor.character = start_pos;
    history_insert_text(&tab.history, tab.cursor, replace_string);
    tab.cursor.character += replace_string.size;
    auto_format_line(tab.cursor.line);
}



// Editor update
void editor_enter_insert_mode()
{
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];

    if (syntax_editor.mode == Editor_Mode::INSERT) {
        return;
    }
    if (syntax_editor.record_insert_commands) {
        dynamic_array_reset(&syntax_editor.last_insert_commands);
    }
    syntax_editor.mode = Editor_Mode::INSERT;
    history_start_complex_command(&tab.history);
}

void editor_leave_insert_mode()
{
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];

    if (syntax_editor.mode != Editor_Mode::INSERT) {
        return;
    }
    syntax_editor.mode = Editor_Mode::NORMAL;
    history_stop_complex_command(&tab.history);
    history_set_cursor_pos(&tab.history, tab.cursor);
    dynamic_array_reset(&editor.code_completion_suggestions);
    auto_format_line(tab.cursor.line);
}

void editor_split_line_at_cursor(int indentation_offset)
{
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto& mode = editor.mode;
    auto& cursor = tab.cursor;

    Source_Line* line = source_code_get_line(tab.code, cursor.line);
    auto& text = line->text;
    int line_size = text.size; // Text may be invalid after applying history changes
    String cutout = string_create_substring_static(&text, cursor.character, text.size);

    history_start_complex_command(&tab.history);
    SCOPE_EXIT(history_stop_complex_command(&tab.history));

    int new_line_index = cursor.line + 1;
    history_insert_line(&tab.history, new_line_index, math_maximum(0, line->indentation + indentation_offset));

    if (cursor.character != line_size) {
        history_insert_text(&tab.history, text_index_make(new_line_index, 0), cutout);
        history_delete_text(&tab.history, cursor, line_size);
    }
    cursor = text_index_make(new_line_index, 0);
}

namespace Motions
{
    Source_Line* get_line(const Text_Index& pos) {
        auto& editor = syntax_editor;
        auto& tab = editor.tabs[editor.open_tab_index];
        auto code = tab.code;

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

    void move_forwards_over_line(Text_Index& pos) 
    {
        auto& editor = syntax_editor;
        auto& tab = editor.tabs[editor.open_tab_index];
        auto code = tab.code;

        auto line = get_line(pos);
        if (line == nullptr) return;

        if (pos.character < line->text.size) {
            pos.character += 1;
            return;
        }

        if (pos.line + 1 >= code->line_count) {
            return;
        }

        pos.character = 0;
        pos.line += 1;
    }

    void move_backwards_over_line(Text_Index& pos)
    {
        auto& editor = syntax_editor;
        auto& tab = editor.tabs[editor.open_tab_index];
        auto code = tab.code;

        if (pos.character > 0) {
            pos.character -= 1;
            return;
        }

        if (pos.line == 0) return;
        pos = text_index_make_line_end(code, pos.line - 1);
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
        auto& editor = syntax_editor;
        auto& tab = editor.tabs[editor.open_tab_index];
        auto code = tab.code;

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
                    if (line->text.size != 0 && line->text.characters[line->text.size - 1] == start_char) {
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
        case ';': index += 1; return parse_result_success(movement_make(Movement_Type::REPEAT_LAST_SEARCH, repeat_count));
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
            if (index + 1 >= cmd.size) {
                return parse_result_completable<Movement>();
            }
            if (cmd[index + 1] == 'g') {
                index += 2;
                if (repeat_count_exists) {
                    return parse_result_success(movement_make(Movement_Type::GOTO_LINE_NUMBER, repeat_count));
                }
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

        // Handle gT and gt
        {
            char curr_char = cmd[index];
            char follow_char = index + 1 < cmd.size ? cmd[index + 1] : '?';
            bool follow_char_valid = index + 1 < cmd.size;

            if (curr_char == 'g' && (follow_char == 'T' || follow_char == 't')) 
            {
                if (!repeat_count_exists) {
                    repeat_count = 0;
                }
                if (follow_char == 'T') {
                    return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::GOTO_PREV_TAB, repeat_count));
                }
                else {
                    return parse_result_success(normal_mode_command_make(Normal_Mode_Command_Type::GOTO_NEXT_TAB, repeat_count));
                }
            }
        }

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
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto code = tab.code;

    pos = sanitize_index(pos);
    Source_Line* line = source_code_get_line(code, pos.line);

    auto do_char_search = [&](char c, bool forward, bool search_towards) {
        auto char_equals = [](char c, void* test_char) -> bool {
            char other = *(char*)test_char;
            return c == other;
        };
        Text_Index start = pos;
        bool found = Motions::goto_next_in_set(start, char_equals, (void*)&c, forward, true);
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
            pos.character = tab.last_line_x_pos - line->indentation * 4;
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
            tab.last_line_x_pos = 10000; // Look at jk movements after $ to understand this
            set_horizontal_pos = false;
            repeat_movement = false;
            break;
        }
        case Movement_Type::TO_START_OF_LINE: {
            pos.character = 0;
            tab.last_line_x_pos = 0; // Look at jk movements after $ to understand this
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
            tab.last_line_x_pos = pos.character + 4 * line->indentation;
        }
    }

    return pos;
}

Text_Range motion_evaluate(const Motion& motion, Text_Index pos)
{
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto code = tab.code;

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
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto code = tab.code;
    auto history = &tab.history;

    history_start_complex_command(history);
    SCOPE_EXIT(history_stop_complex_command(history));

    // Handle single line case
    if (range.start.line == range.end.line) {
        history_delete_text(history, range.start, range.end.character);
        return;
    }

    // Delete text in first line
    auto line = Motions::get_line(range.start);
    auto end_line = Motions::get_line(range.end);
    if (end_line == nullptr || line == nullptr) return;
    history_delete_text(history, range.start, line->text.size);

    // Append remaining text of last-line into first line
    String remainder = string_create_substring_static(&end_line->text, range.end.character, end_line->text.size);
    history_insert_text(history, range.start, remainder);
    if (line->indentation != end_line->indentation && range.start.character == 0) {
        history_change_indent(history, range.start.line, end_line->indentation);
    }

    // Delete all lines inbetween
    for (int i = range.start.line + 1; range.start.line + 1 < code->line_count && i <= range.end.line; i++) {
        history_remove_line(history, range.start.line + 1);
    }
}

void text_range_append_to_string(Text_Range range, String* str)
{
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto code = tab.code;

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
    auto& tab = editor.tabs[editor.open_tab_index];
    auto code = tab.code;
    auto& cursor = tab.cursor;
    auto history = &tab.history;

    history_start_complex_command(history);
    SCOPE_EXIT(history_stop_complex_command(history));

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
            history_insert_line_with_text(history, line_insert_index + i, yank_line.indentation, yank_line.text);
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
        history_insert_text(history, pos, first_line.text);

        if (yank_lines.size > 1)
        {
            // Insert the rest of the lines
            for (int i = 1; i < yank_lines.size; i++) {
                const Yank_Line& yank_line = yank_lines[i];
                history_insert_line_with_text(history, pos.line + i, yank_line.indentation, yank_line.text);
            }

            // Append first line cutoff to last line, e.g. asdf|what and then put needs to add what to the end
            Source_Line* first = source_code_get_line(code, pos.line);
            int cutoff_start = pos.character + first_line.text.size;
            String substring = string_create_substring_static(&first->text, cutoff_start, first->text.size);
            history_insert_text(history, text_index_make_line_end(code, pos.line + yank_lines.size - 1), substring);
            history_delete_text(history, text_index_make(pos.line, cutoff_start), first->text.size);
        }
    }
}

void normal_command_execute(Normal_Mode_Command& command)
{
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto code = tab.code;
    auto& cursor = tab.cursor;
    auto history = &tab.history;

    auto line = source_code_get_line(code, cursor.line);
    auto& tokens = line->tokens;

    // Save cursor pos
    history_set_cursor_pos(history, cursor);

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
    bool execute_as_complex = command.type != Normal_Mode_Command_Type::UNDO && command.type != Normal_Mode_Command_Type::REDO &&
        command.type != Normal_Mode_Command_Type::GOTO_NEXT_TAB && command.type != Normal_Mode_Command_Type::GOTO_PREV_TAB;
    if (execute_as_complex) {
        history_start_complex_command(history);
    }

    SCOPE_EXIT(
        if (command.type != Normal_Mode_Command_Type::GOTO_NEXT_TAB && command.type != Normal_Mode_Command_Type::GOTO_PREV_TAB) 
        {
            syntax_editor_sanitize_cursor();
            history_set_cursor_pos(history, cursor);
            auto_format_line(cursor.line);
            if (execute_as_complex) {
                history_stop_complex_command(history);
            }
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
        history_insert_line(history, new_line_index, line->indentation);
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
                start_line = math_minimum(code->line_count - 1, start_line);
            }

            string_reset(&editor.yank_string);
            for (int i = start_line; i <= end_line; i += 1) {
                auto line = source_code_get_line(code, i);
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
        // printf("Yanked: was_line = %s ----\n%s\n----\n", (editor.yank_was_line ? "true" : "false"), editor.yank_string.characters);

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
        history_delete_char(history, cursor);
        history_insert_char(history, cursor, command.options.character);
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
        history_undo(history);
        auto cursor_history = history_get_cursor_pos(history);
        if (cursor_history.available) {
            cursor = cursor_history.value;
        }
        break;
    }
    case Normal_Mode_Command_Type::REDO: {
        history_redo(history);
        auto cursor_history = history_get_cursor_pos(history);
        if (cursor_history.available) {
            cursor = cursor_history.value;
        }
        break;
    }

    case Normal_Mode_Command_Type::SCROLL_DOWNWARDS_HALF_PAGE:
    case Normal_Mode_Command_Type::SCROLL_UPWARDS_HALF_PAGE: {
        int dir = command.type == Normal_Mode_Command_Type::SCROLL_DOWNWARDS_HALF_PAGE ? -1 : 1;
        tab.cam_start += (tab.cam_start - tab.cam_end) / 2 * dir;
        break;
    }
    case Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_TOP: {
        tab.cam_start = tab.cursor.line - MIN_CURSOR_DISTANCE;
        break;
    }
    case Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_CENTER: {
        tab.cam_start = tab.cursor.line - (tab.cam_end - tab.cam_start) / 2;
        break;
    }
    case Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_BOTTOM: {
        tab.cam_start = tab.cursor.line - (tab.cam_end - tab.cam_start) + MIN_CURSOR_DISTANCE;
        break;
    }
    case Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_TOP: {
        cursor.line = tab.cam_start + MIN_CURSOR_DISTANCE;
        cursor.character = 0;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_CENTER: {
        cursor.line = tab.cam_start + (tab.cam_end - tab.cam_start) / 2;
        cursor.character = 0;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_BOTTOM: {
        cursor.line = tab.cam_end - MIN_CURSOR_DISTANCE;
        cursor.character = 0;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Mode_Command_Type::GOTO_NEXT_TAB: 
    case Normal_Mode_Command_Type::GOTO_PREV_TAB: {
        int repeat_count = command.repeat_count;
        if (editor.tabs.size == 1) break;

        int next_tab_index = editor.open_tab_index;
        if (repeat_count != 0) {
            next_tab_index = repeat_count - 1;
            next_tab_index = math_clamp(next_tab_index, 0, editor.tabs.size - 1);
        }
        else {
            next_tab_index = next_tab_index + (command.type == Normal_Mode_Command_Type::GOTO_NEXT_TAB ? 1 : -1);
            next_tab_index = math_modulo(next_tab_index, editor.tabs.size);
        }

        syntax_editor_switch_tab(next_tab_index);
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
    case Normal_Mode_Command_Type::ENTER_FUZZY_FIND_DEFINITION: {
        string_reset(&editor.fuzzy_find_search);
        editor.fuzzy_line_edit = line_editor_make();
        dynamic_array_reset(&editor.fuzzy_find_suggestions);
        editor.mode = Editor_Mode::FUZZY_FIND_DEFINITION;
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
    auto& tab = editor.tabs[editor.open_tab_index];
    auto code = tab.code;
    auto history = &tab.history;
    auto& cursor = tab.cursor;
    auto& pos = cursor.character;
    auto line = source_code_get_line(code, cursor.line);
    auto& text = line->text;

    history_start_complex_command(history);
    SCOPE_EXIT(history_stop_complex_command(history));

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
            history_insert_text(history, cursor, editor.last_recorded_code_completion);
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
            history_change_indent(history, cursor.line, line->indentation + 1);
            break;
        }
        editor_split_line_at_cursor(1);
        break;
    }
    case Insert_Command_Type::REMOVE_INDENTATION: {
        if (line->indentation > 0) {
            history_change_indent(history, cursor.line, line->indentation - 1);
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
            history_insert_char(history, cursor, double_char);
        }
        history_insert_char(history, cursor, input.letter);
        pos += 1;
        // Inserting delimiters between space critical tokens can lead to spaces beeing removed
        auto_format_line(cursor.line);
        break;
    }
    case Insert_Command_Type::SPACE:
    {
        // Handle strings and comments, where we always just add a space
        if (line->is_comment) {
            history_insert_char(history, cursor, ' ');
            pos += 1;
            break;
        }
        if (pos == 0) break;
        syntax_editor_synchronize_tokens();
        auto token = get_cursor_token(false);
        if (token.type == Token_Type::COMMENT) {
            if (pos > token.start_index + 1) {
                history_insert_char(history, cursor, ' ');
                pos += 1;
            }
            break;
        }
        if (token.type == Token_Type::LITERAL && token.options.literal_value.type == Literal_Type::STRING) {
            if (pos > token.start_index && pos < token.end_index) {
                history_insert_char(history, cursor, ' ');
                pos += 1;
            }
            break;
        }

        char prev = text[pos - 1];
        if ((char_is_space_critical(prev)) || (pos == text.size && prev != ' ')) {
            history_insert_char(history, cursor, ' ');
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
            auto prev_line = source_code_get_line(code, cursor.line - 1);
            // Merge this line_index with previous one
            Text_Index insert_index = text_index_make(cursor.line - 1, prev_line->text.size);
            history_insert_text(history, insert_index, text);
            history_remove_line(history, cursor.line);
            cursor = insert_index;
            auto_format_line(cursor.line);
            break;
        }

        history_delete_char(history, text_index_make(cursor.line, pos - 1));
        pos -= 1;
        auto_format_line(cursor.line);
        break;
    }
    case Insert_Command_Type::NUMBER_LETTER:
    case Insert_Command_Type::IDENTIFIER_LETTER:
    {
        history_insert_char(history, cursor, input.letter);
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

    switch (editor.mode)
    {
    case Editor_Mode::INSERT: 
    {
        Parse_Result<Insert_Command> result = parse_insert_command(msg);
        if (result.type == Parse_Result_Type::SUCCESS) {
            insert_command_execute(result.result);
        }
        break;
    }
    case Editor_Mode::NORMAL: 
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
            case Key_Code::P: command_type = Normal_Mode_Command_Type::ENTER_FUZZY_FIND_DEFINITION; break;
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

        break;
    }
    case Editor_Mode::FUZZY_FIND_DEFINITION: 
    {
        // Exit if requested
        if (msg.key_code == Key_Code::L && msg.ctrl_down && msg.key_down) {
            editor.mode = Editor_Mode::NORMAL;
            return;
        }

        if (msg.key_code == Key_Code::RETURN && msg.key_down) 
        {
            if (editor.fuzzy_find_suggestions.size > 0) 
            {
                editor.mode = Editor_Mode::NORMAL;
                auto symbol = editor.fuzzy_find_suggestions[0];
                assert(symbol->definition_node != 0, "");

                // Switch tab to file with symbol
                Source_Code* code = compiler_find_ast_source_code(symbol->definition_node);
                int index = syntax_editor_add_tab(code->file_path);
                syntax_editor_switch_tab(index);

                auto& tab = editor.tabs[editor.open_tab_index];
                Token_Index token = symbol->definition_node->range.start;
                auto line = source_code_get_line(tab.code, token.line);
                tab.cursor.line = token.line;
                if (token.token < line->tokens.size) {
                    tab.cursor.character = line->tokens[token.token].start_index;
                }
                else {
                    tab.cursor.character = 0;
                }
            }
            return;
        }

        bool changed = false;
        if (msg.key_code == Key_Code::TAB && msg.key_down && editor.fuzzy_find_suggestions.size > 0) {
            auto symbol = editor.fuzzy_find_suggestions[0];
            auto& search = editor.fuzzy_find_search;
            int reset_pos = 0;

            Optional<int> result = string_find_character_index_reverse(&search, '~', search.size-1);
            if (result.available) {
                reset_pos = result.value + 1;
            }
            string_remove_substring(&search, reset_pos, search.size);
            string_append_string(&search, symbol->id);
            if (symbol->type == Symbol_Type::MODULE) {
                string_append_character(&search, '~');
            }
            changed = true;

            editor.fuzzy_line_edit.pos = search.size;
            editor.fuzzy_line_edit.select_start = search.size;
        }

        // Otherwise let line handler use key-message
        if (!changed) {
            changed = line_editor_feed_key_message(editor.fuzzy_line_edit, &editor.fuzzy_find_search, msg);
        }

        if (changed) 
        {
            if (editor.fuzzy_find_search.size == 0) {
                dynamic_array_reset(&editor.fuzzy_find_suggestions);
                return;
            }

            auto& tab = editor.tabs[editor.open_tab_index];
            syntax_editor_synchronize_with_compiler(false);

            Symbol_Table* symbol_table = nullptr;
            Array<String> path_parts = string_split(editor.fuzzy_find_search, '~');
            SCOPE_EXIT(string_split_destroy(path_parts));

            String search = editor.fuzzy_find_search;
            bool is_intern = true;
            if (path_parts[0].size == 0) { // E.g. first term is a ~
                search = string_create_substring_static(&editor.fuzzy_find_search, 1, editor.fuzzy_find_search.size);
                symbol_table = compiler.main_source->module_progress->module_analysis->symbol_table;
                is_intern = false;
            }
            else 
            {
                Token_Index cursor_token_index = token_index_make(tab.cursor.line, get_cursor_token_index(true));
                AST::Node* node = Parser::find_smallest_enclosing_node(upcast(tab.code->root), cursor_token_index);
                Analysis_Pass* pass = code_query_get_analysis_pass(node); // Note: May be null?
                assert(pass != 0, "After compile the module discory should always execute with normal pass...");
                symbol_table = code_query_get_ast_node_symbol_table(node);
                is_intern = true;
            }
            assert(symbol_table != 0, "At least root table should always be available");

            // Follow path
            Dynamic_Array<Symbol*> symbols = dynamic_array_create<Symbol*>();
            SCOPE_EXIT(dynamic_array_destroy(&symbols));
            bool search_includes = true;
            {
                for (int i = 0; i < path_parts.size - 1; i++) 
                {
                    String part = path_parts[i];
                    if (i == 0 && part.size == 0) {
                        continue; 
                    }

                    String* id = identifier_pool_add(&compiler.identifier_pool, part);
                    dynamic_array_reset(&symbols);
                    symbol_table_query_id(symbol_table, id, search_includes, (is_intern ? Symbol_Access_Level::INTERNAL : Symbol_Access_Level::GLOBAL), &symbols);
                    search_includes = false;
                    is_intern = false;

                    Symbol_Table* next_table = nullptr;
                    for (int j = 0; j < symbols.size; j++) {
                        auto symbol = symbols[j];
                        if (symbol->type == Symbol_Type::MODULE) {
                            next_table = symbol->options.module_progress->module_analysis->symbol_table;
                            break;
                        }
                    }

                    if (next_table == 0) {
                        return;
                    }
                    symbol_table = next_table;
                }
            }

            // Add all symbols to fuzzy search
            dynamic_array_reset(&symbols);
            symbol_table_query_id(symbol_table, 0, search_includes, Symbol_Access_Level::INTERNAL, &symbols);
            String last = path_parts[path_parts.size - 1];
            fuzzy_search_start_search(last);
            for (int i = 0; i < symbols.size; i++) {
                auto symbol = symbols[i];
                if (symbol->definition_node != 0) {
                    fuzzy_search_add_item(*symbol->id, i);
                }
            }

            auto items = fuzzy_search_rank_results(true, 3);
            auto& suggestions = editor.fuzzy_find_suggestions;
            dynamic_array_reset(&suggestions);
            for (int i = 0; i < items.size; i++) {
                dynamic_array_push_back(&suggestions, symbols[items[i].user_index]);
            }
        }
        break;
    }
    default:panic("");
    }
}

void syntax_editor_update()
{
    auto& editor = syntax_editor;
    auto& input = syntax_editor.input;
    auto& mode = syntax_editor.mode;

    SCOPE_EXIT(
        for (int i = 0; i < editor.tabs.size; i++) {
            source_code_sanity_check(editor.tabs[i].code);
        }
    );

    // Handle input replay
    {
        auto& tab = editor.tabs[editor.open_tab_index];
        auto& replay = editor.input_replay;
        if (editor.input->key_pressed[(int)Key_Code::F9]) {
            if (replay.currently_recording) {
                logg("Ending recording of inputs, captures: %d messages!\n", replay.recorded_inputs.size);
                replay.currently_recording = false;
                string_reset(&replay.code_state_afterwards);
                source_code_append_to_string(tab.code, &replay.code_state_afterwards);
            }
            else {
                logg("Started recording keyboard inputs!", replay.recorded_inputs.size);
                replay.currently_recording = true;
                replay.cursor_start = tab.cursor;
                replay.start_mode = editor.mode;
                string_reset(&replay.code_state_initial);
                source_code_append_to_string(tab.code, &replay.code_state_initial);
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
                tab.cursor = replay.cursor_start;
                editor.mode = replay.start_mode;
                for (int i = 0; i < replay.recorded_inputs.size; i++) {
                    syntax_editor_process_key_message(replay.recorded_inputs[i]);
                }
                auto text_afterwards = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&text_afterwards));
                source_code_append_to_string(tab.code, &text_afterwards);
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
        String filename = string_create();
        SCOPE_EXIT(string_destroy(&filename));
        bool worked = file_io_open_file_selection_dialog(&filename);
        if (worked) {
            int tab_index = syntax_editor_add_tab(filename);
            syntax_editor_switch_tab(tab_index);
        }
    }
    else if (syntax_editor.input->key_pressed[(int)Key_Code::S] && syntax_editor.input->key_down[(int)Key_Code::CTRL]) {
        syntax_editor_save_text_file();
    }
    else if (syntax_editor.input->key_pressed[(int)Key_Code::F8]) {
        compiler_run_testcases(compiler.timer, true);
    }

    // Handle Editor inputs
    for (int i = 0; i < input->key_messages.size; i++) {
        syntax_editor_process_key_message(input->key_messages[i]);
    }

    // Generate GUI
    {
        // Draw Tabs
        auto root_node = gui_add_node(gui_root_handle(), gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_none());
        auto tabs_container = gui_add_node(root_node, gui_size_make_fill(), gui_size_make_fit(), gui_drawable_make_rect(vec4(0.1f, 0.1f, 0.7f, 1.0f)));
        if (editor.tabs.size > 1)
        {
            gui_node_set_layout(tabs_container, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::MIN);
            gui_node_set_padding(tabs_container, 2, 2, true);
            for (int i = 0; i < editor.tabs.size; i++)
            {
                auto& tab = editor.tabs[i];
                String name = tab.code->file_path;
                int start = 0;
                int end = name.size;

                Optional<int> path_found = string_find_character_index_reverse(&name, '/', name.size - 1);
                if (path_found.available) {
                    start = path_found.value + 1;
                }
                path_found = string_find_character_index_reverse(&name, '\\', name.size - 1);
                if (path_found.available) {
                    if (path_found.value + 1 > start) {
                        start = path_found.value + 1;
                    }
                }
                if (string_ends_with(name.characters, ".upp")) {
                    end = name.size - 4;
                }
                name = string_create_substring_static(&name, start, end);

                vec4 bg_color = vec4(0.3f, 0.3f, 0.3f, 1.0f);
                if (editor.open_tab_index == i) {
                    bg_color = vec4(0.8f, 0.4f, 0.1f, 1.0f);
                }
                auto container = gui_add_node(tabs_container, gui_size_make_fit(), gui_size_make_fit(), gui_drawable_make_rect(bg_color, 2, vec4(0.2f, 0.2f, 0.2f, 1.0f)));
                gui_node_set_layout(container, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::CENTER);
                gui_node_set_padding(container, 2, 2);
                gui_node_enable_input(container);
                if (container.mouse_hover && editor.input->mouse_pressed[(int)Mouse_Key_Code::LEFT]) {
                    if (editor.input->key_down[(int)Key_Code::CTRL]) {
                        if (editor.main_tab_index == i) {
                            editor.main_tab_index = -1;
                        }
                        else {
                            editor.main_tab_index = i;
                            editor.code_changed_since_last_compile = true;
                            syntax_editor_synchronize_with_compiler(false);
                        }
                    }
                    else {
                        syntax_editor_switch_tab(i);
                    }
                }

                if (editor.main_tab_index == i) {
                    gui_add_node(container, gui_size_make_fixed(2), gui_size_make_fixed(1), gui_drawable_make_none()); // Padding
                    gui_add_node(container, gui_size_make_fixed(5), gui_size_make_fixed(5), gui_drawable_make_rect(vec4(1.0f, 0.8f, 0.0f, 1.0f), 0, vec4(0, 0, 0, 1), 2));
                    gui_add_node(container, gui_size_make_fixed(2), gui_size_make_fixed(1), gui_drawable_make_none()); // Padding
                }
                gui_push_text(container, name, vec4(1.0f));

                gui_add_node(container, gui_size_make_fixed(2), gui_size_make_fixed(1), gui_drawable_make_none()); // Padding
                auto rm_button = gui_add_node(container, gui_size_make_fixed(8), gui_size_make_fixed(8), gui_drawable_make_rect(vec4(0.8f, 0.0f, 0.0f, 1.0f)));
                gui_add_node(container, gui_size_make_fixed(2), gui_size_make_fixed(1), gui_drawable_make_none()); // Padding
                gui_node_enable_input(rm_button);
                bool should_delete = false;
                if (rm_button.mouse_hover && editor.input->mouse_pressed[(int)Mouse_Key_Code::LEFT]) {
                    should_delete = true;
                }
                if (should_delete) {
                    syntax_editor_close_tab(editor.open_tab_index);
                    i -= 1;
                }
            }
        }
        auto code_node = gui_add_node(root_node, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_none());
        editor.code_box = gui_node_get_previous_frame_box(code_node);
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



// Syntax Highlighting
void syntax_highlighting_mark_range(Token_Range range, vec3 normal_color, vec3 empty_range_color, Mark_Type mark_type)
{
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto code = tab.code;

    auto index = range.start;
    auto end = range.end;

    // Handle empty ranges
    assert(token_index_compare(index, end) >= 0, "token indices must be in order");

    if (token_index_equal(index, end))
    {
        auto line = source_code_get_line(code, index.line);
        int line_index = range.start.line - tab.cam_start;
        // Draw range at end of line
        if (index.token >= line->tokens.size) {
            Rich_Text::mark_line(&syntax_editor.editor_text, mark_type, empty_range_color, line_index, line->text.size, line->text.size + 1);
            return;
        }

        // Otherwise draw range one after cursor?
        auto& token = line->tokens[index.token];
        Rich_Text::mark_line(&syntax_editor.editor_text, mark_type, empty_range_color, line_index, token.start_index, token.start_index + 1);
        return;
    }

    // Otherwise draw mark on all affected lines
    bool quit_loop = false;
    for (int i = range.start.line; i <= range.end.line && i < code->line_count; i += 1)
    {
        auto line = source_code_get_line(code, i);
        int char_start = 0;
        int char_end = line->text.size;
        if (i == range.start.line && range.start.token < line->tokens.size) {
            char_start = line->tokens[range.start.token].start_index;
        }
        if (i == range.end.line && range.end.token - 1 < line->tokens.size) {
            if (range.end.token - 1 >= 0) {
                char_end = line->tokens[range.end.token - 1].end_index;
            }
            else {
                char_end = 0;
            }
        }
        if (char_start >= char_end) continue;
        Rich_Text::mark_line(&syntax_editor.editor_text, mark_type, normal_color, i - tab.cam_start, char_start, char_end);
    }
}

void syntax_highlighting_mark_section(AST::Node* base, Parser::Section section, vec3 normal_color, vec3 empty_range_color, Mark_Type mark_type)
{
    auto& editor = syntax_editor;
    auto& tab = editor.tabs[editor.open_tab_index];
    auto code = tab.code;

    assert(base != 0, "");
    auto ranges = syntax_editor.token_range_buffer;
    dynamic_array_reset(&ranges);

    auto& node = base;
    Parser::ast_base_get_section_token_range(code, node, section, &ranges);
    for (int i = 0; i < ranges.size; i++) {
        syntax_highlighting_mark_range(ranges[i], normal_color, empty_range_color, mark_type);
    }
}

void syntax_highlighting_highlight_identifiers_recursive(AST::Node* base)
{
    // Highlight symbols
    Symbol* symbol = code_query_get_ast_node_symbol(base);
    if (symbol != 0) {
        vec3 color = symbol_type_to_color(symbol->type);
        syntax_highlighting_mark_section(base, Parser::Section::IDENTIFIER, color, color, Mark_Type::TEXT_COLOR);
    }

    // Highlight member-accesses
    if (base->type == AST::Node_Type::EXPRESSION)
    {
        auto expr = downcast<AST::Expression>(base);
        auto pass = code_query_get_analysis_pass(base);
        if (expr->type == AST::Expression_Type::MEMBER_ACCESS && pass != 0)
        {
            auto info = pass_get_node_info(pass, expr, Info_Query::TRY_READ);
            if (info != nullptr) 
            {
                vec3 color;
                bool mark = false;
                switch (info->specifics.member_access.type)
                {
                case Member_Access_Type::DOT_CALL: 
                case Member_Access_Type::DOT_CALL_AS_MEMBER: mark = true; color = Syntax_Color::FUNCTION; break;
                case Member_Access_Type::STRUCT_MEMBER_ACCESS: 
                case Member_Access_Type::STRUCT_UP_OR_DOWNCAST:
                case Member_Access_Type::STRUCT_POLYMORHPIC_PARAMETER_ACCESS: mark = true; color = Syntax_Color::MEMBER; break;
                case Member_Access_Type::STRUCT_SUBTYPE: mark = true; color = Syntax_Color::SUBTYPE; break;
                case Member_Access_Type::ENUM_MEMBER_ACCESS: mark = true; color = Syntax_Color::ENUM_MEMBER; break;
                default: panic("");
                }

                if (mark) {
                    syntax_highlighting_mark_section(base, Parser::Section::END_TOKEN, color, color, Mark_Type::TEXT_COLOR);
                }
            }
        }
        else if (expr->type == AST::Expression_Type::AUTO_ENUM) {
            vec3 color = Syntax_Color::ENUM_MEMBER;
            syntax_highlighting_mark_section(base, Parser::Section::END_TOKEN, color, color, Mark_Type::TEXT_COLOR);
        }
    }

    // Highlight struct content
    if (base->type == AST::Node_Type::STRUCT_MEMBER) {
        auto member = downcast<AST::Structure_Member_Node>(base);
        vec3 color = member->is_expression ? Syntax_Color::MEMBER : Syntax_Color::SUBTYPE;
        syntax_highlighting_mark_section(base, Parser::Section::FIRST_TOKEN, color, color, Mark_Type::TEXT_COLOR);
    }

    // Highlight parameters
    if (base->type == AST::Node_Type::PARAMETER) {
        vec3 color = Syntax_Color::VARIABLE;
        syntax_highlighting_mark_section(base, Parser::Section::FIRST_TOKEN, color, color, Mark_Type::TEXT_COLOR);
    }

    // Highlight enum-members
    if (base->type == AST::Node_Type::ENUM_MEMBER) {
        vec3 color = Syntax_Color::ENUM_MEMBER;
        syntax_highlighting_mark_section(base, Parser::Section::FIRST_TOKEN, color, color, Mark_Type::TEXT_COLOR);
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



// Rendering
void syntax_editor_render()
{
    auto& editor = syntax_editor;

    // Prepare Render
    syntax_editor_sanitize_cursor();

    auto state_2D = pipeline_state_make_alpha_blending();
    auto pass_context = rendering_core_query_renderpass("Context pass", state_2D, 0);
    auto pass_2D = rendering_core_query_renderpass("2D state", state_2D, 0);
    render_pass_add_dependency(pass_2D, rendering_core.predefined.main_pass);
    render_pass_add_dependency(pass_context, pass_2D);

    // Draw tabs with gui
    auto& tab = editor.tabs[editor.open_tab_index];
    auto code = tab.code;
    auto& cursor = tab.cursor;

    // Calculate camera start
    auto& cam_start = tab.cam_start;
    auto& cam_end = tab.cam_end;
    auto& code_box = editor.code_box;
    {
        int line_count = (code_box.max.y - code_box.min.y) / editor.text_display.char_size.y;
        cam_end = cam_start + line_count;

        // Clamp camera to cursor if cursor moved or text changed
        History_Timestamp timestamp = history_get_timestamp(&tab.history);
        if (!text_index_equal(tab.last_render_cursor_pos, cursor) || tab.last_render_timestamp.node_index != timestamp.node_index) 
        {
            tab.last_render_cursor_pos = cursor;
            tab.last_render_timestamp = timestamp;

            auto cursor_line = cursor.line;
            if (cursor_line - MIN_CURSOR_DISTANCE < cam_start) {
                cam_start = cursor_line - MIN_CURSOR_DISTANCE;
            }
            else if (cursor_line + MIN_CURSOR_DISTANCE > cam_end) {
                cam_start = cursor_line - line_count + MIN_CURSOR_DISTANCE;
            }
        }

        cam_start = math_clamp(cam_start, 0, code->line_count - 1);
        cam_end = cam_start + line_count;
    }

    // Push Source-Code into Rich-Text
    Text_Display::set_frame(&editor.text_display, code_box.min, Anchor::BOTTOM_LEFT, code_box.max - code_box.min);
    Rich_Text::reset(&editor.editor_text);
    for (int i = cam_start; i < code->line_count && i < cam_end + 1; i++)
    {
        Source_Line* line = source_code_get_line(code, i);
        int screen_index = i - cam_start;
        auto text = &editor.editor_text;

        // Push line I guess? Or push token by token?
        Rich_Text::add_line(text, false, line->indentation);
        Rich_Text::append(text, line->text);
        Rich_Text::append(text, " "); // So that we can have a underline if something is missing at end of line

        // Color tokens based on type (Initial coloring, syntax highlighting is done later)
        for (int j = 0; j < line->tokens.size; j++)
        {
            auto& token = line->tokens[j];

            vec3 color = Syntax_Color::TEXT;
            switch (token.type)
            {
            case Token_Type::COMMENT: color = Syntax_Color::COMMENT; break;
            case Token_Type::INVALID: color = vec3(1.0f, 0.8f, 0.8f); break;
            case Token_Type::KEYWORD: color = Syntax_Color::KEYWORD; break;
            case Token_Type::IDENTIFIER: color = Syntax_Color::IDENTIFIER_FALLBACK; break;
            case Token_Type::LITERAL: {
                switch (token.options.literal_value.type)
                {
                case Literal_Type::BOOLEAN: color = vec3(0.5f, 0.5f, 1.0f); break;
                case Literal_Type::STRING: color = Syntax_Color::STRING; break;
                case Literal_Type::INTEGER:
                case Literal_Type::FLOAT_VAL:
                case Literal_Type::NULL_VAL:
                    color = Syntax_Color::LITERAL_NUMBER; break;
                default: panic("");
                }
                break;
            }
            default: continue;
            }

            Rich_Text::line_set_text_color_range(text, color, screen_index, token.start_index, token.end_index);
        }
    }

    // Find objects under cursor for Syntax highlighting
    Token_Index cursor_token_index = token_index_make(cursor.line, get_cursor_token_index(true));
    AST::Node* node = Parser::find_smallest_enclosing_node(upcast(code->root), cursor_token_index);
    Analysis_Pass* pass = code_query_get_analysis_pass(node); // Note: May be null?
    Symbol* symbol = code_query_get_ast_node_symbol(node); // May be null

    // Syntax Highlighting
    if (true)
    {
        syntax_highlighting_highlight_identifiers_recursive(upcast(code->root));

        // Highlight selected symbol occurances
        if (symbol != 0 && editor.mode != Editor_Mode::FUZZY_FIND_DEFINITION)
        {
            // Highlight all instances of the symbol
            vec3 color = vec3(1.0f, 1.0f, 0.3f) * 0.3f;
            for (int i = 0; i < symbol->references.size; i++) {
                auto node = &symbol->references[i]->base;
                if (compiler_find_ast_source_code(node) != code)
                    continue;
                syntax_highlighting_mark_section(node, Parser::Section::IDENTIFIER, color, color, Mark_Type::BACKGROUND_COLOR);
            }

            // Highlight Definition
            if (symbol->definition_node != 0 && compiler_find_ast_source_code(symbol->definition_node) == code) {
                syntax_highlighting_mark_section(symbol->definition_node, Parser::Section::IDENTIFIER, color, color, Mark_Type::BACKGROUND_COLOR);
            }
        }

        if (editor.mode == Editor_Mode::FUZZY_FIND_DEFINITION && editor.fuzzy_find_suggestions.size > 0) {
            auto symbol = editor.fuzzy_find_suggestions[0];
            Source_Code* code = compiler_find_ast_source_code(symbol->definition_node);
            if (code == tab.code) {
                vec3 color = vec3(1.0f, 1.0f, 0.3f) * 0.3f;
                syntax_highlighting_mark_range(symbol->definition_node->range, color, color, Rich_Text::Mark_Type::BACKGROUND_COLOR);
            }
        }

        // Error messages 
        for (int i = 0; i < editor.errors.size; i++)
        {
            auto& error = editor.errors[i];
            if (error.code != tab.code) continue;
            syntax_highlighting_mark_range(error.range, vec3(1.0f, 0.0f, 0.0f), vec3(1.0f, 1.0f, 0.0f), Mark_Type::UNDERLINE);
        }
    }

    // Set cursor text-background
    if (editor.mode == Editor_Mode::NORMAL) {
        Rich_Text::mark_line(&editor.editor_text, Mark_Type::BACKGROUND_COLOR, vec3(0.25f), cursor.line - cam_start, cursor.character, cursor.character + 1);
    }

    // Render Code
    Text_Display::render(&editor.text_display, pass_2D);

    // Draw Cursor
    if (true)
    {
        auto display = &syntax_editor.text_display;
        const int t = 2;
        vec2 min = Text_Display::get_char_position(display, cursor.line - cam_start, cursor.character, Anchor::BOTTOM_LEFT);
        vec2 max = min + vec2((float)t, display->char_size.y);
        min = min + vec2(-t, 0);
        max = max + vec2(-t, 0);

        renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_min_max(min, max), Syntax_Color::COMMENT);
        if (editor.mode != Editor_Mode::INSERT) {
            vec2 offset = vec2(display->char_size.x + t, 0.0f);
            renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_min_max(min + offset, max + offset), Syntax_Color::COMMENT);

            int l = 2;
            renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_min_max(min + vec2(t, 0), min + vec2(t + l, t)), Syntax_Color::COMMENT);
            renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_min_max(max - vec2(t+l, t) + offset, max - vec2(t, 0) + offset), Syntax_Color::COMMENT);
        }
        renderer_2D_draw(editor.renderer_2D, pass_2D);
    }

    // Calculate context
    Rich_Text::Rich_Text context_text = Rich_Text::create(vec3(1));
    SCOPE_EXIT(Rich_Text::destroy(&context_text));
    Rich_Text::Rich_Text call_info_text = Rich_Text::create(vec3(1));
    SCOPE_EXIT(Rich_Text::destroy(&context_text));
    if (editor.mode != Editor_Mode::FUZZY_FIND_DEFINITION)
    {
        Rich_Text::Rich_Text* text = &context_text;

        // If we are in Insert-Mode, prioritize code-completion
        bool show_normal_mode_context = true;
        if (editor.code_completion_suggestions.size != 0 && editor.mode == Editor_Mode::INSERT)
        {
            Rich_Text::add_seperator_line(text);
            show_normal_mode_context = false;
            auto& suggestions = syntax_editor.code_completion_suggestions;
            for (int i = 0; i < editor.code_completion_suggestions.size; i++) {
                auto sugg = editor.code_completion_suggestions[i];
                Rich_Text::add_line(text);
                Rich_Text::append(text, sugg);
            }
        }

        // Error messages (If cursor is directly on error)
        if (editor.errors.size > 0 && show_normal_mode_context && editor.mode == Editor_Mode::NORMAL)
        {
            bool first_error = true;
            for (int i = 0; i < editor.errors.size; i++)
            {
                auto& error = editor.errors[i];
                if (error.code != tab.code) continue;

                auto line = source_code_get_line(code, cursor.line);
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

                if (inside_error) 
                {
                    show_normal_mode_context = false;
                    if (first_error) {
                        Rich_Text::add_seperator_line(text);
                        first_error = false;
                    }

                    Rich_Text::add_line(text);
                    Rich_Text::set_text_color(text, vec3(1.0f, 0.5f, 0.5f));
                    Rich_Text::set_underline(text, vec3(1.0f, 0.5f, 0.5f));
                    Rich_Text::append(text, "Error:");
                    Rich_Text::set_text_color(text);
                    Rich_Text::append(text, " ");
                    Rich_Text::append(text, error.message);

                    // Add error infos
                    if (error.semantic_error_index != -1) {
                        auto& semantic_error = compiler.semantic_analyser->errors[error.semantic_error_index];
                        for (int j = 0; j < semantic_error.information.size; j++) {
                            auto& error_info = semantic_error.information[j];
                            add_line(text, false, 1);
                            error_information_append_to_rich_text(error_info, text);
                        }
                    }
                }
            }
        }

        // Symbol Info
        if (show_normal_mode_context && symbol != 0 && pass != 0)
        {
            Rich_Text::add_seperator_line(text);
            Rich_Text::add_line(text);

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
                datatype_append_to_rich_text(type, text);
                Rich_Text::set_text_color(text);
            }

            if (symbol->type != Symbol_Type::TYPE)
            {
                Rich_Text::add_line(text, false, 2);
                Rich_Text::set_text_color(text, symbol_type_to_color(symbol->type));
                Rich_Text::append(text, *symbol->id);

                if (after_text != 0) {
                    Rich_Text::set_text_color(text);
                    Rich_Text::append(text, ": ");
                    Rich_Text::append(text, after_text);
                }
            }
        }

        // Analysis-Info
        if (node->type == AST::Node_Type::EXPRESSION && pass != 0 && show_normal_mode_context)
        {
            auto expr = AST::downcast<AST::Expression>(node);
            auto expression_info = pass_get_node_info(pass, expr, Info_Query::TRY_READ);
            if (expression_info != 0 && !expression_info->contains_errors)
            {
                Rich_Text::add_seperator_line(text);
                Rich_Text::add_line(text);
                Rich_Text::append(text, "Expr: ");
                datatype_append_to_rich_text(expression_info_get_type(expression_info, false), text);
            }
        }

        // Call-info
        AST::Node* iter = node;
        AST::Arguments* args = nullptr;
        AST::Argument* arg = nullptr;
        while (iter != 0) {
            if (iter->type == AST::Node_Type::ARGUMENTS) {
                args = downcast<AST::Arguments>(iter);
                break;
            }
            else if (iter->type == AST::Node_Type::ARGUMENT) {
                arg = downcast<AST::Argument>(iter);
                args = downcast<AST::Arguments>(iter->parent);
                break;
            }
            iter = iter->parent;
        }

        if (args != nullptr)
        {
            int arg_index = -1;
            if (arg != 0) {
                for (int i = 0; i < args->arguments.size; i++) {
                    if (args->arguments[i] == arg) {
                        arg_index = i;
                        break;
                    }
                }
            }

            auto info = pass_get_node_info(pass, args, Info_Query::TRY_READ);
            if (info != nullptr)
            {
                Rich_Text::Rich_Text* text = &call_info_text;
                Rich_Text::add_line(text);

                String* name = nullptr;
                vec3 color = Syntax_Color::IDENTIFIER_FALLBACK;
                switch (info->call_type)
                {
                case Call_Type::FUNCTION: name = info->options.function->name; color = Syntax_Color::FUNCTION; break;
                case Call_Type::DOT_CALL: name = info->options.dot_call_function->name; color = Syntax_Color::FUNCTION; break;
                case Call_Type::STRUCT_INITIALIZER: name = info->options.struct_init.structure->content.name; color = Syntax_Color::TYPE; break;
                }

                bool is_struct_init = info->call_type == Call_Type::STRUCT_INITIALIZER;
                if (name != nullptr) {
                    Rich_Text::set_text_color(text, color);
                    Rich_Text::append(text, *name);
                }
                else {
                    Rich_Text::set_text_color(text, Syntax_Color::IDENTIFIER_FALLBACK);
                    Rich_Text::append(text, "Params:");
                }

                if (is_struct_init) {
                    Rich_Text::append_character(text, '.');
                }
                else {
                    Rich_Text::append_character(text, ' ');
                }

                Rich_Text::set_text_color(text);
                Rich_Text::append(text, is_struct_init ? "{" : "(");
                for (int i = 0; i < info->matched_parameters.size; i += 1) 
                {
                    const auto& param_info = info->matched_parameters[i];

                    bool highlight = param_info.argument_index == arg_index && arg_index != -1;
                    if (highlight) {
                        Rich_Text::set_bg(text, vec3(0.2f, 0.3f, 0.3f));
                        Rich_Text::set_underline(text, vec3(0.8f));
                    }

                    vec3 name_color = Syntax_Color::IDENTIFIER_FALLBACK;
                    if (!param_info.is_set && param_info.required) {
                        name_color = vec3(1.0f, 0.5f, 0.5f);
                    }

                    Rich_Text::set_text_color(text, name_color);
                    Rich_Text::append(text, *param_info.name);
                    if (param_info.param_type != nullptr) {
                        Rich_Text::append(text, ": ");
                        datatype_append_to_rich_text(param_info.param_type, text);
                    }
                    Rich_Text::set_text_color(text, vec3(1.0f));

                    if (highlight) {
                        Rich_Text::stop_bg(text);
                        Rich_Text::stop_underline(text);
                    }
                    if (i != info->matched_parameters.size - 1) {
                        Rich_Text::append(text, ", ");
                    }
                }
                Rich_Text::append(text, is_struct_init ? "}" : ")");
            }
        }
    }

    // Push some example text for testing
    if (false)
    {
        const vec3 COLOR_BG = vec3(0.2f);
        const vec3 COLOR_TEXT = vec3(1.0f);
        const vec3 COLOR_ERROR_TEXT = vec3(1.0f, 0.5f, 0.5f);

        Rich_Text::Rich_Text* text = &context_text;
        Rich_Text::add_line(text);
        Rich_Text::append(text, "Hello there, this is the final thing of the final thingy");
        Rich_Text::mark_line(text, Mark_Type::TEXT_COLOR, COLOR_ERROR_TEXT, 0, 6, 11);
        Rich_Text::mark_line(text, Mark_Type::TEXT_COLOR, COLOR_TEXT, 0, 7, 8);
    }

    // Position context and call_info text
    if (context_text.lines.size > 0 || call_info_text.lines.size > 0)
    {
        const vec3 COLOR_BG = vec3(0.2f);
        const vec3 COLOR_TEXT = vec3(1.0f);
        const vec3 COLOR_ERROR_TEXT = vec3(1.0f, 0.5f, 0.5f);
        const vec3 COLOR_BORDER = vec3(0.5f, 0.0f, 1.0f);

        const int BORDER_SIZE = 2;
        const int PADDING = 2;

        bool draw_context = context_text.lines.size > 0;
        bool draw_call_info = call_info_text.lines.size > 0;

        // Figure out text position
        vec2 char_size = text_renderer_get_aligned_char_size(editor.text_renderer, editor.normal_text_size_pixel * 0.75f);
        Text_Display::Text_Display context_display = Text_Display::make(
            &context_text, editor.renderer_2D, editor.text_renderer, char_size, 2
        );

        vec2 context_size = vec2(0);
        vec2 call_info_size = vec2(0);
        if (draw_context) {
            auto text = &context_text;
            context_size = char_size * vec2(math_maximum(30, text->max_line_char_count), text->lines.size);
            context_size = context_size + 2 * (BORDER_SIZE + PADDING);
        }
        if (draw_call_info) {
            auto text = &call_info_text;
            call_info_size = char_size * vec2(text->max_line_char_count, text->lines.size);
            call_info_size = call_info_size + 2 * (BORDER_SIZE + PADDING);
        }

        // Figure out positioning
        vec2 context_pos = vec2(0.0f);
        vec2 call_info_pos = vec2(0.0f);
        {
            vec2 cursor_pos = Text_Display::get_char_position(&editor.text_display, cursor.line - cam_start, cursor.character, Anchor::BOTTOM_LEFT);
            context_pos.x = cursor_pos.x;
            call_info_pos.x = cursor_pos.x;

            int width = rendering_core.render_information.backbuffer_width;
            int height = rendering_core.render_information.backbuffer_height;
            int box_height = context_size.y + call_info_size.y;

            // Move box above cursor if more space is available there
            int pixels_below = cursor_pos.y;
            int pixels_above = height - cursor_pos.y - char_size.y;
            if (pixels_below >= box_height || pixels_below > pixels_above) {
                // Place below cursor
                call_info_pos.y = cursor_pos.y;
                context_pos.y = cursor_pos.y - call_info_size.y;
            }
            else {
                // Place above cursor
                call_info_pos.y = cursor_pos.y + char_size.y + call_info_size.y;
                context_pos.y = call_info_pos.y + context_pos.y;
            }
        }

        if (draw_context) {
            Text_Display::set_background_color(&context_display, COLOR_BG);
            Text_Display::set_border(&context_display, BORDER_SIZE, COLOR_BORDER);
            Text_Display::set_padding(&context_display, PADDING);
            Text_Display::set_frame(&context_display, context_pos, Anchor::TOP_LEFT, context_size);
            Text_Display::render(&context_display, pass_context);
        }
        if (draw_call_info) {
            Text_Display::Text_Display call_display = Text_Display::make(
                &call_info_text, editor.renderer_2D, editor.text_renderer, char_size, 2
            );
            Text_Display::set_background_color(&call_display, COLOR_BG);
            Text_Display::set_border(&call_display, BORDER_SIZE, COLOR_BORDER);
            Text_Display::set_padding(&call_display, PADDING);
            Text_Display::set_frame(&call_display, call_info_pos, Anchor::TOP_LEFT, call_info_size);
            Text_Display::render(&call_display, pass_context);
        }
    }

    // Show completable command
    if (editor.command_buffer.size > 0) 
    {
        Rich_Text::Rich_Text rich_text = Rich_Text::create(vec3(1));
        SCOPE_EXIT(Rich_Text::destroy(&rich_text));
        Rich_Text::add_line(&rich_text);
        Rich_Text::append(&rich_text, editor.command_buffer);

        vec2 pos = Text_Display::get_char_position(&editor.text_display, cursor.line - cam_start, cursor.character, Anchor::TOP_RIGHT);
        pos.x += 4;

        vec2 char_size = text_renderer_get_aligned_char_size(editor.text_renderer, editor.normal_text_size_pixel * 0.6f);
        Text_Display::Text_Display display = Text_Display::make(
            &rich_text, editor.renderer_2D, editor.text_renderer, char_size, 2
        );
        vec2 size = display.char_size * vec2(editor.command_buffer.size, 1) + 2 * (1 + 1);
        Text_Display::set_background_color(&display, vec3(0.2f));
        Text_Display::set_border(&display, 1, vec3(0.3f));
        Text_Display::set_padding(&display, 1);
        Text_Display::set_frame(&display, pos, Anchor::TOP_LEFT, size);
        Text_Display::render(&display, pass_context);
    }

    // Draw Fuzzy-Find
    if (editor.mode == Editor_Mode::FUZZY_FIND_DEFINITION)
    {
        auto& line_edit = editor.fuzzy_line_edit;

        Rich_Text::Rich_Text rich_text = Rich_Text::create(vec3(1));
        SCOPE_EXIT(Rich_Text::destroy(&rich_text));
        Rich_Text::add_line(&rich_text);
        Rich_Text::append(&rich_text, editor.fuzzy_find_search);

        // Draw highlighted
        if (line_edit.pos != line_edit.select_start) {
            int start = math_minimum(line_edit.pos, line_edit.select_start);
            int end = math_maximum(line_edit.pos, line_edit.select_start);
            Rich_Text::mark_line(&rich_text, Rich_Text::Mark_Type::BACKGROUND_COLOR, vec3(0.3f), 0, start, end);
        }

        // Push suggestions
        if (editor.fuzzy_find_suggestions.size > 0) 
        {
            Rich_Text::add_seperator_line(&rich_text);
            for (int i = 0; i < editor.fuzzy_find_suggestions.size; i++) 
            {
                auto symbol = editor.fuzzy_find_suggestions[i];
                Rich_Text::add_line(&rich_text);
                vec3 color = symbol_type_to_color(symbol->type);
                Rich_Text::set_text_color(&rich_text, color);
                if (i == 0) {
                    Rich_Text::set_bg(&rich_text, vec3(0.3f));
                    Rich_Text::set_underline(&rich_text, Syntax_Color::STRING);
                }
                else {
                    Rich_Text::set_bg(&rich_text, vec3(0.08f));
                }
                Rich_Text::append(&rich_text, *editor.fuzzy_find_suggestions[i]->id);

                Rich_Text::set_text_color(&rich_text);
                Rich_Text::append(&rich_text, ": ");
                String* string = Rich_Text::start_line_manipulation(&rich_text);
                symbol_type_append_to_string(symbol->type, string);
                Rich_Text::stop_line_manipulation(&rich_text);
            }
        }

        // Create display and render
        vec2 char_size = editor.text_display.char_size;
        Text_Display::Text_Display display = Text_Display::make(
            &rich_text, editor.renderer_2D, editor.text_renderer, char_size, 2
        );
        Text_Display::set_border(&display, 0, vec3(1.0f));

        int width = rendering_core.render_information.backbuffer_width;
        int height = rendering_core.render_information.backbuffer_height;

        int length = ((int)(width / 2) / (int)char_size.x) * char_size.x;
        vec2 size = vec2((float)length, char_size.y * rich_text.lines.size);
        vec2 pos = vec2(width / 2 - length / 2, height - 30);
        Text_Display::set_frame(&display, pos, Anchor::TOP_LEFT, size);
        Text_Display::set_background_color(&display, vec3(0.5f));
        Text_Display::render(&display, pass_2D);

        // Draw cursor
        const int t = 2;
        vec2 min = Text_Display::get_char_position(&display, 0, line_edit.pos, Anchor::BOTTOM_LEFT);
        vec2 max = min + vec2((float)t, char_size.y);
        renderer_2D_add_rectangle(editor.renderer_2D, bounding_box_2_make_min_max(min, max), Syntax_Color::COMMENT);
        renderer_2D_draw(editor.renderer_2D, pass_2D);
    }

    // Render gui
    gui_update_and_render(pass_2D);
}



