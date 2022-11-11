#include "source_code.hpp"

#include "../../datastructures/hashtable.hpp"
#include "compiler.hpp"
#include "../../utility/character_info.hpp"
#include "lexer.hpp"
#include <string>

// PROTOTYPES
bool source_block_is_comment_block(Block_Index block_index);
void source_text_remove_invalid_whitespaces(String& text);

// Source Code
Source_Code* source_code_create()
{
    Source_Code* result = new Source_Code;
    result->block_buffer = dynamic_array_create_empty<Source_Block>(1);
    result->free_blocks = dynamic_array_create_empty<int>(1);
    source_code_reset(result);
    return result;
}

Block_Index source_block_insert_empty_block(Line_Index line_index)
{
    // Get block index
    Block_Index new_index;
    auto code = line_index.block_index.code;
    if (code->free_blocks.size > 0) {
        new_index = block_index_make(code, dynamic_array_remove_last(&code->free_blocks));
    }
    else {
        new_index = block_index_make(code, dynamic_array_push_back_dummy(&code->block_buffer));
    }

    // Create new source line
    Source_Line new_line;
    {
        // INFO: Use unsafe version of index_value here, because block isn't valid
        auto block = index_value_unsafe(new_index);
        block->lines = dynamic_array_create_empty<Source_Line>(1);
        block->parent = line_index.block_index;
        block->valid = true;

        new_line.is_block_reference = true;
        new_line.options.block_index = new_index;
    }

    // Insert ordered into parent
    auto parent_block = index_value(line_index.block_index);
    assert(line_index.line_index >= 0 && line_index.line_index <= parent_block->lines.size, "Index must be valid");
    dynamic_array_insert_ordered(&parent_block->lines, new_line, line_index.line_index);
    return new_index;
}

void source_code_remove_empty_block(Block_Index block_index)
{
    auto block = index_value(block_index);
    assert(block->lines.size == 0, "must be empty");
    assert(block_index.block_index != 0, "must not be root");
    assert(block->valid != 0, "must be valid block");
    // Remove from parent
    dynamic_array_remove_ordered(&index_value(block->parent)->lines, block_index_to_line_index(block_index).line_index);
    // Destroy
    dynamic_array_destroy(&block->lines);
    block->valid = false;
    dynamic_array_push_back(&block_index.code->free_blocks, block_index.block_index);
}

void source_block_insert_line(Line_Index line_index)
{
    Source_Line new_line;
    new_line.is_block_reference = false;
    new_line.options.text.infos = dynamic_array_create_empty<Render_Info>(1);
    new_line.options.text.tokens = dynamic_array_create_empty<Token>(1);
    new_line.options.text.text = string_create_empty(1);

    // Insert ordered into parent
    auto parent_block = index_value(line_index.block_index);
    assert(line_index.line_index >= 0 && line_index.line_index <= parent_block->lines.size, "Index must be valid");
    dynamic_array_insert_ordered(&parent_block->lines, new_line, line_index.line_index);
}

void source_line_destroy(Source_Line* line) {
    if (line->is_block_reference) {
        return;
    }
    auto& source_text = line->options.text;
    string_destroy(&source_text.text);
    dynamic_array_destroy(&source_text.tokens);
    dynamic_array_destroy(&source_text.infos);
}

void source_block_destroy(Source_Block* block)
{
    if (!block->valid) return;
    for (int i = 0; i < block->lines.size; i++) {
        source_line_destroy(&block->lines[i]);
    }
    dynamic_array_destroy(&block->lines);
    block->valid = false;
}

void source_code_reset(Source_Code* code)
{
    for (int i = 0; i < code->block_buffer.size; i++) {
        source_block_destroy(&code->block_buffer[i]);
    }
    dynamic_array_reset(&code->block_buffer);
    dynamic_array_reset(&code->free_blocks);

    // Insert root block
    Source_Block root;
    root.lines = dynamic_array_create_empty<Source_Line>(1);
    root.valid = true;
    root.parent = block_index_make(code, -1);
    dynamic_array_push_back(&code->block_buffer, root);

    // Insert root line_index
    source_block_insert_line(line_index_make(block_index_make_root(code), 0));
}

void source_code_destroy(Source_Code* code)
{
    for (int i = 0; i < code->block_buffer.size; i++) {
        source_block_destroy(&code->block_buffer[i]);
    }
    dynamic_array_destroy(&code->block_buffer);
    dynamic_array_destroy(&code->free_blocks);
}



