#pragma once
#include "../datastructures/string.hpp"

String characters_get_valid_identifier_characters();
String characters_get_non_identifier_non_whitespace();
String characters_get_whitespaces();
String characters_get_all_letters();


typedef bool (*char_test_fn)(char c, void* userdata);

bool char_is_digit(char c, void* _unused = nullptr);
int char_digit_value(char c);
bool char_is_whitespace(char c, void* unused = nullptr);
bool char_is_letter(char c, void* _unused = nullptr);
bool char_is_operator(char c, void* _unused = nullptr);
bool char_is_valid_identifier(char c, void* _unused = nullptr); // a-z, A-Z, _ and # (For some reason the last two also
char char_get_lowercase(char c); // Returns the same char if already lowercase or not uppercase

