#pragma once

#include "../../math/vectors.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "lexer.hpp"

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
    int indentation;

    Token_Position token_start_pos; // TODO: Remove with token block
};

struct Source_Block
{
    int parent_index;
    int line_offset; // Relative to parent start
    int block_size;  // Does not include child-block lines
    int indentation;
    Dynamic_Array<int> child_blocks;
};

struct Source_Code
{
    Dynamic_Array<Source_Line> lines;
    Dynamic_Array<Source_Block> blocks;
};

Source_Code source_code_create();
void source_code_destroy(Source_Code* code);
void source_code_reset(Source_Code* code);

void source_code_fill_from_string(Source_Code* code, String text);
void source_code_append_to_string(Source_Code* code, String* text);
void source_code_tokenize_all(Source_Code* code);
void source_code_reconstruct_blocks(Source_Code* code);
