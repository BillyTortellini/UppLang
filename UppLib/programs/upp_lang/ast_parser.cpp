#include "ast_parser.hpp"
#include "compiler.hpp"

typedef bool(*block_has_finished_function)(AST_Parser* parser);
typedef int(*block_error_find_end)(AST_Parser* parser);

void ast_parser_parse_statement_block_with_error_handling(
    AST_Parser* parser, AST_Node* parent, block_has_finished_function finished_fn, block_error_find_end find_resume_fn, int block_start_index);

int ast_node_check_for_undefines(AST_Node* node)
{
    if (node == 0) return 0;
    int count = 0;
    if (node->type == AST_Node_Type::UNDEFINED) {
        count++;
        panic("wellerman\n");
    }
    if (node->neighbor == node) panic("HEY");
    count += ast_node_check_for_undefines(node->neighbor);
    count += ast_node_check_for_undefines(node->child_start);
    return count;
}

int count = 0;
AST_Node* ast_parser_make_node_no_parent(AST_Parser* parser)
{
    count++;
    AST_Node* node = stack_allocator_allocate<AST_Node>(&parser->allocator);
    node->type = AST_Node_Type::UNDEFINED;
    node->child_count = 0;
    node->child_start = 0;
    node->child_end = 0;
    node->neighbor = 0;
    node->parent = 0;
    node->id = 0;
    node->token_range = token_range_make(-1, -1);
    node->literal_token = 0;
    node->alloc_index = count;
    return node;
}

void ast_node_add_child(AST_Node* parent, AST_Node* child)
{
    if (parent->child_count == 0) {
        parent->child_start = child;
        parent->child_end = child;
        child->neighbor = 0;
    }
    else {
        if (parent->child_end == child) panic("HEY");
        parent->child_end->neighbor = child;
        parent->child_end = child;
        if (parent->child_end == parent->child_end->neighbor) panic("WHAT");
    }
    child->parent = parent;
    parent->child_count++;
}

AST_Node* ast_parser_make_node_child(AST_Parser* parser, AST_Node* parent)
{
    AST_Node* node = ast_parser_make_node_no_parent(parser);
    ast_node_add_child(parent, node);
    return node;
}

AST_Parser_Checkpoint ast_parser_checkpoint_make(AST_Parser* parser, AST_Node* node)
{
    AST_Parser_Checkpoint result;
    result.parser = parser;
    result.node = node;
    if (node == 0) {
        result.last_child = 0;
        result.node_child_count = 0;
    }
    else 
    {
        result.node_child_count = node->child_count;
        result.last_child = node->child_end;
        if (node->child_count == 0) {
            assert(node->child_start == 0 && node->child_end == 0, "");
        }
        else {
            assert(node->child_start->type != AST_Node_Type::UNDEFINED && node->child_end->type != AST_Node_Type::UNDEFINED, "");
        }
    }
    result.rewind_token_index = parser->index;
    result.stack_checkpoint = stack_checkpoint_make(&parser->allocator);
    return result;
}

void check_node_parent(AST_Node* node)
{
    if (node == 0) return;
    int count = 0;
    AST_Node* child = node->child_start;
    while (child != 0) {
        assert(child->neighbor != child, "");
        count++;
        child = child->neighbor;
    }
    assert(count == node->child_count, "");
    check_node_parent(node->parent);
}

void ast_parser_checkpoint_reset(AST_Parser_Checkpoint checkpoint)
{
    checkpoint.parser->index = checkpoint.rewind_token_index;
    if (checkpoint.node != 0)
    {
        if (checkpoint.node_child_count == 0) {
            checkpoint.node->child_count = 0;

            checkpoint.node->child_start = 0;
            checkpoint.node->child_end = 0;
        }
        else if (checkpoint.node_child_count == 1) {
            checkpoint.node->child_count = checkpoint.node_child_count;
            checkpoint.node->child_start = checkpoint.last_child;

            checkpoint.node->child_end = checkpoint.last_child;
            checkpoint.node->child_end->neighbor = 0;
        }
        else {
            checkpoint.node->child_count = checkpoint.node_child_count;

            checkpoint.node->child_end = checkpoint.last_child;
            checkpoint.node->child_end->neighbor = 0;
        }
    }

    stack_checkpoint_rewind(checkpoint.stack_checkpoint);
    check_node_parent(checkpoint.node);
}

bool ast_parser_test_next_identifier(AST_Parser* parser, String* id)
{
    if (parser->index >= parser->code_source->tokens.size) {
        return false;
    }
    if (parser->code_source->tokens[parser->index].type == Token_Type::IDENTIFIER_NAME) {
        if (parser->code_source->tokens[parser->index].attribute.id == id) return true;
    }
    return false;
}

bool ast_parser_test_next_token(AST_Parser* parser, Token_Type type)
{
    if (parser->index >= parser->code_source->tokens.size) {
        return false;
    }
    if (parser->code_source->tokens[parser->index].type == type) {
        return true;
    }
    return false;
}

bool ast_parser_test_next_2_tokens(AST_Parser* parser, Token_Type type1, Token_Type type2)
{
    if (parser->index + 1 >= parser->code_source->tokens.size) {
        return false;
    }
    if (parser->code_source->tokens[parser->index].type == type1 && parser->code_source->tokens[parser->index + 1].type == type2) {
        return true;
    }
    return false;
}

bool ast_parser_test_next_3_tokens(AST_Parser* parser, Token_Type type1, Token_Type type2, Token_Type type3)
{
    if (parser->index + 2 >= parser->code_source->tokens.size) {
        return false;
    }
    if (parser->code_source->tokens[parser->index].type == type1 &&
        parser->code_source->tokens[parser->index + 1].type == type2 &&
        parser->code_source->tokens[parser->index + 2].type == type3) {
        return true;
    }
    return false;
}

bool ast_parser_test_next_4_tokens(AST_Parser* parser, Token_Type type1, Token_Type type2, Token_Type type3,
    Token_Type type4)
{
    if (parser->index + 3 >= parser->code_source->tokens.size) {
        return false;
    }
    if (parser->code_source->tokens[parser->index].type == type1 &&
        parser->code_source->tokens[parser->index + 1].type == type2 &&
        parser->code_source->tokens[parser->index + 2].type == type3 &&
        parser->code_source->tokens[parser->index + 3].type == type4)
    {
        return true;
    }
    return false;
}

bool ast_parser_test_next_5_tokens(AST_Parser* parser, Token_Type type1, Token_Type type2, Token_Type type3,
    Token_Type type4, Token_Type type5)
{
    if (parser->index + 4 >= parser->code_source->tokens.size) {
        return false;
    }
    if (parser->code_source->tokens[parser->index].type == type1 &&
        parser->code_source->tokens[parser->index + 1].type == type2 &&
        parser->code_source->tokens[parser->index + 2].type == type3 &&
        parser->code_source->tokens[parser->index + 3].type == type4 &&
        parser->code_source->tokens[parser->index + 4].type == type5) {
        return true;
    }
    return false;
}

int ast_parser_find_next_token_type(AST_Parser* parser, Token_Type type)
{
    int index = parser->index;
    while (index < parser->code_source->tokens.size)
    {
        if (parser->code_source->tokens[index].type == type) {
            return index;
        }
        index++;
    }
    return index;
}

int ast_parser_find_next_line_start_token(AST_Parser* parser)
{
    int i = parser->index;
    int line = parser->code_source->tokens[parser->index].position.start.line;
    while (i < parser->code_source->tokens.size) {
        int token_line = parser->code_source->tokens[i].position.start.line;
        if (token_line != line) return i;
        i++;
    }
    return i;
}

int ast_parser_find_parenthesis_ending(AST_Parser* parser, int start_index, Token_Type open_type, Token_Type closed_type, bool* depth_negativ)
{
    int i = parser->index;
    int depth = 0;
    while (i < parser->code_source->tokens.size)
    {
        if (parser->code_source->tokens[i].type == open_type) depth++;
        if (parser->code_source->tokens[i].type == closed_type) {
            depth--;
            if (depth <= 0) {
                if (depth == 0) {
                    *depth_negativ = false;
                }
                else {
                    *depth_negativ = true;
                }
                return i;
            }
        }
        i++;
    }
    *depth_negativ = true;
    return i;
}

void ast_parser_log_error(AST_Parser* parser, const char* msg, Token_Range range)
{
    if (parser->errors.size > 0) {
        Compiler_Error last_error = parser->errors[parser->errors.size - 1];
        if (last_error.range.start_index <= range.start_index || last_error.range.end_index >= range.start_index) {
            return; // Skip nested errors
        }
    }
    Compiler_Error error;
    error.message = msg;
    error.range = range;
    dynamic_array_push_back(&parser->errors, error);
}

bool ast_parser_parse_expression(AST_Parser* parser, AST_Node* parent);
bool ast_parser_parse_type(AST_Parser* parser, AST_Node* parent);
bool ast_parser_parse_parameter_block(
    AST_Parser* parser, AST_Node* parent, bool is_named_parameter_block, Token_Type open_parenthesis_type, Token_Type closed_parenthesis_type, bool log_error
);

