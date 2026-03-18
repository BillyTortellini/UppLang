#include "syntax_colors.hpp"
#include "symbol_table.hpp"


vec3 palette_color_to_vec3(Palette_Color color)
{
    switch (color)
    {
    case Palette_Color::NONE: return vec3(0.0f);

    case Palette_Color::WHITE: return vec3(1.0f);
    case Palette_Color::BLACK: return vec3(0.0f);
    case Palette_Color::RED: return vec3(1, 0, 0);
    case Palette_Color::GREEN: return vec3(0, 1, 0);
    case Palette_Color::BLUE: return vec3(0, 0, 1);
    case Palette_Color::YELLOW: return vec3(1, 1, 0);
    case Palette_Color::MAGENTA: return vec3(1, 0, 1);
    case Palette_Color::CYAN: return vec3(0, 1, 1);

    case Palette_Color::PURPLE: return vec3(.65f, .4f, .8f);
    case Palette_Color::ORANGE: return vec3(.85f, .65f, .0f);

    case Palette_Color::GREY1: return vec3(.1f);
    case Palette_Color::GREY2: return vec3(.2f);
    case Palette_Color::GREY3: return vec3(.3f);
    case Palette_Color::GREY4: return vec3(.4f);
    case Palette_Color::GREY5: return vec3(.5f);
    case Palette_Color::GREY6: return vec3(.6f);
    case Palette_Color::GREY7: return vec3(.7f);
    case Palette_Color::GREY8: return vec3(.8f);
    case Palette_Color::GREY9: return vec3(.9f);

    case Palette_Color::SLIGHT_LIGHT_GREEN: return vec3_color_from_code("#388E3C");
    case Palette_Color::LIGHT_BLUE: return vec3(.7f, .7f, 1.0f);
    case Palette_Color::SLIGHT_LIGHT_BLUE: return vec3(.5f, .5f, 1.0f);
    case Palette_Color::LIGHT_RED: return vec3(1.0f, .8f, .8f);
    case Palette_Color::SLIGHT_LIGHT_RED: return vec3(1.0f, .5f, .5f);
    case Palette_Color::SLIGHT_DARK_GREEN: return vec3(0.0f, 0.85f, 0.0f);
    case Palette_Color::SLIGHT_DARK_RED: return vec3(0.0f, 0.7f, 0.0f);
    case Palette_Color::BLUE_SLIGHT_DESATURATED: return vec3(0.1f, 0.1f, 0.9f);
    case Palette_Color::DARK_YELLOW: return vec3(0.3f, 0.3f, 0.1f);
    case Palette_Color::DARK_PURPLE: return vec3(0.5f, 0.3f, 1.0f);
    case Palette_Color::SLIGHT_DARK_AMBER: return vec3(.85f, .65f, .0f);
    case Palette_Color::PASTEL_YELLOW: return vec3_color_from_code("#E6EE9C");
    case Palette_Color::PASTEL_PURPLE: return vec3_color_from_code("#B867C5");
    case Palette_Color::LIGHT_ORANGE: return vec3_color_from_code("#D6B93A");

    case Palette_Color::LIGHT_CYAN: return vec3_color_from_code("#4874DB");;
    case Palette_Color::MIDDLE_CYAN: return vec3_color_from_code("#81D4FA");
    case Palette_Color::DARK_CYAN: return vec3_color_from_code("#0489C9");

    default: panic("");
    }

    return vec3(1, 0, 1);
}

