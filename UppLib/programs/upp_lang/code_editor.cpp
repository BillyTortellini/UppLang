#include "Code_Editor.hpp"

#include "../../utility/file_io.hpp"
#include "../../rendering/renderer_2D.hpp"

vec3 KEYWORD_COLOR = vec3(0.65f, 0.4f, 0.8f);
vec3 COMMENT_COLOR = vec3(0.0f, 1.0f, 0.0f);
vec3 FUNCTION_COLOR = vec3(0.7f, 0.7f, 0.4f);
vec3 MODULE_COLOR = vec3(0.3f, 0.6f, 0.7f);
vec3 IDENTIFIER_FALLBACK_COLOR = vec3(0.7f, 0.7f, 1.0f);
vec3 TEXT_COLOR = vec3(1.0f);
vec3 VARIABLE_COLOR = vec3(0.5f, 0.5f, 0.8f);
vec3 TYPE_COLOR = vec3(0.4f, 0.9f, 0.9f);
vec3 PRIMITIVE_TYPE_COLOR = vec3(0.1f, 0.3f, 1.0f);
vec3 STRING_LITERAL_COLOR = vec3(0.85f, 0.65f, 0.0f);
vec4 BG_COLOR = vec4(0);
vec4 ERROR_BG_COLOR = vec4(0.7f, 0.0f, 0.0f, 1.0f);

Code_Editor code_editor_create(Text_Renderer* text_renderer, Rendering_Core* core, Timer* timer)
{
    Code_Editor result;
    result.compiler = compiler_create(timer);
    result.text_editor = text_editor_create(text_renderer, core);
    result.context_info = string_create_empty(16);
    result.show_context_info = false;
    result.context_info_pos = vec2(0.0f);

    // Load file into text editor
    Optional<String> content = file_io_load_text_file("upp_code/editor_text.txt");
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
    string_destroy(&editor->context_info);
}

Optional<int> code_editor_get_closest_token_to_text_position(Code_Editor* editor, Text_Position pos)
{
    if (editor->compiler.lexer.tokens.size == 0) return optional_make_failure<int>();
    for (int i = 0; i < editor->compiler.lexer.tokens.size; i++)
    {
        Token* t = &editor->compiler.lexer.tokens[i];
        if (t->position.start.line == pos.line && t->position.start.character <= pos.character && t->position.end.character >= pos.character) {
            return optional_make_success(i);
        }
    }
    return optional_make_failure<int>();
}

AST_Node* code_editor_get_closest_node_to_text_position(Code_Editor* editor, Text_Position pos)
{
    AST_Parser* parser = &editor->compiler.parser;
    AST_Node* closest = parser->root_node;
    while (true)
    {
        bool continue_search = true;
        AST_Node* child = closest->child_start;
        while (child != 0 && continue_search)
        {
            Token* token_start, *token_end;
            {
                int min = 0;
                int max = parser->lexer->tokens.size;
                int start_index = child->token_range.start_index;
                int end_index = child->token_range.end_index;
                if (start_index == -1 || end_index == -1) continue;
                start_index = math_clamp(start_index, min, max);
                end_index = math_clamp(end_index, min, max);
                token_start = &parser->lexer->tokens[start_index];
                token_end = &parser->lexer->tokens[math_maximum(0, end_index - 1)];
            }

            Text_Slice node_slice = text_slice_make(token_start->position.start, token_end->position.end);
            if (text_slice_contains_position(node_slice, pos, editor->text_editor->text)) {
                closest = child;
                continue_search = false;
            }
        }
        if (continue_search) break;
    }
    return closest;
}

Symbol_Table* code_editor_find_symbol_table_of_node(Code_Editor* editor, AST_Node* node)
{
    Symbol_Table* nearest_table = nullptr;
    while (node != nullptr)
    {
        Symbol_Table** table = hashtable_find_element(&editor->compiler.analyser.ast_to_symbol_table, node);
        if (table != 0) {
            return *table;
        }
        node = node->parent;
    }
    return 0;
}

Symbol_Table* code_editor_find_symbol_table_of_text_position(Code_Editor* editor, Text_Position pos)
{
    AST_Node* closest_node = code_editor_get_closest_node_to_text_position(editor, pos);
    return code_editor_find_symbol_table_of_node(editor, closest_node);
}

