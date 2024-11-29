#include "source_code.hpp"

#include "../../datastructures/hashtable.hpp"
#include "compiler.hpp"
#include "../../utility/character_info.hpp"
#include "lexer.hpp"
#include "ast.hpp"
#include <string>

const int BUNDLE_MAX_SIZE = 500;
const int BUNDLE_MERGE_THRESHHOLD = 300; // If a bundle can merge with a neighbor bundle into another bundle with fewer than these lines, it will do so

// PROTOTYPES
void source_text_remove_invalid_whitespaces(String& text);

void add_first_bundle_and_line(Source_Code* code)
{
    Line_Bundle first_bundle;
    first_bundle.first_line_index = 0;
    first_bundle.lines = dynamic_array_create<Source_Line>();

    Source_Line first_line;
    first_line.indentation = 0;
    first_line.text = string_create();
    first_line.tokens = dynamic_array_create<Token>();
    first_line.item_infos = dynamic_array_create<Code_Analysis_Item>();
    first_line.is_comment = false;
    first_line.comment_block_indentation = -1;
    first_line.is_folded = false;

    dynamic_array_push_back(&first_bundle.lines, first_line);
    dynamic_array_push_back(&code->bundles, first_bundle);
    code->line_count = 1;
}

// Source Code
Source_Code* source_code_create()
{
    Source_Code* result = new Source_Code;
    result->line_count = 0;
    result->bundles = dynamic_array_create<Line_Bundle>();
    result->block_id_range = dynamic_array_create<Block_ID_Range>();
    result->symbol_table_ranges = dynamic_array_create<Symbol_Table_Range>();
    result->root_table = nullptr;
    add_first_bundle_and_line(result);
    return result;
}

Source_Code* source_code_copy(Source_Code* copy_from)
{
    Source_Code* result = new Source_Code;
    *result = *copy_from;
    result->line_count = copy_from->line_count;
    result->bundles = dynamic_array_create_copy<Line_Bundle>(copy_from->bundles.data, copy_from->bundles.size);
    for (int i = 0; i < result->bundles.size; i++) {
        auto& bundle = result->bundles[i];
        bundle.lines = dynamic_array_create_copy<Source_Line>(bundle.lines.data, bundle.lines.size);
        for (int j = 0; j < bundle.lines.size; j++) {
            auto& line = bundle.lines[j];
            line.text = string_copy(line.text);
            line.tokens = dynamic_array_create_copy<Token>(line.tokens.data, line.tokens.size);
            line.item_infos = dynamic_array_create_copy(line.item_infos.data, line.item_infos.size);
        }
    }

    result->symbol_table_ranges = dynamic_array_create_copy(copy_from->symbol_table_ranges.data, copy_from->symbol_table_ranges.size);
    result->block_id_range = dynamic_array_create_copy(copy_from->block_id_range.data, copy_from->block_id_range.size);

    return result;
}

void source_line_destroy(Source_Line* line)
{
    dynamic_array_destroy(&line->tokens);
    dynamic_array_destroy(&line->item_infos);
    string_destroy(&line->text);
}

void source_code_destroy(Source_Code* code)
{
    for (int i = 0; i < code->bundles.size; i++) {
        auto& bundle = code->bundles[i];
        dynamic_array_for_each(bundle.lines, source_line_destroy);
        dynamic_array_destroy(&bundle.lines);
    }
    dynamic_array_destroy(&code->bundles);
    dynamic_array_destroy(&code->symbol_table_ranges);
    dynamic_array_destroy(&code->block_id_range);
    code->line_count = 0;
    delete code;
}

void source_code_reset(Source_Code* code)
{
    for (int i = 0; i < code->bundles.size; i++) {
        auto& bundle = code->bundles[i];
        dynamic_array_for_each(bundle.lines, source_line_destroy);
        dynamic_array_destroy(&bundle.lines);
    }
    dynamic_array_reset(&code->bundles);

    add_first_bundle_and_line(code);
}

void source_code_print_bundles(Source_Code* code)
{
    printf("\nLines: %d, Bundles: %d\n------------------\n", code->line_count, code->bundles.size);
    int line_start = 0;
    for (int i = 0; i < code->bundles.size; i++) {
        auto& bundle = code->bundles[i];
        printf("Bundle %d: From/To %d-%d,  (#%d)\n", i, bundle.first_line_index, bundle.first_line_index + bundle.lines.size - 1, bundle.lines.size);
    }
}

