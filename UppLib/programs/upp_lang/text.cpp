#include "text.hpp"

#include <cstring>

DynamicArray<String> text_create_empty() {
    DynamicArray<String> text = dynamic_array_create_empty<String>(6);
    dynamic_array_push_back(&text, string_create_empty(16));
    return text;
}

void text_destroy(DynamicArray<String>* text) {
    for (int i = 0; i < text->size; i++) {
        string_destroy(&text->data[i]);
    }
    dynamic_array_destroy(text);
}

void text_reset(DynamicArray<String>* text) {
    for (int i = 0; i < text->size; i++) {
        string_destroy(&text->data[i]);
    }
    dynamic_array_reset(text);
    dynamic_array_push_back(text, string_create_empty(16));
}

void text_delete_line(DynamicArray<String>* text, int line) {
    if (line == 0 && text->size == 1) return;
    if (line < 0 || line > text->size) return;
    string_destroy(&text->data[line]);
    dynamic_array_remove_ordered(text, line);
}

Text_Position text_position_make(int line, int character)
{
    Text_Position pos;
    pos.line = line;
    pos.character = character;
    return pos;
}

Text_Position text_position_make_start() {
    return text_position_make(0, 0);
}

Text_Position text_position_make_end(DynamicArray<String>* text) {
    return text_position_make(text->size-1, text->data[text->size-1].size);
}

bool text_position_are_equal(Text_Position a, Text_Position b) {
    return a.line == b.line && a.character == b.character;
}

void text_position_sanitize(Text_Position* pos, DynamicArray<String> text) {
    pos->line = math_clamp(pos->line, 0, math_maximum(0, text.size-1));
    pos->character = math_clamp(pos->character, 0, text[pos->line].size);
}

Text_Position text_position_previous(Text_Position pos, DynamicArray<String> text) {
    Text_Position result = pos;
    if (pos.character > 0) {
        result.character--;
        return result;
    }
    else {
        if (pos.line == 0) return pos;
        else {
            pos.line--;
            pos.character = text.data[pos.line].size;
            return pos;
        }
    }
}

Text_Position text_position_next(Text_Position pos, DynamicArray<String> text) {
    String* line = &text[pos.line];
    Text_Position next = pos;
    if (pos.character < line->size) next.character++;
    else if (pos.line < text.size - 1) { next.line++; next.character = 0; }
    return next;
}

bool text_position_are_in_order(Text_Position* a, Text_Position* b) {
    if (b->line > a->line) return true;
    else if (b->line < a->line) return false;
    else {
        return b->character >= a->character;
    }
}

Text_Slice text_slice_make(Text_Position start, Text_Position end) {
    Text_Slice result;
    result.start = start;
    result.end = end;
    return result;
}

Text_Slice text_slice_make_character_after(Text_Position pos, DynamicArray<String> text)
{
    text_position_sanitize(&pos, text);
    Text_Position next = text_position_next(pos, text);
    return text_slice_make(pos, next);
}

bool text_slice_contains_position(Text_Slice slice, Text_Position pos, DynamicArray<String> text)
{
    Text_Position end = text_position_previous(slice.end, text);
    return text_position_are_in_order(&slice.start, &pos) &&
        text_position_are_in_order(&pos, &end);
}

Text_Slice text_slice_make_line(DynamicArray<String> text, int line)
{
    if (line < 0 || line >= text.size) return text_slice_make(text_position_make(0, 0), text_position_make(0, 0));
    String* str = &text[line];
    return text_slice_make(text_position_make(line, 0), text_position_make(line, str->size));
}

void text_slice_sanitize(Text_Slice* slice, DynamicArray<String> text) {
    text_position_sanitize(&slice->start, text);
    text_position_sanitize(&slice->end, text);
    if (!text_position_are_in_order(&slice->start, &slice->end)) {
        Text_Position swap = slice->start;
        slice->start = slice->end;
        slice->end = swap;
    }
}

void text_append_slice_to_string(DynamicArray<String> text, Text_Slice slice, String* string)
{
    text_slice_sanitize(&slice, text);
    if (slice.start.line == slice.end.line) { // Special case if slice is only in one line
        String* line = &text[slice.start.line];
        string_append_character_array(string,
            array_create_static(line->characters + slice.start.character, slice.end.character - slice.start.character));
        return;
    }

    // Append from start line to end
    String* start_line = &text[slice.start.line];
    string_append_character_array(string,
        array_create_static(start_line->characters + slice.start.character, start_line->size - slice.start.character));
    string_append_character(string, '\n');

    // Append lines between start and end
    for (int i = slice.start.line+1; i < slice.end.line; i++) {
        string_append_string(string, &text[i]);
        string_append_character(string, '\n');
    }

    // Append from endline start to end
    String* end_line = &text[slice.end.line];
    string_append_character_array(string, array_create_static(end_line->characters, slice.end.character));
}

