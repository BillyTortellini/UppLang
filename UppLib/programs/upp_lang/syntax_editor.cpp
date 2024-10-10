#include "syntax_editor.hpp"

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/renderer_2d.hpp"
#include "../../utility/file_io.hpp"
#include "../../utility/character_info.hpp"
#include "../../utility/fuzzy_search.hpp"

#include "../../win32/input.hpp"
#include "syntax_colors.hpp"
#include "compiler.hpp"
#include "ast.hpp"
#include "symbol_table.hpp"
#include "semantic_analyser.hpp"
#include "parser.hpp"
#include "source_code.hpp"
#include "code_history.hpp"

// Editor
struct Error_Display
{
    String message;
    Token_Range range;
    bool from_main_source;
};

Error_Display error_display_make(String msg, Token_Range range, bool from_main_source) {
    Error_Display result;
    result.message = msg;
    result.range = range;
    result.from_main_source = from_main_source;
    return result;
}

enum class Editor_Mode
{
    NORMAL,
    INSERT,
};

struct Input_Replay
{
    String code_state_initial;
    String code_state_afterwards;
    Dynamic_Array<Key_Message> recorded_inputs;
    bool currently_recording;
    bool currently_replaying;
    Editor_Mode start_mode;
    Text_Index cursor_start;
};


struct Syntax_Editor
{
    // Editing
    Editor_Mode mode;
    Source_Code* code;
    Text_Index cursor;
    const char* file_path;

    Code_History history;
    History_Timestamp last_token_synchronized;
    bool code_changed_since_last_compile;
    bool last_compile_was_with_code_gen;

    bool space_before_cursor;
    bool space_after_cursor;

    Dynamic_Array<String> code_completion_suggestions;
    Input_Replay input_replay;

    // Rendering
    String context_text;
    Dynamic_Array<Error_Display> errors;
    Dynamic_Array<Token_Range> token_range_buffer;
    int camera_start_line;

    Input* input;
    Rendering_Core* rendering_core;
    Renderer_2D* renderer_2D;
    Text_Renderer* text_renderer;
    vec2 character_size;
};

enum class Insert_Command_Type
{
    IDENTIFIER_LETTER,
    NUMBER_LETTER,
    DELIMITER_LETTER,
    SPACE,
    BACKSPACE,

    EXIT_INSERT_MODE,
    ENTER,
    ENTER_REMOVE_ONE_INDENT,
    ADD_INDENTATION,
    REMOVE_INDENTATION,
    MOVE_LEFT,
    MOVE_RIGHT,
    INSERT_CODE_COMPLETION,
};

struct Insert_Command
{
    Insert_Command_Type type;
    char letter;
};

enum class Normal_Command
{
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_UP,
    MOVE_DOWN,
    MOVE_LINE_START,
    MOVE_LINE_END,
    ADD_LINE_ABOVE,
    ADD_LINE_BELOW,
    INSERT_BEFORE,
    INSERT_AFTER,
    INSERT_AT_LINE_START,
    INSERT_AT_LINE_END,
    CHANGE_CHARACTER,
    DELETE_CHARACTER,
    UNDO,
    REDO,
};

// Globals
static Syntax_Editor syntax_editor;


// InputReplay
Input_Replay input_replay_create()
{
    Input_Replay result;
    result.code_state_initial = string_create_empty(1);
    result.code_state_afterwards = string_create_empty(1);
    result.recorded_inputs = dynamic_array_create<Key_Message>(1);
    result.currently_recording = false;
    result.currently_replaying = false;
    return result;
}

void input_replay_destroy(Input_Replay* replay)
{
    string_destroy(&replay->code_state_afterwards);
    string_destroy(&replay->code_state_initial);
    dynamic_array_destroy(&replay->recorded_inputs);
}

// Helpers
Token token_make_dummy() {
    Token t;
    t.type = Token_Type::INVALID;
    t.start_index = 0;
    t.end_index = 0;
    return t;
}

int get_cursor_token_index(bool after_cursor) {
    auto& c = syntax_editor.cursor;
    auto line = source_code_get_line(syntax_editor.code, c.line);
    return character_index_to_token(&line->tokens, c.character, after_cursor);
}

Token get_cursor_token(bool after_cursor)
{
    auto& c = syntax_editor.cursor;
    int tok_index = get_cursor_token_index(after_cursor);
    auto tokens = source_code_get_line(syntax_editor.code, c.line)->tokens;
    if (tok_index >= tokens.size) return token_make_dummy();
    return tokens[tok_index];
}

void syntax_editor_set_text(String string)
{
    auto& editor = syntax_editor;
    editor.cursor = text_index_make(0, 0);
    source_code_fill_from_string(editor.code, string);
    source_code_tokenize(editor.code);
    code_history_reset(&editor.history);
    editor.last_token_synchronized = history_get_timestamp(&editor.history);
    editor.code_changed_since_last_compile = true;
    // compiler_compile_clean(editor.code, Compile_Type::ANALYSIS_ONLY, string_create(syntax_editor.file_path));
}

void syntax_editor_load_text_file(const char* filename)
{
    auto& editor = syntax_editor;
    Optional<String> content = file_io_load_text_file(filename);
    SCOPE_EXIT(file_io_unload_text_file(&content););

    String result;
    if (content.available) {
        result = content.value;
        editor.file_path = filename;
        syntax_editor_set_text(result);
    }
    else {
        result = string_create("main :: (x : int) -> void \n{\n\n}");
        SCOPE_EXIT(string_destroy(&result));
        syntax_editor_set_text(result);
    }
}

void syntax_editor_save_text_file()
{
    String whole_text = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&whole_text));
    source_code_append_to_string(syntax_editor.code, &whole_text);
    auto success = file_io_write_file(syntax_editor.file_path, array_create_static((byte*)whole_text.characters, whole_text.size));
    if (!success) {
        logg("Saving file failed for path \"%s\"\n", syntax_editor.file_path);
    }
    else {
        logg("Saved file \"%s\"!\n", syntax_editor.file_path);
    }
}

void syntax_editor_sanitize_cursor()
{
    auto& editor = syntax_editor;
    auto& cursor = editor.cursor;
    auto& code = editor.code;
    if (cursor.line < 0) cursor.line = 0;
    if (cursor.line >= code->line_count) {
        cursor.line = code->line_count - 1;
    }
    auto& text = source_code_get_line(code, cursor.line)->text;
    cursor.character = math_clamp(cursor.character, 0, editor.mode == Editor_Mode::INSERT ? text.size : math_maximum(0, text.size - 1));

    if (string_test_char(text, cursor.character, ' ')) {
        editor.space_after_cursor = false;
    }
    if (string_test_char(text, cursor.character - 1, ' ')) {
        editor.space_before_cursor = false;
    }
}

bool syntax_editor_sanitize_line(int line_index)
{
    Source_Line* line = source_code_get_line(syntax_editor.code, line_index);
    if (line->is_comment) {
        return false;
    }

    // Remove all Spaces except space critical ones
    auto& editor = syntax_editor;
    auto& text = line->text;
    auto pos = editor.cursor.character;

    bool changed = false;
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
            history_delete_char(&editor.history, text_index_make(line_index, index));
            changed = true;
            if (pos > index) {
                pos -= 1;
            }
        }
        else {
            index += 1;
        }
    }

    if (editor.cursor.line == line_index)
    {
        editor.cursor.character = pos;
        syntax_editor_sanitize_cursor();
    }
    return changed;
}

void syntax_editor_synchronize_tokens()
{
    auto& editor = syntax_editor;
    // Get changes since last sync
    Dynamic_Array<Code_Change> changes = dynamic_array_create<Code_Change>(1);
    SCOPE_EXIT(dynamic_array_destroy(&changes));
    auto now = history_get_timestamp(&editor.history);
    history_get_changes_between(&editor.history, editor.last_token_synchronized, now, &changes);
    editor.last_token_synchronized = now;
    if (changes.size != 0) {
        editor.code_changed_since_last_compile = true;
    }

    // Find out which lines were changed
    auto line_changes = dynamic_array_create<int>();
    SCOPE_EXIT(dynamic_array_destroy(&line_changes));
    auto helper_add_delete_line_item = [&line_changes](int new_line_index, bool is_insert) -> void 
    {
        for (int i = 0; i < line_changes.size; i++) {
            auto& line_index = line_changes[i];
            if (is_insert) {
                if (line_index >= new_line_index) {
                    line_index += 1;
                }
            }
            else {
                if (line_index == new_line_index) {
                    dynamic_array_swap_remove(&line_changes, i);
                    i = i - 1;
                    continue;
                }
                if (line_index > new_line_index) {
                    line_index -= 1;
                }
            }
        }
        // Note: Only changed lines need to be added to line_changes, so we don't add additions here
    };

    for (int i = 0; i < changes.size; i++)
    {
        auto& change = changes[i];
        switch (change.type)
        {
        case Code_Change_Type::LINE_INSERT: {
            helper_add_delete_line_item(change.options.line_insert.line_index, change.apply_forwards);
            break;
        }
        case Code_Change_Type::CHAR_INSERT:
        case Code_Change_Type::TEXT_INSERT:
        {
            int changed_line = change.type == Code_Change_Type::CHAR_INSERT ? change.options.char_insert.index.line : change.options.text_insert.index.line;
            bool found = false;
            for (int j = 0; j < line_changes.size; j++) {
                if (line_changes[j] ==  changed_line) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                dynamic_array_push_back(&line_changes, changed_line);
            }
            break;
        }
        case Code_Change_Type::LINE_INDENTATION_CHANGE: break;
        default: panic("");
        }
    }

    // Update changed lines
    for (int i = 0; i < line_changes.size; i++)
    {
        auto& index = line_changes[i];
        bool changed = syntax_editor_sanitize_line(index);
        assert(!changed, "Syntax editor has to make sure that lines are sanitized after edits!");

        source_code_tokenize_line(syntax_editor.code, line_changes[i]);
        //logg("Synchronized: %d/%d\n", index.block_index.block_index, index.line_index);
    }
}

