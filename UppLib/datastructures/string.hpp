#pragma once

#include "../utility/utils.hpp"

struct String
{
    char* characters;
    int size;
    int capacity;
    
    char& operator[](int index) {
        return characters[index];
    }
};

String string_create_static(const char* content);
String string_create_static_with_size(char* content, int size);
String string_create(const char* content);
String string_create_formated(const char* format, ...);
String string_create_empty(int capacity);
String string_create_from_string_with_extra_capacity(String* other, int extra_capacity);
String string_create_substring(String* string, int start_index, int end_index);
u64 string_calculate_hash(String* string);
bool string_equals(String* s1, String* s2);
void string_create_from_filepath_to_path_and_filename(String* path, String* filename, const char* filepath);
void string_destroy(String* string);

void string_remove_substring(String* string, int start_index, int end_index);
void string_reset(String* string);
void string_reserve(String* string, int new_capacity);
void string_append(String* string, const char* appendix);
void string_append_string(String* string, String* appendix);
void string_append_formated(String* string, const char* format, ...);
void string_append_character(String* string, char c);
void string_truncate(String* string, int vector_length);
void string_replace_character(String* string, char to_replace, char replace_with);
Optional<int> string_find_character_index_reverse(String* string, char character, int startpos);
bool string_ends_with(const char* string, const char* ending);
void string_remove_character(String* string, int index);
void string_insert_character_before(String* string, byte character, int index);
