#include "text_editor.hpp"

#include "../../rendering/text_renderer.hpp"
#include "../../win32/window.hpp"
#include "../../rendering/opengl_state.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/shader_program.hpp"
#include "../../rendering/mesh_utils.hpp"
#include "../../math/scalars.hpp"
#include "../../utility/file_io.hpp"
#include "../../win32/timing.hpp"

//#include "compiler.hpp"
//#include "ast_interpreter.hpp"

void text_change_destroy_single(TextChange* change) 
{
    if (change->symbol_type == TextChangeType::STRING_INSERTION) {
        string_destroy(&change->string);
    }
    else if (change->symbol_type == TextChangeType::STRING_DELETION) {
        string_destroy(&change->string);
    }
    else if (change->symbol_type == TextChangeType::COMPLEX) {
        for (int i = 0; i < change->sub_changes.size; i++) {
            text_change_destroy_single(&change->sub_changes[i]);
        }
        dynamic_array_destroy(&change->sub_changes);
    }
}

void text_change_destroy_changes_in_future(TextChange* change) {
    TextChange* curr = change->next;
    while (curr != 0) {
        TextChange* after = curr->next;
        text_change_destroy_single(curr);
        delete curr;
        curr = after;
    }
}

void text_change_destroy_changes_in_past(TextChange* change) {
    TextChange* curr = change->previous;
    while (curr != 0) {
        TextChange* after = curr->previous;
        text_change_destroy_single(curr);
        delete curr;
        curr = after;
    }
}

void text_change_apply(TextChange* change, Text_Editor* editor)
{
    switch (change->symbol_type)
    {
    case TextChangeType::STRING_DELETION: {
        text_delete_slice(&editor->lines, change->slice);
        editor->cursor_position = change->slice.start;
        text_position_sanitize(&editor->cursor_position, editor->lines);
        break;
    }
    case TextChangeType::STRING_INSERTION: {
        text_insert_string(&editor->lines, change->slice.start, change->string);
        editor->cursor_position = change->slice.end;
        text_position_sanitize(&editor->cursor_position, editor->lines);
        break;
    }
    case TextChangeType::CHARACTER_DELETION: {
        Text_Slice slice = text_slice_make(change->character_position, text_position_next(change->character_position, editor->lines));
        text_delete_slice(&editor->lines, slice);
        break;
    }
    case TextChangeType::CHARACTER_INSERTION: {
        text_insert_character_before(&editor->lines, change->character_position, change->character);
        break;
    }
    case TextChangeType::COMPLEX: {
        for (int i = 0; i < change->sub_changes.size; i++) {
            text_change_apply(&change->sub_changes[i], editor);
        }
        break;
    }
    }
    editor->text_changed = true;
}

void text_editor_clamp_cursor(Text_Editor* editor);
void text_change_undo(TextChange* change, Text_Editor* editor)
{
    switch (change->symbol_type)
    {
    case TextChangeType::STRING_DELETION: {
        text_insert_string(&editor->lines, change->slice.start, change->string);
        editor->cursor_position = text_position_previous(change->slice.end, editor->lines);
        text_editor_clamp_cursor(editor);
        break;
    }
    case TextChangeType::STRING_INSERTION: {
        text_delete_slice(&editor->lines, change->slice);
        editor->cursor_position = change->slice.start;
        text_editor_clamp_cursor(editor);
        break;
    }
    case TextChangeType::CHARACTER_DELETION: {
        text_insert_character_before(&editor->lines, change->character_position, change->character);
        break;
    }
    case TextChangeType::CHARACTER_INSERTION: {
        Text_Slice slice = text_slice_make(change->character_position, text_position_next(change->character_position, editor->lines));
        text_delete_slice(&editor->lines, slice);
        break;
    }
    case TextChangeType::COMPLEX: {
        for (int i = change->sub_changes.size - 1; i >= 0; i--) {
            text_change_undo(&change->sub_changes[i], editor);
        }
        break;
    }
    }
    editor->text_changed = true;
}

TextHistory text_history_create() {
    TextHistory result;
    result.current = 0;
    result.undo_first_change = false;
    result.recording_depth = 0;
    return result;
}

void text_history_destroy(TextHistory* history) {
    if (history->current != 0) {
        text_change_destroy_changes_in_future(history->current);
        text_change_destroy_changes_in_past(history->current);
        delete history->current;
        history->current = 0;
    }
}

void text_editor_clamp_cursor(Text_Editor* editor);
void text_history_record_change(TextHistory* history, TextChange change) 
{
    if (history->recording_depth != 0) {
        dynamic_array_push_back(&history->complex_command, change);
        return;
    }

    TextChange* record = new TextChange();
    *record = change;

    // Special case at the start of the program
    if (history->current == 0) {
        history->current = record;
        history->undo_first_change = true;
        return;
    }
    // Future is erased when we do new stuff (TODO: Do some tree like structure stuff in this case)
    text_change_destroy_changes_in_future(history->current);

    if (history->current->previous == 0 && !history->undo_first_change) {
        text_change_destroy_single(history->current);
        delete history->current;
        history->current = record;
        history->undo_first_change = true;
    }
    else 
    {
        record->previous = history->current;
        history->current->next = record;
        history->current = record;
    }
}

void text_history_insert_string(TextHistory* history, Text_Editor* editor, Text_Position pos, String string)
{
    Text_Slice slice = text_calculate_insertion_string_slice(&editor->lines, pos, string);
    TextChange change;
    change.next = 0;
    change.previous = 0;
    change.slice = slice;
    change.string = string;
    change.symbol_type = TextChangeType::STRING_INSERTION;
    text_change_apply(&change, editor);
    text_editor_clamp_cursor(editor);
    text_history_record_change(history, change);
}

void text_history_delete_slice(TextHistory* history, Text_Editor* editor, Text_Slice slice)
{
    if (text_position_are_equal(slice.start, slice.end)) return;
    String str = string_create_empty(32);
    text_append_slice_to_string(editor->lines, slice, &str);
    TextChange change;
    change.next = 0;
    change.previous = 0;
    change.slice = slice;
    change.string = str;
    change.symbol_type = TextChangeType::STRING_DELETION;
    text_change_apply(&change, editor);
    text_editor_clamp_cursor(editor);
    text_history_record_change(history, change);
}

void text_history_insert_character(TextHistory* history, Text_Editor* editor, Text_Position pos, char c) {
    TextChange change;
    change.next = 0;
    change.previous = 0;
    change.character_position = pos;
    change.character = c;
    change.symbol_type = TextChangeType::CHARACTER_INSERTION;
    text_change_apply(&change, editor);
    text_editor_clamp_cursor(editor);
    text_history_record_change(history, change);
}

void text_history_delete_character(TextHistory* history, Text_Editor* editor, Text_Position pos) {
    text_position_sanitize(&pos, editor->lines);
    TextChange change;
    change.next = 0;
    change.previous = 0;
    change.character_position = pos;
    change.character = text_get_character_after(&editor->lines, pos);
    change.symbol_type = TextChangeType::CHARACTER_DELETION;
    text_change_apply(&change, editor);
    text_editor_clamp_cursor(editor);
    text_history_record_change(history, change);
}

void text_history_start_record_complex_command(TextHistory* history) {
    if (history->recording_depth < 0) panic("Error, recording depth is negative!!\n");
    if (history->recording_depth == 0) history->complex_command = dynamic_array_create_empty<TextChange>(32);
    history->recording_depth++;
}

void text_history_stop_record_complex_command(TextHistory* history) {
    if (history->recording_depth <= 0) panic("Recording stopped with invalid recording depth\n");
    history->recording_depth--;
    if (history->recording_depth == 0)
    {
        TextChange change;
        change.symbol_type = TextChangeType::COMPLEX;
        change.sub_changes = history->complex_command;
        change.next = 0;
        change.previous = 0;
        text_history_record_change(history, change);
    }
}

void text_history_undo(TextHistory* history, Text_Editor* editor) {
    if (history->recording_depth != 0) panic("Cannot undo history while recording!\n");
    if (history->current == 0) {
        logg("Undo history empty\n");
        return;
    }
    if (history->current->previous != 0) {
        text_change_undo(history->current, editor);
        history->current = history->current->previous;
    }
    else {
        if (history->undo_first_change) {
            history->undo_first_change = false;
            logg("Undo first change false\n");
            text_change_undo(history->current, editor);
        }
        else {
            logg("Undo history empty/at start\n");
        }
    }
}

void text_history_redo(TextHistory* history, Text_Editor* editor)
{
    if (history->recording_depth != 0) panic("Cannot redo history while recording!\n");
    if (history->current == 0) return;
    if (history->current->previous == 0 && !history->undo_first_change) {
        text_change_apply(history->current, editor);
        history->undo_first_change = true;
        return;
    }
    if (history->current->next != 0) {
        history->current = history->current->next;
        text_change_apply(history->current, editor);
    }
}

NormalModeCommand normal_mode_command_make(NormalModeCommandType::ENUM symbol_type, int repeat_count);
Movement movement_make(MovementType::ENUM symbol_type, int repeat_count, char search_char);
Text_Editor text_editor_create(TextRenderer* text_renderer, FileListener* listener, OpenGLState* state)
{
    Text_Editor result;
    result.lines = text_create_empty();

    result.renderer = text_renderer;
    result.text_highlights = dynamic_array_create_empty<DynamicArray<TextHighlight>>(32);
    result.cursor_shader = optional_unwrap(shader_program_create(listener, { "resources/shaders/cursor.frag", "resources/shaders/cursor.vert" }));
    result.cursor_mesh = mesh_utils_create_quad_2D(state);
    result.line_size_cm = 1;
    result.first_rendered_line = 0;
    result.first_rendered_char = 0;
    result.line_count_buffer = string_create_empty(16);
    result.last_editor_region = bounding_box_2_make_min_max(vec2(-1, -1), vec2(1, 1));
    result.last_text_height = 0.0f;

    result.history = text_history_create();
    result.mode = TextEditorMode::NORMAL;
    result.cursor_position = text_position_make(0, 0);
    result.horizontal_position = 0;
    result.text_changed = true;
    result.last_search_char = ' ';
    result.last_search_was_forwards = true;
    result.last_keymessage_time = 0.0;

    result.last_normal_mode_command = normal_mode_command_make(NormalModeCommandType::MOVEMENT, 0);
    result.last_normal_mode_command.movement = movement_make(MovementType::MOVE_LEFT, 0, 0);
    result.normal_mode_incomplete_command = dynamic_array_create_empty<Key_Message>(32);
    result.record_insert_mode_inputs = true;
    result.last_insert_mode_inputs = dynamic_array_create_empty<Key_Message>(32);
    result.yanked_string = string_create_empty(64);
    result.last_yank_was_line = false;

    result.parser = ast_parser_create();
    result.lexer = lexer_create();
    result.analyser = semantic_analyser_create();
    result.generator = bytecode_generator_create();
    result.bytecode_interpreter = bytecode_intepreter_create();
    result.intermediate_generator = intermediate_generator_create();

    return result;
}

