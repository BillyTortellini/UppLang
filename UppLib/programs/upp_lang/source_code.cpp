#include "source_code.hpp"

#include "../../datastructures/hashtable.hpp"
#include "compiler.hpp"
#include "../../utility/character_info.hpp"
#include "lexer.hpp"
#include <string>

void source_line_destroy(Source_Line* line);



// Source Code
void source_line_destroy(Source_Line* line) {
    string_destroy(&line->text);
    dynamic_array_destroy(&line->tokens);
    dynamic_array_destroy(&line->infos);
}

void source_code_insert_line_empty(Source_Code* code, int line_index, int indentation)
{
    Source_Line line;
    line.indentation = indentation;
    line.text = string_create_empty(4);
    line.tokens = dynamic_array_create_empty<Token>(1);
    line.infos = dynamic_array_create_empty<Render_Info>(1);
    dynamic_array_insert_ordered(&code->lines, line, line_index);
}

void source_block_destroy(Source_Block* block)
{
    dynamic_array_destroy(&block->child_blocks);
}

Source_Code source_code_create()
{
    Source_Code result;
    result.lines = dynamic_array_create_empty<Source_Line>(1);
    result.blocks = dynamic_array_create_empty<Source_Block>(1);
    source_code_reset(&result);
    return result;
}

void source_code_reset(Source_Code* code)
{
    for (int i = 0; i < code->lines.size; i++) {
        source_line_destroy(&code->lines[i]);
    }
    dynamic_array_reset(&code->lines);
    source_code_insert_line_empty(code, 0, 0);

    for (int i = 0; i < code->blocks.size; i++) {
        source_block_destroy(&code->blocks[i]);
    }
    dynamic_array_reset(&code->blocks);

    Source_Block block;
    block.child_blocks = dynamic_array_create_empty<int>(1);
    block.indentation = 0;
    block.line_count = 1;
    block.line_offset = 0;
    block.parent_index = -1;
    dynamic_array_push_back(&code->blocks, block);
}

void source_code_destroy(Source_Code* code)
{
    for (int i = 0; i < code->lines.size; i++) {
        source_line_destroy(&code->lines[i]);
    }
    dynamic_array_destroy(&code->lines);
}

void source_code_fill_from_string(Source_Code* code, String text)
{
    // Get all characters into the string
    int index = 0;
    source_code_reset(code);
    if (text.size == 0) {
        source_code_insert_line_empty(code, 0, 0);
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

void source_code_tokenize_all(Source_Code* code)
{
    for (int i = 0; i < code->lines.size; i++)
    {
        auto& line = code->lines[i];
        lexer_tokenize_text(line.text, &line.tokens);
    }
}

void source_code_reconstruct_block(Source_Code* code, int& index, int indentation_level, int parent_index, int parent_start_line)
{
    // Add block
    int block_index = code->blocks.size;
    int start_index = index;
    {
        Source_Block block;
        block.child_blocks = dynamic_array_create_empty<int>(1);
        block.parent_index = parent_index;
        block.indentation = indentation_level;
        block.line_count = 0;
        block.line_offset = index - parent_start_line;
        dynamic_array_push_back(&code->blocks, block);
        if (parent_index != -1) {
            dynamic_array_push_back(&code->blocks[parent_index].child_blocks, block_index);
        }
    }

    while (index < code->lines.size)
    {
        auto& line = code->lines[index];
        if (line.indentation > indentation_level) {
            source_code_reconstruct_block(code, index, indentation_level + 1, block_index, start_index);
        }
        else if (line.indentation < indentation_level) {
            break;
        }
        else {
            index += 1;
        }
    }
    code->blocks[block_index].line_count = index - start_index;
    assert(index - start_index > 0, "Cannot have blocks with 0 lines!");
}

void source_code_reconstruct_blocks(Source_Code* code)
{
    int index = 0;
    for (int i = 0; i < code->blocks.size; i++) {
        source_block_destroy(&code->blocks[i]);
    }
    dynamic_array_reset(&code->blocks);
    source_code_reconstruct_block(code, index, 0, -1, 0);
}




