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
    vec3 TEXT =                Colors::WHITE; // Not quite sure if this is ever used...
    vec3 IDENTIFIER_FALLBACK = vec3(0.7f, 0.7f, 1.0f); // For identifiers where type cannot be displayed
    vec3 STRING =              Colors::ORANGE;
    vec3 LITERAL_NUMBER =      Colors::WHITE * 0.6f;
    vec3 KEYWORD =             Colors::PURPLE;
    vec3 COMMENT =             Colors::GREEN;
    vec3 FUNCTION =            vec3(0.7f, 0.7f, 0.4f);
    vec3 MODULE =              vec3(0.3f, 0.6f, 0.7f);
    vec3 VARIABLE =            vec3(0.5f, 0.5f, 0.8f);
    vec3 PRIMITIVE =           vec3(0.1f, 0.3f, 1.0f);
    vec3 TYPE =                vec3(0.4f, 0.9f, 0.9f);
    vec3 SUBTYPE =             vec3(102 / 255.0f, 153 / 255.0f, 153/ 255.0f);
    vec3 MEMBER =              vec3(0.7f, 0.7f, 1.0f);
    vec3 ENUM_MEMBER = vec3(0 / 255.0f, 153 / 255.0f, 204 / 255.0f);

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
    case Symbol_Type::TYPE: return Syntax_Color::TYPE; 
    case Symbol_Type::VARIABLE: return Syntax_Color::VARIABLE; 
    case Symbol_Type::COMPTIME_VALUE: return Syntax_Color::VARIABLE; 
    case Symbol_Type::DEFINITION_UNFINISHED: return Syntax_Color::IDENTIFIER_FALLBACK; 
    case Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL: return Syntax_Color::IDENTIFIER_FALLBACK; 
    case Symbol_Type::ERROR_SYMBOL: return Syntax_Color::IDENTIFIER_FALLBACK; 
    case Symbol_Type::VARIABLE_UNDEFINED: return Syntax_Color::VARIABLE; 
    case Symbol_Type::GLOBAL: return Syntax_Color::VARIABLE; 
    case Symbol_Type::PARAMETER: return Syntax_Color::VARIABLE; 
    case Symbol_Type::POLYMORPHIC_VALUE: return Syntax_Color::TYPE; 
    case Symbol_Type::POLYMORPHIC_FUNCTION: return Syntax_Color::FUNCTION; 
    default: panic("");
    }
    return Syntax_Color::IDENTIFIER_FALLBACK;
}
