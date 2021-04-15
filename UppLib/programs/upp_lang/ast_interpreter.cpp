#include "ast_interpreter.hpp"

AST_Interpreter ast_interpreter_create()
{
    AST_Interpreter result;
    result.argument_evaluation_buffer = dynamic_array_create_empty<AST_Interpreter_Value>(16);
    result.symbol_table = dynamic_array_create_empty<AST_Interpreter_Variable>(16);
    result.scope_beginnings = dynamic_array_create_empty<int>(16);
    result.function_scope_beginnings = dynamic_array_create_empty<int>(16);
    dynamic_array_push_back(&result.scope_beginnings, 0);
    dynamic_array_push_back(&result.function_scope_beginnings, 0);
    return result;
}

void ast_interpreter_destroy(AST_Interpreter* interpreter) {
    dynamic_array_destroy(&interpreter->symbol_table);
    dynamic_array_destroy(&interpreter->scope_beginnings);
    dynamic_array_destroy(&interpreter->function_scope_beginnings);
    dynamic_array_destroy(&interpreter->argument_evaluation_buffer);
}

int ast_interpreter_find_variable_index(AST_Interpreter* interpreter, int var_name) 
{
    int function_scope_beginning = interpreter->function_scope_beginnings[interpreter->function_scope_beginnings.size - 1];
    for (int i = interpreter->symbol_table.size - 1; i >= function_scope_beginning; i--) {
        if (interpreter->symbol_table[i].variable_name == var_name) {
            return i;
        }
    }
    return -1;
}

AST_Interpreter_Variable* ast_interpreter_find_variable(AST_Interpreter* interpreter, int var_name) {
    int i = ast_interpreter_find_variable_index(interpreter, var_name);
    if (i == -1) return 0;
    return &interpreter->symbol_table[i];
}

void ast_interpreter_begin_new_scope(AST_Interpreter* interpreter) {
    dynamic_array_push_back(&interpreter->scope_beginnings, interpreter->symbol_table.size);
}

void ast_interpreter_exit_scope(AST_Interpreter* interpreter) {
    if (interpreter->scope_beginnings.size == 0) {
        panic("Should not happend!\n");
        return;
    }
    int scope_start = interpreter->scope_beginnings[interpreter->scope_beginnings.size - 1];
    dynamic_array_rollback_to_size(&interpreter->symbol_table, scope_start);
    dynamic_array_rollback_to_size(&interpreter->scope_beginnings, interpreter->scope_beginnings.size-1);
}

void ast_interpreter_begin_new_function_scope(AST_Interpreter* interpreter) {
    ast_interpreter_begin_new_scope(interpreter);
    dynamic_array_push_back(&interpreter->function_scope_beginnings, interpreter->symbol_table.size);
}

void ast_interpreter_end_function_scope(AST_Interpreter* interpreter) {
    ast_interpreter_exit_scope(interpreter);
    dynamic_array_swap_remove(&interpreter->function_scope_beginnings, interpreter->function_scope_beginnings.size - 1);
}

void ast_interpreter_define_variable(AST_Interpreter* interpreter, Primitive_Type type, int var_name)
{
    int current_scope_start = interpreter->scope_beginnings[interpreter->scope_beginnings.size - 1];
    if (ast_interpreter_find_variable_index(interpreter, var_name) >= current_scope_start) {
        logg("Variable %s already defined in this scope!", lexer_identifer_to_string(interpreter->analyser->parser->lexer, var_name).characters);
        return;
    }

    AST_Interpreter_Variable var;
    var.value.type = type;
    var.value.bool_value = false;
    var.value.int_value = -69;
    var.value.float_value = -69.69f;
    var.variable_name = var_name;
    dynamic_array_push_back(&interpreter->symbol_table, var);
}

