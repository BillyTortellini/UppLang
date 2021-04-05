#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "lexer.hpp"

namespace Unary_Operation_Type
{
    enum ENUM
    {
        NEGATE,
        NOT,
    };
}

namespace Binary_Operation_Type
{
    enum ENUM
    {
        // Arithmetic stuff
        ADDITION,
        SUBTRACTION,
        DIVISION,
        MULTIPLICATION,
        MODULO,
        // Boolean stuff
        AND,
        OR,
        // Comparisons
        EQUAL,
        NOT_EQUAL,
        LESS,
        LESS_OR_EQUAL,
        GREATER,
        GREATER_OR_EQUAL,
    };
}

namespace Expression_Type
{
    enum ENUM
    {
        BINARY_OPERATION,
        UNARY_OPERATION,
        LITERAL,
        FUNCTION_CALL,
        VARIABLE_READ,
    };
}

namespace Statement_Type
{
    enum ENUM
    {
        STATEMENT_BLOCK, // { x := 5; y++; ...}
        IF_BLOCK,
        IF_ELSE_BLOCK,
        WHILE,
        BREAK,
        CONTINUE,
        RETURN_STATEMENT,
        EXPRESSION, // for function calls x();
        VARIABLE_ASSIGNMENT, // x = 5;
        VARIABLE_DEFINITION, // x : int;
        VARIABLE_DEFINE_ASSIGN, // x : int = 5;
        VARIABLE_DEFINE_INFER, // x := 5;
    };
};

namespace AST_Node_Type
{
    enum ENUM
    {
        ROOT,
        FUNCTION,
        PARAMETER_BLOCK,
        PARAMETER,
        STATEMENT,
        STATEMENT_BLOCK,
        EXPRESSION,
        UNDEFINED, // Just for debugging
    };
}

typedef int AST_Node_Index;
struct AST_Node
{
    AST_Node_Type::ENUM type; 
    AST_Node_Index parent;
    DynamicArray<AST_Node_Index> children;
    union
    {
        Statement_Type::ENUM statement_type;
        Expression_Type::ENUM expression_type;
    };
    union {
        struct {
            int name_id; // Multipurpose: variable read, write, function name, function call
            int type_id; // Multipurpose: variable type, return type
        };
        Binary_Operation_Type::ENUM binary_op_type;
        Unary_Operation_Type::ENUM unary_op_type;
    };
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