// Finds the bundle which contains the line index
int source_code_get_line_bundle_index(Source_Code* code, int line_index)
{
    assert(line_index >= 0, "");

    // Do linear search for now (Binary search possible, but I think it's only usefull after a few hundred lines...)
    // Also last accessed lines could be cached, but this can be done in the future
    for (int i = 0; i < code->bundles.size; i++)
    {
        auto& bundle = code->bundles[i];
        if (line_index >= bundle.first_line_index && line_index < bundle.first_line_index + bundle.lines.size) {
            return i;
        }
    }

    // Otherwise we return the last code bundle
    return code->bundles.size - 1;
}

Source_Line* source_code_get_line(Source_Code* code, int line_index)
{
    int bundle_index = source_code_get_line_bundle_index(code, line_index);
    auto& bundle = code->bundles[bundle_index];
    return &bundle.lines[line_index - bundle.first_line_index];
}

bool source_line_is_comment(Source_Line* line)
{
    if (line->text.size < 2) return false;
    if (line->text.characters[0] != '/' || line->text.characters[1] != '/') return false;
    return true;
}

bool source_line_is_multi_line_comment_start(Source_Line* line)
{
    if (line->text.size < 2) return false;
    if (line->text.characters[0] != '/' || line->text.characters[1] != '/') return false;

    for (int i = 2; i < line->text.size; i++) {
        char c = line->text[i];
        if (c != ' ' && c != '\r' && c != '\t') return false;
    }
    return true;
}

// Checks if the comment information is up-to date on the given line by checking the previous line.
// If it is not up-to-date, it will update all following lines until the information is correct again
void update_line_block_comment_information(Source_Code* code, int line_index)
{
    int bundle_index = source_code_get_line_bundle_index(code, line_index);
    Line_Bundle* bundle = &code->bundles[bundle_index];
    Source_Line* line = &bundle->lines[line_index - bundle->first_line_index];

    // Check if previous line is in comment block
    int comment_indentation = -1;
    if (line_index > 0) {
        Source_Line* prev_line = source_code_get_line(code, line_index - 1);
        if (prev_line->comment_block_indentation != -1) {
            comment_indentation = prev_line->comment_block_indentation;
        }
        else if (source_line_is_multi_line_comment_start(prev_line)) {
            comment_indentation = prev_line->indentation + 1;
        }
    }

    // Update following lines if they are wrong...
    for (int i = line_index; i < code->line_count; i++)
    {
        // Check if we step over bundle boundary
        while (i >= bundle->first_line_index + bundle->lines.size) {
            bundle_index += 1;
            bundle = &code->bundles[bundle_index];
        }
        line = &bundle->lines[i - bundle->first_line_index];

        // Figure out expected indentation and is comment
        int expected_comment_indentation = -1;
        bool expected_is_comment = false;
        if (comment_indentation == -1)
        {
            expected_comment_indentation = -1;
            if (source_line_is_multi_line_comment_start(line)) {
                expected_is_comment = true;
                comment_indentation = line->indentation + 1;
            }
            else {
                expected_is_comment = source_line_is_comment(line);
            }
        }
        else 
        {
            // Check if indentation ended
            if (line->indentation < comment_indentation) 
            {
                expected_comment_indentation = -1;
                if (source_line_is_multi_line_comment_start(line)) {
                    expected_is_comment = true;
                    comment_indentation = line->indentation + 1;
                }
                else {
                    comment_indentation = -1;
                    expected_is_comment = source_line_is_comment(line);
                }
            }
            else { // Line is inside comment-block
                expected_is_comment = true;
                expected_comment_indentation = comment_indentation;
            }
        }

        // Stop this loop if next line is correct
        if (line->is_comment == expected_is_comment && line->comment_block_indentation == expected_comment_indentation) {
            if (i == line_index) { // Only exit if we have checked one line after first line
                continue;
            }
            break;
        }
        else {
            line->is_comment = expected_is_comment;
            line->comment_block_indentation = expected_comment_indentation;
        }
    }
}


