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
AST_Item* line_parse_binop_chain(Syntax_Line* line, int end_index);
Render_Item* token_index_to_render_item(Syntax_Line* line, int index);
AST_Item* line_parse_operand_suffix(Syntax_Line* line, AST_Item* operand);
String delimiter_type_to_string(Delimiter_Type type);
void syntax_editor_insert_line(Syntax_Editor* editor, int index);
int syntax_editor_find_visual_cursor_pos(Syntax_Editor* editor, int line_pos, int line_index, Editor_Mode mode);

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
    auto& tokens = line->tokens;
    if (index >= tokens.size) return false;
    auto& token = tokens[index];
    return token.type == Syntax_Token_Type::IDENTIFIER && token.options.identifier.is_keyword && token.options.identifier.keyword_type == type;
}

bool line_test_keyword(Syntax_Line* line, Keyword_Type type) {
    return line_test_keyword_by_index(line, line->parse_index, type);
}

bool line_test_token_by_index(Syntax_Line* line, int index, Syntax_Token_Type type)
{
    assert(index >= 0, "");
    auto& tokens = line->tokens;
    if (index >= tokens.size) return false;
    bool match = tokens[index].type == type;
    if (match && type == Syntax_Token_Type::IDENTIFIER) {
        return !tokens[index].options.identifier.is_keyword; // Only match identifiers with keywords
    }
    return match;
}

bool line_test_delimiter_by_index(Syntax_Line* line, int index, Delimiter_Type type)
{
    assert(index >= 0, "");
    auto& tokens = line->tokens;
    if (index >= tokens.size) return false;
    return tokens[index].type == Syntax_Token_Type::DELIMITER && tokens[index].options.delimiter == type;
}

bool line_test_token(Syntax_Line* line, Syntax_Token_Type type) {
    return line_test_token_by_index(line, line->parse_index, type);
}

bool line_test_delimiter(Syntax_Line* line, Delimiter_Type type) {
    return line_test_delimiter_by_index(line, line->parse_index, type);
}

bool line_test_binop_by_index(Syntax_Line* line, int index)
{
    return line_test_delimiter_by_index(line, index,  Delimiter_Type::MINUS) ||
        line_test_delimiter_by_index(line, index,  Delimiter_Type::PLUS) ||
        line_test_delimiter_by_index(line, index,  Delimiter_Type::SLASH) ||
        line_test_delimiter_by_index(line, index,  Delimiter_Type::STAR);
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
    assert(line_test_delimiter_by_index(line, token_index, Delimiter_Type::OPEN_PARENTHESIS), "");
    auto& tokens = line->tokens;
    int depth = 1;
    int index = line->parse_index + 1;
    while (index < tokens.size) {
        if (line_test_delimiter_by_index(line, index, Delimiter_Type::OPEN_PARENTHESIS)) {
            depth += 1;
        }
        else if (line_test_delimiter_by_index(line, index, Delimiter_Type::CLOSED_PARENTHESIS)) {
            depth -= 1;
            if (depth == 0) {
                return index;
            }
        }
        index += 1;
    }
    return tokens.size;
}

int line_find_next_delimiter_not_in_parenthesis(Syntax_Line* line, int max_index_exclusive, Delimiter_Type delimiter)
{
    int index = line->parse_index;
    auto& tokens = line->tokens;
    int depth = 0;
    while (index < max_index_exclusive)
    {
        if (line_test_delimiter_by_index(line, index, Delimiter_Type::OPEN_PARENTHESIS)) {
            depth += 1;
        }
        else if (line_test_delimiter_by_index(line, index, Delimiter_Type::CLOSED_PARENTHESIS)) {
            depth -= 1;
            if (depth < 0) {
                return tokens.size;
            }
        }
        else if (line_test_delimiter_by_index(line, index, delimiter)) {
            if (depth == 0) {
                return index;
            }
        }
        index += 1;
    }
    return max_index_exclusive;
}

