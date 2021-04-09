#include "semantic_analyser.hpp"

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

Symbol* symbol_table_find_symbol_of_type(Symbol_Table* table, int name, Symbol_Type::ENUM symbol_type, bool* in_current_scope)
{
    *in_current_scope = false;
    for (int i = 0; i < table->symbols.size; i++) {
        if (table->symbols[i].name == name && table->symbols[i].symbol_type == symbol_type) {
            *in_current_scope = true;
            return &table->symbols[i];
        }
    }
    if (table->parent != 0) {
        Symbol* result = symbol_table_find_symbol_of_type(table->parent, name, symbol_type, in_current_scope);
        *in_current_scope = false;
        return result;
    }
    return 0;
}

void symbol_table_define_type(Symbol_Table* table, int name_id, Variable_Type::ENUM variable_type)
{
    bool in_current_scope;
    Symbol* sym = symbol_table_find_symbol_of_type(table, name_id, Symbol_Type::TYPE, &in_current_scope);
    if (sym != 0) {
        panic("Types should not overlap currently!\n");
        return;
    }

    Symbol s;
    s.symbol_type = Symbol_Type::TYPE;
    s.variable_type = variable_type;
    s.name = name_id;
    dynamic_array_push_back(&table->symbols, s);
}

Variable_Type::ENUM symbol_table_find_type(Symbol_Table* table, int name_id) 
{
    bool in_current_scope;
    Symbol* s = symbol_table_find_symbol_of_type(table, name_id, Symbol_Type::TYPE, &in_current_scope);
    if (s == 0) {
        return Variable_Type::ERROR_TYPE;
    }
    else return s->variable_type;
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
    analyser->node_to_table_mappings[node_index] = analyser->symbol_tables.size-1;
    return table;
}

void semantic_analyser_define_function(Semantic_Analyser* analyser, Symbol_Table* table, int function_index) 
{
    int function_name = analyser->parser->nodes[function_index].name_id;
    bool in_current_scope;
    Symbol* func = symbol_table_find_symbol_of_type(table, function_name, Symbol_Type::FUNCTION, &in_current_scope);
    if (func != 0 && in_current_scope) {
        semantic_analyser_log_error(analyser, "Function already defined!", function_index);
        return;
    }

    Symbol s;
    s.symbol_type = Symbol_Type::FUNCTION;
    s.name = function_name;
    s.function_index = function_index;
    dynamic_array_push_back(&table->symbols, s);
}

void semantic_analyser_define_variable(Semantic_Analyser* analyser, Symbol_Table* table, int node_index, Variable_Type::ENUM variable_type) 
{
    int var_name = analyser->parser->nodes[node_index].name_id;
    bool in_current_scope;
    Symbol* func = symbol_table_find_symbol_of_type(table, var_name, Symbol_Type::VARIABLE, &in_current_scope);
    if (func != 0 && in_current_scope) {
        semantic_analyser_log_error(analyser, "Variable already defined!", node_index);
        return;
    }

    Symbol s;
    s.symbol_type = Symbol_Type::VARIABLE;
    s.variable_type = variable_type;
    s.name = var_name;
    dynamic_array_push_back(&table->symbols, s);
}

