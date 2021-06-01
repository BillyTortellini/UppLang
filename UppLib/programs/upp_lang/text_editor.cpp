#include "text_editor.hpp"

#include "../../rendering/text_renderer.hpp"
#include "../../win32/window.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/shader_program.hpp"
#include "../../rendering/mesh_utils.hpp"
#include "../../math/scalars.hpp"
#include "../../utility/file_io.hpp"
#include "../../win32/timing.hpp"

//#include "compiler.hpp"
//#include "ast_interpreter.hpp"

void text_change_destroy_single(Text_Change* change) 
{
    if (change->type == Text_Change_Type::STRING_INSERTION) {
        string_destroy(&change->string);
    }
    else if (change->type == Text_Change_Type::STRING_DELETION) {
        string_destroy(&change->string);
    }
    else if (change->type == Text_Change_Type::COMPLEX) {
        for (int i = 0; i < change->sub_changes.size; i++) {
            text_change_destroy_single(&change->sub_changes[i]);
        }
        dynamic_array_destroy(&change->sub_changes);
    }
}

void text_change_destroy_changes_in_future(Text_Change* change) {
    Text_Change* curr = change->next;
    while (curr != 0) {
        Text_Change* after = curr->next;
        text_change_destroy_single(curr);
        delete curr;
        curr = after;
    }
}

void text_change_destroy_changes_in_past(Text_Change* change) {
    Text_Change* curr = change->previous;
    while (curr != 0) {
        Text_Change* after = curr->previous;
        text_change_destroy_single(curr);
        delete curr;
        curr = after;
    }
}

void text_editor_clamp_cursor(Text_Editor* editor);
void text_change_apply(Text_Change* change, Text_Editor* editor)
{
    switch (change->type)
    {
    case Text_Change_Type::STRING_DELETION: {
        text_delete_slice(&editor->text, change->slice);
        editor->cursor_position = change->slice.start;
        text_position_sanitize(&editor->cursor_position, editor->text);
        break;
    }
    case Text_Change_Type::STRING_INSERTION: {
        text_insert_string(&editor->text, change->slice.start, change->string);
        editor->cursor_position = change->slice.end;
        text_position_sanitize(&editor->cursor_position, editor->text);
        break;
    }
    case Text_Change_Type::CHARACTER_DELETION: {
        Text_Slice slice = text_slice_make(change->character_position, text_position_next(change->character_position, editor->text));
        text_delete_slice(&editor->text, slice);
        break;
    }
    case Text_Change_Type::CHARACTER_INSERTION: {
        text_insert_character_before(&editor->text, change->character_position, change->character);
        break;
    }
    case Text_Change_Type::COMPLEX: {
        for (int i = 0; i < change->sub_changes.size; i++) {
            text_change_apply(&change->sub_changes[i], editor);
        }
        break;
    }
    }
    text_editor_clamp_cursor(editor);
    editor->text_changed = true;
}

void text_change_undo(Text_Change* change, Text_Editor* editor)
{
    switch (change->type)
    {
    case Text_Change_Type::STRING_DELETION: {
        text_insert_string(&editor->text, change->slice.start, change->string);
        editor->cursor_position = text_position_previous(change->slice.end, editor->text);
        text_editor_clamp_cursor(editor);
        break;
    }
    case Text_Change_Type::STRING_INSERTION: {
        text_delete_slice(&editor->text, change->slice);
        editor->cursor_position = change->slice.start;
        text_editor_clamp_cursor(editor);
        break;
    }
    case Text_Change_Type::CHARACTER_DELETION: {
        text_insert_character_before(&editor->text, change->character_position, change->character);
        break;
    }
    case Text_Change_Type::CHARACTER_INSERTION: {
        Text_Slice slice = text_slice_make(change->character_position, text_position_next(change->character_position, editor->text));
        text_delete_slice(&editor->text, slice);
        break;
    }
    case Text_Change_Type::COMPLEX: {
        for (int i = change->sub_changes.size - 1; i >= 0; i--) {
            text_change_undo(&change->sub_changes[i], editor);
        }
        break;
    }
    }
    editor->cursor_position = change->cursor_pos_before_change;
    text_editor_clamp_cursor(editor);
    editor->text_changed = true;
}

Text_History text_history_create(Text_Editor* editor) {
    Text_History result;
    result.editor = editor;
    result.current = 0;
    result.undo_first_change = false;
    result.recording_depth = 0;
    return result;
}

void text_history_destroy(Text_History* history) {
    if (history->current != 0) {
        text_change_destroy_changes_in_future(history->current);
        text_change_destroy_changes_in_past(history->current);
        delete history->current;
        history->current = 0;
    }
}

