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
const char& String::at(int index) {
    assert(index >= 0 &&index < size, "");
    return data[index];
}

const char& String::operator[](int index) {
    assert(index >= 0 && index < size, "");
    return data[index];
}

String string_make(const char* c_string) 
{
    String result;
    result.data = c_string;
    result.size = strlen(c_string);
    return result;
}

String string_make(const char* data, int size) 
{
    String result;
    result.data = data;
    result.size = size;
    return result;
}

String string_make_substring(String other, int start, int end_exclusive)
{
    if (start < 0) {
        start = 0;
    }
    if (end_exclusive > other.size) {
        end_exclusive = other.size;
    }

    return string_make(&other.data[start], end_exclusive - start);
}

String string_allocate_copy(String other, Allocator_Base* allocator)
{
    if (other.size == 0) {
        return string_make("", 0);
    }

    char* data = allocator->allocate_array<char>(other.size).data;
    memory_copy_no_overlap(&data[0], (void*) &other.data[0], other.size);
    return string_make(data, other.size);
}

void string_deallocate(String* string, Allocator_Base* allocator)
{
    if (string->size == 0) return;
    allocator->deallocate_raw(&string->data, string->size);
    string->data = nullptr;
    string->size = -1;
}

bool string_equals(String a, String b)
{
    if (a.size != b.size) { return false; }
    if (a.data == b.data) return true;
    return memcmp(a.data, b.data, a.size) == 0;
}

bool String::operator==(String other) {
    return string_equals(*this, other);
}

bool String::operator!=(String other) {
    return !string_equals(*this, other);
}

bool string_equals_pointers(String* a, String* b)
{
    return string_equals(*a, *b);
}

u64 hash_string_pointer(String* string) {
    return hash_memory(array_make((byte*)string->data, string->size));
}

u64 hash_string(String string) {
    return hash_string_pointer(&string);
}

u64 string_hash(String a) {
    return hash_string(a);
}

u64 string_hash_pointer(String* a) {
    return hash_string_pointer(a);
}

Comparison_Result string_compare(String a, String b)
{
    int res = strncmp(a.data, b.data, math_minimum(a.size, b.size));
    if (res < 0) return Comparison_Result::LESS;
    if (res == 0) return Comparison_Result::EQUAL;
    return Comparison_Result::GREATER;
}

bool string_test_substring(String string, int start_index, String other) 
{
    if (string.size - start_index < other.size) {
        return false;
    }
    return strncmp(string.data + start_index, other.data, other.size) == 0;
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
            char c = string.data[i + j];
            char o = substring.data[j];
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
    return strncmp(str.data, start.data, start.size) == 0;
}

bool string_ends_with(String str, String end) 
{
    if (end.size > 0) return true;
    if (end.size > str.size) return false;
    return memory_compare((void*)end.data, (void*)&str.data[str.size - end.size], end.size);
}

bool string_contains_character(String string, char character) 
{
    for (int i = 0; i < string.size; i++) {
        if (string.data[i] == character) {
            return true;
        }
    }
    return false;
}

