#include "compiler.hpp"

#include "../../datastructures/string.hpp"

String variable_type_to_string(Variable_Type::ENUM type)
{
    switch (type)
    {
    case Variable_Type::BOOLEAN:
        return string_create_static("BOOL");
    case Variable_Type::INTEGER:
        return string_create_static("INT");
    case Variable_Type::FLOAT:
        return string_create_static("FLOAT");
    case Variable_Type::ERROR_TYPE:
        return string_create_static("ERROR_TYPE");
    }
    return string_create_static("INVALID_VALUE_TYPE_ENUM");
}

int parser_find_next_token_type(Parser* parser, Token_Type::ENUM type)
{
    for (int i = parser->index; i < parser->lexer->tokens.size; i++) {
        if (parser->lexer->tokens[i].type == type) {
            return i;
        }
    }
    return parser->lexer->tokens.size;
}

void parser_log_intermediate_error(Parser* parser, const char* msg, int start_token, int end_token)
{
    ParserError error;
    error.error_message = msg;
    error.token_start_index = start_token;
    error.token_end_index = end_token;
    dynamic_array_push_back(&parser->intermediate_errors, error);
}

void parser_reset_intermediate_errors(Parser* parser) {
    dynamic_array_reset(&parser->intermediate_errors);
}

void parser_print_intermediate_errors(Parser* parser) {
    for (int i = 0; i < parser->intermediate_errors.size; i++) {
        ParserError* error = &parser->intermediate_errors.data[i];
        logg("Intermediate error #%d: %s\n", i, error->error_message);
    }
}

void parser_log_unresolvable_error(Parser* parser, const char* msg, int start_token, int end_token)
{
    ParserError error;
    error.error_message = msg;
    error.token_start_index = start_token;
    error.token_end_index = end_token;
    dynamic_array_push_back(&parser->unresolved_errors, error);
}

bool parser_test_next_token(Parser* parser, Token_Type::ENUM type)
{
    if (parser->index >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type) {
        return true;
    }
    return false;
}

bool parser_test_next_2_tokens(Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2)
{
    if (parser->index + 1 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 && parser->lexer->tokens[parser->index + 1].type == type2) {
        return true;
    }
    return false;
}

bool parser_test_next_3_tokens(Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2, Token_Type::ENUM type3)
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

bool parser_test_next_4_tokens(Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2, Token_Type::ENUM type3, Token_Type::ENUM type4)
{
    if (parser->index + 3 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 &&
        parser->lexer->tokens[parser->index + 1].type == type2 &&
        parser->lexer->tokens[parser->index + 2].type == type3 &&
        parser->lexer->tokens[parser->index + 3].type == type4) {
        return true;
    }
    return false;
}





/*
    AST
*/
void ast_node_expression_destroy(Ast_Node_Expression* expression)
{
    if (expression->free_symbol_table_on_destroy) {
        symbol_table_destroy(expression->symbol_table);
    }
    if (expression->type == ExpressionType::OP_LOGICAL_NOT ||
        expression->type == ExpressionType::OP_NEGATE)
    {
        if (expression->left != 0) {
            ast_node_expression_destroy(expression->left);
            delete expression->left;
        }
    }
    // Binary operations
    if (expression->type == ExpressionType::OP_ADD ||
        expression->type == ExpressionType::OP_SUBTRACT ||
        expression->type == ExpressionType::OP_DIVIDE ||
        expression->type == ExpressionType::OP_MULTIPLY ||
        expression->type == ExpressionType::OP_MODULO ||
        expression->type == ExpressionType::OP_BOOLEAN_AND ||
        expression->type == ExpressionType::OP_BOOLEAN_OR ||
        expression->type == ExpressionType::OP_EQUAL ||
        expression->type == ExpressionType::OP_NOT_EQUAL ||
        expression->type == ExpressionType::OP_GREATER_EQUAL ||
        expression->type == ExpressionType::OP_GREATER_THAN ||
        expression->type == ExpressionType::OP_LESS_EQUAL ||
        expression->type == ExpressionType::OP_LESS_THAN) 
    {
        if (expression->left != 0) {
            ast_node_expression_destroy(expression->left);
            delete expression->left;
        }
        if (expression->right != 0) {
            ast_node_expression_destroy(expression->right);
            delete expression->right;
        }
    }

    if (expression->type == ExpressionType::FUNCTION_CALL) {
        for (int i = 0; i < expression->arguments.size; i++) {
            ast_node_expression_destroy(&expression->arguments[i]);
        }
        dynamic_array_destroy(&expression->arguments);
    }
}

void ast_node_statement_block_destroy(Ast_Node_Statement_Block* block);
void ast_node_statement_destroy(Ast_Node_Statement* statement) 
{
    if (statement->free_symbol_table_on_destroy) {
        symbol_table_destroy(statement->symbol_table);
    }
    if (statement->type == StatementType::VARIABLE_ASSIGNMENT ||
        statement->type == StatementType::IF_BLOCK ||
        statement->type == StatementType::IF_ELSE_BLOCK ||
        statement->type == StatementType::WHILE ||
        statement->type == StatementType::RETURN_STATEMENT ||
        statement->type == StatementType::EXPRESSION ||
        statement->type == StatementType::VARIABLE_DEFINE_ASSIGN ||
        statement->type == StatementType::VARIABLE_DEFINE_INFER) 
    {
        ast_node_expression_destroy(&statement->expression);
    }
    if (statement->type == StatementType::IF_BLOCK ||
        statement->type == StatementType::IF_ELSE_BLOCK ||
        statement->type == StatementType::STATEMENT_BLOCK ||
        statement->type == StatementType::WHILE) 
    {
        ast_node_statement_block_destroy(&statement->statements);
    }
    if (statement->type == StatementType::IF_ELSE_BLOCK) {
        ast_node_statement_block_destroy(&statement->else_statements);
    }
}

void ast_node_statement_block_destroy(Ast_Node_Statement_Block* block) {
    if (block->free_symbol_table_on_destroy) {
        symbol_table_destroy(block->symbol_table);
    }
    for (int i = 0; i < block->statements.size; i++) {
        Ast_Node_Statement* statement = &block->statements.data[i];
        ast_node_statement_destroy(statement);
    }
    dynamic_array_destroy(&block->statements);
}

void ast_node_function_destroy(Ast_Node_Function* function)
{
    if (function->free_symbol_table_on_destroy) {
        symbol_table_destroy(function->symbol_table);
    }
    dynamic_array_destroy(&function->parameters);
    ast_node_statement_block_destroy(&function->body);
}