// Loading/Writing from file
void source_block_fill_from_string(Block_Index parent_index, String text, int* text_index, int indentation)
{
    Block_Index block_index;
    if (indentation == 0) {
        assert(parent_index.block_index == 0, "");
        block_index = parent_index;
    }
    else {
        auto parent_block = index_value(parent_index);
        block_index = source_block_insert_empty_block(line_index_make(parent_index, parent_block->lines.size));
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

        // Find line_index end
        int line_start_index = index;
        while (index < text.size && text.characters[index] != '\n') {
            index += 1;
        }
        int line_end_index = index;
        if (index != text.size) {
            index += 1; // Skip "
        }

        auto line_index = line_index_make(block_index, index_value(block_index)->lines.size);
        source_block_insert_line(line_index);
        String substring = string_create_substring_static(&text, line_start_index, line_end_index);
        source_text_remove_invalid_whitespaces(substring);
        string_append_string(&index_value_text(line_index)->text, &substring);
    }
}

void source_code_fill_from_string(Source_Code* code, String text)
{
    // Reset
    {
        source_code_reset(code);
        auto& root_block = code->block_buffer[0];
        source_line_destroy(&root_block.lines[0]);
        dynamic_array_reset(&root_block.lines);
    }

    int text_index = 0;
    source_block_fill_from_string(block_index_make_root(code), text, &text_index, 0);

    // Check if root block not empty (E.g. empty text)
    {
        auto& root_block = code->block_buffer[0];
        if (root_block.lines.size == 0) {
            source_block_insert_line(line_index_make(block_index_make_root(code), 0));
        }
    }
    source_code_sanity_check(code);
}

void source_block_append_to_string(Block_Index block_index, String* text, int indentation)
{
    auto block = index_value(block_index);
    for (int i = 0; i < block->lines.size; i++)
    {
        auto& line = block->lines[i];
        if (line.is_block_reference) {
            source_block_append_to_string(line.options.block_index, text, indentation + 1);
        }
        else {
            for (int j = 0; j < indentation; j++) {
                string_append_formated(text, "\t");
            }
            string_append_string(text, &line.options.text.text);
            string_append_formated(text, "\n");
        }
    }
}

void source_code_append_to_string(Source_Code* code, String* text) {
    source_block_append_to_string(block_index_make_root(code), text, 0);
}



// Utils
void source_text_remove_invalid_whitespaces(String& text)
{
    // NOTE: This is a copy paste from syntax-editor
    int index = 0;
    while (index < text.size)
    {
        char curr = text[index];
        char next = index + 1 < text.size ? text[index + 1] : '!'; // Any non-space critical chars will do
        char prev = index - 1 >= 0 ? text[index - 1] : '!';
        if (prev == '/' && curr == '/') break; // Skip comments
        // Skip strings
        if (curr == '"')
        {
            index += 1;
            while (index < text.size)
            {
                curr = text[index];
                if (curr == '\\') {
                    index += 2;
                    continue;
                }
                if (curr == '"') {
                    index += 1;
                    prev = curr;
                    break;
                }
                index += 1;
                prev = curr;
            }
            continue;
        }

        if (curr == ' ' && !(char_is_space_critical(prev) && char_is_space_critical(next))) {
            string_remove_character(&text, index);
        }
        else {
            index += 1;
        }
    }
}

void source_code_tokenize_line(Line_Index index)
{
    auto line = index_value_text(index);
    if (source_block_inside_comment(index.block_index)) {
        lexer_tokenize_text_as_comment(line->text, &line->tokens);
    }
    else {
        lexer_tokenize_text(line->text, &line->tokens);
    }
}

void source_code_tokenize_block(Block_Index index, bool inside_comment)
{
    if (source_block_is_comment_block(index)) {
        inside_comment = true;
    }
    auto block= index_value(index);
    for (int i = 0; i < block->lines.size; i++) {
        auto& line = block->lines[i];
        if (line.is_block_reference) {
            source_code_tokenize_block(line.options.block_index, inside_comment);
        }
        else {
            auto& text = line.options.text;
            if (inside_comment) {
                lexer_tokenize_text_as_comment(text.text, &text.tokens);
            }
            else {
                lexer_tokenize_text(text.text, &text.tokens);
            }
        }
    }
}

void source_code_tokenize(Source_Code* code)
{
    auto root_index = block_index_make_root(code);
    source_code_tokenize_block(root_index, false);
}

