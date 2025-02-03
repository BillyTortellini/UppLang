#include "c_lexer.hpp"

#include "../../utility/hash_functions.hpp"
#include "../upp_lang/compiler_misc.hpp"

Text_Position text_position_make(int line_index, int character)
{
    Text_Position pos;
    pos.line_index = line_index;
    pos.character = character;
    return pos;
}

Text_Slice text_slice_make(Text_Position start, Text_Position end)
{
    Text_Slice slice;
    slice.start = start;
    slice.end = end;
    return slice;
}

bool token_type_is_keyword(C_Token_Type type)
{
    switch (type)
    {
    case C_Token_Type::IF: return true;
    case C_Token_Type::MODULE: return true;
    case C_Token_Type::ELSE: return true;
    case C_Token_Type::FOR: return true;
    case C_Token_Type::WHILE: return true;
    case C_Token_Type::SWITCH: return true;
    case C_Token_Type::CONTINUE: return true;
    case C_Token_Type::DEFAULT: return true;
    case C_Token_Type::CASE: return true;
    case C_Token_Type::BREAK: return true;
    case C_Token_Type::RETURN: return true;
    case C_Token_Type::STRUCT: return true;
    case C_Token_Type::UNION: return true;
    case C_Token_Type::C_UNION: return true;
    case C_Token_Type::ENUM: return true;
    case C_Token_Type::NEW: return true;
    case C_Token_Type::DEFER: return true;
    case C_Token_Type::DELETE_TOKEN: return true;
    case C_Token_Type::BOOLEAN_LITERAL: return true;
    case C_Token_Type::CAST: return true;
    case C_Token_Type::CAST_RAW: return true;
    case C_Token_Type::CAST_PTR: return true;
    case C_Token_Type::EXTERN: return true;
    }
    return false;
}