void ast_node_root_destroy(Ast_Node_Root* root)
{
    if (root->free_symbol_table_on_destroy) {
        symbol_table_destroy(root->symbol_table);
    }
    for (int i = 0; i < root->functions.size; i++)
    {
        Ast_Node_Function* function = &root->functions.data[i];
        ast_node_function_destroy(function);
    }
    dynamic_array_destroy(&root->functions);
}

void ast_node_expression_append_to_string(String* string, Ast_Node_Expression* expression, Lexer* lexer)
{
    if (expression->type == ExpressionType::OP_ADD) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " + ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_NEGATE) {
        string_append(string, "-");
        ast_node_expression_append_to_string(string, expression->left, lexer);
    }
    if (expression->type == ExpressionType::OP_LOGICAL_NOT) {
        string_append(string, "!");
        ast_node_expression_append_to_string(string, expression->left, lexer);
    }
    if (expression->type == ExpressionType::OP_MULTIPLY) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " * ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_SUBTRACT) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " - ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_DIVIDE) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " / ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_MODULO) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " % ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_BOOLEAN_AND) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " && ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_BOOLEAN_OR) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " || ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_EQUAL) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " || ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_NOT_EQUAL) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " || ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_LESS_THAN) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " < ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_LESS_EQUAL) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " <= ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_GREATER_THAN) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " > ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_GREATER_EQUAL) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " >= ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    else if (expression->type == ExpressionType::LITERAL) {
        Token& t = lexer->tokens[expression->literal_token_index];
        if (t.type == Token_Type::INTEGER_LITERAL) {
            string_append_formated(string, "%d", lexer->tokens[expression->literal_token_index].attribute.integer_value);
        }
        else if (t.type == Token_Type::FLOAT_LITERAL) {
            string_append_formated(string, "%f", lexer->tokens[expression->literal_token_index].attribute.float_value);
        }
        else if (t.type == Token_Type::BOOLEAN_LITERAL) {
            string_append_formated(string, "%s",
                lexer->tokens[expression->literal_token_index].attribute.bool_value ? "true" : "false");
        }
    }
    else if (expression->type == ExpressionType::VARIABLE_READ) {
        string_append_formated(string, "%s", lexer->identifiers[expression->variable_name_id].characters);
    }
    else if (expression->type == ExpressionType::FUNCTION_CALL) {
        string_append_formated(string, "%s(", lexer->identifiers[expression->variable_name_id].characters);
        for (int i = 0; i < expression->arguments.size; i++) {
            ast_node_expression_append_to_string(string, &expression->arguments[i], lexer);
            if (i != expression->arguments.size-1)
                string_append_formated(string, ", ");
        }
        string_append_formated(string, ")");
    }
}

void ast_node_statement_append_to_string(String* string, Ast_Node_Statement* statement, Lexer* lexer, int indentation_level);
void ast_node_statement_block_append_to_string(String* string, Ast_Node_Statement_Block* block, Lexer* lexer, int current_indentation_lvl)
{
    for (int i = 0; i < current_indentation_lvl; i++) {
        string_append_formated(string, "    ");
    }
   string_append_formated(string, "{\n");
   for (int i = 0; i < block->statements.size; i++) {
       Ast_Node_Statement* statement = &block->statements.data[i];
       for (int i = 0; i < current_indentation_lvl+1; i++) {
           string_append_formated(string, "    ");
       }
       ast_node_statement_append_to_string(string, statement, lexer, current_indentation_lvl+1);
       string_append_formated(string, "\n");
   }
   for (int i = 0; i < current_indentation_lvl; i++) {
       string_append_formated(string, "    ");
   }
   string_append_formated(string, "}");
}

void ast_node_statement_append_to_string(String* string, Ast_Node_Statement* statement, Lexer* lexer, int indentation_level)
{
    if (statement->type == StatementType::VARIABLE_DEFINITION) {
        string_append_formated(string, "%s : %s;",
            lexer->identifiers[statement->variable_name_id].characters,
            lexer->identifiers[statement->variable_type_id].characters);
    }
    else if (statement->type == StatementType::VARIABLE_ASSIGNMENT) {
        string_append_formated(string, "%s = ", lexer->identifiers[statement->variable_name_id].characters);
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append(string, ";");
    }
    else if (statement->type == StatementType::RETURN_STATEMENT) {
        string_append_formated(string, "return ");
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append_formated(string, ";");
    }
    else if (statement->type == StatementType::VARIABLE_DEFINE_ASSIGN) {
        string_append_formated(string, "%s : %s = ",
            lexer->identifiers[statement->variable_name_id].characters,
            lexer->identifiers[statement->variable_type_id].characters);
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append_formated(string, ";");
    }
    else if (statement->type == StatementType::VARIABLE_DEFINE_INFER) {
        string_append_formated(string, "%s := ",
            lexer->identifiers[statement->variable_name_id].characters);
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append_formated(string, ";");
    }
    else if (statement->type == StatementType::EXPRESSION) {
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append_formated(string, ";");
    }
    else if (statement->type == StatementType::CONTINUE) {
        string_append_formated(string, "continue;");
    }
    else if (statement->type == StatementType::BREAK) {
        string_append_formated(string, "break;");
    }
    else if (statement->type == StatementType::STATEMENT_BLOCK) {
        ast_node_statement_block_append_to_string(string, &statement->statements, lexer, indentation_level);
    }
    else if (statement->type == StatementType::WHILE)
    {
        string_append_formated(string, "while ");
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append_formated(string, "\n");
        ast_node_statement_block_append_to_string(string, &statement->statements, lexer, indentation_level);
    }
    else if (statement->type == StatementType::IF_BLOCK || statement->type == StatementType::IF_ELSE_BLOCK) 
    {
        // Print if block
        string_append_formated(string, "if ");
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append_formated(string, "\n");
        ast_node_statement_block_append_to_string(string, &statement->statements, lexer, indentation_level);

        if (statement->type == StatementType::IF_ELSE_BLOCK) {
            string_append_formated(string, "else\n");
            ast_node_statement_block_append_to_string(string, &statement->statements, lexer, indentation_level);
        }
    }
}

void ast_node_function_append_to_string(String* string, Ast_Node_Function* function, Lexer* lexer)
{
    string_append_formated(string, "%s :: (", lexer->identifiers[function->function_name_id].characters);
    for (int i = 0; i < function->parameters.size; i++) {
        Ast_Node_Function_Parameter* param = &(function->parameters.data[i]);
        string_append_formated(string, "%s : %s, ",
            lexer->identifiers[param->name_id].characters,
            lexer->identifiers[param->type_id].characters);
    }
    string_append_formated(string, ") -> %s\n", lexer->identifiers[function->return_type_id].characters);
    ast_node_statement_block_append_to_string(string, &function->body, lexer, 0);
}