void source_block_check_sanity(Block_Index index)
{
    auto block = index_value(index);
    assert(block->lines.size != 0, "No empty blocks allowed");
    if (index.block_index == 0) {
        assert(block->parent.block_index == -1, "");
    }

    bool last_was_block = false;
    for (int i = 0; i < block->lines.size; i++)
    {
        auto& line = block->lines[i];
        assert(!(last_was_block && line.is_block_reference), "No two blocks are allowed to follow one another!");
        last_was_block = line.is_block_reference;
        if (!line.is_block_reference) {
            continue;
        }
        auto child_index = line.options.block_index;
        auto child_block = index_value(child_index);
        assert(index_equal(child_block->parent, index), "Parent/Child connections must be correct!");
        source_block_check_sanity(child_index);
    }
}

void source_code_sanity_check(Source_Code* code)
{
    assert(code->block_buffer.size >= 1, "Root block must exist");
    source_block_check_sanity(block_index_make_root(code));
}




bool source_line_is_comment(Line_Index line_index)
{
    if (index_value(line_index)->is_block_reference) {
        return false;
    }
    auto text = index_value_text(line_index)->text;
    if (text.size < 2) return false;
    if (text[0] != '/' || text[1] != '/') return false;
    return true;
}

bool source_line_is_multi_line_comment_start(Line_Index line_index)
{
    if (!source_line_is_comment(line_index)) return false;
    auto text = index_value_text(line_index)->text;
    for (int i = 2; i < text.size; i++) {
        char c = text[i];
        if (c != ' ' && c != '\r' && c != '\t') return false;
    }
    return true;
}

bool source_block_is_comment_block(Block_Index block_index)
{
    auto block = index_value(block_index);
    if (block->parent.block_index == -1) return false;
    auto line_index = block_index_to_line_index(block_index);
    if (line_index.line_index == 0) return false;
    return source_line_is_multi_line_comment_start(line_index_make(block->parent, line_index.line_index - 1));
}

bool source_block_inside_comment(Block_Index block_index)
{
    if (source_block_is_comment_block(block_index)) return true;
    auto block = index_value(block_index);
    if (block->parent.block_index != -1) {
        return source_block_inside_comment(block->parent);
    }
    return false;
}

bool source_index_is_end_of_line(Line_Index line_index)
{
    auto block = index_value(line_index.block_index);
    assert(line_index.line_index <= block->lines.size, "");
    return line_index.line_index == block->lines.size;
}




// Index Functions
Source_Block* index_value_unsafe(Block_Index index) {
    // DOCS: Only use this function when sanitizing values or if you need to manipulate blocks at a low level
    return &index.code->block_buffer[index.block_index];
}

Source_Block* index_value(Block_Index index) {
    auto block = index_value_unsafe(index);
    assert(block->valid, "");
    return block;
}

Source_Line* index_value(Line_Index index) {
    return &index_value(index.block_index)->lines[index.line_index];
}

Source_Text* index_value_text(Line_Index index) {
    auto source_line = index_value(index);
    assert(!source_line->is_block_reference, "for index value text this must be a text line");
    return &source_line->options.text;
}

Token* index_value(Token_Index index) {
    return &index_value_text(index.line_index)->tokens[index.token];
}

char index_value(Text_Index index) {
    auto text = index_value_text(index.line_index)->text;
    if (index.pos >= text.size) return '\0';
    return text[index.pos];
}

Block_Index block_index_make(Source_Code* code, int block_index) {
    Block_Index index;
    index.code = code;
    index.block_index = block_index;
    return index;
}

Block_Index block_index_make_root(Source_Code* code) {
    return block_index_make(code, 0);
}

Line_Index block_index_to_line_index(Block_Index block_index)
{
    auto block = index_value(block_index);
    assert(block->parent.block_index != -1, "Cannot get line index of root!");
    auto parent_block = index_value(block->parent);
    for (int i = 0; i < parent_block->lines.size; i++) {
        auto& line = parent_block->lines[i];
        if (line.is_block_reference && line.options.block_index.block_index == block_index.block_index) {
            return line_index_make(block->parent, i);
        }
    }
    panic("Blocks should always be found in their parent!");
    return line_index_make(block_index, 0);
}

Line_Index line_index_make(Block_Index block_index, int line_index) {
    Line_Index index;
    index.block_index = block_index;
    index.line_index = line_index;
    return index;
}