bool ast_parser_parse_identifier_or_path(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);

    if (!ast_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    node->id = parser->code_source->tokens[parser->index].attribute.id;
    parser->index++;

    bool is_template_analysis = false;
    if (ast_parser_parse_parameter_block(parser, node, false, Token_Type::COMPARISON_LESS, Token_Type::COMPARISON_GREATER, false)) {
        is_template_analysis = true;
    }

    if (ast_parser_test_next_token(parser, Token_Type::DOUBLE_COLON))
    {
        parser->index++;
        if (is_template_analysis) {
            node->type = AST_Node_Type::IDENTIFIER_PATH_TEMPLATED;
        }
        else {
            node->type = AST_Node_Type::IDENTIFIER_PATH;
        }
        if (ast_parser_parse_identifier_or_path(parser, node)) {
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    else {
        if (is_template_analysis) {
            node->type = AST_Node_Type::IDENTIFIER_NAME_TEMPLATED;
        }
        else {
            node->type = AST_Node_Type::IDENTIFIER_NAME;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_function_signature(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);

    if (ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS))
    {
        node->type = AST_Node_Type::FUNCTION_SIGNATURE;
        if (!ast_parser_parse_parameter_block(parser, node, true, Token_Type::OPEN_PARENTHESIS, Token_Type::CLOSED_PARENTHESIS, true)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_test_next_token(parser, Token_Type::ARROW)) {
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        parser->index++;
        if (!ast_parser_parse_type(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_type(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);

    if (ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS))
    {
        node->type = AST_Node_Type::TYPE_FUNCTION_POINTER;
        if (!ast_parser_parse_parameter_block(parser, node, false, Token_Type::OPEN_PARENTHESIS, Token_Type::CLOSED_PARENTHESIS, true)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_test_next_token(parser, Token_Type::ARROW)) {
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        parser->index++;
        if (!ast_parser_parse_type(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_parse_identifier_or_path(parser, node)) {
        node->type = AST_Node_Type::TYPE_IDENTIFIER;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_token(parser, Token_Type::OP_STAR)) {
        node->type = AST_Node_Type::TYPE_POINTER_TO;
        parser->index++;
        if (ast_parser_parse_type(parser, node)) {
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
    }

    if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACKETS))
    {
        parser->index++;
        node->type = AST_Node_Type::TYPE_SLICE;
        if (ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACKETS)) {
            parser->index++;
            if (!ast_parser_parse_type(parser, node)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }

        node->type = AST_Node_Type::TYPE_ARRAY;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACKETS)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        parser->index++;
        if (!ast_parser_parse_type(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_argument_block(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);
    node->type = AST_Node_Type::ARGUMENTS;

    if (!ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++;
    if (ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
        parser->index++;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    // TODO: Better error handling
    while (!ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS))
    {
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
            parser->index++;
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        if (ast_parser_test_next_token(parser, Token_Type::COMMA)) {
            parser->index++;
            continue;
        }
        // Error
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++; // Skip )
    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

bool ast_parser_parse_statement_block(AST_Parser* parser, AST_Node* parent, bool label_allowed);
bool ast_parser_parse_expression(AST_Parser* parser, AST_Node* parent);
AST_Node* ast_parser_parse_expression_no_parents(AST_Parser* parser);
AST_Node* ast_parser_parse_expression_single_value(AST_Parser* parser)
{
    /*
        Prefix Operations:
            Negate - a          X
            Not    ! a          X
            AddrOf * a          X
            Deref  & a          X
            Cast   cast<u64> a  X
        Operands:
            Expr   (a+...)      X
            Lit    true         X
            Read   a            X
        Postfix Operations
            Mem    a.
            Array  a[]
            Call   a()
    */
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, 0);

    // Parse operand
    AST_Node* node = 0;
    if (ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) // Parenthesized expression
    {
        parser->index++;
        node = ast_parser_parse_expression_no_parents(parser);
        if (node == 0 || !ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index++;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) // Variable read
    {
        node = ast_parser_make_node_no_parent(parser);
        if (!ast_parser_parse_identifier_or_path(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        node->type = AST_Node_Type::EXPRESSION_VARIABLE_READ;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    }
    else if (ast_parser_test_next_token(parser, Token_Type::INTEGER_LITERAL) || // Literal read
        ast_parser_test_next_token(parser, Token_Type::FLOAT_LITERAL) ||
        ast_parser_test_next_token(parser, Token_Type::BOOLEAN_LITERAL) ||
        ast_parser_test_next_token(parser, Token_Type::STRING_LITERAL) ||
        ast_parser_test_next_token(parser, Token_Type::NULLPTR))
    {
        node = ast_parser_make_node_no_parent(parser);
        node->type = AST_Node_Type::EXPRESSION_LITERAL;
        node->literal_token = &parser->code_source->tokens[parser->index];
        parser->index++;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    }
    else if (ast_parser_test_next_token(parser, Token_Type::NEW))
    {
        node = ast_parser_make_node_no_parent(parser);
        node->type = AST_Node_Type::EXPRESSION_NEW;
        parser->index++;
        if (ast_parser_test_next_2_tokens(parser, Token_Type::OPEN_BRACKETS, Token_Type::CLOSED_BRACKETS)) {
            ast_parser_log_error(parser, "Cannot have new with empty brackets", token_range_make(checkpoint.rewind_token_index, parser->index));
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACKETS))
        {
            node->type = AST_Node_Type::EXPRESSION_NEW_ARRAY;
            parser->index++;
            if (!ast_parser_parse_expression(parser, node)) {
                ast_parser_log_error(parser, "Invalid array-size expression in new", token_range_make(checkpoint.rewind_token_index, parser->index));
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
            if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACKETS)) {
                ast_parser_log_error(parser, "Missing closing brackets in array new", token_range_make(checkpoint.rewind_token_index, parser->index));
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
            parser->index++;
        }
        if (!ast_parser_parse_type(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    }
    else if (ast_parser_test_next_token(parser, Token_Type::HASHTAG)) 
    {
        parser->index++;
        if (!ast_parser_test_next_identifier(parser, parser->id_bake)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index++;

        node = ast_parser_make_node_no_parent(parser);
        node->type = AST_Node_Type::EXPRESSION_BAKE;
        if (!ast_parser_test_next_token(parser, Token_Type::COMPARISON_LESS)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index++;

        if (!ast_parser_parse_type(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }

        if (!ast_parser_test_next_token(parser, Token_Type::COMPARISON_GREATER)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        parser->index++;

        if (!ast_parser_parse_statement_block(parser, node, false)) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return node;
    }

    // Parse post operators
    if (node != 0)
    {
        while (true)
        {
            if (ast_parser_test_next_2_tokens(parser, Token_Type::DOT, Token_Type::IDENTIFIER_NAME)) // Member access
            {
                AST_Node* new_node = ast_parser_make_node_no_parent(parser);
                new_node->type = AST_Node_Type::EXPRESSION_MEMBER_ACCESS;
                new_node->id = parser->code_source->tokens[parser->index + 1].attribute.id;
                new_node->token_range = token_range_make(parser->index, parser->index + 2);
                parser->index += 2;
                ast_node_add_child(new_node, node);
                node = new_node;
            }
            else if (ast_parser_test_next_token(parser, Token_Type::OPEN_BRACKETS)) // Array access
            {
                AST_Node* new_node = ast_parser_make_node_no_parent(parser);
                new_node->type = AST_Node_Type::EXPRESSION_ARRAY_ACCESS;
                parser->index++;
                ast_node_add_child(new_node, node);
                if (!ast_parser_parse_expression(parser, new_node)) {
                    ast_parser_checkpoint_reset(checkpoint);
                    return 0;
                }
                if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACKETS)) {
                    ast_parser_checkpoint_reset(checkpoint);
                    return 0;
                }
                parser->index++;
                new_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
                node = new_node;
            }
            else if (ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) // Function call
            {
                AST_Node* new_node = ast_parser_make_node_no_parent(parser);
                new_node->type = AST_Node_Type::EXPRESSION_FUNCTION_CALL;
                ast_node_add_child(new_node, node);
                if (!ast_parser_parse_argument_block(parser, new_node)) {
                    ast_parser_checkpoint_reset(checkpoint);
                    return 0;
                }
                new_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
                node = new_node;
            }
            else {
                return node;
            }
        }
    }

    // Parse Pre-Operators
    node = ast_parser_make_node_no_parent(parser);
    if (ast_parser_test_next_token(parser, Token_Type::CAST))
    {
        parser->index += 1;
        node->type = AST_Node_Type::EXPRESSION_CAST;
        if (ast_parser_test_next_token(parser, Token_Type::COMPARISON_LESS)) 
        {
            parser->index++;
            if (!ast_parser_parse_type(parser, node)) {
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
            if (!ast_parser_test_next_token(parser, Token_Type::COMPARISON_GREATER)) {
                ast_parser_checkpoint_reset(checkpoint);
                return 0;
            }
            parser->index += 1;
        }
    }
    else if (ast_parser_test_next_2_tokens(parser, Token_Type::DOT, Token_Type::IDENTIFIER_NAME)) {
        node->type = AST_Node_Type::EXPRESSION_AUTO_MEMBER;
        node->id = parser->code_source->tokens[parser->index + 1].attribute.id;
        node->token_range = token_range_make(parser->index, parser->index + 2);
        parser->index += 2;
        return node;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::OP_MINUS)) {
        node->type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE;
        parser->index++;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::LOGICAL_NOT)) {
        node->type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT;
        parser->index++;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::OP_STAR)) {
        node->type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF;
        parser->index++;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::LOGICAL_BITWISE_AND)) {
        node->type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
        parser->index++;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::LOGICAL_AND))
    {
        node->type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
        parser->index++;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);

        AST_Node* child_node = ast_parser_make_node_child(parser, node);
        child_node->type = AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
        child_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);

        AST_Node* operand_node = ast_parser_parse_expression_single_value(parser);
        if (operand_node == 0) {
            ast_parser_checkpoint_reset(checkpoint);
            return 0;
        }
        ast_node_add_child(child_node, operand_node);
        return node;
    }
    else {
        ast_parser_checkpoint_reset(checkpoint);
        return 0;
    }

    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index); // TODO: Think what token mappings should include
    AST_Node* child_node = ast_parser_parse_expression_single_value(parser);
    if (child_node == 0) {
        ast_parser_checkpoint_reset(checkpoint);
        return 0;
    }
    ast_node_add_child(node, child_node);
    return node;
}

bool ast_parser_parse_binary_operator(AST_Parser* parser, AST_Node_Type* op_type, int* op_priority)
{
    /*
        Priority tree:
            0       ---     &&
            1       ---     ||
            2       ---     ==, !=
            3       ---     <, >, <=, >=
            4       ---     +, -
            5       ---     *, /
            6       ---     %
    */
    if (parser->index + 1 >= parser->code_source->tokens.size) return false;
    switch (parser->code_source->tokens[parser->index].type)
    {
    case Token_Type::LOGICAL_AND: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND;
        *op_priority = 0;
        break;
    }
    case Token_Type::LOGICAL_OR: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR;
        *op_priority = 1;
        break;
    }
    case Token_Type::COMPARISON_EQUAL: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL;
        *op_priority = 2;
        break;
    }
    case Token_Type::COMPARISON_NOT_EQUAL: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL;
        *op_priority = 2;
        break;
    }
    case Token_Type::COMPARISON_GREATER: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER;
        *op_priority = 3;
        break;
    }
    case Token_Type::COMPARISON_GREATER_EQUAL: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL;
        *op_priority = 3;
        break;
    }
    case Token_Type::COMPARISON_LESS: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS;
        *op_priority = 3;
        break;
    }
    case Token_Type::COMPARISON_LESS_EQUAL: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL;
        *op_priority = 3;
        break;
    }
    case Token_Type::OP_PLUS: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION;
        *op_priority = 4;
        break;
    }
    case Token_Type::OP_MINUS: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION;
        *op_priority = 4;
        break;
    }
    case Token_Type::OP_STAR: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION;
        *op_priority = 5;
        break;
    }
    case Token_Type::OP_SLASH: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION;
        *op_priority = 5;
        break;
    }
    case Token_Type::OP_PERCENT: {
        *op_type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO;
        *op_priority = 6;
        break;
    }
    default: {
        return false;
    }
    }

    parser->index++;
    return true;
}