Source_Line* source_code_insert_line(Source_Code* code, int new_line_index, int indentation)
{
    int bundle_index = source_code_get_line_bundle_index(code, new_line_index);
    Line_Bundle* bundle = &code->bundles[bundle_index];

    // Split bundle if max size is exceeded
    if (bundle->lines.size > BUNDLE_MAX_SIZE)
    {
        int new_line_count = BUNDLE_MAX_SIZE / 2;
        int split_index = bundle->lines.size - new_line_count;

        Line_Bundle new_bundle;
        new_bundle.first_line_index = bundle->first_line_index + split_index;
        new_bundle.lines = dynamic_array_create<Source_Line>(new_line_count);

        // Copy lines into new bundle
        for (int i = 0; i < new_line_count; i++) {
            dynamic_array_push_back(&new_bundle.lines, bundle->lines[i + split_index]);
        }
        dynamic_array_rollback_to_size(&bundle->lines, split_index);

        // Insert new bundle
        dynamic_array_insert_ordered(&code->bundles, new_bundle, bundle_index + 1);

        // Check in which bundle we need to insert the index
        if (new_line_index >= bundle->first_line_index + bundle->lines.size) {
            bundle_index = bundle_index + 1;
        }
        // Refresh pointer after insertion
        bundle = &code->bundles[bundle_index];
    }

    // Add new line to bundle
    {
        int index_in_bundle = new_line_index - bundle->first_line_index;
        assert(new_line_index >= bundle->first_line_index, "Should be the case");
        assert(index_in_bundle <= bundle->lines.size, "");

        Source_Line line;
        line.indentation = indentation;
        line.text = string_create();
        line.tokens = dynamic_array_create<Token>();
        line.item_infos = dynamic_array_create<Code_Analysis_Item>();
        line.is_comment = false;
        line.is_folded = false;
        line.comment_block_indentation = -1;
        dynamic_array_insert_ordered(&bundle->lines, line, index_in_bundle);
    }

    // Update following bundles line start index
    for (int i = bundle_index + 1; i < code->bundles.size; i++) {
        code->bundles[i].first_line_index += 1;
    }
    code->line_count += 1;

    // Update comment block infos
    update_line_block_comment_information(code, new_line_index);

    return &bundle->lines[new_line_index - bundle->first_line_index];
}

void source_code_remove_line(Source_Code* code, int line_index)
{
    int bundle_index = source_code_get_line_bundle_index(code, line_index);
    Line_Bundle* bundle = &code->bundles[bundle_index];

    // Don't remove line if only one line exists
    if (code->line_count <= 1) {
        auto& line = code->bundles[0].lines[0];
        string_reset(&line.text);
        return;
    }

    // Remove line from bundle
    {
        Source_Line* line = &bundle->lines[line_index - bundle->first_line_index];
        source_line_destroy(line);
        dynamic_array_remove_ordered(&bundle->lines, line_index - bundle->first_line_index);
    }

    // Update following bundles line start index
    for (int i = bundle_index + 1; i < code->bundles.size; i++) {
        code->bundles[i].first_line_index -= 1;
    }
    code->line_count -= 1;

    // Update block comment infos
    if (line_index < code->line_count) {
        update_line_block_comment_information(code, line_index);
    }

    if (bundle->lines.size == 0)
    {
        // This means that the bundle should just be deleted, as merging was not possible previously
        dynamic_array_destroy(&bundle->lines);
        dynamic_array_remove_ordered(&code->bundles, bundle_index);
        return;
    }
    else if (bundle->lines.size < BUNDLE_MERGE_THRESHHOLD)
    {
        // Check if merging with previous/next bundle is possible
        if (bundle_index > 0 && code->bundles[bundle_index - 1].lines.size + bundle->lines.size < BUNDLE_MERGE_THRESHHOLD) {
            // Update current bundle so that we always merge with next bundle
            bundle_index = bundle_index - 1;
            bundle = &code->bundles[bundle_index];
        }
        else if (bundle_index + 1 < code->bundles.size && code->bundles[bundle_index + 1].lines.size + bundle->lines.size < BUNDLE_MERGE_THRESHHOLD) {
            // Merge with next bundle
        }
        else {
            // Exit if merge not possible
            return;
        }

        // Add lines from next bundle to our bundle
        Line_Bundle* next_bundle = &code->bundles[bundle_index + 1];
        for (int i = 0; i < next_bundle->lines.size; i++) {
            dynamic_array_push_back(&bundle->lines, next_bundle->lines[i]);
        }
        dynamic_array_destroy(&next_bundle->lines);
        dynamic_array_swap_remove(&code->bundles, bundle_index + 1);
    }
}

