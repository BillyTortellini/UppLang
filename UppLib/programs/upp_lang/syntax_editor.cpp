#include "syntax_editor.hpp"

#include "../../rendering/text_renderer.hpp"
#include "../../datastructures/string.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/renderer_2d.hpp"

#include "../../win32/input.hpp"
#include "syntax_colors.hpp"

/*
    PARSING
*/
AST_Item* line_parse_binop_chain(Syntax_Line* line, int max_index_exclusive);
AST_Item* line_parse_definition(Syntax_Line* line, int max_index_exclusive);
Render_Item* token_index_to_render_item(Syntax_Line* line, int index);
AST_Item* line_parse_operand_suffix(Syntax_Line* line, AST_Item* operand, int max_index_exclusive);
void syntax_editor_insert_line(Syntax_Editor* editor, int index, int indentation_index);
int syntax_editor_find_visual_cursor_pos(Syntax_Editor* editor, int line_pos, int line_index, Editor_Mode mode);

void syntax_editor_add_operator_mapping(Syntax_Editor* editor, Operator_Type type, const char* str)
{
    Operator_Mapping o;
    o.string = string_create_static(str);
    o.type = type;
    dynamic_array_push_back(&editor->operator_table, o);
}

Range range_make_length(int start, int length) {
    Range result;
    result.start = start;
    result.end = start + length;
    return result;
}

Range range_make_same(int start) {
    Range result;
    result.start = start;
    result.end = start;
    return result;
}

Range range_make(int start, int end) {
    Range result;
    result.start = start;
    result.end = end;
    return result;
}

AST_Item* line_allocate_item(Syntax_Line* line, AST_Type type, Range range)
{
    AST_Item* item = new AST_Item;
    dynamic_array_push_back(&line->allocated_items, item);
    item->type = type;
    item->range = range;
    return item;
}

bool line_test_keyword_by_index(Syntax_Line* line, int index, Keyword_Type type)
{
    assert(index >= 0, "");
    auto& tokens = line->parse_tokens;
    if (index >= tokens.size) return false;
    auto& token = tokens[index];
    return token.type == Parse_Token_Type::KEYWORD && token.options.keyword == type;
}

bool line_test_token_by_index(Syntax_Line* line, int index, Parse_Token_Type type)
{
    assert(index >= 0, "");
    auto& tokens = line->parse_tokens;
    if (index >= tokens.size) return false;
    return tokens[index].type == type;
}

bool line_test_operator_by_index(Syntax_Line* line, int index, Operator_Type type)
{
    assert(index >= 0, "");
    auto& tokens = line->parse_tokens;
    if (index >= tokens.size) return false;
    return tokens[index].type == Parse_Token_Type::OPERATOR && tokens[index].options.operation == type;
}

bool line_test_token(Syntax_Line* line, Parse_Token_Type type) {
    return line_test_token_by_index(line, line->parse_index, type);
}

bool line_test_operator(Syntax_Line* line, Operator_Type type) {
    return line_test_operator_by_index(line, line->parse_index, type);
}

bool line_test_keyword(Syntax_Line* line, Keyword_Type type) {
    return line_test_keyword_by_index(line, line->parse_index, type);
}

bool line_test_binop_by_index(Syntax_Line* line, int index)
{
    if (!line_test_token_by_index(line, index, Parse_Token_Type::OPERATOR)) return false;
    // Filter out non binop operations
    switch (line->parse_tokens[index].options.operation)
    {
    case Operator_Type::DOT:
    case Operator_Type::COLON:
    case Operator_Type::OPEN_PARENTHESIS:
    case Operator_Type::CLOSED_PARENTHESIS:
    case Operator_Type::OPEN_BRACKET:
    case Operator_Type::CLOSED_BRACKET:
    case Operator_Type::OPEN_BRACES:
    case Operator_Type::CLOSED_BRACES:
        return false;
    }
    return true;
}

void line_log_error(Syntax_Line* line, const char* msg, int token_index)
{
    Error_Message result;
    result.text = string_create_static(msg);
    result.token_index = token_index;
    dynamic_array_push_back(&line->error_messages, result);
}

int line_find_closing_parenthesis(Syntax_Line* line, int token_index)
{
    assert(line_test_operator_by_index(line, token_index, Operator_Type::OPEN_PARENTHESIS), "");
    auto& tokens = line->parse_tokens;
    int depth = 1;
    int index = line->parse_index + 1;
    while (index < tokens.size) {
        if (line_test_operator_by_index(line, index, Operator_Type::OPEN_PARENTHESIS)) {
            depth += 1;
        }
        else if (line_test_operator_by_index(line, index, Operator_Type::CLOSED_PARENTHESIS)) {
            depth -= 1;
            if (depth == 0) {
                return index;
            }
        }
        index += 1;
    }
    return tokens.size;
}

int line_find_next_delimiter_not_in_parenthesis(Syntax_Line* line, int max_index_exclusive, Operator_Type delimiter)
{
    int index = line->parse_index;
    auto& tokens = line->parse_tokens;
    int depth = 0;
    while (index < max_index_exclusive)
    {
        if (line_test_operator_by_index(line, index, Operator_Type::OPEN_PARENTHESIS)) {
            depth += 1;
        }
        else if (line_test_operator_by_index(line, index, Operator_Type::CLOSED_PARENTHESIS)) {
            depth -= 1;
            if (depth < 0) {
                return tokens.size;
            }
        }
        else if (line_test_operator_by_index(line, index, delimiter)) {
            if (depth == 0) {
                return index;
            }
        }
        index += 1;
    }
    return max_index_exclusive;
}

bool line_end_reached(Syntax_Line* line) {
    return line->parse_index >= line->parse_tokens.size;
}

