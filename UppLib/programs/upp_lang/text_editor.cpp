#include "text_editor.hpp"

#include "compiler.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../win32/window.hpp"
#include "../../rendering/opengl_state.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/shader_program.hpp"
#include "../../rendering/mesh_utils.hpp"
#include "../../math/scalars.hpp"

Text_Editor text_editor_create(TextRenderer* text_renderer, FileListener* listener, OpenGLState* state)
{
    Text_Editor result;
    result.renderer = text_renderer;
    result.current_line = 0;
    result.current_character = 0;
    result.cursor_mode = TextEditorCursorMode::BLOCK;
    result.line_size_cm = 1;
    result.lines = dynamic_array_create_empty<String>(32);
    dynamic_array_push_back(&result.lines, string_create_empty(64));
    result.text_highlights = dynamic_array_create_empty<DynamicArray<TextHighlight>>(32);

    result.cursor_shader = optional_unwrap(shader_program_create(listener, { "resources/shaders/cursor.frag", "resources/shaders/cursor.vert" }));
    result.cursor_mesh = mesh_utils_create_quad_2D(state);

    return result;
}

void text_editor_synchronize_highlights_array(Text_Editor* editor)
{
    while (editor->text_highlights.size < editor->lines.size) {
        DynamicArray<TextHighlight> line_highlights = dynamic_array_create_empty<TextHighlight>(32);
        dynamic_array_push_back(&editor->text_highlights, line_highlights);
    }
}

void text_editor_add_highlight(Text_Editor* editor, TextHighlight highlight, int line_number)
{
    if (line_number >= editor->lines.size) {
        return;
    }
    text_editor_synchronize_highlights_array(editor);
    dynamic_array_push_back(&editor->text_highlights.data[line_number], highlight);
}

void text_editor_reset_highlights(Text_Editor* editor)
{
    for (int i = 0; i < editor->text_highlights.size; i++) {
        dynamic_array_reset(&editor->text_highlights.data[i]);
    }
}

void text_editor_destroy(Text_Editor* editor) 
{
    for (int i = 0; i < editor->lines.size; i++) {
        string_destroy(&editor->lines[i]);
    }
    dynamic_array_destroy(&editor->lines);
    for (int i = 0; i < editor->text_highlights.size; i++) {
        dynamic_array_destroy(&editor->text_highlights.data[i]);
    }
    dynamic_array_destroy(&editor->text_highlights);
}

void text_editor_remove_line(Text_Editor* editor, int line) {
    if (line <= 0 || line >= editor->lines.size) return;
    string_destroy(&editor->lines[line]);
    dynamic_array_remove_ordered(&editor->lines, line);
}

void text_editor_draw_bounding_box(Text_Editor* editor, OpenGLState* state, BoundingBox2 bb, vec3 color)
{
    shader_program_set_uniform(editor->cursor_shader, state, "position", bb.minimum_coordinates);
    shader_program_set_uniform(editor->cursor_shader, state, "size", bb.maximum_coordinates - bb.minimum_coordinates);
    shader_program_set_uniform(editor->cursor_shader, state, "color", color);
    mesh_gpu_data_draw_with_shader_program(&editor->cursor_mesh, editor->cursor_shader, state);
}

BoundingBox2 text_editor_get_character_bounding_box(Text_Editor* editor, float text_height, int line, int character)
{
    float glyph_advance = text_renderer_get_cursor_advance(editor->renderer, text_height);
    vec2 cursor_pos = vec2(glyph_advance * character, 0.0f) + vec2(-1.0f, 1.0f - (line+1.0f) * text_height);
    vec2 cursor_size = vec2(glyph_advance, text_height);

    BoundingBox2 result;
    result.minimum_coordinates = cursor_pos;
    result.maximum_coordinates = cursor_pos + cursor_size;
    return result;
}

void bounding_box_2_set_width(BoundingBox2* bb, float width) {
    bb->maximum_coordinates.x = bb->minimum_coordinates.x + width;
}

