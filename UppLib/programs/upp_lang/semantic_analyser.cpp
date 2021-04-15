#include "semantic_analyser.hpp"

#include "../../datastructures/string.hpp"

Semantic_Node_Information semantic_node_information_make(int symbol_table_index, int expression_result_type)
{
    Semantic_Node_Information info;
    info.symbol_table_index = symbol_table_index;
    info.expression_result_type_index = expression_result_type;
    return info;
}

Type_Signature type_signature_make_primitive(Primitive_Type type) {
    Type_Signature result;
    result.type = Signature_Type::PRIMITIVE;
    result.primtive_type = type;
    return result;
}

Type_Signature type_signature_make(Signature_Type type) {
    Type_Signature result;
    result.type = type;
    return result;
}

Type_Signature type_signature_make_pointer(int type_index_pointed_to) {
    Type_Signature result;
    result.type = Signature_Type::POINTER;
    result.pointed_to_type_index = type_index_pointed_to;
    return result;
}

void type_signature_destroy(Type_Signature* sig) {
    if (sig->type == Signature_Type::FUNCTION)
        dynamic_array_destroy(&sig->parameter_type_indices);
}

String variable_type_to_string(Primitive_Type type)
{
    switch (type)
    {
    case Primitive_Type::BOOLEAN:
        return string_create_static("BOOL");
    case Primitive_Type::INTEGER:
        return string_create_static("INT");
    case Primitive_Type::FLOAT:
        return string_create_static("FLOAT");
    }
    return string_create_static("INVALID_VALUE_TYPE_ENUM");
}

Symbol_Table symbol_table_create(Symbol_Table* parent)
{
    Symbol_Table result;
    result.parent = parent;
    result.symbols = dynamic_array_create_empty<Symbol>(8);
    return result;
}

void symbol_table_destroy(Symbol_Table* table) {
    dynamic_array_destroy(&table->symbols);
}

Symbol* symbol_table_find_symbol(Symbol_Table* table, int name, bool* in_current_scope) 
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

Symbol* symbol_table_find_symbol_of_type_with_scope_info(Symbol_Table* table, int name, Symbol_Type::ENUM symbol_type, bool* in_current_scope)
{
    *in_current_scope = false;
    for (int i = 0; i < table->symbols.size; i++) {
        if (table->symbols[i].name == name && table->symbols[i].symbol_type == symbol_type) {
            *in_current_scope = true;
            return &table->symbols[i];
        }
    }
    if (table->parent != 0) {
        Symbol* result = symbol_table_find_symbol_of_type_with_scope_info(table->parent, name, symbol_type, in_current_scope);
        *in_current_scope = false;
        return result;
    }
    return 0;
}

Symbol* symbol_table_find_symbol_of_type(Symbol_Table* table, int name, Symbol_Type::ENUM symbol_type)
{
    for (int i = 0; i < table->symbols.size; i++) {
        if (table->symbols[i].name == name && table->symbols[i].symbol_type == symbol_type) {
            return &table->symbols[i];
        }
    }
    if (table->parent != 0) {
        Symbol* result = symbol_table_find_symbol_of_type(table->parent, name, symbol_type);
        return result;
    }
    return 0;
}

void symbol_table_define_type(Symbol_Table* table, int name_id, int type_index)
{
    Symbol* sym = symbol_table_find_symbol_of_type(table, name_id, Symbol_Type::TYPE);
    if (sym != 0) {
        panic("Types should not overlap currently!\n");
        return;
    }

    Symbol s;
    s.symbol_type = Symbol_Type::TYPE;
    s.type_index = type_index;
    s.name = name_id;
    dynamic_array_push_back(&table->symbols, s);
}

/*
    SEMANTIC ANALYSER
*/
void semantic_analyser_log_error(Semantic_Analyser* analyser, const char* msg, int node_index)
{
    Compiler_Error error;
    error.message = msg;
    error.range = analyser->parser->token_mapping[node_index];
    dynamic_array_push_back(&analyser->errors, error);
}

void semantic_analyser_log_error(Semantic_Analyser* analyser, const char* msg, int node_start_index, int node_end_index)
{
    Compiler_Error error;
    error.message = msg;
    error.range.start_index = analyser->parser->token_mapping[node_start_index].start_index;
    error.range.end_index = analyser->parser->token_mapping[node_end_index].end_index;
    dynamic_array_push_back(&analyser->errors, error);
}

