#include "source_code.hpp"

#include "../../datastructures/hashtable.hpp"
#include "compiler.hpp"
#include "../../utility/character_info.hpp"
#include "lexer.hpp"
#include <string>

void source_line_destroy(Source_Line* line);

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
    root.valid = true;
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

void source_code_fill_from_string(Source_Code* code, String text)
{
    return;
    /*
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
    */
}

void source_code_append_to_string(Source_Code* code, String* text)
{
    /*
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
    */
}

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

void source_block_check_sanity(Block_Index index)
{
    auto block = index_value(index);
    assert(block->lines.size != 0 || block->children.size != 0, "No empty blocks allowed");
    if (index.block == 0) {
        assert(block->parent.block == -1, "");
    }

    for (int i = 0; i < block->children.size; i++)
    {
        auto child_index = block->children[i];
        auto child_block = index_value(child_index);
        assert(index_equals(child_block->parent, index), "Parent/Child connections must be correct!");
        assert(child_block->line_index >= 0 && child_block->line_index <= block->lines.size, "Must be in parent line range");
        if (i + 1 < block->children.size)
        {
            auto next_index = block->children[i + 1];
            auto next_block = index_value(next_index);
            assert(next_block->line_index != child_block->line_index, "Block line numbers must be different");
            assert(next_block->line_index > child_block->line_index, "Block line numbers must be increasing");
            logg("Fuck you");
        }
        source_block_check_sanity(child_index);
    }
}

void source_code_sanity_check(Source_Code* code)
{
    assert(code->blocks.size >= 1, "Root block must exist");
    source_block_check_sanity(block_index_make_root(code));
}



// Index Functions
Source_Block* index_value_unsafe(Block_Index index) {
    // DOCS: Only use this function when sanitizing values or if you need to manipulate blocks at a low level
    return &index.code->blocks[index.block];
}

Source_Block* index_value(Block_Index index) {
    auto block = index_value_unsafe(index);
    assert(block->valid, "");
    return block;
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
    auto block = index_value_unsafe(index);
    return index.block >= 0 && index.block < blocks.size && block->valid;
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

void index_sanitize(Block_Index* index)
{
    auto& blocks = index->code->blocks;
    index->block = math_clamp(index->block, 0, blocks.size - 1);
    auto block = index_value_unsafe(*index);
    while (!block->valid) {
        index->block = block->parent.block;
        block = index_value_unsafe(*index);
    }
}

void index_sanitize(Line_Index* index)
{
    index_sanitize(&index->block);
    auto block = index_value(index->block);
    auto& lines = block->lines;
    if (lines.size == 0) {
        assert(block->children.size != 0, "");
        *index = block_get_start_line(block->children[0]);
        return;
    }
    index->line = math_clamp(index->line, 0, lines.size - 1);
}

void index_sanitize(Token_Index* index)
{
    index_sanitize(&index->line);
    auto& tokens = index_value(index->line)->tokens;
    index->token = math_clamp(index->token, 0, tokens.size);
}

void index_sanitize(Text_Index* index)
{
    index_sanitize(&index->line);
    auto& text = index_value(index->line)->text;
    index->pos = math_clamp(index->pos, 0, text.size);
}

bool index_equals(Block_Index a, Block_Index b) {
    assert(a.code == b.code, "");
    return a.block == b.block;
}

bool index_equals(Line_Index a, Line_Index b) {
    if (!index_equals(a.block, b.block)) return false;
    return a.line == b.line;
}

int index_compare(Line_Index a, Line_Index b) 
{
    assert(a.block.code == b.block.code, "");
    if (a.block.block == b.block.block) 
    {
        if (a.line == b.line) return 0;
        return a.line < b.line ? 1 : -1;
    }

    int a_indent = block_index_get_indentation(a.block);
    int b_indent = block_index_get_indentation(b.block);
    auto a_block = index_value(a.block);
    auto b_block = index_value(b.block);

    while (a_block->parent.block != b_block->parent.block) 
    {
        if (a_block->parent.block == b.block.block) {
            return a_block->line_index <= b.line ? 1 : -1;
        }
        else if (b_block->parent.block == a.block.block) {
            return b_block->line_index > a.line ? 1 : -1;
        }

        if (a_indent > b_indent) 
        {
            a = line_index_make(a_block->parent, a_block->line_index);
            a_block = index_value(a.block);
            a_indent -= 1;
        }
        else {
            b = line_index_make(b_block->parent, b_block->line_index);
            b_block = index_value(b.block);
            b_indent -= 1;
        }
    }
    panic("");
    return -2;
}



// Movement/Navigation
Line_Index block_get_start_line(Block_Index block_index)
{
    auto block = index_value(block_index);
    while (true)
    {
        if (block->children.size == 0) {
            break;
        }
        auto first_child = index_value(block->children[0]);
        if (first_child->line_index != 0) {
            break;
        }
        block_index = block->children[0];
        block = first_child;
    }

    return line_index_make(block_index, 0);
}

Line_Index block_get_end_line(Block_Index block_index)
{
    auto block = index_value(block_index);
    while (true)
    {
        if (block->children.size == 0) {
            break;
        }
        auto last_child = index_value(block->children[block->children.size - 1]);
        if (last_child->line_index != block->lines.size) {
            break;
        }
        block_index = block->children[block->children.size-1];
        block = last_child;
    }

    return line_index_make(block_index, block->lines.size-1);
}

Line_Index line_index_next(Line_Index index)
{
    // Check for blocks after current line
    auto block = index_value(index.block);
    for (int i = 0; i < block->children.size; i++) {
        auto child = index_value(block->children[i]);
        if (index.line + 1 == child->line_index) {
            return block_get_start_line(block->children[i]);
        }
    }

    // Check if at end of current block
    if (index.line + 1 < block->lines.size) {
        return line_index_make(index.block, index.line + 1);
    }

    // Check if we are last line of whole program
    if (index.block.block == 0) {
        return index;
    }

    // Step out of current block and repeat
    auto parent_block = index_value(block->parent);
    while (block->line_index == parent_block->lines.size)
    {
        if (block->parent.block == 0) {
            return index;
        }
        block = parent_block;
        parent_block = index_value(block->parent);
    }

    return line_index_make(block->parent, block->line_index);
}

Line_Index line_index_prev(Line_Index index)
{
    // Check for blocks before current line
    auto block = index_value(index.block);
    for (int i = 0; i < block->children.size; i++) {
        auto child = index_value(block->children[i]);
        if (index.line == child->line_index) {
            return block_get_end_line(block->children[i]);
        }
    }

    // Check if there is a previous line in current block
    if (index.line > 0) {
        return line_index_make(index.block, index.line - 1);
    }

    // Check if we are the first line of the program
    if (index.block.block == 0) {
        return index;
    }

    // Step out of current block
    while (block->line_index == 0)
    {
        if (block->parent.block == 0) {
            return index;
        }
        block = index_value(block->parent);
    }
    return line_index_make(block->parent, block->line_index - 1);
}

int block_index_get_indentation(Block_Index block_index) 
{
    int indentation = 0;
    while (block_index.block != 0) {
        indentation += 1;
        block_index = index_value(block_index)->parent;
    }
    return indentation;
}