void ast_node_root_append_to_string(String* string, Ast_Node_Root* root, Lexer* lexer)
{
    string_append_formated(string, "\nRoot: (Function count #%d)\n", root->functions.size);
    for (int i = 0; i < root->functions.size; i++)
    {
        Ast_Node_Function* function = &(root->functions.data[i]);
        ast_node_function_append_to_string(string, function, lexer);
        string_append_formated(string, "\n");
    }
}



/*
    PARSER
*/
bool parser_parse_expression(Parser* parser, Ast_Node_Expression* expression);

bool parser_parse_expression_single_value(Parser* parser, Ast_Node_Expression* expression)
{
    int rewind_point = parser->index;
    expression->left = 0;
    expression->right = 0;
    if (parser_test_next_token(parser, Token_Type::IDENTIFIER))
    {
        expression->variable_name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        parser->index++;
        if (parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) 
        { 
            parser->index++;
            expression->type = ExpressionType::FUNCTION_CALL;
            expression->arguments = dynamic_array_create_empty<Ast_Node_Expression>(4);
            while (true)
            {
                Ast_Node_Expression argument;
                if (!parser_parse_expression(parser, &argument)) {
                    break;
                }
                dynamic_array_push_back(&expression->arguments, argument);
                if (!parser_test_next_token(parser, Token_Type::COMMA)) {
                    break;
                }
                parser->index++;
            }
            // Here we could check last token for , and error if this is the case, but it does not really matter
            if (parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
                parser->index++;
                return true;
            }
            // Error, no end of function
            for (int i = 0; i < expression->arguments.size; i++) {
                ast_node_expression_destroy(&expression->arguments[i]);
            }
            dynamic_array_destroy(&expression->arguments);
            parser->index = rewind_point;
            return false;
        }
        else {
            expression->type = ExpressionType::VARIABLE_READ;
        }
        return true;
    }
    else if (parser_test_next_token(parser, Token_Type::OP_MINUS))
    {
        expression->type = ExpressionType::OP_NEGATE;
        parser->index++;
        expression->left = new Ast_Node_Expression();
        if (!parser_parse_expression_single_value(parser, expression->left)) {
            delete expression->left;
            parser->index--;
            return false;
        }
        return true;
    }
    else if (parser_test_next_token(parser, Token_Type::LOGICAL_NOT))
    {
        expression->type = ExpressionType::OP_LOGICAL_NOT;
        parser->index++;
        expression->left = new Ast_Node_Expression();
        if (!parser_parse_expression_single_value(parser, expression->left)) {
            delete expression->left;
            parser->index--;
            return false;
        }
        return true;
    }
    else if (parser_test_next_token(parser, Token_Type::INTEGER_LITERAL) ||
        parser_test_next_token(parser, Token_Type::FLOAT_LITERAL) ||
        parser_test_next_token(parser, Token_Type::BOOLEAN_LITERAL))
    {
        expression->type = ExpressionType::LITERAL;
        expression->literal_token_index = parser->index;
        parser->index++;
        return true;
    }
    else if (parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS))
    {
        int rewind_point = parser->index;
        parser->index++;
        if (parser_parse_expression(parser, expression)) {
            if (parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
                parser->index++;
                return true;
            }
            else {
                ast_node_expression_destroy(expression);
                parser->index = rewind_point;
                return false;
            }
        }
        parser->index = rewind_point;
        return false;
    }
    else {
        parser_log_intermediate_error(parser,
            "Error, could not parse single expression, does not start with constant or identifier\n", parser->index, parser->index + 1);
        return false;
    }
}

bool parser_parse_binary_operation(Parser* parser, ExpressionType::ENUM* op_type, int* op_priority)
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
    if (parser_test_next_token(parser, Token_Type::OP_PLUS)) {
        *op_type = ExpressionType::OP_ADD;
        *op_priority = 4;
    }
    else if (parser_test_next_token(parser, Token_Type::OP_MINUS)) {
        *op_type = ExpressionType::OP_SUBTRACT;
        *op_priority = 4;
    }
    else if (parser_test_next_token(parser, Token_Type::OP_SLASH)) {
        *op_type = ExpressionType::OP_DIVIDE;
        *op_priority = 5;
    }
    else if (parser_test_next_token(parser, Token_Type::OP_STAR)) {
        *op_type = ExpressionType::OP_MULTIPLY;
        *op_priority = 5;
    }
    else if (parser_test_next_token(parser, Token_Type::OP_PERCENT)) {
        *op_type = ExpressionType::OP_MODULO;
        *op_priority = 6;
    }
    else if (parser_test_next_token(parser, Token_Type::LOGICAL_AND)) {
        *op_type = ExpressionType::OP_BOOLEAN_AND;
        *op_priority = 0;
    }
    else if (parser_test_next_token(parser, Token_Type::LOGICAL_OR)) {
        *op_type = ExpressionType::OP_BOOLEAN_OR;
        *op_priority = 1;
    }
    else if (parser_test_next_token(parser, Token_Type::COMPARISON_EQUAL)) {
        *op_type = ExpressionType::OP_EQUAL;
        *op_priority = 2;
    }
    else if (parser_test_next_token(parser, Token_Type::COMPARISON_NOT_EQUAL)) {
        *op_type = ExpressionType::OP_NOT_EQUAL;
        *op_priority = 2;
    }
    else if (parser_test_next_token(parser, Token_Type::COMPARISON_GREATER)) {
        *op_type = ExpressionType::OP_GREATER_THAN;
        *op_priority = 3;
    }
    else if (parser_test_next_token(parser, Token_Type::COMPARISON_GREATER_EQUAL)) {
        *op_type = ExpressionType::OP_GREATER_EQUAL;
        *op_priority = 3;
    }
    else if (parser_test_next_token(parser, Token_Type::COMPARISON_LESS)) {
        *op_type = ExpressionType::OP_LESS_THAN;
        *op_priority = 3;
    }
    else if (parser_test_next_token(parser, Token_Type::COMPARISON_LESS_EQUAL)) {
        *op_type = ExpressionType::OP_LESS_EQUAL;
        *op_priority = 3;
    }
    else {
        return false;
    }
    parser->index++;
    return true;
}