bool string_contains_only_characters_in_set(String string, String set, bool use_set_complement)
{
    for (int i = 0; i < string.size; i++)
    {
        char c = string.data[i];
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
    return str.data[char_index] == c;
}

Optional<int> string_find_character_index(String string, char c, int start_position) 
{
    if (start_position >= string.size) {
        return optional_make_failure<int>();
    }
    const char* result = strchr(string.data + start_position, c);
    if (result == nullptr) {
        return optional_make_failure<int>();
    }
    else {
        return optional_make_success<int>(result - string.data);
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
        if (string.data[i] == character) {
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
    float result = strtof(string.data, &end_ptr);
    if (string.data + string.size != end_ptr) {
        return optional_make_failure<float>();
    }
    return optional_make_success(result);
}

Optional<int> string_parse_int(String string) 
{
    char* end_ptr;
    int result = strtol(string.data, &end_ptr, 10);
    if (string.data + string.size != end_ptr) {
        return optional_make_failure<int>();
    }
    return optional_make_success(result);
}

Optional<i64> string_parse_i64(String string) 
{
    char* end_ptr;
    i64 result = strtoll(string.data, &end_ptr, 10);
    if (string.data + string.size != end_ptr) {
        return optional_make_failure<i64>();
    }
    return optional_make_success(result);
}

Optional<i64> string_parse_i64_hex(String string) 
{
    char* end_ptr;
    i64 result = strtoll(string.data, &end_ptr, 16);
    if (string.data + string.size != end_ptr) {
        return optional_make_failure<i64>();
    }
    return optional_make_success(result);
}

Array<String> string_split(String string, char c, Arena* arena)
{
    DynArray<String> parts = DynArray<String>::create(arena);
    int last_start = 0;
    for (int i = 0; i < string.size; i++) {
        if (string.data[i] == c) {
            String sub = string_make_substring(string, last_start, i);
            parts.push_back(sub);
            last_start = i + 1;
        }
    }
    String end = string_make_substring(string, last_start, string.size);
    parts.push_back(end);

    return parts.as_array();
}

String_Split_Pair string_split_once(String string, char c)
{
    String_Split_Pair result;
    result.a = string_make("", 0);
    result.b = string_make("", 0);
    result.worked = false;

    auto split_index_opt = string_find_character_index(string, c, 0);
    if (!split_index_opt.available) return result;

    result.a = string_make_substring(string, 0, split_index_opt.value);
    result.b = string_make_substring(string, split_index_opt.value + 1, string.size);
    result.worked = true;
    return result;
}

String string_get_whitespace_trimmed(String string)
{
    int start = 0;
    while (start < string.size) {
        char c = string[start];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            start += 1;
            continue;
        }
        break;
    }

    int end = string.size - 1;
    while (end <= start && end >= 0) {
        char c = string[end];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            end -= 1;
            continue;
        }
        break;
    }

    if (end < start) return string_make("", 0);
    return string_make_substring(string, start, end + 1); // End inclusive
}



// String_List
String_List String_List::create_empty(Allocator_Base* allocator)
{
    String_List result;
    result.size = 0;
    result.allocator = allocator;
    result.options.head = nullptr;
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
    this->options.head = nullptr;

    size = 0;
}

void String_List::set(String string)
{
    // Small-string optimization
    if (string.size <= STRING_LIST_SMALL_STRING_SIZE) 
    {
        reset(); // Remove all allocated nodes
        size = string.size;
        memory_copy_no_overlap(&options.small_string_buffer[0], (void*)string.data, string.size);
        return;
    }

    if (size <= STRING_LIST_SMALL_STRING_SIZE) {
        this->options.head = nullptr; // Going from smallstring to list requires this
    }
    size = string.size;

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

        memory_copy_no_overlap(
            &current_node->buffer[0],
            (void*)&string.data[current_char],
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

void String_List::deallocate() {
    reset();
}

void String_List::destroy()
{
    reset();
}

void String_List::append_to_dynstring(DynString* output)
{
    if (size == 0) return;
    
    output->reserve_capacity(output->size + size);

    if (size <= STRING_LIST_SMALL_STRING_SIZE) {
        memory_copy_no_overlap(&output->data[output->size], &options.small_string_buffer[0], size);
        output->size += size;
        return;
    }

    int current_char = 0;
    String_List_Node* node = options.head;
    while (current_char < size)
    {
        assert(node != nullptr, "");

        int copy_size = math_minimum(STRING_LIST_NODE_BUFFER_SIZE, size - current_char);
        memory_copy_no_overlap(&output->data[output->size + current_char], (void*)&node->buffer[0], copy_size);

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
    result.data = nullptr;
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
    return string_make(string.data, string.size);
}

void dynstring_reserve_capacity(DynString* string, int new_capacity) 
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
    if (string->capacity != 0 && string->arena->resize(string->data, string->capacity, new_capacity)) {
        string->capacity = new_capacity;
        return;
    }

    char* new_buffer = (char*) string->arena->allocate_raw(new_capacity, 1);
    if (string->capacity != 0) {
        memory_copy_no_overlap(new_buffer, string->data, string->size);
    }
    string->capacity = new_capacity;
    string->data = new_buffer;
}

void dynstring_reset(DynString* string) {
    string->size = 0;
}

void dynstring_truncate(DynString* string, int size) {
    string->size = size;
}

void dynstring_set(DynString* string, String value) {
    dynstring_reserve_capacity(string, value.size);
    memory_copy(string->data, (void*) value.data, value.size);
    string->size = value.size;
}

void dynstring_append(DynString* string, String appendix)
{
    dynstring_reserve_capacity(string, string->size + appendix.size);
    memcpy((void*)&string->data[string->size], (void*)appendix.data, appendix.size);
    string->size += appendix.size;
}

void dynstring_append(DynString* string, const char* c_string) {
    dynstring_append(string, string_make(c_string));
}

void dynstring_append(DynString* string, Identifier identifier) {
    string->append(identifier);
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
    dynstring_reserve_capacity(string, string->size + message_length + 1);

    // Add formated string at end of current string
    int ret_value = vsnprintf(string->data + string->size, string->capacity - string->size, format, args);
    assert(ret_value >= 0, "Should always work if params are correct and enough space is provided");
    string->size = string->size + message_length;

    va_end(args);
}

void dynstring_insert(DynString* string, String insertion, int position) 
{
    if (insertion.size == 0) return;

    position = math_clamp(position, 0, (int)string->size);
    int new_size = string->size + insertion.size;
    dynstring_reserve_capacity(string, new_size+1);

    // Create a hole in the string where the insertion will be placed
    memory_copy(&string->data[position + insertion.size], &string->data[position], string->size - position);
    // Fill the hole with the insertion
    memory_copy(&string->data[position], (void*) insertion.data, insertion.size);
    string->size = new_size;
}

void dynstring_prepend(DynString* string, String prependix) {
    dynstring_insert(string, prependix, 0);
}

void dynstring_add_null_terminator(DynString* string) {
    dynstring_reserve_capacity(string, string->size + 1);
    string->data[string->size] = '\0';
}

void dynstring_remove_substring(DynString* string, int start_index, int end_index) 
{
    if (string->size == 0 || end_index <= start_index) {
        return;
    }
    start_index = math_clamp(start_index, 0, (int)string->size-1);
    end_index   = math_clamp(end_index,   0, (int)string->size);
    memory_copy(&string->data[start_index], &string->data[end_index], string->size - end_index);
    string->size = string->size - (end_index - start_index);
}

void dynstring_remove_character(DynString* string, int index) {
    dynstring_remove_substring(string, index, index + 1);
}

void dynstring_replace_character(DynString* string, char to_replace, char replace_with)
{
    for (int i = 0; i < string->size; i++) {
        if (string->data[i] == to_replace) {
            string->data[i] = replace_with;
        }
    }
}

void dynstring_remove_trailing_whitespace(DynString* str) 
{
    while (str->size > 0 && char_is_whitespace(str->data[str->size - 1])) {
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

DynString* DynString::reserve_capacity(int new_capacity) {
    dynstring_reserve_capacity(this, new_capacity);
    return this;
}

DynString* DynString::reserve_space(int required_space) {
    dynstring_reserve_capacity(this, this->size + required_space);
    return this;
}

String DynString::to_string() {
    return string_make(data, size);
}

const char* DynString::to_cstring() {
    dynstring_add_null_terminator(this);
    return data;
}

char& DynString::operator[](int index) {
    assert(index >= 0 && index < size, "");
    return data[index];
}

char& DynString::at(int index) {
    assert(index >= 0 && index < size, "");
    return data[index];
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

DynString* DynString::append_in_quotation(String string) {
    this->append('"')->append(string)->append('"');
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
    dynstring_reserve_capacity(this, size + message_length + 1);

    // Add formated string at end of current string
    int ret_value = vsnprintf(data + size, capacity - size, format, args);
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

DynString* DynString::append(Identifier identifier)
{
    this->append(identifier.string);
    return this;
}