Text_Slice text_calculate_insertion_string_slice(DynamicArray<String>* text, Text_Position pos, String insertion)
{
    Text_Slice result;
    result.start = pos;

    text_position_sanitize(&pos, *text);
    // Dumb implementation: Go through each character and add it to the current position
    for (int i = 0; i < insertion.size; i++) {
        char c = insertion.characters[i];
        String* line = &text->data[pos.line];
        if (c == '\n') {
            pos.line += 1;
            pos.character = 0;
        }
        else {
            pos.character++;
        }
    }

    result.end = pos;
    return result;
}

void text_insert_string(DynamicArray<String>* text, Text_Position pos, String insertion)
{
    text_position_sanitize(&pos, *text);
    // Dumb implementation: Go through each character and add it to the current position
    for (int i = 0; i < insertion.size; i++) {
        char c = insertion.characters[i];
        String* line = &text->data[pos.line];
        if (c == '\n') {
            String new_line = string_create_substring(line, pos.character, line->size - 1);
            string_truncate(line, pos.character);
            dynamic_array_insert_ordered(text, new_line, pos.line+1);
            pos.line += 1;
            pos.character = 0;
        }
        else if (c == '\r') {

        }
        else {
            string_insert_character_before(line, c, pos.character);
            pos.character++;
        }
    }
}

void text_delete_slice(DynamicArray<String>* text, Text_Slice slice)
{
    text_slice_sanitize(&slice, *text);
    if (slice.end.line == slice.start.line)
    {
        String* line = &text->data[slice.end.line];
        string_remove_substring(line, slice.start.character, slice.end.character);
        return;
    }

    String* start_line = &text->data[slice.start.line];
    String* end_line = &text->data[slice.end.line];
    string_remove_substring(start_line, slice.start.character, start_line->size);
    string_remove_substring(end_line, 0, slice.end.character);
    string_append_string(start_line, end_line);
    for (int i = slice.start.line + 1; i <= slice.end.line; i++) {
        text_delete_line(text, slice.start.line + 1);
    }
}

void text_set_string(DynamicArray<String>* text, String* string)
{
    text_destroy(text);
    *text = text_create_empty();
    text_insert_string(text, text_position_make(0, 0), *string);
}

void text_append_to_string(DynamicArray<String>* text, String* result)
{
    text_append_slice_to_string(*text, 
        text_slice_make(text_position_make(0, 0), text_position_make(text->size - 1, text->data[text->size - 1].size)),
        result);
}

char text_get_character_after(DynamicArray<String>* text, Text_Position pos)
{
    String* line = &text->data[pos.line];
    if (pos.character >= line->size) {
        if (pos.line == text->size - 1) return '\0';
        return '\n';
    }
    else {
        return text->data[pos.line].characters[pos.character];
    }
}

bool text_check_correctness(DynamicArray<String> text) 
{
    if (text.size == 0) {
        logg("Correctness failed, text size is 0\n");
        return false;
    }

    for (int i = 0; i < text.size; i++) {
        String* line = &text[i];
        if (line->characters == 0) {
            logg("Correctness failed, text on line #%d is NULL\n", i);
            return false;
        }
        if (strlen(line->characters) != line->size) {
            logg("Correctness failed, line #%d length/size (%d) does not match string size(%d):\"%s\"",
                i, line->size, strlen(line->characters), line->characters);
            return false;
        }
    }
    return true;
}

bool test_text_to_string_and_back(String string)
{
    DynamicArray<String> text = text_create_empty();
    SCOPE_EXIT(text_destroy(&text));
    text_set_string(&text, &string);

    String reverted = string_create_empty(64);
    SCOPE_EXIT(string_destroy(&reverted));
    text_append_to_string(&text, &reverted);
    if (!string_equals(&reverted, &string)) {
        logg("Error: string \"%s\" does not match \"%s\"\n", reverted.characters, string.characters);
        return false;
    }
    return true;
}