void syntax_editor_synchronize_with_compiler(bool generate_code)
{
    syntax_editor_synchronize_tokens();
    auto& editor = syntax_editor;
    
    if (!editor.code_changed_since_last_compile)
    {
        if (!generate_code) {
            return;
        }
        else if (editor.last_compile_was_with_code_gen) {
            return;
        }
    }
    syntax_editor.code_changed_since_last_compile = false;
    syntax_editor.last_compile_was_with_code_gen = generate_code;
    //compiler_compile_incremental(&syntax_editor.history, (generate_code ? Compile_Type::BUILD_CODE : Compile_Type::ANALYSIS_ONLY));
    compiler_compile_clean(syntax_editor.code, (generate_code ? Compile_Type::BUILD_CODE : Compile_Type::ANALYSIS_ONLY), string_create(syntax_editor.file_path));

    // Collect errors from all compiler stages
    {
        for (int i = 0; i < editor.errors.size; i++) {
            string_destroy(&editor.errors[i].message);
        }
        dynamic_array_reset(&editor.errors);

        // Parse Errors
        for (int i = 0; i < compiler.program_sources.size; i++)
        {
            auto& parse_errors = compiler.program_sources[i]->parsed_code->error_messages;
            for (int j = 0; j < parse_errors.size; j++) {
                auto& error = parse_errors[j];
                dynamic_array_push_back(
                    &editor.errors, 
                    error_display_make(string_create_static(error.msg), error.range, compiler.program_sources[i]->origin == Code_Origin::MAIN_PROJECT)
                );
            }
        }

        auto error_ranges = dynamic_array_create<Token_Range>(1);
        SCOPE_EXIT(dynamic_array_destroy(&error_ranges));

        // Semantic Analysis Errors
        for (int i = 0; i < compiler.semantic_analyser->errors.size; i++)
        {
            auto& error = compiler.semantic_analyser->errors[i];
            auto& node = error.error_node;
            dynamic_array_reset(&error_ranges);
            if (node == 0) continue;
            if (compiler_find_ast_program_source(node) != compiler.main_source) continue;
            Parser::ast_base_get_section_token_range(editor.code, node, error.section, &error_ranges);
            for (int j = 0; j < error_ranges.size; j++) {
                auto& range = error_ranges[j];
                assert(token_index_compare(range.start, range.end) >= 0, "hey");
                String string = string_create_empty(4);
                semantic_error_append_to_string(error, &string);
                dynamic_array_push_back(&editor.errors, error_display_make(string, range, true));
            }
        }
    }
}


// Code Queries
Analysis_Pass* code_query_get_analysis_pass(AST::Node* base);
Symbol* code_query_get_ast_node_symbol(AST::Node* base)
{
    if (base->type != AST::Node_Type::DEFINITION_SYMBOL &&
        base->type != AST::Node_Type::PATH_LOOKUP &&
        base->type != AST::Node_Type::SYMBOL_LOOKUP &&
        base->type != AST::Node_Type::PARAMETER) {
        return 0;
    }

    auto pass = code_query_get_analysis_pass(base);
    if (pass == 0) return 0;
    switch (base->type)
    {
    case AST::Node_Type::DEFINITION_SYMBOL: {
        auto info = pass_get_node_info(pass, AST::downcast<AST::Definition_Symbol>(base), Info_Query::TRY_READ);
        return info == 0 ? 0 : info->symbol;
    }
    case AST::Node_Type::SYMBOL_LOOKUP: {
        auto info = pass_get_node_info(pass, AST::downcast<AST::Symbol_Lookup>(base), Info_Query::TRY_READ);
        return info == 0 ? 0 : info->symbol;
    }
    case AST::Node_Type::PATH_LOOKUP: {
        auto info = pass_get_node_info(pass, AST::downcast<AST::Path_Lookup>(base), Info_Query::TRY_READ);
        return info == 0 ? 0 : info->symbol;
    }
    case AST::Node_Type::PARAMETER: {
        auto info = pass_get_node_info(pass, AST::downcast<AST::Parameter>(base), Info_Query::TRY_READ);
        return info == 0 ? 0 : info->symbol;
    }
    }
    panic("");
    return 0;
}

Analysis_Pass* code_query_get_analysis_pass(AST::Node* base)
{
    auto& mappings = compiler.semantic_analyser->ast_to_pass_mapping;
    while (base != 0)
    {
        auto passes = hashtable_find_element(&mappings, base);
        if (passes != 0) {
            assert(passes->passes.size != 0, "");
            return passes->passes[0];
        }
        base = base->parent;
    }
    return 0;
}

Symbol_Table* code_query_get_ast_node_symbol_table(AST::Node* base)
{
    /*
    The three Nodes that have Symbol tables are:
        - Module
        - Function (Parameter symbol are here
        - Code-Block
    */
    Symbol_Table* table = compiler.semantic_analyser->root_symbol_table;
    Analysis_Pass* pass = code_query_get_analysis_pass(base);
    if (pass == 0) return table;
    while (base != 0)
    {
        switch (base->type)
        {
        case AST::Node_Type::MODULE: {
            auto info = pass_get_node_info(pass, AST::downcast<AST::Module>(base), Info_Query::TRY_READ);
            if (info == 0) { break; }
            return info->symbol_table;
        }
        case AST::Node_Type::CODE_BLOCK: {
            auto block = pass_get_node_info(pass, AST::downcast<AST::Code_Block>(base), Info_Query::TRY_READ);
            if (block == 0) { break; }
            return block->symbol_table;
        }
        case AST::Node_Type::EXPRESSION: {
            auto expr = AST::downcast<AST::Expression>(base);
            if (expr->type == AST::Expression_Type::FUNCTION) {
                if (pass->origin_workload->type == Analysis_Workload_Type::FUNCTION_HEADER) {
                    return ((Workload_Function_Header*)(pass->origin_workload))->progress->function->options.normal.parameter_table;
                }
            }
            break;
        }
        }
        base = base->parent;
    }
    return table;
}



// Commands
void editor_enter_insert_mode()
{
    syntax_editor.mode = Editor_Mode::INSERT;
    history_start_complex_command(&syntax_editor.history);
}

void editor_leave_insert_mode()
{
    syntax_editor.mode = Editor_Mode::NORMAL;
    history_stop_complex_command(&syntax_editor.history);
    history_set_cursor_pos(&syntax_editor.history, syntax_editor.cursor);
    dynamic_array_reset(&syntax_editor.code_completion_suggestions);
}

void editor_split_line_at_cursor(int indentation_offset)
{
    auto& mode = syntax_editor.mode;
    auto& cursor = syntax_editor.cursor;
    Source_Line* line = source_code_get_line(syntax_editor.code, cursor.line);
    auto& text = line->text;
    int line_size = text.size; // Text may be invalid after applying history changes
    String cutout = string_create_substring_static(&text, cursor.character, text.size);

    history_start_complex_command(&syntax_editor.history);
    SCOPE_EXIT(history_stop_complex_command(&syntax_editor.history));

    int new_line_index = cursor.line + 1;
    history_insert_line(&syntax_editor.history, new_line_index, math_maximum(0, line->indentation + indentation_offset));

    if (cursor.character != line_size) {
        history_insert_text(&syntax_editor.history, text_index_make(new_line_index, 0), cutout);
        history_delete_text(&syntax_editor.history, cursor, line_size);
        syntax_editor_sanitize_line(cursor.line);
    }
    syntax_editor_sanitize_line(new_line_index);
    cursor = text_index_make(new_line_index, 0);
}

String code_completion_get_partially_typed_word()
{
    syntax_editor_synchronize_tokens();
    if (syntax_editor.space_after_cursor) {
        return string_create_static("");
    }
    auto token = get_cursor_token(false);
    String result;
    if (token.type == Token_Type::IDENTIFIER) {
        result = *token.options.identifier;
    }
    else {
        return string_create_static("");
    }

    auto pos = syntax_editor.cursor.character;
    if (pos < token.end_index) {
        result = string_create_substring_static(&result, 0, pos - token.start_index);
    }
    return result;
}

