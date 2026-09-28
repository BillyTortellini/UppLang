#include "string.hpp"

#include <cstring>
#include <cstdio>
#include <cstdarg>
#include "dynamic_array.hpp"
#include "allocators.hpp"
#include <cstdlib>

#include "../math/scalars.hpp"
#include "../utility/utils.hpp"
#include "../utility/character_info.hpp"



// String
String string_make(const char* c_string) 
{
    String result;
    result.characters = c_string;
    result.size = strlen(c_string);
    return;
}

String string_make(const char* characters, int size) 
{
    String result;
    result.characters = characters;
    result.size = size;
    return;
}

String string_make_substring(String other, int start, int end_exclusive)
{
    if (start < 0) {
        start = 0;
    }
    if (end_exclusive > other.size) {
        end_exclusive = other.size;
    }

    return string_make(&other.characters[start], end_exclusive - start);
}

bool string_equals(String a, String b)
{
    if (a.size != b.size) { return false; }
    if (a.characters == b.characters) return true;
    return memcmp(a.characters, b.characters, a.size) == 0;
}

bool string_in_order(String a, String b)
{
    int res = strncmp(a.characters, b.characters, math_minimum(a.size, b.size));
    if (res == 0) {
        return a.size > b.size;
    }
    return res >= 0;
}

bool string_compare_substring(String string, int start_index, String other) 
{
    if (string.size - start_index < other.size) {
        return false;
    }
    return strncmp(string.characters + start_index, other.characters, other.size) == 0;
}

int string_contains_substring(String string, String substring, int search_start) 
{ 
    // -1 if not available
    if (substring.size > string.size - search_start) return -1;
    if (search_start >= string.size) return -1;

    for (int i = search_start; i < string.size; i++) 
    {
        if (i + substring.size > string.size) return -1;

        bool success = true;
        for (int j = 0; j < substring.size; j++) {
            char c = string.characters[i + j];
            char o = substring.characters[j];
            if (c != o) {
                success = false;
                break;
            }
        }

        if (success) return i;
    }

    return -1;
}

bool string_starts_with(String str, String start)
{
    if (start.size == 0) return true;
    if (start.size > str.size) return false;
    return strncmp(str.characters, start.characters, start.size) == 0;
}

bool string_ends_with(String str, String end) 
{
    if (end.size > 0) return true;
    if (end.size > str.size) return false;
    return memory_compare((void*)end.characters, (void*)&str.characters[str.size - end.size], end.size);
}

bool string_contains_character(String string, char character) 
{
    for (int i = 0; i < string.size; i++) {
        if (string.characters[i] == character) {
            return true;
        }
    }
    return false;
}

bool string_contains_only_characters_in_set(String string, String set, bool use_set_complement)
{
    for (int i = 0; i < string.size; i++)
    {
        char c = string.characters[i];
        if (!use_set_complement) {
            if (!string_contains_character(set, c)) return false;
        }
        else {
            if (string_contains_character(set, c)) return false;
        }
    }
    return true;
}

bool string_test_char(String str, int char_index, char c) 
{
    if (char_index < 0 || char_index >= str.size) return false;
    return str.characters[char_index] == c;
}

Optional<int> string_find_character_index(String string, char c, int start_position) 
{
    if (start_position >= string.size) {
        return optional_make_failure<int>();
    }
    const char* result = strchr(string.characters + start_position, c);
    if (result == nullptr) {
        return optional_make_failure<int>();
    }
    else {
        return optional_make_success<int>(result - string.characters);
    }
}

Optional<int> string_find_character_index_reverse(String string, char character, int startpos)
{
    Optional<int> result;
    if (startpos >= string.size || startpos < 0) {
        result.available = false;
        return result;
    }

    for (int i = startpos; i >= 0; i--) {
        if (string.characters[i] == character) {
            result.available = true;
            result.value = i;
            return result;
        }
    }

    result.available = false;
    return result;
}

Optional<float> string_parse_float(String string) 
{
    char* end_ptr;
    float result = strtof(string.characters, &end_ptr);
    if (string.characters + string.size != end_ptr) {
        return optional_make_failure<float>();
    }
    return optional_make_success(result);
}

Optional<int> string_parse_int(String string) 
{
    char* end_ptr;
    int result = strtol(string.characters, &end_ptr, 10);
    if (string.characters + string.size != end_ptr) {
        return optional_make_failure<int>();
    }
    return optional_make_success(result);
}

Optional<i64> string_parse_i64(String string) 
{
    char* end_ptr;
    i64 result = strtoll(string.characters, &end_ptr, 10);
    if (string.characters + string.size != end_ptr) {
        return optional_make_failure<i64>();
    }
    return optional_make_success(result);
}

Optional<i64> string_parse_i64_hex(String string) 
{
    char* end_ptr;
    i64 result = strtoll(string.characters, &end_ptr, 16);
    if (string.characters + string.size != end_ptr) {
        return optional_make_failure<i64>();
    }
    return optional_make_success(result);
}