const char* token_type_to_string(C_Token_Type type)
{
    switch (type)
    {
    case C_Token_Type::IF: return "IF";
    case C_Token_Type::ELSE: return "ELSE";
    case C_Token_Type::FOR: return "FOR";
    case C_Token_Type::WHILE: return "WHILE";
    case C_Token_Type::SWITCH: return "SWITCH";
    case C_Token_Type::CONTINUE: return "CONTINUE";
    case C_Token_Type::DEFAULT: return "DEFAULT";
    case C_Token_Type::CASE: return "CASE";
    case C_Token_Type::EXTERN: return "EXTERN";
    case C_Token_Type::MODULE: return "MODULE";
    case C_Token_Type::STRUCT: return "STRUCT";
    case C_Token_Type::UNION: return "UNION";
    case C_Token_Type::C_UNION: return "C_UNION";
    case C_Token_Type::ENUM: return "ENUM";
    case C_Token_Type::BREAK: return "BREAK";
    case C_Token_Type::DOT: return "DOT";
    case C_Token_Type::NEW: return "NEW";
    case C_Token_Type::DELETE_TOKEN: return "DELETE";
    case C_Token_Type::NULLPTR: return "NULL";
    case C_Token_Type::DEFER: return "DEFER";
    case C_Token_Type::COLON: return "COLON";
    case C_Token_Type::COMMA: return "COMMA";
    case C_Token_Type::DOUBLE_COLON: return "DOUBLE_COLON";
    case C_Token_Type::INFER_ASSIGN: return "INFER_ASSIGN";
    case C_Token_Type::ARROW: return "ARROW";
    case C_Token_Type::SEMICOLON: return "SEMICOLON";
    case C_Token_Type::OPEN_PARENTHESIS: return "OPEN_BRACKET";
    case C_Token_Type::CLOSED_PARENTHESIS: return "CLOSED_BRACKET";
    case C_Token_Type::OPEN_BRACES: return "OPEN_CURLY_BRACKET";
    case C_Token_Type::CLOSED_BRACES: return "CLOSED_CURLY_BRACKET";
    case C_Token_Type::OPEN_BRACKETS: return "OPEN_SQUARE_BRACKET";
    case C_Token_Type::CLOSED_BRACKETS: return "CLOSED_SQUARE_BRACKET";
    case C_Token_Type::HASHTAG: return "#";
    case C_Token_Type::TILDE: return "~";
    case C_Token_Type::DOLLAR: return "$";
    case C_Token_Type::OP_ASSIGNMENT: return "OP_ASSIGNMENT";
    case C_Token_Type::OP_PLUS: return "OP_PLUS";
    case C_Token_Type::OP_MINUS: return "OP_MINUS";
    case C_Token_Type::OP_SLASH: return "OP_SLASH";
    case C_Token_Type::OP_STAR: return "OP_STAR";
    case C_Token_Type::OP_PERCENT: return "OP_PERCENT";
    case C_Token_Type::COMPARISON_LESS: return "COMPARISON_LESS";
    case C_Token_Type::COMPARISON_LESS_EQUAL: return "COMPARISON_LESS_EQUAL";
    case C_Token_Type::COMPARISON_GREATER: return "COMPARISON_GREATER";
    case C_Token_Type::COMPARISON_GREATER_EQUAL: return "COMPARISON_GREATER_EQUAL";
    case C_Token_Type::COMPARISON_EQUAL: return "COMPARISON_EQUAL";
    case C_Token_Type::COMPARISON_NOT_EQUAL: return "COMPARISON_NOT_EQUAL";
    case C_Token_Type::COMPARISON_POINTER_EQUAL: return "COMPARISON_POINTER_EQUAL";
    case C_Token_Type::COMPARISON_POINTER_NOT_EQUAL: return "COMPARISON_POINTER_NOT_EQUAL";
    case C_Token_Type::LOGICAL_AND: return "LOGICAL_AND";
    case C_Token_Type::LOGICAL_OR: return "LOGICAL_OR";
    case C_Token_Type::LOGICAL_BITWISE_AND: return "LOGICAL_BITWISE_AND";
    case C_Token_Type::LOGICAL_BITWISE_OR: return "LOGICAL_BITWISE_OR";
    case C_Token_Type::LOGICAL_NOT: return "LOGICAL_NOT";
    case C_Token_Type::INTEGER_LITERAL: return "INT_LITERAL";
    case C_Token_Type::FLOAT_LITERAL: return "FLOAT_LITERAL";
    case C_Token_Type::BOOLEAN_LITERAL: return "BOOLEAN_LITERAL";
    case C_Token_Type::STRING_LITERAL: return "STRING_LITERAL";
    case C_Token_Type::IDENTIFIER_NAME: return "IDENTIFIER";
    case C_Token_Type::ERROR_TOKEN: return "ERROR_TOKE";
    case C_Token_Type::COMMENT: return "COMMENT";
    case C_Token_Type::WHITESPACE: return "WHITESPACE";
    case C_Token_Type::NEW_LINE: return "NEWLINE";
    case C_Token_Type::RETURN: return "RETURN";
    case C_Token_Type::CAST: return "CAST";
    case C_Token_Type::CAST_RAW: return "CAST_RAW";
    case C_Token_Type::CAST_PTR: return "CAST_PTR";
    }
    panic("Should not happen!");
    return "TOKEN_NOT_KNOWN";
}

Token_Attribute token_attribute_make_empty() {
    Token_Attribute result;
    result.integer_value = 67676767;
    return result;
}

C_Token token_make(C_Token_Type type, Token_Attribute attribute, Text_Slice position, int index)
{
    C_Token result;
    result.type = type;
    result.attribute = attribute;
    result.position = position;
    result.source_code_index = index;
    return result;
}

C_Token token_make(C_Token_Type type, Token_Attribute attribute, int line_index, int character, int length, int index)
{
    C_Token result;
    result.type = type;
    result.attribute = attribute;
    result.position = text_slice_make(text_position_make(line_index, character), text_position_make(line_index, character+length));
    result.source_code_index = index;
    return result;
}

