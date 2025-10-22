#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"

struct Token;
struct Identifier_Pool_Lock;
struct Source_Code;

// Tokens
enum class Operator
{
    ADDITION,
    SUBTRACTION,
    DIVISON,
    MULTIPLY,
    MODULO,
    COMMA,           // ,
    DOT,             // .
    TILDE,           // ~    Used in Path-lookup A~B~c
    TILDE_STAR,      // ~*   Import all
    TILDE_STAR_STAR, // ~**  Transitive import all
    COLON,           // :
    SEMI_COLON,      // ;
    APOSTROPHE,      // '
    QUESTION_MARK,   // ?
    OPTIONAL_POINTER,   // ?*
    DOT_CALL,        // .>
    NOT,            // !
    AMPERSAND,
    LESS_THAN,
    GREATER_THAN,
    LESS_EQUAL,
    GREATER_EQUAL,
    EQUALS,
    NOT_EQUALS,
    POINTER_EQUALS,     // *==
    POINTER_NOT_EQUALS, // *!=
    DEFINE_COMPTIME, // ::
    DEFINE_INFER, // :=
    DEFINE_INFER_POINTER, // :=*
    DEFINE_INFER_RAW, // :=~
    AND,
    OR,
    ARROW,       // =>, note: different to DOT_CALL ->
    INFER_ARROW, // .=>
    DOLLAR,
    ASSIGN,
    ASSIGN_POINTER, // =*
    ASSIGN_RAW, // =~
    ASSIGN_ADD,
    ASSIGN_SUB,
    ASSIGN_MULT,
    ASSIGN_DIV,
    ASSIGN_MODULO,
    UNINITIALIZED, // _

    MAX_ENUM_VALUE
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
    DEFAULT,
    MODULE,
    NEW,
    STRUCT,
    UNION,
    ENUM,
    DELETE_KEYWORD,
    DEFER,
    DEFER_RESTORE,
    CAST,
    CAST_POINTER,
    BAKE,
    INSTANCIATE,
    GET_OVERLOAD,
    GET_OVERLOAD_POLY,
    IMPORT,
    AS,
    CONTEXT,
    FOR,
    IN_KEYWORD,
    CONST_KEYWORD,
    MUTABLE, // mut
    EXTERN,

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
    int end_index;
    union {
        Operator op;
        String* identifier; // In string pool
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
String operator_get_string(Operator op);



// Lexer
void lexer_initialize();
void lexer_shutdown();
void lexer_tokenize_line(String text, Dynamic_Array<Token>* tokens, Identifier_Pool_Lock* identifier_pool);

