#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"
#include "text.hpp"

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
        COMMENT,
        WHITESPACE, // Tabs and spaces
        NEW_LINE, // \n
        // Controll Tokens 
        ERROR_TOKEN // <- This is usefull because now errors propagate to syntax analysis
    };
}

union TokenAttribute
{
    int integer_value;
    float float_value;
    bool bool_value;
    int identifier_number;
};

struct Token
{
    Token_Type::ENUM type;
    TokenAttribute attribute;
    // Position information
    Text_Slice position;
    int source_code_index;
};

// TODO: Lexer should also have a token array with whitespaces and comments, for editor/ide's sake
struct Lexer
{
    DynamicArray<String> identifiers;
    Hashtable<String, int> identifier_index_lookup_table;
    DynamicArray<Token> tokens;
    DynamicArray<Token> tokens_with_whitespaces;
};

bool token_type_is_keyword(Token_Type::ENUM type);
const char* token_type_to_string(Token_Type::ENUM type);

Lexer lexer_create();
void lexer_destroy(Lexer* result);
void lexer_parse_string(Lexer* lexer, String* code);
void lexer_print(Lexer* result);
String lexer_identifer_to_string(Lexer* Lexer, int index); // Does not do much
int lexer_add_or_find_identifier_by_string(Lexer* Lexer, String identifier);
void lexer_print_identifiers(Lexer* lexer);
