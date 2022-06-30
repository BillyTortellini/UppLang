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

struct Text_Index
{
    Line_Index line;
    int pos;
};

struct Token_Index
{
    Line_Index line;
    int token;
};

// Source Code
struct Render_Info
{
    int pos;  // x position in line including line indentation
    int line;
    int size; // length of token
    vec3 color;
};

struct Source_Line
{
    String text;
    Dynamic_Array<Token> tokens;
    Dynamic_Array<Render_Info> infos;

    // Rendering
    int render_index;
    int render_indent;
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
void source_code_tokenize_block(Block_Index index, bool recursive);
void source_code_sanity_check(Source_Code* code);

// Index Functions
Block_Index block_index_make(Source_Code* code, int block);
Block_Index block_index_make_root(Source_Code* code);
Line_Index line_index_make(Block_Index block, int line);
Token_Index token_index_make(Line_Index line, int token);
Text_Index text_index_make(Line_Index line, int pos);

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

bool index_equals(Block_Index a, Block_Index b);
bool index_equals(Line_Index a, Line_Index b);
int index_compare(Line_Index a, Line_Index b); // 1 if in order, 0 if equal, -1 if not in order

// Movement Utility
Line_Index block_get_start_line(Block_Index block_index);
Line_Index block_get_end_line(Block_Index block_index);
Line_Index line_index_next(Line_Index index);
Line_Index line_index_prev(Line_Index index);
int block_index_get_indentation(Block_Index block);






