#include "string.hpp"

#include <cstring>
#include <cstdio>
#include <cstdarg>

#include "../math/scalars.hpp"
#include "../utility/utils.hpp"

String string_create_static_with_size(const char* content, int length)
{
    String result;
    result.size = length;
    result.characters = const_cast<char*>(content);
    result.capacity = length+1;
    return result;
}

String string_create_substring(String* string, int start_index, int end_index)
{
    if (start_index > end_index) {
        return string_create_empty(32);
    }
    start_index = math_clamp(start_index, 0, string->size-1);
    end_index = math_clamp(end_index, 0, string->size-1);

    String result;
    result.capacity = end_index - start_index + 2;
    result.characters = new char[result.capacity];
    result.size = end_index - start_index+1;
    memory_copy(result.characters, &string->characters[start_index], result.size);
    result.characters[result.size] = 0;
    return result;
}

u64 string_calculate_hash(String* string)
{
    u64 hash = 0;
    for (int i = 0; i < string->size; i++) {
        hash = (hash + string->characters[i]) * 18181979;
    }
    return hash;
}

bool string_equals(String* s1, String* s2)
{
    if (s1->size != s2->size) { return false; }
    return memcmp(s1->characters, s2->characters, s1->size) == 0;
}

String string_create_static(const char* content)
{
    String result;
    result.characters = const_cast<char*>(content);
    result.size = (int)strlen(content);
    result.capacity = result.size + 1;
    return result;
}

String string_create_empty(int capacity) {
    String result;
    result.characters = new char[capacity];
    result.characters[0] = 0;
    result.size = 0;
    result.capacity = capacity;
    return result;
}

String string_create_from_string_with_extra_capacity(String* other, int extra_capacity) {
    String result;
    result.capacity = other->size + 1 + extra_capacity;
    result.characters = new char[result.capacity];
    strcpy_s(result.characters, result.capacity, other->characters);
    result.size = other->size;
    return result;
}

String string_create(const char* content) {
    String result;
    result.size = (int)strlen(content);
    result.characters = new char[result.size+1];
    result.capacity = result.size + 1;
    strcpy_s(result.characters, result.size+1, content);
    return result;
}

void string_create_from_filepath_to_path_and_filename(String* path, String* filename, const char* filepath)
{
    // 1. Search last / or \
    // Create new strings before and after that
    String buffer = string_create(filepath); // Make editable buffer from const char
    int last_slash = -1;
    for (int i = 0; i < buffer.size; i++) {
        // Replace all \ with /
        if (buffer.characters[i] == '\\') {
            buffer.characters[i] = '/';
            last_slash = i;
        }
    }

    if (last_slash == -1) {
        // No path exists in filepath
        *filename = buffer;
        *path = string_create("./");
    }
    else {
        // Filepath contains a path and a filename, needs to be split
        *path = buffer;
        path->size = last_slash+1;
        *filename = string_create(path->characters + path->size);
        path->characters[path->size] = 0; // Add 0 terminator to path
    }
}

void string_destroy(String* string) {
    if (string->characters != 0) {
        delete[] string->characters;
    }
}

void string_reserve(String* string, int new_capacity) {
    if (string->capacity >= new_capacity) {
        return;
    }
    char* resized_buffer = new char[new_capacity];
    strcpy_s(resized_buffer, new_capacity, string->characters);
    delete[] string->characters;
    string->characters = resized_buffer;
    string->capacity = new_capacity;
}

void string_append(String* string, const char* appendix) {
    int appendix_length = (int)strlen(appendix);
    int required_capacity = string->size + appendix_length + 1;
    if (string->capacity < required_capacity) {
        string_reserve(string, required_capacity);
    }
    strcpy_s(string->characters + string->size, appendix_length+1, appendix);
    string->size += appendix_length;
}

void string_replace_character(String* string, char to_replace, char replace_with)
{
    for (int i = 0; i < string->size; i++) {
        if (string->characters[i] == to_replace) {
            string->characters[i] = replace_with;
        }
    }
}

void string_truncate(String* string, int vector_length) {
    if (vector_length < string->size) {
        string->size = vector_length;
        string->characters[vector_length] = 0;
    }
}

Optional<int> string_find_character_index_reverse(String* string, char character, int startpos)
{
    Optional<int> result;
    if (startpos >= string->size || startpos < 0) {
        result.available = false;
        return result;
    }

    for (int i = startpos; i >= 0; i--) {
        if (string->characters[i] == character) {
            result.available = true;
            result.value = i;
            return result;
        }
    }

    result.available = false;
    return result;
}

bool string_ends_with(const char* string, const char* ending) {
    int ending_length = (int) strlen(ending);
    int string_length = (int) strlen(string);
    if (ending_length > string_length) return false;
    return strcmp(ending, &(string[string_length - ending_length])) == 0;
}

String string_create_formated(const char* format, ...) 
{
    va_list args;
    va_start(args, format);

    // Allocate buffer
    String result;
    result.size = vsnprintf(0, 0, format, args);
    result.capacity = result.size+1;
    result.characters = new char[result.capacity];

    // Fill buffer
    vsnprintf(result.characters, result.capacity, format, args);
    va_end(args);

    return result;
}

void string_append_formated(String* string, const char* format, ...) 
{
    va_list args;
    va_start(args, format);
    int message_length = vsnprintf(0, 0, format, args);
    string_reserve(string, string->size + message_length + 1);
    int ret_val = vsnprintf(string->characters + string->size, string->capacity - string->size, format, args);
    if (ret_val < 0) {
        logg("Shit?\n");
    }
    string->size = string->size + message_length;
    if (string->size != strlen(string->characters)) {
        logg("shit??\n");
    }
    va_end(args);
}

void string_append_character(String* string, char c)
{
    if (string->size + 2 > string->capacity) {
        string_reserve(string, string->capacity * 2);
    }
    string->characters[string->size] = c; // Overwrite 0 terminator
    string->characters[string->size+1] = 0; // Set 0 terminator again
    string->size += 1;
}

void string_remove_substring(String* string, int start_index, int end_index) 
{
    if (string->size == 0 || end_index < start_index) {
        return;
    }
    start_index = math_clamp(start_index, 0, string->size-1);
    end_index = math_clamp(end_index, 0, string->size-1);
    int length = end_index - start_index + 1;
    for (int i = start_index; i < string->size - length; i++) {
        string->characters[i] = string->characters[i + length];
    }
    string->size = string->size - length;
    string->characters[string->size] = 0;
}

void string_reset(String* string)
{
    string->size = 0;
    string->characters[0] = 0;
}

void string_append_string(String* string, String* appendix)
{
    string_reserve(string, string->size + appendix->size + 1);
    memory_copy(string->characters + string->size, appendix->characters, appendix->size + 1);
    string->size += appendix->size;
}

void string_remove_character(String* string, int index) 
{
    if (index >= string->size) {
        return;
    }
    for (int i = index; i + 1 <= string->size; i++) {
        string->characters[i] = string->characters[i+1];
    }
    string->size -= 1;
}

void string_insert_character_before(String* string, byte character, int index)
{
    // Check if enough space is available
    if (string->size + 2 >= string->capacity) {
        string_reserve(string, string->capacity*2);
    }
    // Move all character after index one forward (Overwriting the 0 terminator)
    for (int i = string->size; i - 1 >= index; i--) {
        string->characters[i] = string->characters[i-1];
    }
    // Insert character
    string->characters[index] = character;
    // Update size
    string->size += 1;
    string->characters[string->size] = 0; // Null-Terminator
}

