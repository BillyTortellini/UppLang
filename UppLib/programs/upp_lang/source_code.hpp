#pragma once

#include "../../math/vectors.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "lexer.hpp"

struct Module_Progress;
struct Source_Code;
struct Source_Line;
namespace AST
{
    struct Node;
    struct Module;
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



// Source Code
struct Source_Line
{
    // Content
    String text;
    Dynamic_Array<Token> tokens;
    int indentation;

    // Comment/Lexing info
    bool is_comment; // True if we are inside a comment block or if the line starts with //
    // If it is a line of a comment block, the indention of the block, -1 if not a comment block line
    // If this line is the start of the comment block, then -1 (Execpt in hierarchical comment stacks)
    // Also for stacked/hierarchical comment blocks, this is the lowest indentation (And always > 0)
    int comment_block_indentation;  

    bool is_folded;
    int fold_index;
    int on_screen_index; // May not be visible depending on cam_start/end
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

    // Origin info
    String file_path;
    bool used_in_last_compile; // All files that weren't used in last compile + aren't open in editor are removed
    bool open_in_editor;
    bool code_changed_since_last_compile;

    // Analysis-Info
    AST::Module* root;
    Dynamic_Array<AST::Node*> allocated_nodes;
    Dynamic_Array<Error_Message> error_messages; // Parsing errors, not semantic-errors!
    Module_Progress* module_progress; // Analysis progress, may be 0 if not analysed yet
};

Source_Code* source_code_create(String file_path, bool used_in_last_compile, bool open_in_editor); // Takes ownership of file-path
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
void source_code_tokenize(Source_Code* code);
void source_code_tokenize_line(Source_Code* code, int line_index);
void source_code_tokenize_line(Source_Line* line);

void source_code_sanity_check(Source_Code* code);
bool source_line_is_comment(Source_Line* line);
bool source_line_is_multi_line_comment_start(Source_Line* line);

// Index Functions
int source_code_get_line_bundle_index(Source_Code* code, int line_index);
Source_Line* source_code_get_line(Source_Code* code, int line_index);