AST_Interpreter_Statement_Result ast_interpreter_execute_statment_block(AST_Interpreter* interpreter, int block_index);
AST_Interpreter_Value ast_interpreter_evaluate_expression(AST_Interpreter* interpreter, int expression_index)
{
    AST_Interpreter_Value result;
    result.type = Primitive_Type::ERROR_TYPE;
    AST_Node* expression = &interpreter->analyser->parser->nodes[expression_index];

    if (expression->type == AST_Node_Type::EXPRESSION_LITERAL) {
        Token& token = interpreter->analyser->parser->lexer->tokens[interpreter->analyser->parser->token_mapping[expression_index].start_index];
        if (token.type == Token_Type::INTEGER_LITERAL) {
            result.type = Primitive_Type::INTEGER;
            result.int_value = token.attribute.integer_value;
        }
        else if (token.type == Token_Type::FLOAT_LITERAL) {
            result.type = Primitive_Type::FLOAT;
            result.float_value = token.attribute.float_value;
        }
        else if (token.type == Token_Type::BOOLEAN_LITERAL) {
            result.type = Primitive_Type::BOOLEAN;
            result.bool_value = token.attribute.bool_value;
        }
        else {
            panic("I dont think it is possible to ever get here!\n");
        }
        return result;
    }
    else if (expression->type == AST_Node_Type::EXPRESSION_VARIABLE_READ)
    {
        AST_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, expression->name_id);
        return var->value;
    }
    else if (expression->type == AST_Node_Type::EXPRESSION_FUNCTION_CALL)
    {
        // Find function
        bool unused;
        Symbol* function_symbol = symbol_table_find_symbol_of_type(
            interpreter->analyser->symbol_tables[interpreter->analyser->semantic_information[expression_index]],
            expression->name_id, Symbol_Type::FUNCTION, &unused
        );
        int function_index = function_symbol->function_index;
        AST_Node* function = &interpreter->analyser->parser->nodes[function_index];
        AST_Node* parameter_block = &interpreter->analyser->parser->nodes[function->children[0]];

        // Evaluate arguments before making new scope, since afterwards expressions that need variable reads dont work
        dynamic_array_reset(&interpreter->argument_evaluation_buffer);
        for (int i = 0; i < expression->children.size; i++) {
            AST_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, expression->children[i]);
            dynamic_array_push_back(&interpreter->argument_evaluation_buffer, val);
        }

        ast_interpreter_begin_new_function_scope(interpreter);
        // Push arguments on the stack
        for (int i = 0; i < interpreter->argument_evaluation_buffer.size; i++) {
            AST_Interpreter_Value val = interpreter->argument_evaluation_buffer[i];
            int parameter_name = interpreter->analyser->parser->nodes[parameter_block->children[i]].name_id;
            ast_interpreter_define_variable(interpreter, val.type, parameter_name);
            AST_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, parameter_name);
            var->value = val;
        }
        // Start function
        {
            AST_Interpreter_Statement_Result result;
            result = ast_interpreter_execute_statment_block(interpreter, function->children[1]);
            if (result.is_return) {
                ast_interpreter_end_function_scope(interpreter);
                return result.return_value;
            }
            ast_interpreter_end_function_scope(interpreter);
        }
        return result;
    }
    else if (expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL ||
        expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL ||
        expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS ||
        expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL||
        expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER ||
        expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL)
    {
        AST_Interpreter_Value left_operand = ast_interpreter_evaluate_expression(interpreter, expression->children[0]);
        AST_Interpreter_Value right_operand = ast_interpreter_evaluate_expression(interpreter, expression->children[1]);
        AST_Interpreter_Value& l = left_operand;
        AST_Interpreter_Value& r = right_operand;
        if (left_operand.type != right_operand.type) { // Implicit casting would happen here
            return result;
        }

        result.type = Primitive_Type::BOOLEAN;
        if (left_operand.type == Primitive_Type::FLOAT)
        {
            switch (expression->type)
            {
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL: result.bool_value = l.float_value == r.float_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL: result.bool_value = l.float_value != r.float_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL: result.bool_value = l.float_value <= r.float_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS: result.bool_value = l.float_value < r.float_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL: result.bool_value = l.float_value >= r.float_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER: result.bool_value = l.float_value > r.float_value; break;
            }
        }
        else if (left_operand.type == Primitive_Type::INTEGER)
        {
            switch (expression->type)
            {
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL: result.bool_value = l.int_value == r.int_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL: result.bool_value = l.int_value != r.int_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL: result.bool_value = l.int_value <= r.int_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS: result.bool_value = l.int_value < r.int_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL: result.bool_value = l.int_value >= r.int_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER: result.bool_value = l.int_value > r.int_value; break;
            }
        }
        else if (left_operand.type == Primitive_Type::BOOLEAN) {
            switch (expression->type)
            {
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL: result.bool_value = l.bool_value == r.bool_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL: result.bool_value = l.bool_value != r.bool_value; break;
            default: {
                logg("Cannot do comparisions on booleans!");
                result.type = Primitive_Type::ERROR_TYPE;
                return result;
            }
            }
        }
        return result;
    }
    else if (expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION ||
        expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION ||
        expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO ||
        expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION ||
        expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION)
    {
        AST_Interpreter_Value left_operand = ast_interpreter_evaluate_expression(interpreter, expression->children[0]);
        AST_Interpreter_Value right_operand = ast_interpreter_evaluate_expression(interpreter, expression->children[1]);
        AST_Interpreter_Value& l = left_operand;
        AST_Interpreter_Value& r = right_operand;
        if (left_operand.type != right_operand.type) { // Implicit casting would happen here
            return result;
        }
        if (left_operand.type == Primitive_Type::FLOAT)
        {
            result.type = Primitive_Type::FLOAT;
            switch (expression->type)
            {
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION: result.float_value = l.float_value + r.float_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION: result.float_value = l.float_value - r.float_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION: result.float_value = l.float_value * r.float_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION: result.float_value = l.float_value / r.float_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO: {
                logg("Float modulo float not supported!\n");
                result.type = Primitive_Type::ERROR_TYPE;
                break;
            }
            }
        }
        else if (left_operand.type == Primitive_Type::INTEGER)
        {
            result.type = Primitive_Type::INTEGER;
            switch (expression->type)
            {
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION: result.int_value = l.int_value + r.int_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION: result.int_value = l.int_value - r.int_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION: result.int_value = l.int_value * r.int_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO: result.int_value = l.int_value % r.int_value; break;
            case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION: {
                if (r.int_value == 0) {
                    logg("Integer Division by zero!\n");
                    result.type = Primitive_Type::ERROR_TYPE;
                    break;
                }
                result.int_value = l.int_value / r.int_value; break;
            }
            }
        }
        else if (left_operand.type == Primitive_Type::BOOLEAN) {
            result.type = Primitive_Type::ERROR_TYPE;
        }
        return result;
    }
    else if (expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND ||
        expression->type == AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR)
    {
        AST_Interpreter_Value left_operand = ast_interpreter_evaluate_expression(interpreter, expression->children[0]);
        AST_Interpreter_Value right_operand = ast_interpreter_evaluate_expression(interpreter, expression->children[1]);
        if (left_operand.type != Primitive_Type::BOOLEAN ||
            right_operand.type != Primitive_Type::BOOLEAN) {
            logg("Left an right of Logic-Operator (&& or ||) must be boolean values: left operand type: %s, right operand type:  %s\n",
                variable_type_to_string(left_operand.type).characters,
                variable_type_to_string(right_operand.type).characters);
            return result;
        }

        result.type = Primitive_Type::BOOLEAN;
        switch (expression->type) {
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND: result.bool_value = left_operand.bool_value && right_operand.bool_value; break;
        case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR: result.bool_value = left_operand.bool_value || right_operand.bool_value; break;
        }
        return result;
    }
    else if (expression->type == AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT) {
        AST_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, expression->children[0]);
        if (val.type != Primitive_Type::BOOLEAN) {
            logg("Logical not only works on boolean value!\n");
            return result;
        }
        result.type = Primitive_Type::BOOLEAN;
        result.bool_value = !val.bool_value;
        return result;
    }
    else if (expression->type == AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE) {
        AST_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, expression->children[0]);
        if (val.type == Primitive_Type::BOOLEAN) {
            logg("Negate does not work on boolean values");
            return result;
        }
        if (val.type == Primitive_Type::FLOAT) {
            result.type = val.type;
            result.float_value = -val.float_value;
        }
        if (val.type == Primitive_Type::INTEGER) {
            result.type = val.type;
            result.int_value = -val.int_value;
        }
        return result;
    }

    logg("Expression type invalid!\n");
    return result;
}

