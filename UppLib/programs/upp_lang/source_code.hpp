#pragma once

#include "../../math/vectors.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "compiler_misc.hpp"

struct Source_Code;
struct Source_Line;
struct Symbol;
struct Symbol_Table;
struct Compilation_Unit;
struct Datatype;
struct Analysis_Pass;
struct Datatype_Enum;
struct Expression_Info;

namespace AST
{
    struct Node;
    struct Expression;
    struct Call_Node;
}

// Indices
struct Text_Index
{
    int line;
    int character;
};

struct Text_Range
{
    Text_Index start;
    Text_Index end;
};

Text_Index text_index_make(int line, int character);
Text_Index text_index_make_line_end(Source_Code* code, int line);
bool text_index_equal(const Text_Index& a, const Text_Index& b);
bool text_index_in_order(const Text_Index& a, const Text_Index& b);
Text_Range text_range_make(Text_Index start, Text_Index end);
bool text_range_contains(Text_Range range, Text_Index index);



// Analysis Info
struct Editor_Info_Reference
{
    int start_char;
    int end_char;
    int tree_depth;
    int editor_info_mapping_start_index;
    int editor_info_mapping_count;
    int item_index;
};

struct Compiler_Error_Info
{
    const char* message;
    Compilation_Unit* unit;
    Text_Index text_index; // For goto-error
    int semantic_error_index; // -1 if parsing error
};

struct Symbol_Table_Range
{
    Text_Range range;
    Analysis_Pass* pass;
    Symbol_Table* symbol_table;
    int tree_depth;
};

struct Block_ID_Range
{
    Text_Range range;
    String* block_id;
    int tree_depth;
};



// Source Code
struct Source_Line
{
    String text;
    Dynamic_Array<Editor_Info_Reference> item_infos;
};

struct Line_Bundle
{
    Dynamic_Array<Source_Line> lines;
    int first_line_index;
};

struct Error_Message
{
    const char* msg;
    Text_Range range;
};

struct Source_Code
{
    // Note: Source_Code always contains at least 1 line + 1 bundle (So that index 0 is always valid)
    Dynamic_Array<Line_Bundle> bundles;
    int line_count;

    // Analysis info
    Symbol_Table* root_table;
    Dynamic_Array<Symbol_Table_Range> symbol_table_ranges;
    Dynamic_Array<Block_ID_Range> block_id_range;
};

Source_Code* source_code_create(); 
Source_Code* source_code_copy(Source_Code* copy_from); 
Source_Code* source_code_load_from_file(String filepath);
void source_code_destroy(Source_Code* code);
void source_code_reset(Source_Code* code);
void source_code_print_bundles(Source_Code* code);

// Manipulation Helpers
Source_Line* source_code_insert_line(Source_Code* code, int new_line_index);
void source_code_remove_line(Source_Code* code, int line_index);

// Utility
void source_code_fill_from_string(Source_Code* code, String text);
void source_code_append_to_string(Source_Code* code, String* text);

// Index Functions
int source_code_get_line_bundle_index(Source_Code* code, int line_index);
Source_Line* source_code_get_line(Source_Code* code, int line_index);
Source_Line* source_code_get_line(Source_Code* code, int line_index, int& nearby_bundle_index);




