#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../math/vectors.hpp"

// Prototypes
struct Text_Renderer;
struct Rendering_Core;
struct Input;
struct Renderer_2D;
struct AST_Item;
struct Syntax_Editor;

/*
    Features I want:
     - Auto-Formating (Whitespaces + Text-Wrapping)
     - Better Parser Errors (Display missing Gaps)
     - Code-Completion
     - Transformative Copy-Paste
     - Level-Of-Detail (Folding)
     - Indentation-Based Navigation
     - Code-Views (Dion-Slices, only display/edit a Slice of Code)
     - Editing-Freedom (Delete wherever I want, insert wherever I want)
     - Better Rendering (Smooth-Scrolling, Cursor-Movement Traces, Good Selection)
     - Single Project-Files

    How I want to achieve this:
    By combining Editor, Lexer and Parser into one big thing

    Design-Ideas:
     - Token-Based Editing
     - Syntax-Guided Layout

    Milestones:
     X   Single Line where Text is tokenized, whitespace handling (Lexer)
     X   Cursor Movement in tokens, simple Editing (Insert before/after, Change, delete)
     X   Single Line basic Expression Parsing (Calculator things, Parenthesis + Operators) 
     X   Parsing Error-Messages and Formating
     X   More 'difficult' Expression Things (Function Calls, Member access, Unary Operators)
     X   Multiple Lines + Movement between lines
     X   Statements: Expressions, Definitions, Assignments
     O   Keyword-Statements (Break Continue Return Delete)
     O   Block Statements (If Else, While, Defer, Switch)
     O   Indentation Handling
     O   Top-Level Expressions (Function, Struct, Enum, Module)
     O   All other Expressions (Arrays, Types, new, ...)
     O   Reintegration with Compiler
     O   Vim-Feature Reintegration (Undo-Redo, Yank/Put, Movements and Motions)

    Token Types:
      IDENTIFIERS,
      KEYWORDS,
      DELIMITER (1-3 Character Operators, e.g. +-/*(){}[],.:;!)
      COMMENT
      ERROR
      WHITESPACE
      LITERALS (String Literals!)

    Definitions, Assignments:
        x: int
        id COLON Expr
*/

// Lexer
enum class Syntax_Token_Type
{
    IDENTIFIER,
    DELIMITER,
    NUMBER,
};

enum class Delimiter_Type
{
    PLUS,  // +
    MINUS, // -
    SLASH, // /
    STAR,  // *
    COMMA, // ,
    DOT,   // .
    COLON, // :
    ASSIGN, // =
    OPEN_PARENTHESIS,
    CLOSED_PARENTHESIS,
};

enum class Keyword_Type
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
};

struct Syntax_Token
{
    Syntax_Token_Type type;
    struct
    {
        Delimiter_Type delimiter;
        struct {
            String identifier;
            bool is_keyword;
            Keyword_Type keyword_type;
        } identifier;
        struct {
            String text;
        } number;
    } options;
};



// Parser
struct Range
{
    int start;
    int end;
};

struct Binop_Link
{
    bool operator_missing;
    int token_index;
    AST_Item* operand;
};

enum class Definition_Type
{
    NORMAL,   // x: int
    COMPTIME, // x:: int
    INFER,    // x := 5
    ASSIGN,   // x: int = 5
};

enum class AST_Type
{
    // Expressions
    BINOP_CHAIN,
    UNARY_OPERATION,
    PARENTHESIS,
    FUNCTION_CALL,
    MEMBER_ACCESS,
    OPERAND, // Identifier or Number

    // Statements
    DEFINITION,
    STATEMENT_ASSIGNMENT,
    STATEMENT_EXPRESSION,

    // Special
    ERROR_NODE,
};

struct AST_Item
{
    AST_Type type;
    Range range;
    union
    {
        struct {
            Definition_Type definition_type;
            AST_Item* type_expression;
            AST_Item* value_expression;
            int assign_token_index;
        } statement_definition;
        struct {
            AST_Item* left_side;
            AST_Item* right_side;
            int assign_token_index;
        } statement_assignment;
        AST_Item* statement_expression;
        struct {
            AST_Item* start_item;
            Dynamic_Array<Binop_Link> items;
        } binop_chain;
        struct {
            AST_Item* item;
            int operator_token_index;
        } unary_op;
        struct {
            AST_Item* function_item;
            Dynamic_Array<AST_Item*> arguments;
            Dynamic_Array<int> comma_indices;
            bool has_closing_parenthesis;
            int open_parenthesis_index;
            int closing_parenthesis_index;
        } function_call;
        struct {
            AST_Item* item;
            bool has_identifier;
            int identifier_token_index;
            int dot_token_index;
        } member_access;
        struct {
            AST_Item* item;
            bool has_closing_parenthesis;
        } parenthesis;
    } options;
};

struct Error_Message
{
    String text;
    int token_index;
};

struct Render_Item
{
    int size;
    int pos;
    bool is_token; // Either Token or Gap
    int token_index;
};



// EDITOR
enum class Editor_Mode
{
    NORMAL,
    INPUT,
};

enum class Insert_Mode
{
    APPEND, // Appends inputs to current token
    BEFORE,  // New token is added after current one
};

struct Syntax_Line
{
    Dynamic_Array<Syntax_Token> tokens;
    Dynamic_Array<Render_Item> render_items;
    Dynamic_Array<Error_Message> error_messages;
    Dynamic_Array<AST_Item*> allocated_items;
    AST_Item* root;
    int parse_index;
    int render_item_offset;
};

struct Syntax_Editor
{
    // Editing
    Editor_Mode mode;
    Insert_Mode insert_mode;
    Dynamic_Array<Syntax_Line> lines;
    int cursor_index; // In range 0-tokens.size
    int line_index;
    Hashtable<String, Keyword_Type> keyword_table;

    // Rendering
    Rendering_Core* rendering_core;
    Renderer_2D* renderer_2D;
    Text_Renderer* text_renderer;
    vec2 character_size;
};

Syntax_Editor* syntax_editor_create(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D);
void syntax_editor_destroy(Syntax_Editor* editor);
void syntax_editor_update(Syntax_Editor* editor, Input* input);
void syntax_editor_render(Syntax_Editor* editor);












