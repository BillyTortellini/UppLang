#include "syntax_colors.hpp"
#include "symbol_table.hpp"

vec3 syntax_color_to_vec3(Syntax_Color color)
{
    switch (color)
    {
    case Syntax_Color::NONE: return vec3(0.0f);
    case Syntax_Color::SEPERATOR_LINE: return vec3(0.0f);

    case Syntax_Color::WHITE: return vec3(1.0f);
    case Syntax_Color::BLACK: return vec3(0.0f);
    case Syntax_Color::RED: return vec3(1, 0, 0);
    case Syntax_Color::GREEN: return vec3(0, 1, 0);
    case Syntax_Color::BLUE: return vec3(0, 0, 1);
    case Syntax_Color::YELLOW: return vec3(1, 1, 0);
    case Syntax_Color::MAGENTA: return vec3(1, 0, 1);
    case Syntax_Color::CYAN: return vec3(0, 1, 1);
    case Syntax_Color::PURPLE: return vec3(.65f, .4f, .8f);
    case Syntax_Color::ORANGE: return vec3(.85f, .65f, .0f);

    case Syntax_Color::TEXT:     return vec3(1.0f);
    case Syntax_Color::LITERAL_NUMBER: return vec3(.6f);
    case Syntax_Color::VARIABLE: return vec3(.7f, .7f, 1.0f);
    case Syntax_Color::DATATYPE: return vec3_color_from_code("#4874DB");//vec3_color_from_code("#28C6D9");
    case Syntax_Color::VALUE_DEFINITION: return vec3_color_from_code("#81D4FA");
    case Syntax_Color::PRIMITIVE: return vec3_color_from_code("#4874DB");
    case Syntax_Color::STRING: return vec3(.85f, .65f, .0f);
    case Syntax_Color::SUBTYPE: return vec3_color_from_code("#0489C9");
    case Syntax_Color::MEMBER: return vec3_color_from_code("#E6EE9C");
    case Syntax_Color::ENUM_MEMBER: return vec3_color_from_code("#0489C9");
    case Syntax_Color::KEYWORD: return vec3_color_from_code("#B867C5"); // Purple
    case Syntax_Color::COMMENT: return vec3(0.0f, 0.85f, 0.0f);
    case Syntax_Color::FUNCTION: return vec3_color_from_code("#D6B93A");
    case Syntax_Color::MODULE: return vec3_color_from_code("#388E3C");
    case Syntax_Color::INVALID_TOKEN: return vec3(1.0f, 0.8f, 0.8f);
    case Syntax_Color::BOOLEAN_LITERAL: return vec3(0.5f, 0.5f, 1.0f); 
    case Syntax_Color::FOLD_ERROR_BG: return vec3(0.75f, 0.15f, 0.15f);
    case Syntax_Color::FOLD_BG: return vec3(0.4f);
    case Syntax_Color::SEARCH_HIGHLIGHT_BG: return vec3(0.3f);
    case Syntax_Color::VISUAL_BLOCK_BG: return vec3(0.4f);
    case Syntax_Color::CURSOR_BG: return vec3(0.25f);
    case Syntax_Color::SUGGESTION_BG: return vec3(0.3f);
    case Syntax_Color::FILE_DIRECTORY: return vec3(0.1f, 0.1f, 0.9f);
    case Syntax_Color::ERROR_DISPLAY_TEXT: return vec3(1.0f, 0.5f, 0.5f);

    case Syntax_Color::BG_NORMAL: return vec3(0.0f);
    case Syntax_Color::BG_ERROR: return vec3(.7f, 0, 0);
    case Syntax_Color::BG_HIGHLIGHT: return vec3(.3f, .3f, .1f);

    case Syntax_Color::CONTEXT_BG: return vec3(0.2f);
    case Syntax_Color::CONTEXT_TEXT: return vec3(1.0f);
    case Syntax_Color::CONTEXT_ERROR_TEXT: return vec3(1.0f, 0.5f, 0.5f);
    case Syntax_Color::CONTEXT_BORDER: return vec3(0.5f, 0.0f, 1.0f);

    default: panic("");
    }
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


