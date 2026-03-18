#pragma once
#include "../../math/vectors.hpp"

struct vec3;
struct vec4;
struct Symbol;
enum class Symbol_Type;

enum class Palette_Color : u8
{
    NONE,

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

    LIGHT_BLUE,
    LIGHT_RED,
    SLIGHT_LIGHT_BLUE,
    SLIGHT_LIGHT_RED,
    SLIGHT_DARK_GREEN,
    SLIGHT_LIGHT_GREEN,
    SLIGHT_DARK_RED,
    BLUE_SLIGHT_DESATURATED,
    DARK_YELLOW,
    DARK_PURPLE,
    SLIGHT_DARK_AMBER,
    PASTEL_YELLOW,
    PASTEL_PURPLE,
    LIGHT_ORANGE,

    LIGHT_CYAN,
    MIDDLE_CYAN,
    DARK_CYAN,

    GREY1,
    GREY2,
    GREY3,
    GREY4,
    GREY5,
    GREY6,
    GREY7,
    GREY8,
    GREY9,
};

vec3 palette_color_to_vec3(Palette_Color color);

enum class Syntax_Color : u8
{
    NONE, // transparent black, 0, 0, 0, 0

    // Token things
    TEXT,
    INVALID_TOKEN,

    VARIABLE,
    VALUE_DEFINITION,
    STRING_LITERAL,
    BOOLEAN_LITERAL,
    LITERAL_NUMBER,

    DATATYPE,
    PRIMITIVE, // int float bool, i8,....
    SUBTYPE,

    KEYWORD,
    MEMBER,
    ENUM_MEMBER,
    COMMENT,
    FUNCTION,
    MODULE,

    // Editor things
    FOLD_TEXT,
    FOLD_BG,
    FOLD_ERROR_BG,

    COMPLETABLE_COMMAND_TEXT,
    COMPLETABLE_COMMAND_BG,
    COMPLETABLE_COMMAND_BORDER,

	CONTEXT_BG,
	CONTEXT_TEXT,
	CONTEXT_ERROR_TEXT,
	CONTEXT_BORDER,

    SEARCH_HIGHLIGHT_BG,
    VISUAL_BLOCK_BG,
    CURSOR_BG,
    SUGGESTION_BG,
    FILE_DIRECTORY,
    MISSING_PARAM,
    CURRENT_PARAM_BG,
    CURRENT_PARAM_UNDERLINE,

    ERROR_DISPLAY_TEXT,
    ERROR_NAVIGATION_HIGHLIGHT,

    BG_NORMAL,
    BG_ERROR,
    BG_HIGHLIGHT,

    ERROR_NAVIGATION_BG,
    LINE_EDIT_HIGHLIGHT_BG,

};

Palette_Color syntax_color_to_palette_color(Syntax_Color color);
vec3 syntax_color_to_vec3(Syntax_Color color);

Syntax_Color symbol_type_to_color(Symbol_Type type);
Syntax_Color symbol_to_color(Symbol* symbol, bool is_definition);
