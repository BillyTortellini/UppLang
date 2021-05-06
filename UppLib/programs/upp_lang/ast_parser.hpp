#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "lexer.hpp"

namespace AST_Node_Type
{
    enum ENUM
    {
        ROOT, // Children: functions
        FUNCTION, // Child 0: parameter_block, Child 1: Return Type, Child 2: Statement_Block
        PARAMETER_BLOCK, // Children: Parameters
        PARAMETER, // Child 0: Type
        TYPE_IDENTIFIER, // No Children
        TYPE_POINTER_TO, // Child 0: Type
        TYPE_ARRAY_SIZED, // Child 0: Expression (Compile time available), Child 1: Type
        TYPE_ARRAY_UNSIZED, // Child 0: Type
        STATEMENT_BLOCK, // Children: Statements
        STATEMENT_IF, // Child 0: Condition, Child 1: Statements
        STATEMENT_IF_ELSE, // Child 0: Condition, Child 1: if-Statement_Block, Child 2: Else-Statement-Block
        STATEMENT_WHILE, // Child 0: Condition, Child 1: Statements
        STATEMENT_BREAK, // No Children
        STATEMENT_CONTINUE, // No Children
        STATEMENT_RETURN, // Child 0: Return-Expression
        STATEMENT_EXPRESSION, // Child 0: Expression
        STATEMENT_ASSIGNMENT, // Child 0: Destination-Expression, Child 1: Value-Expression
        STATEMENT_VARIABLE_DEFINITION, //
        STATEMENT_VARIABLE_DEFINE_ASSIGN, // Child 0: Type index, Child 1: Value-Expression
        STATEMENT_VARIABLE_DEFINE_INFER, // Child 0: Expression
        EXPRESSION_LITERAL,
        EXPRESSION_FUNCTION_CALL,
        EXPRESSION_VARIABLE_READ,
        EXPRESSION_ARRAY_ACCESS, // Child 0: Access-to-Expression, Child 1: Index-Expression
        EXPRESSION_MEMBER_ACCESS, // Child 0: left side, name_id is the .what operator a.y.y[5].z
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
        EXPRESSION_UNARY_OPERATION_ADDRESS_OF,
        EXPRESSION_UNARY_OPERATION_DEREFERENCE,
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
};

struct Token_Range
{
    int start_index;
    int end_index;
};
Token_Range token_range_make(int start_index, int end_index);

struct Compiler_Error
{
    const char* message;
    Token_Range range;
};

struct AST_Parser
{
    DynamicArray<AST_Node> nodes;
    DynamicArray<Token_Range> token_mapping;
    DynamicArray<Compiler_Error> errors;
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

AST_Parser ast_parser_create();
void ast_parser_parse(AST_Parser* parser, Lexer* lexer);
void ast_parser_destroy(AST_Parser* parser);
void ast_parser_append_to_string(AST_Parser* parser, String* string);
String ast_node_type_to_string(AST_Node_Type::ENUM type);