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
#include "../../utility/directory_crawler.hpp"
#include "../../win32/thread.hpp"
#include "../../win32/process.hpp"
#include "../../win32/window.hpp"

#include "../../win32/input.hpp"
#include "syntax_colors.hpp"
#include "compiler.hpp"
#include "ast.hpp"
#include "symbol_table.hpp"
#include "semantic_analyser.hpp"
#include "parser.hpp"
#include "source_code.hpp"
#include "code_history.hpp"
#include "editor_analysis_info.hpp"
#include "debugger.hpp"

#include "ir_code.hpp"

#include "../../utility/rich_text.hpp"
#include "../../utility/line_edit.hpp"
#include "../../utility/ui_system.hpp"

const int MIN_CURSOR_DISTANCE = 3;
using Rich_Text::Mark_Type;

// Structures/Enums
struct Error_Display
{
    String message;
    Token_Range range;

    Compilation_Unit* unit;
    bool is_token_range_duplicate;
    int semantic_error_index; // -1 if parsing error
};



// Motions/Movements and Commands
enum class Movement_Type
{
    SEARCH_FORWARDS_TO, // t
    SEARCH_FORWARDS_FOR, // f
    SEARCH_BACKWARDS_TO, // T
    SEARCH_BACKWARDS_FOR, // F
    REPEAT_TEXT_SEARCH, // n
    REPEAT_TEXT_SEARCH_REVERSE, // N
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
    PARAGRAPH_END, // }
    PARAGRAPH_START, // {
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
    BLOCK, // b or B
    PARAGRAPH, // p or P
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

enum class Normal_Command_Type
{
    MOVEMENT,
    ENTER_INSERT_MODE_AFTER_MOVEMENT, // i, a, I, A
    ENTER_INSERT_MODE_NEW_LINE_BELOW, // o
    ENTER_INSERT_MODE_NEW_LINE_ABOVE, // O
    DELETE_MOTION, // d [m] or D/dd
    CHANGE_MOTION, // c [m] or C/cc
    REPLACE_CHAR, // r
    REPLACE_MOTION_WITH_YANK, // R
	FORMAT_MOTION, // =

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
    GOTO_NEXT_TAB, // gt
    GOTO_PREV_TAB, // gT
    GOTO_DEFINITION, // F12
    CLOSE_TAB, // :q or wq

    // Folding
    FOLD_CURRENT_BLOCK, // gb
    FOLD_HIGHER_INDENT_IN_BLOCK, // gf
    UNFOLD_IN_BLOCK, // gF

    // Others
    ENTER_VISUAL_BLOCK_MODE,
    ENTER_FUZZY_FIND_DEFINITION,
    ENTER_SHOW_ERROR_MODE,
    ENTER_TEXT_SEARCH, // / 
    ENTER_TEXT_SEARCH_REVERSE, // ?
    SEARCH_IDENTIFER_UNDER_CURSOR, // *
    VISUALIZE_MOTION, // not sure
    GOTO_LAST_JUMP, // Ctrl-O
    GOTO_NEXT_JUMP, // Ctrl-I
    ADD_INDENTATION, // >
    REMOVE_INDENTATION, // >
    TOGGLE_LINE_BREAKPOINT, // gp

    MAX_ENUM_VALUE
};

struct Normal_Mode_Command
{
    Normal_Command_Type type;
    int repeat_count;
    union {
        Motion motion;
        Movement movement;
        char character;
    } options;
};

enum class Editor_Mode
{
    NORMAL,
    INSERT,
    FUZZY_FIND_DEFINITION,
    TEXT_SEARCH,
    VISUAL_BLOCK,
    ERROR_NAVIGATION,
};

struct Code_Fold
{
    int line_start;
    int line_end; // Inclusive
    int indentation;
};

struct Line_Breakpoint
{
    int line_number;
    Source_Breakpoint* src_breakpoint; 
    bool enabled; // This is toggle via UI, but I'm not sure if it's already implemented
};

struct Editor_Tab
{
    Source_Code* code; // Note: This is different than the code in the compilation unit
    Code_History history;
    Compilation_Unit* compilation_unit;
    History_Timestamp last_code_info_synch;
    History_Timestamp last_compiler_synchronized;
    int last_code_completion_info_index;
    Text_Index last_code_completion_query_pos;
    bool requires_recompile; // E.g. when loaded from a file, or otherwise modified

    Dynamic_Array<Code_Fold> folds;
    Dynamic_Array<Line_Breakpoint> breakpoints;

    // Cursor and camera
    Text_Index cursor;
    int last_line_x_pos; // Position with indentation * 4 for up/down movements

    int cam_start;
    int cam_end;

    History_Timestamp last_render_timestamp;
    Text_Index last_render_cursor_pos;

    Dynamic_Array<Text_Index> jump_list;
    int last_jump_index;
};

enum class Suggestion_Type
{
    ID,
    STRUCT_MEMBER,
    ENUM_MEMBER,
    SYMBOL,
    FILE
};

struct Editor_Suggestion
{
    Suggestion_Type type;
    String* text;
    union {
        struct {
            Datatype_Struct* structure;
            Datatype* member_type;
        } struct_member;
        struct {
            Datatype_Enum* enumeration;
        } enum_member;
        Symbol* symbol;
        int file_index_in_crawler;
        struct {
            float r, g, b;
        } id_color;
    } options; 
};

struct Particle
{
    vec2 position;
    vec2 velocity;
    float radius;
    vec3 color;
    float creation_time;
    float life_time;
};

struct Watch_Value
{
    String name;
    String value_as_text;
	bool show_multiline;
};

struct Pass_Filter
{
	String required_substring;
	bool is_active;
	bool connected_to_next;
};

struct Syntax_Editor
{
    // Editing
    Window* window;
    Editor_Mode mode;
    Dynamic_Array<Editor_Tab> tabs;
    int open_tab_index;
    int main_tab_index; // If -1, use the currently open tab for compiling
    float normal_text_size_pixel;
    int compile_count;

    bool last_compile_was_with_code_gen;
    Compilation_Unit* last_compile_main_unit;

    String yank_string;
    bool yank_was_line;

    // Compiler Info
    Compiler_Analysis_Data* analysis_data;

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

    // Search and Fuzzy-Find
    String fuzzy_search_text;
    Line_Editor search_text_edit;
    int last_code_completion_tab;
    Dynamic_Array<Editor_Suggestion> suggestions; // Used both for fuzzy-find and code-completion
    Directory_Crawler* directory_crawler;
    Hashset<Symbol_Table*> symbol_table_already_visited;

    String search_text;
    Text_Index search_start_pos;
    int search_start_cam_start;
    bool search_reverse;
    bool last_insert_was_shift_enter;

    // Misc
    int visible_line_count;
    int visual_block_start_line;

    Dynamic_Array<int> error_indices_sorted;
    Text_Index navigate_error_mode_cursor_before;
    int navigate_error_mode_tab_before;
    int navigate_error_cam_start;
    int navigate_error_index;

    bool show_semantic_infos;

    // Rendering
    Dynamic_Array<Particle> particles;
    double last_update_time;
    Random random;

    Bounding_Box2 code_box;
    Input* input;
    Rendering_Core* rendering_core;
    Renderer_2D* renderer_2D;
    Text_Renderer* text_renderer;
    int frame_index;

    // Compiler thread
    Thread compiler_thread;
    Semaphore compiler_wait_semaphore;
    Semaphore compilation_finish_semaphore;
    bool compiler_thread_should_close;
    bool compiler_work_started;

    bool compiler_build_code;
    Compilation_Unit* compiler_main_unit;

    // Debugger
    Debugger* debugger;
    Dynamic_Array<Watch_Value> watch_values;
    int selected_stack_frame;

	// Pass-Filters
	bool prefer_poly_passes;
	bool prefer_base_pass;
	bool pass_filter_ui_open;
	Dynamic_Array<Pass_Filter> poly_pass_filters;
	Hashtable<Analysis_Pass*, bool> filtered_passes;
};



// Code-Diff
struct Line_Diff_Item
{
	bool is_insert; // otherwise delete
	int char_index;
	int length;
};

struct Line_Diff
{
	int new_line_index;
	Dynamic_Array<Line_Diff_Item> items;
	bool indent_changed;
};

struct Line_Insert_Or_Delete
{
	int line_index;
	bool is_insert;
};

struct Code_Diff
{
	Dynamic_Array<Line_Insert_Or_Delete> line_inserts_and_deletes;
	Dynamic_Array<Line_Diff> line_diffs;
};



// Globals
static Syntax_Editor syntax_editor;

// Prototypes
void syntax_editor_synchronize_with_compiler(bool generate_code);
void normal_command_execute(Normal_Mode_Command& command);
void syntax_editor_save_text_file();
void syntax_editor_update_line_visible_and_fold_info(int tab_index);
Text_Range motion_evaluate(const Motion& motion, Text_Index pos);
Error_Display error_display_make(String msg, Token_Range range, Compilation_Unit* unit, bool is_token_range_duplicate, int semantic_error_index);
void syntax_editor_sanitize_cursor();
Text_Index code_query_text_index_at_last_synchronize(Text_Index text_index, int tab_index, bool move_forwards_in_time);
unsigned long compiler_thread_entry_fn(void* userdata);


// Helper Namespaces
namespace Line_Movement
{
    int move_lines_up_or_down(int line_index, int steps)
    {
        auto& code = syntax_editor.tabs[syntax_editor.open_tab_index].code;
        int new_index = line_index + steps;
        new_index = math_clamp(new_index, 0, code->line_count - 1);
        return new_index;
    }

    typedef bool (*line_condition_fn)(Source_Line* line, int cond_value);

    int move_while_condition(int line_index, int dir, line_condition_fn condition, bool invert_condition, int cond_value, bool move_out_of_condition)
    {
        auto& code = syntax_editor.tabs[syntax_editor.open_tab_index].code;
        line_index = math_clamp(line_index, 0, code->line_count - 1);
        auto line = source_code_get_line(code, line_index);
        bool cond = condition(line, cond_value);
        if (invert_condition) {
            cond = !cond;
        }
        if (!cond) return line_index;

        if (dir > 0) dir = 1;
        else dir = -1;

        while (true)
        {
            int next_line_index = line_index + dir;
            if (next_line_index < 0) {
                return 0;
            }
            else if (next_line_index >= code->line_count) {
                return next_line_index - 1;
            }
            auto next_line = source_code_get_line(code, next_line_index);

            bool cond = condition(next_line, cond_value);
            if (invert_condition) {
                cond = !cond;
            }
            if (!cond) {
                return move_out_of_condition ? next_line_index : line_index;
            }
            line_index = next_line_index;
        }

        panic("");
        return 0;
    }

    int move_visible_lines_up_or_down(int line_index, int steps)
    {
        auto& tab = syntax_editor.tabs[syntax_editor.open_tab_index];
        auto& code = tab.code;

        int dir = steps >= 0 ? 1 : -1;
        line_index = math_clamp(line_index, 0, code->line_count - 1);
        for (int i = 0; i < math_absolute(steps); i++)
        {
            if (line_index < 0) {
                return 0;
            }
            else if (line_index >= code->line_count) {
                return code->line_count - 1;
            }

            // Step
            auto line = source_code_get_line(code, line_index);
            if (line->is_folded)
            {
                auto& fold = tab.folds[line->fold_index];
                if (dir > 0) {
                    line_index = fold.line_end + 1;
                }
                else {
                    line_index = fold.line_start - 1;
                }
            }
            else {
                line_index += dir;
            }
        }
        line_index = math_clamp(line_index, 0, code->line_count - 1);
        return line_index;
    }

    int move_to_fold_boundary(int line_index, int dir, bool move_out_of_fold)
    {
        auto& tab = syntax_editor.tabs[syntax_editor.open_tab_index];
        auto& code = tab.code;
        auto line = source_code_get_line(code, math_clamp(line_index, 0, code->line_count - 1));
        if (line->is_folded) {
            auto& fold = tab.folds[line->fold_index];
            if (dir >= 0) {
                line_index = fold.line_end + (move_out_of_fold ? 1 : 0);
            }
            else {
                line_index = fold.line_start - (move_out_of_fold ? 1 : 0);
            }
        }
        return math_clamp(line_index, 0, code->line_count - 1);
    }

    int move_to_block_boundary(int line_index, int dir, bool move_outside_block, int block_indent) {
        auto inside_block = [](Source_Line* line, int block_indent) -> bool { return line->indentation >= block_indent; };
        return move_while_condition(line_index, dir, inside_block, false, block_indent, move_outside_block);
    }
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

