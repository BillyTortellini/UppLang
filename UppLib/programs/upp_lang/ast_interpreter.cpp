#include "ast_interpreter.hpp"

Ast_Interpreter ast_interpreter_create(Ast_Node_Root* root, Lexer* lexer)
{
    Ast_Interpreter result;
    result.root = root;
    result.lexer = lexer;
    result.argument_evaluation_buffer = dynamic_array_create_empty<Ast_Interpreter_Value>(16);
    result.symbol_table = dynamic_array_create_empty<Ast_Interpreter_Variable>(16);
    result.scope_beginnings = dynamic_array_create_empty<int>(16);
    dynamic_array_push_back(&result.scope_beginnings, 0);
    result.function_scope_beginnings = dynamic_array_create_empty<int>(16);
    dynamic_array_push_back(&result.function_scope_beginnings, 0);
    return result;
}

void ast_interpreter_destroy(Ast_Interpreter* interpreter) {
    dynamic_array_destroy(&interpreter->symbol_table);
    dynamic_array_destroy(&interpreter->scope_beginnings);
    dynamic_array_destroy(&interpreter->function_scope_beginnings);
    dynamic_array_destroy(&interpreter->argument_evaluation_buffer);
}

int ast_interpreter_find_variable_index(Ast_Interpreter* interpreter, int var_name) {
    int function_scope_beginning = interpreter->function_scope_beginnings[interpreter->function_scope_beginnings.size - 1];
    for (int i = interpreter->symbol_table.size - 1; i >= function_scope_beginning; i--) {
        if (interpreter->symbol_table[i].variable_name == var_name) {
            return i;
        }
    }
    return -1;
}

Ast_Interpreter_Variable* ast_interpreter_find_variable(Ast_Interpreter* interpreter, int var_name) {
    int i = ast_interpreter_find_variable_index(interpreter, var_name);
    if (i == -1) return 0;
    return &interpreter->symbol_table[i];
}

void ast_interpreter_begin_new_scope(Ast_Interpreter* interpreter) {
    dynamic_array_push_back(&interpreter->scope_beginnings, interpreter->symbol_table.size);
}

void ast_interpreter_exit_scope(Ast_Interpreter* interpreter) {
    if (interpreter->scope_beginnings.size == 0) {
        panic("Should not happend!\n");
        return;
    }
    int scope_start = interpreter->scope_beginnings[interpreter->scope_beginnings.size - 1];
    dynamic_array_remove_range_ordered(&interpreter->symbol_table, scope_start, interpreter->symbol_table.size);
    dynamic_array_swap_remove(&interpreter->scope_beginnings, interpreter->scope_beginnings.size - 1);
}

void ast_interpreter_begin_new_function_scope(Ast_Interpreter* interpreter) {
    ast_interpreter_begin_new_scope(interpreter);
    dynamic_array_push_back(&interpreter->function_scope_beginnings, interpreter->symbol_table.size);
}

void ast_interpreter_end_function_scope(Ast_Interpreter* interpreter) {
    ast_interpreter_exit_scope(interpreter);
    dynamic_array_swap_remove(&interpreter->function_scope_beginnings, interpreter->function_scope_beginnings.size - 1);
}

Variable_Type::ENUM ast_interpreter_token_index_to_value_type(Ast_Interpreter* interpreter, int index)
{
    if (index == interpreter->int_token_index) return Variable_Type::INTEGER;
    if (index == interpreter->float_token_index) return Variable_Type::FLOAT;
    if (index == interpreter->bool_token_index) return Variable_Type::BOOLEAN;
    return Variable_Type::ERROR_TYPE;
}

void ast_interpreter_define_variable(Ast_Interpreter* interpreter, Variable_Type::ENUM type, int var_name)
{
    int current_scope_start = interpreter->scope_beginnings[interpreter->scope_beginnings.size - 1];
    if (ast_interpreter_find_variable_index(interpreter, var_name) >= current_scope_start) {
        logg("Variable %s already defined in this scope!", lexer_identifer_to_string(interpreter->lexer, var_name).characters);
        return;
    }

    Ast_Interpreter_Variable var;
    var.value.type = type;
    var.value.bool_value = false;
    var.value.int_value = -69;
    var.value.float_value = -69.69f;
    var.variable_name = var_name;
    dynamic_array_push_back(&interpreter->symbol_table, var);
}