AST_Node* ast_parser_parse_binary_expression(AST_Parser* parser, AST_Node* node, int min_priority)
{
    int start_point = parser->index;
    int rewind_point = parser->index;

    bool first_run = true;
    int max_priority = 999;
    while (true)
    {
        AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, node);

        int first_op_priority;
        int first_op_index = parser->index;
        AST_Node_Type first_op_type;
        if (!ast_parser_parse_binary_operator(parser, &first_op_type, &first_op_priority)) {
            break;
        }
        if (first_op_priority < max_priority) {
            max_priority = first_op_priority;
        }
        if (first_op_priority < min_priority) {
            parser->index = rewind_point; // Undo the binary operation, maybe just do 0
            break;
        }

        AST_Node* operator_node = ast_parser_make_node_no_parent(parser);
        operator_node->type = AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND; // This is just so that we dont have any undefines
        AST_Node* right_operand_node = ast_parser_parse_expression_single_value(parser);
        if (right_operand_node == 0) {
            ast_parser_checkpoint_reset(checkpoint);
            break;
        }
        rewind_point = parser->index;

        int second_op_priority;
        AST_Node_Type second_op_type;
        bool second_op_exists = ast_parser_parse_binary_operator(parser, &second_op_type, &second_op_priority);
        if (second_op_exists)
        {
            parser->index--;
            if (second_op_priority > max_priority) {
                right_operand_node = ast_parser_parse_binary_expression(parser, right_operand_node, second_op_priority);
            }
        }

        ast_node_add_child(operator_node, node);
        ast_node_add_child(operator_node, right_operand_node);
        operator_node->type = first_op_type;
        operator_node->token_range = token_range_make(
            operator_node->child_start->token_range.start_index,
            operator_node->child_end->token_range.end_index
        );

        node = operator_node;
        if (!second_op_exists) break;
    }

    return node;
}

AST_Node* ast_parser_parse_expression_no_parents(AST_Parser* parser)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, 0);
    AST_Node* single_value_node = ast_parser_parse_expression_single_value(parser);
    if (single_value_node == 0) {
        ast_parser_checkpoint_reset(checkpoint);
        return 0;
    }
    return ast_parser_parse_binary_expression(parser, single_value_node, 0);
}

bool ast_parser_parse_expression(AST_Parser* parser, AST_Node* parent)
{
    AST_Node* op_tree_root_index = ast_parser_parse_expression_no_parents(parser);
    if (op_tree_root_index == 0) { return false; }
    ast_node_add_child(parent, op_tree_root_index);
    return true;
}

bool ast_parser_parse_statement_block(AST_Parser* parser, AST_Node* parent, bool label_allowed);
bool ast_parser_parse_statement(AST_Parser* parser, AST_Node* parent);
bool ast_parser_parse_single_statement_or_block(AST_Parser* parser, AST_Node* parent, bool label_allowed)
{
    if (ast_parser_parse_statement_block(parser, parent, label_allowed)) {
        return true;
    }

    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);
    if (!ast_parser_parse_statement(parser, node)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    node->type = AST_Node_Type::STATEMENT_BLOCK;
    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);

    return true;
}