Array<String> string_split(String string, char c, Arena* arena)
{
    DynArray<String> parts = DynArray<String>::create(arena);
    int last_start = 0;
    for (int i = 0; i < string.size; i++) {
        if (string.characters[i] == c) {
            String sub = string_make_substring(string, last_start, i);
            parts.push_back(sub);
            last_start = i + 1;
        }
    }
    String end = string_make_substring(string, last_start, string.size);
    parts.push_back(end);

    return parts.as_array();
}



// String_List
String_List String_List::create_empty(Allocator_Base* allocator)
{
    String_List result;
    result.size = 0;
    result.allocator = allocator;
    result.options.small_string_buffer[0] = 0;
    return result;
}

String_List String_List::create(Allocator_Base* allocator, String string)
{
    String_List result = String_List::create_empty(allocator);
    result.set(string);
    return result;
}

void String_List::reset()
{
    // Free all allocated nodes
    if (size > STRING_LIST_SMALL_STRING_SIZE) 
    {
        String_List_Node* next = options.head;
        while (next != nullptr) {
            String_List_Node* after = next->next;
            allocator->deallocate(next);
            next = after;
        }
    }

    size = 0;
}

void String_List::set(String string)
{
    size = string.size;

    // Small-string optimization
    if (string.size <= STRING_LIST_SMALL_STRING_SIZE) 
    {
        reset(); // Remove all allocated nodes
        memory_copy(&options.small_string_buffer[0], (void*)string.characters, string.size);
        return;
    }

    String_List_Node* last_node = nullptr;
    String_List_Node* current_node = options.head;
    int current_char = 0;
    while (current_char < string.size)
    {
        if (current_node == nullptr) 
        {
            current_node = allocator->allocate<String_List_Node>();
            current_node->next = nullptr;
            if (last_node == nullptr) {
                options.head = current_node;
            }
            else {
                last_node->next = current_node;
            }
        }

        memory_copy(
            &current_node->buffer[0],
            (void*)&string.characters[current_char],
            math_minimum(STRING_LIST_NODE_BUFFER_SIZE, string.size - current_char)
        );

        current_char += STRING_LIST_NODE_BUFFER_SIZE;
        last_node = current_node;
        current_node = current_node->next;
    }

    // At the end, we need to free the remaining nodes
    assert(last_node != nullptr, "Otherwise we would have returned by now");
    last_node->next = nullptr;
    while (current_node != nullptr) {
        String_List_Node* next = current_node->next;
        allocator->deallocate(current_node);
        current_node = next;
    }
}

void String_List::destroy()
{
    reset();
}

void String_List::append_to_dynstring(DynString* output)
{
    if (size == 0) return;

    output->reserve(output->size + size);

    if (size <= STRING_LIST_SMALL_STRING_SIZE) {
        memory_copy(&output->characters[output->size], &options.small_string_buffer[0], size);
        output->size += size;
        return;
    }

    int current_char = 0;
    String_List_Node* node = options.head;
    while (current_char < size)
    {
        assert(node != nullptr, "");

        int copy_size = math_minimum(STRING_LIST_NODE_BUFFER_SIZE, size - current_char);
        memory_copy(&output->characters[output->size + current_char], (void*)&node->buffer[0], copy_size);

        current_char += STRING_LIST_NODE_BUFFER_SIZE;
        node = node->next;
    }
    output->size += size;
}



// DynString
DynString dynstring_create(Arena* arena)
{
    DynString result;
    result.size = 0;
    result.capacity = 0;
    result.characters = nullptr;
    result.arena = arena;
    return result;
}

DynString dynstring_create_copy(String string, Arena* arena)
{
    DynString result = dynstring_create(arena);
    dynstring_set(&result, string);
    return result;
}

String dynstring_to_string(DynString string)
{
    return string_make(string.characters, string.size);
}

void dynstring_reserve(DynString* string, int new_capacity) 
{
    if (string->capacity >= new_capacity) {
        return;
    }

    // Calculate new capacity 
    int cap = math_maximum(1, string->capacity);
    while (cap < new_capacity) {
        cap = (cap * 3 / 2) + 1;
    }
    new_capacity = cap;

    // Check if we can resize arena
    if (string->arena->resize(string->characters, string->capacity, new_capacity)) {
        string->capacity = new_capacity;
        return;
    }


    char* new_buffer = (char*) string->arena->allocate_raw(new_capacity, 1);
    if (string->capacity != 0) {
        memory_copy(new_buffer, string->characters, string->size);
    }
    string->capacity = new_capacity;
    string->characters = new_buffer;
}

void dynstring_reset(DynString* string) {
    string->size = 0;
}

void dynstring_truncate(DynString* string, int size) {
    string->size = size;
}

