#include "Code_Editor.hpp"

#include "../../utility/file_io.hpp"

Code_Editor code_editor_create(Text_Renderer* text_renderer, Rendering_Core* core, Timer* timer)
{
    Code_Editor result;
    result.compiler = compiler_create(timer);
    result.text_editor = text_editor_create(text_renderer, core);

    // Load file into text editor
    Optional<String> content = file_io_load_text_file("editor_text.txt");
    if (content.available) {
        SCOPE_EXIT(string_destroy(&content.value););
        text_set_string(&result.text_editor->text, &content.value);
    }
    else {
        String sample_text = string_create_static("main :: (x : int) -> void \n{\n\n}");
        text_set_string(&result.text_editor->text, &sample_text);
    }

    return result;
}

void code_editor_destroy(Code_Editor* editor)
{
    compiler_destroy(&editor->compiler);
    text_editor_destroy(editor->text_editor);
}

void code_editor_jump_to_definition(Code_Editor* editor)
{
    if (editor->compiler.parser.errors.size != 0 || editor->compiler.analyser.errors.size != 0) return;
    // Check if we are on a word, and extract if possible
    Motion m;
    m.contains_edges = false;
    m.repeat_count = 1;
    m.motion_type = Motion_Type::WORD;
    Text_Slice result = motion_evaluate_at_position(m, editor->text_editor->cursor_position, editor->text_editor);
    if (text_position_are_equal(result.start, result.end)) return;
    String search_name = string_create_empty(32);
    SCOPE_EXIT(string_destroy(&search_name));
    text_append_slice_to_string(editor->text_editor->text, result, &search_name);

    AST_Node_Index closest_node_index = ast_parser_get_closest_node_to_text_position(
        &editor->compiler.parser, editor->text_editor->cursor_position, editor->text_editor->text
    );
    /*
    int symbol_table_index = editor->compiler.analyser.semantic_information[closest_node_index].symbol_table_index;
    Symbol_Table* symbol_table = editor->compiler.analyser.symbol_tables[symbol_table_index];
    if (symbol_table != 0) {
        Symbol* s = symbol_table_find_symbol_by_string(symbol_table, &search_name, &editor->compiler.lexer);
        if (s != 0 && s->token_index_definition != -1) {
            Token* token = &editor->compiler.lexer.tokens[s->token_index_definition];
            Text_Position result_pos = editor->compiler.lexer.tokens[s->token_index_definition].position.start;
            if (math_absolute(result_pos.line - editor->text_editor->cursor_position.line) > 5) {
                text_editor_record_jump(editor->text_editor, editor->text_editor->cursor_position, result_pos);
            }
            editor->text_editor->cursor_position = result_pos;
            editor->text_editor->horizontal_position = editor->text_editor->cursor_position.character;
            text_editor_clamp_cursor(editor->text_editor);
        }
    }
    */
}