Ast_Interpreter_Statement_Result ast_interpreter_execute_statment_block(Ast_Interpreter* interpreter, Ast_Node_Statement_Block* block);
Ast_Interpreter_Value ast_interpreter_evaluate_expression(Ast_Interpreter* interpreter, Ast_Node_Expression* expression)
{
    Ast_Interpreter_Value result;
    result.type = Variable_Type::ERROR_TYPE;

    if (expression->type == ExpressionType::LITERAL) {
        Token& token = interpreter->lexer->tokens[expression->literal_token_index];
        if (token.type == Token_Type::INTEGER_LITERAL) {
            result.type = Variable_Type::INTEGER;
            result.int_value = token.attribute.integer_value;
        }
        else if (token.type == Token_Type::FLOAT_LITERAL) {
            result.type = Variable_Type::FLOAT;
            result.float_value = token.attribute.float_value;
        }
        else if (token.type == Token_Type::BOOLEAN_LITERAL) {
            result.type = Variable_Type::BOOLEAN;
            result.bool_value = token.attribute.bool_value;
        }
        else {
            panic("I dont think it is possible to ever get here!\n");
        }
        return result;
    }
    else if (expression->type == ExpressionType::VARIABLE_READ)
    {
        Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, expression->variable_name_id);
        if (var == 0) {
            logg("Expression variable %s not defined!\n", lexer_identifer_to_string(interpreter->lexer, expression->variable_name_id).characters);
            return result;
        }
        return var->value;
    }
    else if (expression->type == ExpressionType::FUNCTION_CALL)
    {
        Ast_Node_Function* function = 0;
        for (int i = 0; i < interpreter->root->functions.size; i++) {
            if (interpreter->root->functions[i].function_name_id == expression->variable_name_id) {
                function = &interpreter->root->functions[i];
            }
        }
        if (function == 0) 
        {
            if (expression->variable_name_id == interpreter->print_token_index) {
                String str = string_create_empty(64);
                SCOPE_EXIT(string_destroy(&str));
                string_append_formated(&str, "print: ");
                for (int i = 0; i < expression->arguments.size; i++) {
                    Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, &expression->arguments[i]);
                    ast_interpreter_value_append_to_string(val, &str);
                    string_append_formated(&str, ", ");
                }
                logg("%s\n", str.characters);
                return result;
            }
            logg("Function named %s not found!\n", lexer_identifer_to_string(interpreter->lexer, expression->variable_name_id).characters);
            return result;
        }
        if (function->parameters.size != expression->arguments.size) {
            logg("Function call does not have enough arguments!\n");
            return result;
        }
        // Evaluate arguments before making new scope, since afterwards expressions that need variable reads dont work
        dynamic_array_reset(&interpreter->argument_evaluation_buffer);
        for (int i = 0; i < expression->arguments.size; i++) {
            Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, &expression->arguments[i]);
            dynamic_array_push_back(&interpreter->argument_evaluation_buffer, val);
        }

        ast_interpreter_begin_new_function_scope(interpreter);
        // Push arguments on the stack
        bool success = true;
        for (int i = 0; i < interpreter->argument_evaluation_buffer.size; i++) {
            Ast_Interpreter_Value val = interpreter->argument_evaluation_buffer[i];
            if (val.type != ast_interpreter_token_index_to_value_type(interpreter, function->parameters[i].type_id)) {
                logg("Argument type does not match parameter type of function!\n");
                success = false;
                break;
            }
            ast_interpreter_define_variable(interpreter, val.type, function->parameters[i].name_id);
            Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, function->parameters[i].name_id);
            var->value = val;
        }
        if (success) {
            Ast_Interpreter_Statement_Result result;
            result = ast_interpreter_execute_statment_block(interpreter, &function->body);
            if (result.is_return) {
                if (result.return_value.type != ast_interpreter_token_index_to_value_type(interpreter, function->return_type_id)) {
                    logg("Return value does not match return type of function %s\n", 
                        lexer_identifer_to_string(interpreter->lexer, function->function_name_id).characters);
                }
                ast_interpreter_end_function_scope(interpreter);
                return result.return_value;
            }
        }
        ast_interpreter_end_function_scope(interpreter);
        return result;
    }
    else if (expression->type == ExpressionType::OP_EQUAL ||
        expression->type == ExpressionType::OP_NOT_EQUAL ||
        expression->type == ExpressionType::OP_LESS_EQUAL ||
        expression->type == ExpressionType::OP_LESS_THAN ||
        expression->type == ExpressionType::OP_GREATER_EQUAL ||
        expression->type == ExpressionType::OP_GREATER_THAN)
    {
        Ast_Interpreter_Value left_operand = ast_interpreter_evaluate_expression(interpreter, expression->left);
        Ast_Interpreter_Value right_operand = ast_interpreter_evaluate_expression(interpreter, expression->right);
        Ast_Interpreter_Value& l = left_operand;
        Ast_Interpreter_Value& r = right_operand;
        if (left_operand.type != right_operand.type) { // Implicit casting would happen here
            return result;
        }

        result.type = Variable_Type::BOOLEAN;
        if (left_operand.type == Variable_Type::FLOAT)
        {
            switch (expression->type)
            {
            case ExpressionType::OP_EQUAL: result.bool_value = l.float_value == r.float_value; break;
            case ExpressionType::OP_NOT_EQUAL: result.bool_value = l.float_value != r.float_value; break;
            case ExpressionType::OP_LESS_EQUAL: result.bool_value = l.float_value <= r.float_value; break;
            case ExpressionType::OP_LESS_THAN: result.bool_value = l.float_value < r.float_value; break;
            case ExpressionType::OP_GREATER_EQUAL: result.bool_value = l.float_value >= r.float_value; break;
            case ExpressionType::OP_GREATER_THAN: result.bool_value = l.float_value > r.float_value; break;
            }
        }
        else if (left_operand.type == Variable_Type::INTEGER)
        {
            switch (expression->type)
            {
            case ExpressionType::OP_EQUAL: result.bool_value = l.int_value == r.int_value; break;
            case ExpressionType::OP_NOT_EQUAL: result.bool_value = l.int_value != r.int_value; break;
            case ExpressionType::OP_LESS_EQUAL: result.bool_value = l.int_value <= r.int_value; break;
            case ExpressionType::OP_LESS_THAN: result.bool_value = l.int_value < r.int_value; break;
            case ExpressionType::OP_GREATER_EQUAL: result.bool_value = l.int_value >= r.int_value; break;
            case ExpressionType::OP_GREATER_THAN: result.bool_value = l.int_value > r.int_value; break;
            }
        }
        else if (left_operand.type == Variable_Type::BOOLEAN) {
            switch (expression->type)
            {
            case ExpressionType::OP_EQUAL: result.bool_value = l.bool_value == r.bool_value; break;
            case ExpressionType::OP_NOT_EQUAL: result.bool_value = l.bool_value != r.bool_value; break;
            default: {
                logg("Cannot do comparisions on booleans!");
                result.type = Variable_Type::ERROR_TYPE;
                return result;
            }
            }
        }
        return result;
    }
    else if (expression->type == ExpressionType::OP_ADD ||
        expression->type == ExpressionType::OP_SUBTRACT ||
        expression->type == ExpressionType::OP_MODULO ||
        expression->type == ExpressionType::OP_MULTIPLY ||
        expression->type == ExpressionType::OP_DIVIDE)
    {
        Ast_Interpreter_Value left_operand = ast_interpreter_evaluate_expression(interpreter, expression->left);
        Ast_Interpreter_Value right_operand = ast_interpreter_evaluate_expression(interpreter, expression->right);
        Ast_Interpreter_Value& l = left_operand;
        Ast_Interpreter_Value& r = right_operand;
        if (left_operand.type != right_operand.type) { // Implicit casting would happen here
            return result;
        }
        if (left_operand.type == Variable_Type::FLOAT)
        {
            result.type = Variable_Type::FLOAT;
            switch (expression->type)
            {
            case ExpressionType::OP_ADD: result.float_value = l.float_value + r.float_value; break;
            case ExpressionType::OP_SUBTRACT: result.float_value = l.float_value - r.float_value; break;
            case ExpressionType::OP_MULTIPLY: result.float_value = l.float_value * r.float_value; break;
            case ExpressionType::OP_DIVIDE: result.float_value = l.float_value / r.float_value; break;
            case ExpressionType::OP_MODULO: {
                logg("Float modulo float not supported!\n");
                result.type = Variable_Type::ERROR_TYPE;
                break;
            }
            }
        }
        else if (left_operand.type == Variable_Type::INTEGER)
        {
            result.type = Variable_Type::INTEGER;
            switch (expression->type)
            {
            case ExpressionType::OP_ADD: result.int_value = l.int_value + r.int_value; break;
            case ExpressionType::OP_SUBTRACT: result.int_value = l.int_value - r.int_value; break;
            case ExpressionType::OP_MULTIPLY: result.int_value = l.int_value * r.int_value; break;
            case ExpressionType::OP_MODULO: result.int_value = l.int_value % r.int_value; break;
            case ExpressionType::OP_DIVIDE: {
                if (r.int_value == 0) {
                    logg("Integer Division by zero!\n");
                    result.type = Variable_Type::ERROR_TYPE;
                    break;
                }
                result.int_value = l.int_value / r.int_value; break;
            }
            }
        }
        else if (left_operand.type == Variable_Type::BOOLEAN) {
            result.type = Variable_Type::ERROR_TYPE;
        }
        return result;
    }
    else if (expression->type == ExpressionType::OP_BOOLEAN_AND ||
        expression->type == ExpressionType::OP_BOOLEAN_OR)
    {
        Ast_Interpreter_Value left_operand = ast_interpreter_evaluate_expression(interpreter, expression->left);
        Ast_Interpreter_Value right_operand = ast_interpreter_evaluate_expression(interpreter, expression->right);
        if (left_operand.type != Variable_Type::BOOLEAN ||
            right_operand.type != Variable_Type::BOOLEAN) {
            logg("Left an right of Logic-Operator (&& or ||) must be boolean values: left operand type: %s, right operand type:  %s\n",
                variable_type_to_string(left_operand.type).characters,
                variable_type_to_string(right_operand.type).characters);
            return result;
        }

        result.type = Variable_Type::BOOLEAN;
        switch (expression->type) {
        case ExpressionType::OP_BOOLEAN_AND: result.bool_value = left_operand.bool_value && right_operand.bool_value; break;
        case ExpressionType::OP_BOOLEAN_OR: result.bool_value = left_operand.bool_value || right_operand.bool_value; break;
        }
        return result;
    }
    else if (expression->type == ExpressionType::OP_LOGICAL_NOT) {
        Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, expression->left);
        if (val.type != Variable_Type::BOOLEAN) {
            logg("Logical not only works on boolean value!\n");
            return result;
        }
        result.type = Variable_Type::BOOLEAN;
        result.bool_value = !val.bool_value;
        return result;
    }
    else if (expression->type == ExpressionType::OP_NEGATE) {
        Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, expression->left);
        if (val.type == Variable_Type::BOOLEAN) {
            logg("Negate does not work on boolean values");
            return result;
        }
        if (val.type == Variable_Type::FLOAT) {
            result.type = val.type;
            result.float_value = -val.float_value;
        }
        if (val.type == Variable_Type::INTEGER) {
            result.type = val.type;
            result.int_value = -val.int_value;
        }
        return result;
    }

    logg("Expression type invalid!\n");
    return result;
}