AST_Item* line_parse_simple_operand(Syntax_Line* line, int max_index_exclusive)
{
    auto& tokens = line->parse_tokens;
    if (line_test_token(line, Parse_Token_Type::IDENTIFIER) || line_test_token(line, Parse_Token_Type::NUMBER)) {
        line->parse_index += 1;
        return line_allocate_item(line, AST_Type::OPERAND, range_make_length(line->parse_index - 1, 1));
    }
    else if (line_test_operator(line, Operator_Type::OPEN_PARENTHESIS))
    {
        AST_Item* result_item = line_allocate_item(line, AST_Type::PARENTHESIS,
            range_make(line->parse_index, line_find_closing_parenthesis(line, line->parse_index))
        );
        auto& parenthesis = result_item->options.parenthesis;
        int closing_index = result_item->range.end;
        bool has_closure = closing_index != tokens.size;
        parenthesis.has_closing_parenthesis = has_closure;
        if (has_closure) {
            assert(closing_index < max_index_exclusive, "Dont think this can happen!");
        }
        else {
            line_log_error(line, "Missing Closing parenthesis", line->parse_index);
        }

        parenthesis.comma_indices = dynamic_array_create_empty<int>(1);
        parenthesis.items = dynamic_array_create_empty<AST_Item*>(1);

        line->parse_index += 1;
        if (line_test_operator(line, Operator_Type::CLOSED_PARENTHESIS)) {
            line->parse_index += 1;
            return result_item;
        }

        int comma_index = line_find_next_delimiter_not_in_parenthesis(line, closing_index, Operator_Type::COMMA);
        while (comma_index != closing_index)
        {
            dynamic_array_push_back(&parenthesis.comma_indices, comma_index);
            dynamic_array_push_back(&parenthesis.items, line_parse_definition(line, comma_index));
            line->parse_index = comma_index + 1;
            comma_index = line_find_next_delimiter_not_in_parenthesis(line, closing_index, Operator_Type::COMMA);
        }
        dynamic_array_push_back(&parenthesis.items, line_parse_definition(line, closing_index));
        line->parse_index = closing_index + 1;
        return result_item;
    }
    else if (line_test_operator(line, Operator_Type::DOT)) {
        AST_Item* error_item = line_allocate_item(line, AST_Type::ERROR_NODE, range_make_same(line->parse_index));
        return line_parse_operand_suffix(line, error_item, max_index_exclusive);
    }
    else if (line_test_token(line, Parse_Token_Type::KEYWORD))
    {
        if (line_test_keyword(line, Keyword_Type::BREAK)) {
            AST_Item* result = line_allocate_item(line, AST_Type::STATEMENT_BREAK, range_make(line->parse_index, max_index_exclusive));
            line->parse_index += 1;
            result->options.statement_break = line_parse_binop_chain(line, max_index_exclusive);
            result->range.end = line->parse_index;
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::CONTINUE)) {
            AST_Item* result = line_allocate_item(line, AST_Type::STATEMENT_CONTINUE, range_make(line->parse_index, max_index_exclusive));
            line->parse_index += 1;
            result->options.statement_continue = line_parse_binop_chain(line, max_index_exclusive);
            result->range.end = line->parse_index;
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::DELETE_KEYWORD)) {
            AST_Item* result = line_allocate_item(line, AST_Type::STATEMENT_DELETE, range_make(line->parse_index, max_index_exclusive));
            line->parse_index += 1;
            result->options.statement_delete = line_parse_binop_chain(line, max_index_exclusive);
            result->range.end = line->parse_index;
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::IF)) {
            AST_Item* result = line_allocate_item(line, AST_Type::STATEMENT_IF, range_make(line->parse_index, max_index_exclusive));
            line->parse_index += 1;
            result->options.statement_if = line_parse_binop_chain(line, max_index_exclusive);
            result->range.end = line->parse_index;
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::SWITCH)) {
            AST_Item* result = line_allocate_item(line, AST_Type::STATEMENT_SWITCH, range_make(line->parse_index, max_index_exclusive));
            line->parse_index += 1;
            result->options.statement_switch = line_parse_binop_chain(line, max_index_exclusive);
            result->range.end = line->parse_index;
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::WHILE)) {
            AST_Item* result = line_allocate_item(line, AST_Type::STATEMENT_WHILE, range_make(line->parse_index, max_index_exclusive));
            line->parse_index += 1;
            result->options.statement_while = line_parse_binop_chain(line, max_index_exclusive);
            result->range.end = line->parse_index;
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::CASE)) {
            AST_Item* result = line_allocate_item(line, AST_Type::STATEMENT_CASE, range_make(line->parse_index, max_index_exclusive));
            line->parse_index += 1;
            result->options.statement_case = line_parse_binop_chain(line, max_index_exclusive);
            result->range.end = line->parse_index;
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::RETURN)) {
            AST_Item* result = line_allocate_item(line, AST_Type::STATEMENT_RETURN, range_make(line->parse_index, max_index_exclusive));
            line->parse_index += 1;
            result->options.statement_return.has_value = false;
            result->options.statement_return.expression = 0;
            if (line->parse_index < max_index_exclusive) {
                result->options.statement_return.has_value = true;
                result->options.statement_return.expression = line_parse_binop_chain(line, max_index_exclusive);
            }
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::ELSE)) {
            AST_Item* result = line_allocate_item(line, AST_Type::STATEMENT_ELSE, range_make_length(line->parse_index, 1));
            line->parse_index += 1;
            if (line->parse_index < max_index_exclusive && line_test_keyword(line, Keyword_Type::IF)) {
                result->type = AST_Type::STATEMENT_ELSE_IF;
                line->parse_index += 1;
                result->options.statement_else_if = line_parse_binop_chain(line, max_index_exclusive);
                result->range.end = line->parse_index;
            }
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::DEFER)) {
            AST_Item* result = line_allocate_item(line, AST_Type::STATEMENT_DEFER, range_make_length(line->parse_index, 1));
            line->parse_index += 1;
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::MODULE)) {
            AST_Item* result = line_allocate_item(line, AST_Type::MODULE, range_make_length(line->parse_index, 1));
            line->parse_index += 1;
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::STRUCT)) {
            AST_Item* result = line_allocate_item(line, AST_Type::STRUCT, range_make_length(line->parse_index, 1));
            line->parse_index += 1;
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::UNION)) {
            AST_Item* result = line_allocate_item(line, AST_Type::UNION, range_make_length(line->parse_index, 1));
            line->parse_index += 1;
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::C_UNION)) {
            AST_Item* result = line_allocate_item(line, AST_Type::C_UNION, range_make_length(line->parse_index, 1));
            line->parse_index += 1;
            return result;
        }
        else if (line_test_keyword(line, Keyword_Type::ENUM)) {
            AST_Item* result = line_allocate_item(line, AST_Type::ENUM, range_make_length(line->parse_index, 1));
            line->parse_index += 1;
            return result;
        }
    }

    return 0;
}