// Input expression is not empty
bool parser_parse_expression_new_priority(Parser* parser, Ast_Node_Expression* expression, int min_priority)
{
    int start_point = parser->index;
    int rewind_point = parser->index;

    bool first_run = true;
    int first_run_priority = -1;
    // Parse expression start operand
    while (true)
    {
        int first_op_priority;
        ExpressionType::ENUM first_op_type;
        if (!parser_parse_binary_operation(parser, &first_op_type, &first_op_priority)) {
            break;
        }
        if (first_run) {
            first_run = false;
            first_run_priority = first_op_priority;
        }
        else {
            if (first_op_priority < first_run_priority) {
                first_run_priority = first_op_priority;
            }
            if (first_op_priority < min_priority) {
                parser->index = rewind_point;
                break;
            }
        }

        Ast_Node_Expression right_operand;
        if (!parser_parse_expression_single_value(parser, &right_operand)) {
            parser->index = rewind_point;
            break;
        }
        rewind_point = parser->index;

        int second_op_priority;
        ExpressionType::ENUM second_op_type;
        bool second_op_exists = parser_parse_binary_operation(parser, &second_op_type, &second_op_priority);
        if (second_op_exists) {
            parser->index--;
            if (second_op_priority > first_run_priority) {
                parser_parse_expression_new_priority(parser, &right_operand, second_op_priority);
            }
        }
        Ast_Node_Expression* new_left = new Ast_Node_Expression();
        Ast_Node_Expression* new_right = new Ast_Node_Expression();
        *new_right = right_operand;
        *new_left = *expression;
        expression->type = first_op_type;
        expression->left = new_left;
        expression->right = new_right;
        if (!second_op_exists) break;
    }

    return parser->index != start_point;
}

bool parser_parse_expression(Parser* parser, Ast_Node_Expression* expression)
{
    expression->symbol_table = 0;
    expression->free_symbol_table_on_destroy = false;
    if (!parser_parse_expression_single_value(parser, expression)) {
        return false;
    }
    parser_parse_expression_new_priority(parser, expression, 0);
    return true;
}

bool parser_parse_statment_block_or_single_statement(Parser* parser, Ast_Node_Statement_Block* block);
bool parser_parse_statement_block(Parser* parser, Ast_Node_Statement_Block* block);
bool parser_parse_statement(Parser* parser, Ast_Node_Statement* statement)
{
    statement->symbol_table = 0;
    statement->free_symbol_table_on_destroy = false;
    int rewind_point = parser->index;

    if (parser_parse_statement_block(parser, &statement->statements)) {
        statement->type = StatementType::STATEMENT_BLOCK;
        return true;
    }

    bool valid_statement = false;
    if (!valid_statement && parser_test_next_token(parser, Token_Type::IF))
    {
        parser->index++;
        if (!parser_parse_expression(parser, &statement->expression)) {
            parser->index = rewind_point;
            return false;
        }
        if (!parser_parse_statment_block_or_single_statement(parser, &statement->statements)) {
            parser->index = rewind_point;
            return false;
        }
        statement->type = StatementType::IF_BLOCK;
        rewind_point = parser->index;

        // Test else part
        if (parser_test_next_token(parser, Token_Type::ELSE)) {
            parser->index++;
            if (!parser_parse_statment_block_or_single_statement(parser, &statement->else_statements)) {
                parser->index = rewind_point;
                return true;
            }
            statement->type = StatementType::IF_ELSE_BLOCK;
            return true;
        }
        return true;
    }

    if (!valid_statement && parser_test_next_token(parser, Token_Type::WHILE))
    {
        parser->index++;
        if (!parser_parse_expression(parser, &statement->expression)) {
            parser->index = rewind_point;
            return false;
        }
        if (!parser_parse_statment_block_or_single_statement(parser, &statement->statements)) {
            parser->index = rewind_point;
            return false;
        }
        statement->type = StatementType::WHILE;
        return true;
    }

    if (!valid_statement && parser_test_next_token(parser, Token_Type::BREAK))
    {
        statement->type = StatementType::BREAK;
        parser->index++;
        valid_statement = true;
    }

    if (!valid_statement && parser_test_next_token(parser, Token_Type::CONTINUE))
    {
        statement->type = StatementType::CONTINUE;
        parser->index++;
        valid_statement = true;
    }

    if (!valid_statement && parser_test_next_token(parser, Token_Type::RETURN))
    {
        parser->index++;
        Ast_Node_Expression expression;
        if (!parser_parse_expression(parser, &expression)) { // Return may also be fine if the function does not return anything
            parser->index = rewind_point;
            return false;
        }
        statement->type = StatementType::RETURN_STATEMENT;
        statement->expression = expression;
        valid_statement = true;
    }

    if (!valid_statement && parser_test_next_4_tokens(parser,
        Token_Type::IDENTIFIER, Token_Type::COLON, Token_Type::IDENTIFIER, Token_Type::SEMICOLON)) // Variable definition 'x : int;'
    {
        statement->type = StatementType::VARIABLE_DEFINITION;
        statement->variable_name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        statement->variable_type_id = parser->lexer->tokens[parser->index + 2].attribute.identifier_number;
        parser->index += 3; // ! not 4, since the ; parsing is done by the caller of this function
        valid_statement = true;
    }

    if (!valid_statement && parser_test_next_4_tokens(parser,
        Token_Type::IDENTIFIER, Token_Type::COLON, Token_Type::IDENTIFIER, Token_Type::OP_ASSIGNMENT)) // Variable define-assign 'x : int = ...'
    {
        statement->type = StatementType::VARIABLE_DEFINE_ASSIGN;
        statement->variable_name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        statement->variable_type_id = parser->lexer->tokens[parser->index + 2].attribute.identifier_number;
        parser->index += 4;
        Ast_Node_Expression expr;
        if (!parser_parse_expression(parser, &expr)) {
            parser->index = rewind_point;
            return false;
        }
        statement->expression = expr;
        valid_statement = true;
    }

    if (!valid_statement &&
        parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER, Token_Type::INFER_ASSIGN)) // Variable define-assign 'x :='
    {
        statement->type = StatementType::VARIABLE_DEFINE_INFER;
        statement->variable_name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        parser->index += 2;
        Ast_Node_Expression expr;
        if (!parser_parse_expression(parser, &expr)) {
            parser->index = rewind_point;
            return false;
        }
        statement->expression = expr;
        valid_statement = true;
    }

    if (!valid_statement && parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER, Token_Type::OP_ASSIGNMENT))
    {
        // Assignment
        statement->type = StatementType::VARIABLE_ASSIGNMENT;
        statement->variable_name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        parser->index += 2;

        // Parse expression
        if (!parser_parse_expression(parser, &statement->expression)) {
            parser->index = rewind_point;
            return false;
        }
        valid_statement = true;
    }

    if (!valid_statement)
    {
        if (parser_parse_expression(parser, &statement->expression)) {
            statement->type = StatementType::EXPRESSION;
            valid_statement = true;
        }
    }

    if (!valid_statement) {
        return false;
    }
    if (parser_test_next_token(parser, Token_Type::SEMICOLON)) {
        parser->index++;
        return true;
    }
    else {
        ast_node_statement_destroy(statement);
        parser->index = rewind_point;
        return false;
    }
}

