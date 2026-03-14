#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/allocators.hpp"

enum class Token_Class
{
	LIST_START,
	LIST_END,
	SEPERATOR,
	OTHER
};

enum class Token_Type
{
    IDENTIFIER,
    COMMENT,
    INVALID, 

    // Tokens for parsing
    BLOCK_START,
    BLOCK_END,
    LINE_END, // Seperator for items in blocks

    // Literals
    LITERAL_INTEGER,
    LITERAL_FLOAT,
    LITERAL_STRING,

    LITERAL_TRUE,
    LITERAL_FALSE,
    LITERAL_NULL,

    // Operators
    _OPERATORS_START_,

    CONCATENATE_LINES,    // \

    ADDITION,             // +
    SUBTRACTION,          // - 
    DIVISON,              // /
    MULTIPLY,             // *
    MODULO,               // %

    EQUALS,               // ==
    NOT_EQUALS,           // !=
    POINTER_EQUALS,       // *==
    POINTER_NOT_EQUALS,   // *!=
    LESS_THAN,            // <
    GREATER_THAN,         // >
    LESS_EQUAL,           // <=
    GREATER_EQUAL,        // >=

    AND,                  // &&
    OR,                   // ||
    NOT,                  // !

    PARENTHESIS_OPEN,     // (
    PARENTHESIS_CLOSED,   // )
    BRACKET_OPEN,         // [
    BRACKET_CLOSED,       // ]
    CURLY_BRACE_OPEN,     // {
    CURLY_BRACE_CLOSED,   // }

    COMMA,                // ,
    DOT,                  // .
    TILDE,                // ~    Used in Path-lookup A~B~c
    TILDE_STAR,           // ~*   Import all
    TILDE_STAR_STAR,      // ~**  Transitive import all
    COLON,                // :
    SEMI_COLON,           // ;
    APOSTROPHE,           // '
    QUESTION_MARK,        // ?
    OPTIONAL_POINTER,     // ?*
    DOT_CALL,             // ->
    ADDRESS_OF,           // -*
    DEREFERENCE,          // -&
    OPTIONAL_DEREFERENCE, // -?&
    DEFINE_COMPTIME,      // ::
    DEFINE_INFER,         // :=
    ARROW,                // =>, note: different to DOT_CALL ->
    DOLLAR,               // $
    ASSIGN,               // =
    ASSIGN_ADD,           // +=
    ASSIGN_SUB,           // -=
    ASSIGN_MULT,          // *=
    ASSIGN_DIV,           // /=
    ASSIGN_MODULO,        // %=
    UNINITIALIZED,        // _

    _OPERATORS_END_,

    // Keywords
    _KEYWORDS_START_,

    FUNCTION_KEYWORD,
    FUNCTION_POINTER_KEYWORD,
    RETURN,
    BREAK,
    CONTINUE,
    IF,
    ELSE,
    LOOP,
    IN_KEYWORD,
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
    IMPORT,
    AS,
    EXTERN,
    
    BAKE,
    INSTANCIATE,
    GET_OVERLOAD,
    GET_OVERLOAD_POLY,
    EXPLICIT_BLOCK,
    ADD_BINOP,
    ADD_UNOP,
    ADD_CAST,
    ADD_ARRAY_ACCESS,
    ADD_ITERATOR,

    _KEYWORDS_END_,
};

struct Token
{
    Token_Type type;
    int start;
    int end;
    int line;
    union 
    {
        i64 integer_value;
        f64 float_value;
        String* string_value; // Set to null without further processing
    } options; 
};

Token token_make(Token_Type type, int start, int end, int line);

void tokenizer_tokenize_line(String text, DynArray<Token>* tokens, int line_index, bool remove_comments);
void tokenizer_parse_string_literal(String literal, String* append_to);

Token_Class token_type_get_class(Token_Type type);
bool token_type_is_operator(Token_Type type);
bool token_type_is_keyword(Token_Type type);
Token_Type token_type_get_partner(Token_Type type);
const char* token_type_as_cstring(Token_Type token_type);