AST_Interpreter_Statement_Result ast_interpreter_result_make_empty() {
    AST_Interpreter_Statement_Result result;
    result.is_break = false;
    result.is_continue = false;
    result.is_return = false;
    return result;
}

AST_Interpreter_Statement_Result ast_interpreter_result_make_break() {
    AST_Interpreter_Statement_Result result;
    result.is_break = true;
    result.is_continue = false;
    result.is_return = false;
    return result;
}

AST_Interpreter_Statement_Result ast_interpreter_result_make_continue() {
    AST_Interpreter_Statement_Result result;
    result.is_break = false;
    result.is_continue = true;
    result.is_return = false;
    return result;
}

AST_Interpreter_Statement_Result ast_interpreter_result_make_return(AST_Interpreter_Value val) {
    AST_Interpreter_Statement_Result result;
    result.is_break = false;
    result.is_continue = false;
    result.is_return = true;
    result.return_value = val;
    return result;
}

AST_Interpreter_Statement_Result ast_interpreter_execute_statement(AST_Interpreter* interpreter, int statement_index);
AST_Interpreter_Statement_Result ast_interpreter_execute_statment_block(AST_Interpreter* interpreter, int block_index)
{
    AST_Node* block = &interpreter->analyser->parser->nodes[block_index];
    ast_interpreter_begin_new_scope(interpreter);
    for (int i = 0; i < block->children.size; i++) {
        AST_Interpreter_Statement_Result result = ast_interpreter_execute_statement(interpreter, block->children[i]);
        if (result.is_return || result.is_continue || result.is_break) {
            ast_interpreter_exit_scope(interpreter);
            return result;
        }
    }
    ast_interpreter_exit_scope(interpreter);
    return ast_interpreter_result_make_empty();
}