void code_completion_find_suggestions()
{
    auto& editor = syntax_editor;
    auto& suggestions = editor.code_completion_suggestions;
    dynamic_array_reset(&suggestions);
    if (editor.mode != Editor_Mode::INSERT || editor.cursor.character == 0) {
        return;
    }

    if (editor.input_replay.currently_recording || editor.input_replay.currently_replaying) {
        return;
    }

    // Exit early if we are not in a completion context (Comments etc...)
    {
        syntax_editor_synchronize_tokens();
        auto current_token = get_cursor_token(false);
        if (current_token.type == Token_Type::COMMENT) {
            return;
        }
    }
    auto partially_typed = code_completion_get_partially_typed_word();
    fuzzy_search_start_search(partially_typed);

    // Check if we are on special node
    syntax_editor_synchronize_with_compiler(false);
    Token_Index cursor_token_index = token_index_make(syntax_editor.cursor.line, get_cursor_token_index(false));
    auto node = Parser::find_smallest_enclosing_node(&compiler.main_source->parsed_code->root->base, cursor_token_index);

    Symbol_Table* specific_table = 0;

    // Check for specific contexts where we can fill the suggestions more smartly (e.g. when typing struct member, module-path, auto-enum, ...)
    if (node->type == AST::Node_Type::EXPRESSION)
    {
        auto expr = AST::downcast<AST::Expression>(node);
        auto pass = code_query_get_analysis_pass(node);
        Datatype* type = 0;
        if (expr->type == AST::Expression_Type::MEMBER_ACCESS && pass != 0) {
            auto info = pass_get_node_info(pass, expr->options.member_access.expr, Info_Query::TRY_READ);
            if (info != 0) {
                type = expression_info_get_type(info, false);
                if (info->result_type == Expression_Result_Type::TYPE) {
                    type = info->options.type;
                }
            }
        }
        else if (expr->type == AST::Expression_Type::AUTO_ENUM && pass != 0) {
            auto info = pass_get_node_info(pass, expr, Info_Query::TRY_READ);
            if (info != 0) {
                type = expression_info_get_type(info, false);
            }
        }

        if (type != 0)
        {
            auto original = type;
            type = type->base_type;
            switch (type->type)
            {
            case Datatype_Type::ARRAY:
            case Datatype_Type::SLICE: {
                fuzzy_search_add_item(string_create_static("data"));
                fuzzy_search_add_item(string_create_static("size"));
                break;
            }
            case Datatype_Type::STRUCT_INSTANCE_TEMPLATE:
            case Datatype_Type::STRUCT: 
            {
                Datatype_Struct* structure = 0;
                if (type->type == Datatype_Type::STRUCT) {
                    structure = downcast<Datatype_Struct>(type);
                }
                else if (type->type == Datatype_Type::STRUCT_INSTANCE_TEMPLATE) {
                    structure = downcast<Datatype_Struct_Instance_Template>(type)->struct_base->body_workload->struct_type;
                }

                Struct_Content* content = type_mods_get_subtype(structure, original->mods);
                auto& members = content->members;
                for (int i = 0; i < members.size; i++) {
                    auto& mem = members[i];
                    fuzzy_search_add_item(*mem.id);
                }
                if (content->subtypes.size > 0) {
                    fuzzy_search_add_item(*compiler.predefined_ids.tag);
                }
                for (int i = 0; i < content->subtypes.size; i++) {
                    auto sub = content->subtypes[i];
                    fuzzy_search_add_item(*sub->name);
                }
                // Add base name if available
                if (original->mods.subtype_index->indices.size > 0) {
                    Struct_Content* content = type_mods_get_subtype(structure, type->mods, type->mods.subtype_index->indices.size - 1);
                    fuzzy_search_add_item(*content->name);
                }
                break;
            }
            case Datatype_Type::ENUM:
                auto& members = downcast<Datatype_Enum>(type)->members;
                for (int i = 0; i < members.size; i++) {
                    auto& mem = members[i];
                    fuzzy_search_add_item(*mem.name);
                }
                break;
            }

            // Search for dot-calls
            Datatype* poly_base_type = compiler.type_system.predefined_types.unknown_type;
            if (type->type == Datatype_Type::STRUCT) {
                auto struct_type = downcast<Datatype_Struct>(type);
                if (struct_type->workload != 0 && struct_type->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
                    poly_base_type = upcast(struct_type->workload->polymorphic.instance.parent->body_workload->struct_type);
                }
            }

            auto context = code_query_get_ast_node_symbol_table(node)->operator_context;
            auto iter = hashtable_iterator_create(&context->custom_operators);
            while (hashtable_iterator_has_next(&iter))
            {
                SCOPE_EXIT(hashtable_iterator_next(&iter));

                Custom_Operator_Key key = *iter.key;
                Custom_Operator overload = *iter.value;

                if (key.type != Custom_Operator_Type::DOT_CALL) {
                    continue;
                }
                if (!(types_are_equal(type, key.options.dot_call.datatype) || types_are_equal(poly_base_type, key.options.dot_call.datatype))) {
                    continue;
                }
                fuzzy_search_add_item(*key.options.dot_call.id);
            }
        }
    }
    else if (node->type == AST::Node_Type::PATH_LOOKUP) {
        // This probably only happens if the last token was ~
        int cursor_token_index = get_cursor_token_index(false);
        auto prev_token = get_cursor_token(false);
        if (prev_token.type == Token_Type::OPERATOR && prev_token.options.op == Operator::TILDE && cursor_token_index > 0) {
            // Try to get token before ~, which should be an identifier...
            Token_Index prev = token_index_make(editor.cursor.line, cursor_token_index - 1);
            auto node = Parser::find_smallest_enclosing_node(&compiler.main_source->parsed_code->root->base, prev);
            auto pass = code_query_get_analysis_pass(node);
            if (node->type == AST::Node_Type::SYMBOL_LOOKUP && pass != 0) {
                auto info = pass_get_node_info(pass, AST::downcast<AST::Symbol_Lookup>(node), Info_Query::TRY_READ);
                if (info != 0) {
                    auto symbol = info->symbol;
                    if (symbol->type == Symbol_Type::MODULE) {
                        specific_table = symbol->options.module_progress->module_analysis->symbol_table;
                    }
                }
            }
        }
    }
    else if (node->type == AST::Node_Type::SYMBOL_LOOKUP)
    {
        auto lookup = AST::downcast<AST::Symbol_Lookup>(node);
        auto path = AST::downcast<AST::Path_Lookup>(node->parent);
        auto pass = code_query_get_analysis_pass(node);
        if (pass != 0)
        {
            // Find previous part of the current path
            int prev_index = -1;
            for (int i = 0; i < path->parts.size; i++) {
                if (path->parts[i] == lookup) {
                    prev_index = i - 1;
                    break;
                }
            }
            // Try to get symbol table from there
            if (prev_index != -1) {
                auto info = pass_get_node_info(pass, path->parts[prev_index], Info_Query::TRY_READ);
                if (info != 0) {
                    auto symbol = info->symbol;
                    if (symbol->type == Symbol_Type::MODULE) {
                        specific_table = symbol->options.module_progress->module_analysis->symbol_table;
                    }
                }
            }
        }
    }
    else {
        // Check if we should fill context options
        bool fill_context_options = false;
        auto& tokens = source_code_get_line(editor.code, editor.cursor.line)->tokens;
        if (tokens.size != 0 && tokens[0].type == Token_Type::KEYWORD && tokens[0].options.keyword == Keyword::CONTEXT)
        { 
            if (editor.space_before_cursor && cursor_token_index.token == 0) {
                fill_context_options = true;
            }
            else if (cursor_token_index.token == 1 && !editor.space_before_cursor) {
                fill_context_options = true;
            }
        }

        if (fill_context_options) {
            auto& ids = compiler.predefined_ids;
            fuzzy_search_add_item(*ids.set_cast_option);
            fuzzy_search_add_item(*ids.id_import);
            fuzzy_search_add_item(*ids.add_binop);
            fuzzy_search_add_item(*ids.add_unop);
            fuzzy_search_add_item(*ids.add_cast);
            fuzzy_search_add_item(*ids.add_dot_call);
            fuzzy_search_add_item(*ids.add_array_access);
            fuzzy_search_add_item(*ids.add_iterator);
        }
    }

    // If no text has been written yet and we aren't on some special context (e.g. no suggestions), return
    if (specific_table == nullptr && syntax_editor.code_completion_suggestions.size == 0 && partially_typed.size == 0) {
        return;
    }

    if (syntax_editor.code_completion_suggestions.size == 0)
    {
        bool search_includes = specific_table == 0;
        if (specific_table == 0) {
            specific_table = code_query_get_ast_node_symbol_table(node);
        }
        if (specific_table != 0) {
            auto results = dynamic_array_create<Symbol*>(1);
            SCOPE_EXIT(dynamic_array_destroy(&results));
            symbol_table_query_id(specific_table, 0, search_includes, Symbol_Access_Level::INTERNAL, &results);
            for (int i = 0; i < results.size; i++) {
                fuzzy_search_add_item(*results[i]->id);
            }
        }
    }

    auto results = fuzzy_search_rank_results(true, 3);
    for (int i = 0; i < results.size; i++) {
        dynamic_array_push_back(&syntax_editor.code_completion_suggestions, results[i].item_name);
    }
}

void code_completion_insert_suggestion()
{
    auto& editor = syntax_editor;
    auto& suggestions = editor.code_completion_suggestions;
    if (suggestions.size == 0) return;
    if (editor.cursor.character == 0) return;
    String replace_string = suggestions[0];
    auto line = source_code_get_line(editor.code, editor.cursor.line);

    // Remove current token
    int token_index = get_cursor_token_index(false);
    int start_pos = line->tokens[token_index].start_index;
    history_delete_text(&editor.history, text_index_make(editor.cursor.line, start_pos), editor.cursor.character);
    // Insert suggestion instead
    editor.cursor.character = start_pos;
    history_insert_text(&editor.history, editor.cursor, replace_string);
    editor.cursor.character += replace_string.size;
    syntax_editor_sanitize_line(editor.cursor.line);
}