void code_editor_update(Code_Editor* editor, Input* input, double time)
{
    for (int i = 0; i < input->key_messages.size; i++)
    {
        Key_Message* msg = &input->key_messages[i];
        // Filter out messages not meant for editor
        if (editor->text_editor->mode == Text_Editor_Mode::NORMAL) 
        {
            if (msg->character == '*' && msg->key_down) {
                code_editor_jump_to_definition(editor);
                continue;
            }
            else if (msg->key_code == Key_Code::S && msg->key_down) {
                String output = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&output););
                text_append_to_string(&editor->text_editor->text, &output);
                file_io_write_file("editor_text.txt", array_create_static((byte*)output.characters, output.size));
                logg("Saved text file!\n");
                continue;
            }
            else if (msg->key_code == Key_Code::F5) {
                continue;
            }
        }
        text_editor_handle_key_message(editor->text_editor, msg);
    }

    bool text_changed = editor->text_editor->text_changed;
    text_editor_update(editor->text_editor, input, time);

    if (text_changed || input->key_pressed[(int)Key_Code::F5])
    {
        String source_code = string_create_empty(2048);
        SCOPE_EXIT(string_destroy(&source_code));
        text_append_to_string(&editor->text_editor->text, &source_code);
        if (input->key_pressed[(int)Key_Code::F5]) {
            compiler_compile(&editor->compiler, &source_code, true);
            compiler_execute(&editor->compiler);
        }
        else {
            compiler_compile(&editor->compiler, &source_code, false);
        }

        // Do syntax highlighting
        text_editor_reset_highlights(editor->text_editor);
        vec3 KEYWORD_COLOR = vec3(0.65f, 0.4f, 0.8f);
        vec3 COMMENT_COLOR = vec3(0.0f, 1.0f, 0.0f);
        vec3 FUNCTION_COLOR = vec3(0.7f, 0.7f, 0.4f);
        vec3 IDENTIFIER_FALLBACK_COLOR = vec3(0.7f, 0.7f, 1.0f);
        vec3 VARIABLE_COLOR = vec3(0.5f, 0.5f, 0.8f);
        vec3 TYPE_COLOR = vec3(0.4f, 0.9f, 0.9f);
        vec3 PRIMITIVE_TYPE_COLOR = vec3(0.1f, 0.3f, 1.0f);
        vec3 STRING_LITERAL_COLOR = vec3(0.85f, 0.65f, 0.0f);
        vec3 ERROR_TOKEN_COLOR = vec3(1.0f, 0.0f, 0.0f);
        vec4 BG_COLOR = vec4(0);
        for (int i = 0; i < editor->compiler.lexer.tokens_with_whitespaces.size; i++)
        {
            Token t = editor->compiler.lexer.tokens_with_whitespaces[i];
            if (t.type == Token_Type::COMMENT)
                text_editor_add_highlight_from_slice(editor->text_editor, t.position, COMMENT_COLOR, BG_COLOR);
            else if (token_type_is_keyword(t.type))
                text_editor_add_highlight_from_slice(editor->text_editor, t.position, KEYWORD_COLOR, BG_COLOR);
            else if (t.type == Token_Type::STRING_LITERAL)
                text_editor_add_highlight_from_slice(editor->text_editor, t.position, STRING_LITERAL_COLOR, BG_COLOR);
            else if (t.type == Token_Type::ERROR_TOKEN)
                text_editor_add_highlight_from_slice(editor->text_editor, t.position, ERROR_TOKEN_COLOR, BG_COLOR);
            else if (t.type == Token_Type::IDENTIFIER)
            {
                AST_Node_Index nearest_node_index = ast_parser_get_closest_node_to_text_position(
                    &editor->compiler.parser, t.position.start, editor->text_editor->text
                );
                AST_Node* nearest_node = &editor->compiler.parser.nodes[nearest_node_index];
                vec3 color = IDENTIFIER_FALLBACK_COLOR;
                if (nearest_node->type == AST_Node_Type::EXPRESSION_FUNCTION_CALL ||
                    nearest_node->type == AST_Node_Type::FUNCTION) {
                    color = FUNCTION_COLOR;
                }
                if (nearest_node->type == AST_Node_Type::STRUCT) {
                    color = TYPE_COLOR;
                }
                /*
                if (editor->compiler.analyser.symbol_tables.size != 0)
                {
                    Symbol_Table* table = editor->compiler.analyser.symbol_tables[
                        editor->compiler.analyser.semantic_information[nearest_node_index].symbol_table_index
                    ];
                    Symbol* symbol = symbol_table_find_symbol(table, t.attribute.identifier_number);
                    if (symbol != 0)
                    {
                        if (symbol->symbol_type == Symbol_Type::TYPE) {
                            if (symbol->type->type == Signature_Type::PRIMITIVE) {
                                color = PRIMITIVE_TYPE_COLOR;
                            }
                            else {
                                color = TYPE_COLOR;
                            }
                        }
                        else if (symbol->symbol_type == Symbol_Type::VARIABLE) {
                            color = VARIABLE_COLOR;
                        }
                    }
                }
                */
                text_editor_add_highlight_from_slice(editor->text_editor, t.position, color, BG_COLOR);
            }
        }

        // Highlight parse errors
        if (editor->compiler.parser.errors.size > 0 || editor->compiler.analyser.errors.size > 0) {
            logg("\n\nThere were errors while compiling!\n");
        }
        for (int i = 0; i < editor->compiler.parser.errors.size; i++) {
            Compiler_Error e = editor->compiler.parser.errors[i];
            e.range.end_index += 1;
            e.range.end_index = math_minimum(editor->compiler.lexer.tokens.size - 1, e.range.end_index);
            text_editor_add_highlight_from_slice(editor->text_editor, token_range_to_text_slice(e.range, &editor->compiler), vec3(1.0f), vec4(1.0f, 0.0f, 0.0f, 0.3f));
            logg("Parse Error: %s\n", e.message);
        }
        if (editor->compiler.parser.errors.size == 0) {
            for (int i = 0; i < editor->compiler.analyser.errors.size; i++) {
                Compiler_Error e = editor->compiler.analyser.errors[i];
                text_editor_add_highlight_from_slice(editor->text_editor, token_range_to_text_slice(e.range, &editor->compiler), vec3(1.0f), vec4(1.0f, 0.0f, 0.0f, 0.3f));
                logg("Semantic Error: %s\n", e.message);
            }
        }
    }
}

void code_editor_render(Code_Editor* editor, Rendering_Core* core, Bounding_Box2 editor_box) {
    text_editor_render(editor->text_editor, core, editor_box);
}