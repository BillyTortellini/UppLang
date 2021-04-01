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

struct Lexer
{
    DynamicArray<String> identifiers;
    Hashtable<String, int> identifier_index_lookup_table;
    // TODO: Lexer should also have a token array with whitespaces and comments, for editor/ide's sake
    DynamicArray<Token> tokens;
    bool has_errors;
};

Lexer lexer_parse_string(String* code);
void lexer_destroy(Lexer* result);
void lexer_print(Lexer* result);
String lexer_identifer_to_string(Lexer* Lexer, int index);
int lexer_add_or_find_identifier_by_string(Lexer* Lexer, String identifier);



/*
    AST
*/
namespace SymbolType
{
    enum ENUM
    {
        VARIABLE,
        FUNCTION,
        TYPE,
        // Here will also be types later on
        // Maybe i should put int, float, bool .... in here
    };
};

struct Ast_Node_Function;
namespace Variable_Type {
    enum ENUM;
}
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

struct Parameter
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
    DynamicArray<Parameter> parameters;
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

/*
    AST_Interpreter
*/
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

struct Ast_Interpreter_Value
{
    Variable_Type::ENUM type;
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

Ast_Interpreter_Value ast_interpreter_execute_main(Ast_Node_Root* root, Lexer* Lexer);
void ast_interpreter_value_append_to_string(Ast_Interpreter_Value value, String* string);
String ast_interpreter_value_type_to_string(Variable_Type::ENUM type);
