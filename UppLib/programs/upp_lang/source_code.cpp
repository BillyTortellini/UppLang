#include "source_code.hpp"

#include "../../datastructures/hashtable.hpp"
#include "compiler.hpp"
#include "../../utility/character_info.hpp"
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
    first_line.item_infos = dynamic_array_create<Editor_Info_Reference>();

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
            line.item_infos = dynamic_array_create_copy(line.item_infos.data, line.item_infos.size);
        }
    }

    result->symbol_table_ranges = dynamic_array_create_copy(copy_from->symbol_table_ranges.data, copy_from->symbol_table_ranges.size);
    result->block_id_range = dynamic_array_create_copy(copy_from->block_id_range.data, copy_from->block_id_range.size);

    return result;
}

void source_line_destroy(Source_Line* line)
{
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
        line.item_infos = dynamic_array_create<Editor_Info_Reference>();
        dynamic_array_insert_ordered(&bundle->lines, line, index_in_bundle);
    }

    // Update following bundles line start index
    for (int i = bundle_index + 1; i < code->bundles.size; i++) {
        code->bundles[i].first_line_index += 1;
    }
    code->line_count += 1;

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
    }

    // Because we always start with an empty line after reset, we want to remove it if other lines were added
    if (code->line_count > 0) {
        source_code_remove_line(code, 0);
    }
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


