#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "text.hpp"
#include "compiler_misc.hpp"

struct Compiler;
struct Token_Range;
struct Compiler_Error;
struct Code_Source;
struct Identifier_Pool;
struct Token;

enum class AST_Node_Type
{
    ROOT, // Child 0: Definitions
    DEFINITIONS, // Children: functions, globals, modules, structs or extern function declaration
    EXTERN_FUNCTION_DECLARATION, // id: function name Child 0: Type
    EXTERN_LIB_IMPORT, // id: lib filename
    EXTERN_HEADER_IMPORT, // id: header name, Children: IDENTIFIER_NAME
    LOAD_FILE, // id: filename
    MODULE, // Child 0: Definitions
    MODULE_TEMPLATED, // Child 0: Parameter_Block_Unnamed, Child 1: DEFINITIONS
    TEMPLATE_PARAMETERS, // Children: IDENTIFIER_NAME
    STRUCT, // Children: Variable definitions
    FUNCTION, // Child 0: Function_Signature, Child 1: Statement_Block
    FUNCTION_SIGNATURE, // Child 0: Parameter_Block_Named, Child 1 (optional): Return Type
    IDENTIFIER_NAME, // id defined
    IDENTIFIER_NAME_TEMPLATED, // Child 0: Parameter_Block_Unnamed
    IDENTIFIER_PATH, // Child 0: Identifier-Type, id defined
    IDENTIFIER_PATH_TEMPLATED, // Child 0: Parameter_Block_Unnamed, Child 1: Identifier
    PARAMETER_BLOCK_UNNAMED, // Children: Types
    PARAMETER_BLOCK_NAMED, // Children: Named_Parameter
    NAMED_PARAMETER, // Child 0: Type
    TYPE_FUNCTION_POINTER, // Child 0: Parameter_Block_Unnamed, Child 1 (optional): Return Type
    TYPE_IDENTIFIER, // Child 0: Either Identifer or Identifier Path
    TYPE_POINTER_TO, // Child 0: Type
    TYPE_ARRAY, // Child 0: Expression (Compile time available), Child 1: Type
    TYPE_SLICE, // Child 0: Type
    STATEMENT_BLOCK, // Children: Statements
    STATEMENT_IF, // Child 0: Condition, Child 1: Statements
    STATEMENT_DEFER, // Child 0: Statement_Block
    STATEMENT_IF_ELSE, // Child 0: Condition, Child 1: if-Statement_Block, Child 2: Else-Statement-Block
    STATEMENT_WHILE, // Child 0: Condition, Child 1: Statements
    STATEMENT_BREAK, // No Children
    STATEMENT_CONTINUE, // No Children
    STATEMENT_RETURN, // Child 0: Return-Expression
    STATEMENT_EXPRESSION, // Child 0: Expression
    STATEMENT_ASSIGNMENT, // Child 0: Destination-Expression, Child 1: Value-Expression
    STATEMENT_VARIABLE_DEFINITION, // Child 0: Type index, id
    STATEMENT_VARIABLE_DEFINE_ASSIGN, // Child 0: Type index, Child 1: Value-Expression
    STATEMENT_VARIABLE_DEFINE_INFER, // Child 0: Expression
    STATEMENT_DELETE, // Child =: expression
    ARGUMENTS, // Children: Expressions
    EXPRESSION_NEW, // Child 0: Type
    EXPRESSION_NEW_ARRAY, // Child 0: Array size expression, Child 1: Type
    EXPRESSION_LITERAL,
    EXPRESSION_FUNCTION_CALL, // Child 0: Expression, Child 1: ARGUMENTS
    EXPRESSION_VARIABLE_READ, // Child 0: Identifier
    EXPRESSION_ARRAY_ACCESS, // Child 0: Access-to-Expression, Child 1: Index-Expression
    EXPRESSION_MEMBER_ACCESS, // Child 0: left side, id is the .what operator a.y.y[5].z
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

bool ast_node_type_is_identifier_node(AST_Node_Type type);
bool ast_node_type_is_binary_expression(AST_Node_Type type);
bool ast_node_type_is_unary_expression(AST_Node_Type type);
bool ast_node_type_is_expression(AST_Node_Type type);
bool ast_node_type_is_statement(AST_Node_Type type);
bool ast_node_type_is_type(AST_Node_Type type);

struct AST_Node
{
    AST_Node_Type type; 
    AST_Node* parent;
    AST_Node* neighbor;
    AST_Node* child_start;
    AST_Node* child_end;
    int child_count;
    // Node information
    String* id; // Multipurpose: variable read, write, function name, function call, module name
    Token* literal_token;
    Token_Range token_range;

    // DEBUGGING
    int alloc_index;
};

struct AST_Parser
{
    Stack_Allocator allocator;
    Dynamic_Array<Compiler_Error> errors;

    Code_Source* code_source;
    int index;

    String* id_lib;
    String* id_load;
};

struct AST_Parser_Checkpoint
{
    AST_Parser* parser;
    int rewind_token_index;

    AST_Node* node;
    AST_Node* last_child;
    int node_child_count;

    Stack_Checkpoint stack_checkpoint;
};

AST_Parser ast_parser_create();
void ast_parser_destroy(AST_Parser* parser);

void ast_parser_reset(AST_Parser* parser, Identifier_Pool* id_pool);
void ast_parser_parse(AST_Parser* parser, Code_Source* source);

void ast_node_append_to_string(Code_Source* code_source, AST_Node* node, String* string, int indentation_lvl);
String ast_node_type_to_string(AST_Node_Type type);
