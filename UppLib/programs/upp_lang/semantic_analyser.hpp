#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"

#include "lexer.hpp"
#include "ast_parser.hpp"

namespace Variable_Type
{
    enum ENUM
    {
        INTEGER,
        FLOAT,
        BOOLEAN,
        ERROR_TYPE,
        VOID_TYPE,
    };
};
String variable_type_to_string(Variable_Type::ENUM type);

namespace Symbol_Type
{
    enum ENUM
    {
        VARIABLE,
        FUNCTION,
        TYPE,
    };
};

struct Symbol 
{
    int name;
    Symbol_Type::ENUM symbol_type;
    union
    {
        Variable_Type::ENUM variable_type;
        int function_index;
    };
};

struct Symbol_Table
{
    Symbol_Table* parent;
    DynamicArray<Symbol> symbols;
};

struct Semantic_Analyser
{
    DynamicArray<Symbol_Table*> symbol_tables;
    DynamicArray<int> node_to_table_mappings;
    DynamicArray<Compiler_Error> errors;
    AST_Parser* parser;

    Variable_Type::ENUM function_return_type;
    int loop_depth;
};

Symbol_Table symbol_table_create(Symbol_Table* parent);
void symbol_table_destroy(Symbol_Table* table);
Symbol* symbol_table_find_symbol(Symbol_Table* table, int name, bool* in_current_scope);
Symbol* symbol_table_find_symbol_of_type(Symbol_Table* table, int name, Symbol_Type::ENUM symbol_type, bool* in_current_scope);

Semantic_Analyser semantic_analyser_create();
void semantic_analyser_destroy(Semantic_Analyser* analyser);
void semantic_analyser_analyse(Semantic_Analyser* analyser, AST_Parser* parser);