void normal_command_execute(Normal_Command command)
{
    auto& editor = syntax_editor;
    auto& cursor = editor.cursor;
    auto line = source_code_get_line(editor.code, cursor.line);
    auto& tokens = line->tokens;

    SCOPE_EXIT(history_set_cursor_pos(&editor.history, editor.cursor));
    SCOPE_EXIT(syntax_editor_sanitize_cursor());

    editor.space_before_cursor = false;
    switch (command)
    {
    case Normal_Command::INSERT_AFTER: {
        syntax_editor_synchronize_tokens();
        editor_enter_insert_mode();
        cursor.character = get_cursor_token(true).end_index;
        break;
    }
    case Normal_Command::INSERT_BEFORE: {
        syntax_editor_synchronize_tokens();
        editor_enter_insert_mode();
        cursor.character = get_cursor_token(true).start_index;
        break;
    }
    case Normal_Command::MOVE_LEFT: {
        syntax_editor_synchronize_tokens();
        if (tokens.size == 0) {
            cursor.character = 0;
            break;
        }
        auto index = get_cursor_token_index(false);
        cursor.character = tokens[index].start_index;
        break;
    }
    case Normal_Command::MOVE_RIGHT: {
        syntax_editor_synchronize_tokens();
        auto index = get_cursor_token_index(true) + 1;
        if (index >= tokens.size) break;
        cursor.character = tokens[index].start_index;
        break;
    }
    case Normal_Command::INSERT_AT_LINE_END: {
        cursor.character = line->text.size;
        editor_enter_insert_mode();
        break;
    }
    case Normal_Command::INSERT_AT_LINE_START: {
        cursor.character = 0;
        editor_enter_insert_mode();
        break;
    }
    case Normal_Command::UNDO: {
        history_undo(&editor.history);
        auto cursor_history = history_get_cursor_pos(&editor.history);
        if (cursor_history.available) {
            cursor = cursor_history.value;
        }
        break;
    }
    case Normal_Command::REDO: {
        history_redo(&editor.history);
        auto cursor_history = history_get_cursor_pos(&editor.history);
        if (cursor_history.available) {
            cursor = cursor_history.value;
        }
        break;
    }
    case Normal_Command::CHANGE_CHARACTER:
    case Normal_Command::DELETE_CHARACTER: {
        if (line->text.size == 0) break;
        history_delete_char(&editor.history, editor.cursor);
        bool changed = syntax_editor_sanitize_line(cursor.line);
        if (command == Normal_Command::CHANGE_CHARACTER) {
            editor_enter_insert_mode();
        }
        break;
    }
    case Normal_Command::MOVE_LINE_START: {
        cursor.character = 0;
        break;
    }
    case Normal_Command::MOVE_LINE_END: {
        cursor.character = line->text.size;
        break;
    }
    case Normal_Command::ADD_LINE_ABOVE:
    case Normal_Command::ADD_LINE_BELOW:
    {
        history_start_complex_command(&editor.history);
        SCOPE_EXIT(history_stop_complex_command(&editor.history));

        bool below = command == Normal_Command::ADD_LINE_BELOW;
        int new_line_index = cursor.line + (below ? 1 : 0);
        history_insert_line(&editor.history, new_line_index, line->indentation);
        cursor = text_index_make(new_line_index, 0);
        editor_enter_insert_mode();
        break;
    }
    case Normal_Command::MOVE_UP: {
        // FUTURE: Use token render positions to move up/down
        cursor.line -= 1;
        break;
    }
    case Normal_Command::MOVE_DOWN: {
        cursor.line += 1;
        break;
    }
    default: panic("");
    }
}

void insert_command_execute(Insert_Command input)
{
    auto& editor = syntax_editor;
    auto& mode = editor.mode;
    auto& cursor = editor.cursor;
    auto line = source_code_get_line(editor.code, cursor.line);
    auto& text = line->text;
    auto& pos = cursor.character;

    history_start_complex_command(&editor.history);
    SCOPE_EXIT(history_stop_complex_command(&editor.history));

    assert(mode == Editor_Mode::INSERT, "");
    syntax_editor_sanitize_cursor();
    SCOPE_EXIT(syntax_editor_sanitize_cursor());

    // Reset Space before/after cursor
    if (input.type != Insert_Command_Type::IDENTIFIER_LETTER && input.type != Insert_Command_Type::BACKSPACE && input.type != Insert_Command_Type::NUMBER_LETTER) {
        syntax_editor.space_before_cursor = false;
        if (input.type != Insert_Command_Type::DELIMITER_LETTER) {
            syntax_editor.space_after_cursor = false;
        }
    }
    SCOPE_EXIT(code_completion_find_suggestions());
    // Experimental: Automatically complete identifier if we are about to enter some seperation afterwards
    if (false)
    {
        bool complete_current = false;
        switch (input.type)
        {
        case Insert_Command_Type::ADD_INDENTATION:
        case Insert_Command_Type::ENTER:
        case Insert_Command_Type::ENTER_REMOVE_ONE_INDENT:
        case Insert_Command_Type::SPACE:
        {
            syntax_editor_synchronize_tokens();
            auto after_token = get_cursor_token(true);
            complete_current = line->tokens.size != 1;
            if (!complete_current) {
                complete_current =
                    !(after_token.type == Token_Type::OPERATOR &&
                        (after_token.options.op == Operator::DEFINE_COMPTIME ||
                            after_token.options.op == Operator::DEFINE_INFER ||
                            after_token.options.op == Operator::COLON));
            }
            break;
        }
        case Insert_Command_Type::DELIMITER_LETTER:
            complete_current = input.letter != ':';
            break;
        }

        if (complete_current) {
            code_completion_insert_suggestion();
        }
    }

    // Handle Universal Inputs
    switch (input.type)
    {
    case Insert_Command_Type::INSERT_CODE_COMPLETION: {
        code_completion_insert_suggestion();
        break;
    }
    case Insert_Command_Type::EXIT_INSERT_MODE: {
        //code_completion_insert_suggestion();
        editor_leave_insert_mode();
        break;
    }
    case Insert_Command_Type::ENTER: {
        editor_split_line_at_cursor(0);
        break;
    }
    case Insert_Command_Type::ENTER_REMOVE_ONE_INDENT: {
        editor_split_line_at_cursor(-1);
        break;
    }
    case Insert_Command_Type::ADD_INDENTATION: {
        if (pos == 0) {
            history_change_indent(&syntax_editor.history, cursor.line, line->indentation + 1);
            break;
        }
        editor_split_line_at_cursor(1);
        break;
    }
    case Insert_Command_Type::REMOVE_INDENTATION: {
        if (line->indentation > 0) {
            history_change_indent(&syntax_editor.history, cursor.line, line->indentation - 1);
        }
        break;
    }
    case Insert_Command_Type::MOVE_LEFT: {
        pos = math_maximum(0, pos - 1);
        break;
    }
    case Insert_Command_Type::MOVE_RIGHT: {
        pos = math_minimum(line->text.size, pos + 1);
        break;
    }
    case Insert_Command_Type::DELIMITER_LETTER:
    {
        bool insert_double_after = false;
        bool skip_auto_input = false;
        char double_char = ' ';
        if (char_is_parenthesis(input.letter))
        {
            Parenthesis p = char_to_parenthesis(input.letter);
            // Check if line_index is properly parenthesised with regards to the one token I currently am on
            // This is easy to check, but design wise I feel like I only want to check the token I am on
            if (p.is_open)
            {
                int open_count = 0;
                int closed_count = 0;
                for (int i = 0; i < text.size; i++) {
                    char c = text[i];
                    if (!char_is_parenthesis(c)) continue;
                    Parenthesis found = char_to_parenthesis(c);
                    if (found.type == p.type) {
                        if (found.is_open) open_count += 1;
                        else closed_count += 1;
                    }
                }
                insert_double_after = open_count == closed_count;
                if (insert_double_after) {
                    p.is_open = false;
                    double_char = parenthesis_to_char(p);
                }
            }
            else {
                skip_auto_input = pos < text.size&& text[pos] == input.letter;
            }
        }
        if (input.letter == '"')
        {
            if (pos < text.size && text[pos] == '"') {
                skip_auto_input = true;
            }
            else {
                int count = 0;
                for (int i = 0; i < text.size; i++) {
                    if (text[i] == '"') count += 1;
                }
                if (count % 2 == 0) {
                    insert_double_after = true;
                    double_char = '"';
                }
            }
        }

        if (skip_auto_input) {
            pos += 1;
            break;
        }
        if (insert_double_after) {
            history_insert_char(&syntax_editor.history, cursor, double_char);
        }
        history_insert_char(&syntax_editor.history, cursor, input.letter);
        pos += 1;
        // Inserting delimiters between space critical tokens can lead to spaces beeing removed
        syntax_editor_sanitize_line(cursor.line);
        break;
    }
    case Insert_Command_Type::SPACE:
    {
        if (line->is_comment) {
            history_insert_char(&syntax_editor.history, cursor, ' ');
            pos += 1;
            break;
        }

        if (pos == 0) break;
        syntax_editor_synchronize_tokens();
        auto token = get_cursor_token(false);
        if (token.type == Token_Type::COMMENT) {
            if (pos > token.start_index + 1) {
                history_insert_char(&syntax_editor.history, cursor, ' ');
                pos += 1;
            }
            break;
        }
        if (token.type == Token_Type::LITERAL && token.options.literal_value.type == Literal_Type::STRING) {
            if (pos > token.start_index && pos < token.end_index) {
                history_insert_char(&syntax_editor.history, cursor, ' ');
                pos += 1;
            }
            break;
        }

        char c = text[pos - 1];
        if (pos < text.size) {
            if (char_is_space_critical(c) && char_is_space_critical(text[pos])) {
                history_insert_char(&syntax_editor.history, cursor, ' ');
                pos += 1;
                break;
            }
        }

        if (char_is_space_critical(c) && !syntax_editor.space_before_cursor) {
            syntax_editor.space_before_cursor = true;
        }
        break;
    }
    case Insert_Command_Type::BACKSPACE:
    {
        if (syntax_editor.space_before_cursor) {
            syntax_editor.space_before_cursor = false;
            break;
        }
        if (string_test_char(text, pos - 1, ' ') && syntax_editor.space_after_cursor) {
            syntax_editor.space_after_cursor = false;
            pos -= 1;
            break;
        }

        if (pos == 0)
        {
            if (cursor.line == 0) {
                // We are at the first line_index in the code
                break;
            }
            auto prev_line = source_code_get_line(editor.code, cursor.line - 1);
            // Merge this line_index with previous one
            Text_Index insert_index = text_index_make(cursor.line - 1, prev_line->text.size);
            history_insert_text(&syntax_editor.history, insert_index, text);
            history_remove_line(&syntax_editor.history, cursor.line);
            cursor = insert_index;
            syntax_editor_sanitize_line(cursor.line);
            break;
        }

        syntax_editor.space_after_cursor = string_test_char(text, pos, ' ') || syntax_editor.space_after_cursor;
        history_delete_char(&syntax_editor.history, text_index_make(cursor.line, pos - 1));
        pos -= 1;
        syntax_editor.space_before_cursor = string_test_char(text, pos - 1, ' ');
        syntax_editor_sanitize_line(cursor.line);
        break;
    }
    case Insert_Command_Type::NUMBER_LETTER:
    case Insert_Command_Type::IDENTIFIER_LETTER:
    {
        int insert_pos = pos;
        if (syntax_editor.space_before_cursor) {
            syntax_editor.space_before_cursor = false;
            history_insert_char(&syntax_editor.history, cursor, ' ');
            pos += 1;
        }
        history_insert_char(&syntax_editor.history, cursor, input.letter);
        pos += 1;
        if (syntax_editor.space_after_cursor) {
            syntax_editor.space_after_cursor = false;
            history_insert_char(&syntax_editor.history, cursor, ' ');
        }
        break;
    }
    default: break;
    }
}



