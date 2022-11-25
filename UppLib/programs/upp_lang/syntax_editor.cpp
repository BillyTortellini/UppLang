#include "syntax_editor.hpp"

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/renderer_2d.hpp"
#include "../../utility/file_io.hpp"
#include "../../utility/character_info.hpp"

#include "../../win32/input.hpp"
#include "syntax_colors.hpp"
#include "compiler.hpp"
#include "ast.hpp"
#include "dependency_analyser.hpp"
#include "semantic_analyser.hpp"
#include "parser.hpp"
#include "source_code.hpp"
#include "code_history.hpp"

// Editor
struct Fuzzy_Search
{
    String option;
    int preamble_length;
    bool is_longer;
    bool substrings_in_order;
    bool all_characters_contained;
    int substring_count;
};

struct Error_Display
{
    String message;
    Token_Range range;
    bool from_main_source;
};

Error_Display error_display_make(String msg, Token_Range range) {
    Error_Display result;
    result.message = msg;
    result.range = range;
    result.from_main_source = compiler.main_source->code == range.start.line_index.block_index.code;
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

    Dynamic_Array<Fuzzy_Search> code_completion_suggestions;
    Input_Replay input_replay;

    // Rendering
    String context_text;
    Dynamic_Array<Error_Display> errors;
    Dynamic_Array<Token_Range> token_range_buffer;
    int camera_start_line;
    int last_line_count;
    int last_cursor_line;

    Input* input;
    Rendering_Core* rendering_core;
    Window* window;
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
    CHANGE_TOKEN,
    DELETE_TOKEN,
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
    result.recorded_inputs = dynamic_array_create_empty<Key_Message>(1);
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
    return character_index_to_token(&index_value_text(c.line_index)->tokens, c.pos, after_cursor);
}

Token get_cursor_token(bool after_cursor)
{
    auto& c = syntax_editor.cursor;
    int tok_index = get_cursor_token_index(after_cursor);
    auto& tokens = index_value_text(c.line_index)->tokens;
    if (tok_index >= tokens.size) return token_make_dummy();
    return tokens[tok_index];
}

void syntax_editor_set_text(String string)
{
    auto& editor = syntax_editor;
    editor.cursor = text_index_make(block_get_first_text_line(block_index_make_root(editor.code)), 0);

    source_code_fill_from_string(editor.code, string);
    source_code_tokenize(editor.code);
    code_history_reset(&editor.history);
    editor.last_token_synchronized = history_get_timestamp(&editor.history);
    editor.code_changed_since_last_compile = true;
    compiler_compile_clean(editor.code, Compile_Type::ANALYSIS_ONLY, string_create(syntax_editor.file_path));
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
    }
    else {
        result = string_create_static("main :: (x : int) -> void \n{\n\n}");
    }
    syntax_editor_set_text(result);
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
    index_sanitize(&cursor.line_index);
    if (index_value(cursor.line_index)->is_block_reference) {
        cursor.line_index = block_get_first_text_line(index_value(cursor.line_index)->options.block_index);
        cursor.pos = 0;
    }
    auto& text = index_value_text(cursor.line_index)->text;
    cursor.pos = math_clamp(cursor.pos, 0, editor.mode == Editor_Mode::INSERT ? text.size : math_maximum(0, text.size - 1));

    if (string_test_char(text, cursor.pos, ' ')) {
        editor.space_after_cursor = false;
    }
    if (string_test_char(text, cursor.pos - 1, ' ')) {
        editor.space_before_cursor = false;
    }
}

bool syntax_editor_sanitize_line(Line_Index line_index)
{
    if (source_block_inside_comment(line_index.block_index)) {
        return false;
    }

    // Remove all Spaces except space critical ones
    auto& editor = syntax_editor;
    auto& text = index_value_text(line_index)->text;
    auto pos = editor.cursor.pos;

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
            history_delete_char(&editor.history, text_index_make(editor.cursor.line_index, index));
            changed = true;
            if (pos > index) {
                pos -= 1;
            }
        }
        else {
            index += 1;
        }
    }

    if (index_equal(editor.cursor.line_index, line_index)) 
    {
        editor.cursor.pos = pos;
        syntax_editor_sanitize_cursor();
    }
    return changed;
}

