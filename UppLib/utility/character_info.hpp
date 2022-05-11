#pragma once
#include "../datastructures/string.hpp"

String characters_get_valid_identifier_characters();
String characters_get_non_identifier_non_whitespace();
String characters_get_whitespaces();
String characters_get_all_letters();
bool char_is_digit(int c);
bool char_is_letter(int c);
bool char_is_valid_identifier(int c);

