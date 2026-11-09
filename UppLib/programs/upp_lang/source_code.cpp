#include "source_code.hpp"

#include "../../datastructures/hashtable.hpp"
#include "../../utility/character_info.hpp"
#include "../../utility/file_io.hpp"
#include "ast.hpp"
#include <string>

struct DynString;

// PROTOTYPES
void source_text_remove_invalid_whitespaces(DynString* text);

void add_first_line(Source_Code* code)
{
    Source_Line first_line;
    first_line.text = String_List::create_empty(code->free_list.upcast());
    first_line.item_infos = List<Editor_Info_Reference>::create(code->free_list.upcast());
    code->lines.push_back(first_line);
}

// Source Code
Source_Code* source_code_create()
{
    Source_Code* result = new Source_Code;
    result->arena = Arena::create();
    source_code_reset(result);
    return result;
}

Source_Code* source_code_copy(Source_Code* copy_from)
{
    SCRATCH_ARENA_MAKE_SCOPED(&copy_from->arena);
    DynString tmp_string = DynString::create(scratch_arena);


    Source_Code* result = source_code_create();
    result->lines.reset(); // Remove first line, because I assume we copy that
    result->lines.reserve(copy_from->lines.size);

    // Copy lines
    for (int i = 0; i < copy_from->lines.size; i++)
    {
        Source_Line* other = &copy_from->lines[i];
        source_code_insert_line(result, result->lines.size);
        Source_Line* line = &result->lines[result->lines.size - 1];
        line->text.set(tmp_string.reset()->append(other->text)->to_string());

        auto node = other->item_infos.head;
        while (node != nullptr) {
            line->item_infos.append(node->value);
            node = node->next;
        }
    }

    // Copy Infos
    result->symbol_table_ranges.append_array(copy_from->symbol_table_ranges.as_array());
    result->block_id_range.append_array(copy_from->block_id_range.as_array());
    result->root_table = copy_from->root_table;

    return result;
}

void source_code_destroy(Source_Code* code)
{
    code->arena.destroy();
    delete code;
}

void source_code_reset(Source_Code* code)
{
    code->arena.reset();
    code->lines = DynArray<Source_Line>::create(&code->arena);
    code->free_list = Free_List::create_with_info(
        code->arena.upcast(),
        math_maximum(sizeof(String_List_Node), sizeof(List_Node<Editor_Info_Reference>)),
        math_maximum(alignof(String_List_Node), alignof(List_Node<Editor_Info_Reference>))
    );

    code->block_id_range = DynArray<Block_ID_Range>::create(&code->arena);
    code->symbol_table_ranges = DynArray<Symbol_Table_Range>::create(&code->arena);
    code->root_table = nullptr;

    add_first_line(code);
}

int source_code_line_get_size(Source_Code* code, int line_index) {
    if (line_index < 0 || line_index >= code->lines.size) return 0;
    return code->lines[line_index].text.size;
}

void source_code_line_append_to_string(Source_Code* code, int line_index, DynString* append_to) {
    if (line_index < 0 || line_index >= code->lines.size) return;
    append_to->append(code->lines[line_index].text);
}

void source_code_line_get_items(Source_Code* code, int line_index, DynArray<Editor_Info_Reference>* append_to)
{
    if (line_index < 0 || line_index >= code->lines.size) return;

    auto info_list = &code->lines[line_index].item_infos;
    append_to->reserve(append_to->size + info_list->element_count);
    auto node = info_list->head;
    while (node != nullptr) {
        append_to->push_back(node->value);
        node = node->next;
    }
}

void source_code_line_set_text(Source_Code* code, int line_index, String text)
{
    if (line_index < 0 || line_index >= code->lines.size) return;
    code->lines[line_index].text.set(text);
}

void source_code_line_set_items(Source_Code* code, int line_index, Array<Editor_Info_Reference> editor_infos)
{
    if (line_index < 0 || line_index >= code->lines.size) return;

    auto info_list = &code->lines[line_index].item_infos;
    info_list->reset();
    for (int i = 0; i < editor_infos.size; i++) {
        auto& info = editor_infos[i];
        info_list->append(info);
    }
}

List<Editor_Info_Reference>* source_code_get_line_items(Source_Code* code, int line_index) {
    return &code->lines[line_index].item_infos;
}

void source_code_insert_line(Source_Code* code, int new_line_index)
{
    Source_Line new_line;
    new_line.item_infos = List<Editor_Info_Reference>::create(code->free_list.upcast());
    new_line.text = String_List::create_empty(code->free_list.upcast());
    code->lines.insert_ordered(new_line, new_line_index);
}

void source_code_remove_line(Source_Code* code, int line_index)
{
    Source_Line* line = &code->lines[line_index];
    line->text.deallocate();
    line->item_infos.deallocate();
    // Don't remove line if only one line exists
    if (code->lines.size > 1) {
        code->lines.remove_ordered(line_index);
    }
}

// Loading/Writing from file
void source_code_fill_from_string(Source_Code* code, String text)
{
    SCRATCH_ARENA_MAKE_SCOPED(&code->arena);
    DynString tmp_string = DynString::create(scratch_arena);

    // Reset
    source_code_reset(code);

    // Parse all lines
    int index = 0;
    int comment_indent = -1;
    while (index < text.size)
    {
        // Find line end
        int line_start_index = index;
        while (index < text.size && text.characters[index] != '\n') {
            index += 1;
        }
        int line_end_index = index;
        if (index != text.size) {
            index += 1; // Skip \n
        }

        // Turn \t into spaces
        String line_substring = string_make_substring(text, line_start_index, line_end_index);
        tmp_string.reset();
        int sub_index = 0;
        while (sub_index < line_substring.size && line_substring[sub_index] == '\t') {
            tmp_string.append("    ");
            sub_index += 1;
        }
        tmp_string.append(string_make_substring(line_substring, sub_index, line_substring.size));
        source_text_remove_invalid_whitespaces(&tmp_string);

        source_code_insert_line(code, code->lines.size);
        Source_Line* line = &code->lines[code->lines.size - 1];
        line->text.set(tmp_string.to_string());
    }

    // Because we always start with an empty line after reset, we want to remove it if other lines were added
    if (code->lines.size > 0) {
        source_code_remove_line(code, 0);
    }
}

void source_code_append_to_string(Source_Code* code, DynString* text) 
{
    for (int i = 0; i < code->lines.size; i++)
    {
        Source_Line* line = &code->lines[i];
        text->append(line->text)->append("\n");
    }
}

Source_Code* source_code_load_from_file(String filepath)
{
    SCRATCH_ARENA_MAKE_SCOPED(nullptr);
    DynString file_content = DynString::create(scratch_arena);
	bool file_loaded = file_io_load_text_file(filepath, &file_content);
	if (!file_loaded) {
	    return nullptr;
	}

	Source_Code* source_code = source_code_create();
	source_code_fill_from_string(source_code, file_content.to_string());
	return source_code;
}

// Utils
void source_text_remove_invalid_whitespaces(DynString* text)
{
    int index = 0;
    while (index < text->size)
    {
        char curr = (*text)[index];
        // Remove control characters from line, like \n, \t, \r and others
        if (curr < ' '){
            dynstring_remove_character(text, index);
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
    index.character = (line < 0 || line >= code->lines.size) ? 0 : source_code_line_get_size(code, line);
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

int Source_Code::line_count() {
    return lines.size;
}
