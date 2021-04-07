#pragma once

#include "ast_interpreter.hpp"

struct Ast_Interpreter_Value
{
    Variable_Type::ENUM type;
    union
    {
        int int_value;
        float float_value;
        bool bool_value;
    };
};

struct Ast_Interpreter_Variable
{
    int variable_name;
    Ast_Interpreter_Value value;
};

struct Ast_Interpreter
{
    Ast_Node_Root* root;
    DynamicArray<Ast_Interpreter_Variable> symbol_table;
    DynamicArray<int> scope_beginnings;
    DynamicArray<int> function_scope_beginnings;
    DynamicArray<Ast_Interpreter_Value> argument_evaluation_buffer;
    Lexer* lexer;

    // This is stuff from before semantic analysis, so this should be changed i think
    int int_token_index;
    int float_token_index;
    int bool_token_index;
    int print_token_index;
};

struct Ast_Interpreter_Statement_Result
{
    bool is_break;
    bool is_continue;
    bool is_return;
    Ast_Interpreter_Value return_value;
};

Ast_Interpreter_Value ast_interpreter_execute_main(Ast_Node_Root* root, Lexer* Lexer);
void ast_interpreter_value_append_to_string(Ast_Interpreter_Value value, String* string);