    int move_while_condition(String text, int char_index, bool forward, char_test_fn condition, void* userdata, bool invert_condition, bool move_out_of_condition)
    {
        if (text.size == 0) return 0;
        char_index = math_clamp(char_index, 0, text.size);
        bool cond = condition(text.characters[char_index], condition);
        if (invert_condition) {
            cond = !cond;
        }
        if (!cond) return char_index;

        const int dir = forward ? 1 : -1;
        while (true)
        {
            int next_char_index = char_index + dir;
            if (next_char_index < 0) {
                return 0;
            }
            else if (next_char_index > text.size) {
                return text.size;
            }
            char next_char = text[next_char_index];

            bool cond = condition(next_char, userdata);
            if (invert_condition) {
                cond = !cond;
            }
            if (!cond) {
                return move_out_of_condition ? next_char_index : char_index;
            }
            char_index = next_char_index;
        }
        return 0;
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

        // Special handling for c_string literals ""
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

        // Check if we are on end parenthesis
        if (!found) {
            if (pos.character == 0 && line->text.size > 0 && line->text[0] == end_char && pos.line > 0)
            {
                auto prev_line = source_code_get_line(code, pos.line - 1);
                if (prev_line->indentation > line->indentation) {
                    int block_start_line_index = Line_Movement::move_to_block_boundary(pos.line - 1, -1, true, line->indentation + 1);
                    auto block_start_line = source_code_get_line(code, block_start_line_index);
                    auto& text = block_start_line->text;
                    if (block_start_line->indentation == line->indentation && text.size > 0 && text.characters[text.size - 1] == start_char) {
                        found = true;
                        start = text_index_make(block_start_line_index, text.size - 1);
                        is_block_parenthesis = true;
                    }
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
        return result;
    }

    Normal_Mode_Command normal_mode_command_make(Normal_Command_Type command_type, int repeat_count) {
        Normal_Mode_Command result;
        result.type = command_type;
        result.repeat_count = repeat_count;
        return result;
    }

    Normal_Mode_Command normal_mode_command_make_char(Normal_Command_Type command_type, int repeat_count, char character) {
        Normal_Mode_Command result;
        result.type = command_type;
        result.repeat_count = repeat_count;
        result.options.character = character;
        return result;
    }

    Normal_Mode_Command normal_mode_command_make_motion(Normal_Command_Type command_type, int repeat_count, Motion motion) {
        Normal_Mode_Command result;
        result.type = command_type;
        result.repeat_count = repeat_count;
        result.options.motion = motion;
        return result;
    }

    Normal_Mode_Command normal_mode_command_make_movement(Normal_Command_Type command_type, int repeat_count, Movement movement) {
        Normal_Mode_Command result;
        result.type = command_type;
        result.repeat_count = 1;
        result.options.movement = movement;
        return result;
    }

    Normal_Mode_Command normal_mode_command_make_line_motion(Normal_Command_Type command_type, int repeat_count) {
        Normal_Mode_Command result;
        result.type = command_type;
        result.repeat_count = 1;
        result.options.motion = motion_make_from_movement(movement_make(Movement_Type::MOVE_DOWN, repeat_count));
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
        case '}': index += 1; return parse_result_success(movement_make(Movement_Type::PARAGRAPH_END, repeat_count));
        case '{': index += 1; return parse_result_success(movement_make(Movement_Type::PARAGRAPH_START, repeat_count));
        case 'n': index += 1; return parse_result_success(movement_make(Movement_Type::REPEAT_TEXT_SEARCH, repeat_count));
        case 'N': index += 1; return parse_result_success(movement_make(Movement_Type::REPEAT_TEXT_SEARCH_REVERSE, repeat_count));

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
        case 'P':
        case 'p': return parse_result_success(motion_make(Motion_Type::PARAGRAPH, repeat_count, contains_edges));
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
            if (msg.shift_down) {
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

            if (curr_char == 'g')
            {
                switch (follow_char)
                {
                case 'T':
                case 't': {
                    if (!repeat_count_exists) {
                        repeat_count = 0;
                    }
                    return parse_result_success(
                        normal_mode_command_make((follow_char == 't' ? Normal_Command_Type::GOTO_NEXT_TAB : Normal_Command_Type::GOTO_PREV_TAB), repeat_count)
                    );
                }
                case 'b': return parse_result_success(normal_mode_command_make(Normal_Command_Type::FOLD_CURRENT_BLOCK, repeat_count));
                case 'f': return parse_result_success(normal_mode_command_make(Normal_Command_Type::FOLD_HIGHER_INDENT_IN_BLOCK, repeat_count));
                case 'F': return parse_result_success(normal_mode_command_make(Normal_Command_Type::UNFOLD_IN_BLOCK, repeat_count));
                case 'p': return parse_result_success(normal_mode_command_make(Normal_Command_Type::TOGGLE_LINE_BREAKPOINT, 1));
                }
            }
        }

        // Check if it is a movement
        Parse_Result<Movement> movement_parse = parse_movement(index, repeat_count_exists ? repeat_count : -1);
        if (movement_parse.type == Parse_Result_Type::SUCCESS) {
            return parse_result_success(normal_mode_command_make_movement(Normal_Command_Type::MOVEMENT, 1, movement_parse.result));
        }
        else if (movement_parse.type == Parse_Result_Type::COMPLETABLE) {
            return parse_result_completable<Normal_Mode_Command>();
        }

        if (index >= cmd.size) return parse_result_completable<Normal_Mode_Command>();

        // Check character
        Normal_Command_Type command_type = Normal_Command_Type::MAX_ENUM_VALUE;
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
                    Normal_Command_Type::DELETE_MOTION, repeat_count,
                    motion_make_from_movement(movement_make(Movement_Type::MOVE_RIGHT, 1))
                )
            );
        case 'i':
            return parse_result_success(normal_mode_command_make_movement(
                Normal_Command_Type::ENTER_INSERT_MODE_AFTER_MOVEMENT, 1,
                movement_make(Movement_Type::MOVE_LEFT, 0))
            );
        case 'I':
            return parse_result_success(normal_mode_command_make_movement(
                Normal_Command_Type::ENTER_INSERT_MODE_AFTER_MOVEMENT, 1,
                movement_make(Movement_Type::TO_START_OF_LINE, 1))
            );
        case 'a':
            return parse_result_success(normal_mode_command_make_movement(
                Normal_Command_Type::ENTER_INSERT_MODE_AFTER_MOVEMENT, 1,
                movement_make(Movement_Type::MOVE_RIGHT, 1))
            );
        case 'A':
            return parse_result_success(normal_mode_command_make_movement(
                Normal_Command_Type::ENTER_INSERT_MODE_AFTER_MOVEMENT, 1,
                movement_make(Movement_Type::TO_END_OF_LINE, 1))
            );
        case 'o': return parse_result_success(normal_mode_command_make(Normal_Command_Type::ENTER_INSERT_MODE_NEW_LINE_BELOW, 1));
        case 'O': return parse_result_success(normal_mode_command_make(Normal_Command_Type::ENTER_INSERT_MODE_NEW_LINE_ABOVE, 1));
        case '.': return parse_result_success(normal_mode_command_make(Normal_Command_Type::REPEAT_LAST_COMMAND, repeat_count));
        case '/': return parse_result_success(normal_mode_command_make(Normal_Command_Type::ENTER_TEXT_SEARCH, repeat_count));
        case '?': return parse_result_success(normal_mode_command_make(Normal_Command_Type::ENTER_TEXT_SEARCH_REVERSE, repeat_count));
        case '*': return parse_result_success(normal_mode_command_make(Normal_Command_Type::SEARCH_IDENTIFER_UNDER_CURSOR, repeat_count));
        case 'V': return parse_result_success(normal_mode_command_make(Normal_Command_Type::ENTER_VISUAL_BLOCK_MODE, repeat_count));
        case 'D':
            return parse_result_success(
                normal_mode_command_make_motion(
                    Normal_Command_Type::DELETE_MOTION, repeat_count,
                    motion_make_from_movement(movement_make(Movement_Type::TO_END_OF_LINE, 1))
                )
            );
        case 'C':
            return parse_result_success(
                normal_mode_command_make_motion(
                    Normal_Command_Type::CHANGE_MOTION, repeat_count,
                    motion_make_from_movement(movement_make(Movement_Type::TO_END_OF_LINE, 1))
                )
            );
        case 'Y': return parse_result_success(normal_mode_command_make_line_motion(Normal_Command_Type::YANK_MOTION, repeat_count - 1));
        case 'L': return parse_result_success(normal_mode_command_make(Normal_Command_Type::MOVE_CURSOR_VIEWPORT_BOTTOM, 1));
        case 'M': return parse_result_success(normal_mode_command_make(Normal_Command_Type::MOVE_CURSOR_VIEWPORT_CENTER, 1));
        case 'H': return parse_result_success(normal_mode_command_make(Normal_Command_Type::MOVE_CURSOR_VIEWPORT_TOP, 1));
        case 'p': return parse_result_success(normal_mode_command_make(Normal_Command_Type::PUT_AFTER_CURSOR, repeat_count));
        case 'P': return parse_result_success(normal_mode_command_make(Normal_Command_Type::PUT_BEFORE_CURSOR, repeat_count));
        case 'u': return parse_result_success(normal_mode_command_make(Normal_Command_Type::UNDO, repeat_count));
        case 'r': {
            if (!follow_char_valid) {
                return parse_result_completable<Normal_Mode_Command>();
            }
            return parse_result_success(normal_mode_command_make_char(Normal_Command_Type::REPLACE_CHAR, 1, follow_char));
        }
        case ':':
        {
            if (!follow_char_valid) {
                return parse_result_completable<Normal_Mode_Command>();
            }
            if (follow_char == 'q') {
                return parse_result_success(normal_mode_command_make(Normal_Command_Type::CLOSE_TAB, 1));
            }
            return parse_result_failure<Normal_Mode_Command>();
        }
        case '>': command_type = Normal_Command_Type::ADD_INDENTATION; parse_motion_afterwards = true; break;
        case '<': command_type = Normal_Command_Type::REMOVE_INDENTATION; parse_motion_afterwards = true; break;
        case 'd':
        case 'c':
        case 'y':
		case '=':
        {
            if (!follow_char_valid) {
                return parse_result_completable<Normal_Mode_Command>();
            }
            bool include_edges = false;
            if (curr_char == 'd') {
                command_type = Normal_Command_Type::DELETE_MOTION;
                include_edges = true;
            }
            else if (curr_char == 'c') {
                command_type = Normal_Command_Type::CHANGE_MOTION;
            }
            else if (curr_char == 'y') {
                command_type = Normal_Command_Type::YANK_MOTION;
            }
            else if (curr_char == '=') {
                command_type = Normal_Command_Type::FORMAT_MOTION;
            }
            parse_motion_afterwards = true;

            if (follow_char == curr_char) { // dd, yy, cc or ==
                index += 1;
                return parse_result_success(normal_mode_command_make_line_motion(command_type, repeat_count - 1));
            }
            break;
        }
        case 'v': command_type = Normal_Command_Type::VISUALIZE_MOTION; parse_motion_afterwards = true; break;
        case 'R': command_type = Normal_Command_Type::REPLACE_MOTION_WITH_YANK; parse_motion_afterwards = true; break;
        case 'z': {
            if (!follow_char_valid) {
                return parse_result_completable<Normal_Mode_Command>();
            }

            if (follow_char == 't') {
                return parse_result_success(normal_mode_command_make(Normal_Command_Type::MOVE_VIEWPORT_CURSOR_TOP, 1));
            }
            else if (follow_char == 'z') {
                return parse_result_success(normal_mode_command_make(Normal_Command_Type::MOVE_VIEWPORT_CURSOR_CENTER, 1));
            }
            else if (follow_char == 'b') {
                return parse_result_success(normal_mode_command_make(Normal_Command_Type::MOVE_VIEWPORT_CURSOR_BOTTOM, 1));
            }
            return parse_result_failure<Normal_Mode_Command>();
        }
        }

        // Check if further parsing is required
        if (command_type == Normal_Command_Type::MAX_ENUM_VALUE) {
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

namespace Text_Editing
{
    void particles_add_in_range(Text_Range range, vec3 base_color)
    {
        auto& editor = syntax_editor;
        auto& tab = editor.tabs[editor.open_tab_index];
    
        auto& line_count = editor.visible_line_count;
        int cam_start_visual = source_code_get_line(tab.code, tab.cam_start)->visible_index;
        int cam_end_visual = source_code_get_line(tab.code, tab.cam_end)->visible_index;
        int last_line_visible_index = -1;
        for (int i = range.start.line; i <= range.end.line; i++)
        {
            auto line = source_code_get_line(tab.code, i);
            if (line->visible_index == last_line_visible_index) continue;
            last_line_visible_index = line->visible_index;
            if (line->visible_index < cam_start_visual) continue;
            if (line->visible_index > cam_end_visual) break;
    
            int start = range.start.line == i ? range.start.character : 0;
            int end = range.end.line == i ? range.end.character : line->text.size;
            if (line->is_folded) {
                start = 0;
                end = 4;
            }
            start += line->indentation * 4;
            end += line->indentation * 4;
    
            auto char_size = editor.text_display.char_size;
            vec2 min = vec2(editor.code_box.min.x, editor.code_box.max.y - char_size.y * (line->visible_index - cam_start_visual + 1)) + vec2(char_size.x * start, 0.0f);
            vec2 max = vec2(editor.code_box.min.x, editor.code_box.max.y - char_size.y * (line->visible_index - cam_start_visual)) + vec2(char_size.x * end, 0.0f);
    
            float radius = 10;
            float dist_between = 4;
            int x_count = (max.x - min.x) / dist_between;
            int y_count = (max.y - min.y) / dist_between;
            for (int x = 0; x < x_count; x++) {
                for (int y = 0; y < y_count; y++) {
                    if (random_next_float(&editor.random) < 0.1f) continue;
    
                    Particle p;
                    p.color = base_color;
                    p.color.x += (2 * random_next_float(&editor.random) - 1.0f) * 0.3f;
                    p.color.y += (2 * random_next_float(&editor.random) - 1.0f) * 0.3f;
                    p.color.z += (2 * random_next_float(&editor.random) - 1.0f) * 0.3f;
    
                    p.position.x = min.x + (x / (float)x_count) * (max.x - min.x);
                    p.position.y = min.y + (y / (float)y_count) * (max.y - min.y);
                    p.radius = radius + (random_next_float(&editor.random) * 5) - 2;
                    p.creation_time = editor.last_update_time;
                    p.life_time = 0.3f + random_next_float(&editor.random) * 1.5f;
    
                    vec2 vel = vec2(random_next_float(&editor.random) - 0.5f, random_next_float(&editor.random) - 0.5f) * 2.0f;
                    vel = vector_normalize_safe(vel);
                    vel = vel * (30 + random_next_float(&editor.random) * 100);
                    p.velocity = vel;
                    dynamic_array_push_back(&editor.particles, p);
                }
            }
        }
    }

    void insert_char(Text_Index index, char c, bool with_particles) 
    {
        auto& editor = syntax_editor;
        auto history = &editor.tabs[editor.open_tab_index].history;
        history_insert_char(history, index, c);

        if (with_particles)
        {
            Text_Range range;
            range.start = index;
            range.end = index;
            range.end.character += 1;
            particles_add_in_range(range, vec3(0.5f, 0.5f, 0.5f));
        }
    }

    void insert_text(Text_Index index, String str, bool with_particles)
    {
        auto& editor = syntax_editor;
        auto history = &editor.tabs[editor.open_tab_index].history;
        history_insert_text(history, index, str);

        if (with_particles) {
            Text_Range range;
            range.start = index;
            range.end = index;
            range.end.character += str.size;
            particles_add_in_range(range, vec3(0.5f, 0.5f, 0.5f));
        }
    }
    
    void delete_text(Text_Index index, int char_end, bool with_particles)
    {
        if (with_particles) {
            Text_Range range = text_range_make(index, text_index_make(index.line, char_end));
            particles_add_in_range(range, vec3(0.8f, 0.2f, 0.2f));
        }

        auto& editor = syntax_editor;
        auto history = &editor.tabs[editor.open_tab_index].history;
        history_delete_text(history, index, char_end);
    }

    void delete_char(Text_Index index, bool with_particles)
    {
        if (with_particles) {
            particles_add_in_range(text_range_make(index, text_index_make(index.line, index.character + 1)), vec3(0.8f, 0.2f, 0.2f));
        }

        auto& editor = syntax_editor;
        auto history = &editor.tabs[editor.open_tab_index].history;
        history_delete_char(history, index);
    }

    void remove_line(int line_index, bool with_particles)
    {
        auto& editor = syntax_editor;
        auto& tab = editor.tabs[editor.open_tab_index];
        auto history = &tab.history;

        if (with_particles) {
            Text_Range range = text_range_make(text_index_make(line_index, 0), text_index_make_line_end(tab.code, line_index));
            particles_add_in_range(range, vec3(0.8f, 0.2f, 0.2f));
        }

        history_remove_line(history, line_index);
    }

    // Note: Does not yank text
    void delete_text_range(Text_Range range, bool is_line_motion, bool with_particles)
    {
        auto& editor = syntax_editor;
        auto& tab = editor.tabs[editor.open_tab_index];
        auto code = tab.code;
        auto history = &tab.history;
    
        if (is_line_motion) {
            range.start.character = 0;
            range.end = text_index_make_line_end(code, range.end.line);
        }
        if (with_particles) {
            particles_add_in_range(range, vec3(0.8f, 0.2f, 0.2f));
        }
        history_start_complex_command(history);
        SCOPE_EXIT(history_stop_complex_command(history));
    
        if (is_line_motion) {
            for (int i = range.start.line; i <= range.end.line; i++) {
                remove_line(range.start.line, false);
            }
            return;
        }
    
        // Handle single line case
        if (range.start.line == range.end.line) {
            delete_text(range.start, range.end.character, false);
            return;
        }
    
        // Delete text in first line
        auto line = Motions::get_line(range.start);
        auto end_line = Motions::get_line(range.end);
        if (end_line == nullptr || line == nullptr) return;
        delete_text(range.start, line->text.size, false);
    
        // Append remaining text of last-line into first line
        String remainder = string_create_substring_static(&end_line->text, range.end.character, end_line->text.size);
        insert_text(range.start, remainder, false);
        if (line->indentation != end_line->indentation && range.start.character == 0) {
            history_change_indent(history, range.start.line, end_line->indentation);
        }
    
        // Delete all lines inbetween
        for (int i = range.start.line + 1; range.start.line + 1 < code->line_count && i <= range.end.line; i++) {
            remove_line(range.start.line + 1, false);
        }
    }

    void token_expects_space_before_or_after (
        Dynamic_Array<Token>& tokens, int token_index, bool& out_space_before, bool& out_space_after, bool& out_ignore_lex_changes)
	{
		out_space_after = false;
		out_space_before = false;
		if (token_index >= tokens.size) return;

		// Handle different token types
		const auto& token = tokens[token_index];
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
			if (token.options.keyword == Keyword::NEW && token_index + 1 < tokens.size) {
				const auto& next = tokens[token_index + 1];
				if (next.type == Token_Type::PARENTHESIS && next.options.parenthesis.is_open && next.options.parenthesis.type == Parenthesis_Type::BRACES) {
					out_space_after = false;
				}
			}
			if ((token.options.keyword == Keyword::CAST || token.options.keyword == Keyword::CAST_POINTER) && token_index + 1 < tokens.size) {
				const auto& next = tokens[token_index + 1];
				if (next.type == Token_Type::PARENTHESIS && next.options.parenthesis.is_open && next.options.parenthesis.type == Parenthesis_Type::BRACES) {
					out_space_after = false;
				}
			}
			if (token.options.keyword == Keyword::CAST || token.options.keyword == Keyword::CAST_POINTER ||
				token.options.keyword == Keyword::BAKE || token.options.keyword == Keyword::INSTANCIATE ||
				token.options.keyword == Keyword::GET_OVERLOAD || token.options.keyword == Keyword::GET_OVERLOAD_POLY ||
				token.options.keyword == Keyword::MUTABLE || token.options.keyword == Keyword::IN_KEYWORD) 
			{
				out_space_before = false;
			}
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
		case Operator::INFER_ARROW:
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
		case Operator::APOSTROPHE:
		case Operator::UNINITIALIZED:
		case Operator::QUESTION_MARK:
		case Operator::OPTIONAL_POINTER:
		case Operator::DOT_CALL:
		case Operator::DOLLAR: {
			out_space_after = false;
			out_space_before = false;
			break;
		}

		 // Special case for : = and : : 
		case Operator::ASSIGN: {
			out_space_after = true;
			out_space_before = true;
			if (token_index - 1 >= 0) {
				auto& prev = tokens[token_index - 1];
				if (prev.type == Token_Type::OPERATOR && (prev.options.op == Operator::COLON || prev.options.op == Operator::DOT)) {
					out_space_before = false;
					out_ignore_lex_changes = true;
				}
			}
			break;
		}
		case Operator::COLON: {
			out_space_after = true;
			out_space_before = false;
			if (token_index + 1 < tokens.size) {
				auto& next = tokens[token_index + 1];
				if (next.type == Token_Type::OPERATOR && (next.options.op == Operator::ASSIGN || next.options.op == Operator::COLON)) {
					out_space_after = false;
					out_ignore_lex_changes = true;
				}
			}
			break;
		}

							// Only space after
		case Operator::COMMA:
		case Operator::TILDE_STAR:
		case Operator::TILDE_STAR_STAR:
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
			if (token_index <= 0 || token_index + 1 >= tokens.size) {
				out_space_after = false;
				out_space_before = false;
				break;
			}

			// Otherwise use heuristic to check if it is a binop
			// The heuristic currently used: Check if previous and next are 'values' (e.g. literals/identifiers or parenthesis), if both are, then binop
			bool prev_is_value = false;
			{
				const auto& t = tokens[token_index - 1];
				if (t.type == Token_Type::IDENTIFIER || t.type == Token_Type::LITERAL) {
					prev_is_value = true;
				}
				if (t.type == Token_Type::PARENTHESIS && !t.options.parenthesis.is_open && t.options.parenthesis.type != Parenthesis_Type::BRACKETS) {
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
				if (t.type == Token_Type::KEYWORD) {
					switch (t.options.keyword)
					{
					case Keyword::CAST:
					case Keyword::CAST_POINTER:
					case Keyword::INSTANCIATE:
					case Keyword::NEW:
						next_is_value = true;
					}
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

	void tokenize_and_auto_format_line(int tab_index, int line_index, Identifier_Pool_Lock* pool_lock)
	{
		auto& editor = syntax_editor;
		auto& tab = editor.tabs[tab_index];
		auto code = tab.code;
		Source_Line* line = source_code_get_line(code, line_index);
		if (line->is_comment || line->comment_block_indentation != -1) {
			lexer_tokenize_line(line->text, &line->tokens, pool_lock);
			return;
		}

		bool cursor_on_line = tab.cursor.line == line_index;
		bool respect_cursor_space = editor.mode == Editor_Mode::INSERT && cursor_on_line;
		int cursor = tab.cursor.character;
		auto& text = line->text;

		// 1. Remove non-essential whitespaces
		String trimmed_text = string_create_empty(text.size);
		SCOPE_EXIT(string_destroy(&trimmed_text));
		{
			int i = 0;
			bool prev_was_critical = false;
			int cursor_offset = 0;
			bool prev_was_slash = false;
			bool prev_was_backslash = false;
			bool inside_string = false;
			bool inside_comment = false;
			for (int i = 0; i < text.size; i++)
			{
				char c = text[i];
				bool next_is_critical = i + 1 < text.size ? char_is_space_critical(text[i + 1]) : false;

				if (c == '/' && prev_was_slash && !inside_string) {
					inside_comment = true;
				}
				else if (c == '"' && !prev_was_backslash && !inside_comment) {
					inside_string = !inside_string;
				}

				bool remove = false;
				if (c < ' ') {
					remove = true;
				}
				else if (c != ' ') {
					remove = false;
				}
				else if (c == ' ')
				{
					remove = true;

					// Otherwise we are on space
					if (prev_was_critical && next_is_critical) {
						remove = false;
					}

					// Don't remove cursor is before or after
					if (respect_cursor_space && (cursor + cursor_offset == trimmed_text.size || cursor + cursor_offset == trimmed_text.size + 1)) {
						if (prev_was_critical && cursor + cursor_offset == trimmed_text.size + 1) {
							remove = false;
						}
					}

					// Remove if we are still on start of line
					if (trimmed_text.size == 0) {
						remove = true;
					}
				}

				if (inside_comment || inside_string) {
					remove = false;
				}

				if (remove)
				{
					if (cursor + cursor_offset > trimmed_text.size) {
						cursor_offset -= 1;
					}
				}
				else {
					string_append_character(&trimmed_text, c);
					prev_was_critical = char_is_space_critical(c);
					prev_was_slash = c == '/';
					prev_was_backslash = c == '\\';
				}
			}

			// Trim spaces at end of line
			if (!inside_comment && !inside_string) {
				while (trimmed_text.size > 0 && trimmed_text[text.size - 1] == ' ')
				{
					if (respect_cursor_space && cursor >= text.size) {
						break;
					}
					string_remove_character(&trimmed_text, text.size - 1);
				}
			}

			cursor += cursor_offset;
		}

		// 2. Tokenize trimmed-string
		auto& tokens = line->tokens;
		dynamic_array_reset(&tokens);
		lexer_tokenize_line(trimmed_text, &tokens, pool_lock);

		// 3. Add visual (non-critical) spaces between tokens
		int index_shift_for_tokens_after_current = 0;
		for (int i = 0; i < tokens.size; i++)
		{
			Token& curr = tokens[i];
			curr.start_index += index_shift_for_tokens_after_current;
			curr.end_index += index_shift_for_tokens_after_current;

			if (i == tokens.size - 1) break;
			Token& next = tokens[i + 1];

			// Check if spacing is expected between tokens
			bool space_between_tokens_expected = false;
			{
				bool space_before, space_after;
				bool unused = false;
				token_expects_space_before_or_after(tokens, i, space_before, space_after, unused);
				if (space_after) space_between_tokens_expected = true;
				token_expects_space_before_or_after(tokens, i + 1, space_before, space_after, unused);
				if (space_before) space_between_tokens_expected = true;

				// Special handling for closing parenthesis, as I want a space there
				if (curr.type == Token_Type::PARENTHESIS && !curr.options.parenthesis.is_open && curr.options.parenthesis.type != Parenthesis_Type::BRACKETS &&
					next.type != Token_Type::OPERATOR && next.type != Token_Type::PARENTHESIS) {
					space_between_tokens_expected = true;
				}
			}

			// Add space if required
			if (curr.end_index == next.start_index + index_shift_for_tokens_after_current && space_between_tokens_expected) {
				// Insert space between tokens
				if (curr.end_index < cursor) {
					cursor += 1;
				}
				string_insert_character_before(&trimmed_text, ' ', curr.end_index);
				index_shift_for_tokens_after_current += 1;
			}
		}

		// 4. Compare trimmed text to text and add changes to history and line-diff
		int text_index = 0;
		int trimmed_index = 0;
		while (!(text_index >= text.size && trimmed_index >= trimmed_text.size))
		{
			char text_char = text_index < text.size ? text[text_index] : '\0';
			char trimmed_char = trimmed_index < trimmed_text.size ? trimmed_text[trimmed_index] : '\0';

			if (text_char == trimmed_char) {
				text_index += 1;
				trimmed_index += 1;
				continue;
			}

			if (text_char == ' ') {
				history_delete_char(&tab.history, text_index_make(line_index, text_index));
			}
			else if (trimmed_char == ' ') {
				history_insert_char(&tab.history, text_index_make(line_index, text_index), ' ');
			}
			else {
				panic("Text and trimmed text should only differ by spaces!");
			}
		}

		// 5. Set cursor
		if (cursor_on_line) {
			tab.cursor.character = cursor;
		}
	}
}



// Suggestions
Editor_Suggestion suggestion_make_symbol(Symbol* symbol) {
	Editor_Suggestion result;
	result.type = Suggestion_Type::SYMBOL;
	result.options.symbol = symbol;
	result.text = symbol->id;
	return result;
}

Editor_Suggestion suggestion_make_id(String* id, vec3 color = vec3(1.0f)) {
	Editor_Suggestion result;
	result.type = Suggestion_Type::ID;
	result.text = id;
	result.options.id_color.r = color.x;
	result.options.id_color.g = color.y;
	result.options.id_color.b = color.z;
	return result;
}

Editor_Suggestion suggestion_make_file(int file_index) {
	Editor_Suggestion result;
	result.type = Suggestion_Type::FILE;
	result.options.file_index_in_crawler = file_index;
	result.text = &directory_crawler_get_content(syntax_editor.directory_crawler)[file_index].name;
	return result;
}

Editor_Suggestion suggestion_make_struct_member(Datatype_Struct* struct_type, Datatype* member_type, String* id) {
	Editor_Suggestion result;
	result.type = Suggestion_Type::STRUCT_MEMBER;
	result.options.struct_member.structure = struct_type;
	result.options.struct_member.member_type = member_type;
	result.text = id;
	return result;
}

Editor_Suggestion suggestion_make_enum_member(Datatype_Enum* enum_type, String* id) {
	Editor_Suggestion result;
	result.type = Suggestion_Type::ENUM_MEMBER;
	result.options.enum_member.enumeration = enum_type;
	result.text = id;
	return result;
}



// Tabs
// Returns new tab index
int syntax_editor_add_tab(String file_path)
{
	auto& editor = syntax_editor;

	Compilation_Unit* unit = compiler_add_compilation_unit(file_path, true, false);
	if (unit == nullptr) {
		return editor.open_tab_index;
	}
	if (unit->editor_tab_index != -1) {
		return unit->editor_tab_index;
	}

	// Otherwise create new tab
	Editor_Tab tab;
	tab.compilation_unit = unit;
	tab.code = source_code_copy(tab.compilation_unit->code);
	tab.requires_recompile = true;
	tab.history = code_history_create(tab.code);
	tab.folds = dynamic_array_create<Code_Fold>();
	tab.last_code_info_synch = history_get_timestamp(&tab.history);
	tab.last_compiler_synchronized = history_get_timestamp(&tab.history);
	tab.last_code_completion_info_index = -1;
	tab.last_render_timestamp = history_get_timestamp(&tab.history);
	tab.last_code_completion_query_pos = text_index_make(-1, -1);
	tab.last_render_cursor_pos = text_index_make(0, 0);
	tab.cursor = text_index_make(0, 0);
	tab.last_line_x_pos = 0;
	tab.cam_start = 0;
	tab.cam_end = 0;
	tab.breakpoints = dynamic_array_create<Line_Breakpoint>();
	tab.last_jump_index = -1;
	tab.jump_list = dynamic_array_create<Text_Index>();
	dynamic_array_push_back(&syntax_editor.tabs, tab);
	unit->editor_tab_index = syntax_editor.tabs.size - 1;

	syntax_editor_update_line_visible_and_fold_info(syntax_editor.tabs.size - 1);
	// Auto format all lines
	auto lock = identifier_pool_lock_aquire(&compiler.identifier_pool);
	SCOPE_EXIT(identifier_pool_lock_release(lock));
	for (int i = 0; i < tab.code->line_count; i++) {
		auto line = source_code_get_line(tab.code, i);
		lexer_tokenize_line(line->text, &line->tokens, &lock);
	}

	return syntax_editor.tabs.size - 1;
}

void editor_tab_destroy(Editor_Tab* tab) {
	code_history_destroy(&tab->history);
	dynamic_array_destroy(&tab->folds);
	dynamic_array_destroy(&tab->jump_list);
	dynamic_array_destroy(&tab->breakpoints);
	if (tab->compilation_unit != nullptr) {
		tab->compilation_unit->open_in_editor = false;
	}
	source_code_destroy(tab->code);
}

void syntax_editor_update_line_visible_and_fold_info(int tab_index)
{
	auto& editor = syntax_editor;
	auto& tab = editor.tabs[tab_index];
	auto& folds = tab.folds;
	auto code = tab.code;

	Code_Fold dummy_fold;
	dummy_fold.line_start = -1;
	dummy_fold.line_end = -1;
	dummy_fold.indentation = 0;

	int fold_index = 0;
	int visible_index = 0;
	bool last_was_fold = false;
	Code_Fold* fold = folds.size > 0 ? &folds[0] : &dummy_fold;
	for (int i = 0; i < code->line_count; i++)
	{
		auto line = source_code_get_line(code, i);

		// Get fold for line
		while (i > fold->line_end && fold_index + 1 < folds.size) {
			fold = &folds[fold_index + 1];
			fold_index += 1;
		}
		line->is_folded = i >= fold->line_start && i <= fold->line_end;
		if (line->is_folded) {
			line->fold_index = fold_index;
		}

		if (last_was_fold && !line->is_folded) {
			visible_index += 1;
		}
		line->visible_index = visible_index;
		if (!line->is_folded) {
			visible_index += 1;
		}
		last_was_fold = line->is_folded;
	}
}

void syntax_editor_add_fold(int line_start, int line_end, int indentation)
{
	auto& tab = syntax_editor.tabs[syntax_editor.open_tab_index];
	auto& folds = tab.folds;

	// Find insertion index
	int i = 0;
	for (i = 0; i < folds.size; i++) {
		auto& fold = folds[i];
		if (line_start < fold.line_start) {
			if (line_end >= fold.line_start) {
				assert(line_end >= fold.line_end, "Folds should not overlap");
			}
			break;
		}
		else if (line_start == fold.line_start) {
			if (line_end == fold.line_end) return; // Fold already exists
			if (line_end > fold.line_end) {
				break;
			}
		}
	}

	Code_Fold fold;
	fold.line_start = line_start;
	fold.line_end = line_end;
	fold.indentation = indentation;
	dynamic_array_insert_ordered(&folds, fold, i);

	syntax_editor_update_line_visible_and_fold_info(syntax_editor.open_tab_index);
}

struct Comparator_Compiler_Error
{
	bool operator()(const int& a_index, const int& b_index)
	{
		auto& editor = syntax_editor;
		Compiler_Error_Info& a = editor.analysis_data->compiler_errors[a_index];
		Compiler_Error_Info& b = editor.analysis_data->compiler_errors[b_index];

		int tab_a = -1;
		int tab_b = -1;
		for (int i = 0; i < editor.tabs.size; i++) {
			auto unit = editor.tabs[i].compilation_unit;
			if (a.unit == unit) {
				tab_a = i;
			}
			if (b.unit == unit) {
				tab_b = i;
			}
		}

		if (tab_a != tab_b) {
			return tab_a < tab_b;
		}

		if (a.text_index.line != b.text_index.line) {
			return a.text_index.line < b.text_index.line;
		}
		return a.text_index.character < b.text_index.character;
	}
};

void sort_error_indices()
{
	auto& editor = syntax_editor;
	auto& indices = editor.error_indices_sorted;
	dynamic_array_reset(&indices);
	dynamic_array_reserve(&indices, editor.analysis_data->compiler_errors.size);
	for (int i = 0; i < editor.analysis_data->compiler_errors.size; i++) {
		dynamic_array_push_back(&indices, i);
	}
	dynamic_array_sort(&indices, Comparator_Compiler_Error());
}

void syntax_editor_switch_tab(int new_tab_index)
{
	auto& editor = syntax_editor;
	if (editor.open_tab_index == new_tab_index) return;
	if (new_tab_index < 0 || new_tab_index >= editor.tabs.size) return;

	editor.open_tab_index = new_tab_index;
	auto& tab = editor.tabs[editor.open_tab_index];
	// Re-Sort errors since sorting depends on open tab
	if (editor.analysis_data != nullptr) {
		sort_error_indices();
	}
}

void syntax_editor_close_tab(int tab_index, bool force_close = false)
{
	auto& editor = syntax_editor;
	if (editor.tabs.size <= 1 && !force_close) return;
	if (tab_index < 0 || tab_index >= editor.tabs.size) return;

	syntax_editor_save_text_file();

	Editor_Tab* tab = &editor.tabs[tab_index];
	tab->compilation_unit->editor_tab_index = -1;
	editor_tab_destroy(&editor.tabs[tab_index]);
	dynamic_array_remove_ordered(&editor.tabs, tab_index);

	// Update tab index for all compilation units
	for (int i = 0; i < editor.tabs.size; i++) {
		auto& tab = editor.tabs[i];
		tab.compilation_unit->editor_tab_index = i;
	}

	editor.open_tab_index = math_minimum(editor.tabs.size - 1, editor.open_tab_index);
	if (tab_index == editor.main_tab_index) {
		editor.main_tab_index = -1;
	}
	else if (editor.main_tab_index > tab_index) {
		editor.main_tab_index -= 1;
	}
}




void syntax_editor_initialize(Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Window* window, Input* input)
{
	memory_zero(&syntax_editor);
	syntax_editor.window = window;
	gui_initialize(text_renderer, window);
	ui_system_initialize();

	syntax_editor.debugger = debugger_create();
	syntax_editor.last_code_completion_tab = -1;
	syntax_editor.compile_count = 0;
	syntax_editor.symbol_table_already_visited = hashset_create_pointer_empty<Symbol_Table*>(4);
	syntax_editor.last_insert_was_shift_enter = false;
	syntax_editor.random = random_make_time_initalized();
	syntax_editor.last_update_time = timer_current_time_in_seconds();
	syntax_editor.particles = dynamic_array_create<Particle>();
	syntax_editor.directory_crawler = directory_crawler_create();
	syntax_editor.frame_index = 1;
	syntax_editor.last_compile_was_with_code_gen = false;
	syntax_editor.analysis_data = nullptr;
	syntax_editor.editor_text = Rich_Text::create(vec3(1.0f));
	syntax_editor.normal_text_size_pixel = convertHeight(0.48f, Unit::CENTIMETER);
	syntax_editor.text_display = Text_Display::make(
		&syntax_editor.editor_text, renderer_2D, text_renderer, text_renderer_get_aligned_char_size(text_renderer, syntax_editor.normal_text_size_pixel), 4
	);
	Text_Display::set_padding(&syntax_editor.text_display, 2);
	Text_Display::set_block_outline(&syntax_editor.text_display, 3, vec3(0.5f));

	syntax_editor.visible_line_count = (int)(rendering_core.render_information.backbuffer_height / syntax_editor.text_display.char_size.y) + 1;
	syntax_editor.command_buffer = string_create();

	syntax_editor.fuzzy_search_text = string_create();
	syntax_editor.suggestions = dynamic_array_create<Editor_Suggestion>();
	syntax_editor.search_text = string_create();

	syntax_editor.yank_string = string_create();
	syntax_editor.yank_was_line = false;

	syntax_editor.last_normal_command.type = Normal_Command_Type::MOVEMENT;
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

	syntax_editor.show_semantic_infos = false;

	syntax_editor.error_indices_sorted = dynamic_array_create<int>();

	compiler_initialize();

	String default_filename = string_create_static("upp_code/editor_text.upp");
	int tab_index = syntax_editor_add_tab(default_filename);
	syntax_editor_switch_tab(tab_index);
	assert(tab_index != -1, "");
	syntax_editor.open_tab_index = 0;
	syntax_editor.main_tab_index = 0;

	syntax_editor.compiler_build_code = false;
	syntax_editor.compiler_main_unit = nullptr;
	syntax_editor.compiler_work_started = false;
	syntax_editor.compiler_thread_should_close = false;
	syntax_editor.compiler_wait_semaphore = semaphore_create(0, 1);
	syntax_editor.compilation_finish_semaphore = semaphore_create(0, 1);
	syntax_editor.compiler_thread = thread_create(compiler_thread_entry_fn, nullptr);

	syntax_editor.watch_values = dynamic_array_create<Watch_Value>();
	syntax_editor.selected_stack_frame = 0;

	syntax_editor.pass_filter_ui_open = false;
	syntax_editor.poly_pass_filters = dynamic_array_create<Pass_Filter>();
	syntax_editor.filtered_passes = hashtable_create_pointer_empty<Analysis_Pass*, bool>(1);
	syntax_editor.prefer_poly_passes = true;
	syntax_editor.prefer_base_pass = false;
}

void syntax_editor_destroy()
{
	auto& editor = syntax_editor;
	ui_system_shutdown();
	debugger_destroy(editor.debugger);
	hashset_destroy(&editor.symbol_table_already_visited);
	directory_crawler_destroy(editor.directory_crawler);
	dynamic_array_destroy(&editor.particles);
	dynamic_array_destroy(&editor.suggestions);
	Rich_Text::destroy(&editor.editor_text);
	string_destroy(&syntax_editor.command_buffer);
	string_destroy(&syntax_editor.yank_string);
	string_destroy(&syntax_editor.fuzzy_search_text);
	string_destroy(&syntax_editor.search_text);
	compiler_destroy();
	dynamic_array_destroy(&syntax_editor.error_indices_sorted);


	dynamic_array_destroy(&editor.last_insert_commands);
	string_destroy(&editor.last_recorded_code_completion);

	dynamic_array_for_each(editor.tabs, editor_tab_destroy);
	dynamic_array_destroy(&editor.tabs);

	for (int i = 0; i < editor.watch_values.size; i++) {
		string_destroy(&editor.watch_values[i].name);
		string_destroy(&editor.watch_values[i].value_as_text);
	}
	dynamic_array_destroy(&editor.watch_values);

	for (int i = 0; i < editor.poly_pass_filters.size; i++) {
		string_destroy(&editor.poly_pass_filters[i].required_substring);
	}
	dynamic_array_destroy(&editor.poly_pass_filters);
	hashtable_destroy(&editor.filtered_passes);
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
		auto path = tab.compilation_unit->filepath;
		auto success = file_io_write_file(path.characters, array_create_static((byte*)whole_text.characters, whole_text.size));
		if (!success) {
			logg("Saving file failed for path \"%s\"\n", path.characters);
		}
		else {
			// logg("Saved file \"%s\"!\n", path.characters);
		}
	}
}

Line_Diff* code_diff_get_or_add_line_diff(Code_Diff* code_diff, int line_index)
{
	auto& line_diffs = code_diff->line_diffs;

	Line_Diff* line_diff = nullptr;
	for (int i = 0; i < line_diffs.size; i++) {
		auto& diff = line_diffs[i];
		if (diff.new_line_index == line_index) {
			return &diff;
		}
	}

	Line_Diff new_diff;
	new_diff.new_line_index = line_index;
	new_diff.items = dynamic_array_create<Line_Diff_Item>();
	new_diff.indent_changed = false;
	dynamic_array_push_back(&line_diffs, new_diff);
	return &line_diffs[line_diffs.size - 1];
}

Code_Diff code_diff_create_from_changes(Dynamic_Array<Code_Change> changes)
{
	Code_Diff result;
	result.line_diffs = dynamic_array_create<Line_Diff>();
	result.line_inserts_and_deletes = dynamic_array_create<Line_Insert_Or_Delete>();

	auto store_changed_line_index = [&](int line_index, bool is_insert, int char_index, int length)
		{
			Line_Diff_Item item;
			item.char_index = char_index;
			item.is_insert = is_insert;
			item.length = length;
			Line_Diff* line_diff = code_diff_get_or_add_line_diff(&result, line_index);
			dynamic_array_push_back(&line_diff->items, item);
		};

	for (int i = 0; i < changes.size; i++)
	{
		auto& change = changes[i];
		int line_index = -1;
		switch (change.type)
		{
		case Code_Change_Type::LINE_INSERT:
		{
			int line_index = change.options.line_insert.line_index;

			// Update line change infos
			for (int j = 0; j < result.line_diffs.size; j++)
			{
				auto& diff = result.line_diffs[j];
				if (change.apply_forwards) {
					if (line_index <= diff.new_line_index) {
						diff.new_line_index += 1;
					}
				}
				else
				{
					if (line_index == diff.new_line_index) {
						dynamic_array_destroy(&diff.items);
						dynamic_array_swap_remove(&result.line_diffs, j);
						j = j - 1;
						continue;
					}
					else if (line_index < diff.new_line_index) {
						diff.new_line_index -= 1;
					}
				}
			}

			Line_Insert_Or_Delete insert;
			insert.is_insert = change.apply_forwards;
			insert.line_index = line_index;
			dynamic_array_push_back(&result.line_inserts_and_deletes, insert);
			break;
		}
		case Code_Change_Type::CHAR_INSERT: {
			auto insert = change.options.char_insert;
			store_changed_line_index(insert.index.line, change.apply_forwards, insert.index.character, 1);
			break;
		}
		case Code_Change_Type::TEXT_INSERT: {
			auto insert = change.options.text_insert;
			store_changed_line_index(insert.index.line, change.apply_forwards, insert.index.character, insert.text.size);
			break;
		}
		case Code_Change_Type::LINE_INDENTATION_CHANGE: {
			auto line_diff = code_diff_get_or_add_line_diff(&result, change.options.indentation_change.line_index);
			line_diff->indent_changed = true;
			break;
		}
		default: panic("");
		}
	}

	return result;
}

void code_diff_destroy(Code_Diff* code_diff)
{
	for (int i = 0; i < code_diff->line_diffs.size; i++) {
		dynamic_array_destroy(&code_diff->line_diffs[i].items);
	}
	dynamic_array_destroy(&code_diff->line_diffs);
	dynamic_array_destroy(&code_diff->line_inserts_and_deletes);
}

void code_diff_update_folds_jumps_and_breakpoints(Code_Diff code_diff, int tab_index)
{
	auto& tab = syntax_editor.tabs[tab_index];
	auto& folds = tab.folds;
	auto& jump_list = tab.jump_list;
	auto& breakpoints = tab.breakpoints;
	bool folds_changed = false;
	bool jump_list_changed = false;

	// Update folds and jump indices
	for (int i = 0; i < code_diff.line_inserts_and_deletes.size; i++)
	{
		const auto& line_insert = code_diff.line_inserts_and_deletes[i];
		auto line_index = line_insert.line_index;

		folds_changed = true;
		// Update fold line-range
		for (int j = 0; j < folds.size; j++)
		{
			auto& fold = folds[j];

			bool inside_fold = false;
			bool before_fold = false;
			if (line_insert.is_insert) {
				inside_fold = line_index > fold.line_start && line_index <= fold.line_end;
				before_fold = line_index <= fold.line_start;
			}
			else {
				inside_fold = line_index >= fold.line_start && line_index <= fold.line_end;
				before_fold = line_index < fold.line_start;
			}

			// Remove fold if change was made inside
			if (inside_fold) {
				dynamic_array_remove_ordered(&folds, j);
				j = j - 1;
			}
			else if (before_fold) {
				int diff = line_insert.is_insert ? 1 : -1;
				fold.line_start += diff;
				fold.line_end += diff;
			}
		}

		// Update jump list line indices
		for (int j = 0; j < tab.jump_list.size; j++)
		{
			auto& pos = tab.jump_list[j];
			if (line_insert.is_insert) {
				if (line_index <= pos.line) {
					pos.line += 1;
					jump_list_changed = true;
				}
			}
			else
			{
				if (line_index <= pos.line) {
					pos.line -= 1;
					jump_list_changed = true;
				}
			}
		}

		// Update breakpoints
		for (int j = 0; j < breakpoints.size; j++)
		{
			auto& breakpoint = breakpoints[j];
			if (line_insert.is_insert) {
				if (line_index <= breakpoint.line_number) {
					breakpoint.line_number += 1;
				}
			}
			else
			{
				if (line_index == breakpoint.line_number) {
					// Remove breakpoint
					dynamic_array_swap_remove(&breakpoints, j);
					j -= 1;
				}
				else if (line_index < breakpoint.line_number) {
					breakpoint.line_number -= 1;
				}
			}
		}
	}

	// Remove-folds where changes happened
	for (int i = 0; i < code_diff.line_diffs.size; i++)
	{
		const auto& line = code_diff.line_diffs[i];
		if (line.items.size == 0 && !line.indent_changed) continue;

		int line_index = line.new_line_index;
		for (int j = 0; j < folds.size; j++) {
			auto& fold = folds[j];
			if (line_index >= fold.line_start && line_index <= fold.line_end) {
				dynamic_array_remove_ordered(&folds, j);
				folds_changed = true;
				j = j - 1;
			}
		}
	}

	if (folds_changed) {
		syntax_editor_update_line_visible_and_fold_info(tab_index);
	}

	// Prune jump list if positions are close to one another
	if (jump_list_changed)
	{
		auto code = tab.code;
		// Prune jumps with same position
		for (int i = 0; i < tab.jump_list.size; i++)
		{
			auto& jump = tab.jump_list[i];
			Text_Index prev;
			if (i > 0) {
				prev = tab.jump_list[i - 1];
			}
			else {
				prev = text_index_make(-20, -20);
			}

			bool should_delete = false;
			if (jump.line < 0 || jump.line >= code->line_count) {
				should_delete = true;
			}
			else {
				auto line = source_code_get_line(code, jump.line);
				jump.character = math_clamp(jump.character, 0, line->text.size);
				if (jump.line == prev.line) {
					should_delete = true;
				}
			}

			if (should_delete)
			{
				dynamic_array_remove_ordered(&tab.jump_list, i);
				if (tab.last_jump_index >= tab.jump_list.size) {
					tab.last_jump_index = math_maximum(0, tab.jump_list.size - 1);
				}
				i = i - 1;
			}
		}
	}
}

void code_diff_update_tokenization(Code_Diff& code_diff, Source_Code* code, int tab_index)
{
	Identifier_Pool_Lock lock = identifier_pool_lock_aquire(&compiler.identifier_pool);
	SCOPE_EXIT(identifier_pool_lock_release(lock));
	for (int i = 0; i < code_diff.line_diffs.size; i++)
	{
		auto& line = code_diff.line_diffs[i];
		if (line.items.size == 0) continue;
		source_code_tokenize_line(source_code_get_line(code, line.new_line_index), &lock);
	}
}

void code_diff_update_analysis_infos(Code_Diff code_diff, Source_Code* code)
{
	for (int line_index = 0; line_index < code_diff.line_diffs.size; line_index++)
	{
		auto line_diff = code_diff.line_diffs[line_index];
		if (line_diff.items.size == 0) continue;
		auto line = source_code_get_line(code, line_diff.new_line_index);

		// Update analysis infos
		for (int item_index = 0; item_index < line_diff.items.size; item_index++)
		{
			const auto& change = line_diff.items[item_index];
			int insert_index = change.char_index;
			int insert_length = change.length;

			// Update analysis infos
			for (int i = 0; i < line->item_infos.size; i++)
			{
				auto& item_info = line->item_infos[i];
				if (change.is_insert)
				{
					if (insert_index <= item_info.start_char) { // Inserted before item
						item_info.start_char += insert_length;
						item_info.end_char += insert_length;
					}
					else if (insert_index <= item_info.end_char) { // Inserted inside item
						item_info.end_char += insert_length;
					}
					else { // Inserted after item
					}
				}
				else
				{
					const int delete_start = insert_index;
					const int delete_length = insert_length;
					const int delete_end = insert_index + insert_length;

					// Figure out new start-char position
					auto& start_char = item_info.start_char;
					if (delete_end <= start_char) {
						start_char -= delete_length;
					}
					else if (delete_start <= start_char) {
						start_char = delete_start;
					}

					// Figure out new end_char position
					auto& end_char = item_info.end_char;
					if (delete_end <= end_char) {
						end_char -= delete_length;
					}
					else if (delete_start <= end_char) {
						end_char = delete_start;
					}

					// Remove token if it was completely removed...
					if (start_char >= end_char) {
						dynamic_array_swap_remove(&line->item_infos, i);
						i -= 1;
					}
				}
			}
		}
	}
}

// Synchronizes lexing, folds + visible-indices, jump-positions and code-analysis-information
void syntax_editor_synchronize_code_information()
{
	auto& editor = syntax_editor;

	for (int tab_index = 0; tab_index < editor.tabs.size; tab_index++)
	{
		auto& tab = editor.tabs[tab_index];

		// Get changes since last sync
		auto now = history_get_timestamp(&tab.history);
		if (tab.last_code_info_synch.node_index == now.node_index) {
			continue;
		}

		Dynamic_Array<Code_Change> changes = dynamic_array_create<Code_Change>();
		SCOPE_EXIT(dynamic_array_destroy(&changes));
		history_get_changes_between(&tab.history, tab.last_code_info_synch, now, &changes);
		Code_Diff code_diff = code_diff_create_from_changes(changes);
		SCOPE_EXIT(code_diff_destroy(&code_diff));
		code_diff_update_tokenization(code_diff, tab.code, tab_index);
		code_diff_update_analysis_infos(code_diff, tab.code);
		code_diff_update_folds_jumps_and_breakpoints(code_diff, tab_index);
		tab.last_code_info_synch = history_get_timestamp(&tab.history);
	}
}

unsigned long compiler_thread_entry_fn(void* userdata)
{
	auto& editor = syntax_editor;

	bool worked = fiber_initialize();
	assert(worked, "panic");

	// logg("Compiler thread waiting now\n");
	semaphore_wait(editor.compiler_wait_semaphore);
	while (!editor.compiler_thread_should_close)
	{
		Compilation_Unit* compilation_unit = editor.compiler_main_unit;
		bool generate_code = editor.compiler_build_code;
		// logg("Compiler thread signaled\n");

		// Compile here
		compiler_compile(compilation_unit, generate_code ? Compile_Type::BUILD_CODE : Compile_Type::ANALYSIS_ONLY);
		compiler_analysis_update_source_code_information();

		// Artificial sleep, to see how 'sluggish' editor becomes...
		// timer_sleep_for(1.0);

		semaphore_increment(editor.compilation_finish_semaphore, 1);
		//logg("Compiler thread waiting now\n");
		semaphore_wait(editor.compiler_wait_semaphore);
	}

	semaphore_destroy(editor.compilation_finish_semaphore);
	semaphore_destroy(editor.compiler_wait_semaphore);
	thread_destroy(editor.compiler_thread);
	return 0;
}

// Checks if there are any new compilation infos, and starts a new compilation cycle if code has changed
void syntax_editor_synchronize_with_compiler(bool generate_code)
{
	syntax_editor_synchronize_code_information();
	auto& editor = syntax_editor;

	// Check if code has changed
	Editor_Tab& main_tab = syntax_editor.tabs[editor.main_tab_index == -1 ? editor.open_tab_index : editor.main_tab_index];
	bool should_compile = true;
	{
		bool code_has_changed = false;
		for (int i = 0; i < editor.tabs.size; i++) {
			auto& tab = editor.tabs[i];
			if (tab.history.current != tab.last_compiler_synchronized.node_index || tab.requires_recompile) {
				code_has_changed = true;
			}
		}
		if (editor.last_compile_main_unit != main_tab.compilation_unit) {
			code_has_changed = true;
		}

		if (!code_has_changed && !(generate_code && !editor.last_compile_was_with_code_gen)) {
			should_compile = false;
		}
	}

	// Get latest compiler work (Check semaphore if compilation was finished)
	bool got_compiler_update = editor.compiler_work_started;
	if (editor.compiler_work_started)
	{
		bool compiler_finished_compile = semaphore_try_wait(editor.compilation_finish_semaphore);
		if (!compiler_finished_compile) {
			return;
		}
		editor.compiler_work_started = false;

		// Update open_in_editor info
		for (int i = 0; i < compiler.compilation_units.size; i++) {
			compiler.compilation_units[i]->open_in_editor = false;
		}
		for (int i = 0; i < editor.tabs.size; i++) {
			editor.tabs[i].compilation_unit->open_in_editor = true;
		}
	}

	assert(!editor.compiler_work_started, "");
	if (!should_compile && !got_compiler_update) {
		return;
	}
	// logg("Synchronize with compiler: text-updated: %s, compiler-update_received: %s\n", (should_compile ? "TRUE" : "FALSE"), (got_compiler_update ? "TRUE" : "FALSE"));

	if (got_compiler_update)
	{
		if (editor.analysis_data != nullptr) {
			compiler_analysis_data_destroy(editor.analysis_data);
			editor.analysis_data = nullptr;
		}
		editor.analysis_data = compiler.analysis_data;
		compiler.analysis_data = nullptr;
		sort_error_indices();
		dynamic_array_reset(&editor.suggestions);
		hashtable_reset(&editor.filtered_passes);
		editor.compile_count += 1;
	}

	if (got_compiler_update || should_compile)
	{
		// Update compiler with new code (Swap out editor and compiler source-code)
		for (int i = 0; i < editor.tabs.size; i++)
		{
			auto& tab = editor.tabs[i];
			assert(tab.code != tab.compilation_unit->code, "");

			// Update code if necessary
			auto now = history_get_timestamp(&tab.history);
			Dynamic_Array<Code_Change> changes = dynamic_array_create<Code_Change>();
			SCOPE_EXIT(dynamic_array_destroy(&changes));
			history_get_changes_between(&tab.history, tab.last_compiler_synchronized, now, &changes);
			Code_Diff code_diff = code_diff_create_from_changes(changes);
			SCOPE_EXIT(code_diff_destroy(&code_diff));

			if (tab.last_compiler_synchronized.node_index != now.node_index || tab.requires_recompile)
			{
				// Keep the compiler-version of the code up-to-date (Add changes and tokenize correctly)
				for (int i = 0; i < changes.size; i++) {
					code_change_apply(tab.compilation_unit->code, &changes[i], true);
				}
				code_diff_update_tokenization(code_diff, tab.compilation_unit->code, i);
				tab.last_compiler_synchronized = history_get_timestamp(&tab.history);
				tab.requires_recompile = false;
			}

			if (got_compiler_update) {
				// Swap out code so we get newest analysis infos
				Source_Code* swap = tab.code;
				tab.code = tab.compilation_unit->code;
				tab.compilation_unit->code = swap;
				tab.history.code = tab.code;

				// Note: I don't think we should tokenize here, as tokenization is always correct when passed to compiler
				// code_diff_update_tokenization(code_diff, i);
				code_diff_update_analysis_infos(code_diff, tab.code);
				// Note: Don't do code_diff_update_folds_jumps_and_breakpoints, as this is data in tab, and is already up-to-date
				syntax_editor_update_line_visible_and_fold_info(i);
			}
		}
	}

	if (should_compile)
	{
		editor.last_compile_main_unit = main_tab.compilation_unit;
		editor.last_compile_was_with_code_gen = generate_code;

		// Start work in compile thread
		editor.compiler_main_unit = main_tab.compilation_unit;
		editor.compiler_build_code = generate_code;
		semaphore_increment(editor.compiler_wait_semaphore, 1);
		editor.compiler_work_started = true;
	}
}

void syntax_editor_wait_for_newest_compiler_info(bool build_code)
{
	auto& editor = syntax_editor;
	if (editor.compiler_work_started) {
		semaphore_wait(editor.compilation_finish_semaphore);
		semaphore_increment(editor.compilation_finish_semaphore, 1);
	}
	syntax_editor_synchronize_with_compiler(build_code);
	if (editor.compiler_work_started) {
		semaphore_wait(editor.compilation_finish_semaphore);
		semaphore_increment(editor.compilation_finish_semaphore, 1);
		syntax_editor_synchronize_with_compiler(build_code);
	}
}

void syntax_editor_save_state(String file_path)
{
	auto& editor = syntax_editor;

	String output = string_create();
	SCOPE_EXIT(string_destroy(&output));
	string_append_formated(&output, "open_tab=%d\n", editor.open_tab_index);
	string_append_formated(&output, "main_tab=%d\n", editor.main_tab_index);

	for (int i = 0; i < editor.tabs.size; i++)
	{
		auto& tab = editor.tabs[i];
		string_append_formated(&output, "tab=%s\n", tab.compilation_unit->filepath.characters);
		string_append_formated(&output, "cursor_line=%d\n", tab.cursor.line);
		string_append_formated(&output, "cursor_char=%d\n", tab.cursor.character);
		string_append_formated(&output, "cam_start=%d\n", tab.cam_start);
		for (int j = 0; j < tab.folds.size; j++) {
			auto& fold = tab.folds[j];
			string_append_formated(&output, "fold=%d;%d;%d\n", fold.line_start, fold.line_end, fold.indentation);
		}
		for (int j = 0; j < tab.breakpoints.size; j++) {
			auto& bp = tab.breakpoints[j];
			string_append_formated(&output, "breakpoint=%d\n", bp.line_number);
		}
	}
	for (int i = 0; i < editor.watch_values.size; i++) {
		string_append_formated(&output, "watch_value=%s\n", editor.watch_values[i].name.characters);
	}

	file_io_write_file(file_path.characters, array_create_static((byte*)output.characters, output.size));
}

// Returns true if breakpoint was removed
bool syntax_editor_toggle_line_breakpoint(int tab_index, int line_index)
{
	if (tab_index < 0 || tab_index >= syntax_editor.tabs.size) return false;
	auto& tab = syntax_editor.tabs[tab_index];
	if (line_index < 0 || line_index >= tab.code->line_count) return false;

	auto& breakpoints = tab.breakpoints;
	int index = -1;
	for (int i = 0; i < breakpoints.size; i++) {
		if (breakpoints[i].line_number == line_index) {
			index = i;
			break;
		}
	}

	// Remove breakpoint if already exists
	bool debugger_running = debugger_get_state(syntax_editor.debugger).process_state == Debug_Process_State::HALTED;
	if (index != -1) {
		auto& bp = breakpoints[index];
		if (bp.src_breakpoint != nullptr && debugger_running) {
			debugger_remove_source_breakpoint(syntax_editor.debugger, bp.src_breakpoint);
		}
		dynamic_array_swap_remove(&breakpoints, index);
		return true;
	}

	Line_Breakpoint bp;
	bp.line_number = line_index;
	bp.src_breakpoint = nullptr;
	bp.enabled = true;
	if (debugger_running) {
		bp.src_breakpoint = debugger_add_source_breakpoint(syntax_editor.debugger, bp.line_number, tab.compilation_unit);
	}
	dynamic_array_push_back(&breakpoints, bp);

	return false;
}

void syntax_editor_load_state(String file_path)
{
	auto& editor = syntax_editor;
	editor.mode = Editor_Mode::NORMAL;

	auto file_opt = file_io_load_text_file(file_path.characters);
	SCOPE_EXIT(file_io_unload_text_file(&file_opt));
	if (!file_opt.available) {
		return;
	}

	// Reset current data
	{
		debugger_reset(editor.debugger);
		for (int i = 0; i < editor.tabs.size; i++) {
			auto& tab = editor.tabs[i];
			dynamic_array_reset(&tab.breakpoints);
		}
		for (int i = 0; i < editor.watch_values.size; i++) {
			auto& value = editor.watch_values[i];
			string_destroy(&value.name);
			string_destroy(&value.value_as_text);
		}
		dynamic_array_reset(&editor.watch_values);
	}

	String session = file_opt.value;
	Array<String> lines = string_split(session, '\n');
	SCOPE_EXIT(string_split_destroy(lines));

	bool last_tab_valid = false;
	int last_tab_index = -1;
	bool first_tab = true;
	int open_tab_index = -5;
	int main_tab_index = -5;
	for (int i = 0; i < lines.size; i++)
	{
		String line = lines[i];
		auto seperator = string_find_character_index(&line, '=', 0);
		if (!seperator.available) {
			continue;
		}
		String setting = string_create_substring_static(&line, 0, seperator.value);
		String value = string_create_substring_static(&line, seperator.value + 1, line.size);
		if (setting.size == 0 || value.size == 0) continue;

		int* int_value_to_set = nullptr;
		if (string_equals_cstring(&setting, "open_tab")) {
			int_value_to_set = &open_tab_index;
		}
		else if (string_equals_cstring(&setting, "main_tab")) {
			int_value_to_set = &main_tab_index;
		}
		else if (string_equals_cstring(&setting, "tab"))
		{
			if (first_tab) {
				first_tab = false;
				editor.main_tab_index = -100; // See close tab to check why
				for (int i = 0; i < editor.tabs.size; i++) {
					syntax_editor_close_tab(0, true);
				}
				editor.main_tab_index = -1;
			}

			syntax_editor_switch_tab(syntax_editor_add_tab(value));
			last_tab_valid = true;
		}
		else if (string_equals_cstring(&setting, "cursor_char")) {
			if (last_tab_valid) {
				int_value_to_set = &editor.tabs[editor.open_tab_index].cursor.character;
			}
		}
		else if (string_equals_cstring(&setting, "cursor_line")) {
			if (last_tab_valid) {
				int_value_to_set = &editor.tabs[editor.open_tab_index].cursor.line;
			}
		}
		else if (string_equals_cstring(&setting, "cam_start")) {
			if (last_tab_valid) {
				int_value_to_set = &editor.tabs[editor.open_tab_index].cam_start;
			}
		}
		else if (string_equals_cstring(&setting, "fold"))
		{
			auto parts = string_split(value, ';');
			SCOPE_EXIT(string_split_destroy(parts));
			if (parts.size != 3) {
				continue;
			}

			int start, end, indentation;
			bool success = true;

			auto int_parse = string_parse_int(&parts[0]);
			success = success && int_parse.available;
			if (int_parse.available) { start = int_parse.value; }
			int_parse = string_parse_int(&parts[1]);
			success = success && int_parse.available;
			if (int_parse.available) { end = int_parse.value; }
			int_parse = string_parse_int(&parts[2]);
			success = success && int_parse.available;
			if (int_parse.available) { indentation = int_parse.value; }

			if (!last_tab_valid) {
				success = false;
			}

			// Sanity check (File may have been changed since last session)
			if (success)
			{
				auto& tab = editor.tabs[editor.open_tab_index];
				if (start < 0 || start >= tab.code->line_count) {
					success = false;
				}
				if (end < 0 || end >= tab.code->line_count) {
					success = false;
				}
				int min_indent = 99999;
				for (int i = start; i <= end && success && i < tab.code->line_count; i++) {
					auto src_line = source_code_get_line(tab.code, i);
					min_indent = math_minimum(min_indent, src_line->indentation);
				}
				if (min_indent != indentation) {
					success = false;
				}
			}

			if (success) {
				syntax_editor_add_fold(start, end, indentation);
			}
		}
		else if (string_equals_cstring(&setting, "breakpoint"))
		{
			if (last_tab_valid) {
				auto& tab = editor.tabs[editor.open_tab_index];

				auto int_parse = string_parse_int(&value);
				if (!int_parse.available) {
					continue;
				}
				int line_no = int_parse.value;
				if (line_no < 0 || line_no >= tab.code->line_count) {
					printf("Invalid breakpoint in session file\n");
					continue;
				}

				syntax_editor_toggle_line_breakpoint(editor.open_tab_index, line_no);
			}
		}
		else if (string_equals_cstring(&setting, "watch_value")) {
			Watch_Value watch_value;
			watch_value.name = string_copy(value);
			watch_value.value_as_text = string_create();
			watch_value.show_multiline = false;
			dynamic_array_push_back(&editor.watch_values, watch_value);
		}
		else {
			logg("Unrecognized session option: %s\n", setting.characters);
		}

		if (int_value_to_set != nullptr)
		{
			auto int_parse = string_parse_int(&value);
			if (int_parse.available) {
				*int_value_to_set = int_parse.value;
			}
		}
	}

	if (editor.tabs.size == 0) {
		syntax_editor_switch_tab(syntax_editor_add_tab(string_create_static("upp_code/editor_text.upp")));
		editor.main_tab_index = -1;
	}
	else
	{
		if (open_tab_index < editor.tabs.size) {
			editor.open_tab_index = open_tab_index;
		}
		if (main_tab_index == -1 || (main_tab_index < editor.tabs.size && main_tab_index >= 0)) {
			editor.main_tab_index = main_tab_index;
		}
	}
}



// Helpers
Error_Display error_display_make(String msg, Token_Range range, Compilation_Unit* unit, bool is_token_range_duplicate, int semantic_error_index)
{
	Error_Display result;
	result.message = msg;
	result.range = range;
	result.unit = unit;
	result.is_token_range_duplicate = is_token_range_duplicate;
	result.semantic_error_index = semantic_error_index;
	return result;
}

char get_cursor_char(char dummy_char)
{
	auto& editor = syntax_editor;
	auto& tab = editor.tabs[editor.open_tab_index];
	auto& c = tab.cursor;

	int index = c.character;
	if (editor.mode == Editor_Mode::INSERT) {
		index -= 1;
	}
	Source_Line* line = source_code_get_line(tab.code, c.line);
	if (index >= line->text.size || index < 0) return dummy_char;
	return line->text.characters[index];
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
bool syntax_editor_add_position_to_jump_list()
{
	auto& editor = syntax_editor;
	auto& tab = editor.tabs[editor.open_tab_index];
	auto& cursor = tab.cursor;

	auto& jump_list = tab.jump_list;
	auto& last_jump_index = tab.last_jump_index;
	if (jump_list.size == 0) {
		dynamic_array_push_back(&jump_list, cursor);
		last_jump_index = 0;
		return true;
	}

	if (last_jump_index >= 0)
	{
		Text_Index last_pos = jump_list.data[last_jump_index];
		if (math_absolute(last_pos.line - cursor.line) <= 7) {
			return false;
		}

		if (last_jump_index - 1 >= 0) {
			Text_Index pre_pre_pos = jump_list.data[last_jump_index - 1];
			if (pre_pre_pos.line == cursor.line) {
				return false;
			}
		}
		dynamic_array_rollback_to_size(&jump_list, math_minimum(jump_list.size, last_jump_index + 1));
	}
	else {
		dynamic_array_rollback_to_size(&jump_list, math_minimum(jump_list.size, 1));
	}

	dynamic_array_push_back(&jump_list, cursor);
	last_jump_index = jump_list.size - 1;
	return true;
}

void syntax_editor_goto_symbol_definition(Symbol* symbol)
{
	auto& editor = syntax_editor;

	// Switch tab to file with symbol
	auto unit = symbol->definition_unit;
	if (unit == nullptr) return;
	int index = syntax_editor_add_tab(unit->filepath); // Doesn't add a tab if already open
	syntax_editor_switch_tab(index);

	auto& tab = editor.tabs[editor.open_tab_index];
	tab.cursor = code_query_text_index_at_last_synchronize(symbol->definition_text_index, editor.open_tab_index, true);
	syntax_editor_sanitize_cursor();
}



// Code Queries
Text_Index code_query_text_index_at_last_synchronize(Text_Index text_index, int tab_index, bool move_forwards_in_time)
{
	auto& tab = syntax_editor.tabs[tab_index];

	// Get changes since last sync
	auto now = history_get_timestamp(&tab.history);
	if (tab.last_compiler_synchronized.node_index == now.node_index) {
		return text_index;
	}

	Dynamic_Array<Code_Change> changes = dynamic_array_create<Code_Change>();
	SCOPE_EXIT(dynamic_array_destroy(&changes));
	if (move_forwards_in_time) {
		history_get_changes_between(&tab.history, tab.last_compiler_synchronized, now, &changes);
	}
	else {
		history_get_changes_between(&tab.history, now, tab.last_compiler_synchronized, &changes);
	}

	// Go through changes and update infos
	for (int i = 0; i < changes.size; i++)
	{
		auto& change = changes[i];
		int line_index = -1;
		switch (change.type)
		{
		case Code_Change_Type::LINE_INSERT:
		{
			int line_index = change.options.line_insert.line_index;
			if (change.apply_forwards) {
				if (line_index <= text_index.line) {
					text_index.line += 1;
				}
			}
			else {
				if (line_index < text_index.line) {
					text_index.line -= 1;
				}
				else if (line_index == text_index.line) {
					text_index.line = math_maximum(text_index.line - 1, 0);
					text_index.character = 0;
				}
			}
			break;
		}
		case Code_Change_Type::CHAR_INSERT:
		case Code_Change_Type::TEXT_INSERT:
		{
			int insert_index = -1;
			int insert_length = 1;
			int line_index = -1;
			if (change.type == Code_Change_Type::CHAR_INSERT) {
				line_index = change.options.char_insert.index.line;
				insert_index = change.options.char_insert.index.character;
				insert_length = 1;
			}
			else {
				line_index = change.options.text_insert.index.line;
				insert_index = change.options.text_insert.index.character;
				insert_length = change.options.text_insert.text.size;
			}
			if (text_index.line != line_index) continue;

			if (change.apply_forwards) {
				if (insert_index <= text_index.character) {
					text_index.character += insert_length;
				}
			}
			else {
				if (text_index.character >= insert_index && text_index.character <= insert_index + insert_length) {
					text_index.character = insert_index;
				}
				else if (insert_index + insert_length < text_index.character) {
					text_index.character -= insert_length;
				}
			}
			break;
		}
		case Code_Change_Type::LINE_INDENTATION_CHANGE: {
			break;
		}
		default: panic("");
		}
	}
	return text_index;
}

void analysis_pass_append_polymorphic_infos(Analysis_Pass* pass, String* string, bool& out_is_poly, bool& out_is_base)
{
	out_is_poly = false;
	out_is_base = false;

	if (pass == nullptr) return;
	if (pass->origin_workload == nullptr) return;

	// Generate pass-string
	Workload_Base* workload = pass->origin_workload;

	int polymorphic_depth = 0;
	int set_poly_value_count = 0;
	while (workload != nullptr)
	{
		SCOPE_EXIT(workload = workload->poly_parent_workload);

		Array<Pattern_Variable_State> active_pattern_values = workload->active_pattern_variable_states;
		if (active_pattern_values.size > 0) {
			polymorphic_depth += 1;
			if (polymorphic_depth > 1) {
				string_append(string, "\n");
			}
		}
		for (int i = 0; i < active_pattern_values.size; i++)
		{
			auto poly_value = active_pattern_values[i];

			String* name = workload->active_pattern_variable_states_origin->pattern_variables[i].name;
			assert(name != nullptr, "");
			if (name == nullptr) {
				panic("Should not happen");
				continue;
			}
			for (int i = 0; i < polymorphic_depth - 1; i++) {
				string_append(string, "  ");
			}
			string_append_string(string, name);
			string_append(string, "=");

			if (poly_value.type != Pattern_Variable_State_Type::SET) {
				string_append(string, "Not Set\n");
				continue;
			}
			set_poly_value_count += 1;

			// Otherwise append name and value
			auto format = datatype_value_format_single_line();
			format.show_datatype = true;
			format.datatype_format = datatype_format_make_default();
			format.datatype_format.append_struct_poly_parameter_values = true;
			datatype_append_value_to_string(
				poly_value.options.value.type, &syntax_editor.analysis_data->type_system, poly_value.options.value.memory,
				string, format, 0, Memory_Source(nullptr), Memory_Source(nullptr)
			);

			if (i != active_pattern_values.size - 1) {
				string_append(string, "\n");
			}
		}
	}

	if (polymorphic_depth > 0) {
		out_is_poly = true;
	}
	out_is_base = set_poly_value_count == 0;
}

bool analysis_pass_passes_filters(Analysis_Pass* pass)
{
	// Check if pass already ran through filter
	bool* is_preferred_opt = hashtable_find_element(&syntax_editor.filtered_passes, pass);
	if (is_preferred_opt != nullptr) {
		return *is_preferred_opt;
	}

	// Check if valid/polymorphic
	if (pass == nullptr) {
		if (!syntax_editor.prefer_poly_passes) {
			hashtable_insert_element(&syntax_editor.filtered_passes, pass, true);
			return true;
		}
		return false;
	}
	if (pass->origin_workload == nullptr) {
		if (!syntax_editor.prefer_poly_passes) {
			hashtable_insert_element(&syntax_editor.filtered_passes, pass, true);
			return true;
		}
		return false;
	}

	// Generate pass-string (Contains Template-Values as text)
	String pass_string = string_create(32);
	SCOPE_EXIT(string_destroy(&pass_string));
	bool is_poly, is_base;
	analysis_pass_append_polymorphic_infos(pass, &pass_string, is_poly, is_base);

	// I'm currently assuming that if we don't have templated values, then it's non-polymorphic
	if (!is_poly) {
		hashtable_insert_element(&syntax_editor.filtered_passes, pass, !syntax_editor.prefer_poly_passes);
		return !syntax_editor.prefer_poly_passes;
	}
	if (syntax_editor.prefer_base_pass) {
		hashtable_insert_element(&syntax_editor.filtered_passes, pass, is_base);
		return is_base;
	}
	else if (is_base) {
		hashtable_insert_element(&syntax_editor.filtered_passes, pass, false);
		return false;
	}

	// Check filters
	auto& filters = syntax_editor.poly_pass_filters;
	bool passed_filters = false;
	if (filters.size == 0) {
		passed_filters = true;
	}
	for (int i = 0; i < filters.size; i++)
	{
		// Connected filters should be AND-ed together
		bool passes_filter = true;
		bool at_least_one_active_filter = false;
		while (i < filters.size)
		{
			Pass_Filter* filter = &filters[i];
			if (filter->is_active && filter->required_substring.size > 0) {
				at_least_one_active_filter = true;
				bool pass = string_contains_substring(pass_string, 0, filter->required_substring) != -1;
				if (!pass) {
					passes_filter = false;
				}
			}
			if (!filter->connected_to_next) {
				break;
			}
			i += 1;
		}
		if (!at_least_one_active_filter) {
			passes_filter = true;
		}

		if (passes_filter) {
			passed_filters = true;
		}
	}

	// Cache filter result
	hashtable_insert_element(&syntax_editor.filtered_passes, pass, passed_filters);
	return passed_filters;
}

// This function uses the Pass-Filter to find a pass that is preferred
Semantic_Info* code_analysis_item_get_selected_semantic_info(Code_Analysis_Item& item)
{
	assert(item.semantic_info_mapping_count != 0, "Should not happen with current implementation");
	if (item.semantic_info_mapping_count == 1) {
		return &syntax_editor.analysis_data->semantic_infos[item.semantic_info_mapping_start_index];
	}

	// Pick the first pass that passes filters
	for (int i = 0; i < item.semantic_info_mapping_count; i++) {
		auto& semantic_info = syntax_editor.analysis_data->semantic_infos[item.semantic_info_mapping_start_index + i];
		auto pass = semantic_info.pass;
		if (analysis_pass_passes_filters(pass)) {
			return &semantic_info;
		}
	}

	// If no preferred pass was found, just return the first one (Maybe we should notify the user if this happens)
	return &syntax_editor.analysis_data->semantic_infos[item.semantic_info_mapping_start_index];
}

struct Position_Info
{
	Semantic_Info_Expression* expression_info;
	Semantic_Info_Symbol* symbol_info;
	Semantic_Info_Call* call_info;
	int call_argument_index;
};

Position_Info code_query_find_position_infos(Text_Index index, Dynamic_Array<int>* errors_to_fill)
{
	Position_Info result;
	result.symbol_info = nullptr;
	result.expression_info = nullptr;
	result.call_info = nullptr;
	result.call_argument_index = -1;

	auto& editor = syntax_editor;
	auto& tab = editor.tabs[editor.open_tab_index];

	auto line = source_code_get_line(tab.code, index.line);
	auto& analysis_items = line->item_infos;
	int previous_expr_depth = -1;
	int previous_call_depth = -1;
	for (int i = 0; i < analysis_items.size; i++)
	{
		auto& item = analysis_items[i];

		bool on_item = false;
		if (item.start_char == item.end_char && index.character == item.start_char) {
			on_item = true;
		}
		else {
			on_item = index.character >= item.start_char && index.character < item.end_char;
		}
		if (!on_item) continue;
		if (item.semantic_info_mapping_count == 0) continue; // Shouldn't happen currently, but still check

		// Now we need to select the analysis pass we want to use
		int semantic_info_index = item.semantic_info_mapping_start_index;
		// TODO: Change selection based on some input or whatever

		auto semantic_info_ptr = code_analysis_item_get_selected_semantic_info(item);
		Semantic_Info& semantic_info = *semantic_info_ptr;
		switch (semantic_info.type)
		{
		case Semantic_Info_Type::CALL_INFORMATION:
		{
			if (item.tree_depth > previous_call_depth)
			{
				result.call_info = &semantic_info.options.call_info;
				previous_call_depth = item.tree_depth;

				// Find argument index by counting commas
				if (index.character == item.start_char) {
					result.call_argument_index = 0;
				}
				else if (index.character == item.end_char - 1) {
					result.call_argument_index = semantic_info.options.call_info.call_node->arguments.size - 1; // Note: -1 is also fine here
				}
				else
				{
					// Figure out closest argument
					int closest_index = -1;
					int min_dist = 1000000;
					for (int j = 0; j < analysis_items.size; j++) {
						const auto& other_item = analysis_items[j];
						const auto& other_info = editor.analysis_data->semantic_infos[other_item.semantic_info_mapping_start_index];
						if (other_info.type != Semantic_Info_Type::ARGUMENT) continue;
						auto arg_info = other_info.options.argument_info;
						if (arg_info.call_node != semantic_info.options.call_info.call_node) continue;
						int distance = math_minimum(
							math_absolute(index.character - other_item.start_char),
							math_absolute(index.character - (other_item.end_char - 1))
						);
						if (index.character >= other_item.start_char && index.character < other_item.end_char) {
							distance = 0;
						}

						if (distance < min_dist) {
							min_dist = distance;
							closest_index = arg_info.argument_index;
						}
					}
					result.call_argument_index = closest_index;
				}
			}
			break;
		}
		case Semantic_Info_Type::SYMBOL_LOOKUP: {
			result.symbol_info = &semantic_info.options.symbol_info;
			break;
		}
		case Semantic_Info_Type::ERROR_ITEM: {
			if (errors_to_fill != nullptr) {
				dynamic_array_push_back(errors_to_fill, semantic_info.options.error_index);
			}
			break;
		}
		case Semantic_Info_Type::EXPRESSION_INFO: {
			if (item.tree_depth > previous_expr_depth) {
				result.expression_info = &semantic_info.options.expression;
				previous_expr_depth = item.tree_depth;
			}
			break;
		}
		case Semantic_Info_Type::ARGUMENT:
		case Semantic_Info_Type::MARKUP: {
			break;
		}
		default: panic("");
		}
	}

	return result;
}

Symbol_Table* code_query_find_symbol_table_at_position(Text_Index index)
{
	auto& editor = syntax_editor;
	auto& tab = editor.tabs[editor.open_tab_index];
	if (editor.analysis_data == nullptr) return nullptr;

	index = code_query_text_index_at_last_synchronize(index, editor.open_tab_index, false);

	// We keep a set of closest ranges, so we can filter based on Pass afterwards
	Dynamic_Array<Symbol_Table_Range> closest_ranges = dynamic_array_create<Symbol_Table_Range>();
	SCOPE_EXIT(dynamic_array_destroy(&closest_ranges));

	auto& table_ranges = tab.code->symbol_table_ranges;
	int deepest_level = -1;
	for (int i = 0; i < table_ranges.size; i++) {
		auto& table_range = table_ranges[i];
		if (!text_range_contains(table_range.range, index)) continue;
		if (table_range.tree_depth < deepest_level) continue;
		if (table_range.tree_depth > deepest_level) {
			dynamic_array_reset(&closest_ranges);
		}
		dynamic_array_push_back(&closest_ranges, table_range);
		deepest_level = table_range.tree_depth;
	}
	if (closest_ranges.size == 0) return tab.code->root_table;

	// Filter based on pass
	for (int i = 0; i < closest_ranges.size; i++) {
		auto& table_range = closest_ranges[i];
		if (analysis_pass_passes_filters(table_range.pass)) {
			return table_range.symbol_table;
		}
	}

	// Otherwise return first table
	return closest_ranges[0].symbol_table;
}



// Code Completion
void suggestions_fill_with_file_directory(String search_path)
{
	auto& editor = syntax_editor;
	auto& tab = editor.tabs[editor.open_tab_index];

	Array<String> path_parts = string_split(search_path, '/');
	SCOPE_EXIT(string_split_destroy(path_parts));

	Directory_Crawler* crawler = editor.directory_crawler;
	directory_crawler_set_path_to_file_dir(crawler, tab.compilation_unit->filepath);

	bool success = true;
	for (int i = 0; i < path_parts.size - 1 && success; i++)
	{
		String part = path_parts[i];
		auto files = directory_crawler_get_content(crawler);
		bool found = false;
		for (int j = 0; j < files.size; j++) {
			auto file = files[j];
			if (string_equals(&file.name, &part)) {
				directory_crawler_go_down_one_directory(crawler, j);
				found = true;
				break;
			}
		}
		if (!found) {
			success = false;
		}
	}
	if (!success) {
		return;
	}

	// Fuzzy find file
	auto files = directory_crawler_get_content(crawler);
	fuzzy_search_start_search(path_parts[path_parts.size - 1], 10);
	for (int i = 0; i < files.size; i++) {
		auto& file = files[i];
		if (!file.is_directory) {
			if (!string_ends_with(file.name.characters, ".upp")) {
				continue;
			}
		}
		fuzzy_search_add_item(file.name, i);
	}

	auto items = fuzzy_search_get_results(true, 3);
	auto& suggestions = editor.suggestions;
	dynamic_array_reset(&suggestions);
	for (int i = 0; i < items.size; i++) {
		dynamic_array_push_back(&suggestions, suggestion_make_file(items[i].user_index));
	}
}

bool text_index_inside_comment_or_string_literal(Text_Index index, bool& out_inside_literal)
{
	auto code = syntax_editor.tabs[syntax_editor.open_tab_index].code;
	auto line = source_code_get_line(code, index.line);
	out_inside_literal = false;
	if (line->is_comment) {
		return true;
	}

	bool in_literal = false;
	bool prev_was_backslash = false;
	bool prev_was_slash = false;
	for (int i = 0; i < index.character; i++)
	{
		char curr = line->text.characters[i];
		if (curr == '\"') {
			if (!prev_was_backslash) {
				in_literal = !in_literal;
			}
			prev_was_backslash = false;
			prev_was_slash = false;
		}
		else if (curr == '\\') {
			prev_was_backslash = !prev_was_backslash;
			prev_was_slash = false;
		}
		else if (curr == '/') {
			if (!in_literal && prev_was_slash) {
				return true; // Inside comment
			}
			prev_was_backslash = false;
			prev_was_slash = true;
		}
	}

	if (in_literal) {
		out_inside_literal = true;
		return true;
	}
	return false;
}

Datatype* editor_pattern_variable_get_type(Pattern_Variable* pattern_variable, Analysis_Pass* pass, Pattern_Variable_State_Type& out_state)
{
	out_state = Pattern_Variable_State_Type::UNSET;
	if (pass == nullptr) return nullptr;

	Workload_Base* lookup_workload = pass->origin_workload;
	auto active_pattern_values = 
		pattern_variable_find_instance_workload(pattern_variable, lookup_workload, true)->active_pattern_variable_states;
	assert(active_pattern_values.data != nullptr, "");

	const auto& value = active_pattern_values[pattern_variable->value_access_index];
	out_state = value.type;
	switch (value.type)
	{
	case Pattern_Variable_State_Type::SET: return value.options.value.type;
	case Pattern_Variable_State_Type::PATTERN: return value.options.pattern_type;
	case Pattern_Variable_State_Type::UNSET: return upcast(pattern_variable->pattern_variable_type);
	default: panic("");
	}

	return nullptr;
}

void code_completion_find_suggestions()
{
	auto& editor = syntax_editor;
	auto& tab = editor.tabs[editor.open_tab_index];
	auto& suggestions = editor.suggestions;
	auto& cursor = tab.cursor;

	// Early exit if we have suggestions cached
	syntax_editor_synchronize_code_information();
	if (text_index_equal(tab.last_code_completion_query_pos, cursor) &&
		editor.last_code_completion_tab == editor.open_tab_index &&
		tab.last_code_completion_info_index == editor.compile_count)
	{
		return;
	}
	tab.last_code_completion_info_index = editor.compile_count;
	tab.last_code_completion_query_pos = cursor;
	editor.last_code_completion_tab = editor.open_tab_index;

	dynamic_array_reset(&suggestions);

	// Early exit if we aren't in completion context
	auto line = source_code_get_line(tab.code, cursor.line);
	bool inside_string_literal = false;
	{
		if (editor.mode != Editor_Mode::INSERT || cursor.character == 0 || tab.compilation_unit == nullptr || line->is_comment || line->is_folded) {
			return;
		}

		// Check if we are inside line comment
		if (text_index_inside_comment_or_string_literal(cursor, inside_string_literal)) {
			if (!inside_string_literal) {
				return;
			}
		}
	}

	auto test_char = [](String str, int index, char c) -> bool {
		if (index < 0 || index >= str.size) return false;
		return str.characters[index] == c;
		};

	// Import file code completion
	if (inside_string_literal)
	{
		bool add_file_suggestion = false;

		// Figure out if we are on import-keyword
		int word_end = cursor.character;
		auto char_is_quotation = [](char c, void* userdata) -> bool {return c == '\"'; };
		word_end = Motions::move_while_condition(line->text, cursor.character - 1, false, char_is_quotation, nullptr, true, true);
		String file_path = string_create_substring_static(&line->text, word_end + 1, cursor.character);
		if (test_char(line->text, word_end, '\"'))
		{
			word_end -= 1;
			// Skip whitespaces
			word_end = Motions::move_while_condition(line->text, word_end, false, char_is_whitespace, nullptr, false, true);
			int word_start = Motions::move_while_condition(line->text, word_end, false, char_is_valid_identifier, nullptr, false, false);
			if (word_start != word_end) {
				String substring = string_create_substring_static(&line->text, word_start, word_end + 1);
				if (string_equals_cstring(&substring, "import")) {
					add_file_suggestion = true;
				}
			}
		}

		if (add_file_suggestion) {
			suggestions_fill_with_file_directory(file_path);
		}

		return;
	}



	Dynamic_Array<Editor_Suggestion> unranked_suggestions = dynamic_array_create<Editor_Suggestion>();
	SCOPE_EXIT(dynamic_array_destroy(&unranked_suggestions));
	auto& ids = compiler.identifier_pool.predefined_ids;

	// Get partially typed word
	String partially_typed = string_create_static("");
	bool is_member_access = false;
	bool is_dot_call_access = false;
	bool is_path_lookup = false;
	bool is_hashtag = false;
	int op_index = cursor.character;
	{
		int word_start = Motions::move_while_condition(line->text, cursor.character - 1, false, char_is_valid_identifier, nullptr, false, false);
		partially_typed = string_create_substring_static(&line->text, word_start, cursor.character);
		if (partially_typed.size == 1 && !char_is_valid_identifier(partially_typed.characters[0])) {
			partially_typed = string_create_static("");
		}
		if (test_char(line->text, word_start, '#')) {
			is_hashtag = true;
		}
		if (test_char(line->text, word_start - 2, '-') && test_char(line->text, word_start - 1, '>')) {
			op_index = word_start - 2;
			is_dot_call_access = true;
		}
		else if (test_char(line->text, cursor.character - 2, '-') && test_char(line->text, cursor.character - 1, '>')) {
			is_dot_call_access = true;
			op_index = cursor.character - 2;
		}
		if (test_char(line->text, word_start - 1, '.') || test_char(line->text, cursor.character - 1, '.')) {
			is_member_access = true;
		}
		if (test_char(line->text, word_start - 1, '~') || test_char(line->text, cursor.character - 1, '~')) {
			is_path_lookup = true;
		}
	}
	fuzzy_search_start_search(partially_typed, 10);
	syntax_editor_synchronize_code_information();

	Text_Index cursor_char_index = tab.cursor;
	if (cursor_char_index.character > 0) {
		cursor_char_index.character -= 1;
	}
	Position_Info position_info = code_query_find_position_infos(cursor_char_index, nullptr);

	// Member-Access code completion
	if (is_hashtag) {
		fuzzy_search_add_item(*ids.hashtag_bake, unranked_suggestions.size);
		dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.hashtag_bake));
		fuzzy_search_add_item(*ids.hashtag_instanciate, unranked_suggestions.size);
		dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.hashtag_instanciate));
		fuzzy_search_add_item(*ids.hashtag_get_overload, unranked_suggestions.size);
		dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.hashtag_get_overload));
		fuzzy_search_add_item(*ids.hashtag_get_overload_poly, unranked_suggestions.size);
		dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.hashtag_get_overload_poly));
	}
	else if (is_dot_call_access && op_index > 0)
	{
		// Op index is at the -> start, so one before should be the 
		Position_Info dot_call_position_info = code_query_find_position_infos(text_index_make(cursor.line, op_index - 1), nullptr);
		Semantic_Info_Expression* expr_info = dot_call_position_info.expression_info;
		if (expr_info != nullptr)
		{
			Analysis_Pass* analysis_pass = expr_info->analysis_pass;
			Datatype* call_value_type = expr_info->info->cast_info.result_type;
			Symbol_Table* symbol_table = code_query_find_symbol_table_at_position(cursor_char_index);
			if (symbol_table != nullptr)
			{
				Arena arena = Arena::create(256);
				SCOPE_EXIT(arena.destroy());
				DynArray<Symbol*> symbols = symbol_table_query_all_symbols(symbol_table, Lookup_Type::DOT_CALL_LOOKUP, Symbol_Access_Level::INTERNAL, &arena);
				for (int i = 0; i < symbols.size; i++)
				{
					auto symbol = symbols[i];

					// Handle aliases
					String* original_id = symbol->id;
					while (symbol->type == Symbol_Type::ALIAS) {
						symbol = symbol->options.alias_for;
					}

					// Check if symbol is callable
					Datatype* value_type = nullptr;
					Call_Signature* call_signature = nullptr;
					switch (symbol->type)
					{
					case Symbol_Type::COMPTIME_VALUE: {
						value_type = symbol->options.constant.type;
						break;
					}
					case Symbol_Type::GLOBAL: {
						value_type = symbol->options.global->type;
						break;
					}
					case Symbol_Type::FUNCTION: {
						call_signature = symbol->options.function->signature;
						break;
					}
					case Symbol_Type::PARAMETER: {
						auto& param = symbol->options.parameter;
						value_type = param.function->function->signature->parameters[param.index_in_non_polymorphic_signature].datatype;
						break;
					}
					case Symbol_Type::PATTERN_VARIABLE: 
					{
						Pattern_Variable_State_Type state = Pattern_Variable_State_Type::UNSET;
						value_type = editor_pattern_variable_get_type(symbol->options.pattern_variable, analysis_pass, state);
						break;
					}
					case Symbol_Type::VARIABLE: {
						value_type = symbol->options.variable_type;
						break;
					}
					case Symbol_Type::POLYMORPHIC_FUNCTION: {
						call_signature = symbol->options.poly_function.poly_header->signature;
						break;
					}
					case Symbol_Type::HARDCODED_FUNCTION: {
						call_signature = editor.analysis_data->hardcoded_function_signatures[(int)symbol->options.hardcoded];
						break;
					}
					}

					// Check if value is callable
					if (value_type != nullptr) {
						value_type = value_type->base_type;
					}
					if (value_type != nullptr && value_type->type == Datatype_Type::FUNCTION_POINTER) {
						call_signature = downcast<Datatype_Function_Pointer>(value_type)->signature;
					}

					// Check if dot-call expr type matches function first parameter
					if (call_signature == nullptr) continue;
					if (call_signature->parameters.size == 0 || call_signature->return_type_index == 0) continue;
					auto param = call_signature->parameters[0];
					if (param.requires_named_addressing) continue;

					// Check if types are compatible (Does not check for casts...)
					Datatype* param_type = param.datatype;
					Datatype* arg_type = call_value_type;

					Expression_Cast_Info cast_info = cast_info_make_empty(arg_type, false);
					const char* out_error_msg = "";
					if (!try_updating_type_mods(cast_info, param_type->mods, &out_error_msg, &editor.analysis_data->type_system)) {
						continue;
					}
					arg_type = cast_info.result_type;
					if (param_type->contains_pattern) {
						if (!pattern_checker_create_and_check(&arena, param_type, arg_type)) {
							continue;
						}
					}
					else if (!types_are_equal(param_type, arg_type)) {
						continue;
					}

					fuzzy_search_add_item(*original_id, unranked_suggestions.size);
					dynamic_array_push_back(&unranked_suggestions, suggestion_make_symbol(symbol));
				}
			}
		}
	}
	else if (is_member_access)
	{
		Datatype* type = nullptr;

		auto expr_info = position_info.expression_info;
		if (expr_info != nullptr && expr_info->is_member_access && expr_info->member_access_info.value_type != nullptr)
		{
			// Note: For member-accesses, we need the type of the value, not the member-access expression
			type = expr_info->member_access_info.value_type;
			if (type->mods.optional_flags != 0) {
				fuzzy_search_add_item(string_create_static("value"), unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.value));
				type = nullptr;
			}
		}

