#pragma once
#include "../../math/vectors.hpp"

struct vec3;
struct vec4;
struct Symbol;
enum class Symbol_Type;

enum class Syntax_Color : u8
{
    NONE, // transparent black, 0, 0, 0, 0
    SEPERATOR_LINE, // Special token to indicate a seperator line

    WHITE,
    BLACK,
    RED,
    GREEN,
    BLUE,
    YELLOW,
    MAGENTA,
    CYAN,
    PURPLE,
    ORANGE,

    TEXT,
    VARIABLE,
    DATATYPE,
    VALUE_DEFINITION,
    PRIMITIVE,
    STRING,
    LITERAL_NUMBER,
    SUBTYPE,
    MEMBER,
    ENUM_MEMBER,
    KEYWORD,
    COMMENT,
    FUNCTION,
    MODULE,
    INVALID_TOKEN,
    BOOLEAN_LITERAL,
    FOLD_ERROR_BG,
    FOLD_BG,
    SEARCH_HIGHLIGHT_BG,
    VISUAL_BLOCK_BG,
    CURSOR_BG,
    SUGGESTION_BG,
    FILE_DIRECTORY,

    ERROR_DISPLAY_TEXT,

    BG_NORMAL,
    BG_ERROR,
    BG_HIGHLIGHT,

	CONTEXT_BG,
	CONTEXT_TEXT,
	CONTEXT_ERROR_TEXT,
	CONTEXT_BORDER,
};

vec3 syntax_color_to_vec3(Syntax_Color color);

Syntax_Color symbol_type_to_color(Symbol_Type type);
Syntax_Color symbol_to_color(Symbol* symbol, bool is_definition);
