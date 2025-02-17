#pragma once

#include "../utility/utils.hpp"
#include "../datastructures/array.hpp"

struct String
{
    char* characters;
    int size;
    int capacity;
    
    char& operator[](int index) {
        return characters[index];
    }
};

void string_destroy(String* string);

String string_create(int capacity = 0);
String string_create(const char* content);
String string_create_empty(int capacity);
String string_copy(String other);
String string_create_from_string_with_extra_capacity(String* other, int extra_capacity);
String string_create_formated(const char* format, ...);
String string_create_substring(String* string, int start_index, int end_index);
void string_create_from_filepath_to_path_and_filename(String* path, String* filename, const char* filepath);

String string_create_static(const char* content);
String string_create_static_with_size(const char* content, int length);
String string_create_substring_static(String* string, int start_pos, int end_pos);

bool string_equals(String* s1, String* s2);
bool string_equals_cstring(String* string, const char* compare);
bool string_in_order(String* s1, String* s2);
void string_clear(String* string);
void string_set_characters(String* string, const char* characters);

void string_remove_substring(String* string, int start_index, int end_index);
bool string_compare_substring(String* string, int start_index, String* other);
int string_contains_substring(String string, int search_start, String substring); // -1 if not available
void string_reset(String* string);
void string_reserve(String* string, int new_capacity);
void string_append(String* string, const char* appendix);
void string_append_string(String* string, String* appendix);
void string_prepend_string(String* string, String* prepension);
void string_append_formated(String* string, const char* format, ...);
void string_append_character(String* string, char c);
void string_append_character_array(String* string, Array<char> appendix); // Difference to append c_string is that appendix does not need to be 0 terminated
void string_truncate(String* string, int vector_length);
void string_replace_character(String* string, char to_replace, char replace_with);
bool string_starts_with(String str, const char* start);
bool string_ends_with(const char* string, const char* ending);
void string_remove_character(String* string, int index);
void string_insert_character_before(String* string, byte character, int index);
void string_insert_string(String* string, String* insertion, int position);
bool string_contains_character(String string, char character);
bool string_contains_only_characters_in_set(String* string, String set, bool use_set_complement);
bool string_test_char(String str, int char_index, char c);
Optional<int> string_find_character_index(String* string, char c, int start_position);
Optional<int> string_find_character_index_reverse(String* string, char character, int startpos);
Optional<float> string_parse_float(String* string);
Optional<int> string_parse_int(String* string);
Optional<i64> string_parse_i64(String* string);
Optional<i64> string_parse_i64_hex(String string);
Array<String> string_split(String string, char c); 
void string_split_destroy(Array<String> parts);
bool string_fill_from_line(String* to_fill);
String string_create_filename_from_path_static(String* filepath);
