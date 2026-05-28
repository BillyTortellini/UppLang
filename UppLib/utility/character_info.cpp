#include "character_info.hpp"

#include "../utility/utils.hpp"

bool char_is_whitespace(char c, void* unused) {
    return c == ' ' || c == '\n' || c == '\t';
}

bool char_is_digit(char c, void* unused) {
    return (c >= '0' && c <= '9');
}

int char_digit_value(char c) {
    if (char_is_digit(c)) return c - '0';
    else {
        return 0;
    }
}

bool char_is_letter(char c, void* unused) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

bool char_is_valid_identifier(char c, void* unused) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        (c == '_' || c == '#');
}

bool char_is_space_critical(char c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        (c == '_');
}

char char_get_lowercase(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - ('A' - 'a');
    }
    return c;
}

bool char_is_operator(char c, void* _unused) {
    return !(char_is_valid_identifier(c) || char_is_whitespace(c));
}

int char_get_hexadecimal_value(char c) // Returns -1 if not hexadecimal
{
    if (char_is_digit(c)) {
        return (c - '0');
    }
    switch (c)
    {
    case 'A': 
    case 'a': return 10;
    case 'B': 
    case 'b': return 11;
    case 'C': 
    case 'c': return 12;
    case 'D': 
    case 'd': return 13;
    case 'E': 
    case 'e': return 14;
    case 'F': 
    case 'f': return 15;
    default: return -1;
    }
    return -1;
}

Char_Group::Char_Group(Char_Group_Type type, bool is_inverted, const char* custom_set)
{
    this->type = type;
    this->custom_set = custom_set;
    this->is_inverted = is_inverted;
}

bool char_group_type_contains(char c, Char_Group_Type type, const char* custom_set)
{
    switch (type)
    {
    case Char_Group_Type::LETTER: {
        return 
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z');
    }
    case Char_Group_Type::DIGIT: {
        return c >= '0' && c <= '9';
    }
    case Char_Group_Type::PARENTHESIS: {
        return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
    }
    case Char_Group_Type::OPERATOR: 
    {
        switch (c)
        {
        case '!':
        case '?':
        case '\"':
        case '\'':
        case '\\':
        case '&':
        case '|':
        case '+':
        case '-':
        case '*':
        case '/':
        case '%':
        case '$':
        case '=':
        case '<':
        case '>':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '´':
        case '`':
        case '.':
        case ':':
        case ',':
        case ';':
        case '^':
        case '°':
        case '§':
        case '~':
        case '#':
            return true;
        }
        return false;
    }
    case Char_Group_Type::WHITESPACE: {
        return c == ' ' || c == '\t';
    }
    case Char_Group_Type::IDENTIFIER: {
        return
            char_group_type_contains(c, Char_Group_Type::LETTER) ||
            char_group_type_contains(c, Char_Group_Type::DIGIT) ||
            c == '_' ||
            c == '#';
    }
    case Char_Group_Type::SPACE_CRITICAL: {
        return
            char_group_type_contains(c, Char_Group_Type::LETTER) ||
            char_group_type_contains(c, Char_Group_Type::DIGIT) ||
            c == '_';
    }
    case Char_Group_Type::CUSTOM_SET: {
        if (custom_set == nullptr) return false;
        int index = 0;
        while (custom_set[index] != '\0') {
            if (c == custom_set[index]) return true;
            index += 1;
        }
        return false;
    }
    case Char_Group_Type::HEX_DIGIT: {
        return
            char_group_type_contains(c, Char_Group_Type::DIGIT) ||
            (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f');
    }
    default: panic("");
    }

    return false;
}

bool Char_Group::contains(char c)
{
    bool result = char_group_type_contains(c, type, custom_set);
    return is_inverted ? !result : result;
}

Char_Group Char_Group::invert() {
    return Char_Group(type, !is_inverted, custom_set);
}