Symbol_Table* semantic_analyser_install_symbol_table(Semantic_Analyser* analyser, Symbol_Table* parent, int node_index) 
{
    Symbol_Table* table = new Symbol_Table();
    *table = symbol_table_create(parent);
    dynamic_array_push_back(&analyser->symbol_tables, table);
    analyser->semantic_information[node_index].symbol_table_index = analyser->symbol_tables.size-1;
    return table;
}

void semantic_analyser_define_variable(Semantic_Analyser* analyser, Symbol_Table* table, int node_index, int type_index) 
{
    bool in_current_scope;
    int var_name = analyser->parser->nodes[node_index].name_id;
    Symbol* var_symbol = symbol_table_find_symbol_of_type_with_scope_info(table, var_name, Symbol_Type::VARIABLE, &in_current_scope);
    if (var_symbol != 0 && in_current_scope) {
        semantic_analyser_log_error(analyser, "Variable already defined!", node_index);
        return;
    }

    Symbol s;
    s.symbol_type = Symbol_Type::VARIABLE;
    s.type_index = type_index;
    s.name = var_name;
    dynamic_array_push_back(&table->symbols, s);
}

int semantic_analyser_find_or_create_type_signature(Semantic_Analyser* analyser, Type_Signature s, bool* already_defined)
{
    *already_defined = true;
    for (int i = 0; i < analyser->types.size; i++) {
        Type_Signature* cmp = &analyser->types[i];
        if (cmp->type == s.type) {
            if (cmp->type == Signature_Type::POINTER && cmp->pointed_to_type_index == s.pointed_to_type_index) return i;
            if (cmp->type == Signature_Type::ERROR_TYPE) return i;
            if (cmp->type == Signature_Type::PRIMITIVE) return i;
            if (cmp->type == Signature_Type::FUNCTION) {
                if (cmp->parameter_type_indices.size != s.parameter_type_indices.size) continue;
                for (int i = 0; i < cmp->parameter_type_indices.size; i++) {
                    if (cmp->parameter_type_indices[i] != s.parameter_type_indices[i]) continue;
                }
                return i;
            }
        }
    }
    *already_defined = false;
    dynamic_array_push_back(&analyser->types, s);
    return analyser->types.size - 1;
}

int semantic_analyser_analyse_type(Semantic_Analyser* analyser, int type_node_index)
{
    AST_Node* type_node = &analyser->parser->nodes[type_node_index];
    switch (type_node->type)
    {
    case AST_Node_Type::TYPE_IDENTIFIER:
    {
        Symbol* symbol_type = symbol_table_find_symbol_of_type(analyser->symbol_tables[0], type_node->name_id, Symbol_Type::TYPE);
        if (symbol_type == 0) {
            semantic_analyser_log_error(analyser, "Invalid type, identifier is not a type!", type_node_index);
            return analyser->error_type_index;
        }
        return symbol_type->type_index;
    }
    case AST_Node_Type::TYPE_POINTER_TO:
        Type_Signature s;
        s.type = Signature_Type::POINTER;
        s.pointed_to_type_index = semantic_analyser_analyse_type(analyser, type_node->children[0]);
        bool unused;
        return semantic_analyser_find_or_create_type_signature(analyser, s, &unused);
    }

    panic("This should not happen, this means that the child was not a type!\n");
    return -1;
}

Expression_Analysis_Result expression_analysis_result_make(int result_type_index, bool has_memory_address)
{
    Expression_Analysis_Result result;
    result.type_index = result_type_index;
    result.has_memory_address = has_memory_address;
    return result;
}