AST_Interpreter_Statement_Result ast_interpreter_execute_statement(AST_Interpreter* interpreter, int statement_index)
{
    AST_Interpreter_Statement_Result result = ast_interpreter_result_make_empty();
    AST_Node* statement = &interpreter->analyser->parser->nodes[statement_index];

    if (statement->type == AST_Node_Type::STATEMENT_RETURN) {
        result.is_return = true;
        result.return_value = ast_interpreter_evaluate_expression(interpreter, statement->children[0]);
        return result;
    }
    else if (statement->type == AST_Node_Type::STATEMENT_BREAK) {
        return ast_interpreter_result_make_break();
    }
    else if (statement->type == AST_Node_Type::STATEMENT_CONTINUE) {
        return ast_interpreter_result_make_continue();
    }
    else if (statement->type == AST_Node_Type::STATEMENT_BLOCK) {
        return ast_interpreter_execute_statment_block(interpreter, statement->children[0]);
    }
    else if (statement->type == AST_Node_Type::STATEMENT_EXPRESSION) // Expression can be function call, thats why we need it here
    {
        ast_interpreter_evaluate_expression(interpreter, statement->children[0]);
        return result;
    }
    else if (statement->type == AST_Node_Type::STATEMENT_WHILE)
    {
        while (true)
        {
            AST_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, statement->children[0]);
            if (val.type != Primitive_Type::BOOLEAN) {
                logg("WHILE condition is not a boolean!\n");
                return result;
            }
            if (val.bool_value) {
                AST_Interpreter_Statement_Result res = ast_interpreter_execute_statment_block(interpreter, statement->children[1]);
                if (res.is_return) return result;
                if (res.is_continue) continue;
                if (res.is_break) return result;
            }
            else {
                return result;
            }
        }
    }
    else if (statement->type == AST_Node_Type::STATEMENT_IF) {
        AST_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, statement->children[0]);
        if (val.type != Primitive_Type::BOOLEAN) {
            logg("If expression is not a boolean!\n");
            return result;
        }
        if (val.bool_value) {
            return ast_interpreter_execute_statment_block(interpreter, statement->children[1]);
        }
    }
    else if (statement->type == AST_Node_Type::STATEMENT_IF_ELSE) {
        AST_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, statement->children[0]);
        if (val.type != Primitive_Type::BOOLEAN) {
            logg("If expression is not a boolean!\n");
            return result;
        }
        if (val.bool_value) {
            return ast_interpreter_execute_statment_block(interpreter, statement->children[1]);
        }
        else {
            return ast_interpreter_execute_statment_block(interpreter, statement->children[2]);
        }
    }
    else if (statement->type == AST_Node_Type::STATEMENT_VARIABLE_DEFINITION)
    {
        bool unused;
        Symbol* s = symbol_table_find_symbol(
            interpreter->analyser->symbol_tables[interpreter->analyser->semantic_information[statement_index]],
            statement->name_id, &unused
        );
        ast_interpreter_define_variable(interpreter, s->variable_type, statement->name_id);
    }
    else if (statement->type == AST_Node_Type::STATEMENT_ASSIGNMENT)
    {
        AST_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, statement->name_id);
        var->value = ast_interpreter_evaluate_expression(interpreter, statement->children[0]);
    }
    else if (statement->type == AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN) // x : int = 5
    {
        AST_Interpreter_Value value = ast_interpreter_evaluate_expression(interpreter, statement->children[0]);
        ast_interpreter_define_variable(interpreter, value.type, statement->name_id);
        AST_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, statement->name_id);
        var->value = value;
    }
    else if (statement->type == AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER)
    {
        AST_Interpreter_Value value = ast_interpreter_evaluate_expression(interpreter, statement->children[0]);
        ast_interpreter_define_variable(interpreter, value.type, statement->name_id);
        AST_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, statement->name_id);
        var->value = value;
    }
    return result;
}

