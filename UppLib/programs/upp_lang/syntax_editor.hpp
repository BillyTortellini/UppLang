#pragma once

struct Rendering_Core;
struct Input;
struct Renderer_2D;
struct Text_Renderer;

void syntax_editor_create(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Input* input);
void syntax_editor_destroy();
void syntax_editor_update();
void syntax_editor_render();

/*
Problems with current parser:
    Cursor Rendering with Non-Essential spaces
    Essential spaces before operators not working, e.g. "a |+" -> "a|+" because of the cursor index
    Essential spaces before an operator?
        "+| a" -> "+|a" -> '+ |a'
    2 Spaces doesn't work
    3 Spaces doesn't work
        "a  | b" -> "a |b"
    Space at start doesn't work
        "| b" -> "|b"

    if 

    So for rendering I need: 
        - Tokens (Text, color) 
        - Space before/Space after
        - Cursor Token + Cursor offset from token

    Essential Spaces:
        Determination of 


*/

/*
Lessons Learned:

What I am currently actually doing is creating a Code-Formater that does
Formating all the time, even during editing. If you would just run a normal formater after
every edit, you would run into the same problems I am facing now, some examples:

Problem 1:
    a + |b
    After deleting one the space before b, the formater would immediately add the space again after it, leading to either
    a + |b  (Delete operation did nothing)
    a +| b  (The cursor has moved)

Problem 2:
    If you do live formating then you have to question which Cursor placements are valid, and which aren't
    a + b
    Is the Position after + exactly the same as the position before b?



The general problems I am dealing with revolve around Space-Insertion and Space-Deletion in cases
where we don't want these things to happen. This auto Insertion/Deletion is problematic because
there is a difference between 
    Essential Spaces  ... Determine the Semantics/Tokenization of the Program
    Formatting Spaces ... Just there for visual clarity and readability
This problem could be solved by _not_ saving 'formating' spaces in the underlying text representation

Also the underlying text determines the possible Cursor-Positions

Then we also have some cases where we want double-spaces, so that we can insert between space-critical tokens
    if x
    if y x

And currently I am also unaware of how to implement gaps properly, since they should also have a cursor position
But maybe that cursor position may not be reachable in Insert mode

Gaps can possibly be inserted everywhere (Between Tokens), right?
    _ := 15
    _ := _ 
    x.

Scenarios:
    "+"        ->   _ + _
    "++"       ->   _ + _ + _
    "+-"       ->   _ + -_
    "x.5"      ->   x._ _ 5 
    "x..5"     ->   x._._ 5
    "a b"      ->   a _ b
    "else x"   ->   else _ x
    "15a.5"    ->   15 a.5
    "x +b"     ->   x  + b

Formatter
Input:  Text, Syntax-Tokens, Cursor Position
Output:
    - Display-Spacing between tokens (Given by the before/after booleans)
    - Trimmed Text                   (Given by essential spaces after things)
    - Trimmed cursor position        (Needs special handling, but this is done by the algorithm)
    - Cursor Token + token offset    (Given by function cursor-char to cursor token)

Algorithm:
    Tokenize Text
    -LATER: Determine Gaps based on parsing
    Determine Cursor-Token + Cursor offset
    Determine Token-Info for each token
    Trim Text based on token-info, update token-text mapping
    Set cursor-char based on prev. cursor-token
    Render line based on token infos 

So we have 2 basic classes of tokens, which are:
    Space-Critical          (Identifier, Keyword, Number-Literal) 
        - Spaces between Space critical tokens must be preserve
        - Spaces after a space critical token must be preserved (If the cursor is on it)
        - 2-Spaces must be preserved between Space critical tokens (If the cursor is in between)
    Non-Space Critical      (Operator, Parenthesis, Unexpected Char, Gap)
        - Depending on the context, these operators have different display spaces
            Both sides: "x+y"     ->  x + y
            No side:    "!-15"    ->  !-15
            Right side:  "x,y,z"  ->  x, y, z 

If i display the tokens based on token info, I 
think I need a function from char-pos to token index + offset


   
Another possibility is to make the editing be dependent on the token you are on (Previous solution)
but this also implies that text editing and Token-Editing need completely different logic, which seems annoying
*/

/*
Going back to Text-Representation: Why?
---------------------------------------
The problem with tokens is that they are a direct result of the underlying text, and when changing that text
changes in e.g. the middle of a token, this could cause the token-structure to change drastically
This could be resolved by a purely token based approach (E.g. -> and - > are two different things),
but I think this doesn't have a lot of benefits and only restricts editing-possibilities
    E.g. cannot combine two strings/numbers, cannot divide operators...
        what 54 -> what54
        x := 5  -> x: int = 5
With token based editing, I think you would need own UI to edit things like Comments and strings

So my current approach is to have an underlying text representation, which 
gets tokenized and parsed, and after that the text is formated automatically,
wheras rendering can still be done on the tokens to allow displaying additional information (Like Gaps, variable names, Context-Info), which
don't influence the editing process. 

And I think in normal mode I want token-based navigation, with another mode that reverts back to text (Inserting inside := operator)
*/

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
Editor/Syntax Properties:
 - Editor does auto-formating and shows missing Pieces (Gaps)
 - Lines can be parsed indiviually
 - Formating-Grammer is different than actual Language-Grammer
 - Indentation is important for the whole Project-Structure

Output: Abstract Syntax Tree without modifications

How should the Editor work?
Navigation?
Editing?
Behavior on Gaps/Indentation?
Multi-Sign Operators? (::, ->)

*/

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










/*

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














*/



