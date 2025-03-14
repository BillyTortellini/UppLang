#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"

struct Identifier_Pool_Lock;

enum class C_Token_Type
{
    // Keywords
    IF,
    ELSE,
    MODULE,
    FOR,
    WHILE,
    SWITCH,
    CONTINUE,
    DEFAULT,
    CASE,
    BREAK,
    RETURN,
    STRUCT,
    UNION,
    C_UNION,
    ENUM,
    NEW,
    DELETE_TOKEN,
    CAST,
    CAST_RAW,
    CAST_PTR,
    NULLPTR,
    DEFER,
    EXTERN,
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
    HASHTAG,      // #
    TILDE,        // ~
    DOLLAR,       // $
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
    COMPARISON_POINTER_EQUAL, // *==
    COMPARISON_POINTER_NOT_EQUAL, // *!=

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
    STRING_LITERAL,
    // Other important stuff
    IDENTIFIER_NAME,
    COMMENT,
    WHITESPACE, // Tabs and spaces
    NEW_LINE, // \n
    // Controll Tokens 
    ERROR_TOKEN // <- This is usefull because now errors propagate to syntax analysis
};

struct Text_Position
{
    int line_index;
    int character;
};

struct Text_Slice
{
    Text_Position start;
    Text_Position end;
};

union Token_Attribute
{
    int integer_value;
    float float_value;
    bool bool_value;
    String* id;
};

struct C_Token
{
    C_Token_Type type;
    Token_Attribute attribute;
    // Position information
    Text_Slice position;
    int source_code_index;
};

struct Identifier_Pool;
struct C_Lexer
{
    Identifier_Pool_Lock* pool_lock;
    Hashtable<String, C_Token_Type> keywords;
    Dynamic_Array<C_Token> tokens;
    Dynamic_Array<C_Token> tokens_with_decoration; // Includes comments and whitespaces
};

bool token_type_is_keyword(C_Token_Type type);
const char* token_type_to_string(C_Token_Type type);

C_Lexer c_lexer_create();
void c_lexer_destroy(C_Lexer* result);
void c_lexer_lex(C_Lexer* lexer, String* code, Identifier_Pool_Lock* pool_lock);
void c_lexer_print(C_Lexer* result);