Ast_Interpreter_Statement_Result ast_interpreter_result_make_empty() {
    Ast_Interpreter_Statement_Result result;
    result.is_break = false;
    result.is_continue = false;
    result.is_return = false;
    return result;
}

Ast_Interpreter_Statement_Result ast_interpreter_result_make_break() {
    Ast_Interpreter_Statement_Result result;
    result.is_break = true;
    result.is_continue = false;
    result.is_return = false;
    return result;
}

Ast_Interpreter_Statement_Result ast_interpreter_result_make_continue() {
    Ast_Interpreter_Statement_Result result;
    result.is_break = false;
    result.is_continue = true;
    result.is_return = false;
    return result;
}

Ast_Interpreter_Statement_Result ast_interpreter_result_make_return(Ast_Interpreter_Value val) {
    Ast_Interpreter_Statement_Result result;
    result.is_break = false;
    result.is_continue = false;
    result.is_return = true;
    result.return_value = val;
    return result;
}

Ast_Interpreter_Statement_Result ast_interpreter_execute_statement(Ast_Interpreter* interpreter, Ast_Node_Statement* statement);
Ast_Interpreter_Statement_Result ast_interpreter_execute_statment_block(Ast_Interpreter* interpreter, Ast_Node_Statement_Block* block)
{
    ast_interpreter_begin_new_scope(interpreter);
    for (int i = 0; i < block->statements.size; i++) {
        Ast_Interpreter_Statement_Result result = ast_interpreter_execute_statement(interpreter, &block->statements[i]);
        if (result.is_return || result.is_continue || result.is_break) {
            ast_interpreter_exit_scope(interpreter);
            return result;
        }
    }
    ast_interpreter_exit_scope(interpreter);
    return ast_interpreter_result_make_empty();
}

