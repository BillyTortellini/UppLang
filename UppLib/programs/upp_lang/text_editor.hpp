#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../rendering/mesh_gpu_data.hpp"
#include "../../utility/file_listener.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../win32/input.hpp"
#include "text.hpp"
#include "ast_parser.hpp"
#include "lexer.hpp"
#include "semantic_analyser.hpp"
#include "ast_interpreter.hpp"
#include "bytecode.hpp"
#include "bytecode_interpreter.hpp"

struct Input;
struct OpenGLState;

namespace TextChangeType
{
    enum ENUM
    {
        STRING_INSERTION,
        STRING_DELETION,
        CHARACTER_DELETION,
        CHARACTER_INSERTION,
        COMPLEX,
    };
}

struct TextChange
{
    TextChange* next;
    TextChange* previous;
    TextChangeType::ENUM symbol_type;

    // For string deletion/insertion
    String string;
    Text_Slice slice;
    // For character stuff
    Text_Position character_position;
    char character;
    // For complex command
    DynamicArray<TextChange> sub_changes;
};

struct TextHistory 
{
    TextChange* current;
    // Do something with recording complex commands
    bool undo_first_change;
    int recording_depth;
    DynamicArray<TextChange> complex_command;
};

namespace MovementType
{
    enum ENUM
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
}

// Missing: <> ''
namespace MotionType
{
    enum ENUM
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
}

struct Movement
{
    MovementType::ENUM symbol_type;
    int repeat_count;
    char search_char;
};

struct Motion
{
    MotionType::ENUM symbol_type;
    int repeat_count;
    Movement movement; // If type == MOVEMENT
    bool contains_edges; // In vim, this would be inner or around motions (iw or aw)
};

namespace NormalModeCommandType
{
    enum ENUM
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
        UNDO,
        REDO,
        MOVE_VIEWPORT_CURSOR_TOP, // zt
        MOVE_VIEWPORT_CURSOR_CENTER, // zz
        MOVE_VIEWPORT_CURSOR_BOTTOM, // zb
        MOVE_CURSOR_VIEWPORT_TOP, // 'Shift-H'
        MOVE_CURSOR_VIEWPORT_CENTER, // 'Shift-M'
        MOVE_CURSOR_VIEWPORT_BOTTOM, // 'Shift-L'
    };
}

struct NormalModeCommand
{
    NormalModeCommandType::ENUM symbol_type;
    Motion motion;
    Movement movement;
    char character;
    int repeat_count;
};

struct TextHighlight
{
    vec3 text_color;
    vec4 background_color;
    int character_start;
    int character_end;
};

namespace TextEditorMode
{
    enum ENUM
    {
        NORMAL,
        INSERT,
    };
}

struct Text_Editor
{
    // Text editor state
    DynamicArray<String> lines;

    // Rendering Stuff
    TextRenderer* renderer;
    ShaderProgram* cursor_shader;
    Mesh_GPU_Data cursor_mesh;
    DynamicArray<DynamicArray<TextHighlight>> text_highlights;
    float line_size_cm;
    double last_keymessage_time;
    String line_count_buffer;
    BoundingBox2 last_editor_region;
    float last_text_height;
    int first_rendered_line;
    int first_rendered_char;

    // Editor Stuff
    TextHistory history;
    TextEditorMode::ENUM mode;
    Text_Position cursor_position;
    int horizontal_position;
    bool text_changed;
    DynamicArray<Key_Message> normal_mode_incomplete_command;
    DynamicArray<Key_Message> last_insert_mode_inputs;
    NormalModeCommand last_normal_mode_command;
    bool record_insert_mode_inputs;
    String yanked_string;
    bool last_yank_was_line;
    char last_search_char;
    bool last_search_was_forwards;

    // IDE schtuff
    AST_Parser parser;
    Lexer lexer;
    Semantic_Analyser analyser;
    AST_Interpreter interpreter;
    Bytecode_Generator generator;
    Bytecode_Interpreter bytecode_interpreter;
};

Text_Editor text_editor_create(TextRenderer* text_renderer, FileListener* listener, OpenGLState* state);
void text_editor_destroy(Text_Editor* editor);
void text_editor_update(Text_Editor* editor, Input* input, double time);
void text_editor_render(Text_Editor* editor, OpenGLState* state, int width, int height, int dpi, BoundingBox2 editor_box, double time);