BoundingBox2 bounding_box_2_combine(BoundingBox2 bb1, BoundingBox2 bb2) {
    BoundingBox2 result;
    result.minimum_coordinates = vec2(
        math_minimum(bb1.minimum_coordinates.x, bb2.minimum_coordinates.x),
        math_minimum(bb1.minimum_coordinates.y, bb2.minimum_coordinates.y)
    );
    result.maximum_coordinates = vec2(
        math_maximum(bb1.maximum_coordinates.x, bb2.maximum_coordinates.x),
        math_maximum(bb1.maximum_coordinates.y, bb2.maximum_coordinates.y)
    );
    return result;
}

void text_editor_render(Text_Editor* editor, OpenGLState* state, int width, int height, int dpi)
{
    float text_height = 2.0f * (editor->line_size_cm) / (height / (float)dpi * 2.54f);

    // Draw Highlights
    for (int i = 0; i < editor->text_highlights.size; i++) {
        for (int j = 0; j < editor->text_highlights.data[i].size; j++)
        {
            TextHighlight highlight = editor->text_highlights.data[i].data[j];
            BoundingBox2 highlight_start = text_editor_get_character_bounding_box(editor, text_height, i, highlight.character_start);
            BoundingBox2 highlight_end = text_editor_get_character_bounding_box(editor, text_height, i, highlight.character_end-1);
            BoundingBox2 combined = bounding_box_2_combine(highlight_start, highlight_end);
            text_editor_draw_bounding_box(editor, state, combined, highlight.background_color);
        }
    }

    // Draw lines
    text_editor_synchronize_highlights_array(editor);
    vec2 line_pos = vec2(-1.0f, 1.0f-text_height);
    for (int i = 0; i < editor->lines.size; i++) 
    {
        String* line = &editor->lines[i];
        TextLayout* line_layout = text_renderer_calculate_text_layout(editor->renderer, line, text_height, 1.0f);
        for (int j = 0; j < editor->text_highlights.data[i].size; j++)
        {
            TextHighlight* highlight = &editor->text_highlights.data[i].data[j];
            // Draw text background 
            {
                BoundingBox2 highlight_start = text_editor_get_character_bounding_box(editor, text_height, i, highlight->character_start);
                BoundingBox2 highlight_end = text_editor_get_character_bounding_box(editor, text_height, i, highlight->character_end-1);
                BoundingBox2 combined = bounding_box_2_combine(highlight_start, highlight_end);
                text_editor_draw_bounding_box(editor, state, combined, highlight->background_color);
            }
            // Set text color
            for (int k = highlight->character_start; k < highlight->character_end && k < line_layout->character_positions.size; k++)
            {
                Character_Position* char_pos = &line_layout->character_positions.data[k];
                char_pos->color = highlight->text_color;
            }
        }

        text_renderer_add_text_from_layout(editor->renderer, line_layout, line_pos);
        line_pos.y -= (text_height);
    }
    text_renderer_render(editor->renderer, state);

    // Draw cursor 
    {
        BoundingBox2 cursor_bb = text_editor_get_character_bounding_box(editor, text_height, editor->current_line, editor->current_character);
        if (editor->cursor_mode == TextEditorCursorMode::LINE) {
            float pixel_normalized = 2.0f/width;
            bounding_box_2_set_width(&cursor_bb, math_maximum(pixel_normalized*3.0f, text_height * 0.04f));
        }
        text_editor_draw_bounding_box(editor, state, cursor_bb, vec3(0.0f, 1.0f, 0.0f));
    }
}

void text_editor_set_string(Text_Editor* editor, String* string)
{
    dynamic_array_reset(&editor->lines);

    int last_newline = -1;
    int index = 0;
    while (index < string->size)
    {
        if (string->characters[index] == '\n') {
            String new_line = string_create_substring(string, last_newline+1, index-1);
            dynamic_array_push_back(&editor->lines, new_line);
            last_newline = index;
        }
        index++;
    }

    // Add last string (to string end)
    String new_line = string_create_substring(string, last_newline+1, string->size);
    dynamic_array_push_back(&editor->lines, new_line);
    dynamic_array_push_back(&editor->lines, string_create(""));
}

void text_editor_append_text_to_string(Text_Editor* editor, String* result)
{
    result->size = 0;
    for (int i = 0; i < editor->lines.size; i++) {
        String* line = &editor->lines.data[i];
        string_append(result, line->characters);
        string_append(result, "\n");
    }
}