Ast_Interpreter_Statement_Result ast_interpreter_execute_statement(Ast_Interpreter* interpreter, Ast_Node_Statement* statement)
{
    Ast_Interpreter_Statement_Result result = ast_interpreter_result_make_empty();

    if (statement->type == StatementType::RETURN_STATEMENT) {
        result.is_return = true;
        result.return_value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        return result;
    }
    else if (statement->type == StatementType::EXPRESSION) // Expression can be function call, thats why we need it here
    {
        ast_interpreter_evaluate_expression(interpreter, &statement->expression); 
        return result;
    }
    else if (statement->type == StatementType::WHILE)
    {
        while (true)
        {
            Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
            if (val.type != Variable_Type::BOOLEAN) {
                logg("WHILE condition is not a boolean!\n");
                return result;
            }
            if (val.bool_value) {
                Ast_Interpreter_Statement_Result res = ast_interpreter_execute_statment_block(interpreter, &statement->statements);
                if (res.is_return) return result;
                if (res.is_continue) continue;
                if (res.is_break) return result;
            }
            else {
                return result;
            }
        }
    }
    else if (statement->type == StatementType::IF_BLOCK) {
        Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        if (val.type != Variable_Type::BOOLEAN) {
            logg("If expression is not a boolean!\n");
            return result;
        }
        if (val.bool_value) {
            return ast_interpreter_execute_statment_block(interpreter, &statement->statements);
        }
    }
    else if (statement->type == StatementType::IF_ELSE_BLOCK) {
        Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        if (val.type != Variable_Type::BOOLEAN) {
            logg("If expression is not a boolean!\n");
            return result;
        }
        if (val.bool_value) {
            return ast_interpreter_execute_statment_block(interpreter, &statement->statements);
        }
        else {
            return ast_interpreter_execute_statment_block(interpreter, &statement->else_statements);
        }
    }
    else if (statement->type == StatementType::VARIABLE_DEFINITION) {
        if (statement->variable_type_id == interpreter->int_token_index) {
            ast_interpreter_define_variable(interpreter, Variable_Type::INTEGER, statement->variable_name_id);
        }
        else if (statement->variable_type_id == interpreter->float_token_index) {
            ast_interpreter_define_variable(interpreter, Variable_Type::FLOAT, statement->variable_name_id);
        }
        else if (statement->variable_type_id == interpreter->bool_token_index) {
            ast_interpreter_define_variable(interpreter, Variable_Type::BOOLEAN, statement->variable_name_id);
        }
        else {
            logg("Type-Error: %s is not a valid type\n", lexer_identifer_to_string(interpreter->lexer, statement->variable_type_id).characters);
            return result;
        }
    }
    else if (statement->type == StatementType::VARIABLE_ASSIGNMENT)
    {
        Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, statement->variable_name_id);
        if (var == 0) {
            logg("Variable assignment statement variable %s not defined!\n", 
                lexer_identifer_to_string(interpreter->lexer, statement->variable_name_id).characters);
            return result;
        }
        Ast_Interpreter_Value value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        if (value.type != var->value.type) {
            logg("Variable assignment failed, variable type does not match expression type:\n %s = %s\n",
                variable_type_to_string(var->value.type).characters,
                variable_type_to_string(value.type).characters);
            return result;
        }
        var->value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
    }
    else if (statement->type == StatementType::VARIABLE_DEFINE_ASSIGN) // x : int = 5
    {
        Ast_Interpreter_Value value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        Variable_Type::ENUM var_type;
        if (statement->variable_type_id == interpreter->int_token_index) var_type = Variable_Type::INTEGER;
        else if (statement->variable_type_id == interpreter->float_token_index) var_type = Variable_Type::FLOAT;
        else if (statement->variable_type_id == interpreter->bool_token_index) var_type = Variable_Type::BOOLEAN;
        else {
            logg("Type-Error: %s is not a valid type\n", lexer_identifer_to_string(interpreter->lexer, statement->variable_type_id).characters);
            return result;
        }
        if (var_type != value.type) {
            logg("Types not compatible, var type: ", lexer_identifer_to_string(interpreter->lexer, statement->variable_type_id).characters);
            return result;
        }

        ast_interpreter_define_variable(interpreter, var_type, statement->variable_name_id);
        Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, statement->variable_name_id);
        var->value = value;
    }
    else if (statement->type == StatementType::VARIABLE_DEFINE_INFER)
    {
        Ast_Interpreter_Value value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        ast_interpreter_define_variable(interpreter, value.type, statement->variable_name_id);
        Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, statement->variable_name_id);
        var->value = value;
    }
    else if (statement->type == StatementType::STATEMENT_BLOCK) {
        return ast_interpreter_execute_statment_block(interpreter, &statement->statements);
    }
    else if (statement->type == StatementType::BREAK) {
        return ast_interpreter_result_make_break();
    }
    else if (statement->type == StatementType::CONTINUE) {
        return ast_interpreter_result_make_continue();
    }
    return result;
}