Line_Index line_index_make_root(Source_Code* code) {
    return line_index_make(block_index_make_root(code), 0);
}

Text_Index text_index_make(Line_Index line_index, int pos)
{
    Text_Index index;
    index.line_index = line_index;
    index.pos = pos;
    return index;
}

Token_Index token_index_make(Line_Index line_index, int token) {
    Token_Index index;
    index.line_index = line_index;
    index.token = token;
    return index;
}

Token_Index token_index_make_root(Source_Code* code) {
    return token_index_make(line_index_make_root(code), 0);
}

Token_Index token_index_make_line_start(Line_Index index) {
    return token_index_make(index, 0);
}

Token_Index token_index_make_line_end(Line_Index index) {
    auto line = index_value_text(index);
    return token_index_make(index, line->tokens.size);
}

Token_Index token_index_make_block_start(Block_Index block_index) {
    while (true)
    {
        auto first_line = index_value(line_index_make(block_index, 0));
        if (first_line->is_block_reference) {
            block_index = first_line->options.block_index;
        }
        else {
            return token_index_make_line_start(line_index_make(block_index, 0));
        }
    }
    panic("Hey");
}

Token_Index token_index_make_block_end(Block_Index block_index) {
    while (true)
    {
        auto block = index_value(block_index);
        auto last_line = dynamic_array_last(&block->lines);
        if (last_line.is_block_reference) {
            block_index = last_line.options.block_index;
        }
        else {
            return token_index_make_line_end(line_index_make(block_index, block->lines.size - 1));
        }
    }
    panic("Hey");
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
    auto& blocks = index.code->block_buffer;
    auto block = index_value_unsafe(index);
    return index.block_index >= 0 && index.block_index < blocks.size && block->valid;
}

bool index_valid(Line_Index index)
{
    if (!index_valid(index.block_index)) return false;
    auto& lines = index_value(index.block_index)->lines;
    return index.line_index >= 0 && index.line_index < lines.size;
}

bool index_valid(Token_Index index)
{
    if (!index_valid(index.line_index)) return false;
    auto line = index_value(index.line_index);
    if (line->is_block_reference) return false;
    auto& tokens = line->options.text.tokens;
    return index.token >= 0 && index.token <= tokens.size; // Here <= because a token index == tokens.size may be used in range to indicate line_index end
}

bool index_valid(Text_Index index)
{
    if (!index_valid(index.line_index)) return false;
    auto line = index_value(index.line_index);
    if (line->is_block_reference) return false;
    auto& text = line->options.text.text;
    return index.pos >= 0 && index.pos <= text.size; // Here <= because a token index == tokens.size may be used in range to indicate line_index end
}

void index_sanitize(Block_Index* index)
{
    auto& blocks = index->code->block_buffer;
    index->block_index = math_clamp(index->block_index, 0, blocks.size - 1);
    auto block = index_value_unsafe(*index);
    while (!block->valid) {
        index->block_index = block->parent.block_index;
        block = index_value_unsafe(*index);
    }
}

void index_sanitize(Line_Index* index)
{
    index_sanitize(&index->block_index);
    auto block = index_value(index->block_index);
    index->line_index = math_clamp(index->line_index, 0, block->lines.size - 1);
}

void index_sanitize(Token_Index* index)
{
    index_sanitize(&index->line_index);
    auto& tokens = index_value_text(index->line_index)->tokens;
    index->token = math_clamp(index->token, 0, tokens.size);
}

void index_sanitize(Text_Index* index)
{
    index_sanitize(&index->line_index);
    auto& text = index_value_text(index->line_index)->text;
    index->pos = math_clamp(index->pos, 0, text.size);
}

bool index_equal(Block_Index a, Block_Index b)
{
    assert(a.code == b.code, "");
    return a.block_index == b.block_index;
}

bool index_equal(Line_Index a, Line_Index b)
{
    if (!index_equal(a.block_index, b.block_index)) return false;
    return a.line_index == b.line_index;
}

bool index_equal(Token_Index a, Token_Index b)
{
    if (!index_equal(a.line_index, b.line_index)) return false;
    return a.token == b.token;
}

bool index_equal(Text_Index a, Text_Index b)
{
    if (!index_equal(a.line_index, b.line_index)) return false;
    return a.pos == b.pos;
}