TextHighlight text_highlight_make(vec3 text_color, vec3 background_color, int character_start, int character_end) {
    TextHighlight result;
    result.background_color = background_color;
    result.text_color = text_color;
    result.character_end = character_end;
    result.character_start = character_start;
    return result;
}


/*
    Logic
*/
void text_editor_logic_clamp_cursor(Text_Editor_Logic* logic, Text_Editor* editor)
{
    editor->current_line = math_clamp(editor->current_line, 0, editor->lines.size-1);
    String* line = &editor->lines[editor->current_line];
    if (logic->mode == TextEditorLogicMode::INSERT) {
        editor->current_character = math_clamp(editor->current_character, 0, line->size+1);
    }
    else {
        if (line->size==0) {
            editor->current_character = math_clamp(editor->current_character, 0, 0);
        }
        else {
            editor->current_character = math_clamp(editor->current_character, 0, line->size-1);
        }
    }
}

void insert_mode_enter(Text_Editor_Logic* logic, Text_Editor* editor) {
    logic->mode = TextEditorLogicMode::INSERT;
    text_editor_logic_clamp_cursor(logic, editor);
    dynamic_array_reset(&logic->last_insert_mode_inputs);
}

void insert_mode_handle_message(Text_Editor_Logic* logic, Text_Editor* editor, Key_Message* msg)
{
    bool msg_is_valid_command = true;
    if (msg->key_code == KEY_CODE::L && msg->ctrl_down) {
        logic->mode = TextEditorLogicMode::NORMAL;
        text_editor_logic_clamp_cursor(logic, editor);
        return;
    }
    if (msg->key_code == KEY_CODE::TAB && msg->key_down)
    {
        String* line = &editor->lines[editor->current_line];
        string_insert_character_before(line, ' ', editor->current_character);
        editor->current_character++;
        while (editor->current_character % 4 != 0) {
            string_insert_character_before(line, ' ', editor->current_character);
            editor->current_character++;
        }
        logic->text_changed = true;
    }
    if (msg->character >= 32) {
        // Insert character
        String* line = &editor->lines[editor->current_line];
        string_insert_character_before(line, msg->character, editor->current_character);
        editor->current_character += 1;
        logic->text_changed = true;
    }
    else if (msg->key_code == KEY_CODE::RETURN && msg->key_down) {
        String* line = &editor->lines[editor->current_line];
        String new_line = string_create_substring(line, editor->current_character, line->size-1);
        string_truncate(line, editor->current_character);
        dynamic_array_insert_ordererd(&editor->lines, new_line, editor->current_line+1);
        editor->current_line++;
        editor->current_character = 0;
        logic->text_changed = true;
    }
    else if (msg->key_code == KEY_CODE::BACKSPACE && msg->key_down) {
        if (editor->current_character == 0) {
            if (editor->current_line != 0) {
                String* line = &editor->lines[editor->current_line];
                String* line_before = &editor->lines[editor->current_line-1];
                int new_char_pos = line_before->size;
                string_append(line_before, line->characters);
                string_destroy(line);
                dynamic_array_remove_ordered(&editor->lines, editor->current_line);
                logic->text_changed = true;
                editor->current_line--;
                editor->current_character = new_char_pos;
            }
        }
        else {
            string_remove_character(&editor->lines[editor->current_line], editor->current_character-1);
            editor->current_character--;
            logic->text_changed = true;
        }
    }
    else {
        msg_is_valid_command = false;
    }
    if (msg_is_valid_command && logic->mode == TextEditorLogicMode::INSERT) {
        dynamic_array_push_back(&logic->last_insert_mode_inputs, *msg);
    }
}

int key_messages_parse_repeat_count(Array<Key_Message> messages, int* number_length)
{
    int repeat_count = 0;
    int message_index = 0;
    for (int i = 0; i < messages.size; i++)
    {
        Key_Message* msg = &messages[i];
        if (i == 0 && msg->character == '0') { // Special case, because 0 returns you back to start of the line
            *number_length = 0;
            return 1;
        }
        if (msg->character == 0 || !msg->key_down) { message_index++; continue; };
        if (msg->character >= '0' && msg->character <= '9') {
            message_index++;
            repeat_count = repeat_count * 10 + (msg->character - '0');
        }
        else break;
    }
    if (repeat_count == 0) repeat_count = 1;
    *number_length = message_index;
    return repeat_count;
}