// Returns true if there is a statement block, false if not
bool parser_parse_statement_block(Parser* parser, Ast_Node_Statement_Block* block)
{
    block->symbol_table = 0;
    block->free_symbol_table_on_destroy = false;
    int scope_start = parser->index;
    int rewind_index = parser->index;
    if (!parser_test_next_token(parser, Token_Type::OPEN_BRACES)) {
        return false;
    }
    parser->index++;

    bool exit_failure = false;
    block->statements = dynamic_array_create_empty<Ast_Node_Statement>(16);
    SCOPE_EXIT(if (exit_failure) ast_node_statement_block_destroy(block););
    Ast_Node_Statement statement;
    while (parser->index < parser->lexer->tokens.size)
    {
        if (parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
            parser->index++;
            return true;
        }

        rewind_index = parser->index;
        if (parser_parse_statement(parser, &statement)) {
            dynamic_array_push_back(&block->statements, statement);
        }
        else
        {
            // Error recovery, go after next ; or }
            int next_semicolon = parser_find_next_token_type(parser, Token_Type::SEMICOLON);
            int next_braces = parser_find_next_token_type(parser, Token_Type::CLOSED_BRACES);
            if (next_semicolon >= parser->lexer->tokens.size || next_braces >= parser->lexer->tokens.size) {
                parser_log_intermediate_error(parser, "Scope block does not end with } or;\n", parser->index, next_semicolon);
                exit_failure = true;
                return false;
            }
            if (next_semicolon < next_braces) {
                parser_log_unresolvable_error(parser, "Could not parse statement, skipped it\n", parser->index, next_semicolon);
                parser->index = next_semicolon + 1;
            }
            else {
                parser_log_unresolvable_error(parser, "Could not parse statement, skipped it\n", parser->index, next_braces);
                parser->index = next_braces + 1;
                return true;
            }
        }
    }
    parser_log_unresolvable_error(parser, "Scope block does not end with }\n", scope_start, parser->lexer->tokens.size - 1);
    exit_failure = true;
    return false;
}

// Different to parse_block, this function does not need {} around the statements
bool parser_parse_statment_block_or_single_statement(Parser* parser, Ast_Node_Statement_Block* block)
{
    if (parser_parse_statement_block(parser, block)) {
        return true;
    }
    Ast_Node_Statement statement;
    if (!parser_parse_statement(parser, &statement)) {
        return false;
    }
    block->statements = dynamic_array_create_empty<Ast_Node_Statement>(1);
    dynamic_array_push_back(&block->statements, statement);
    return true;
}

bool parser_parse_function(Parser* parser, Ast_Node_Function* function)
{
    function->symbol_table = 0;
    function->free_symbol_table_on_destroy = false;
    int rewind_point = parser->index;
    function->parameters = dynamic_array_create_empty<Ast_Node_Function_Parameter>(8);
    bool exit_failure = false;
    SCOPE_EXIT(
        if (exit_failure) {
            dynamic_array_destroy(&function->parameters);
            parser->index = rewind_point;
        }
    );

    // Parse Function start
    if (!parser_test_next_3_tokens(parser, Token_Type::IDENTIFIER, Token_Type::DOUBLE_COLON, Token_Type::OPEN_PARENTHESIS)) {
        parser_log_intermediate_error(parser, "Could not parse function, it did not start with 'ID :: ('", parser->index, parser->index + 3);
        exit_failure = true;
        return false;
    }
    function->function_name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
    parser->index += 3;

    // Parse Parameters
    while (!parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS))
    {
        // Parameters need to be named, meaning x : int     
        if (!parser_test_next_3_tokens(parser, Token_Type::IDENTIFIER, Token_Type::COLON, Token_Type::IDENTIFIER)) {
            parser_log_intermediate_error(parser, "Could not parse function, parameter was not in the form ID : TYPE", parser->index, parser->index + 3);
            exit_failure = true;
            return false;
        }

        Ast_Node_Function_Parameter param;
        param.name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        param.type_id = parser->lexer->tokens[parser->index + 2].attribute.identifier_number;
        dynamic_array_push_back(&function->parameters, param);
        parser->index += 3;

        // Check for ) or ,
        if (parser_test_next_token(parser, Token_Type::COMMA)) {
            parser->index++;
        }
    }
    parser->index++; // Skip )

    // Parse Return type
    if (!parser_test_next_2_tokens(parser, Token_Type::ARROW, Token_Type::IDENTIFIER)) {
        parser_log_intermediate_error(parser, "Could not parse function, did not find return type after Parameters '-> TYPE'",
            parser->index, parser->index + 2);
        exit_failure = true;
        return false;
    }
    function->return_type_id = parser->lexer->tokens[parser->index + 1].attribute.identifier_number;
    parser->index += 2;

    // Parse statements
    if (!parser_parse_statement_block(parser, &function->body)) {
        exit_failure = true;
        return false;
    };
    SCOPE_EXIT(if (exit_failure) ast_node_statement_block_destroy(&function->body););

    return true;
}


bool parser_parse_root(Parser* parser, Ast_Node_Root* root)
{
    root->functions = dynamic_array_create_empty<Ast_Node_Function>(32);
    root->symbol_table = 0;
    root->free_symbol_table_on_destroy = false;

    Ast_Node_Function function;
    while (true)
    {
        if (parser_parse_function(parser, &function)) {
            dynamic_array_push_back(&root->functions, function);
        }
        else if (parser->index >= parser->lexer->tokens.size) {
            break;
        }
        else {
            // Skip to next token in next line, then try parsing again
            int next_line_token = parser->index;
            while (next_line_token < parser->lexer->tokens.size &&
                parser->lexer->tokens[next_line_token].line_number == parser->lexer->tokens[parser->index].line_number)
            {
                next_line_token++;
            }
            if (next_line_token >= parser->lexer->tokens.size) {
                parser_log_unresolvable_error(parser, "Could not parse last function in file!\n", 
                    parser->index, parser->lexer->tokens.size - 1);
                break;
            }
            else {
                // Skip to next line token, try parsing funciton again
                parser_log_unresolvable_error(parser, "Could not parse function header!\n", parser->index, next_line_token - 1);
                parser->index = next_line_token;
            }
        }
    }

    return true;
}