void test_text_editor()
{
    DynamicArray<String> text = dynamic_array_create_empty<String>(4);
    dynamic_array_push_back(&text, string_create_empty(64));

    String str = string_create_static("Hello there\n What is up my dude\n\n Hello there\n what\n\n");
    text_set_string(&text, &str);

    // Test string to text/reverse
    Array<String> test_strings = array_create_from_list({
        string_create_static(""),
        string_create_static("."),
        string_create_static("\n"),
        string_create_static("\n\n\n"),
        string_create_static("\n\n\n."),
        string_create_static("What up\n my dude\n hello there\n\n\n\n"),
        string_create_static("What up\n my dude\n hello there\n\n\n\nwhat."),
    });
    for (int i = 0; i < test_strings.size; i++) {
        String* test_string = &test_strings[i];
        if (!test_text_to_string_and_back(*test_string)) {
        }
    }

    // Test appending slice
    Text_Slice slice = text_slice_make(text_position_make(0, 0), text_position_make(3, 4));
    String slice_substr = string_create_empty(64);
    text_append_slice_to_string(text, slice, &slice_substr);
    logg("\n\nTesting slice:\nString: \"%s\"\n", slice_substr.characters);

    // Test Insertion
    string_reset(&slice_substr);
    String insertion_str = string_create_static("Test me\nNEW\nNEW\n what --- ");
    text_insert_string(&text, slice.end, insertion_str);
    text_append_to_string(&text, &slice_substr);
    logg("\n\nString after Insertion: \"%s\"\n", slice_substr.characters);
    if (!text_check_correctness(text))
        logg("Error");

    // Test slice deletion
    string_reset(&slice_substr);
    text_set_string(&text, &str);
    text_delete_slice(&text, slice);
    text_append_to_string(&text, &slice_substr);
    if (!text_check_correctness(text))
        logg("Error");
    logg("\n\nString after slice deletion: \"%s\"\n", slice_substr.characters);

    logg("shit");
}

void text_insert_character_before(DynamicArray<String>* text, Text_Position pos, char c)
{
    text_position_sanitize(&pos, *text);
    String* line = &text->data[pos.line];
    if (c == '\n') {
        String new_line = string_create_substring(line, pos.character, line->size - 1);
        string_truncate(line, pos.character);
        dynamic_array_insert_ordered(text, new_line, pos.line + 1);
    }
    else {
        string_insert_character_before(line, c, pos.character);
    }
}

Text_Position text_get_last_position(DynamicArray<String>* text)
{
    return text_position_make(text->size - 1, text->data[text->size - 1].size);
}

Text_Iterator text_iterator_make(DynamicArray<String>* text, Text_Position pos)
{
    Text_Iterator result;
    text_position_sanitize(&pos, *text);
    result.text = text;
    result.position = pos;
    result.character = text_get_character_after(text, pos);
    return result;
}

void text_iterator_set_position(Text_Iterator* it, Text_Position pos)
{
    text_position_sanitize(&pos, *it->text);
    it->position = pos;
    it->character = text_get_character_after(it->text, pos);
}

bool text_iterator_has_next(Text_Iterator* it)
{
    String* line = &it->text->data[it->position.line];
    return it->position.character < line->size || it->position.line < it->text->size - 1;
}

void text_iterator_advance(Text_Iterator* it)
{
    it->position = text_position_next(it->position, *it->text);
    it->character = text_get_character_after(it->text, it->position);
}

void text_iterator_move_back(Text_Iterator* it) {
    it->position = text_position_previous(it->position, *(it->text));
    it->character = text_get_character_after(it->text, it->position);
}

bool text_iterator_goto_next_character(Text_Iterator* it, char c, bool forwards) 
{
    if (forwards) {
        while (text_iterator_has_next(it)) {
            if (it->character == c) return true;
            text_iterator_advance(it);
        }
        return false;
    }
    else {
        while (!text_position_are_equal(it->position, text_position_make_start())) {
            if (it->character == c) return true;
            text_iterator_move_back(it);
        }
        return false;
    }
}

bool text_iterator_goto_next_in_set(Text_Iterator* it, String set)
{
    while (text_iterator_has_next(it)) {
        for (int i = 0; i < set.size; i++) {
            char c = set.characters[i];
            if (it->character == c) {
                return true;
            }
        }
        text_iterator_advance(it);
    }
    return false;
}

bool text_iterator_skip_characters_in_set(Text_Iterator* iterator, String set, bool skip_in_set)
{
    while (text_iterator_has_next(iterator))
    {
        bool is_in_set = string_contains_character(set, iterator->character);
        if (!skip_in_set && is_in_set) {
            return true;
        }
        else if (skip_in_set && !is_in_set) {
            return true;
        }
        text_iterator_advance(iterator);
    }

    return false;
}