AST_Item* line_parse_operand_suffix(Syntax_Line* line, AST_Item* operand, int max_index_exclusive)
{
    auto& tokens = line->parse_tokens;
    if (line->parse_index >= max_index_exclusive) return 0;
    if (line_test_operator(line, Operator_Type::OPEN_PARENTHESIS))
    {
        AST_Item* item = line_allocate_item(line, AST_Type::FUNCTION_CALL, range_make_length(operand->range.start, 1));
        auto& call = item->options.function_call;
        call.function_item = operand;
        call.parenthesis_item = line_parse_simple_operand(line, max_index_exclusive);
        item->range.end = line->parse_index;
        return item;
    }
    else if (line_test_operator(line, Operator_Type::DOT))
    {
        AST_Item* access_item = line_allocate_item(line, AST_Type::MEMBER_ACCESS, range_make_length(operand->range.start, 1));
        auto& member = access_item->options.member_access;
        member.item = operand;
        member.dot_token_index = line->parse_index;
        line->parse_index += 1;
        if (line_test_token(line, Parse_Token_Type::IDENTIFIER)) {
            member.has_identifier = true;
            member.identifier_token_index = line->parse_index;
            line->parse_index += 1;
            access_item->range.end = line->parse_index;
        }
        else {
            member.has_identifier = false;
            member.identifier_token_index = -1;
        }
        return access_item;
    }
    return 0;
}

AST_Item* line_parse_operand(Syntax_Line* line, int max_index_exclusive)
{
    auto& tokens = line->parse_tokens;

    // Parse prefixes
    if (line_test_operator(line, Operator_Type::SUBTRACTION) || line_test_operator(line, Operator_Type::MULTIPLY) ||
        line_test_operator(line, Operator_Type::AMPERSAND) || line_test_operator(line, Operator_Type::NOT)) 
    {
        AST_Item* prefix_item = line_allocate_item(line, AST_Type::UNARY_OPERATION, range_make_length(line->parse_index, 1));
        prefix_item->options.unary_op.operator_token_index = line->parse_index;
        line->parse_index += 1;
        AST_Item* operand = line_parse_operand(line, max_index_exclusive);
        if (operand == 0) {
            operand = line_allocate_item(line, AST_Type::ERROR_NODE, range_make_same(line->parse_index));
        }
        prefix_item->options.unary_op.item = operand;
        return prefix_item;
    }

    AST_Item* operand_item = line_parse_simple_operand(line, max_index_exclusive);
    AST_Item* suffix_item = operand_item;
    while (suffix_item != 0) {
        operand_item = suffix_item;
        suffix_item = line_parse_operand_suffix(line, operand_item, max_index_exclusive);
    }
    return operand_item;
}