Parser parser_parse(Lexer* lexer)
{
    Parser parser;
    parser.index = 0;
    parser.lexer = lexer;
    parser.intermediate_errors = dynamic_array_create_empty<ParserError>(16);
    parser.unresolved_errors = dynamic_array_create_empty<ParserError>(16);
    parser.semantic_analysis_errors = dynamic_array_create_empty<const char*>(16);

    // Parse root
    if (!parser_parse_root(&parser, &parser.root)) {
        logg("Dont quite know what to do herer lol\n");
    }

    // Do semantic checking
    parser_semantic_analysis(&parser);

    return parser;
}

void parser_destroy(Parser* parser)
{
    dynamic_array_destroy(&parser->intermediate_errors);
    dynamic_array_destroy(&parser->unresolved_errors);
    dynamic_array_destroy(&parser->semantic_analysis_errors);
    ast_node_root_destroy(&parser->root);
}

void parser_report_semantic_analysis_error(Parser* parser, const char* msg) {
    dynamic_array_push_back(&parser->semantic_analysis_errors, msg);
}

SymbolTable symbol_table_create(SymbolTable* parent)
{
    SymbolTable result;
    result.parent = parent;
    result.symbols = dynamic_array_create_empty<Symbol>(8);
    return result;
}

SymbolTable* symbol_table_create_new(SymbolTable* parent) {
    SymbolTable* res = new SymbolTable();
    *res = symbol_table_create(parent);
    return res;
}

void symbol_table_destroy(SymbolTable* table) {
    dynamic_array_destroy(&table->symbols);
}

Symbol* symbol_table_find_symbol(SymbolTable* table, int name, bool* in_current_scope) 
{
    *in_current_scope = false;
    for (int i = 0; i < table->symbols.size; i++) {
        if (table->symbols[i].name == name) {
            *in_current_scope = true;
            return &table->symbols[i];
        }
    }
    if (table->parent != 0) {
        Symbol* result = symbol_table_find_symbol(table->parent, name, in_current_scope);
        *in_current_scope = false;
        return result;
    }
    return 0;
}

Symbol* symbol_table_find_symbol_type(SymbolTable* table, int name, SymbolType::ENUM symbol_type, bool* in_current_scope)
{
    *in_current_scope = false;
    for (int i = 0; i < table->symbols.size; i++) {
        if (table->symbols[i].name == name && table->symbols[i].symbol_type == symbol_type) {
            *in_current_scope = true;
            return &table->symbols[i];
        }
    }
    if (table->parent != 0) {
        Symbol* result = symbol_table_find_symbol_type(table->parent, name, symbol_type, in_current_scope);
        *in_current_scope = false;
        return result;
    }
    return 0;
}

void symbol_table_define_function(SymbolTable* table, Parser* parser, Ast_Node_Function* function) 
{
    bool in_current_scope;
    Symbol* func = symbol_table_find_symbol_type(table, function->function_name_id, SymbolType::FUNCTION, &in_current_scope);
    if (func != 0 && in_current_scope) {
        parser_report_semantic_analysis_error(parser, "Function already defined");
        return;
    }
    Symbol s;
    s.symbol_type = SymbolType::FUNCTION;
    s.name = function->function_name_id;
    s.function = function;
    dynamic_array_push_back(&table->symbols, s);
}

void symbol_table_define_variable(SymbolTable* table, Parser* parser, int name_id, Variable_Type::ENUM variable_type) 
{
    bool in_current_scope;
    Symbol* func = symbol_table_find_symbol_type(table, name_id, SymbolType::VARIABLE, &in_current_scope);
    if (func != 0 && in_current_scope) {
        parser_report_semantic_analysis_error(parser, "Variable already define in current scope");
        return;
    }

    Symbol s;
    s.symbol_type = SymbolType::VARIABLE;
    s.variable_type = variable_type;
    s.name = name_id;
    dynamic_array_push_back(&table->symbols, s);
}

void symbol_table_define_type(SymbolTable* table, Parser* parser, int name_id, Variable_Type::ENUM variable_type)
{
    bool in_current_scope;
    Symbol* sym = symbol_table_find_symbol_type(table, name_id, SymbolType::TYPE, &in_current_scope);
    if (sym != 0) {
        panic("Types should not overlap currently!\n");
        return;
    }

    Symbol s;
    s.symbol_type = SymbolType::TYPE;
    s.variable_type = variable_type;
    s.name = name_id;
    dynamic_array_push_back(&table->symbols, s);
}

Variable_Type::ENUM symbol_table_find_type(SymbolTable* table, int name_id) {
    bool in_current_scope;
    Symbol* s = symbol_table_find_symbol_type(table, name_id, SymbolType::TYPE, &in_current_scope);
    if (s == 0) {
        return Variable_Type::ERROR_TYPE;
    }
    else return s->variable_type;
}