// Syntax Editor
void syntax_editor_process_key_message(Key_Message& msg)
{
    auto& mode = syntax_editor.mode;
    if (mode == Editor_Mode::INSERT)
    {
        Insert_Command input;
        if ((msg.key_code == Key_Code::P || msg.key_code == Key_Code::N) && msg.ctrl_down && msg.key_down) {
            input.type = Insert_Command_Type::INSERT_CODE_COMPLETION;
        }
        else if (msg.key_code == Key_Code::SPACE && msg.key_down) {
            input.type = msg.shift_down ? Insert_Command_Type::INSERT_CODE_COMPLETION : Insert_Command_Type::SPACE;
        }
        else if (msg.key_code == Key_Code::L && msg.key_down && msg.ctrl_down) {
            input.type = Insert_Command_Type::EXIT_INSERT_MODE;
        }
        else if (msg.key_code == Key_Code::ARROW_LEFT && msg.key_down) {
            input.type = Insert_Command_Type::MOVE_LEFT;
        }
        else if (msg.key_code == Key_Code::ARROW_RIGHT && msg.key_down) {
            input.type = Insert_Command_Type::MOVE_RIGHT;
        }
        else if (msg.key_code == Key_Code::BACKSPACE && msg.key_down) {
            input.type = Insert_Command_Type::BACKSPACE;
        }
        else if (msg.key_code == Key_Code::RETURN && msg.key_down) {
            if (msg.shift_down) {
                input.type = Insert_Command_Type::ENTER_REMOVE_ONE_INDENT;
            }
            else {
                input.type = Insert_Command_Type::ENTER;
            }
        }
        else if (char_is_letter(msg.character) || msg.character == '_') {
            input.type = Insert_Command_Type::IDENTIFIER_LETTER;
            input.letter = msg.character;
        }
        else if (char_is_digit(msg.character)) {
            input.type = Insert_Command_Type::NUMBER_LETTER;
            input.letter = msg.character;
        }
        else if (msg.key_code == Key_Code::TAB && msg.key_down) {
            if (msg.shift_down) {
                input.type = Insert_Command_Type::REMOVE_INDENTATION;
            }
            else {
                input.type = Insert_Command_Type::ADD_INDENTATION;
            }
        }
        else if (msg.key_down && msg.character != -1) {
            if (string_contains_character(characters_get_non_identifier_non_whitespace(), msg.character)) {
                input.type = Insert_Command_Type::DELIMITER_LETTER;
                input.letter = msg.character;
            }
            else {
                return;
            }
        }
        else {
            return;
        }
        insert_command_execute(input);
    }
    else
    {
        syntax_editor.space_before_cursor = false;
        syntax_editor.space_after_cursor = false;
        Normal_Command command;
        if (msg.key_code == Key_Code::L && msg.key_down) {
            command = Normal_Command::MOVE_RIGHT;
        }
        else if (msg.key_code == Key_Code::H && msg.key_down) {
            command = Normal_Command::MOVE_LEFT;
        }
        else if (msg.key_code == Key_Code::J && msg.key_down) {
            command = Normal_Command::MOVE_DOWN;
        }
        else if (msg.key_code == Key_Code::K && msg.key_down) {
            command = Normal_Command::MOVE_UP;
        }
        else if (msg.key_code == Key_Code::O && msg.key_down) {
            if (msg.shift_down) {
                command = Normal_Command::ADD_LINE_ABOVE;
            }
            else {
                command = Normal_Command::ADD_LINE_BELOW;
            }
        }
        else if (msg.key_code == Key_Code::NUM_0 && msg.key_down) {
            command = Normal_Command::MOVE_LINE_START;
        }
        else if (msg.key_code == Key_Code::NUM_4 && msg.key_down && msg.shift_down) {
            command = Normal_Command::MOVE_LINE_END;
        }
        else if (msg.key_code == Key_Code::A && msg.key_down) {
            if (msg.shift_down) {
                command = Normal_Command::INSERT_AT_LINE_END;
            }
            else {
                command = Normal_Command::INSERT_AFTER;
            }
        }
        else if (msg.key_code == Key_Code::I && msg.key_down) {
            if (msg.shift_down) {
                command = Normal_Command::INSERT_AT_LINE_START;
            }
            else {
                command = Normal_Command::INSERT_BEFORE;
            }
        }
        else if (msg.key_code == Key_Code::R && msg.key_down) {
            if (msg.ctrl_down) {
                command = Normal_Command::REDO;
            }
            else {
                command = Normal_Command::CHANGE_CHARACTER;
            }
        }
        else if (msg.key_code == Key_Code::X && msg.key_down) {
            command = Normal_Command::DELETE_CHARACTER;
        }
        else if (msg.key_code == Key_Code::U && msg.key_down) {
            command = Normal_Command::UNDO;
        }
        else {
            return;
        }
        normal_command_execute(command);
    }
}

void syntax_editor_update()
{
    auto& editor = syntax_editor;
    auto& input = syntax_editor.input;
    auto& mode = syntax_editor.mode;
    
    SCOPE_EXIT(source_code_sanity_check(editor.code));

    // Handle input replay
    {
        auto& replay = editor.input_replay;
        if (editor.input->key_pressed[(int)Key_Code::F9]) {
            if (replay.currently_recording) {
                logg("Ending recording of inputs, captures: %d messages!\n", replay.recorded_inputs.size);
                replay.currently_recording = false;
                string_reset(&replay.code_state_afterwards);
                source_code_append_to_string(editor.code, &replay.code_state_afterwards);
            }
            else {
                logg("Started recording keyboard inputs!", replay.recorded_inputs.size);
                replay.currently_recording = true;
                replay.cursor_start = editor.cursor;
                replay.start_mode = editor.mode;
                string_reset(&replay.code_state_initial);
                source_code_append_to_string(editor.code, &replay.code_state_initial);
                dynamic_array_reset(&replay.recorded_inputs);
            }
        }
        if (editor.input->key_pressed[(int)Key_Code::F10]) {
            if (replay.currently_recording) {
                logg("Cannot replay recorded inputs, since we are CURRENTLY RECORDING!\n");
            }
            else {
                replay.currently_replaying = true;
                SCOPE_EXIT(replay.currently_replaying = false);
                // Set new state
                syntax_editor_set_text(replay.code_state_initial);
                editor.cursor = replay.cursor_start;
                editor.mode = replay.start_mode;
                for (int i = 0; i < replay.recorded_inputs.size; i++) {
                    syntax_editor_process_key_message(replay.recorded_inputs[i]);
                }
                auto text_afterwards = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&text_afterwards));
                source_code_append_to_string(editor.code, &text_afterwards);
                if (!string_equals(&text_afterwards, &replay.code_state_afterwards)) {
                    logg("Replaying the events did not end in the same output!\n");
                }
                else {
                    logg("Replay successfull, everything happened as expected");
                }
            }
        }
        if (replay.currently_recording) {
            dynamic_array_append_other(&replay.recorded_inputs, &input->key_messages);
        }
    }

    // Check shortcuts pressed
    if (syntax_editor.input->key_pressed[(int)Key_Code::O] && syntax_editor.input->key_down[(int)Key_Code::CTRL]) {
        auto open_file = file_io_open_file_selection_dialog();
        if (open_file.available) {
            syntax_editor_load_text_file(open_file.value.characters);
        }
    }
    else if (syntax_editor.input->key_pressed[(int)Key_Code::S] && syntax_editor.input->key_down[(int)Key_Code::CTRL]) {
        syntax_editor_save_text_file();
    }
    else if (syntax_editor.input->key_pressed[(int)Key_Code::F8]) {
        compiler_run_testcases(compiler.timer, true);
        syntax_editor_load_text_file(editor.file_path);
    }

    // Handle Editor inputs
    for (int i = 0; i < input->key_messages.size; i++) {
        syntax_editor_process_key_message(input->key_messages[i]);
    }

    bool build_and_run = syntax_editor.input->key_pressed[(int)Key_Code::F5];
    syntax_editor_synchronize_with_compiler(build_and_run);
    if (build_and_run)
    {
        // Display error messages or run the program
        if (!compiler_errors_occured()) {
            auto exit_code = compiler_execute();
            String output = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&output));
            exit_code_append_to_string(&output, exit_code);
            logg("\nProgram Exit with Code: %s\n", output.characters);
        }
        else {
            // Print errors
            logg("Could not run program, there were errors:\n");
            for (int i = 0; i < editor.errors.size; i++) {
                auto error = editor.errors[i];
                logg("\t%s\n", error.message.characters);
            }
        }
    }
}

