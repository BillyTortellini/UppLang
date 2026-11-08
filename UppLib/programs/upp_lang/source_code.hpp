#pragma once

#include "../../math/vectors.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "../../utility/identifier_pool.hpp"
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

// Indices
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
    Identifier block_id;
    int tree_depth;
};



// Source Code
struct Source_Line
{
    String_List text;
    List<Editor_Info_Reference> item_infos;
};

struct Source_Code
{
    Arena arena;
    Free_List free_list;
    DynArray<Source_Line> lines;

    // Analysis info
    Symbol_Table* root_table;
    DynArray<Symbol_Table_Range> symbol_table_ranges;
    DynArray<Block_ID_Range> block_id_range;

    int line_count();
};

Source_Code* source_code_create(); 
Source_Code* source_code_copy(Source_Code* copy_from); 
Source_Code* source_code_load_from_file(String filepath);
void source_code_destroy(Source_Code* code);
void source_code_reset(Source_Code* code);

// Manipulation Helpers
void source_code_insert_line(Source_Code* code, int new_line_index);
void source_code_remove_line(Source_Code* code, int line_index);

// Utility
void source_code_fill_from_string(Source_Code* code, String text);
void source_code_append_to_string(Source_Code* code, DynString* text);

// Index Functions
void source_code_line_set_text(Source_Code* code, int line_index, String text);
void source_code_line_append_to_string(Source_Code* code, int line_index, DynString* append_to);
int  source_code_line_get_size(Source_Code* code, int line_index);
void source_code_line_get_items(Source_Code* code, int line_index, DynArray<Editor_Info_Reference>* append_to);
void source_code_line_set_items(Source_Code* code, int line_index, Array<Editor_Info_Reference> editor_infos);