C_Token token_make_with_slice(C_Token_Type type, Token_Attribute attribute, Text_Slice slice, int length, int index)
{
    C_Token result;
    result.type = type;
    result.attribute = attribute;
    result.position = slice;
    result.source_code_index = index;
    return result;
}

bool code_parse_comments(C_Lexer* lexer, String* code, int* index, int* character_pos, int* line_number)
{
    if (*index + 1 >= code->size) return false;
    // Single line_index comments
    int start_index = *index;
    int start_char = *character_pos;
    int line_start = *line_number;
    if (code->characters[*index] == '/' && code->characters[*index + 1] == '/') 
    {
        while (*index < code->size && code->characters[*index] != '\n') {
            *index = *index + 1;
            *character_pos = *character_pos + 1;
        }
        *index = *index + 1;
        *character_pos = 0;
        *line_number = *line_number + 1;
        dynamic_array_push_back(&lexer->tokens, token_make(
            C_Token_Type::COMMENT, 
            token_attribute_make_empty(), 
            text_slice_make(text_position_make(line_start, start_char), text_position_make(*line_number, 0)),
            start_index)
        );
        return true;
    }

    if (code->characters[*index] == '/' && code->characters[*index + 1] == '*') 
    {
        *character_pos = *character_pos + 2;
        *index = *index + 2;
        int comment_depth = 1;
        while (*index + 1 < code->size) 
        {
            char current = code->characters[*index];
            char next = code->characters[*index+1];
            if (current == '/' && next == '*') {
                comment_depth++;
                *index = *index + 2;
                *character_pos = *character_pos + 2;
                continue;
            }
            if (current == '*' && next == '/') {
                comment_depth--;
                *index = *index + 2;
                *character_pos = *character_pos + 2;
                if (comment_depth == 0) break;
                continue;
            }

            *index = *index + 1;
            if (current == '\n') {
                *character_pos = 0;
                *line_number = *line_number + 1;
            }
            else {
                *character_pos = *character_pos + 1;
            }
        }
        // To prevent errors when the last comment isnt closed
        if (comment_depth != 0 && *index == code->size-1) 
        {
            if (code->characters[*index] == '\n') {
                *line_number = *line_number + 1;
                *character_pos = 0;
            }
            else {
                *character_pos = *character_pos + 1;
            }
            *index = *index + 1;
        }
        dynamic_array_push_back(&lexer->tokens, token_make(
            C_Token_Type::COMMENT, 
            token_attribute_make_empty(), 
            text_slice_make(text_position_make(line_start, start_char), text_position_make(*line_number, *character_pos)),
            start_index)
        );
        return true;
    }
    return false;
}

bool code_parse_newline(C_Lexer* lexer, String* code, int* index, int* character_pos, int* line_number)
{
    int i = *index;
    if (i < code->size && code->characters[i] == '\n') 
    {
        dynamic_array_push_back(&lexer->tokens, token_make(
            C_Token_Type::NEW_LINE, 
            token_attribute_make_empty(), 
            text_slice_make(text_position_make(*line_number, *character_pos), text_position_make(*line_number+1, 0)),
            *index)
        );
        *index = *index + 1;
        *character_pos = 0;
        *line_number = *line_number + 1;
        return true;
    }
    return false;
}

bool code_parse_whitespace(C_Lexer* lexer, String* code, int* index, int* character_pos, int* line_number)
{
    int start = *index;
    int char_start = *character_pos;
    bool changed = false;
    while (*index < code->size && string_contains_character(string_create_static("\t \r"), code->characters[*index]))
    {
        (*character_pos)++;
        *index = *index + 1;
        changed = true;
    }
    if (changed) {
        dynamic_array_push_back(&lexer->tokens, token_make(
            C_Token_Type::WHITESPACE, token_attribute_make_empty(), *line_number, char_start, *character_pos - char_start, start)
        );
        return true;
    }
    return false;
}