Ast_Interpreter_Value ast_interpreter_execute_main(Ast_Node_Root* root, Lexer* lexer)
{
    Ast_Interpreter interpreter = ast_interpreter_create(root, lexer);
    SCOPE_EXIT(ast_interpreter_destroy(&interpreter));
    Ast_Interpreter_Value error_value;
    error_value.type = Variable_Type::ERROR_TYPE;

    // Find main
    Ast_Node_Function* main = 0;
    {
        int* main_identifer = hashtable_find_element(&lexer->identifier_index_lookup_table, string_create_static("main"));
        if (main_identifer == 0) {
            logg("Main not defined\n");
            return error_value;
        }
        for (int i = 0; i < root->functions.size; i++) {
            if (root->functions[i].function_name_id == *main_identifer) {
                main = &root->functions[i];
            }
        }
        if (main == 0) {
            logg("Main function not found\n");
            return error_value;
        }
    }

    // Find token indices for types
    interpreter.int_token_index = lexer_add_or_find_identifier_by_string(lexer, string_create_static("int"));
    interpreter.bool_token_index = lexer_add_or_find_identifier_by_string(lexer, string_create_static("bool"));
    interpreter.float_token_index = lexer_add_or_find_identifier_by_string(lexer, string_create_static("float"));
    interpreter.print_token_index = lexer_add_or_find_identifier_by_string(lexer, string_create_static("print"));

    Ast_Interpreter_Statement_Result main_result = ast_interpreter_execute_statment_block(&interpreter, &main->body);
    if (!main_result.is_return) {
        logg("No return statement found!\n");
        return error_value;
    }

    return main_result.return_value;
}

void ast_interpreter_value_append_to_string(Ast_Interpreter_Value value, String* string)
{
    switch (value.type)
    {
    case Variable_Type::BOOLEAN:
        string_append_formated(string, "BOOL: %s ", value.bool_value ? "true" : "false"); break;
    case Variable_Type::INTEGER:
        string_append_formated(string, "INT: %d ", value.int_value); break;
    case Variable_Type::FLOAT:
        string_append_formated(string, "FLOAT: %f ", value.float_value); break;
    case Variable_Type::ERROR_TYPE:
        string_append_formated(string, "ERROR-Type "); break;
    default:
        string_append_formated(string, "SHOULD_NOT_HAPPEN.EXE"); break;
    }
    return;
    return;
}