void syntax_editor_synchronize_tokens()
{
    auto& editor = syntax_editor;
    // Get changes since last sync
    Dynamic_Array<Code_Change> changes = dynamic_array_create_empty<Code_Change>(1);
    SCOPE_EXIT(dynamic_array_destroy(&changes));
    auto now = history_get_timestamp(&editor.history);
    history_get_changes_between(&editor.history, editor.last_token_synchronized, now, &changes);
    editor.last_token_synchronized = now;
    if (changes.size != 0) {
        editor.code_changed_since_last_compile = true;
    }

    // Find out which lines were changed
    auto line_changes = dynamic_array_create_empty<Line_Index>(1);
    SCOPE_EXIT(dynamic_array_destroy(&line_changes));
    auto helper_add_delete_line_item = [&line_changes](Line_Index new_line_index, bool is_insert) -> void {
        for (int i = 0; i < line_changes.size; i++) {
            auto& line_index = line_changes[i];
            if (!index_equal(line_index.block_index, new_line_index.block_index)) {
                continue;
            }
            if (is_insert) {
                if (line_index.line_index >= new_line_index.line_index) {
                    line_index.line_index += 1;
                }
            }
            else {
                if (line_index.line_index == new_line_index.line_index) {
                    dynamic_array_swap_remove(&line_changes, i);
                    i = i - 1;
                    continue;
                }
                if (line_index.line_index > new_line_index.line_index) {
                    line_index.line_index -= 1;
                }
            }
        }
    };

    for (int i = 0; i < changes.size; i++)
    {
        auto& change = changes[i];
        switch (change.type)
        {
        case Code_Change_Type::BLOCK_INSERT: {
            helper_add_delete_line_item(change.options.block_insert.line_index, change.apply_forwards);
            break;
        }
        case Code_Change_Type::BLOCK_MERGE: {
            helper_add_delete_line_item(change.options.block_merge.from_line_index, change.apply_forwards);
            break;
        }
        case Code_Change_Type::LINE_INSERT: {
            helper_add_delete_line_item(change.options.line_insert, change.apply_forwards);
            break;
        }
        case Code_Change_Type::TEXT_INSERT:
        {
            auto& changed_line = change.options.text_insert.index.line_index;
            bool found = false;
            for (int j = 0; j < line_changes.size; j++) {
                if (index_equal(line_changes[j], changed_line)) {
                    found = true;
                }
            }
            if (!found) {
                dynamic_array_push_back(&line_changes, changed_line);
            }
            break;
        }
        default: panic("");
        }
    }

    // Update changed lines
    for (int i = 0; i < line_changes.size; i++)
    {
        auto& index = line_changes[i];
        bool changed = syntax_editor_sanitize_line(index);
        assert(!changed, "Syntax editor has to make sure that lines are sanitized after edits!");

        source_code_tokenize_line(line_changes[i]);
        logg("Synchronized: %d/%d\n", index.block_index.block_index, index.line_index);
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
    compiler_compile_incremental(&syntax_editor.history, (generate_code ? Compile_Type::BUILD_CODE : Compile_Type::ANALYSIS_ONLY));
    //compiler_compile_clean(syntax_editor.code, (generate_code ? Compile_Type::BUILD_CODE : Compile_Type::ANALYSIS_ONLY), string_create(syntax_editor.file_path));

    // Collect errors from all compiler stages
    {
        for (int i = 0; i < editor.errors.size; i++) {
            string_destroy(&editor.errors[i].message);
        }
        dynamic_array_reset(&editor.errors);

        // Parse Errors
        for (int i = 0; i < compiler.code_sources.size; i++)
        {
            auto& parse_errors = compiler.code_sources[i]->source_parse->error_messages;
            for (int j = 0; j < parse_errors.size; j++) {
                auto& error = parse_errors[j];
                dynamic_array_push_back(&editor.errors, error_display_make(string_create_static(error.msg), AST::node_range_to_token_range(error.range)));
            }
        }

        auto error_ranges = dynamic_array_create_empty<Token_Range>(1);
        SCOPE_EXIT(dynamic_array_destroy(&error_ranges));

        // Dependency Errors
        for (int i = 0; i < compiler.dependency_analyser->errors.size; i++)
        {
            auto& error = compiler.dependency_analyser->errors[i];
            auto& node = error.error_node;
            if (node == 0) continue;
            if (compiler_find_ast_code_source(node) != compiler.main_source) continue;
            dynamic_array_reset(&error_ranges);
            Parser::ast_base_get_section_token_range(node, Parser::Section::IDENTIFIER, &error_ranges);
            for (int j = 0; j < error_ranges.size; j++) {
                auto& range = error_ranges[j];
                dynamic_array_push_back(&editor.errors, error_display_make(string_create_static("Symbol already exists"), range));
            }
        }

        // Semantic Analysis Errors
        for (int i = 0; i < compiler.semantic_analyser->errors.size; i++)
        {
            auto& error = compiler.semantic_analyser->errors[i];
            auto& node = error.error_node;
            dynamic_array_reset(&error_ranges);
            if (node == 0) continue;
            if (compiler_find_ast_code_source(node) != compiler.main_source) continue;
            Parser::ast_base_get_section_token_range(node, semantic_error_get_section(error), &error_ranges);
            for (int j = 0; j < error_ranges.size; j++) {
                auto& range = error_ranges[j];
                String string = string_create_empty(4);
                semantic_error_append_to_string(error, &string);
                dynamic_array_push_back(&editor.errors, error_display_make(string, range));
            }
        }
    }
}


// Code Queries
Symbol* code_query_get_ast_node_symbol(AST::Node* base)
{
    switch (base->type)
    {
    case AST::Node_Type::DEFINITION: {
        auto definition = AST::downcast<AST::Definition>(base);
        return definition->symbol;
    }
    case AST::Node_Type::SYMBOL_READ: {
        auto read = AST::downcast<AST::Symbol_Read>(base);
        return read->resolved_symbol;
    }
    case AST::Node_Type::PARAMETER: {
        auto param = AST::downcast<AST::Parameter>(base);
        return param->symbol;
    }
    }
    return 0;
}

Analysis_Pass* code_query_get_ast_node_analysis_pass(AST::Node* base)
{
    auto& mappings = compiler.dependency_analyser->mapping_ast_to_items;
    Analysis_Item* item = 0;
    while (base != 0)
    {
        auto optional_item = hashtable_find_element(&mappings, base);
        if (optional_item != 0) {
            item = *optional_item;
            break;
        }
        base = base->parent;
    }
    if (item == 0) {
        return 0;
    }
    if (item->passes.size == 0) return 0;
    return item->passes[0];
}

Symbol_Table* code_query_get_ast_node_symbol_table(AST::Node* base)
{
    /*
    The three Nodes that have Symbol tables are:
        - Module
        - Function (Parameter symbol are here
        - Code-Block
    */
    while (base != 0)
    {
        switch (base->type)
        {
        case AST::Node_Type::MODULE: {
            auto module = AST::downcast<AST::Module>(base);
            return module->symbol_table;
        }
        case AST::Node_Type::CODE_BLOCK: {
            auto block = AST::downcast<AST::Code_Block>(base);
            return block->symbol_table;
        }
        case AST::Node_Type::EXPRESSION: {
            auto expr = AST::downcast<AST::Expression>(base);
            if (expr->type == AST::Expression_Type::FUNCTION) {
                return expr->options.function.symbol_table;
            }
            break;
        }
        }
        base = base->parent;
    }
    return compiler.dependency_analyser->root_symbol_table;
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

void editor_remove_token(int token_index)
{
    auto& editor = syntax_editor;
    auto& cursor = editor.cursor;
    auto line = index_value_text(cursor.line_index);

    syntax_editor_synchronize_tokens();
    auto& tokens = line->tokens;
    if (tokens.size == 0) {
        return;
    }

    bool critical_before = false;
    if (token_index > 0) {
        critical_before = char_is_space_critical(line->text[tokens[token_index - 1].end_index - 1]);
    }
    bool critical_after = false;
    if (token_index + 1 < tokens.size) {
        critical_after = char_is_space_critical(line->text[tokens[token_index + 1].start_index]);
    }

    history_start_complex_command(&syntax_editor.history);
    SCOPE_EXIT(history_stop_complex_command(&syntax_editor.history));
    SCOPE_EXIT(syntax_editor_sanitize_cursor());

    // Delete
    int delete_start = token_index > 0 ? tokens[token_index - 1].end_index : 0;
    int delete_end = token_index + 1 < tokens.size ? tokens[token_index + 1].start_index : line->text.size;
    cursor.pos = delete_start;
    history_delete_text(&editor.history, text_index_make(cursor.line_index, delete_start), delete_end);

    // Re-Enter critical spaces
    if (critical_before && critical_after) {
        history_insert_char(&editor.history, text_index_make(cursor.line_index, delete_start), ' ');
        cursor.pos += 1;
    }
    if (editor.mode == Editor_Mode::INSERT) {
        if (!(critical_before && critical_after)) {
            editor.space_before_cursor = critical_before;
        }
        editor.space_after_cursor = critical_after;
    }
}

void editor_split_line_at_cursor(int indentation_offset)
{
    auto& mode = syntax_editor.mode;
    auto& cursor = syntax_editor.cursor;
    auto& text = index_value_text(cursor.line_index)->text;
    int cutof_index = text.size; // Text may be invalid after applying history changes
    String cutout = string_create_substring_static(&text, cursor.pos, text.size);

    history_start_complex_command(&syntax_editor.history);
    SCOPE_EXIT(history_stop_complex_command(&syntax_editor.history));

    Line_Index new_line_index = line_index_make(cursor.line_index.block_index, cursor.line_index.line_index + 1);
    history_insert_line(&syntax_editor.history, new_line_index);
    if (indentation_offset == 1) {
        new_line_index = history_add_line_indent(&syntax_editor.history, new_line_index);
    }
    else if (indentation_offset == -1) {
        new_line_index = history_remove_line_indent(&syntax_editor.history, new_line_index);
    }
    else if (indentation_offset != 0) {
        panic("Should not happen!");
    }

    if (cursor.pos != cutof_index) {
        history_insert_text(&syntax_editor.history, text_index_make(new_line_index, 0), cutout);
        history_delete_text(&syntax_editor.history, cursor, cutof_index);
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

    auto pos = syntax_editor.cursor.pos;
    if (pos < token.end_index) {
        result = string_create_substring_static(&result, 0, pos - token.start_index);
    }
    return result;
}

bool code_completion_fuzzy_in_order(Fuzzy_Search* a, Fuzzy_Search* b)
{
    if (a->is_longer != b->is_longer) {
        return b->is_longer;
    }
    if (a->all_characters_contained != b->all_characters_contained) {
        return a->all_characters_contained;
    }
    if (a->preamble_length != b->preamble_length) {
        return a->preamble_length > b->preamble_length;
    }
    if (a->substring_count != b->substring_count) {
        return a->substring_count < b->substring_count;
    }
    if (a->substrings_in_order != b->substrings_in_order) {
        return a->substrings_in_order;
    }
    return string_in_order(&a->option, &b->option);
}

void code_completion_add_and_rank(String option, String typed)
{
    Fuzzy_Search result;
    result.option = option;
    result.is_longer = typed.size > option.size;
    result.substring_count = 0;
    result.substrings_in_order = true;
    result.preamble_length = 0;
    result.all_characters_contained = true;
    if (option.size == 0) {
        return;
    }
    if (typed.size == 0) {
        dynamic_array_push_back(&syntax_editor.code_completion_suggestions, result);
        return;
    }

    int last_sub_start = -1;
    int typed_index = 0;
    while (typed_index < typed.size)
    {
        // Find maximum substring
        int max_length = 0;
        int max_start_index = 0;
        for (int start = 0; start < option.size; start++)
        {
            int length = 0;
            while (start + length < option.size && typed_index + length < typed.size)
            {
                auto char_o = option[start + length];
                auto char_t = typed[typed_index + length];
                if (char_o != char_t) {
                    break;
                }
                length += 1;
            }
            if (length > max_length) {
                max_length = length;
                max_start_index = start;
            }
        }

        // Add to statistic
        result.substring_count += 1;
        if (max_start_index == 0 && typed_index == 0) {
            result.preamble_length = max_length;
        }
        if (max_length == 0) {
            result.all_characters_contained = false;
            typed_index += 1;
            continue;
        }
        if (max_start_index <= last_sub_start) {
            result.substrings_in_order = false;
        }
        last_sub_start = max_start_index;
        typed_index += max_length;
    }

    dynamic_array_push_back(&syntax_editor.code_completion_suggestions, result);
    return;
}

void code_completion_find_suggestions()
{
    auto& editor = syntax_editor;
    auto& suggestions = editor.code_completion_suggestions;
    dynamic_array_reset(&suggestions);
    if (editor.mode != Editor_Mode::INSERT || editor.cursor.pos == 0) {
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

    // Check if we are on special node
    syntax_editor_synchronize_with_compiler(false);
    Token_Index cursor_token_index = token_index_make(syntax_editor.cursor.line_index, get_cursor_token_index(false));
    auto node = Parser::find_smallest_enclosing_node(&compiler.main_source->source_parse->root->base, cursor_token_index);

    bool fill_from_symbol_table = false;
    Symbol_Table* specific_table = 0;

    if (node->type == AST::Node_Type::EXPRESSION)
    {
        auto expr = AST::downcast<AST::Expression>(node);
        auto source_parse = code_query_get_ast_node_analysis_pass(node);
        Type_Signature* type = 0;
        if (expr->type == AST::Expression_Type::MEMBER_ACCESS && source_parse != 0) {
            auto info = analysis_pass_get_info(source_parse, expr->options.member_access.expr);
            type = expression_info_get_type(info);
            if (info->result_type == Expression_Result_Type::TYPE) {
                type = info->options.type;
            }
        }
        else if (expr->type == AST::Expression_Type::AUTO_ENUM && source_parse != 0) {
            type = expression_info_get_type(analysis_pass_get_info(source_parse, expr));
        }
        else {
            fill_from_symbol_table = true;
        }

        if (type != 0)
        {
            switch (type->type)
            {
            case Signature_Type::ARRAY:
            case Signature_Type::SLICE: {
                code_completion_add_and_rank(string_create_static("data"), partially_typed);
                code_completion_add_and_rank(string_create_static("size"), partially_typed);
                break;
            }
            case Signature_Type::STRUCT: {
                for (int i = 0; i < type->options.structure.members.size; i++) {
                    auto& mem = type->options.structure.members[i];
                    code_completion_add_and_rank(*mem.id, partially_typed);
                }
                break;
            }
            case Signature_Type::ENUM:
                for (int i = 0; i < type->options.enum_type.members.size; i++) {
                    auto& mem = type->options.enum_type.members[i];
                    code_completion_add_and_rank(*mem.id, partially_typed);
                }
                break;
            }
        }
    }
    else if (node->parent != 0 && node->parent->type == AST::Node_Type::SYMBOL_READ)
    {
        auto parent_read = AST::downcast<AST::Symbol_Read>(node->parent);
        if (parent_read->resolved_symbol->type == Symbol_Type::MODULE) {
            specific_table = parent_read->resolved_symbol->options.module_table;
        }
        fill_from_symbol_table = true;
    }
    else {
        fill_from_symbol_table = true;
    }

    // Early exit if nothing has been typed (And we aren't on member access or similar)
    if (partially_typed.size == 0 && fill_from_symbol_table && specific_table == 0) {
        return;
    }

    // Find all available symbols
    if (fill_from_symbol_table)
    {
        Symbol_Table* symbol_table;
        if (specific_table != 0) {
            symbol_table = specific_table;
        }
        else {
            symbol_table = code_query_get_ast_node_symbol_table(node);
        }

        while (symbol_table != 0)
        {
            auto iter = hashtable_iterator_create(&symbol_table->symbols);
            while (hashtable_iterator_has_next(&iter))
            {
                Symbol* symbol = (*iter.value);
                code_completion_add_and_rank(*symbol->id, partially_typed);
                hashtable_iterator_next(&iter);
            }
            symbol_table = symbol_table->parent;

            // If a specific table is specified, only iterate over that one
            if (specific_table != 0) {
                break;
            }
        }
    }

    if (suggestions.size == 0) return;
    // Sort by ranking
    dynamic_array_bubble_sort(suggestions, code_completion_fuzzy_in_order);
    // Cut off at appropriate point
    int last_cutoff = 1;
    auto& last_sug = suggestions[0];
    const int MIN_CUTTOFF_VALUE = 3;
    for (int i = 1; i < suggestions.size; i++)
    {
        auto& sug = suggestions[i];
        bool valid_cutoff = false;
        if (last_sug.is_longer != sug.is_longer) valid_cutoff = true;
        if (last_sug.substrings_in_order != sug.substrings_in_order) valid_cutoff = true;
        if (last_sug.preamble_length != sug.preamble_length) valid_cutoff = true;
        if (last_sug.all_characters_contained != sug.all_characters_contained) valid_cutoff = true;
        if (last_sug.substring_count != sug.substring_count) valid_cutoff = true;
        if (valid_cutoff && i >= MIN_CUTTOFF_VALUE) {
            dynamic_array_rollback_to_size(&suggestions, i);
            return;
        }
    }
}

void code_completion_insert_suggestion()
{
    auto& editor = syntax_editor;
    auto& suggestions = editor.code_completion_suggestions;
    if (suggestions.size == 0) return;
    if (editor.cursor.pos == 0) return;
    String replace_string = suggestions[0].option;
    auto line = index_value(editor.cursor.line_index);
    // Remove current token
    int token_index = get_cursor_token_index(false);
    int start_pos = index_value_text(editor.cursor.line_index)->tokens[token_index].start_index;
    history_delete_text(&editor.history, text_index_make(editor.cursor.line_index, start_pos), editor.cursor.pos);
    // Insert suggestion instead
    editor.cursor.pos = start_pos;
    history_insert_text(&editor.history, editor.cursor, replace_string);
    editor.cursor.pos += replace_string.size;
    syntax_editor_sanitize_line(editor.cursor.line_index);
}



void normal_command_execute(Normal_Command command)
{
    auto& editor = syntax_editor;
    auto& cursor = editor.cursor;
    auto line = index_value_text(cursor.line_index);
    auto& tokens = line->tokens;

    SCOPE_EXIT(history_set_cursor_pos(&editor.history, editor.cursor));
    SCOPE_EXIT(syntax_editor_sanitize_cursor());

    editor.space_before_cursor = false;
    switch (command)
    {
    case Normal_Command::INSERT_AFTER: {
        syntax_editor_synchronize_tokens();
        editor_enter_insert_mode();
        cursor.pos = get_cursor_token(true).end_index;
        break;
    }
    case Normal_Command::INSERT_BEFORE: {
        syntax_editor_synchronize_tokens();
        editor_enter_insert_mode();
        cursor.pos = get_cursor_token(true).start_index;
        break;
    }
    case Normal_Command::MOVE_LEFT: {
        syntax_editor_synchronize_tokens();
        if (tokens.size == 0) {
            cursor.pos = 0;
            break;
        }
        auto index = get_cursor_token_index(false);
        cursor.pos = tokens[index].start_index;
        break;
    }
    case Normal_Command::MOVE_RIGHT: {
        syntax_editor_synchronize_tokens();
        auto index = get_cursor_token_index(true) + 1;
        if (index >= tokens.size) break;
        cursor.pos = tokens[index].start_index;
        break;
    }
    case Normal_Command::INSERT_AT_LINE_END: {
        cursor.pos = line->text.size;
        editor_enter_insert_mode();
        break;
    }
    case Normal_Command::INSERT_AT_LINE_START: {
        cursor.pos = 0;
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
    case Normal_Command::DELETE_TOKEN:
    case Normal_Command::CHANGE_TOKEN:
    {
        if (command == Normal_Command::CHANGE_TOKEN) {
            editor_enter_insert_mode();
        }
        editor_remove_token(get_cursor_token_index(true));
        break;
    }
    case Normal_Command::MOVE_LINE_START: {
        cursor.pos = 0;
        break;
    }
    case Normal_Command::MOVE_LINE_END: {
        cursor.pos = line->text.size;
        break;
    }
    case Normal_Command::ADD_LINE_ABOVE:
    case Normal_Command::ADD_LINE_BELOW:
    {
        history_start_complex_command(&editor.history);
        SCOPE_EXIT(history_stop_complex_command(&editor.history));

        bool below = command == Normal_Command::ADD_LINE_BELOW;
        auto new_line = line_index_make(cursor.line_index.block_index, below ? cursor.line_index.line_index + 1 : cursor.line_index.line_index);
        history_insert_line(&editor.history, new_line);
        cursor = text_index_make(new_line, 0);
        editor_enter_insert_mode();
        break;
    }
    case Normal_Command::MOVE_UP: {
        // FUTURE: Use token render positions to move up/down
        cursor.line_index = line_index_prev(cursor.line_index);
        break;
    }
    case Normal_Command::MOVE_DOWN: {
        cursor.line_index = line_index_next(cursor.line_index);
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
    auto line = index_value_text(cursor.line_index);
    auto& text = line->text;
    auto& pos = cursor.pos;

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
            cursor.line_index = history_add_line_indent(&syntax_editor.history, cursor.line_index);
            break;
        }
        editor_split_line_at_cursor(1);
        break;
    }
    case Insert_Command_Type::REMOVE_INDENTATION: {
        cursor.line_index = history_remove_line_indent(&syntax_editor.history, cursor.line_index);
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
        syntax_editor_sanitize_line(cursor.line_index);
        break;
    }
    case Insert_Command_Type::SPACE:
    {
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
            auto prev_line = line_index_prev(cursor.line_index);
            if (index_equal(prev_line, cursor.line_index)) {
                // We are at the first line_index in the code
                break;
            }
            // Merge this line_index with previous one
            Text_Index insert_index = text_index_make(prev_line, index_value_text(prev_line)->text.size);
            history_insert_text(&syntax_editor.history, insert_index, text);
            history_remove_line(&syntax_editor.history, cursor.line_index);
            cursor = insert_index;
            syntax_editor_sanitize_line(cursor.line_index);
            break;
        }

        syntax_editor.space_after_cursor = string_test_char(text, pos, ' ') || syntax_editor.space_after_cursor;
        history_delete_char(&syntax_editor.history, text_index_make(cursor.line_index, pos - 1));
        pos -= 1;
        syntax_editor.space_before_cursor = string_test_char(text, pos - 1, ' ');
        syntax_editor_sanitize_line(cursor.line_index);
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
        if (msg.key_code == Key_Code::SPACE && msg.key_down) {
            input.type = msg.ctrl_down ? Insert_Command_Type::INSERT_CODE_COMPLETION : Insert_Command_Type::SPACE;
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
                command = Normal_Command::CHANGE_TOKEN;
            }
        }
        else if (msg.key_code == Key_Code::X && msg.key_down) {
            command = Normal_Command::DELETE_TOKEN;
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

void syntax_editor_initialize(Rendering_Core * rendering_core, Text_Renderer * text_renderer, Renderer_2D * renderer_2D, Input * input, Timer * timer)
{
    memory_zero(&syntax_editor);
    syntax_editor.context_text = string_create_empty(256);
    syntax_editor.errors = dynamic_array_create_empty<Error_Display>(1);
    syntax_editor.token_range_buffer = dynamic_array_create_empty<Token_Range>(1);
    syntax_editor.code_completion_suggestions = dynamic_array_create_empty<Fuzzy_Search>(1);

    syntax_editor.code = source_code_create();
    syntax_editor.history = code_history_create(syntax_editor.code);
    syntax_editor.last_token_synchronized = history_get_timestamp(&syntax_editor.history);
    syntax_editor.code_changed_since_last_compile = true;

    syntax_editor.text_renderer = text_renderer;
    syntax_editor.rendering_core = rendering_core;
    syntax_editor.renderer_2D = renderer_2D;
    syntax_editor.input = input;
    syntax_editor.mode = Editor_Mode::NORMAL;
    syntax_editor.cursor = text_index_make(line_index_make(block_index_make(syntax_editor.code, 0), 0), 0);
    syntax_editor.camera_start_line = 0;
    syntax_editor.last_line_count = 100;

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
vec2 text_to_screen_coord(int line_index, int character)
{
    auto editor = &syntax_editor;
    float w = editor->rendering_core->render_information.viewport_width;
    float h = editor->rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 char_size = editor->character_size * scaling_factor;
    vec2 line_start = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = line_start + vec2(character * char_size.x, -(line_index + 1) * char_size.y);
    return cursor;
}

void syntax_editor_draw_underline(int line_index, int character, int length, vec3 color)
{
    float w = syntax_editor.rendering_core->render_information.viewport_width;
    float h = syntax_editor.rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 char_size = syntax_editor.character_size * scaling_factor;
    vec2 size = char_size * vec2((float)length, 1 / 8.0f);
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * char_size.x, -(line_index + 1) * char_size.y);
    cursor = cursor + size * vec2(0.5f, 0.5f);
    renderer_2D_add_rectangle(syntax_editor.renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_cursor_line(int line_index, int character, vec3 color)
{
    auto editor = &syntax_editor;
    float w = editor->rendering_core->render_information.viewport_width;
    float h = editor->rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 char_size = editor->character_size * scaling_factor;
    vec2 size = char_size * vec2(0.1f, 1.0f);
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * char_size.x, -(line_index + 1) * char_size.y);
    cursor = cursor + size * vec2(0.5f, 0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_text_background(int line_index, int character, int length, vec3 color)
{
    auto& editor = syntax_editor;
    auto offset = editor.character_size * vec2(0.0f, 0.5f);

    float w = editor.rendering_core->render_information.viewport_width;
    float h = editor.rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }

    vec2 char_size = editor.character_size * scaling_factor;
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * char_size.x, -line_index * char_size.y);

    vec2 size = char_size * vec2((float)length, 1.0f);
    cursor = cursor + size * vec2(0.5f, -0.5f);
    renderer_2D_add_rectangle(editor.renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_string(String string, vec3 color, int line_index, int character)
{
    auto editor = &syntax_editor;
    vec2 pos = vec2(-1.0f, 1.0f) + vec2(character, -(line_index + 1)) * editor->character_size;
    text_renderer_set_color(editor->text_renderer, color);
    text_renderer_add_text(editor->text_renderer, &string, pos, editor->character_size.y, 1.0f);
}

void syntax_editor_draw_string_in_box(String string, vec3 text_color, vec3 box_color, int line_index, int character, float text_size)
{
    int text_width = 0;
    int max_text_width = 0;
    int text_height = 1;
    for (int i = 0; i < string.size; i++) {
        auto c = string[i];
        if (c == '\n') {
            text_width = 0;
            text_height += 1;
        }
        else {
            text_width += 1;
            max_text_width = math_maximum(max_text_width, text_width);
        }
    }

    auto editor = &syntax_editor;
    vec2 pos = vec2(-1.0f, 1.0f) + vec2(character, -line_index) * editor->character_size + vec2(0.0f, -editor->character_size.y * text_size * text_height);
    text_renderer_set_color(editor->text_renderer, text_color);
    text_renderer_add_text(editor->text_renderer, &string, pos, editor->character_size.y * text_size, 1.0f);

    float w = editor->rendering_core->render_information.viewport_width;
    float h = editor->rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 size = editor->character_size * scaling_factor;
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * size.x, -line_index * size.y);
    size = size * vec2(max_text_width, text_height) * text_size;
    cursor = cursor + size * vec2(0.5f, -0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, box_color, 0.0f);
}

void syntax_editor_draw_block_outline(int line_start, int line_end, int indentation)
{
    if (indentation == 0) return;
    auto offset = syntax_editor.character_size * vec2(0.5f, 1.0f);
    vec2 start = text_to_screen_coord(line_start, (indentation - 1) * 4) + offset;
    vec2 end = text_to_screen_coord(line_end, (indentation - 1) * 4) + offset;
    start.y -= syntax_editor.character_size.y * 0.1;
    end.y += syntax_editor.character_size.y * 0.1;
    renderer_2D_add_line(syntax_editor.renderer_2D, start, end, vec3(0.4f), 3, 0.0f);
    vec2 l_end = end + vec2(syntax_editor.character_size.x * 0.5f, 0.0f);
    renderer_2D_add_line(syntax_editor.renderer_2D, end, l_end, vec3(0.4f), 3, 0.0f);
}



// Syntax Highlighting
void syntax_highlighting_mark_range(Token_Range range, vec3 normal_color, vec3 empty_range_color, bool underline)
{
    auto index = range.start;
    auto end = range.end;

    typedef void (*draw_fn)(int line_index, int character, int length, vec3 color);
    draw_fn draw_mark = underline ? syntax_editor_draw_underline : syntax_editor_draw_text_background;

    assert(index_compare(index, end) >= 0, "hey");
    if (index_equal(index, end))
    {
        auto line = index_value_text(index.line_index);
        if (token_index_is_end_of_line(index)) {
            draw_mark(line->render_index, line->render_end_pos, 1, empty_range_color);
        }
        else {
            auto& info = line->infos[index.token];
            draw_mark(line->render_index, info.pos, info.size, normal_color);
        }
        return;
    }

    bool quit_loop = false;
    while (true)
    {
        auto line = index_value_text(index.line_index);

        int draw_start = line->render_start_pos;
        int draw_end = line->render_end_pos;
        if (!token_index_is_end_of_line(index)) {
            draw_start = line->infos[index.token].pos;
        }
        if (index_equal(index.line_index, end.line_index)) {
            if (end.token - 1 >= 0) {
                const auto& info = line->infos[end.token - 1];
                draw_end = info.pos + info.size;
            }
        }

        if (draw_start != draw_end) {
            draw_mark(line->render_index, draw_start, draw_end - draw_start, normal_color);
        }
        if (index_equal(index.line_index, end.line_index)) {
            break;
        }
        index = token_index_make(line_index_next(index.line_index), 0);
    }
}

void syntax_highlighting_mark_section(AST::Node * base, Parser::Section section, vec3 normal_color, vec3 empty_range_color, bool underline)
{
    assert(base != 0, "");
    auto ranges = syntax_editor.token_range_buffer;
    dynamic_array_reset(&ranges);

    auto& node = base;
    Parser::ast_base_get_section_token_range(node, section, &ranges);
    for (int i = 0; i < ranges.size; i++) {
        syntax_highlighting_mark_range(ranges[i], normal_color, empty_range_color, underline);
    }
}

void syntax_highlighting_set_section_text_color(AST::Node * base, Parser::Section section, vec3 color)
{
    assert(base != 0, "");
    auto ranges = syntax_editor.token_range_buffer;
    dynamic_array_reset(&ranges);

    auto& node = base;
    Parser::ast_base_get_section_token_range(node, section, &ranges);
    for (int i = 0; i < ranges.size; i++)
    {
        const auto& range = ranges[i];
        auto iter = range.start;
        while (!index_equal(iter, range.end)) {
            if (token_index_is_end_of_line(iter)) {
                iter = token_index_make(line_index_next(iter.line_index), 0);
                continue;
            }
            auto line = index_value_text(iter.line_index);
            line->infos[iter.token].color = color;
            iter = token_index_next(iter);
        }
    }
}

void syntax_highlighting_set_symbol_colors(AST::Node * base)
{
    Symbol* symbol = code_query_get_ast_node_symbol(base);
    if (symbol != 0) {
        syntax_highlighting_set_section_text_color(base, Parser::Section::IDENTIFIER, symbol_type_to_color(symbol->type));
    }

    // Do syntax highlighting for children
    int index = 0;
    auto child = AST::base_get_child(base, index);
    while (child != 0)
    {
        syntax_highlighting_set_symbol_colors(child);
        index += 1;
        child = AST::base_get_child(base, index);
    }
}




// Code Layout 
void operator_space_before_after(Token_Index index, bool& space_before, bool& space_after)
{
    auto& tokens = index_value_text(index.line_index)->tokens;
    assert(tokens[index.token].type == Token_Type::OPERATOR, "");
    auto op_info = syntax_operator_info(tokens[index.token].options.op);
    space_before = false;
    space_after = false;

    bool use_info = true;
    // Approximate if Operator is binop or not (Sometimes this cannot be detected with parsing alone)
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
    auto line = index_value_text(index.line_index);
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
    if (a.type == Token_Type::KEYWORD) {
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
        operator_space_before_after(token_index_make(index.line_index, index.token + 1), space_before, unused);
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

void syntax_editor_layout_line(Line_Index line_index, int screen_index, int indentation)
{
    auto& editor = syntax_editor;
    auto& cursor = editor.cursor;
    auto line = index_value_text(line_index);
    line->render_index = screen_index;
    line->render_indent = indentation;
    line->render_start_pos = indentation * 4;

    dynamic_array_reset(&line->infos);
    int pos = indentation * 4;
    for (int i = 0; i < line->tokens.size; i++)
    {
        auto& token = line->tokens[i];
        bool on_token = index_equal(cursor.line_index, line_index) && get_cursor_token_index(true) == i;
        if (on_token && token.start_index == cursor.pos) {
            pos += editor.space_after_cursor ? 1 : 0;
            pos += editor.space_before_cursor ? 1 : 0;
        }

        Render_Info info;
        info.line_index = line->render_index;
        info.pos = pos;
        info.size = token.end_index - token.start_index;
        info.color = Syntax_Color::TEXT;
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

        if (display_space_after_token(token_index_make(line_index, i)) && i != line->tokens.size - 1) {
            pos += 1;
        }
        dynamic_array_push_back(&line->infos, info);
    }
    line->render_end_pos = pos;
}

void syntax_editor_layout_block(Block_Index block_index, int* screen_index, int indentation)
{
    auto block = index_value(block_index);
    block->render_start = *screen_index;
    block->render_indent = indentation;

    for (int i = 0; i < block->lines.size; i++)
    {
        auto& line = block->lines[i];
        if (line.is_block_reference) {
            syntax_editor_layout_block(line.options.block_index, screen_index, indentation + 1);
        }
        else {
            syntax_editor_layout_line(line_index_make(block_index, i), *screen_index, indentation);
            *screen_index += 1;
        }
    }
    block->render_end = *screen_index;
}

void syntax_editor_layout_apply_camera_offset(Block_Index block_index)
{
    auto block = index_value(block_index);
    block->render_start -= syntax_editor.camera_start_line;
    block->render_end -= syntax_editor.camera_start_line;

    for (int i = 0; i < block->lines.size; i++)
    {
        auto& line = block->lines[i];
        if (line.is_block_reference) {
            syntax_editor_layout_apply_camera_offset(line.options.block_index);
        }
        else {
            auto& text = line.options.text;
            text.render_index -= syntax_editor.camera_start_line;
            for (int j = 0; j < text.infos.size; j++) {
                text.infos[j].line_index -= syntax_editor.camera_start_line;
            }
        }
    }
}



// Rendering
void syntax_editor_render_block(Block_Index block_index)
{
    auto block = index_value(block_index);
    // Render lines
    for (int i = 0; i < block->lines.size; i++)
    {
        auto line = block->lines[i];
        if (line.is_block_reference) {
            syntax_editor_render_block(line.options.block_index);
        }
        else {
            auto& text = line.options.text;
            for (int j = 0; j < text.tokens.size; j++)
            {
                auto& token = text.tokens[j];
                auto& info = text.infos[j];
                auto str = token_get_string(token, text.text);
                syntax_editor_draw_string(str, info.color, info.line_index, info.pos);
            }
        }
    }
    syntax_editor_draw_block_outline(block->render_start, block->render_end, block->render_indent);
}

bool syntax_editor_display_analysis_info(AST::Node * node)
{
    if (node->type != AST::Node_Type::EXPRESSION) return false;
    auto expr = AST::downcast<AST::Expression>(node);
    if (expr->type != AST::Expression_Type::MEMBER_ACCESS) return false;

    auto source_parse = code_query_get_ast_node_analysis_pass(node);
    if (source_parse == 0) return false;

    auto expr_type = expression_info_get_type(analysis_pass_get_info(source_parse, expr));
    string_append_formated(&syntax_editor.context_text, "Expr Result-Type: ");
    type_signature_append_to_string(&syntax_editor.context_text, expr_type);
    return true;
}

void syntax_editor_render()
{
    auto& editor = syntax_editor;
    auto& cursor = syntax_editor.cursor;

    // Prepare Render
    editor.character_size.y = text_renderer_cm_to_relative_height(editor.text_renderer, editor.rendering_core, 0.55f);
    editor.character_size.x = text_renderer_get_cursor_advance(editor.text_renderer, editor.character_size.y);

    // Layout Source Code
    syntax_editor_sanitize_cursor();
    int screen_index = 0;
    syntax_editor_layout_block(block_index_make_root(editor.code), &screen_index, 0);

    // Check where cursor is
    {
        int line_count = 2.0f / editor.character_size.y;
        editor.last_line_count = line_count;
        int visible_start = editor.camera_start_line;
        int visible_end = visible_start + line_count;

        auto cursor_line = index_value_text(cursor.line_index)->render_index;
        editor.last_cursor_line = cursor_line;
        if (cursor_line < visible_start) {
            editor.camera_start_line = cursor_line;
        }
        else if (cursor_line > visible_end) {
            editor.camera_start_line = cursor_line - line_count;
        }
        // Adjust all render infos
        syntax_editor_layout_apply_camera_offset(block_index_make_root(editor.code));
    }

    // Draw Text-Representation @ the bottom of the screen 
    if (false)
    {
        int line_index = 2.0f / editor.character_size.y - 1;
        syntax_editor_draw_string(index_value_text(cursor.line_index)->text, Syntax_Color::TEXT, line_index, 0);
        if (editor.mode == Editor_Mode::NORMAL) {
            syntax_editor_draw_text_background(line_index, cursor.pos, 1, Syntax_Color::COMMENT);
        }
        else {
            syntax_editor_draw_cursor_line(line_index, cursor.pos, Syntax_Color::COMMENT);
        }
    }

    bool show_context_info = false;
    auto& context = syntax_editor.context_text;
    string_reset(&context);

    // Draw error messages
    Token_Index cursor_token_index = token_index_make(syntax_editor.cursor.line_index, get_cursor_token_index(true));
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

    //if (!show_context_info)
    //{
    //    show_context_info = true;
    //    auto node = Parser::find_smallest_enclosing_node(&compiler.main_source->source_parse->root->base, cursor_token_index);
    //    AST::base_append_to_string(node, &context);
    //}

    // Draw Code-Completion
    if (editor.code_completion_suggestions.size != 0 && editor.mode == Editor_Mode::INSERT)
    {
        auto& suggestions = syntax_editor.code_completion_suggestions;
        show_context_info = true;
        string_reset(&context);
        for (int i = 0; i < editor.code_completion_suggestions.size; i++)
        {
            auto sugg = editor.code_completion_suggestions[i];
            string_append_string(&context, &sugg.option);
            if (i != editor.code_completion_suggestions.size - 1) {
                string_append_formated(&context, "\n");
            }
        }
    }

    // Display Hover Information
    {
        auto node = Parser::find_smallest_enclosing_node(&compiler.main_source->source_parse->root->base, cursor_token_index);

        if (!show_context_info) {
            show_context_info = syntax_editor_display_analysis_info(node);
        }

        // Check if it is definition or Symbol Read
        Symbol* symbol = code_query_get_ast_node_symbol(node);
        if (symbol != 0)
        {
            // Highlight all instances of the symbol
            vec3 color = vec3(1.0f, 1.0f, 0.3f) * 0.3f;
            for (int i = 0; i < symbol->references.size; i++) {
                auto node = &symbol->references[i]->read->base;
                if (compiler_find_ast_source_code(node) != editor.code)
                    continue;
                syntax_highlighting_mark_section(node, Parser::Section::IDENTIFIER, color, color, false);
            }

            // Highlight Definition
            if (symbol->definition_node != 0 && compiler_find_ast_source_code(symbol->definition_node) == editor.code) {
                syntax_highlighting_mark_section(symbol->definition_node, Parser::Section::IDENTIFIER, color, color, false);
            }

            // Show Symbol context
            if (!show_context_info)
            {
                show_context_info = true;
                string_append_string(&context, symbol->id);
                string_append_formated(&context, " ");
                while (symbol->type == Symbol_Type::SYMBOL_ALIAS) {
                    string_append_formated(&context, " alias of %s ", symbol->options.alias->id->characters);
                    symbol = symbol->options.alias;
                }
                Type_Signature* type = 0;
                switch (symbol->type)
                {
                case Symbol_Type::COMPTIME_VALUE:
                    string_append_formated(&context, "Comptime ");
                    type = symbol->options.constant.type;
                    break;
                case Symbol_Type::HARDCODED_FUNCTION:
                    type = hardcoded_type_to_signature(symbol->options.hardcoded);
                    break;
                case Symbol_Type::PARAMETER:
                    type = symbol->options.parameter.type;
                    break;
                case Symbol_Type::TYPE:
                    type = symbol->options.type;
                    break;
                case Symbol_Type::VARIABLE:
                    type = symbol->options.variable_type;
                    break;
                }
                if (type != 0) {
                    type_signature_append_to_string(&context, type);
                }
            }
        }
    }

    if (!show_context_info) {
        show_context_info = true;
        auto str = token_type_as_string(get_cursor_token(false).type);
        string_append_string(&context, &str);
    }
    // Syntax Highlighting
    syntax_highlighting_set_symbol_colors(&compiler.main_source->source_parse->root->base);

    // Draw Cursor
    {
        auto line = index_value_text(cursor.line_index);
        auto& text = line->text;
        auto& tokens = line->tokens;
        auto& infos = line->infos;
        auto& pos = cursor.pos;
        auto token_index = get_cursor_token_index(true);

        Render_Info info;
        if (token_index < infos.size) {
            info = infos[token_index];
        }
        else {
            info.pos = line->render_indent * 4;
            info.size = 1;
            info.line_index = line->render_index;
        }

        if (editor.mode == Editor_Mode::NORMAL)
        {
            int box_start = info.pos;
            int box_end = math_maximum(info.pos + info.size, box_start + 1);
            syntax_editor_draw_text_background(info.line_index, box_start, box_end - box_start, vec3(0.2f));
            syntax_editor_draw_cursor_line(info.line_index, box_start, Syntax_Color::COMMENT);
            syntax_editor_draw_cursor_line(info.line_index, box_end, Syntax_Color::COMMENT);
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

            int cursor_pos = line->render_indent * 4;
            if (tokens.size != 0)
            {
                auto tok_start = tokens[token_index].start_index;
                int cursor_offset = cursor.pos - tok_start;
                cursor_pos = info.pos + cursor_offset;

                if (editor.space_after_cursor && cursor_offset == 0) {
                    cursor_pos = info.pos - 1;
                }
            }
            if (editor.space_before_cursor && !editor.space_after_cursor) {
                cursor_pos += 1;
            }
            syntax_editor_draw_cursor_line(line->render_index, cursor_pos, Syntax_Color::COMMENT);
        }
    }

    // Render Source Code
    syntax_editor_render_block(block_index_make_root(editor.code));

    // Render Primitives
    renderer_2D_render(editor.renderer_2D, editor.rendering_core);
    text_renderer_render(editor.text_renderer, editor.rendering_core);

    // Draw context
    if (show_context_info)
    {
        Token_Index display_index = token_index_make(syntax_editor.cursor.line_index, get_cursor_token_index(false));
        auto line = index_value_text(display_index.line_index);
        int char_index = 0;
        if (!token_index_is_end_of_line(display_index)) {
            auto& info = line->infos[display_index.token];
            char_index = info.pos; // + (cursor.pos - index_value(cursor_token_index)->start_index);
        }
        syntax_editor_draw_string_in_box(context, vec3(1.0f), vec3(0.2f), line->render_index + 1, char_index, 0.8f);
    }

    // Render Primitives (2nd Pass so context is above all else)
    // PERF: This is garbage rendering, since this will probably require a GPU/CPU synchronization
    renderer_2D_render(editor.renderer_2D, editor.rendering_core);
    text_renderer_render(editor.text_renderer, editor.rendering_core);
}



