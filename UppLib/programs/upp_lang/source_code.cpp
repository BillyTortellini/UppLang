#include "source_code.hpp"

#include "../../datastructures/hashtable.hpp"
#include "compiler.hpp"
#include "../../utility/character_info.hpp"
#include "lexer.hpp"
#include <string>

void source_line_destroy(Source_Line* line);

// Handle Helpers
Source_Block* index_value(Block_Index index) {
    return &index.code->blocks[index.block];
}

Source_Line* index_value(Line_Index index) {
    return &index_value(index.block)->lines[index.line];
}

Token* index_value(Token_Index index) {
    return &index_value(index.line)->tokens[index.token];
}

char index_value(Text_Index index) {
    auto text = index_value(index.line)->text;
    if (index.pos >= text.size) return '\0';
    return text[index.pos];
}

Block_Index block_index_make(Source_Code* code, int block) {
    Block_Index index;
    index.code = code;
    index.block = block;
    return index;
}

Block_Index block_index_make_root(Source_Code* code) {
    return block_index_make(code, 0);
}

Line_Index line_index_make(Block_Index block, int line) {
    Line_Index index;
    index.block = block;
    index.line = line;
    return index;
}

Token_Index token_index_make(Line_Index line, int token) {
    Token_Index index;
    index.line = line;
    index.token = token;
    return index;
}

Text_Index text_index_make(Line_Index line, int pos)
{
    Text_Index index;
    index.line = line;
    index.pos = pos;
    return index;
}

bool index_valid(Block_Index index)
{
    auto& blocks = index.code->blocks;
    return index.block >= 0 && index.block < blocks.size;
}

bool index_valid(Line_Index index)
{
    if (!index_valid(index.block)) return false;
    auto& lines = index_value(index.block)->lines;
    return index.line >= 0 && index.line < lines.size;
}

bool index_valid(Token_Index index)
{
    if (!index_valid(index.line)) return false;
    auto& tokens = index_value(index.line)->tokens;
    return index.token >= 0 && index.token <= tokens.size; // Here <= because a token index == tokens.size may be used in range to indicate line end
}

bool index_valid(Text_Index index)
{
    if (!index_valid(index.line)) return false;
    auto& text = index_value(index.line)->text;
    return index.pos >= 0 && index.pos <= text.size; // Here <= because a token index == tokens.size may be used in range to indicate line end
}



// Blocks and Lines
void source_line_insert_empty(Line_Index index)
{
    Source_Line line;
    line.text = string_create_empty(4);
    line.tokens = dynamic_array_create_empty<Token>(1);
    line.infos = dynamic_array_create_empty<Render_Info>(1);
    auto block = index_value(index.block);
    dynamic_array_insert_ordered(&block->lines, line, index.line);
}

void source_line_destroy(Source_Line* line) {
    string_destroy(&line->text);
    dynamic_array_destroy(&line->tokens);
    dynamic_array_destroy(&line->infos);
}

void source_block_destroy(Source_Block* block)
{
    for (int i = 0; i < block->lines.size; i++) {
        source_line_destroy(&block->lines[i]);
    }
    dynamic_array_destroy(&block->lines);
    dynamic_array_destroy(&block->children);
}



// Source Code
Source_Code* source_code_create()
{
    Source_Code* result = new Source_Code;
    result->blocks = dynamic_array_create_empty<Source_Block>(1);
    source_code_reset(result);
    return result;
}

void source_code_reset(Source_Code* code)
{
    for (int i = 0; i < code->blocks.size; i++) {
        source_block_destroy(&code->blocks[i]);
    }
    dynamic_array_reset(&code->blocks);

    // Insert root block
    Source_Block root;
    root.children = dynamic_array_create_empty<Block_Index>(1);
    root.lines = dynamic_array_create_empty<Source_Line>(1);
    root.line_index = 0;
    root.parent = block_index_make(code, -1);
    dynamic_array_push_back(&code->blocks, root);

    // Insert root line
    source_line_insert_empty(line_index_make(block_index_make_root(code), 0));
}

void source_code_destroy(Source_Code* code)
{
    for (int i = 0; i < code->blocks.size; i++) {
        source_block_destroy(&code->blocks[i]);
    }
    dynamic_array_destroy(&code->blocks);
}

/*
void source_code_fill_from_string(Source_Code* code, String text)
{
    // Get all characters into the string
    int index = 0;
    source_code_reset(code);
    if (text.size == 0) {
        source_line_insert_empty(code, 0, 0);
        return;
    }

    // Parse all lines
    while (index < text.size)
    {
        // Find indentation level
        int line_indent = 0;
        while (index < text.size && text.characters[index] == '\t') {
            line_indent += 1;
            index += 1;
        }
        // Find line end
        int line_start_index = index;
        int line_end_index = index;
        while (true)
        {
            if (index >= text.size) {
                line_end_index = index;
                break;
            }
            char c = text.characters[index];
            if (c == '\n') {
                line_end_index = index;
                index += 1;
                break;
            }
            if (c == '\t' || c == '\r') {
                index += 1;
                continue;
            }
            index += 1;
        }

        {
            Source_Line line;
            line.indentation = line_indent;
            line.text = string_create_substring(&text, line_start_index, line_end_index);
            line.tokens = dynamic_array_create_empty<Token>(1);
            line.infos = dynamic_array_create_empty<Render_Info>(1);
            dynamic_array_push_back(&code->lines, line);
        }
    }
}

void source_code_append_to_string(Source_Code* code, String* text)
{
    for (int i = 0; i < code->lines.size; i++)
    {
        auto& line = code->lines[i];
        for (int j = 0; j < line.indentation; j++) {
            string_append_formated(text, "\t");
        }
        string_append_string(text, &line.text);
        if (i != code->lines.size - 1) {
            string_append_formated(text, "\n");
        }
    }
}
*/

void source_code_tokenize_block(Block_Index index, bool recursive)
{
    auto block = index_value(index);
    for (int i = 0; i < block->lines.size; i++) {
        auto& line = block->lines[i];
        lexer_tokenize_text(line.text, &line.tokens);
    }
    if (!recursive) return;
    for (int i = 0; i < block->children.size; i++) {
        source_code_tokenize_block(block->children[i], recursive);
    }
}




