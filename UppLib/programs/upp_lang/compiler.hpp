#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"

#include "lexer.hpp"

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

namespace SymbolType
{
    enum ENUM
    {
        VARIABLE,
        FUNCTION,
        TYPE,
    };
};

struct Ast_Node_Function;
struct Symbol {
    int name;
    SymbolType::ENUM symbol_type;
    Variable_Type::ENUM variable_type;
    Ast_Node_Function* function;
};

struct SymbolTable
{
    SymbolTable* parent;
    DynamicArray<Symbol> symbols;
};

SymbolTable symbol_table_create(SymbolTable* parent);
void symbol_table_destroy(SymbolTable* table);
SymbolTable* symbol_table_create_new(SymbolTable* parent);
Symbol* symbol_table_find_symbol(SymbolTable* table, int name, bool* in_current_scope);
Symbol* symbol_table_find_symbol_type(SymbolTable* table, int name, SymbolType::ENUM symbol_type, bool* in_current_scope);

/*
    AST
*/
namespace ExpressionType
{
    enum ENUM
    {
        OP_ADD,
        OP_SUBTRACT,
        OP_DIVIDE,
        OP_MULTIPLY,
        OP_MODULO,
        OP_BOOLEAN_AND,
        OP_BOOLEAN_OR,
        OP_GREATER_THAN,
        OP_GREATER_EQUAL,
        OP_LESS_THAN,
        OP_LESS_EQUAL,
        OP_EQUAL,
        OP_NOT_EQUAL,
        OP_NEGATE,
        OP_LOGICAL_NOT,
        LITERAL,
        FUNCTION_CALL,
        VARIABLE_READ,
    };
}

struct Ast_Node_Expression
{
    ExpressionType::ENUM type;
    SymbolTable* symbol_table;
    bool free_symbol_table_on_destroy;
    int literal_token_index;
    int variable_name_id;
    Ast_Node_Expression* left;
    Ast_Node_Expression* right;
    DynamicArray<Ast_Node_Expression> arguments;
};

namespace StatementType
{
    enum ENUM
    {
        VARIABLE_ASSIGNMENT, // x = 5;
        VARIABLE_DEFINITION, // x : int;
        VARIABLE_DEFINE_ASSIGN, // x : int = 5;
        VARIABLE_DEFINE_INFER, // x := 5;
        STATEMENT_BLOCK, // { x := 5; y++; ...}
        IF_BLOCK,
        IF_ELSE_BLOCK,
        WHILE,
        RETURN_STATEMENT,
        BREAK,
        EXPRESSION, // for function calls x();
        CONTINUE,
    };
};

struct Ast_Node_Statement;
struct Ast_Node_Statement_Block
{
    SymbolTable* symbol_table;
    bool free_symbol_table_on_destroy;
    DynamicArray<Ast_Node_Statement> statements;
};

struct Ast_Node_Statement
{
    StatementType::ENUM type;
    SymbolTable* symbol_table;
    bool free_symbol_table_on_destroy;
    int variable_name_id;
    int variable_type_id;
    Ast_Node_Expression expression;
    Ast_Node_Statement_Block statements;
    Ast_Node_Statement_Block else_statements;
};

struct Ast_Node_Function_Parameter
{
    int name_id;
    int type_id;
};

struct Ast_Node_Function
{
    SymbolTable* symbol_table;
    bool free_symbol_table_on_destroy;
    int function_name_id;
    int return_type_id;
    DynamicArray<Ast_Node_Function_Parameter> parameters;
    Ast_Node_Statement_Block body;
};

struct Ast_Node_Root
{
    SymbolTable* symbol_table;
    bool free_symbol_table_on_destroy;
    DynamicArray<Ast_Node_Function> functions;
};
void ast_node_root_append_to_string(String* string, Ast_Node_Root* root, Lexer* lexer_result);

/*
    Parser
*/
struct ParserError
{
    const char* error_message;
    int token_start_index;
    int token_end_index;
};

struct Parser
{
    Ast_Node_Root root;
    // TODO: Rethink error handling in the parser, intermediate/unresolved seem a bit wonkey
    DynamicArray<ParserError> intermediate_errors;
    DynamicArray<ParserError> unresolved_errors;
    DynamicArray<const char*> semantic_analysis_errors;
    // Stuff for parsing
    int index;
    Lexer* lexer;
    // Stuff for semantic analysis
    Variable_Type::ENUM current_function_return_type;
    int loop_depth;
};

// TODO: This stuff is weird, who handles what memory?
Parser parser_parse(Lexer* result);
void parser_destroy(Parser* parser);

/*
    Semantic Analysis
*/
void parser_semantic_analysis(Parser* parser);

