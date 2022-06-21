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

Source_Code source_code_create()
{
    Source_Code result;
    result.lines = dynamic_array_create_empty<Source_Line>(1);
    source_code_insert_line_empty(&result, 0, 0);
    return result;
}

void source_code_reset(Source_Code* code)
{
    for (int i = 0; i < code->lines.size; i++) {
        source_line_destroy(&code->lines[i]);
    }
    dynamic_array_reset(&code->lines);
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
        lexer_tokens_to_text(&line.tokens, &line.text);
    }
}