AST_Interpreter_Value ast_interpreter_execute_main(AST_Interpreter* interpreter, Semantic_Analyser* analyser)
{
    interpreter->analyser = analyser;
    AST_Interpreter_Value error_value;
    error_value.type = Primitive_Type::ERROR_TYPE;
    dynamic_array_reset(&interpreter->argument_evaluation_buffer);
    dynamic_array_reset(&interpreter->function_scope_beginnings);
    dynamic_array_reset(&interpreter->scope_beginnings);
    dynamic_array_reset(&interpreter->symbol_table);
    dynamic_array_push_back(&interpreter->scope_beginnings, 0);
    dynamic_array_push_back(&interpreter->function_scope_beginnings, 0);

    // Find main
    AST_Node* main = 0;
    {
        int* main_identifer = hashtable_find_element(&analyser->parser->lexer->identifier_index_lookup_table, string_create_static("main"));
        if (main_identifer == 0) {
            logg("Main not defined\n");
            return error_value;
        }
        Symbol_Table* root_table = analyser->symbol_tables[analyser->semantic_information[0]];
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_of_type(root_table, *main_identifer, Symbol_Type::FUNCTION, &in_current_scope);
        if (s == 0) {
            logg("Main not defined\n");
            return error_value;
        }
        main = &analyser->parser->nodes[s->function_index];
    }

    AST_Interpreter_Statement_Result main_result = ast_interpreter_execute_statment_block(interpreter, main->children[1]);
    if (!main_result.is_return) {
        logg("No return statement found!\n");
        return error_value;
    }

    return main_result.return_value;
}

void ast_interpreter_value_append_to_string(AST_Interpreter_Value value, String* string)
{
    switch (value.type)
    {
    case Primitive_Type::BOOLEAN:
        string_append_formated(string, "BOOL: %s ", value.bool_value ? "true" : "false"); break;
    case Primitive_Type::INTEGER:
        string_append_formated(string, "INT: %d ", value.int_value); break;
    case Primitive_Type::FLOAT:
        string_append_formated(string, "FLOAT: %f ", value.float_value); break;
    case Primitive_Type::ERROR_TYPE:
        string_append_formated(string, "ERROR-Type "); break;
    default:
        string_append_formated(string, "SHOULD_NOT_HAPPEN.EXE"); break;
    }
    return;
    return;
}

