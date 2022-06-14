#pragma once

#include "../../math/vectors.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"

// Prototypes
struct Identifier_Pool;

// TYPES
constexpr auto SYNTAX_OPERATOR_COUNT = 30;
enum class Syntax_Operator
{
    ADDITION,
    SUBTRACTION,
    DIVISON,
    MULTIPLY,
    MODULO,
    COMMA,
    DOT,
    TILDE,
    COLON,
    NOT,
    AMPERSAND,
    LESS_THAN,
    GREATER_THAN,
    LESS_EQUAL,
    GREATER_EQUAL,
    EQUALS,
    NOT_EQUALS,
    POINTER_EQUALS,
    POINTER_NOT_EQUALS,
    DEFINE_COMPTIME,
    DEFINE_INFER,
    AND,
    OR,
    ARROW,
    DOLLAR,
    ASSIGN,
    ASSIGN_ADD,
    ASSIGN_SUB,
    ASSIGN_MULT,
    ASSIGN_DIV
};

enum class Operator_Type
{
    BINOP,
    UNOP,
    BOTH,
};

enum class Keyword
{
    RETURN,
    BREAK,
    CONTINUE,
    IF,
    ELSE,
    WHILE,
    SWITCH,
    CASE,
    DEFAULT,
    MODULE,
    NEW,
    STRUCT,
    UNION,
    C_UNION,
    ENUM,
    DELETE_KEYWORD,
    DEFER,
    CAST,
    CAST_RAW,
    CAST_PTR,
    BAKE,
    IMPORT,
    NULL_KEYWORD,

    MAX_ENUM_VALUE,
};

enum class Parenthesis_Type
{
    PARENTHESIS,
    BRACKETS,   // []
    BRACES,     // {} 
};

struct Parenthesis
{
    Parenthesis_Type type;
    bool is_open;
};

enum class Literal_Type
{
    STRING,
    INTEGER,
    FLOAT_VAL,
    BOOLEAN,
    NULL_VAL,
};

struct Literal_Value
{
    Literal_Type type;
    union {
        String* string;
        int int_val;
        bool boolean;
        float float_val;
    } options;
};

struct Operator_Info
{
    String string;
    Operator_Type type;
    bool space_before;
    bool space_after;
};




// Source Code
enum class Token_Type
{
    IDENTIFIER,
    KEYWORD,
    LITERAL,
    OPERATOR,
    PARENTHESIS,
    INVALID, // Unexpected Characters (like | or ;), strings with invalid escape sequences or invalid identifiers (5member)
    COMMENT, // Single line for now (Required for reconstructing text from tokens)
};

struct Token
{
    Token_Type type;
    int start_index;
    int end_index; // In theory I can remove the end index since it is given by start of next token
    union {
        Syntax_Operator op;
        String* identifier;
        Literal_Value literal_value;
        Keyword keyword;
        Parenthesis parenthesis;
    } options;
};

struct Source_Line
{
    Array<Token> tokens;
    Optional<int> follow_block_index;
};

struct Source_Block {
    Dynamic_Array<Source_Line> lines;
};

struct Source_Code {
    Dynamic_Array<Source_Block> blocks;
    bool delete_tokens_on_destroy;
};

struct Token_Position {
    int block;
    int line;
    int token;
};

struct Token_Range {
    Token_Position start;
    Token_Position end;
};

// Lexer
void lexer_initialize(Identifier_Pool* pool);
void lexer_shutdown();
void lexer_tokenize_text(String text, Dynamic_Array<Token>* tokens);
void lexer_tokens_to_text(Dynamic_Array<Token>* tokens, String* text);

// Helpers
int character_index_to_token(Dynamic_Array<Token>* tokens, int char_index);
bool char_is_parenthesis(char c);
Parenthesis char_to_parenthesis(char c);
char parenthesis_to_char(Parenthesis p);
String syntax_keyword_as_string(Keyword keyword);
String token_get_string(Token token, String text);
bool is_space_critical(Token* t);
Operator_Info syntax_operator_info(Syntax_Operator op);

