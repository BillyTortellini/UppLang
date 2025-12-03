#include "syntax_colors.hpp"
#include "symbol_table.hpp"

namespace Colors
{
    const vec3 WHITE = vec3(1);
    const vec3 BLACK = vec3(0);
    const vec3 RED = vec3(1, 0, 0);
    const vec3 GREEN = vec3(0, 1, 0);
    const vec3 BLUE = vec3(0, 0, 1);
    const vec3 YELLOW = vec3(1, 1, 0);
    const vec3 MAGENTA = vec3(1, 0, 1);
    const vec3 CYAN = vec3(0, 1, 1);

    const vec3 PURPLE = vec3(0.65f, 0.4f, 0.8f);
    const vec3 ORANGE = vec3(0.85f, 0.65f, 0.0f);
}

namespace Syntax_Color
{
    // White/Grey/Light-Blue tones
    vec3 TEXT =                Colors::WHITE; // Not quite sure if this is ever used...
    vec3 LITERAL_NUMBER =      Colors::WHITE * 0.6f;
    vec3 VARIABLE =            vec3(0.7f, 0.7f, 1.0f);
    vec3 VALUE_DEFINITION =    vec3_color_from_code("#81D4FA");
    vec3 MEMBER =              vec3_color_from_code("#E6EE9C");

    // Hard accents
    vec3 KEYWORD =             vec3_color_from_code("#B867C5"); // Purple
    vec3 MODULE =              vec3_color_from_code("#388E3C");
    vec3 COMMENT =             vec3(0.0f, 0.85f, 0.0f);
    vec3 FUNCTION =            vec3_color_from_code("#D6B93A");
    vec3 STRING =              Colors::ORANGE;

    // Dark-Blue/Light blue for Types
    // vec3 PRIMITIVE =           vec3_color_from_code("#1764BB");
    // vec3 DATATYPE =                vec3_color_from_code("#1764BB");//vec3_color_from_code("#28C6D9");
    // vec3 SUBTYPE =             vec3_color_from_code("#616161");
    // vec3 ENUM_MEMBER = vec3(0 / 255.0f, 153 / 255.0f, 204 / 255.0f);

    vec3 PRIMITIVE   = vec3_color_from_code("#4874DB");
    vec3 DATATYPE        = vec3_color_from_code("#4874DB");//vec3_color_from_code("#28C6D9");
    vec3 SUBTYPE     = vec3_color_from_code("#0489C9");
    vec3 ENUM_MEMBER = vec3_color_from_code("#0489C9");

    // Other
    vec4 BG_NORMAL = vec4(0);
    vec4 BG_ERROR = vec4(0.7f, 0.0f, 0.0f, 1.0f);
    vec3 BG_HIGHLIGHT = vec3(1.0f, 1.0f, 0.3f) * 0.3f;
};

vec3 symbol_type_to_color(Symbol_Type type)
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

vec3 symbol_to_color(Symbol* symbol, bool is_definition)
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