void text_editor_destroy(Text_Editor* editor)
{
    text_history_destroy(&editor->history);
    text_destroy(&editor->lines);
    for (int i = 0; i < editor->text_highlights.size; i++) {
        dynamic_array_destroy(&editor->text_highlights.data[i]);
    }
    dynamic_array_destroy(&editor->text_highlights);
    shader_program_destroy(editor->cursor_shader);
    mesh_gpu_data_destroy(&editor->cursor_mesh);
    string_destroy(&editor->yanked_string);
    string_destroy(&editor->line_count_buffer);

    dynamic_array_destroy(&editor->normal_mode_incomplete_command);
    dynamic_array_destroy(&editor->last_insert_mode_inputs);

    ast_parser_destroy(&editor->parser);
    lexer_destroy(&editor->lexer);
    semantic_analyser_destroy(&editor->analyser);
    bytecode_generator_destroy(&editor->generator);
    bytecode_interpreter_destroy(&editor->bytecode_interpreter);
    intermediate_generator_destroy(&editor->intermediate_generator);
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

void text_editor_draw_bounding_box(Text_Editor* editor, OpenGLState* state, BoundingBox2 bb, vec4 color)
{
    opengl_state_set_blending_state(state, true, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_FUNC_ADD);
    shader_program_set_uniform(editor->cursor_shader, state, "position", bb.min);
    shader_program_set_uniform(editor->cursor_shader, state, "size", bb.max - bb.min);
    shader_program_set_uniform(editor->cursor_shader, state, "color", color);
    mesh_gpu_data_draw_with_shader_program(&editor->cursor_mesh, editor->cursor_shader, state);
    opengl_state_set_blending_state(state, false, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_FUNC_ADD);
}

BoundingBox2 text_editor_get_character_bounding_box(Text_Editor* editor, float text_height, int line, int character, BoundingBox2 editor_region)
{
    float glyph_advance = text_renderer_get_cursor_advance(editor->renderer, text_height);
    vec2 cursor_pos = vec2(glyph_advance * (character - editor->first_rendered_char), 0.0f) + 
        vec2(editor_region.min.x, editor_region.max.y - ((line - editor->first_rendered_line) + 1.0f) * text_height);
    vec2 cursor_size = vec2(glyph_advance, text_height);

    BoundingBox2 result;
    result.min = cursor_pos;
    result.max = cursor_pos + cursor_size;
    return result;
}

Text_Slice token_range_to_slice(Token_Range range, Text_Editor* editor)
{
    range.end_index = math_clamp(range.end_index, 0, editor->lexer.tokens.size - 1);
    return text_slice_make(
        editor->lexer.tokens[range.start_index].position.start,
        editor->lexer.tokens[range.end_index-1].position.end
    );
}
void text_editor_add_highlight_from_slice(Text_Editor* editor, Text_Slice slice, vec3 text_color, vec4 background_color);
void text_editor_render(Text_Editor* editor, OpenGLState* state, int width, int height, int dpi, BoundingBox2 editor_region, double time)
{
    // Testing
    /*
    {
        String test = string_create_empty(32);
        SCOPE_EXIT(string_destroy(&test));
        text_editor_reset_highlights(editor);

        int count = 0;
        Text_Slice min_slice = text_slice_make(text_position_make(0, 0), text_position_make(0, 0));
        int min_slice_size = 10000;
        int min_slice_index = -1;
        for (int i = 0; i < editor->parser.token_mapping.size; i++) 
        {
            Token_Range range = editor->parser.token_mapping[i];
            Text_Slice slice = token_range_to_slice(range, editor);
            if (text_slice_contains_position(slice, editor->cursor_position, editor->lines)) {
                int slice_size = 100000;
                if (slice.start.line != slice.end.line) slice_size = (slice.end.line - slice.start.line) * 15;
                else slice_size = slice.end.character - slice.start.character;
                if (slice_size < min_slice_size) {
                    min_slice_size = slice_size;
                    min_slice = slice;
                    min_slice_index = i;
                }
            }
        }
        if (min_slice_index != -1) {
            text_editor_add_highlight_from_slice(editor, min_slice, vec3(1.0f), vec4(0.75f, 0.0f, 0.0f, 0.5f));
            String type_str = ast_node_type_to_string(editor->parser.nodes[min_slice_index].type);
            string_append_formated(&test, "%s\n", type_str.characters);
            text_renderer_add_text(editor->renderer, &test, vec2(0.0f, 0.0f), 0.05f, 1.0f);
        }
    }
    */

    //text_editor_draw_bounding_box(editor, state, editor_region, vec3(0.0f, 0.2f, 0.0f));
    float text_height = 2.0f * (editor->line_size_cm) / (height / (float)dpi * 2.54f);
    editor->last_editor_region = editor_region;
    editor->last_text_height = text_height;

    // Calculate minimum and maximum line in viewport
    int max_line_count = (editor_region.max.y - editor_region.min.y) / text_height;
    if (editor->cursor_position.line < editor->first_rendered_line) {
        editor->first_rendered_line = editor->cursor_position.line;
    }
    int last_line = math_minimum(editor->first_rendered_line + max_line_count - 1, editor->lines.size - 1);
    if (editor->cursor_position.line > last_line) {
        last_line = editor->cursor_position.line;
        editor->first_rendered_line = last_line - max_line_count + 1;
    }

    // Draw line numbers (Reduces the editor viewport for the text)
    {
        string_reset(&editor->line_count_buffer);
        string_append_formated(&editor->line_count_buffer, "%d ", editor->lines.size);
        int line_number_char_count = editor->line_count_buffer.size;

        vec2 line_pos = vec2(editor_region.min.x, editor_region.max.y - text_height);
        for (int i = editor->first_rendered_line; i <= last_line; i++)
        {
            // Do line number formating
            string_reset(&editor->line_count_buffer);
            if (i == editor->cursor_position.line) {
                string_append_formated(&editor->line_count_buffer, "%d", i);
            }
            else {
                int offset_to_cursor = math_absolute(editor->cursor_position.line - i);
                string_append_formated(&editor->line_count_buffer, "%d", offset_to_cursor);
                while (editor->line_count_buffer.size < line_number_char_count) {
                    string_insert_character_before(&editor->line_count_buffer, ' ', 0);
                }
            }

            // Trim line number if we are outside of the text_region
            TextLayout* layout = text_renderer_calculate_text_layout(editor->renderer, &editor->line_count_buffer, text_height, 1.0f);
            for (int j = layout->character_positions.size - 1; j >= 0; j--) {
                BoundingBox2 positioned_char = layout->character_positions[j].bounding_box;
                positioned_char.min += line_pos;
                positioned_char.max += line_pos;
                if (!bounding_box_2_is_other_box_inside(editor_region, positioned_char)) {
                    dynamic_array_remove_ordered(&layout->character_positions, j);
                }
                else {
                    layout->character_positions[j].color = vec3(0.5f, 0.5f, 1.0f);
                }
            }
            text_renderer_add_text_from_layout(editor->renderer, layout, line_pos);
            line_pos.y -= (text_height);
        }
        editor_region.min.x += text_renderer_calculate_text_width(editor->renderer, line_number_char_count + 1, text_height);
    }

    // Calculate the first and last character to be drawn in any line (Viewport)
    int max_character_count = (editor_region.max.x - editor_region.min.x) / text_renderer_get_cursor_advance(editor->renderer, text_height);
    if (editor->cursor_position.character < editor->first_rendered_char) {
        editor->first_rendered_char = editor->cursor_position.character;
    }
    int last_char = editor->first_rendered_char + max_character_count - 1;
    if (editor->cursor_position.character > last_char) {
        last_char = editor->cursor_position.character;
        editor->first_rendered_char = last_char - max_character_count + 1;
    }

    // Draw lines
    text_editor_synchronize_highlights_array(editor);
    vec2 line_pos = vec2(editor_region.min.x, editor_region.max.y - text_height);
    for (int i = editor->first_rendered_line; i <= last_line; i++)
    {
        String* line = &editor->lines[i];
        String truncated_line = string_create_substring_static(line, editor->first_rendered_char, last_char + 1);
        TextLayout* line_layout = text_renderer_calculate_text_layout(editor->renderer, &truncated_line, text_height, 1.0f);
        for (int j = 0; j < editor->text_highlights.data[i].size; j++)
        {
            TextHighlight* highlight = &editor->text_highlights.data[i].data[j];
            // Draw text background 
            {
                BoundingBox2 highlight_start = text_editor_get_character_bounding_box(editor,
                    text_height, i, highlight->character_start, editor_region);
                BoundingBox2 highlight_end = text_editor_get_character_bounding_box(editor, text_height, i, highlight->character_end - 1,
                    editor_region);
                BoundingBox2 combined = bounding_box_2_combine(highlight_start, highlight_end);
                text_editor_draw_bounding_box(editor, state, combined, highlight->background_color);
            }
            // Set text color
            for (int k = highlight->character_start; k < highlight->character_end &&
                k - editor->first_rendered_char < line_layout->character_positions.size; k++)
            {
                if (k - editor->first_rendered_char < 0) continue;
                Character_Position* char_pos = &line_layout->character_positions.data[k - editor->first_rendered_char];
                char_pos->color = highlight->text_color;
            }
        }

        text_renderer_add_text_from_layout(editor->renderer, line_layout, line_pos);
        line_pos.y -= (text_height);
    }
    text_renderer_render(editor->renderer, state);

    // Draw cursor 
    {
        // Actually i want some time since last input to start blinking the cursor
        double inactivity_time_to_cursor_blink = 1.0;
        double blink_length = 0.5;
        bool show_cursor = true;
        if (editor->last_keymessage_time + inactivity_time_to_cursor_blink < time) {
            show_cursor = math_modulo(time - editor->last_keymessage_time - inactivity_time_to_cursor_blink, blink_length * 2.0) > blink_length;
        }
        BoundingBox2 cursor_bb = text_editor_get_character_bounding_box(
            editor, text_height, editor->cursor_position.line, editor->cursor_position.character, editor_region
        );
        // Change cursor height if there are messages to be parsed
        float cursor_height = text_height;
        if (editor->mode == TextEditorMode::NORMAL && editor->normal_mode_incomplete_command.size != 0) cursor_height *= 0.5f;
        cursor_bb.max.y = cursor_bb.min.y + cursor_height;

        if (editor->mode == TextEditorMode::INSERT) {
            float pixel_normalized = 2.0f / width;
            float width = math_maximum(pixel_normalized * 3.0f, text_height * 0.04f);
            cursor_bb.max.x = cursor_bb.min.x + width;
        }
        if (show_cursor) {
            text_editor_draw_bounding_box(editor, state, cursor_bb, vec4(0.0f, 1.0f, 0.0f, 1.0f));
        }
    }
}

TextHighlight text_highlight_make(vec3 text_color, vec4 background_color, int character_start, int character_end) {
    TextHighlight result;
    result.background_color = background_color;
    result.text_color = text_color;
    result.character_end = character_end;
    result.character_start = character_start;
    return result;
}

void text_editor_add_highlight_from_slice(Text_Editor* editor, Text_Slice slice, vec3 text_color, vec4 background_color)
{
    for (int line = slice.start.line; line <= slice.end.line; line++) {
        int start_character = 0;
        int end_character = editor->lines[line].size;
        if (line == slice.start.line) start_character = slice.start.character;
        if (line == slice.end.line) end_character = slice.end.character;
        if (start_character != end_character) {
            text_editor_add_highlight(editor, text_highlight_make(text_color, background_color, start_character, end_character), line);
        }
    }
}


/*
    Logic
*/
String characters_get_string_valid_identifier_characters() {
    return string_create_static("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890_");
}

String characters_get_string_non_identifier_non_whitespace() {
    return string_create_static("!\"§$%&/()[]{}<>|=\\?´`+*~#'-.:,;^°");
}

String characters_get_string_whitespaces() {
    return string_create_static("\n \t");
}

String characters_get_string_all_letters() {
    return string_create_static("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

Movement movement_make(MovementType::ENUM symbol_type, int repeat_count, char search_char = '\0') {
    Movement result;
    result.symbol_type = symbol_type;
    result.repeat_count = repeat_count;
    result.search_char = search_char;
    return result;
}

Text_Slice text_slice_make_inside_parenthesis(DynamicArray<String>* text, Text_Position pos, char open_parenthesis, char closed_parenthesis)
{
    Text_Slice result = text_slice_make(pos, pos);
    Text_Position text_start = text_position_make_start();
    Text_Position text_end = text_position_make_end(text);

    // Get to first parenthesis
    // "w(123)"
    Text_Position start = pos;
    int indentation_level = 1;
    while (!text_position_are_equal(start, text_start))
    {
        if (text_get_character_after(text, start) == closed_parenthesis && !text_position_are_equal(start, pos)) {
            indentation_level++;
        }
        else if (text_get_character_after(text, start) == open_parenthesis) {
            indentation_level--;
        }
        if (indentation_level == 0) break;
        start = text_position_previous(start, *text);
    }
    if (indentation_level != 0) return result;
    start = text_position_next(start, *text); // Because we want to be inside the parenthesis, not on them

    // Now we have to move forward until we hit the closing parenthesis
    Text_Position end = start;
    indentation_level = 1;
    while (!text_position_are_equal(end, text_end))
    {
        if (text_get_character_after(text, end) == closed_parenthesis) {
            indentation_level--;
        }
        else if (text_get_character_after(text, end) == open_parenthesis) {
            indentation_level++;
        }
        if (indentation_level == 0) break;
        end = text_position_next(end, *text);
    }
    if (indentation_level != 0) return result; // Error because there was no proper end of indentation

    result = text_slice_make(start, end);
    return result;
}

Text_Slice text_slice_make_enclosure(DynamicArray<String>* text, Text_Position pos,
    String enclosure_start_set, bool complement_start_set, String enclosure_end_set, bool complement_end_set)
{
    Text_Position text_start = text_position_make_start();
    Text_Position text_end = text_position_make_end(text);

    // Go backwards until we find the start of the word
    Text_Position i = pos;
    Text_Position word_start = pos;
    while (!text_position_are_equal(i, text_start))
    {
        i = text_position_previous(i, *text);
        if (complement_start_set) {
            if (!string_contains_character(enclosure_start_set, text_get_character_after(text, i)))
                break;
        }
        else {
            if (string_contains_character(enclosure_start_set, text_get_character_after(text, i)))
                break;
        }
        word_start = i;
    }

    // Go forwards until we find the end of the word
    i = pos;
    Text_Position word_end = pos;
    while (!text_position_are_equal(i, text_end))
    {
        i = text_position_next(i, *text);
        word_end = i;
        if (complement_start_set) {
            if (!string_contains_character(enclosure_end_set, text_get_character_after(text, i)))
                break;
        }
        else {
            if (string_contains_character(enclosure_end_set, text_get_character_after(text, i)))
                break;
        }
    }
    return text_slice_make(word_start, word_end);
}

Text_Slice text_slice_get_current_word_slice(DynamicArray<String>* text, Text_Position pos, bool* on_word)
{
    *on_word = false;
    Text_Iterator it = text_iterator_make(text, pos);
    String whitespace_characters = characters_get_string_whitespaces();
    String operator_characters = characters_get_string_non_identifier_non_whitespace();
    String identifier_characters = characters_get_string_valid_identifier_characters();
    if (string_contains_character(whitespace_characters, it.character)) return text_slice_make(pos, pos);
    if (string_contains_character(identifier_characters, it.character)) {
        *on_word = true;
        return text_slice_make_enclosure(text, pos, identifier_characters, true, identifier_characters, true);
    }
    if (string_contains_character(operator_characters, it.character)) {
        *on_word = true;
        return text_slice_make_enclosure(text, pos, operator_characters, true, operator_characters, true);
    }
    panic("Characters in string were not whitespace, not operator and not identifer, character: %c\n", it.character);
    return text_slice_make(pos, pos);
}

// Forward declarations or something
Text_Slice motion_evaluate_at_position(Motion motion, DynamicArray<String>* text, Text_Position pos, char*, bool*, int*);
Motion motion_make(MotionType::ENUM symbol_type, int repeat_count, bool contains_edges);

Text_Position movement_evaluate_at_position(Movement movement, DynamicArray<String>* text, Text_Position pos,
    char* last_search_char, bool* last_search_was_forwards, int* horizontal_position)
{
    String word_characters = characters_get_string_valid_identifier_characters();
    String whitespace_characters = characters_get_string_whitespaces();
    String operator_characters = characters_get_string_non_identifier_non_whitespace();

    bool repeat_movement = true;
    bool set_horizontal_pos = true;
    for (int i = 0; i < movement.repeat_count && repeat_movement; i++)
    {
        Text_Iterator iterator = text_iterator_make(text, pos);
        Text_Position next_position = text_position_next(iterator.position, *text);
        text_position_sanitize(&next_position, *text);
        char next_character = text_get_character_after(text, next_position);
        switch (movement.symbol_type)
        {
        case MovementType::MOVE_DOWN: {
            pos.line += 1;
            pos.character = *horizontal_position;
            set_horizontal_pos = false;
            break;
        }
        case MovementType::MOVE_UP: {
            pos.line -= 1;
            pos.character = *horizontal_position;
            set_horizontal_pos = false;
            break;
        }
        case MovementType::MOVE_LEFT: {
            pos.character -= 1;
            break;
        }
        case MovementType::MOVE_RIGHT: {
            pos.character += 1;
            break;
        }
        case MovementType::TO_END_OF_LINE: {
            String* line = &text->data[pos.line];
            pos.character = line->size;
            *horizontal_position = 10000; // Look at jk movements after $ to understand this
            set_horizontal_pos = false;
            break;
        }
        case MovementType::TO_START_OF_LINE: {
            pos.character = 0;
            break;
        }
        case MovementType::NEXT_WORD:
        {
            bool currently_on_word;
            Text_Slice current_word = text_slice_get_current_word_slice(text, pos, &currently_on_word);
            if (currently_on_word) {
                text_iterator_set_position(&iterator, current_word.end);
                text_iterator_skip_characters_in_set(&iterator, whitespace_characters, true);
            }
            text_iterator_skip_characters_in_set(&iterator, whitespace_characters, true);
            pos = iterator.position;
            break;
        }
        case MovementType::NEXT_SPACE: {
            text_iterator_skip_characters_in_set(&iterator, whitespace_characters, false); // Skip current non-whitespaces
            text_iterator_skip_characters_in_set(&iterator, whitespace_characters, true); // Skip current whitespaces
            pos = iterator.position;
            break;
        }
        case MovementType::END_OF_WORD: {
            bool currently_on_word;
            Text_Slice current_word = text_slice_get_current_word_slice(text, pos, &currently_on_word);
            if (currently_on_word)
            {
                // Check if we are on end of word
                if (text_position_are_equal(iterator.position, text_position_previous(current_word.end, *text))) {
                    text_iterator_advance(&iterator);
                    text_iterator_skip_characters_in_set(&iterator, whitespace_characters, true);
                    current_word = text_slice_get_current_word_slice(text, iterator.position, &currently_on_word);
                    text_iterator_set_position(&iterator, text_position_previous(current_word.end, *text));
                }
                else {
                    text_iterator_set_position(&iterator, text_position_previous(current_word.end, *text));
                }
            }
            else {
                text_iterator_skip_characters_in_set(&iterator, whitespace_characters, true);
                current_word = text_slice_get_current_word_slice(text, iterator.position, &currently_on_word);
                text_iterator_set_position(&iterator, text_position_previous(current_word.end, *text));
            }
            pos = iterator.position;
            break;
        }
        case MovementType::END_OF_WORD_AFTER_SPACE: {
            Text_Slice current_word = motion_evaluate_at_position(motion_make(MotionType::SPACES, 1, false),
                text, iterator.position, last_search_char, last_search_was_forwards, horizontal_position);
            Text_Position result = text_position_previous(current_word.end, *text);
            if (text_position_are_equal(result, pos)) { // Currently on end of word, skip one character
                text_iterator_advance(&iterator);
            }
            text_iterator_skip_characters_in_set(&iterator, whitespace_characters, true); // Skip whitespace
            current_word = motion_evaluate_at_position(motion_make(MotionType::SPACES, 1, false),
                text, iterator.position, last_search_char, last_search_was_forwards, horizontal_position);
            pos = text_position_previous(current_word.end, *text);
            break;
        }
        case MovementType::PREVIOUS_SPACE: {
            Text_Slice current_word = motion_evaluate_at_position(motion_make(MotionType::SPACES, 1, false),
                text, iterator.position, last_search_char, last_search_was_forwards, horizontal_position);
            Text_Position it = pos;
            if (text_position_are_equal(current_word.start, it)) {
                it = text_position_previous(it, *text);
            }
            // iterator move backwards until not on space
            while (string_contains_character(whitespace_characters, text_get_character_after(text, it)) &&
                !text_position_are_equal(text_position_make_start(), it)) {
                it = text_position_previous(it, *text);
            }
            current_word = motion_evaluate_at_position(motion_make(MotionType::SPACES, 1, false),
                text, it, last_search_char, last_search_was_forwards, horizontal_position);
            pos = current_word.start;
            break;
        }
        case MovementType::PREVIOUS_WORD: {
            Text_Slice current_word = motion_evaluate_at_position(motion_make(MotionType::WORD, 1, false),
                text, iterator.position, last_search_char, last_search_was_forwards, horizontal_position);
            Text_Position it = pos;
            if (text_position_are_equal(current_word.start, it)) {
                it = text_position_previous(it, *text);
            }
            // iterator move backwards until not on space
            while (string_contains_character(whitespace_characters, text_get_character_after(text, it)) &&
                !text_position_are_equal(text_position_make_start(), it)) {
                it = text_position_previous(it, *text);
            }
            current_word = motion_evaluate_at_position(motion_make(MotionType::WORD, 1, false),
                text, it, last_search_char, last_search_was_forwards, horizontal_position);
            pos = current_word.start;
            break;
        }
        case MovementType::NEXT_PARAGRAPH: {
            int line = pos.line;
            while (line < text->size && string_contains_only_characters_in_set(&text->data[line], whitespace_characters, false)) {
                line++;
            }
            while (line < text->size && !string_contains_only_characters_in_set(&text->data[line], whitespace_characters, false)) {
                line++;
            }
            pos.line = line;
            pos.character = 0;
            break;
        }
        case MovementType::PREVIOUS_PARAGRAPH: {
            int line = pos.line;
            while (line > 0 && string_contains_only_characters_in_set(&text->data[line], whitespace_characters, false)) {
                line--;
            }
            while (line > 0 && !string_contains_only_characters_in_set(&text->data[line], whitespace_characters, false)) {
                line--;
            }
            pos.line = line;
            pos.character = 0;
            break;
        }
        case MovementType::JUMP_ENCLOSURE: {
            // If on some type of parenthesis its quite logical () {} [] <>, just search next thing
            // The question is what to do when not on such a thing
            char open_parenthesis = '\0';
            char closed_parenthesis = '\0';
            bool on_open_side = true;
            switch (iterator.character) {
            case '(': open_parenthesis = '('; closed_parenthesis = ')'; on_open_side = true; break;
            case ')': open_parenthesis = '('; closed_parenthesis = ')'; on_open_side = false; break;
            case '{': open_parenthesis = '{'; closed_parenthesis = '}'; on_open_side = true; break;
            case '}': open_parenthesis = '{'; closed_parenthesis = '}'; on_open_side = false; break;
            case '[': open_parenthesis = '['; closed_parenthesis = ']'; on_open_side = true; break;
            case ']': open_parenthesis = '['; closed_parenthesis = ']'; on_open_side = false; break;
            }
            if (open_parenthesis == '\0') {
                break;
            }
            Text_Slice slice = text_slice_make_inside_parenthesis(text, pos, open_parenthesis, closed_parenthesis);
            if (text_position_are_equal(slice.start, slice.end)) break;
            if (on_open_side) {
                pos = slice.end;
            }
            else {
                pos = text_position_previous(slice.start, *text);
            }
            break;
        }
        case MovementType::SEARCH_FORWARDS_FOR: {
            if (iterator.character == movement.search_char) text_iterator_advance(&iterator);
            bool found = text_iterator_goto_next_character(&iterator, movement.search_char, true);
            if (found) {
                pos = iterator.position;
            }
            *last_search_char = movement.search_char;
            *last_search_was_forwards = true;
            break;
        }
        case MovementType::SEARCH_FORWARDS_TO: {
            if (next_character == movement.search_char) {
                text_iterator_advance(&iterator);
                text_iterator_advance(&iterator);
            }
            bool found = text_iterator_goto_next_character(&iterator, movement.search_char, true);
            if (found) {
                text_iterator_move_back(&iterator);
                pos = iterator.position;
            }
            *last_search_char = movement.search_char;
            *last_search_was_forwards = true;
            break;
        }
        case MovementType::SEARCH_BACKWARDS_FOR: {
            if (iterator.character == movement.search_char) text_iterator_move_back(&iterator);
            bool found = text_iterator_goto_next_character(&iterator, movement.search_char, false);
            if (found) {
                pos = iterator.position;
            }
            *last_search_char = movement.search_char;
            *last_search_was_forwards = false;
            break;

        }
        case MovementType::SEARCH_BACKWARDS_TO: {
            if (iterator.character == movement.search_char) text_iterator_move_back(&iterator);
            bool found = text_iterator_goto_next_character(&iterator, movement.search_char, false);
            if (found) {
                pos = iterator.position;
            }
            *last_search_char = movement.search_char;
            *last_search_was_forwards = false;
            break;
        }
        case MovementType::REPEAT_LAST_SEARCH: {
            Movement search_movement;
            if (last_search_was_forwards) search_movement.symbol_type = MovementType::SEARCH_FORWARDS_FOR;
            else search_movement.symbol_type = MovementType::SEARCH_BACKWARDS_FOR;
            search_movement.search_char = *last_search_char;
            search_movement.repeat_count = 1;
            pos = movement_evaluate_at_position(search_movement, text, pos, last_search_char, last_search_was_forwards, horizontal_position);
            break;
        }
        case MovementType::REPEAT_LAST_SEARCH_REVERSE_DIRECTION: {
            Movement search_movement;
            if (!last_search_was_forwards) search_movement.symbol_type = MovementType::SEARCH_FORWARDS_FOR;
            else search_movement.symbol_type = MovementType::SEARCH_BACKWARDS_FOR;
            search_movement.search_char = *last_search_char;
            search_movement.repeat_count = 1;
            pos = movement_evaluate_at_position(search_movement, text, pos, last_search_char, last_search_was_forwards, horizontal_position);
            break;
        }
        case MovementType::GOTO_END_OF_TEXT: {
            pos = text_position_make_end(text);
            repeat_movement = false;
            break;
        }
        case MovementType::GOTO_START_OF_TEXT: {
            pos = text_position_make_start();
            repeat_movement = false;
            break;
        }
        case MovementType::GOTO_LINE_NUMBER: {
            pos.line = movement.repeat_count;
            repeat_movement = false;
            break;
        }
        default: {
            logg("ERROR: Movement not supported yet!\n");
        }
        }
        text_position_sanitize(&pos, *text);
        if (set_horizontal_pos) *horizontal_position = pos.character;
    }

    return pos;
}

/*
    Words are made out of (Like programming identifiers) A-Z 0-9 _
    Then we have operators like (){}[]/\-, so basically everything except \t\n ' ', which make up 'big' words
    So motion spaces and motion word do this:
        1. Are we currently on a word (Defined like above)
            - If yes, go back to find when we last werent on a word


*/

Motion motion_make(MotionType::ENUM symbol_type, int repeat_count, bool contains_edges) {
    Motion result;
    result.symbol_type = symbol_type;
    result.repeat_count = repeat_count;
    result.contains_edges = contains_edges;
    return result;
}

Motion motion_make_from_movement(Movement movement) {
    Motion result;
    result.symbol_type = MotionType::MOVEMENT;
    result.movement = movement;
    result.repeat_count = 1;
    result.contains_edges = false;
    return result;
}

Text_Slice motion_evaluate_at_position(Motion motion, DynamicArray<String>* text, Text_Position pos,
    char* last_search_char, bool* last_search_was_forwards, int* horizontal_position)
{
    Text_Slice result;
    switch (motion.symbol_type)
    {
    case MotionType::MOVEMENT: {
        Text_Position end_pos = movement_evaluate_at_position(motion.movement, text, pos, last_search_char, last_search_was_forwards, horizontal_position);
        if (!text_position_are_in_order(&pos, &end_pos)) {
            text_position_next(pos, *text);
        }
        result = text_slice_make(pos, end_pos);
        text_slice_sanitize(&result, *text);
        break;
    }
    case MotionType::WORD: {
        bool unused;
        result = text_slice_get_current_word_slice(text, pos, &unused);
        break;
    }
    case MotionType::SPACES: {
        String spaces = string_create_static(" \n\t");
        if (!string_contains_character(spaces, text_get_character_after(text, pos))) {
            result = text_slice_make_enclosure(text, pos, spaces, false, spaces, false);
        }
        else {
            result = text_slice_make(pos, pos);
        }
        break;
    }
    case MotionType::BRACES: {
        result = text_slice_make_inside_parenthesis(text, pos, '{', '}');
        break;
    }
    case MotionType::BRACKETS: {
        result = text_slice_make_inside_parenthesis(text, pos, '[', ']');
        break;
    }
    case MotionType::PARENTHESES: {
        result = text_slice_make_inside_parenthesis(text, pos, '(', ')');
        break;
    }
    case MotionType::QUOTATION_MARKS: {
        result = text_slice_make_enclosure(text, pos, string_create_static("\""), false, string_create_static("\""), false);
        break;
    }
    case MotionType::PARAGRAPH: {
        int paragraph_start = pos.line;
        int paragraph_end = pos.line;
        while (paragraph_start > 0) {
            String* line = &text->data[paragraph_start];
            if (string_contains_only_characters_in_set(line, string_create_static(" \t"), false)) break;
            paragraph_start--;
        }
        while (paragraph_end < text->size) {
            String* line = &text->data[paragraph_end];
            if (string_contains_only_characters_in_set(line, string_create_static(" \t"), false)) break;
            paragraph_end++;
        }
        result.start = text_position_make(paragraph_start, 0);
        result.end = text_position_make(paragraph_end, 0);
        break;
    }
    default:
        result = text_slice_make(pos, pos);
        logg("Motion not supported yet!\n");
    }

    if (motion.contains_edges && !text_position_are_equal(result.start, result.end)) {
        result.start = text_position_previous(result.start, *text);
        result.end = text_position_next(result.end, *text);
    }

    return result;
}

NormalModeCommand normal_mode_command_make(NormalModeCommandType::ENUM symbol_type, int repeat_count) {
    NormalModeCommand result;
    result.symbol_type = symbol_type;
    result.repeat_count = repeat_count;
    return result;
}

NormalModeCommand normal_mode_command_make_with_char(NormalModeCommandType::ENUM symbol_type, int repeat_count, char character) {
    NormalModeCommand result;
    result.symbol_type = symbol_type;
    result.repeat_count = repeat_count;
    result.character = character;
    return result;
}

NormalModeCommand normal_mode_command_make_with_motion(NormalModeCommandType::ENUM symbol_type, int repeat_count, Motion motion) {
    NormalModeCommand result;
    result.symbol_type = symbol_type;
    result.repeat_count = repeat_count;
    result.motion = motion;
    return result;
}

NormalModeCommand normal_mode_command_make_movement(Movement movement) {
    NormalModeCommand result;
    result.symbol_type = NormalModeCommandType::MOVEMENT;
    result.repeat_count = 1;
    result.movement = movement;
    return result;
}

namespace ParseResultType
{
    enum ENUM
    {
        SUCCESS,
        COMPLETABLE,
        FAILURE
    };
}

template<typename T>
struct ParseResult
{
    ParseResultType::ENUM symbol_type;
    int key_message_count; // How much messages were consumed from the array
    T result;
};

template<typename T>
ParseResult<T> parse_result_make_success(T t, int key_message_count) {
    ParseResult<T> result;
    result.symbol_type = ParseResultType::SUCCESS;
    result.key_message_count = key_message_count;
    result.result = t;
    return result;
}

template<typename T>
ParseResult<T> parse_result_make_failure() {
    ParseResult<T> result;
    result.symbol_type = ParseResultType::FAILURE;
    return result;
}

template<typename T>
ParseResult<T> parse_result_make_completable() {
    ParseResult<T> result;
    result.symbol_type = ParseResultType::COMPLETABLE;
    return result;
}

template<typename T, typename K>
ParseResult<T> parse_result_propagate_non_success(ParseResult<K> prev_result) {
    ParseResult<T> result;
    result.symbol_type = prev_result.symbol_type;
    return result;
}

ParseResult<int> key_messages_parse_repeat_count(Array<Key_Message> messages)
{
    int repeat_count = 0;
    int message_index = 0;
    for (int i = 0; i < messages.size; i++)
    {
        Key_Message* msg = &messages[i];
        if (i == 0 && msg->character == '0') { // Special case, because 0 returns you back to start of the line
            return parse_result_make_success(1, 0);
        }
        //if (msg->character == 0 || !msg->key_down) { message_index++; continue; };
        if (!msg->key_down) { message_index++; continue; };
        if (msg->character >= '0' && msg->character <= '9') {
            message_index++;
            repeat_count = repeat_count * 10 + (msg->character - '0');
        }
        else break;
    }
    if (repeat_count == 0) repeat_count = 1;
    return parse_result_make_success(repeat_count, message_index);
}

ParseResult<Movement> key_messages_parse_movement(Array<Key_Message> messages, ParseResult<int> repeat_count)
{
    if (messages.size == 0) return parse_result_make_completable<Movement>();

    // Check for 1 character movements
    {
        Key_Message msg = messages[0];
        if (msg.character == 'h') {
            return parse_result_make_success(movement_make(MovementType::MOVE_LEFT, repeat_count.result), 1);
        }
        else if (msg.character == 'l') {
            return parse_result_make_success(movement_make(MovementType::MOVE_RIGHT, repeat_count.result), 1);
        }
        else if (msg.character == 'j') {
            return parse_result_make_success(movement_make(MovementType::MOVE_DOWN, repeat_count.result), 1);
        }
        else if (msg.character == 'k') {
            return parse_result_make_success(movement_make(MovementType::MOVE_UP, repeat_count.result), 1);
        }
        else if (msg.character == '$') {
            return parse_result_make_success(movement_make(MovementType::TO_END_OF_LINE, repeat_count.result), 1);
        }
        else if (msg.character == '0') {
            return parse_result_make_success(movement_make(MovementType::TO_START_OF_LINE, repeat_count.result), 1);
        }
        else if (msg.character == 'w') {
            return parse_result_make_success(movement_make(MovementType::NEXT_WORD, repeat_count.result), 1);
        }
        else if (msg.character == 'W') {
            return parse_result_make_success(movement_make(MovementType::NEXT_SPACE, repeat_count.result), 1);
        }
        else if (msg.character == 'b') {
            return parse_result_make_success(movement_make(MovementType::PREVIOUS_WORD, repeat_count.result), 1);
        }
        else if (msg.character == 'B') {
            return parse_result_make_success(movement_make(MovementType::PREVIOUS_SPACE, repeat_count.result), 1);
        }
        else if (msg.character == 'e') {
            return parse_result_make_success(movement_make(MovementType::END_OF_WORD, repeat_count.result), 1);
        }
        else if (msg.character == 'E') {
            return parse_result_make_success(movement_make(MovementType::END_OF_WORD_AFTER_SPACE, repeat_count.result), 1);
        }
        else if (msg.character == '%') {
            return parse_result_make_success(movement_make(MovementType::JUMP_ENCLOSURE, repeat_count.result), 1);
        }
        else if (msg.character == ';') {
            return parse_result_make_success(movement_make(MovementType::REPEAT_LAST_SEARCH, repeat_count.result), 1);
        }
        else if (msg.character == ',') {
            return parse_result_make_success(movement_make(MovementType::REPEAT_LAST_SEARCH_REVERSE_DIRECTION, repeat_count.result), 1);
        }
        else if (msg.character == '}') {
            return parse_result_make_success(movement_make(MovementType::NEXT_PARAGRAPH, repeat_count.result), 1);
        }
        else if (msg.character == '{') {
            return parse_result_make_success(movement_make(MovementType::PREVIOUS_PARAGRAPH, repeat_count.result), 1);
        }
        else if (msg.character == 'G') {
            if (repeat_count.result > 1) {
                return parse_result_make_success(movement_make(MovementType::GOTO_LINE_NUMBER, repeat_count.result), 1);
            }
            else {
                return parse_result_make_success(movement_make(MovementType::GOTO_END_OF_TEXT, repeat_count.result), 1);
            }
        }
        else if (msg.character == 'g') {
            if (repeat_count.key_message_count != 0) {
                return parse_result_make_success(movement_make(MovementType::GOTO_LINE_NUMBER, repeat_count.result), 1);
            }
            if (messages.size == 1) {
                return parse_result_make_completable<Movement>();
            }
            if (messages.size > 1) {
                if (messages[1].character == 'g') {
                    return parse_result_make_success(movement_make(MovementType::GOTO_START_OF_TEXT, repeat_count.result), 2);
                }
            }
            return parse_result_make_failure<Movement>();
        }
    }

    // Check 2 character movements (f F t T)
    if (messages.size == 1 && (messages[0].character == 't' || messages[0].character == 'f' ||
        messages[0].character == 'F' || messages[0].character == 'T' || messages[0].character == 'g')) {
        return parse_result_make_completable<Movement>();
    }
    if (messages.size >= 2)
    {
        if (messages[0].character == 'f') {
            return parse_result_make_success(movement_make(MovementType::SEARCH_FORWARDS_FOR, repeat_count.result, messages[1].character), 2);
        }
        else if (messages[0].character == 'F') {
            return parse_result_make_success(movement_make(MovementType::SEARCH_BACKWARDS_FOR, repeat_count.result, messages[1].character), 2);
        }
        else if (messages[0].character == 't') {
            return parse_result_make_success(movement_make(MovementType::SEARCH_FORWARDS_TO, repeat_count.result, messages[1].character), 2);
        }
        else if (messages[0].character == 'T') {
            return parse_result_make_success(movement_make(MovementType::SEARCH_BACKWARDS_TO, repeat_count.result, messages[1].character), 2);
        }
    }

    return parse_result_make_failure<Movement>();
}

ParseResult<Motion> key_messages_parse_motion(Array<Key_Message> messages)
{
    ParseResult<int> repeat_count_parse = key_messages_parse_repeat_count(messages);
    messages = array_make_slice(&messages, repeat_count_parse.key_message_count, messages.size);
    if (messages.size == 0) return parse_result_make_completable<Motion>();

    // Motions may also be movements, so we check if we can parse a movement first
    ParseResult<Movement> movement_parse = key_messages_parse_movement(messages, repeat_count_parse);
    if (movement_parse.symbol_type == ParseResultType::SUCCESS) {
        return parse_result_make_success(motion_make_from_movement(movement_parse.result),
            movement_parse.key_message_count + repeat_count_parse.key_message_count);
    }

    // Now we need to check for real motions, which may start with a repeat count
    if (messages[0].character != 'i' && messages[0].character != 'a') return parse_result_propagate_non_success<Motion>(movement_parse);
    else if (messages.size == 1) return parse_result_make_completable<Motion>();
    bool contains_edges = messages[0].character == 'a';

    // Now we need to determine the motion
    switch (messages[1].character)
    {
    case 'w':
        return parse_result_make_success<Motion>(motion_make(MotionType::WORD, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case 'W':
        return parse_result_make_success<Motion>(motion_make(MotionType::SPACES, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case '(':
        return parse_result_make_success<Motion>(motion_make(MotionType::PARENTHESES, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case ')':
        return parse_result_make_success<Motion>(motion_make(MotionType::PARENTHESES, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case '[':
        return parse_result_make_success<Motion>(motion_make(MotionType::BRACKETS, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case ']':
        return parse_result_make_success<Motion>(motion_make(MotionType::BRACKETS, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case '{':
        return parse_result_make_success<Motion>(motion_make(MotionType::BRACES, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case '}':
        return parse_result_make_success<Motion>(motion_make(MotionType::BRACES, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case '"':
        return parse_result_make_success<Motion>(motion_make(MotionType::QUOTATION_MARKS, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case 'p':
        return parse_result_make_success<Motion>(motion_make(MotionType::PARAGRAPH, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case 'P':
        return parse_result_make_success<Motion>(motion_make(MotionType::PARAGRAPH, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    }

    return parse_result_propagate_non_success<Motion>(movement_parse);
}

ParseResult<NormalModeCommand> key_messages_parse_normal_mode_command(Array<Key_Message> messages)
{
    ParseResult<int> repeat_count = key_messages_parse_repeat_count(messages);
    if (repeat_count.symbol_type != ParseResultType::SUCCESS) {
        return parse_result_propagate_non_success<NormalModeCommand>(repeat_count);
    }

    messages = array_make_slice(&messages, repeat_count.key_message_count, messages.size);
    if (messages.size == 0) return parse_result_make_completable<NormalModeCommand>();

    // Check if it is a movement
    ParseResult<Movement> movement_parse = key_messages_parse_movement(messages, repeat_count);
    if (movement_parse.symbol_type == ParseResultType::SUCCESS) {
        return parse_result_make_success(normal_mode_command_make_movement(movement_parse.result),
            repeat_count.key_message_count + movement_parse.key_message_count);
    }
    else if (movement_parse.symbol_type == ParseResultType::COMPLETABLE) {
        return parse_result_make_completable<NormalModeCommand>();
    }

    // Check 1 character commands
    switch (messages[0].character)
    {
    case 'x':
        return parse_result_make_success(
            normal_mode_command_make(NormalModeCommandType::DELETE_CHARACTER, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'i':
        return parse_result_make_success(
            normal_mode_command_make(NormalModeCommandType::ENTER_INSERT_MODE_ON_CURSOR, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'I':
        return parse_result_make_success(
            normal_mode_command_make(NormalModeCommandType::ENTER_INSERT_MODE_LINE_START, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'a':
        return parse_result_make_success(
            normal_mode_command_make(NormalModeCommandType::ENTER_INSERT_MODE_AFTER_CURSOR, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'A':
        return parse_result_make_success(
            normal_mode_command_make(NormalModeCommandType::ENTER_INSERT_MODE_LINE_END, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'o':
        return parse_result_make_success(
            normal_mode_command_make(NormalModeCommandType::ENTER_INSERT_MODE_NEW_LINE_BELOW, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'O':
        return parse_result_make_success(
            normal_mode_command_make(NormalModeCommandType::ENTER_INSERT_MODE_NEW_LINE_ABOVE, repeat_count.result), 1 + repeat_count.key_message_count);
    case '.':
        return parse_result_make_success(
            normal_mode_command_make(NormalModeCommandType::REPEAT_LAST_COMMAND, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'D':
        return parse_result_make_success(
            normal_mode_command_make_with_motion(
                NormalModeCommandType::DELETE_MOTION, repeat_count.result, motion_make_from_movement(movement_make(MovementType::TO_END_OF_LINE, 1))
            ),
            1 + repeat_count.key_message_count);
    case 'C':
        return parse_result_make_success(
            normal_mode_command_make_with_motion(
                NormalModeCommandType::CHANGE_MOTION, repeat_count.result, motion_make_from_movement(movement_make(MovementType::TO_END_OF_LINE, 1))
            ),
            1 + repeat_count.key_message_count);
    case 'L':
        return parse_result_make_success(
            normal_mode_command_make(NormalModeCommandType::MOVE_CURSOR_VIEWPORT_BOTTOM, repeat_count.result),
            repeat_count.key_message_count + 1);
    case 'M':
        return parse_result_make_success(
            normal_mode_command_make(NormalModeCommandType::MOVE_CURSOR_VIEWPORT_CENTER, repeat_count.result),
            repeat_count.key_message_count + 1);
    case 'H':
        return parse_result_make_success(
            normal_mode_command_make(NormalModeCommandType::MOVE_CURSOR_VIEWPORT_TOP, repeat_count.result),
            repeat_count.key_message_count + 1);
    case 'p':
        return parse_result_make_success(normal_mode_command_make(NormalModeCommandType::PUT_AFTER_CURSOR, repeat_count.result),
            1 + repeat_count.key_message_count);
    case 'P':
        return parse_result_make_success(normal_mode_command_make(NormalModeCommandType::PUT_BEFORE_CURSOR, repeat_count.result),
            1 + repeat_count.key_message_count);
    case 'Y':
        return parse_result_make_success(normal_mode_command_make(NormalModeCommandType::YANK_LINE, repeat_count.result),
            1 + repeat_count.key_message_count);
    case 'u':
        return parse_result_make_success(normal_mode_command_make(NormalModeCommandType::UNDO, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'r':
    case 'd':
    case 'c':
    case 'v':
    case 'y':
    case 'z':
        if (messages.size == 1) return parse_result_make_completable<NormalModeCommand>();
    }
    if (messages[0].key_code == KEY_CODE::R && messages[0].ctrl_down && messages[0].key_down) {
        return parse_result_make_success(normal_mode_command_make(NormalModeCommandType::REDO, repeat_count.result), 1 + repeat_count.key_message_count);
    }

    if (messages.size == 1) return parse_result_make_failure<NormalModeCommand>(); // No 1 size command detected

    // Parse multi key normal mode commands (d and c for now)
    if (messages[0].character == 'y' && messages[1].character == 'y') {
        return parse_result_make_success(normal_mode_command_make(NormalModeCommandType::YANK_LINE, repeat_count.result),
            1 + repeat_count.key_message_count);
    }
    if (messages[0].character == 'd') {
        ParseResult<Motion> motion_parse = key_messages_parse_motion(array_make_slice(&messages, 1, messages.size));
        if (motion_parse.symbol_type == ParseResultType::SUCCESS) {
            return parse_result_make_success(
                normal_mode_command_make_with_motion(NormalModeCommandType::DELETE_MOTION, repeat_count.result, motion_parse.result),
                repeat_count.key_message_count + 1 + motion_parse.key_message_count
            );
        }
        if (messages[1].character == 'd') {
            return parse_result_make_success(
                normal_mode_command_make(NormalModeCommandType::DELETE_LINE, repeat_count.result),
                repeat_count.key_message_count + 2
            );
        }
        return parse_result_propagate_non_success<NormalModeCommand>(motion_parse);
    }
    if (messages[0].character == 'y') {
        ParseResult<Motion> motion_parse = key_messages_parse_motion(array_make_slice(&messages, 1, messages.size));
        if (motion_parse.symbol_type == ParseResultType::SUCCESS) {
            return parse_result_make_success(
                normal_mode_command_make_with_motion(NormalModeCommandType::YANK_MOTION, repeat_count.result, motion_parse.result),
                repeat_count.key_message_count + 1 + motion_parse.key_message_count
            );
        }
        return parse_result_propagate_non_success<NormalModeCommand>(motion_parse);
    }
    if (messages[0].character == 'c') {
        ParseResult<Motion> motion_parse = key_messages_parse_motion(array_make_slice(&messages, 1, messages.size));
        if (motion_parse.symbol_type == ParseResultType::SUCCESS) {
            return parse_result_make_success(
                normal_mode_command_make_with_motion(NormalModeCommandType::CHANGE_MOTION, repeat_count.result, motion_parse.result),
                repeat_count.key_message_count + 1 + motion_parse.key_message_count
            );
        }
        if (messages[1].character == 'c') {
            return parse_result_make_success(
                normal_mode_command_make(NormalModeCommandType::CHANGE_LINE, repeat_count.result),
                repeat_count.key_message_count + 2
            );
        }
        return parse_result_propagate_non_success<NormalModeCommand>(motion_parse);
    }
    if (messages[0].character == 'r') {
        return parse_result_make_success(
            normal_mode_command_make_with_char(NormalModeCommandType::REPLACE_CHARACTER, repeat_count.result, messages[1].character),
            repeat_count.key_message_count + 2
        );
    }
    if (messages[0].character == 'v') {
        ParseResult<Motion> motion_parse = key_messages_parse_motion(array_make_slice(&messages, 1, messages.size));
        if (motion_parse.symbol_type == ParseResultType::SUCCESS) {
            return parse_result_make_success(
                normal_mode_command_make_with_motion(NormalModeCommandType::VISUALIZE_MOTION, repeat_count.result, motion_parse.result),
                repeat_count.key_message_count + 1 + motion_parse.key_message_count
            );
        }
        return parse_result_propagate_non_success<NormalModeCommand>(motion_parse);
    }
    if (messages[0].character == 'z') {
        if (messages[1].character == 't') {
            return parse_result_make_success(normal_mode_command_make(NormalModeCommandType::MOVE_VIEWPORT_CURSOR_TOP, repeat_count.result),
                repeat_count.key_message_count + 2);
        }
        if (messages[1].character == 'z') {
            return parse_result_make_success(normal_mode_command_make(NormalModeCommandType::MOVE_VIEWPORT_CURSOR_CENTER, repeat_count.result),
                repeat_count.key_message_count + 2);
        }
        if (messages[1].character == 'b') {
            return parse_result_make_success(normal_mode_command_make(NormalModeCommandType::MOVE_VIEWPORT_CURSOR_BOTTOM, repeat_count.result),
                repeat_count.key_message_count + 2);
        }
        return parse_result_make_failure<NormalModeCommand>();
    }
    return parse_result_make_failure<NormalModeCommand>();
}

void text_editor_clamp_cursor(Text_Editor* editor)
{
    text_position_sanitize(&editor->cursor_position, editor->lines);
    String* line = &editor->lines[editor->cursor_position.line];
    if (line->size != 0 && editor->mode == TextEditorMode::NORMAL) {
        editor->cursor_position.character = math_clamp(editor->cursor_position.character, 0, line->size - 1);
    }
}

void insert_mode_enter(Text_Editor* editor) {
    editor->mode = TextEditorMode::INSERT;
    text_editor_clamp_cursor(editor);
    text_history_start_record_complex_command(&editor->history);
    if (editor->record_insert_mode_inputs) dynamic_array_reset(&editor->last_insert_mode_inputs);
}

void insert_mode_exit(Text_Editor* editor) {
    editor->mode = TextEditorMode::NORMAL;
    if (editor->cursor_position.character != 0) {
        editor->cursor_position.character--;
    }
    text_editor_clamp_cursor(editor);
    text_history_stop_record_complex_command(&editor->history);
    editor->horizontal_position = editor->cursor_position.character;
}

void insert_mode_handle_message(Text_Editor* editor, Key_Message* msg);
void normal_mode_command_execute(NormalModeCommand command, Text_Editor* editor)
{
    bool save_as_last_command = false;
    switch (command.symbol_type)
    {
    case NormalModeCommandType::CHANGE_LINE: {
        text_history_start_record_complex_command(&editor->history);
        text_history_delete_slice(&editor->history, editor, text_slice_make_line(editor->lines, editor->cursor_position.line));
        insert_mode_enter(editor);
        text_history_stop_record_complex_command(&editor->history);
        editor->cursor_position.character = 0;
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::CHANGE_MOTION: {
        Text_Slice slice = motion_evaluate_at_position(command.motion, &editor->lines, editor->cursor_position,
            &editor->last_search_char, &editor->last_search_was_forwards, &editor->horizontal_position);
        if (command.motion.symbol_type == MotionType::MOVEMENT &&
            (command.motion.movement.symbol_type == MovementType::SEARCH_FORWARDS_FOR ||
                command.motion.movement.symbol_type == MovementType::SEARCH_FORWARDS_TO)) {
            slice.end = text_position_next(slice.end, editor->lines);
        }
        text_history_start_record_complex_command(&editor->history);
        text_history_delete_slice(&editor->history, editor, slice);
        insert_mode_enter(editor);
        text_history_stop_record_complex_command(&editor->history);
        editor->cursor_position = slice.start;
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::DELETE_CHARACTER: {
        Text_Position next = editor->cursor_position;
        for (int i = 0; i < command.repeat_count; i++) {
            if (editor->lines[editor->cursor_position.line].size != 0) {
                text_history_delete_character(&editor->history, editor, editor->cursor_position);
                text_editor_clamp_cursor(editor);
            }
        }
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::DELETE_LINE: {
        if (editor->lines.size == 0) break;
        Text_Position delete_start = editor->cursor_position;
        delete_start.character = 0;
        Text_Position delete_end = editor->cursor_position;
        delete_end.character = 0;
        for (int i = 0; i < command.repeat_count; i++) {
            delete_end.line++;
        }
        bool delete_last_line = delete_end.line >= editor->lines.size;
        text_position_sanitize(&delete_end, editor->lines);

        string_reset(&editor->yanked_string);
        if (delete_last_line) {
            delete_end = text_position_make_end(&editor->lines);
            Text_Slice line_slice = text_slice_make(delete_start, delete_end);
            delete_start = text_position_previous(delete_start, editor->lines);
            text_append_slice_to_string(editor->lines, line_slice, &editor->yanked_string);
            string_append_character(&editor->yanked_string, '\n');
        }
        Text_Slice slice = text_slice_make(delete_start, delete_end);
        if (!delete_last_line) {
            text_append_slice_to_string(editor->lines, slice, &editor->yanked_string);
        }
        editor->last_yank_was_line = true;
        text_history_delete_slice(&editor->history, editor, slice);
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::DELETE_MOTION: {
        Text_Slice slice = motion_evaluate_at_position(command.motion, &editor->lines, editor->cursor_position,
            &editor->last_search_char, &editor->last_search_was_forwards, &editor->horizontal_position);
        if (command.motion.symbol_type == MotionType::MOVEMENT &&
            (command.motion.movement.symbol_type == MovementType::SEARCH_FORWARDS_FOR ||
                command.motion.movement.symbol_type == MovementType::SEARCH_FORWARDS_TO)) {
            slice.end = text_position_next(slice.end, editor->lines);
        }
        editor->last_yank_was_line = false;
        string_reset(&editor->yanked_string);
        text_append_slice_to_string(editor->lines, slice, &editor->yanked_string);
        text_history_delete_slice(&editor->history, editor, slice);
        editor->cursor_position = slice.start;
        text_editor_clamp_cursor(editor);
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::ENTER_INSERT_MODE_AFTER_CURSOR: {
        editor->cursor_position.character++;
        insert_mode_enter(editor);
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::ENTER_INSERT_MODE_ON_CURSOR: {
        insert_mode_enter(editor);
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::ENTER_INSERT_MODE_LINE_END: {
        editor->cursor_position.character = editor->lines[editor->cursor_position.line].size;
        insert_mode_enter(editor);
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::ENTER_INSERT_MODE_LINE_START: {
        editor->cursor_position.character = 0;
        Text_Iterator it = text_iterator_make(&editor->lines, editor->cursor_position);
        text_iterator_skip_characters_in_set(&it, string_create_static(" \t"), true);
        editor->cursor_position = it.position;
        insert_mode_enter(editor);
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::ENTER_INSERT_MODE_NEW_LINE_ABOVE: {
        Text_Position prev_line_last;
        prev_line_last.line = math_maximum(editor->cursor_position.line - 1, 0);
        prev_line_last.character = editor->lines[prev_line_last.line].size;
        text_history_start_record_complex_command(&editor->history);
        text_history_insert_string(&editor->history, editor, prev_line_last, string_create_formated("\n"));
        insert_mode_enter(editor);
        text_history_stop_record_complex_command(&editor->history); // Works because recording is an int that counts up/down
        editor->text_changed = true;
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::ENTER_INSERT_MODE_NEW_LINE_BELOW: {
        Text_Position line_last;
        line_last.line = editor->cursor_position.line;
        line_last.character = editor->lines[line_last.line].size;
        text_history_start_record_complex_command(&editor->history);
        text_history_insert_string(&editor->history, editor, line_last, string_create_formated("\n"));
        insert_mode_enter(editor);
        text_history_stop_record_complex_command(&editor->history); // Works because recording is an int that counts up/down
        editor->text_changed = true;
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::MOVEMENT: {
        for (int i = 0; i < command.repeat_count; i++) {
            editor->cursor_position = movement_evaluate_at_position(command.movement, &editor->lines, editor->cursor_position,
                &editor->last_search_char, &editor->last_search_was_forwards, &editor->horizontal_position);
            text_editor_clamp_cursor(editor);
        }
        break;
    }
    case NormalModeCommandType::VISUALIZE_MOTION: {
        Text_Slice slice = motion_evaluate_at_position(command.motion, &editor->lines, editor->cursor_position,
            &editor->last_search_char, &editor->last_search_was_forwards, &editor->horizontal_position);
        text_editor_reset_highlights(editor);
        text_editor_add_highlight_from_slice(editor, slice, vec3(1.0f), vec4(0.0f, 0.3f, 0.0f, 1.0f));
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::REPLACE_CHARACTER: {
        String* line = &editor->lines[editor->cursor_position.line];
        if (line->size == 0) break;
        text_history_start_record_complex_command(&editor->history);
        text_history_delete_character(&editor->history, editor, editor->cursor_position);
        text_history_insert_character(&editor->history, editor, editor->cursor_position, command.character);
        text_history_stop_record_complex_command(&editor->history);
        save_as_last_command = true;
        break;
    }
    case NormalModeCommandType::REPEAT_LAST_COMMAND: {
        editor->record_insert_mode_inputs = false;
        normal_mode_command_execute(editor->last_normal_mode_command, editor);
        if (editor->mode == TextEditorMode::INSERT) {
            for (int i = 0; i < editor->last_insert_mode_inputs.size; i++) {
                insert_mode_handle_message(editor, &editor->last_insert_mode_inputs[i]);
            }
        }
        editor->record_insert_mode_inputs = true;
        break;
    }
    case NormalModeCommandType::UNDO: {
        text_history_undo(&editor->history, editor);
        text_editor_clamp_cursor(editor);
        break;
    }
    case NormalModeCommandType::REDO: {
        text_history_redo(&editor->history, editor);
        text_editor_clamp_cursor(editor);
        break;
    }
    case NormalModeCommandType::YANK_MOTION: {
        Text_Slice slice = motion_evaluate_at_position(command.motion, &editor->lines, editor->cursor_position,
            &editor->last_search_char, &editor->last_search_was_forwards, &editor->horizontal_position);
        string_reset(&editor->yanked_string);
        text_append_slice_to_string(editor->lines, slice, &editor->yanked_string);
        editor->last_yank_was_line = false;
        break;
    }
    case NormalModeCommandType::YANK_LINE: {
        if (editor->lines.size == 0) break;
        Text_Position delete_start = editor->cursor_position;
        delete_start.character = 0;
        Text_Position delete_end = editor->cursor_position;
        delete_end.character = 0;
        for (int i = 0; i < command.repeat_count; i++) {
            delete_end.line++;
        }
        text_position_sanitize(&delete_end, editor->lines);
        Text_Slice slice = text_slice_make(delete_start, delete_end);
        string_reset(&editor->yanked_string);
        text_append_slice_to_string(editor->lines, slice, &editor->yanked_string);
        editor->last_yank_was_line = true;
        break;
    }
    case NormalModeCommandType::PUT_BEFORE_CURSOR: {
        if (editor->last_yank_was_line) {
            Text_Position pos = editor->cursor_position;
            pos.character = 0;
            text_position_sanitize(&pos, editor->lines);
            String copy = string_create_from_string_with_extra_capacity(&editor->yanked_string, 0);
            text_history_insert_string(&editor->history, editor, pos, copy);
            break;
        }
        String copy = string_create_from_string_with_extra_capacity(&editor->yanked_string, 0);
        text_history_insert_string(&editor->history, editor, editor->cursor_position, copy);
        break;
    }
    case NormalModeCommandType::PUT_AFTER_CURSOR: {
        if (editor->last_yank_was_line) {
            Text_Position pos = editor->cursor_position;
            pos.character = 0;
            pos.line++;
            text_position_sanitize(&pos, editor->lines);
            String copy = string_create_from_string_with_extra_capacity(&editor->yanked_string, 0);
            text_history_insert_string(&editor->history, editor, pos, copy);
            break;
        }
        editor->cursor_position = text_position_next(editor->cursor_position, editor->lines);
        String copy = string_create_from_string_with_extra_capacity(&editor->yanked_string, 0);
        text_history_insert_string(&editor->history, editor, editor->cursor_position, copy);
        break;
    }
    case NormalModeCommandType::MOVE_VIEWPORT_CURSOR_TOP: {
        editor->first_rendered_line = editor->cursor_position.line;
        break;
    }
    case NormalModeCommandType::MOVE_VIEWPORT_CURSOR_CENTER: {
        int line_count = (editor->last_editor_region.max.y - editor->last_editor_region.min.y) / editor->last_text_height;
        editor->first_rendered_line = math_maximum(0, editor->cursor_position.line - line_count / 2);
        break;
    }
    case NormalModeCommandType::MOVE_VIEWPORT_CURSOR_BOTTOM: {
        int line_count = (editor->last_editor_region.max.y - editor->last_editor_region.min.y) / editor->last_text_height;
        editor->first_rendered_line = math_maximum(0, editor->cursor_position.line - line_count);
        break;
    }
    case NormalModeCommandType::MOVE_CURSOR_VIEWPORT_TOP: {
        editor->cursor_position.line = editor->first_rendered_line;
        break;
    }
    case NormalModeCommandType::MOVE_CURSOR_VIEWPORT_CENTER: {
        int line_count = (editor->last_editor_region.max.y - editor->last_editor_region.min.y) / editor->last_text_height;
        editor->cursor_position.line = editor->first_rendered_line + line_count / 2;
        text_editor_clamp_cursor(editor);
        break;
    }
    case NormalModeCommandType::MOVE_CURSOR_VIEWPORT_BOTTOM: {
        int line_count = (editor->last_editor_region.max.y - editor->last_editor_region.min.y) / editor->last_text_height;
        editor->cursor_position.line = editor->first_rendered_line + line_count - 1;
        text_editor_clamp_cursor(editor);
        break;
    }
    }
    if (save_as_last_command) editor->last_normal_mode_command = command;
}

void insert_mode_handle_message(Text_Editor* editor, Key_Message* msg)
{
    bool msg_is_valid_command = true;
    if (msg->key_code == KEY_CODE::L && msg->ctrl_down) {
        if (editor->record_insert_mode_inputs) dynamic_array_push_back(&editor->last_insert_mode_inputs, *msg);
        insert_mode_exit(editor);
        return;
    }
    else if (msg->key_code == KEY_CODE::TAB && msg->key_down)
    {
        text_history_insert_character(&editor->history, editor, editor->cursor_position, ' ');
        editor->cursor_position.character++;
        while (editor->cursor_position.character % 4 != 0) {
            text_history_insert_character(&editor->history, editor, editor->cursor_position, ' ');
            editor->cursor_position.character++;
        }
    }
    else if (msg->key_code == KEY_CODE::W && msg->key_down && msg->ctrl_down) {
        NormalModeCommand cmd = normal_mode_command_make_with_motion(NormalModeCommandType::DELETE_MOTION, 1,
            motion_make_from_movement(movement_make(MovementType::PREVIOUS_WORD, 1, 0)));
        normal_mode_command_execute(cmd, editor);
    }
    else if (msg->key_code == KEY_CODE::U && msg->key_down && msg->ctrl_down) {
        Text_Position line_start = editor->cursor_position;
        line_start.character = 0;
        text_history_delete_slice(&editor->history, editor, text_slice_make(line_start, editor->cursor_position));
        editor->cursor_position = line_start;
    }
    else if (msg->character >= 32) {
        text_history_insert_character(&editor->history, editor, editor->cursor_position, msg->character);
        editor->cursor_position.character++;
    }
    else if (msg->key_code == KEY_CODE::RETURN && msg->key_down) {
        text_history_insert_character(&editor->history, editor, editor->cursor_position, '\n');
        editor->cursor_position.line++;
        editor->cursor_position.character = 0;
    }
    else if (msg->key_code == KEY_CODE::BACKSPACE && msg->key_down)
    {
        Text_Position prev = text_position_previous(editor->cursor_position, editor->lines);
        text_history_delete_character(&editor->history, editor, prev);
        editor->cursor_position = prev;
    }
    else { // No valid command found
        msg_is_valid_command = false;
    }

    if (msg_is_valid_command && editor->mode == TextEditorMode::INSERT && editor->record_insert_mode_inputs) {
        dynamic_array_push_back(&editor->last_insert_mode_inputs, *msg);
    }
}

void key_messages_append_to_string(Array<Key_Message> messages, String* str) {
    for (int i = 0; i < messages.size; i++) {
        Key_Message m = messages.data[i];
        string_append_formated(str, "\t");
        key_message_append_to_string(&m, str);
        string_append_formated(str, "\n");
    }
}

void normal_mode_handle_message(Text_Editor* editor, Key_Message* new_message)
{
    // Filter out special messages
    {
        if (new_message->key_code == KEY_CODE::L && new_message->ctrl_down && new_message->key_down) {
            dynamic_array_reset(&editor->normal_mode_incomplete_command);
            logg("Command canceled!\n");
            return;
        }
        // Filter out messages (Key Up messages + random shift or alt or ctrl clicks)
        if ((new_message->character == 0 && !(new_message->ctrl_down && new_message->key_down)) || !new_message->key_down
            || new_message->key_code == KEY_CODE::ALT) {
            //logg("message filtered\n");
            return;
        }
    }
    DynamicArray<Key_Message>* messages = &editor->normal_mode_incomplete_command;
    dynamic_array_push_back(messages, *new_message);

    ParseResult<NormalModeCommand> command_parse = key_messages_parse_normal_mode_command(dynamic_array_to_array(messages));
    if (command_parse.symbol_type == ParseResultType::SUCCESS) {
        normal_mode_command_execute(command_parse.result, editor);
        dynamic_array_reset(messages);
    }
    else if (command_parse.symbol_type == ParseResultType::FAILURE) {
        String output = string_create_formated("Could not parse input, length: %d\n", messages->size);
        SCOPE_EXIT(string_destroy(&output));
        key_messages_append_to_string(dynamic_array_to_array(messages), &output);
        logg("%s\n", output.characters);
        dynamic_array_reset(messages);
    }
}

float zoom = 0.0f;
void text_editor_update(Text_Editor* editor, Input* input, double current_time)
{
    // Update zoom in editor
    zoom += input->mouse_wheel_delta;
    editor->line_size_cm = 1.0f * math_power(1.1f, zoom);
    if (input->key_messages.size != 0) {
        editor->last_keymessage_time = current_time;
    }
    // Handle all messages
    for (int i = 0; i < input->key_messages.size; i++)
    {
        Key_Message* msg = &input->key_messages[i];

        if (editor->mode == TextEditorMode::NORMAL) {
            normal_mode_handle_message(editor, msg);
        }
        else {
            insert_mode_handle_message(editor, msg);
        }
    }
    if (!text_check_correctness(editor->lines)) {
        panic("error, suit yourself\n");
        __debugbreak();
    }

    // IDE STUFF
    if (input->key_down[KEY_CODE::CTRL] && input->key_pressed[KEY_CODE::S]) {
        String output = string_create_empty(256);
        SCOPE_EXIT(string_destroy(&output););
        text_append_to_string(&editor->lines, &output);
        file_io_write_file("editor_text.txt", array_create_static((byte*)output.characters, output.size));
        logg("Saved text file!\n");
    }

    if (editor->text_changed || input->key_pressed[KEY_CODE::F5])
    {
        String source_code = string_create_empty(2048);
        SCOPE_EXIT(string_destroy(&source_code));
        text_append_to_string(&editor->lines, &source_code);
        logg("--------SOURCE CODE--------: \n%s\n", source_code.characters);

        lexer_parse_string(&editor->lexer, &source_code);
        ast_parser_parse(&editor->parser, &editor->lexer);
        // Debug print AST
        {
            String printed_ast = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&printed_ast));
            ast_parser_append_to_string(&editor->parser, &printed_ast);
            logg("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
            logg("Ast: \n%s\n", printed_ast.characters);
        }

        if (true)
        { 
            if (editor->parser.errors.size == 0) {
                semantic_analyser_analyse(&editor->analyser, &editor->parser);
            }
            if (editor->analyser.errors.size == 0 && input->key_pressed[KEY_CODE::F5] && true) 
            {
                String result_str = string_create_empty(32);
                SCOPE_EXIT(string_destroy(&result_str));

                // Generate Intermediate Code
                intermediate_generator_generate(&editor->intermediate_generator, &editor->analyser);
                intermediate_generator_append_to_string(&result_str, &editor->intermediate_generator);
                logg("%s\n\n", result_str.characters);
                string_reset(&result_str);

                // Generate Bytecode from IM
                bytecode_generator_generate(&editor->generator, &editor->intermediate_generator);
                bytecode_generator_append_bytecode_to_string(&editor->generator, &result_str);
                logg("BYTECODE_GENERATOR RESULT: \n--------------------------------\n%s\n", result_str.characters);

                // Execute Bytecode
                double bytecode_start = timing_current_time_in_seconds();
                bytecode_interpreter_execute_main(&editor->bytecode_interpreter, &editor->generator);
                double bytecode_end = timing_current_time_in_seconds();
                float bytecode_time = (bytecode_end - bytecode_start);
                logg("Bytecode interpreter result: %d (%2.5f seconds)\n", *(int*)(byte*)&editor->bytecode_interpreter.return_register[0], bytecode_time);
            }
        }

        // Do syntax highlighting
        text_editor_reset_highlights(editor);
        for (int i = 0; i < editor->lexer.tokens_with_whitespaces.size; i++)
        {
            Token t = editor->lexer.tokens_with_whitespaces[i];
            vec3 IDENTIFIER_COLOR = vec3(0.7f, 0.7f, 1.0f);
            vec3 KEYWORD_COLOR = vec3(0.4f, 0.4f, 0.8f);
            vec3 COMMENT_COLOR = vec3(0.0f, 1.0f, 0.0f);
            vec4 BG_COLOR = vec4(0);
            if (t.type == Token_Type::COMMENT)
                text_editor_add_highlight_from_slice(editor, t.position, COMMENT_COLOR, BG_COLOR);
            else if (token_type_is_keyword(t.type))
                text_editor_add_highlight_from_slice(editor, t.position, KEYWORD_COLOR, BG_COLOR);
            else if (t.type == Token_Type::IDENTIFIER)
                text_editor_add_highlight_from_slice(editor, t.position, IDENTIFIER_COLOR, BG_COLOR);
        }
        // Highlight parse errors
        for (int i = 0; i < editor->parser.errors.size; i++) {
            Compiler_Error e = editor->parser.errors[i];
            text_editor_add_highlight_from_slice(editor, token_range_to_slice(e.range, editor), vec3(1.0f), vec4(1.0f, 0.0f, 0.0f, 0.3f));
            logg("Parse Error: %s\n", e.message);
        }
        if (editor->parser.errors.size == 0) {
            for (int i = 0; i < editor->analyser.errors.size; i++) {
                Compiler_Error e = editor->analyser.errors[i];
                text_editor_add_highlight_from_slice(editor, token_range_to_slice(e.range, editor), vec3(1.0f), vec4(1.0f, 0.0f, 0.0f, 0.3f));
                logg("Semantic Error: %s\n", e.message);
            }
        }
    }

    editor->text_changed = false;
}




