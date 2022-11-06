#pragma once

#include "../../math/vectors.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "lexer.hpp"

// Handles
struct Block_Index
{
    Source_Code* code;
    int block_index;
};

struct Line_Index
{
    Block_Index block_index;
    int line_index;
};

struct Token_Index
{
    Line_Index line_index;
    int token;
};

struct Token_Range
{
    Token_Index start;
    Token_Index end;
};

struct Text_Index
{
    Line_Index line_index;
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
    int pos;  // x position in line_index including line_index indentation
    int line_index;
    int size; // length of token
    vec3 color;
    vec3 bg_color;
};

struct Source_Text
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

struct Source_Line
{
    bool is_block_reference;
    union {
        Block_Index block_index;
        Source_Text text;
    } options;
};

struct Source_Block
{
    Block_Index parent;
    Dynamic_Array<Source_Line> lines;

    // Allocation Info
    bool valid;

    // Rendering
    int render_start;
    int render_end;
    int render_indent;
};

struct Source_Code
{
    // Note: Blocks are stored with a free list, so that undo/redo can recreate the exact same block indices
    Dynamic_Array<Source_Block> block_buffer; // Block 0 is _always_ the root block
    Dynamic_Array<int> free_blocks; // Free list for blocks
};

Source_Code* source_code_create();
void source_code_destroy(Source_Code* code);
void source_code_reset(Source_Code* code);

// Manipulation Helpers
void source_line_destroy(Source_Line* line_index);
void source_block_destroy(Source_Block* block);
void source_block_insert_line(Line_Index line_index);
Block_Index source_block_insert_empty_block(Line_Index line_index);
void source_code_remove_empty_block(Block_Index block_index);

// Utility
void source_code_fill_from_string(Source_Code* code, String text);
void source_code_append_to_string(Source_Code* code, String* text);
void source_code_tokenize(Source_Code* code);
void source_code_tokenize_line(Line_Index line_index);
void source_code_sanity_check(Source_Code* code);
bool source_block_inside_comment(Block_Index block_index);
bool source_line_is_comment(Line_Index line_index);
bool source_line_is_multi_line_comment_start(Line_Index line_index);
bool source_index_is_end_of_line(Line_Index line_index);

// Index Functions
Block_Index block_index_make(Source_Code* code, int block_index);
Block_Index block_index_make_root(Source_Code* code);
Line_Index block_index_to_line_index(Block_Index block_index);

Line_Index line_index_make(Block_Index block_index, int line_index);
Line_Index line_index_make_root(Source_Code* code);

Text_Index text_index_make(Line_Index line_index, int pos);

Token_Index token_index_make(Line_Index line_index, int token);
Token_Index token_index_make_root(Source_Code* code);
Token_Index token_index_make_line_start(Line_Index index);
Token_Index token_index_make_line_end(Line_Index index);
Token_Index token_index_make_block_start(Block_Index index);
Token_Index token_index_make_block_end(Block_Index index);

Token_Range token_range_make(Token_Index start, Token_Index end);
Token_Range token_range_make_offset(Token_Index start, int offset);
Token_Range token_range_make_block(Block_Index block_index);

Source_Block* index_value_unsafe(Block_Index index);
Source_Block* index_value(Block_Index index);
Source_Line* index_value(Line_Index index);
Source_Text* index_value_text(Line_Index index);
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
Line_Index block_get_first_text_line(Block_Index block_index);
Line_Index block_get_last_text_line(Block_Index block_index);
int block_index_get_indentation(Block_Index block_index);

Line_Index line_index_next(Line_Index index);
Line_Index line_index_prev(Line_Index index);
Optional<Block_Index> line_index_block_after(Line_Index index);
bool line_index_is_end_of_block(Line_Index index);

Token_Index token_index_next(Token_Index index);
Token_Index token_index_prev(Token_Index index);
Token_Index token_index_advance(Token_Index index, int offset);
bool token_index_is_end_of_line(Token_Index index);

bool token_range_contains(Token_Range range, Token_Index index);





