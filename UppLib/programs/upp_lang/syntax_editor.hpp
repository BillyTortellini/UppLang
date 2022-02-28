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
TODO and Ending-Thoughts:
 - Finish all expressions 
 - Add literal tokens (True/False, Strings, Float numbers)
 - Sanity check for syntax-item ranges

Second parser for analysable parsing
 - Checks for correct things, otherwise logs errors
 - Generates a new Structure which only contains analysable Structures
*/

/*
    Features I want:
     - Auto-Formating (Whitespaces + Text-Wrapping)
     - Editing-Freedom (Delete wherever I want, insert wherever I want)
     - Code-Completion
     - Better Parser Errors (Display missing Gaps, possible continuations)
     - Level-Of-Detail (Folding)
     - Indentation-Based Navigation
     - Code-Views (Dion-Slices, only display/edit a Slice of Code, e.g. only 1 Module)
     - Better Rendering (Smooth-Scrolling, Cursor-Movement Traces, Good Selection)
     - Less input (Less Keystrokes for Space, Semicolon, {})
     - Transformative Copy-Paste (Requires Selection of valid source code)
     - Single Project-Files

    How I want to achieve this:
    By combining Editor, Lexer and Parser into one Entity


    Milestones:
     X   Single Line where Text is tokenized, whitespace handling (Lexer)
     X   Cursor Movement in tokens, simple Editing (Insert before/after, Change, delete)
     X   Single Line basic Expression Parsing (Calculator things, Parenthesis + Operators) 
     X   Parsing Error-Messages and Formating
     X   More 'difficult' Expression Things (Function Calls, Member access, Unary Operators)
     X   Multiple Lines + Movement between lines
     X   Statements: Expressions, Definitions, Assignments
     X   Keyword-Statements (Break Continue Return Delete)
     X   Block Statements (If Else, While, Defer, Switch)
     X   Indentation Handling (Block Detection)
     X   Top-Level Expressions (Function, Struct, Enum, Module)
     X   Multi-Delimiter Binops
     O   All other Expressions (Arrays, Types, new, auto-stuff, cast, binops, unary_ops, ...)
     O   New Datastructure for valid, analysable Code-Tree
     O   Reintegration with Compiler
     O   Vim-Feature Reintegration (Undo-Redo, Yank/Put, Movements and Motions)

    Current Design:
     - Token-Based Editing
     - Syntax-Guided Layout
     - Lines can be parsed separatly

    Other Expressions:
     - Paths
     - Array Type + Slice Type [5]int 
     - new 
     - new array
     - array Access (array[5])
        

*/

// Lexer
enum class Syntax_Token_Type
{
    IDENTIFIER,
    DELIMITER,
    NUMBER,
};

struct Syntax_Token
{
    Syntax_Token_Type type;
    union
    {
        String identifier;
        char delimiter;
        String number;
    } options;
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

enum class Operator_Type
{
    ADDITION,           // +
    SUBTRACTION,        // -
    DIVISON,            // /
    MULTIPLY,           // *
    MODULO,             // %
    COMMA,              // ,
    DOT,                // .
    COLON,              // :
    ASSIGN,             // =
    NOT,                // !
    AMPERSAND,          // &
    VERT_LINE,          // |
    OPEN_PARENTHESIS,   // (
    CLOSED_PARENTHESIS, // )
    OPEN_BRACKET,       // [
    CLOSED_BRACKET,     // ]
    OPEN_BRACES,        // {
    CLOSED_BRACES,      // }
    LESS_THAN,          // <
    GREATER_THAN,       // >
    LESS_EQUAL,         // <=
    GREATER_EQUAL,      // >=
    EQUALS,             // ==
    NOT_EQUALS,         // !=
    POINTER_EQUALS,     // *==
    POINTER_NOT_EQUALS, // *!=
    AND,                // &&
    OR,                 // ||
    ARROW,              // ->
};

struct Operator_Mapping
{
    Operator_Type type;
    String string;
};

enum class Parse_Token_Type
{
    IDENTIFIER,
    KEYWORD,
    OPERATOR,
    NUMBER,
    INVALID, // Currently only single |
};

struct Parse_Token
{
    Parse_Token_Type type;
    int start_index;
    int length;
    union {
        Keyword_Type keyword;
        Operator_Type operation;
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
    bool token_is_missing;
    int token_index;
    AST_Item* operand;
};

enum class Definition_Type
{
    NORMAL,   // x: int
    COMPTIME, // x:: int
    INFER,    // x := 5
    DEFINE_ASSIGN,   // x: int = 5
    ASSIGNMENT,   // x + 32 = 15
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

    MODULE,
    STRUCT,
    C_UNION,
    UNION,
    ENUM,

    // Statements
    DEFINITION,
    STATEMENT_ASSIGNMENT,
    // Keyword Statements
    STATEMENT_BREAK,
    STATEMENT_CONTINUE,
    STATEMENT_RETURN,
    STATEMENT_DELETE,
    // Block-Statements
    STATEMENT_IF,
    STATEMENT_ELSE,
    STATEMENT_ELSE_IF,
    STATEMENT_WHILE,
    STATEMENT_DEFER,
    STATEMENT_SWITCH,
    STATEMENT_CASE,

    // Special
    ERROR_NODE,
};

struct AST_Item
{
    AST_Type type;
    Range range;
    union
    {
        // Top-Level
        struct {
            Definition_Type definition_type;
            AST_Item* assign_to_expr;
            AST_Item* type_expression;
            AST_Item* value_expression;
            int assign_token_index;
            int colon_token_index;
        } statement_definition;

        // Statements
        AST_Item* statement_delete;
        AST_Item* statement_continue;
        AST_Item* statement_break;
        AST_Item* statement_if;
        AST_Item* statement_else_if;
        AST_Item* statement_while;
        AST_Item* statement_switch;
        AST_Item* statement_case;
        struct {
            bool has_value;
            AST_Item* expression;
        } statement_return;

        // Expressions
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
            AST_Item* parenthesis_item;
        } function_call;
        struct {
            AST_Item* item;
            bool has_identifier;
            int identifier_token_index;
            int dot_token_index;
        } member_access;
        struct {
            Dynamic_Array<AST_Item*> items;
            Dynamic_Array<int> comma_indices;
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
    bool is_keyword;
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
    Dynamic_Array<Syntax_Token> syntax_tokens;
    Dynamic_Array<Parse_Token> parse_tokens;
    Dynamic_Array<Render_Item> render_items;
    Dynamic_Array<Error_Message> error_messages;
    Dynamic_Array<AST_Item*> allocated_items;
    AST_Item* root;
    int parse_index;
    int render_item_offset;
    int indentation_level;
    bool requires_block;
};

struct Syntax_Block;
struct Block_Item
{
    bool is_line;
    int line_index;
    Syntax_Block* block;
};

struct Syntax_Block
{
    Syntax_Block* parent;
    Dynamic_Array<Block_Item> items;
    int line_start_index;
    int line_end_index_exclusive;
    int indentation;
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
    Dynamic_Array<Operator_Mapping> operator_table;

    // Block-Parsing
    Syntax_Block* root_block;
    int block_parse_index;

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












