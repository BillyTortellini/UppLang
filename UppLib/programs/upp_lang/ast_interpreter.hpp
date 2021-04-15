#pragma once

#include "semantic_analyser.hpp"

struct AST_Interpreter_Value
{
    Primitive_Type type;
    union
    {
        int int_value;
        float float_value;
        bool bool_value;
    };
};

struct AST_Interpreter_Variable
{
    int variable_name;
    AST_Interpreter_Value value;
};

struct AST_Interpreter
{
    Semantic_Analyser* analyser;
    DynamicArray<AST_Interpreter_Variable> symbol_table;
    DynamicArray<int> scope_beginnings;
    DynamicArray<int> function_scope_beginnings;
    DynamicArray<AST_Interpreter_Value> argument_evaluation_buffer;
};

struct AST_Interpreter_Statement_Result
{
    bool is_break;
    bool is_continue;
    bool is_return;
    AST_Interpreter_Value return_value;
};

AST_Interpreter ast_interpreter_create();
void ast_interpreter_destroy(AST_Interpreter* interpreter);
AST_Interpreter_Value ast_interpreter_execute_main(AST_Interpreter* interpreter, Semantic_Analyser* analyser);
void ast_interpreter_value_append_to_string(AST_Interpreter_Value value, String* string);
