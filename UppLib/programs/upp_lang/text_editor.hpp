#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../rendering/mesh_gpu_data.hpp"
#include "../../utility/file_listener.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../win32/input.hpp"

struct Input;
struct OpenGLState;

struct TextHighlight
{
    vec3 text_color;
    vec3 background_color;
    int character_start;
    int character_end;
};

namespace TextEditorCursorMode
{
    enum ENUM
    {
        BLOCK,
        LINE,
    };
}

struct Text_Editor
{
    // Data for rendering
    TextRenderer* renderer;
    ShaderProgram* cursor_shader;
    Mesh_GPU_Data cursor_mesh;

    // Text editor state
    DynamicArray<String> lines;
    DynamicArray<DynamicArray<TextHighlight>> text_highlights;
    int current_line;
    int current_character;
    TextEditorCursorMode::ENUM cursor_mode;
    float line_size_cm;
};

Text_Editor text_editor_create(TextRenderer* text_renderer, FileListener* listener, OpenGLState* state);
void text_editor_destroy(Text_Editor* editor);
void text_editor_render(Text_Editor* editor, OpenGLState* state, int width, int height, int dpi);
void text_editor_set_string(Text_Editor* editor, String* string);
void text_editor_append_text_to_string(Text_Editor* editor, String* string);




/*
    TextEditorLogic
*/

namespace TextEditorLogicMode
{
    enum ENUM
    {
        NORMAL,
        INSERT,
    };
}

struct Text_Editor_Logic
{
    TextEditorLogicMode::ENUM mode;
    String fill_string;
    bool text_changed;

    // Command handling
    DynamicArray<Key_Message> normal_mode_incomplete_command;
    int count;
    // Insert mode
    DynamicArray<Key_Message> last_insert_mode_inputs;
};

Text_Editor_Logic text_editor_logic_create();
void text_editor_logic_destroy(Text_Editor_Logic* logic);
void text_editor_logic_update(Text_Editor_Logic* logic, Text_Editor* editor, Input* input);



