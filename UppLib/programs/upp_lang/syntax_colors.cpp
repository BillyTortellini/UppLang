#include "syntax_colors.hpp"
#include "dependency_analyser.hpp"

namespace Syntax_Color
{
    vec3 KEYWORD = vec3(0.65f, 0.4f, 0.8f);
    vec3 COMMENT = vec3(0.0f, 1.0f, 0.0f);
    vec3 FUNCTION = vec3(0.7f, 0.7f, 0.4f);
    vec3 MODULE = vec3(0.3f, 0.6f, 0.7f);
    vec3 IDENTIFIER_FALLBACK = vec3(0.7f, 0.7f, 1.0f);
    vec3 TEXT = vec3(1.0f);
    vec3 VARIABLE = vec3(0.5f, 0.5f, 0.8f);
    vec3 TYPE = vec3(0.4f, 0.9f, 0.9f);
    vec3 PRIMITIVE = vec3(0.1f, 0.3f, 1.0f);
    vec3 STRING = vec3(0.85f, 0.65f, 0.0f);
    vec3 LITERAL_NUMBER = vec3(0.6f);

    vec4 BG_NORMAL = vec4(0);
    vec4 BG_ERROR = vec4(0.7f, 0.0f, 0.0f, 1.0f);
    vec4 BG_HIGHLIGHT = vec4(0.3f, 0.3f, 0.3f, 1.0f);
};

vec3 symbol_type_to_color(Symbol_Type type)
{
    switch (type)
    {
    case Symbol_Type::HARDCODED_FUNCTION: return Syntax_Color::FUNCTION; 
    case Symbol_Type::EXTERN_FUNCTION: return Syntax_Color::FUNCTION; 
    case Symbol_Type::FUNCTION: return Syntax_Color::FUNCTION; 
    case Symbol_Type::MODULE: return Syntax_Color::MODULE; 
    case Symbol_Type::TYPE: return Syntax_Color::TYPE; 
    case Symbol_Type::VARIABLE: return Syntax_Color::VARIABLE; 
    case Symbol_Type::COMPTIME_VALUE: return Syntax_Color::VARIABLE; 
    case Symbol_Type::SYMBOL_ALIAS: return Syntax_Color::IDENTIFIER_FALLBACK; 
    case Symbol_Type::UNRESOLVED: return Syntax_Color::IDENTIFIER_FALLBACK; 
    case Symbol_Type::ERROR_SYMBOL: return Syntax_Color::IDENTIFIER_FALLBACK; 
    case Symbol_Type::VARIABLE_UNDEFINED: return Syntax_Color::VARIABLE; 
    case Symbol_Type::GLOBAL: return Syntax_Color::VARIABLE; 
    case Symbol_Type::PARAMETER: return Syntax_Color::VARIABLE; 
    default: panic("");
    }
    return Syntax_Color::IDENTIFIER_FALLBACK;
}