void code_editor_jump_to_definition(Code_Editor* editor)
{
    AST_Node* closest_node = code_editor_get_closest_node_to_text_position(editor, editor->text_editor->cursor_position);
    if (!ast_node_type_is_identifier_node(closest_node->type)) {
        return;
    }
    while (ast_node_type_is_identifier_node(closest_node->parent->type)) {
        closest_node = closest_node->parent;
    }

    Symbol_Table* nearest_table = code_editor_find_symbol_table_of_node(editor, closest_node);
    if (nearest_table != 0)
    {
        Identifier_Analysis_Result result = semantic_analyser_analyse_identifier_node(&editor->compiler.analyser, nearest_table, closest_node, false);
        if (result.type != Analysis_Result_Type::SUCCESS) return;
        Symbol* symbol = result.options.symbol;
        if (symbol->definition_node != 0) {
            Token* token = &editor->compiler.lexer.tokens[symbol->definition_node->token_range.start_index];
            Text_Position result_pos = token->position.start;
            if (math_absolute(result_pos.line - editor->text_editor->cursor_position.line) > 5) {
                text_editor_record_jump(editor->text_editor, editor->text_editor->cursor_position, result_pos);
            }
            editor->text_editor->cursor_position = result_pos;
            editor->text_editor->horizontal_position = editor->text_editor->cursor_position.character;
            text_editor_clamp_cursor(editor->text_editor);
        }
    }
}

void highlight_identifiers(Code_Editor* editor, AST_Node* node, Symbol_Table* symbol_table)
{
    Token_Range node_range = node->token_range;
    // Variables definition, module def, funciton def, parameters
    if (node->type == AST_Node_Type::MODULE || node->type == AST_Node_Type::MODULE_TEMPLATED) {
        Token_Range r = node_range;
        r.start_index += 1;
        r.end_index = r.start_index + 1;
        text_editor_add_highlight_from_slice(editor->text_editor, token_range_to_text_slice(r, &editor->compiler), MODULE_COLOR, BG_COLOR);
    }
    else if (node->type == AST_Node_Type::STRUCT) {
        Token_Range r = node_range;
        r.end_index = r.start_index + 1;
        text_editor_add_highlight_from_slice(editor->text_editor, token_range_to_text_slice(r, &editor->compiler), TYPE_COLOR, BG_COLOR);
    }
    else if (node->type == AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN ||
        node->type == AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER ||
        node->type == AST_Node_Type::STATEMENT_VARIABLE_DEFINITION) 
    {
        Token_Range r = node_range;
        r.end_index = r.start_index + 1;
        text_editor_add_highlight_from_slice(editor->text_editor, token_range_to_text_slice(r, &editor->compiler), VARIABLE_COLOR, BG_COLOR);
    }
    else if (node->type == AST_Node_Type::FUNCTION) {
        Token_Range r = node_range;
        r.end_index = r.start_index + 1;
        text_editor_add_highlight_from_slice(editor->text_editor, token_range_to_text_slice(r, &editor->compiler), FUNCTION_COLOR, BG_COLOR);
    }
    else if (node->type == AST_Node_Type::NAMED_PARAMETER) {
        Token_Range r = node_range;
        r.end_index = r.start_index + 1;
        text_editor_add_highlight_from_slice(editor->text_editor, token_range_to_text_slice(r, &editor->compiler), VARIABLE_COLOR, BG_COLOR);
    }
    else if (ast_node_type_is_identifier_node(node->type))
    {
        Identifier_Analysis_Result result = semantic_analyser_analyse_identifier_node(&editor->compiler.analyser, symbol_table, node, false);
        switch (result.type)
        {
        case Analysis_Result_Type::SUCCESS:
        {
            while (node != 0)
            {
                AST_Node* child_node = 0;
                if (node->type == AST_Node_Type::IDENTIFIER_PATH || node->type == AST_Node_Type::IDENTIFIER_PATH_TEMPLATED)
                {
                    // Highlight module name
                    Token_Range r = node_range;
                    r.end_index = r.start_index + 1;
                    text_editor_add_highlight_from_slice(editor->text_editor, token_range_to_text_slice(r, &editor->compiler), MODULE_COLOR, BG_COLOR);
                    child_node = node->type == AST_Node_Type::IDENTIFIER_PATH ? node->child_start : node->child_start->neighbor;
                }
                else
                {
                    // Highlight identifier name
                    Symbol* symbol = result.options.symbol;
                    vec3 color = IDENTIFIER_FALLBACK_COLOR;
                    switch (symbol->type)
                    {
                    case Symbol_Type::FUNCTION: color = FUNCTION_COLOR; break;
                    case Symbol_Type::MODULE: color = MODULE_COLOR; break;
                    case Symbol_Type::TYPE: color = TYPE_COLOR; break;
                    case Symbol_Type::VARIABLE: color = VARIABLE_COLOR; break;
                    default: panic("HEY");
                    }
                    Token_Range r = node_range;
                    r.end_index = r.start_index + 1;
                    text_editor_add_highlight_from_slice(editor->text_editor, token_range_to_text_slice(r, &editor->compiler), color, BG_COLOR);
                }

                if (node->type == AST_Node_Type::IDENTIFIER_NAME_TEMPLATED || node->type == AST_Node_Type::IDENTIFIER_PATH_TEMPLATED) 
                {
                    // Highlight template params
                    if (node->type == AST_Node_Type::IDENTIFIER_NAME_TEMPLATED) {
                        highlight_identifiers(editor, node->child_start, symbol_table);
                    }
                    else {
                        highlight_identifiers(editor, node->child_start->neighbor, symbol_table);
                    }
                }

                if (child_node == 0) {
                    node = 0;
                }
                else {
                    node_range = child_node->token_range;
                    node = child_node;
                }
            }
            break;
        }
        case Analysis_Result_Type::ERROR_OCCURED:
            break;
        case Analysis_Result_Type::DEPENDENCY:
            break;
        default: panic("HEY");
        }

        return;
    }

    Symbol_Table** new_table = hashtable_find_element(&editor->compiler.analyser.ast_to_symbol_table, node);
    if (new_table != 0) {
        symbol_table = *new_table;
    }
    AST_Node* child = node->child_start;
    while (child != 0) {
        highlight_identifiers(editor, child, symbol_table);
        child = child->neighbor;
    }
}

