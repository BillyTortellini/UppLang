#pragma once

#include "../../math/vectors.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "lexer.hpp"

// Handles
struct Block_Index
{
    Source_Code* code;
    int block;
};

struct Line_Index
{
    Block_Index block;
    int line;
};

struct Token_Index
{
    Line_Index line;
    int token;
};

struct Token_Range
{
    Token_Index start;
    Token_Index end;
};

struct Text_Index
{
    Line_Index line;
    int pos;
};

struct Text_Range
{
    Text_Index start;
    Text_Index end;
};




// Source Code
struct Render_Info
{
    int pos;  // x position in line including line indentation
    int line;
    int size; // length of token
    vec3 color;
    vec3 bg_color;
};

struct Source_Line
{
    String text;
    Dynamic_Array<Token> tokens;

    // Rendering
    Dynamic_Array<Render_Info> infos;
    int render_index;
    int render_indent;
    int render_start_pos;
    int render_end_pos;
};

struct Source_Block
{
    Block_Index parent;
    Dynamic_Array<Block_Index> children;
    Dynamic_Array<Source_Line> lines;
    int line_index; // Relative to parent start

    // Allocation Info
    bool valid;

    // Rendering
    int render_start;
    int render_end;
    int render_indent;
};

struct Source_Code
{
    Dynamic_Array<Source_Block> blocks;
};

Source_Code* source_code_create();
void source_code_destroy(Source_Code* code);
void source_code_reset(Source_Code* code);

// Manipulation Helpers
void source_line_destroy(Source_Line* line);
void source_block_destroy(Source_Block* block);
void source_line_insert_empty(Line_Index index);

// Utility
void source_code_fill_from_string(Source_Code* code, String text);
void source_code_append_to_string(Source_Code* code, String* text);
void source_code_tokenize(Source_Code* code);
void source_code_tokenize_line(Line_Index line_index);
void source_code_sanity_check(Source_Code* code);
bool source_block_inside_comment(Block_Index block_index);
bool source_line_is_comment(Line_Index line_index);
bool source_line_is_multi_line_comment_start(Line_Index line_index);
bool source_line_is_end_of_block(Line_Index line_index);



// Index Functions
Block_Index block_index_make(Source_Code* code, int block);
Block_Index block_index_make_root(Source_Code* code);

Line_Index line_index_make(Block_Index block, int line);
Line_Index line_index_make_root(Source_Code* code);
Line_Index line_index_make_first_in_block(Block_Index block_index);
Line_Index line_index_make_last_in_block(Block_Index block_index);

Text_Index text_index_make(Line_Index line, int pos);

Token_Index token_index_make(Line_Index line, int token);
Token_Index token_index_make_root(Source_Code* code);
Token_Index token_index_make_line_end(Line_Index index);
Token_Index token_index_make_block_start(Block_Index index);
Token_Index token_index_make_block_end(Block_Index index);

Token_Range token_range_make(Token_Index start, Token_Index end);
Token_Range token_range_make_offset(Token_Index start, int offset);
Token_Range token_range_make_block(Block_Index block_index);

Source_Block* index_value_unsafe(Block_Index index);
Source_Block* index_value(Block_Index index);
Source_Line* index_value(Line_Index index);
Token* index_value(Token_Index index);
char index_value(Text_Index index);

bool index_valid(Block_Index index);
bool index_valid(Line_Index index);
bool index_valid(Token_Index index);
bool index_valid(Text_Index index);

void index_sanitize(Block_Index* index);
void index_sanitize(Line_Index* index);
void index_sanitize(Token_Index* index);
void index_sanitize(Text_Index* index);

bool index_equal(Block_Index a, Block_Index b);
bool index_equal(Line_Index a, Line_Index b);
bool index_equal(Token_Index a, Token_Index b);
bool index_equal(Text_Index a, Text_Index b);

int index_compare(Line_Index a, Line_Index b);
int index_compare(Token_Index a, Token_Index b);

// Movement Utility
Line_Index block_get_start_line(Block_Index block_index);
Line_Index block_get_end_line(Block_Index block_index);
int block_index_get_indentation(Block_Index block);

Line_Index line_index_next(Line_Index index);
Line_Index line_index_prev(Line_Index index);
Optional<Block_Index> line_index_block_after(Line_Index index);
Optional<Block_Index> line_index_block_before(Line_Index index);
bool line_index_is_last_in_block(Line_Index index);

Token_Index token_index_next(Token_Index index);
Token_Index token_index_prev(Token_Index index);
Token_Index token_index_advance(Token_Index index, int offset);
bool token_index_is_last_in_line(Token_Index index);

bool token_range_contains(Token_Range range, Token_Index index);