bool line_end_reached(Syntax_Line* line) {
    return line->parse_index >= line->tokens.size;
}

AST_Item* line_parse_simple_operand(Syntax_Line* line)
{
    auto& tokens = line->tokens;
    if (line_test_token(line, Syntax_Token_Type::IDENTIFIER) || line_test_token(line, Syntax_Token_Type::NUMBER)) {
        line->parse_index += 1;
        return line_allocate_item(line, AST_Type::OPERAND, range_make_length(line->parse_index - 1, 1));
    }
    else if (line_test_delimiter(line, Delimiter_Type::OPEN_PARENTHESIS))
    {
        // Search for closing Parenthesis
        AST_Item* result = line_allocate_item(line, AST_Type::PARENTHESIS, range_make(0, 0));
        int closing_index = line_find_closing_parenthesis(line, line->parse_index);
        result->options.parenthesis.has_closing_parenthesis = closing_index != tokens.size;
        if (result->options.parenthesis.has_closing_parenthesis) {
            result->range = range_make(line->parse_index, closing_index + 1);
        }
        else {
            line_log_error(line, "Missing Closing parenthesis", line->parse_index);
            result->range = range_make(line->parse_index, closing_index);
        }
        line->parse_index = line->parse_index + 1;
        result->options.parenthesis.item = line_parse_binop_chain(line, closing_index);
        line->parse_index += 1; // Skip over closing parenthesis
        return result;
    }
    else if (line_test_delimiter(line, Delimiter_Type::DOT)) {
        AST_Item* error_item = line_allocate_item(line, AST_Type::ERROR_NODE, range_make_same(line->parse_index));
        return line_parse_operand_suffix(line, error_item);
    }

    return 0;
}

AST_Item* line_parse_operand_suffix(Syntax_Line* line, AST_Item* operand)
{
    auto& tokens = line->tokens;
    if (line_test_delimiter(line, Delimiter_Type::OPEN_PARENTHESIS))
    {
        int closing_index = line_find_closing_parenthesis(line, line->parse_index);
        AST_Item* call_item = line_allocate_item(line, AST_Type::FUNCTION_CALL, range_make(operand->range.start, closing_index + 1));
        call_item->options.function_call.has_closing_parenthesis = closing_index != tokens.size;
        call_item->options.function_call.closing_parenthesis_index = closing_index;
        call_item->options.function_call.open_parenthesis_index = line->parse_index;
        if (closing_index == tokens.size) {
            call_item->range.end = closing_index;
            line_log_error(line, "Missing Closing parenthesis", line->parse_index);
        }
        call_item->options.function_call.function_item = operand;
        call_item->options.function_call.arguments = dynamic_array_create_empty<AST_Item*>(1);
        call_item->options.function_call.comma_indices = dynamic_array_create_empty<int>(1);

        line->parse_index += 1;
        if (line_test_delimiter(line, Delimiter_Type::CLOSED_PARENTHESIS) || line_end_reached(line)) {
            line->parse_index += 1;
            return call_item;
        }
        int comma_index = line_find_next_delimiter_not_in_parenthesis(line, closing_index, Delimiter_Type::COMMA);
        while (comma_index != closing_index)
        {
            dynamic_array_push_back(&call_item->options.function_call.comma_indices, comma_index);
            AST_Item* argument = line_parse_binop_chain(line, comma_index);
            dynamic_array_push_back(&call_item->options.function_call.arguments, argument);
            line->parse_index = comma_index + 1;
            comma_index = line_find_next_delimiter_not_in_parenthesis(line, closing_index, Delimiter_Type::COMMA);
        }
        AST_Item* last_argument = line_parse_binop_chain(line, closing_index);
        dynamic_array_push_back(&call_item->options.function_call.arguments, last_argument);
        line->parse_index = closing_index + 1;
        return call_item;
    }
    else if (line_test_delimiter(line, Delimiter_Type::DOT))
    {
        AST_Item* access_item = line_allocate_item(line, AST_Type::MEMBER_ACCESS, range_make_length(operand->range.start, 1));
        auto& member = access_item->options.member_access;
        member.item = operand;
        member.dot_token_index = line->parse_index;
        line->parse_index += 1;
        if (line_test_token(line, Syntax_Token_Type::IDENTIFIER)) {
            member.has_identifier = true;
            member.identifier_token_index = line->parse_index;
            line->parse_index += 1;
            access_item->range.end = line->parse_index;
        }
        else {
            member.has_identifier = false;
            member.identifier_token_index = -1;
            //line_log_error(line, "Missing member name", member.dot_token_index);
        }
        return access_item;
    }
    return 0;
}

