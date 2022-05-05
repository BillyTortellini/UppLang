#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "text.hpp"
#include "compiler_misc.hpp"

struct Compiler;
struct Token_Range;
struct Compiler_Error;
struct Identifier_Pool;
struct Token;


enum class AST_Node_Type
{
    // TOP-LEVEL DECLARATIONS
    ROOT, // Child 0: Definitions
    DEFINITIONS, // Children: Extern/load declarations, constant_definition, variable_definition
    EXTERN_FUNCTION_DECLARATION, // id: function name Child 0: Type
    EXTERN_LIB_IMPORT, // id: lib filename
    EXTERN_HEADER_IMPORT, // id: header name, Children: IDENTIFIER_NAME
    LOAD_FILE, // id: filename

    // Building Blocks
    IDENTIFIER_NAME, // id defined
    IDENTIFIER_PATH, // Child 0: Identifier-Type, id defined

    PARAMETER_BLOCK, // Children: Parameter or PARAMETER_COMPTIME
    PARAMETER, // Child 0: Type-Expression
    PARAMETER_COMPTIME, // Child 0: Type-Expression

    ARGUMENTS, // Children: Named or Unnamed ARGUMENT
    ARGUMENT_UNNAMED, // Child 0: Expressionl:
    ARGUMENT_NAMED, // id: name, Child 0: Expression

    ENUM_MEMBER, // Optional<Child 0>: Expression
    SWITCH_CASE, // Child 0: Expression, Child 1: Statement_Block
    SWITCH_DEFAULT_CASE, // Child 0: Statement_Block

    // Statements
    STATEMENT_BLOCK, // Children: Statements, Optional: id = block_name
    STATEMENT_IF, // Child 0: Condition, Child 1: Statements, Optional: id = block_name
    STATEMENT_DEFER, // Child 0: Statement_Block
    STATEMENT_IF_ELSE, // Child 0: Condition, Child 1: if-Statement_Block, Child 2: Else-Statement-Block
    STATEMENT_WHILE, // Child 0: Condition, Child 1: Statements, Optional: id = block_name
    STATEMENT_SWITCH, // Child 0: Expression, Children: Switch_Cases, Optional: id = block_name
    STATEMENT_BREAK, // No Children, Optional: id = block_name
    STATEMENT_CONTINUE, // No Children, Optional: id = block_name
    STATEMENT_RETURN, // Optional: Child 0: Return-Expression
    STATEMENT_EXPRESSION, // Child 0: Expression
    STATEMENT_ASSIGNMENT, // Child 0: Destination-Expression, Child 1: Value-Expression
    STATEMENT_DELETE, // Child = expression

    // DEFINITIONS
    COMPTIME_DEFINE_ASSIGN, // Child 0: Expression
    COMPTIME_DEFINE_INFER,  // Child 0: Type-Expression, Child 1: Value-Expression
    VARIABLE_DEFINITION,    // Child 0: Type-Expression
    VARIABLE_DEFINE_ASSIGN, // Child 0: Type-Expression, Child 1: Value-Expression
    VARIABLE_DEFINE_INFER,  // Child 0: Expression

    // Expressions
    EXPRESSION_POINTER,    // Child 0: Expr
    EXPRESSION_IDENTIFIER, // Child 0: Identifier
    EXPRESSION_SLICE_TYPE, // []expr_0
    EXPRESSION_ARRAY_TYPE, // [expr_0]expr_1      

    // Type_End
    EXPRESSION_LITERAL,   
    EXPRESSION_NEW, // Child 0: Type
    EXPRESSION_NEW_ARRAY, // Child 0: Array size expression, Child 1: Type
    EXPRESSION_FUNCTION_CALL, // Child 0: Expression, Child 1: ARGUMENTS
    EXPRESSION_ARRAY_ACCESS, // Child 0: Access-to-Expression, Child 1: Index Expression
    EXPRESSION_ARRAY_INITIALIZER, // Child 0: Type, Child 1-n: Expressions
    EXPRESSION_STRUCT_INITIALIZER, // Child 0: Type, Child 1: ARGUMENTS
    EXPRESSION_AUTO_ARRAY_INITIALIZER, // Child 0-n: Expressions
    EXPRESSION_AUTO_STRUCT_INITIALIZER, // Child 0: Arguments
    EXPRESSION_AUTO_ENUM, // ID: name of access member
    EXPRESSION_MEMBER_ACCESS, // Child 0: left side, id is the .what operator a.y.y[5].z
    EXPRESSION_CAST, // Either: Child 0: type, Child 1: Expression    OR    Child 0: Expression
    EXPRESSION_CAST_PTR, // Either: Child 0: type, Child 1: Expression    OR    Child 0: Expression
    EXPRESSION_CAST_RAW, // Child 0: Expression
    EXPRESSION_BAKE, // Child 0: type, Child 1: Statement_Block
    EXPRESSION_TYPE_INFO, // Child 0: Expression;
    EXPRESSION_TYPE_OF,   // Child 0: Expression

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
    EXPRESSION_BINARY_OPERATION_POINTER_EQUAL,
    EXPRESSION_BINARY_OPERATION_POINTER_NOT_EQUAL,

    EXPRESSION_UNARY_OPERATION_NEGATE,
    EXPRESSION_UNARY_OPERATION_NOT,
    EXPRESSION_UNARY_OPERATION_DEREFERENCE,

    // Previous top level nodes:
    MODULE, // Child 0: Definitions
    FUNCTION, // Child 0: Statement_Block, Child 1: Function_Signature
    FUNCTION_SIGNATURE, // Child 0: Parameter_Block_Named, Child 1 (optional): Return-Type
    STRUCT, // Children: Variable definitions
    UNION, // Children: Variable definitions
    C_UNION, // Children: Variable definitions
    ENUM, // Children: Enum_Member

    UNDEFINED, // Just for debugging
};

bool ast_node_type_is_identifier_node(AST_Node_Type type);
bool ast_node_type_is_expression(AST_Node_Type type);
bool ast_node_type_is_binary_operation(AST_Node_Type type);
bool ast_node_type_is_statement(AST_Node_Type type);

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

struct Lexer_Source
{
    Dynamic_Array<Token> tokens;
    Dynamic_Array<Token> tokens_with_decoration; // Includes comments and whitespaces
    AST_Node* root_node;
};

struct AST_Parser
{
    Stack_Allocator allocator;
    Dynamic_Array<Compiler_Error> errors;

    Lexer_Source* code_source;
    int index;

    String* id_lib;
    String* id_load;
    String* id_bake;
    String* id_type_of;
    String* id_type_info;
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
void ast_parser_parse(AST_Parser* parser, Lexer_Source* source);

void ast_node_append_to_string(Lexer_Source* code_source, AST_Node* node, String* string, int indentation_lvl);
String ast_node_type_to_string(AST_Node_Type type);
