#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"

// Note: A Text position is inbetween two characters, not ON a character... e.g. "|ab", "a|b", "ab|", where | signifies a text position
//       This means that the character may also be string->size, or 0
struct Text_Position
{
    int line; // Starts at 0
    int character; // Starts at 0
};
Text_Position text_position_make(int line, int character);
Text_Position text_position_make_start();
Text_Position text_position_make_end(DynamicArray<String>* text);
Text_Position text_position_make_line_end(DynamicArray<String>* text, int line);
bool text_position_are_equal(Text_Position a, Text_Position b);
void text_position_sanitize(Text_Position* pos, DynamicArray<String> text);
Text_Position text_position_next(Text_Position pos, DynamicArray<String> text);
Text_Position text_position_previous(Text_Position pos, DynamicArray<String> text);
bool text_position_are_in_order(Text_Position* a, Text_Position* b);

struct Text_Slice {
    Text_Position start;
    Text_Position end;
};
Text_Slice text_slice_make(Text_Position start, Text_Position end);
void text_slice_sanitize(Text_Slice* slice, DynamicArray<String> text); 
Text_Slice text_slice_make_line(DynamicArray<String> text, int line);
Text_Slice text_slice_make_character_after(Text_Position pos, DynamicArray<String> text);
bool text_slice_contains_position(Text_Slice slice, Text_Position pos);

// Text Functions
DynamicArray<String> text_create_empty();
void text_destroy(DynamicArray<String>* text);
void text_reset(DynamicArray<String>* text);
Text_Slice text_calculate_insertion_string_slice(DynamicArray<String>* text, Text_Position pos, String insertion);
void text_insert_string(DynamicArray<String>* text, Text_Position pos, String insertion);
void text_insert_character_before(DynamicArray<String>* text, Text_Position pos, char c);
void text_delete_slice(DynamicArray<String>* text, Text_Slice slice);
void text_delete_line(DynamicArray<String>* text, int line);
void text_append_slice_to_string(DynamicArray<String> text, Text_Slice slice, String* string);
void text_set_string(DynamicArray<String>* text, String* string);
void text_append_to_string(DynamicArray<String>* text, String* result);
char text_get_character_after(DynamicArray<String>* text, Text_Position pos);
bool text_check_correctness(DynamicArray<String> text);
Text_Position text_get_last_position(DynamicArray<String>* text);

// Text Iterator
struct Text_Iterator
{
    DynamicArray<String>* text;
    Text_Position position;
    char character;
};
Text_Iterator text_iterator_make(DynamicArray<String>* text, Text_Position pos);
bool text_iterator_has_next(Text_Iterator* it);
void text_iterator_advance(Text_Iterator* it);
void text_iterator_move_back(Text_Iterator* it);
bool text_iterator_goto_next_in_set(Text_Iterator* it, String set);
bool text_iterator_goto_next_character(Text_Iterator* it, char c, bool forwards);
void text_iterator_set_position(Text_Iterator* it, Text_Position pos);
bool text_iterator_skip_characters_in_set(Text_Iterator* iterator, String set, bool skip_in_set);


void test_text_editor();
