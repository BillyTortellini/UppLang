#pragma once

#include "../utility/utils.hpp"
#include "../datastructures/array.hpp"

struct Arena;

struct String;
struct DynString;
struct String_List;
struct Allocator_Base;



// String
// Strings are immutable views of strings, which may not be null-terminated
struct String
{
    const char* characters;
    int size;
};

String string_make(const char* c_string);
String string_make(char* characters, int size);
String string_make_substring(String other, int start, int end_exclusive);

bool string_equals(String a, String b);
bool string_in_order(String a, String b);

bool string_compare_substring(String string, int start_index, String other);
int string_contains_substring(String string, String substring, int start_index = 0); // -1 if not available
bool string_starts_with(String str, String start);
bool string_ends_with(String str, String end);

bool string_contains_character(String string, char character);
bool string_contains_only_characters_in_set(String string, String set, bool use_set_complement);
bool string_test_char(String str, int char_index, char c);

Optional<int> string_find_character_index(String string, char c, int start_position);
Optional<int> string_find_character_index_reverse(String string, char character, int startpos);
Optional<float> string_parse_float(String string);
Optional<int> string_parse_int(String string);
Optional<i64> string_parse_i64(String string);
Optional<i64> string_parse_i64_hex(String string);

Array<String> string_split(String string, char c, Arena* arena); 



// String_List
const int STRING_LIST_NODE_BUFFER_SIZE = 32;
const int STRING_LIST_SMALL_STRING_SIZE = 8;
struct String_List_Node
{
    char buffer[STRING_LIST_NODE_BUFFER_SIZE];
    String_List_Node* next;
};

// A string list with <= SMALL_STRING_SIZE characters does not allocate any memowy
struct String_List
{
    Allocator_Base* allocator;
    int size; // String size in bytes
    union {
        String_List_Node* head;
        char small_string_buffer[STRING_LIST_SMALL_STRING_SIZE];
    } options;

    static String_List create_empty(Allocator_Base* allocator);
    static String_List create(Allocator_Base* allocator, String string);
    void destroy();

    void reset(); // Sets string to empty string (Frees all allocated blocks)
    void set(String string);
    void append_to_dynstring(DynString* output);
};




// DynString
struct DynString
{
    char* characters;
    int size;
    int capacity;
    Arena* arena;

    static DynString create(Arena* arena);
    static DynString create_copy(String string, Arena* arena);
    
    DynString* append(const char* text);
    DynString* append(DynString str);
    DynString* append(DynString* str);
    DynString* append(char c);
    DynString* append(String string);
    DynString* append(String* string);
    DynString* append_formated(const char* format, ...);
    DynString* append(String_List string_list);

    DynString* reset();
    void reserve(int new_capacity);
    String to_string();
    const char* to_cstring(); // Adds nullterminator if none available
};

DynString dynstring_create(Arena* arena);
DynString dynstring_create_copy(String string, Arena* arena);

String dynstring_to_string(DynString string);
void dynstring_reserve(DynString* string, int new_capacity);
void dynstring_reset(DynString* string);
void dynstring_truncate(DynString* string, int size);
void dynstring_set(DynString* string, String set_to);

void dynstring_append(DynString* string, String appendix);
void dynstring_append_formated(DynString* string, const char* format, ...);
void dynstring_append_character(DynString* string, char c);
void dynstring_prepend(DynString* string, String prependix);
void dynstring_insert(DynString* string, String insertion, int position);
void dynstring_add_null_terminator(DynString* string);

void dynstring_remove_character(DynString* string, int index);
void dynstring_remove_substring(DynString* string, int start_index, int end_index_exclusive);
void dynstring_replace_character(DynString* string, char to_replace, char replace_with);
void dynstring_remove_trailing_whitespace(DynString* str);

bool dynstring_read_line_from_console(DynString* to_fill); // Returns true if EOF was encountered