Expression_Analysis_Result semantic_analyser_analyse_expression(Semantic_Analyser* analyser, Symbol_Table* table, int expression_index)
{
    AST_Node* expression = &analyser->parser->nodes[expression_index];
    analyser->semantic_information[expression_index].expression_result_type_index = analyser->error_type_index;

    bool is_binary_op = false;
    bool is_unary_op = false;
    bool int_valid, float_valid, bool_valid;
    int_valid = float_valid = bool_valid = false;
    bool return_left_type = false;
    int return_type_index;
    switch (expression->type)
    {
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL: 
    {
        Symbol* func_symbol = symbol_table_find_symbol_of_type(table, expression->name_id, Symbol_Type::FUNCTION);
        if (func_symbol == 0) {
            semantic_analyser_log_error(analyser, "Function call to not defined Function!", expression_index);
            return expression_analysis_result_make(analyser->error_type_index, true);
        }

        Type_Signature* signature = &analyser->types[func_symbol->type_index];
        if (expression->children.size != signature->parameter_type_indices.size) {
            semantic_analyser_log_error(analyser, "Argument size does not match function parameter size!", expression_index);
        }
        for (int i = 0; i < signature->parameter_type_indices.size && i < expression->children.size; i++) 
        {
            Expression_Analysis_Result expr_result = semantic_analyser_analyse_expression(analyser, table, expression->children[i]);
            if (expr_result.type_index != signature->parameter_type_indices[i] || expr_result.type_index == analyser->error_type_index) {
                semantic_analyser_log_error(analyser, "Argument type does not match parameter type", expression->children[i]);
            }
        }

        analyser->semantic_information[expression_index].expression_result_type_index = signature->return_type_index;
        return expression_analysis_result_make(signature->return_type_index, false);
    }
    case AST_Node_Type::EXPRESSION_VARIABLE_READ: 
    {
        Symbol* s = symbol_table_find_symbol_of_type(table, expression->name_id, Symbol_Type::VARIABLE);
        if (s == 0) {
            semantic_analyser_log_error(analyser, "Expression variable not defined", expression_index);
            return expression_analysis_result_make(analyser->error_type_index, true);
        }
        analyser->semantic_information[expression_index].expression_result_type_index = s->type_index;
        return expression_analysis_result_make(s->type_index, true);
    }
    case AST_Node_Type::EXPRESSION_LITERAL: 
    {
        Token_Type::ENUM type = analyser->parser->lexer->tokens[analyser->parser->token_mapping[expression_index].start_index].type;
        if (type == Token_Type::BOOLEAN_LITERAL) {
            analyser->semantic_information[expression_index].expression_result_type_index = analyser->bool_type_index;
            return expression_analysis_result_make(analyser->bool_type_index, false);
        }
        if (type == Token_Type::INTEGER_LITERAL) {
            analyser->semantic_information[expression_index].expression_result_type_index = analyser->int_type_index;
            return expression_analysis_result_make(analyser->int_type_index, false);
        }
        if (type == Token_Type::FLOAT_LITERAL) {
            analyser->semantic_information[expression_index].expression_result_type_index = analyser->float_type_index;
            return expression_analysis_result_make(analyser->float_type_index, false);
        }
        panic("This should not happend\n");
        break;
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_ADDITION: 
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_SUBTRACTION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_DIVISION:
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MULTIPLICATION: {
        is_binary_op = true;
        int_valid = float_valid = true;
        return_left_type = true;
        break;
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER: 
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL: 
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS: 
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL: {
        is_binary_op = true;
        int_valid = float_valid = true;
        return_type_index = analyser->bool_type_index;
        break;
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_MODULO: {
        is_binary_op = true;
        int_valid = true;
        return_left_type = true;
        break;
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_AND: 
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_OR: {
        is_binary_op = true;
        bool_valid = true;
        return_left_type = true;
        break;
    }
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_EQUAL: 
    case AST_Node_Type::EXPRESSION_BINARY_OPERATION_NOT_EQUAL: {
        is_binary_op = true;
        bool_valid, int_valid, float_valid = true;
        return_type_index = analyser->bool_type_index;
        break;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT: {
        is_unary_op = true;
        bool_valid = true;
        return_type_index = analyser->bool_type_index;
        break;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE: {
        is_unary_op = true;
        float_valid = int_valid = true;
        return_left_type = true;
        break;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_ADDRESS_OF: 
    {
        Expression_Analysis_Result result = semantic_analyser_analyse_expression(analyser, table, expression->children[0]);
        if (!result.has_memory_address) {
            semantic_analyser_log_error(analyser, "Cannot get address of expression!", expression->children[0]);
        }
        bool unused;
        int result_type = semantic_analyser_find_or_create_type_signature(analyser, type_signature_make_pointer(result.type_index), &unused);
        analyser->semantic_information[expression_index].expression_result_type_index = result_type;
        return expression_analysis_result_make(result_type, false);
        break;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_DEREFERENCE: {
        Expression_Analysis_Result result = semantic_analyser_analyse_expression(analyser, table, expression->children[0]);
        Type_Signature* signature = &analyser->types[result.type_index];
        if (signature->type != Signature_Type::POINTER) {
            semantic_analyser_log_error(analyser, "Tried to dereference non pointer type!", expression->children[0]);
            return expression_analysis_result_make(analyser->error_type_index, false);
        }
        analyser->semantic_information[expression_index].expression_result_type_index = signature->pointed_to_type_index;
        return expression_analysis_result_make(signature->pointed_to_type_index, true);
        break;
    }
    default: {
        panic("Not all expression covered!\n");
        break;
    }
    }

    if (is_binary_op) 
    {
        Expression_Analysis_Result left_expr_result = semantic_analyser_analyse_expression(analyser, table, expression->children[0]);
        Expression_Analysis_Result right_expr_result = semantic_analyser_analyse_expression(analyser, table, expression->children[1]);
        if (left_expr_result.type_index != right_expr_result.type_index) {
            semantic_analyser_log_error(analyser, "Left and right of binary operation do not match", expression_index);
        }
        if (!int_valid && left_expr_result.type_index == analyser->int_type_index) {
            semantic_analyser_log_error(analyser, "Operands cannot be integers", expression_index);
            return expression_analysis_result_make(analyser->error_type_index, false);
        }
        if (!bool_valid && left_expr_result.type_index == analyser->bool_type_index) {
            semantic_analyser_log_error(analyser, "Operands cannot be booleans", expression_index);
            return expression_analysis_result_make(analyser->error_type_index, false);
        }
        if (!float_valid && left_expr_result.type_index == analyser->float_type_index) {
            semantic_analyser_log_error(analyser, "Operands cannot be floats", expression_index);
            return expression_analysis_result_make(analyser->error_type_index, false);
        }
        if (return_left_type) {
            analyser->semantic_information[expression_index].expression_result_type_index = left_expr_result.type_index;
            return expression_analysis_result_make(left_expr_result.type_index, false);
        }
        analyser->semantic_information[expression_index].expression_result_type_index = return_type_index;
        return expression_analysis_result_make(return_type_index, false);
    }
    if (is_unary_op) 
    {
        int left_type_index = semantic_analyser_analyse_expression(analyser, table, expression->children[0]).type_index;
        if (!int_valid && left_type_index ==analyser->int_type_index) {
            semantic_analyser_log_error(analyser, "Operand cannot be integer", expression_index);
            return expression_analysis_result_make(analyser->error_type_index, false);
        }
        if (!bool_valid && left_type_index == analyser->bool_type_index) {
            semantic_analyser_log_error(analyser, "Operand cannot be boolean", expression_index);
            return expression_analysis_result_make(analyser->error_type_index, false);
        }
        if (!float_valid && left_type_index == analyser->float_type_index) {
            semantic_analyser_log_error(analyser, "Operand cannot be float", expression_index);
            return expression_analysis_result_make(analyser->error_type_index, false);
        }
        if (return_left_type) {
            analyser->semantic_information[expression_index].expression_result_type_index = left_type_index;
            return expression_analysis_result_make(left_type_index, false);
        }
        analyser->semantic_information[expression_index].expression_result_type_index = return_type_index;
        return expression_analysis_result_make(return_type_index, false);
    }

    return expression_analysis_result_make(return_type_index, false);
}

Statement_Analysis_Result semantic_analyser_analyse_statement_block(Semantic_Analyser* analyser, Symbol_Table* parent, int block_index, bool create_symbol_table);
Statement_Analysis_Result semantic_analyser_analyse_statement(Semantic_Analyser* analyser, Symbol_Table* parent, int statement_index)
{
    AST_Node* statement = &analyser->parser->nodes[statement_index];
    switch (statement->type)
    {
    case AST_Node_Type::STATEMENT_RETURN: {
        int return_type_index = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]).type_index;
        if (return_type_index != analyser->function_return_type_index) {
            semantic_analyser_log_error(analyser, "Return type does not match function return type", statement_index);
        }
        return Statement_Analysis_Result::RETURN;
    }
    case AST_Node_Type::STATEMENT_BREAK: {
        if (analyser->loop_depth <= 0) {
            semantic_analyser_log_error(analyser, "Break not inside loop!", statement_index);
        }
        return Statement_Analysis_Result::BREAK;
    }
    case AST_Node_Type::STATEMENT_CONTINUE: {
        if (analyser->loop_depth <= 0) {
            semantic_analyser_log_error(analyser, "Continue not inside loop!", statement_index);
        }
        return Statement_Analysis_Result::CONTINUE;
    }
    case AST_Node_Type::STATEMENT_EXPRESSION: {
        AST_Node* node = &analyser->parser->nodes[statement->children[0]];
        if (node->type != AST_Node_Type::EXPRESSION_FUNCTION_CALL) {
            semantic_analyser_log_error(analyser, "Expression statement must be funciton call!", statement_index);
            return Statement_Analysis_Result::NO_RETURN;
        }
        semantic_analyser_analyse_expression(analyser, parent, statement->children[0]);
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_BLOCK: {
        return semantic_analyser_analyse_statement_block(analyser, parent, statement->children[0], true);
    }
    case AST_Node_Type::STATEMENT_IF:
    {
        int condition_type_index = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]).type_index;
        if (condition_type_index != analyser->bool_type_index) {
            semantic_analyser_log_error(analyser, "If condition must be of boolean type!", statement_index);
        }
        semantic_analyser_analyse_statement_block(analyser, parent, statement->children[1], true);
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_IF_ELSE:
    {
        int condition_type_index = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]).type_index;
        if (condition_type_index != analyser->bool_type_index) {
            semantic_analyser_log_error(analyser, "If condition must be of boolean type!", statement_index);
        }
        Statement_Analysis_Result if_result = semantic_analyser_analyse_statement_block(analyser, parent, statement->children[1], true);
        Statement_Analysis_Result else_result = semantic_analyser_analyse_statement_block(analyser, parent, statement->children[2], true);
        if (if_result == else_result) return if_result;
        return Statement_Analysis_Result::NO_RETURN; // Maybe i need to do something different here, but I dont think so
    }
    case AST_Node_Type::STATEMENT_WHILE:
    {
        int condition_type_index = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]).type_index;
        if (condition_type_index != analyser->bool_type_index) {
            semantic_analyser_log_error(analyser, "If condition must be of boolean type!", statement_index);
        }
        analyser->loop_depth++;
        Statement_Analysis_Result block_result = semantic_analyser_analyse_statement_block(analyser, parent, statement->children[1], true);
        analyser->loop_depth--;
        if (block_result == Statement_Analysis_Result::RETURN) {
            semantic_analyser_log_error(analyser, "While loop never runs more than once, since it always returns!", statement_index);
        }
        else if (block_result == Statement_Analysis_Result::CONTINUE) {
            semantic_analyser_log_error(analyser, "While loop stops, since it always continues!", statement_index);
        }
        else if (block_result == Statement_Analysis_Result::BREAK) {
            semantic_analyser_log_error(analyser, "While loop never more than once, since it always breaks!", statement_index);
        }
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_ASSIGNMENT:
    {
        Expression_Analysis_Result left_result = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]);
        Expression_Analysis_Result right_result = semantic_analyser_analyse_expression(analyser, parent, statement->children[1]);
        if (!left_result.has_memory_address) {
            semantic_analyser_log_error(analyser, "Left side of assignment cannot be assigned to, does not have a memory address", statement_index);
        }
        if (left_result.type_index != right_result.type_index) {
            semantic_analyser_log_error(analyser, "Left side of assignment is not the same as right side", statement_index);
        }
        return Statement_Analysis_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION:
    {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_of_type_with_scope_info(parent, statement->name_id, Symbol_Type::VARIABLE, &in_current_scope);
        if (s != 0 && in_current_scope) {
            semantic_analyser_log_error(analyser, "Variable already defined", statement_index);
            break;
        }
        int var_type_index = semantic_analyser_analyse_type(analyser, statement->children[0]);
        semantic_analyser_define_variable(analyser, parent, statement_index, var_type_index);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
    {
        bool in_current_scope;
        {
            Symbol* s = symbol_table_find_symbol_of_type_with_scope_info(parent, statement->name_id, Symbol_Type::VARIABLE, &in_current_scope);
            if (s != 0 && in_current_scope) {
                semantic_analyser_log_error(analyser, "Variable already defined", statement_index);
                break;
            }
        }
        int var_type_index = semantic_analyser_analyse_type(analyser, statement->children[0]);
        int assignment_type_index = semantic_analyser_analyse_expression(analyser, parent, statement->children[1]).type_index;
        if (assignment_type_index != var_type_index && assignment_type_index != analyser->error_type_index) {
            semantic_analyser_log_error(analyser, "Variable type does not match expression type", statement_index);
        }
        semantic_analyser_define_variable(analyser, parent, statement_index, var_type_index);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER:
    {
        {
            bool in_current_scope;
            Symbol* s = symbol_table_find_symbol_of_type_with_scope_info(parent, statement->name_id, Symbol_Type::VARIABLE, &in_current_scope);
            if (s != 0 && in_current_scope) {
                semantic_analyser_log_error(analyser, "Variable already defined", statement_index);
                break;
            }
        }
        semantic_analyser_define_variable(analyser, parent, statement_index, 
            semantic_analyser_analyse_expression(analyser, parent, statement->children[0]).type_index);
        break;
    }
    default: {
        panic("Should be covered!\n");
        break;
    }
    }

    return Statement_Analysis_Result::NO_RETURN;
}

Statement_Analysis_Result semantic_analyser_analyse_statement_block(Semantic_Analyser* analyser, Symbol_Table* parent, int block_index, bool create_symbol_table)
{
    Symbol_Table* table;
    if (create_symbol_table)
        table = semantic_analyser_install_symbol_table(analyser, parent, block_index);
    else
        table = parent;

    int result_type_found = false; // Continue or break make 'dead code' returns or other things invalid
    Statement_Analysis_Result result = Statement_Analysis_Result::NO_RETURN;
    AST_Node* block = &analyser->parser->nodes[block_index];
    for (int i = 0; i < block->children.size; i++)
    {
        Statement_Analysis_Result statement_result = semantic_analyser_analyse_statement(analyser, table, block->children[i]);
        switch (statement_result)
        {
        case Statement_Analysis_Result::BREAK:
        case Statement_Analysis_Result::CONTINUE: {
            if (!result_type_found)
            {
                result = Statement_Analysis_Result::NO_RETURN;
                if (i != block->children.size - 1) {
                    // This should probably be a warning
                    semantic_analyser_log_error(analyser, "Code will never be reached, break or continue before prevents that!",
                        block->children[i + 1], block->children[block->children.size - 1]);
                }
                result_type_found = true;
            }
            break;
        }
        case Statement_Analysis_Result::RETURN:
            if (!result_type_found)
            {
                result = Statement_Analysis_Result::RETURN;
                if (i != block->children.size - 1) {
                    // This should probably be a warning
                    semantic_analyser_log_error(analyser, "Code will never be reached, return before prevents that!",
                        block->children[i + 1], block->children[block->children.size - 1]);
                }
                result_type_found = true;
            }
            break;
        case Statement_Analysis_Result::NO_RETURN:
            break;
        }
    }
    return result;
}

void semantic_analyser_analyse_function(Semantic_Analyser* analyser, Symbol_Table* parent, int function_index)
{
    AST_Node* function = &analyser->parser->nodes[function_index];
    Symbol_Table* table = semantic_analyser_install_symbol_table(analyser, parent, function_index);

    // Define parameter variables
    AST_Node* parameter_block = &analyser->parser->nodes[function->children[0]];
    Type_Signature* function_signature = &analyser->types[symbol_table_find_symbol_of_type(parent, function->name_id, Symbol_Type::FUNCTION)->type_index];
    for (int i = 0; i < parameter_block->children.size; i++) {
        semantic_analyser_define_variable(analyser, table, parameter_block->children[i], function_signature->parameter_type_indices[i]);
    }

    analyser->function_return_type_index = function_signature->return_type_index;
    analyser->loop_depth = 0;
    Statement_Analysis_Result result = semantic_analyser_analyse_statement_block(analyser, table, function->children[2], false);
    if (result != Statement_Analysis_Result::RETURN) {
        semantic_analyser_log_error(analyser, "Not all code paths return a value!", function_index);
    }
}

// Semantic Analyser
Semantic_Analyser semantic_analyser_create()
{
    Semantic_Analyser result;
    result.symbol_tables = dynamic_array_create_empty<Symbol_Table*>(64);
    result.semantic_information = dynamic_array_create_empty<Semantic_Node_Information>(64);
    result.errors = dynamic_array_create_empty<Compiler_Error>(64);
    result.types = dynamic_array_create_empty<Type_Signature>(64);
    return result;
}

void semantic_analyser_destroy(Semantic_Analyser* analyser)
{
    for (int i = 0; i < analyser->symbol_tables.size; i++) {
        symbol_table_destroy(analyser->symbol_tables[i]);
        delete analyser->symbol_tables[i];
    }
    dynamic_array_destroy(&analyser->symbol_tables);
    dynamic_array_destroy(&analyser->semantic_information);
    dynamic_array_destroy(&analyser->errors);
    dynamic_array_destroy(&analyser->types);
}

void semantic_analyser_analyse_function_header(Semantic_Analyser* analyser, Symbol_Table* table, int function_index) 
{
    AST_Node* function = &analyser->parser->nodes[function_index];
    int function_name = analyser->parser->nodes[function_index].name_id;
    Symbol* func = symbol_table_find_symbol_of_type(table, function_name, Symbol_Type::FUNCTION);
    if (func != 0) {
        semantic_analyser_log_error(analyser, "Function already defined!", function_index);
        return;
    }

    AST_Node* parameter_block = &analyser->parser->nodes[function->children[0]];
    Type_Signature function_signature;
    function_signature.type = Signature_Type::FUNCTION;
    function_signature.parameter_type_indices = dynamic_array_create_empty<int>(parameter_block->children.size);
    for (int i = 0; i < parameter_block->children.size; i++) {
        int parameter_index = parameter_block->children[i];
        AST_Node* parameter = &analyser->parser->nodes[parameter_index];
        dynamic_array_push_back(&function_signature.parameter_type_indices, semantic_analyser_analyse_type(analyser, parameter->children[0]));
    }
    function_signature.return_type_index = semantic_analyser_analyse_type(analyser, function->children[1]);
    bool already_defined;
    int function_type_index = semantic_analyser_find_or_create_type_signature(analyser, function_signature, &already_defined);
    if (already_defined) {
        dynamic_array_destroy(&function_signature.parameter_type_indices);
    }

    Symbol s;
    s.symbol_type = Symbol_Type::FUNCTION;
    s.name = function_name;
    s.function_index = function_index;
    s.type_index = function_type_index;
    dynamic_array_push_back(&table->symbols, s);
}

void semantic_analyser_analyse(Semantic_Analyser* analyser, AST_Parser* parser)
{
    // TODO: We could also reuse the previous memory in the symbol tables, like in the parser
    for (int i = 0; i < analyser->symbol_tables.size; i++) {
        symbol_table_destroy(analyser->symbol_tables[i]);
        delete analyser->symbol_tables[i];
    }
    for (int i = 0; i < analyser->types.size; i++) {
        type_signature_destroy(&analyser->types[i]);
    }
    dynamic_array_reset(&analyser->symbol_tables);
    dynamic_array_reset(&analyser->semantic_information);
    dynamic_array_reset(&analyser->errors);
    dynamic_array_reset(&analyser->types);
    analyser->parser = parser;

    dynamic_array_reserve(&analyser->semantic_information, parser->nodes.size);
    for (int i = 0; i < parser->nodes.size; i++) {
        dynamic_array_push_back(&analyser->semantic_information, semantic_node_information_make(0, 0));
    }

    Symbol_Table* root_table = semantic_analyser_install_symbol_table(analyser, 0, 0);
    // Define primitive types
    {
        analyser->error_type_index = 0;
        dynamic_array_push_back(&analyser->types, type_signature_make(Signature_Type::ERROR_TYPE));
        analyser->int_type_index = analyser->types.size;
        analyser->bool_type_index = analyser->types.size + 1;
        analyser->float_type_index = analyser->types.size + 2;
        dynamic_array_push_back(&analyser->types, type_signature_make_primitive(Primitive_Type::INTEGER));
        dynamic_array_push_back(&analyser->types, type_signature_make_primitive(Primitive_Type::BOOLEAN));
        dynamic_array_push_back(&analyser->types, type_signature_make_primitive(Primitive_Type::FLOAT));

        // Add tokens for basic datatypes
        int int_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("int"));
        int bool_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("bool"));
        int float_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("float"));
        symbol_table_define_type(root_table, int_token_index, analyser->int_type_index);
        symbol_table_define_type(root_table, bool_token_index, analyser->bool_type_index);
        symbol_table_define_type(root_table, float_token_index, analyser->float_type_index);
    }

    // Add all functions to root_table
    AST_Node* root = &analyser->parser->nodes[0];
    for (int i = 0; i < root->children.size; i++) {
        semantic_analyser_analyse_function_header(analyser, root_table, root->children[i]);
    }
    analyser->semantic_information[0].symbol_table_index = 0;

    // Analyse all functions
    for (int i = 0; i < root->children.size; i++) {
        semantic_analyser_analyse_function(analyser, root_table, root->children[i]);
    }
}
