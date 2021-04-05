#include "ast_structure_test.hpp"

int ast_parser_get_next_node_index_no_parent(AST_Parser* parser, AST_Node_Type::ENUM node_type)
{
    while (parser->next_free_node >= parser->nodes.size) {
        AST_Node node;
        node.type = AST_Node_Type::UNDEFINED;
        node.children = dynamic_array_create_empty<AST_Node_Index>(2);
        node.parent = -1;
        dynamic_array_push_back(&parser->nodes, node);
    }

    AST_Node* node = &parser->nodes[parser->next_free_node];
    parser->next_free_node++;
    node->type = node_type;
    node->parent = -1;
    dynamic_array_reset(&node->children);
    return parser->next_free_node - 1;
}

int ast_parser_get_next_node_index(AST_Parser* parser, AST_Node_Index parent_index, AST_Node_Type::ENUM node_type)
{
    int index = ast_parser_get_next_node_index_no_parent(parser, node_type);
    AST_Node* node = &parser->nodes[index];
    node->parent = parent_index;
    if (parent_index != -1) {
        dynamic_array_push_back(&parser->nodes[parent_index].children, parser->next_free_node - 1);
    }
    return index;
}


AST_Parser_Checkpoint ast_parser_checkpoint_make(AST_Parser* parser, AST_Node_Index parent_index)
{
    AST_Parser_Checkpoint result;
    result.parser = parser;
    result.parent_index = parent_index;
    result.parent_child_count = parser->nodes.data[parent_index].children.size;
    result.rewind_token_index = parser->index;
    result.next_free_node_index = parser->next_free_node;
    return result;
}

void ast_parser_checkpoint_reset(AST_Parser_Checkpoint checkpoint) 
{
    checkpoint.parser->index = checkpoint.rewind_token_index;
    checkpoint.parser->next_free_node = checkpoint.next_free_node_index;
    if (checkpoint.parent_index != -1) { // This is the case if root
        DynamicArray<AST_Node_Index>* parent_childs = &checkpoint.parser->nodes.data[checkpoint.parent_index].children;
        dynamic_array_remove_range_ordered(parent_childs, checkpoint.parent_child_count, parent_childs->size); 
    }
}

bool ast_parser_test_next_token(AST_Parser* parser, Token_Type::ENUM type)
{
    if (parser->index >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type) {
        return true;
    }
    return false;
}

bool ast_parser_test_next_2_tokens(AST_Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2)
{
    if (parser->index + 1 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 && parser->lexer->tokens[parser->index + 1].type == type2) {
        return true;
    }
    return false;
}

bool ast_parser_test_next_3_tokens(AST_Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2, Token_Type::ENUM type3)
{
    if (parser->index + 2 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 &&
        parser->lexer->tokens[parser->index + 1].type == type2 &&
        parser->lexer->tokens[parser->index + 2].type == type3) {
        return true;
    }
    return false;
}

bool ast_parser_test_next_4_tokens(AST_Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2, Token_Type::ENUM type3,
    Token_Type::ENUM type4)
{
    if (parser->index + 3 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 &&
        parser->lexer->tokens[parser->index + 1].type == type2 &&
        parser->lexer->tokens[parser->index + 2].type == type3 &&
        parser->lexer->tokens[parser->index + 3].type == type4) 
    {
        return true;
    }
    return false;
}

bool ast_parser_test_next_5_tokens(AST_Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2, Token_Type::ENUM type3,
    Token_Type::ENUM type4, Token_Type::ENUM type5)
{
    if (parser->index + 4 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 &&
        parser->lexer->tokens[parser->index + 1].type == type2 &&
        parser->lexer->tokens[parser->index + 2].type == type3 &&
        parser->lexer->tokens[parser->index + 3].type == type4 &&
        parser->lexer->tokens[parser->index + 4].type == type5) {
        return true;
    }
    return false;
}

