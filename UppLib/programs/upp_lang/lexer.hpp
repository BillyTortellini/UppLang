#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"

struct Token;
struct Identifier_Pool;
struct Source_Code;

// Tokens
constexpr auto SYNTAX_OPERATOR_COUNT = 30;
enum class Operator
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
        void* null_ptr;
    } options;
};

struct Operator_Info
{
    String string;
    Operator_Type type;
    bool space_before;
    bool space_after;
};

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
        Operator op;
        String* identifier;
        Literal_Value literal_value;
        Keyword keyword;
        Parenthesis parenthesis;
    } options;
};

bool char_is_space_critical(char c);
int character_index_to_token(Dynamic_Array<Token>* tokens, int char_index);
bool char_is_parenthesis(char c);
Parenthesis char_to_parenthesis(char c);
char parenthesis_to_char(Parenthesis p);

String syntax_keyword_as_string(Keyword keyword);
String token_get_string(Token token, String text);
Operator_Info syntax_operator_info(Operator op);



// Token Code
struct Token_Line
{
    Array<Token> tokens;
    Optional<int> follow_block;
    int origin_line_index;
};

struct Token_Block
{
    Dynamic_Array<Token_Line> lines;
    int parent_line;
    int parent_block;
    int indentation;
};

struct Token_Code
{
    Dynamic_Array<Token_Block> blocks;
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

Token_Code token_code_create_from_source(Source_Code* source_code);
void token_code_destroy(Token_Code* code);
Token_Block* token_position_get_block(Token_Position p, Token_Code* code);
Token_Line* token_position_get_line(Token_Position p, Token_Code* code);
Token* token_position_get_token(Token_Position p, Token_Code* code);
void token_position_sanitize(Token_Position* p, Token_Code* code);
bool token_position_are_equal(Token_Position a, Token_Position b);
bool token_position_in_order(Token_Position a, Token_Position b, Token_Code* code);
Token_Position token_position_next(Token_Position pos, Token_Code* code);



// Lexer
void lexer_initialize(Identifier_Pool* pool);
void lexer_shutdown();
void lexer_tokenize_text(String text, Dynamic_Array<Token>* tokens);
void lexer_tokens_to_text(Dynamic_Array<Token>* tokens, String* text);