Variable_Type::ENUM semantic_analysis_analyse_expression(SymbolTable* parent_table, Parser* parser, Ast_Node_Expression* expression)
{
    expression->symbol_table = parent_table;
    expression->free_symbol_table_on_destroy = false;
    switch (expression->type)
    {
    case ExpressionType::FUNCTION_CALL: 
    {
        bool in_current_scope;
        Symbol* func_symbol = 
            symbol_table_find_symbol_type(expression->symbol_table, expression->variable_name_id, SymbolType::FUNCTION, &in_current_scope);
        if (func_symbol == 0) {
            parser_report_semantic_analysis_error(parser, "Function call to a not defined function!");
        }
        Ast_Node_Function* function = func_symbol->function;
        if (expression->arguments.size != function->parameters.size) {
            parser_report_semantic_analysis_error(parser, "Call arguments and function parameter count do not match");
        }
        for (int i = 0; i < function->parameters.size && i < expression->arguments.size; i++) 
        {
            Ast_Node_Expression* argument = &expression->arguments[i];
            Variable_Type::ENUM argument_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, argument);
            Ast_Node_Function_Parameter p = function->parameters[i];
            Variable_Type::ENUM param_type = symbol_table_find_type(expression->symbol_table, p.type_id);
            if (argument_type != param_type || argument_type == Variable_Type::ERROR_TYPE) {
                logg("Arguments type does not match parameter type in function call");
            }
        }
        return symbol_table_find_type(expression->symbol_table, function->return_type_id);
        break;
    }
    case ExpressionType::LITERAL: {
        if (parser->lexer->tokens[expression->literal_token_index].type == Token_Type::BOOLEAN_LITERAL) {
            return Variable_Type::BOOLEAN;
        }
        if (parser->lexer->tokens[expression->literal_token_index].type == Token_Type::INTEGER_LITERAL) {
            return Variable_Type::INTEGER;
        }
        if (parser->lexer->tokens[expression->literal_token_index].type == Token_Type::FLOAT_LITERAL) {
            return Variable_Type::FLOAT;
        }
        panic("This should not happend\n");
        break;
    }
    case ExpressionType::OP_ADD: 
    case ExpressionType::OP_SUBTRACT:
    case ExpressionType::OP_DIVIDE:
    case ExpressionType::OP_MULTIPLY: 
    {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        Variable_Type::ENUM right_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->right);
        if (left_type != right_type) {
            parser_report_semantic_analysis_error(parser, "Left and right values of arithmetic op do not have the same type");
        }
        if (left_type != Variable_Type::INTEGER && left_type != Variable_Type::FLOAT) {
            parser_report_semantic_analysis_error(parser, "Arithmetic operation requires either int or float types on both sides");
        }
        return left_type;
        break;
    }
    case ExpressionType::OP_GREATER_EQUAL: 
    case ExpressionType::OP_GREATER_THAN: 
    case ExpressionType::OP_LESS_EQUAL: 
    case ExpressionType::OP_LESS_THAN: 
    {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        Variable_Type::ENUM right_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->right);
        if (left_type != right_type) {
            parser_report_semantic_analysis_error(parser, "Left and right values of arithmetic op do not have the same type");
        }
        if (left_type != Variable_Type::INTEGER && left_type != Variable_Type::FLOAT) {
            parser_report_semantic_analysis_error(parser, "Arithmetic operation requires either int or float types on both sides");
        }
        return Variable_Type::BOOLEAN;
        break;
    }
    case ExpressionType::OP_MODULO: {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        Variable_Type::ENUM right_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->right);
        if (left_type != right_type) {
            parser_report_semantic_analysis_error(parser, "Left and right values of modulo not have the same type");
        }
        if (left_type != Variable_Type::INTEGER) {
            parser_report_semantic_analysis_error(parser, "Modulo needs integer parameters");
        }
        return left_type;
        break;
    }
    case ExpressionType::OP_BOOLEAN_AND: 
    case ExpressionType::OP_BOOLEAN_OR: {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        Variable_Type::ENUM right_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->right);
        if (left_type != right_type) {
            parser_report_semantic_analysis_error(parser, "Left and right values of boolean op do not have the same type");
        }
        if (left_type != Variable_Type::BOOLEAN) {
            parser_report_semantic_analysis_error(parser, "Boolean opeartions need boolean left and right");
        }
        return Variable_Type::BOOLEAN;
        break;
    }
    case ExpressionType::OP_EQUAL: 
    case ExpressionType::OP_NOT_EQUAL: {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        Variable_Type::ENUM right_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->right);
        if (left_type != right_type) {
            parser_report_semantic_analysis_error(parser, "Left and right values do not have the same type");
        }
        return Variable_Type::BOOLEAN;
        break;
    }
    case ExpressionType::OP_LOGICAL_NOT: {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        if (left_type != Variable_Type::BOOLEAN) {
            parser_report_semantic_analysis_error(parser, "Logical not needs boolean expression");
        }
        return Variable_Type::BOOLEAN;
        break;
    }
    case ExpressionType::OP_NEGATE: {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        if (left_type != Variable_Type::FLOAT || left_type != Variable_Type::INTEGER) {
            parser_report_semantic_analysis_error(parser, "Negate requires float or integer");
        }
        return left_type;
        break;
    }
    case ExpressionType::VARIABLE_READ: {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_type(expression->symbol_table, 
            expression->variable_name_id, SymbolType::VARIABLE, &in_current_scope);
        if (s == 0) {
            parser_report_semantic_analysis_error(parser, "Expression variable not defined!");
            return Variable_Type::ERROR_TYPE;
        }
        return s->variable_type;
        break;
    }
    default: {
        panic("Not all expression covered!\n");
        break;
    }
    }
    return Variable_Type::ERROR_TYPE;
}