AST_Item* line_parse_binop_chain(Syntax_Line* line, int max_index_exclusive)
{
    if (line->parse_index >= max_index_exclusive) {
        return line_allocate_item(line, AST_Type::ERROR_NODE, range_make_same(line->parse_index));
    }

    AST_Item* first_operand = line_parse_operand(line, max_index_exclusive);
    if (first_operand == 0)
    {
        if (line_test_binop_by_index(line, line->parse_index)) {
            // Add gap
            first_operand = line_allocate_item(line, AST_Type::ERROR_NODE, range_make_same(line->parse_index));
        }
        else {
            line_log_error(line, "Operand Invalid", line->parse_index);
            first_operand = line_allocate_item(line, AST_Type::ERROR_NODE, range_make_length(line->parse_index, 1));
            line->parse_index += 1;
        }
    }
    if (line->parse_index >= max_index_exclusive) {
        return first_operand;
    }

    AST_Item* binop = line_allocate_item(line, AST_Type::BINOP_CHAIN, range_make_same(first_operand->range.start));
    binop->options.binop_chain.start_item = first_operand;
    binop->options.binop_chain.items = dynamic_array_create_empty<Binop_Link>(1);
    while (line->parse_index < max_index_exclusive)
    {
        Binop_Link link;
        link.token_index = line->parse_index;
        link.token_is_missing = !line_test_binop_by_index(line, line->parse_index);
        if (!link.token_is_missing) {
            line->parse_index += 1;
        }

        AST_Item* operand;
        if (line->parse_index >= max_index_exclusive) {
            operand = line_allocate_item(line, AST_Type::ERROR_NODE, range_make_same(line->parse_index));
        }
        else
        {
            operand = line_parse_operand(line, max_index_exclusive);
            if (operand == 0)
            {
                if (line_test_binop_by_index(line, line->parse_index)) {
                    // Two binops in succession, add gap between
                    operand = line_allocate_item(line, AST_Type::ERROR_NODE, range_make_same(line->parse_index));
                }
                else {
                    line_log_error(line, "Operand Invalid", line->parse_index);
                    operand = line_allocate_item(line, AST_Type::ERROR_NODE, range_make_length(line->parse_index, 1));
                    line->parse_index += 1;
                }
            }
        }

        link.operand = operand;
        dynamic_array_push_back(&binop->options.binop_chain.items, link);
    }
    return binop;
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

void line_rendering_insert_space(Syntax_Line* line) {
    line->render_item_offset += 1;
}

void line_rendering_add_token(Syntax_Line* line, int index, Syntax_Editor* editor)
{
    auto& render_items = line->render_items;
    auto& syntax_tokens = line->syntax_tokens;
    auto& parse_tokens = line->parse_tokens;
    assert(index < parse_tokens.size, "");
    auto& parse_token = parse_tokens[index];

    if (&editor->lines[editor->line_index] == line &&
        editor->mode == Editor_Mode::INPUT &&
        editor->insert_mode == Insert_Mode::BEFORE &&
        editor->cursor_index == parse_token.start_index) {
        line->render_item_offset += 1;
    }

    for (int i = parse_token.start_index; i < parse_token.start_index + parse_token.length; i++)
    {
        auto& syntax_token = syntax_tokens[i];

        Render_Item item;
        item.is_token = true;
        item.pos = line->render_item_offset;
        item.token_index = i;
        item.is_keyword = parse_token.type == Parse_Token_Type::KEYWORD;
        switch (syntax_token.type)
        {
        case Syntax_Token_Type::DELIMITER:
            item.size = 1;
            break;
        case Syntax_Token_Type::IDENTIFIER:
            item.size = syntax_token.options.identifier.size;
            break;
        case Syntax_Token_Type::NUMBER:
            item.size = syntax_token.options.number.size;
            break;
        default: panic("");
        }
        dynamic_array_push_back(&render_items, item);
        line->render_item_offset += item.size;
    }
}

void line_rendering_add_tokens(Syntax_Line* line, Range range, Syntax_Editor* editor, bool seperate_with_space)
{
    for (int i = range.start; i < range.end; i++) {
        line_rendering_add_token(line, i, editor);
        if (i != range.end - 1 && seperate_with_space) {
            line_rendering_insert_space(line);
        }
    }
}

void line_rendering_add_gap(Syntax_Line* line)
{
    Render_Item item;
    item.is_token = false;
    item.pos = line->render_item_offset;
    item.token_index = -1;
    item.size = 1;
    dynamic_array_push_back(&line->render_items, item);
    line->render_item_offset += 1;
}

void line_rendering_ast_item_to_render_item(Syntax_Line* line, AST_Item* item, Syntax_Editor* editor)
{
    switch (item->type)
    {
    case AST_Type::DEFINITION:
    {
        auto& definition = item->options.statement_definition;
        line_rendering_ast_item_to_render_item(line, definition.assign_to_expr, editor);
        switch (definition.definition_type)
        {
        case Definition_Type::DEFINE_ASSIGN: // id: expr = expr
            line_rendering_add_token(line, definition.colon_token_index, editor);
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, definition.type_expression, editor);
            line_rendering_insert_space(line);
            line_rendering_add_token(line, definition.assign_token_index, editor);
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, definition.value_expression, editor);
            break;
        case Definition_Type::COMPTIME: // id :: expr
            line_rendering_insert_space(line);
            line_rendering_add_token(line, definition.colon_token_index, editor);
            line_rendering_add_token(line, definition.colon_token_index + 1, editor);
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, definition.value_expression, editor);
            break;
        case Definition_Type::INFER: // id := expr
            line_rendering_insert_space(line);
            line_rendering_add_token(line, definition.colon_token_index, editor);
            line_rendering_add_token(line, definition.colon_token_index + 1, editor);
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, definition.value_expression, editor);
            break;
        case Definition_Type::NORMAL: // id: expr
            line_rendering_add_token(line, definition.colon_token_index, editor);
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, definition.type_expression, editor);
            break;
        case Definition_Type::ASSIGNMENT:
            line_rendering_insert_space(line);
            line_rendering_add_token(line, definition.assign_token_index, editor);
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, definition.value_expression, editor);
            break;
        default: panic("");
        }
        break;
    }
    case AST_Type::STATEMENT_DELETE: {
        line_rendering_add_token(line, item->range.start, editor);
        line_rendering_insert_space(line);
        line_rendering_ast_item_to_render_item(line, item->options.statement_delete, editor);
        break;
    }
    case AST_Type::STATEMENT_CONTINUE: {
        line_rendering_add_token(line, item->range.start, editor);
        line_rendering_insert_space(line);
        line_rendering_ast_item_to_render_item(line, item->options.statement_continue, editor);
        break;
    }
    case AST_Type::STATEMENT_BREAK: {
        line_rendering_add_token(line, item->range.start, editor);
        line_rendering_insert_space(line);
        line_rendering_ast_item_to_render_item(line, item->options.statement_break, editor);
        break;
    }
    case AST_Type::STATEMENT_IF: {
        line_rendering_add_token(line, item->range.start, editor);
        line_rendering_insert_space(line);
        line_rendering_ast_item_to_render_item(line, item->options.statement_if, editor);
        break;
    }
    case AST_Type::STATEMENT_SWITCH: {
        line_rendering_add_token(line, item->range.start, editor);
        line_rendering_insert_space(line);
        line_rendering_ast_item_to_render_item(line, item->options.statement_switch, editor);
        break;
    }
    case AST_Type::STATEMENT_WHILE: {
        line_rendering_add_token(line, item->range.start, editor);
        line_rendering_insert_space(line);
        line_rendering_ast_item_to_render_item(line, item->options.statement_while, editor);
        break;
    }
    case AST_Type::STATEMENT_CASE: {
        line_rendering_add_token(line, item->range.start, editor);
        line_rendering_insert_space(line);
        line_rendering_ast_item_to_render_item(line, item->options.statement_case, editor);
        break;
    }
    case AST_Type::STATEMENT_ELSE: {
        line_rendering_add_token(line, item->range.start, editor);
        break;
    }
    case AST_Type::STATEMENT_ELSE_IF: {
        line_rendering_add_token(line, item->range.start, editor);
        line_rendering_insert_space(line);
        line_rendering_add_token(line, item->range.start + 1, editor);
        line_rendering_insert_space(line);
        line_rendering_ast_item_to_render_item(line, item->options.statement_else_if, editor);
        break;
    }
    case AST_Type::STATEMENT_DEFER: {
        line_rendering_add_token(line, item->range.start, editor);
        break;
    }
    case AST_Type::STATEMENT_RETURN:
    {
        line_rendering_add_token(line, item->range.start, editor);
        if (item->options.statement_return.has_value) {
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, item->options.statement_return.expression, editor);
        }
        break;
    }
    case AST_Type::BINOP_CHAIN: {
        auto& binop = item->options.binop_chain;
        line_rendering_ast_item_to_render_item(line, binop.start_item, editor);
        for (int i = 0; i < item->options.binop_chain.items.size; i++) {
            Binop_Link link = item->options.binop_chain.items[i];
            if (!link.token_is_missing && line->parse_tokens[link.token_index].options.operation != Operator_Type::COMMA) {
                line_rendering_insert_space(line);
            }
            if (link.token_is_missing) {
                line_rendering_add_gap(line);
            }
            else {
                line_rendering_add_token(line, link.token_index, editor);
            }
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, link.operand, editor);
        }
        break;
    }
    case AST_Type::UNARY_OPERATION:
    {
        auto& unary = item->options.unary_op;
        line_rendering_add_token(line, unary.operator_token_index, editor); // Add .
        line_rendering_ast_item_to_render_item(line, unary.item, editor);
        break;
    }
    case AST_Type::MEMBER_ACCESS:
    {
        auto& access = item->options.member_access;
        line_rendering_ast_item_to_render_item(line, access.item, editor);
        line_rendering_add_token(line, access.dot_token_index, editor); // Add .
        if (access.has_identifier) {
            line_rendering_add_token(line, access.identifier_token_index, editor); // Add .
        }
        else {
            line_rendering_add_gap(line);
        }
        break;
    }
    case AST_Type::FUNCTION_CALL:
    {
        auto& call = item->options.function_call;
        line_rendering_ast_item_to_render_item(line, call.function_item, editor);
        line_rendering_ast_item_to_render_item(line, call.parenthesis_item, editor);
        break;
    }
    case AST_Type::ERROR_NODE: {
        if (item->range.start == item->range.end) {
            line_rendering_add_gap(line);
        }
        else {
            line_rendering_add_tokens(line, item->range, editor, true);
        }
        break;
    }
    case AST_Type::OPERAND: {
        line_rendering_add_token(line, item->range.start, editor);
        break;
    }
    case AST_Type::MODULE:
    case AST_Type::STRUCT:
    case AST_Type::UNION:
    case AST_Type::C_UNION:
    case AST_Type::ENUM: {
        line_rendering_add_token(line, item->range.start, editor);
        break;
    }
    case AST_Type::PARENTHESIS: {
        auto& parenthesis = item->options.parenthesis;
        line_rendering_add_token(line, item->range.start, editor); // Add (
        for (int i = 0; i < parenthesis.items.size; i++) {
            line_rendering_ast_item_to_render_item(line, parenthesis.items[i], editor);
            if (i != parenthesis.items.size - 1) {
                int comma_index = parenthesis.comma_indices[i];
                assert(line_test_operator_by_index(line, comma_index, Operator_Type::COMMA), "");
                line_rendering_add_token(line, comma_index, editor); // Add ,
                line_rendering_insert_space(line);
            }
        }
        if (parenthesis.has_closing_parenthesis) {
            line_rendering_add_token(line, item->range.end, editor);
        }
        break;
    }
    default: panic("");
    }
}