void syntax_editor_initialize(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Input* input, Timer* timer)
{
    memory_zero(&syntax_editor);
    syntax_editor.context_text = string_create_empty(256);
    syntax_editor.errors = dynamic_array_create<Error_Display>(1);
    syntax_editor.token_range_buffer = dynamic_array_create<Token_Range>(1);
    syntax_editor.code_completion_suggestions = dynamic_array_create<String>();

    syntax_editor.code = source_code_create();
    syntax_editor.history = code_history_create(syntax_editor.code);
    syntax_editor.last_token_synchronized = history_get_timestamp(&syntax_editor.history);
    syntax_editor.code_changed_since_last_compile = true;

    syntax_editor.text_renderer = text_renderer;
    syntax_editor.rendering_core = rendering_core;
    syntax_editor.renderer_2D = renderer_2D;
    syntax_editor.input = input;
    syntax_editor.mode = Editor_Mode::NORMAL;
    syntax_editor.cursor = text_index_make(0, 0);
    syntax_editor.camera_start_line = 0;

    syntax_editor.input_replay = input_replay_create();

    compiler_initialize(timer);
    compiler_run_testcases(timer, false);
    syntax_editor_load_text_file("upp_code/editor_text.upp");
}

void syntax_editor_destroy()
{
    auto& editor = syntax_editor;
    source_code_destroy(editor.code);
    code_history_destroy(&editor.history);
    dynamic_array_destroy(&editor.code_completion_suggestions);
    string_destroy(&syntax_editor.context_text);
    compiler_destroy();
    for (int i = 0; i < editor.errors.size; i++) {
        string_destroy(&editor.errors[i].message);
    }
    dynamic_array_destroy(&editor.errors);
    dynamic_array_destroy(&editor.token_range_buffer);
}



// RENDERING
// Draw Commands

// Returns bottom left vector
vec2 syntax_editor_position_to_pixel(int line_index, int character)
{
    float height = rendering_core.render_information.backbuffer_height;
    return vec2(0.0f, height) + syntax_editor.character_size * vec2(character, -line_index - 1);
}

void syntax_editor_draw_underline(int line_index, int character, int length, vec3 color)
{
    vec2 pos = syntax_editor_position_to_pixel(line_index, character);
    vec2 size = vec2((float)length, 1.0f / 8.f) * syntax_editor.character_size;
    renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_anchor(pos, size, Anchor::BOTTOM_LEFT), color);
}

void syntax_editor_draw_cursor_line(int line_index, int character, vec3 color)
{
    vec2 pos = syntax_editor_position_to_pixel(line_index, character);
    vec2 size = vec2(0.1f, 1.0) * syntax_editor.character_size;
    renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_anchor(pos, size, Anchor::BOTTOM_LEFT), color);
}

void syntax_editor_draw_text_background(int line_index, int character, int length, vec3 color)
{
    vec2 pos = syntax_editor_position_to_pixel(line_index, character);
    vec2 size = vec2(length, 1) * syntax_editor.character_size;
    renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_anchor(pos, size, Anchor::BOTTOM_LEFT), color);
}

void syntax_editor_draw_string(String string, vec3 color, int line_index, int character)
{
    vec2 pos = syntax_editor_position_to_pixel(line_index, character);
    text_renderer_add_text(syntax_editor.text_renderer, string, pos, Anchor::BOTTOM_LEFT, syntax_editor.character_size.y, color);
}

// Note: Always takes Anchor::TOP_LEFT, because string may grow downwards...
void syntax_editor_draw_string_in_box(String string, vec3 text_color, vec3 box_color, vec2 position, float text_height)
{
    vec2 char_size = vec2(text_renderer_character_width(syntax_editor.text_renderer, text_height), text_height);

    int line_pos = 0;
    int max_line_pos = 0;
    int current_char_pos = 0;
    int last_draw_character_index = 0;
    int next_draw_char_pos = 0;
    for (int i = 0; i <= string.size; i++) {
        char c = i == string.size ? 0 : string[i];
        bool draw_until_this_character = i == string.size;
        const int draw_line_index = line_pos;

        if (c == '\n') {
            draw_until_this_character = true;
            line_pos += 1;
            current_char_pos = 0;
        }
        else if (c == '\t') {
            draw_until_this_character = true;
            current_char_pos += 4;
        }
        else if (c < 32) {
            // Ignore other ascii control sequences
        }
        else {
            // Normal character
            current_char_pos += 1;
        }
        max_line_pos = math_maximum(max_line_pos, current_char_pos);

        if (draw_until_this_character) {
            String line = string_create_substring_static(&string, last_draw_character_index, i);
            last_draw_character_index = i + 1;
            vec2 text_line_pos = position + char_size * vec2(next_draw_char_pos, -draw_line_index);
            text_renderer_add_text(syntax_editor.text_renderer, line, text_line_pos, Anchor::TOP_LEFT, text_height, text_color);
            next_draw_char_pos = current_char_pos;
        }
    }

    // Draw surrounding retangle
    vec2 text_size = char_size * vec2(max_line_pos, line_pos + 1);
    renderer_2D_add_rectangle(syntax_editor.renderer_2D, bounding_box_2_make_anchor(position, text_size, Anchor::TOP_LEFT), box_color);
}

void syntax_editor_draw_block_outline(int line_start, int line_end, int indentation)
{
    if (indentation == 0) return;
    auto offset = syntax_editor.character_size * vec2(0.5f, 1.0f);
    vec2 start = syntax_editor_position_to_pixel(line_start, (indentation - 1) * 4) + offset;
    vec2 end = syntax_editor_position_to_pixel(line_end, (indentation - 1) * 4) + offset;
    start.y -= syntax_editor.character_size.y * 0.1;
    end.y += syntax_editor.character_size.y * 0.1;
    renderer_2D_add_line(syntax_editor.renderer_2D, start, end, vec3(0.4f), 3);
    vec2 l_end = end + vec2(syntax_editor.character_size.x * 0.5f, 0.0f);
    renderer_2D_add_line(syntax_editor.renderer_2D, end, l_end, vec3(0.4f), 3);
}



// Syntax Highlighting
void syntax_highlighting_mark_range(Token_Range range, vec3 normal_color, vec3 empty_range_color, bool underline)
{
    auto index = range.start;
    auto end = range.end;

    typedef void (*draw_fn)(int line_index, int character, int length, vec3 color);
    draw_fn draw_mark = underline ? syntax_editor_draw_underline : syntax_editor_draw_text_background;

    // Handle empty ranges
    assert(token_index_compare(index, end) >= 0, "hey");
    if (token_index_equal(index, end))
    {
        auto line = source_code_get_line(syntax_editor.code, index.line);
        int render_end_pos = line->indentation * 4;
        if (line->tokens.size > 0) {
            auto& last = line->infos[line->tokens.size - 1];
            render_end_pos = last.pos + last.size;
        }
        if (index.token >= line->tokens.size) {
            draw_mark(line->screen_index, render_end_pos, 1, empty_range_color);
        }
        else {
            auto& info = line->infos[index.token];
            draw_mark(line->screen_index, info.pos, info.size, normal_color);
        }
        return;
    }

    // Otherwise draw mark on all affected lines
    bool quit_loop = false;
    while (true)
    {
        auto line = source_code_get_line(syntax_editor.code, index.line);

        // Figure out start and end of highlight in line
        int highlight_start = line->indentation * 4;
        int highlight_end = line->indentation * 4;
        if (line->tokens.size > 0) 
        {
            const auto& last = line->infos[line->tokens.size - 1];
            highlight_end = last.pos + last.size;
            if (index.token < line->tokens.size) {
                highlight_start = line->infos[index.token].pos;
            }
        }

        if (index.line == end.line) {
            if (end.token > 0 && end.token - 1 < line->infos.size) {
                const auto& info = line->infos[end.token - 1];
                highlight_end = info.pos + info.size;
            }
        }

        // Draw
        if (highlight_start != highlight_end) {
            draw_mark(line->screen_index, highlight_start, highlight_end - highlight_start, normal_color);
        }

        // Break loop or go to next line
        if (index.line == end.line) {
            break;
        }
        index = token_index_make(index.line + 1, 0);
    }
}

void syntax_highlighting_mark_section(AST::Node* base, Parser::Section section, vec3 normal_color, vec3 empty_range_color, bool underline)
{
    assert(base != 0, "");
    auto ranges = syntax_editor.token_range_buffer;
    dynamic_array_reset(&ranges);

    auto& node = base;
    Parser::ast_base_get_section_token_range(syntax_editor.code, node, section, &ranges);
    for (int i = 0; i < ranges.size; i++) {
        syntax_highlighting_mark_range(ranges[i], normal_color, empty_range_color, underline);
    }
}

