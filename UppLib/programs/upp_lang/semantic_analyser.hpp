#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"

#include "lexer.hpp"
#include "ast_parser.hpp"

/*
    Changes:
        Function Parameters
        Function Return type
        Function children count
        Variable definition
        Variable define-assign
        Assignment Statement
        Expression AddressOf
        Expression Dereference
*/

enum class Primitive_Type
{
    INTEGER,
    FLOAT,
    BOOLEAN,
};

enum class Signature_Type
{
    PRIMITIVE,
    POINTER,
    FUNCTION,
    ERROR_TYPE,
    // Future: Array, function, union, ...
};

struct Type_Signature
{
    Signature_Type type;
    Primitive_Type primtive_type;
    int pointed_to_type_index;
    DynamicArray<int> parameter_type_indices;
    int return_type_index;
};

String variable_type_to_string(Primitive_Type type);

namespace Symbol_Type
{
    enum ENUM
    {
        VARIABLE,
        FUNCTION,
        TYPE, // This will be required when we have Structs
    };
};

struct Symbol
{
    int name;
    Symbol_Type::ENUM symbol_type;
    int type_index;
    int function_index;
};

struct Symbol_Table
{
    Symbol_Table* parent;
    DynamicArray<Symbol> symbols;
};

struct Semantic_Node_Information
{
    int symbol_table_index;
    int expression_result_type_index;
};

struct Semantic_Analyser
{
    DynamicArray<Type_Signature> types;
    DynamicArray<Symbol_Table*> symbol_tables;
    DynamicArray<Semantic_Node_Information> semantic_information;
    DynamicArray<Compiler_Error> errors;

    // Temporary stuff needed for analysis
    AST_Parser* parser;
    int function_return_type_index;
    int loop_depth;

    // Type indices
    int error_type_index;
    int int_type_index;
    int float_type_index;
    int bool_type_index;
};

enum class Statement_Analysis_Result
{
    NO_RETURN,
    RETURN,
    CONTINUE,
    BREAK
};

struct Expression_Analysis_Result
{
    int type_index;
    bool has_memory_address;
};

Symbol_Table symbol_table_create(Symbol_Table* parent);
void symbol_table_destroy(Symbol_Table* table);
Symbol* symbol_table_find_symbol(Symbol_Table* table, int name, bool* in_current_scope);
Symbol* symbol_table_find_symbol_of_type(Symbol_Table* table, int name, Symbol_Type::ENUM symbol_type);

Semantic_Analyser semantic_analyser_create();
void semantic_analyser_destroy(Semantic_Analyser* analyser);
void semantic_analyser_analyse(Semantic_Analyser* analyser, AST_Parser* parser);
