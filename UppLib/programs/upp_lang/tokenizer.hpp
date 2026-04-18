#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/allocators.hpp"
#include "source_code.hpp"
#include "../../math/vectors.hpp"

struct Arena;

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
    LITERAL_NIL,

    // Operators
    _OPERATORS_START_,

    PLUS,                 // +
    MINUS,                // - 
    SLASH,                // /
    ASTERIX,              // *
    PERCENTAGE,           // %

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
    POSTFIX_CALL_ARROW,   // ->
    FUNCTION_ARROW,       // =>, note: different to DOT_CALL ->
    SUBTYPE_ACCESS,       // .>
    BASETYPE_ACCESS,      // .<
    ADDRESS_OF,           // -*
    DEREFERENCE,          // -&
    OPTIONAL_DEREFERENCE, // -?&
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

    // Toplevel Definitions
    FUNCTION_KEYWORD, // fn
    MODULE,
    STRUCT,
    UNION,
    ENUM,
    VAR,
    GLOBAL_KEYWORD,
    CONST_KEYWORD,
    OPERATORS,
    IMPORT,
    EXTERN,

    // Block statements
    IF,
    ELSE,
    SWITCH,
    DEFAULT,
    LOOP,
    SCOPE,
    DEFER,

    AS,
    IN_KEYWORD,
    RETURN,
    BREAK,
    CONTINUE,
    CAST,
    DEFER_RESTORE,
    
    // #keywords, 
    BAKE,
    INSTANCIATE,
    GET_OVERLOAD,
    GET_OVERLOAD_POLY,

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

void tokenizer_tokenize_single_line(String text, DynArray<Token>* tokens, int line_index, bool remove_comments);
DynArray<Token> tokenize_partial_code(
    Source_Code* code, Text_Index index, Arena* arena, int& token_index, bool handle_line_continuations, bool remove_comments);

// Returns start/end index (Inclusive), -1 if not found
ivec2 tokens_get_parenthesis_range(DynArray<Token> tokens, int start, Token_Type type, Arena* arena);
void tokenizer_parse_string_literal(String literal, String* append_to);

Token_Class token_type_get_class(Token_Type type);
bool token_type_is_operator(Token_Type type);
bool token_type_is_keyword(Token_Type type);
Token_Type token_type_get_partner(Token_Type type);
const char* token_type_as_cstring(Token_Type token_type);

struct Continuation_Info
{
	bool connects_to_previous;
	bool connects_to_next;
	bool is_statement_start;
	bool is_parenthesis;
	Token_Type type;
};
Continuation_Info token_type_get_continuation_info(Token_Type type);
Continuation_Info continuation_info_make(Token_Type type, bool connects_to_previous, bool connects_to_next, bool is_statement_start, bool is_parenthesis);