void syntax_highlighting_set_section_text_color(AST::Node* base, Parser::Section section, vec3 color)
{
    assert(base != 0, "");
    auto ranges = syntax_editor.token_range_buffer;
    dynamic_array_reset(&ranges);

    auto& node = base;
    Parser::ast_base_get_section_token_range(syntax_editor.code, node, section, &ranges);
    for (int r = 0; r < ranges.size; r++)
    {
        Token_Range range = ranges[r];
        for (int line_index = range.start.line; line_index <= range.end.line; line_index += 1)
        {
            Source_Line* line = source_code_get_line(syntax_editor.code, line_index);

            int start_index = line_index == range.start.line ? range.start.token : 0;
            int end_index = line_index == range.end.line ? range.end.token : line->tokens.size;
            for (int token = start_index; token < end_index && token < line->tokens.size; token += 1) {
                line->infos[token].color = color;
            }
        }
    }
}

void syntax_highlighting_highlight_identifiers_recursive(AST::Node* base)
{
    Symbol* symbol = code_query_get_ast_node_symbol(base);
    if (symbol != 0) {
        syntax_highlighting_set_section_text_color(base, Parser::Section::IDENTIFIER, symbol_type_to_color(symbol->type));
    }

    // Highlight dot-calls
    if (base->type == AST::Node_Type::EXPRESSION) 
    {
        auto expr = downcast<AST::Expression>(base);
        if (expr->type == AST::Expression_Type::MEMBER_ACCESS) 
        {
            auto pass = code_query_get_analysis_pass(base);
            if (pass != 0)
            {
                auto info = pass_get_node_info(pass, expr, Info_Query::TRY_READ);
                if (info != nullptr) {
                    if (info->result_type == Expression_Result_Type::DOT_CALL || info->specifics.member_access.options.dot_call_function != nullptr) {
                        syntax_highlighting_set_section_text_color(base, Parser::Section::END_TOKEN, Syntax_Color::FUNCTION);
                    }
                }
            }
        }
    }

    // Do syntax highlighting for children
    int index = 0;
    auto child = AST::base_get_child(base, index);
    while (child != 0)
    {
        syntax_highlighting_highlight_identifiers_recursive(child);
        index += 1;
        child = AST::base_get_child(base, index);
    }
}




// Code Layout 
void operator_space_before_after(Token_Index index, bool& space_before, bool& space_after)
{
    auto& tokens = source_code_get_line(syntax_editor.code, index.line)->tokens;
    assert(tokens[index.token].type == Token_Type::OPERATOR, "");
    auto op_info = syntax_operator_info(tokens[index.token].options.op);
    space_before = false;
    space_after = false;

    bool use_info = true;
    // Approximate if Operator is overload or not (Sometimes this cannot be detected with parsing alone)
    if (op_info.type == Operator_Type::BOTH)
    {
        // Current approximation for is_binop: The current and previous type has to be a value
        use_info = false;
        if (index.token > 0 && index.token + 1 < tokens.size)
        {
            bool prev_is_value = false;
            {
                const auto& t = tokens[index.token - 1];
                if (t.type == Token_Type::IDENTIFIER || t.type == Token_Type::LITERAL) {
                    prev_is_value = true;
                }
                if (t.type == Token_Type::PARENTHESIS && !t.options.parenthesis.is_open && t.options.parenthesis.type != Parenthesis_Type::BRACKETS) {
                    prev_is_value = true;
                }
            }
            bool next_is_value = false;
            {
                const auto& t = tokens[index.token + 1];
                if (t.type == Token_Type::IDENTIFIER || t.type == Token_Type::LITERAL) {
                    next_is_value = true;
                }
                if (t.type == Token_Type::PARENTHESIS && t.options.parenthesis.is_open) {
                    next_is_value = true;
                }
                if (t.type == Token_Type::OPERATOR) { // Operators usually indicate values, but this is just an approximation
                    next_is_value = true;
                }
            }
            use_info = prev_is_value && next_is_value;
        }
        /*
        // OLD IS_BINOP APPROXIMATION
        use_info = !(prev_type == Token_Type::OPERATOR ||
            (prev_type == Token_Type::PARENTHESIS && tokens[token_index - 1].options.parenthesis.is_open) ||
            (prev_type == Token_Type::PARENTHESIS && tokens[token_index - 1].options.parenthesis.type == Parenthesis_Type::BRACKETS) ||
            (prev_type == Token_Type::KEYWORD));
        */
    }

    if (use_info) {
        space_before = op_info.space_before;
        space_after = op_info.space_after;
    }
}

bool display_space_after_token(Token_Index index)
{
    auto line = source_code_get_line(syntax_editor.code, index.line);
    auto& text = line->text;
    auto& tokens = line->tokens;

    // No space after final line_index token
    if (index.token + 1 >= tokens.size) return false;

    auto& a = tokens[index.token];
    auto& b = tokens[index.token + 1];

    // Space critical tokens
    if (string_test_char(text, a.end_index, ' ')) {
        return true;
    }

    // End of line_index comments
    if (b.type == Token_Type::COMMENT) {
        return true;
    }

    // Invalids should always have space before and after
    if (a.type == Token_Type::INVALID || b.type == Token_Type::INVALID) {
        return true;
    }

    // Special keyword handling
    if (a.type == Token_Type::KEYWORD || b.type == Token_Type::KEYWORD) {
        return true;
    }

    // Ops that have spaces after
    if (a.type == Token_Type::OPERATOR)
    {
        bool unused, space_after;
        operator_space_before_after(index, unused, space_after);
        if (space_after) {
            return true;
        }
    }

    // Ops that have spaces before
    if (b.type == Token_Type::OPERATOR)
    {
        bool unused, space_before;
        operator_space_before_after(token_index_make(index.line, index.token + 1), space_before, unused);
        if (space_before) {
            return true;
        }
    }

    if (a.type == Token_Type::PARENTHESIS) {
        // Closed paranethesis should have a space after if it isn't used as an array type
        if (!a.options.parenthesis.is_open && char_is_space_critical(text[b.start_index]) && a.options.parenthesis.type != Parenthesis_Type::BRACKETS) {
            return true;
        }
    }

    return false;
}



// Rendering
int syntax_editor_draw_block_outlines_recursive(int line_index, int indentation)
{
    auto code = syntax_editor.code;
    int block_start = line_index;

    // Find end of block
    int block_end = code->line_count - 1;
    while (line_index < code->line_count)
    {
        if (line_index >= code->line_count) {
            block_end = code->line_count - 1;
            break;
        }

        auto line = source_code_get_line(code, line_index);
        if (line->indentation > indentation) {
            line_index = syntax_editor_draw_block_outlines_recursive(line_index, indentation + 1) + 1;
        }
        else if (line->indentation == indentation) {
            line_index += 1;
        }
        else { // line->indentation < indentation
            block_end = line_index - 1;
            break;
        }
    }

    syntax_editor_draw_block_outline(block_start - syntax_editor.camera_start_line, block_end - syntax_editor.camera_start_line + 1, indentation);
    return block_end;
}