int index_compare(Line_Index a, Line_Index b)
{
    assert(a.block_index.code == b.block_index.code, "");
    if (index_equal(a.block_index, b.block_index))
    {
        if (a.line_index == b.line_index) return 0;
        return a.line_index < b.line_index ? 1 : -1;
    }

    int a_indent = block_index_get_indentation(a.block_index);
    int b_indent = block_index_get_indentation(b.block_index);
    while (!index_equal(a.block_index, b.block_index))
    {
        if (a_indent > b_indent) {
            a = block_index_to_line_index(a.block_index);
            a_indent -= 1;
        }
        else {
            b = block_index_to_line_index(b.block_index);
            b_indent -= 1;
        }
    }
    return a.line_index < b.line_index ? 1 : -1;
}

int index_compare(Token_Index a, Token_Index b)
{
    int line_cmp = index_compare(a.line_index, b.line_index);
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
    //        2. Ranges at the end of a line_index (E.g. missing tokens)
    if (range.start.line_index.block_index.code != index.line_index.block_index.code) return false; // Handle multiple files
    if (index_equal(range.start, range.end) && index_equal(range.start.line_index, index.line_index)) {
        // Special case of same line + empty 
        auto& tokens = index_value_text(index.line_index)->tokens;
        if (token_index_is_end_of_line(range.start)) {
            return index.token >= tokens.size;
        }
        return index.token == range.start.token; // Ranges with size 0 should still be able to capture some tokens
    }
    if (token_index_is_end_of_line(range.end)) {
        range.end.token += 1; // Hack to make the comparison work
    }
    return index_compare(range.start, index) >= 0 && index_compare(index, range.end) > 0;
}



// Movement/Navigation
Line_Index block_get_first_text_line(Block_Index block_index)
{
    auto block = index_value(block_index);
    if (block->lines[0].is_block_reference) {
        return block_get_first_text_line(block->lines[0].options.block_index);
    }
    return line_index_make(block_index, 0);
}

Line_Index block_get_last_text_line(Block_Index block_index)
{
    auto block = index_value(block_index);
    auto last_line = dynamic_array_last(&block->lines);
    if (last_line.is_block_reference) {
        return block_get_last_text_line(last_line.options.block_index);
    }
    return line_index_make(block_index, block->lines.size - 1);
}

int block_index_get_indentation(Block_Index block_index)
{
    int indentation = 0;
    while (block_index.block_index != 0) {
        indentation += 1;
        block_index = index_value(block_index)->parent;
    }
    return indentation;
}

Line_Index line_index_next(Line_Index line_index)
{
    // Check if we are stepping out of our current block
    auto block = index_value(line_index.block_index);
    line_index.line_index += 1;
    if (line_index.line_index < block->lines.size) {
        auto line = index_value(line_index);
        if (line->is_block_reference) {
            return block_get_first_text_line(line->options.block_index);
        }
        else {
            return line_index;
        }
    }

    if (line_index.block_index.block_index == 0) {
        return block_get_last_text_line(line_index.block_index); // End of code reached
    }
    return line_index_next(block_index_to_line_index(line_index.block_index));
}

Line_Index line_index_prev(Line_Index line_index)
{
    // Check if we are stepping out of our current block
    auto block = index_value(line_index.block_index);
    line_index.line_index -= 1;
    if (line_index.line_index >= 0) {
        auto line = index_value(line_index);
        if (line->is_block_reference) {
            return block_get_last_text_line(line->options.block_index);
        }
        else {
            return line_index;
        }
    }

    if (line_index.block_index.block_index == 0) {
        return block_get_first_text_line(line_index.block_index); // Start of code reached
    }
    return line_index_prev(block_index_to_line_index(line_index.block_index));
}

Optional<Block_Index> line_index_block_after(Line_Index line_index)
{
    auto block = index_value(line_index.block_index);
    if (line_index.line_index + 1 >= block->lines.size) {
        return optional_make_failure<Block_Index>();
    }
    auto& line_after = block->lines[line_index.line_index + 1];
    if (line_after.is_block_reference) {
        return optional_make_success(line_after.options.block_index);
    }
    return optional_make_failure<Block_Index>();
}

bool line_index_is_end_of_block(Line_Index index) {
    return index.line_index >= index_value(index.block_index)->lines.size;
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

bool token_index_is_end_of_line(Token_Index index)
{
    auto line_item = index_value(index.line_index);
    if (line_item->is_block_reference) {
        return true;
    }
    auto& tokens = line_item->options.text.tokens;
    assert(index.token <= tokens.size, "");
    return index.token >= tokens.size;
}