void semantic_analysis_analyse_statement_block(SymbolTable* parent_table, Parser* parser, Ast_Node_Statement_Block* block, bool create_new_scope);
void semantic_analysis_analyse_statement(SymbolTable* parent_table, Parser* parser, Ast_Node_Statement* statement)
{
    // I think i need an analysis if there is a return on all paths (if and elsee
    statement->symbol_table = parent_table;
    statement->free_symbol_table_on_destroy = false;
    switch (statement->type)
    {
    case StatementType::RETURN_STATEMENT: {
        Variable_Type::ENUM return_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        if (return_type != parser->current_function_return_type) {
            parser_report_semantic_analysis_error(parser, "Return type does not match function return type");
        }
        break;
    }
    case StatementType::BREAK: {
        if (parser->loop_depth <= 0) {
            parser_report_semantic_analysis_error(parser, "Break outside of loop");
        }
        break;
    }
    case StatementType::CONTINUE: {
        if (parser->loop_depth <= 0) {
            parser_report_semantic_analysis_error(parser, "Continue outside of loop");
        }
        break;
    }
    case StatementType::EXPRESSION: {
        if (statement->expression.type != ExpressionType::FUNCTION_CALL) {
            parser_report_semantic_analysis_error(parser, "Single expression statement is not a function call!");
        }
        break;
    }
    case StatementType::STATEMENT_BLOCK: {
        semantic_analysis_analyse_statement_block(statement->symbol_table, parser, &statement->statements, true);
        break;
    }
    case StatementType::IF_BLOCK: {
        Variable_Type::ENUM condition_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        if (condition_type != Variable_Type::BOOLEAN) {
            parser_report_semantic_analysis_error(parser, "If condition is not a boolean!");
        }
        semantic_analysis_analyse_statement_block(statement->symbol_table, parser, &statement->statements, true);
        break;
    }
    case StatementType::IF_ELSE_BLOCK: {
        Variable_Type::ENUM condition_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        if (condition_type != Variable_Type::BOOLEAN) {
            parser_report_semantic_analysis_error(parser, "If condition is not a boolean!");
        }
        semantic_analysis_analyse_statement_block(statement->symbol_table, parser, &statement->statements, true);
        semantic_analysis_analyse_statement_block(statement->symbol_table, parser, &statement->else_statements, true);
        break;
    }
    case StatementType::WHILE: {
        Variable_Type::ENUM condition_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        if (condition_type != Variable_Type::BOOLEAN) {
            parser_report_semantic_analysis_error(parser, "While condition is not a boolean!");
        }
        semantic_analysis_analyse_statement_block(statement->symbol_table, parser, &statement->statements, true);
        break;
    }
    case StatementType::VARIABLE_ASSIGNMENT: {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_type(statement->symbol_table, statement->variable_name_id, SymbolType::VARIABLE, &in_current_scope);
        if (s == 0) {
            parser_report_semantic_analysis_error(parser, "Variable assignment, variable not defined!");
        }
        Variable_Type::ENUM assignment_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        if (assignment_type != s->variable_type) {
            parser_report_semantic_analysis_error(parser, "Variable type does not match expression type");
        }
        break;
    }
    case StatementType::VARIABLE_DEFINITION:
    {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_type(statement->symbol_table, statement->variable_name_id, SymbolType::VARIABLE, &in_current_scope);
        if (s != 0 && in_current_scope) {
            parser_report_semantic_analysis_error(parser, "Variable already defined!");
            break;
        }
        Symbol* var_type = symbol_table_find_symbol_type(statement->symbol_table, statement->variable_type_id, SymbolType::TYPE, &in_current_scope);
        if (var_type == 0) {
            parser_report_semantic_analysis_error(parser, "Variable definition failed, variable type is invalid");
            break;
        }
        symbol_table_define_variable(statement->symbol_table, parser, statement->variable_name_id, var_type->variable_type);
        break;
    }
    case StatementType::VARIABLE_DEFINE_ASSIGN:
    {
        bool in_current_scope;
        {
            Symbol* s = symbol_table_find_symbol_type(statement->symbol_table, statement->variable_name_id, SymbolType::VARIABLE, &in_current_scope);
            if (s != 0 && in_current_scope) {
                parser_report_semantic_analysis_error(parser, "Variable already defined!");
                break;
            }
        }
        Symbol* var_type = symbol_table_find_symbol_type(statement->symbol_table, statement->variable_type_id, SymbolType::TYPE, &in_current_scope);
        if (var_type == 0) {
            parser_report_semantic_analysis_error(parser, "Variable definition failed, variable type is invalid");
            break;
        }
        Variable_Type::ENUM assignment_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        if (assignment_type != var_type->variable_type) {
            parser_report_semantic_analysis_error(parser, "Variable type does not match expression type");
        }
        symbol_table_define_variable(statement->symbol_table, parser, statement->variable_name_id, var_type->variable_type);
        break;
    }
    case StatementType::VARIABLE_DEFINE_INFER:
    {
        bool in_current_scope;
        {
            Symbol* s = symbol_table_find_symbol_type(statement->symbol_table, statement->variable_name_id, SymbolType::VARIABLE, &in_current_scope);
            if (s != 0 && in_current_scope) {
                parser_report_semantic_analysis_error(parser, "Variable already defined!");
                break;
            }
        }
        Variable_Type::ENUM assignment_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        symbol_table_define_variable(statement->symbol_table, parser, statement->variable_name_id, assignment_type);
        break;
    }
    default: {
        panic("Should be covered!\n");
        break;
    }
    }

    return;
}

void semantic_analysis_analyse_statement_block(SymbolTable* parent_table, Parser* parser, Ast_Node_Statement_Block* block, bool create_new_scope)
{
    // This should have its own symbol table, think of if/else statements... -> or if else while has its own stuff...
    if (create_new_scope) {
        block->symbol_table = symbol_table_create_new(parent_table);
        block->free_symbol_table_on_destroy = true;
    }
    else {
        block->symbol_table = parent_table;
        block->free_symbol_table_on_destroy = false;
    }
    for (int i = 0; i < block->statements.size; i++) {
        Ast_Node_Statement* statement = &block->statements[i];
        semantic_analysis_analyse_statement(block->symbol_table, parser, statement);
    }
}

void semantic_analysis_analyse_function(SymbolTable* parent_table, Parser* parser, Ast_Node_Function* function)
{
    function->symbol_table = symbol_table_create_new(parent_table);
    function->free_symbol_table_on_destroy = true;
    // Define paramenter variables
    for (int i = 0; i < function->parameters.size; i++)
    {
        Ast_Node_Function_Parameter p = function->parameters[i];
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_type(function->symbol_table, p.type_id, SymbolType::TYPE, &in_current_scope);
        if (s == 0) {
            parser_report_semantic_analysis_error(parser, "Variable type is not defined!\n");
            // I define the variable as error-type, so that there arent that many follow up errors
            symbol_table_define_variable(function->symbol_table, parser, p.name_id, Variable_Type::ERROR_TYPE);
            continue;
        }
        symbol_table_define_variable(function->symbol_table, parser, p.name_id, s->variable_type);
    }

    // Set return type
    {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_type(function->symbol_table, function->return_type_id, SymbolType::TYPE, &in_current_scope);
        if (s == 0) {
            parser_report_semantic_analysis_error(parser, "Function return type not valid type!");
            parser->current_function_return_type = Variable_Type::ERROR_TYPE;
        }
        else {
            parser->current_function_return_type = s->variable_type;
        }
    }
    parser->loop_depth = 0;

    semantic_analysis_analyse_statement_block(function->symbol_table, parser, &function->body, false);
}

void parser_semantic_analysis(Parser* parser)
{
    Ast_Node_Root* root = &parser->root;
    root->symbol_table = symbol_table_create_new(0);
    root->free_symbol_table_on_destroy = true;
    // Add tokens for basic datatypes
    {
        int int_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("int"));
        int bool_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("bool"));
        int float_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("float"));
        int void_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("void"));
        symbol_table_define_type(root->symbol_table, parser, int_token_index, Variable_Type::INTEGER);
        symbol_table_define_type(root->symbol_table, parser, bool_token_index, Variable_Type::BOOLEAN);
        symbol_table_define_type(root->symbol_table, parser, float_token_index, Variable_Type::FLOAT);
        symbol_table_define_type(root->symbol_table, parser, void_token_index, Variable_Type::VOID_TYPE);
    }

    for (int i = 0; i < root->functions.size; i++) {
        Ast_Node_Function* function = &root->functions[i];
        symbol_table_define_function(root->symbol_table, parser, function);
    }

    // Do semantic analysis on all functions
    for (int i = 0; i < root->functions.size; i++) {
        Ast_Node_Function* function = &root->functions[i];
        semantic_analysis_analyse_function(root->symbol_table, parser, function);
    }
}