Variable_Type::ENUM semantic_analyser_analyse_expression(Semantic_Analyser* analyser, Symbol_Table* table, int expression_index)
{
    AST_Node* expression = &analyser->parser->nodes[expression_index];

    bool is_binary_op = false;
    bool is_unary_op = false;
    bool int_valid, float_valid, bool_valid;
    int_valid = float_valid = bool_valid = false;
    bool return_left_type = false;
    Variable_Type::ENUM return_type;
    switch (expression->type)
    {
    case AST_Node_Type::EXPRESSION_FUNCTION_CALL: 
    {
        bool in_current_scope;
        Symbol* func_symbol = symbol_table_find_symbol_of_type(table, expression->name_id, Symbol_Type::FUNCTION, &in_current_scope);
        if (func_symbol == 0) {
            semantic_analyser_log_error(analyser, "Funciton not defined!", expression_index);
            return Variable_Type::ERROR_TYPE;
        }

        AST_Node* function = &analyser->parser->nodes[func_symbol->function_index];
        AST_Node* parameter_block = &analyser->parser->nodes[function->children[0]];
        if (expression->children.size != parameter_block->children.size) {
            semantic_analyser_log_error(analyser, "Argument size does not match function parameter size!", expression_index);
        }
        for (int i = 0; i < function->children.size && i < expression->children.size; i++) 
        {
            AST_Node* argument = &analyser->parser->nodes[expression->children[i]];
            Variable_Type::ENUM argument_type = semantic_analyser_analyse_expression(analyser, table, expression->children[i]);
            AST_Node* param = &analyser->parser->nodes[parameter_block->children[i]];
            Variable_Type::ENUM param_type = symbol_table_find_type(table, param->type_id);
            if (argument_type != param_type || argument_type == Variable_Type::ERROR_TYPE) {
                semantic_analyser_log_error(analyser, "Argument type does not match parameter type", parameter_block->children[i]);
            }
        }
        return symbol_table_find_type(table, function->type_id);
        break;
    }
    case AST_Node_Type::EXPRESSION_VARIABLE_READ: 
    {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_of_type(table, expression->name_id, Symbol_Type::VARIABLE, &in_current_scope);
        if (s == 0) {
            semantic_analyser_log_error(analyser, "Experssion variable not defined", expression_index);
            return Variable_Type::ERROR_TYPE;
        }
        return s->variable_type;
        break;
    }
    case AST_Node_Type::EXPRESSION_LITERAL: 
    {
        Token_Type::ENUM type = analyser->parser->lexer->tokens[analyser->parser->token_mapping[expression_index].start_index].type;
        if (type == Token_Type::BOOLEAN_LITERAL) {
            return Variable_Type::BOOLEAN;
        }
        if (type == Token_Type::INTEGER_LITERAL) {
            return Variable_Type::INTEGER;
        }
        if (type == Token_Type::FLOAT_LITERAL) {
            return Variable_Type::FLOAT;
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
        return_type = Variable_Type::BOOLEAN;
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
        return_type = Variable_Type::BOOLEAN;
        break;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NOT: {
        is_unary_op = true;
        bool_valid = true;
        return_type = Variable_Type::BOOLEAN;
        break;
    }
    case AST_Node_Type::EXPRESSION_UNARY_OPERATION_NEGATE: {
        is_unary_op = true;
        float_valid = int_valid = true;
        return_left_type = true;
        break;
    }
    default: {
        panic("Not all expression covered!\n");
        break;
    }
    }

    if (is_binary_op) 
    {
        Variable_Type::ENUM left_type = semantic_analyser_analyse_expression(analyser, table, expression->children[0]);
        Variable_Type::ENUM right_type = semantic_analyser_analyse_expression(analyser, table, expression->children[1]);
        if (left_type != right_type) {
            semantic_analyser_log_error(analyser, "Left and right of arithmenic op do not match", expression_index);
        }
        if (!int_valid && left_type == Variable_Type::INTEGER) {
            semantic_analyser_log_error(analyser, "Operands cannot be integers", expression_index);
            return Variable_Type::ERROR_TYPE;
        }
        if (!bool_valid && left_type == Variable_Type::BOOLEAN) {
            semantic_analyser_log_error(analyser, "Operands cannot be booleans", expression_index);
            return Variable_Type::ERROR_TYPE;
        }
        if (!float_valid && left_type == Variable_Type::FLOAT) {
            semantic_analyser_log_error(analyser, "Operands cannot be floats", expression_index);
            return Variable_Type::ERROR_TYPE;
        }
        if (return_left_type) return left_type;
        return return_type;
    }
    if (is_unary_op) 
    {
        Variable_Type::ENUM left_type = semantic_analyser_analyse_expression(analyser, table, expression->children[0]);
        if (!int_valid && left_type == Variable_Type::INTEGER) {
            semantic_analyser_log_error(analyser, "Operand cannot be integer", expression_index);
            return Variable_Type::ERROR_TYPE;
        }
        if (!bool_valid && left_type == Variable_Type::BOOLEAN) {
            semantic_analyser_log_error(analyser, "Operand cannot be boolean", expression_index);
            return Variable_Type::ERROR_TYPE;
        }
        if (!float_valid && left_type == Variable_Type::FLOAT) {
            semantic_analyser_log_error(analyser, "Operand cannot be float", expression_index);
            return Variable_Type::ERROR_TYPE;
        }
        if (return_left_type) return left_type;
        return return_type;
    }

    return Variable_Type::ERROR_TYPE;
}

namespace Analyser_Result
{
    enum ENUM {
        NO_RETURN,
        RETURN,
        CONTINUE,
        BREAK
    };
}

Analyser_Result::ENUM semantic_analyser_analyse_statement_block(Semantic_Analyser* analyser, Symbol_Table* parent, int block_index, bool create_symbol_table);
Analyser_Result::ENUM semantic_analyser_analyse_statement(Semantic_Analyser* analyser, Symbol_Table* parent, int statement_index)
{
    AST_Node* statement = &analyser->parser->nodes[statement_index];
    Variable_Type::ENUM result = Variable_Type::VOID_TYPE;
    switch (statement->type)
    {
    case AST_Node_Type::STATEMENT_RETURN: {
        Variable_Type::ENUM return_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]);
        if (return_type != analyser->function_return_type) {
            semantic_analyser_log_error(analyser, "Return type does not match function return type", statement_index);
        }
        return Analyser_Result::RETURN;
    }
    case AST_Node_Type::STATEMENT_BREAK: {
        if (analyser->loop_depth <= 0) {
            semantic_analyser_log_error(analyser, "Break not inside loop!", statement_index);
        }
        return Analyser_Result::BREAK;
    }
    case AST_Node_Type::STATEMENT_CONTINUE: {
        if (analyser->loop_depth <= 0) {
            semantic_analyser_log_error(analyser, "Continue not inside loop!", statement_index);
        }
        return Analyser_Result::CONTINUE;
    }
    case AST_Node_Type::STATEMENT_EXPRESSION: {
        AST_Node* node = &analyser->parser->nodes[statement->children[0]];
        if (node->type != AST_Node_Type::EXPRESSION_FUNCTION_CALL) {
            semantic_analyser_log_error(analyser, "Expression statement must be funciton call!", statement_index);
        }
        return Analyser_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_BLOCK: {
        return semantic_analyser_analyse_statement_block(analyser, parent, statement->children[0], true);
    }
    case AST_Node_Type::STATEMENT_IF: 
    {
        Variable_Type::ENUM condition_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]);
        if (condition_type != Variable_Type::BOOLEAN) {
            semantic_analyser_log_error(analyser, "If condition must be of boolean type!", statement_index);
        }
        semantic_analyser_analyse_statement_block(analyser, parent, statement->children[1], true);
        return Analyser_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_IF_ELSE: 
    {
        Variable_Type::ENUM condition_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]);
        if (condition_type != Variable_Type::BOOLEAN) {
            semantic_analyser_log_error(analyser, "If condition must be of boolean type!", statement_index);
        }
        Analyser_Result::ENUM if_result = semantic_analyser_analyse_statement_block(analyser, parent, statement->children[1], true);
        Analyser_Result::ENUM else_result = semantic_analyser_analyse_statement_block(analyser, parent, statement->children[2], true);
        if (if_result == else_result) return if_result;
        return Analyser_Result::NO_RETURN; // Maybe i need to do something different here, but I dont think so
    }
    case AST_Node_Type::STATEMENT_WHILE: 
    {
        Variable_Type::ENUM condition_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]);
        if (condition_type != Variable_Type::BOOLEAN) {
            semantic_analyser_log_error(analyser, "If condition must be of boolean type!", statement_index);
        }
        analyser->loop_depth++;
        Analyser_Result::ENUM block_result = semantic_analyser_analyse_statement_block(analyser, parent, statement->children[1], true);
        analyser->loop_depth--;
        if (block_result == Analyser_Result::RETURN) {
            semantic_analyser_log_error(analyser, "While loop never runs more than once, since it always returns!", statement_index);
        }
        else if (block_result == Analyser_Result::CONTINUE) {
            semantic_analyser_log_error(analyser, "While loop stops, since it always continues!", statement_index);
        }
        else if (block_result == Analyser_Result::BREAK) {
            semantic_analyser_log_error(analyser, "While loop never more than once, since it always breaks!", statement_index);
        }
        return Analyser_Result::NO_RETURN;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_ASSIGNMENT: 
    {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_of_type(parent, statement->name_id, Symbol_Type::VARIABLE, &in_current_scope);
        if (s == 0) {
            semantic_analyser_log_error(analyser, "Variable not defined, cannot be assigned to!", statement_index);
            break;
        }
        Variable_Type::ENUM assignment_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]);
        if (assignment_type != s->variable_type) {
            semantic_analyser_log_error(analyser, "Variable type does not match expression type", statement_index);
        }
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINITION:
    {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_of_type(parent, statement->name_id, Symbol_Type::VARIABLE, &in_current_scope);
        if (s != 0 && in_current_scope) {
            semantic_analyser_log_error(analyser, "Variable already defined", statement_index);
            break;
        }
        Symbol* var_type = symbol_table_find_symbol_of_type(parent, statement->type_id, Symbol_Type::TYPE, &in_current_scope);
        if (var_type == 0) {
            semantic_analyser_log_error(analyser, "Variable definition failed, variable type is invalid", statement_index);
            break;
        }
        semantic_analyser_define_variable(analyser, parent, statement_index, var_type->variable_type);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_ASSIGN:
    {
        bool in_current_scope;
        {
            Symbol* s = symbol_table_find_symbol_of_type(parent, statement->name_id, Symbol_Type::VARIABLE, &in_current_scope);
            if (s != 0 && in_current_scope) {
                semantic_analyser_log_error(analyser, "Variable already defined", statement_index);
                break;
            }
        }
        Symbol* var_type = symbol_table_find_symbol_of_type(parent, statement->type_id, Symbol_Type::TYPE, &in_current_scope);
        if (var_type == 0) {
            semantic_analyser_log_error(analyser, "Variable definition failed, variable type is invalid", statement_index);
            break;
        }
        Variable_Type::ENUM assignment_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]);
        if (assignment_type != var_type->variable_type && assignment_type != Variable_Type::ERROR_TYPE) {
            semantic_analyser_log_error(analyser, "Variable type does not match expression type", statement_index);
        }
        semantic_analyser_define_variable(analyser, parent, statement_index, var_type->variable_type);
        break;
    }
    case AST_Node_Type::STATEMENT_VARIABLE_DEFINE_INFER:
    {
        {
            bool in_current_scope;
            Symbol* s = symbol_table_find_symbol_of_type(parent, statement->name_id, Symbol_Type::VARIABLE, &in_current_scope);
            if (s != 0 && in_current_scope) {
                semantic_analyser_log_error(analyser, "Variable already defined", statement_index);
                break;
            }
        }
        Variable_Type::ENUM assignment_type = semantic_analyser_analyse_expression(analyser, parent, statement->children[0]);
        semantic_analyser_define_variable(analyser, parent, statement_index, assignment_type);
        break;
    }
    default: {
        panic("Should be covered!\n");
        break;
    }
    }

    return Analyser_Result::NO_RETURN;
}

