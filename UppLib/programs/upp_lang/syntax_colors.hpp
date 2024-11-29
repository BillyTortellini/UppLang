#pragma once
#include "../../math/vectors.hpp"

struct vec3;
struct vec4;

namespace Syntax_Color
{
    extern vec3 KEYWORD;
    extern vec3 COMMENT;
    extern vec3 FUNCTION;
    extern vec3 MODULE;
    extern vec3 IDENTIFIER_FALLBACK;
    extern vec3 TEXT;
    extern vec3 VARIABLE;
    extern vec3 TYPE;
    extern vec3 PRIMITIVE;
    extern vec3 STRING;
    extern vec3 LITERAL_NUMBER;
    extern vec3 SUBTYPE;
    extern vec3 MEMBER;
    extern vec3 ENUM_MEMBER;

    extern vec4 BG_NORMAL;
    extern vec4 BG_ERROR;
    extern vec3 BG_HIGHLIGHT;
};

enum class Symbol_Type;
vec3 symbol_type_to_color(Symbol_Type type);