		// Add possible member-accesses for given type
		if (type != 0)
		{
			auto original = type;
			type = type->base_type;
			switch (type->type)
			{
			case Datatype_Type::ARRAY:
			case Datatype_Type::SLICE: {
				fuzzy_search_add_item(string_create_static("data"), unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.data));
				fuzzy_search_add_item(string_create_static("size"), unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.size));
				break;
			}
			case Datatype_Type::OPTIONAL_TYPE: {
				fuzzy_search_add_item(*ids.value, unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.value));
				fuzzy_search_add_item(*ids.is_available, unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.is_available));
				break;
			}
			case Datatype_Type::STRUCT_PATTERN:
			case Datatype_Type::STRUCT:
			{
				Datatype_Struct* structure = 0;
				if (type->type == Datatype_Type::STRUCT) {
					structure = downcast<Datatype_Struct>(type);
				}
				else if (type->type == Datatype_Type::STRUCT_PATTERN) {
					structure = downcast<Datatype_Struct_Pattern>(type)->instance->header->origin.struct_workload->body_workload->struct_type;
				}

				Struct_Content* content = type_mods_get_subtype(structure, original->mods);
				auto& members = content->members;
				for (int i = 0; i < members.size; i++) {
					auto& mem = members[i];
					fuzzy_search_add_item(*mem.id, unranked_suggestions.size);
					dynamic_array_push_back(&unranked_suggestions, suggestion_make_struct_member(structure, mem.type, mem.id));
				}
				// if (content->subtypes.size > 0) {
				// 	fuzzy_search_add_item(*ids.tag, unranked_suggestions.size);
				// 	dynamic_array_push_back(&unranked_suggestions, suggestion_make_struct_member(structure, content->tag_member.type, ids.tag));
				// }
				for (int i = 0; i < content->subtypes.size; i++) {
					auto sub = content->subtypes[i];
					fuzzy_search_add_item(*sub->name, unranked_suggestions.size);
					dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(sub->name));
				}
				// Add base name if available
				if (original->mods.subtype_index->indices.size > 0) {
					Struct_Content* content = type_mods_get_subtype(structure, type->mods, type->mods.subtype_index->indices.size - 1);
					fuzzy_search_add_item(*content->name, unranked_suggestions.size);
					dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(content->name));
				}
				break;
			}
			case Datatype_Type::ENUM:
				auto& members = downcast<Datatype_Enum>(type)->members;
				for (int i = 0; i < members.size; i++) {
					auto& mem = members[i];
					fuzzy_search_add_item(*mem.name, unranked_suggestions.size);
					dynamic_array_push_back(&unranked_suggestions, suggestion_make_enum_member(downcast<Datatype_Enum>(type), mem.name));
				}
				break;
			}
		}
	}
	else if (is_path_lookup)
	{
		Symbol_Table* symbol_table = nullptr;

		auto text = line->text;
		int char_index = cursor.character - 1;
		// Move backwards to ~
		char_index = Motions::move_while_condition(text, char_index, false, char_is_valid_identifier, nullptr, false, true);
		if (test_char(text, char_index, '~')) {
			Position_Info position_info = code_query_find_position_infos(text_index_make(cursor.line, char_index - 1), nullptr);
			if (position_info.symbol_info != nullptr) {
				auto& symbol = position_info.symbol_info->symbol;
				if (symbol->type == Symbol_Type::MODULE) {
					symbol_table = symbol->options.upp_module->symbol_table;
				}
			}
		}

		if (symbol_table != nullptr)
		{
			Arena arena = Arena::create(256);
			SCOPE_EXIT(arena.destroy());
			auto results = symbol_table_query_all_symbols(
				symbol_table, Lookup_Type::LOCAL_SEARCH, Symbol_Access_Level::INTERNAL, &arena
			);
			for (int i = 0; i < results.size; i++) {
				fuzzy_search_add_item(*results[i]->id, unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_symbol(results[i]));
			}
		}
	}
	else
	{
		// Auto-Complete context _option_()
		if (unranked_suggestions.size == 0)
		{
			// Check if we should fill context options
			bool fill_context_options = false;
			int word_end = cursor.character - 1;
			word_end = Motions::move_while_condition(line->text, word_end, false, char_is_valid_identifier, nullptr, false, true);
			word_end = Motions::move_while_condition(line->text, word_end, false, char_is_whitespace, nullptr, false, true);
			int word_start = Motions::move_while_condition(line->text, word_end, false, char_is_valid_identifier, nullptr, false, false);
			if (word_start != word_end) {
				String substring = string_create_substring_static(&line->text, word_start, word_end + 1);
				if (string_equals_cstring(&substring, "context")) {
					fill_context_options = true;
				}
			}

			if (fill_context_options) {
				fuzzy_search_add_item(*ids.set_cast_option, unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.set_cast_option));
				fuzzy_search_add_item(*ids.id_import, unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.id_import));
				fuzzy_search_add_item(*ids.add_binop, unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.add_binop));
				fuzzy_search_add_item(*ids.add_unop, unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.add_unop));
				fuzzy_search_add_item(*ids.add_cast, unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.add_cast));
				fuzzy_search_add_item(*ids.add_array_access, unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.add_array_access));
				fuzzy_search_add_item(*ids.add_iterator, unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.add_iterator));
			}
		}

		// Auto complete continue/break
		if (unranked_suggestions.size == 0)
		{
			bool add_block_id_suggestions = false;
			int word_end = cursor.character - 1;
			word_end = Motions::move_while_condition(line->text, word_end, false, char_is_valid_identifier, nullptr, false, true);
			word_end = Motions::move_while_condition(line->text, word_end, false, char_is_whitespace, nullptr, false, true);
			int word_start = Motions::move_while_condition(line->text, word_end, false, char_is_valid_identifier, nullptr, false, false);
			if (word_start != word_end) {
				String substring = string_create_substring_static(&line->text, word_start, word_end + 1);
				if (string_equals_cstring(&substring, "continue") || string_equals_cstring(&substring, "break")) {
					add_block_id_suggestions = true;
				}
			}

			if (add_block_id_suggestions)
			{
				auto& id_ranges = tab.code->block_id_range;
				auto prev_cursor_index = code_query_text_index_at_last_synchronize(cursor, editor.open_tab_index, false);
				for (int i = 0; i < id_ranges.size; i++) {
					auto id_range = id_ranges[i];
					if (text_range_contains(id_range.range, prev_cursor_index)) {
						fuzzy_search_add_item(*id_range.block_id, unranked_suggestions.size);
						dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(id_range.block_id));
					}
				}
			}
		}

		// Exit if nothing specific was found
		if (unranked_suggestions.size == 0 && partially_typed.size == 0) {
			return;
		}

		// Search symbols
		Symbol_Table* symbol_table = code_query_find_symbol_table_at_position(cursor_char_index);
		if (unranked_suggestions.size == 0 && symbol_table != nullptr)
		{
			Arena arena = Arena::create(256);
			SCOPE_EXIT(arena.destroy());
			auto results = symbol_table_query_all_symbols(
				symbol_table, Lookup_Type::NORMAL, Symbol_Access_Level::INTERNAL, &arena
			);
			for (int i = 0; i < results.size; i++) {
				fuzzy_search_add_item(*results[i]->id, unranked_suggestions.size);
				dynamic_array_push_back(&unranked_suggestions, suggestion_make_symbol(results[i]));
			}
		}

		// Experimental: Add longer keywords to suggestions
		fuzzy_search_add_item(*ids.cast_pointer, unranked_suggestions.size);
		dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.cast_pointer, Syntax_Color::KEYWORD));
		fuzzy_search_add_item(*ids.defer_restore, unranked_suggestions.size);
		dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.defer_restore, Syntax_Color::KEYWORD));
		fuzzy_search_add_item(*ids.cast, unranked_suggestions.size);
		dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.cast, Syntax_Color::KEYWORD));
		fuzzy_search_add_item(*ids.defer, unranked_suggestions.size);
		dynamic_array_push_back(&unranked_suggestions, suggestion_make_id(ids.defer, Syntax_Color::KEYWORD));
	}

	// Add results to suggestions
	auto results = fuzzy_search_get_results(true, 3);
	for (int i = 0; i < results.size; i++) {
		dynamic_array_push_back(&syntax_editor.suggestions, unranked_suggestions[results[i].user_index]);
	}
}