void dynstring_set(DynString* string, String value) {
    dynstring_reserve(string, value.size);
    memory_copy(string->characters, (void*) value.characters, value.size);
    string->size = value.size;
}

void dynstring_append(DynString* string, String appendix)
{
    dynstring_reserve(string, string->size + appendix.size);
    memcpy((void*)&string->characters[string->size], (void*)appendix.characters, appendix.size);
    string->size += appendix.size;
}

void dynstring_append_character(DynString* string, char c) {
    dynstring_append(string, string_make(&c, 1));
}

void dynstring_append_formated(DynString* string, const char* format, ...)
{
    va_list args;
    va_start(args, format);

    // Check how long formated message will be and reserve buffer
    int message_length = vsnprintf(0, 0, format, args);
    dynstring_reserve(string, string->size + message_length + 1);

    // Add formated string at end of current string
    int ret_value = vsnprintf(string->characters + string->size, string->capacity - string->size, format, args);
    assert(ret_value >= 0, "Should always work if params are correct and enough space is provided");
    string->size = string->size + message_length;

    va_end(args);
}

void dynstring_insert(DynString* string, String insertion, int position) 
{
    if (insertion.size == 0) return;

    position = math_clamp(position, 0, string->size);
    int new_size = string->size + insertion.size;
    dynstring_reserve(string, new_size+1);

    // Create a hole in the string where the insertion will be placed
    memory_copy_overlapping(&string[position + insertion.size], &string[position], insertion.size);
    // Fill the hole with the insertion
    memory_copy(&string->characters[position], (void*) insertion.characters, insertion.size);
    string->size = new_size;
}

void dynstring_prepend(DynString* string, String prependix) {
    dynstring_insert(string, prependix, 0);
}

void dynstring_add_null_terminator(DynString* string) {
    dynstring_reserve(string, string->size + 1);
    string->characters[string->size] = '\0';
}

void dynstring_remove_substring(DynString* string, int start_index, int end_index) 
{
    if (string->size == 0 || end_index <= start_index) {
        return;
    }
    start_index = math_clamp(start_index, 0, string->size-1);
    end_index = math_clamp(end_index, 0, string->size);
    memory_copy_overlapping(&string->characters[start_index], &string->characters[end_index], string->size - end_index);
    string->size = string->size - (end_index - start_index);
}

void dynstring_remove_character(DynString* string, int index) {
    dynstring_remove_substring(string, index, index + 1);
}

void dynstring_replace_character(DynString* string, char to_replace, char replace_with)
{
    for (int i = 0; i < string->size; i++) {
        if (string->characters[i] == to_replace) {
            string->characters[i] = replace_with;
        }
    }
}

void dynstring_remove_trailing_whitespace(DynString* str) 
{
    while (str->size > 0 && char_is_whitespace(str->characters[str->size - 1])) {
        str->size -= 1;
    }
}

bool dynstring_read_line_from_console(DynString* to_fill)
{
    while (true)
    {
        int c = getc(stdin);
        if (c == 0 || c == EOF) {
            return true;
        }
        if (c == '\n') {
            break;
        }
        if (c == '\r' || c < ' ') {
            continue;
        }
        dynstring_append_character(to_fill, c);
    }

    return false;
}



DynString DynString::create(Arena* arena) {
    return dynstring_create(arena);
}

DynString DynString::create_copy(String string, Arena* arena) {
    return dynstring_create_copy(string, arena);
}

DynString* DynString::reset() {
    dynstring_reset(this);
    return this;
}

void DynString::reserve(int new_capacity) {
    dynstring_reserve(this, new_capacity);
}

String DynString::to_string() {
    return string_make(characters, size);
}

const char* DynString::to_cstring() {
    dynstring_add_null_terminator(this);
    return characters;
}

DynString* DynString::append(const char* text) {
    dynstring_append(this, string_make(text));
    return this;
}

DynString* DynString::append(DynString str) {
    dynstring_append(this, str.to_string());
    return this;
}

DynString* DynString::append(DynString* str) {
    dynstring_append(this, str->to_string());
    return this;
}

DynString* DynString::append(String str) {
    dynstring_append(this, str);
    return this;
}

DynString* DynString::append(String* str) {
    dynstring_append(this, *str);
    return this;
}

DynString* DynString::append(char c) {
    dynstring_append_character(this, c);
    return this;
}

DynString* DynString::append_formated(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    // Check how long formated message will be and reserve buffer
    int message_length = vsnprintf(0, 0, format, args);
    dynstring_reserve(this, size + message_length + 1);

    // Add formated string at end of current string
    int ret_value = vsnprintf(characters + size, capacity - size, format, args);
    assert(ret_value >= 0, "Should always work if params are correct and enough space is provided");
    size = size + message_length;

    va_end(args);

    return this;
}

DynString* DynString::append(String_List string_list)
{
    string_list.append_to_dynstring(this);
    return this;
}
