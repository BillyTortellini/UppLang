#pragma once

#include "../../math/vectors.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "lexer.hpp"
#include "compiler_misc.hpp"

struct Module_Progress;
struct Source_Code;
struct Source_Line;
struct Parameter_Matching_Info;
struct Symbol;
struct Symbol_Table;
struct Compilation_Unit;
struct Datatype;
struct Analysis_Pass;
struct Datatype_Enum;
struct Compilation_Unit;
struct Identifier_Pool_Lock;
struct Expression_Info;

namespace AST
{
    struct Symbol_Lookup;
    struct Node;
    struct Module;
    struct Expression;
    struct Arguments;
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

struct Token_Index
{
    int line;
    int token;
};

struct Token_Range
{
    Token_Index start;
    Token_Index end;
};

Token_Index token_index_make(int line, int token);
Token_Index token_index_make_line_end(Source_Code* code, int line_index);
Token_Range token_range_make(Token_Index start, Token_Index end);
Token_Range token_range_make_offset(Token_Index start, int offset);

bool token_index_valid(Token_Index index, Source_Code* code);
bool token_index_equal(Token_Index a, Token_Index b);
int token_index_compare(Token_Index a, Token_Index b);
bool token_range_contains(Token_Range range, Token_Index index);



// Analysis Info
enum class Member_Access_Type
{
    STRUCT_MEMBER_ACCESS, // Includes subtype and tag access
    STRUCT_POLYMORHPIC_PARAMETER_ACCESS,
    ENUM_MEMBER_ACCESS,
    DOT_CALL_AS_MEMBER,
    DOT_CALL,
    OPTIONAL_PTR_ACCESS,
    STRUCT_SUBTYPE, // Generates a type, e.g. x: Node.Expression
    STRUCT_UP_OR_DOWNCAST, // a: Node, a.Expression.something --> The .Expression is a downcast
};

enum class Code_Analysis_Item_Type
{
    EXPRESSION_INFO,
    SYMBOL_LOOKUP,
    CALL_INFORMATION,
    ARGUMENT,
    MARKUP,
    ERROR_ITEM
};

struct Code_Analysis_Item_Symbol_Info{
    Symbol* symbol;
    bool is_definition;
    Analysis_Pass* pass;
    AST::Symbol_Lookup* lookup;
};

struct Code_Analysis_Item_Expression{
    AST::Expression* expr;
    Expression_Info* info;
    Datatype* member_access_value_type; // Since the Editor cannot query Analysis-Pass, we store member-access infos here...
};

struct Code_Analysis_Item_Call_Info {
    Parameter_Matching_Info* matching_info;
    AST::Arguments* arguments;
};

struct Code_Analysis_Argument_Info {
    Parameter_Matching_Info* matching_info;
    int argument_index;
};

union Code_Analysis_Item_Option
{
    Code_Analysis_Item_Option() {};
    Code_Analysis_Item_Expression expression;
    Code_Analysis_Item_Symbol_Info symbol_info;
    Code_Analysis_Item_Call_Info call_info;
    Code_Analysis_Argument_Info argument_info;
    vec3 markup_color;
    int error_index;
};

struct Code_Analysis_Item
{
    Code_Analysis_Item_Type type;
    int start_char;
    int end_char;
    int tree_depth;
    Code_Analysis_Item_Option options;
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
    // Content
    String text;
    int indentation;

    // Analysis Info
    Dynamic_Array<Token> tokens;
    Dynamic_Array<Code_Analysis_Item> item_infos;

    // Comment/Lexing info
    bool is_comment; // True if we are inside a comment block or if the line starts with //
    // If it is a line of a comment block, the indention of the block, -1 if not a comment block line
    // If this line is the start of the comment block, then -1 (Except in hierarchical comment stacks)
    // Also for stacked/hierarchical comment blocks, this is the lowest indentation (And always > 0)
    int comment_block_indentation;  

    // Fold information
    bool is_folded;
    int fold_index;
    int visible_index; // Index including folds
};

struct Line_Bundle
{
    Dynamic_Array<Source_Line> lines;
    int first_line_index;
};

struct Error_Message
{
    const char* msg;
    Token_Range range;
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
void source_code_destroy(Source_Code* code);
void source_code_reset(Source_Code* code);
void source_code_print_bundles(Source_Code* code);

// Manipulation Helpers
Source_Line* source_code_insert_line(Source_Code* code, int new_line_index, int indentation);
void source_code_remove_line(Source_Code* code, int line_index);

// Utility
void source_code_fill_from_string(Source_Code* code, String text);
void source_code_append_to_string(Source_Code* code, String* text);

void update_line_block_comment_information(Source_Code* code, int line_index);
void source_code_tokenize(Source_Code* code, Identifier_Pool_Lock* pool_lock);
void source_code_tokenize_line(Source_Line* line, Identifier_Pool_Lock* pool_lock);

void source_code_sanity_check(Source_Code* code);
bool source_line_is_comment(Source_Line* line);
bool source_line_is_multi_line_comment_start(Source_Line* line);

// Index Functions
int source_code_get_line_bundle_index(Source_Code* code, int line_index);
Source_Line* source_code_get_line(Source_Code* code, int line_index);

Text_Range token_range_to_text_range(Token_Range range, Source_Code* code);
Token_Range text_range_to_token_range(Text_Range range, Source_Code* code);
Text_Index token_index_to_text_index(Token_Index index, Source_Code* code, bool token_start);




