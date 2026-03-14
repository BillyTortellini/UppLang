#include "character_info.hpp"

String characters_get_valid_identifier_characters() {
    return string_create_static("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890_");
}

String characters_get_non_identifier_non_whitespace() {
    return string_create_static("!\"§$%&/()[]{}<>|=\\?´`+*~#'-.:,;^°");
}

String characters_get_whitespaces() {
    return string_create_static("\n \t");
}

String characters_get_all_letters() {
    return string_create_static("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

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

char char_get_lowercase(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - ('A' - 'a');
    }
    return c;
}

bool char_is_operator(char c, void* _unused) {
    return !(char_is_valid_identifier(c) || char_is_whitespace(c));
}

bool char_is_parenthesis(char c) {
    return string_contains_character(string_create_static("[]{}()"), c);
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

bool char_is_space_critical(char c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        (c == '_');
}