Palette_Color syntax_color_to_palette_color(Syntax_Color color)
{
    switch (color)
    {
    case Syntax_Color::NONE: return Palette_Color::NONE;

    case Syntax_Color::TEXT:     return Palette_Color::WHITE;
    case Syntax_Color::LITERAL_NUMBER: return Palette_Color::GREY6;
    case Syntax_Color::VARIABLE: return Palette_Color::LIGHT_BLUE; // vec3(.7f, .7f, 1.0f);
    case Syntax_Color::COMMENT: return Palette_Color::SLIGHT_DARK_GREEN;
    case Syntax_Color::INVALID_TOKEN: return Palette_Color::LIGHT_RED;

    case Syntax_Color::DATATYPE: return Palette_Color::LIGHT_CYAN; 
    case Syntax_Color::VALUE_DEFINITION: return Palette_Color::MIDDLE_CYAN;
    case Syntax_Color::PRIMITIVE: return Palette_Color::LIGHT_CYAN;
    case Syntax_Color::STRING_LITERAL: return Palette_Color::SLIGHT_DARK_AMBER;
    case Syntax_Color::SUBTYPE: return Palette_Color::DARK_CYAN;
    case Syntax_Color::MEMBER: return Palette_Color::PASTEL_YELLOW;
    case Syntax_Color::ENUM_MEMBER: return Palette_Color::DARK_CYAN; 
    case Syntax_Color::KEYWORD: return Palette_Color::PASTEL_PURPLE; // Purple
    case Syntax_Color::FUNCTION: return Palette_Color::LIGHT_ORANGE;
    case Syntax_Color::MODULE: return Palette_Color::SLIGHT_LIGHT_GREEN;
    case Syntax_Color::BOOLEAN_LITERAL: Palette_Color::SLIGHT_LIGHT_BLUE;

    case Syntax_Color::FOLD_ERROR_BG: return Palette_Color::SLIGHT_LIGHT_RED;
    case Syntax_Color::FOLD_BG: return Palette_Color::GREY4;

    case Syntax_Color::SEARCH_HIGHLIGHT_BG: return Palette_Color::GREY3;
    case Syntax_Color::VISUAL_BLOCK_BG: return Palette_Color::GREY4;

    case Syntax_Color::CURSOR_BG: return Palette_Color::GREY3;
    case Syntax_Color::SUGGESTION_BG: return Palette_Color::GREY3;

    case Syntax_Color::FILE_DIRECTORY: return Palette_Color::BLUE_SLIGHT_DESATURATED;
    case Syntax_Color::ERROR_DISPLAY_TEXT: return Palette_Color::SLIGHT_LIGHT_RED;

    case Syntax_Color::BG_NORMAL: return Palette_Color::NONE;
    case Syntax_Color::BG_ERROR: return Palette_Color::SLIGHT_DARK_RED;
    case Syntax_Color::BG_HIGHLIGHT: return Palette_Color::DARK_YELLOW;

    case Syntax_Color::MISSING_PARAM: return Palette_Color::SLIGHT_LIGHT_RED;
    case Syntax_Color::CURRENT_PARAM_BG: return Palette_Color::GREY3;
    case Syntax_Color::CURRENT_PARAM_UNDERLINE: return Palette_Color::GREY8;

    case Syntax_Color::COMPLETABLE_COMMAND_BG: return Palette_Color::GREY2;
    case Syntax_Color::COMPLETABLE_COMMAND_BORDER: return Palette_Color::GREY3;
    case Syntax_Color::ERROR_NAVIGATION_HIGHLIGHT: return Palette_Color::GREY6;
    case Syntax_Color::ERROR_NAVIGATION_BG: return Palette_Color::GREY5;

    case Syntax_Color::CONTEXT_BG: return Palette_Color::GREY2;
    case Syntax_Color::CONTEXT_TEXT: return Palette_Color::WHITE;
    case Syntax_Color::CONTEXT_ERROR_TEXT: return Palette_Color::SLIGHT_LIGHT_RED;
    case Syntax_Color::CONTEXT_BORDER: return Palette_Color::DARK_PURPLE;

    case Syntax_Color::LINE_EDIT_HIGHLIGHT_BG: return Palette_Color::GREY3;

    default: panic("");
    }
    return Palette_Color::NONE;
}

vec3 syntax_color_to_vec3(Syntax_Color color) {
    return palette_color_to_vec3(syntax_color_to_palette_color(color));
}

Syntax_Color symbol_type_to_color(Symbol_Type type)
{
    switch (type)
    {
    case Symbol_Type::HARDCODED_FUNCTION: return Syntax_Color::FUNCTION; 
    case Symbol_Type::FUNCTION: return Syntax_Color::FUNCTION; 
    case Symbol_Type::MODULE: return Syntax_Color::MODULE; 
    case Symbol_Type::DATATYPE: return Syntax_Color::DATATYPE; 
    case Symbol_Type::VARIABLE: return Syntax_Color::VARIABLE; 
    case Symbol_Type::COMPTIME_VALUE: return Syntax_Color::VARIABLE; 
    case Symbol_Type::DEFINITION_UNFINISHED: return Syntax_Color::TEXT; 
    case Symbol_Type::ALIAS: return Syntax_Color::VARIABLE; 
    case Symbol_Type::ALIAS_UNFINISHED: return Syntax_Color::VARIABLE; 
    case Symbol_Type::ERROR_SYMBOL: return Syntax_Color::TEXT; 
    case Symbol_Type::VARIABLE_UNDEFINED: return Syntax_Color::VARIABLE; 
    case Symbol_Type::GLOBAL: return Syntax_Color::VARIABLE; 
    case Symbol_Type::PARAMETER: return Syntax_Color::VARIABLE; 
    case Symbol_Type::PATTERN_VARIABLE: return Syntax_Color::VARIABLE; 
    case Symbol_Type::POLYMORPHIC_FUNCTION: return Syntax_Color::FUNCTION; 
    default: panic("");
    }
    return Syntax_Color::TEXT;
}

Syntax_Color symbol_to_color(Symbol* symbol, bool is_definition)
{
    int i = 0;
    switch (symbol->type)
    {
    case Symbol_Type::DATATYPE: {
        if (symbol->options.datatype->type == Datatype_Type::PRIMITIVE) {
            return Syntax_Color::PRIMITIVE;
        }
    }
    case Symbol_Type::VARIABLE:
    case Symbol_Type::VARIABLE_UNDEFINED:
    case Symbol_Type::GLOBAL:
    case Symbol_Type::PARAMETER:
    case Symbol_Type::COMPTIME_VALUE: 
    {
        if (is_definition) {
            return Syntax_Color::VALUE_DEFINITION;
        }
        break;
    }
    }
    return symbol_type_to_color(symbol->type);
}


