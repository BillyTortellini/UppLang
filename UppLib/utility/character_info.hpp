#pragma once

#include "../utility/datatypes.hpp"

int char_get_hexadecimal_value(char c); // Returns -1 if not hexadecimal
bool char_is_digit(char c, void* _unused = nullptr);
int char_digit_value(char c);
bool char_is_whitespace(char c, void* unused = nullptr);
bool char_is_letter(char c, void* _unused = nullptr);
bool char_is_valid_identifier(char c, void* _unused = nullptr); // a-z, A-Z, _ and # (For some reason the last two also
char char_get_lowercase(char c); // Returns the same char if already lowercase or not uppercase

enum class Char_Group_Type : u8
{
	// Basic groups
	LETTER,
	DIGIT,
	OPERATOR,
	WHITESPACE,
	PARENTHESIS,

	// Combined groups
	IDENTIFIER, // Letters + digits + _ and # (Not sure why hashtag)
	SPACE_CRITICAL, // Like identifier without #
	HEX_DIGIT,
	CUSTOM_SET
};

bool char_group_type_contains(char c, Char_Group_Type type, const char* custom_set = nullptr);

struct Char_Group
{
	const char* custom_set;
	Char_Group_Type type;
	bool is_inverted;

	Char_Group(Char_Group_Type type, bool is_inverted = false, const char* custom_set = "");
	bool contains(char c);
	Char_Group invert();
};