void line_reset(Syntax_Line* line)
{
    auto& syntax_tokens = line->syntax_tokens;

    // Reset
    for (int i = 0; i < line->allocated_items.size; i++) {
        AST_Item* item = line->allocated_items[i];
        switch (item->type)
        {
        case AST_Type::BINOP_CHAIN:
            dynamic_array_destroy(&item->options.binop_chain.items);
            break;
        case AST_Type::PARENTHESIS:
            dynamic_array_destroy(&item->options.parenthesis.items);
            dynamic_array_destroy(&item->options.parenthesis.comma_indices);
            break;
        }
        delete item;
    }
    dynamic_array_reset(&line->allocated_items);
    dynamic_array_reset(&line->error_messages);
    dynamic_array_reset(&line->render_items);
    dynamic_array_reset(&line->parse_tokens);
    line->root = 0;
    line->parse_index = 0;
    line->render_item_offset = 0;
    line->root = 0;
    line->requires_block = false;
}

AST_Item* line_parse_definition(Syntax_Line* line, int max_index_exclusive)
{
    auto& syntax_tokens = line->syntax_tokens;
    if (syntax_tokens.size == 0) return 0;
    if (line->parse_index >= max_index_exclusive) {
        return line_allocate_item(line, AST_Type::ERROR_NODE, range_make_same(line->parse_index));
    }

    // Definition
    int colon_token_index = line_find_next_delimiter_not_in_parenthesis(line, max_index_exclusive, Operator_Type::COLON);
    int assign_token_index = line_find_next_delimiter_not_in_parenthesis(line, max_index_exclusive, Operator_Type::ASSIGN);
    int left_end_index = math_minimum(colon_token_index, assign_token_index);
    if (left_end_index < max_index_exclusive)
    {
        AST_Item* result = line_allocate_item(line, AST_Type::DEFINITION, range_make(line->parse_index, max_index_exclusive));
        auto& definition = result->options.statement_definition;
        definition.assign_token_index = assign_token_index;
        definition.colon_token_index = colon_token_index;
        definition.assign_to_expr = line_parse_binop_chain(line, left_end_index);
        definition.type_expression = 0;
        definition.value_expression = 0;
        line->parse_index = left_end_index + 1;
        if (colon_token_index < assign_token_index)
        {
            if (line_test_operator(line, Operator_Type::COLON)) {
                definition.definition_type = Definition_Type::COMPTIME;
                definition.type_expression = 0;
                line->parse_index += 1;
                definition.value_expression = line_parse_binop_chain(line, max_index_exclusive);
            }
            else if (line_test_operator(line, Operator_Type::ASSIGN)) {
                definition.definition_type = Definition_Type::INFER;
                definition.type_expression = 0;
                line->parse_index += 1;
                definition.value_expression = line_parse_binop_chain(line, max_index_exclusive);
            }
            else if (assign_token_index < max_index_exclusive) {
                definition.definition_type = Definition_Type::DEFINE_ASSIGN;
                definition.type_expression = line_parse_binop_chain(line, assign_token_index);
                line->parse_index = assign_token_index + 1;
                definition.value_expression = line_parse_binop_chain(line, max_index_exclusive);
            }
            else {
                definition.definition_type = Definition_Type::NORMAL;
                definition.value_expression = 0;
                definition.type_expression = line_parse_binop_chain(line, max_index_exclusive);
            }
        }
        else { // Assignment before colon
            definition.definition_type = Definition_Type::ASSIGNMENT;
            definition.value_expression = line_parse_binop_chain(line, max_index_exclusive);
        }
        line->parse_index = max_index_exclusive;
        return result;
    }

    // Expression
    return line_parse_binop_chain(line, max_index_exclusive);
}

