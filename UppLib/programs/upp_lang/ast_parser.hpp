#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "lexer.hpp"
#include "text.hpp"

struct Compiler;
struct Lexer;
struct Token_Range;
struct Compiler_Error;

enum class AST_Node_Type
{
    ROOT, // Children: functions
    STRUCT, // Children: Variable definitions
    FUNCTION, // Child 0: Function_Signature, Child 2: Statement_Block
    FUNCTION_SIGNATURE, // Child 0: Parameter_Block_Named, Child 1 (optional): Return Type
    PARAMETER_BLOCK_UNNAMED, // Children: Types
    PARAMETER_BLOCK_NAMED, // Children: Named_Parameter
    NAMED_PARAMETER, // Child 0: Type
    TYPE_FUNCTION_POINTER, // Child 0: Parameter_Block_Unnamed, Child 1 (optional): Return Type
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
    STATEMENT_VARIABLE_DEFINITION, // Child 0: Type index, name_id
    STATEMENT_VARIABLE_DEFINE_ASSIGN, // Child 0: Type index, Child 1: Value-Expression
    STATEMENT_VARIABLE_DEFINE_INFER, // Child 0: Expression
    STATEMENT_DELETE, // Child =: expression
    EXPRESSION_NEW, // Child 0: Type
    EXPRESSION_NEW_ARRAY, // Child 0: Array size expression, Child 1: Type
    EXPRESSION_LITERAL,
    EXPRESSION_FUNCTION_CALL, // Children: Argument expressions, name_id is name of function to call
    EXPRESSION_VARIABLE_READ,
    EXPRESSION_ARRAY_ACCESS, // Child 0: Access-to-Expression, Child 1: Index-Expression
    EXPRESSION_MEMBER_ACCESS, // Child 0: left side, name_id is the .what operator a.y.y[5].z
    EXPRESSION_CAST, // Child 0: type, Child 1: Expression
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

bool ast_node_type_is_binary_expression(AST_Node_Type type);
bool ast_node_type_is_unary_expression(AST_Node_Type type);
bool ast_node_type_is_expression(AST_Node_Type type);
bool ast_node_type_is_statement(AST_Node_Type type);
bool ast_node_type_is_type(AST_Node_Type type);

typedef int AST_Node_Index;
struct AST_Node
{
    AST_Node_Type type; 
    AST_Node_Index parent;
    Dynamic_Array<AST_Node_Index> children;
    // Node information
    int name_id; // Multipurpose: variable read, write, function name, function call
};

struct AST_Parser
{
    Dynamic_Array<AST_Node> nodes;
    Dynamic_Array<Token_Range> token_mapping;
    Dynamic_Array<Compiler_Error> errors;
    Lexer* lexer;
    int index;
    AST_Node_Index next_free_node;
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
int ast_parser_get_closest_node_to_text_position(AST_Parser* parser, Text_Position pos, Dynamic_Array<String> text);
String ast_node_type_to_string(AST_Node_Type type);