bool ast_parser_parse_single_variable_definition(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);
    if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::COLON))
    {
        parser->index += 2;
        if (!ast_parser_parse_type(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            node->type = AST_Node_Type::STATEMENT_VARIABLE_DEFINITION;
            node->id = parser->code_source->tokens[checkpoint.rewind_token_index].attribute.id;
            parser->index += 1;
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
    }
    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_variable_creation_statement(AST_Parser* parser, AST_Node* parent_node)
{
    if (ast_parser_parse_single_variable_definition(parser, parent_node)) {
        return true;
    }

    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_node);
    AST_Node* node = ast_parser_make_node_child(parser, parent_node);

    if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::COLON))
    {
        parser->index += 2;
        if (!ast_parser_parse_type(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::OP_ASSIGNMENT))
        {
            node->type = AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN;
            node->id = parser->code_source->tokens[checkpoint.rewind_token_index].attribute.id;
            parser->index += 1;
            if (!ast_parser_parse_expression(parser, node)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
            if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
                parser->index++;
                node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
                return true;
            }
            else {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
        }
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::INFER_ASSIGN))
    {
        node->type = AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER;
        node->id = parser->code_source->tokens[parser->index].attribute.id;
        parser->index += 2;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_switch_statement(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* switch_node = ast_parser_make_node_child(parser, parent);
    switch_node->type = AST_Node_Type::STATEMENT_SWITCH;

    block_error_find_end switch_error_find_end_fn = [](AST_Parser* parser) -> int {
        bool unused;
        int next_brace = ast_parser_find_parenthesis_ending(parser, parser->index, Token_Type::OPEN_BRACES, Token_Type::CLOSED_BRACES, &unused);
        int next_case = ast_parser_find_next_token_type(parser, Token_Type::CASE);
        int next_default = ast_parser_find_next_token_type(parser, Token_Type::DEFAULT);
        int recover_index = next_brace;
        if (next_case < next_default && next_case < next_brace) {
            recover_index = next_case;
        }
        if (next_default < next_case && next_default < next_brace) {
            recover_index = next_default;
        }
        return recover_index;
    };

    if (!ast_parser_test_next_token(parser, Token_Type::SWITCH)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++;

    if (!ast_parser_parse_expression(parser, switch_node)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    String* label_id = 0;
    if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::COLON)) {
        label_id = parser->code_source->tokens[parser->index].attribute.id;
        parser->index += 2;
    }

    if (!ast_parser_test_next_token(parser, Token_Type::OPEN_BRACES)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index += 1;

    while (parser->index < parser->code_source->tokens.size && !ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES))
    {
        bool error_occured = false;
        AST_Parser_Checkpoint recoverable_checkpoint = ast_parser_checkpoint_make(parser, switch_node);
        AST_Node* case_node = ast_parser_make_node_child(parser, switch_node);
        if (ast_parser_test_next_token(parser, Token_Type::CASE))
        {
            case_node->type = AST_Node_Type::SWITCH_CASE;
            parser->index++;
            if (!ast_parser_parse_expression(parser, case_node)) {
                error_occured = true;
            }
            if (!error_occured) {
                if (ast_parser_test_next_token(parser, Token_Type::COLON)) {
                    parser->index++;
                }
                else {
                    error_occured = true;
                }
            }
        }
        else if (ast_parser_test_next_2_tokens(parser, Token_Type::DEFAULT, Token_Type::COLON)) {
            case_node->type = AST_Node_Type::SWITCH_DEFAULT_CASE;
            parser->index += 2;
        }
        else {
            error_occured = true;
        }

        // Parse statement block without {}
        if (!error_occured)
        {
            ast_parser_parse_statement_block_with_error_handling(
                parser, case_node,
                [](AST_Parser* parser) -> bool {
                    return ast_parser_test_next_token(parser, Token_Type::CASE) ||
                        ast_parser_test_next_token(parser, Token_Type::DEFAULT) ||
                        ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES);
                },
                switch_error_find_end_fn, parser->index
                    );
            case_node->child_end->id = label_id;
        }

        // Do error handling
        if (!error_occured) {
            case_node->token_range = token_range_make(recoverable_checkpoint.rewind_token_index, parser->index);
            continue;
        }

        ast_parser_checkpoint_reset(recoverable_checkpoint);
        parser->index = switch_error_find_end_fn(parser);
        ast_parser_log_error(parser, "Could not parse switch case", token_range_make(recoverable_checkpoint.rewind_token_index, parser->index));
        return false;
    }

    if (ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
        parser->index++;
    }
    else {
        ast_parser_log_error(parser, "Switch statement never ends", token_range_make(checkpoint.rewind_token_index, parser->code_source->tokens.size));
    }

    switch_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

bool ast_parser_parse_while(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);
    if (ast_parser_test_next_token(parser, Token_Type::WHILE))
    {
        node->type = AST_Node_Type::STATEMENT_WHILE;
        parser->index++;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_parse_single_statement_or_block(parser, node, true)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }
    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_statement(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    if (ast_parser_parse_variable_creation_statement(parser, parent)) {
        return true;
    }
    if (ast_parser_parse_statement_block(parser, parent, true)) {
        return true;
    }
    if (ast_parser_parse_switch_statement(parser, parent)) {
        return true;
    }
    if (ast_parser_parse_while(parser, parent)) {
        return true;
    }

    AST_Node* node = ast_parser_make_node_child(parser, parent);
    if (ast_parser_parse_expression(parser, node))
    {
        node->type = AST_Node_Type::STATEMENT_EXPRESSION;
        if (ast_parser_test_next_token(parser, Token_Type::OP_ASSIGNMENT))
        {
            node->type = AST_Node_Type::STATEMENT_ASSIGNMENT;
            parser->index++;
            if (!ast_parser_parse_expression(parser, node)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
        }

        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    if (ast_parser_test_next_token(parser, Token_Type::DEFER))
    {
        parser->index++;
        if (ast_parser_parse_single_statement_or_block(parser, node, false)) {
            node->type = AST_Node_Type::STATEMENT_DEFER;
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        ast_parser_log_error(parser, "Invalid statement after defer keyword", token_range_make(checkpoint.rewind_token_index, parser->index));
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    if (ast_parser_test_next_token(parser, Token_Type::DELETE_TOKEN))
    {
        node->type = AST_Node_Type::STATEMENT_DELETE;
        parser->index++;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_log_error(parser, "Invalid expression after delete", token_range_make(checkpoint.rewind_token_index, parser->index));
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    if (ast_parser_test_next_token(parser, Token_Type::IF))
    {
        node->type = AST_Node_Type::STATEMENT_IF;
        parser->index++;
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_parse_single_statement_or_block(parser, node, true)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }

        if (ast_parser_test_next_token(parser, Token_Type::ELSE))
        {
            node->type = AST_Node_Type::STATEMENT_IF_ELSE;
            parser->index++;
            if (!ast_parser_parse_single_statement_or_block(parser, node, true)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
        }
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_3_tokens(parser, Token_Type::BREAK, Token_Type::IDENTIFIER_NAME, Token_Type::SEMICOLON))
    {
        node->type = AST_Node_Type::STATEMENT_BREAK;
        node->id = parser->code_source->tokens[parser->index + 1].attribute.id;
        parser->index += 3;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_3_tokens(parser, Token_Type::CONTINUE, Token_Type::IDENTIFIER_NAME, Token_Type::SEMICOLON))
    {
        node->type = AST_Node_Type::STATEMENT_CONTINUE;
        node->id = parser->code_source->tokens[parser->index + 1].attribute.id;
        parser->index += 3;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_token(parser, Token_Type::RETURN))
    {
        node->type = AST_Node_Type::STATEMENT_RETURN;
        parser->index++;
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        if (!ast_parser_parse_expression(parser, node)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
            return true;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

void ast_parser_parse_statement_block_with_error_handling(
    AST_Parser* parser, AST_Node* parent, block_has_finished_function finished_fn, block_error_find_end find_resume_fn, int block_start_index)
{
    int start_token_index = parser->index;
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);

    node->type = AST_Node_Type::STATEMENT_BLOCK;
    while (!finished_fn(parser))
    {
        if (parser->index >= parser->code_source->tokens.size) {
            ast_parser_log_error(parser, "Statement block did not end!", token_range_make(block_start_index, parser->index));
            node->token_range = token_range_make(checkpoint.rewind_token_index, parser->code_source->tokens.size);
            return;
        }
        AST_Parser_Checkpoint recoverable_checkpoint = ast_parser_checkpoint_make(parser, node);
        if (ast_parser_parse_statement(parser, node)) {
            continue;
        }
        ast_parser_checkpoint_reset(recoverable_checkpoint);

        // Error handling, check if we can continue at next line or after next semicolon
        int next_semi = ast_parser_find_next_token_type(parser, Token_Type::SEMICOLON);
        int next_line = ast_parser_find_next_line_start_token(parser);
        int next_resume = find_resume_fn(parser);
        if (next_line < next_semi && next_line < next_resume) {
            ast_parser_log_error(parser, "Could not parse statement", token_range_make(parser->index, next_line));
            parser->index = next_line;
            continue;
        }
        if (next_semi < next_resume) {
            ast_parser_log_error(parser, "Could not parse statement", token_range_make(parser->index, next_semi));
            parser->index = next_semi + 1;
            continue;
        }
        ast_parser_log_error(parser, "Could not parse statement", token_range_make(parser->index, next_resume));
        parser->index = next_resume;
    }

    node->token_range = token_range_make(start_token_index, parser->index);
    if (node->type != AST_Node_Type::STATEMENT_BLOCK) {
        logg("Wath");
    }
    if (node->token_range.start_index == 0) {
        logg("What");
    }
    return;
}

bool ast_parser_parse_statement_block(AST_Parser* parser, AST_Node* parent, bool label_allowed)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    String* id = 0;
    if (label_allowed && ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::COLON)) {
        id = parser->code_source->tokens[parser->index].attribute.id;
        parser->index += 2;
    }
    if (!ast_parser_test_next_token(parser, Token_Type::OPEN_BRACES)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++;

    ast_parser_parse_statement_block_with_error_handling(
        parser, parent,
        [](AST_Parser* parser) -> bool {
            if (ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
                parser->index++;
                return true;
            }
            return false;
        },
        [](AST_Parser* parser) -> int {
            bool unused;
            return ast_parser_find_parenthesis_ending(parser, parser->index, Token_Type::OPEN_BRACES, Token_Type::CLOSED_BRACES, &unused);
        }, 
            parser->index - 1
        );

    AST_Node* block_node = parent->child_end;
    block_node->id = id;
    block_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

typedef bool(*ast_parsing_function)(AST_Parser* parser, AST_Node* parent);
bool ast_parser_parse_enclosed_list(AST_Parser* parser, AST_Node* parent, Token_Type seperator_token, ast_parsing_function parse_func, bool empty_valid,
    Token_Type enclosure_start, Token_Type enclosure_end, AST_Node_Type list_node_type)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);
    node->type = list_node_type;
    if (!ast_parser_test_next_token(parser, enclosure_start)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++;

    bool first_time = true;
    while (true)
    {
        AST_Parser_Checkpoint recoverable_checkpoint = ast_parser_checkpoint_make(parser, node);
        if (parse_func(parser, node))
        {
            first_time = false;
            if (ast_parser_test_next_token(parser, seperator_token)) {
                parser->index++;
                continue;
            }
            break;
        }
        if (first_time) {
            if (!empty_valid) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
            break;
        }
        first_time = false;

        // Error recovery
        {
            ast_parser_checkpoint_reset(recoverable_checkpoint);
            // Error handling: find next ), or next Comma, or next report error and do further error handling
            bool unused;
            int index_enclosure_ending = ast_parser_find_parenthesis_ending(parser, parser->index, enclosure_start, enclosure_end, &unused);
            int index_next_closed_braces = ast_parser_find_next_token_type(parser, Token_Type::CLOSED_BRACES);
            int index_next_seperator = ast_parser_find_next_token_type(parser, seperator_token);
            if (index_next_seperator < index_enclosure_ending && index_next_seperator < index_next_closed_braces) {
                ast_parser_log_error(parser, "Could not parse list item", token_range_make(parser->index, index_next_seperator));
                parser->index = index_next_seperator + 1;
                continue;
            }
            if (index_enclosure_ending < index_next_closed_braces) {
                ast_parser_log_error(parser, "Could not parse list item", token_range_make(parser->index, index_enclosure_ending));
                parser->index = index_enclosure_ending + 1;
                node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
                return true;
            }
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    if (!ast_parser_test_next_token(parser, enclosure_end)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++;
    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

bool ast_parser_parse_parameter_block(
    AST_Parser* parser, AST_Node* parent, bool is_named_parameter_block, Token_Type open_parenthesis_type, Token_Type closed_parenthesis_type, bool log_errors)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);

    if (is_named_parameter_block) {
        node->type = AST_Node_Type::PARAMETER_BLOCK_NAMED;
    }
    else {
        node->type = AST_Node_Type::PARAMETER_BLOCK_UNNAMED;
    }

    if (!ast_parser_test_next_token(parser, open_parenthesis_type)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++;
    if (ast_parser_test_next_token(parser, closed_parenthesis_type)) {
        parser->index++;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    while (true)
    {
        AST_Parser_Checkpoint recoverable_checkpoint = ast_parser_checkpoint_make(parser, node);
        bool success = true;
        if (is_named_parameter_block)
        {
            if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::COLON))
            {
                AST_Node* parameter_node = ast_parser_make_node_child(parser, node);
                parser->index += 2;

                success = ast_parser_parse_type(parser, parameter_node);
                if (success)
                {
                    parameter_node->type = AST_Node_Type::NAMED_PARAMETER;
                    parameter_node->id = parser->code_source->tokens[recoverable_checkpoint.rewind_token_index].attribute.id;
                    parameter_node->token_range = token_range_make(recoverable_checkpoint.rewind_token_index, parser->index);
                }
            }
        }
        else {
            success = ast_parser_parse_type(parser, node);
        }

        if (success)
        {
            if (ast_parser_test_next_token(parser, Token_Type::COMMA)) {
                parser->index++;
                continue;
            }
            else if (ast_parser_test_next_token(parser, closed_parenthesis_type)) {
                parser->index++;
                node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
                return true;
            }
            else {
                success = false;
            }
        }

        if (!success)
        {
            if (!log_errors) {
                // This is necessary in the case of identifier paths, since <> are also Comparison operators
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
            ast_parser_checkpoint_reset(recoverable_checkpoint);
            // Error handling: find next ), or next Comma, or next  report error and do further error handling
            int next_closed_braces = ast_parser_find_next_token_type(parser, Token_Type::CLOSED_BRACES);
            bool unused;
            int next_closed_parenthesis = ast_parser_find_parenthesis_ending(parser, parser->index, open_parenthesis_type, closed_parenthesis_type, &unused);
            int next_comma = ast_parser_find_next_token_type(parser, Token_Type::COMMA);
            if (next_comma < next_closed_parenthesis && next_comma < next_closed_braces) {
                ast_parser_log_error(parser, "Could not parse function parameter", token_range_make(parser->index, next_comma));
                parser->index = next_comma + 1;
                continue;
            }
            if (next_closed_parenthesis < next_closed_braces) {
                ast_parser_log_error(parser, "Could not parse parameters", token_range_make(parser->index, next_closed_parenthesis));
                parser->index = next_closed_parenthesis + 1;
                node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
                return true;
            }
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    panic("Should not happen!\n");
    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_enum(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* enum_node = ast_parser_make_node_child(parser, parent);
    enum_node->type = AST_Node_Type::ENUM;

    // Parse Enum name
    if (!ast_parser_test_next_4_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::DOUBLE_COLON, Token_Type::ENUM, Token_Type::OPEN_BRACES)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    enum_node->id = parser->code_source->tokens[parser->index].attribute.id;
    parser->index += 4;

    while (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES))
    {
        AST_Parser_Checkpoint member_checkpoint = ast_parser_checkpoint_make(parser, enum_node);
        bool success = ast_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME);

        if (success)
        {
            AST_Node* member_node = ast_parser_make_node_child(parser, enum_node);
            member_node->type = AST_Node_Type::ENUM_MEMBER;
            member_node->id = parser->code_source->tokens[parser->index].attribute.id;
            parser->index++;

            if (ast_parser_test_next_token(parser, Token_Type::DOUBLE_COLON)) {
                parser->index++;
                success = ast_parser_parse_expression(parser, member_node);
            }
            member_node->token_range = token_range_make(member_checkpoint.rewind_token_index, parser->index);
        }

        if (success) {
            success = ast_parser_test_next_token(parser, Token_Type::SEMICOLON);
        }
        if (success) {
            parser->index++;
        }


        // Do error handling here (Goto next semicolon or Closed Braces)
        if (!success)
        {
            ast_parser_checkpoint_reset(member_checkpoint);
            int next_semicolon = ast_parser_find_next_token_type(parser, Token_Type::SEMICOLON);
            int next_closing_braces = ast_parser_find_next_token_type(parser, Token_Type::CLOSED_BRACES);
            if (next_semicolon < next_closing_braces) {
                ast_parser_log_error(parser, "Enum member invalid!", token_range_make(member_checkpoint.rewind_token_index, next_semicolon + 1));
                parser->index = next_semicolon + 1;
                continue;
            }
            ast_parser_log_error(parser, "Enum member invalid!", token_range_make(checkpoint.rewind_token_index, next_closing_braces));
            parser->index = next_closing_braces;
            break;
        }
    }
    if (ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
        parser->index++;
    }

    enum_node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

bool ast_parser_parse_struct(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);

    // Parse Struct name
    if (ast_parser_test_next_4_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::DOUBLE_COLON, Token_Type::STRUCT, Token_Type::OPEN_BRACES)) {
        node->type = AST_Node_Type::STRUCT;
    }
    else if (ast_parser_test_next_4_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::DOUBLE_COLON, Token_Type::UNION, Token_Type::OPEN_BRACES)) {
        node->type = AST_Node_Type::UNION;
    }
    else {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    node->id = parser->code_source->tokens[parser->index].attribute.id;
    parser->index += 4;

    while (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES))
    {
        AST_Parser_Checkpoint member_checkpoint = ast_parser_checkpoint_make(parser, node);
        bool success = ast_parser_parse_single_variable_definition(parser, node);
        // Do error handling here (Goto next semicolon or Closed Braces)
        if (!success)
        {
            ast_parser_checkpoint_reset(member_checkpoint);
            int next_semicolon = ast_parser_find_next_token_type(parser, Token_Type::SEMICOLON);
            int next_closing_braces = ast_parser_find_next_token_type(parser, Token_Type::CLOSED_BRACES);
            if (next_semicolon < next_closing_braces) {
                ast_parser_log_error(parser, "Variable definition invalid!", token_range_make(member_checkpoint.rewind_token_index, next_semicolon + 1));
                parser->index = next_semicolon + 1;
                continue;
            }
            ast_parser_log_error(parser, "Variable definition invalid!", token_range_make(checkpoint.rewind_token_index, next_closing_braces));
            parser->index = next_closing_braces;
            break;
        }
    }

    if (ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
        parser->index++;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }
    ast_parser_checkpoint_reset(checkpoint);
    return false;
}

bool ast_parser_parse_function(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);
    AST_Node* node = ast_parser_make_node_child(parser, parent);
    node->type = AST_Node_Type::FUNCTION;

    // Parse Function start
    if (!ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::DOUBLE_COLON)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    node->id = parser->code_source->tokens[parser->index].attribute.id;
    parser->index += 2;

    if (!ast_parser_parse_function_signature(parser, node)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    if (!ast_parser_parse_statement_block(parser, node, false)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    };

    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

bool ast_parser_parse_extern_source_declarations(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent);

    if (ast_parser_test_next_2_tokens(parser, Token_Type::HASHTAG, Token_Type::IDENTIFIER_NAME))
    {
        if (parser->code_source->tokens[parser->index + 1].attribute.id = parser->id_load)
        {
            parser->index += 2;
            if (ast_parser_test_next_2_tokens(parser, Token_Type::STRING_LITERAL, Token_Type::SEMICOLON)) {
                AST_Node* node = ast_parser_make_node_child(parser, parent);
                node->type = AST_Node_Type::LOAD_FILE;
                node->id = parser->code_source->tokens[parser->index].attribute.id;
                parser->index += 2;
                node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
                return true;
            }
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    if (!ast_parser_test_next_token(parser, Token_Type::EXTERN)) {
        return false;
    }
    parser->index += 1;

    if (ast_parser_test_next_2_tokens(parser, Token_Type::STRING_LITERAL, Token_Type::OPEN_BRACES))
    {
        AST_Node* node = ast_parser_make_node_child(parser, parent);
        node->type = AST_Node_Type::EXTERN_HEADER_IMPORT;
        node->id = parser->code_source->tokens[parser->index].attribute.id;
        parser->index += 2;
        while (true)
        {
            if (ast_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) {
                AST_Node* child_node = ast_parser_make_node_child(parser, node);
                child_node->type = AST_Node_Type::IDENTIFIER_NAME;
                child_node->id = parser->code_source->tokens[parser->index].attribute.id;
                child_node->token_range = token_range_make(parser->index, parser->index + 1);
                parser->index++;
            }
            else if (ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
                parser->index++;
                break;
            }
            else {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
        }

        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (ast_parser_test_next_3_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::STRING_LITERAL, Token_Type::SEMICOLON))
    {
        String* id1 = parser->code_source->tokens[parser->index].attribute.id;
        String* id2 = parser->code_source->tokens[parser->index + 1].attribute.id;
        if (id1 != parser->id_lib) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        AST_Node* node = ast_parser_make_node_child(parser, parent);
        node->type = AST_Node_Type::EXTERN_LIB_IMPORT;
        node->id = id2;
        parser->index += 3;
        node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
        return true;
    }

    if (!ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER_NAME, Token_Type::DOUBLE_COLON)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    AST_Node* node = ast_parser_make_node_child(parser, parent);
    node->type = AST_Node_Type::EXTERN_FUNCTION_DECLARATION;
    node->id = parser->code_source->tokens[parser->index].attribute.id;
    parser->index += 2;

    if (!ast_parser_parse_type(parser, node)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    if (!ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index += 1;
    node->token_range = token_range_make(checkpoint.rewind_token_index, parser->index);
    return true;
}

bool ast_parser_parse_module(AST_Parser* parser, AST_Node* parent);
bool ast_parser_parse_definitions(AST_Parser* parser, AST_Node* parent)
{
    int start_index = parser->index;
    AST_Node* node = ast_parser_make_node_child(parser, parent);
    node->type = AST_Node_Type::DEFINITIONS;

    while (parser->index < parser->code_source->tokens.size)
    {
        if (ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
            break;
        }

        AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, node);
        if (ast_parser_parse_function(parser, node)) {
            continue;
        }
        if (ast_parser_parse_struct(parser, node)) {
            continue;
        }
        if (ast_parser_parse_module(parser, node)) {
            continue;
        }
        if (ast_parser_parse_variable_creation_statement(parser, node)) {
            continue;
        }
        if (ast_parser_parse_extern_source_declarations(parser, node)) {
            continue;
        }
        if (ast_parser_parse_enum(parser, node)) {
            continue;
        }

        ast_parser_checkpoint_reset(checkpoint);
        bool exit_braces_found;
        int next_closing_braces = ast_parser_find_parenthesis_ending(
            parser, checkpoint.rewind_token_index, Token_Type::OPEN_BRACES, Token_Type::CLOSED_BRACES, &exit_braces_found
        );
        int next_line = ast_parser_find_next_line_start_token(parser);
        if (next_line < next_closing_braces) {
            ast_parser_log_error(parser, "Could not parse Definitions", token_range_make(parser->index, next_line));
            parser->index = next_line;
            continue;
        }

        ast_parser_log_error(parser, "Could not parse Definitions", token_range_make(parser->index, next_closing_braces + 1));
        parser->index = next_closing_braces + 1;
        if (exit_braces_found) {
            break;
        }
    }

    node->token_range.start_index = start_index;
    node->token_range.end_index = math_clamp(parser->index, 0, parser->code_source->tokens.size);
    return true;
}

bool ast_parser_parse_module(AST_Parser* parser, AST_Node* parent)
{
    AST_Parser_Checkpoint start_checkpoint = ast_parser_checkpoint_make(parser, parent);
    if (!ast_parser_test_next_2_tokens(parser, Token_Type::MODULE, Token_Type::IDENTIFIER_NAME)) {
        return false;
    }
    parser->index += 2;

    AST_Node* node = ast_parser_make_node_child(parser, parent);
    node->id = parser->code_source->tokens[parser->index - 1].attribute.id;
    node->type = AST_Node_Type::MODULE;

    bool is_template_analysis = ast_parser_parse_enclosed_list(
        parser, node, Token_Type::COMMA,
        [](AST_Parser* parser, AST_Node* parent) -> bool {
            if (!ast_parser_test_next_token(parser, Token_Type::IDENTIFIER_NAME)) {
                return false;
            }
            AST_Node* node = ast_parser_make_node_child(parser, parent);
            node->type = AST_Node_Type::IDENTIFIER_NAME;
            node->id = parser->code_source->tokens[parser->index].attribute.id;
            node->token_range = token_range_make(parser->index, parser->index + 1);
            parser->index++;
            return true;
        },
        false, Token_Type::COMPARISON_LESS, Token_Type::COMPARISON_GREATER, AST_Node_Type::TEMPLATE_PARAMETERS
            );
    if (is_template_analysis) {
        node->type = AST_Node_Type::MODULE_TEMPLATED;
    }

    if (!ast_parser_test_next_token(parser, Token_Type::OPEN_BRACES)) {
        ast_parser_checkpoint_reset(start_checkpoint);
        return false;
    }
    parser->index++;

    if (!ast_parser_parse_definitions(parser, node)) {
        ast_parser_checkpoint_reset(start_checkpoint);
        return false;
    }

    if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
        ast_parser_checkpoint_reset(start_checkpoint);
        return false;
    }
    parser->index++;

    node->token_range.start_index = start_checkpoint.rewind_token_index;
    node->token_range.end_index = math_clamp(parser->index, 0, parser->code_source->tokens.size);

    return true;
}

void ast_parser_parse_root(AST_Parser* parser)
{
    parser->code_source->root_node = ast_parser_make_node_no_parent(parser);
    parser->code_source->root_node->type = AST_Node_Type::ROOT;
    ast_parser_parse_definitions(parser, parser->code_source->root_node);
    parser->code_source->root_node->token_range = token_range_make(0, math_maximum(0, parser->code_source->tokens.size - 1));
}

void ast_parser_check_sanity(AST_Parser* parser, AST_Node* node)
{
    while (node != 0)
    {
        SCOPE_EXIT(node = node->neighbor;);

        // Verify child count and parent relation
        if (node->child_start != 0)
        {
            int count = 0;
            AST_Node* child = node->child_start;
            while (child != 0)
            {
                assert(child->parent == node, "HEY");
                count++;
                if (count == node->child_count) {
                    assert(node->child_end == child, "");
                }
                ast_parser_check_sanity(parser, child);
                child = child->neighbor;
            }
            assert(count == node->child_count, "");
        }

        // Check parent indices
        if (node->parent == 0) {
            assert(node->type == AST_Node_Type::ROOT, "HEY");
        }

        // Check if token mappings are only 0 if Definitions or Root
        if (parser->code_source->tokens.size != 0)
        {
            int start = node->token_range.start_index;
            int end = node->token_range.end_index;
            if (start < 0 || end < 0 || start >= parser->code_source->tokens.size || end > parser->code_source->tokens.size) {
                logg("Should not happen: range: %d-%d\n", start, end);
                panic("Should not happen!");
            }
            if (start == end) {
                if (node->type != AST_Node_Type::ROOT && node->type != AST_Node_Type::DEFINITIONS && node->type != AST_Node_Type::STATEMENT_BLOCK) {
                    logg("Should not happen: range: %d-%d\n", start, end);
                    logg("Node_Type::%s\n", ast_node_type_to_string(node->type).characters);
                    panic("Should not happen!");
                }
            }
        }

        // Check if child types are allowed for given node type
        AST_Node* child = node->child_start;
        switch (node->type)
        {
        case AST_Node_Type::ROOT:
        case AST_Node_Type::MODULE:
            assert(node->child_count == 1, "");
            assert(child->type == AST_Node_Type::DEFINITIONS, "");
            break;
        case AST_Node_Type::ENUM: {
            while (child != 0) {
                assert(child->type == AST_Node_Type::ENUM_MEMBER, "");
                child = child->neighbor;
            }
            break;
        }
        case AST_Node_Type::ENUM_MEMBER:
            assert(node->child_count == 0 || node->child_count == 1, "");
            if (child != 0) {
                assert(ast_node_type_is_expression(child->type), "");
            }
            break;
        case AST_Node_Type::MODULE_TEMPLATED:
            assert(node->child_count == 2, "");
            assert(child->type == AST_Node_Type::TEMPLATE_PARAMETERS, "");
            assert(child->neighbor->type == AST_Node_Type::DEFINITIONS, "");
            break;
        case AST_Node_Type::DEFINITIONS:
            while (child != 0)
            {
                AST_Node_Type child_type = child->type;
                assert(child_type == AST_Node_Type::FUNCTION ||
                    child_type == AST_Node_Type::STRUCT ||
                    child_type == AST_Node_Type::UNION ||
                    child_type == AST_Node_Type::ENUM ||
                    child_type == AST_Node_Type::EXTERN_FUNCTION_DECLARATION ||
                    child_type == AST_Node_Type::EXTERN_LIB_IMPORT ||
                    child_type == AST_Node_Type::EXTERN_HEADER_IMPORT ||
                    child_type == AST_Node_Type::LOAD_FILE ||
                    child_type == AST_Node_Type::MODULE ||
                    child_type == AST_Node_Type::MODULE_TEMPLATED ||
                    child_type == AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN ||
                    child_type == AST_Node_Type::STATEMENT_VARIABLE_DEFINITION ||
                    child_type == AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER, "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::UNION:
        case AST_Node_Type::STRUCT:
            while (child != 0) {
                assert(child->type == AST_Node_Type::STATEMENT_VARIABLE_DEFINITION, "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::EXTERN_FUNCTION_DECLARATION:
            assert(node->child_count == 1, "");
            assert(ast_node_type_is_type(child->type), "");
            break;
        case AST_Node_Type::EXTERN_HEADER_IMPORT: {
            while (child != 0) {
                assert(child->type == AST_Node_Type::IDENTIFIER_NAME, "");
                child = child->neighbor;
            }
            break;
        }
        case AST_Node_Type::LOAD_FILE:
        case AST_Node_Type::EXTERN_LIB_IMPORT:
        case AST_Node_Type::IDENTIFIER_NAME:
            assert(node->child_count == 0, "");
            break;
        case AST_Node_Type::IDENTIFIER_NAME_TEMPLATED:
            assert(node->child_count == 1, "");
            if (child->type != AST_Node_Type::PARAMETER_BLOCK_UNNAMED) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::IDENTIFIER_PATH:
            assert(node->child_count == 1, "");
            if (child->type != AST_Node_Type::IDENTIFIER_NAME &&
                child->type != AST_Node_Type::IDENTIFIER_PATH &&
                child->type != AST_Node_Type::IDENTIFIER_NAME_TEMPLATED &&
                child->type != AST_Node_Type::IDENTIFIER_PATH_TEMPLATED) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::IDENTIFIER_PATH_TEMPLATED:
            assert(node->child_count == 2, "");
            if (child->type != AST_Node_Type::PARAMETER_BLOCK_UNNAMED) {
                panic("Should not happen");
            }
            if (child->neighbor->type != AST_Node_Type::IDENTIFIER_NAME &&
                child->neighbor->type != AST_Node_Type::IDENTIFIER_PATH &&
                child->neighbor->type != AST_Node_Type::IDENTIFIER_NAME_TEMPLATED &&
                child->neighbor->type != AST_Node_Type::IDENTIFIER_PATH_TEMPLATED) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::FUNCTION:
            assert(node->child_count == 2, "");
            if (child->type != AST_Node_Type::FUNCTION_SIGNATURE ||
                child->neighbor->type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::PARAMETER_BLOCK_NAMED:
            while (child != 0) {
                assert(child->type == AST_Node_Type::NAMED_PARAMETER, "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::PARAMETER_BLOCK_UNNAMED:
            while (child != 0) {
                assert(ast_node_type_is_type(child->type), "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::FUNCTION_SIGNATURE:
            assert(node->child_count == 1 || node->child_count == 2, "");
            assert(child->type == AST_Node_Type::PARAMETER_BLOCK_NAMED, "");
            if (node->child_count == 2) {
                assert(ast_node_type_is_type(child->neighbor->type), "");
            }
            break;
        case AST_Node_Type::TYPE_FUNCTION_POINTER:
            assert(node->child_count == 1 || node->child_count == 2, "");
            assert(child->type == AST_Node_Type::PARAMETER_BLOCK_UNNAMED, "");
            if (node->child_count == 2) {
                assert(ast_node_type_is_type(child->neighbor->type), "");
            }
            break;
        case AST_Node_Type::TYPE_SLICE:
        case AST_Node_Type::TYPE_POINTER_TO:
        case AST_Node_Type::NAMED_PARAMETER:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_type(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::TYPE_ARRAY:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::SWITCH_CASE:
            assert(node->child_count == 2, "");
            assert(ast_node_type_is_expression(node->child_start->type), "");
            assert(node->child_end->type == AST_Node_Type::STATEMENT_BLOCK, "");
            break;
        case AST_Node_Type::SWITCH_DEFAULT_CASE:
            assert(node->child_count == 1, "");
            assert(node->child_end->type == AST_Node_Type::STATEMENT_BLOCK, "");
            break;
        case AST_Node_Type::STATEMENT_SWITCH:
            assert(node->child_count >= 1, "");
            assert(ast_node_type_is_expression(child->type), "");
            child = child->neighbor;
            while (child != 0) {
                assert(child->type == AST_Node_Type::SWITCH_CASE || child->type == AST_Node_Type::SWITCH_DEFAULT_CASE, "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::STATEMENT_BLOCK:
            while (child != 0) {
                assert(ast_node_type_is_statement(child->type), "");
                child = child->neighbor;
            }
            break;
        case AST_Node_Type::STATEMENT_WHILE:
        case AST_Node_Type::STATEMENT_IF:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            if (child->neighbor->type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_IF_ELSE:
            assert(node->child_count == 3, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            if (child->neighbor->type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            if (child->neighbor->neighbor->type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_DEFER:
            assert(node->child_count == 1, "");
            if (child->type != AST_Node_Type::STATEMENT_BLOCK) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_BREAK:
        case AST_Node_Type::STATEMENT_CONTINUE:
            assert(node->child_count == 0, "");
            break;
        case AST_Node_Type::TYPE_IDENTIFIER: {
            assert(node->child_count == 1, "");
            AST_Node_Type child_type = child->type;
            if (child_type != AST_Node_Type::IDENTIFIER_NAME &&
                child_type != AST_Node_Type::IDENTIFIER_PATH &&
                child_type != AST_Node_Type::IDENTIFIER_NAME_TEMPLATED &&
                child_type != AST_Node_Type::IDENTIFIER_PATH_TEMPLATED
                ) {
                panic("Should not happen");
            }
            break;
        }
        case AST_Node_Type::STATEMENT_EXPRESSION:
        case AST_Node_Type::STATEMENT_RETURN:
            if (node->child_count == 0) return;
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_ASSIGNMENT:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(child->neighbor->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_type(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_type(child->type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(child->neighbor->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::STATEMENT_DELETE:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::ARGUMENTS: {
            while (child != 0) {
                assert(ast_node_type_is_expression(child->type), "");
                child = child->neighbor;
            }
            break;
        }
        case AST_Node_Type::TEMPLATE_PARAMETERS: {
            if (node->child_count == 0) {
                panic("Should not happen");
            }
            while (child != 0) {
                assert(child->type == AST_Node_Type::IDENTIFIER_NAME, "");
                child = child->neighbor;
            }
            break;
        }
        case AST_Node_Type::EXPRESSION_NEW:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_type(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_NEW_ARRAY:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_type(child->neighbor->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_LITERAL:
            assert(node->child_count == 0, "");
            break;
        case AST_Node_Type::EXPRESSION_FUNCTION_CALL:
        {
            assert(node->child_count == 2, "");
            AST_Node_Type child_type_0 = child->type;
            AST_Node_Type child_type_1 = child->neighbor->type;
            if (!ast_node_type_is_expression(child_type_0)) {
                panic("Should not happen");
            }
            if (child_type_1 != AST_Node_Type::ARGUMENTS) {
                panic("Should not happen");
            }
            break;
        }
        case AST_Node_Type::EXPRESSION_VARIABLE_READ:
            assert(node->child_count == 1, "");
            if (child->type != AST_Node_Type::IDENTIFIER_NAME &&
                child->type != AST_Node_Type::IDENTIFIER_PATH &&
                child->type != AST_Node_Type::IDENTIFIER_PATH_TEMPLATED &&
                child->type != AST_Node_Type::IDENTIFIER_NAME_TEMPLATED
                ) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_ARRAY_ACCESS:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(child->neighbor->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_BAKE:
            assert(node->child_count == 2, "");
            assert(ast_node_type_is_type(node->child_start->type), "");
            assert(node->child_end->type == AST_Node_Type::STATEMENT_BLOCK, "");
            break;
        case AST_Node_Type::EXPRESSION_CAST:
            assert(node->child_count == 2 || node->child_count == 1, "");
            if (node->child_count == 2) {
                if (!ast_node_type_is_type(child->type)) {
                    panic("Should not happen");
                }
                if (!ast_node_type_is_expression(child->neighbor->type)) {
                    panic("Should not happen");
                }
            }
            else {
                if (!ast_node_type_is_expression(child->type)) {
                    panic("Should not happen");
                }
            }
            break;
        case AST_Node_Type::EXPRESSION_AUTO_MEMBER:
            assert(node->child_count == 0, "");
            break;
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER:
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL:
            assert(node->child_count == 2, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            if (!ast_node_type_is_expression(child->neighbor->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE:
        case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT:
        case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF:
        case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE:
            assert(node->child_count == 1, "");
            if (!ast_node_type_is_expression(child->type)) {
                panic("Should not happen");
            }
            break;
        case AST_Node_Type::UNDEFINED:
            panic("Should not happen!");
            break;
        default:
            panic("Should not happen!");
            break;
        }
    }
}

Token_Range adjust_token_range(AST_Parser* parser, AST_Node* node)
{
    Token_Range range = node->token_range;
    AST_Node* child = node->child_start;
    while (child != 0)
    {
        Token_Range child_range = adjust_token_range(parser, child);
        if (child_range.start_index < range.start_index) {
            range.start_index = child_range.start_index;
        }
        if (child_range.end_index > range.end_index) {
            range.end_index = child_range.end_index;
        }
        child = child->neighbor;
    }
    node->token_range = range;
    return range;
}

void ast_parser_reset(AST_Parser* parser, Identifier_Pool* id_pool)
{
    count = 0; // Debugging
    stack_allocator_reset(&parser->allocator);
    parser->id_lib = identifier_pool_add(id_pool, string_create_static("lib"));
    parser->id_load = identifier_pool_add(id_pool, string_create_static("load"));
    parser->id_bake = identifier_pool_add(id_pool, string_create_static("bake"));
    dynamic_array_reset(&parser->errors);
}

void ast_parser_parse(AST_Parser* parser, Code_Source* source)
{
    // Reset parser data
    parser->code_source = source;
    parser->index = 0;

    // Parse
    ast_parser_parse_root(parser);

    // Check sanity
    ast_node_check_for_undefines(source->root_node);
    ast_parser_check_sanity(parser, source->root_node);

    // Cleanup token mapping, which I am still not sure if it is necessary
    adjust_token_range(parser, source->root_node);
}

AST_Parser ast_parser_create()
{
    AST_Parser parser;
    parser.index = 0;
    parser.allocator = stack_allocator_create_empty(sizeof(AST_Node) * 2048);
    parser.errors = dynamic_array_create_empty<Compiler_Error>(64);
    return parser;
}

void ast_parser_destroy(AST_Parser* parser) {
    stack_allocator_destroy(&parser->allocator);
    dynamic_array_destroy(&parser->errors);
}

String ast_node_type_to_string(AST_Node_Type type)
{
    switch (type)
    {
    case AST_Node_Type::ROOT: return string_create_static("ROOT");
    case AST_Node_Type::STRUCT: return string_create_static("STRUCT");
    case AST_Node_Type::UNION: return string_create_static("UNION");
    case AST_Node_Type::ENUM: return string_create_static("ENUM");
    case AST_Node_Type::ENUM_MEMBER: return string_create_static("ENUM_MEMBER");
    case AST_Node_Type::DEFINITIONS: return string_create_static("DEFINITIONS");
    case AST_Node_Type::MODULE_TEMPLATED: return string_create_static("MODULE_TEMPLATED");
    case AST_Node_Type::TEMPLATE_PARAMETERS: return string_create_static("TEMPLATE_PARAMETERS");
    case AST_Node_Type::MODULE: return string_create_static("MODULE");
    case AST_Node_Type::EXTERN_FUNCTION_DECLARATION: return string_create_static("EXTERN_FUNCTION_DECLARATION");
    case AST_Node_Type::EXTERN_LIB_IMPORT: return string_create_static("EXTERN_LIB_IMPORT");
    case AST_Node_Type::EXTERN_HEADER_IMPORT: return string_create_static("EXTERN_HEADER_IMPORT");
    case AST_Node_Type::LOAD_FILE: return string_create_static("LOAD_FILE");
    case AST_Node_Type::FUNCTION: return string_create_static("FUNCTION");
    case AST_Node_Type::IDENTIFIER_NAME: return string_create_static("IDENTIFIER_NAME");
    case AST_Node_Type::IDENTIFIER_PATH: return string_create_static("IDENTIFIER_PATH");
    case AST_Node_Type::IDENTIFIER_NAME_TEMPLATED: return string_create_static("IDENTIFIER_NAME_TEMPLATED");
    case AST_Node_Type::IDENTIFIER_PATH_TEMPLATED: return string_create_static("IDENTIFIER_PATH_TEMPLATED");
    case AST_Node_Type::ARGUMENTS: return string_create_static("ARGUMENTS");
    case AST_Node_Type::SWITCH_CASE: return string_create_static("SWITCH_CASE");
    case AST_Node_Type::SWITCH_DEFAULT_CASE: return string_create_static("SWITCH_DEFAULT_CASE");
    case AST_Node_Type::FUNCTION_SIGNATURE: return string_create_static("FUNCTION_SIGNATURE");
    case AST_Node_Type::TYPE_FUNCTION_POINTER: return string_create_static("TYPE_FUNCTION_POINTER");
    case AST_Node_Type::TYPE_IDENTIFIER: return string_create_static("TYPE_IDENTIFIER");
    case AST_Node_Type::TYPE_POINTER_TO: return string_create_static("TYPE_POINTER_TO");
    case AST_Node_Type::TYPE_ARRAY: return string_create_static("TYPE_ARRAY");
    case AST_Node_Type::TYPE_SLICE: return string_create_static("TYPE_SLICE");
    case AST_Node_Type::PARAMETER_BLOCK_UNNAMED: return string_create_static("PARAMETER_BLOCK_UNNAMED");
    case AST_Node_Type::PARAMETER_BLOCK_NAMED: return string_create_static("PARAMETER_BLOCK_NAMED");
    case AST_Node_Type::NAMED_PARAMETER: return string_create_static("PARAMETER");
    case AST_Node_Type::STATEMENT_DEFER: return string_create_static("STATEMENT_DEFER");
    case AST_Node_Type::STATEMENT_BLOCK: return string_create_static("STATEMENT_BLOCK");
    case AST_Node_Type::STATEMENT_IF: return string_create_static("STATEMENT_IF");
    case AST_Node_Type::STATEMENT_IF_ELSE: return string_create_static("STATEMENT_IF_ELSE");
    case AST_Node_Type::STATEMENT_SWITCH: return string_create_static("STATEMENT_SWITCH");
    case AST_Node_Type::STATEMENT_WHILE: return string_create_static("STATEMENT_WHILE");
    case AST_Node_Type::STATEMENT_BREAK: return string_create_static("STATEMENT_BREAK");
    case AST_Node_Type::STATEMENT_CONTINUE: return string_create_static("STATEMENT_CONTINUE");
    case AST_Node_Type::STATEMENT_RETURN: return string_create_static("STATEMENT_RETURN");
    case AST_Node_Type::STATEMENT_EXPRESSION: return string_create_static("STATEMENT_EXPRESSION");
    case AST_Node_Type::STATEMENT_ASSIGNMENT: return string_create_static("STATEMENT_ASSIGNMENT");
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION: return string_create_static("STATEMENT_VARIABLE_DEFINITION");
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN: return string_create_static("STATEMENT_VARIABLE_DEFINE_ASSIGN");
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER: return string_create_static("STATEMENT_VARIABLE_DEFINE_INFER");
    case AST_Node_Type::STATEMENT_DELETE: return string_create_static("STATEMENT_DELETE");
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS: return string_create_static("EXPRESSION_ARRAY_INDEX");
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS: return string_create_static("EXPRESSION_MEMBER_ACCESS");
    case AST_Node_Type::EXPRESSION_CAST: return string_create_static("EXPRESSION_CAST");
    case AST_Node_Type::EXPRESSION_BAKE: return string_create_static("EXPRESSION_BAKE");
    case AST_Node_Type::EXPRESSION_LITERAL: return string_create_static("EXPRESSION_LITERAL");
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL: return string_create_static("EXPRESSION_FUNCTION_CALL");
    case AST_Node_Type::EXPRESSION_VARIABLE_READ: return string_create_static("EXPRESSION_VARIABLE_READ");
    case AST_Node_Type::EXPRESSION_NEW: return string_create_static("EXPRESSION_NEW");
    case AST_Node_Type::EXPRESSION_NEW_ARRAY: return string_create_static("EXPRESSION_NEW_ARRAY");
    case AST_Node_Type::EXPRESSION_AUTO_MEMBER: return string_create_static("EXPRESSION_AUTO_MEMBER");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION: return string_create_static("EXPRESSION_BINARY_OPERATION_ADDITION");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION: return string_create_static("EXPRESSION_BINARY_OPERATION_SUBTRACTION");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION: return string_create_static("EXPRESSION_BINARY_OPERATION_DIVISION");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION: return string_create_static("EXPRESSION_BINARY_OPERATION_MULTIPLICATION");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO: return string_create_static("EXPRESSION_BINARY_OPERATION_MODULO");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND: return string_create_static("EXPRESSION_BINARY_OPERATION_AND");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR: return string_create_static("EXPRESSION_BINARY_OPERATION_OR");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL: return string_create_static("EXPRESSION_BINARY_OPERATION_EQUAL");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL: return string_create_static("EXPRESSION_BINARY_OPERATION_NOT_EQUAL");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS: return string_create_static("EXPRESSION_BINARY_OPERATION_LESS");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL: return string_create_static("EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER: return string_create_static("EXPRESSION_BINARY_OPERATION_GREATER");
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL: return string_create_static("EXPRESSION_BINARY_OPERATIONGREATER_OR_EQUAL");
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE: return string_create_static("EXPRESSION_UNARY_OPERATION_NEGATE");
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT: return string_create_static("EXPRESSION_UNARY_OPERATION_NOT");
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF: return string_create_static("EXPRESSION_UNARY_ADDRESS_OF");
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE: return string_create_static("EXPRESSION_UNARY_DEREFERENCE");
    case AST_Node_Type::UNDEFINED: return string_create_static("UNDEFINED");
    }
    panic("Should not happen");
    return string_create_static("SHOULD NOT FUCKING HAPPEN MOTHERFUCKER");
}

bool ast_node_type_is_identifier_node(AST_Node_Type type) {
    return type >= AST_Node_Type::IDENTIFIER_NAME && type <= AST_Node_Type::IDENTIFIER_PATH_TEMPLATED;
}
bool ast_node_type_is_binary_expression(AST_Node_Type type) {
    return type >= AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION && type <= AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL;
}
bool ast_node_type_is_unary_expression(AST_Node_Type type) {
    return type >= AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE && type <= AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
}
bool ast_node_type_is_expression(AST_Node_Type type) {
    return type >= AST_Node_Type::EXPRESSION_NEW && type <= AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE;
}
bool ast_node_type_is_statement(AST_Node_Type type) {
    return type >= AST_Node_Type::STATEMENT_BLOCK && type <= AST_Node_Type::STATEMENT_DELETE;
}
bool ast_node_type_is_type(AST_Node_Type type) {
    return type >= AST_Node_Type::FUNCTION_SIGNATURE && type <= AST_Node_Type::TYPE_SLICE;
}

void ast_node_identifer_or_path_append_to_string(AST_Node* node, String* string)
{
    string_append(string, node->id->characters);
    if (node->type == AST_Node_Type::IDENTIFIER_PATH) {
        string_append(string, "::");
        ast_node_identifer_or_path_append_to_string(node->child_start, string);
    }
}

void ast_node_expression_append_to_string(Code_Source* code_source, AST_Node* node, String* string);
void ast_node_arguments_append_to_string(Code_Source* code_source, AST_Node* node, String* string)
{
    string_append(string, "(");
    AST_Node* child = node->child_start;
    while (child != 0) {
        ast_node_expression_append_to_string(code_source, child, string);
        if (child->neighbor != 0) {
            string_append(string, ",");
        }
        child = child->neighbor;
    }
    string_append(string, ")");
}

void ast_node_expression_append_to_string(Code_Source* code_source, AST_Node* node, String* string)
{
    bool bin_op = false;
    bool unary_op = false;
    const char* bin_op_str = "asfd";
    switch (node->type)
    {
    case AST_Node_Type::EXPRESSION_LITERAL: {
        Token t = *node->literal_token;
        switch (t.type) {
        case Token_Type::BOOLEAN_LITERAL: string_append_formated(string, t.attribute.bool_value ? "TRUE" : "FALSE"); break;
        case Token_Type::INTEGER_LITERAL: string_append_formated(string, "%d", t.attribute.integer_value); break;
        case Token_Type::FLOAT_LITERAL: string_append_formated(string, "%3.2f", t.attribute.float_value); break;
        case Token_Type::STRING_LITERAL: string_append_formated(string, "\"%s\"", t.attribute.id->characters); break;
        }
        return;
    }
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL:
        ast_node_expression_append_to_string(code_source, node->child_start, string);
        ast_node_arguments_append_to_string(code_source, node->child_start->neighbor, string);
        return;
    case AST_Node_Type::EXPRESSION_VARIABLE_READ:
        ast_node_identifer_or_path_append_to_string(node->child_start, string);
        return;
    case AST_Node_Type::EXPRESSION_ARRAY_ACCESS:
        ast_node_expression_append_to_string(code_source, node->child_start, string);
        string_append_formated(string, "[");
        ast_node_expression_append_to_string(code_source, node->child_start->neighbor, string);
        string_append_formated(string, "]");
        return;
    case AST_Node_Type::EXPRESSION_MEMBER_ACCESS:
        ast_node_expression_append_to_string(code_source, node->child_start, string);
        string_append_formated(string, ".%s", node->id->characters);
        return;
    case AST_Node_Type::EXPRESSION_BAKE:
        string_append_formated(string, "#bake");
        return;
    case AST_Node_Type::EXPRESSION_CAST:
        string_append_formated(string, "cast(...)");
        ast_node_expression_append_to_string(code_source, node->child_count == 1 ? node->child_start : node->child_end, string);
        return;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION: bin_op = true, bin_op_str = "+"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION: bin_op = true, bin_op_str = "-"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION: bin_op = true, bin_op_str = "/"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION: bin_op = true, bin_op_str = "*"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO: bin_op = true, bin_op_str = "%"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND: bin_op = true, bin_op_str = "&&"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR: bin_op = true, bin_op_str = "||"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL: bin_op = true, bin_op_str = "=="; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL: bin_op = true, bin_op_str = "!="; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS: bin_op = true, bin_op_str = "<"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL: bin_op = true, bin_op_str = "<="; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER: bin_op = true, bin_op_str = ">"; break;
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL: bin_op = true, bin_op_str = ">="; break;
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE: unary_op = true, bin_op_str = "-"; break;
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT:unary_op = true, bin_op_str = "!"; break;
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF:unary_op = true, bin_op_str = "*"; break;
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE:unary_op = true, bin_op_str = "&"; break;
    default: return;
    }
    if (bin_op)
    {
        string_append_formated(string, "(");
        ast_node_expression_append_to_string(code_source, node->child_start, string);
        string_append_formated(string, " %s ", bin_op_str);
        ast_node_expression_append_to_string(code_source, node->child_start->neighbor, string);
        string_append_formated(string, ")");
        return;
    }
    else if (unary_op)
    {
        string_append_formated(string, bin_op_str);
        ast_node_expression_append_to_string(code_source, node->child_start, string);
    }
}

void ast_node_append_to_string(Code_Source* code_source, AST_Node* node, String* string, int indentation_lvl)
{
    for (int j = 0; j < indentation_lvl; j++) {
        string_append_formated(string, "  ");
    }
    string_append_formated(string, "#%d ", node->alloc_index);
    String type_str = ast_node_type_to_string(node->type);
    string_append_string(string, &type_str);
    if (ast_node_type_is_expression(node->type)) {
        string_append_formated(string, ": ");
        ast_node_expression_append_to_string(code_source, node, string);
        //string_append_formated(string, "\n");
    }
    if (code_source->tokens.size > 0) {
        /*
        int start_index = node->token_range.start_index;
        int end_index = node->token_range.end_index;
        if (end_index == code_source->tokens.size) {
            end_index = code_source->tokens.size - 1;
        }
        string_append_formated(string, " Line-Range: %d-%d, Character-Range: %d-%d ",
            code_source->tokens[start_index].position.start.line,
            code_source->tokens[end_index].position.end.line,
            code_source->tokens[start_index].position.start.character,
            code_source->tokens[end_index].position.end.character
        );
        */
    }
    string_append_formated(string, "\n");
    AST_Node* child = node->child_start;
    while (child != 0) {
        ast_node_append_to_string(code_source, child, string, indentation_lvl + 1);
        child = child->neighbor;
    }
}