void line_parse_and_format(Syntax_Line* line, Syntax_Editor* editor)
{
    line_reset(line);
    auto& syntax_tokens = line->syntax_tokens;
    if (syntax_tokens.size == 0) return;

    // Lex tokens
    {
        int index = 0;
        while (index < syntax_tokens.size)
        {
            auto& syntax_token = syntax_tokens[index];
            Parse_Token parse_token;
            parse_token.start_index = index;
            parse_token.length = 0;
            switch (syntax_token.type)
            {
            case Syntax_Token_Type::IDENTIFIER: {
                parse_token.length = 1;
                Keyword_Type* result = hashtable_find_element(&editor->keyword_table, syntax_token.options.identifier);
                if (result != 0) {
                    parse_token.type = Parse_Token_Type::KEYWORD;
                    parse_token.options.keyword = *result;
                }
                else {
                    parse_token.type = Parse_Token_Type::IDENTIFIER;
                }
                break;
            }
            case Syntax_Token_Type::NUMBER: {
                parse_token.length = 1;
                parse_token.type = Parse_Token_Type::NUMBER;
                break;
            }
            case Syntax_Token_Type::DELIMITER:
            {
                int longest_index = -1;
                int longest_size = 0;
                for (int i = 0; i < editor->operator_table.size; i++)
                {
                    auto& mapping = editor->operator_table[i];
                    auto& op_str = mapping.string;
                    bool matches = true;
                    for (int j = 0; j < op_str.size; j++)
                    {
                        if (index + j >= syntax_tokens.size) {
                            matches = false;
                            break;
                        }
                        auto& test_token = syntax_tokens[index + j];
                        if (test_token.type != Syntax_Token_Type::DELIMITER) {
                            matches = false;
                            break;
                        }
                        if (test_token.options.delimiter != op_str[j]) {
                            matches = false;
                            break;
                        }
                    }
                    if (matches && op_str.size > longest_size) {
                        longest_index = i;
                        longest_size = op_str.size;
                    }
                }
                if (longest_index == -1) {
                    parse_token.type = Parse_Token_Type::INVALID;
                    parse_token.length = 1;
                }
                else {
                    parse_token.type = Parse_Token_Type::OPERATOR;
                    parse_token.options.operation = editor->operator_table[longest_index].type;
                    parse_token.length = editor->operator_table[longest_index].string.size;
                }
                break;
            }
            default: panic("");
            }

            assert(parse_token.length != 0, "");
            index += parse_token.length;
            dynamic_array_push_back(&line->parse_tokens, parse_token);
        }
    }

    // Parse and Render
    line->root = line_parse_definition(line, line->parse_tokens.size);
    line_rendering_ast_item_to_render_item(line, line->root, editor);

    // Add indentation to all render items
    for (int i = 0; i < line->render_items.size; i++) {
        auto& item = line->render_items[i];
        item.pos += line->indentation_level * 4;
    }

    // Sanity Checks
    // Check that all tokens are displayed
    bool print_render = false;
    for (int i = 0; i < syntax_tokens.size; i++) {
        if (token_index_to_render_item(line, i) == 0) {
            logg("Error, Could not find token #%d\n", i);
            print_render = true;
        }
    }

    if (print_render)
    {
        String str = string_create_empty(256);
        SCOPE_EXIT(string_destroy(&str));
        string_append_formated(&str, "Tokens: ");
        for (int i = 0; i < syntax_tokens.size; i++) {
            syntax_token_append_to_string(syntax_tokens[i], &str);
            string_append_formated(&str, " ");
        }
        string_append_formated(&str, "\nRender_Items: ");
        for (int i = 0; i < line->render_items.size; i++) {
            auto& render_item = line->render_items[i];
            if (render_item.is_token) {
                syntax_token_append_to_string(syntax_tokens[render_item.token_index], &str);
            }
            string_append_formated(&str, " ");
        }
        logg("%s\n\n", str.characters);
        panic("Hey");
    }
}

Syntax_Block* syntax_editor_parse_block(Syntax_Editor* editor, Syntax_Block* parent, int indentation)
{
    Syntax_Block* block = new Syntax_Block;
    block->items = dynamic_array_create_empty<Block_Item>(1);
    block->parent = parent;
    block->line_start_index = editor->block_parse_index;
    block->indentation = indentation;

    while (editor->block_parse_index < editor->lines.size)
    {
        auto& line = editor->lines[editor->block_parse_index];
        if (line.indentation_level < indentation) {
            break;
        }
        else if (line.indentation_level == indentation)
        {
            auto& line = editor->lines[editor->block_parse_index];
            line_parse_and_format(&line, editor);

            Block_Item item;
            item.is_line = true;
            item.line_index = editor->block_parse_index;
            dynamic_array_push_back(&block->items, item);
            editor->block_parse_index += 1;
        }
        else {
            Block_Item item;
            item.is_line = false;
            item.block = syntax_editor_parse_block(editor, block, indentation + 1);
            dynamic_array_push_back(&block->items, item);
        }
    }

    block->line_end_index_exclusive = editor->block_parse_index;
    return block;
}

void syntax_block_destroy(Syntax_Block* block)
{
    for (int i = 0; i < block->items.size; i++) {
        auto& item = block->items[i];
        if (!item.is_line) {
            syntax_block_destroy(item.block);
        }
    }
    dynamic_array_destroy(&block->items);
    delete block;
}



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
    auto& syntax_tokens = editor->lines[editor->line_index].syntax_tokens;
    if (syntax_tokens.size == 0) {
        editor->cursor_index = 0;
        return;
    }
    if (editor->cursor_index > syntax_tokens.size) {
        editor->cursor_index = syntax_tokens.size;
    }
    if (editor->cursor_index < 0) {
        editor->cursor_index = 0;
    }
}

void syntax_editor_remove_cursor_token(Syntax_Editor* editor)
{
    auto& syntax_tokens = editor->lines[editor->line_index].syntax_tokens;
    if (editor->cursor_index >= syntax_tokens.size || editor->cursor_index < 0) return;
    Syntax_Token t = syntax_tokens[editor->cursor_index];
    switch (t.type)
    {
    case Syntax_Token_Type::IDENTIFIER:
        string_destroy(&t.options.identifier);
        break;
    case Syntax_Token_Type::NUMBER:
        string_destroy(&t.options.number);
        break;
    }
    dynamic_array_remove_ordered(&syntax_tokens, editor->cursor_index);
}