// Loading/Writing from file
void source_code_fill_from_string(Source_Code* code, String text)
{
    // Reset
    source_code_reset(code);

    // Parse all lines
    int index = 0;
    int comment_indent = -1;
    while (index < text.size)
    {
        // Find indentation level
        int indent_start_index = index;
        int line_indent = 0;
        while (index < text.size && text.characters[index] == '\t') 
        {
            if (text.characters[index] == '\t') {
                line_indent += 1;
                index += 1;
            }
            else if (index + 3 < text.size &&
                text.characters[index] == ' ' &&
                text.characters[index + 1] == ' ' &&
                text.characters[index + 2] == ' ' &&
                text.characters[index + 3] == ' ') 
            {
                line_indent += 1;
                index += 4;
            }
            else {
                break;
            }
        }

        // Find line end
        int line_start_index = index;
        while (index < text.size && text.characters[index] != '\n') {
            index += 1;
        }
        int line_end_index = index;
        if (index != text.size) {
            index += 1; // Skip \n
        }

        Source_Line* line = source_code_insert_line(code, code->line_count, line_indent);
        String substring = string_create_substring_static(&text, line_start_index, line_end_index);
        string_append_string(&line->text, &substring);
        source_text_remove_invalid_whitespaces(line->text);

        // Handle comment info
        if (comment_indent == -1)
        {
            line->comment_block_indentation = -1;
            if (source_line_is_multi_line_comment_start(line))
            {
                line->is_comment = true;
                comment_indent = line_indent + 1;
            }
            else {
                line->is_comment = source_line_is_comment(line);
            }
        }
        else
        {
            if (line_indent < comment_indent) {
                comment_indent = -1;
                line->comment_block_indentation = -1;
                line->is_comment = source_line_is_comment(line);
            }
            else {
                line->comment_block_indentation = comment_indent;
                line->is_comment = true;
            }
        }
    }

    // Because we always start with an empty line after reset, we want to remove it if other lines were added
    if (code->line_count > 0) {
        source_code_remove_line(code, 0);
    }

    source_code_sanity_check(code);
}

void source_code_append_to_string(Source_Code* code, String* text) 
{
    for (int i = 0; i < code->bundles.size; i++)
    {
        auto& bundle = code->bundles[i];
        for (int j = 0; j < bundle.lines.size; j++) {
            Source_Line& line = bundle.lines[j];
            for (int k = 0; k < line.indentation; k++) {
                string_append_formated(text, "\t");
            }
            string_append_string(text, &line.text);
            string_append_formated(text, "\n");
        }
    }
}



// Utils
void source_text_remove_invalid_whitespaces(String& text)
{
    int index = 0;
    while (index < text.size)
    {
        char curr = text[index];
        // Remove control characters from line, like \n, \t, \r and others
        if (curr < ' '){
            string_remove_character(&text, index);
        }
        else {
            index += 1;
        }
    }
}

void source_code_tokenize_line(Source_Line* line)
{
    if (line->is_comment) {
        return;
    }
    lexer_tokenize_line(line->text, &line->tokens, &compiler.identifier_pool);
}

void source_code_tokenize_line(Source_Code* code, int line_index)
{
    source_code_tokenize_line(source_code_get_line(code, line_index));
}

void source_code_tokenize(Source_Code* code)
{
    for (int i = 0; i < code->bundles.size; i++) {
        auto& bundle = code->bundles[i];
        for (int j = 0; j < bundle.lines.size; j++) {
            source_code_tokenize_line(&bundle.lines[j]);
        }
    }
}

void source_code_sanity_check(Source_Code* code)
{
    int line_count = 0;
    int comment_indentation = -1;

    for (int i = 0; i < code->bundles.size; i++)
    {
        auto& bundle = code->bundles[i];
        for (int j = 0; j < bundle.lines.size; j++)
        {
            Source_Line* line = &bundle.lines[j];
            line_count += 1;

            // Check comment info
            if (comment_indentation == -1)
            {
                if (source_line_is_multi_line_comment_start(line))
                {
                    assert(line->is_comment, "");
                    comment_indentation = line->indentation + 1;
                }
                else {
                    assert(line->is_comment == source_line_is_comment(line), "");
                }
            }
            else
            {
                if (line->indentation < comment_indentation) 
                {
                    assert(line->comment_block_indentation == -1, "");
                    if (source_line_is_multi_line_comment_start(line))
                    {
                        assert(line->is_comment, "");
                        comment_indentation = line->indentation + 1;
                    }
                    else {
                        assert(line->is_comment == source_line_is_comment(line), "");
                        comment_indentation = -1;
                    }
                }
                else
                {
                    assert(line->is_comment, "");
                }
            }
        }
    }
}




// Indices
Text_Index text_index_make(int line, int character) {
    return { line, character };
}

Text_Index text_index_make_line_end(Source_Code* code, int line) {
    Text_Index index;
    index.line = line;
    index.character = source_code_get_line(code, line)->text.size;
    return index;
}

bool text_index_equal(const Text_Index& a, const Text_Index& b) {
    return a.line == b.line && a.character == b.character;
}

bool text_index_in_order(const Text_Index& a, const Text_Index& b) {
    if (a.line > b.line) return false;
    if (a.line < b.line) return true;
    return a.character <= b.character;
}