void code_completion_insert_suggestion()
{
	auto& editor = syntax_editor;
	auto& tab = editor.tabs[editor.open_tab_index];

	String replace_string;
	if (editor.record_insert_commands)
	{
		auto& suggestions = editor.suggestions;
		string_reset(&editor.last_recorded_code_completion);
		if (suggestions.size == 0) return;
		replace_string = *suggestions[0].text;
		string_append_string(&editor.last_recorded_code_completion, &replace_string);
	}
	else {
		replace_string = editor.last_recorded_code_completion;
	}
	if (replace_string.size == 0) return;
	if (tab.cursor.character == 0) return;


	// Remove current token
	auto line = source_code_get_line(tab.code, tab.cursor.line);
	int start_pos = tab.cursor.character;
	if (char_is_valid_identifier(get_cursor_char('!'))) {
		start_pos = Motions::move_while_condition(line->text, tab.cursor.character - 1, false, char_is_valid_identifier, nullptr, false, false);
		Text_Editing::delete_text(text_index_make(tab.cursor.line, start_pos), tab.cursor.character, true);
	}

	// Insert suggestion instead
	tab.cursor.character = start_pos;
	Text_Editing::insert_text(tab.cursor, replace_string, true);
	tab.cursor.character += replace_string.size;
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
	dynamic_array_reset(&editor.suggestions);
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
	for (int repeat_index = 0; repeat_index < movement.repeat_count && repeat_movement; repeat_index++)
	{
		switch (movement.type)
		{
		case Movement_Type::MOVE_DOWN:
		case Movement_Type::MOVE_UP: {
			syntax_editor_synchronize_code_information(); // Synchronize visible indices of lines
			int dir = movement.type == Movement_Type::MOVE_UP ? -1 : 1;
			pos = sanitize_index(pos);
			pos.line = Line_Movement::move_visible_lines_up_or_down(pos.line, movement.repeat_count * dir);
			pos.line = Line_Movement::move_to_fold_boundary(pos.line, -1, false);
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
			Motions::move_while_in_set(pos, char_is_whitespace, nullptr, false, false); // Move backwards until start of word
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
		case Movement_Type::PARAGRAPH_START:
		case Movement_Type::PARAGRAPH_END:
		{
			auto line_is_empty = [](Source_Line* line, int _unused) -> bool { return line->text.size == 0; };
			if (movement.type == Movement_Type::PARAGRAPH_START)
			{
				int line_index = pos.line;
				line_index = Line_Movement::move_while_condition(line_index, -1, line_is_empty, false, 0, true); // Skip if we are on empty lines
				line_index = Line_Movement::move_while_condition(line_index, -1, line_is_empty, true, 0, false); // Goto start
				if (line_index == pos.line) { // If we are on paragraph start, redo one line backwards
					line_index = pos.line - 1;
					line_index = Line_Movement::move_while_condition(line_index, -1, line_is_empty, false, 0, true);
					line_index = Line_Movement::move_while_condition(line_index, -1, line_is_empty, true, 0, false);
				}
				pos = text_index_make(line_index, 0);
			}
			else {
				int line_index = pos.line;
				line_index = Line_Movement::move_while_condition(line_index, 1, line_is_empty, false, 0, true); // Skip if we are on empty lines
				line_index = Line_Movement::move_while_condition(line_index, 1, line_is_empty, true, 0, false); // Goto paragraph end
				if (line_index == pos.line) {
					line_index = pos.line + 1;
					line_index = Line_Movement::move_while_condition(line_index, 1, line_is_empty, false, 0, true);
					line_index = Line_Movement::move_while_condition(line_index, 1, line_is_empty, true, 0, false);
				}
				pos = text_index_make(line_index, 0);
			}

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
		case Movement_Type::REPEAT_TEXT_SEARCH_REVERSE:
		case Movement_Type::REPEAT_TEXT_SEARCH:
		{
			auto& tab = editor.tabs[editor.open_tab_index];
			auto& search_text = editor.search_text;

			bool search_reverse = editor.search_reverse;
			if (movement.type == Movement_Type::REPEAT_TEXT_SEARCH_REVERSE) {
				search_reverse = !search_reverse;
			}

			if (search_text.size == 0) {
				repeat_movement = false;
				break;
			}

			// Otherwise go through lines from start and try to find occurance
			Text_Index index = pos;
			bool found = false;
			for (int offset = 0; offset < tab.code->line_count; offset += 1)
			{
				int line_index = math_modulo(index.line + offset * (search_reverse ? -1 : 1), tab.code->line_count);
				auto line = source_code_get_line(tab.code, line_index);

				if (search_reverse)
				{
					if (line_index == index.line && index.character == 0) {
						continue;
					}

					// Search substrings until last substring is found...
					int last_substring_start = string_contains_substring(line->text, 0, search_text);
					if (last_substring_start == -1) {
						continue;
					}

					int max = line->text.size;
					if (index.line == line_index) {
						max = index.character - 1;
					}
					while (last_substring_start + 1 < max) {
						int substr = string_contains_substring(line->text, last_substring_start + 1, search_text);
						if (substr == -1 || substr > max) {
							break;
						}
						last_substring_start = substr;
					}

					if (last_substring_start + 1 < max) {
						found = true;
						index = text_index_make(line_index, last_substring_start);
						break;
					}
				}
				else
				{
					int pos = 0;
					if (line_index == index.line) {
						pos = index.character + 1;
					}
					int start = string_contains_substring(line->text, pos, search_text);
					if (start != -1) {
						index = text_index_make(line_index, start);
						found = true;
						break;
					}
				}
			}

			if (found) {
				pos = index;
			}
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
		int start_indentation = source_code_get_line(code, line_start)->indentation;
		if (line_start + 1 < code->line_count) {
			auto line = source_code_get_line(code, line_start);
			auto next_line = source_code_get_line(code, line_start + 1);
			if (next_line->indentation > line->indentation) {
				start_indentation += 1;
				line_start += 1;
			}
		}

		int block_indentation = start_indentation - (motion.repeat_count - 1);
		int block_start = Line_Movement::move_to_block_boundary(line_start, -1, false, block_indentation);
		int block_end = Line_Movement::move_to_block_boundary(line_start, 1, false, block_indentation);

		result.start = text_index_make(block_start, 0);
		result.end = text_index_make_line_end(code, block_end);
		if (motion.contains_edges) {
			if (result.start.line > 0) {
				result.start.line -= 1;
			}
		}
		break;
	}
	case Motion_Type::PARAGRAPH:
	{
		auto line_is_empty = [](Source_Line* line, int _unused) -> bool { return line->text.size == 0; };
		int line_start = Line_Movement::move_while_condition(pos.line, -1, line_is_empty, true, 0, false);
		int line_end = Line_Movement::move_while_condition(line_start, 1, line_is_empty, true, 0, false);

		if (motion.contains_edges) {
			line_start = Line_Movement::move_while_condition(line_start - 1, -1, line_is_empty, false, 0, false);
			line_end = Line_Movement::move_while_condition(line_start + 1, 1, line_is_empty, false, 0, false);
		}

		result.start = text_index_make(line_start, 0);
		result.end = text_index_make_line_end(code, line_end);

		break;
	}
	default:
		panic("Invalid motion value_type");
		result = text_range_make(pos, pos);
		break;
	}

	return result;
}

bool motion_is_line_motion(const Motion& motion) {
	return
		motion.motion_type == Motion_Type::BLOCK ||
		motion.motion_type == Motion_Type::PARAGRAPH ||
		(motion.motion_type == Motion_Type::MOVEMENT &&
			(motion.movement.type == Movement_Type::GOTO_START_OF_TEXT ||
				motion.movement.type == Movement_Type::GOTO_END_OF_TEXT ||
				motion.movement.type == Movement_Type::GOTO_LINE_NUMBER ||
				motion.movement.type == Movement_Type::MOVE_UP ||
				motion.movement.type == Movement_Type::MOVE_DOWN));
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

	int min_indent = 999999;
	for (int i = range.start.line; i <= range.end.line; i += 1) {
		auto line = source_code_get_line(code, i);
		min_indent = math_minimum(line->indentation, min_indent);
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
		for (int j = 0; j < line->indentation - min_indent; j++) {
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
	auto indent = source_code_get_line(code, cursor.line)->indentation;
	if (editor.yank_was_line)
	{
		int line_insert_index = cursor.line + (before_cursor ? 0 : 1);
		for (int i = 0; i < yank_lines.size; i++) {
			auto& yank_line = yank_lines[i];
			history_insert_line_with_text(history, line_insert_index + i, yank_line.indentation + indent, yank_line.text);
			auto range = text_range_make(text_index_make(line_insert_index + i, 0), text_index_make(line_insert_index + i, yank_line.text.size));
			Text_Editing::particles_add_in_range(range, vec3(0.2f, 0.5f, 0.2f));
		}
		cursor = text_index_make(line_insert_index, 0);
	}
	else
	{
		// Insert first line
		const Yank_Line& first_line = yank_lines[0];
		auto& pos = cursor;
		pos.character += before_cursor ? 0 : 1;
		pos = sanitize_index(pos);
		Text_Editing::insert_text(pos, first_line.text, true);

		if (yank_lines.size > 1)
		{
			// Insert the rest of the lines
			for (int i = 1; i < yank_lines.size; i++) {
				const Yank_Line& yank_line = yank_lines[i];
				history_insert_line_with_text(history, pos.line + i, yank_line.indentation + indent, yank_line.text);
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

void center_cursor_on_error(int error_index)
{
	auto& editor = syntax_editor;

	if (editor.analysis_data == nullptr) {
		return;
	}
	auto& errors = editor.analysis_data->compiler_errors;
	if (error_index < 0 || error_index >= errors.size) return;

	// Note: We need to take the error out of the error list here, as tab-switching will change the error index
	Compiler_Error_Info error = errors[error_index];
	if (error.unit != editor.tabs[editor.open_tab_index].compilation_unit) {
		int tab_index = syntax_editor_add_tab(error.unit->filepath);
		syntax_editor_switch_tab(tab_index);
	}
	auto& tab = editor.tabs[editor.open_tab_index];
	tab.cursor = code_query_text_index_at_last_synchronize(error.text_index, editor.open_tab_index, true);
	tab.cursor = sanitize_index(tab.cursor);
	auto cmd = Parsing::normal_mode_command_make(Normal_Command_Type::MOVE_VIEWPORT_CURSOR_CENTER, 1);
	normal_command_execute(cmd);
}

void normal_command_execute(Normal_Mode_Command& command)
{
	auto& editor = syntax_editor;
	auto& tab = editor.tabs[editor.open_tab_index];
	auto code = tab.code;
	auto& cursor = tab.cursor;
	auto history = &tab.history;

	auto line = source_code_get_line(code, cursor.line);

	// Filter commands during debug mode
	bool debugger_running = debugger_get_state(editor.debugger).process_state != Debug_Process_State::NO_ACTIVE_PROCESS;
	if (debugger_running)
	{
		bool command_ok = false;
		switch (command.type)
		{
		case Normal_Command_Type::MOVEMENT:
		case Normal_Command_Type::YANK_MOTION:
		case Normal_Command_Type::SCROLL_DOWNWARDS_HALF_PAGE:
		case Normal_Command_Type::SCROLL_UPWARDS_HALF_PAGE:
		case Normal_Command_Type::MOVE_VIEWPORT_CURSOR_TOP:
		case Normal_Command_Type::MOVE_VIEWPORT_CURSOR_CENTER:
		case Normal_Command_Type::MOVE_VIEWPORT_CURSOR_BOTTOM:
		case Normal_Command_Type::MOVE_CURSOR_VIEWPORT_TOP:
		case Normal_Command_Type::MOVE_CURSOR_VIEWPORT_CENTER:
		case Normal_Command_Type::MOVE_CURSOR_VIEWPORT_BOTTOM:
		case Normal_Command_Type::GOTO_NEXT_TAB:
		case Normal_Command_Type::GOTO_PREV_TAB:
		case Normal_Command_Type::GOTO_DEFINITION:
		case Normal_Command_Type::CLOSE_TAB:
		case Normal_Command_Type::FOLD_CURRENT_BLOCK:
		case Normal_Command_Type::FOLD_HIGHER_INDENT_IN_BLOCK:
		case Normal_Command_Type::UNFOLD_IN_BLOCK:
		case Normal_Command_Type::ENTER_VISUAL_BLOCK_MODE:
		case Normal_Command_Type::ENTER_FUZZY_FIND_DEFINITION:
		case Normal_Command_Type::ENTER_SHOW_ERROR_MODE:
		case Normal_Command_Type::ENTER_TEXT_SEARCH:
		case Normal_Command_Type::ENTER_TEXT_SEARCH_REVERSE:
		case Normal_Command_Type::SEARCH_IDENTIFER_UNDER_CURSOR:
		case Normal_Command_Type::VISUALIZE_MOTION:
		case Normal_Command_Type::GOTO_LAST_JUMP:
		case Normal_Command_Type::GOTO_NEXT_JUMP:
		case Normal_Command_Type::TOGGLE_LINE_BREAKPOINT:
			command_ok = true;
		}
		if (!command_ok) {
			return;
		}
	}

	// Save cursor pos
	history_set_cursor_pos(history, cursor);

	// Record command for repeat
	if (command.type == Normal_Command_Type::ENTER_INSERT_MODE_AFTER_MOVEMENT ||
		command.type == Normal_Command_Type::ENTER_INSERT_MODE_NEW_LINE_ABOVE ||
		command.type == Normal_Command_Type::ENTER_INSERT_MODE_NEW_LINE_BELOW ||
		command.type == Normal_Command_Type::DELETE_MOTION ||
		command.type == Normal_Command_Type::CHANGE_MOTION ||
		command.type == Normal_Command_Type::PUT_AFTER_CURSOR ||
		command.type == Normal_Command_Type::PUT_BEFORE_CURSOR)
	{
		editor.last_normal_command = command;
	}

	// Start complex command for non-history commands
	bool execute_as_complex = command.type != Normal_Command_Type::UNDO && command.type != Normal_Command_Type::REDO &&
		command.type != Normal_Command_Type::GOTO_NEXT_TAB && command.type != Normal_Command_Type::GOTO_PREV_TAB &&
		command.type != Normal_Command_Type::ENTER_SHOW_ERROR_MODE && command.type != Normal_Command_Type::CLOSE_TAB &&
		command.type != Normal_Command_Type::GOTO_DEFINITION;
	if (execute_as_complex) {
		history_start_complex_command(history);
	}

	Source_Code* previous_code = tab.code;
	SCOPE_EXIT(
		if (previous_code == editor.tabs[editor.open_tab_index].code) {
			syntax_editor_sanitize_cursor();
			history_set_cursor_pos(history, cursor);
		}
	if (execute_as_complex) {
		history_stop_complex_command(history);
	}
		);

	switch (command.type)
	{
	case Normal_Command_Type::MOVEMENT:
	{
		auto movement = command.options.movement;

		// Handle moving into fold
		if (line->is_folded && (movement.type == Movement_Type::MOVE_LEFT || movement.type == Movement_Type::MOVE_RIGHT)) {
			// Delete this fold
			auto& folds = tab.folds;
			for (int i = 0; i < folds.size; i++) {
				auto fold = folds[i];
				if (cursor.line >= fold.line_start && cursor.line <= fold.line_end) {
					dynamic_array_remove_ordered(&folds, i);
					cursor.line = fold.line_start;
					cursor.character = 0;
					syntax_editor_update_line_visible_and_fold_info(editor.open_tab_index);
				}
			}
			break;
		}

		cursor = movement_evaluate(movement, cursor);
		syntax_editor_sanitize_cursor();

		if (movement.type == Movement_Type::JUMP_ENCLOSURE || movement.type == Movement_Type::GOTO_LINE_NUMBER ||
			movement.type == Movement_Type::PARAGRAPH_START || movement.type == Movement_Type::PARAGRAPH_END)
		{
			syntax_editor_add_position_to_jump_list();
		}

		break;
	}
	case Normal_Command_Type::ENTER_INSERT_MODE_AFTER_MOVEMENT: {
		editor_enter_insert_mode();
		cursor = movement_evaluate(command.options.movement, cursor);
		syntax_editor_sanitize_cursor();
		break;
	}
	case Normal_Command_Type::ENTER_INSERT_MODE_NEW_LINE_BELOW:
	case Normal_Command_Type::ENTER_INSERT_MODE_NEW_LINE_ABOVE: {
		bool below = command.type == Normal_Command_Type::ENTER_INSERT_MODE_NEW_LINE_BELOW;
		int new_line_index = cursor.line + (below ? 1 : 0);
		history_insert_line(history, new_line_index, line->indentation);
		cursor = text_index_make(new_line_index, 0);
		editor_enter_insert_mode();
		break;
	}
	case Normal_Command_Type::YANK_MOTION:
	case Normal_Command_Type::DELETE_MOTION:
	{
		// First yank deleted text
		const auto& motion = command.options.motion;
		bool is_line_motion = motion_is_line_motion(motion);
		if (is_line_motion)
		{
			editor.yank_was_line = true;
			auto range = motion_evaluate(motion, cursor);
			if (command.type == Normal_Command_Type::YANK_MOTION) {
				Text_Editing::particles_add_in_range(range, vec3(0.2f, 0.2f, 0.8f));
			}

			int start_line = range.start.line;
			int end_line = range.end.line;

			int min_indent = 99999;
			for (int i = start_line; i <= end_line; i += 1) {
				auto line = source_code_get_line(code, i);
				min_indent = math_minimum(line->indentation, min_indent);
			}

			string_reset(&editor.yank_string);
			for (int i = start_line; i <= end_line; i += 1) {
				auto line = source_code_get_line(code, i);
				for (int j = 0; j < line->indentation - min_indent; j++) {
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
			if (command.type == Normal_Command_Type::YANK_MOTION) {
				Text_Editing::particles_add_in_range(range, vec3(0.2f, 0.2f, 0.8f));
			}
			string_reset(&editor.yank_string);
			text_range_append_to_string(range, &editor.yank_string);
		}
		// printf("Yanked: was_line = %s ----\n%s\n----\n", (editor.yank_was_line ? "true" : "false"), editor.yank_string.characters);

		// Delete if necessary
		if (command.type == Normal_Command_Type::DELETE_MOTION) {
			auto range = motion_evaluate(command.options.motion, cursor);
			Text_Editing::delete_text_range(range, is_line_motion, true);
			cursor = range.start;
		}
		break;
	}
	case Normal_Command_Type::CHANGE_MOTION: {
		auto range = motion_evaluate(command.options.motion, cursor);
		cursor = range.start;
		editor_enter_insert_mode();
		// bool is_line_motion = command.options.motion.motion_type == Motion_Type::LINE || command.options.motion.motion_type == Motion_Type::BLOCK;
		Text_Editing::delete_text_range(range, false, true);
		break;
	}
	case Normal_Command_Type::PUT_AFTER_CURSOR:
	case Normal_Command_Type::PUT_BEFORE_CURSOR: {
		syntax_editor_insert_yank(command.type == Normal_Command_Type::PUT_BEFORE_CURSOR);
		break;
	}
	case Normal_Command_Type::REPLACE_CHAR: {
		auto curr_char = Motions::get_char(cursor);
		if (curr_char == '\0' || curr_char == command.options.character) break;
		Text_Editing::delete_char(cursor, true);
		Text_Editing::insert_char(cursor, command.options.character, true);
		break;
	}
	case Normal_Command_Type::REPLACE_MOTION_WITH_YANK: {
		auto range = motion_evaluate(command.options.motion, cursor);
		cursor = range.start;
		if (text_index_equal(range.start, range.end)) {
			break;
		}
		bool is_line_motion = motion_is_line_motion(command.options.motion);
		Text_Editing::delete_text_range(range, is_line_motion, true);
		syntax_editor_insert_yank(true);
		break;
	}
	case Normal_Command_Type::FORMAT_MOTION: {
		auto range = motion_evaluate(command.options.motion, cursor);
		Identifier_Pool_Lock lock = identifier_pool_lock_aquire(&compiler.identifier_pool);
		for (int i = range.start.line; i <= range.end.line; i++) {
			Text_Editing::tokenize_and_auto_format_line(editor.open_tab_index, i, &lock);
		}
		identifier_pool_lock_release(lock);
		break;
	}
	case Normal_Command_Type::UNDO: {
		history_undo(history);
		auto cursor_history = history_get_cursor_pos(history);
		if (cursor_history.available) {
			cursor = cursor_history.value;
		}
		break;
	}
	case Normal_Command_Type::REDO: {
		history_redo(history);
		auto cursor_history = history_get_cursor_pos(history);
		if (cursor_history.available) {
			cursor = cursor_history.value;
		}
		break;
	}
	case Normal_Command_Type::SCROLL_DOWNWARDS_HALF_PAGE:
	case Normal_Command_Type::SCROLL_UPWARDS_HALF_PAGE: {
		int dir = command.type == Normal_Command_Type::SCROLL_DOWNWARDS_HALF_PAGE ? 1 : -1;
		int cursor_to_cam_start =
			source_code_get_line(tab.code, tab.cam_start)->visible_index -
			source_code_get_line(tab.code, cursor.line)->visible_index;
		tab.cam_start = Line_Movement::move_visible_lines_up_or_down(tab.cam_start, editor.visible_line_count / 2 * dir);

		cursor.line = Line_Movement::move_visible_lines_up_or_down(tab.cam_start, -cursor_to_cam_start);
		syntax_editor_sanitize_cursor();
		break;
	}
	case Normal_Command_Type::MOVE_VIEWPORT_CURSOR_TOP: {
		tab.cam_start = Line_Movement::move_visible_lines_up_or_down(tab.cursor.line, -MIN_CURSOR_DISTANCE);
		break;
	}
	case Normal_Command_Type::MOVE_VIEWPORT_CURSOR_CENTER: {
		tab.cam_start = Line_Movement::move_visible_lines_up_or_down(tab.cursor.line, -editor.visible_line_count / 2);
		break;
	}
	case Normal_Command_Type::MOVE_VIEWPORT_CURSOR_BOTTOM: {
		tab.cam_start = Line_Movement::move_visible_lines_up_or_down(tab.cursor.line, -(editor.visible_line_count - MIN_CURSOR_DISTANCE - 1));
		break;
	}
	case Normal_Command_Type::MOVE_CURSOR_VIEWPORT_TOP: {
		cursor.line = Line_Movement::move_visible_lines_up_or_down(tab.cam_start, MIN_CURSOR_DISTANCE);
		cursor.character = 0;
		syntax_editor_sanitize_cursor();
		break;
	}
	case Normal_Command_Type::MOVE_CURSOR_VIEWPORT_CENTER: {
		cursor.line = Line_Movement::move_visible_lines_up_or_down(tab.cam_start, editor.visible_line_count / 2);
		cursor.character = 0;
		syntax_editor_sanitize_cursor();
		break;
	}
	case Normal_Command_Type::MOVE_CURSOR_VIEWPORT_BOTTOM: {
		cursor.line = Line_Movement::move_visible_lines_up_or_down(tab.cam_start, editor.visible_line_count - MIN_CURSOR_DISTANCE - 1);
		cursor.character = 0;
		syntax_editor_sanitize_cursor();
		break;
	}
	case Normal_Command_Type::GOTO_NEXT_TAB:
	case Normal_Command_Type::GOTO_PREV_TAB: {
		int repeat_count = command.repeat_count;
		if (editor.tabs.size == 1) break;

		int next_tab_index = editor.open_tab_index;
		if (repeat_count != 0) {
			next_tab_index = repeat_count - 1;
			next_tab_index = math_clamp(next_tab_index, 0, editor.tabs.size - 1);
		}
		else {
			next_tab_index = next_tab_index + (command.type == Normal_Command_Type::GOTO_NEXT_TAB ? 1 : -1);
			next_tab_index = math_modulo(next_tab_index, editor.tabs.size);
		}

		syntax_editor_switch_tab(next_tab_index);
		break;
	}
	case Normal_Command_Type::CLOSE_TAB: {
		syntax_editor_close_tab(editor.open_tab_index);
		break;
	}
	case Normal_Command_Type::GOTO_DEFINITION:
	{
		syntax_editor_synchronize_code_information();
		Position_Info position_info = code_query_find_position_infos(cursor, nullptr);

		// Goto symbol definition if symbol
		if (position_info.symbol_info != nullptr) {
			syntax_editor_goto_symbol_definition(position_info.symbol_info->symbol);
			syntax_editor_add_position_to_jump_list();
		}

		// Handle Goto-Member access
		if (position_info.expression_info == nullptr) break;
		if (!position_info.expression_info->is_member_access) break;
		if (!position_info.expression_info->member_access_info.has_definition) break;
		auto& info = position_info.expression_info->member_access_info;
		Text_Index old_index = info.definition_index;

		// Switch to unit
		int index = syntax_editor_add_tab(info.member_definition_unit->filepath); // Doesn't add a tab if already open
		syntax_editor_switch_tab(index);

		auto& tab = editor.tabs[editor.open_tab_index];
		tab.cursor = code_query_text_index_at_last_synchronize(old_index, editor.open_tab_index, false);
		syntax_editor_sanitize_cursor();
		break;
	}
	case Normal_Command_Type::REPEAT_LAST_COMMAND:
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
	case Normal_Command_Type::ENTER_FUZZY_FIND_DEFINITION: {
		syntax_editor_wait_for_newest_compiler_info(false);
		string_reset(&editor.fuzzy_search_text);
		editor.search_text_edit = line_editor_make();
		dynamic_array_reset(&editor.suggestions);
		editor.mode = Editor_Mode::FUZZY_FIND_DEFINITION;
		break;
	}
	case Normal_Command_Type::ENTER_SHOW_ERROR_MODE: {
		syntax_editor_wait_for_newest_compiler_info(false);
		if (editor.analysis_data == nullptr) {
			break;
		}
		else if (editor.analysis_data->compiler_errors.size == 0) {
			break;
		}

		editor.mode = Editor_Mode::ERROR_NAVIGATION;
		editor.navigate_error_mode_cursor_before = tab.cursor;
		editor.navigate_error_mode_tab_before = editor.open_tab_index;
		editor.navigate_error_cam_start = 0;
		editor.navigate_error_index = 0;
		center_cursor_on_error(editor.error_indices_sorted[editor.navigate_error_index]);
		break;
	}
	case Normal_Command_Type::ENTER_VISUAL_BLOCK_MODE: {
		editor.mode = Editor_Mode::VISUAL_BLOCK;
		editor.visual_block_start_line = cursor.line;
		break;
	}
	case Normal_Command_Type::ENTER_TEXT_SEARCH_REVERSE:
	case Normal_Command_Type::ENTER_TEXT_SEARCH: {
		editor.search_text_edit = line_editor_make();
		editor.search_text_edit.select_start = 0;
		editor.search_reverse = command.type == Normal_Command_Type::ENTER_TEXT_SEARCH_REVERSE;
		editor.search_text_edit.pos = editor.search_text.size;
		editor.search_start_pos = tab.cursor;
		editor.search_start_cam_start = tab.cam_start;
		editor.mode = Editor_Mode::TEXT_SEARCH;
		break;
	}
	case Normal_Command_Type::SEARCH_IDENTIFER_UNDER_CURSOR:
	{
		Text_Range range = motion_evaluate(Parsing::motion_make(Motion_Type::WORD, 1, false), cursor);
		if (text_index_equal(range.start, range.end)) break;

		assert(range.start.line == range.end.line && range.start.line == cursor.line, "");
		String substr = string_create_substring_static(&line->text, range.start.character, range.end.character);
		editor.search_reverse = false;
		string_reset(&editor.search_text);
		string_append_string(&editor.search_text, &substr);

		cursor = movement_evaluate(Parsing::movement_make(Movement_Type::REPEAT_TEXT_SEARCH, 1), cursor);
		syntax_editor_add_position_to_jump_list();
		break;
	}
	case Normal_Command_Type::ADD_INDENTATION:
	case Normal_Command_Type::REMOVE_INDENTATION: {
		Motion motion = command.options.motion;
		Text_Range range = motion_evaluate(motion, cursor);
		for (int i = range.start.line; i <= range.end.line; i++) {
			Source_Line* line = source_code_get_line(code, i);
			int new_indent = line->indentation + (command.type == Normal_Command_Type::ADD_INDENTATION ? 1 : -1) * command.repeat_count;
			if (new_indent < 0) break;
			history_change_indent(history, i, new_indent);
		}
		break;
	}
	case Normal_Command_Type::FOLD_CURRENT_BLOCK: {
		// If we are currently in a fold we don't fold
		if (line->is_folded) {
			break;
		}

		// Figure out start/end of current block
		Text_Range range = motion_evaluate(Parsing::motion_make(Motion_Type::BLOCK, command.repeat_count, false), cursor);
		if (range.start.line == range.end.line) break;
		logg("Fold from %d to %d\n", range.start.line, range.end.line);
		int indent = math_maximum(0, line->indentation - math_maximum(0, command.repeat_count - 1));
		syntax_editor_add_fold(range.start.line, range.end.line, indent);

		break;
	}
	case Normal_Command_Type::FOLD_HIGHER_INDENT_IN_BLOCK:
	{
		// If we are currently in a fold we don't fold
		if (line->is_folded) {
			break;
		}
		Text_Range range = motion_evaluate(Parsing::motion_make(Motion_Type::BLOCK, command.repeat_count, false), cursor);
		int indent = math_maximum(0, line->indentation - math_maximum(0, command.repeat_count - 1));

		int last_start = -1;
		for (int i = range.start.line; i <= range.end.line; i++)
		{
			auto line = source_code_get_line(code, i);
			if (last_start == -1) {
				if (line->indentation > indent) {
					last_start = i;
				}
			}
			else {
				if (line->indentation <= indent || i == code->line_count - 1) {
					int start = last_start;
					int end = i - 1;
					if (i == code->line_count - 1) {
						end = code->line_count - 1;
					}
					last_start = -1;

					if (end != start) {
						syntax_editor_add_fold(start, end, indent + 1);
					}
				}
			}
		}
		if (last_start != -1 && last_start != range.end.line) {
			syntax_editor_add_fold(last_start, range.end.line, indent + 1);
		}

		break;
	}
	case Normal_Command_Type::UNFOLD_IN_BLOCK:
	{
		Text_Range range = motion_evaluate(Parsing::motion_make(Motion_Type::BLOCK, command.repeat_count, false), cursor);
		if (range.start.line == range.end.line) break;

		int indent = 100000;
		for (int i = range.start.line; i <= range.end.line; i++) {
			auto line = source_code_get_line(tab.code, i);
			indent = math_minimum(indent, line->indentation);
		}

		auto& folds = tab.folds;
		bool found = false;
		for (int i = 0; i < folds.size; i++) {
			auto& fold = folds[i];
			if (fold.line_start >= range.start.line && fold.line_start <= range.end.line) {
				dynamic_array_remove_ordered(&folds, i);
				i = i - 1;
				found = true;
			}
		}
		if (found) {
			syntax_editor_update_line_visible_and_fold_info(editor.open_tab_index);
		}
		break;
	}
	case Normal_Command_Type::GOTO_LAST_JUMP: {
		auto& jump_list = tab.jump_list;
		auto& last_jump_index = tab.last_jump_index;
		if (jump_list.size <= 0 || last_jump_index < 0) break;

		Text_Index jump_to = jump_list[last_jump_index];
		last_jump_index -= 1;

		if (jump_to.line == cursor.line) {
			if (last_jump_index < 0) {
				break;
			}
			jump_to = jump_list[last_jump_index];
			last_jump_index -= 1;
		}

		cursor = jump_to;
		syntax_editor_sanitize_cursor();
		break;
	}
	case Normal_Command_Type::GOTO_NEXT_JUMP: {
		auto& jump_list = tab.jump_list;
		auto& last_jump_index = tab.last_jump_index;
		if (jump_list.size <= 0) break;

		if (last_jump_index + 1 >= jump_list.size) break;
		last_jump_index += 1;
		Text_Index jump_to = jump_list[last_jump_index];

		if (jump_to.line == cursor.line) {
			if (last_jump_index + 1 >= jump_list.size) break;
			last_jump_index += 1;
			jump_to = jump_list[last_jump_index];
		}

		cursor = jump_to;
		syntax_editor_sanitize_cursor();
		break;
	}
	case Normal_Command_Type::VISUALIZE_MOTION: {
		Text_Range range = motion_evaluate(command.options.motion, cursor);
		if (text_index_equal(range.start, range.end)) {
			break;
		}

		Text_Editing::particles_add_in_range(range, vec3(1.0f));
		break;
	}
	case Normal_Command_Type::TOGGLE_LINE_BREAKPOINT:
	{
		syntax_editor_toggle_line_breakpoint(editor.open_tab_index, cursor.line);
		break;
	}
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

	SCOPE_EXIT(
		Identifier_Pool_Lock lock = identifier_pool_lock_aquire(&compiler.identifier_pool);
	Text_Editing::tokenize_and_auto_format_line(editor.open_tab_index, cursor.line, &lock);
	identifier_pool_lock_release(lock);
		);

	if (editor.record_insert_commands) {
		dynamic_array_push_back(&editor.last_insert_commands, input);
	}

	if (input.type != Insert_Command_Type::ENTER_REMOVE_ONE_INDENT) {
		editor.last_insert_was_shift_enter = false;
	}

	// Handle Universal Inputs
	switch (input.type)
	{
	case Insert_Command_Type::INSERT_CODE_COMPLETION: {
		syntax_editor_synchronize_code_information();
		code_completion_find_suggestions();
		if (editor.suggestions.size == 0) {
			syntax_editor_wait_for_newest_compiler_info(false);
			code_completion_find_suggestions();
		}

		if (editor.suggestions.size != 0) {
			code_completion_insert_suggestion();
		}
		else {
			Insert_Command cmd;
			cmd.letter = ' ';
			cmd.type = Insert_Command_Type::SPACE;
			insert_command_execute(cmd);
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
		if (editor.last_insert_was_shift_enter) {
			if (line->indentation > 0) {
				history_change_indent(history, cursor.line, line->indentation - 1);
			}
		}
		else {
			editor_split_line_at_cursor(-1);
		}
		editor.last_insert_was_shift_enter = true;
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
			Text_Editing::delete_text_range(text_range_make(start, end), false, true);
		}
		break;
	}
	case Insert_Command_Type::DELETE_TO_LINE_START: {
		if (cursor.character == 0) break;
		auto to = cursor;
		cursor.character = 0;
		Text_Editing::delete_text_range(text_range_make(cursor, to), false, true);
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
				skip_auto_input = pos < text.size && text[pos] == input.letter;
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
			Text_Editing::insert_char(cursor, double_char, true);
		}
		Text_Editing::insert_char(cursor, input.letter, true);
		pos += 1;
		break;
	}
	case Insert_Command_Type::SPACE:
	{
		// Handle strings and comments, where we always just add a space
		if (line->is_comment) {
			Text_Editing::insert_char(cursor, ' ', true);
			pos += 1;
			break;
		}
		if (pos == 0) break;

		// Check if inside comment or string literal
		bool _unused;
		if (text_index_inside_comment_or_string_literal(cursor, _unused)) {
			Text_Editing::insert_char(cursor, ' ', true);
			pos += 1;
			break;
		}

		char prev = text[pos - 1];
		if ((char_is_space_critical(prev)) || (pos == text.size && prev != ' ')) {
			Text_Editing::insert_char(cursor, ' ', true);
			pos += 1;
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
			break;
		}

		if (pos - 2 >= 0 && pos - 1 < text.size) {
			auto char_on = text.characters[pos - 1];
			auto char_prev = text.characters[pos - 2];
			if (char_on == ' ' && char_is_operator(char_prev)) {
				Text_Editing::delete_char(text_index_make(cursor.line, pos - 2), true);
				Text_Editing::delete_char(text_index_make(cursor.line, pos - 2), true);
				pos -= 2;
				break;
			}
		}

		Text_Editing::delete_char(text_index_make(cursor.line, pos - 1), true);
		pos -= 1;
		break;
	}
	case Insert_Command_Type::NUMBER_LETTER:
	case Insert_Command_Type::IDENTIFIER_LETTER:
	{
		Text_Editing::insert_char(cursor, input.letter, true);
		pos += 1;
		break;
	}
	default: panic("");
	}
}

void remove_folds_on_cursor()
{
	auto& editor = syntax_editor;
	auto& tab = editor.tabs[editor.open_tab_index];
	auto& cursor = tab.cursor;
	auto line = source_code_get_line(tab.code, cursor.line);

	if (line->is_folded) {
		auto& folds = tab.folds;
		for (int i = 0; i < folds.size; i++) {
			auto& fold = folds[i];
			if (fold.line_start <= cursor.line && fold.line_end >= cursor.line) {
				dynamic_array_remove_ordered(&folds, i);
				i -= 1;
			}
		}
		syntax_editor_update_line_visible_and_fold_info(editor.open_tab_index);
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
	case Editor_Mode::VISUAL_BLOCK:
	{
		auto& cmd_buffer = editor.command_buffer;

		// Filter out special/unnecessary messages
		if (msg.key_code == Key_Code::L && msg.ctrl_down && msg.key_down) {
			editor.mode = Editor_Mode::NORMAL;
			return;
		}
		// Filter out messages (Key Up messages + random shift or alt or ctrl clicks)
		if ((msg.character == 0 && !(msg.ctrl_down && msg.key_down)) || !msg.key_down || msg.key_code == Key_Code::ALT) {
			return;
		}

		auto& tab = editor.tabs[editor.open_tab_index];
		auto& cursor = tab.cursor;

		// Check if Block-Mode command is executed
		Normal_Command_Type cmd_type = Normal_Command_Type::MAX_ENUM_VALUE;
		switch (msg.character)
		{
		case 'c':
		case 'C': cmd_type = Normal_Command_Type::CHANGE_MOTION; break;
		case 'd':
		case 'D': cmd_type = Normal_Command_Type::DELETE_MOTION; break;
		case 'y':
		case 'Y': cmd_type = Normal_Command_Type::YANK_MOTION; break;
		case '>': cmd_type = Normal_Command_Type::REMOVE_INDENTATION; break;
		case '<': cmd_type = Normal_Command_Type::ADD_INDENTATION; break;
		default: break;
		}
		if (cmd_type != Normal_Command_Type::MAX_ENUM_VALUE)
		{
			Movement movement;
			if (cursor.line >= editor.visual_block_start_line) {
				movement = Parsing::movement_make(Movement_Type::MOVE_UP, cursor.line - editor.visual_block_start_line);
			}
			else {
				movement = Parsing::movement_make(Movement_Type::MOVE_DOWN, editor.visual_block_start_line - cursor.line);
			}
			auto cmd = Parsing::normal_mode_command_make_motion(cmd_type, 1, Parsing::motion_make_from_movement(movement));
			editor.mode = Editor_Mode::NORMAL;
			normal_command_execute(cmd);
			return;
		}

		// Check that character is valid
		if (msg.character < ' ' || msg.character > 128) {
			string_reset(&editor.command_buffer);
			return;
		}

		// Otherwise try to parse command
		int index = 0;
		string_append_character(&cmd_buffer, msg.character);;

		Parse_Result<Movement> result = Parsing::parse_movement(index);
		if (result.type == Parse_Result_Type::SUCCESS) {
			cursor = movement_evaluate(result.result, cursor);
			string_reset(&cmd_buffer);
		}
		else if (result.type == Parse_Result_Type::FAILURE) {
			string_reset(&cmd_buffer);
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
		{
			Normal_Command_Type command_type = Normal_Command_Type::MAX_ENUM_VALUE;
			if (msg.ctrl_down)
			{
				switch (msg.key_code)
				{
				case Key_Code::R: command_type = Normal_Command_Type::REDO; break;
				case Key_Code::U: command_type = Normal_Command_Type::SCROLL_UPWARDS_HALF_PAGE; break;
				case Key_Code::D: command_type = Normal_Command_Type::SCROLL_DOWNWARDS_HALF_PAGE; break;
				case Key_Code::O: command_type = Normal_Command_Type::GOTO_LAST_JUMP; break;
				case Key_Code::I: command_type = Normal_Command_Type::GOTO_NEXT_JUMP; break;
				case Key_Code::P: command_type = Normal_Command_Type::ENTER_FUZZY_FIND_DEFINITION; break;
				case Key_Code::E: command_type = Normal_Command_Type::ENTER_SHOW_ERROR_MODE; break;
				case Key_Code::G: command_type = Normal_Command_Type::GOTO_DEFINITION; break;
				}
			}

			if (command_type != Normal_Command_Type::MAX_ENUM_VALUE)
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
	case Editor_Mode::TEXT_SEARCH:
	{
		auto& search_text = editor.search_text;
		auto& tab = editor.tabs[editor.open_tab_index];

		// Exit if requested
		if (msg.key_code == Key_Code::L && msg.ctrl_down && msg.key_down) {
			tab.cursor = editor.search_start_pos;
			editor.mode = Editor_Mode::NORMAL;
			return;
		}
		if (msg.key_code == Key_Code::RETURN && msg.key_down) {
			editor.mode = Editor_Mode::NORMAL;
			syntax_editor_add_position_to_jump_list();
			return;
		}

		bool changed = line_editor_feed_key_message(editor.search_text_edit, &search_text, msg);
		if (!changed) break;

		// If search size = 0, return
		if (search_text.size == 0) {
			tab.cursor = editor.search_start_pos;
			tab.cam_start = editor.search_start_cam_start;
			break;
		}

		// Otherwise go through lines from start and try to find occurance
		tab.cursor = movement_evaluate(Parsing::movement_make(Movement_Type::REPEAT_TEXT_SEARCH, 1), editor.search_start_pos);
		remove_folds_on_cursor();
		break;
	}
	case Editor_Mode::FUZZY_FIND_DEFINITION:
	{
		// Exit if requested
		if (msg.key_code == Key_Code::L && msg.ctrl_down && msg.key_down) {
			editor.mode = Editor_Mode::NORMAL;
			dynamic_array_reset(&editor.suggestions);
			return;
		}

		if (msg.key_code == Key_Code::RETURN && msg.key_down)
		{
			if (editor.suggestions.size == 0) {
				return;
			}

			editor.mode = Editor_Mode::NORMAL;
			auto suggestion = editor.suggestions[0];
			if (suggestion.type == Suggestion_Type::SYMBOL) {
				syntax_editor_goto_symbol_definition(suggestion.options.symbol);
			}
			else
			{
				assert(suggestion.type == Suggestion_Type::FILE, "Nothing else should be in fuzzy find");
				File_Info file_info = directory_crawler_get_content(editor.directory_crawler)[suggestion.options.file_index_in_crawler];
				if (file_info.is_directory) {
					return;
				}
				String full_path = string_copy(directory_crawler_get_path(editor.directory_crawler));
				SCOPE_EXIT(string_destroy(&full_path));
				string_append(&full_path, "/");
				string_append_string(&full_path, &file_info.name);

				int tab_index = syntax_editor_add_tab(full_path);
				syntax_editor_switch_tab(tab_index);
			}
			syntax_editor_add_position_to_jump_list();
			remove_folds_on_cursor();
			return;
		}

		bool changed = false;
		bool auto_complete = msg.key_down && ((msg.key_code == Key_Code::TAB) || (msg.key_code == Key_Code::SPACE && msg.shift_down));
		if (auto_complete && msg.key_down && editor.suggestions.size > 0)
		{
			auto sugg = editor.suggestions[0];
			auto& search = editor.fuzzy_search_text;
			if (sugg.type == Suggestion_Type::SYMBOL)
			{
				auto symbol = sugg.options.symbol;

				int reset_pos = 0;
				Optional<int> result = string_find_character_index_reverse(&search, '~', search.size - 1);
				if (result.available) {
					reset_pos = result.value + 1;
				}
				string_remove_substring(&search, reset_pos, search.size);
				string_append_string(&search, symbol->id);
				if (symbol->type == Symbol_Type::MODULE) {
					string_append_character(&search, '~');
				}
				changed = true;
			}
			else
			{
				assert(sugg.type == Suggestion_Type::FILE, "Nothing else should be in fuzzy find");
				File_Info file_info = directory_crawler_get_content(editor.directory_crawler)[sugg.options.file_index_in_crawler];

				int reset_pos = 0;
				Optional<int> result = string_find_character_index_reverse(&search, '/', search.size - 1);
				if (result.available) {
					reset_pos = result.value + 1;
				}
				string_remove_substring(&search, reset_pos, search.size);
				string_append_string(&search, &file_info.name);
				if (file_info.is_directory) {
					string_append_character(&search, '/');
				}
				changed = true;
			}

			if (changed) {
				editor.search_text_edit.pos = search.size;
				editor.search_text_edit.select_start = search.size;
			}
		}

		// Otherwise let line handler use key-message
		if (!changed && !auto_complete) {
			changed = line_editor_feed_key_message(editor.search_text_edit, &editor.fuzzy_search_text, msg);
		}

		if (changed)
		{
			if (editor.fuzzy_search_text.size == 0) {
				dynamic_array_reset(&editor.suggestions);
				return;
			}

			auto& tab = editor.tabs[editor.open_tab_index];

			String search = editor.fuzzy_search_text;
			if (search.size >= 2 && search.characters[0] == '.' && search.characters[1] == '/') {
				search = string_create_substring_static(&editor.fuzzy_search_text, 2, editor.fuzzy_search_text.size);
				suggestions_fill_with_file_directory(search);
				break;
			}

			Symbol_Table* symbol_table = nullptr;
			Array<String> path_parts = string_split(editor.fuzzy_search_text, '~');
			SCOPE_EXIT(string_split_destroy(path_parts));

			bool is_intern = true;
			if (path_parts[0].size == 0) { // E.g. first term is a ~
				search = string_create_substring_static(&editor.fuzzy_search_text, 1, editor.fuzzy_search_text.size);
				symbol_table = tab.code->root_table;
				is_intern = false;
			}
			else
			{
				symbol_table = code_query_find_symbol_table_at_position(tab.cursor);
				is_intern = true;
			}

			// Note: Usually root-symbol table is never null, but it may happen
			// on the very first compile after initialize, or if compiler has analysis disabled
			if (symbol_table == nullptr) {
				break;
			}

			// Follow path
			Arena arena = Arena::create(256);
			SCOPE_EXIT(arena.destroy());
			bool search_includes = true;
			{
				for (int i = 0; i < path_parts.size - 1; i++)
				{
					String part = path_parts[i];
					if (i == 0 && part.size == 0) {
						continue;
					}

					String* id = identifier_pool_lock_and_add(&compiler.identifier_pool, part);
					DynArray<Symbol*> symbols = symbol_table_query_id(
						symbol_table, id, (search_includes ? Lookup_Type::NORMAL : Lookup_Type::LOCAL_SEARCH), 
						(is_intern ? Symbol_Access_Level::INTERNAL : Symbol_Access_Level::GLOBAL), &arena
					);
					search_includes = false;
					is_intern = false;

					Symbol_Table* next_table = nullptr;
					for (int j = 0; j < symbols.size; j++) {
						auto symbol = symbols[j];
						if (symbol->type == Symbol_Type::MODULE) {
							next_table = symbol->options.upp_module->symbol_table;
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
			DynArray<Symbol*> symbols = symbol_table_query_all_symbols(
				symbol_table, (search_includes ? Lookup_Type::NORMAL : Lookup_Type::LOCAL_SEARCH), 
				Symbol_Access_Level::INTERNAL, &arena
			);
			String last = path_parts[path_parts.size - 1];
			fuzzy_search_start_search(last, 10);
			for (int i = 0; i < symbols.size; i++) {
				auto symbol = symbols[i];
				if (symbol->definition_unit != 0) {
					fuzzy_search_add_item(*symbol->id, i);
				}
			}

			auto items = fuzzy_search_get_results(true, 3);
			auto& suggestions = editor.suggestions;
			dynamic_array_reset(&suggestions);
			for (int i = 0; i < items.size; i++) {
				dynamic_array_push_back(&suggestions, suggestion_make_symbol(symbols[items[i].user_index]));
			}
		}
		break;
	}
	case Editor_Mode::ERROR_NAVIGATION:
	{
		// Filter out special/unnecessary messages
		if (msg.key_code == Key_Code::L && msg.ctrl_down && msg.key_down) {
			editor.mode = Editor_Mode::NORMAL;
			editor.open_tab_index = editor.navigate_error_mode_tab_before;
			editor.tabs[editor.open_tab_index].cursor = editor.navigate_error_mode_cursor_before;
			return;
		}
		if (msg.key_code == Key_Code::RETURN && msg.key_down) {
			editor.mode = Editor_Mode::NORMAL;
			syntax_editor_add_position_to_jump_list();
			return;
		}
		// Filter out messages (Key Up messages + random shift or alt or ctrl clicks)
		if ((msg.character == 0 && !(msg.ctrl_down && msg.key_down)) || !msg.key_down || msg.key_code == Key_Code::ALT) {
			return;
		}

		if (msg.character == 'j' || msg.character == 'k')
		{
			auto& index = editor.navigate_error_index;
			auto& errors = editor.analysis_data->compiler_errors;
			index += msg.character == 'j' ? 1 : -1;
			index = math_clamp(index, 0, errors.size - 1);
			center_cursor_on_error(editor.error_indices_sorted[index]);
		}
		else if (msg.character == 'l' || msg.character == 'h') {
			center_cursor_on_error(editor.error_indices_sorted[editor.navigate_error_index]);
		}

		break;
	}
	default:panic("");
	}
}

int ir_block_find_first_instruction_hitting_statement_rec(IR_Code_Block* block, AST::Statement* statement, IR_Code_Block** out_code_block)
{
	for (int i = 0; i < block->instructions.size; i++)
	{
		auto& instr = block->instructions[i];

		if (instr.associated_statement == statement) {
			*out_code_block = block;
			return i;
		}

		int result_index = -1;
		switch (instr.type)
		{
		case IR_Instruction_Type::BLOCK: {
			result_index = ir_block_find_first_instruction_hitting_statement_rec(instr.options.block, statement, out_code_block);
			break;
		}
		case IR_Instruction_Type::WHILE: {
			result_index = ir_block_find_first_instruction_hitting_statement_rec(instr.options.while_instr.code, statement, out_code_block);
			break;
		}
		case IR_Instruction_Type::IF: {
			result_index = ir_block_find_first_instruction_hitting_statement_rec(instr.options.if_instr.true_branch, statement, out_code_block);
			if (result_index != -1) break;
			result_index = ir_block_find_first_instruction_hitting_statement_rec(instr.options.if_instr.false_branch, statement, out_code_block);
			break;
		}
		}

		if (result_index != -1) {
			return result_index;
		}
	}

	return -1;
}

// Note: For expression-evaluation, only member-access and array-access are currently supported
struct Watch_Value_Post_Op
{
	bool is_member_access;
	String member_name;
	int array_access;
};

void watch_values_update()
{
	auto& editor = syntax_editor;
	auto debugger = editor.debugger;
	auto debugger_state = debugger_get_state(debugger);
	if (debugger_state.process_state != Debug_Process_State::HALTED) {
		return;
	}

	Dynamic_Array<Watch_Value_Post_Op> post_ops = dynamic_array_create<Watch_Value_Post_Op>();
	SCOPE_EXIT(dynamic_array_destroy(&post_ops));
	Dynamic_Array<u8> byte_buffer = dynamic_array_create<u8>(16);
	SCOPE_EXIT(dynamic_array_destroy(&byte_buffer));
	for (int i = 0; i < editor.watch_values.size; i++)
	{
		auto& watch_value = editor.watch_values[i];
		string_reset(&watch_value.value_as_text);

		// Parse post-ops
		bool success = true;
		String value_name = string_create_substring_static(&watch_value.name, 0, watch_value.name.size);
		dynamic_array_reset(&post_ops);
		{
			int index = 0;
			auto& text = watch_value.name;
			int first_op_index = text.size + 1;

			// Skip characters until first . or [
			while (index < text.size)
			{
				char c = text[index];
				if (c == '.' || c == '[') {
					break;
				}
				if (!char_is_valid_identifier(c)) {
					success = false;
					break;
				}
				index += 1;
			}
			value_name = string_create_substring_static(&watch_value.name, 0, index);

			// Parse post ops
			while (index < text.size && success)
			{
				char c = text[index];
				if (c == '.')
				{
					first_op_index = math_minimum(first_op_index, index);

					int member_access_start = index + 1;
					int member_access_end = index + 1;
					while (member_access_end < text.size) {
						char c = text[member_access_end];
						if (char_is_valid_identifier(c)) {
							member_access_end += 1;
							continue;
						}
						break;
					}

					if (member_access_start != member_access_end) {
						Watch_Value_Post_Op op;
						op.is_member_access = true;
						op.member_name = string_create_substring_static(&text, member_access_start, member_access_end);
						dynamic_array_push_back(&post_ops, op);
					}
					else {
						success = false;
					}
					index = member_access_end;
					continue;
				}
				else if (c == '[')
				{
					first_op_index = math_minimum(first_op_index, index);

					index += 1;
					int value = 0;
					while (index < text.size) {
						char c = text[index];
						if (char_is_digit(c)) {
							value = value * 10 + char_digit_value(c);
						}
						else if (c == ']') {
							index += 1;
							break;
						}
						else {
							success = false;
							break;
						}
						index += 1;
					}
					Watch_Value_Post_Op op;
					op.is_member_access = false;
					op.array_access = value;
					dynamic_array_push_back(&post_ops, op);
				}
				else {
					index += 1;
				}
			}
		}

		if (!success) {
			string_append_formated(&watch_value.value_as_text, "Parsing Watch-Value failed");
			continue;
		}

		// Read value
		Debugger_Value_Read result = debugger_read_variable_value(debugger, value_name, &byte_buffer, editor.selected_stack_frame, 3);
		if (!result.success) {
			string_append_formated(&watch_value.value_as_text, result.error_msg);
			continue;
		}

		// Apply post ops to value
		byte* value_ptr = byte_buffer.data;
		Memory_Source local_source = Memory_Source(nullptr);
		Datatype* type = result.result_type;
		for (int i = 0; i < post_ops.size && success; i++)
		{
			auto& post_op = post_ops[i];

			type = datatype_get_non_const_type(type);
			// Dereference pointers
			while (type->type == Datatype_Type::POINTER)
			{
				void* pointer = nullptr;
				if (!local_source.read_single_value(value_ptr, &pointer)) {
					string_append(&watch_value.value_as_text, "Pointer dereference failed!");
					success = false;
					break;
				}
				if (pointer == nullptr) {
					string_append(&watch_value.value_as_text, "Auto-derefernce failed, pointer was null");
					success = false;
					break;
				}
				value_ptr = (byte*)pointer;
				type = datatype_get_non_const_type(downcast<Datatype_Pointer>(type)->element_type);
				local_source = debugger_get_state(debugger).memory_source;
			}

			if (post_op.is_member_access)
			{
				Struct_Content* content = nullptr;
				if (type->type == Datatype_Type::SUBTYPE) {
					content = type_mods_get_subtype(downcast<Datatype_Struct>(type->base_type), type->mods);
				}
				else if (type->type == Datatype_Type::STRUCT) {
					content = &downcast<Datatype_Struct>(type->base_type)->content;
				}
				else {
					string_append(&watch_value.value_as_text, "Member access only valid on structs in Watch-values");
					success = false;
					break;
				}

				int member_index = -1;
				for (int i = 0; i < content->members.size; i++) {
					auto member = content->members[i];
					if (string_equals(member.id, &post_op.member_name)) {
						member_index = i;
						break;
					}
				}

				if (member_index == -1) {
					string_append(&watch_value.value_as_text, "Struct did not contain member");
					success = false;
					break;
				}

				Struct_Member& member = content->members[member_index];
				value_ptr = value_ptr + member.offset;
				type = datatype_get_non_const_type(member.type);
			}
			else
			{
				// Array access
				Datatype* element_type = nullptr;
				int element_count = 0;
				if (type->type == Datatype_Type::ARRAY)
				{
					auto array_type = downcast<Datatype_Array>(type);
					if (!array_type->count_known) {
						string_append(&watch_value.value_as_text, "Array count not known");
						success = false;
						break;
					}
					element_count = (int)array_type->element_count;
					element_type = array_type->element_type;
				}
				else if (type->type == Datatype_Type::SLICE)
				{
					auto slice_type = downcast<Datatype_Slice>(type);
					Upp_Slice_Base slice_value;
					if (!local_source.read_single_value(value_ptr, &slice_value)) {
						string_append(&watch_value.value_as_text, "Slice memory access failed");
						success = false;
						break;
					}
					if (slice_value.data == nullptr) {
						string_append(&watch_value.value_as_text, "Slice data was nullptr");
						success = false;
						break;
					}

					element_type = slice_type->element_type;
					value_ptr = (byte*)slice_value.data;
					local_source = debugger_get_state(debugger).memory_source;
					element_count = slice_value.size;
				}
				else {
					string_append(&watch_value.value_as_text, "Array access only valid on Arrays/Slices");
					success = false;
					break;
				}

				// Add array-offset to value-ptr
				if (post_op.array_access < 0 || post_op.array_access >= element_count) {
					string_append(&watch_value.value_as_text, "Array access index out of bounds");
					success = false;
					break;
				}
				if (!element_type->memory_info.available) {
					string_append(&watch_value.value_as_text, "Array memory not available");
					success = false;
					break;
				}
				value_ptr = value_ptr + element_type->memory_info.value.size * post_op.array_access;
				type = element_type;
			}
		}
		if (!success) {
			continue;
		}


		Datatype_Value_Format format = datatype_value_format_multi_line(8, 3);
		if (!watch_value.show_multiline) {
			format = datatype_value_format_single_line();
			format.show_member_names = false;
			format.show_datatype = false;
		}
		datatype_append_value_to_string(
			type, &editor.analysis_data->type_system, value_ptr, &watch_value.value_as_text,
			format, 0, local_source, debugger_state.memory_source
		);
	}
}

void cursor_goto_selected_stack_frame()
{
	auto& editor = syntax_editor;

	// Find position of stack frame
	if (debugger_get_state(editor.debugger).process_state != Debug_Process_State::HALTED) {
		return;
	}
	Array<Stack_Frame> stack_frames = debugger_get_stack_frames(editor.debugger);
	if (editor.selected_stack_frame < 0 || editor.selected_stack_frame >= stack_frames.size) {
		return;
	}
	auto& frame = stack_frames[editor.selected_stack_frame];
	Assembly_Source_Information info = debugger_get_assembly_source_information(editor.debugger, frame.instruction_pointer);
	if (info.unit == nullptr) {
		return;
	}
	Compilation_Unit* unit = info.unit;
	int line_index = info.upp_line_index;

	// Switch to correct tab
	if (editor.tabs[editor.open_tab_index].compilation_unit != unit) {
		int tab_index = syntax_editor_add_tab(unit->filepath);
		syntax_editor_switch_tab(tab_index);
	}

	// Move cursor
	auto& tab = editor.tabs[editor.open_tab_index];
	tab.cursor.line = line_index;
	tab.cursor.character = 0;
	syntax_editor_sanitize_cursor();

	tab.last_render_cursor_pos.line = -1; // So that camera moves to cursor
	remove_folds_on_cursor();
}

void syntax_editor_update(bool& animations_running)
{
	auto& editor = syntax_editor;
	auto& input = syntax_editor.input;
	auto& mode = syntax_editor.mode;
	animations_running = false;

	UI_Input_Info input_info = ui_system_start_frame(input);
	String tmp_str = string_create();
	SCOPE_EXIT(string_destroy(&tmp_str));

	SCOPE_EXIT(
		for (int i = 0; i < editor.tabs.size; i++) {
			source_code_sanity_check(editor.tabs[i].code);
		}
	);

	// Update particles
	{
		auto time = timer_current_time_in_seconds();
		float delta = time - editor.last_update_time;
		editor.last_update_time = time;

		// Remove all 'old' particles
		auto& particles = editor.particles;
		{
			Dynamic_Array<Particle> survivors = dynamic_array_create<Particle>(particles.size);
			for (int i = 0; i < particles.size; i++) {
				auto& p = particles[i];
				if (p.creation_time + p.life_time < time) {
					continue;
				}
				dynamic_array_push_back(&survivors, p);
			}

			dynamic_array_destroy(&editor.particles);
			editor.particles = survivors;
		}

		// Move particles
		for (int i = 0; i < particles.size; i++)
		{
			auto& p = particles[i];
			auto vel = p.velocity;
			float t = (time - p.creation_time) / p.life_time;
			p.position += p.velocity * delta * (1.0f - t);
		}

		if (particles.size > 0) {
			animations_running = true;
		}
	}

	// Check shortcuts pressed
	static bool show_debugger_ui = false;
	bool handle_key_messages_in_editor = true;
	if (input->key_pressed[(int)Key_Code::O] &&
		input->key_down[(int)Key_Code::CTRL] &&
		input->key_down[(int)Key_Code::SHIFT])
	{
		editor_leave_insert_mode();
		editor.mode = Editor_Mode::NORMAL;
		String filename = string_create();
		SCOPE_EXIT(string_destroy(&filename));
		bool worked = file_io_open_file_selection_dialog(&filename);
		if (worked) {
			int tab_index = syntax_editor_add_tab(filename);
			syntax_editor_switch_tab(tab_index);
		}
	}
	else if (input->key_pressed[(int)Key_Code::S] && input->key_down[(int)Key_Code::CTRL]) {
		syntax_editor_save_text_file();
	}
	else if (input->key_pressed[(int)Key_Code::F8]) {
		syntax_editor_wait_for_newest_compiler_info(false);
		compiler_run_testcases(true);
	}
	else if (input->key_pressed[(int)Key_Code::B] && input->key_down[(int)Key_Code::CTRL])
	{
		show_debugger_ui = !show_debugger_ui;
		handle_key_messages_in_editor = false;
	}
	else if (input->key_pressed[(int)Key_Code::SPACE] && input->key_down[(int)Key_Code::CTRL]) {
		editor.show_semantic_infos = !editor.show_semantic_infos;
		handle_key_messages_in_editor = false;
	}
	else if (input->key_pressed[(int)Key_Code::T] && input->key_down[(int)Key_Code::CTRL])
	{
		editor.pass_filter_ui_open = !editor.pass_filter_ui_open;
	}

	// Pass-Filter UI
	if (editor.pass_filter_ui_open)
	{
		handle_key_messages_in_editor = false;
		if (input->key_pressed[(int)Key_Code::ESCAPE] || (input->key_pressed[(int)Key_Code::L] && input->key_down[(int)Key_Code::CTRL])) {
			editor.pass_filter_ui_open = false;
		}
	}
	if (editor.pass_filter_ui_open)
	{
		bool filters_updated = false;

		auto window = ui_system_add_window(window_style_make_floating("Pass-Filters"));
		ui_system_push_active_container(window.container, false);
		SCOPE_EXIT(ui_system_pop_active_container());

		ui_system_push_next_component_label("Prefer polymorphic passes:");
		bool new_prefer = ui_system_push_checkbox(editor.prefer_poly_passes);
		if (new_prefer != editor.prefer_poly_passes) {
			editor.prefer_poly_passes = new_prefer;
			filters_updated = true;
		}
		ui_system_push_next_component_label("Prefer base-pass:");
		bool prefer_base = ui_system_push_checkbox(editor.prefer_base_pass);
		if (prefer_base != editor.prefer_base_pass) {
			editor.prefer_base_pass = prefer_base;
			filters_updated = true;
		}

		ui_system_push_label("Filters:", false);
		auto& filters = editor.poly_pass_filters;
		for (int i = 0; i < filters.size; i++)
		{
			Pass_Filter& filter = filters[i];
			ui_system_push_active_container(ui_system_push_line_container(), false);
			SCOPE_EXIT(ui_system_pop_active_container());

			bool new_is_active = ui_system_push_checkbox(filter.is_active);
			if (new_is_active != filter.is_active) {
				filter.is_active = new_is_active;
				filters_updated = true;
			}

			bool new_connected = ui_system_push_checkbox(filter.connected_to_next);
			if (new_connected != filter.connected_to_next) {
				filter.connected_to_next = new_connected;
				filters_updated = true;
			}

			auto update = ui_system_push_text_input(filter.required_substring);
			if (update.text_was_changed) {
				string_reset(&filter.required_substring);
				string_append_string(&filter.required_substring, &update.new_text);
				filters_updated = true;
			}

			bool remove = ui_system_push_icon_button(ui_icon_make(Icon_Type::X_MARK, Icon_Rotation::NONE, vec3(1, 0, 0)), true);
			if (remove) {
				string_destroy(&filter.required_substring);
				filters_updated = true;
				dynamic_array_remove_ordered(&filters, i);
				i -= 1;
				continue;
			}
		}

		auto input = ui_system_push_button("Add Filter");
		if (input.was_pressed)
		{
			Pass_Filter filter;
			filter.connected_to_next = false;
			filter.is_active = true;
			filter.required_substring = string_create("");
			dynamic_array_push_back(&editor.poly_pass_filters, filter);
			filters_updated = true;
		}

		static bool section_line_passes_open = true;
		auto section_info = ui_system_push_subsection(section_line_passes_open, "Line-Passes", true);
		section_line_passes_open = section_info.enabled;
		if (section_info.enabled)
		{
			ui_system_push_active_container(section_info.container, false);
			SCOPE_EXIT(ui_system_pop_active_container());

			// Show all passes on current line
			Dynamic_Array<Analysis_Pass*> passes = dynamic_array_create<Analysis_Pass*>();
			SCOPE_EXIT(dynamic_array_destroy(&passes));
			auto line = source_code_get_line(editor.tabs[editor.open_tab_index].code, editor.tabs[editor.open_tab_index].cursor.line);
			for (int i = 0; i < line->item_infos.size; i++) {
				auto& item = line->item_infos[i];
				for (int j = 0; j < item.semantic_info_mapping_count; j++) {
					auto& semantic_info = editor.analysis_data->semantic_infos[item.semantic_info_mapping_start_index + j];
					dynamic_array_push_back(&passes, semantic_info.pass);
				}
			}
			// Remove duplicates
			for (int i = 0; i < passes.size; i++) {
				auto pass = passes[i];
				for (int j = i + 1; j < passes.size; j++) {
					if (passes[j] == pass) {
						dynamic_array_swap_remove(&passes, j);
						j -= 1;
					}
				}
			}

			for (int i = 0; i < passes.size; i++)
			{
				auto pass = passes[i];
				bool passes_filter = analysis_pass_passes_filters(pass);

				string_reset(&tmp_str);
				bool is_poly, is_base;
				analysis_pass_append_polymorphic_infos(pass, &tmp_str, is_poly, is_base);

				String tmp_header = string_create();
				SCOPE_EXIT(string_destroy(&tmp_header));
				string_append_formated(&tmp_header, "pass: %p ", pass);
				if (is_base) {
					string_append(&tmp_header, "Base ");
				}
				else if (is_poly) {
					string_append(&tmp_header, "Instance ");
				}
				else {
					string_append(&tmp_header, "Normal ");
				}
				string_append(&tmp_header, passes_filter ? "PASSES" : "FILTERED");

				auto header_container = ui_system_push_indented_container(0, passes_filter, vec3(Syntax_Color::BG_HIGHLIGHT));
				ui_system_push_active_container(header_container, true);
				ui_system_push_label(tmp_header, false);

				if (!is_poly || is_base) {
					continue;
				}

				auto container = ui_system_push_indented_container(2, false, vec3(0.0f));
				ui_system_push_active_container(container, false);

				Array<String> lines = string_split(tmp_str, '\n');
				SCOPE_EXIT(string_split_destroy(lines));
				for (int i = 0; i < lines.size; i++) {
					auto line = lines[i];
					if (line.size == 0) continue;
					ui_system_push_label(line, false);
				}
				ui_system_pop_active_container();
			}
		}

		if (filters_updated) {
			hashtable_reset(&editor.filtered_passes);
		}
	}

	// Debugger-UI
	const bool debugger_running = debugger_get_state(editor.debugger).process_state != Debug_Process_State::NO_ACTIVE_PROCESS;
	bool update_watch_values = false;
	bool action_continue = false;
	bool action_stop = false;
	bool action_step_over = false;
	bool action_step_into = false;
	bool action_step_out_of = false;
	bool action_print_line_info = false;
	bool action_open_debugger_command_line = false;
	if (input_info.has_keyboard_input) {
		handle_key_messages_in_editor = false;
	}

	if (show_debugger_ui)
	{
		Window_Handle handle = ui_system_add_window(window_style_make_anchored("Debugger_Info"));
		ui_system_push_active_container(handle.container, false);
		SCOPE_EXIT(ui_system_pop_active_container());

		static bool status_open = true;
		UI_Subsection_Info subsection_info = ui_system_push_subsection(status_open, "Status", false);
		status_open = subsection_info.enabled;
		if (status_open)
		{
			ui_system_push_active_container(subsection_info.container, false);
			SCOPE_EXIT(ui_system_pop_active_container());

			if (debugger_running)
			{
				Array<Stack_Frame> stack_frames = debugger_get_stack_frames(editor.debugger);
				Dynamic_Array<String> strings = dynamic_array_create<String>(stack_frames.size);
				SCOPE_EXIT(
					for (int i = 0; i < strings.size; i++) {
						string_destroy(&strings[i]);
					}
					dynamic_array_destroy(&strings);
				);

				for (int i = 0; i < stack_frames.size; i++)
				{
					auto frame = stack_frames[i];
					String str = string_create_formated("%2d ", i);

					bool found_info = false;
					Assembly_Source_Information info = debugger_get_assembly_source_information(editor.debugger, frame.instruction_pointer);
					if (info.ir_function != 0) {
						auto slot = editor.analysis_data->function_slots[info.ir_function->function_slot_index];
						if (slot.modtree_function != 0) {
							string_append_string(&str, slot.modtree_function->name);
							found_info = true;
						}
					}

					if (!found_info) {
						Closest_Symbol_Info symbol_info = debugger_find_closest_symbol_name(editor.debugger, frame.instruction_pointer);
						string_append_formated(&str, "[0x%08llX] ", frame.stack_frame_start_address);
						closest_symbol_info_append_to_string(editor.debugger, symbol_info, &str);
					}

					dynamic_array_push_back(&strings, str);
				}

				ui_system_push_next_component_label("Stack-Frames:");
				static Dropdown_State dropdown_state;
				dropdown_state.value = editor.selected_stack_frame;
				ui_system_push_dropdown(dropdown_state, dynamic_array_as_array(&strings));
				if (dropdown_state.value_was_changed)
				{
					editor.selected_stack_frame = dropdown_state.value;
					update_watch_values = true;
					cursor_goto_selected_stack_frame();
				}

				ui_system_push_active_container(ui_system_push_line_container(), false);
				action_continue = ui_system_push_icon_button(ui_icon_make(Icon_Type::TRIANGLE_LEFT, Icon_Rotation::NONE, vec3(0, 1, 0)), true);
				action_step_over = ui_system_push_icon_button(ui_icon_make(Icon_Type::ARROW_LEFT, Icon_Rotation::NONE, vec3(1)), true);
				action_step_into = ui_system_push_icon_button(ui_icon_make(Icon_Type::ARROW_LEFT, Icon_Rotation::ROT_90, vec3(1)), true);
				action_step_out_of = ui_system_push_icon_button(ui_icon_make(Icon_Type::ARROW_LEFT, Icon_Rotation::ROT_270, vec3(1)), true);
				ui_system_pop_active_container();
			}
			else {
				ui_system_push_label("Debugger not running", false);
			}
		}

		static bool breakpoints_open = true;
		subsection_info = ui_system_push_subsection(breakpoints_open, "Breakpoints", true);
		breakpoints_open = subsection_info.enabled;
		if (breakpoints_open)
		{
			ui_system_push_active_container(subsection_info.container, false);
			SCOPE_EXIT(ui_system_pop_active_container());

			auto push_tab_breakpoints = [&](int tab_index) {
				auto& tab = editor.tabs[tab_index];
				auto& breakpoints = tab.breakpoints;
				for (int i = 0; i < breakpoints.size; i++)
				{
					auto& breakpoint = breakpoints[i];
					// How do my breakpoints look like? Enabled/Disable checkbox, line number, line-preview, remove (X)?
					ui_system_push_active_container(ui_system_push_line_container(), false);
					SCOPE_EXIT(ui_system_pop_active_container());
					breakpoint.enabled = ui_system_push_checkbox(breakpoint.enabled);
					// Note: Breakpoint.enabled doesn't do anything yet

					bool remove = ui_system_push_icon_button(ui_icon_make(Icon_Type::X_MARK, Icon_Rotation::NONE, vec3(1, 0, 0)), true);
					if (remove) {
						syntax_editor_toggle_line_breakpoint(tab_index, breakpoint.line_number);
						i -= 1;
						continue;
					}

					string_reset(&tmp_str);
					String src_text = source_code_get_line(tab.code, breakpoint.line_number)->text;
					string_append_formated(&tmp_str, "#%05d, \"%s\"", breakpoint.line_number, src_text.characters);
					ui_system_push_label(tmp_str.characters, false);
				}
				};

			push_tab_breakpoints(editor.open_tab_index);
			for (int i = 0; i < editor.tabs.size; i++)
			{
				auto& tab = editor.tabs[i];
				if (i == editor.open_tab_index) continue;
				if (tab.breakpoints.size == 0) continue;
				String name = string_create_filename_from_path_static(&tab.compilation_unit->filepath);
				ui_system_push_label(name, false);
				push_tab_breakpoints(i);
			}
		}

		static bool watch_window_open = true;
		subsection_info = ui_system_push_subsection(watch_window_open, "Watch_Window", true);
		watch_window_open = subsection_info.enabled;
		if (watch_window_open)
		{
			ui_system_push_active_container(subsection_info.container, false);
			SCOPE_EXIT(ui_system_pop_active_container());

			auto& watch_values = editor.watch_values;
			for (int i = 0; i < watch_values.size; i++)
			{
				auto& watch_value = watch_values[i];
				ui_system_push_active_container(ui_system_push_line_container(), false);

				Text_Input_State state = ui_system_push_text_input(watch_value.name);
				if (state.text_was_changed) {
					string_reset(&watch_value.name);
					string_append_string(&watch_value.name, &state.new_text);
					string_reset(&watch_value.value_as_text);
					update_watch_values = true;
					animations_running = true;
				}

				bool show_multiline = ui_system_push_checkbox(watch_value.show_multiline);
				if (show_multiline != watch_value.show_multiline) {
					watch_value.show_multiline = show_multiline;
					update_watch_values = true;
				}

				bool pressed = ui_system_push_icon_button(ui_icon_make(Icon_Type::X_MARK, Icon_Rotation::NONE, vec3(1, 0, 0)), true);
				ui_system_pop_active_container();
				if (pressed) {
					string_destroy(&watch_value.name);
					string_destroy(&watch_value.value_as_text);
					dynamic_array_remove_ordered(&watch_values, i);
					i -= 1;
					animations_running = true;
					continue;
				}

				if (!debugger_running) {
					continue;
				}

				Array<String> lines = string_split(watch_value.value_as_text, '\n');
				SCOPE_EXIT(string_split_destroy(lines));
				for (int i = 0; i < lines.size; i++) {
					ui_system_push_label(lines[i], false);
				}
			}

			ui_system_push_next_component_label("New:");
			Text_Input_State input = ui_system_push_text_input(string_create_static("Expression"));
			if (input.text_was_changed)
			{
				bool is_valid = true;
				for (int i = 0; i < input.new_text.size; i++) {
					char c = input.new_text[i];
					if (!(char_is_valid_identifier(c) || c == '[' || c == ']' || c == '.')) {
						is_valid = false;
						break;
					}
				}

				if (is_valid) {
					Watch_Value new_value;
					new_value.name = string_copy(input.new_text);
					new_value.value_as_text = string_create();
					new_value.show_multiline = false;
					dynamic_array_push_back(&editor.watch_values, new_value);
					watch_values_update();
				}
			}
		}
	}

	// Handle Editor inputs
	if (handle_key_messages_in_editor) {
		for (int i = 0; i < input->key_messages.size; i++) {
			syntax_editor_process_key_message(input->key_messages[i]);
		}
	}
	syntax_editor_synchronize_with_compiler(false);

	// Generate GUI (Tabs)
	{
		auto root_node = gui_add_node(gui_root_handle(), gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_none());
		auto tabs_container = gui_add_node(root_node, gui_size_make_fill(), gui_size_make_fit(), gui_drawable_make_rect(vec4(0.1f, 0.1f, 0.1f, 1.0f)));
		if (editor.tabs.size > 1)
		{
			gui_node_set_layout(tabs_container, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::MIN);
			gui_node_set_padding(tabs_container, 2, 2, true);
			for (int i = 0; i < editor.tabs.size; i++)
			{
				auto& tab = editor.tabs[i];
				String name = tab.compilation_unit->filepath;
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
					syntax_editor_close_tab(i);
					i -= 1;
				}
			}
		}
		if (debugger_running)
		{
			auto center_container = gui_add_node(root_node, gui_size_make_fill(), gui_size_make_fit(), gui_drawable_make_rect(vec4(0.4f, 0.1f, 0.7f, 1.0f)));
			gui_node_set_layout(center_container, GUI_Stack_Direction::TOP_TO_BOTTOM, GUI_Alignment::CENTER);
			auto debug_container = gui_add_node(center_container, gui_size_make_fit(), gui_size_make_fit(), gui_drawable_make_rect(vec4(0.3f)));
			gui_node_set_padding(debug_container, 3, 3);
			gui_node_set_layout(debug_container, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::MIN);

			gui_push_text(debug_container, string_create_static("Debugger running!"), vec4(1.0f));
		}
		auto code_node = gui_add_node(root_node, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_none());
		if (code_node.first_time_created) {
			auto& info = rendering_core.render_information;
			auto height = info.backbuffer_height;
			editor.code_box = bounding_box_2_make_min_max(vec2(0, 0), vec2(info.backbuffer_width, info.backbuffer_height));
		}
		else {
			editor.code_box = gui_node_get_previous_frame_box(code_node);
		}
	}

	// Handle error navigation mode
	if (mode == Editor_Mode::ERROR_NAVIGATION && editor.analysis_data != 0 && editor.analysis_data->compiler_errors.size == 0) {
		mode = Editor_Mode::NORMAL;
	}

	bool synch_with_compiler = syntax_editor.input->key_pressed[(int)Key_Code::N] && syntax_editor.input->key_down[(int)Key_Code::CTRL];
	bool build_and_run = syntax_editor.input->key_pressed[(int)Key_Code::F5];
	if (editor.compiler_work_started) {
		animations_running = true;
	}

	// Handle debugger
	if (debugger_running)
	{
		bool execution_changed = false;

		// Print mapping infos if this is se case
		auto& keys = syntax_editor.input->key_pressed;
		if (keys[(int)Key_Code::F4]) { action_print_line_info = true; }
		else if (keys[(int)Key_Code::F5]) { action_continue = true; }
		else if (keys[(int)Key_Code::F6]) { action_step_over = true; }
		else if (keys[(int)Key_Code::F7]) { action_step_into = true; }
		else if (keys[(int)Key_Code::F8]) { action_step_out_of = true; }
		else if (keys[(int)Key_Code::F9]) { action_stop = true; }

		// Execute Actions
		if (action_print_line_info) {
			auto& tab = editor.tabs[editor.open_tab_index];
			debugger_print_line_translation(editor.debugger, tab.compilation_unit, tab.cursor.line, editor.analysis_data);
		}
		if (action_open_debugger_command_line) {
			window_set_focus_on_console();
			debugger_wait_for_console_command(editor.debugger);
			execution_changed = true;
		}
		else if (action_stop) {
			debugger_reset(editor.debugger);
			execution_changed = true;
		}
		else if (action_continue) {
			debugger_resume_until_next_halt_or_exit(editor.debugger);
			execution_changed = true;
		}
		else if (action_step_over) {
			debugger_step_over_statement(editor.debugger, false);
			execution_changed = true;
		}
		else if (action_step_into) {
			debugger_step_over_statement(editor.debugger, true);
			execution_changed = true;
		}
		else if (action_step_out_of) {
			debugger_step_out(editor.debugger);
			execution_changed = true;
		}

		// Update cursor/watch-values on changes
		if (execution_changed) {
			window_set_focus(editor.window);
			update_watch_values = true;
			editor.selected_stack_frame = 0;
			cursor_goto_selected_stack_frame();
		}
		if (update_watch_values) {
			watch_values_update();
		}

		return;
	}

	// For now always wait for newest compiler information
	if (build_and_run || synch_with_compiler) {
		syntax_editor_wait_for_newest_compiler_info(build_and_run);
	}

	if (!build_and_run || editor.analysis_data == nullptr) {
		return;
	}

	// Start debugger if we are in C-Compilation mode
	if (compiler_can_execute_c_compiled(editor.analysis_data))
	{
		editor_leave_insert_mode();
		editor.mode = Editor_Mode::NORMAL;

		double started = debugger_start_process(
			editor.debugger,
			"D:/Projects/UppLang/backend/build/main.exe",
			"D:/Projects/UppLang/backend/build/main.pdb",
			"D:/Projects/UppLang/backend/build/main.obj",
			editor.analysis_data
		);

		// Add breakpoints
		{
			auto& editor = syntax_editor;
			auto debugger = editor.debugger;

			for (int i = 0; i < editor.tabs.size; i++)
			{
				auto& tab = editor.tabs[i];
				auto& line_breakpoints = tab.breakpoints;
				for (int i = 0; i < line_breakpoints.size; i++) {
					auto& line_bp = line_breakpoints[i];
					line_bp.src_breakpoint = debugger_add_source_breakpoint(editor.debugger, line_bp.line_number, tab.compilation_unit);
				}
			}

		}
		debugger_resume_until_next_halt_or_exit(editor.debugger); // Run until we hit one of our breakpoints
		watch_values_update();

		window_set_focus(editor.window);
		// show_debugger_ui = true;
		return;
	}

	auto& errors = editor.analysis_data->compiler_errors;
	// Display error messages or run the program
	if (errors.size == 0)
	{
		auto exit_code = compiler_execute(editor.analysis_data);
		String output = string_create_empty(256);
		SCOPE_EXIT(string_destroy(&output));
		exit_code_append_to_string(&output, exit_code);
		logg("\nProgram Exit with Code: %s\n", output.characters);
	}
	else
	{
		// Print errors
		logg("Could not run program, there were errors:\n");
		String tmp = string_create();
		SCOPE_EXIT(string_destroy(&tmp));

		// Append Parser errors
		for (int i = 0; i < errors.size; i++)
		{
			const auto& error = errors[i];
			if (error.semantic_error_index != -1) {
				continue;
			}
			string_append_formated(&tmp, "\t%s\n", error.message);
		}

		// Append semantic errors
		semantic_analyser_append_semantic_errors_to_string(editor.analysis_data, &tmp, 1);
		logg(tmp.characters);

		// Enter error-show mode
		auto cmd = Parsing::normal_mode_command_make(Normal_Command_Type::ENTER_SHOW_ERROR_MODE, 1);
		normal_command_execute(cmd);
	}
}



// Syntax Highlighting
// Rendering
void symbol_get_infos(Symbol* symbol, Analysis_Pass* pass, Datatype** out_type, const char** out_text)
{
	Datatype* type = nullptr;
	const char* after_text = nullptr;
	switch (symbol->type)
	{
	case Symbol_Type::COMPTIME_VALUE:
		after_text = "Comptime";
		type = symbol->options.constant.type;
		break;
	case Symbol_Type::HARDCODED_FUNCTION:
		// type = upcast(hardcoded_type_to_signature(symbol->options.hardcoded, syntax_editor.analysis_data));
		break;
	case Symbol_Type::GLOBAL:
		after_text = "Global";
		type = upcast(symbol->options.global->type);
		break;
	case Symbol_Type::FUNCTION:
		after_text = "Function";
		// type = upcast(symbol->options.function->signature);
		break;
	case Symbol_Type::PARAMETER: 
	{
		after_text = "Parameter";
		auto& param_info = symbol->options.parameter;
		if (pass != nullptr)
		{
			auto progress = analysis_workload_try_get_function_progress(pass->origin_workload);
			if (progress != nullptr)
			{
				if (progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
					type = progress->poly_function.poly_header->signature->parameters[param_info.index_in_polymorphic_signature].datatype;
				}
				else if (progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
					type = progress->function->signature->parameters[param_info.index_in_non_polymorphic_signature].datatype;
				}
			}
		}
		if (type == nullptr) {
			type = param_info.function->function->signature->parameters[param_info.index_in_non_polymorphic_signature].datatype;
		}
		break;
	}
	case Symbol_Type::PATTERN_VARIABLE:
	{
		after_text = "Pattern Variable";
		Pattern_Variable_State_Type state = Pattern_Variable_State_Type::UNSET;
		type = editor_pattern_variable_get_type(symbol->options.pattern_variable, pass, state);
		if (state == Pattern_Variable_State_Type::UNSET) {
			after_text = "Pattern Variable (UNSET)";
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

	*out_text = after_text;
	*out_type = type;
}

enum class Line_Symbol_Type
{
	CURRENT_INSTRUCTION,
	ABOVE_STACK,
	LEAVING_FUNCTION,
	BREAKPOINT
};

struct Line_Symbol {
	Line_Symbol_Type type;
	int line_index;
};

vec3 token_get_syntax_color_based_on_surrounding(Dynamic_Array<Token> tokens, int index)
{
	auto& token = tokens[index];

	// Find default color based on token-type
	vec3 color = Syntax_Color::TEXT;
	switch (token.type)
	{
	case Token_Type::COMMENT: color = Syntax_Color::COMMENT; break;
	case Token_Type::INVALID: color = vec3(1.0f, 0.8f, 0.8f); break;
	case Token_Type::KEYWORD: color = Syntax_Color::KEYWORD; break;
	case Token_Type::IDENTIFIER: color = Syntax_Color::VARIABLE; break;
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
	default: break;
	}

	auto& test_op = [&](int offset, Operator op) -> bool {
		int i = index + offset;
		if (i < 0 || i >= tokens.size) return false;
		auto t = tokens[i];
		if (t.type != Token_Type::OPERATOR) return false;
		return t.options.op == op;
		};
	auto& test_keyword = [&](int offset, Keyword keyword) -> bool {
		int i = index + offset;
		if (i < 0 || i >= tokens.size) return false;
		auto t = tokens[i];
		if (t.type != Token_Type::KEYWORD) return false;
		return t.options.keyword == keyword;
		};
	auto& test_parenthesis = [&](int offset, Parenthesis parenthesis) -> bool {
		int i = index + offset;
		if (i < 0 || i >= tokens.size) return false;
		auto t = tokens[i];
		if (t.type != Token_Type::PARENTHESIS) return false;
		return t.options.parenthesis.is_open == parenthesis.is_open && t.options.parenthesis.type == parenthesis.type;
		};
	auto& test_identifier = [&](int offset) -> bool {
		int i = index + offset;
		if (i < 0 || i >= tokens.size) return false;
		auto t = tokens[i];
		return t.type == Token_Type::IDENTIFIER;
		};

	// Only do special highlighting on identifiers
	if (!test_identifier(0)) return color;

	// Test prev-tokens
	if (test_op(-1, Operator::DOT)) {
		return Syntax_Color::MEMBER;
	}
	else if (test_op(-1, Operator::DOT_CALL)) {
		return Syntax_Color::FUNCTION;
	}
	else if (test_op(-1, Operator::DOLLAR)) {
		return Syntax_Color::VALUE_DEFINITION;
	}

	// Test next token
	if (test_op(1, Operator::TILDE) || test_op(1, Operator::TILDE_STAR) || test_op(1, Operator::TILDE_STAR_STAR)) {
		return Syntax_Color::MODULE;
	}
	else if (test_op(1, Operator::DEFINE_INFER) || test_op(1, Operator::DEFINE_INFER_POINTER) ||
		test_op(1, Operator::DEFINE_INFER_RAW) || test_op(1, Operator::COLON))
	{
		return Syntax_Color::VALUE_DEFINITION;
	}

	// Check for known comptime definitions, e.g. struct, module, enum, function
	if (test_op(1, Operator::DEFINE_COMPTIME))
	{
		if (test_keyword(2, Keyword::STRUCT) || test_keyword(2, Keyword::ENUM) || test_keyword(2, Keyword::UNION)) {
			return Syntax_Color::VALUE_DEFINITION;
		}
		if (test_keyword(2, Keyword::MODULE)) {
			return Syntax_Color::MODULE;
		}

		// Test for functions
		if (!test_parenthesis(2, char_to_parenthesis('('))) return Syntax_Color::VALUE_DEFINITION;
		// Now we have identifier :: (
		if (!test_parenthesis(3, char_to_parenthesis(')'))) return Syntax_Color::FUNCTION;
		if (test_op(3, Operator::DOLLAR) || test_keyword(3, Keyword::MUTABLE)) return Syntax_Color::FUNCTION;
		if (test_identifier(3) && test_op(4, Operator::COLON)) return Syntax_Color::FUNCTION;

		// Otherwise we don't assume it's a function, but a normal value
		return Syntax_Color::VALUE_DEFINITION;
	}

	return color;
}

void syntax_editor_render()
{
	auto& editor = syntax_editor;
	editor.frame_index += 1;

	// Prepare Render
	syntax_editor_synchronize_code_information();
	syntax_editor_sanitize_cursor();

	auto state_2D = pipeline_state_make_alpha_blending();
	auto pass_context = rendering_core_query_renderpass("Context pass", state_2D, 0);
	auto pass_2D = rendering_core_query_renderpass("2D state", state_2D, 0);

	auto particle_state = pipeline_state_make_alpha_blending();
	particle_state.blending_state.equation = Blend_Equation::MAXIMUM;
	particle_state.blending_state.source = Blend_Operand::ONE;
	particle_state.blending_state.destination = Blend_Operand::ONE;
	auto pass_particles = rendering_core_query_renderpass("particles", particle_state, 0);

	render_pass_add_dependency(pass_2D, rendering_core.predefined.main_pass);
	render_pass_add_dependency(pass_context, pass_2D);
	render_pass_add_dependency(pass_2D, pass_particles);

	// Render particles
	{
		auto& particles = editor.particles;

		// Remove all 'old' particles
		{
			const float SURVIVAL_TIME = 2.0f;
			float time = (float)editor.last_update_time;
			Dynamic_Array<Particle> survivors = dynamic_array_create<Particle>(particles.size);
			for (int i = 0; i < particles.size; i++) {
				auto& p = particles[i];
				if (p.creation_time + SURVIVAL_TIME < time) {
					continue;
				}
				dynamic_array_push_back(&survivors, p);
			}

			dynamic_array_destroy(&editor.particles);
			editor.particles = survivors;
		}

		auto& predef = rendering_core.predefined;
		auto description = vertex_description_create({ predef.position2D, predef.color4, predef.texture_coordinates, predef.index });
		auto mesh = rendering_core_query_mesh("particles", description, true);

		// For each particle push the mesh
		auto positions = mesh_push_attribute_slice(mesh, predef.position2D, particles.size * 4);
		auto uvs = mesh_push_attribute_slice(mesh, predef.texture_coordinates, particles.size * 4);
		auto colors = mesh_push_attribute_slice(mesh, predef.color4, particles.size * 4);
		auto indices = mesh_push_attribute_slice(mesh, predef.index, particles.size * 6);
		vec2 screen_size_half = vec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height) / 2;
		for (int i = 0; i < particles.size; i++)
		{
			auto& particle = particles[i];
			vec2 min = particle.position - vec2(particle.radius / 2.0f);
			vec2 max = min + vec2(particle.radius);
			positions[i * 4 + 0] = (min - screen_size_half) / screen_size_half;
			positions[i * 4 + 1] = (vec2(max.x, min.y) - screen_size_half) / screen_size_half;
			positions[i * 4 + 2] = (max - screen_size_half) / screen_size_half;
			positions[i * 4 + 3] = (vec2(min.x, max.y) - screen_size_half) / screen_size_half;
			uvs[i * 4 + 0] = vec2(0);
			uvs[i * 4 + 1] = vec2(1, 0);
			uvs[i * 4 + 2] = vec2(1, 1);
			uvs[i * 4 + 3] = vec2(0, 1);
			float t = (editor.last_update_time - particle.creation_time) / particle.life_time;
			float alpha = 1.0f;
			float FADE_DELAY = 0.3f;
			if (t > FADE_DELAY) {
				alpha = 1.0f - (t - FADE_DELAY) / (1.0f - FADE_DELAY);
			}
			colors[i * 4 + 0] = vec4(particle.color, alpha);
			colors[i * 4 + 1] = vec4(particle.color, alpha);
			colors[i * 4 + 2] = vec4(particle.color, alpha);
			colors[i * 4 + 3] = vec4(particle.color, alpha);
			indices[i * 6 + 0] = i * 4 + 0;
			indices[i * 6 + 1] = i * 4 + 1;
			indices[i * 6 + 2] = i * 4 + 2;
			indices[i * 6 + 3] = i * 4 + 0;
			indices[i * 6 + 4] = i * 4 + 2;
			indices[i * 6 + 5] = i * 4 + 3;
		}

		auto shader = rendering_core_query_shader("particle.glsl");
		render_pass_draw(pass_particles, shader, mesh, Mesh_Topology::TRIANGLES, {});
	}

	auto& tab = editor.tabs[editor.open_tab_index];
	auto code = tab.code;
	auto& cursor = tab.cursor;
	bool debugger_running = debugger_get_state(editor.debugger).process_state != Debug_Process_State::NO_ACTIVE_PROCESS;

	// Calculate camera line range + on_screen_indices for lines
	auto& cam_start = tab.cam_start;
	auto& cam_end = tab.cam_end;
	auto& code_box = editor.code_box;
	{
		auto& line_count = editor.visible_line_count;
		line_count = (int)((code_box.max.y - code_box.min.y) / editor.text_display.char_size.y) + 1;

		int last_visual_line_index = source_code_get_line(code, code->line_count - 1)->visible_index;
		// Set cam-start to first line in fold
		cam_start = math_clamp(cam_start, 0, code->line_count - 1);
		cam_start = Line_Movement::move_to_fold_boundary(cam_start, -1, false);
		cam_end = Line_Movement::move_visible_lines_up_or_down(cam_start, line_count);
		cam_end = Line_Movement::move_to_fold_boundary(cam_end, 1, false);

		// Clamp camera to cursor if cursor moved or text changed
		History_Timestamp timestamp = history_get_timestamp(&tab.history);
		if (!text_index_equal(tab.last_render_cursor_pos, cursor) || tab.last_render_timestamp.node_index != timestamp.node_index)
		{
			tab.last_render_cursor_pos = cursor;
			tab.last_render_timestamp = timestamp;

			int cam_start_visible = source_code_get_line(code, cam_start)->visible_index;
			int cam_end_visible = source_code_get_line(code, cam_end)->visible_index;
			int cursor_line = source_code_get_line(code, cursor.line)->visible_index;
			bool updated = false;
			if (cursor_line < cam_start_visible + MIN_CURSOR_DISTANCE) {
				cam_start = Line_Movement::move_visible_lines_up_or_down(cursor.line, -MIN_CURSOR_DISTANCE);
				updated = true;
			}
			else if (cursor_line >= cam_end_visible - MIN_CURSOR_DISTANCE) {
				int new_cam_start = Line_Movement::move_visible_lines_up_or_down(cursor.line, -math_maximum(0, line_count - MIN_CURSOR_DISTANCE - 1));
				if (new_cam_start > cam_start) {
					cam_start = new_cam_start;
					updated = true;
				}
			}

			// Re-calculate cam_end
			if (updated) {
				cam_end = Line_Movement::move_visible_lines_up_or_down(cam_start, line_count);
				cam_end = Line_Movement::move_to_fold_boundary(cam_end, 1, false);
			}
		}
	}

	// Render line numbers (And breakpoints)
	int cam_start_visible = source_code_get_line(code, cam_start)->visible_index;
	{
		// Figure out how much space is used by line-numbers + symbols
		auto get_digits = [](int number) -> int {
			int digits = 1;
			while ((number / 10) != 0) {
				digits += 1;
				number = number / 10;
			}
			return digits;
			};

		vec2 char_size = editor.text_display.char_size;
		int line_num_digits = math_maximum(get_digits(tab.code->line_count), 4) + 1;
		const int char_count_before = line_num_digits + 1 + 2; // Number of line-digits + 1 offset for current line + 2 for breakpoints/current instr
		code_box.min.x += char_size.x * char_count_before; // Move code-box to the right

		// Find line symbols
		Array<Stack_Frame> stack_frames = debugger_get_stack_frames(editor.debugger);
		Dynamic_Array<Line_Symbol> line_symbols = dynamic_array_create<Line_Symbol>();
		SCOPE_EXIT(dynamic_array_destroy(&line_symbols));
		int green_line_index = -1;
		for (int i = 0; i < stack_frames.size; i++)
		{
			auto& frame = stack_frames[i];
			Assembly_Source_Information info = debugger_get_assembly_source_information(editor.debugger, frame.instruction_pointer);

			// Find unit and upp_line_index of currently executing instruction
			int upp_line_index = -1;
			Compilation_Unit* unit = nullptr;
			bool leaving_function = false;
			if (info.unit != nullptr) {
				upp_line_index = info.upp_line_index;
				unit = info.unit;
			}
			else if (info.ir_function != nullptr)
			{
				// Here we don't know the exact line, but we know the function
				int distance_to_start = math_absolute((i64)frame.instruction_pointer - (i64)info.function_start_address);
				int distance_to_end = math_absolute((i64)frame.instruction_pointer - (i64)info.function_end_address);
				if (distance_to_end < distance_to_start && distance_to_end < 8) { // Just some things to figure out if we are in prolog
					leaving_function = true;
				}

				auto modtree_fn = editor.analysis_data->function_slots[info.ir_function->function_slot_index].modtree_function;
				AST::Node* function_origin_node = nullptr;
				if (modtree_fn != nullptr)
				{
					switch (modtree_fn->function_type)
					{
					case ModTree_Function_Type::BAKE: {
						function_origin_node = upcast(modtree_fn->options.bake->analysis_workload->bake_node);
						break;
					}
					case ModTree_Function_Type::EXTERN: {
						auto symbol = modtree_fn->options.extern_definition->symbol;
						if (symbol != 0) {
							function_origin_node = symbol->definition_node;
						}
						break;
					}
					case ModTree_Function_Type::NORMAL: {
						function_origin_node = upcast(modtree_fn->options.normal.progress->body_workload->body_node);
						break;
					}
					default: panic("");
					}
				}

				if (function_origin_node != nullptr) {
					unit = compiler_find_ast_compilation_unit(function_origin_node);
					upp_line_index = function_origin_node->range.start.line;
				}
			}

			if (upp_line_index == -1 || editor.tabs[editor.open_tab_index].compilation_unit != unit) {
				continue;
			}

			Line_Symbol line_symbol;
			line_symbol.line_index = upp_line_index;
			line_symbol.type = i == 0 ? Line_Symbol_Type::CURRENT_INSTRUCTION : Line_Symbol_Type::ABOVE_STACK;
			if (leaving_function) {
				line_symbol.type = Line_Symbol_Type::LEAVING_FUNCTION;
			}
			if (i == editor.selected_stack_frame) {
				green_line_index = line_symbol.line_index;
			}
			dynamic_array_push_back(&line_symbols, line_symbol);
		}

		for (int i = 0; i < tab.breakpoints.size; i++) {
			auto bp = tab.breakpoints[i];
			Line_Symbol symbol;
			symbol.type = Line_Symbol_Type::BREAKPOINT;
			symbol.line_index = bp.line_number;
			dynamic_array_push_back(&line_symbols, symbol);
		}

		// Draw line numbers
		String text = string_create();
		SCOPE_EXIT(string_destroy(&text));
		int last_visible_index = -1;
		int cursor_visible_index = source_code_get_line(code, cursor.line)->visible_index;

		int prev = -1;
		for (int i = cam_start; i <= cam_end; i = Line_Movement::move_visible_lines_up_or_down(i, 1))
		{
			// Prevent infinite loop with visible lines...
			if (i == prev) {
				break;
			}
			prev = i;

			const int visible_index = source_code_get_line(code, i)->visible_index;
			float y_pos = code_box.max.y - (visible_index - cam_start_visible) * char_size.y;

			// Find line symbols
			bool is_breakpoint = false;
			bool is_current_instr = false;
			bool is_stack_instr = false;
			bool is_leaving_function = false;
			for (int j = 0; j < line_symbols.size; j++) {
				auto line_symbol = line_symbols[j];
				if (line_symbol.line_index != i) continue;
				switch (line_symbol.type) {
				case Line_Symbol_Type::BREAKPOINT: is_breakpoint = true; break;
				case Line_Symbol_Type::CURRENT_INSTRUCTION: is_current_instr = true; break;
				case Line_Symbol_Type::ABOVE_STACK: is_stack_instr = true; break;
				case Line_Symbol_Type::LEAVING_FUNCTION: is_leaving_function = true; break;
				default: panic("");
				}
			}

			// Draw line symbols
			vec3 pos_color = i == green_line_index ? vec3(0.2f, 1.0f, 0.2f) : vec3(1.0f, 1.0f, 0.0f);
			if (is_current_instr) {
				text_renderer_add_text(
					editor.text_renderer, string_create_static(">"), vec2((line_num_digits + 1) * char_size.x, y_pos), Anchor::TOP_LEFT,
					char_size, pos_color
				);
			}
			else if (is_leaving_function) {
				text_renderer_add_text(
					editor.text_renderer, string_create_static("<"), vec2((line_num_digits + 1) * char_size.x, y_pos), Anchor::TOP_LEFT,
					char_size, pos_color
				);
			}
			else if (is_stack_instr) {
				text_renderer_add_text(
					editor.text_renderer, string_create_static("*"), vec2((line_num_digits + 1) * char_size.x, y_pos), Anchor::TOP_LEFT,
					char_size, pos_color
				);
			}
			if (is_breakpoint) {
				text_renderer_add_text(
					editor.text_renderer, string_create_static("o"), vec2(line_num_digits * char_size.x, y_pos), Anchor::TOP_LEFT,
					char_size, vec3(1.0f, 0.0f, 0.0f)
				);
			}

			// Draw line number
			int number = math_absolute(cursor_visible_index - visible_index);
			float x_pos = (line_num_digits - get_digits(number)) * char_size.x;;
			vec3 color = vec3(0.f, .5f, 1.0f);
			if (number == 0) { // Special case for cursor line
				number = cursor.line;
				color = color * 1.6f;
				x_pos = 0;
			}

			string_reset(&text);
			string_append_formated(&text, "%d", number);
			text_renderer_add_text(
				editor.text_renderer, text, vec2(x_pos, y_pos), Anchor::TOP_LEFT, char_size, color
			);
		}
	}

	// Push Source-Code into Rich-Text
	Text_Display::set_frame(&editor.text_display, code_box.min, Anchor::BOTTOM_LEFT, code_box.max - code_box.min);
	Rich_Text::reset(&editor.editor_text);
	{
		int line_index = cam_start;
		bool condition = true;
		while (condition)
		{
			auto text = &editor.editor_text;
			SCOPE_EXIT(
				if (line_index == cam_end) {
					condition = false;
				}
				else {
					line_index = Line_Movement::move_visible_lines_up_or_down(line_index, 1);
				}
					);

			// Handle fold rendering
			Source_Line* line = source_code_get_line(code, line_index);
			if (line->is_folded)
			{
				auto& fold = tab.folds[line->fold_index];
				bool contains_errors = false;

				if (editor.analysis_data != 0)
				{
					auto& errors = editor.analysis_data->compiler_errors;
					for (int i = 0; i < errors.size; i++) {
						const auto& error = errors[i];
						if (error.unit != tab.compilation_unit) {
							continue;
						}
						// Check if error range has lines in 
						if (error.text_index.line >= fold.line_start && error.text_index.line <= fold.line_end) {
							contains_errors = true;
							break;
						}
					}
				}

				Rich_Text::add_line(text, false, fold.indentation);
				if (contains_errors) {
					Rich_Text::set_bg(text, vec3(0.75f, 0.15f, 0.15f));
				}
				else {
					Rich_Text::set_bg(text, vec3(0.4f));
				}
				Rich_Text::append(text, "|...|");
				if (Line_Movement::move_visible_lines_up_or_down(line_index, 1) >= cam_end) {
					break;
				}
				continue;
			}

			// Push line
			Rich_Text::add_line(text, false, line->indentation);
			if (line->comment_block_indentation != -1 && line->is_comment) {
				Rich_Text::set_text_color(text, Syntax_Color::COMMENT);
			}
			Rich_Text::append(text, line->text);
			Rich_Text::append(text, " "); // So that we can have a underline if something is missing at end of line
			if (line->comment_block_indentation != -1 && line->is_comment) {
				continue;
			}

			// Color tokens based on type (Initial coloring, syntax highlighting is done later)
			for (int j = 0; j < line->tokens.size; j++)
			{
				auto& token = line->tokens[j];
				vec3 color = token_get_syntax_color_based_on_surrounding(line->tokens, j);
				Rich_Text::mark_line(
					text, Rich_Text::Mark_Type::TEXT_COLOR, color, line->visible_index - cam_start_visible, token.start_index, token.end_index
				);
			}
		}
	}

	// Get hover infos
	Dynamic_Array<int> hover_errors = dynamic_array_create<int>();
	SCOPE_EXIT(dynamic_array_destroy(&hover_errors));
	Position_Info hover_info = code_query_find_position_infos(cursor, &hover_errors);

	// Text-Highlighting
	bool cursor_is_on_fold = source_code_get_line(code, cursor.line)->is_folded;
	{
		// Highlight search results in editor
		if (editor.mode == Editor_Mode::TEXT_SEARCH && editor.search_text.size != 0)
		{
			auto text = &editor.editor_text;
			auto& search_text = editor.search_text;
			for (int i = 0; i < text->lines.size; i++)
			{
				if (text->lines[i].is_seperator) continue;
				String str = text->lines[i].text;
				int substring_start = string_contains_substring(str, 0, search_text);
				while (substring_start != -1) {
					Rich_Text::mark_line(text, Rich_Text::Mark_Type::BACKGROUND_COLOR, vec3(0.3f), i, substring_start, substring_start + search_text.size);
					substring_start = string_contains_substring(str, substring_start + search_text.size, search_text);
				}
			}
		}

		// Highlight visual block lines
		if (editor.mode == Editor_Mode::VISUAL_BLOCK)
		{
			int index = math_minimum(cursor.line, editor.visual_block_start_line);
			int end_index = math_maximum(cursor.line, editor.visual_block_start_line);
			index = math_maximum(cam_start, index);
			end_index = math_minimum(cam_end, end_index);
			while (index <= end_index)
			{
				auto line = source_code_get_line(code, index);
				if (!line->is_folded) {
					Rich_Text::set_line_bg(&editor.editor_text, vec3(0.4f), line->visible_index - cam_start_visible);
				}
				if (index == end_index) break;
				index = Line_Movement::move_visible_lines_up_or_down(index, 1);
			}
		}

		// Syntax Highlighting
		{
			auto text = &editor.editor_text;

			Symbol* highlight_symbol = nullptr;
			if (hover_info.symbol_info != nullptr) {
				highlight_symbol = hover_info.symbol_info->symbol;
			}
			bool highlight_only_definition = false;
			if (editor.mode == Editor_Mode::FUZZY_FIND_DEFINITION || editor.mode == Editor_Mode::TEXT_SEARCH || cursor_is_on_fold) {
				highlight_symbol = nullptr;
			}
			else if (editor.mode == Editor_Mode::FUZZY_FIND_DEFINITION && editor.suggestions.size > 0) {
				highlight_symbol = nullptr;
				auto& sugg = editor.suggestions[0];
				if (sugg.type == Suggestion_Type::SYMBOL) {
					highlight_symbol = sugg.options.symbol;
					highlight_only_definition = true;
				}
			}

			// Loop over all visible lines
			int line_index = cam_start;
			while (true)
			{
				auto line = source_code_get_line(code, line_index);
				SCOPE_EXIT(line_index = Line_Movement::move_visible_lines_up_or_down(line_index, 1));
				if (line->is_folded) {
					if (line_index == cam_end) break;
					continue;
				}

				auto& analysis_items = line->item_infos;
				for (int i = 0; i < analysis_items.size; i++)
				{
					auto& analysis_item = analysis_items[i];
					vec3 color = vec3(1, 0, 0);
					Rich_Text::Mark_Type mark_type = Rich_Text::Mark_Type::TEXT_COLOR;
					int start = analysis_item.start_char;
					Semantic_Info* semantic_info_ptr = code_analysis_item_get_selected_semantic_info(analysis_item);
					Semantic_Info& info = *semantic_info_ptr;
					switch (info.type)
					{
					case Semantic_Info_Type::EXPRESSION_INFO:
					{
						if (info.options.expression.expr->type != AST::Expression_Type::MEMBER_ACCESS) continue;
						start += 1; // Skip member access '.'
						switch (info.options.expression.info->specifics.member_access.type)
						{
						case Member_Access_Type::DOT_CALL:
						case Member_Access_Type::DOT_CALL_AS_MEMBER: color = Syntax_Color::FUNCTION; break;
						case Member_Access_Type::STRUCT_POLYMORHPIC_PARAMETER_ACCESS:
						case Member_Access_Type::STRUCT_MEMBER_ACCESS: color = Syntax_Color::MEMBER; break;
						case Member_Access_Type::STRUCT_SUBTYPE:
						case Member_Access_Type::STRUCT_UP_OR_DOWNCAST: color = Syntax_Color::SUBTYPE; break;
						case Member_Access_Type::ENUM_MEMBER_ACCESS: color = Syntax_Color::ENUM_MEMBER; break;
						default: panic("");
						}
						break;
					}
					case Semantic_Info_Type::SYMBOL_LOOKUP:
					{
						auto symbol = info.options.symbol_info.symbol;
						color = symbol_to_color(symbol, info.options.symbol_info.is_definition);
						// Highlight background if hover symbol
						if (symbol == highlight_symbol && !(highlight_only_definition && !info.options.symbol_info.is_definition)) {
							Rich_Text::mark_line(
								text, Rich_Text::Mark_Type::BACKGROUND_COLOR, Syntax_Color::BG_HIGHLIGHT,
								line->visible_index - cam_start_visible, analysis_item.start_char, analysis_item.end_char
							);
						}
						break;
					}
					case Semantic_Info_Type::MARKUP: color = info.options.markup_color; break;
					case Semantic_Info_Type::ERROR_ITEM:
					{
						// Special handling, as errors may appear at the end of the line
						int end_char = analysis_item.end_char;
						if (analysis_item.start_char == analysis_item.end_char) {
							end_char += 1;
						}
						color = vec3(1.0f, 0.0f, 0.0f);
						mark_type = Rich_Text::Mark_Type::UNDERLINE;
						Rich_Text::mark_line(text, mark_type, color, line->visible_index - cam_start_visible, analysis_item.start_char, end_char);
						continue;
					}
					default: continue;
					}
					Rich_Text::mark_line(text, mark_type, color, line->visible_index - cam_start_visible, start, analysis_item.end_char);
				}

				if (line_index >= cam_end) break;
			}
		}
	}

	// Set cursor text-background
	if (editor.mode == Editor_Mode::NORMAL && !cursor_is_on_fold && cursor.line >= cam_start && cursor.line <= cam_end) {
		auto cursor_line = source_code_get_line(code, cursor.line);
		Rich_Text::mark_line(
			&editor.editor_text,
			Mark_Type::BACKGROUND_COLOR, vec3(0.25f),
			cursor_line->visible_index - cam_start_visible, cursor.character, cursor.character + 1
		);
	}

	// Render Code
	Text_Display::render(&editor.text_display, pass_2D);



	// Draw Cursor
	if (cursor.line >= cam_start && cursor.line <= cam_end)
	{
		auto cursor_line = source_code_get_line(code, cursor.line);

		Text_Index pos = cursor;
		if (cursor_is_on_fold) {
			pos.character = 0;
		}

		auto display = &syntax_editor.text_display;
		const int t = 2;
		vec2 min = Text_Display::get_char_position(display, cursor_line->visible_index - cam_start_visible, pos.character, Anchor::BOTTOM_LEFT);
		vec2 max = min + vec2((float)t, display->char_size.y);
		min = min + vec2(-t, 0);
		max = max + vec2(-t, 0);

		renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_min_max(min, max), Syntax_Color::COMMENT);
		if (editor.mode != Editor_Mode::INSERT) {
			int char_offset = cursor_is_on_fold ? 5 : 1;
			vec2 offset = vec2((display->char_size.x * char_offset) + t, 0.0f);
			renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_min_max(min + offset, max + offset), Syntax_Color::COMMENT);

			int l = 2;
			renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_min_max(min + vec2(t, 0), min + vec2(t + l, t)), Syntax_Color::COMMENT);
			renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_min_max(max - vec2(t + l, t) + offset, max - vec2(t, 0) + offset), Syntax_Color::COMMENT);
		}
		renderer_2D_draw(editor.renderer_2D, pass_2D);
	}

	// Calculate context
	auto suggestions_append_to_rich_text = [](Rich_Text::Rich_Text* text)
		{
			auto type_system = &syntax_editor.analysis_data->type_system;
			auto suggestions = syntax_editor.suggestions;

			int max_suggestion_length = 0;
			for (int i = 0; i < suggestions.size; i++) {
				max_suggestion_length = math_maximum(max_suggestion_length, suggestions[i].text->size);
			}

			// Limit suggestion length to 36 characters?
			max_suggestion_length = math_minimum(max_suggestion_length, 36);

			for (int i = 0; i < suggestions.size; i++)
			{
				auto& sugg = suggestions[i];

				Rich_Text::add_line(text);
				if (i == 0) {
					Rich_Text::set_line_bg(text, vec3(0.3f));
					Rich_Text::set_underline(text, Syntax_Color::STRING);
				}

				String sugg_text = string_create_static("");
				Datatype* sugg_type = nullptr;
				Symbol_Type symbol_type = (Symbol_Type)-1;

				switch (sugg.type)
				{
				case Suggestion_Type::ID: {
					auto c = sugg.options.id_color;
					Rich_Text::set_text_color(text, vec3(c.r, c.g, c.b));
					sugg_text = *sugg.text;
					break;
				}
				case Suggestion_Type::SYMBOL: {
					auto symbol = sugg.options.symbol;
					vec3 color = symbol_to_color(symbol, true);
					Rich_Text::set_text_color(text, color);
					symbol_type = symbol->type;
					const char* unused = nullptr;
					symbol_get_infos(symbol, nullptr, &sugg_type, &unused);
					sugg_text = *symbol->id;
					break;
				}
				case Suggestion_Type::STRUCT_MEMBER: {
					Rich_Text::set_text_color(text, Syntax_Color::MEMBER);
					sugg_text = *sugg.text;
					sugg_type = sugg.options.struct_member.member_type;
					break;
				}
				case Suggestion_Type::ENUM_MEMBER: {
					Rich_Text::set_text_color(text, Syntax_Color::ENUM_MEMBER);
					sugg_text = *sugg.text;
					sugg_type = upcast(sugg.options.enum_member.enumeration);
					break;
				}
				case Suggestion_Type::FILE: {
					auto file_info = directory_crawler_get_content(syntax_editor.directory_crawler)[sugg.options.file_index_in_crawler];
					if (file_info.is_directory) {
						Rich_Text::set_text_color(text, vec3(0.1f, 0.1f, 0.9f));
					}
					else {
						Rich_Text::set_text_color(text, vec3(1.0f));
					}
					sugg_text = *sugg.text;
					break;
				}
				default: panic("");
				}

				// Append suggestion text at appropriate length
				if (sugg_text.size > max_suggestion_length) {
					String cpy = sugg_text;
					sugg_text = string_create_substring_static(&cpy, 0, max_suggestion_length - 2);
					Rich_Text::append(text, sugg_text);
					Rich_Text::set_text_color(text);
					Rich_Text::append(text, "..");
				}
				else {
					Rich_Text::append(text, sugg_text);
					// Fill with spacebar until we hit max length
					for (int i = 0; i < max_suggestion_length - sugg_text.size; i++) {
						Rich_Text::append_character(text, ' ');
					}
				}

				if ((int)symbol_type != -1 || sugg_type != nullptr) {
					Rich_Text::set_text_color(text);
					Rich_Text::append(text, ": ");
				}

				// Append Symbol-Type
				if ((int)symbol_type != -1) {
					String* string = Rich_Text::start_line_manipulation(text);
					symbol_type_append_to_string(symbol_type, string);
					Rich_Text::stop_line_manipulation(text);
					if (sugg_type != nullptr) {
						Rich_Text::append_character(text, ' ');
					}
				}

				if (sugg_type != nullptr) {
					datatype_append_to_rich_text(sugg_type, type_system, text);
				}
			}
		};
	auto error_append_to_rich_text = [&](Compiler_Error_Info error, Rich_Text::Rich_Text* text, bool with_info) {
		Rich_Text::set_text_color(text, vec3(1.0f, 0.5f, 0.5f));
		Rich_Text::set_underline(text, vec3(1.0f, 0.5f, 0.5f));
		Rich_Text::append(text, "Error:");
		Rich_Text::set_text_color(text);
		Rich_Text::append(text, " ");
		Rich_Text::append(text, error.message);

		// Add error infos
		if (error.semantic_error_index != -1 && with_info) {
			auto& semantic_error = syntax_editor.analysis_data->semantic_errors[error.semantic_error_index];
			for (int j = 0; j < semantic_error.information.size; j++) {
				auto& error_info = semantic_error.information[j];
				Rich_Text::add_line(text, false, 1);
				error_information_append_to_rich_text(error_info, editor.analysis_data, text);
			}
		}
		};

	bool show_context =
		editor.mode != Editor_Mode::FUZZY_FIND_DEFINITION &&
		editor.mode != Editor_Mode::TEXT_SEARCH &&
		editor.mode != Editor_Mode::ERROR_NAVIGATION &&
		!(editor.mode == Editor_Mode::NORMAL && !editor.show_semantic_infos) &&
		(cursor.line >= cam_start && cursor.line <= cam_end) &&
		!cursor_is_on_fold;

	Rich_Text::Rich_Text context_text = Rich_Text::create(vec3(1));
	SCOPE_EXIT(Rich_Text::destroy(&context_text));
	Rich_Text::Rich_Text call_info_text = Rich_Text::create(vec3(1));
	SCOPE_EXIT(Rich_Text::destroy(&context_text));

	auto type_system = &syntax_editor.analysis_data->type_system;
	String tmp = string_create();
	SCOPE_EXIT(string_destroy(&tmp));
	if (show_context)
	{
		Rich_Text::Rich_Text* text = &context_text;
		bool show_normal_mode_context = true;

		// Code-Completion Suggestions
		if (editor.mode == Editor_Mode::INSERT)
		{
			code_completion_find_suggestions();
			if (editor.suggestions.size != 0)
			{
				Rich_Text::add_seperator_line(text);
				show_normal_mode_context = false;
				suggestions_append_to_rich_text(text);
			}
		}

		// Show hover errors
		for (int i = 0; i < hover_errors.size; i++)
		{
			auto& error = editor.analysis_data->compiler_errors[hover_errors[i]];
			show_normal_mode_context = false;
			if (i == 0) {
				Rich_Text::add_seperator_line(text);
			}
			Rich_Text::add_line(text);
			error_append_to_rich_text(error, text, i == 0);
		}

		// Symbol Info
		if (show_normal_mode_context && hover_info.symbol_info != 0)
		{
			Rich_Text::add_seperator_line(text);
			Rich_Text::add_line(text);

			Datatype* type = 0;
			const char* after_text = nullptr;
			Symbol* symbol = hover_info.symbol_info->symbol;
			Analysis_Pass* pass = hover_info.symbol_info->pass;
			symbol_get_infos(symbol, pass, &type, &after_text);

			Rich_Text::append(text, "Symbol: ");
			vec3 symbol_color = symbol_to_color(symbol, true);
			Rich_Text::set_text_color(text, symbol_color);
			Rich_Text::append(text, *symbol->id);
			Rich_Text::set_text_color(text);

			string_reset(&tmp);
			symbol_type_append_to_string(symbol->type, &tmp);
			Rich_Text::append(text, ", ");
			Rich_Text::set_text_color(text, symbol_color);
			Rich_Text::append(text, tmp);

			if (type != 0) {
				Rich_Text::add_line(text, false, 1);
				Rich_Text::append(text, "Datatype: ");
				datatype_append_to_rich_text(type, type_system, text);
				Rich_Text::set_text_color(text);
			}
		}

		// Analysis-Info
		if (show_normal_mode_context && hover_info.expression_info != nullptr)
		{
			Rich_Text::add_seperator_line(text);
			Rich_Text::add_line(text);

			// Add expression type
			Rich_Text::append(text, "Expression: ");
			string_reset(&tmp);
			AST::expression_append_to_string(hover_info.expression_info->expr, &tmp);
			Rich_Text::append(text, tmp);

			// Add Datatype
			auto& cast_info = hover_info.expression_info->info->cast_info;
			Rich_Text::add_line(text, false, 1);
			Rich_Text::append(text, "Result-Type:  ");
			datatype_append_to_rich_text(cast_info.result_type, type_system, text);
			if (cast_info.result_value_is_temporary) {
				Rich_Text::set_text_color(text);
				Rich_Text::append(text, ", temporary");
			}

			// Add cast infos
			if (!types_are_equal(cast_info.initial_type, cast_info.result_type)) {
				Rich_Text::add_line(text, false, 1);
				Rich_Text::append(text, "Initial-Type: ");
				datatype_append_to_rich_text(cast_info.initial_type, type_system, text);
				if (cast_info.initial_value_is_temporary) {
					Rich_Text::set_text_color(text);
					Rich_Text::append(text, ", temporary");
				}
			}
			if (cast_info.deref_count != 0) {
				Rich_Text::add_line(text, false, 1);
				Rich_Text::append_formated(text, "Deref-Count: %d", cast_info.deref_count);
			}
			if (cast_info.cast_type != Cast_Type::NO_CAST) {
				Rich_Text::add_line(text, false, 1);
				Rich_Text::append(text, "Cast-Type: ");
				Rich_Text::append(text, cast_type_to_string(cast_info.cast_type));
			}

			// Show context info
			auto& context = hover_info.expression_info->info->context;
			if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
				Rich_Text::add_line(text, false, 1);
				Rich_Text::append(text, "Context expected: ");
				datatype_append_to_rich_text(context.expected_type.type, type_system, text);
			}
			else if (context.type == Expression_Context_Type::AUTO_DEREFERENCE) {
				Rich_Text::add_line(text, false, 1);
				Rich_Text::append(text, "Context: Auto-Dereference");
			}
		}

		// Call-info
		if (hover_info.call_info != nullptr)
		{
			Rich_Text::Rich_Text* text = &call_info_text;
			Rich_Text::add_line(text);

			auto call_info = hover_info.call_info->callable_call;
			int arg_index = hover_info.call_argument_index;

			// Check if call is dot-call
			bool is_dot_call = false;
			if (call_info->call_node != nullptr) {
				auto call_node = call_info->call_node;
				if (call_node->base.parent != nullptr && call_node->base.parent->type == AST::Node_Type::EXPRESSION) {
					auto expr = downcast<AST::Expression>(call_node->base.parent);
					if (expr->type == AST::Expression_Type::FUNCTION_CALL && expr->options.call.is_dot_call) {
						is_dot_call = true;
					}
				}
			}

			String* name = nullptr;
			vec3 color = Syntax_Color::TEXT;
			bool is_struct_init = false;
			switch (call_info->origin.type)
			{
			case Call_Origin_Type::FUNCTION: 
			{
				name = call_info->origin.options.function->name;
				color = Syntax_Color::FUNCTION;
				break;
			}
			case Call_Origin_Type::STRUCT_INITIALIZER: 
			{
				is_struct_init = true;
				if (call_info->argument_matching_success) {
					name = call_info->origin.options.struct_content->name;
					color = Syntax_Color::TYPE;
					break;
				}
			}
			}

			if (name != nullptr) {
				Rich_Text::set_text_color(text, color);
				Rich_Text::append(text, *name);
			}
			else {
				Rich_Text::set_text_color(text, Syntax_Color::TEXT);
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
			bool first = true;
			for (int i = is_dot_call ? 1 : 0; i < call_info->parameter_values.size; i += 1)
			{
				const auto& param_info = call_info->origin.signature->parameters[i];
				const auto& param_value = call_info->parameter_values[i];

				if (param_info.requires_named_addressing || param_info.must_not_be_set) {
					continue;
				}

				if (!first) {
					Rich_Text::append(text, ", ");
				}
				first = false;

				bool highlight = param_value.options.argument_index == arg_index && arg_index != -1;
				if (highlight) {
					Rich_Text::set_bg(text, vec3(0.2f, 0.3f, 0.3f));
					Rich_Text::set_underline(text, vec3(0.8f));
				}

				vec3 name_color = Syntax_Color::VALUE_DEFINITION;
				if (param_value.value_type == Parameter_Value_Type::NOT_SET && param_info.required) {
					name_color = vec3(1.0f, 0.5f, 0.5f);
				}

				Rich_Text::set_text_color(text, name_color);
				Rich_Text::append(text, *param_info.name);
				if (param_info.datatype != nullptr) {
					Rich_Text::append(text, ": ");
					datatype_append_to_rich_text(param_info.datatype, type_system, text);
				}
				Rich_Text::set_text_color(text, vec3(1.0f));

				if (!param_info.required) {
					Rich_Text::append(text, " =..");
				}

				if (highlight) {
					Rich_Text::stop_bg(text);
					Rich_Text::stop_underline(text);
				}
			}
			Rich_Text::append(text, is_struct_init ? "}" : ")");
		}
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
		if (draw_context)
		{
			auto text = &context_text;
			int max_line_char_count = 0;
			for (int i = 0; i < text->lines.size; i++) {
				auto& line = text->lines[i];
				max_line_char_count = math_maximum(max_line_char_count, line.text.size + context_display.indentation_spaces * line.indentation);
			}

			context_size = char_size * vec2(math_maximum(30, max_line_char_count), text->lines.size);
			context_size = context_size + 2 * (BORDER_SIZE + PADDING);
		}
		if (draw_call_info)
		{
			auto text = &call_info_text;
			int max_line_char_count = 0;
			for (int i = 0; i < text->lines.size; i++) {
				auto& line = text->lines[i];
				max_line_char_count = math_maximum(max_line_char_count, line.text.size + context_display.indentation_spaces * line.indentation);
			}

			call_info_size = char_size * vec2(max_line_char_count, text->lines.size);
			call_info_size = call_info_size + 2 * (BORDER_SIZE + PADDING);
		}

		// Figure out positioning
		vec2 context_pos = vec2(0.0f);
		vec2 call_info_pos = vec2(0.0f);
		{
			vec2 cursor_pos = Text_Display::get_char_position(
				&editor.text_display, source_code_get_line(code, cursor.line)->visible_index - cam_start_visible, cursor.character, Anchor::BOTTOM_LEFT
			);
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

			// Move boxes to the left if they are too far
			if (context_pos.x + context_size.x > width) {
				int move_left = (context_pos.x + context_size.x) - width;
				context_pos.x = context_pos.x - move_left;
				context_pos.x = math_maximum(0, (int)context_pos.x);
			}
			if (call_info_pos.x + call_info_size.x > width) {
				int move_left = (call_info_pos.x + call_info_size.x) - width;
				call_info_pos.x = call_info_pos.x - move_left;
				call_info_pos.x = math_maximum(0, (int)call_info_pos.x);
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

		vec2 pos = Text_Display::get_char_position(
			&editor.text_display, source_code_get_line(code, cursor.line)->visible_index - cam_start_visible, cursor.character, Anchor::TOP_RIGHT
		);
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

	// Draw mode overlays
	if (editor.mode == Editor_Mode::FUZZY_FIND_DEFINITION ||
		editor.mode == Editor_Mode::TEXT_SEARCH ||
		editor.mode == Editor_Mode::ERROR_NAVIGATION)
	{
		auto& line_edit = editor.search_text_edit;

		Rich_Text::Rich_Text rich_text = Rich_Text::create(vec3(1));
		SCOPE_EXIT(Rich_Text::destroy(&rich_text));

		if (editor.mode == Editor_Mode::ERROR_NAVIGATION)
		{
			auto& errors = editor.analysis_data->compiler_errors;
			int& index = editor.navigate_error_index;
			int& cam_start = editor.navigate_error_cam_start;
			const int& MAX_LINES = 5;

			if (cam_start > index) cam_start = index;
			if (cam_start + MAX_LINES < index) cam_start = index - MAX_LINES;

			if (cam_start > 0) {
				Rich_Text::add_line(&rich_text);
				Rich_Text::append(&rich_text, "...");
			}

			for (int i = 0; i < errors.size; i++) {
				auto& error = errors[editor.error_indices_sorted[i]];

				int error_line_index = rich_text.lines.size;
				Rich_Text::add_line(&rich_text);
				Rich_Text::append_formated(&rich_text, "#%2d: ", i + 1);
				error_append_to_rich_text(error, &rich_text, i == index);
				if (i == index) {
					Rich_Text::set_line_bg(&rich_text, vec3(0.65f), error_line_index);
				}

				if (i >= MAX_LINES && i != errors.size - 1) {
					Rich_Text::add_line(&rich_text);
					Rich_Text::append(&rich_text, "...");
					break;
				}
			}
		}
		else
		{
			Rich_Text::add_line(&rich_text);
			Rich_Text::append(&rich_text, editor.mode == Editor_Mode::FUZZY_FIND_DEFINITION ? editor.fuzzy_search_text : editor.search_text);

			// Draw highlighted
			if (line_edit.pos != line_edit.select_start) {
				int start = math_minimum(line_edit.pos, line_edit.select_start);
				int end = math_maximum(line_edit.pos, line_edit.select_start);
				Rich_Text::mark_line(&rich_text, Rich_Text::Mark_Type::BACKGROUND_COLOR, vec3(0.3f), 0, start, end);
			}

			// Push suggestions in Fuzzy-Find Mode
			if (editor.mode == Editor_Mode::FUZZY_FIND_DEFINITION && editor.suggestions.size > 0) {
				Rich_Text::add_seperator_line(&rich_text);
				suggestions_append_to_rich_text(&rich_text);
			}
		}

		// Create display and render
		vec2 char_size = text_renderer_get_aligned_char_size(editor.text_renderer, editor.normal_text_size_pixel * 0.85f);
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
		if (editor.mode != Editor_Mode::ERROR_NAVIGATION)
		{
			const int t = 2;
			vec2 min = Text_Display::get_char_position(&display, 0, line_edit.pos, Anchor::BOTTOM_LEFT);
			vec2 max = min + vec2((float)t, char_size.y);
			renderer_2D_add_rectangle(editor.renderer_2D, bounding_box_2_make_min_max(min, max), Syntax_Color::COMMENT);
			renderer_2D_draw(editor.renderer_2D, pass_2D);
		}
	}

	// Render gui
	gui_update_and_render(pass_2D);

	ui_system_end_frame_and_render(editor.window, editor.input, pass_2D);
}



