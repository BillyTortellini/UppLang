#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../math/vectors.hpp"

// Prototypes
struct Text_Renderer;
struct Rendering_Core;
struct Input;
struct Renderer_2D;

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
     O   Cursor Movement in tokens, simple Editing (Insert before/after, Change, delete)
     O   Single Line basic Expression Parsing (Calculator things, Parenthesis + Operators) 
     O   Parsing Error-Messages and Formating
     O   Multiple Lines with Indentation Handling 

    Token Types:
      IDENTIFIERS,
      KEYWORDS,
      DELIMITER (1-3 Character Operators, e.g. +-/*(){}[],.:;!)
      COMMENT
      ERROR
      WHITESPACE
      LITERALS (String Literals!)
*/

// Editor
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
    OPEN_PARENTHESIS,
    CLOSED_PARENTHESIS,
};

struct Syntax_Token
{
    Syntax_Token_Type type;
    int size;
    int x_pos;
    struct
    {
        Delimiter_Type delimiter;
        String identifier;
        struct {
            String text;
        } number;
    } options;
};

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

struct Syntax_Editor
{
    // Editing
    Editor_Mode mode;
    Dynamic_Array<Syntax_Token> tokens;
    int cursor_index; // In range 0-tokens.size
    Insert_Mode insert_mode;

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












