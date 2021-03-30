#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"

/*
    TODO:
     - How to represent primitive datatypes
     - Semantic Analysis (Symbol table handling)
*/

/* Tokens */
namespace Token_Type
{
    enum ENUM
    {
        // Keywords
        IF,
        ELSE,
        FOR,
        WHILE,
        CONTINUE,
        BREAK,
        RETURN,
        // Delimiters
        DOT,        // .
        COLON,      // :
        COMMA,      // ,
        SEMICOLON,  // ;
        OPEN_PARENTHESIS, // (
        CLOSED_PARENTHESIS, // )
        OPEN_BRACES, // {
        CLOSED_BRACES, // }
        OPEN_BRACKETS, // [
        CLOSED_BRACKETS, // ]
        DOUBLE_COLON,   // ::
        INFER_ASSIGN, // :=
        ARROW,        // ->
        // Operations
        OP_ASSIGNMENT, // =
        OP_PLUS, // +
        OP_MINUS, // -
        OP_SLASH, // /
        OP_STAR, // *
        OP_PERCENT, // %
        // Assignments
        COMPARISON_LESS, // <
        COMPARISON_LESS_EQUAL, // <=
        COMPARISON_GREATER, // >
        COMPARISON_GREATER_EQUAL, // >=
        COMPARISON_EQUAL, // ==
        COMPARISON_NOT_EQUAL, // !=
        // Boolean Logic operators
        LOGICAL_AND, // &&
        LOGICAL_OR, // || 
        LOGICAL_BITWISE_AND, // &
        LOGICAL_BITWISE_OR, //  |
        LOGICAL_NOT, // !
        // Constants (Literals)
        INTEGER_LITERAL,
        FLOAT_LITERAL,
        BOOLEAN_LITERAL,
        // Other important stuff
        IDENTIFIER,
        // Controll Tokens 
        ERROR_TOKEN // <- This is usefull because now errors propagate to syntax analysis
    };
}

bool token_type_is_keyword(Token_Type::ENUM type);

union TokenAttribute
{
    int integer_value;
    float float_value;
    double double_value;
    bool bool_value;
    int identifier_number; // String identifier
};

struct Token
{
    Token_Type::ENUM type;
    TokenAttribute attribute;
    int line_number;
    int character_position;
    int lexem_length;
    int source_code_index;
};

struct LexerResult
{
    DynamicArray<String> identifiers;
    Hashtable<String, int> identifier_index_lookup_table;
    DynamicArray<Token> tokens;
    bool has_errors;
};

LexerResult lexer_parse_string(String* code);
void lexer_result_destroy(LexerResult* result);
void lexer_result_print(LexerResult* result);
String lexer_result_identifer_to_string(LexerResult* result, int index);



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
    DynamicArray<Ast_Node_Statement> statements;
};

struct Ast_Node_Statement
{
    StatementType::ENUM type;
    int variable_name_id;
    int variable_type_id;
    Ast_Node_Expression expression;
    Ast_Node_Statement_Block statements;
    Ast_Node_Statement_Block else_statements;
};

struct Parameter
{
    int name_id;
    int type_id;
};

struct Ast_Node_Function
{
    int function_name_id;
    int return_type_id;
    DynamicArray<Parameter> parameters;
    Ast_Node_Statement_Block body;
};

struct Ast_Node_Root
{
    DynamicArray<Ast_Node_Function> functions;
};
void ast_node_root_append_to_string(String* string, Ast_Node_Root* root, LexerResult* lexer_result);


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
    Array<Token> tokens;
    DynamicArray<ParserError> intermediate_errors; // Intermediate errors are reported just for debugging, I probably dont want this
    DynamicArray<ParserError> unresolved_errors;
    int index;
    Ast_Node_Root root;
};

Parser parser_parse(LexerResult* result);
void parser_destroy(Parser* parser);


/*
    AST_Interpreter
*/
namespace Ast_Interpreter_Value_Type
{
    enum ENUM
    {
        INTEGER,
        FLOAT,
        BOOLEAN,
        ERROR_VAL
    };
};

struct Ast_Interpreter_Value
{
    Ast_Interpreter_Value_Type::ENUM type;
    int int_value;
    float float_value;
    bool bool_value;
};

struct Ast_Interpreter_Statement_Result
{
    bool is_break;
    bool is_continue;
    bool is_return;
    Ast_Interpreter_Value return_value;
};


Ast_Interpreter_Value ast_interpreter_execute_main(Ast_Node_Root* root, LexerResult* lexer);
void ast_interpreter_value_append_to_string(Ast_Interpreter_Value value, String* string);
String ast_interpreter_value_type_to_string(Ast_Interpreter_Value_Type::ENUM type);
