#include "compiler.hpp"

#include <cstring>

bool token_type_is_keyword(Token_Type::ENUM type)
{
    switch (type)
    {
    case Token_Type::IF: return true;
    case Token_Type::ELSE: return true;
    case Token_Type::FOR: return true;
    case Token_Type::WHILE: return true;
    case Token_Type::CONTINUE: return true;
    case Token_Type::BREAK: return true;
    case Token_Type::RETURN: return true;
    }
    return false;
}

TokenAttribute token_attribute_make_empty() {
    TokenAttribute result;
    result.integer_value = 67676767;
    return result;
}

Token token_make(Token_Type::ENUM type, TokenAttribute attribute, int line_num, int char_pos, int char_len, int code_index)
{
    Token result;
    result.type = type;
    result.attribute = attribute;
    result.line_number = line_num;
    result.character_position = char_pos;
    result.lexem_length = char_len;
    result.source_code_index = code_index;
    return result;
}

bool skip_comments(String* code, int* index, int* character_pos, int* line_number)
{
    if (*index + 1 >= code->size) return false;
    if (code->characters[*index] == '/' && code->characters[*index + 1] == '/') {
        while (*index < code->size && code->characters[*index] != '\n') {
            *index = *index + 1;
            *character_pos = *character_pos + 1;
        }
        *index = *index + 1;
        *character_pos = 0;
        *line_number = *line_number + 1;
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
        return true;
    }
    return false;
}

void skip_whitespace_and_comments(String* code, int* index, int* character_pos, int* line_number)
{
    while (*index < code->size && string_contains_character(string_create_static("\t \r\n/"), code->characters[*index]))
    {
        if (skip_comments(code, index, character_pos, line_number)) continue;
        else if (code->characters[*index] == '/') break;
        (*character_pos)++;
        if (code->characters[*index] == '\n') {
            *character_pos = 0;
            *line_number = *line_number + 1;
        }
        *index = *index + 1;
    }
}

bool character_is_digit(int c) {
    return (c >= '0' && c <= '9');
}