template<typename T>
Array<T> dynamic_array_make_slice(DynamicArray<T>* array, int start_index, int end_index)
{
    end_index = math_clamp(end_index, 0, array->size-1);
    start_index = math_clamp(start_index, 0, end_index);
    Array<T> result;
    result.data = &array->data[start_index];
    result.size = end_index - start_index + 1;
    return result;
}

struct MotionParseResult
{
    bool motion_detected;
    int motion_length; // Length in key messages
    int character_start, character_end;
    int line_start, line_end;
    bool completion_possible; // If no motion was detected, but a motion could be completed with more input (valid prefix)
};

void motion_parse_result_clamp_cursor_position(MotionParseResult* result, Text_Editor* editor) {
    result->line_end = math_clamp(result->line_end, 0, math_maximum(0, editor->lines.size-1));
    result->line_start = math_clamp(result->line_start, 0, math_maximum(0, editor->lines.size-1));
    result->character_end = math_clamp(result->character_end, 0, math_maximum(0, editor->lines[result->line_end].size-1));
    result->character_start = math_clamp(result->character_start, 0, math_maximum(0, editor->lines[result->line_start].size-1));
}

struct Text_Editor_Iterator
{
    Text_Editor* editor;
    int current_character, current_line;
    char character;
};

bool text_editor_iterator_has_next(Text_Editor_Iterator* iterator) 
{
    String* line = &iterator->editor->lines[iterator->current_line];
    if (iterator->current_character < line->size) {
        return true;
    }
    else if (iterator->current_line < iterator->editor->lines.size-1) {
        return true;
    }
    return false;
}

void text_editor_iterator_advance(Text_Editor_Iterator* iterator) 
{
    if (!text_editor_iterator_has_next(iterator)) return;

    String* line = &iterator->editor->lines[iterator->current_line];
    if (iterator->current_character < line->size) {
        iterator->current_character++;
    }
    else {
        iterator->current_character = 0;
        iterator->current_line++;
        if (iterator->current_line >= iterator->editor->lines.size) {
            return;
        }
    }

    // Set current character
    line = &iterator->editor->lines[iterator->current_line];
    if (iterator->current_character >= line->size) {
        iterator->character = '\n';
    }
    else {
        iterator->character = line->characters[iterator->current_character];
    }
}

Text_Editor_Iterator text_editor_iterator_make(Text_Editor* editor, int character, int line) 
{
    Text_Editor_Iterator result;
    result.editor = editor;
    result.current_line = line;
    result.current_character = character;
    if (text_editor_iterator_has_next(&result)) {
        if (character >= editor->lines[line].size) { 
            result.character = '\n';
        }
        else {
            result.character = editor->lines[result.current_line].characters[result.current_character]; 
        }
    }
    return result;
}

bool text_editor_iterator_goto_next_in_set(Text_Editor_Iterator* iterator, String set) {
    while (text_editor_iterator_has_next(iterator)) {
        for (int i = 0; i < set.size; i++) {
            char c = set.characters[i];
            if (iterator->character == c) {
                return true;
            }
        }
        text_editor_iterator_advance(iterator);
    }
    return false;
}

bool string_contains_character(String string, char character) {
    for (int i = 0; i < string.size; i++) {
        if (string.characters[i] == character) {
            return true;
        }
    }
    return false;
}

bool text_editor_iterator_skip_characters_in_set(Text_Editor_Iterator* iterator, String set, bool skip_in_set) {
    while (text_editor_iterator_has_next(iterator)) 
    {
        bool is_in_set = string_contains_character(set, iterator->character);
        if (!skip_in_set && is_in_set) {
            return true;
        }
        else if (skip_in_set && !is_in_set) {
            return true;
        }
        text_editor_iterator_advance(iterator);
    }

    return false;
}

String characters_get_string_valid_identifier_characters() {
    return string_create_static("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890_");
}