void text_editor_clamp_cursor(Text_Editor* editor);
void text_history_record_change(Text_History* history, Text_Change change) 
{
    if (history->recording_depth != 0) {
        dynamic_array_push_back(&history->complex_command, change);
        return;
    }

    Text_Change* record = new Text_Change();
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

void text_history_insert_string(Text_History* history, Text_Position pos, String string)
{
    Text_Slice slice = text_calculate_insertion_string_slice(&history->editor->text, pos, string);
    Text_Change change;
    change.next = 0;
    change.previous = 0;
    change.slice = slice;
    change.string = string;
    change.type = Text_Change_Type::STRING_INSERTION;

    change.cursor_pos_before_change = history->editor->cursor_position;
    text_change_apply(&change, history->editor);
    text_history_record_change(history, change);
}

void text_history_delete_slice(Text_History* history, Text_Slice slice)
{
    if (text_position_are_equal(slice.start, slice.end)) return;
    String str = string_create_empty(32);
    text_append_slice_to_string(history->editor->text, slice, &str);
    Text_Change change;
    change.next = 0;
    change.previous = 0;
    change.slice = slice;
    change.string = str;
    change.type = Text_Change_Type::STRING_DELETION;

    change.cursor_pos_before_change = history->editor->cursor_position;
    text_change_apply(&change, history->editor);
    text_editor_clamp_cursor(history->editor);
    text_history_record_change(history, change);
}

void text_history_insert_character(Text_History* history, Text_Position pos, char c) {
    Text_Change change;
    change.next = 0;
    change.previous = 0;
    change.character_position = pos;
    change.character = c;
    change.type = Text_Change_Type::CHARACTER_INSERTION;

    change.cursor_pos_before_change = history->editor->cursor_position;
    text_change_apply(&change, history->editor);
    text_editor_clamp_cursor(history->editor);
    text_history_record_change(history, change);
}

void text_history_delete_character(Text_History* history, Text_Position pos) {
    text_position_sanitize(&pos, history->editor->text);
    Text_Change change;
    change.next = 0;
    change.previous = 0;
    change.character_position = pos;
    change.character = text_get_character_after(&history->editor->text, pos);
    change.type = Text_Change_Type::CHARACTER_DELETION;

    change.cursor_pos_before_change = history->editor->cursor_position;
    text_change_apply(&change, history->editor);
    text_editor_clamp_cursor(history->editor);
    text_history_record_change(history, change);
}

void text_history_start_record_complex_command(Text_History* history) {
    if (history->recording_depth < 0) panic("Error, recording depth is negative!!\n");
    if (history->recording_depth == 0) history->complex_command = dynamic_array_create_empty<Text_Change>(32);
    history->recording_depth++;
    history->complex_command_start_pos = history->editor->cursor_position;
}

void text_history_stop_record_complex_command(Text_History* history) {
    if (history->recording_depth <= 0) panic("Recording stopped with invalid recording depth\n");
    history->recording_depth--;
    if (history->recording_depth == 0)
    {
        Text_Change change;
        change.type = Text_Change_Type::COMPLEX;
        change.sub_changes = history->complex_command;
        change.next = 0;
        change.previous = 0;
        change.cursor_pos_before_change = history->complex_command_start_pos;
        text_history_record_change(history, change);
    }
}

void text_history_undo(Text_History* history, Text_Editor* editor) {
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

void text_history_redo(Text_History* history, Text_Editor* editor)
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

Normal_Mode_Command normal_mode_command_make(Normal_Mode_Command_Type command_type, int repeat_count);
Movement movement_make(Movement_Type movement_type, int repeat_count, char search_char);
Text_Editor* text_editor_create(Text_Renderer* text_renderer, Rendering_Core* core)
{
    Text_Editor* result = new Text_Editor();
    result->text = text_create_empty();

    result->renderer = text_renderer;
    result->text_highlights = dynamic_array_create_empty<Dynamic_Array<Text_Highlight>>(32);
    result->cursor_shader = shader_program_create_from_multiple_sources(core, { "resources/shaders/cursor.frag", "resources/shaders/cursor.vert" });
    result->cursor_mesh = mesh_utils_create_quad_2D(core);
    result->pipeline_state = pipeline_state_make_default();
    result->pipeline_state.depth_state.test_type = Depth_Test_Type::IGNORE_DEPTH;
    result->pipeline_state.blending_state.blending_enabled = true;
    result->pipeline_state.culling_state.culling_enabled = true;
    result->line_size_cm = 0.3f;
    result->first_rendered_line = 0;
    result->first_rendered_char = 0;
    result->line_count_buffer = string_create_empty(16);
    result->last_editor_region = bounding_box_2_make_min_max(vec2(-1, -1), vec2(1, 1));
    result->last_text_height = 0.0f;

    result->history = text_history_create(result);
    result->mode = Text_Editor_Mode::NORMAL;
    result->cursor_position = text_position_make(0, 0);
    result->last_change_position = text_position_make(0, 0);
    result->horizontal_position = 0;
    result->text_changed = true;
    result->last_search_char = ' ';
    result->last_search_was_forwards = true;
    result->last_keymessage_time = 0.0;
    result->jump_history = dynamic_array_create_empty<Text_Editor_Jump>(32);
    result->jump_history_index = 0;

    result->last_normal_mode_command = normal_mode_command_make(Normal_Mode_Command_Type::MOVEMENT, 0);
    result->last_normal_mode_command.movement = movement_make(Movement_Type::MOVE_LEFT, 0, 0);
    result->normal_mode_incomplete_command = dynamic_array_create_empty<Key_Message>(32);
    result->record_insert_mode_inputs = true;
    result->last_insert_mode_inputs = dynamic_array_create_empty<Key_Message>(32);
    result->yanked_string = string_create_empty(64);
    result->last_yank_was_line = false;

    return result;
}

void text_editor_destroy(Text_Editor* editor)
{
    text_history_destroy(&editor->history);
    text_destroy(&editor->text);
    for (int i = 0; i < editor->text_highlights.size; i++) {
        dynamic_array_destroy(&editor->text_highlights.data[i]);
    }
    dynamic_array_destroy(&editor->text_highlights);
    shader_program_destroy(editor->cursor_shader);
    mesh_gpu_buffer_destroy(&editor->cursor_mesh);
    string_destroy(&editor->yanked_string);
    string_destroy(&editor->line_count_buffer);

    dynamic_array_destroy(&editor->normal_mode_incomplete_command);
    dynamic_array_destroy(&editor->last_insert_mode_inputs);
    dynamic_array_destroy(&editor->jump_history);
}

void text_editor_synchronize_highlights_array(Text_Editor* editor)
{
    while (editor->text_highlights.size < editor->text.size) {
        Dynamic_Array<Text_Highlight> line_highlights = dynamic_array_create_empty<Text_Highlight>(32);
        dynamic_array_push_back(&editor->text_highlights, line_highlights);
    }
}

void text_editor_add_highlight(Text_Editor* editor, Text_Highlight highlight, int line_number)
{
    if (line_number >= editor->text.size) {
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

void text_editor_draw_bounding_box(Text_Editor* editor, Rendering_Core* core, BoundingBox2 bb, vec4 color)
{
    shader_program_set_uniform(editor->cursor_shader, core, "position", bb.min);
    shader_program_set_uniform(editor->cursor_shader, core, "size", bb.max - bb.min);
    shader_program_set_uniform(editor->cursor_shader, core, "color", color);
    mesh_gpu_buffer_draw_with_shader_program(&editor->cursor_mesh, editor->cursor_shader, core);
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

void text_editor_add_highlight_from_slice(Text_Editor* editor, Text_Slice slice, vec3 text_color, vec4 background_color);
void text_editor_render(Text_Editor* editor, Rendering_Core* core, int width, int height, int dpi, BoundingBox2 editor_region, double time)
{
    rendering_core_updated_pipeline_state(core, editor->pipeline_state);
    float text_height = 2.0f * (editor->line_size_cm) / (height / (float)dpi * 2.54f);
    editor->last_editor_region = editor_region;
    editor->last_text_height = text_height;

    // Calculate minimum and maximum line in viewport
    int max_line_count = (editor_region.max.y - editor_region.min.y) / text_height;
    if (editor->cursor_position.line < editor->first_rendered_line) {
        editor->first_rendered_line = editor->cursor_position.line;
    }
    int last_line = math_minimum(editor->first_rendered_line + max_line_count - 1, editor->text.size - 1);
    if (editor->cursor_position.line > last_line) {
        last_line = editor->cursor_position.line;
        editor->first_rendered_line = last_line - max_line_count + 1;
    }

    // Draw line numbers (Reduces the editor viewport for the text)
    {
        string_reset(&editor->line_count_buffer);
        string_append_formated(&editor->line_count_buffer, "%d ", editor->text.size);
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
            Text_Layout* layout = text_renderer_calculate_text_layout(editor->renderer, &editor->line_count_buffer, text_height, 1.0f);
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
        String* line = &editor->text[i];
        String truncated_line = string_create_substring_static(line, editor->first_rendered_char, last_char + 1);
        Text_Layout* line_layout = text_renderer_calculate_text_layout(editor->renderer, &truncated_line, text_height, 1.0f);
        for (int j = 0; j < editor->text_highlights.data[i].size; j++)
        {
            Text_Highlight* highlight = &editor->text_highlights.data[i].data[j];
            // Draw text background 
            {
                BoundingBox2 highlight_start = text_editor_get_character_bounding_box(editor,
                    text_height, i, highlight->character_start, editor_region);
                BoundingBox2 highlight_end = text_editor_get_character_bounding_box(editor, text_height, i, highlight->character_end - 1,
                    editor_region);
                BoundingBox2 combined = bounding_box_2_combine(highlight_start, highlight_end);
                text_editor_draw_bounding_box(editor, core, combined, highlight->background_color);
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

    text_renderer_render(editor->renderer, core);

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
        if (editor->mode == Text_Editor_Mode::NORMAL && editor->normal_mode_incomplete_command.size != 0) cursor_height *= 0.5f;
        cursor_bb.max.y = cursor_bb.min.y + cursor_height;

        if (editor->mode == Text_Editor_Mode::INSERT) {
            float pixel_normalized = 2.0f / width;
            float width = math_maximum(pixel_normalized * 3.0f, text_height * 0.04f);
            cursor_bb.max.x = cursor_bb.min.x + width;
        }
        if (show_cursor) {
            text_editor_draw_bounding_box(editor, core, cursor_bb, vec4(0.0f, 1.0f, 0.0f, 1.0f));
        }
    }
}

Text_Highlight text_highlight_make(vec3 text_color, vec4 background_color, int character_start, int character_end) {
    Text_Highlight result;
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
        int end_character = editor->text[line].size;
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

Movement movement_make(Movement_Type movement_type, int repeat_count, char search_char = '\0') {
    Movement result;
    result.type = movement_type;
    result.repeat_count = repeat_count;
    result.search_char = search_char;
    return result;
}

Text_Slice text_slice_make_inside_parenthesis(Dynamic_Array<String>* text, Text_Position pos, char open_parenthesis, char closed_parenthesis)
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

Text_Slice text_slice_make_enclosure(Dynamic_Array<String>* text, Text_Position pos,
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

Text_Slice text_slice_get_current_word_slice(Dynamic_Array<String>* text, Text_Position pos, bool* on_word)
{
    *on_word = false;
    Text_Iterator it = text_iterator_make(text, pos);
    String whitespace_characters = characters_get_string_whitespaces();
    String operator_characters = characters_get_string_non_identifier_non_whitespace();
    String identifier_characters = characters_get_string_valid_identifier_characters();
    if (it.character == '\0') 
        return text_slice_make(pos, pos);
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
Text_Slice motion_evaluate_at_position(Motion motion, Text_Position pos, Text_Editor* editor);
Motion motion_make(Motion_Type motion_type, int repeat_count, bool contains_edges);

Text_Position movement_evaluate_at_position(Movement movement, Text_Position pos, Text_Editor* editor)
{
    String word_characters = characters_get_string_valid_identifier_characters();
    String whitespace_characters = characters_get_string_whitespaces();
    String operator_characters = characters_get_string_non_identifier_non_whitespace();

    bool repeat_movement = true;
    bool set_horizontal_pos = true;
    for (int i = 0; i < movement.repeat_count && repeat_movement; i++)
    {
        Text_Iterator iterator = text_iterator_make(&editor->text, pos);
        Text_Position next_position = text_position_next(iterator.position, editor->text);
        text_position_sanitize(&next_position, editor->text);
        char next_character = text_get_character_after(&editor->text, next_position);
        switch (movement.type)
        {
        case Movement_Type::MOVE_DOWN: {
            pos.line += 1;
            pos.character = editor->horizontal_position;
            set_horizontal_pos = false;
            break;
        }
        case Movement_Type::MOVE_UP: {
            pos.line -= 1;
            pos.character = editor->horizontal_position;
            set_horizontal_pos = false;
            break;
        }
        case Movement_Type::MOVE_LEFT: {
            pos.character -= 1;
            break;
        }
        case Movement_Type::MOVE_RIGHT: {
            pos.character += 1;
            break;
        }
        case Movement_Type::TO_END_OF_LINE: {
            String* line = &editor->text.data[pos.line];
            pos.character = line->size;
            editor->horizontal_position = 10000; // Look at jk movements after $ to understand this
            set_horizontal_pos = false;
            break;
        }
        case Movement_Type::TO_START_OF_LINE: {
            pos.character = 0;
            break;
        }
        case Movement_Type::NEXT_WORD:
        {
            bool currently_on_word;
            Text_Slice current_word = text_slice_get_current_word_slice(&editor->text, pos, &currently_on_word);
            if (currently_on_word) {
                text_iterator_set_position(&iterator, current_word.end);
                text_iterator_skip_characters_in_set(&iterator, whitespace_characters, true);
            }
            text_iterator_skip_characters_in_set(&iterator, whitespace_characters, true);
            pos = iterator.position;
            break;
        }
        case Movement_Type::NEXT_SPACE: {
            text_iterator_skip_characters_in_set(&iterator, whitespace_characters, false); // Skip current non-whitespaces
            text_iterator_skip_characters_in_set(&iterator, whitespace_characters, true); // Skip current whitespaces
            pos = iterator.position;
            break;
        }
        case Movement_Type::END_OF_WORD: {
            bool currently_on_word;
            Text_Slice current_word = text_slice_get_current_word_slice(&editor->text, pos, &currently_on_word);
            if (currently_on_word)
            {
                // Check if we are on end of word
                if (text_position_are_equal(iterator.position, text_position_previous(current_word.end, editor->text))) {
                    text_iterator_advance(&iterator);
                    text_iterator_skip_characters_in_set(&iterator, whitespace_characters, true);
                    current_word = text_slice_get_current_word_slice(&editor->text, iterator.position, &currently_on_word);
                    text_iterator_set_position(&iterator, text_position_previous(current_word.end, editor->text));
                }
                else {
                    text_iterator_set_position(&iterator, text_position_previous(current_word.end, editor->text));
                }
            }
            else {
                text_iterator_skip_characters_in_set(&iterator, whitespace_characters, true);
                current_word = text_slice_get_current_word_slice(&editor->text, iterator.position, &currently_on_word);
                text_iterator_set_position(&iterator, text_position_previous(current_word.end, editor->text));
            }
            pos = iterator.position;
            break;
        }
        case Movement_Type::END_OF_WORD_AFTER_SPACE: {
            Text_Slice current_word = motion_evaluate_at_position(motion_make(Motion_Type::SPACES, 1, false), iterator.position, editor);
            Text_Position result = text_position_previous(current_word.end, editor->text);
            if (text_position_are_equal(result, pos)) { // Currently on end of word, skip one character
                text_iterator_advance(&iterator);
            }
            text_iterator_skip_characters_in_set(&iterator, whitespace_characters, true); // Skip whitespace
            current_word = motion_evaluate_at_position(motion_make(Motion_Type::SPACES, 1, false), iterator.position, editor);
            pos = text_position_previous(current_word.end, editor->text);
            break;
        }
        case Movement_Type::PREVIOUS_SPACE: {
            Text_Slice current_word = motion_evaluate_at_position(motion_make(Motion_Type::SPACES, 1, false), iterator.position, editor);
            Text_Position it = pos;
            if (text_position_are_equal(current_word.start, it)) {
                it = text_position_previous(it, editor->text);
            }
            // iterator move backwards until not on space
            while (string_contains_character(whitespace_characters, text_get_character_after(&editor->text, it)) &&
                !text_position_are_equal(text_position_make_start(), it)) {
                it = text_position_previous(it, editor->text);
            }
            current_word = motion_evaluate_at_position(motion_make(Motion_Type::SPACES, 1, false), it, editor);
            pos = current_word.start;
            break;
        }
        case Movement_Type::PREVIOUS_WORD: {
            Text_Slice current_word = motion_evaluate_at_position(motion_make(Motion_Type::WORD, 1, false), iterator.position, editor);
            Text_Position it = pos;
            if (text_position_are_equal(current_word.start, it)) {
                it = text_position_previous(it, editor->text);
            }
            // iterator move backwards until not on space
            while (string_contains_character(whitespace_characters, text_get_character_after(&editor->text, it)) &&
                !text_position_are_equal(text_position_make_start(), it)) {
                it = text_position_previous(it, editor->text);
            }
            current_word = motion_evaluate_at_position(motion_make(Motion_Type::WORD, 1, false), it, editor);
            pos = current_word.start;
            break;
        }
        case Movement_Type::NEXT_PARAGRAPH: {
            int line = pos.line;
            while (line < editor->text.size && string_contains_only_characters_in_set(&editor->text.data[line], whitespace_characters, false)) {
                line++;
            }
            while (line < editor->text.size && !string_contains_only_characters_in_set(&editor->text.data[line], whitespace_characters, false)) {
                line++;
            }
            pos.line = line;
            pos.character = 0;
            break;
        }
        case Movement_Type::PREVIOUS_PARAGRAPH: {
            int line = pos.line;
            while (line > 0 && string_contains_only_characters_in_set(&editor->text.data[line], whitespace_characters, false)) {
                line--;
            }
            while (line > 0 && !string_contains_only_characters_in_set(&editor->text.data[line], whitespace_characters, false)) {
                line--;
            }
            pos.line = line;
            pos.character = 0;
            break;
        }
        case Movement_Type::JUMP_ENCLOSURE: {
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
            Text_Slice slice = text_slice_make_inside_parenthesis(&editor->text, pos, open_parenthesis, closed_parenthesis);
            if (on_open_side) {
                pos = slice.end;
            }
            else {
                pos = text_position_previous(slice.start, editor->text);
            }
            break;
        }
        case Movement_Type::SEARCH_FORWARDS_FOR: 
        case Movement_Type::SEARCH_FORWARDS_TO:
        {
            if (movement.type == Movement_Type::SEARCH_FORWARDS_FOR) {
                if (iterator.character == movement.search_char) text_iterator_advance(&iterator);
            }
            else
            {
                if (next_character == movement.search_char) {
                    text_iterator_advance(&iterator);
                    text_iterator_advance(&iterator);
                }
            }

            Text_Position max_position;
            {
                int line_count = (editor->last_editor_region.max.y - editor->last_editor_region.min.y) / editor->last_text_height;
                max_position = text_position_make_line_end(&editor->text, editor->first_rendered_line + line_count);
            }
            while (text_iterator_has_next(&iterator) && text_position_are_in_order(&iterator.position, &max_position)) {
                if (iterator.character == movement.search_char) {
                    if (movement.type == Movement_Type::SEARCH_FORWARDS_TO) text_iterator_move_back(&iterator);
                    pos = iterator.position;
                    break;
                }
                text_iterator_advance(&iterator);
            }

            editor->last_search_char = movement.search_char;
            editor->last_search_was_forwards = true;
            break;
        }
        case Movement_Type::SEARCH_BACKWARDS_FOR: {
            if (iterator.character == movement.search_char) text_iterator_move_back(&iterator);
            bool found = text_iterator_goto_next_character(&iterator, movement.search_char, false);
            Text_Position min_pos = text_position_make(editor->first_rendered_line, 0);
            if (found && text_position_are_in_order(&min_pos, &iterator.position)) {
                pos = iterator.position;
            }
            editor->last_search_char = movement.search_char;
            editor->last_search_was_forwards = false;
            break;
        }
        case Movement_Type::SEARCH_BACKWARDS_TO: {
            if (iterator.character == movement.search_char) text_iterator_move_back(&iterator);
            bool found = text_iterator_goto_next_character(&iterator, movement.search_char, false);
            if (found) {
                pos = iterator.position;
            }
            editor->last_search_char = movement.search_char;
            editor->last_search_was_forwards = false;
            break;
        }
        case Movement_Type::REPEAT_LAST_SEARCH: {
            Movement search_movement;
            bool forwards = editor->last_search_was_forwards;
            if (forwards) search_movement.type = Movement_Type::SEARCH_FORWARDS_FOR;
            else search_movement.type = Movement_Type::SEARCH_BACKWARDS_FOR;
            search_movement.search_char = editor->last_search_char;
            search_movement.repeat_count = 1;
            pos = movement_evaluate_at_position(search_movement, pos, editor);
            editor->last_search_was_forwards = forwards;
            break;
        }
        case Movement_Type::REPEAT_LAST_SEARCH_REVERSE_DIRECTION: {
            Movement search_movement;
            bool forwards = editor->last_search_was_forwards;
            if (!forwards) search_movement.type = Movement_Type::SEARCH_FORWARDS_FOR;
            else search_movement.type = Movement_Type::SEARCH_BACKWARDS_FOR;
            search_movement.search_char = editor->last_search_char;
            search_movement.repeat_count = 1;
            pos = movement_evaluate_at_position(search_movement, pos, editor);
            editor->last_search_was_forwards = forwards;
            break;
        }
        case Movement_Type::GOTO_END_OF_TEXT: {
            pos = text_position_make_end(&editor->text);
            repeat_movement = false;
            break;
        }
        case Movement_Type::GOTO_START_OF_TEXT: {
            pos = text_position_make_start();
            repeat_movement = false;
            break;
        }
        case Movement_Type::GOTO_LINE_NUMBER: {
            pos.line = movement.repeat_count;
            repeat_movement = false;
            break;
        }
        default: {
            logg("ERROR: Movement not supported yet!\n");
        }
        }
        text_position_sanitize(&pos, editor->text);
        if (set_horizontal_pos) editor->horizontal_position = pos.character;
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

Motion motion_make(Motion_Type motion_type, int repeat_count, bool contains_edges) {
    Motion result;
    result.motion_type = motion_type;
    result.repeat_count = repeat_count;
    result.contains_edges = contains_edges;
    return result;
}

Motion motion_make_from_movement(Movement movement) {
    Motion result;
    result.motion_type = Motion_Type::MOVEMENT;
    result.movement = movement;
    result.repeat_count = 1;
    result.contains_edges = false;
    return result;
}

Text_Slice motion_evaluate_at_position(Motion motion, Text_Position pos, Text_Editor* editor)
{
    Text_Slice result;
    switch (motion.motion_type)
    {
    case Motion_Type::MOVEMENT: {
        Text_Position end_pos = movement_evaluate_at_position(motion.movement, pos, editor);
        if (!text_position_are_in_order(&pos, &end_pos)) {
            text_position_next(pos, editor->text);
        }
        result = text_slice_make(pos, end_pos);
        text_slice_sanitize(&result, editor->text);
        break;
    }
    case Motion_Type::WORD: {
        bool unused;
        result = text_slice_get_current_word_slice(&editor->text, pos, &unused);
        break;
    }
    case Motion_Type::SPACES: {
        String spaces = string_create_static(" \n\t");
        if (!string_contains_character(spaces, text_get_character_after(&editor->text, pos))) {
            result = text_slice_make_enclosure(&editor->text, pos, spaces, false, spaces, false);
        }
        else {
            result = text_slice_make(pos, pos);
        }
        break;
    }
    case Motion_Type::BRACES: {
        result = text_slice_make_inside_parenthesis(&editor->text, pos, '{', '}');
        break;
    }
    case Motion_Type::BRACKETS: {
        result = text_slice_make_inside_parenthesis(&editor->text, pos, '[', ']');
        break;
    }
    case Motion_Type::PARENTHESES: {
        result = text_slice_make_inside_parenthesis(&editor->text, pos, '(', ')');
        break;
    }
    case Motion_Type::QUOTATION_MARKS: {
        result = text_slice_make_enclosure(&editor->text, pos, string_create_static("\""), false, string_create_static("\""), false);
        break;
    }
    case Motion_Type::PARAGRAPH: {
        int paragraph_start = pos.line;
        int paragraph_end = pos.line;
        while (paragraph_start > 0) {
            String* line = &editor->text.data[paragraph_start];
            if (string_contains_only_characters_in_set(line, string_create_static(" \t"), false)) break;
            paragraph_start--;
        }
        while (paragraph_end < editor->text.size) {
            String* line = &editor->text.data[paragraph_end];
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
        result.start = text_position_previous(result.start, editor->text);
        result.end = text_position_next(result.end, editor->text);
    }

    return result;
}

Normal_Mode_Command normal_mode_command_make(Normal_Mode_Command_Type command_type, int repeat_count) {
    Normal_Mode_Command result;
    result.type = command_type;
    result.repeat_count = repeat_count;
    return result;
}

Normal_Mode_Command normal_mode_command_make_with_char(Normal_Mode_Command_Type command_type, int repeat_count, char character) {
    Normal_Mode_Command result;
    result.type = command_type;
    result.repeat_count = repeat_count;
    result.character = character;
    return result;
}

Normal_Mode_Command normal_mode_command_make_with_motion(Normal_Mode_Command_Type command_type, int repeat_count, Motion motion) {
    Normal_Mode_Command result;
    result.type = command_type;
    result.repeat_count = repeat_count;
    result.motion = motion;
    return result;
}

Normal_Mode_Command normal_mode_command_make_movement(Movement movement) {
    Normal_Mode_Command result;
    result.type = Normal_Mode_Command_Type::MOVEMENT;
    result.repeat_count = 1;
    result.movement = movement;
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
    Parse_Result_Type symbol_type;
    int key_message_count; // How much messages were consumed from the array
    T result;
};

template<typename T>
Parse_Result<T> parse_result_make_success(T t, int key_message_count) {
    Parse_Result<T> result;
    result.symbol_type = Parse_Result_Type::SUCCESS;
    result.key_message_count = key_message_count;
    result.result = t;
    return result;
}

template<typename T>
Parse_Result<T> parse_result_make_failure() {
    Parse_Result<T> result;
    result.symbol_type = Parse_Result_Type::FAILURE;
    return result;
}

template<typename T>
Parse_Result<T> parse_result_make_completable() {
    Parse_Result<T> result;
    result.symbol_type = Parse_Result_Type::COMPLETABLE;
    return result;
}

template<typename T, typename K>
Parse_Result<T> parse_result_propagate_non_success(Parse_Result<K> prev_result) {
    Parse_Result<T> result;
    result.symbol_type = prev_result.symbol_type;
    return result;
}

Parse_Result<int> key_messages_parse_repeat_count(Array<Key_Message> messages)
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

Parse_Result<Movement> key_messages_parse_movement(Array<Key_Message> messages, Parse_Result<int> repeat_count)
{
    if (messages.size == 0) return parse_result_make_completable<Movement>();

    // Check for 1 character movements
    {
        Key_Message msg = messages[0];
        if (msg.character == 'h') {
            return parse_result_make_success(movement_make(Movement_Type::MOVE_LEFT, repeat_count.result), 1);
        }
        else if (msg.character == 'l') {
            return parse_result_make_success(movement_make(Movement_Type::MOVE_RIGHT, repeat_count.result), 1);
        }
        else if (msg.character == 'j') {
            return parse_result_make_success(movement_make(Movement_Type::MOVE_DOWN, repeat_count.result), 1);
        }
        else if (msg.character == 'k') {
            return parse_result_make_success(movement_make(Movement_Type::MOVE_UP, repeat_count.result), 1);
        }
        else if (msg.character == '$') {
            return parse_result_make_success(movement_make(Movement_Type::TO_END_OF_LINE, repeat_count.result), 1);
        }
        else if (msg.character == '0') {
            return parse_result_make_success(movement_make(Movement_Type::TO_START_OF_LINE, repeat_count.result), 1);
        }
        else if (msg.character == 'w') {
            return parse_result_make_success(movement_make(Movement_Type::NEXT_WORD, repeat_count.result), 1);
        }
        else if (msg.character == 'W') {
            return parse_result_make_success(movement_make(Movement_Type::NEXT_SPACE, repeat_count.result), 1);
        }
        else if (msg.character == 'b') {
            return parse_result_make_success(movement_make(Movement_Type::PREVIOUS_WORD, repeat_count.result), 1);
        }
        else if (msg.character == 'B') {
            return parse_result_make_success(movement_make(Movement_Type::PREVIOUS_SPACE, repeat_count.result), 1);
        }
        else if (msg.character == 'e') {
            return parse_result_make_success(movement_make(Movement_Type::END_OF_WORD, repeat_count.result), 1);
        }
        else if (msg.character == 'E') {
            return parse_result_make_success(movement_make(Movement_Type::END_OF_WORD_AFTER_SPACE, repeat_count.result), 1);
        }
        else if (msg.character == '%') {
            return parse_result_make_success(movement_make(Movement_Type::JUMP_ENCLOSURE, repeat_count.result), 1);
        }
        else if (msg.character == ';') {
            return parse_result_make_success(movement_make(Movement_Type::REPEAT_LAST_SEARCH, repeat_count.result), 1);
        }
        else if (msg.character == ',') {
            return parse_result_make_success(movement_make(Movement_Type::REPEAT_LAST_SEARCH_REVERSE_DIRECTION, repeat_count.result), 1);
        }
        else if (msg.character == '}') {
            return parse_result_make_success(movement_make(Movement_Type::NEXT_PARAGRAPH, repeat_count.result), 1);
        }
        else if (msg.character == '{') {
            return parse_result_make_success(movement_make(Movement_Type::PREVIOUS_PARAGRAPH, repeat_count.result), 1);
        }
        else if (msg.character == 'G') {
            if (repeat_count.result > 1) {
                return parse_result_make_success(movement_make(Movement_Type::GOTO_LINE_NUMBER, repeat_count.result), 1);
            }
            else {
                return parse_result_make_success(movement_make(Movement_Type::GOTO_END_OF_TEXT, repeat_count.result), 1);
            }
        }
        else if (msg.character == 'g') {
            if (repeat_count.key_message_count != 0) {
                return parse_result_make_success(movement_make(Movement_Type::GOTO_LINE_NUMBER, repeat_count.result), 1);
            }
            if (messages.size == 1) {
                return parse_result_make_completable<Movement>();
            }
            if (messages.size > 1) {
                if (messages[1].character == 'g') {
                    return parse_result_make_success(movement_make(Movement_Type::GOTO_START_OF_TEXT, repeat_count.result), 2);
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
            return parse_result_make_success(movement_make(Movement_Type::SEARCH_FORWARDS_FOR, repeat_count.result, messages[1].character), 2);
        }
        else if (messages[0].character == 'F') {
            return parse_result_make_success(movement_make(Movement_Type::SEARCH_BACKWARDS_FOR, repeat_count.result, messages[1].character), 2);
        }
        else if (messages[0].character == 't') {
            return parse_result_make_success(movement_make(Movement_Type::SEARCH_FORWARDS_TO, repeat_count.result, messages[1].character), 2);
        }
        else if (messages[0].character == 'T') {
            return parse_result_make_success(movement_make(Movement_Type::SEARCH_BACKWARDS_TO, repeat_count.result, messages[1].character), 2);
        }
    }

    return parse_result_make_failure<Movement>();
}

Parse_Result<Motion> key_messages_parse_motion(Array<Key_Message> messages)
{
    Parse_Result<int> repeat_count_parse = key_messages_parse_repeat_count(messages);
    messages = array_make_slice(&messages, repeat_count_parse.key_message_count, messages.size);
    if (messages.size == 0) return parse_result_make_completable<Motion>();

    // Motions may also be movements, so we check if we can parse a movement first
    Parse_Result<Movement> movement_parse = key_messages_parse_movement(messages, repeat_count_parse);
    if (movement_parse.symbol_type == Parse_Result_Type::SUCCESS) {
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
        return parse_result_make_success<Motion>(motion_make(Motion_Type::WORD, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case 'W':
        return parse_result_make_success<Motion>(motion_make(Motion_Type::SPACES, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case '(':
        return parse_result_make_success<Motion>(motion_make(Motion_Type::PARENTHESES, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case ')':
        return parse_result_make_success<Motion>(motion_make(Motion_Type::PARENTHESES, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case '[':
        return parse_result_make_success<Motion>(motion_make(Motion_Type::BRACKETS, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case ']':
        return parse_result_make_success<Motion>(motion_make(Motion_Type::BRACKETS, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case '{':
        return parse_result_make_success<Motion>(motion_make(Motion_Type::BRACES, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case '}':
        return parse_result_make_success<Motion>(motion_make(Motion_Type::BRACES, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case '"':
        return parse_result_make_success<Motion>(motion_make(Motion_Type::QUOTATION_MARKS, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case 'p':
        return parse_result_make_success<Motion>(motion_make(Motion_Type::PARAGRAPH, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    case 'P':
        return parse_result_make_success<Motion>(motion_make(Motion_Type::PARAGRAPH, repeat_count_parse.result, contains_edges), 2 + repeat_count_parse.key_message_count);
    }

    return parse_result_propagate_non_success<Motion>(movement_parse);
}

Parse_Result<Normal_Mode_Command> key_messages_parse_normal_mode_command(Array<Key_Message> messages)
{
    Parse_Result<int> repeat_count = key_messages_parse_repeat_count(messages);
    if (repeat_count.symbol_type != Parse_Result_Type::SUCCESS) {
        return parse_result_propagate_non_success<Normal_Mode_Command>(repeat_count);
    }

    messages = array_make_slice(&messages, repeat_count.key_message_count, messages.size);
    if (messages.size == 0) return parse_result_make_completable<Normal_Mode_Command>();

    // Check if it is a movement
    Parse_Result<Movement> movement_parse = key_messages_parse_movement(messages, repeat_count);
    if (movement_parse.symbol_type == Parse_Result_Type::SUCCESS) {
        return parse_result_make_success(normal_mode_command_make_movement(movement_parse.result),
            repeat_count.key_message_count + movement_parse.key_message_count);
    }
    else if (movement_parse.symbol_type == Parse_Result_Type::COMPLETABLE) {
        return parse_result_make_completable<Normal_Mode_Command>();
    }

    // Check 1 character commands
    switch (messages[0].character)
    {
    case '=':
        return parse_result_make_success(
            normal_mode_command_make(Normal_Mode_Command_Type::FORMAT_TEXT, 1), 1 + repeat_count.key_message_count);
    case 'x':
        return parse_result_make_success(
            normal_mode_command_make(Normal_Mode_Command_Type::DELETE_CHARACTER, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'i':
        return parse_result_make_success(
            normal_mode_command_make(Normal_Mode_Command_Type::ENTER_INSERT_MODE_ON_CURSOR, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'I':
        return parse_result_make_success(
            normal_mode_command_make(Normal_Mode_Command_Type::ENTER_INSERT_MODE_LINE_START, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'a':
        return parse_result_make_success(
            normal_mode_command_make(Normal_Mode_Command_Type::ENTER_INSERT_MODE_AFTER_CURSOR, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'A':
        return parse_result_make_success(
            normal_mode_command_make(Normal_Mode_Command_Type::ENTER_INSERT_MODE_LINE_END, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'o':
        return parse_result_make_success(
            normal_mode_command_make(Normal_Mode_Command_Type::ENTER_INSERT_MODE_NEW_LINE_BELOW, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'O':
        return parse_result_make_success(
            normal_mode_command_make(Normal_Mode_Command_Type::ENTER_INSERT_MODE_NEW_LINE_ABOVE, repeat_count.result), 1 + repeat_count.key_message_count);
    case '.':
        return parse_result_make_success(
            normal_mode_command_make(Normal_Mode_Command_Type::REPEAT_LAST_COMMAND, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'D':
        return parse_result_make_success(
            normal_mode_command_make_with_motion(
                Normal_Mode_Command_Type::DELETE_MOTION, repeat_count.result, motion_make_from_movement(movement_make(Movement_Type::TO_END_OF_LINE, 1))
            ),
            1 + repeat_count.key_message_count);
    case 'C':
        return parse_result_make_success(
            normal_mode_command_make_with_motion(
                Normal_Mode_Command_Type::CHANGE_MOTION, repeat_count.result, motion_make_from_movement(movement_make(Movement_Type::TO_END_OF_LINE, 1))
            ),
            1 + repeat_count.key_message_count);
    case 'L':
        return parse_result_make_success(
            normal_mode_command_make(Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_BOTTOM, repeat_count.result),
            repeat_count.key_message_count + 1);
    case 'M':
        return parse_result_make_success(
            normal_mode_command_make(Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_CENTER, repeat_count.result),
            repeat_count.key_message_count + 1);
    case 'H':
        return parse_result_make_success(
            normal_mode_command_make(Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_TOP, repeat_count.result),
            repeat_count.key_message_count + 1);
    case 'p':
        return parse_result_make_success(normal_mode_command_make(Normal_Mode_Command_Type::PUT_AFTER_CURSOR, repeat_count.result),
            1 + repeat_count.key_message_count);
    case 'P':
        return parse_result_make_success(normal_mode_command_make(Normal_Mode_Command_Type::PUT_BEFORE_CURSOR, repeat_count.result),
            1 + repeat_count.key_message_count);
    case 'Y':
        return parse_result_make_success(normal_mode_command_make(Normal_Mode_Command_Type::YANK_LINE, repeat_count.result),
            1 + repeat_count.key_message_count);
    case 'u':
        return parse_result_make_success(normal_mode_command_make(Normal_Mode_Command_Type::UNDO, repeat_count.result), 1 + repeat_count.key_message_count);
    case 'd':
    case 'r':
    case 'c':
    case 'v':
    case 'y':
    case 'z':
        if (messages.size == 1) return parse_result_make_completable<Normal_Mode_Command>();
    }
    if (messages[0].ctrl_down && messages[0].key_down)
    {
        if (messages[0].key_code == KEY_CODE::R) {
            return parse_result_make_success(normal_mode_command_make(Normal_Mode_Command_Type::REDO, repeat_count.result), 1 + repeat_count.key_message_count);
        }
        else if (messages[0].key_code == KEY_CODE::U) {
            return parse_result_make_success(
                normal_mode_command_make(Normal_Mode_Command_Type::SCROLL_UPWARDS_HALF_PAGE, 1), 1 + repeat_count.key_message_count);
        }
        else if (messages[0].key_code == KEY_CODE::D) {
            return parse_result_make_success(
                normal_mode_command_make(Normal_Mode_Command_Type::SCROLL_DOWNWARDS_HALF_PAGE, 1), 1 + repeat_count.key_message_count);
        }
        else if (messages[0].key_code == KEY_CODE::O) {
            return parse_result_make_success(
                normal_mode_command_make(Normal_Mode_Command_Type::GOTO_LAST_JUMP, 1), 1 + repeat_count.key_message_count);
        }
        else if (messages[0].key_code == KEY_CODE::I) {
            return parse_result_make_success(
                normal_mode_command_make(Normal_Mode_Command_Type::GOTO_NEXT_JUMP, 1), 1 + repeat_count.key_message_count);
        }
    }

    if (messages.size == 1) return parse_result_make_failure<Normal_Mode_Command>(); // No 1 size command detected

    // Parse multi key normal mode commands (d and c for now)
    if (messages[0].character == 'y' && messages[1].character == 'y') {
        return parse_result_make_success(normal_mode_command_make(Normal_Mode_Command_Type::YANK_LINE, repeat_count.result),
            1 + repeat_count.key_message_count);
    }
    if (messages[0].character == 'd') {
        Parse_Result<Motion> motion_parse = key_messages_parse_motion(array_make_slice(&messages, 1, messages.size));
        if (motion_parse.symbol_type == Parse_Result_Type::SUCCESS) {
            return parse_result_make_success(
                normal_mode_command_make_with_motion(Normal_Mode_Command_Type::DELETE_MOTION, repeat_count.result, motion_parse.result),
                repeat_count.key_message_count + 1 + motion_parse.key_message_count
            );
        }
        if (messages[1].character == 'd') {
            return parse_result_make_success(
                normal_mode_command_make(Normal_Mode_Command_Type::DELETE_LINE, repeat_count.result),
                repeat_count.key_message_count + 2
            );
        }
        return parse_result_propagate_non_success<Normal_Mode_Command>(motion_parse);
    }
    if (messages[0].character == 'y') {
        Parse_Result<Motion> motion_parse = key_messages_parse_motion(array_make_slice(&messages, 1, messages.size));
        if (motion_parse.symbol_type == Parse_Result_Type::SUCCESS) {
            return parse_result_make_success(
                normal_mode_command_make_with_motion(Normal_Mode_Command_Type::YANK_MOTION, repeat_count.result, motion_parse.result),
                repeat_count.key_message_count + 1 + motion_parse.key_message_count
            );
        }
        return parse_result_propagate_non_success<Normal_Mode_Command>(motion_parse);
    }
    if (messages[0].character == 'c') {
        Parse_Result<Motion> motion_parse = key_messages_parse_motion(array_make_slice(&messages, 1, messages.size));
        if (motion_parse.symbol_type == Parse_Result_Type::SUCCESS) {
            return parse_result_make_success(
                normal_mode_command_make_with_motion(Normal_Mode_Command_Type::CHANGE_MOTION, repeat_count.result, motion_parse.result),
                repeat_count.key_message_count + 1 + motion_parse.key_message_count
            );
        }
        if (messages[1].character == 'c') {
            return parse_result_make_success(
                normal_mode_command_make(Normal_Mode_Command_Type::CHANGE_LINE, repeat_count.result),
                repeat_count.key_message_count + 2
            );
        }
        return parse_result_propagate_non_success<Normal_Mode_Command>(motion_parse);
    }
    if (messages[0].character == 'r') {
        return parse_result_make_success(
            normal_mode_command_make_with_char(Normal_Mode_Command_Type::REPLACE_CHARACTER, repeat_count.result, messages[1].character),
            repeat_count.key_message_count + 2
        );
    }
    if (messages[0].character == 'v') {
        Parse_Result<Motion> motion_parse = key_messages_parse_motion(array_make_slice(&messages, 1, messages.size));
        if (motion_parse.symbol_type == Parse_Result_Type::SUCCESS) {
            return parse_result_make_success(
                normal_mode_command_make_with_motion(Normal_Mode_Command_Type::VISUALIZE_MOTION, repeat_count.result, motion_parse.result),
                repeat_count.key_message_count + 1 + motion_parse.key_message_count
            );
        }
        return parse_result_propagate_non_success<Normal_Mode_Command>(motion_parse);
    }
    if (messages[0].character == 'z') {
        if (messages[1].character == 't') {
            return parse_result_make_success(normal_mode_command_make(Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_TOP, repeat_count.result),
                repeat_count.key_message_count + 2);
        }
        if (messages[1].character == 'z') {
            return parse_result_make_success(normal_mode_command_make(Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_CENTER, repeat_count.result),
                repeat_count.key_message_count + 2);
        }
        if (messages[1].character == 'b') {
            return parse_result_make_success(normal_mode_command_make(Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_BOTTOM, repeat_count.result),
                repeat_count.key_message_count + 2);
        }
        return parse_result_make_failure<Normal_Mode_Command>();
    }
    return parse_result_make_failure<Normal_Mode_Command>();
}

void text_editor_clamp_cursor(Text_Editor* editor)
{
    text_position_sanitize(&editor->cursor_position, editor->text);
    String* line = &editor->text[editor->cursor_position.line];
    if (line->size != 0 && editor->mode == Text_Editor_Mode::NORMAL) {
        editor->cursor_position.character = math_clamp(editor->cursor_position.character, 0, line->size - 1);
    }
}

void insert_mode_enter(Text_Editor* editor) {
    editor->mode = Text_Editor_Mode::INSERT;
    text_editor_clamp_cursor(editor);
    text_history_start_record_complex_command(&editor->history);
    if (editor->record_insert_mode_inputs) dynamic_array_reset(&editor->last_insert_mode_inputs);
}

void insert_mode_exit(Text_Editor* editor) {
    editor->mode = Text_Editor_Mode::NORMAL;
    if (editor->cursor_position.character != 0) {
        editor->cursor_position.character--;
    }
    text_editor_clamp_cursor(editor);
    text_history_stop_record_complex_command(&editor->history);
    editor->horizontal_position = editor->cursor_position.character;
}

int text_editor_find_line_indentation(Text_Editor* editor, int line_number, bool count_parenthesis)
{
    // If the selected line is empty (Only contains spaces, we will have to go up the lines until we find an non-empty line upwards)
    bool last_character_was_open_parenthesis = false;
    {
        String* line = &editor->text[line_number];
        while (line_number >= 0 && string_contains_only_characters_in_set(line, string_create_static(" "), false)) {
            line_number--;
            if (line_number == -1) return 0;
            line = &editor->text[line_number];
        }

        int char_pos = line->size - 1;
        bool found = false;
        char found_char = ' ';
        while (char_pos >= 0)
        {
            char c = line->characters[char_pos];
            if (c != ' ') {
                found = true;
                found_char = c;
                break;
            }
            char_pos -= 1;
        }

        if (found) {
            if (string_contains_character(string_create_static("([{"), found_char)) {
                last_character_was_open_parenthesis = true;
            }
        }
    }
    Text_Position start_pos = text_position_make(line_number, 0);
    Text_Iterator it = text_iterator_make(&editor->text, start_pos);
    text_iterator_skip_characters_in_set(&it, string_create_static(" "), true);
    if (it.position.line != line_number) {
        panic("I dont think this can happen, text must be wrong!");
        return 0;
    }
    int indentation = it.position.character;
    if (last_character_was_open_parenthesis && count_parenthesis) indentation += 4;
    return indentation;
}

void text_editor_set_line_indentation(Text_Editor* editor, int line_number, int indentation)
{
    if (line_number < 0 || line_number >= editor->text.size || indentation < 0) return;
    String* line = &editor->text[line_number];
    int current_line_indentation = 0;
    for (int i = 0; i < line->size; i++) {
        if (line->characters[i] == ' ') {
            current_line_indentation = i + 1;
        }
        else break;
    }

    text_history_start_record_complex_command(&editor->history);
    SCOPE_EXIT(text_history_stop_record_complex_command(&editor->history));
    if (current_line_indentation < indentation)
    {
        int diff = indentation - current_line_indentation;
        for (int i = 0; i < diff; i++) {
            text_history_insert_character(&editor->history, text_position_make(line_number, 0), ' ');
        }
        if (editor->cursor_position.line == line_number) {
            editor->cursor_position.character += diff;
        }
        text_editor_clamp_cursor(editor);
    }
    else if (current_line_indentation > indentation)
    {
        int diff = current_line_indentation - indentation;
        if (editor->cursor_position.line == line_number) {
            editor->cursor_position.character -= diff;
            text_editor_clamp_cursor(editor);
        }
        for (int i = 0; i < diff; i++) {
            char c = line->characters[0];
            if (c != ' ') panic("Should not happen");
            text_history_delete_character(&editor->history, text_position_make(line_number, 0));
        }
    }
}

void text_editor_record_jump(Text_Editor* editor, Text_Position start, Text_Position end)
{
    if (editor->jump_history_index == editor->jump_history.size) {
        editor->jump_history_index++;
        Text_Editor_Jump jump;
        jump.jump_start = start;
        jump.jump_end = end;
        dynamic_array_push_back(&editor->jump_history, jump);
    }
    else {
        editor->jump_history[editor->jump_history_index].jump_start = start;
        editor->jump_history[editor->jump_history_index].jump_end = end;
        editor->jump_history_index++;
        dynamic_array_rollback_to_size(&editor->jump_history, editor->jump_history_index);
    }
}

void insert_mode_handle_message(Text_Editor* editor, Key_Message* msg);
void normal_mode_command_execute(Normal_Mode_Command command, Text_Editor* editor)
{
    bool save_as_last_command = false;
    switch (command.type)
    {
    case Normal_Mode_Command_Type::CHANGE_LINE: {
        text_history_start_record_complex_command(&editor->history);
        text_history_delete_slice(&editor->history, text_slice_make_line(editor->text, editor->cursor_position.line));
        insert_mode_enter(editor);
        text_history_stop_record_complex_command(&editor->history);
        editor->cursor_position.character = 0;
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::CHANGE_MOTION: {
        Text_Slice slice = motion_evaluate_at_position(command.motion, editor->cursor_position, editor);
        if (command.motion.motion_type == Motion_Type::MOVEMENT &&
            (command.motion.movement.type == Movement_Type::SEARCH_FORWARDS_FOR ||
                command.motion.movement.type == Movement_Type::SEARCH_FORWARDS_TO)) {
            slice.end = text_position_next(slice.end, editor->text);
        }
        text_history_start_record_complex_command(&editor->history);
        text_history_delete_slice(&editor->history, slice);
        insert_mode_enter(editor);
        text_history_stop_record_complex_command(&editor->history);
        editor->cursor_position = slice.start;
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::DELETE_CHARACTER: {
        Text_Position next = editor->cursor_position;
        for (int i = 0; i < command.repeat_count; i++) {
            if (editor->text[editor->cursor_position.line].size != 0) {
                text_history_delete_character(&editor->history, editor->cursor_position);
                text_editor_clamp_cursor(editor);
            }
        }
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::DELETE_LINE:
    {
        if (editor->text.size == 0) break;
        Text_Position delete_start = editor->cursor_position;
        delete_start.character = 0;
        Text_Position delete_end = editor->cursor_position;
        delete_end.character = 0;
        for (int i = 0; i < command.repeat_count; i++) {
            delete_end.line++;
        }
        bool delete_last_line = delete_end.line >= editor->text.size;
        text_position_sanitize(&delete_end, editor->text);

        string_reset(&editor->yanked_string);
        if (delete_last_line) {
            delete_end = text_position_make_end(&editor->text);
            Text_Slice line_slice = text_slice_make(delete_start, delete_end);
            delete_start = text_position_previous(delete_start, editor->text);
            text_append_slice_to_string(editor->text, line_slice, &editor->yanked_string);
            string_append_character(&editor->yanked_string, '\n');
        }
        Text_Slice slice = text_slice_make(delete_start, delete_end);
        if (!delete_last_line) {
            text_append_slice_to_string(editor->text, slice, &editor->yanked_string);
        }
        editor->last_yank_was_line = true;
        text_history_delete_slice(&editor->history, slice);
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::DELETE_MOTION:
    {
        Text_Slice deletion_slice;
        if (command.motion.motion_type == Motion_Type::MOVEMENT &&
            (command.motion.movement.type == Movement_Type::MOVE_UP || command.motion.movement.type == Movement_Type::MOVE_DOWN))
        {
            // Handle this as an line delete
            int line_start = editor->cursor_position.line;
            int line_end = editor->cursor_position.line;
            if (command.motion.movement.type == Movement_Type::MOVE_UP) {
                line_start -= command.repeat_count * command.motion.repeat_count * command.motion.movement.repeat_count;
            }
            else {
                line_end += command.repeat_count * command.motion.repeat_count * command.motion.movement.repeat_count;
            }
            Text_Position start = text_position_make(line_start, 0);
            Text_Position end = text_position_make(line_end + 1, 0);
            if (end.line >= editor->text.size) {
                end = text_position_make_end(&editor->text);
                text_position_sanitize(&start, editor->text);
                start = text_position_previous(start, editor->text);
            }
            editor->last_yank_was_line = true;
            deletion_slice = text_slice_make(start, end);
        }
        else
        {
            deletion_slice = motion_evaluate_at_position(command.motion, editor->cursor_position, editor);
            if (command.motion.motion_type == Motion_Type::MOVEMENT &&
                (command.motion.movement.type == Movement_Type::SEARCH_FORWARDS_FOR ||
                    command.motion.movement.type == Movement_Type::SEARCH_FORWARDS_TO)) {
                deletion_slice.end = text_position_next(deletion_slice.end, editor->text);
            }
            editor->last_yank_was_line = false;
        }

        string_reset(&editor->yanked_string);
        text_append_slice_to_string(editor->text, deletion_slice, &editor->yanked_string);
        text_history_delete_slice(&editor->history, deletion_slice);
        editor->cursor_position = deletion_slice.start;
        text_editor_clamp_cursor(editor);
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::ENTER_INSERT_MODE_AFTER_CURSOR: {
        editor->cursor_position.character++;
        insert_mode_enter(editor);
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::ENTER_INSERT_MODE_ON_CURSOR: {
        insert_mode_enter(editor);
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::ENTER_INSERT_MODE_LINE_END: {
        editor->cursor_position.character = editor->text[editor->cursor_position.line].size;
        insert_mode_enter(editor);
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::ENTER_INSERT_MODE_LINE_START: {
        editor->cursor_position.character = 0;
        Text_Iterator it = text_iterator_make(&editor->text, editor->cursor_position);
        text_iterator_skip_characters_in_set(&it, string_create_static(" \t"), true);
        editor->cursor_position = it.position;
        insert_mode_enter(editor);
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::ENTER_INSERT_MODE_NEW_LINE_ABOVE: {
        Text_Position new_pos;
        new_pos.line = editor->cursor_position.line;
        new_pos.character = 0;
        int indentation = text_editor_find_line_indentation(editor, math_maximum(0, new_pos.line - 1), true);
        text_history_start_record_complex_command(&editor->history);
        text_history_insert_character(&editor->history, new_pos, '\n');
        for (int i = 0; i < indentation; i++) {
            text_history_insert_character(&editor->history, new_pos, ' ');
            new_pos.character += 1;
        }
        editor->cursor_position = new_pos;
        insert_mode_enter(editor);
        text_history_stop_record_complex_command(&editor->history); // Works because recording is an int that counts up/down
        editor->text_changed = true;
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::ENTER_INSERT_MODE_NEW_LINE_BELOW: {
        Text_Position new_pos;
        new_pos.line = editor->cursor_position.line;
        new_pos.character = editor->text[new_pos.line].size;
        int indentation = text_editor_find_line_indentation(editor, new_pos.line, true);
        text_history_start_record_complex_command(&editor->history);
        text_history_insert_character(&editor->history, new_pos, '\n');
        new_pos.line += 1;
        new_pos.character = 0;
        for (int i = 0; i < indentation; i++) {
            text_history_insert_character(&editor->history, new_pos, ' ');
            new_pos.character += 1;
        }
        editor->cursor_position = new_pos;
        insert_mode_enter(editor);
        text_history_stop_record_complex_command(&editor->history); // Works because recording is an int that counts up/down
        editor->text_changed = true;
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::FORMAT_TEXT:
    {
        // Start at the start of the text, go through every single character, check
        int depth = 0;
        text_history_start_record_complex_command(&editor->history);
        SCOPE_EXIT(text_history_stop_record_complex_command(&editor->history));
        for (int line_index = 0; line_index < editor->text.size; line_index++)
        {
            String* line = &editor->text[line_index];
            if (string_contains_only_characters_in_set(line, string_create_static(" "), false)) continue;
            int depth_diff_after_line = 0;
            bool use_depth_minus_one = false;
            {
                bool first_non_space_char = true;
                for (int i = 0; i < line->size; i++) {
                    if (string_contains_character(string_create_static("{(["), line->characters[i])) {
                        depth_diff_after_line++;
                    }
                    if (string_contains_character(string_create_static("})]"), line->characters[i])) {
                        if (first_non_space_char) {
                            use_depth_minus_one = true;
                        }
                        depth_diff_after_line--;
                    }
                    if (line->characters[i] != ' ') first_non_space_char = false;
                }
            }

            int expected_depth = use_depth_minus_one ? (depth - 1) * 4 : depth * 4;
            expected_depth = math_maximum(0, expected_depth);
            text_editor_set_line_indentation(editor, line_index, expected_depth);
            depth = math_maximum(0, depth + depth_diff_after_line);
        }
        break;
    }
    case Normal_Mode_Command_Type::MOVEMENT: {
        for (int i = 0; i < command.repeat_count; i++) {
            editor->cursor_position = movement_evaluate_at_position(command.movement, editor->cursor_position, editor);
            text_editor_clamp_cursor(editor);
        }
        break;
    }
    case Normal_Mode_Command_Type::VISUALIZE_MOTION: {
        Text_Slice slice = motion_evaluate_at_position(command.motion, editor->cursor_position, editor);
        text_editor_reset_highlights(editor);
        text_editor_add_highlight_from_slice(editor, slice, vec3(1.0f), vec4(0.0f, 0.3f, 0.0f, 1.0f));
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::REPLACE_CHARACTER: {
        String* line = &editor->text[editor->cursor_position.line];
        if (line->size == 0) {
            text_history_insert_character(&editor->history, editor->cursor_position, command.character);
            break;
        }
        text_history_start_record_complex_command(&editor->history);
        bool forward = editor->cursor_position.character == line->size - 1;
        text_history_delete_character(&editor->history, editor->cursor_position);
        if (forward) {
            Text_Position next = text_position_next(editor->cursor_position, editor->text);
            text_history_insert_character(&editor->history, next, command.character);
            editor->cursor_position = next;
        }
        else {
            text_history_insert_character(&editor->history, editor->cursor_position, command.character);
        }
        text_history_stop_record_complex_command(&editor->history);
        save_as_last_command = true;
        break;
    }
    case Normal_Mode_Command_Type::REPEAT_LAST_COMMAND: {
        editor->record_insert_mode_inputs = false;
        normal_mode_command_execute(editor->last_normal_mode_command, editor);
        if (editor->mode == Text_Editor_Mode::INSERT) {
            for (int i = 0; i < editor->last_insert_mode_inputs.size; i++) {
                insert_mode_handle_message(editor, &editor->last_insert_mode_inputs[i]);
            }
        }
        editor->record_insert_mode_inputs = true;
        break;
    }
    case Normal_Mode_Command_Type::UNDO: {
        text_history_undo(&editor->history, editor);
        text_editor_clamp_cursor(editor);
        break;
    }
    case Normal_Mode_Command_Type::REDO: {
        text_history_redo(&editor->history, editor);
        text_editor_clamp_cursor(editor);
        break;
    }
    case Normal_Mode_Command_Type::YANK_MOTION: {
        Text_Slice slice = motion_evaluate_at_position(command.motion, editor->cursor_position, editor);
        string_reset(&editor->yanked_string);
        text_append_slice_to_string(editor->text, slice, &editor->yanked_string);
        editor->last_yank_was_line = false;
        break;
    }
    case Normal_Mode_Command_Type::YANK_LINE: {
        if (editor->text.size == 0) break;
        Text_Position delete_start = editor->cursor_position;
        delete_start.character = 0;
        Text_Position delete_end = editor->cursor_position;
        delete_end.character = 0;
        for (int i = 0; i < command.repeat_count; i++) {
            delete_end.line++;
        }
        text_position_sanitize(&delete_end, editor->text);
        Text_Slice slice = text_slice_make(delete_start, delete_end);
        string_reset(&editor->yanked_string);
        text_append_slice_to_string(editor->text, slice, &editor->yanked_string);
        editor->last_yank_was_line = true;
        break;
    }
    case Normal_Mode_Command_Type::PUT_BEFORE_CURSOR: {
        if (editor->last_yank_was_line) {
            Text_Position start_pos = editor->cursor_position;
            Text_Position pos = editor->cursor_position;
            pos.character = 0;
            text_position_sanitize(&pos, editor->text);
            String copy = string_create(editor->yanked_string.characters);
            text_history_insert_string(&editor->history, pos, copy);
            editor->cursor_position = start_pos;
            text_editor_clamp_cursor(editor);
            break;
        }
        String copy = string_create_from_string_with_extra_capacity(&editor->yanked_string, 0);
        text_history_insert_string(&editor->history, editor->cursor_position, copy);
        break;
    }
    case Normal_Mode_Command_Type::PUT_AFTER_CURSOR: {
        if (editor->last_yank_was_line) {
            Text_Position start_pos = editor->cursor_position;
            Text_Position pos = editor->cursor_position;
            pos.character = 0;
            pos.line++;
            text_position_sanitize(&pos, editor->text);
            String copy = string_create_from_string_with_extra_capacity(&editor->yanked_string, 0);
            text_history_insert_string(&editor->history, pos, copy);
            editor->cursor_position = start_pos;
            text_editor_clamp_cursor(editor);
            break;
        }
        editor->cursor_position = text_position_next(editor->cursor_position, editor->text);
        String copy = string_create_from_string_with_extra_capacity(&editor->yanked_string, 0);
        text_history_insert_string(&editor->history, editor->cursor_position, copy);
        break;
    }
    case Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_TOP: {
        editor->first_rendered_line = editor->cursor_position.line;
        break;
    }
    case Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_CENTER: {
        int line_count = (editor->last_editor_region.max.y - editor->last_editor_region.min.y) / editor->last_text_height;
        editor->first_rendered_line = math_maximum(0, editor->cursor_position.line - line_count / 2);
        break;
    }
    case Normal_Mode_Command_Type::MOVE_VIEWPORT_CURSOR_BOTTOM: {
        int line_count = (editor->last_editor_region.max.y - editor->last_editor_region.min.y) / editor->last_text_height;
        editor->first_rendered_line = math_maximum(0, editor->cursor_position.line - line_count);
        break;
    }
    case Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_TOP: {
        editor->cursor_position.line = editor->first_rendered_line;
        break;
    }
    case Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_CENTER: {
        int line_count = (editor->last_editor_region.max.y - editor->last_editor_region.min.y) / editor->last_text_height;
        editor->cursor_position.line = editor->first_rendered_line + line_count / 2;
        text_editor_clamp_cursor(editor);
        break;
    }
    case Normal_Mode_Command_Type::MOVE_CURSOR_VIEWPORT_BOTTOM: {
        int line_count = (editor->last_editor_region.max.y - editor->last_editor_region.min.y) / editor->last_text_height;
        editor->cursor_position.line = editor->first_rendered_line + line_count - 1;
        text_editor_clamp_cursor(editor);
        break;
    }
    case Normal_Mode_Command_Type::SCROLL_DOWNWARDS_HALF_PAGE: {
        int line_count = (editor->last_editor_region.max.y - editor->last_editor_region.min.y) / editor->last_text_height;
        editor->cursor_position.line += line_count / 2;
        text_editor_clamp_cursor(editor);
        editor->first_rendered_line = math_minimum(editor->text.size - 1, editor->first_rendered_line + line_count / 2);
        break;
    }
    case Normal_Mode_Command_Type::SCROLL_UPWARDS_HALF_PAGE: {
        int line_count = (editor->last_editor_region.max.y - editor->last_editor_region.min.y) / editor->last_text_height;
        editor->cursor_position.line -= line_count / 2;
        text_editor_clamp_cursor(editor);
        editor->first_rendered_line = math_maximum(0, editor->first_rendered_line - line_count / 2);
        break;
    }
    case Normal_Mode_Command_Type::GOTO_LAST_JUMP: {
        if (editor->jump_history_index == 0) break;
        editor->jump_history_index--;
        editor->cursor_position = editor->jump_history[editor->jump_history_index].jump_start;
        text_editor_clamp_cursor(editor);
        break;
    }
    case Normal_Mode_Command_Type::GOTO_NEXT_JUMP: {
        if (editor->jump_history_index >= editor->jump_history.size) break;
        editor->cursor_position = editor->jump_history[editor->jump_history_index].jump_end;
        editor->jump_history_index++;
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
        text_history_insert_character(&editor->history, editor->cursor_position, ' ');
        editor->cursor_position.character++;
        while (editor->cursor_position.character % 4 != 0) {
            text_history_insert_character(&editor->history, editor->cursor_position, ' ');
            editor->cursor_position.character++;
        }
    }
    else if (msg->key_code == KEY_CODE::W && msg->key_down && msg->ctrl_down)
    {
        if (editor->cursor_position.character == 0) {
            if (editor->cursor_position.line != 0) {
                Text_Position previous = text_position_previous(editor->cursor_position, editor->text);
                text_history_delete_character(&editor->history, previous);
                editor->cursor_position = previous;
            }
        }
        else
        {
            Text_Position pos = text_position_previous(editor->cursor_position, editor->text);
            char char_under_cursor = text_get_character_after(&editor->text, pos);
            bool all_whitespaces = false;
            if (char_under_cursor == ' ')
            {
                // Check if the before the cursor are only spaces
                all_whitespaces = true;
                while (pos.character >= 0) {
                    char c = text_get_character_after(&editor->text, pos);
                    if (c != ' ') {
                        all_whitespaces = false;
                        break;
                    }
                    pos.character -= 1;
                }
                pos.character = 0;
            }
            if (all_whitespaces) {
                pos.character = 0;
                pos.line = editor->cursor_position.line;
                text_history_delete_slice(&editor->history, text_slice_make(pos, editor->cursor_position));
                editor->cursor_position = pos;
            }
            else {
                Normal_Mode_Command cmd = normal_mode_command_make_with_motion(Normal_Mode_Command_Type::DELETE_MOTION, 1,
                    motion_make_from_movement(movement_make(Movement_Type::PREVIOUS_WORD, 1, 0)));
                normal_mode_command_execute(cmd, editor);
            }
        }
    }
    else if (msg->key_code == KEY_CODE::U && msg->key_down && msg->ctrl_down) {
        Text_Position line_start = editor->cursor_position;
        line_start.character = 0;
        text_history_delete_slice(&editor->history, text_slice_make(line_start, editor->cursor_position));
        editor->cursor_position = line_start;
    }
    else if (msg->character >= 32 && msg->character < 128)
    {
        text_history_insert_character(&editor->history, editor->cursor_position, msg->character);
        editor->cursor_position.character++;
        // Do some stupid formating
        if (string_contains_character(string_create_static("}])"), msg->character))
        {
            // Check if the line before is empty, if it is, find the matching parenthesis and put current parenthesis on this level
            String* line = &editor->text[editor->cursor_position.line];
            bool before_is_whitespace = true;
            for (int i = 0; i < editor->cursor_position.character - 1; i++) {
                if (text_get_character_after(&editor->text, text_position_make(editor->cursor_position.line, i)) != ' ') {
                    before_is_whitespace = false;
                    break;
                }
            }
            if (before_is_whitespace)
            {
                Text_Position closing_pos = text_position_make(editor->cursor_position.line, editor->cursor_position.character - 1);
                Movement mov;
                mov.repeat_count = 1;
                mov.type = Movement_Type::JUMP_ENCLOSURE;
                Text_Position other_pos = movement_evaluate_at_position(mov, closing_pos, editor);
                int target_indentation = text_editor_find_line_indentation(editor, other_pos.line, false);
                text_editor_set_line_indentation(editor, editor->cursor_position.line, target_indentation);
            }
        }
    }
    else if (msg->key_code == KEY_CODE::RETURN && msg->key_down)
    {
        int indentation = text_editor_find_line_indentation(editor, editor->cursor_position.line, true);
        text_history_insert_character(&editor->history, editor->cursor_position, '\n');
        editor->cursor_position.line++;
        editor->cursor_position.character = 0;
        text_editor_set_line_indentation(editor, editor->cursor_position.line, indentation);
    }
    else if (msg->key_code == KEY_CODE::BACKSPACE && msg->key_down)
    {
        Text_Position prev = text_position_previous(editor->cursor_position, editor->text);
        text_history_delete_character(&editor->history, prev);
        editor->cursor_position = prev;
    }
    else { // No valid command found
        msg_is_valid_command = false;
    }

    if (msg_is_valid_command && editor->mode == Text_Editor_Mode::INSERT && editor->record_insert_mode_inputs) {
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
    Dynamic_Array<Key_Message>* messages = &editor->normal_mode_incomplete_command;
    dynamic_array_push_back(messages, *new_message);

    Parse_Result<Normal_Mode_Command> command_parse = key_messages_parse_normal_mode_command(dynamic_array_as_array(messages));
    if (command_parse.symbol_type == Parse_Result_Type::SUCCESS) {
        normal_mode_command_execute(command_parse.result, editor);
        dynamic_array_reset(messages);
    }
    else if (command_parse.symbol_type == Parse_Result_Type::FAILURE) {
        String output = string_create_formated("Could not parse input, length: %d\n", messages->size);
        SCOPE_EXIT(string_destroy(&output));
        key_messages_append_to_string(dynamic_array_as_array(messages), &output);
        logg("%s\n", output.characters);
        dynamic_array_reset(messages);
    }
}

void text_editor_handle_key_message(Text_Editor* editor, Key_Message* message)
{
    if (editor->mode == Text_Editor_Mode::NORMAL) {
        normal_mode_handle_message(editor, message);
    }
    else {
        insert_mode_handle_message(editor, message);
    }
}

float zoom = -7.0f;
void text_editor_update(Text_Editor* editor, Input* input, double current_time)
{
    zoom += input->mouse_wheel_delta;
    editor->line_size_cm = 1.0f * math_power(1.1f, zoom);

    if (input->key_messages.size != 0) {
        editor->last_keymessage_time = current_time;
    }

    if (editor->text_changed)
    {
        if (math_absolute(editor->last_change_position.line - editor->cursor_position.line) > 8) {
            if (editor->jump_history_index != 0) {
                text_editor_record_jump(editor, editor->jump_history[editor->jump_history_index - 1].jump_end, editor->cursor_position);
            }
        }
        editor->last_change_position = editor->cursor_position;
    }

    if (!text_check_correctness(editor->text)) {
        panic("error, suit yourself\n");
        __debugbreak();
    }
    editor->text_changed = false;
}