void code_skip_whitespace_and_comments(C_Lexer* lexer, String* code, int* index, int* character_pos, int* line_number)
{
    while (true)
    {
        if (code_parse_comments(lexer, code, index, character_pos, line_number)) continue;
        if (code_parse_newline(lexer, code, index, character_pos, line_number)) continue;
        if (code_parse_whitespace(lexer, code, index, character_pos, line_number)) continue;
        break;
    }
}

bool character_is_digit(int c) {
    return (c >= '0' && c <= '9');
}

bool character_is_letter(int c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

C_Lexer c_lexer_create()
{
    C_Lexer lexer;
    lexer.tokens = dynamic_array_create<C_Token>(1024);
    lexer.tokens_with_decoration = dynamic_array_create<C_Token>(1024);

    lexer.keywords = hashtable_create_empty<String, C_Token_Type>(64, &hash_string, &string_equals);
    hashtable_insert_element(&lexer.keywords, string_create_static("if"), C_Token_Type::IF);
    hashtable_insert_element(&lexer.keywords, string_create_static("else"), C_Token_Type::ELSE);
    hashtable_insert_element(&lexer.keywords, string_create_static("for"), C_Token_Type::FOR);
    hashtable_insert_element(&lexer.keywords, string_create_static("while"), C_Token_Type::WHILE);
    hashtable_insert_element(&lexer.keywords, string_create_static("switch"), C_Token_Type::SWITCH);
    hashtable_insert_element(&lexer.keywords, string_create_static("continue"), C_Token_Type::CONTINUE);
    hashtable_insert_element(&lexer.keywords, string_create_static("default"), C_Token_Type::DEFAULT);
    hashtable_insert_element(&lexer.keywords, string_create_static("case"), C_Token_Type::CASE);
    hashtable_insert_element(&lexer.keywords, string_create_static("break"), C_Token_Type::BREAK);
    hashtable_insert_element(&lexer.keywords, string_create_static("return"), C_Token_Type::RETURN);
    hashtable_insert_element(&lexer.keywords, string_create_static("struct"), C_Token_Type::STRUCT);
    hashtable_insert_element(&lexer.keywords, string_create_static("union"), C_Token_Type::UNION);
    hashtable_insert_element(&lexer.keywords, string_create_static("c_union"), C_Token_Type::C_UNION);
    hashtable_insert_element(&lexer.keywords, string_create_static("cast"), C_Token_Type::CAST);
    hashtable_insert_element(&lexer.keywords, string_create_static("cast_raw"), C_Token_Type::CAST_RAW);
    hashtable_insert_element(&lexer.keywords, string_create_static("cast_ptr"), C_Token_Type::CAST_PTR);
    hashtable_insert_element(&lexer.keywords, string_create_static("null"), C_Token_Type::NULLPTR);
    hashtable_insert_element(&lexer.keywords, string_create_static("new"), C_Token_Type::NEW);
    hashtable_insert_element(&lexer.keywords, string_create_static("delete"), C_Token_Type::DELETE_TOKEN);
    hashtable_insert_element(&lexer.keywords, string_create_static("true"), C_Token_Type::BOOLEAN_LITERAL);
    hashtable_insert_element(&lexer.keywords, string_create_static("false"), C_Token_Type::BOOLEAN_LITERAL);
    hashtable_insert_element(&lexer.keywords, string_create_static("defer"), C_Token_Type::DEFER);
    hashtable_insert_element(&lexer.keywords, string_create_static("module"), C_Token_Type::MODULE);
    hashtable_insert_element(&lexer.keywords, string_create_static("extern"), C_Token_Type::EXTERN);
    hashtable_insert_element(&lexer.keywords, string_create_static("enum"), C_Token_Type::ENUM);

    return lexer;
}

enum class Number_Base
{
    DECIMAL = 10,
    OCTAL = 8,
    HEXADECIMAL = 16
};

int number_base_character_to_value(Number_Base base, int character)
{
    if (base == Number_Base::DECIMAL || base == Number_Base::OCTAL) {
        return character - '0';
    }
    else if (base == Number_Base::HEXADECIMAL) 
    {
        if (character >= '0' && character <= '9') {
            return character - '0';
        }
        else if (character >= 'a' && character <= 'f') {
            return character - 'a';

        }
        else if (character >= 'A' && character <= 'F') {
            return character - 'A';
        }
    }
    panic("Invalid base/character");
    return 0;
}

bool number_base_is_valid_character(Number_Base base, int character)
{
    if (base == Number_Base::DECIMAL) {
        return character >= '0' && character <= '9';
    }
    else if (base == Number_Base::OCTAL)
    {
        return character >= '0' && character <= '7';
    }
    else if (base == Number_Base::HEXADECIMAL) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
    }
    panic("Invalid base");
    return false;
}