void syntax_editor_render()
{
    auto& editor = syntax_editor;
    auto& cursor = syntax_editor.cursor;
    auto& code = syntax_editor.code;

    // Prepare Render
    editor.character_size.y = math_floor(convertHeight(0.55f, Unit::CENTIMETER));
    editor.character_size.x = text_renderer_character_width(editor.text_renderer, editor.character_size.y);
    syntax_editor_sanitize_cursor();

    auto state_2D = pipeline_state_make_default();
    state_2D.blending_state.blending_enabled = true;
    state_2D.blending_state.source = Blend_Operand::SOURCE_ALPHA;
    state_2D.blending_state.destination = Blend_Operand::ONE_MINUS_SOURCE_ALPHA;
    state_2D.blending_state.equation = Blend_Equation::ADDITION;
    state_2D.depth_state.test_type = Depth_Test_Type::IGNORE_DEPTH;
    auto pass_context = rendering_core_query_renderpass("Context pass", state_2D, 0);
    auto pass_2D = rendering_core_query_renderpass("2D state", state_2D, 0);
    render_pass_add_dependency(pass_2D, rendering_core.predefined.main_pass);
    render_pass_add_dependency(pass_context, pass_2D);

    // Calculate camera start
    {
        int line_count = rendering_core.render_information.backbuffer_height / editor.character_size.y;
        int visible_start = editor.camera_start_line;
        int visible_end = visible_start + line_count;

        auto cursor_line = cursor.line;
        if (cursor_line < visible_start) {
            editor.camera_start_line = cursor_line;
        }
        else if (cursor_line > visible_end) {
            editor.camera_start_line = cursor_line - line_count;
        }
    }

    // Layout Source Code (e.g. set token render infos for later markup)
    for (int i = 0; i < code->line_count; i++)
    {
        Source_Line* line = source_code_get_line(code, i);
        line->screen_index = i - editor.camera_start_line;

        dynamic_array_reset(&line->infos);
        int pos = line->indentation * 4;
        for (int j = 0; j < line->tokens.size; j++)
        {
            auto& token = line->tokens[j];
            bool on_token = cursor.line == i && get_cursor_token_index(true) == j;
            if (on_token && token.start_index == cursor.character) {
                pos += editor.space_after_cursor ? 1 : 0;
                pos += editor.space_before_cursor ? 1 : 0;
            }

            Render_Info info;
            info.pos = pos;
            info.size = token.end_index - token.start_index;
            info.color = Syntax_Color::TEXT;
            info.bg_color = vec3(0.0f);
            switch (token.type)
            {
            case Token_Type::COMMENT: info.color = Syntax_Color::COMMENT; break;
            case Token_Type::INVALID: info.color = vec3(1.0f, 0.8f, 0.8f); break;
            case Token_Type::KEYWORD: info.color = Syntax_Color::KEYWORD; break;
            case Token_Type::IDENTIFIER: info.color = Syntax_Color::IDENTIFIER_FALLBACK; break;
            case Token_Type::LITERAL: {
                switch (token.options.literal_value.type)
                {
                case Literal_Type::BOOLEAN: info.color = vec3(0.5f, 0.5f, 1.0f); break;
                case Literal_Type::STRING: info.color = Syntax_Color::STRING; break;
                case Literal_Type::INTEGER:
                case Literal_Type::FLOAT_VAL:
                case Literal_Type::NULL_VAL:
                    info.color = Syntax_Color::LITERAL_NUMBER; break;
                default: panic("");
                }
                break;
            }
            }
            pos += info.size;

            if (on_token && syntax_editor.space_before_cursor) {
                pos += 1;
            }

            if (display_space_after_token(token_index_make(i, j)) && j != line->tokens.size - 1) {
                pos += 1;
            }
            dynamic_array_push_back(&line->infos, info);
        }
    }

    // Find objects under cursor
    Token_Index cursor_token_index = token_index_make(syntax_editor.cursor.line, get_cursor_token_index(true));
    AST::Node* node = Parser::find_smallest_enclosing_node(&compiler.main_source->parsed_code->root->base, cursor_token_index);
    Analysis_Pass* pass = code_query_get_analysis_pass(node); // Note: May be null?
    Symbol* symbol = code_query_get_ast_node_symbol(node); // May be null
    // AST::Node* node = nullptr;
    // Analysis_Pass* pass = nullptr;
    // Symbol* symbol = nullptr;

    // Syntax Highlighting
    if (true)
    {
        syntax_highlighting_highlight_identifiers_recursive(&compiler.main_source->parsed_code->root->base);

        // Highlight selected symbol occurances
        if (symbol != 0)
        {
            // Highlight all instances of the symbol
            vec3 color = vec3(1.0f, 1.0f, 0.3f) * 0.3f;
            for (int i = 0; i < symbol->references.size; i++) {
                auto node = &symbol->references[i]->base;
                if (compiler_find_ast_source_code(node) != editor.code)
                    continue;
                syntax_highlighting_mark_section(node, Parser::Section::IDENTIFIER, color, color, false);
            }

            // Highlight Definition
            if (symbol->definition_node != 0 && compiler_find_ast_source_code(symbol->definition_node) == editor.code) {
                syntax_highlighting_mark_section(symbol->definition_node, Parser::Section::IDENTIFIER, color, color, false);
            }
        }
    }

    // Render Source Code
    syntax_editor_draw_block_outlines_recursive(0, 0);

    for (int i = 0; i < code->line_count; i++)
    {
        Source_Line* line = source_code_get_line(code, i);
        for (int j = 0; j < line->tokens.size; j++)
        {
            auto& token = line->tokens[j];
            auto& info = line->infos[j];
            auto str = token_get_string(token, line->text);
            syntax_editor_draw_string(str, info.color, line->screen_index, info.pos);
        }
    }

    // Draw Cursor
    if (true)
    {
        auto line = source_code_get_line(editor.code, cursor.line);
        auto& text = line->text;
        auto& tokens = line->tokens;
        auto& infos = line->infos;
        auto& pos = cursor.character;
        auto token_index = get_cursor_token_index(true);

        Render_Info info;
        if (token_index < infos.size) {
            info = infos[token_index];
        }
        else {
            info.pos = line->indentation * 4;
            info.size = 1;
        }

        if (editor.mode == Editor_Mode::NORMAL)
        {
            int box_start = info.pos;
            int box_end = math_maximum(info.pos + info.size, box_start + 1);
            syntax_editor_draw_text_background(line->screen_index, box_start, box_end - box_start, vec3(0.2f));
            syntax_editor_draw_cursor_line(line->screen_index, box_start, Syntax_Color::COMMENT);
            syntax_editor_draw_cursor_line(line->screen_index, box_end, Syntax_Color::COMMENT);
        }
        else
        {
            // Adjust token index if we are inbetween tokens
            if (pos > 0 && pos < text.size)
            {
                Token* token = &tokens[token_index];
                if (pos == token->start_index && token_index > 0) {
                    if (char_is_space_critical(text[pos - 1]) && !char_is_space_critical(text[pos])) {
                        token_index -= 1;
                        info = infos[token_index];
                    }
                }
            }

            int cursor_pos = line->indentation * 4;
            if (tokens.size != 0)
            {
                auto tok_start = tokens[token_index].start_index;
                int cursor_offset = cursor.character - tok_start;
                cursor_pos = info.pos + cursor_offset;

                if (editor.space_after_cursor && cursor_offset == 0) {
                    cursor_pos = info.pos - 1;
                }
            }
            if (editor.space_before_cursor && !editor.space_after_cursor) {
                cursor_pos += 1;
            }
            syntax_editor_draw_cursor_line(line->screen_index, cursor_pos, Syntax_Color::COMMENT);
        }
    }

    renderer_2D_draw(editor.renderer_2D, pass_2D);
    text_renderer_draw(editor.text_renderer, pass_2D);

    // Calculate context string
    if (true)
    {
        bool show_context_info = false;
        auto& context = syntax_editor.context_text;
        string_reset(&context);

        // Error messages (If cursor is directly on error)
        for (int i = 0; i < editor.errors.size; i++)
        {
            auto& error = editor.errors[i];
            if (!error.from_main_source) continue;
            syntax_highlighting_mark_range(error.range, vec3(1.0f, 0.0f, 0.0f), vec3(1.0f, 1.0f, 0.0f), true);
            if (token_range_contains(error.range, cursor_token_index)) {
                show_context_info = true;
                if (context.size != 0) {
                    string_append(&context, "\n\n");
                }
                string_append_string(&context, &error.message);
            }
        }

        // Code-Completion
        if (editor.code_completion_suggestions.size != 0 && editor.mode == Editor_Mode::INSERT)
        {
            auto& suggestions = syntax_editor.code_completion_suggestions;
            show_context_info = true;
            string_reset(&context);
            for (int i = 0; i < editor.code_completion_suggestions.size; i++)
            {
                auto sugg = editor.code_completion_suggestions[i];
                string_append_string(&context, &sugg);
                if (i != editor.code_completion_suggestions.size - 1) {
                    string_append_formated(&context, "\n");
                }
            }
        }

        // Analysis-Info
        if (!show_context_info && node->type == AST::Node_Type::EXPRESSION && pass != 0) 
        {
            auto expr = AST::downcast<AST::Expression>(node);
            auto expression_info = pass_get_node_info(pass, expr, Info_Query::TRY_READ);
            if (expression_info != 0 && expression_info->contains_errors) 
            {
                auto expr_type = expression_info_get_type(expression_info, false);
                string_append_formated(&syntax_editor.context_text, "Expr Result-Type: ");
                datatype_append_to_string(&syntax_editor.context_text, expr_type);
                show_context_info = true;
            }
        }

        // Symbol Info
        if (!show_context_info && symbol != 0)
        {
            show_context_info = true;
            string_append_string(&context, symbol->id);
            string_append_formated(&context, " ");
            Datatype* type = 0;
            switch (symbol->type)
            {
            case Symbol_Type::COMPTIME_VALUE:
                string_append_formated(&context, "Comptime ");
                type = symbol->options.constant.type;
                break;
            case Symbol_Type::HARDCODED_FUNCTION:
                type = upcast(hardcoded_type_to_signature(symbol->options.hardcoded));
                break;
            case Symbol_Type::PARAMETER: {
                auto progress = analysis_workload_try_get_function_progress(pass->origin_workload);
                type = progress->function->signature->parameters[symbol->options.parameter.index_in_non_polymorphic_signature].type;
                break;
            }
            case Symbol_Type::POLYMORPHIC_VALUE: {
                assert(pass->origin_workload->polymorphic_values.data != nullptr, "");
                const auto& value = pass->origin_workload->polymorphic_values[symbol->options.polymorphic_value.access_index];
                if (value.only_datatype_known) {
                    type = value.options.type;
                }
                else {
                    type = value.options.value.type;
                }
                break;
            }
            case Symbol_Type::TYPE:
                type = symbol->options.type;
                break;
            case Symbol_Type::VARIABLE:
                type = symbol->options.variable_type;
                break;
            default: break;
            }

            if (type != 0) {
                datatype_append_to_string(&context, type);
            }
        }

        // Draw context
        if (show_context_info)
        {
            int char_index = 0;
            auto line = source_code_get_line(editor.code, cursor.line);
            int cursor_token = get_cursor_token_index(false);
            if (cursor_token < line->tokens.size) {
                char_index = line->infos[cursor_token].pos;
            }
            else {
                char_index = line->indentation * 4;
            }
            vec2 context_pos = syntax_editor_position_to_pixel(line->screen_index, char_index);
            syntax_editor_draw_string_in_box(context, vec3(1.0f), vec3(0.2f), context_pos, syntax_editor.character_size.y * 0.8f);
        }
    }

    // Render Primitives
    renderer_2D_draw(editor.renderer_2D, pass_context);
    text_renderer_draw(editor.text_renderer, pass_context);
}