void normal_mode_handle_command(Syntax_Editor* editor, Normal_Command command)
{
    auto& current_line = editor->lines[editor->line_index];
    auto& syntax_tokens = current_line.syntax_tokens;
    auto& cursor = editor->cursor_index;
    switch (command)
    {
    case Normal_Command::DELETE_TOKEN: {
        if (cursor == syntax_tokens.size) return;
        syntax_editor_remove_cursor_token(editor);
        break;
    }
    case Normal_Command::CHANGE_TOKEN: {
        if (syntax_tokens.size == 0) return;
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
        cursor = syntax_tokens.size;
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
        editor->cursor_index = syntax_tokens.size;
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
            for (int i = 0; i <= new_line.syntax_tokens.size; i++) {
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
            for (int i = 0; i <= new_line.syntax_tokens.size; i++) {
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
    auto& syntax_tokens = line.syntax_tokens;
    auto& cursor = editor->cursor_index;
    assert(editor->mode == Editor_Mode::INPUT, "");
    syntax_editor_sanitize_cursor(editor);

    if (cursor == syntax_tokens.size) {
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
        auto& token = syntax_tokens[cursor];
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
        dynamic_array_insert_ordered(&syntax_tokens, new_token, cursor);
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
                bool is_delimiter_letter = false;
                for (int i = 0; i < editor->operator_table.size && !is_delimiter_letter; i++) {
                    auto& mapping = editor->operator_table[i];
                    for (int j = 0; j < mapping.string.size; j++) {
                        if (mapping.string[j] == msg.character) {
                            is_delimiter_letter = true;
                            break;
                        }
                    }
                }

                if (!is_delimiter_letter) continue;
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

    if (editor->root_block != 0) {
        syntax_block_destroy(editor->root_block);
        editor->root_block = 0;
    }
    editor->block_parse_index = 0;
    editor->root_block = syntax_editor_parse_block(editor, 0, 0);
}

void syntax_editor_insert_line(Syntax_Editor* editor, int index, int indentation_level)
{
    Syntax_Line line;
    line.error_messages = dynamic_array_create_empty<Error_Message>(1);
    line.render_items = dynamic_array_create_empty<Render_Item>(1);
    line.syntax_tokens = dynamic_array_create_empty<Syntax_Token>(1);
    line.parse_tokens = dynamic_array_create_empty<Parse_Token>(1);
    line.allocated_items = dynamic_array_create_empty<AST_Item*>(1);
    line.indentation_level = indentation_level;
    dynamic_array_insert_ordered(&editor->lines, line, index);
}

Syntax_Editor* syntax_editor_create(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D)
{
    Syntax_Editor* result = new Syntax_Editor;
    result->cursor_index = 0;
    result->mode = Editor_Mode::INPUT;
    result->insert_mode = Insert_Mode::APPEND;
    result->line_index = 0;
    result->root_block = 0;
    result->lines = dynamic_array_create_empty<Syntax_Line>(1);
    syntax_editor_insert_line(result, 0, 0);

    result->keyword_table = hashtable_create_empty<String, Keyword_Type>(8, hash_string, string_equals);
    hashtable_insert_element(&result->keyword_table, string_create_static("break"), Keyword_Type::BREAK);
    hashtable_insert_element(&result->keyword_table, string_create_static("case"), Keyword_Type::CASE);
    hashtable_insert_element(&result->keyword_table, string_create_static("cast"), Keyword_Type::CAST);
    hashtable_insert_element(&result->keyword_table, string_create_static("continue"), Keyword_Type::CONTINUE);
    hashtable_insert_element(&result->keyword_table, string_create_static("c_union"), Keyword_Type::C_UNION);
    hashtable_insert_element(&result->keyword_table, string_create_static("default"), Keyword_Type::DEFAULT);
    hashtable_insert_element(&result->keyword_table, string_create_static("defer"), Keyword_Type::DEFER);
    hashtable_insert_element(&result->keyword_table, string_create_static("delete"), Keyword_Type::DELETE_KEYWORD);
    hashtable_insert_element(&result->keyword_table, string_create_static("else"), Keyword_Type::ELSE);
    hashtable_insert_element(&result->keyword_table, string_create_static("if"), Keyword_Type::IF);
    hashtable_insert_element(&result->keyword_table, string_create_static("module"), Keyword_Type::MODULE);
    hashtable_insert_element(&result->keyword_table, string_create_static("new"), Keyword_Type::NEW);
    hashtable_insert_element(&result->keyword_table, string_create_static("return"), Keyword_Type::RETURN);
    hashtable_insert_element(&result->keyword_table, string_create_static("struct"), Keyword_Type::STRUCT);
    hashtable_insert_element(&result->keyword_table, string_create_static("switch"), Keyword_Type::SWITCH);
    hashtable_insert_element(&result->keyword_table, string_create_static("union"), Keyword_Type::UNION);
    hashtable_insert_element(&result->keyword_table, string_create_static("while"), Keyword_Type::WHILE);

    result->operator_table = dynamic_array_create_empty<Operator_Mapping>(1);
    syntax_editor_add_operator_mapping(result, Operator_Type::ADDITION, "+");
    syntax_editor_add_operator_mapping(result, Operator_Type::SUBTRACTION, "-");
    syntax_editor_add_operator_mapping(result, Operator_Type::DIVISON, "/");
    syntax_editor_add_operator_mapping(result, Operator_Type::MULTIPLY, "*");
    syntax_editor_add_operator_mapping(result, Operator_Type::MODULO, "%");
    syntax_editor_add_operator_mapping(result, Operator_Type::COMMA, ",");
    syntax_editor_add_operator_mapping(result, Operator_Type::DOT, ".");
    syntax_editor_add_operator_mapping(result, Operator_Type::COLON, ":");
    syntax_editor_add_operator_mapping(result, Operator_Type::ASSIGN, "=");
    syntax_editor_add_operator_mapping(result, Operator_Type::NOT, "!");
    syntax_editor_add_operator_mapping(result, Operator_Type::AMPERSAND, "&");
    syntax_editor_add_operator_mapping(result, Operator_Type::VERT_LINE, "|");
    syntax_editor_add_operator_mapping(result, Operator_Type::OPEN_PARENTHESIS, "(");
    syntax_editor_add_operator_mapping(result, Operator_Type::CLOSED_PARENTHESIS, ")");
    syntax_editor_add_operator_mapping(result, Operator_Type::OPEN_BRACKET, "[");
    syntax_editor_add_operator_mapping(result, Operator_Type::CLOSED_BRACKET, "]");
    syntax_editor_add_operator_mapping(result, Operator_Type::OPEN_BRACES, "{");
    syntax_editor_add_operator_mapping(result, Operator_Type::CLOSED_BRACES, "}");
    syntax_editor_add_operator_mapping(result, Operator_Type::LESS_THAN, "<");
    syntax_editor_add_operator_mapping(result, Operator_Type::GREATER_THAN, ">");
    syntax_editor_add_operator_mapping(result, Operator_Type::LESS_EQUAL, "<=");
    syntax_editor_add_operator_mapping(result, Operator_Type::GREATER_EQUAL, ">=");
    syntax_editor_add_operator_mapping(result, Operator_Type::EQUALS, "==");
    syntax_editor_add_operator_mapping(result, Operator_Type::NOT_EQUALS, "!=");
    syntax_editor_add_operator_mapping(result, Operator_Type::POINTER_EQUALS, "*==");
    syntax_editor_add_operator_mapping(result, Operator_Type::POINTER_NOT_EQUALS, "*!=");
    syntax_editor_add_operator_mapping(result, Operator_Type::AND, "&&");
    syntax_editor_add_operator_mapping(result, Operator_Type::OR, "||");
    syntax_editor_add_operator_mapping(result, Operator_Type::ARROW, "->");

    result->text_renderer = text_renderer;
    result->rendering_core = rendering_core;
    result->renderer_2D = renderer_2D;
    return result;
}

void syntax_editor_destroy(Syntax_Editor* editor)
{
    for (int i = 0; i < editor->lines.size; i++)
    {
        auto& line = editor->lines[i];
        line_reset(&line);
        dynamic_array_destroy(&line.syntax_tokens);
        dynamic_array_destroy(&line.error_messages);
        dynamic_array_destroy(&line.render_items);
        dynamic_array_destroy(&line.allocated_items);
        dynamic_array_destroy(&line.parse_tokens);
    }
    if (editor->root_block != 0) {
        syntax_block_destroy(editor->root_block);
        editor->root_block = 0;
    }
    dynamic_array_destroy(&editor->lines);
    dynamic_array_destroy(&editor->operator_table);
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

Render_Item* token_index_to_render_item(Syntax_Line* line, int index)
{
    auto& items = line->render_items;
    for (int i = 0; i < items.size; i++) {
        auto& item = items[i];;
        if (item.is_token && item.token_index == index) {
            return &item;
        }
    }
    return 0;
}

int syntax_editor_find_visual_cursor_pos(Syntax_Editor* editor, int line_pos, int line_index, Editor_Mode mode)
{
    auto& line = editor->lines[line_index];
    auto& cursor = line_pos;
    auto& syntax_tokens = line.syntax_tokens;
    auto& render_items = line.render_items;

    if (syntax_tokens.size == 0) return line.indentation_level * 4;
    if (cursor == syntax_tokens.size) {
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
    return 0;
}

void syntax_editor_render_line(Syntax_Editor* editor, int line_index)
{
    auto& line = editor->lines[line_index];
    auto& syntax_tokens = line.syntax_tokens;
    auto& render_items = line.render_items;
    for (int i = 0; i < render_items.size; i++)
    {
        auto& item = render_items[i];
        if (item.is_token)
        {
            Syntax_Token* token = &syntax_tokens[item.token_index];
            switch (token->type)
            {
            case Syntax_Token_Type::DELIMITER: {
                char str[2] = {token->options.delimiter, '\0'};
                syntax_editor_draw_string(editor, string_create_static(str), Syntax_Color::TEXT, line_index, item.pos);
                break;
            }
            case Syntax_Token_Type::IDENTIFIER:
                syntax_editor_draw_string(editor, token->options.identifier,
                    item.is_keyword ? Syntax_Color::KEYWORD : Syntax_Color::IDENTIFIER_FALLBACK, line_index, item.pos);
                break;
            case Syntax_Token_Type::NUMBER:
                syntax_editor_draw_string(editor, token->options.number, Syntax_Color::LITERAL, line_index, item.pos);
                break;
            default: panic("");
            }
        }
        else {
            syntax_editor_draw_underline(editor, line_index, item.pos, item.size, vec3(0.8f, 0.8f, 0.0f));
        }
    }

    // Render Error messages
    for (int i = 0; i < line.error_messages.size; i++) {
        auto& msg = line.error_messages[i];
        Render_Item* item = token_index_to_render_item(&line, msg.token_index);
        syntax_editor_draw_underline(editor, line_index, item->pos, item->size, vec3(0.8f, 0.0f, 0.0f));
    }

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

void syntax_editor_render_block(Syntax_Editor* editor, Syntax_Block* block)
{
    for (int i = 0; i < block->items.size; i++)
    {
        auto& block_item = block->items[i];
        if (block_item.is_line) {
            syntax_editor_render_line(editor, block_item.line_index);
        }
        else {
            syntax_editor_render_block(editor, block_item.block);
        }
    }
}

void syntax_editor_render(Syntax_Editor* editor)
{
    auto& cursor = editor->cursor_index;

    // Prepare Render
    editor->character_size.y = text_renderer_cm_to_relative_height(editor->text_renderer, editor->rendering_core, 0.8f);
    editor->character_size.x = text_renderer_get_cursor_advance(editor->text_renderer, editor->character_size.y);

    // Render AST-Tree
    syntax_editor_render_block(editor, editor->root_block);

    // Render Primitives
    renderer_2D_render(editor->renderer_2D, editor->rendering_core);
    text_renderer_render(editor->text_renderer, editor->rendering_core);
}