bool ast_parser_parse_expression(AST_Parser* parser, int parent_index);
bool ast_parser_parse_argument_block(AST_Parser* parser, AST_Node_Index parent_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    if (!ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) {
        return false;
    }
    parser->index++;

    // TODO: Better error handling
    while (!ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS))
    {
        if (!ast_parser_parse_expression(parser, parent_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
            parser->index++;
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
    return true;
}

bool ast_parser_parse_expression(AST_Parser* parser, AST_Node_Index parent_index);
AST_Node_Index ast_parser_parse_expression_single_value(AST_Parser* parser, AST_Node_Index parent_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    if (ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS))
    {
        parser->index++;
        if (ast_parser_parse_expression(parser, parent_index)) 
        {
            if (ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
                parser->index++;
                return parser->nodes[parent_index].children[parser->nodes[parent_index].children.size-1];
            }
        }
        ast_parser_checkpoint_reset(checkpoint);
        return -1;
    }

    int node_index = ast_parser_get_next_node_index_no_parent(parser, AST_Node_Type::EXPRESSION);
    AST_Node* expression = &parser->nodes[node_index];
    // Cases: Function Call, Variable read, Literal Value, Unary Operation
    if (ast_parser_test_next_token(parser, Token_Type::IDENTIFIER))
    {
        expression->expression_type = Expression_Type::VARIABLE_READ;
        expression->name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        parser->index++;
        if (ast_parser_parse_argument_block(parser, node_index)) { // Function call parameters
            expression->expression_type = Expression_Type::FUNCTION_CALL;
        }
        return node_index;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::INTEGER_LITERAL) ||
        ast_parser_test_next_token(parser, Token_Type::FLOAT_LITERAL) ||
        ast_parser_test_next_token(parser, Token_Type::BOOLEAN_LITERAL))
    {
        expression->expression_type = Expression_Type::LITERAL;
        parser->index++;
        return node_index;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::OP_MINUS))
    {
        expression->expression_type = Expression_Type::UNARY_OPERATION;
        expression->unary_op_type = Unary_Operation_Type::NEGATE;
        parser->index++;
        AST_Node_Index child_index = ast_parser_parse_expression_single_value(parser, node_index);
        if (child_index == -1) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        dynamic_array_push_back(&expression->children, child_index);
        parser->nodes[child_index].parent = node_index;
        return true;
    }
    else if (ast_parser_test_next_token(parser, Token_Type::LOGICAL_NOT))
    {
        expression->expression_type = Expression_Type::UNARY_OPERATION;
        expression->unary_op_type = Unary_Operation_Type::NOT;
        parser->index++;
        AST_Node_Index child_index = ast_parser_parse_expression_single_value(parser, node_index);
        if (child_index == -1) {
            ast_parser_checkpoint_reset(checkpoint);
            return -1;
        }
        dynamic_array_push_back(&expression->children, child_index);
        parser->nodes[child_index].parent = node_index;
        return true;
    }

    ast_parser_checkpoint_reset(checkpoint);
    return -1;
}