String characters_get_string_all_letters() {
    return string_create_static("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

// Returns true if there was a movement
MotionParseResult key_messages_parse_movement(
    Text_Editor_Logic* logic, Text_Editor* editor,
    Array<Key_Message> messages)
{
    MotionParseResult result;
    result.character_start = editor->current_character;
    result.line_start = editor->current_line;

    // All movements may start with a repeat count
    int repeat_count_end_index = 0;
    int repeat_count = key_messages_parse_repeat_count(messages, &repeat_count_end_index);
    if (repeat_count_end_index >= messages.size) {
        result.completion_possible = true;
        result.motion_detected = false;
        return result;
    }

    // Check for 1 character movements
    {
        Key_Message* msg = &messages[repeat_count_end_index];
        bool single_character_movement = false;
        result.line_end = result.line_start;
        result.character_end = result.character_start;

        if (msg->character == 'h') {
            result.character_end = result.character_start - repeat_count;
            single_character_movement = true;
        }
        else if (msg->character == 'l') {
            result.character_end = result.character_start + repeat_count;
            single_character_movement = true;
        }
        else if (msg->character == 'j') {
            result.line_end = result.line_start + repeat_count;
            single_character_movement = true;
        }
        else if (msg->character == 'k') {
            result.line_end = result.line_start - repeat_count;
            single_character_movement = true;
        }
        else if (msg->character == '$') {
            result.character_end = editor->lines[editor->current_line].size;
            single_character_movement = true;
        }
        else if (msg->character == '0') {
            result.character_end = 0;
            single_character_movement = true;
        }
        else if (msg->character == 'w') 
        {
            // Find next word, meaning skip all characters until next ' ' '\t' '\n'
            single_character_movement = true;
            Text_Editor_Iterator iterator = text_editor_iterator_make(editor, editor->current_character, editor->current_line);
            String valid_identifiers = characters_get_string_valid_identifier_characters();
            if (string_contains_character(valid_identifiers, iterator.character)) 
            {
                if (text_editor_iterator_skip_characters_in_set(&iterator, valid_identifiers, true)) 
                {
                    text_editor_iterator_skip_characters_in_set(&iterator, string_create_static(" \n"), true);
                    result.character_end = iterator.current_character;
                    result.line_end = iterator.current_line;
                }
            }
            else {
                if (text_editor_iterator_skip_characters_in_set(&iterator, valid_identifiers, false)) {
                    result.character_end = iterator.current_character;
                    result.line_end = iterator.current_line;
                }
            }
        }
        else if (msg->character == 'W') {
            Text_Editor_Iterator iterator = text_editor_iterator_make(editor, editor->current_character, editor->current_line);
            single_character_movement = true;
            if (text_editor_iterator_goto_next_in_set(&iterator, string_create_static(" \n"))) {
                if (text_editor_iterator_skip_characters_in_set(&iterator, string_create_static(" \n"), true)) {
                    result.character_end = iterator.current_character;
                    result.line_end = iterator.current_line;
                }
            }
        }
        else if (msg->character == 'b') {

        }
        else if (msg->character == 'B') {

        }
        else if (msg->character == 'e') {

        }
        else if (msg->character == 'E') {

        }
        else if (msg->character == '%') {

        }
        if (single_character_movement) {
            result.motion_detected = true;
            result.motion_length = repeat_count_end_index+1;
            motion_parse_result_clamp_cursor_position(&result, editor);
            return result;
        }
    }

    // Multi character movements not currently supported (f and t movements)
    result.motion_detected = false;
    result.completion_possible = false;
    result.motion_length = 0;
    return result;
}

MotionParseResult key_messages_parse_motion(Text_Editor_Logic* logic, Text_Editor* editor, Array<Key_Message> messages)
{
    MotionParseResult result;
    // Motions may also be movements, so we check if we can parse a movement first
    result = key_messages_parse_movement(logic, editor, messages);
    if (result.motion_detected) {
        return result;
    }
    // Now we need to check for real motions
    bool completion_possible = result.completion_possible;
    // Motions may  start with a repeat count
    int repeat_count_end_index;
    int repeat_count = key_messages_parse_repeat_count(messages, &repeat_count_end_index);

    // Parse motions 'iw, aw, bracket types [ { " ( ' '
    result.motion_detected = false;
    return result;
}

// Returns true if an action was performed
bool normal_mode_handle_single_character_commands(Text_Editor_Logic* logic, Text_Editor* editor, Key_Message* msg, int repeat_count)
{
    if (msg->character == 'i') {
        insert_mode_enter(logic, editor);
        return true;
    }
    else if (msg->character == 'a') {
        insert_mode_enter(logic, editor);
        editor->current_character++;
        text_editor_logic_clamp_cursor(logic, editor);
        return true;
    }
    else if (msg->character == 'I') {
        insert_mode_enter(logic, editor);
        editor->current_character = 0;
        text_editor_logic_clamp_cursor(logic, editor);
        return true;
    }
    else if (msg->character == 'A') {
        editor->current_character = editor->lines[editor->current_line].size;
        insert_mode_enter(logic, editor);
        text_editor_logic_clamp_cursor(logic, editor);
        return true;
    }
    else if (msg->character == 'x')
    {
        String* line = &editor->lines[editor->current_line];
        string_remove_character(line, editor->current_character);
        text_editor_logic_clamp_cursor(logic, editor);
        logic->text_changed = true;
        return true;
    }
    else if (msg->character == 'o')
    {
        String line = string_create_empty(64);
        dynamic_array_insert_ordererd(&editor->lines, line, editor->current_line+1);
        editor->current_line++;
        text_editor_logic_clamp_cursor(logic, editor);
        insert_mode_enter(logic, editor);
        logic->text_changed = true;
        return true;
    }
    else if (msg->character == 'O')
    {
        String line = string_create_empty(64);
        dynamic_array_insert_ordererd(&editor->lines, line, editor->current_line);
        text_editor_logic_clamp_cursor(logic, editor);
        insert_mode_enter(logic, editor);
        logic->text_changed = true;
        return true;
    }
    else if (msg->character == '.')
    {
        for (int i = 0; i < logic->last_insert_mode_inputs.size; i++) {
            insert_mode_handle_message(logic, editor, &logic->last_insert_mode_inputs[i]);
        }
    }
    else if (msg->character == 'p')
    {
        String line = string_create_empty(64);
        SCOPE_EXIT(string_destroy(&line));
        Text_Editor_Iterator iterator = text_editor_iterator_make(editor, 0, 0);
        while (text_editor_iterator_has_next(&iterator)) {
            string_append_character(&line, iterator.character);
            text_editor_iterator_advance(&iterator);
        }
        logg("Iterator resutl: %s\n", line.characters);
        return true;
    }

    return false;
}

bool motion_parse_result_correct_start_end(MotionParseResult* motion)
{
    bool change = false;
    if (motion->line_end < motion->line_start) {
        int swap = motion->line_end;
        motion->line_end = motion->line_start;
        motion->line_start = swap;
        change = true;
    }
    if (motion->character_end < motion->character_start) {
        int swap = motion->character_end;
        motion->character_end = motion->character_start;
        motion->character_start = swap;
        change = true;
    }
    return change;
}

void normal_mode_handle_message(Text_Editor_Logic* logic, Text_Editor* editor, Key_Message* new_message)
{
    // Filter out special messages
    {
        if (new_message->key_code == KEY_CODE::L && new_message->ctrl_down) {
            dynamic_array_reset(&logic->normal_mode_incomplete_command);
            return;
        }
        //if (!(new_message->key_code == KEY_CODE::TAB && new_message->key_down)) { // Whitelisting all non-character creating keys
        if (!new_message->key_down || new_message->character == 0) return; // Ignore all other characters
        //}
    }
    DynamicArray<Key_Message>* messages = &logic->normal_mode_incomplete_command;
    dynamic_array_push_back(messages, *new_message);

    bool command_completion_possible = false;
    // Parse movements
    {
        MotionParseResult motion = key_messages_parse_movement(logic, editor, dynamic_array_make_slice(messages, 0, messages->size));
        if (motion.motion_detected) {
            editor->current_character = motion.character_end;
            editor->current_line = motion.line_end;
            text_editor_logic_clamp_cursor(logic, editor);
            dynamic_array_reset(messages);
            return;
        }
        else {
            command_completion_possible = command_completion_possible || motion.completion_possible;
        }
    }

    // Parse number, since each command can start with a repeat count
    int message_index;
    int repeat_count = key_messages_parse_repeat_count(dynamic_array_make_slice(messages, 0, messages->size), &message_index);
    if (message_index >= messages->size) {
        return; // Only a number is currently input, we need to wait for more input
    }
    Key_Message* msg = &messages->data[message_index];

    // Parse single character commands
    if (message_index == messages->size - 1) {
        if (normal_mode_handle_single_character_commands(logic, editor, msg, repeat_count)) {
            dynamic_array_reset(messages);
            return;
        }
    }

    // Parse multi character inputs
    if (msg->character == 'd')
    {
        if (messages->size - message_index <= 1) {
            return; // Command completion is possible, we just need more numbers after 'd'
        }
        else
        {
            msg = &messages->data[message_index+1];
            if (msg->character == 'd') {
                // Input is 'dd'
                for (int i = 0; i < repeat_count; i++) {
                    text_editor_remove_line(editor, editor->current_line);
                }
                text_editor_logic_clamp_cursor(logic, editor);
                logic->text_changed = true;
                dynamic_array_reset(messages);
                return;
            }
            else {
                MotionParseResult motion =
                    key_messages_parse_motion(logic, editor, dynamic_array_make_slice(messages, message_index+1, messages->size));
                if (motion.motion_detected)
                {
                    // Delete all in that motion
                    bool motion_swap = motion_parse_result_correct_start_end(&motion);
                    if (motion.line_start != motion.line_end) {
                        String* start_line = &editor->lines[motion.line_start];
                        string_truncate(start_line, motion.character_start);
                        for (int i = motion.line_start+1; i < motion.line_end; i++) {
                            text_editor_remove_line(editor, motion.line_start+1);
                        }
                        String* end_line = &editor->lines[motion.line_start+1];
                        string_remove_substring(end_line, 0, motion.character_end);
                        string_append_string(start_line, end_line);
                        text_editor_remove_line(editor, motion.line_start+1);
                    }
                    else {
                        String* line = &editor->lines[motion.line_start];
                        string_remove_substring(line, motion.character_start, motion.character_end);
                    }
                    editor->current_character = motion.character_start;
                    editor->current_line = motion.line_start;
                    text_editor_logic_clamp_cursor(logic, editor);
                    logic->text_changed = true;

                    dynamic_array_reset(messages);
                }
                else {
                    command_completion_possible = command_completion_possible | motion.completion_possible;
                }
            }
        }
    }

    // Clear input if command completion is not possible anymore
    if (!command_completion_possible) {
        logg("Could not parse input, length: %d\n", messages->size);
        dynamic_array_reset(messages);
    }
}

Text_Editor_Logic text_editor_logic_create()
{
    Text_Editor_Logic logic;
    logic.mode = TextEditorLogicMode::NORMAL;
    logic.fill_string = string_create_empty(256);
    logic.text_changed = true;

    logic.normal_mode_incomplete_command = dynamic_array_create_empty<Key_Message>(32);
    logic.last_insert_mode_inputs = dynamic_array_create_empty<Key_Message>(32);
    /*
    logic.normal_mode_commands = dynamic_array_create_empty<Text_Editor_Command>(128);
    dynamic_array_push_back(&logic.normal_mode_commands, text_editor_command_make('h', false, true, &text_editor_command_move_left));
    */

    return logic;
}

void text_editor_logic_destroy(Text_Editor_Logic* logic)
{
    string_destroy(&logic->fill_string);
    dynamic_array_destroy(&logic->normal_mode_incomplete_command);
    dynamic_array_destroy(&logic->last_insert_mode_inputs);
}

void key_message_append_to_string(Key_Message* msg, String* string)
{
    if (msg->character == 0) {
        string_append_formated(string, "char: '\\0' ");
    }
    else {
        string_append_formated(string, "char: '%c'  ", (byte)msg->character);
    }
    string_append_formated(string, "key_code: %s ", key_code_to_string(msg->key_code));
    string_append_formated(string, "down: %s ", msg->key_down ? "TRUE" : "FALSE");
    string_append_formated(string, "shift: %s ", msg->shift_down ? "TRUE" : "FALSE");
    string_append_formated(string, "alt: %s ", msg->alt_down ? "TRUE" : "FALSE");
}

float zoom = 0.0f;
void text_editor_logic_update(Text_Editor_Logic* logic, Text_Editor* editor, Input* input)
{
    // Update zoom in editor
    zoom += input->mouse_wheel_delta;
    editor->line_size_cm = 1.0f * math_power(1.1f, zoom);

    // Handle all messages
    for (int i = 0; i < input->key_messages.size; i++)
    {
        Key_Message* msg = &input->key_messages[i];
        if (logic->mode == TextEditorLogicMode::NORMAL) {
            normal_mode_handle_message(logic, editor, msg);
        }
        else {
            insert_mode_handle_message(logic, editor, msg);
        }
    }

    // Set editor cursor
    if (logic->mode == TextEditorLogicMode::INSERT) {
        editor->cursor_mode = TextEditorCursorMode::LINE;
    }
    else {
        editor->cursor_mode = TextEditorCursorMode::BLOCK;
    }

    

    // Stuff that has nothing to do with the text editor, but with the programming language
    // Do syntax highlighting
    if (logic->text_changed)
    {
        text_editor_reset_highlights(editor);
        text_editor_append_text_to_string(editor, &logic->fill_string);
        logg("\nPARSING SOURCE_CODE:\n-----------------------\n%s\n---------------------\n", logic->fill_string.characters);
        LexerResult result = lexer_parse_string(&logic->fill_string);
        SCOPE_EXIT(lexer_result_destroy(&result));

        // Highlight identifiers
        for (int i = 0; i < result.tokens.size; i++) {
            Token* token = &result.tokens.data[i];
            if (token->type == TokenTypeA::IDENTIFIER) {
                text_editor_add_highlight(
                    editor,
                    text_highlight_make(
                        vec3(0.7f, 0.7f, 1.0f),
                        vec3(0.0f, 0.0f, 0.0f),
                        token->character_position,
                        token->character_position + token->lexem_length
                    ),
                    token->line_number
                );
            }
            else if (token_type_is_keyword(token->type)) {
                text_editor_add_highlight(
                    editor,
                    text_highlight_make(
                        vec3(0.4f, 0.4f, 0.8f),
                        vec3(0.0f, 0.0f, 0.0f),
                        token->character_position,
                        token->character_position + token->lexem_length
                    ),
                    token->line_number
                );
            }
        }

        Parser parser = parser_parse(&result);
        SCOPE_EXIT(parser_destroy(&parser));
        String printed_ast = string_create_empty(256);
        SCOPE_EXIT(string_destroy(&printed_ast));
        ast_node_root_append_to_string(&printed_ast, &parser.root, &result);
        logg("Ast: \n%s\n", printed_ast.characters);

        if (parser.unresolved_errors.size > 0) {
            logg("There were parser errors: %d\n", parser.unresolved_errors.size);
            for (int i = 0; i < parser.unresolved_errors.size; i++) {
                logg("Parser error #%d: %s", parser.unresolved_errors.size, parser.unresolved_errors[i].error_message);
            }
        }
        else {
            int main_result = ast_interpreter_execute_main(&parser.root, &result);
            logg("Main RESULT: %d\n", main_result);
        }


        for (int i = 0; i < parser.unresolved_errors.size; i++)
        {
            ParserError* error = &parser.unresolved_errors.data[i];
            Token* start_token = &result.tokens.data[error->token_start_index];
            Token* end_token = &result.tokens.data[error->token_end_index];
            for (int i = start_token->line_number; i <= end_token->line_number && i < editor->lines.size; i++)
            {
                int char_start = 0;
                if (i == start_token->line_number) {
                    char_start = start_token->character_position;
                }
                int char_end = editor->lines.data[i].size;
                if (i == end_token->line_number) {
                    char_end = end_token->character_position + end_token->lexem_length;
                }
                text_editor_add_highlight(
                    editor,
                    text_highlight_make(
                        vec3(1.0f, 1.0f, 1.0f),
                        vec3(1.0f, 0.0f, 0.0f),
                        char_start, char_end
                    ),
                    i
                );
            }
        }
    }
    logic->text_changed = false;
}




