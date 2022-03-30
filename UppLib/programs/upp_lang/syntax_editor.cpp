#include "syntax_editor.hpp"

#include "../../rendering/text_renderer.hpp"
#include "../../datastructures/string.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/renderer_2d.hpp"

#include "../../win32/input.hpp"
#include "syntax_colors.hpp"

// PROTOTYPES
int syntax_editor_find_visual_cursor_pos(Syntax_Editor* editor, int line_pos, int line_index, Editor_Mode mode);
void syntax_editor_insert_line(Syntax_Editor* editor, int index, int indentation_level);

// Helpers
String characters_get_valid_identifier_characters() {
    return string_create_static("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890_");
}

String characters_get_non_identifier_non_whitespace() {
    return string_create_static("!\"§$%&/()[]{}<>|=\\?´`+*~#'-.:,;^°");
}

String characters_get_whitespaces() {
    return string_create_static("\n \t");
}

String characters_get_all_letters() {
    return string_create_static("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

bool char_is_digit(int c) {
    return (c >= '0' && c <= '9');
}

bool char_is_letter(int c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

void syntax_token_append_to_string(Syntax_Token token, String* string)
{
    switch (token.type)
    {
    case Syntax_Token_Type::IDENTIFIER:
        string_append_string(string, &token.options.identifier);
        break;
    case Syntax_Token_Type::DELIMITER: {
        string_append_character(string, token.options.delimiter);
        break;
    }
    case Syntax_Token_Type::NUMBER:
        string_append_string(string, &token.options.number);
        break;
    default: panic("");
    }
}



// Parsing
bool format_parser_parse_any(Format_Parser* parser);
bool format_parser_parse_operand(Format_Parser* parser);
bool format_parser_parse_simple_operand(Format_Parser* parser);
bool format_parser_parse_parenthesis(Format_Parser* parser);
bool format_parser_test_delimiter(Format_Parser* parser, char delimiter);

Format_Item* format_parser_add_empty_item(Format_Parser* parser, Format_Item_Type type, int token_length)
{
    Format_Item item;
    item.type = type;
    item.token_start_index = parser->index;
    item.token_length = token_length;
    dynamic_array_push_back(&parser->line->format_items, item);
    parser->index += token_length;
    return &parser->line->format_items[parser->line->format_items.size - 1];
}

Optional<Operator_Mapping> format_parser_test_operator(Format_Parser* parser)
{
    auto& tokens = parser->line->tokens;
    auto& index = parser->index;
    if (parser->index >= parser->max_parse_index) {
        return optional_make_failure<Operator_Mapping>();
    }

    auto& op_table = parser->editor->operator_mapping;
    int longest = -1;
    int longest_index = -1;
    for (int i = 0; i < op_table.size; i++)
    {
        auto& op_str = op_table[i].string;
        bool matches = true;
        for (int j = 0; j < op_str.size; j++)
        {
            char op_char = op_str[j];
            if (index + j >= parser->max_parse_index) {
                matches = false;
                break;
            }
            auto& token = tokens[index + j];
            if (!(token.type == Syntax_Token_Type::DELIMITER && token.options.delimiter == op_char)) {
                matches = false;
                break;
            }
        }
        if (matches && op_str.size > longest) {
            longest_index = i;
            longest = op_str.size;
        }
    }
    if (longest_index != -1) {
        return optional_make_success(op_table[longest_index]);
    }
    return optional_make_failure<Operator_Mapping>();
}

bool format_parser_parse_binop(Format_Parser* parser)
{
    auto& binop_mapping = parser->editor->binop_mapping;
    Optional<int> result = format_parser_test_operator(parser, binop_mapping);
    if (result.available) {
        auto item = format_parser_add_empty_item(parser, Format_Item_Type::BINARY_OPERAND, binop_mapping[result.value].size);
        item->options.binop = (Format_Binop)result.value;
    }
    return result.available;
}

bool format_parser_parse_unop(Format_Parser* parser)
{
    auto& unop_mapping = parser->editor->unop_mapping;
    Optional<int> result = format_parser_test_operator(parser, unop_mapping);
    if (result.available) {
        auto item = format_parser_add_empty_item(parser, Format_Item_Type::UNARY_OPERAND, unop_mapping[result.value].size);
        item->options.unop = (Format_Unop)result.value;
    }
    return result.available;
}

bool format_parser_parse_simple_operand(Format_Parser* parser)
{
    /*
        Valid simple Operands are:
            - Identifier
            - Keywords + optional additional expressions
            - Literal (Number/String/Bool)
            - Parenthesis
    */
    auto& tokens = parser->line->tokens;
    auto& index = parser->index;
    if (index >= parser->max_parse_index) {
        return false;
    }
    auto& token = tokens[index];
    if (token.type == Syntax_Token_Type::NUMBER) {
        format_parser_add_empty_item(parser, Format_Item_Type::LITERAL, 1);
        return true;
    }
    else if (token.type == Syntax_Token_Type::IDENTIFIER) 
    {
        auto& keywords = parser->editor->keyword_table;
        Format_Keyword* found_keyword = hashtable_find_element(&keywords, token.options.identifier);
        if (found_keyword != 0) {
            Format_Item* item = format_parser_add_empty_item(parser, Format_Item_Type::KEYWORD, 1);
            item->options.keyword = *found_keyword;
            // After keywords anything can happen again
            format_parser_parse_any(parser);
        }
        else {
            format_parser_add_empty_item(parser, Format_Item_Type::IDENTIFIER, 1);
        }
        return true;
    }
    else if (format_parser_test_delimiter(parser, '(')) {
        bool must_succeed = format_parser_parse_parenthesis(parser);
        assert(must_succeed, "");
    }
    return false;
}

Format_Item format_item_make_empty(Format_Parser* parser, Format_Item_Type type, int token_length)
{
    Format_Item item;
    item.type = type;
    item.token_start_index = parser->index;
    item.token_length = token_length;
    return item;
}

Format_Item format_parser_parse_single_format_item(Format_Parser* parser)
{
    auto& index = parser->index;
    auto& tokens = parser->line->tokens;
    assert(index < tokens.size, "");
    auto& token = tokens[index];

    Format_Item item;
    switch (token.type)
    {
    case Syntax_Token_Type::IDENTIFIER: {
        auto& keywords = parser->editor->keyword_table;
        Format_Keyword* found_keyword = hashtable_find_element(&keywords, token.options.identifier);
        if (found_keyword != 0) {
            item = format_item_make_empty(parser, Format_Item_Type::KEYWORD, 1);
            item.options.keyword = *found_keyword;
        }
        else {
            item = format_item_make_empty(parser, Format_Item_Type::IDENTIFIER, 1);
        }
        break;
    }
    case Syntax_Token_Type::NUMBER: {
        item = format_item_make_empty(parser, Format_Item_Type::LITERAL, 1);
        break;
    }
    case Syntax_Token_Type::DELIMITER: {
        auto operator_result = format_parser_test_operator(parser);
        if (operator_result.available) {
            item = format_item_make_empty(parser, Format_Item_Type::OPERATOR, operator_result.value.string.size);
            break;
        }

        // Check Parenthesis
        auto parenthesis_result = syntax_token_is_parenthesis(token);
        if (parenthesis_result.available) {
            item = format_item_make_empty(parser, Format_Item_Type::PARENTHESIS, 1);
            item.options.parenthesis.matching_exists = false;
            item.options.parenthesis.matching_index = -1;
            break;
        }

        item = format_item_make_empty(parser, Format_Item_Type::ERROR_ITEM, 1);
        break;
    }
    default: panic("");
    }

    return item;
}

void format_parser_parse_everything(Format_Parser* parser)
{
    auto& tokens = parser->line->tokens;
    auto& index = parser->index;
    while (index < tokens.size)
    {
        Format_Item item = format_parser_parse_single_format_item(parser);
        switch (item.type)
        {
        case Format_Item_Type::ERROR_ITEM:
        case Format_Item_Type::IDENTIFIER:
        case Format_Item_Type::KEYWORD:
        case Format_Item_Type::LITERAL:
        case Format_Item_Type::OPERATOR:
        case Format_Item_Type::PARENTHESIS:
        default: panic(""); // Can never be of type Gap
        }

    }
}


bool format_parser_parse_any(Format_Parser* parser)
{
    /*
        Parse any parses a undeterminedly long binop chain
        1. Try parsing a operand
        2. If failed, try parsing a

        Expected: Operand BinOp Operand BinOp Operand
        Depending on what is given, we either insert a gap for the operand or for the
    */
    auto& index = parser->index;
    auto& tokens = parser->line->tokens;
    bool operand_required = true;
    bool gap_added_last_round = false;
    while (index < parser->max_parse_index)
    {
        bool found_required_item = false; // Either operand or operator
        if (operand_required) {
            found_required_item = format_parser_parse_operand(parser);
        }
        else {
            found_required_item = format_parser_parse_binop(parser);
        }

        if (!found_required_item)
        {
            if (gap_added_last_round) {
                // Finding 2 gapes means that neither Binop nor Operand can be parsed
                format_parser_add_empty_item(parser, Format_Item_Type::ERROR_ITEM, 1);
                gap_added_last_round = false;
                operand_required = false; // Next after error should be another binop
                continue;
            }
            else {
                format_parser_add_empty_item(parser, Format_Item_Type::GAP, 0);
            }
        }

        gap_added_last_round = found_required_item;
        operand_required = !operand_required;
    }

    assert(index == parser->max_parse_index, "");
    return true;
}

Optional<Format_Parenthesis> syntax_token_is_parenthesis(Syntax_Token token)
{
    if (token.type != Syntax_Token_Type::DELIMITER)
        return optional_make_failure<Format_Parenthesis>();
    Format_Parenthesis_Type type;
    bool is_open;
    switch (token.options.delimiter)
    {
    case '[': is_open = true; type = Format_Parenthesis_Type::BRACKETS; break;
    case ']': is_open = false; type = Format_Parenthesis_Type::BRACKETS; break;
    case '{': is_open = true; type = Format_Parenthesis_Type::BRACES; break;
    case '}': is_open = false; type = Format_Parenthesis_Type::BRACES; break;
    case '(': is_open = true; type = Format_Parenthesis_Type::PARENTHESIS; break;
    case ')': is_open = false; type = Format_Parenthesis_Type::PARENTHESIS; break;
    default:
        return optional_make_failure<Format_Parenthesis>();
    }

    Format_Parenthesis result;
    result.is_open = is_open;
    result.type = type;
    return optional_make_success(result);
}

bool format_parser_parse_parenthesis(Format_Parser* parser)
{
    /*
        Parenthesis are either
            (, [, {
        Inside parenthesis, anything is allowed again
    */
    auto& index = parser->index;
    auto& tokens = parser->line->tokens;

    auto is_parenthesis = syntax_token_is_parenthesis(tokens[index]);
    if (!is_parenthesis.available) return false;
    auto start_parenthesis = is_parenthesis.value;
    if (!start_parenthesis.is_open) return false;

    // Find closing parenthesis
    int closing_index = -1;
    for (int i = index + 1; i < parser->max_parse_index; i++) {
        auto is_parenthesis = syntax_token_is_parenthesis(tokens[i]);
        if (is_parenthesis.available) {
            auto closed_parenthesis = is_parenthesis.value;
            if (!closed_parenthesis.is_open && closed_parenthesis.type == start_parenthesis.type) {
                closing_index = i;
                break;
            }
        }
    }

    if (closing_index == -1) {
        auto item = format_parser_add_empty_item(parser, Format_Item_Type::PARENTHESIS, 1);
        item->options.parenthesis.type = start_parenthesis;
        item->options.parenthesis.matching_exists = false;
        item->options.parenthesis.matching_index = false;

        bool must_succeed = format_parser_parse_any(parser);
        assert(must_succeed, "");
    }
    else {
        format_parser_add_empty_item(parser, Format_Item_Type::PARENTHESIS, 1);
        int parenthesis_item_index = parser->line->format_items.size - 1;
        int backup_max_index = parser->max_parse_index;
        parser->max_parse_index = closing_index;
        SCOPE_EXIT(parser->max_parse_index = backup_max_index);

        bool must_succeed = format_parser_parse_any(parser);
        assert(must_succeed && parser->index == backup_max_index, "");

        auto closing_item = format_parser_add_empty_item(parser, Format_Item_Type::PARENTHESIS, 1);
        closing_item->options.parenthesis.type = start_parenthesis;
        closing_item->options.parenthesis.type.is_open = false;
        closing_item->options.parenthesis.matching_exists = true;
        closing_item->options.parenthesis.matching_index = parenthesis_item_index;

        auto open_item = &parser->line->format_items[parenthesis_item_index];
        open_item->options.parenthesis.type = start_parenthesis;
        open_item->options.parenthesis.matching_exists = true;
        open_item->options.parenthesis.matching_index = parser->line->format_items.size - 1;
    }
    return true;
}

bool format_parser_test_delimiter(Format_Parser* parser, char delimiter)
{
    if (parser->index >= parser->max_parse_index) return false;
    const auto& token = parser->line->tokens[parser->index];
    return token.type == Syntax_Token_Type::DELIMITER && token.options.delimiter == delimiter;
}

bool format_parser_parse_operand(Format_Parser* parser)
{
    /*
        A operand is built upon:
            PreOp 'Simple Operand' PostOp
        PreOp may be:
            []
            Unary Op !, *, -, &
        PostOp may be:
            Function Call ()
            Array Access []
       + (32)[15]{200} * 2
    */
    auto& index = parser->index;
    auto& tokens = parser->line->tokens;
    // Pre-Ops
    bool operand_required = false;
    while (index < parser->max_parse_index)
    {
        if (format_parser_parse_unop(parser)) {
            operand_required = true;
            continue;
        }

        if (format_parser_test_delimiter(parser, '[')) {
            bool must_be_true = format_parser_parse_parenthesis(parser);
            assert(must_be_true, "");
            if (index < parser->max_parse_index) {
                operand_required = true;
            }
            continue;
        }

        break;
    }

    if (!format_parser_parse_simple_operand(parser)) {
        if (operand_required) {
            format_parser_add_empty_item(parser, Format_Item_Type::GAP, 0);
            return true;
        }
        return false;
    }

    // Post-Ops
    while (index < parser->max_parse_index)
    {
        if (format_parser_test_delimiter(parser, '[') || format_parser_test_delimiter(parser, '(')) {
            bool must_be_true = format_parser_parse_parenthesis(parser);
            assert(must_be_true, "");
            continue;
        }
        break;
    }
    return true;
}

void format_parser_parse_line(Syntax_Line* line, Syntax_Editor* editor)
{
    Format_Parser parser;
    parser.line = line;
    parser.index = 0;
    parser.editor = editor;
    parser.max_parse_index = line->tokens.size;
    dynamic_array_reset(&line->format_items);
    format_parser_parse_any(&parser);
}

void format_item_append_to_string(String* str, Format_Item item)
{
    switch (item.type)
    {
    case Format_Item_Type::BINARY_OPERAND:
    case Format_Item_Type::ERROR_ITEM:
    case Format_Item_Type::GAP:
    case Format_Item_Type::IDENTIFIER:
    case Format_Item_Type::KEYWORD:
    case Format_Item_Type::LITERAL:
    case Format_Item_Type::PARENTHESIS:
    case Format_Item_Type::UNARY_OPERAND:
    default: panic("");
    }
}




// Editing
enum class Input_Command_Type
{
    IDENTIFIER_LETTER,
    NUMBER_LETTER,
    DELIMITER_LETTER,
    SPACE,
    ENTER,
    ENTER_REMOVE_ONE_INDENT,
    EXIT_INSERT_MODE,
    BACKSPACE,
    ADD_INDENTATION,
    REMOVE_INDENTATION,
};

struct Input_Command
{
    Input_Command_Type type;
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
};

void syntax_editor_sanitize_cursor(Syntax_Editor* editor)
{
    if (editor->line_index > editor->lines.size) {
        editor->line_index = editor->lines.size - 1;
    }
    if (editor->line_index < 0) {
        editor->line_index = 0;
    }
    auto& tokens = editor->lines[editor->line_index].tokens;
    if (tokens.size == 0) {
        editor->cursor_index = 0;
        return;
    }
    if (editor->cursor_index >= tokens.size) {
        editor->cursor_index = tokens.size - 1;
    }
    if (editor->cursor_index < 0) {
        editor->cursor_index = 0;
    }
}

void syntax_editor_remove_cursor_token(Syntax_Editor* editor)
{
    auto& tokens = editor->lines[editor->line_index].tokens;
    if (editor->cursor_index >= tokens.size || editor->cursor_index < 0) return;
    Syntax_Token t = tokens[editor->cursor_index];
    switch (t.type)
    {
    case Syntax_Token_Type::IDENTIFIER:
        string_destroy(&t.options.identifier);
        break;
    case Syntax_Token_Type::NUMBER:
        string_destroy(&t.options.number);
        break;
    }
    dynamic_array_remove_ordered(&tokens, editor->cursor_index);
}

void normal_mode_handle_command(Syntax_Editor* editor, Normal_Command command)
{
    auto& current_line = editor->lines[editor->line_index];
    auto& tokens = current_line.tokens;
    auto& cursor = editor->cursor_index;
    switch (command)
    {
    case Normal_Command::DELETE_TOKEN: {
        syntax_editor_remove_cursor_token(editor);
        break;
    }
    case Normal_Command::CHANGE_TOKEN: {
        if (tokens.size == 0) return;
        syntax_editor_remove_cursor_token(editor);
        editor->mode = Editor_Mode::INPUT;
        editor->insert_mode = Insert_Mode::BEFORE;
        break;
    }
    case Normal_Command::INSERT_AFTER: {
        editor->mode = Editor_Mode::INPUT;
        editor->insert_mode = Insert_Mode::APPEND;
        break;
    }
    case Normal_Command::INSERT_AT_LINE_END: {
        cursor = tokens.size;
        editor->mode = Editor_Mode::INPUT;
        editor->insert_mode = Insert_Mode::BEFORE;
        break;
    }
    case Normal_Command::INSERT_BEFORE: {
        editor->mode = Editor_Mode::INPUT;
        editor->insert_mode = Insert_Mode::BEFORE;
        break;
    }
    case Normal_Command::INSERT_AT_LINE_START: {
        cursor = 0;
        editor->mode = Editor_Mode::INPUT;
        editor->insert_mode = Insert_Mode::BEFORE;
        break;
    }
    case Normal_Command::MOVE_LINE_START: {
        editor->cursor_index = 0;
        break;
    }
    case Normal_Command::MOVE_LINE_END: {
        editor->cursor_index = tokens.size;
        break;
    }
    case Normal_Command::ADD_LINE_ABOVE: {
        syntax_editor_insert_line(editor, editor->line_index, current_line.indentation_level);
        editor->cursor_index = 0;
        editor->mode = Editor_Mode::INPUT;
        editor->insert_mode = Insert_Mode::BEFORE;
        break;
    }
    case Normal_Command::ADD_LINE_BELOW: {
        syntax_editor_insert_line(editor, editor->line_index + 1, current_line.indentation_level);
        editor->line_index += 1;
        editor->cursor_index = 0;
        editor->mode = Editor_Mode::INPUT;
        editor->insert_mode = Insert_Mode::BEFORE;
        break;
    }
    case Normal_Command::MOVE_UP: {
        if (editor->line_index == 0) return;
        int current_pos = syntax_editor_find_visual_cursor_pos(editor, editor->cursor_index, editor->line_index, editor->mode);
        int new_line_index = editor->line_index - 1;
        auto& new_line = editor->lines[new_line_index];
        // Find nearest position
        {
            int min_dist = 10000;
            int min_pos = 0;
            for (int i = 0; i <= new_line.tokens.size; i++) {
                int new_pos = syntax_editor_find_visual_cursor_pos(editor, i, new_line_index, editor->mode);
                int dist = math_absolute(new_pos - current_pos);
                if (dist < min_dist) {
                    min_pos = i;
                    min_dist = dist;
                }
            }
            editor->cursor_index = min_pos;
            editor->line_index = new_line_index;
        }
        break;
    }
    case Normal_Command::MOVE_DOWN: {
        if (editor->line_index + 1 >= editor->lines.size) return;
        int current_pos = syntax_editor_find_visual_cursor_pos(editor, editor->cursor_index, editor->line_index, editor->mode);
        int new_line_index = editor->line_index + 1;
        auto& new_line = editor->lines[new_line_index];
        // Find nearest position
        {
            int min_dist = 10000;
            int min_pos = 0;
            for (int i = 0; i <= new_line.tokens.size; i++) {
                int new_pos = syntax_editor_find_visual_cursor_pos(editor, i, new_line_index, editor->mode);
                int dist = math_absolute(new_pos - current_pos);
                if (dist < min_dist) {
                    min_pos = i;
                    min_dist = dist;
                }
            }
            editor->cursor_index = min_pos;
            editor->line_index = new_line_index;
        }
        break;
    }
    case Normal_Command::MOVE_LEFT: {
        editor->cursor_index -= 1;
        syntax_editor_sanitize_cursor(editor);
        break;
    }
    case Normal_Command::MOVE_RIGHT: {
        editor->cursor_index += 1;
        syntax_editor_sanitize_cursor(editor);
        break;
    }
    default: panic("");
    }
}

void insert_mode_handle_command(Syntax_Editor* editor, Input_Command input)
{
    auto& line = editor->lines[editor->line_index];
    auto& tokens = line.tokens;
    auto& cursor = editor->cursor_index;
    assert(editor->mode == Editor_Mode::INPUT, "");
    syntax_editor_sanitize_cursor(editor);

    if (cursor == tokens.size) {
        editor->insert_mode = Insert_Mode::BEFORE;
    }

    // Handle Universal Inputs
    if (input.type == Input_Command_Type::EXIT_INSERT_MODE) {
        if (editor->insert_mode == Insert_Mode::APPEND) {
            cursor += 1;
            syntax_editor_sanitize_cursor(editor);
        }
        editor->mode = Editor_Mode::NORMAL;
        editor->insert_mode = Insert_Mode::APPEND;
        return;
    }
    if (input.type == Input_Command_Type::ENTER) {
        syntax_editor_insert_line(editor, editor->line_index + 1, editor->lines[editor->line_index].indentation_level);
        editor->line_index = editor->line_index + 1;
        editor->cursor_index = 0;
        return;
    }
    if (input.type == Input_Command_Type::ENTER_REMOVE_ONE_INDENT) {
        int indent = math_maximum(0, editor->lines[editor->line_index].indentation_level - 1);
        syntax_editor_insert_line(editor, editor->line_index + 1, indent);
        editor->line_index = editor->line_index + 1;
        editor->cursor_index = 0;
        return;
    }
    if (input.type == Input_Command_Type::SPACE) {
        if (editor->insert_mode == Insert_Mode::APPEND) {
            editor->insert_mode = Insert_Mode::BEFORE;
            editor->cursor_index += 1;
            syntax_editor_sanitize_cursor(editor);
        }
        return;
    }
    if (input.type == Input_Command_Type::ADD_INDENTATION) {
        if (cursor == 0 && editor->insert_mode == Insert_Mode::BEFORE) {
            line.indentation_level += 1;
        }
        return;
    }
    if (input.type == Input_Command_Type::REMOVE_INDENTATION) {
        if (cursor == 0 && editor->insert_mode == Insert_Mode::BEFORE && line.indentation_level > 0) {
            line.indentation_level -= 1;
        }
        return;
    }

    // Handle the case of editing inside a token
    if (editor->insert_mode == Insert_Mode::APPEND)
    {
        auto& token = tokens[cursor];
        bool input_used = false;
        bool remove_token = false;
        switch (token.type)
        {
        case Syntax_Token_Type::DELIMITER: {
            if (input.type == Input_Command_Type::BACKSPACE) {
                remove_token = true;
                input_used = true;
            }
            break;
        }
        case Syntax_Token_Type::IDENTIFIER: {
            auto& id = token.options.identifier;
            input_used = true;
            switch (input.type)
            {
            case Input_Command_Type::BACKSPACE:
                if (id.size == 1) {
                    remove_token = true;
                    break;
                }
                string_truncate(&id, id.size - 1);
                break;
            case Input_Command_Type::IDENTIFIER_LETTER:
                string_append_character(&id, input.letter);
                break;
            case Input_Command_Type::NUMBER_LETTER:
                string_append_character(&id, input.letter);
                break;
            default: input_used = false;
            }
            break;
        }
        case Syntax_Token_Type::NUMBER: {
            auto& text = token.options.number;
            input_used = true;
            switch (input.type)
            {
            case Input_Command_Type::BACKSPACE:
                if (text.size == 1) {
                    remove_token = true;
                    break;
                }
                string_truncate(&text, text.size - 1);
                break;
            case Input_Command_Type::NUMBER_LETTER:
                string_append_character(&text, input.letter);
                break;
            default: input_used = false;
            }
            break;
        }
        }

        if (remove_token) {
            bool goto_before_mode = token.type == Syntax_Token_Type::NUMBER || token.type == Syntax_Token_Type::IDENTIFIER;
            goto_before_mode = false; // Still not sure about that one
            syntax_editor_remove_cursor_token(editor);
            if (goto_before_mode) {
                editor->insert_mode = Insert_Mode::BEFORE;
            }
            else {
                editor->cursor_index -= 1;
                syntax_editor_sanitize_cursor(editor);
            }
            return;
        }
        if (input_used) return;
    }
    else
    {
        if (input.type == Input_Command_Type::BACKSPACE && cursor != 0) {
            cursor -= 1;
            editor->insert_mode = Insert_Mode::APPEND;
            return;
        }
    }

    // Insert new token if necessary (Mode could either be BEFORE or APPEND)
    Syntax_Token new_token;
    bool token_valid = false;
    switch (input.type)
    {
    case Input_Command_Type::IDENTIFIER_LETTER: {
        token_valid = true;
        new_token.type = Syntax_Token_Type::IDENTIFIER;
        new_token.options.identifier = string_create_empty(1);
        string_append_character(&new_token.options.identifier, input.letter);
        break;
    }
    case Input_Command_Type::NUMBER_LETTER: {
        token_valid = true;
        new_token.type = Syntax_Token_Type::NUMBER;
        new_token.options.number = string_create_empty(1);
        string_append_character(&new_token.options.number, input.letter);
        break;
    }
    case Input_Command_Type::DELIMITER_LETTER: {
        token_valid = true;
        new_token.type = Syntax_Token_Type::DELIMITER;
        new_token.options.delimiter = input.letter;
        break;
    }
    default: token_valid = false;
    }

    if (token_valid) {
        if (editor->insert_mode == Insert_Mode::APPEND) {
            cursor += 1;
        }
        dynamic_array_insert_ordered(&tokens, new_token, cursor);
        syntax_editor_sanitize_cursor(editor);
        editor->insert_mode = Insert_Mode::APPEND;
    }
}

void syntax_editor_update(Syntax_Editor* editor, Input* input)
{
    for (int i = 0; i < input->key_messages.size; i++)
    {
        Key_Message msg = input->key_messages[i];
        if (editor->mode == Editor_Mode::INPUT)
        {
            Input_Command input;
            if (msg.character == 32 && msg.key_down) {
                input.type = Input_Command_Type::SPACE;
            }
            else if (msg.key_code == Key_Code::L && msg.key_down && msg.ctrl_down) {
                input.type = Input_Command_Type::EXIT_INSERT_MODE;
            }
            else if (msg.key_code == Key_Code::BACKSPACE && msg.key_down) {
                input.type = Input_Command_Type::BACKSPACE;
            }
            else if (msg.key_code == Key_Code::RETURN && msg.key_down) {
                if (msg.shift_down) {
                    input.type = Input_Command_Type::ENTER_REMOVE_ONE_INDENT;
                }
                else {
                    input.type = Input_Command_Type::ENTER;
                }
            }
            else if (char_is_letter(msg.character) || msg.character == '_') {
                input.type = Input_Command_Type::IDENTIFIER_LETTER;
                input.letter = msg.character;
            }
            else if (char_is_digit(msg.character)) {
                input.type = Input_Command_Type::NUMBER_LETTER;
                input.letter = msg.character;
            }
            else if (msg.key_code == Key_Code::TAB && msg.key_down) {
                if (msg.shift_down) {
                    input.type = Input_Command_Type::REMOVE_INDENTATION;
                }
                else {
                    input.type = Input_Command_Type::ADD_INDENTATION;
                }
            }
            else if (msg.key_down && msg.character != -1) {
                // Check if delimiter
                bool is_delimiter = false;
                for (int i = 0; i < editor->delimiter_characters.size; i++) {
                    if (editor->delimiter_characters[i] == msg.character) {
                        is_delimiter = true;
                        break;
                    }
                }

                if (!is_delimiter) continue;
                input.type = Input_Command_Type::DELIMITER_LETTER;
                input.letter = msg.character;
            }
            else {
                continue;
            }
            insert_mode_handle_command(editor, input);
        }
        else
        {
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
                command = Normal_Command::CHANGE_TOKEN;
            }
            else if (msg.key_code == Key_Code::X && msg.key_down) {
                command = Normal_Command::DELETE_TOKEN;
            }
            else {
                continue;
            }
            normal_mode_handle_command(editor, command);
        }
    }
}

void syntax_editor_insert_line(Syntax_Editor* editor, int index, int indentation_level)
{
    Syntax_Line line;
    line.tokens = dynamic_array_create_empty<Syntax_Token>(1);
    line.format_items = dynamic_array_create_empty<Format_Item>(1);
    line.indentation_level = indentation_level;
    dynamic_array_insert_ordered(&editor->lines, line, index);
}

void syntax_editor_add_delimiter_character(Syntax_Editor* editor, char c)
{
    auto& delimiter_characters = editor->delimiter_characters;
    bool found = false;
    for (int i = 0; i < delimiter_characters.size; i++) {
        if (delimiter_characters[i] == c) {
            found = true;
            break;
        }
    }
    if (!found) {
        dynamic_array_push_back(&delimiter_characters, c);
    }
}

void operator_mapping_set(Syntax_Editor* editor, Format_Operator op, const char* text, bool is_binop, bool is_unop)
{
    assert(editor->operator_mapping.size == (int)Format_Operator::MAX_ENUM_VALUE, "");
    assert(is_binop || is_unop, "");
    editor->operator_mapping[(int)op].op = op;
    editor->operator_mapping[(int)op].string = string_create_static(text);
    editor->operator_mapping[(int)op].is_binop = is_binop;
    editor->operator_mapping[(int)op].is_unop = is_unop;
}

Syntax_Editor* syntax_editor_create(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D)
{
    Syntax_Editor* result = new Syntax_Editor;
    result->cursor_index = 0;
    result->mode = Editor_Mode::INPUT;
    result->insert_mode = Insert_Mode::APPEND;
    result->line_index = 0;
    result->lines = dynamic_array_create_empty<Syntax_Line>(1);
    syntax_editor_insert_line(result, 0, 0);

    result->text_renderer = text_renderer;
    result->rendering_core = rendering_core;
    result->renderer_2D = renderer_2D;

    // Add mapping infos
    {
        auto& keyword_table = result->keyword_table;
        auto& operator_mapping = result->operator_mapping;
        auto& delimiter_characters = result->delimiter_characters;

        delimiter_characters = dynamic_array_create_empty<char>(32);
        keyword_table = hashtable_create_empty<String, Format_Keyword>(8, hash_string, string_equals);
        operator_mapping = array_create_empty<Operator_Mapping>((int)Format_Operator::MAX_ENUM_VALUE);
        memory_set_bytes(operator_mapping.data, sizeof(Operator_Mapping) * operator_mapping.size, 0);

        hashtable_insert_element(&keyword_table, string_create_static("break"), Format_Keyword::BREAK);
        hashtable_insert_element(&keyword_table, string_create_static("case"), Format_Keyword::CASE);
        hashtable_insert_element(&keyword_table, string_create_static("cast"), Format_Keyword::CAST);
        hashtable_insert_element(&keyword_table, string_create_static("continue"), Format_Keyword::CONTINUE);
        hashtable_insert_element(&keyword_table, string_create_static("c_union"), Format_Keyword::C_UNION);
        hashtable_insert_element(&keyword_table, string_create_static("default"), Format_Keyword::DEFAULT);
        hashtable_insert_element(&keyword_table, string_create_static("defer"), Format_Keyword::DEFER);
        hashtable_insert_element(&keyword_table, string_create_static("delete"), Format_Keyword::DELETE_KEYWORD);
        hashtable_insert_element(&keyword_table, string_create_static("else"), Format_Keyword::ELSE);
        hashtable_insert_element(&keyword_table, string_create_static("if"), Format_Keyword::IF);
        hashtable_insert_element(&keyword_table, string_create_static("module"), Format_Keyword::MODULE);
        hashtable_insert_element(&keyword_table, string_create_static("new"), Format_Keyword::NEW);
        hashtable_insert_element(&keyword_table, string_create_static("return"), Format_Keyword::RETURN);
        hashtable_insert_element(&keyword_table, string_create_static("struct"), Format_Keyword::STRUCT);
        hashtable_insert_element(&keyword_table, string_create_static("switch"), Format_Keyword::SWITCH);
        hashtable_insert_element(&keyword_table, string_create_static("union"), Format_Keyword::UNION);
        hashtable_insert_element(&keyword_table, string_create_static("while"), Format_Keyword::WHILE);

        operator_mapping_set(result, Format_Operator::ADDITION, "+", true, false);
        operator_mapping_set(result, Format_Operator::SUBTRACTION, "-", true, true);
        operator_mapping_set(result, Format_Operator::DIVISON, "/", true, false);
        operator_mapping_set(result, Format_Operator::MULTIPLY, "*", true, true);
        operator_mapping_set(result, Format_Operator::MODULO, "%", true, false);
        operator_mapping_set(result, Format_Operator::COMMA, ",", true, false);
        operator_mapping_set(result, Format_Operator::DOT, ".", true, false);
        operator_mapping_set(result, Format_Operator::TILDE, "~", true, false);
        operator_mapping_set(result, Format_Operator::COLON, ":", true, false);
        operator_mapping_set(result, Format_Operator::ASSIGN, "=", true, false);
        operator_mapping_set(result, Format_Operator::NOT, "!", true, true);
        operator_mapping_set(result, Format_Operator::AMPERSAND, "&", false, true);
        operator_mapping_set(result, Format_Operator::LESS_THAN, "<", true, false);
        operator_mapping_set(result, Format_Operator::GREATER_THAN, ">", true, false);
        operator_mapping_set(result, Format_Operator::LESS_EQUAL, "<=", true, false);
        operator_mapping_set(result, Format_Operator::GREATER_EQUAL, ">=", true, false);
        operator_mapping_set(result, Format_Operator::EQUALS, "==", true, false);
        operator_mapping_set(result, Format_Operator::NOT_EQUALS, "!=", true, false);
        operator_mapping_set(result, Format_Operator::POINTER_EQUALS, "*==", true, false);
        operator_mapping_set(result, Format_Operator::POINTER_NOT_EQUALS, "*!=", true, false);
        operator_mapping_set(result, Format_Operator::DEFINE_COMPTIME, "::", true, false);
        operator_mapping_set(result, Format_Operator::DEFINE_INFER, ":=", true, false);
        operator_mapping_set(result, Format_Operator::AND, "&&", true, false);
        operator_mapping_set(result, Format_Operator::OR, "||", true, false);
        operator_mapping_set(result, Format_Operator::ARROW, "->", true, false);

        syntax_editor_add_delimiter_character(result, '(');
        syntax_editor_add_delimiter_character(result, ')');
        syntax_editor_add_delimiter_character(result, '{');
        syntax_editor_add_delimiter_character(result, '}');
        syntax_editor_add_delimiter_character(result, '[');
        syntax_editor_add_delimiter_character(result, ']');
        syntax_editor_add_delimiter_character(result, '|');
        for (int i = 0; i < operator_mapping.size; i++) {
            auto& str = operator_mapping[i].string;
            for (int j = 0; j < str.size; j++) {
                syntax_editor_add_delimiter_character(result, str[j]);
            }
        }
    }

    return result;
}

void syntax_editor_destroy(Syntax_Editor* editor)
{
    for (int i = 0; i < editor->lines.size; i++)
    {
        auto& line = editor->lines[i];
        dynamic_array_destroy(&line.tokens);
        dynamic_array_destroy(&line.format_items);
    }
    dynamic_array_destroy(&editor->lines);
    array_destroy(&editor->operator_mapping);
    hashtable_destroy(&editor->keyword_table);
    delete editor;
}



// Rendering
void syntax_editor_draw_underline(Syntax_Editor* editor, int line, int character, int length, vec3 color)
{
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
    vec2 size = char_size * vec2((float)length, 1 / 8.0f);
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * char_size.x, -(line + 1) * char_size.y);
    cursor = cursor + size * vec2(0.5f, 0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_cursor_line(Syntax_Editor* editor, vec3 color, int line, int character)
{
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
    vec2 cursor = edge_point + vec2(character * char_size.x, -(line + 1) * char_size.y);
    cursor = cursor + size * vec2(0.5f, 0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_character_box(Syntax_Editor* editor, vec3 color, int line, int character)
{
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
    vec2 cursor = edge_point + vec2(character * size.x, -line * size.y);
    cursor = cursor + size * vec2(0.5f, -0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_string(Syntax_Editor* editor, String string, vec3 color, int line, int character)
{
    vec2 pos = vec2(-1.0f, 1.0f) + vec2(character, -(line + 1)) * editor->character_size;
    text_renderer_set_color(editor->text_renderer, color);
    text_renderer_add_text(editor->text_renderer, &string, pos, editor->character_size.y, 1.0f);
}

int syntax_editor_find_visual_cursor_pos(Syntax_Editor* editor, int line_pos, int line_index, Editor_Mode mode)
{
    auto& line = editor->lines[line_index];
    auto& cursor = line_pos;
    auto& tokens = line.tokens;

    /*
    if (tokens.size == 0) return line.indentation_level * 4;
    if (cursor == tokens.size) {
        auto& last = render_items[render_items.size - 1];
        return last.pos + last.size + 1;
    }

    Render_Item* item = token_index_to_render_item(&line, cursor);
    if (mode == Editor_Mode::NORMAL) {
        return item->pos;
    }
    if (editor->insert_mode == Insert_Mode::BEFORE) {
        return item->pos - 1;
    }
    else {
        return item->pos + item->size;
    }
    panic("hey");
    */
    return 0;
}

void syntax_editor_render_line(Syntax_Editor* editor, int line_index)
{
    auto& line = editor->lines[line_index];
    auto& tokens = line.tokens;

    //auto& render_items = line.render_items;
    int pos = 0;
    for (int i = 0; i < tokens.size; i++)
    {
        auto& token = tokens[i];
        switch (token.type)
        {
        case Syntax_Token_Type::DELIMITER: {
            char str[2] = { token.options.delimiter, '\0' };
            syntax_editor_draw_string(editor, string_create_static(str), Syntax_Color::TEXT, line_index, pos);
            pos += 2;
            break;
        }
        case Syntax_Token_Type::IDENTIFIER:
            syntax_editor_draw_string(editor, token.options.identifier, Syntax_Color::IDENTIFIER_FALLBACK, line_index, pos);
            pos += token.options.identifier.size + 1;
            break;
        case Syntax_Token_Type::NUMBER:
            syntax_editor_draw_string(editor, token.options.number, Syntax_Color::LITERAL, line_index, pos);
            pos += token.options.number.size + 1;
            break;
        default: panic("");
        }
    }

    // Render Error messages
    /*
    for (int i = 0; i < line.error_messages.size; i++) {
        auto& msg = line.error_messages[i];
        Render_Item* item = token_index_to_render_item(&line, msg.token_index);
        syntax_editor_draw_underline(editor, line_index, item->pos, item->size, vec3(0.8f, 0.0f, 0.0f));
    }
    */

    // Render Cursor
    if (line_index == editor->line_index) {
        int cursor_pos = syntax_editor_find_visual_cursor_pos(editor, editor->cursor_index, editor->line_index, editor->mode);
        if (editor->mode == Editor_Mode::NORMAL) {
            syntax_editor_draw_character_box(editor, Syntax_Color::COMMENT, line_index, cursor_pos);
        }
        else {
            syntax_editor_draw_cursor_line(editor, Syntax_Color::COMMENT, line_index, cursor_pos);
        }
    }
}

void syntax_editor_render(Syntax_Editor* editor)
{
    auto& cursor = editor->cursor_index;

    // Prepare Render
    editor->character_size.y = text_renderer_cm_to_relative_height(editor->text_renderer, editor->rendering_core, 0.8f);
    editor->character_size.x = text_renderer_get_cursor_advance(editor->text_renderer, editor->character_size.y);

    // Render lines
    for (int i = 0; i < editor->lines.size; i++) {
        format_parser_parse_line(&editor->lines[i], editor);
        syntax_editor_render_line(editor, i);
    }

    // Render Primitives
    renderer_2D_render(editor->renderer_2D, editor->rendering_core);
    text_renderer_render(editor->text_renderer, editor->rendering_core);
}