bool ast_parser_parse_binary_operation(AST_Parser* parser, Binary_Operation_Type::ENUM* op_type, int* op_priority)
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
    if (parser->index + 1 >= parser->lexer->tokens.size) return false;
    switch (parser->lexer->tokens[parser->index].type)
    {
    case Token_Type::LOGICAL_AND: {
        *op_type = Binary_Operation_Type::AND;
        *op_priority = 0;
        break;
    }
    case Token_Type::LOGICAL_OR: {
        *op_type = Binary_Operation_Type::OR;
        *op_priority = 1;
        break;
    }
    case Token_Type::COMPARISON_EQUAL: {
        *op_type = Binary_Operation_Type::EQUAL;
        *op_priority = 2;
        break;
    }
    case Token_Type::COMPARISON_NOT_EQUAL: {
        *op_type = Binary_Operation_Type::NOT_EQUAL;
        *op_priority = 2;
        break;
    }
    case Token_Type::COMPARISON_GREATER: {
        *op_type = Binary_Operation_Type::GREATER;
        *op_priority = 3;
        break;
    }
    case Token_Type::COMPARISON_GREATER_EQUAL: {
        *op_type = Binary_Operation_Type::GREATER_OR_EQUAL;
        *op_priority = 3;
        break;
    }
    case Token_Type::COMPARISON_LESS: {
        *op_type = Binary_Operation_Type::LESS;
        *op_priority = 3;
        break;
    }
    case Token_Type::COMPARISON_LESS_EQUAL: {
        *op_type = Binary_Operation_Type::LESS_OR_EQUAL;
        *op_priority = 3;
        break;
    }
    case Token_Type::OP_PLUS: {
        *op_type = Binary_Operation_Type::ADDITION;
        *op_priority = 4;
        break;
    }
    case Token_Type::OP_MINUS: {
        *op_type = Binary_Operation_Type::SUBTRACTION;
        *op_priority = 4;
        break;
    }
    case Token_Type::OP_STAR: {
        *op_type = Binary_Operation_Type::MULTIPLICATION;
        *op_priority = 5;
        break;
    }
    case Token_Type::OP_SLASH: {
        *op_type = Binary_Operation_Type::DIVISION;
        *op_priority = 5;
        break;
    }
    case Token_Type::OP_PERCENT: {
        *op_type = Binary_Operation_Type::MODULO;
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

int ast_parser_parse_expression_priority(AST_Parser* parser, AST_Node_Index node_index, int min_priority)
{
    int start_point = parser->index;
    int rewind_point = parser->index;

    bool first_run = true;
    int max_priority = 999;
    while (true)
    {
        AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parser->nodes[node_index].parent);

        int first_op_priority;
        Binary_Operation_Type::ENUM first_op_type;
        if (!ast_parser_parse_binary_operation(parser, &first_op_type, &first_op_priority)) {
            break;
        }
        if (first_op_priority < max_priority) {
            max_priority = first_op_priority;
        }
        if (first_op_priority < min_priority) {
            parser->index = rewind_point; // Undo the binary operation, maybe just do -1
            break;
        }

        AST_Node_Index operator_node = ast_parser_get_next_node_index_no_parent(parser, AST_Node_Type::EXPRESSION);
        AST_Node_Index right_operand_index = ast_parser_parse_expression_single_value(parser, operator_node);
        if (right_operand_index == -1) { 
            ast_parser_checkpoint_reset(checkpoint);
            break;
        }
        rewind_point = parser->index;

        int second_op_priority;
        Binary_Operation_Type::ENUM second_op_type;
        bool second_op_exists = ast_parser_parse_binary_operation(parser, &second_op_type, &second_op_priority);
        if (second_op_exists) 
        {
            parser->index--; 
            if (second_op_priority > max_priority) {
                right_operand_index = ast_parser_parse_expression_priority(parser, right_operand_index, second_op_priority);
            }
        }

        //
        parser->nodes[node_index].parent = operator_node;
        dynamic_array_push_back(&parser->nodes[operator_node].children, node_index);
        parser->nodes[right_operand_index].parent = operator_node;
        dynamic_array_push_back(&parser->nodes[operator_node].children, right_operand_index);
        parser->nodes[operator_node].type = AST_Node_Type::EXPRESSION;
        parser->nodes[operator_node].expression_type = Expression_Type::BINARY_OPERATION;
        parser->nodes[operator_node].binary_op_type = first_op_type;

        node_index = operator_node;
        if (!second_op_exists) break;
    }

    return node_index;
}

bool ast_parser_parse_expression(AST_Parser* parser, int parent_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int single_value_index = ast_parser_parse_expression_single_value(parser, parent_index);
    if (single_value_index == -1) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    AST_Node_Index op_tree_root_index = ast_parser_parse_expression_priority(parser, single_value_index, 0);
    dynamic_array_push_back(&parser->nodes[parent_index].children, op_tree_root_index);
    parser->nodes[op_tree_root_index].parent = parent_index;
    return true;
}

bool ast_parser_parse_statement_block(AST_Parser* parser, AST_Node_Index parent_index);
bool ast_parser_parse_statement(AST_Parser* parser, AST_Node_Index parent_index);

bool ast_parser_parse_single_statement_or_block(AST_Parser* parser, AST_Node_Index parent_index)
{
    if (ast_parser_parse_statement_block(parser, parent_index)) {
        return true;
    }

    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int node_index = ast_parser_get_next_node_index(parser, parent_index, AST_Node_Type::STATEMENT_BLOCK);
    if (!ast_parser_parse_statement(parser, node_index)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    return true;
}

// TODO: Compression-Oriented Programming do this better
bool ast_parser_parse_statement(AST_Parser* parser, AST_Node_Index parent_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int node_index = ast_parser_get_next_node_index(parser, parent_index, AST_Node_Type::STATEMENT);
    AST_Node* statement = &parser->nodes[node_index];

    if (ast_parser_parse_expression(parser, node_index)) 
    {
        statement->statement_type = Statement_Type::EXPRESSION;
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            return true;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    if (ast_parser_test_next_4_tokens(parser, Token_Type::IDENTIFIER, Token_Type::COLON, Token_Type::IDENTIFIER, Token_Type::SEMICOLON)) 
    {
        statement->statement_type = Statement_Type::VARIABLE_DEFINITION;
        statement->name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        statement->type_id = parser->lexer->tokens[parser->index + 2].attribute.identifier_number;
        parser->index += 4;
        return true;
    }

    if (ast_parser_test_next_4_tokens(parser, Token_Type::IDENTIFIER, Token_Type::COLON, Token_Type::IDENTIFIER, Token_Type::OP_ASSIGNMENT))
    {
        statement->statement_type = Statement_Type::VARIABLE_DEFINE_ASSIGN;
        statement->name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        statement->type_id = parser->lexer->tokens[parser->index + 2].attribute.identifier_number;
        parser->index += 4;
        // @IDEA Do something like ast_parse_try_parse(AST_Node_Type::Expression, Token_Type::SEMICOLON)
        if (!ast_parser_parse_expression(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            return true;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER, Token_Type::INFER_ASSIGN))
    {
        statement->statement_type = Statement_Type::VARIABLE_DEFINE_INFER;
        statement->name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        parser->index += 2;
        if (!ast_parser_parse_expression(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            return true;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    if (ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER, Token_Type::OP_ASSIGNMENT))
    {
        statement->statement_type = Statement_Type::VARIABLE_ASSIGNMENT;
        statement->name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        parser->index += 2;

        if (!ast_parser_parse_expression(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_test_next_token(parser, Token_Type::SEMICOLON)) {
            parser->index++;
            return true;
        }
        else {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    if (ast_parser_test_next_token(parser, Token_Type::IF))
    {
        parser->index++;
        statement->statement_type = Statement_Type::IF_BLOCK;
        if (!ast_parser_parse_expression(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (!ast_parser_parse_single_statement_or_block(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }

        if (ast_parser_test_next_token(parser, Token_Type::ELSE)) 
        {
            statement->statement_type = Statement_Type::IF_ELSE_BLOCK;
            parser->index++;
            if (!ast_parser_parse_single_statement_or_block(parser, node_index)) {
                ast_parser_checkpoint_reset(checkpoint);
                return false;
            }
        }
        return true;
    }

    if (ast_parser_test_next_token(parser, Token_Type::WHILE))
    {
        statement->statement_type = Statement_Type::WHILE;
        parser->index++;
        if (!ast_parser_parse_expression(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        if (ast_parser_parse_single_statement_or_block(parser, node_index)) {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        return true;
    }

    if (ast_parser_test_next_2_tokens(parser, Token_Type::BREAK, Token_Type::SEMICOLON)) 
    {
        statement->statement_type = Statement_Type::BREAK;
        parser->index += 2;
        return true;
    }

    if (ast_parser_test_next_2_tokens(parser, Token_Type::CONTINUE, Token_Type::SEMICOLON))
    {
        statement->statement_type = Statement_Type::CONTINUE;
        parser->index += 2;
        return true;
    }

    if (ast_parser_test_next_token(parser, Token_Type::RETURN)) // Currently returns REQUIRE an expression
    {
        statement->statement_type = Statement_Type::RETURN_STATEMENT;
        parser->index++;
        if (ast_parser_parse_expression(parser, node_index)) { 
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
        return true;
    }

    return false;
}

bool ast_parser_parse_statement_block(AST_Parser* parser, AST_Node_Index parent_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int node_index = ast_parser_get_next_node_index(parser, parent_index, AST_Node_Type::STATEMENT_BLOCK);
    AST_Node* block = &parser->nodes[node_index];

    if (!ast_parser_test_next_token(parser, Token_Type::OPEN_BRACES)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++;

    while (!ast_parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) 
    {
        if (!ast_parser_parse_statement(parser, node_index)) 
        {
            ast_parser_checkpoint_reset(checkpoint);
            return false;
        }
    }

    return true;
}

bool ast_parser_parse_parameter_block(AST_Parser* parser, AST_Node_Index parent_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int node_index = ast_parser_get_next_node_index(parser, parent_index, AST_Node_Type::PARAMETER_BLOCK);
    AST_Node* block = &parser->nodes[node_index];

    if (!ast_parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) {
        return false;
    }
    parser->index++;

    // TODO: Better error handling
    while (!ast_parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS))
    {
        if (ast_parser_test_next_4_tokens(parser, Token_Type::IDENTIFIER, Token_Type::COLON, Token_Type::IDENTIFIER, Token_Type::COMMA)) 
        {
            AST_Node_Index parameter_index = ast_parser_get_next_node_index(parser, node_index, AST_Node_Type::PARAMETER);
            AST_Node* node = &parser->nodes[parameter_index];
            node->name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
            node->type_id = parser->lexer->tokens[parser->index + 2].attribute.identifier_number;
            parser->index += 4;
            continue;
        }
        else if (ast_parser_test_next_4_tokens(parser, Token_Type::IDENTIFIER, Token_Type::COLON, Token_Type::IDENTIFIER, Token_Type::CLOSED_PARENTHESIS)) 
        {
            AST_Node_Index parameter_index = ast_parser_get_next_node_index(parser, parent_index, AST_Node_Type::PARAMETER);
            AST_Node* node = &parser->nodes[parameter_index];
            node->name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
            node->type_id = parser->lexer->tokens[parser->index + 2].attribute.identifier_number;
            parser->index += 3;
            break;
        }
        // Error
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    parser->index++; // Skip )

    return true;
}

bool ast_parser_parse_function(AST_Parser* parser, AST_Node_Index parent_index)
{
    AST_Parser_Checkpoint checkpoint = ast_parser_checkpoint_make(parser, parent_index);
    int node_index = ast_parser_get_next_node_index(parser, parent_index, AST_Node_Type::FUNCTION);
    AST_Node* function = &parser->nodes[node_index];

    // Parse Function start
    if (!ast_parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER, Token_Type::DOUBLE_COLON)) {
        return false;
    }
    function->name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
    parser->index += 2;

    // Parse paramenters
    if (!ast_parser_parse_parameter_block(parser, node_index)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }

    // Parse Return type
    if (!ast_parser_test_next_2_tokens(parser, Token_Type::ARROW, Token_Type::IDENTIFIER)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    }
    function->type_id = parser->lexer->tokens[parser->index + 1].attribute.identifier_number;
    parser->index += 2;

    // Parse statements
    if (!ast_parser_parse_statement_block(parser, node_index)) {
        ast_parser_checkpoint_reset(checkpoint);
        return false;
    };

    return true;
}

// @IDEA Something along those lines for error handling
void ast_parser_handle_function_header_parsing_error() {

}

// @IDEA Maybe i want one function that calls itself recursively, taking an AST_Node_Type
void ast_parser_parse_root(AST_Parser* parser)
{
    // TODO: Do error handling if this function parsing fails
    int root_index = ast_parser_get_next_node_index(parser, 0, AST_Node_Type::ROOT);
    AST_Node* root = &parser->nodes[root_index];
    while (ast_parser_parse_function(parser, root_index) && parser->index < parser->lexer->tokens.size) {}
}

AST_Parser ast_parser_parse(Lexer* lexer)
{
    AST_Parser parser;
    parser.index = 0;
    parser.lexer = lexer;
    parser.nodes = dynamic_array_create_empty<AST_Node>(1024);
    parser.token_mapping = dynamic_array_create_empty<Token_Range>(1024);
    parser.intermediate_errors = dynamic_array_create_empty<Parser_Error>(16);
    parser.unresolved_errors = dynamic_array_create_empty<Parser_Error>(16);
    parser.next_free_node = 0;

    ast_parser_parse_root(&parser);
    // Do something with the errors

    return parser;
}

void ast_parser_destroy(AST_Parser* parser)
{
    dynamic_array_destroy(&parser->intermediate_errors);
    dynamic_array_destroy(&parser->unresolved_errors);
    dynamic_array_destroy(&parser->token_mapping);
    dynamic_array_destroy(&parser->nodes);
}