bool character_is_letter(int c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

bool string_equals_cstring(String* string, const char* compare) {
    if (strcmp(string->characters, compare) == 0) {
        return true;
    }
    return false;
}

int lexer_result_add_identifier_by_string(LexerResult* result, String identifier) 
{
    // Identifier is a keyword
    int* identifier_id = hashtable_find_element(&result->identifier_index_lookup_table, identifier);
    if (identifier_id != 0) {
        return *identifier_id;
    }
    else {
        String identifier_string_copy = string_create(identifier.characters);
        dynamic_array_push_back(&result->identifiers, identifier_string_copy);
        int index = result->identifiers.size - 1;
        hashtable_insert_element(&result->identifier_index_lookup_table, identifier_string_copy, index);
        return index;
    }
}

LexerResult lexer_parse_string(String* code)
{
    String identifier_string = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&identifier_string));

    Hashtable<String, int> identifier_index_lookup_table = hashtable_create_empty<String, int>(2048, &string_calculate_hash, &string_equals);
    DynamicArray<String> identifiers = dynamic_array_create_empty<String>(code->size);
    DynamicArray<Token> tokens = dynamic_array_create_empty<Token>(code->size);
    int index = 0;
    int character_pos = 0;
    int line_number = 0;
    bool has_errors = false;
    while (index < code->size)
    {
        // Advance index
        skip_whitespace_and_comments(code, &index, &character_pos, &line_number);
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
        case '.':
            dynamic_array_push_back(&tokens, token_make(Token_Type::DOT, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ';':
            dynamic_array_push_back(&tokens, token_make(Token_Type::SEMICOLON, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ',':
            dynamic_array_push_back(&tokens, token_make(Token_Type::COMMA, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '(':
            dynamic_array_push_back(&tokens, token_make(Token_Type::OPEN_PARENTHESIS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ')':
            dynamic_array_push_back(&tokens, token_make(Token_Type::CLOSED_PARENTHESIS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '{':
            dynamic_array_push_back(&tokens, token_make(Token_Type::OPEN_BRACES, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '}':
            dynamic_array_push_back(&tokens, token_make(Token_Type::CLOSED_BRACES, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '[':
            dynamic_array_push_back(&tokens, token_make(Token_Type::OPEN_BRACKETS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ']':
            dynamic_array_push_back(&tokens, token_make(Token_Type::CLOSED_BRACKETS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '+':
            dynamic_array_push_back(&tokens, token_make(Token_Type::OP_PLUS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '*':
            dynamic_array_push_back(&tokens, token_make(Token_Type::OP_STAR, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '/':
            dynamic_array_push_back(&tokens, token_make(Token_Type::OP_SLASH, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '%':
            dynamic_array_push_back(&tokens, token_make(Token_Type::OP_PERCENT, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
            // Check for ambiguities between one and two characters (< and <=, = and ==, ! and !=, ...)
        case '=':
            if (next_character == '=') {
                dynamic_array_push_back(&tokens, token_make(Token_Type::COMPARISON_EQUAL, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&tokens, token_make(Token_Type::OP_ASSIGNMENT, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '-':
            if (next_character == '>') {
                dynamic_array_push_back(&tokens, token_make(Token_Type::ARROW, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&tokens, token_make(Token_Type::OP_MINUS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '<':
            if (next_character == '=') {
                dynamic_array_push_back(&tokens, token_make(Token_Type::COMPARISON_LESS_EQUAL, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&tokens, token_make(Token_Type::COMPARISON_LESS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '>':
            if (next_character == '=') {
                dynamic_array_push_back(&tokens, token_make(Token_Type::COMPARISON_GREATER_EQUAL, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&tokens, token_make(Token_Type::COMPARISON_GREATER, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '!':
            if (next_character == '=') {
                dynamic_array_push_back(&tokens, token_make(Token_Type::COMPARISON_NOT_EQUAL, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&tokens, token_make(Token_Type::LOGICAL_NOT, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '&':
            if (next_character == '&') {
                dynamic_array_push_back(&tokens, token_make(Token_Type::LOGICAL_AND, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&tokens, token_make(Token_Type::LOGICAL_BITWISE_AND, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '|':
            if (next_character == '|') {
                dynamic_array_push_back(&tokens, token_make(Token_Type::LOGICAL_OR, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&tokens, token_make(Token_Type::LOGICAL_BITWISE_OR, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ':':
            if (next_character == ':') {
                dynamic_array_push_back(&tokens, token_make(Token_Type::DOUBLE_COLON, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            else if (next_character == '=') {
                dynamic_array_push_back(&tokens, token_make(Token_Type::INFER_ASSIGN, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&tokens, token_make(Token_Type::COLON, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        }

        // Constants, Identifier and Keywords
        // Parse Numbers
        if (current_character >= '0' && current_character <= '9')
        {
            int pre_comma_start_index = index;
            int pre_comma_end_index = index;
            // Parse number characters
            while (pre_comma_end_index < code->size && code->characters[pre_comma_end_index] >= '0' && code->characters[pre_comma_end_index] <= '9') {
                pre_comma_end_index++;
            }
            pre_comma_end_index--;

            bool comma_exists = false;
            int post_comma_start_index, post_comma_end_index;
            if (pre_comma_end_index+1 < code->size && code->characters[pre_comma_end_index+1] == '.') 
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
                        code->characters[post_comma_end_index] >= '0' && code->characters[post_comma_end_index] <= '9') {
                        post_comma_end_index++;
                    }
                    post_comma_end_index--;
                }
            }

            int int_value = 0;
            for (int i = pre_comma_start_index; i <= pre_comma_end_index; i++) {
                int num_value = code->characters[i] - '0'; // 0 to 9
                int_value = (int_value * 10) + num_value;
            }
            if (comma_exists) 
            {
                // Calculate float value
                float fractional_value = 0.0f;
                float multiplier = 0.1f;
                for (int i = post_comma_start_index; i <= post_comma_end_index; i++) {
                    int num_value = code->characters[i] - '0'; // 0 to 9
                    fractional_value += num_value * multiplier;
                    multiplier *= 0.1f;
                }
                float float_value = int_value + fractional_value;

                // Add float token
                TokenAttribute attribute;
                attribute.float_value = float_value;
                int character_length;
                if (post_comma_end_index == -1) {
                    character_length = pre_comma_end_index - pre_comma_start_index + 2;
                }
                else {
                    character_length = post_comma_end_index - pre_comma_start_index + 1;
                }
                dynamic_array_push_back(&tokens, token_make(Token_Type::FLOAT_LITERAL, attribute, line_number, character_pos, character_length, index));
                index += character_length;
                character_pos += character_length;
                continue;
            }
            else {
                // Add integer Token
                TokenAttribute attribute;
                attribute.integer_value = int_value;
                int character_length = pre_comma_end_index - pre_comma_start_index + 1;
                dynamic_array_push_back(&tokens, token_make(Token_Type::INTEGER_LITERAL, attribute, line_number, character_pos, character_length, index));
                index += character_length;
                character_pos += character_length;
                continue;
            }
        }

        // Identifiers, keywords or error
        if (!character_is_letter(code->characters[index]))
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
            dynamic_array_push_back(&tokens, token_make(Token_Type::ERROR_TOKEN, token_attribute_make_empty(), line_number, character_pos, error_length, index));
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
            if (string_equals_cstring(&identifier_string, "if")) {
                dynamic_array_push_back(&tokens, token_make(Token_Type::IF, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "else")) {
                dynamic_array_push_back(&tokens, token_make(Token_Type::ELSE, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "for")) {
                dynamic_array_push_back(&tokens, token_make(Token_Type::FOR, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "while")) {
                dynamic_array_push_back(&tokens, token_make(Token_Type::WHILE, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "continue")) {
                dynamic_array_push_back(&tokens, token_make(Token_Type::CONTINUE, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "break")) {
                dynamic_array_push_back(&tokens, token_make(Token_Type::BREAK, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "return")) {
                dynamic_array_push_back(&tokens, token_make(Token_Type::RETURN, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "true")) {
                TokenAttribute attribute;
                attribute.bool_value = true;
                dynamic_array_push_back(&tokens, token_make(Token_Type::BOOLEAN_LITERAL, attribute,
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "false")) {
                TokenAttribute attribute;
                attribute.bool_value = false;
                dynamic_array_push_back(&tokens, token_make(Token_Type::BOOLEAN_LITERAL, attribute,
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else {
                // Identifier is acutally a identifier, not a keyword
                TokenAttribute attribute;
                int* identifier_id = hashtable_find_element(&identifier_index_lookup_table, identifier_string);
                if (identifier_id != 0) {
                    attribute.identifier_number = *identifier_id;
                }
                else {
                    String identifier_string_copy = string_create(identifier_string.characters);
                    dynamic_array_push_back(&identifiers, identifier_string_copy);
                    attribute.identifier_number = identifiers.size - 1;
                    // Identifer needs to be added to table
                    hashtable_insert_element(&identifier_index_lookup_table, identifier_string_copy, attribute.identifier_number);
                }

                // Add token
                dynamic_array_push_back(&tokens, token_make(Token_Type::IDENTIFIER, attribute,
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
        }
    }

    LexerResult result;
    result.has_errors = has_errors;
    result.identifiers = identifiers;
    result.tokens = tokens;
    result.identifier_index_lookup_table = identifier_index_lookup_table;
    return result;
}

void lexer_result_destroy(LexerResult* result)
{
    for (int i = 0; i < result->identifiers.size; i++) {
        string_destroy(&result->identifiers[i]);
    }
    dynamic_array_destroy(&result->identifiers);
    dynamic_array_destroy(&result->tokens);
    hashtable_destroy(&result->identifier_index_lookup_table);
}

String lexer_result_identifer_to_string(LexerResult* result, int index) {
    return result->identifiers[index];
}

const char* tokentype_to_string(Token_Type::ENUM type)
{
    switch (type)
    {
    case Token_Type::IF: return "IF";
    case Token_Type::ELSE: return "ELSE";
    case Token_Type::FOR: return "FOR";
    case Token_Type::WHILE: return "WHILE";
    case Token_Type::CONTINUE: return "CONTINUE";
    case Token_Type::BREAK: return "BREAK";
    case Token_Type::DOT: return "DOT";
    case Token_Type::COLON: return "COLON";
    case Token_Type::COMMA: return "COMMA";
    case Token_Type::DOUBLE_COLON: return "DOUBLE_COLON";
    case Token_Type::INFER_ASSIGN: return "INFER_ASSIGN";
    case Token_Type::ARROW: return "ARROW";
    case Token_Type::SEMICOLON: return "SEMICOLON";
    case Token_Type::OPEN_PARENTHESIS: return "OPEN_BRACKET";
    case Token_Type::CLOSED_PARENTHESIS: return "CLOSED_BRACKET";
    case Token_Type::OPEN_BRACES: return "OPEN_CURLY_BRACKET";
    case Token_Type::CLOSED_BRACES: return "CLOSED_CURLY_BRACKET";
    case Token_Type::OPEN_BRACKETS: return "OPEN_SQUARE_BRACKET";
    case Token_Type::CLOSED_BRACKETS: return "CLOSED_SQUARE_BRACKET";
    case Token_Type::OP_ASSIGNMENT: return "OP_ASSIGNMENT";
    case Token_Type::OP_PLUS: return "OP_PLUS";
    case Token_Type::OP_MINUS: return "OP_MINUS";
    case Token_Type::OP_SLASH: return "OP_SLASH";
    case Token_Type::OP_STAR: return "OP_STAR";
    case Token_Type::OP_PERCENT: return "OP_PERCENT";
    case Token_Type::COMPARISON_LESS: return "COMPARISON_LESS";
    case Token_Type::COMPARISON_LESS_EQUAL: return "COMPARISON_LESS_EQUAL";
    case Token_Type::COMPARISON_GREATER: return "COMPARISON_GREATER";
    case Token_Type::COMPARISON_GREATER_EQUAL: return "COMPARISON_GREATER_EQUAL";
    case Token_Type::COMPARISON_EQUAL: return "COMPARISON_EQUAL";
    case Token_Type::COMPARISON_NOT_EQUAL: return "COMPARISON_NOT_EQUAL";
    case Token_Type::LOGICAL_AND: return "LOGICAL_AND";
    case Token_Type::LOGICAL_OR: return "LOGICAL_OR";
    case Token_Type::LOGICAL_BITWISE_AND: return "LOGICAL_BITWISE_AND";
    case Token_Type::LOGICAL_BITWISE_OR: return "LOGICAL_BITWISE_OR";
    case Token_Type::LOGICAL_NOT: return "LOGICAL_NOT";
    case Token_Type::INTEGER_LITERAL: return "INT_LITERAL";
    case Token_Type::FLOAT_LITERAL: return "FLOAT_LITERAL";
    case Token_Type::BOOLEAN_LITERAL: return "BOOLEAN_LITERAL";
    case Token_Type::IDENTIFIER: return "IDENTIFIER";
    case Token_Type::ERROR_TOKEN: return "ERROR_TOKE";
    }
    return "TOKEN_NOT_KNOWN";
}

void lexer_result_print(LexerResult* result)
{
    String msg = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&msg));
    string_append_formated(&msg, "Tokens: \n");
    for (int i = 0; i < result->tokens.size; i++) {
        Token token = result->tokens[i];
        string_append_formated(&msg, "\t %s (Line %d, Pos %d, Length %d)",
            tokentype_to_string(token.type), token.line_number, token.character_position, token.lexem_length);
        if (token.type == Token_Type::IDENTIFIER) {
            string_append_formated(&msg, " = %s", result->identifiers.data[token.attribute.identifier_number].characters);
        }
        else if (token.type == Token_Type::INTEGER_LITERAL) {
            string_append_formated(&msg, " = %d", token.attribute.integer_value);
        }
        string_append_formated(&msg, "\n");
    }
    logg("\n%s\n", msg.characters);
}

/*
    Wie schaut die syntax aus?

    Variablen, Expressions und Statements
    x : int = expression;
    x : int;
    x := 5;
    x :: (int)5; // Constant int, i dont think i want this (Because attribute system)

    Arrays und dynamic arrays und initialisierung;
    x : int[] = [int: 15, 32, 17, 18];
    x : int[..] = dynamic_array::create();

    Funktionen, Macros und

    Structs, Enums und Unions

    Modularisierung

    Mit was starte ich?
     - Funktionen, Variablen und Statements
     - Scopes
     - If-else

     Program  -> Function*
     Function -> ID ::

     function_name :: (parameters, ...) -> (return_values) {}
     return_value function_name (parameters, ...) {}

     lambdas:
     take_lambda_function :: ( x: (int)->void ) { x(5); } // Definition
     take_lambda_function((num : int) -> void { print(num); });

     TYPE ID(
     int main(int argc, char* argv[]) {}
*/

/*
    Variable Declaration:
        a : int;
    Variable Declaration + Initialization
        a : int = a;
    Variable Declaration with Type Deduction
        a := 5;
    Variable Assignment
        a = (5 + 5);
    If/For/While statement
*/

int parser_find_next_token_type(Parser* parser, Token_Type::ENUM type)
{
    for (int i = parser->index; i < parser->tokens.size; i++) {
        if (parser->tokens[i].type == type) {
            return i;
        }
    }
    return parser->tokens.size;
}

void parser_log_intermediate_error(Parser* parser, const char* msg, int start_token, int end_token)
{
    ParserError error;
    error.error_message = msg;
    error.token_start_index = start_token;
    error.token_end_index = end_token;
    dynamic_array_push_back(&parser->intermediate_errors, error);
}

void parser_reset_intermediate_errors(Parser* parser) {
    dynamic_array_reset(&parser->intermediate_errors);
}

void parser_print_intermediate_errors(Parser* parser) {
    for (int i = 0; i < parser->intermediate_errors.size; i++) {
        ParserError* error = &parser->intermediate_errors.data[i];
        logg("Intermediate error #%d: %s\n", i, error->error_message);
    }
}

void parser_log_unresolvable_error(Parser* parser, const char* msg, int start_token, int end_token)
{
    ParserError error;
    error.error_message = msg;
    error.token_start_index = start_token;
    error.token_end_index = end_token;
    dynamic_array_push_back(&parser->unresolved_errors, error);
}

bool parser_test_next_token(Parser* parser, Token_Type::ENUM type)
{
    if (parser->index >= parser->tokens.size) {
        return false;
    }
    if (parser->tokens[parser->index].type == type) {
        return true;
    }
    return false;
}

bool parser_test_next_2_tokens(Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2)
{
    if (parser->index + 1 >= parser->tokens.size) {
        return false;
    }
    if (parser->tokens[parser->index].type == type1 && parser->tokens[parser->index + 1].type == type2) {
        return true;
    }
    return false;
}

bool parser_test_next_3_tokens(Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2, Token_Type::ENUM type3)
{
    if (parser->index + 2 >= parser->tokens.size) {
        return false;
    }
    if (parser->tokens[parser->index].type == type1 &&
        parser->tokens[parser->index + 1].type == type2 &&
        parser->tokens[parser->index + 2].type == type3) {
        return true;
    }
    return false;
}

bool parser_test_next_4_tokens(Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2, Token_Type::ENUM type3, Token_Type::ENUM type4)
{
    if (parser->index + 3 >= parser->tokens.size) {
        return false;
    }
    if (parser->tokens[parser->index].type == type1 &&
        parser->tokens[parser->index + 1].type == type2 &&
        parser->tokens[parser->index + 2].type == type3 &&
        parser->tokens[parser->index + 3].type == type4) {
        return true;
    }
    return false;
}





/*
    AST
*/
void ast_node_expression_destroy(Ast_Node_Expression* expression)
{
    if (expression->type == ExpressionType::OP_ADD) {
        ast_node_expression_destroy(expression->left);
        ast_node_expression_destroy(expression->right);
        if (expression->left != 0) {
            delete expression->left;
        }
        if (expression->right != 0) {
            delete expression->right;
        }
    }
}

void ast_node_statement_destroy(Ast_Node_Statement* statement) {
    if (statement->type == StatementType::VARIABLE_ASSIGNMENT) {
        ast_node_expression_destroy(&statement->expression);
    }
}

void ast_node_statement_block_destroy(Ast_Node_Statement_Block* block) {
    for (int i = 0; i < block->statements.size; i++) {
        Ast_Node_Statement* statement = &block->statements.data[i];
        ast_node_statement_destroy(statement);
    }
    dynamic_array_destroy(&block->statements);
}

void ast_node_function_destroy(Ast_Node_Function* function)
{
    dynamic_array_destroy(&function->parameters);
    ast_node_statement_block_destroy(&function->body);
}

void ast_node_root_destroy(Ast_Node_Root* root)
{
    for (int i = 0; i < root->functions.size; i++)
    {
        Ast_Node_Function* function = &root->functions.data[i];
        ast_node_function_destroy(function);
    }
    dynamic_array_destroy(&root->functions);
}

void ast_node_expression_append_to_string(String* string, Ast_Node_Expression* expression, LexerResult* result)
{
    if (expression->type == ExpressionType::OP_ADD) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " + ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_NEGATE) {
        string_append(string, "-");
        ast_node_expression_append_to_string(string, expression->left, result);
    }
    if (expression->type == ExpressionType::OP_LOGICAL_NOT) {
        string_append(string, "!");
        ast_node_expression_append_to_string(string, expression->left, result);
    }
    if (expression->type == ExpressionType::OP_MULTIPLY) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " * ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_SUBTRACT) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " - ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_DIVIDE) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " / ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_MODULO) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " % ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_BOOLEAN_AND) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " && ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_BOOLEAN_OR) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " || ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_EQUAL) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " || ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_NOT_EQUAL) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " || ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_LESS_THAN) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " < ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_LESS_EQUAL) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " <= ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_GREATER_THAN) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " > ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_GREATER_EQUAL) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, result);
        string_append(string, " >= ");
        ast_node_expression_append_to_string(string, expression->right, result);
        string_append(string, ")");
    }
    else if (expression->type == ExpressionType::LITERAL) {
        Token& t = result->tokens[expression->literal_token_index];
        if (t.type == Token_Type::INTEGER_LITERAL) {
            string_append_formated(string, "%d", result->tokens[expression->literal_token_index].attribute.integer_value);
        }
        else if (t.type == Token_Type::FLOAT_LITERAL) {
            string_append_formated(string, "%f", result->tokens[expression->literal_token_index].attribute.float_value);
        }
        else if (t.type == Token_Type::BOOLEAN_LITERAL) {
            string_append_formated(string, "%s",
                result->tokens[expression->literal_token_index].attribute.bool_value ? "true" : "false");
        }
    }
    else if (expression->type == ExpressionType::VARIABLE_READ) {
        string_append_formated(string, "%s", result->identifiers[expression->variable_name_id].characters);
    }
}

void ast_node_statement_append_to_string(String* string, Ast_Node_Statement* statement, LexerResult* result, int indentation_level)
{
    for (int i = 0; i < indentation_level; i++) {
        string_append_formated(string, "    ");
    }

    if (statement->type == StatementType::VARIABLE_DEFINITION) {
        string_append_formated(string, "%s : %s;",
            result->identifiers[statement->variable_name_id].characters,
            result->identifiers[statement->variable_type_id].characters);
    }
    else if (statement->type == StatementType::VARIABLE_ASSIGNMENT) {
        string_append_formated(string, "%s = ", result->identifiers[statement->variable_name_id].characters);
        ast_node_expression_append_to_string(string, &statement->expression, result);
        string_append(string, ";");
    }
    else if (statement->type == StatementType::RETURN_STATEMENT) {
        string_append_formated(string, "return ");
        ast_node_expression_append_to_string(string, &statement->expression, result);
        string_append_formated(string, ";");
    }
    else if (statement->type == StatementType::VARIABLE_DEFINE_ASSIGN) {
        string_append_formated(string, "%s : %s = ",
            result->identifiers[statement->variable_name_id].characters,
            result->identifiers[statement->variable_type_id].characters);
        ast_node_expression_append_to_string(string, &statement->expression, result);
        string_append_formated(string, ";");
    }
    else if (statement->type == StatementType::VARIABLE_DEFINE_INFER) {
        string_append_formated(string, "%s := ",
            result->identifiers[statement->variable_name_id].characters);
        ast_node_expression_append_to_string(string, &statement->expression, result);
        string_append_formated(string, ";");
    }
    else if (statement->type == StatementType::STATEMENT_BLOCK) {
        string_append_formated(string, "{\n");
        for (int i = 0; i < statement->statements.statements.size; i++) {
            ast_node_statement_append_to_string(string, &statement->statements.statements[i], result, indentation_level+1);
        }
        string_append_formated(string, "\n");
        for (int i = 0; i < indentation_level; i++) {
            string_append_formated(string, "    ");
        }
        string_append_formated(string, "}");
    }
    else if (statement->type == StatementType::IF_BLOCK || statement->type == StatementType::IF_ELSE_BLOCK) 
    {
        // Print if block
        string_append_formated(string, "if ");
        ast_node_expression_append_to_string(string, &statement->expression, result);
        string_append_formated(string, "\n");
        for (int i = 0; i < indentation_level; i++) {
            string_append_formated(string, "    ");
        }
        string_append_formated(string, "{\n");
        for (int i = 0; i < statement->if_statements.statements.size; i++) {
            ast_node_statement_append_to_string(string, &statement->if_statements.statements[i], result, indentation_level+1);
        }
        string_append_formated(string, "\n");
        for (int i = 0; i < indentation_level; i++) {
            string_append_formated(string, "    ");
        }
        string_append_formated(string, "}");

        if (statement->type == StatementType::IF_ELSE_BLOCK) 
        {
            string_append_formated(string, "\n");
            for (int i = 0; i < indentation_level; i++) {
                string_append_formated(string, "    ");
            }
            string_append_formated(string, "else\n");
            for (int i = 0; i < indentation_level; i++) {
                string_append_formated(string, "    ");
            }
            string_append_formated(string, "{\n");
            for (int i = 0; i < statement->else_statements.statements.size; i++) {
                ast_node_statement_append_to_string(string, &statement->else_statements.statements[i], result, indentation_level + 1);
            }
            string_append_formated(string, "\n");
            for (int i = 0; i < indentation_level; i++) {
                string_append_formated(string, "    ");
            }
            string_append_formated(string, "}");
        }
    }
}

void ast_node_function_append_to_string(String* string, Ast_Node_Function* function, LexerResult* result)
{
    string_append_formated(string, "%s :: (", result->identifiers[function->function_name_id].characters);
    for (int i = 0; i < function->parameters.size; i++) {
        Parameter* param = &(function->parameters.data[i]);
        string_append_formated(string, "%s : %s, ",
            result->identifiers[param->name_id].characters,
            result->identifiers[param->type_id].characters);
    }
    string_append_formated(string, ") -> %s {\n", result->identifiers[function->return_type_id].characters);

    for (int i = 0; i < function->body.statements.size; i++) {
        Ast_Node_Statement* statement = &function->body.statements.data[i];
        ast_node_statement_append_to_string(string, statement, result, 1);
        string_append_formated(string, "\n");
    }
    string_append_formated(string, "}\n");
}

void ast_node_root_append_to_string(String* string, Ast_Node_Root* root, LexerResult* lexer_result)
{
    string_append_formated(string, "\nRoot: (Function count #%d)\n", root->functions.size);
    for (int i = 0; i < root->functions.size; i++)
    {
        Ast_Node_Function* function = &(root->functions.data[i]);
        ast_node_function_append_to_string(string, function, lexer_result);
    }
}



/*
    PARSER
*/
bool parser_parse_expression(Parser* parser, Ast_Node_Expression* expression);

bool parser_parse_expression_single_value(Parser* parser, Ast_Node_Expression* expression)
{
    expression->left = 0;
    expression->right = 0;
    if (parser_test_next_token(parser, Token_Type::IDENTIFIER))
    {
        expression->type = ExpressionType::VARIABLE_READ;
        expression->variable_name_id = parser->tokens[parser->index].attribute.identifier_number;
        parser->index++;
        return true;
    }
    else if (parser_test_next_token(parser, Token_Type::OP_MINUS))
    {
        expression->type = ExpressionType::OP_NEGATE;
        parser->index++;
        expression->left = new Ast_Node_Expression();
        if (!parser_parse_expression_single_value(parser, expression->left)) {
            delete expression->left;
            parser->index--;
            return false;
        }
        return true;
    }
    else if (parser_test_next_token(parser, Token_Type::LOGICAL_NOT))
    {
        expression->type = ExpressionType::OP_LOGICAL_NOT;
        parser->index++;
        expression->left = new Ast_Node_Expression();
        if (!parser_parse_expression_single_value(parser, expression->left)) {
            delete expression->left;
            parser->index--;
            return false;
        }
        return true;
    }
    else if (parser_test_next_token(parser, Token_Type::INTEGER_LITERAL) ||
        parser_test_next_token(parser, Token_Type::FLOAT_LITERAL) ||
        parser_test_next_token(parser, Token_Type::BOOLEAN_LITERAL)) 
    {
        expression->type = ExpressionType::LITERAL;
        expression->literal_token_index = parser->index;
        parser->index++;
        return true;
    }
    else if (parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS))
    {
        int rewind_point = parser->index;
        parser->index++;
        if (parser_parse_expression(parser, expression)) {
            if (parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
                parser->index++;
                return true;
            }
            else {
                ast_node_expression_destroy(expression);
                parser->index = rewind_point;
                return false;
            }
        }
        parser->index = rewind_point;
        return false;
    }
    else {
        parser_log_intermediate_error(parser,
            "Error, could not parse single expression, does not start with constant or identifier\n", parser->index, parser->index+1);
        return false;
    }
}

bool parser_parse_binary_operation(Parser* parser, ExpressionType::ENUM* op_type, int* op_priority)
{
    /*
        Priority tree:
            0       ---     &&
            1       ---     ||
            2       ---     ==, != 
            3       ---     <, >, <=, >=
            4       ---     +, -
            5       ---     *, /
            6       ---     %
    */
    if (parser_test_next_token(parser, Token_Type::OP_PLUS)) {
        *op_type = ExpressionType::OP_ADD;
        *op_priority = 4;
    }
    else if (parser_test_next_token(parser, Token_Type::OP_MINUS)) {
        *op_type = ExpressionType::OP_SUBTRACT;
        *op_priority = 4;
    }
    else if (parser_test_next_token(parser, Token_Type::OP_SLASH)) {
        *op_type = ExpressionType::OP_DIVIDE;
        *op_priority = 5;
    }
    else if (parser_test_next_token(parser, Token_Type::OP_STAR)) {
        *op_type = ExpressionType::OP_MULTIPLY;
        *op_priority = 5;
    }
    else if (parser_test_next_token(parser, Token_Type::OP_PERCENT)) {
        *op_type = ExpressionType::OP_MODULO;
        *op_priority = 6;
    }
    else if (parser_test_next_token(parser, Token_Type::LOGICAL_AND)) {
        *op_type = ExpressionType::OP_BOOLEAN_AND;
        *op_priority = 0;
    }
    else if (parser_test_next_token(parser, Token_Type::LOGICAL_OR)) {
        *op_type = ExpressionType::OP_BOOLEAN_OR;
        *op_priority = 1;
    }
    else if (parser_test_next_token(parser, Token_Type::COMPARISON_EQUAL)) {
        *op_type = ExpressionType::OP_EQUAL;
        *op_priority = 2;
    }
    else if (parser_test_next_token(parser, Token_Type::COMPARISON_NOT_EQUAL)) {
        *op_type = ExpressionType::OP_NOT_EQUAL;
        *op_priority = 2;
    }
    else if (parser_test_next_token(parser, Token_Type::COMPARISON_GREATER)) {
        *op_type = ExpressionType::OP_GREATER_THAN;
        *op_priority = 3;
    }
    else if (parser_test_next_token(parser, Token_Type::COMPARISON_GREATER_EQUAL)) {
        *op_type = ExpressionType::OP_GREATER_EQUAL;
        *op_priority = 3;
    }
    else if (parser_test_next_token(parser, Token_Type::COMPARISON_LESS)) {
        *op_type = ExpressionType::OP_LESS_THAN;
        *op_priority = 3;
    }
    else if (parser_test_next_token(parser, Token_Type::COMPARISON_LESS_EQUAL)) {
        *op_type = ExpressionType::OP_LESS_EQUAL;
        *op_priority = 3;
    }
    else {
        return false;
    }
    parser->index++;
    return true;
}

// Input expression is not empty
bool parser_parse_expression_new_priority(Parser* parser, Ast_Node_Expression* expression, int min_priority)
{
    int start_point = parser->index;
    int rewind_point = parser->index;

    bool first_run = true;
    int first_run_priority = -1;
    // Parse expression start operand
    while (true)
    {
        int first_op_priority;
        ExpressionType::ENUM first_op_type;
        if (!parser_parse_binary_operation(parser, &first_op_type, &first_op_priority)) {
            break;
        }
        if (first_run) {
            first_run = false;
            first_run_priority = first_op_priority;
        }
        else {
            if (first_op_priority < first_run_priority) {
                first_run_priority = first_op_priority;
            }
            if (first_op_priority < min_priority) {
                parser->index = rewind_point;
                break;
            }
        }

        Ast_Node_Expression right_operand;
        if (!parser_parse_expression_single_value(parser, &right_operand)) {
            parser->index = rewind_point;
            break;
        }
        rewind_point = parser->index;

        int second_op_priority;
        ExpressionType::ENUM second_op_type;
        bool second_op_exists = parser_parse_binary_operation(parser, &second_op_type, &second_op_priority);
        if (second_op_exists) {
            parser->index--;
            if (second_op_priority > first_run_priority) {
                parser_parse_expression_new_priority(parser, &right_operand, second_op_priority);
            }
        }
        Ast_Node_Expression* new_left = new Ast_Node_Expression();
        Ast_Node_Expression* new_right = new Ast_Node_Expression();
        *new_right = right_operand;
        *new_left = *expression;
        expression->type = first_op_type;
        expression->left = new_left;
        expression->right = new_right;
        if (!second_op_exists) break;
    }

    return parser->index != start_point;
}

bool parser_parse_expression(Parser* parser, Ast_Node_Expression* expression)
{
    if (!parser_parse_expression_single_value(parser, expression)) {
        return false;
    }
    parser_parse_expression_new_priority(parser, expression, 0);
    return true;
}

bool parser_parse_statment_block_or_single_statement(Parser* parser, Ast_Node_Statement_Block* block);
bool parser_parse_statement_block(Parser* parser, Ast_Node_Statement_Block* block);
bool parser_parse_statement(Parser* parser, Ast_Node_Statement* statement)
{
    int rewind_point = parser->index;

    if (parser_parse_statement_block(parser, &statement->statements)) {
        statement->type = StatementType::STATEMENT_BLOCK;
        return true;
    }

    bool valid_statement = false;
    if (!valid_statement && parser_test_next_token(parser, Token_Type::IF))
    {
        parser->index++;
        if (!parser_parse_expression(parser, &statement->expression)) {
            parser->index = rewind_point;
            return false;
        }
        if (!parser_parse_statment_block_or_single_statement(parser, &statement->if_statements)) {
            parser->index = rewind_point;
            return false;
        }
        statement->type = StatementType::IF_BLOCK;
        rewind_point = parser->index;

        // Test else part
        if (parser_test_next_token(parser, Token_Type::ELSE)) {
            parser->index++;
            if (!parser_parse_statment_block_or_single_statement(parser, &statement->else_statements)) {
                parser->index = rewind_point;
                return true;
            }
            statement->type = StatementType::IF_ELSE_BLOCK;
            return true;
        }
        return true;
    }

    if (!valid_statement && parser_test_next_token(parser, Token_Type::RETURN))
    {
        parser->index++;
        Ast_Node_Expression expression;
        if (!parser_parse_expression(parser, &expression)) { // Return may also be fine if the function does not return anything
            parser->index = rewind_point;
            return false;
        }
        statement->type = StatementType::RETURN_STATEMENT;
        statement->expression = expression;
        valid_statement = true;
    }

    if (!valid_statement && parser_test_next_4_tokens(parser,
        Token_Type::IDENTIFIER, Token_Type::COLON, Token_Type::IDENTIFIER, Token_Type::SEMICOLON)) // Variable definition 'x : int;'
    {
        statement->type = StatementType::VARIABLE_DEFINITION;
        statement->variable_name_id = parser->tokens[parser->index].attribute.identifier_number;
        statement->variable_type_id = parser->tokens[parser->index + 2].attribute.identifier_number;
        parser->index += 3; // ! not 4, since the ; parsing is done by the caller of this function
        valid_statement = true;
    }

    if (!valid_statement && parser_test_next_4_tokens(parser,
        Token_Type::IDENTIFIER, Token_Type::COLON, Token_Type::IDENTIFIER, Token_Type::OP_ASSIGNMENT)) // Variable define-assign 'x : int = ...'
    {
        statement->type = StatementType::VARIABLE_DEFINE_ASSIGN;
        statement->variable_name_id = parser->tokens[parser->index].attribute.identifier_number;
        statement->variable_type_id = parser->tokens[parser->index + 2].attribute.identifier_number;
        parser->index += 4;
        Ast_Node_Expression expr;
        if (!parser_parse_expression(parser, &expr)) {
            parser->index = rewind_point;
            return false;
        }
        statement->expression = expr;
        valid_statement = true;
    }

    if (!valid_statement &&
        parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER, Token_Type::INFER_ASSIGN)) // Variable define-assign 'x :='
    {
        statement->type = StatementType::VARIABLE_DEFINE_INFER;
        statement->variable_name_id = parser->tokens[parser->index].attribute.identifier_number;
        parser->index += 2;
        Ast_Node_Expression expr;
        if (!parser_parse_expression(parser, &expr)) {
            parser->index = rewind_point;
            return false;
        }
        statement->expression = expr;
        valid_statement = true;
    }

    if (!valid_statement && parser_test_next_2_tokens(parser, Token_Type::IDENTIFIER, Token_Type::OP_ASSIGNMENT))
    {
        // Assignment
        statement->type = StatementType::VARIABLE_ASSIGNMENT;
        statement->variable_name_id = parser->tokens[parser->index].attribute.identifier_number;
        parser->index += 2;

        // Parse expression
        if (!parser_parse_expression(parser, &statement->expression)) {
            parser->index = rewind_point;
            return false;
        }
        valid_statement = true;
    }

    if (!valid_statement) {
        return false;
    }
    if (parser_test_next_token(parser, Token_Type::SEMICOLON)) {
        parser->index++;
        return true;
    }

    parser->index = rewind_point;
    return false;
}

// Returns true if there is a statement block, false if not
bool parser_parse_statement_block(Parser* parser, Ast_Node_Statement_Block* block)
{
    int scope_start = parser->index;
    int rewind_index = parser->index;
    if (!parser_test_next_token(parser, Token_Type::OPEN_BRACES)) {
        return false;
    }
    parser->index++;

    bool exit_failure = false;
    block->statements = dynamic_array_create_empty<Ast_Node_Statement>(16);
    SCOPE_EXIT(if (exit_failure) ast_node_statement_block_destroy(block););
    Ast_Node_Statement statement;
    while (parser->index < parser->tokens.size)
    {
        if (parser_test_next_token(parser, Token_Type::CLOSED_BRACES)) {
            parser->index++;
            return true;
        }

        rewind_index = parser->index;
        if (parser_parse_statement(parser, &statement)) {
            dynamic_array_push_back(&block->statements, statement);
        }
        else
        {
            // Error recovery, go after next ; or }
            int next_semicolon = parser_find_next_token_type(parser, Token_Type::SEMICOLON);
            int next_braces = parser_find_next_token_type(parser, Token_Type::CLOSED_BRACES);
            if (next_semicolon >= parser->tokens.size || next_braces >= parser->tokens.size) {
                parser_log_intermediate_error(parser, "Scope block does not end with } or;\n", parser->index, next_semicolon);
                exit_failure = true;
                return false;
            }
            if (next_semicolon < next_braces) {
                parser_log_unresolvable_error(parser, "Could not parse statement, skipped it\n", parser->index, next_semicolon);
                parser->index = next_semicolon + 1;
            }
            else {
                parser_log_unresolvable_error(parser, "Could not parse statement, skipped it\n", parser->index, next_braces);
                parser->index = next_braces + 1;
                return true;
            }
        }
    }
    parser_log_unresolvable_error(parser, "Scope block does not end with }\n", scope_start, parser->tokens.size - 1);
    exit_failure = true;
    return false;
}

// Different to parse_block, this function does not need {} around the statements
bool parser_parse_statment_block_or_single_statement(Parser* parser, Ast_Node_Statement_Block* block)
{
    if (parser_parse_statement_block(parser, block)) {
        return true;
    }
    Ast_Node_Statement statement;
    if (!parser_parse_statement(parser, &statement)) {
        return false;
    }
    block->statements = dynamic_array_create_empty<Ast_Node_Statement>(1);
    dynamic_array_push_back(&block->statements, statement);
    return true;
}

bool parser_parse_function(Parser* parser, Ast_Node_Function* function)
{
    int rewind_point = parser->index;
    function->parameters = dynamic_array_create_empty<Parameter>(8);
    bool exit_failure = false;
    SCOPE_EXIT(
        if (exit_failure) {
            dynamic_array_destroy(&function->parameters);
            parser->index = rewind_point;
        }
    );

    // Parse Function start
    if (!parser_test_next_3_tokens(parser, Token_Type::IDENTIFIER, Token_Type::DOUBLE_COLON, Token_Type::OPEN_PARENTHESIS)) {
        parser_log_intermediate_error(parser, "Could not parse function, it did not start with 'ID :: ('", parser->index, parser->index + 3);
        exit_failure = true;
        return false;
    }
    function->function_name_id = parser->tokens[parser->index].attribute.identifier_number;
    parser->index += 3;

    // Parse Parameters
    while (!parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS))
    {
        // Parameters need to be named, meaning x : int     
        if (!parser_test_next_3_tokens(parser, Token_Type::IDENTIFIER, Token_Type::COLON, Token_Type::IDENTIFIER)) {
            parser_log_intermediate_error(parser, "Could not parse function, parameter was not in the form ID : TYPE", parser->index, parser->index + 3);
            exit_failure = true;
            return false;
        }

        Parameter param;
        param.name_id = parser->tokens[parser->index].attribute.identifier_number;
        param.type_id = parser->tokens[parser->index + 2].attribute.identifier_number;
        dynamic_array_push_back(&function->parameters, param);
        parser->index += 3;

        // Check for ) or ,
        if (parser_test_next_token(parser, Token_Type::COMMA)) {
            parser->index++;
        }
    }
    parser->index++; // Skip )

    // Parse Return type
    if (!parser_test_next_2_tokens(parser, Token_Type::ARROW, Token_Type::IDENTIFIER)) {
        parser_log_intermediate_error(parser, "Could not parse function, did not find return type after Parameters '-> TYPE'",
            parser->index, parser->index + 2);
        exit_failure = true;
        return false;
    }
    function->return_type_id = parser->tokens[parser->index + 1].attribute.identifier_number;
    parser->index += 2;

    // Parse statements
    if (!parser_parse_statement_block(parser, &function->body)) {
        exit_failure = true;
        return false;
    };
    SCOPE_EXIT(if (exit_failure) ast_node_statement_block_destroy(&function->body););

    return true;
}


bool parser_parse_root(Parser* parser, Ast_Node_Root* root)
{
    root->functions = dynamic_array_create_empty<Ast_Node_Function>(32);

    Ast_Node_Function function;
    while (true)
    {
        if (parser_parse_function(parser, &function)) {
            dynamic_array_push_back(&root->functions, function);
        }
        else if (parser->index >= parser->tokens.size) {
            break;
        }
        else {
            // Skip to next token in next line, then try parsing again
            int next_line_token = parser->index;
            while (next_line_token < parser->tokens.size &&
                parser->tokens[next_line_token].line_number == parser->tokens[parser->index].line_number)
            {
                next_line_token++;
            }
            if (next_line_token >= parser->tokens.size) {
                parser_log_unresolvable_error(parser, "Could not parse last function in file!\n", parser->index, parser->tokens.size - 1);
                break;
            }
            else {
                // Skip to next line token, try parsing funciton again
                parser_log_unresolvable_error(parser, "Could not parse function header!\n", parser->index, next_line_token - 1);
                parser->index = next_line_token;
            }
        }
    }

    return true;
}

void parser_semantic_analysis(Parser* parser, LexerResult* lexer_result, Ast_Node_Root* root)
{
}

Parser parser_parse(LexerResult* result)
{
    Parser parser;
    parser.index = 0;
    parser.tokens = dynamic_array_to_array(&result->tokens);
    parser.intermediate_errors = dynamic_array_create_empty<ParserError>(16);
    parser.unresolved_errors = dynamic_array_create_empty<ParserError>(16);

    // Parse root
    if (!parser_parse_root(&parser, &parser.root)) {
        logg("Dont quite know what to do herer lol\n");
    }

    // Do semantic checking
    //parser_semantic_analysis(&parser, result, &root);

    return parser;
}

void parser_destroy(Parser* parser)
{
    dynamic_array_destroy(&parser->intermediate_errors);
    dynamic_array_destroy(&parser->unresolved_errors);
    ast_node_root_destroy(&parser->root);
}




/*
    AST INTERPRETER
*/

struct Ast_Interpreter_Variable
{
    int variable_name;
    Ast_Interpreter_Value value;
};

/*
Symbol Table + Scoped variable "lifetimes"
Where do i put the symbol table?
    --> Is needed in Ast-Interpreter (Is it?, i can just keep track of variables in a stack, which works better i think)
    --> Is necessary for Semantic Analysis, do i need to save the symbol table for specific ast_nodes?
    --> Also required for byte-code generation i guess --> Not exactly, since I know after semantic analysis that everythings correct

First --> Statement_Block also parses {}, and statement parsing recursively calls statment block stuff
*/

struct Ast_Interpreter
{
    Ast_Node_Root* root;
    DynamicArray<Ast_Interpreter_Variable> symbol_table;
    DynamicArray<int> scope_beginnings;
    LexerResult* lexer;

    int int_token_index;
    int float_token_index;
    int bool_token_index;
};

Ast_Interpreter ast_interpreter_create(Ast_Node_Root* root, LexerResult* lexer)
{
    Ast_Interpreter result;
    result.root = root;
    result.lexer = lexer;
    result.symbol_table = dynamic_array_create_empty<Ast_Interpreter_Variable>(16);
    result.scope_beginnings = dynamic_array_create_empty<int>(16);
    dynamic_array_push_back(&result.scope_beginnings, 0);
    return result;
}

void ast_interpreter_destroy(Ast_Interpreter* interpreter) {
    dynamic_array_destroy(&interpreter->symbol_table);
    dynamic_array_destroy(&interpreter->scope_beginnings);
}

int ast_interpreter_find_variable_index(Ast_Interpreter* interpreter, int var_name) {
    for (int i = interpreter->symbol_table.size - 1; i >= 0; i--) {
        if (interpreter->symbol_table[i].variable_name == var_name) {
            return i;
        }
    }
    return -1;
}

Ast_Interpreter_Variable* ast_interpreter_find_variable(Ast_Interpreter* interpreter, int var_name) {
    for (int i = interpreter->symbol_table.size - 1; i >= 0; i--) {
        if (interpreter->symbol_table[i].variable_name == var_name) {
            return &interpreter->symbol_table[i];
        }
    }
    return 0;
}

void ast_interpreter_begin_new_scope(Ast_Interpreter* interpreter) {
    dynamic_array_push_back(&interpreter->scope_beginnings, interpreter->symbol_table.size);
}

void ast_interpreter_exit_scope(Ast_Interpreter* interpreter) {
    if (interpreter->scope_beginnings.size == 0) {
        panic("Should not happend!\n");
        return;
    }
    int scope_start = interpreter->scope_beginnings[interpreter->scope_beginnings.size - 1];
    dynamic_array_remove_range_ordered(&interpreter->symbol_table, scope_start, interpreter->symbol_table.size);
}

void ast_interpreter_define_variable(Ast_Interpreter* interpreter, Ast_Interpreter_Value_Type::ENUM type, int var_name)
{
    int current_scope_start = interpreter->scope_beginnings[interpreter->scope_beginnings.size - 1];
    if (ast_interpreter_find_variable_index(interpreter, var_name) >= current_scope_start) {
        logg("Variable %s already defined in this scope!", lexer_result_identifer_to_string(interpreter->lexer, var_name).characters);
        return;
    }

    Ast_Interpreter_Variable var;
    var.value.type = type;
    var.value.bool_value = false;
    var.value.int_value = -69;
    var.value.float_value = -69.69f;
    var.value.type = type;
    var.variable_name = var_name;
    dynamic_array_push_back(&interpreter->symbol_table, var);
}

Ast_Interpreter_Value ast_interpreter_evaluate_expression(Ast_Interpreter* interpreter, Ast_Node_Expression* expression)
{
    Ast_Interpreter_Value result;
    result.type = Ast_Interpreter_Value_Type::ERROR_VAL;

    if (expression->type == ExpressionType::LITERAL) {
        Token& token = interpreter->lexer->tokens[expression->literal_token_index];
        if (token.type == Token_Type::INTEGER_LITERAL) {
            result.type = Ast_Interpreter_Value_Type::INTEGER;
            result.int_value = token.attribute.integer_value;
        }
        else if (token.type == Token_Type::FLOAT_LITERAL) {
            result.type = Ast_Interpreter_Value_Type::FLOAT;
            result.float_value = token.attribute.float_value;
        }
        else if (token.type == Token_Type::BOOLEAN_LITERAL) {
            result.type = Ast_Interpreter_Value_Type::BOOLEAN;
            result.bool_value = token.attribute.bool_value;
        }
        else {
            panic("I dont think it is possible to ever get here!\n");
        }
        return result;
    }
    else if (expression->type == ExpressionType::VARIABLE_READ)
    {
        Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, expression->variable_name_id);
        if (var == 0) {
            logg("Expression variable %s not defined!\n", lexer_result_identifer_to_string(interpreter->lexer, expression->variable_name_id).characters);
            return result;
        }
        return var->value;
    }
    else if (expression->type == ExpressionType::OP_EQUAL ||
        expression->type == ExpressionType::OP_NOT_EQUAL ||
        expression->type == ExpressionType::OP_LESS_EQUAL ||
        expression->type == ExpressionType::OP_LESS_THAN ||
        expression->type == ExpressionType::OP_GREATER_EQUAL ||
        expression->type == ExpressionType::OP_GREATER_THAN)
    {
        Ast_Interpreter_Value left_operand = ast_interpreter_evaluate_expression(interpreter, expression->left);
        Ast_Interpreter_Value right_operand = ast_interpreter_evaluate_expression(interpreter, expression->right);
        Ast_Interpreter_Value& l = left_operand;
        Ast_Interpreter_Value& r = right_operand;
        if (left_operand.type != right_operand.type) { // Implicit casting would happen here
            return result;
        }

        result.type = Ast_Interpreter_Value_Type::BOOLEAN;
        if (left_operand.type == Ast_Interpreter_Value_Type::FLOAT)
        {
            switch (expression->type)
            {
            case ExpressionType::OP_EQUAL: result.bool_value = l.float_value == r.float_value; break;
            case ExpressionType::OP_NOT_EQUAL: result.bool_value = l.float_value != r.float_value; break;
            case ExpressionType::OP_LESS_EQUAL: result.bool_value = l.float_value <= r.float_value; break;
            case ExpressionType::OP_LESS_THAN: result.bool_value = l.float_value < r.float_value; break;
            case ExpressionType::OP_GREATER_EQUAL: result.bool_value = l.float_value >= r.float_value; break;
            case ExpressionType::OP_GREATER_THAN: result.bool_value = l.float_value > r.float_value; break;
            }
        }
        else if (left_operand.type == Ast_Interpreter_Value_Type::INTEGER)
        {
            switch (expression->type)
            {
            case ExpressionType::OP_EQUAL: result.bool_value = l.int_value == r.int_value; break;
            case ExpressionType::OP_NOT_EQUAL: result.bool_value = l.int_value != r.int_value; break;
            case ExpressionType::OP_LESS_EQUAL: result.bool_value = l.int_value <= r.int_value; break;
            case ExpressionType::OP_LESS_THAN: result.bool_value = l.int_value < r.int_value; break;
            case ExpressionType::OP_GREATER_EQUAL: result.bool_value = l.int_value >= r.int_value; break;
            case ExpressionType::OP_GREATER_THAN: result.bool_value = l.int_value > r.int_value; break;
            }
        }
        else if (left_operand.type == Ast_Interpreter_Value_Type::BOOLEAN) {
            switch (expression->type)
            {
            case ExpressionType::OP_EQUAL: result.bool_value = l.bool_value == r.bool_value; break;
            case ExpressionType::OP_NOT_EQUAL: result.bool_value = l.bool_value != r.bool_value; break;
            default: {
                logg("Cannot do comparisions on booleans!");
                result.type = Ast_Interpreter_Value_Type::ERROR_VAL;
                return result;
            }
            }
        }
        return result;
    }
    else if (expression->type == ExpressionType::OP_ADD ||
        expression->type == ExpressionType::OP_SUBTRACT ||
        expression->type == ExpressionType::OP_MODULO ||
        expression->type == ExpressionType::OP_MULTIPLY ||
        expression->type == ExpressionType::OP_DIVIDE)
    {
        Ast_Interpreter_Value left_operand = ast_interpreter_evaluate_expression(interpreter, expression->left);
        Ast_Interpreter_Value right_operand = ast_interpreter_evaluate_expression(interpreter, expression->right);
        Ast_Interpreter_Value& l = left_operand;
        Ast_Interpreter_Value& r = right_operand;
        if (left_operand.type != right_operand.type) { // Implicit casting would happen here
            return result;
        }
        if (left_operand.type == Ast_Interpreter_Value_Type::FLOAT)
        {
            result.type = Ast_Interpreter_Value_Type::FLOAT;
            switch (expression->type)
            {
            case ExpressionType::OP_ADD: result.float_value = l.float_value + r.float_value; break;
            case ExpressionType::OP_SUBTRACT: result.float_value = l.float_value - r.float_value; break;
            case ExpressionType::OP_MULTIPLY: result.float_value = l.float_value * r.float_value; break;
            case ExpressionType::OP_DIVIDE: result.float_value = l.float_value / r.float_value; break;
            case ExpressionType::OP_MODULO: {
                logg("Float modulo float not supported!\n");
                result.type = Ast_Interpreter_Value_Type::ERROR_VAL;
                break;
            }
            }
        }
        else if (left_operand.type == Ast_Interpreter_Value_Type::INTEGER)
        {
            result.type = Ast_Interpreter_Value_Type::INTEGER;
            switch (expression->type)
            {
            case ExpressionType::OP_ADD: result.int_value = l.int_value + r.int_value; break;
            case ExpressionType::OP_SUBTRACT: result.int_value = l.int_value - r.int_value; break;
            case ExpressionType::OP_MULTIPLY: result.int_value = l.int_value * r.int_value; break;
            case ExpressionType::OP_MODULO: result.int_value = l.int_value % r.int_value; break;
            case ExpressionType::OP_DIVIDE: {
                if (r.int_value == 0) {
                    logg("Integer Division by zero!\n");
                    result.type = Ast_Interpreter_Value_Type::ERROR_VAL;
                    break;
                }
                result.int_value = l.int_value / r.int_value; break;
            }
            }
        }
        else if (left_operand.type == Ast_Interpreter_Value_Type::BOOLEAN) {
            result.type = Ast_Interpreter_Value_Type::ERROR_VAL;
        }
        return result;
    }
    else if (expression->type == ExpressionType::OP_BOOLEAN_AND ||
        expression->type == ExpressionType::OP_BOOLEAN_OR)
    {
        Ast_Interpreter_Value left_operand = ast_interpreter_evaluate_expression(interpreter, expression->left);
        Ast_Interpreter_Value right_operand = ast_interpreter_evaluate_expression(interpreter, expression->right);
        if (left_operand.type != Ast_Interpreter_Value_Type::BOOLEAN ||
            right_operand.type != Ast_Interpreter_Value_Type::BOOLEAN) {
            logg("Left an right of Logic-Operator (&& or ||) must be boolean values: left operand type: %s, right operand type:  %s\n",
                ast_interpreter_value_type_to_string(left_operand.type).characters,
                ast_interpreter_value_type_to_string(right_operand.type).characters);
            return result;
        }

        result.type = Ast_Interpreter_Value_Type::BOOLEAN;
        switch (expression->type) {
        case ExpressionType::OP_BOOLEAN_AND: result.bool_value = left_operand.bool_value && right_operand.bool_value; break;
        case ExpressionType::OP_BOOLEAN_OR: result.bool_value = left_operand.bool_value || right_operand.bool_value; break;
        }
        return result;
    }
    else if (expression->type == ExpressionType::OP_LOGICAL_NOT) {
        Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, expression->left);
        if (val.type != Ast_Interpreter_Value_Type::BOOLEAN) {
            logg("Logical not only works on boolean value!\n");
            return result;
        }
        result.type = Ast_Interpreter_Value_Type::BOOLEAN;
        result.bool_value = !val.bool_value;
        return result;
    }
    else if (expression->type == ExpressionType::OP_NEGATE) {
        Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, expression->left);
        if (val.type == Ast_Interpreter_Value_Type::BOOLEAN) {
            logg("Negate does not work on boolean values");
            return result;
        }
        if (val.type == Ast_Interpreter_Value_Type::FLOAT) {
            result.type = val.type;
            result.float_value = -val.float_value;
        }
        if (val.type == Ast_Interpreter_Value_Type::INTEGER) {
            result.type = val.type;
            result.int_value = -val.int_value;
        }
        return result;
    }

    logg("Expression type invalid!\n");
    return result;
}

struct Ast_Interpreter_Statement_Result
{
    bool is_return;
    Ast_Interpreter_Value return_value;
};

Ast_Interpreter_Statement_Result ast_interpreter_execute_statement(Ast_Interpreter* interpreter, Ast_Node_Statement* statement, LexerResult* lexer);
Ast_Interpreter_Statement_Result ast_interpreter_execute_statment_block(Ast_Interpreter* interpreter, Ast_Node_Statement_Block* block, LexerResult* lexer)
{
    ast_interpreter_begin_new_scope(interpreter);
    for (int i = 0; i < block->statements.size; i++) {
        Ast_Interpreter_Statement_Result result = ast_interpreter_execute_statement(interpreter, &block->statements[i], lexer);
        if (result.is_return) return result;
    }
    ast_interpreter_exit_scope(interpreter);
    Ast_Interpreter_Statement_Result result;
    result.is_return = false;
    return result;
}

Ast_Interpreter_Statement_Result ast_interpreter_execute_statement(Ast_Interpreter* interpreter, Ast_Node_Statement* statement, LexerResult* lexer)
{
    Ast_Interpreter_Statement_Result result;
    result.is_return = false;

    if (statement->type == StatementType::RETURN_STATEMENT) {
        result.is_return = true;
        result.return_value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        return result;
    }
    else if (statement->type == StatementType::IF_BLOCK) {
        Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        if (val.type != Ast_Interpreter_Value_Type::BOOLEAN) {
            logg("If expression is not a boolean!\n");
            return result;
        }
        if (val.bool_value) {
            return ast_interpreter_execute_statment_block(interpreter, &statement->if_statements, lexer);
        }
    }
    else if (statement->type == StatementType::IF_ELSE_BLOCK) {
        Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        if (val.type != Ast_Interpreter_Value_Type::BOOLEAN) {
            logg("If expression is not a boolean!\n");
            return result;
        }
        if (val.bool_value) {
            return ast_interpreter_execute_statment_block(interpreter, &statement->if_statements, lexer);
        }
        else {
            return ast_interpreter_execute_statment_block(interpreter, &statement->else_statements, lexer);
        }
    }
    else if (statement->type == StatementType::VARIABLE_DEFINITION) {
        if (statement->variable_type_id == interpreter->int_token_index) {
            ast_interpreter_define_variable(interpreter, Ast_Interpreter_Value_Type::INTEGER, statement->variable_name_id);
        }
        else if (statement->variable_type_id == interpreter->float_token_index) {
            ast_interpreter_define_variable(interpreter, Ast_Interpreter_Value_Type::FLOAT, statement->variable_name_id);
        }
        else if (statement->variable_type_id == interpreter->bool_token_index) {
            ast_interpreter_define_variable(interpreter, Ast_Interpreter_Value_Type::BOOLEAN, statement->variable_name_id);
        }
        else {
            logg("Type-Error: %s is not a valid type\n", lexer_result_identifer_to_string(lexer, statement->variable_type_id).characters);
            return result;
        }
    }
    else if (statement->type == StatementType::VARIABLE_ASSIGNMENT)
    {
        Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, statement->variable_name_id);
        if (var == 0) {
            logg("Variable assignment statement variable %s not defined!\n", lexer_result_identifer_to_string(lexer, statement->variable_name_id).characters);
            return result;
        }
        Ast_Interpreter_Value value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        if (value.type != var->value.type) {
            logg("Variable assignment failed, variable type does not match expression type:\n %s = %s\n",
                ast_interpreter_value_type_to_string(var->value.type).characters,
                ast_interpreter_value_type_to_string(value.type).characters);
            return result;
        }
        var->value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
    }
    else if (statement->type == StatementType::VARIABLE_DEFINE_ASSIGN) // x : int = 5
    {
        Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, statement->variable_name_id);
        if (var != 0) {
            logg("Variable %s already defined!",
                lexer_result_identifer_to_string(lexer, statement->variable_name_id).characters);
            return result;
        }

        Ast_Interpreter_Value value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        Ast_Interpreter_Value_Type::ENUM var_type;
        if (statement->variable_type_id == interpreter->int_token_index) var_type = Ast_Interpreter_Value_Type::INTEGER;
        else if (statement->variable_type_id == interpreter->float_token_index) var_type = Ast_Interpreter_Value_Type::FLOAT;
        else if (statement->variable_type_id == interpreter->bool_token_index) var_type = Ast_Interpreter_Value_Type::BOOLEAN;
        else {
            logg("Type-Error: %s is not a valid type\n", lexer_result_identifer_to_string(lexer, statement->variable_type_id).characters);
            return result;
        }

        if (var_type != value.type) {
            logg("Types not compatible, var type: ", lexer_result_identifer_to_string(lexer, statement->variable_type_id).characters);
            return result;
        }
        ast_interpreter_define_variable(interpreter, var_type, statement->variable_name_id);
        var = ast_interpreter_find_variable(interpreter, statement->variable_name_id);
        var->value = value;
    }
    else if (statement->type == StatementType::VARIABLE_DEFINE_INFER)
    {
        Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, statement->variable_name_id);
        if (var != 0) {
            logg("Variable %s already defined!",
                lexer_result_identifer_to_string(lexer, statement->variable_name_id).characters);
            return result;
        }

        Ast_Interpreter_Value value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        ast_interpreter_define_variable(interpreter, value.type, statement->variable_name_id);
        var = ast_interpreter_find_variable(interpreter, statement->variable_name_id);
        var->value = value;
    }
    else if (statement->type == StatementType::STATEMENT_BLOCK) {
        return ast_interpreter_execute_statment_block(interpreter, &statement->statements, lexer);
    }

    return result;
}

Ast_Interpreter_Value ast_interpreter_execute_main(Ast_Node_Root* root, LexerResult* lexer)
{
    Ast_Interpreter interpreter = ast_interpreter_create(root, lexer);
    SCOPE_EXIT(ast_interpreter_destroy(&interpreter));
    Ast_Interpreter_Value error_value;
    error_value.type = Ast_Interpreter_Value_Type::ERROR_VAL;

    // Find main
    Ast_Node_Function* main = 0;
    {
        int* main_identifer = hashtable_find_element(&lexer->identifier_index_lookup_table, string_create_static("main"));
        if (main_identifer == 0) {
            logg("Main not defined\n");
            return error_value;
        }
        for (int i = 0; i < root->functions.size; i++) {
            if (root->functions[i].function_name_id == *main_identifer) {
                main = &root->functions[i];
            }
        }
        if (main == 0) {
            logg("Main function not found\n");
            return error_value;
        }
    }

    // Find token indices for types
    interpreter.int_token_index = lexer_result_add_identifier_by_string(lexer, string_create_static("int"));
    interpreter.bool_token_index = lexer_result_add_identifier_by_string(lexer, string_create_static("bool"));
    interpreter.float_token_index = lexer_result_add_identifier_by_string(lexer, string_create_static("float"));

    Ast_Interpreter_Statement_Result main_result = ast_interpreter_execute_statment_block(&interpreter, &main->body, lexer);
    if (!main_result.is_return) {
        logg("No return statement found!\n");
        return error_value;
    }

    return main_result.return_value;
}

void ast_interpreter_value_append_to_string(Ast_Interpreter_Value value, String* string)
{
    switch (value.type)
    {
    case Ast_Interpreter_Value_Type::BOOLEAN:
        string_append_formated(string, "BOOL: %s ", value.bool_value ? "true" : "false"); break;
    case Ast_Interpreter_Value_Type::INTEGER:
        string_append_formated(string, "INT: %d ", value.int_value); break;
    case Ast_Interpreter_Value_Type::FLOAT:
        string_append_formated(string, "FLOAT: %f ", value.float_value); break;
    case Ast_Interpreter_Value_Type::ERROR_VAL:
        string_append_formated(string, "ERROR-Type "); break;
    default:
        string_append_formated(string, "SHOULD_NOT_HAPPEN.EXE"); break;
    }
    return;
    return;
}

String ast_interpreter_value_type_to_string(Ast_Interpreter_Value_Type::ENUM type)
{
    switch (type)
    {
    case Ast_Interpreter_Value_Type::BOOLEAN:
        return string_create_static("BOOL");
    case Ast_Interpreter_Value_Type::INTEGER:
        return string_create_static("INT");
    case Ast_Interpreter_Value_Type::FLOAT:
        return string_create_static("FLOAT");
    case Ast_Interpreter_Value_Type::ERROR_VAL:
        return string_create_static("ERROR_TYPE");
    }
    return string_create_static("INVALID_VALUE_TYPE_ENUM");
}