Analyser_Result::ENUM semantic_analyser_analyse_statement_block(Semantic_Analyser* analyser, Symbol_Table* parent, int block_index, bool create_symbol_table)
{
    Symbol_Table* table;
    if (create_symbol_table)
        table = semantic_analyser_install_symbol_table(analyser, parent, block_index);
    else
        table = parent;

    int result_type_found = false; // Continue or break make 'dead code' returns or other things invalid
    Analyser_Result::ENUM result = Analyser_Result::NO_RETURN;
    AST_Node* block = &analyser->parser->nodes[block_index];
    for (int i = 0; i < block->children.size; i++) 
    {
        Analyser_Result::ENUM statement_result = semantic_analyser_analyse_statement(analyser, table, block->children[i]);
        switch (statement_result)
        {
        case Analyser_Result::BREAK: 
        case Analyser_Result::CONTINUE: {
            if (!result_type_found)
            {
                result = Analyser_Result::NO_RETURN;
                if (i != block->children.size - 1) {
                    // This should probably be a warning
                    semantic_analyser_log_error(analyser, "Code will never be reached, break or continue before prevents that!",
                        block->children[i+1], block->children[block->children.size - 1]);
                }
                result_type_found = true;
            }
            break;
        }
        case Analyser_Result::RETURN:
            if (!result_type_found)
            {
                result = Analyser_Result::RETURN;
                if (i != block->children.size - 1) {
                    // This should probably be a warning
                    semantic_analyser_log_error(analyser, "Code will never be reached, return before prevents that!",
                        block->children[i+1], block->children[block->children.size - 1]);
                }
                result_type_found = true;
            }
            break;
        case Analyser_Result::NO_RETURN:
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
    for (int i = 0; i < parameter_block->children.size; i++)
    {
        AST_Node* parameter = &analyser->parser->nodes[parameter_block->children[i]];
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_of_type(table, parameter->type_id, Symbol_Type::TYPE, &in_current_scope);
        if (s == 0) {
            semantic_analyser_log_error(analyser, "Parameter type not defined!", parameter_block->children[i]);
            semantic_analyser_define_variable(analyser, table, parameter_block->children[i], Variable_Type::ERROR_TYPE);
            continue;
        }
        semantic_analyser_define_variable(analyser, table, parameter_block->children[i], s->variable_type);
    }

    // Set return type
    {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_of_type(table, function->type_id, Symbol_Type::TYPE, &in_current_scope);
        if (s == 0) {
            semantic_analyser_log_error(analyser, "Funciton return type not valid type!", function_index);
            analyser->function_return_type = Variable_Type::ERROR_TYPE;
        }
        else {
            analyser->function_return_type = s->variable_type;
        }
    }
    analyser->loop_depth = 0;
    Analyser_Result::ENUM result = semantic_analyser_analyse_statement_block(analyser, table, function->children[1], false);
    if (result != Analyser_Result::RETURN) {
        semantic_analyser_log_error(analyser, "Not all code paths return a value!", function_index);
    }
}

// Semantic Analyser
Semantic_Analyser semantic_analyser_create()
{
    Semantic_Analyser result;
    result.symbol_tables = dynamic_array_create_empty<Symbol_Table*>(64);
    result.node_to_table_mappings = dynamic_array_create_empty<int>(64);
    result.errors = dynamic_array_create_empty<Compiler_Error>(64);
    return result;
}

void semantic_analyser_destroy(Semantic_Analyser* analyser)
{
    for (int i = 0; i < analyser->symbol_tables.size; i++) {
        symbol_table_destroy(analyser->symbol_tables[i]);
        delete analyser->symbol_tables[i];
    }
    dynamic_array_destroy(&analyser->symbol_tables);
    dynamic_array_destroy(&analyser->node_to_table_mappings);
    dynamic_array_destroy(&analyser->errors);
}

void semantic_analyser_analyse(Semantic_Analyser* analyser, AST_Parser* parser)
{
    // TODO: We could also reuse the previous memory in the symbol tables, like in the parser
    for (int i = 0; i < analyser->symbol_tables.size; i++) {
        symbol_table_destroy(analyser->symbol_tables[i]);
        delete analyser->symbol_tables[i];
    }
    dynamic_array_reset(&analyser->symbol_tables);
    dynamic_array_reset(&analyser->node_to_table_mappings);
    dynamic_array_reset(&analyser->errors);
    analyser->parser = parser;

    dynamic_array_reserve(&analyser->node_to_table_mappings, parser->nodes.size);
    for (int i = 0; i < parser->nodes.size; i++) {
        dynamic_array_push_back(&analyser->node_to_table_mappings, 0);
    }

    Symbol_Table* root_table = semantic_analyser_install_symbol_table(analyser, 0, 0);
    // Add tokens for basic datatypes
    {
        int int_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("int"));
        int bool_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("bool"));
        int float_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("float"));
        int void_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("void"));
        symbol_table_define_type(root_table, int_token_index, Variable_Type::INTEGER);
        symbol_table_define_type(root_table, bool_token_index, Variable_Type::BOOLEAN);
        symbol_table_define_type(root_table, float_token_index, Variable_Type::FLOAT);
        symbol_table_define_type(root_table, void_token_index, Variable_Type::VOID_TYPE);
    }

    // Add all functions to root_table
    AST_Node* root = &analyser->parser->nodes[0];
    for (int i = 0; i < root->children.size; i++) {
        semantic_analyser_define_function(analyser, root_table, root->children[i]);
    }
    analyser->node_to_table_mappings[0] = 0;

    // Analyse all functions
    for (int i = 0; i < root->children.size; i++) {
        semantic_analyser_analyse_function(analyser, root_table, root->children[i]);
    }
}