AST_Item* line_parse_operand(Syntax_Line* line)
{
    auto& tokens = line->tokens;

    // Parse prefixes
    if (line_test_delimiter(line, Delimiter_Type::MINUS) || line_test_delimiter(line, Delimiter_Type::STAR)) {
        AST_Item* prefix_item = line_allocate_item(line, AST_Type::UNARY_OPERATION, range_make_length(line->parse_index, 1));
        prefix_item->options.unary_op.operator_token_index = line->parse_index;
        line->parse_index += 1;
        AST_Item* operand = line_parse_operand(line);
        if (operand == 0) {
            operand = line_allocate_item(line, AST_Type::ERROR_NODE, range_make_same(line->parse_index));
        }
        prefix_item->options.unary_op.item = operand;
        return prefix_item;
    }

    AST_Item* operand_item = line_parse_simple_operand(line);
    AST_Item* suffix_item = operand_item;
    while (suffix_item != 0) {
        operand_item = suffix_item;
        suffix_item = line_parse_operand_suffix(line, operand_item);
    }
    return operand_item;
}

AST_Item* line_parse_binop_chain(Syntax_Line* line, int end_index)
{
    if (line->parse_index >= end_index) {
        return line_allocate_item(line, AST_Type::ERROR_NODE, range_make_same(line->parse_index));
    }

    AST_Item* first_operand = line_parse_operand(line);
    if (first_operand == 0)
    {
        if (line_test_binop_by_index(line, line->parse_index)) {
            first_operand = line_allocate_item(line, AST_Type::ERROR_NODE, range_make_same(line->parse_index));
        }
        else {
            line_log_error(line, "Operand Invalid", line->parse_index);
            first_operand = line_allocate_item(line, AST_Type::ERROR_NODE, range_make_length(line->parse_index, 1));
            line->parse_index += 1;
        }
    }
    if (line->parse_index >= end_index) {
        return first_operand;
    }

    AST_Item* binop = line_allocate_item(line, AST_Type::BINOP_CHAIN, range_make_same(first_operand->range.start));
    binop->options.binop_chain.start_item = first_operand;
    binop->options.binop_chain.items = dynamic_array_create_empty<Binop_Link>(1);
    while (line->parse_index < end_index)
    {
        Binop_Link link;
        link.token_index = line->parse_index;
        if (line_test_binop_by_index(line, line->parse_index)) {
            link.operator_missing = false;
            line->parse_index += 1;
        }
        else {
            link.operator_missing = true;
        }

        AST_Item* operand;
        if (line->parse_index >= end_index) {
            operand = line_allocate_item(line, AST_Type::ERROR_NODE, range_make_same(line->parse_index));
        }
        else {
            operand = line_parse_operand(line);
            if (operand == 0) {
                if (line_test_binop_by_index(line, line->parse_index)) {
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
        string_append_string(string, &token.options.identifier.identifier);
        break;
    case Syntax_Token_Type::DELIMITER: {
        String delim = delimiter_type_to_string(token.options.delimiter);
        string_append_string(string, &delim);
        break;
    }
    case Syntax_Token_Type::NUMBER:
        string_append_string(string, &token.options.number.text);
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
    auto& tokens = line->tokens;
    assert(index < tokens.size, "");
    auto& token = tokens[index];

    if (&editor->lines[editor->line_index] == line &&
        editor->mode == Editor_Mode::INPUT && 
        editor->insert_mode == Insert_Mode::BEFORE &&
        editor->cursor_index == index) {
        line->render_item_offset += 1;
    }

    Render_Item item;
    item.is_token = true;
    item.pos = line->render_item_offset;
    item.token_index = index;
    switch (token.type)
    {
    case Syntax_Token_Type::DELIMITER:
        item.size = 1;
        break;
    case Syntax_Token_Type::IDENTIFIER:
        item.size = token.options.identifier.identifier.size;
        break;
    case Syntax_Token_Type::NUMBER:
        item.size = token.options.number.text.size;
        break;
    default: panic("");
    }
    dynamic_array_push_back(&render_items, item);
    line->render_item_offset += item.size;
}

void line_rendering_add_tokens(Syntax_Line* line, Range range, Syntax_Editor* editor)
{
    for (int i = range.start; i < range.end; i++) {
        line_rendering_add_token(line, i, editor);
        if (i != range.end - 1) {
            line->render_item_offset += 1;
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
    auto& tokens = line->tokens;
    switch (item->type)
    {
    case AST_Type::DEFINITION: 
    {
        auto& definition = item->options.statement_definition;
        switch (definition.definition_type)
        {
        case Definition_Type::ASSIGN: // id: expr = expr
            line_rendering_add_token(line, 0, editor);
            line_rendering_add_token(line, 1, editor);
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, definition.type_expression, editor);
            line_rendering_insert_space(line);
            line_rendering_add_token(line, definition.assign_token_index, editor);
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, definition.value_expression, editor);
            break;
        case Definition_Type::COMPTIME: // id :: expr
            line_rendering_add_token(line, 0, editor);
            line_rendering_insert_space(line);
            line_rendering_add_token(line, 1, editor);
            line_rendering_add_token(line, 2, editor);
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, definition.value_expression, editor);
            break;
        case Definition_Type::INFER: // id := expr
            line_rendering_add_token(line, 0, editor);
            line_rendering_insert_space(line);
            line_rendering_add_token(line, 1, editor);
            line_rendering_add_token(line, 2, editor);
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, definition.value_expression, editor);
            break;
        case Definition_Type::NORMAL: // id: expr
            line_rendering_add_token(line, 0, editor);
            line_rendering_add_token(line, 1, editor);
            line_rendering_insert_space(line);
            line_rendering_ast_item_to_render_item(line, definition.type_expression, editor);
            break;
        default: panic("");
        }
        break;
    }
    case AST_Type::STATEMENT_ASSIGNMENT: {
        auto& assignment = item->options.statement_assignment;
        line_rendering_ast_item_to_render_item(line, assignment.left_side, editor);
        line_rendering_insert_space(line);
        line_rendering_add_token(line, assignment.assign_token_index, editor);
        line_rendering_insert_space(line);
        line_rendering_ast_item_to_render_item(line, assignment.right_side, editor);
        break;
    }
    case AST_Type::STATEMENT_EXPRESSION: {
        line_rendering_ast_item_to_render_item(line, item->options.statement_expression, editor);
        break;
    }
    case AST_Type::BINOP_CHAIN: {
        auto& binop = item->options.binop_chain;
        line_rendering_ast_item_to_render_item(line, binop.start_item, editor);
        for (int i = 0; i < item->options.binop_chain.items.size; i++) {
            line_rendering_insert_space(line);
            Binop_Link link = item->options.binop_chain.items[i];
            if (link.operator_missing) {
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
        line_rendering_add_token(line, call.open_parenthesis_index, editor); // Add (
        for (int i = 0; i < call.arguments.size; i++) {
            line_rendering_ast_item_to_render_item(line, call.arguments[i], editor);
            if (i != call.arguments.size - 1) {
                int comma_index = call.comma_indices[i];
                assert(line_test_delimiter_by_index(line, comma_index, Delimiter_Type::COMMA), "");
                line_rendering_add_token(line, comma_index, editor); // Add ,
                line_rendering_insert_space(line);
            }
        }
        if (call.has_closing_parenthesis) {
            line_rendering_add_token(line, call.closing_parenthesis_index, editor);
        }
        break;
    }
    case AST_Type::ERROR_NODE: {
        if (item->range.start == item->range.end) {
            line_rendering_add_gap(line);
        }
        else {
            line_rendering_add_tokens(line, item->range, editor);
        }
        break;
    }
    case AST_Type::OPERAND: {
        line_rendering_add_token(line, item->range.start, editor);
        break;
    }
    case AST_Type::PARENTHESIS: {
        line_rendering_add_token(line, item->range.start, editor);
        line_rendering_ast_item_to_render_item(line, item->options.parenthesis.item, editor);
        if (item->options.parenthesis.has_closing_parenthesis) {
            line_rendering_add_token(line, item->range.end - 1, editor);
        }
        break;
    }
    default: panic("");
    }
}

void line_reset(Syntax_Line* line)
{
    auto& tokens = line->tokens;

    // Reset
    for (int i = 0; i < line->allocated_items.size; i++) {
        AST_Item* item = line->allocated_items[i];
        switch (item->type)
        {
        case AST_Type::BINOP_CHAIN:
            dynamic_array_destroy(&item->options.binop_chain.items);
            break;
        case AST_Type::FUNCTION_CALL:
            dynamic_array_destroy(&item->options.function_call.arguments);
            dynamic_array_destroy(&item->options.function_call.comma_indices);
            break;
        }
        delete item;
    }
    dynamic_array_reset(&line->allocated_items);
    dynamic_array_reset(&line->error_messages);
    dynamic_array_reset(&line->render_items);
    line->root = 0;
    line->parse_index = 0;
    line->render_item_offset = 0;
    line->root = 0;
}

AST_Item* line_parse_statement(Syntax_Line* line)
{
    auto& tokens = line->tokens;
    if (tokens.size == 0) return 0;

    // Definition
    if (line_test_token(line, Syntax_Token_Type::IDENTIFIER) && line_test_delimiter_by_index(line, 1, Delimiter_Type::COLON)) 
    {
        AST_Item* result = line_allocate_item(line, AST_Type::DEFINITION, range_make(0, tokens.size));
        auto& definition = result->options.statement_definition;
        definition.assign_token_index = -1;
        line->parse_index += 2;
        if (line_test_delimiter_by_index(line, 2, Delimiter_Type::COLON)) {
            definition.definition_type = Definition_Type::COMPTIME;
            definition.type_expression = 0;
            line->parse_index += 1;
            definition.value_expression = line_parse_binop_chain(line, tokens.size);
        }
        else if (line_test_delimiter_by_index(line, 2, Delimiter_Type::ASSIGN)) {
            definition.definition_type = Definition_Type::INFER;
            definition.type_expression = 0;
            definition.assign_token_index = 2;
            line->parse_index += 1;
            definition.value_expression = line_parse_binop_chain(line, tokens.size);
        }
        else {
            int assign_index = line_find_next_delimiter_not_in_parenthesis(line, tokens.size, Delimiter_Type::ASSIGN);
            if (assign_index == tokens.size) {
                definition.definition_type = Definition_Type::NORMAL;
                definition.value_expression = 0;
                definition.type_expression = line_parse_binop_chain(line, tokens.size);
            }
            else {
                definition.assign_token_index = assign_index;
                definition.definition_type = Definition_Type::ASSIGN;
                definition.type_expression = line_parse_binop_chain(line, assign_index);
                line->parse_index = assign_index + 1;
                definition.value_expression = line_parse_binop_chain(line, tokens.size);
            }
        }

        return result;
    }

    // Assignment
    int assign_index = line_find_next_delimiter_not_in_parenthesis(line, tokens.size, Delimiter_Type::ASSIGN);
    if (assign_index != tokens.size)
    {
        AST_Item* result = line_allocate_item(line, AST_Type::STATEMENT_ASSIGNMENT, range_make(0, tokens.size));
        auto& assignment = result->options.statement_assignment;
        assignment.assign_token_index = assign_index;
        assignment.left_side = line_parse_binop_chain(line, assign_index);
        line->parse_index = assign_index + 1;
        assignment.right_side = line_parse_binop_chain(line, tokens.size);
        return result;
    }

    // Expression
    AST_Item* result = line_allocate_item(line, AST_Type::STATEMENT_EXPRESSION, range_make(0, tokens.size));
    result->options.statement_expression = line_parse_binop_chain(line, tokens.size);
    return result;
}

void line_parse_and_format(Syntax_Line* line, Syntax_Editor* editor)
{
    line_reset(line);
    auto& tokens = line->tokens;
    if (tokens.size == 0) return;

    // Check if tokens are keywords
    for (int i = 0; i < tokens.size; i++) {
        auto& token = tokens[i];
        if (token.type == Syntax_Token_Type::IDENTIFIER) {
            Keyword_Type* result =  hashtable_find_element(&editor->keyword_table, token.options.identifier.identifier);
            if (result != 0) {
                token.options.identifier.is_keyword = true;
                token.options.identifier.keyword_type = *result;
            }
            else {
                token.options.identifier.is_keyword = false;
            }
        }
    }

    line->root = line_parse_statement(line);
    line_rendering_ast_item_to_render_item(line, line->root, editor);


    // Sanity Checks

    // Check that all tokens are displayed
    bool print_render = false;
    for (int i = 0; i < tokens.size; i++) {
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
        for (int i = 0; i < tokens.size; i++) {
            syntax_token_append_to_string(tokens[i], &str);
            string_append_formated(&str, " ");
        }
        string_append_formated(&str, "\nRender_Items: ");
        for (int i = 0; i < line->render_items.size; i++) {
            auto& render_item = line->render_items[i];
            if (render_item.is_token) {
                syntax_token_append_to_string(tokens[render_item.token_index], &str);
            }
            string_append_formated(&str, " ");
        }
        logg("%s\n\n", str.characters);
        panic("Hey");
    }


    /*
    ast_item_append_to_string(parser->root, &str, parser->editor);
    logg("PARSED_TEXT:\n---------------\n%s\n\n", str.characters);
    */
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
    DIGIT,
    DELIMITER,
    SPACE,
    ENTER,
    EXIT_INSERT_MODE,
    BACKSPACE,
};

struct Input_Command
{
    Input_Command_Type type;
    char letter;
    Delimiter_Type delimiter;
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
    if (editor->cursor_index > tokens.size) {
        editor->cursor_index = tokens.size;
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
        string_destroy(&t.options.identifier.identifier);
        break;
    case Syntax_Token_Type::NUMBER:
        string_destroy(&t.options.number.text);
        break;
    }
    dynamic_array_remove_ordered(&tokens, editor->cursor_index);
}

void normal_mode_handle_command(Syntax_Editor* editor, Normal_Command command)
{
    auto& tokens = editor->lines[editor->line_index].tokens;
    auto& cursor = editor->cursor_index;
    switch (command)
    {
    case Normal_Command::DELETE_TOKEN: {
        if (cursor == tokens.size) return;
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
    case Normal_Command::INSERT_BEFORE: {
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
        syntax_editor_insert_line(editor, editor->line_index);
        editor->cursor_index = 0;
        editor->mode = Editor_Mode::INPUT;
        editor->insert_mode = Insert_Mode::BEFORE;
        break;
    }
    case Normal_Command::ADD_LINE_BELOW: {
        syntax_editor_insert_line(editor, editor->line_index + 1);
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
    auto& tokens = editor->lines[editor->line_index].tokens;
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
        syntax_editor_insert_line(editor, editor->line_index + 1);
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
                if (id.identifier.size == 1) {
                    remove_token = true;
                    break;
                }
                string_truncate(&id.identifier, id.identifier.size - 1);
                break;
            case Input_Command_Type::IDENTIFIER_LETTER:
                string_append_character(&id.identifier, input.letter);
                break;
            case Input_Command_Type::DIGIT:
                string_append_character(&id.identifier, input.letter);
                break;
            default: input_used = false;
            }
            break;
        }
        case Syntax_Token_Type::NUMBER: {
            auto& text = token.options.number.text;
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
            case Input_Command_Type::DIGIT:
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
        new_token.options.identifier.identifier = string_create_empty(1);
        new_token.options.identifier.is_keyword = false;
        string_append_character(&new_token.options.identifier.identifier, input.letter);
        break;
    }
    case Input_Command_Type::DIGIT: {
        token_valid = true;
        new_token.type = Syntax_Token_Type::NUMBER;
        new_token.options.number.text = string_create_empty(1);
        string_append_character(&new_token.options.number.text, input.letter);
        break;
    }
    case Input_Command_Type::DELIMITER: {
        token_valid = true;
        new_token.type = Syntax_Token_Type::DELIMITER;
        new_token.options.delimiter = input.delimiter;
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
                input.type = Input_Command_Type::ENTER;
            }
            else if (char_is_letter(msg.character) || msg.character == '_') {
                input.type = Input_Command_Type::IDENTIFIER_LETTER;
                input.letter = msg.character;
            }
            else if (char_is_digit(msg.character)) {
                input.type = Input_Command_Type::DIGIT;
                input.letter = msg.character;
            }
            else if (msg.character == '(') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::OPEN_PARENTHESIS;
            }
            else if (msg.character == ')') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::CLOSED_PARENTHESIS;
            }
            else if (msg.character == '+') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::PLUS;
            }
            else if (msg.character == '-') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::MINUS;
            }
            else if (msg.character == '*') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::STAR;
            }
            else if (msg.character == '/') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::SLASH;
            }
            else if (msg.character == '=') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::ASSIGN;
            }
            else if (msg.character == ',') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::COMMA;
            }
            else if (msg.character == '.') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::DOT;
            }
            else if (msg.character == ':') {
                input.type = Input_Command_Type::DELIMITER;
                input.delimiter = Delimiter_Type::COLON;
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
                command = Normal_Command::INSERT_AFTER;
            }
            else if (msg.key_code == Key_Code::I && msg.key_down) {
                command = Normal_Command::INSERT_BEFORE;
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

    for (int i = 0; i < editor->lines.size; i++) {
        line_parse_and_format(&editor->lines[i], editor);
    }
}

