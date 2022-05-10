#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "../../math/vectors.hpp"

/*
    Features I want:
     - Auto-Formating (Whitespaces + Auto-Wrapping, Auto Folding...)
     - Editing-Freedom (Delete wherever I want, insert wherever I want)
     - Code-Completion
     - Better Parser Errors (Display missing Gaps, possible continuations)
     - Level-Of-Detail (Folding)
     - Indentation-Based Navigation (+ other Code-Based navigations)
     - Code-Views (Dion-Slices, only display/edit a Slice of Code, e.g. only 1 Module)
     - Better Rendering (Smooth-Scrolling, Cursor-Movement Traces, Good Selection, Animated Editing)
     - Less input (Less Keystrokes for Space, Semicolon, {})
     - Transformative Copy-Paste (Requires Selection of valid source code)
     - Single Project-Files

    How I want to achieve this:
    By combining Editor, Lexer and Parser into one Entity

    Current Design:
        Blocks have Hierarchical-Representation
        Lines have Text-Representation
        Text gets auto-formated after each edit
        Formating can be done independent from Representation
*/

struct Rendering_Core;
struct Input;
struct Renderer_2D;
struct Text_Renderer;
struct Timer;

// Tokens
constexpr auto SYNTAX_OPERATOR_COUNT = 31;
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
    HASHTAG,
    ASSIGN,
    ASSIGN_ADD,
    ASSIGN_SUB,
    ASSIGN_MULT,
    ASSIGN_DIV
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
    GAP,
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

// Source Code
struct Syntax_Block;
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

bool syntax_position_on_line(Syntax_Position pos);
bool syntax_position_on_token(Syntax_Position pos);
Syntax_Line* syntax_position_get_line(Syntax_Position pos);
Syntax_Token* syntax_position_get_token(Syntax_Position pos);

// Functions
void syntax_editor_create(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Input* input, Timer* timer);
void syntax_editor_destroy();
void syntax_editor_update();
void syntax_editor_render();

Parenthesis char_to_parenthesis(char c);
char parenthesis_to_char(Parenthesis p);















