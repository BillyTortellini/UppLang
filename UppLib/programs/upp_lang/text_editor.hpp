#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../rendering/gpu_buffers.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../win32/input.hpp"
#include "text.hpp"

struct Input;

enum class Text_Change_Type
{
    STRING_INSERTION,
    STRING_DELETION,
    CHARACTER_DELETION,
    CHARACTER_INSERTION,
    COMPLEX,
};

struct Text_Change
{
    Text_Change* next;
    Text_Change* previous;
    Text_Change_Type type;
    Text_Position cursor_pos_before_change;

    // For string deletion/insertion
    String string;
    Text_Slice slice;
    // For character stuff
    Text_Position character_position;
    char character;
    // For complex command
    Dynamic_Array<Text_Change> sub_changes;
};

struct Text_Editor;
struct Text_History 
{
    Text_Editor* editor;
    Text_Change* current;
    bool undo_first_change;
    int recording_depth;
    Dynamic_Array<Text_Change> complex_command;
    Text_Position complex_command_start_pos;
};

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
        NEXT_PARAGRAPH, // }
        PREVIOUS_PARAGRAPH, // {
        GOTO_END_OF_TEXT, // G
        GOTO_START_OF_TEXT, // gg
        GOTO_LINE_NUMBER, // g43
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
    PARAGRAPH, // p
};

struct Movement
{
    Movement_Type type;
    int repeat_count;
    char search_char;
};

struct Motion
{
    Motion_Type motion_type;
    int repeat_count;
    Movement movement; // If type == MOVEMENT
    bool contains_edges; // In vim, this would be inner or around motions (iw or aw)
};
Text_Slice motion_evaluate_at_position(Motion motion, Text_Position pos, Text_Editor* editor);

enum class Normal_Mode_Command_Type
{
    MOVEMENT,
    ENTER_INSERT_MODE_LINE_START,
    ENTER_INSERT_MODE_LINE_END,
    ENTER_INSERT_MODE_ON_CURSOR,
    ENTER_INSERT_MODE_AFTER_CURSOR,
    ENTER_INSERT_MODE_NEW_LINE_BELOW,
    ENTER_INSERT_MODE_NEW_LINE_ABOVE,
    DELETE_CHARACTER,
    REPLACE_CHARACTER,
    DELETE_LINE,
    DELETE_MOTION,
    CHANGE_LINE,
    CHANGE_MOTION,
    YANK_LINE, // TODO: Yank/Put stuff (yy is yank line)
    YANK_MOTION,
    PUT_AFTER_CURSOR,
    PUT_BEFORE_CURSOR,
    REPEAT_LAST_COMMAND,
    VISUALIZE_MOTION,
    FORMAT_TEXT,
    GOTO_LAST_JUMP,
    GOTO_NEXT_JUMP,
    SCROLL_DOWNWARDS_HALF_PAGE,
    SCROLL_UPWARDS_HALF_PAGE,
    UNDO,
    REDO,
    MOVE_VIEWPORT_CURSOR_TOP, // zt
    MOVE_VIEWPORT_CURSOR_CENTER, // zz
    MOVE_VIEWPORT_CURSOR_BOTTOM, // zb
    MOVE_CURSOR_VIEWPORT_TOP, // 'Shift-H'
    MOVE_CURSOR_VIEWPORT_CENTER, // 'Shift-M'
    MOVE_CURSOR_VIEWPORT_BOTTOM, // 'Shift-L'
};

struct Normal_Mode_Command
{
    Normal_Mode_Command_Type type;
    Motion motion;
    Movement movement;
    char character;
    int repeat_count;
};

struct Text_Highlight
{
    vec3 text_color;
    vec4 background_color;
    int character_start;
    int character_end;
};

enum class Text_Editor_Mode
{
    NORMAL,
    INSERT,
};

struct Text_Editor_Jump
{
    Text_Position jump_start;
    Text_Position jump_end;
};

struct Text_Editor
{
    // Text editor state
    Dynamic_Array<String> text;

    // Rendering Stuff
    Text_Renderer* renderer;
    Shader_Program* cursor_shader;
    Mesh_GPU_Buffer cursor_mesh;
    Dynamic_Array<Dynamic_Array<Text_Highlight>> text_highlights;
    float line_size_cm;
    double last_keymessage_time;
    String line_count_buffer;
    BoundingBox2 last_editor_region;
    float last_text_height;
    int first_rendered_line;
    int first_rendered_char;
    Pipeline_State pipeline_state;

    // Editor Stuff
    Text_History history;
    Text_Editor_Mode mode;
    Text_Position cursor_position;
    int horizontal_position;
    bool text_changed;
    Dynamic_Array<Key_Message> normal_mode_incomplete_command;
    Dynamic_Array<Key_Message> last_insert_mode_inputs;
    Normal_Mode_Command last_normal_mode_command;
    bool record_insert_mode_inputs;
    String yanked_string;
    bool last_yank_was_line;
    char last_search_char;
    bool last_search_was_forwards;
    Dynamic_Array<Text_Editor_Jump> jump_history;
    int jump_history_index;
    Text_Position last_change_position;
};

Text_Editor* text_editor_create(Text_Renderer* text_renderer, Rendering_Core* core);
void text_editor_destroy(Text_Editor* editor);

void text_editor_handle_key_message(Text_Editor* editor, Key_Message* message);
void text_editor_update(Text_Editor* editor, Input* input, double time);
void text_editor_render(Text_Editor* editor, Rendering_Core* core, int width, int height, int dpi, BoundingBox2 editor_box, double time);

void text_editor_add_highlight(Text_Editor* editor, Text_Highlight highlight, int line_number);
void text_editor_add_highlight_from_slice(Text_Editor* editor, Text_Slice slice, vec3 text_color, vec4 background_color);
void text_editor_reset_highlights(Text_Editor* editor);
void text_editor_record_jump(Text_Editor* editor, Text_Position start, Text_Position end);
void text_editor_clamp_cursor(Text_Editor* editor);