void syntax_editor_insert_line(Syntax_Editor* editor, int index)
{
    Syntax_Line line;
    line.error_messages = dynamic_array_create_empty<Error_Message>(1);
    line.render_items = dynamic_array_create_empty<Render_Item>(1);
    line.tokens = dynamic_array_create_empty<Syntax_Token>(1);
    line.allocated_items = dynamic_array_create_empty<AST_Item*>(1);
    dynamic_array_insert_ordered(&editor->lines, line, index);
}

Syntax_Editor* syntax_editor_create(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D)
{
    Syntax_Editor* result = new Syntax_Editor;
    result->cursor_index = 0;
    result->mode = Editor_Mode::INPUT;
    result->insert_mode = Insert_Mode::APPEND;
    result->line_index = 0;
    result->lines = dynamic_array_create_empty<Syntax_Line>(1);
    syntax_editor_insert_line(result, 0);

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
        dynamic_array_destroy(&line.tokens);
        dynamic_array_destroy(&line.error_messages);
        dynamic_array_destroy(&line.render_items);
    }
    dynamic_array_destroy(&editor->lines);
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

String delimiter_type_to_string(Delimiter_Type type)
{
    switch (type)
    {
    case Delimiter_Type::CLOSED_PARENTHESIS: return string_create_static(")");
    case Delimiter_Type::OPEN_PARENTHESIS:return string_create_static("(");
    case Delimiter_Type::MINUS: return string_create_static("-");
    case Delimiter_Type::PLUS: return string_create_static("+");
    case Delimiter_Type::SLASH: return string_create_static("/");
    case Delimiter_Type::STAR: return string_create_static("*");
    case Delimiter_Type::COMMA: return string_create_static(",");
    case Delimiter_Type::DOT: return string_create_static(".");
    case Delimiter_Type::COLON: return string_create_static(":");
    case Delimiter_Type::ASSIGN: return string_create_static("=");
    default: panic("");
    }
    return string_create_static("ERROR");
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
    auto& tokens = line.tokens;
    auto& render_items = line.render_items;

    if (tokens.size == 0) return 0;
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
    return 0;
}

