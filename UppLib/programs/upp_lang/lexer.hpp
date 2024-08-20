#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"

struct Token;
struct Identifier_Pool;
struct Source_Code;

// Tokens
enum class Operator
{
    ADDITION,
    SUBTRACTION,
    DIVISON,
    MULTIPLY,
    MODULO,
    COMMA,
    DOT,
    TILDE,           // ~    Used in Path-lookup A~B~c
    TILDE_STAR,      // ~*   Import all
    TILDE_STAR_STAR, // ~**  Transitive import all
    COLON,
    SEMI_COLON,
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
    DEFINE_COMPTIME, // ::
    DEFINE_INFER, // :=
    AND,
    OR,
    ARROW,
    DOLLAR,
    ASSIGN,
    ASSIGN_POINTER, // =*
    ASSIGN_ADD,
    ASSIGN_SUB,
    ASSIGN_MULT,
    ASSIGN_DIV,
    ASSIGN_MODULO,

    MAX_ENUM_VALUE
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
    INSTANCIATE,
    IMPORT,
    AS,
    CONTEXT,
    FOR,
    IN_KEYWORD,
    CONST_KEYWORD,

    MAX_ENUM_VALUE
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
        i64 int_val;
        f64 float_val;
        bool boolean;
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
    COMMENT, // Single line_index for now (Required for reconstructing text from tokens)
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
int character_index_to_token(Dynamic_Array<Token>* tokens, int char_index, bool after_cursor);
bool char_is_parenthesis(char c);
Parenthesis char_to_parenthesis(char c);
char parenthesis_to_char(Parenthesis p);

String token_type_as_string(Token_Type type);
String syntax_keyword_as_string(Keyword keyword);
String token_get_string(Token token, String text);
Operator_Info syntax_operator_info(Operator op);



// Lexer
void lexer_initialize(Identifier_Pool* pool);
void lexer_shutdown();
void lexer_tokenize_text(String text, Dynamic_Array<Token>* tokens);
void lexer_tokenize_text_as_comment(String text, Dynamic_Array<Token>* tokens);