Text_Range text_range_make(Text_Index start, Text_Index end) {
    return { start, end };
}

bool text_range_contains(Text_Range range, Text_Index index) {
    return text_index_in_order(range.start, index) && text_index_in_order(index, range.end);
}

// Token Indices
Token_Index token_index_make(int line, int token)
{
    Token_Index index;
    index.line = line;
    index.token = token;
    return index;
}

Token_Index token_index_make_line_end(Source_Code* code, int line_index)
{
    Token_Index index;
    index.line = line_index;
    auto line = source_code_get_line(code, line_index);
    index.token = line->tokens.size;
    return index;
}

bool token_index_valid(Token_Index index, Source_Code* code)
{
    if (index.line < 0 || index.line >= code->line_count) return false;
    auto line = source_code_get_line(code, index.line);
    return index.token >= 0 && index.token < line->tokens.size;
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
    range.end = start;
    if (offset >= 0) {
        range.end.token += offset;
    }
    else {
        range.start.token += offset;
    }
    return range;
}

bool token_index_equal(Token_Index a, Token_Index b) {
    return a.line == b.line && a.token == b.token;
}

// 1 == sorted, 0 == equal, -1 = not sorted
int token_index_compare(Token_Index a, Token_Index b)
{
    if (a.line != b.line) {
        if (a.line < b.line) return 1;
        else if (a.line == b.line) return 0;
        else return -1;
    }

    if (a.token < b.token) return 1;
    else if (a.token == b.token) return 0;
    else return -1;
}

bool token_range_contains(Token_Range range, Token_Index index)
{
    int cmp_start = token_index_compare(range.start, index);
    int cmp_end = token_index_compare(index, range.end);
    return cmp_start != -1 && cmp_end == 1; // End is not inclusive
}



// Conversion functions
Text_Range token_range_to_text_range(Token_Range range, Source_Code* code)
{
    Text_Range result;
    result.start.line = range.start.line;
    result.end.line = range.end.line;
    auto start_line = source_code_get_line(code, range.start.line);
    auto end_line = source_code_get_line(code, range.end.line);
    if (range.start.token < start_line->tokens.size) {
        result.start.character = start_line->tokens[range.start.token].start_index;
    }
    else {
        if (range.start.token == 0) {
            result.start.character = 0;
        }
        else {
            result.start.character = start_line->text.size;
        }
    }

    if (range.end.token >= end_line->tokens.size) {
        result.end.character = end_line->text.size;
    }
    else if (range.end.token - 1 >= 0) {
        result.end.character = end_line->tokens[range.end.token - 1].end_index;
    }
    else {
        result.end.character = 0;
    }
    return result;
}

Token_Range text_range_to_token_range(Text_Range range, Source_Code* code)
{
    Token_Range result;
    result.start.line = range.start.line;
    result.end.line = range.end.line;

    auto start_line = source_code_get_line(code, range.start.line);
    if (range.start.character == 0) {
        result.start.token = 0;
    }
    else if (range.start.character >= start_line->text.size) {
        result.start.token = math_maximum(0, start_line->tokens.size - 1);
    }
    else {
        auto& tokens = start_line->tokens;
        result.start.token = 0;
        for (int i = 0; i < tokens.size; i++) {
            auto& token = tokens[i];
            if (token.start_index <= range.start.character) {
                result.start.token = i;
            }
            else {
                break;
            }
        }
    }

    auto end_line = source_code_get_line(code, range.end.line);
    if (range.end.character == 0) {
        result.end.token = 0;
    }
    else if (range.end.character >= end_line->text.size) {
        result.end.token = math_maximum(0, end_line->tokens.size);
    }
    else {
        auto& tokens = end_line->tokens;
        result.end.token = 0;
        for (int i = 0; i < tokens.size; i++) {
            auto& token = tokens[i];
            if (token.start_index < range.end.character) {
                result.end.token = i;
            }
            else {
                break;
            }
        }
    }

    return result;
}

Text_Index token_index_to_text_index(Token_Index index, Source_Code* code, bool token_start)
{
    if (index.line >= code->line_count) {
        return text_index_make_line_end(code, code->line_count - 1);
    }

    auto line = source_code_get_line(code, index.line);
    if (line->tokens.size == 0) {
        return text_index_make(index.line, 0);
    }
    else if (index.token >= line->tokens.size) {
        return text_index_make(index.line, line->text.size);
    }

    auto& token = line->tokens[index.token];
    if (token_start) {
        return text_index_make(index.line, token.start_index);
    }
    return text_index_make(index.line, token.end_index);
}