void code_editor_update(Code_Editor* editor, Input* input, double time)
{
    Timer* timer = editor->compiler.timer;
    bool timing_enabled = false;
    double time_update_start = timer_current_time_in_seconds(timer);

    // Execute editor commands
    for (int i = 0; i < input->key_messages.size; i++)
    {
        Key_Message* msg = &input->key_messages[i];
        // Filter out messages not meant for editor
        if (editor->text_editor->mode == Text_Editor_Mode::NORMAL && editor->text_editor->normal_mode_incomplete_command.size == 0)
        {
            if (msg->character == '*' && msg->key_down) {
                code_editor_jump_to_definition(editor);
                continue;
            }
            else if (msg->key_code == Key_Code::S && msg->key_down || msg->key_code == Key_Code::F5) {
                continue;
            }
            else if (msg->key_code == Key_Code::F6 && msg->key_down) {
                continue;
            }
            else if (msg->key_code == Key_Code::B && msg->ctrl_down && msg->key_down) {
                continue;
            }
        }
        text_editor_handle_key_message(editor->text_editor, msg);
    }

    bool save_text_file = input->key_pressed[(int)Key_Code::S];
    bool shortcut_build = false;
    bool shortcut_execute = false;
    if (input->key_pressed[(int)Key_Code::F5]) {
        shortcut_build = true;
        shortcut_execute = true;
    }
    if (input->key_pressed[(int)Key_Code::F6]) {
        shortcut_execute = true;
    }
    if (input->key_pressed[(int)Key_Code::B] && input->key_down[(int)Key_Code::CTRL]) {
        shortcut_build = true;
        save_text_file = true;
    }

    if (save_text_file) {
        String output = string_create_empty(256);
        SCOPE_EXIT(string_destroy(&output););
        text_append_to_string(&editor->text_editor->text, &output);
        file_io_write_file("upp_code/editor_text.txt", array_create_static((byte*)output.characters, output.size));
        logg("Saved text file!\n");
    }

    bool text_changed = editor->text_editor->text_changed;
    text_editor_update(editor->text_editor, input, time);

    // Compile
    if (text_changed || shortcut_build)
    {
        String source_code = string_create_empty(2048);
        SCOPE_EXIT(string_destroy(&source_code));
        text_append_to_string(&editor->text_editor->text, &source_code);
        compiler_compile(&editor->compiler, &source_code, shortcut_build);

        // Print errors
        if (editor->compiler.parser.errors.size > 0 || editor->compiler.analyser.errors.size > 0) {
            logg("\n\nThere were errors while compiling!\n");
        }
        for (int i = 0; i < editor->compiler.parser.errors.size; i++) {
            Compiler_Error e = editor->compiler.parser.errors[i];
            logg("Parse Error: %s\n", e.message);
        }
        if (editor->compiler.parser.errors.size == 0)
        {
            String tmp = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&tmp));
            for (int i = 0; i < editor->compiler.analyser.errors.size; i++)
            {
                Semantic_Error e = editor->compiler.analyser.errors[i];
                semantic_error_append_to_string(&editor->compiler.analyser, e, &tmp);
                logg("Semantic Error: %s\n", tmp.characters);
                string_reset(&tmp);
            }
        }
    }

    // Execute
    if (shortcut_execute) {
        compiler_execute(&editor->compiler);
    }

    double time_input_read_end = timer_current_time_in_seconds(timer);

    // Do syntax highlighting
    {
        text_editor_reset_highlights(editor->text_editor);
        for (int i = 0; i < editor->compiler.lexer.tokens_with_decoration.size; i++)
        {
            Token t = editor->compiler.lexer.tokens_with_decoration[i];
            if (t.type == Token_Type::COMMENT)
                text_editor_add_highlight_from_slice(editor->text_editor, t.position, COMMENT_COLOR, BG_COLOR);
            else if (token_type_is_keyword(t.type))
                text_editor_add_highlight_from_slice(editor->text_editor, t.position, KEYWORD_COLOR, BG_COLOR);
            else if (t.type == Token_Type::STRING_LITERAL)
                text_editor_add_highlight_from_slice(editor->text_editor, t.position, STRING_LITERAL_COLOR, BG_COLOR);
            else if (t.type == Token_Type::ERROR_TOKEN)
                text_editor_add_highlight_from_slice(editor->text_editor, t.position, TEXT_COLOR, ERROR_BG_COLOR);
        }

        if (editor->compiler.analyser.program != 0 && editor->compiler.analyser.program->root_module != 0) {
            highlight_identifiers(editor, editor->compiler.parser.root_node, editor->compiler.analyser.program->root_module->symbol_table);
        }

        for (int i = 0; i < editor->compiler.parser.errors.size; i++) {
            Compiler_Error e = editor->compiler.parser.errors[i];
            text_editor_add_highlight_from_slice(editor->text_editor, token_range_to_text_slice(e.range, &editor->compiler), TEXT_COLOR, ERROR_BG_COLOR);
        }
    }

    double time_syntax_end = timer_current_time_in_seconds(timer);

    // Do context highlighting
    {
        bool search_context = true;
        if (editor->compiler.lexer.tokens.size == 0) search_context = false;
        Optional<int> closest_token = code_editor_get_closest_token_to_text_position(editor, editor->text_editor->cursor_position);
        int closest_index = -1;
        if (closest_token.available) closest_index = closest_token.value;

        for (int i = 0; i < editor->compiler.parser.errors.size; i++) {
            Compiler_Error e = editor->compiler.parser.errors[i];
            if (e.range.start_index <= closest_index && closest_index < e.range.end_index) {
                search_context = false;
                editor->show_context_info = true;
                editor->context_info_pos = text_editor_get_character_bounding_box(editor->text_editor, editor->text_editor->cursor_position).min;
                string_reset(&editor->context_info);
                string_append_formated(&editor->context_info, e.message);
                break;
            }
        }

        if (editor->compiler.parser.errors.size == 0)
        {
            Dynamic_Array<Token_Range> error_locations = dynamic_array_create_empty<Token_Range>(4);
            SCOPE_EXIT(dynamic_array_destroy(&error_locations));
            for (int i = 0; i < editor->compiler.analyser.errors.size; i++)
            {
                Semantic_Error e = editor->compiler.analyser.errors[i];
                dynamic_array_reset(&error_locations);
                semantic_error_get_error_location(&editor->compiler.analyser, e, &error_locations);
                for (int j = 0; j < error_locations.size; j++)
                {
                    Token_Range range = error_locations[j];
                    text_editor_add_highlight_from_slice(editor->text_editor,
                        token_range_to_text_slice(range, &editor->compiler), vec3(1.0f), vec4(1.0f, 0.0f, 0.0f, 0.3f)
                    );
                    if (search_context && range.start_index <= closest_index && closest_index < range.end_index)
                    {
                        search_context = false;
                        editor->show_context_info = true;
                        editor->context_info_pos = text_editor_get_character_bounding_box(editor->text_editor, editor->text_editor->cursor_position).min;
                        string_reset(&editor->context_info);
                        semantic_error_append_to_string(&editor->compiler.analyser, e, &editor->context_info);
                    }
                }
            }
        }

        // Search if we are in a function call
        if (search_context)
        {
            /*
            int node_cursor_index = code_editor_get_closest_node_to_text_position(editor, editor->text_editor->cursor_position);
            int i = node_cursor_index;
            while (i != 0)
            {
                AST_Node* node = &editor->compiler.parser.nodes[i];
                if (node->type == AST_Node_Type::EXPRESSION_FUNCTION_CALL)
                {
                    Symbol_Table* table = code_editor_find_symbol_table_of_node(editor, i);
                    int rewind_index = editor->compiler.analyser.errors.size;
                    SCOPE_EXIT(dynamic_array_rollback_to_size(&editor->compiler.analyser.errors, rewind_index));
                    int identifier_node_index = node->children[0];
                    Identifier_Analysis_Result result = semantic_analyser_analyse_identifier_node(
                        &editor->compiler.analyser, table, &editor->compiler.parser, identifier_node_index, false
                    );
                    if (result.type == Analysis_Result_Type::SUCCESS) {
                        search_context = false;
                        editor->show_context_info = true;
                        editor->context_info_pos = text_editor_get_character_bounding_box(editor->text_editor, editor->text_editor->cursor_position).min;
                        string_reset(&editor->context_info);
                        symbol_append_to_string(&result.options.symbol, &editor->context_info, &editor->compiler.analyser);
                    }
                    break;
                }
                i = node->parent;
            }
            */
        }

        // Search if we are above an identifier, identifier_path, type_identifier, expression_variable_read
        if (search_context)
        {
            /*
            int node_cursor_index = code_editor_get_closest_node_to_text_position(editor, editor->text_editor->cursor_position);
            AST_Node* node = &editor->compiler.parser.nodes[node_cursor_index];
            int index = node_cursor_index;
            if (ast_node_type_is_identifier_node(node->type))
            {
                while (ast_node_type_is_identifier_node(editor->compiler.parser.nodes[node->parent].type)) {
                    index = node->parent;
                    node = &editor->compiler.parser.nodes[node->parent];
                }
                Symbol_Table* table = code_editor_find_symbol_table_of_text_position(editor, editor->text_editor->cursor_position);
                int rewind_index = editor->compiler.analyser.errors.size;
                SCOPE_EXIT(dynamic_array_rollback_to_size(&editor->compiler.analyser.errors, rewind_index));
                Identifier_Analysis_Result result = semantic_analyser_analyse_identifier_node(
                    &editor->compiler.analyser, table, &editor->compiler.parser, index, false
                );
                if (result.type == Analysis_Result_Type::SUCCESS) {
                    search_context = false;
                    editor->show_context_info = true;
                    editor->context_info_pos = text_editor_get_character_bounding_box(editor->text_editor, editor->text_editor->cursor_position).min;
                    string_reset(&editor->context_info);
                    symbol_append_to_string(&result.options.symbol, &editor->context_info, &editor->compiler.analyser);
                }
            }
            */
        }

        if (search_context) {
            editor->show_context_info = false;
        }
    }

    double time_context_end = timer_current_time_in_seconds(timer);

    if (timing_enabled) {
        logg("EDITOR_TIMING:\n---------------\n");
        logg(" input        ... %3.2fms\n", 1000.0f * (float)(time_input_read_end - time_update_start));
        logg(" syntax       ... %3.2fms\n", 1000.0f * (float)(time_syntax_end - time_input_read_end));
        logg(" context      ... %3.2fms\n", 1000.0f * (float)(time_context_end - time_syntax_end));
        logg(" sum          ... %3.2fms\n", 1000.0f * (float)(time_context_end - time_update_start));
    }

    // Highlight current node index
    /*
    if (editor->compiler.parser.errors.size == 0 && editor->compiler.analyser.errors.size == 0)
    {
        int node_cursor_index = code_editor_get_closest_node_to_text_position(editor, editor->text_editor->cursor_position);
        text_editor_add_highlight_from_slice(
            editor->text_editor,
            token_range_to_text_slice(editor->compiler.parser.token_mapping[node_cursor_index], &editor->compiler),
            vec3(1.0f), vec4(vec3(0.3f), 1.0f)
        );
        AST_Node* node = &editor->compiler.parser.nodes[node_cursor_index];
        Token* token_start = &editor->compiler.lexer.tokens[editor->compiler.parser.token_mapping[node_cursor_index].start_index];
        editor->show_context_info = true;
        editor->context_info_pos = text_editor_get_character_bounding_box(editor->text_editor, token_start->position.start).min;
        string_reset(&editor->context_info);
        string_append_formated(&editor->context_info, "%s", ast_node_type_to_string(node->type).characters);

        Optional<int> next_token_index = code_editor_get_closest_token_to_text_position(editor, editor->text_editor->cursor_position);
        if (next_token_index.available)
        {
            string_append_formated(&editor->context_info, "\n");
            for (int i = next_token_index.value; i < editor->compiler.lexer.tokens.size && i < next_token_index.value + 5; i++) {
                Token* t = &editor->compiler.lexer.tokens[i];
                string_append_formated(&editor->context_info, token_type_to_string(t->type));
                string_append_formated(&editor->context_info, " ");
            }
        }
        editor->show_context_info = false;


        //node cursor_index = code_editor_get_closest_node_to_text_position(editor, editor->text_editor->cursor_position);
        //node_cursor_index = ast_parser_get_closest_node_to_text_position(
            //&editor->compiler.parser, editor->text_editor->cursor_position, editor->text_editor->text
        //);
        if (input->key_pressed[(int)Key_Code::M]) {
            node_cursor_index++;
        }
        if (input->key_pressed[(int)Key_Code::N]) {
            node_cursor_index--;
        }
        node_cursor_index = math_clamp(node_cursor_index, 0, editor->compiler.parser.nodes.size);
        text_editor_add_highlight_from_slice(editor->text_editor,
            token_range_to_text_slice(editor->compiler.parser.token_mapping[node_cursor_index], &editor->compiler),
            vec3(1.0f), vec4(0.2f, 0.4f, 0.2f, 1.0f)
        );
    }
    else {
        editor->show_context_info = false;
    }
        */
}

void code_editor_render(Code_Editor* editor, Rendering_Core* core, Bounding_Box2 editor_box)
{
    text_editor_render(editor->text_editor, core, editor_box);
    if (editor->show_context_info)
    {
        float text_height = editor->text_editor->last_text_height * 0.8f;
        Text_Layout* layout = text_renderer_calculate_text_layout(editor->text_editor->renderer, &editor->context_info, text_height, 1.0f);
        vec2 text_pos = editor->context_info_pos - vec2(0.0f, layout->size.y);
        text_renderer_add_text_from_layout(editor->text_editor->renderer, layout, text_pos);
        text_editor_draw_bounding_box(editor->text_editor, core, bounding_box_2_make_min_max(text_pos, text_pos + layout->size), vec4(0.2f, 0.2f, 0.2f, 1.0f));
        text_renderer_render(editor->text_editor->renderer, core);
    }
}