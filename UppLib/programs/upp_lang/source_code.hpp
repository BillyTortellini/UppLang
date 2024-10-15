#pragma once

#include "../../math/vectors.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "lexer.hpp"

// Source Code
struct Render_Info
{
    vec3 color;
    vec3 bg_color;
};

struct Source_Line
{
    // Content
    String text;
    Dynamic_Array<Token> tokens;
    int indentation;

    // Rendering
    Dynamic_Array<Render_Info> infos;
    int screen_index; // E.g. 0 for first line on screen

    // Comment/Lexing info
    bool is_comment; // True if we are inside a comment block or if the line starts with //
    // If it is a line of a comment block, the indention of the block, -1 if not a comment block line
    // If this line is the start of the comment block, then -1 (Execpt in hierarchical comment stacks)
    // Also for stacked/hierarchical comment blocks, this is the lowest indentation (And always > 0)
    int comment_block_indentation;  
};

struct Line_Bundle
{
    Dynamic_Array<Source_Line> lines;
    int first_line_index;
};

struct Source_Code
{
    // Note: Source_Code always contains at least 1 line + 1 bundle (So that index 0 is always valid)
    Dynamic_Array<Line_Bundle> bundles;
    int line_count;
};

Source_Code* source_code_create();
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



