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

Block_Index source_block_insert_empty(Block_Index parent_index, int line_index)
{
    Source_Block new_block;
    new_block.children = dynamic_array_create_empty<Block_Index>(1);
    new_block.lines = dynamic_array_create_empty<Source_Line>(1);
    new_block.line_index = line_index;
    new_block.valid = true;
    new_block.parent = parent_index;
    dynamic_array_push_back(&parent_index.code->blocks, new_block);
    auto new_block_index = block_index_make(parent_index.code, parent_index.code->blocks.size - 1);

    // Insert ordered into parent
    auto parent_block = index_value(parent_index);
    int i = 0;
    while (i < parent_block->children.size)
    {
        auto child_block = index_value(parent_block->children[i]);
        if (line_index < child_block->line_index) {
            break;
        }
        i++;
    }
    dynamic_array_insert_ordered(&parent_block->children, new_block_index, i);
    
    return new_block_index;
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

void source_block_fill_from_string(Block_Index parent_index, String text, int* text_index, int indentation)
{
    Block_Index block_index;
    if (indentation == 0) {
        assert(parent_index.block == 0, "");
        block_index = parent_index;
    }
    else {
        auto parent_block = index_value(parent_index);
        block_index = source_block_insert_empty(parent_index, parent_block->lines.size);
    }

    // Parse all lines
    auto& index = *text_index;
    while (index < text.size)
    {
        // Find indentation level
        int indent_start_index = index;
        int line_indent = 0;
        while (index < text.size && text.characters[index] == '\t') {
            line_indent += 1;
            index += 1;
        }
        if (line_indent > indentation) {
            index = indent_start_index;
            source_block_fill_from_string(block_index, text, text_index, indentation + 1);
            continue;
        }
        else if (line_indent < indentation) {
            index = indent_start_index;
            return;
        }

        // Find line end
        int line_start_index = index;
        while (index < text.size && text.characters[index] != '\n') {
            index += 1;
        }
        int line_end_index = index;
        if (index != text.size) {
            index += 1; // Skip "
        }

        auto line_index = line_index_make(block_index, index_value(block_index)->lines.size);
        source_line_insert_empty(line_index);
        String substring = string_create_substring_static(&text, line_start_index, line_end_index);
        string_append_string(&index_value(line_index)->text, &substring);
    }
}

void source_code_fill_from_string(Source_Code* code, String text)
{
    // Reset
    {
        source_code_reset(code);
        auto& root_block = code->blocks[0];
        source_line_destroy(&root_block.lines[0]);
        dynamic_array_reset(&root_block.lines);
    }

    int text_index = 0;
    source_block_fill_from_string(block_index_make_root(code), text, &text_index, 0);

    // Check if root block not empty
    {
        auto& root_block = code->blocks[0];
        if (root_block.lines.size == 0 && root_block.children.size == 0) {
            source_line_insert_empty(line_index_make(block_index_make_root(code), 0));
        }
    }
}

void source_block_append_to_string(Block_Index index, String* text, int indentation)
{
    auto block = index_value(index);
    int child_index = 0;
    for (int i = 0; i < block->lines.size; i++)
    {
        while (child_index < block->children.size)
        {
            auto next_child = index_value(block->children[child_index]);
            if (next_child->line_index == i) {
                source_block_append_to_string(block->children[child_index], text, indentation + 1);
                child_index++;
            }
            else {
                break;
            }
        }

        auto& line = block->lines[i];
        for (int j = 0; j < indentation; j++) {
            string_append_formated(text, "\t");
        }
        string_append_string(text, &line.text);
        string_append_formated(text, "\n");
    }

    if (child_index < block->children.size) {
        source_block_append_to_string(block->children[child_index], text, indentation + 1);
        assert(child_index + 1 >= block->children.size, "All must be iterated by now");
    }
}

void source_code_append_to_string(Source_Code* code, String* text)
{
    source_block_append_to_string(block_index_make_root(code), text, 0);
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
        assert(index_equal(child_block->parent, index), "Parent/Child connections must be correct!");
        assert(child_block->line_index >= 0 && child_block->line_index <= block->lines.size, "Must be in parent line range");
        if (i + 1 < block->children.size)
        {
            auto next_index = block->children[i + 1];
            auto next_block = index_value(next_index);
            assert(next_block->line_index != child_block->line_index, "Block line numbers must be different");
            assert(next_block->line_index > child_block->line_index, "Block line numbers must be increasing");
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

Line_Index line_index_make_root(Source_Code* code) {
    return line_index_make(block_index_make_root(code), 0);
}

Line_Index line_index_make_first_in_block(Block_Index block_index)
{
    auto block = index_value(block_index);
    while (true)
    {
        if (block->children.size != 0) {
            auto first_index = block->children[0];
            auto first_block = index_value(first_index);
            if (first_block->line_index == 0) {
                block_index = first_index;
                block = first_block;
                continue;
            }
        }
        break;
    }
    assert(block->lines.size > 0, "Hey");
    return line_index_make(block_index, 0);

}

Line_Index line_index_make_last_in_block(Block_Index block_index)
{
    auto block = index_value(block_index);
    while (true)
    {
        if (block->children.size != 0) {
            auto last_index = dynamic_array_last(&block->children);
            auto last_block = index_value(last_index);
            if (last_block->line_index == block->lines.size) {
                block_index = last_index;
                block = last_block;
                continue;
            }
        }
        break;
    }
    assert(block->lines.size > 0, "Hey");
    return line_index_make(block_index, block->lines.size - 1);
}

Text_Index text_index_make(Line_Index line, int pos)
{
    Text_Index index;
    index.line = line;
    index.pos = pos;
    return index;
}

Token_Index token_index_make(Line_Index line, int token) {
    Token_Index index;
    index.line = line;
    index.token = token;
    return index;
}

Token_Index token_index_make_root(Source_Code* code) {
    return token_index_make(line_index_make_root(code), 0);
}

Token_Index token_index_make_line_end(Line_Index index) {
    auto line = index_value(index);
    return token_index_make(index, line->tokens.size);
}

Token_Index token_index_make_block_start(Block_Index index) {
    return token_index_make(line_index_make_first_in_block(index), 0);
}

Token_Index token_index_make_block_end(Block_Index index) {
    return token_index_make_line_end(line_index_make_last_in_block(index));
}

Token_Range token_range_make(Token_Index start, Token_Index end) {
    Token_Range range;
    range.start = start;
    range.end = end;
    return range;
}

Token_Range token_range_make_offset(Token_Index start, int offset) {
    Token_Range range;
    range.start = start;
    range.end = token_index_advance(start, offset);
    return range;
}

Token_Range token_range_make_block(Block_Index block_index) {
    return token_range_make(token_index_make_block_start(block_index), token_index_make_block_end(block_index));
}

bool index_valid(Block_Index index)
{
    auto& blocks = index.code->blocks;
    auto block = index_value_unsafe(index);
    return index.block >= 0 && index.block < blocks.size&& block->valid;
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

bool index_equal(Block_Index a, Block_Index b)
{
    assert(a.code == b.code, "");
    return a.block == b.block;
}

bool index_equal(Line_Index a, Line_Index b)
{
    if (!index_equal(a.block, b.block)) return false;
    return a.line == b.line;
}

bool index_equal(Token_Index a, Token_Index b)
{
    if (!index_equal(a.line, b.line)) return false;
    return a.token == b.token;
}

bool index_equal(Text_Index a, Text_Index b)
{
    if (!index_equal(a.line, b.line)) return false;
    return a.pos == b.pos;
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
    return a.line < b.line ? 1 : -1;
}

int index_compare(Token_Index a, Token_Index b)
{
    int line_cmp = index_compare(a.line, b.line);
    if (line_cmp != 0) {
        return line_cmp;
    }

    if (a.token < b.token) {
        return 1;
    }
    else if (a.token == b.token) {
        return 0;
    }
    else {
        return -1;
    }
}

bool token_range_contains(Token_Range range, Token_Index index) 
{
    // INFO: This function is not a simple compare anymore because we also want to handle
    //        1. Ranges with size 0
    //        2. Ranges at the end of a line
    if (index_compare(range.start, range.end) == 0 && index_compare(range.start.line, index.line) == 0) {
        auto& tokens = index_value(index.line)->tokens;
        if (token_index_is_last_in_line(range.start)) {
            return index.token >= tokens.size - 1;
        }
        return index.token == range.start.token;
    }
    if (token_index_is_last_in_line(range.end)) {
        range.end = token_index_advance(range.end, 1);
    }
    return index_compare(range.start, index) >= 0 && index_compare(index, range.end) > 0;
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
        block_index = block->children[block->children.size - 1];
        block = last_child;
    }

    return line_index_make(block_index, block->lines.size - 1);
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

Optional<Block_Index> line_index_block_before(Line_Index index)
{
    auto block = index_value(index.block);
    for (int i = 0; i < block->children.size; i++) {
        auto child_index = block->children[i];
        auto child_block = index_value(child_index);
        if (index.line == child_block->line_index) {
            return optional_make_success(child_index);
        }
    }
    return optional_make_failure<Block_Index>();
}

Optional<Block_Index> line_index_block_after(Line_Index index)
{
    auto block = index_value(index.block);
    for (int i = 0; i < block->children.size; i++) {
        auto child_index = block->children[i];
        auto child_block = index_value(child_index);
        if (index.line + 1 == child_block->line_index) {
            return optional_make_success(child_index);
        }
    }
    return optional_make_failure<Block_Index>();
}

bool line_index_is_last_in_block(Line_Index index)
{
    auto block = index_value(index.block);
    return index.line >= block->lines.size - 1;
}

Token_Index token_index_advance(Token_Index index, int offset)
{
    index.token += offset;
    return index;
}

Token_Index token_index_next(Token_Index index) {
    return token_index_advance(index, 1);
}

Token_Index token_index_prev(Token_Index index) {
    return token_index_advance(index, -1);
}

bool token_index_is_last_in_line(Token_Index index)
{
    auto line = index_value(index.line);
    assert(index.token <= line->tokens.size, "");
    return index.token == line->tokens.size;
}