void syntax_editor_render(Syntax_Editor* editor)
{
    auto& cursor = editor->cursor_index;

    // Prepare Render
    editor->character_size.y = text_renderer_cm_to_relative_height(editor->text_renderer, editor->rendering_core, 0.8f);
    editor->character_size.x = text_renderer_get_cursor_advance(editor->text_renderer, editor->character_size.y);

    // Render Tokens
    for (int j = 0; j < editor->lines.size; j++)
    {
        auto& line = editor->lines[j];
        auto& tokens = line.tokens;
        auto& render_items = line.render_items;
        for (int i = 0; i < render_items.size; i++)
        {
            auto& item = render_items[i];
            if (item.is_token)
            {
                Syntax_Token* token = &tokens[item.token_index];
                switch (token->type)
                {
                case Syntax_Token_Type::DELIMITER:
                    syntax_editor_draw_string(editor, delimiter_type_to_string(token->options.delimiter), Syntax_Color::TEXT, j, item.pos);
                    break;
                case Syntax_Token_Type::IDENTIFIER:
                    syntax_editor_draw_string(editor, token->options.identifier.identifier, 
                        token->options.identifier.is_keyword ? Syntax_Color::KEYWORD : Syntax_Color::IDENTIFIER_FALLBACK, j, item.pos);
                    break;
                case Syntax_Token_Type::NUMBER:
                    syntax_editor_draw_string(editor, token->options.number.text, Syntax_Color::LITERAL, j, item.pos);
                    break;
                default: panic("");
                }
            }
            else {
                syntax_editor_draw_underline(editor, j, item.pos, item.size, vec3(0.8f, 0.8f, 0.0f));
            }
        }

        // Render Error messages
        for (int i = 0; i < line.error_messages.size; i++) {
            auto& msg = line.error_messages[i];
            Render_Item* item = token_index_to_render_item(&line, msg.token_index);
            syntax_editor_draw_underline(editor, j, item->pos, item->size, vec3(0.8f, 0.0f, 0.0f));
        }

        // Render Cursor
        if (j == editor->line_index) {
            int cursor_pos = syntax_editor_find_visual_cursor_pos(editor, editor->cursor_index, editor->line_index, editor->mode);
            if (editor->mode == Editor_Mode::NORMAL) {
                syntax_editor_draw_character_box(editor, Syntax_Color::COMMENT, j, cursor_pos);
            }
            else {
                syntax_editor_draw_cursor_line(editor, Syntax_Color::COMMENT, j, cursor_pos);
            }
        }
    }

    // Render Primitives
    renderer_2D_render(editor->renderer_2D, editor->rendering_core);
    text_renderer_render(editor->text_renderer, editor->rendering_core);
}