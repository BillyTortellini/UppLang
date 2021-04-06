#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "lexer.hpp"

namespace AST_Node_Type
{
    enum ENUM
    {
        ROOT,
        FUNCTION,
        PARAMETER_BLOCK,
        PARAMETER,
        STATEMENT_BLOCK,
        STATEMENT_IF,
        STATEMENT_IF_ELSE,
        STATEMENT_WHILE,
        STATEMENT_BREAK,
        STATEMENT_CONTINUE,
        STATEMENT_RETURN,
        STATEMENT_EXPRESSION, // for function calls x();
        STATEMENT_VARIABLE_ASSIGNMENT, // x = 5;
        STATEMENT_VARIABLE_DEFINITION, // x : int;
        STATEMENT_VARIABLE_DEFINE_ASSIGN, // x : int = 5;
        STATEMENT_VARIABLE_DEFINE_INFER, // x := 5;
        EXPRESSION_LITERAL,
        EXPRESSION_FUNCTION_CALL,
        EXPRESSION_VARIABLE_READ,
        EXPRESSION_BINARY_OPERATION_ADDITION,
        EXPRESSION_BINARY_OPERATION_SUBTRACTION,
        EXPRESSION_BINARY_OPERATION_DIVISION,
        EXPRESSION_BINARY_OPERATION_MULTIPLICATION,
        EXPRESSION_BINARY_OPERATION_MODULO,
        EXPRESSION_BINARY_OPERATION_AND,
        EXPRESSION_BINARY_OPERATION_OR,
        EXPRESSION_BINARY_OPERATION_EQUAL,
        EXPRESSION_BINARY_OPERATION_NOT_EQUAL,
        EXPRESSION_BINARY_OPERATION_LESS,
        EXPRESSION_BINARY_OPERATION_LESS_OR_EQUAL,
        EXPRESSION_BINARY_OPERATION_GREATER,
        EXPRESSION_BINARY_OPERATION_GREATER_OR_EQUAL,
        EXPRESSION_UNARY_OPERATION_NEGATE,
        EXPRESSION_UNARY_OPERATION_NOT,
        UNDEFINED, // Just for debugging
    };
}

typedef int AST_Node_Index;
struct AST_Node
{
    AST_Node_Type::ENUM type; 
    AST_Node_Index parent;
    DynamicArray<AST_Node_Index> children;
    // Node information from parsing
    int name_id; // Multipurpose: variable read, write, function name, function call
    int type_id; // Multipurpose: variable type, return type
};

struct Token_Range
{
    int start_index;
    int end_index;
};

struct Parser_Error
{
    const char* error_message;
    int token_start_index;
    int token_end_index;
};

struct AST_Parser
{
    DynamicArray<AST_Node> nodes;
    DynamicArray<Token_Range> token_mapping;

    // TODO: Rethink error handling in the parser, intermediate/unresolved seem a bit wonkey
    DynamicArray<Parser_Error> intermediate_errors;
    DynamicArray<Parser_Error> unresolved_errors;

    // Stuff for parsing
    Lexer* lexer;
    int index;
    AST_Node_Index next_free_node; // What is the next free node
};

struct AST_Parser_Checkpoint
{
    AST_Parser* parser;
    AST_Node_Index parent_index;
    int parent_child_count;
    int rewind_token_index;
    int next_free_node_index;
};

AST_Parser ast_parser_parse(Lexer* lexer);
void ast_parser_destroy(AST_Parser* parser);
void ast_parser_append_to_string(AST_Parser* parser, String* string);
