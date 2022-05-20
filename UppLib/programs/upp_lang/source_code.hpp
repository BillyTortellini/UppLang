#pragma once

#include "../../math/vectors.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"

// Prototypes
struct Syntax_Block;
struct Syntax_Line;
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

enum class Syntax_Keyword
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

struct Token_Info
{
    // Character information
    int char_start;
    int char_end;

    // Layout Information
    bool format_space_before;
    bool format_space_after;

    // Rendering Infos
    int screen_pos;
    int screen_size;
    vec3 screen_color;
};

struct Block_Info
{
    int indentation_level;
    int line_start;
    int line_end;
};

struct Line_Info
{
    int index; // Global line index for rendering
    int line_end;
};

struct Syntax_Position // I would use the name "Token_Position", but already in use
{
    Syntax_Block* block;
    int line_index;
    int token_index;
};

struct Syntax_Range
{
    Syntax_Position start;
    Syntax_Position end;
};



// Source Code
enum class Syntax_Token_Type
{
    IDENTIFIER,
    KEYWORD,
    LITERAL_NUMBER,
    LITERAL_STRING,
    LITERAL_BOOL,
    OPERATOR,
    PARENTHESIS,
    UNEXPECTED_CHAR, // Unexpected Character, like | or ; \...
    COMMENT, // Single line for now
    DUMMY, // All empty lines have 1 tokenized dummy token, so i dont have to worry about dumb stuff
};

struct Syntax_Token
{
    Syntax_Token_Type type;
    Token_Info info;
    union {
        Syntax_Operator op;
        String* identifier;
        String* literal_number;
        String* comment;
        struct {
            String* string; // With ""
            bool has_closure;
        } literal_string;
        bool literal_bool;
        Syntax_Keyword keyword;
        char unexpected;
        Parenthesis parenthesis;
    } options;
};

struct Syntax_Line
{
    String text;
    Dynamic_Array<Syntax_Token> tokens;
    Syntax_Block* parent_block;
    Syntax_Block* follow_block;
    Line_Info info;
};

struct Syntax_Block
{
    Syntax_Line* parent_line; // 0 for root
    Dynamic_Array<Syntax_Line*> lines; // Must be non-zero
    Block_Info info;
};



// Lexer
void lexer_initialize(Identifier_Pool* pool);
void lexer_shutdown();
void lexer_tokenize_block(Syntax_Block* block, int indentation);
void lexer_tokenize_syntax_line(Syntax_Line* line);
void lexer_reconstruct_line_text(Syntax_Line* line, int* editor_cursor);

// Source Code
Syntax_Block* syntax_block_create(Syntax_Line* parent_line);
void syntax_block_destroy(Syntax_Block* block);
void syntax_block_append_to_string(Syntax_Block* block, String* string, int indentation);
Syntax_Block* syntax_block_create_from_string(String text);
Syntax_Line* syntax_line_create(Syntax_Block* parent_block, int block_index);
void syntax_line_destroy(Syntax_Line* line);
void syntax_block_sanity_check(Syntax_Block* block);

// Helpers
String syntax_keyword_as_string(Syntax_Keyword keyword);
String syntax_token_as_string(Syntax_Token token);
Syntax_Token syntax_token_make_dummy();

bool syntax_line_is_comment(Syntax_Line* line);
bool syntax_line_is_multi_line_comment(Syntax_Line* line);
bool syntax_line_is_empty(Syntax_Line* line);
int syntax_line_index(Syntax_Line* line);
void syntax_line_remove_token(Syntax_Line* line, int index);
int syntax_line_character_to_token_index(Syntax_Line* line, int char_index);
Syntax_Line* syntax_line_next_line(Syntax_Line* line);
Syntax_Line* syntax_line_prev_line(Syntax_Line* line);
void syntax_line_move(Syntax_Line* line, Syntax_Block* block, int index);
void syntax_line_print_tokens(Syntax_Line* line);
Syntax_Position syntax_line_get_start_pos(Syntax_Line* line);
Syntax_Position syntax_line_get_end_pos(Syntax_Line* line);

// Navigation stuff
bool syntax_position_on_line(Syntax_Position pos);
bool syntax_position_on_token(Syntax_Position pos);
bool syntax_position_in_order(Syntax_Position a, Syntax_Position b);
bool syntax_position_equal(Syntax_Position a, Syntax_Position b);
Syntax_Line* syntax_position_get_line(Syntax_Position pos);
Syntax_Token* syntax_position_get_token(Syntax_Position pos);
Syntax_Position syntax_position_sanitize(Syntax_Position pos);
Syntax_Position syntax_position_advance_one_line(Syntax_Position a);
Syntax_Position syntax_position_advance_one_token(Syntax_Position a);
bool syntax_range_contains(Syntax_Range range, Syntax_Position pos);

bool char_is_parenthesis(char c);
Parenthesis char_to_parenthesis(char c);
char parenthesis_to_char(Parenthesis p);