void c_lexer_lex(C_Lexer* lexer, String* code, Identifier_Pool* identifier_pool)
{
    lexer->identifier_pool = identifier_pool;
    String identifier_string = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&identifier_string));

    dynamic_array_reset(&lexer->tokens);
    dynamic_array_reset(&lexer->tokens_with_decoration);

    int index = 0;
    int character_pos = 0;
    int line_number = 0;
    bool has_errors = false;
    while (index < code->size)
    {
        // Advance index
        code_skip_whitespace_and_comments(lexer, code, &index, &character_pos, &line_number);
        if (index >= code->size) {
            break;
        }

        // Get current character
        int current_character = code->characters[index];
        int next_character = -1;
        if (index + 1 < code->size) {
            next_character = code->characters[index + 1];
        }

        switch (current_character)
        {
            // Check for single symbols
        case '\f': {
            character_pos++;
            index++;
            continue;
        }
        case '.':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::DOT, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '$':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::DOLLAR, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '~':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::TILDE, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '#':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::HASHTAG, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ';':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::SEMICOLON, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ',':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::COMMA, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '(':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::OPEN_PARENTHESIS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ')':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::CLOSED_PARENTHESIS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '{':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::OPEN_BRACES, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '}':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::CLOSED_BRACES, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '[':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::OPEN_BRACKETS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ']':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::CLOSED_BRACKETS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '+':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::OP_PLUS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '*': {
            if (index + 2 < code->size) 
            {
                if (code->characters[index + 1] == '=' && code->characters[index + 2] == '=') {
                    dynamic_array_push_back(
                        &lexer->tokens, token_make(C_Token_Type::COMPARISON_POINTER_EQUAL, token_attribute_make_empty(), line_number, character_pos, 3, index)
                    );
                    character_pos += 3;
                    index += 3;
                    continue;
                }
                else if (code->characters[index + 1] == '!' && code->characters[index + 2] == '=') {
                    dynamic_array_push_back(
                        &lexer->tokens, token_make(C_Token_Type::COMPARISON_POINTER_NOT_EQUAL, token_attribute_make_empty(), line_number, character_pos, 3, index)
                    );
                    character_pos += 3;
                    index += 3;
                    continue;
                }
            }
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::OP_STAR, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        }
        case '/':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::OP_SLASH, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '%':
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::OP_PERCENT, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '=':
            // Check for ambiguities between one and two characters (< and <=, = and ==, ! and !=, ...)
            if (next_character == '=') {
                dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::COMPARISON_EQUAL, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::OP_ASSIGNMENT, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '-':
            if (next_character == '>') {
                dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::ARROW, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::OP_MINUS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '<':
            if (next_character == '=') {
                dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::COMPARISON_LESS_EQUAL, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::COMPARISON_LESS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '>':
            if (next_character == '=') {
                dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::COMPARISON_GREATER_EQUAL, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::COMPARISON_GREATER, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '!':
            if (next_character == '=') {
                dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::COMPARISON_NOT_EQUAL, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::LOGICAL_NOT, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '&':
            if (next_character == '&') {
                dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::LOGICAL_AND, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::LOGICAL_BITWISE_AND, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '|':
            if (next_character == '|') {
                dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::LOGICAL_OR, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::LOGICAL_BITWISE_OR, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ':':
            if (next_character == ':') {
                dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::DOUBLE_COLON, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            else if (next_character == '=') {
                dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::INFER_ASSIGN, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::COLON, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '\"':
        {
            // Parse String literal
            Text_Position start_pos = text_position_make(line_number, character_pos);
            int string_literal_start_index = index;
            bool last_was_escape = false;
            index++;
            character_pos++;

            bool invalid_escape_found = false;
            bool terminated_successfull = false;
            string_reset(&identifier_string);
            while (index < code->size)
            {
                if (last_was_escape)
                {
                    switch (code->characters[index])
                    {
                    case 'n':
                        string_append_character(&identifier_string, '\n');
                        break;
                    case 'r':
                        string_append_character(&identifier_string, '\r');
                        break;
                    case 't':
                        string_append_character(&identifier_string, '\t');
                        break;
                    case '\\':
                        string_append_character(&identifier_string, '\\');
                        break;
                    case '\'':
                        string_append_character(&identifier_string, '\'');
                        break;
                    case '\"':
                        string_append_character(&identifier_string, '\"');
                        break;
                    case '\n':
                        break;
                    default:
                        invalid_escape_found = true;
                        break;
                    }
                    last_was_escape = false;
                }
                else
                {
                    if (code->characters[index] == '\"') {
                        index++;
                        character_pos++;
                        terminated_successfull = true;
                        break;
                    }
                    else if (code->characters[index] == '\n') {
                        terminated_successfull = false;
                        break;
                    }
                    last_was_escape = code->characters[index] == '\\';
                    if (!last_was_escape) {
                        string_append_character(&identifier_string, code->characters[index]);
                    }
                }
                if (code->characters[index] == '\n') {
                    line_number++;
                    character_pos = 0;
                }
                else {
                    character_pos++;
                }
                index++;
            }

            Text_Slice token_slice = text_slice_make(start_pos, text_position_make(line_number, character_pos));
            if (!terminated_successfull || invalid_escape_found)
            {
                has_errors = true;
                dynamic_array_push_back(&lexer->tokens,
                    token_make_with_slice(C_Token_Type::ERROR_TOKEN, token_attribute_make_empty(), token_slice,
                        index - string_literal_start_index, string_literal_start_index)
                );
                continue;
            }

            // Add Token
            Token_Attribute attribute;
            attribute.id = identifier_pool_add(lexer->identifier_pool, identifier_string);
            dynamic_array_push_back(&lexer->tokens, token_make_with_slice(C_Token_Type::STRING_LITERAL, attribute,
                token_slice, index - string_literal_start_index, string_literal_start_index));
            continue;
        }
        }

        // Constants, Identifier and Keywords
        // Parse Numbers
        Number_Base base = Number_Base::DECIMAL;
        if (current_character == '0')
        {
            if (number_base_is_valid_character(Number_Base::OCTAL, next_character))
            {
                index += 1;
                character_pos += 1;
                base = Number_Base::OCTAL;
            }
            else if (next_character == 'x' || next_character == 'X')
            {
                base = Number_Base::HEXADECIMAL;
                index += 2;
                character_pos += 2;
            }
            if (index < code->size) {
                current_character = code->characters[index];
            }
            else {
                break;
            }
            next_character = -1;
            if (index + 1 < code->size) {
                next_character = code->characters[index + 1];
            }
        }
        if (number_base_is_valid_character(base, current_character))
        {
            int pre_comma_start_index = index;
            int pre_comma_end_index = index;
            // Parse number characters
            while (pre_comma_end_index < code->size && number_base_is_valid_character(base, code->characters[pre_comma_end_index])) {
                pre_comma_end_index++;
            }
            pre_comma_end_index--;

            bool comma_exists = false;
            int post_comma_start_index, post_comma_end_index;
            if (pre_comma_end_index + 1 < code->size && code->characters[pre_comma_end_index + 1] == '.')
            {
                comma_exists = true;
                post_comma_start_index = pre_comma_end_index + 2;
                if (pre_comma_end_index + 2 >= code->size) {
                    post_comma_end_index = -1;
                    post_comma_start_index = pre_comma_end_index + 1;
                }
                else {
                    post_comma_end_index = post_comma_start_index;
                    while (post_comma_end_index < code->size &&
                        number_base_is_valid_character(base, code->characters[post_comma_end_index])) {
                        post_comma_end_index++;
                    }
                    post_comma_end_index--;
                }
            }

            int int_value = 0;
            int base_value = (int)base;
            for (int i = pre_comma_start_index; i <= pre_comma_end_index; i++) {
                int num_value = number_base_character_to_value(base, code->characters[i]);
                int_value = (int_value * base_value) + num_value;
            }
            if (comma_exists)
            {
                // Calculate float value
                float fractional_value = 0.0f;
                float multiplier = 1.0f / base_value;
                for (int i = post_comma_start_index; i <= post_comma_end_index; i++) {
                    int num_value = number_base_character_to_value(base, code->characters[i]);
                    fractional_value += num_value * multiplier;
                    multiplier *= 0.1f;
                }
                float float_value = int_value + fractional_value;

                // Add float token
                Token_Attribute attribute;
                attribute.float_value = float_value;
                int character_length;
                if (post_comma_end_index == -1) {
                    character_length = pre_comma_end_index - pre_comma_start_index + 2;
                }
                else {
                    character_length = post_comma_end_index - pre_comma_start_index + 1;
                }
                dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::FLOAT_LITERAL, attribute, line_number, character_pos, character_length, index));
                index += character_length;
                character_pos += character_length;
                continue;
            }
            else {
                // Add integer Token
                Token_Attribute attribute;
                attribute.integer_value = int_value;
                int character_length = pre_comma_end_index - pre_comma_start_index + 1;
                dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::INTEGER_LITERAL, attribute, line_number, character_pos, character_length, index));
                index += character_length;
                character_pos += character_length;
                continue;
            }
        }

        // Identifiers, keywords or error
        if (!(character_is_letter(code->characters[index]) || character_is_digit(code->characters[index]) || code->characters[index] == '_'))
        {
            // Error, parse till next Delimiter
            int error_end_index = index;
            while (error_end_index < code->size && !(
                code->characters[error_end_index] == ';' ||
                code->characters[error_end_index] == ',' ||
                code->characters[error_end_index] == '.' ||
                code->characters[error_end_index] == '(' ||
                code->characters[error_end_index] == ')' ||
                code->characters[error_end_index] == '{' ||
                code->characters[error_end_index] == '}' ||
                code->characters[error_end_index] == '[' ||
                code->characters[error_end_index] == ']' ||
                code->characters[error_end_index] == '=' ||
                code->characters[error_end_index] == '+' ||
                code->characters[error_end_index] == '*' ||
                code->characters[error_end_index] == '%' ||
                code->characters[error_end_index] == '-' ||
                code->characters[error_end_index] == '/' ||
                code->characters[error_end_index] == '\n' ||
                code->characters[error_end_index] == ' ' ||
                code->characters[error_end_index] == '\r' ||
                code->characters[error_end_index] == '\t' ||
                code->characters[error_end_index] == '!'
                )) {
                error_end_index++;
            }
            has_errors = true;
            error_end_index--;
            int error_length = error_end_index - index + 1;
            dynamic_array_push_back(&lexer->tokens, token_make(C_Token_Type::ERROR_TOKEN, token_attribute_make_empty(), line_number, character_pos, error_length, index));
            index += error_length;
            character_pos += error_length;
            continue;
        }

        // Parse identifier/keyword
        {
            int identifier_end_index = index;
            while (identifier_end_index < code->size && (
                character_is_letter(code->characters[identifier_end_index]) ||
                character_is_digit(code->characters[identifier_end_index]) ||
                code->characters[identifier_end_index] == '_'
                )) {
                identifier_end_index++;
            }
            identifier_end_index--;

            int identifier_string_length = identifier_end_index - index + 1;
            string_reserve(&identifier_string, identifier_string_length);
            memory_copy(identifier_string.characters, code->characters + index, identifier_string_length + 1);
            identifier_string.size = identifier_string_length;
            identifier_string.characters[identifier_string.size] = 0;

            // Check if identifier is a keyword
            C_Token_Type* keyword_type = hashtable_find_element(&lexer->keywords, identifier_string);
            if (keyword_type != nullptr) {
                Token_Attribute attrib = token_attribute_make_empty();
                if (*keyword_type == C_Token_Type::BOOLEAN_LITERAL) {
                    if (string_equals(&identifier_string, &string_create_static("true"))) {
                        attrib.bool_value = 1;
                    }
                    else {
                        attrib.bool_value = 0;
                    }
                }
                dynamic_array_push_back(&lexer->tokens,
                    token_make(*keyword_type, attrib, line_number, character_pos, identifier_string_length, index));
            }
            else {
                Token_Attribute attribute;
                attribute.id = identifier_pool_add(lexer->identifier_pool, identifier_string);
                dynamic_array_push_back(&lexer->tokens,
                    token_make(C_Token_Type::IDENTIFIER_NAME, attribute, line_number, character_pos, identifier_string_length, index));
            }
            index += identifier_string_length;
            character_pos += identifier_string_length;
        }
    }

    // Make tokens with non_whitespaces
    for (int i = 0; i < lexer->tokens.size; i++) {
        if (lexer->tokens[i].type == C_Token_Type::WHITESPACE ||
            lexer->tokens[i].type == C_Token_Type::NEW_LINE ||
            lexer->tokens[i].type == C_Token_Type::COMMENT) {
            continue;
        }
        dynamic_array_push_back(&lexer->tokens_with_decoration, lexer->tokens[i]);
    }
    Dynamic_Array<C_Token> swap = lexer->tokens_with_decoration;
    lexer->tokens_with_decoration = lexer->tokens;
    lexer->tokens = swap;
}

void c_lexer_destroy(C_Lexer* lexer)
{
    dynamic_array_destroy(&lexer->tokens);
    dynamic_array_destroy(&lexer->tokens_with_decoration);
    hashtable_destroy(&lexer->keywords);
}

void c_lexer_print(C_Lexer* lexer)
{
    String msg = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&msg));
    string_append_formated(&msg, "Tokens: \n");
    for (int i = 0; i < lexer->tokens_with_decoration.size; i++)
    {
        C_Token token = lexer->tokens_with_decoration[i];
        string_append_formated(&msg, "\t %s (Line %d, Pos %d, size: %d)",
            token_type_to_string(token.type), token.position.start.line_index, token.position.start.character,
            math_maximum(token.position.end.character - token.position.start.character, 0));
        if (token.type == C_Token_Type::IDENTIFIER_NAME) {
            string_append_formated(&msg, " = %s", token.attribute.id->characters);
        }
        else if (token.type == C_Token_Type::INTEGER_LITERAL) {
            string_append_formated(&msg, " = %d", token.attribute.integer_value);
        }
        string_append_formated(&msg, "\n");
    }
    logg("\n%s\n", msg.characters);
}
