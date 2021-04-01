#include "compiler.hpp"

#include "../../datastructures/string.hpp"

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

bool code_skip_comments(String* code, int* index, int* character_pos, int* line_number)
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

void code_skip_whitespace_and_comments(String* code, int* index, int* character_pos, int* line_number)
{
    while (*index < code->size && string_contains_character(string_create_static("\t \r\n/"), code->characters[*index]))
    {
        if (code_skip_comments(code, index, character_pos, line_number)) continue;
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

int lexer_add_or_find_identifier_by_string(Lexer* lexer, String identifier) 
{
    // Identifier is a keyword
    int* identifier_id = hashtable_find_element(&lexer->identifier_index_lookup_table, identifier);
    if (identifier_id != 0) {
        return *identifier_id;
    }
    else {
        String identifier_string_copy = string_create(identifier.characters);
        dynamic_array_push_back(&lexer->identifiers, identifier_string_copy);
        int index = lexer->identifiers.size - 1;
        hashtable_insert_element(&lexer->identifier_index_lookup_table, identifier_string_copy, index);
        return index;
    }
}

Lexer lexer_parse_string(String* code)
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
        code_skip_whitespace_and_comments(code, &index, &character_pos, &line_number);
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

    Lexer result;
    result.has_errors = has_errors;
    result.identifiers = identifiers;
    result.tokens = tokens;
    result.identifier_index_lookup_table = identifier_index_lookup_table;
    return result;
}

void lexer_destroy(Lexer* lexer)
{
    for (int i = 0; i < lexer->identifiers.size; i++) {
        string_destroy(&lexer->identifiers[i]);
    }
    dynamic_array_destroy(&lexer->identifiers);
    dynamic_array_destroy(&lexer->tokens);
    hashtable_destroy(&lexer->identifier_index_lookup_table);
}

String lexer_identifer_to_string(Lexer* lexer, int index) {
    return lexer->identifiers[index];
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

void lexer_print(Lexer* lexer)
{
    String msg = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&msg));
    string_append_formated(&msg, "Tokens: \n");
    for (int i = 0; i < lexer->tokens.size; i++) {
        Token token = lexer->tokens[i];
        string_append_formated(&msg, "\t %s (Line %d, Pos %d, Length %d)",
            tokentype_to_string(token.type), token.line_number, token.character_position, token.lexem_length);
        if (token.type == Token_Type::IDENTIFIER) {
            string_append_formated(&msg, " = %s", lexer->identifiers.data[token.attribute.identifier_number].characters);
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
    for (int i = parser->index; i < parser->lexer->tokens.size; i++) {
        if (parser->lexer->tokens[i].type == type) {
            return i;
        }
    }
    return parser->lexer->tokens.size;
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
    if (parser->index >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type) {
        return true;
    }
    return false;
}

bool parser_test_next_2_tokens(Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2)
{
    if (parser->index + 1 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 && parser->lexer->tokens[parser->index + 1].type == type2) {
        return true;
    }
    return false;
}

bool parser_test_next_3_tokens(Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2, Token_Type::ENUM type3)
{
    if (parser->index + 2 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 &&
        parser->lexer->tokens[parser->index + 1].type == type2 &&
        parser->lexer->tokens[parser->index + 2].type == type3) {
        return true;
    }
    return false;
}

bool parser_test_next_4_tokens(Parser* parser, Token_Type::ENUM type1, Token_Type::ENUM type2, Token_Type::ENUM type3, Token_Type::ENUM type4)
{
    if (parser->index + 3 >= parser->lexer->tokens.size) {
        return false;
    }
    if (parser->lexer->tokens[parser->index].type == type1 &&
        parser->lexer->tokens[parser->index + 1].type == type2 &&
        parser->lexer->tokens[parser->index + 2].type == type3 &&
        parser->lexer->tokens[parser->index + 3].type == type4) {
        return true;
    }
    return false;
}





/*
    AST
*/
void ast_node_expression_destroy(Ast_Node_Expression* expression)
{
    if (expression->free_symbol_table_on_destroy) {
        symbol_table_destroy(expression->symbol_table);
    }
    if (expression->type == ExpressionType::OP_LOGICAL_NOT ||
        expression->type == ExpressionType::OP_NEGATE)
    {
        if (expression->left != 0) {
            ast_node_expression_destroy(expression->left);
            delete expression->left;
        }
    }
    // Binary operations
    if (expression->type == ExpressionType::OP_ADD ||
        expression->type == ExpressionType::OP_SUBTRACT ||
        expression->type == ExpressionType::OP_DIVIDE ||
        expression->type == ExpressionType::OP_MULTIPLY ||
        expression->type == ExpressionType::OP_MODULO ||
        expression->type == ExpressionType::OP_BOOLEAN_AND ||
        expression->type == ExpressionType::OP_BOOLEAN_OR ||
        expression->type == ExpressionType::OP_EQUAL ||
        expression->type == ExpressionType::OP_NOT_EQUAL ||
        expression->type == ExpressionType::OP_GREATER_EQUAL ||
        expression->type == ExpressionType::OP_GREATER_THAN ||
        expression->type == ExpressionType::OP_LESS_EQUAL ||
        expression->type == ExpressionType::OP_LESS_THAN) 
    {
        if (expression->left != 0) {
            ast_node_expression_destroy(expression->left);
            delete expression->left;
        }
        if (expression->right != 0) {
            ast_node_expression_destroy(expression->right);
            delete expression->right;
        }
    }

    if (expression->type == ExpressionType::FUNCTION_CALL) {
        for (int i = 0; i < expression->arguments.size; i++) {
            ast_node_expression_destroy(&expression->arguments[i]);
        }
        dynamic_array_destroy(&expression->arguments);
    }
}

void ast_node_statement_block_destroy(Ast_Node_Statement_Block* block);
void ast_node_statement_destroy(Ast_Node_Statement* statement) 
{
    if (statement->free_symbol_table_on_destroy) {
        symbol_table_destroy(statement->symbol_table);
    }
    if (statement->type == StatementType::VARIABLE_ASSIGNMENT ||
        statement->type == StatementType::IF_BLOCK ||
        statement->type == StatementType::IF_ELSE_BLOCK ||
        statement->type == StatementType::WHILE ||
        statement->type == StatementType::RETURN_STATEMENT ||
        statement->type == StatementType::EXPRESSION ||
        statement->type == StatementType::VARIABLE_DEFINE_ASSIGN ||
        statement->type == StatementType::VARIABLE_DEFINE_INFER) 
    {
        ast_node_expression_destroy(&statement->expression);
    }
    if (statement->type == StatementType::IF_BLOCK ||
        statement->type == StatementType::IF_ELSE_BLOCK ||
        statement->type == StatementType::STATEMENT_BLOCK ||
        statement->type == StatementType::WHILE) 
    {
        ast_node_statement_block_destroy(&statement->statements);
    }
    if (statement->type == StatementType::IF_ELSE_BLOCK) {
        ast_node_statement_block_destroy(&statement->else_statements);
    }
}

void ast_node_statement_block_destroy(Ast_Node_Statement_Block* block) {
    if (block->free_symbol_table_on_destroy) {
        symbol_table_destroy(block->symbol_table);
    }
    for (int i = 0; i < block->statements.size; i++) {
        Ast_Node_Statement* statement = &block->statements.data[i];
        ast_node_statement_destroy(statement);
    }
    dynamic_array_destroy(&block->statements);
}

void ast_node_function_destroy(Ast_Node_Function* function)
{
    if (function->free_symbol_table_on_destroy) {
        symbol_table_destroy(function->symbol_table);
    }
    dynamic_array_destroy(&function->parameters);
    ast_node_statement_block_destroy(&function->body);
}

void ast_node_root_destroy(Ast_Node_Root* root)
{
    if (root->free_symbol_table_on_destroy) {
        symbol_table_destroy(root->symbol_table);
    }
    for (int i = 0; i < root->functions.size; i++)
    {
        Ast_Node_Function* function = &root->functions.data[i];
        ast_node_function_destroy(function);
    }
    dynamic_array_destroy(&root->functions);
}

void ast_node_expression_append_to_string(String* string, Ast_Node_Expression* expression, Lexer* lexer)
{
    if (expression->type == ExpressionType::OP_ADD) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " + ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_NEGATE) {
        string_append(string, "-");
        ast_node_expression_append_to_string(string, expression->left, lexer);
    }
    if (expression->type == ExpressionType::OP_LOGICAL_NOT) {
        string_append(string, "!");
        ast_node_expression_append_to_string(string, expression->left, lexer);
    }
    if (expression->type == ExpressionType::OP_MULTIPLY) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " * ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_SUBTRACT) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " - ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_DIVIDE) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " / ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_MODULO) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " % ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_BOOLEAN_AND) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " && ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_BOOLEAN_OR) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " || ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_EQUAL) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " || ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_NOT_EQUAL) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " || ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_LESS_THAN) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " < ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_LESS_EQUAL) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " <= ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_GREATER_THAN) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " > ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    if (expression->type == ExpressionType::OP_GREATER_EQUAL) {
        string_append(string, "(");
        ast_node_expression_append_to_string(string, expression->left, lexer);
        string_append(string, " >= ");
        ast_node_expression_append_to_string(string, expression->right, lexer);
        string_append(string, ")");
    }
    else if (expression->type == ExpressionType::LITERAL) {
        Token& t = lexer->tokens[expression->literal_token_index];
        if (t.type == Token_Type::INTEGER_LITERAL) {
            string_append_formated(string, "%d", lexer->tokens[expression->literal_token_index].attribute.integer_value);
        }
        else if (t.type == Token_Type::FLOAT_LITERAL) {
            string_append_formated(string, "%f", lexer->tokens[expression->literal_token_index].attribute.float_value);
        }
        else if (t.type == Token_Type::BOOLEAN_LITERAL) {
            string_append_formated(string, "%s",
                lexer->tokens[expression->literal_token_index].attribute.bool_value ? "true" : "false");
        }
    }
    else if (expression->type == ExpressionType::VARIABLE_READ) {
        string_append_formated(string, "%s", lexer->identifiers[expression->variable_name_id].characters);
    }
    else if (expression->type == ExpressionType::FUNCTION_CALL) {
        string_append_formated(string, "%s(", lexer->identifiers[expression->variable_name_id].characters);
        for (int i = 0; i < expression->arguments.size; i++) {
            ast_node_expression_append_to_string(string, &expression->arguments[i], lexer);
            if (i != expression->arguments.size-1)
                string_append_formated(string, ", ");
        }
        string_append_formated(string, ")");
    }
}

void ast_node_statement_append_to_string(String* string, Ast_Node_Statement* statement, Lexer* lexer, int indentation_level);
void ast_node_statement_block_append_to_string(String* string, Ast_Node_Statement_Block* block, Lexer* lexer, int current_indentation_lvl)
{
    for (int i = 0; i < current_indentation_lvl; i++) {
        string_append_formated(string, "    ");
    }
   string_append_formated(string, "{\n");
   for (int i = 0; i < block->statements.size; i++) {
       Ast_Node_Statement* statement = &block->statements.data[i];
       for (int i = 0; i < current_indentation_lvl+1; i++) {
           string_append_formated(string, "    ");
       }
       ast_node_statement_append_to_string(string, statement, lexer, current_indentation_lvl+1);
       string_append_formated(string, "\n");
   }
   for (int i = 0; i < current_indentation_lvl; i++) {
       string_append_formated(string, "    ");
   }
   string_append_formated(string, "}");
}

void ast_node_statement_append_to_string(String* string, Ast_Node_Statement* statement, Lexer* lexer, int indentation_level)
{
    if (statement->type == StatementType::VARIABLE_DEFINITION) {
        string_append_formated(string, "%s : %s;",
            lexer->identifiers[statement->variable_name_id].characters,
            lexer->identifiers[statement->variable_type_id].characters);
    }
    else if (statement->type == StatementType::VARIABLE_ASSIGNMENT) {
        string_append_formated(string, "%s = ", lexer->identifiers[statement->variable_name_id].characters);
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append(string, ";");
    }
    else if (statement->type == StatementType::RETURN_STATEMENT) {
        string_append_formated(string, "return ");
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append_formated(string, ";");
    }
    else if (statement->type == StatementType::VARIABLE_DEFINE_ASSIGN) {
        string_append_formated(string, "%s : %s = ",
            lexer->identifiers[statement->variable_name_id].characters,
            lexer->identifiers[statement->variable_type_id].characters);
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append_formated(string, ";");
    }
    else if (statement->type == StatementType::VARIABLE_DEFINE_INFER) {
        string_append_formated(string, "%s := ",
            lexer->identifiers[statement->variable_name_id].characters);
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append_formated(string, ";");
    }
    else if (statement->type == StatementType::EXPRESSION) {
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append_formated(string, ";");
    }
    else if (statement->type == StatementType::CONTINUE) {
        string_append_formated(string, "continue;");
    }
    else if (statement->type == StatementType::BREAK) {
        string_append_formated(string, "break;");
    }
    else if (statement->type == StatementType::STATEMENT_BLOCK) {
        ast_node_statement_block_append_to_string(string, &statement->statements, lexer, indentation_level);
    }
    else if (statement->type == StatementType::WHILE)
    {
        string_append_formated(string, "while ");
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append_formated(string, "\n");
        ast_node_statement_block_append_to_string(string, &statement->statements, lexer, indentation_level);
    }
    else if (statement->type == StatementType::IF_BLOCK || statement->type == StatementType::IF_ELSE_BLOCK) 
    {
        // Print if block
        string_append_formated(string, "if ");
        ast_node_expression_append_to_string(string, &statement->expression, lexer);
        string_append_formated(string, "\n");
        ast_node_statement_block_append_to_string(string, &statement->statements, lexer, indentation_level);

        if (statement->type == StatementType::IF_ELSE_BLOCK) {
            string_append_formated(string, "else\n");
            ast_node_statement_block_append_to_string(string, &statement->statements, lexer, indentation_level);
        }
    }
}

void ast_node_function_append_to_string(String* string, Ast_Node_Function* function, Lexer* lexer)
{
    string_append_formated(string, "%s :: (", lexer->identifiers[function->function_name_id].characters);
    for (int i = 0; i < function->parameters.size; i++) {
        Parameter* param = &(function->parameters.data[i]);
        string_append_formated(string, "%s : %s, ",
            lexer->identifiers[param->name_id].characters,
            lexer->identifiers[param->type_id].characters);
    }
    string_append_formated(string, ") -> %s\n", lexer->identifiers[function->return_type_id].characters);
    ast_node_statement_block_append_to_string(string, &function->body, lexer, 0);
}

void ast_node_root_append_to_string(String* string, Ast_Node_Root* root, Lexer* lexer)
{
    string_append_formated(string, "\nRoot: (Function count #%d)\n", root->functions.size);
    for (int i = 0; i < root->functions.size; i++)
    {
        Ast_Node_Function* function = &(root->functions.data[i]);
        ast_node_function_append_to_string(string, function, lexer);
        string_append_formated(string, "\n");
    }
}



/*
    PARSER
*/
bool parser_parse_expression(Parser* parser, Ast_Node_Expression* expression);

bool parser_parse_expression_single_value(Parser* parser, Ast_Node_Expression* expression)
{
    int rewind_point = parser->index;
    expression->left = 0;
    expression->right = 0;
    if (parser_test_next_token(parser, Token_Type::IDENTIFIER))
    {
        expression->variable_name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        parser->index++;
        if (parser_test_next_token(parser, Token_Type::OPEN_PARENTHESIS)) 
        { 
            parser->index++;
            expression->type = ExpressionType::FUNCTION_CALL;
            expression->arguments = dynamic_array_create_empty<Ast_Node_Expression>(4);
            while (true)
            {
                Ast_Node_Expression argument;
                if (!parser_parse_expression(parser, &argument)) {
                    break;
                }
                dynamic_array_push_back(&expression->arguments, argument);
                if (!parser_test_next_token(parser, Token_Type::COMMA)) {
                    break;
                }
                parser->index++;
            }
            // Here we could check last token for , and error if this is the case, but it does not really matter
            if (parser_test_next_token(parser, Token_Type::CLOSED_PARENTHESIS)) {
                parser->index++;
                return true;
            }
            // Error, no end of function
            for (int i = 0; i < expression->arguments.size; i++) {
                ast_node_expression_destroy(&expression->arguments[i]);
            }
            dynamic_array_destroy(&expression->arguments);
            parser->index = rewind_point;
            return false;
        }
        else {
            expression->type = ExpressionType::VARIABLE_READ;
        }
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
            "Error, could not parse single expression, does not start with constant or identifier\n", parser->index, parser->index + 1);
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
    expression->symbol_table = 0;
    expression->free_symbol_table_on_destroy = false;
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
    statement->symbol_table = 0;
    statement->free_symbol_table_on_destroy = false;
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
        if (!parser_parse_statment_block_or_single_statement(parser, &statement->statements)) {
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

    if (!valid_statement && parser_test_next_token(parser, Token_Type::WHILE))
    {
        parser->index++;
        if (!parser_parse_expression(parser, &statement->expression)) {
            parser->index = rewind_point;
            return false;
        }
        if (!parser_parse_statment_block_or_single_statement(parser, &statement->statements)) {
            parser->index = rewind_point;
            return false;
        }
        statement->type = StatementType::WHILE;
        return true;
    }

    if (!valid_statement && parser_test_next_token(parser, Token_Type::BREAK))
    {
        statement->type = StatementType::BREAK;
        parser->index++;
        valid_statement = true;
    }

    if (!valid_statement && parser_test_next_token(parser, Token_Type::CONTINUE))
    {
        statement->type = StatementType::CONTINUE;
        parser->index++;
        valid_statement = true;
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
        statement->variable_name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        statement->variable_type_id = parser->lexer->tokens[parser->index + 2].attribute.identifier_number;
        parser->index += 3; // ! not 4, since the ; parsing is done by the caller of this function
        valid_statement = true;
    }

    if (!valid_statement && parser_test_next_4_tokens(parser,
        Token_Type::IDENTIFIER, Token_Type::COLON, Token_Type::IDENTIFIER, Token_Type::OP_ASSIGNMENT)) // Variable define-assign 'x : int = ...'
    {
        statement->type = StatementType::VARIABLE_DEFINE_ASSIGN;
        statement->variable_name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        statement->variable_type_id = parser->lexer->tokens[parser->index + 2].attribute.identifier_number;
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
        statement->variable_name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
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
        statement->variable_name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        parser->index += 2;

        // Parse expression
        if (!parser_parse_expression(parser, &statement->expression)) {
            parser->index = rewind_point;
            return false;
        }
        valid_statement = true;
    }

    if (!valid_statement)
    {
        if (parser_parse_expression(parser, &statement->expression)) {
            statement->type = StatementType::EXPRESSION;
            valid_statement = true;
        }
    }

    if (!valid_statement) {
        return false;
    }
    if (parser_test_next_token(parser, Token_Type::SEMICOLON)) {
        parser->index++;
        return true;
    }
    else {
        ast_node_statement_destroy(statement);
        parser->index = rewind_point;
        return false;
    }
}

// Returns true if there is a statement block, false if not
bool parser_parse_statement_block(Parser* parser, Ast_Node_Statement_Block* block)
{
    block->symbol_table = 0;
    block->free_symbol_table_on_destroy = false;
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
    while (parser->index < parser->lexer->tokens.size)
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
            if (next_semicolon >= parser->lexer->tokens.size || next_braces >= parser->lexer->tokens.size) {
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
    parser_log_unresolvable_error(parser, "Scope block does not end with }\n", scope_start, parser->lexer->tokens.size - 1);
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
    function->symbol_table = 0;
    function->free_symbol_table_on_destroy = false;
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
    function->function_name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
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
        param.name_id = parser->lexer->tokens[parser->index].attribute.identifier_number;
        param.type_id = parser->lexer->tokens[parser->index + 2].attribute.identifier_number;
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
    function->return_type_id = parser->lexer->tokens[parser->index + 1].attribute.identifier_number;
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
    root->symbol_table = 0;
    root->free_symbol_table_on_destroy = false;

    Ast_Node_Function function;
    while (true)
    {
        if (parser_parse_function(parser, &function)) {
            dynamic_array_push_back(&root->functions, function);
        }
        else if (parser->index >= parser->lexer->tokens.size) {
            break;
        }
        else {
            // Skip to next token in next line, then try parsing again
            int next_line_token = parser->index;
            while (next_line_token < parser->lexer->tokens.size &&
                parser->lexer->tokens[next_line_token].line_number == parser->lexer->tokens[parser->index].line_number)
            {
                next_line_token++;
            }
            if (next_line_token >= parser->lexer->tokens.size) {
                parser_log_unresolvable_error(parser, "Could not parse last function in file!\n", 
                    parser->index, parser->lexer->tokens.size - 1);
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

Parser parser_parse(Lexer* lexer)
{
    Parser parser;
    parser.index = 0;
    parser.lexer = lexer;
    parser.intermediate_errors = dynamic_array_create_empty<ParserError>(16);
    parser.unresolved_errors = dynamic_array_create_empty<ParserError>(16);
    parser.semantic_analysis_errors = dynamic_array_create_empty<const char*>(16);

    // Parse root
    if (!parser_parse_root(&parser, &parser.root)) {
        logg("Dont quite know what to do herer lol\n");
    }

    // Do semantic checking
    parser_semantic_analysis(&parser);

    return parser;
}

void parser_destroy(Parser* parser)
{
    dynamic_array_destroy(&parser->intermediate_errors);
    dynamic_array_destroy(&parser->unresolved_errors);
    dynamic_array_destroy(&parser->semantic_analysis_errors);
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
-->
*/

struct Ast_Interpreter
{
    Ast_Node_Root* root;
    DynamicArray<Ast_Interpreter_Variable> symbol_table;
    DynamicArray<int> scope_beginnings;
    DynamicArray<int> function_scope_beginnings;
    DynamicArray<Ast_Interpreter_Value> argument_evaluation_buffer;
    Lexer* lexer;

    int int_token_index;
    int float_token_index;
    int bool_token_index;
    int print_token_index;
};

Ast_Interpreter ast_interpreter_create(Ast_Node_Root* root, Lexer* lexer)
{
    Ast_Interpreter result;
    result.root = root;
    result.lexer = lexer;
    result.argument_evaluation_buffer = dynamic_array_create_empty<Ast_Interpreter_Value>(16);
    result.symbol_table = dynamic_array_create_empty<Ast_Interpreter_Variable>(16);
    result.scope_beginnings = dynamic_array_create_empty<int>(16);
    dynamic_array_push_back(&result.scope_beginnings, 0);
    result.function_scope_beginnings = dynamic_array_create_empty<int>(16);
    dynamic_array_push_back(&result.function_scope_beginnings, 0);
    return result;
}

void ast_interpreter_destroy(Ast_Interpreter* interpreter) {
    dynamic_array_destroy(&interpreter->symbol_table);
    dynamic_array_destroy(&interpreter->scope_beginnings);
    dynamic_array_destroy(&interpreter->function_scope_beginnings);
    dynamic_array_destroy(&interpreter->argument_evaluation_buffer);
}

int ast_interpreter_find_variable_index(Ast_Interpreter* interpreter, int var_name) {
    int function_scope_beginning = interpreter->function_scope_beginnings[interpreter->function_scope_beginnings.size - 1];
    for (int i = interpreter->symbol_table.size - 1; i >= function_scope_beginning; i--) {
        if (interpreter->symbol_table[i].variable_name == var_name) {
            return i;
        }
    }
    return -1;
}

Ast_Interpreter_Variable* ast_interpreter_find_variable(Ast_Interpreter* interpreter, int var_name) {
    int i = ast_interpreter_find_variable_index(interpreter, var_name);
    if (i == -1) return 0;
    return &interpreter->symbol_table[i];
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
    dynamic_array_swap_remove(&interpreter->scope_beginnings, interpreter->scope_beginnings.size - 1);
}

void ast_interpreter_begin_new_function_scope(Ast_Interpreter* interpreter) {
    ast_interpreter_begin_new_scope(interpreter);
    dynamic_array_push_back(&interpreter->function_scope_beginnings, interpreter->symbol_table.size);
}

void ast_interpreter_end_function_scope(Ast_Interpreter* interpreter) {
    ast_interpreter_exit_scope(interpreter);
    dynamic_array_swap_remove(&interpreter->function_scope_beginnings, interpreter->function_scope_beginnings.size - 1);
}

Variable_Type::ENUM ast_interpreter_token_index_to_value_type(Ast_Interpreter* interpreter, int index)
{
    if (index == interpreter->int_token_index) return Variable_Type::INTEGER;
    if (index == interpreter->float_token_index) return Variable_Type::FLOAT;
    if (index == interpreter->bool_token_index) return Variable_Type::BOOLEAN;
    return Variable_Type::ERROR_TYPE;
}

void ast_interpreter_define_variable(Ast_Interpreter* interpreter, Variable_Type::ENUM type, int var_name)
{
    int current_scope_start = interpreter->scope_beginnings[interpreter->scope_beginnings.size - 1];
    if (ast_interpreter_find_variable_index(interpreter, var_name) >= current_scope_start) {
        logg("Variable %s already defined in this scope!", lexer_identifer_to_string(interpreter->lexer, var_name).characters);
        return;
    }

    Ast_Interpreter_Variable var;
    var.value.type = type;
    var.value.bool_value = false;
    var.value.int_value = -69;
    var.value.float_value = -69.69f;
    var.variable_name = var_name;
    dynamic_array_push_back(&interpreter->symbol_table, var);
}

Ast_Interpreter_Statement_Result ast_interpreter_execute_statment_block(Ast_Interpreter* interpreter, Ast_Node_Statement_Block* block);
Ast_Interpreter_Value ast_interpreter_evaluate_expression(Ast_Interpreter* interpreter, Ast_Node_Expression* expression)
{
    Ast_Interpreter_Value result;
    result.type = Variable_Type::ERROR_TYPE;

    if (expression->type == ExpressionType::LITERAL) {
        Token& token = interpreter->lexer->tokens[expression->literal_token_index];
        if (token.type == Token_Type::INTEGER_LITERAL) {
            result.type = Variable_Type::INTEGER;
            result.int_value = token.attribute.integer_value;
        }
        else if (token.type == Token_Type::FLOAT_LITERAL) {
            result.type = Variable_Type::FLOAT;
            result.float_value = token.attribute.float_value;
        }
        else if (token.type == Token_Type::BOOLEAN_LITERAL) {
            result.type = Variable_Type::BOOLEAN;
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
            logg("Expression variable %s not defined!\n", lexer_identifer_to_string(interpreter->lexer, expression->variable_name_id).characters);
            return result;
        }
        return var->value;
    }
    else if (expression->type == ExpressionType::FUNCTION_CALL)
    {
        Ast_Node_Function* function = 0;
        for (int i = 0; i < interpreter->root->functions.size; i++) {
            if (interpreter->root->functions[i].function_name_id == expression->variable_name_id) {
                function = &interpreter->root->functions[i];
            }
        }
        if (function == 0) 
        {
            if (expression->variable_name_id == interpreter->print_token_index) {
                String str = string_create_empty(64);
                SCOPE_EXIT(string_destroy(&str));
                string_append_formated(&str, "print: ");
                for (int i = 0; i < expression->arguments.size; i++) {
                    Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, &expression->arguments[i]);
                    ast_interpreter_value_append_to_string(val, &str);
                    string_append_formated(&str, ", ");
                }
                logg("%s\n", str.characters);
                return result;
            }
            logg("Function named %s not found!\n", lexer_identifer_to_string(interpreter->lexer, expression->variable_name_id).characters);
            return result;
        }
        if (function->parameters.size != expression->arguments.size) {
            logg("Function call does not have enough arguments!\n");
            return result;
        }
        // Evaluate arguments before making new scope, since afterwards expressions that need variable reads dont work
        dynamic_array_reset(&interpreter->argument_evaluation_buffer);
        for (int i = 0; i < expression->arguments.size; i++) {
            Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, &expression->arguments[i]);
            dynamic_array_push_back(&interpreter->argument_evaluation_buffer, val);
        }

        ast_interpreter_begin_new_function_scope(interpreter);
        // Push arguments on the stack
        bool success = true;
        for (int i = 0; i < interpreter->argument_evaluation_buffer.size; i++) {
            Ast_Interpreter_Value val = interpreter->argument_evaluation_buffer[i];
            if (val.type != ast_interpreter_token_index_to_value_type(interpreter, function->parameters[i].type_id)) {
                logg("Argument type does not match parameter type of function!\n");
                success = false;
                break;
            }
            ast_interpreter_define_variable(interpreter, val.type, function->parameters[i].name_id);
            Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, function->parameters[i].name_id);
            var->value = val;
        }
        if (success) {
            Ast_Interpreter_Statement_Result result;
            result = ast_interpreter_execute_statment_block(interpreter, &function->body);
            if (result.is_return) {
                if (result.return_value.type != ast_interpreter_token_index_to_value_type(interpreter, function->return_type_id)) {
                    logg("Return value does not match return type of function %s\n", 
                        lexer_identifer_to_string(interpreter->lexer, function->function_name_id).characters);
                }
                ast_interpreter_end_function_scope(interpreter);
                return result.return_value;
            }
        }
        ast_interpreter_end_function_scope(interpreter);
        return result;
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

        result.type = Variable_Type::BOOLEAN;
        if (left_operand.type == Variable_Type::FLOAT)
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
        else if (left_operand.type == Variable_Type::INTEGER)
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
        else if (left_operand.type == Variable_Type::BOOLEAN) {
            switch (expression->type)
            {
            case ExpressionType::OP_EQUAL: result.bool_value = l.bool_value == r.bool_value; break;
            case ExpressionType::OP_NOT_EQUAL: result.bool_value = l.bool_value != r.bool_value; break;
            default: {
                logg("Cannot do comparisions on booleans!");
                result.type = Variable_Type::ERROR_TYPE;
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
        if (left_operand.type == Variable_Type::FLOAT)
        {
            result.type = Variable_Type::FLOAT;
            switch (expression->type)
            {
            case ExpressionType::OP_ADD: result.float_value = l.float_value + r.float_value; break;
            case ExpressionType::OP_SUBTRACT: result.float_value = l.float_value - r.float_value; break;
            case ExpressionType::OP_MULTIPLY: result.float_value = l.float_value * r.float_value; break;
            case ExpressionType::OP_DIVIDE: result.float_value = l.float_value / r.float_value; break;
            case ExpressionType::OP_MODULO: {
                logg("Float modulo float not supported!\n");
                result.type = Variable_Type::ERROR_TYPE;
                break;
            }
            }
        }
        else if (left_operand.type == Variable_Type::INTEGER)
        {
            result.type = Variable_Type::INTEGER;
            switch (expression->type)
            {
            case ExpressionType::OP_ADD: result.int_value = l.int_value + r.int_value; break;
            case ExpressionType::OP_SUBTRACT: result.int_value = l.int_value - r.int_value; break;
            case ExpressionType::OP_MULTIPLY: result.int_value = l.int_value * r.int_value; break;
            case ExpressionType::OP_MODULO: result.int_value = l.int_value % r.int_value; break;
            case ExpressionType::OP_DIVIDE: {
                if (r.int_value == 0) {
                    logg("Integer Division by zero!\n");
                    result.type = Variable_Type::ERROR_TYPE;
                    break;
                }
                result.int_value = l.int_value / r.int_value; break;
            }
            }
        }
        else if (left_operand.type == Variable_Type::BOOLEAN) {
            result.type = Variable_Type::ERROR_TYPE;
        }
        return result;
    }
    else if (expression->type == ExpressionType::OP_BOOLEAN_AND ||
        expression->type == ExpressionType::OP_BOOLEAN_OR)
    {
        Ast_Interpreter_Value left_operand = ast_interpreter_evaluate_expression(interpreter, expression->left);
        Ast_Interpreter_Value right_operand = ast_interpreter_evaluate_expression(interpreter, expression->right);
        if (left_operand.type != Variable_Type::BOOLEAN ||
            right_operand.type != Variable_Type::BOOLEAN) {
            logg("Left an right of Logic-Operator (&& or ||) must be boolean values: left operand type: %s, right operand type:  %s\n",
                ast_interpreter_value_type_to_string(left_operand.type).characters,
                ast_interpreter_value_type_to_string(right_operand.type).characters);
            return result;
        }

        result.type = Variable_Type::BOOLEAN;
        switch (expression->type) {
        case ExpressionType::OP_BOOLEAN_AND: result.bool_value = left_operand.bool_value && right_operand.bool_value; break;
        case ExpressionType::OP_BOOLEAN_OR: result.bool_value = left_operand.bool_value || right_operand.bool_value; break;
        }
        return result;
    }
    else if (expression->type == ExpressionType::OP_LOGICAL_NOT) {
        Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, expression->left);
        if (val.type != Variable_Type::BOOLEAN) {
            logg("Logical not only works on boolean value!\n");
            return result;
        }
        result.type = Variable_Type::BOOLEAN;
        result.bool_value = !val.bool_value;
        return result;
    }
    else if (expression->type == ExpressionType::OP_NEGATE) {
        Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, expression->left);
        if (val.type == Variable_Type::BOOLEAN) {
            logg("Negate does not work on boolean values");
            return result;
        }
        if (val.type == Variable_Type::FLOAT) {
            result.type = val.type;
            result.float_value = -val.float_value;
        }
        if (val.type == Variable_Type::INTEGER) {
            result.type = val.type;
            result.int_value = -val.int_value;
        }
        return result;
    }

    logg("Expression type invalid!\n");
    return result;
}

Ast_Interpreter_Statement_Result ast_interpreter_result_make_empty() {
    Ast_Interpreter_Statement_Result result;
    result.is_break = false;
    result.is_continue = false;
    result.is_return = false;
    return result;
}

Ast_Interpreter_Statement_Result ast_interpreter_result_make_break() {
    Ast_Interpreter_Statement_Result result;
    result.is_break = true;
    result.is_continue = false;
    result.is_return = false;
    return result;
}

Ast_Interpreter_Statement_Result ast_interpreter_result_make_continue() {
    Ast_Interpreter_Statement_Result result;
    result.is_break = false;
    result.is_continue = true;
    result.is_return = false;
    return result;
}

Ast_Interpreter_Statement_Result ast_interpreter_result_make_return(Ast_Interpreter_Value val) {
    Ast_Interpreter_Statement_Result result;
    result.is_break = false;
    result.is_continue = false;
    result.is_return = true;
    result.return_value = val;
    return result;
}

Ast_Interpreter_Statement_Result ast_interpreter_execute_statement(Ast_Interpreter* interpreter, Ast_Node_Statement* statement);
Ast_Interpreter_Statement_Result ast_interpreter_execute_statment_block(Ast_Interpreter* interpreter, Ast_Node_Statement_Block* block)
{
    ast_interpreter_begin_new_scope(interpreter);
    for (int i = 0; i < block->statements.size; i++) {
        Ast_Interpreter_Statement_Result result = ast_interpreter_execute_statement(interpreter, &block->statements[i]);
        if (result.is_return || result.is_continue || result.is_break) {
            ast_interpreter_exit_scope(interpreter);
            return result;
        }
    }
    ast_interpreter_exit_scope(interpreter);
    return ast_interpreter_result_make_empty();
}

Ast_Interpreter_Statement_Result ast_interpreter_execute_statement(Ast_Interpreter* interpreter, Ast_Node_Statement* statement)
{
    Ast_Interpreter_Statement_Result result = ast_interpreter_result_make_empty();

    if (statement->type == StatementType::RETURN_STATEMENT) {
        result.is_return = true;
        result.return_value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        return result;
    }
    else if (statement->type == StatementType::EXPRESSION) // Expression can be function call, thats why we need it here
    {
        ast_interpreter_evaluate_expression(interpreter, &statement->expression); 
        return result;
    }
    else if (statement->type == StatementType::WHILE)
    {
        while (true)
        {
            Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
            if (val.type != Variable_Type::BOOLEAN) {
                logg("WHILE condition is not a boolean!\n");
                return result;
            }
            if (val.bool_value) {
                Ast_Interpreter_Statement_Result res = ast_interpreter_execute_statment_block(interpreter, &statement->statements);
                if (res.is_return) return result;
                if (res.is_continue) continue;
                if (res.is_break) return result;
            }
            else {
                return result;
            }
        }
    }
    else if (statement->type == StatementType::IF_BLOCK) {
        Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        if (val.type != Variable_Type::BOOLEAN) {
            logg("If expression is not a boolean!\n");
            return result;
        }
        if (val.bool_value) {
            return ast_interpreter_execute_statment_block(interpreter, &statement->statements);
        }
    }
    else if (statement->type == StatementType::IF_ELSE_BLOCK) {
        Ast_Interpreter_Value val = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        if (val.type != Variable_Type::BOOLEAN) {
            logg("If expression is not a boolean!\n");
            return result;
        }
        if (val.bool_value) {
            return ast_interpreter_execute_statment_block(interpreter, &statement->statements);
        }
        else {
            return ast_interpreter_execute_statment_block(interpreter, &statement->else_statements);
        }
    }
    else if (statement->type == StatementType::VARIABLE_DEFINITION) {
        if (statement->variable_type_id == interpreter->int_token_index) {
            ast_interpreter_define_variable(interpreter, Variable_Type::INTEGER, statement->variable_name_id);
        }
        else if (statement->variable_type_id == interpreter->float_token_index) {
            ast_interpreter_define_variable(interpreter, Variable_Type::FLOAT, statement->variable_name_id);
        }
        else if (statement->variable_type_id == interpreter->bool_token_index) {
            ast_interpreter_define_variable(interpreter, Variable_Type::BOOLEAN, statement->variable_name_id);
        }
        else {
            logg("Type-Error: %s is not a valid type\n", lexer_identifer_to_string(interpreter->lexer, statement->variable_type_id).characters);
            return result;
        }
    }
    else if (statement->type == StatementType::VARIABLE_ASSIGNMENT)
    {
        Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, statement->variable_name_id);
        if (var == 0) {
            logg("Variable assignment statement variable %s not defined!\n", 
                lexer_identifer_to_string(interpreter->lexer, statement->variable_name_id).characters);
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
        Ast_Interpreter_Value value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        Variable_Type::ENUM var_type;
        if (statement->variable_type_id == interpreter->int_token_index) var_type = Variable_Type::INTEGER;
        else if (statement->variable_type_id == interpreter->float_token_index) var_type = Variable_Type::FLOAT;
        else if (statement->variable_type_id == interpreter->bool_token_index) var_type = Variable_Type::BOOLEAN;
        else {
            logg("Type-Error: %s is not a valid type\n", lexer_identifer_to_string(interpreter->lexer, statement->variable_type_id).characters);
            return result;
        }
        if (var_type != value.type) {
            logg("Types not compatible, var type: ", lexer_identifer_to_string(interpreter->lexer, statement->variable_type_id).characters);
            return result;
        }

        ast_interpreter_define_variable(interpreter, var_type, statement->variable_name_id);
        Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, statement->variable_name_id);
        var->value = value;
    }
    else if (statement->type == StatementType::VARIABLE_DEFINE_INFER)
    {
        Ast_Interpreter_Value value = ast_interpreter_evaluate_expression(interpreter, &statement->expression);
        ast_interpreter_define_variable(interpreter, value.type, statement->variable_name_id);
        Ast_Interpreter_Variable* var = ast_interpreter_find_variable(interpreter, statement->variable_name_id);
        var->value = value;
    }
    else if (statement->type == StatementType::STATEMENT_BLOCK) {
        return ast_interpreter_execute_statment_block(interpreter, &statement->statements);
    }
    else if (statement->type == StatementType::BREAK) {
        return ast_interpreter_result_make_break();
    }
    else if (statement->type == StatementType::CONTINUE) {
        return ast_interpreter_result_make_continue();
    }
    return result;
}

Ast_Interpreter_Value ast_interpreter_execute_main(Ast_Node_Root* root, Lexer* lexer)
{
    Ast_Interpreter interpreter = ast_interpreter_create(root, lexer);
    SCOPE_EXIT(ast_interpreter_destroy(&interpreter));
    Ast_Interpreter_Value error_value;
    error_value.type = Variable_Type::ERROR_TYPE;

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
    interpreter.int_token_index = lexer_add_or_find_identifier_by_string(lexer, string_create_static("int"));
    interpreter.bool_token_index = lexer_add_or_find_identifier_by_string(lexer, string_create_static("bool"));
    interpreter.float_token_index = lexer_add_or_find_identifier_by_string(lexer, string_create_static("float"));
    interpreter.print_token_index = lexer_add_or_find_identifier_by_string(lexer, string_create_static("print"));

    Ast_Interpreter_Statement_Result main_result = ast_interpreter_execute_statment_block(&interpreter, &main->body);
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
    case Variable_Type::BOOLEAN:
        string_append_formated(string, "BOOL: %s ", value.bool_value ? "true" : "false"); break;
    case Variable_Type::INTEGER:
        string_append_formated(string, "INT: %d ", value.int_value); break;
    case Variable_Type::FLOAT:
        string_append_formated(string, "FLOAT: %f ", value.float_value); break;
    case Variable_Type::ERROR_TYPE:
        string_append_formated(string, "ERROR-Type "); break;
    default:
        string_append_formated(string, "SHOULD_NOT_HAPPEN.EXE"); break;
    }
    return;
    return;
}

String ast_interpreter_value_type_to_string(Variable_Type::ENUM type)
{
    switch (type)
    {
    case Variable_Type::BOOLEAN:
        return string_create_static("BOOL");
    case Variable_Type::INTEGER:
        return string_create_static("INT");
    case Variable_Type::FLOAT:
        return string_create_static("FLOAT");
    case Variable_Type::ERROR_TYPE:
        return string_create_static("ERROR_TYPE");
    }
    return string_create_static("INVALID_VALUE_TYPE_ENUM");
}

void parser_report_semantic_analysis_error(Parser* parser, const char* msg) {
    dynamic_array_push_back(&parser->semantic_analysis_errors, msg);
}

SymbolTable symbol_table_create(SymbolTable* parent)
{
    SymbolTable result;
    result.parent = parent;
    result.symbols = dynamic_array_create_empty<Symbol>(8);
    return result;
}

SymbolTable* symbol_table_create_new(SymbolTable* parent) {
    SymbolTable* res = new SymbolTable();
    *res = symbol_table_create(parent);
    return res;
}

void symbol_table_destroy(SymbolTable* table) {
    dynamic_array_destroy(&table->symbols);
}

Symbol* symbol_table_find_symbol(SymbolTable* table, int name, bool* in_current_scope) 
{
    *in_current_scope = false;
    for (int i = 0; i < table->symbols.size; i++) {
        if (table->symbols[i].name == name) {
            *in_current_scope = true;
            return &table->symbols[i];
        }
    }
    if (table->parent != 0) {
        Symbol* result = symbol_table_find_symbol(table->parent, name, in_current_scope);
        *in_current_scope = false;
        return result;
    }
    return 0;
}

Symbol* symbol_table_find_symbol_type(SymbolTable* table, int name, SymbolType::ENUM symbol_type, bool* in_current_scope)
{
    *in_current_scope = false;
    for (int i = 0; i < table->symbols.size; i++) {
        if (table->symbols[i].name == name && table->symbols[i].symbol_type == symbol_type) {
            *in_current_scope = true;
            return &table->symbols[i];
        }
    }
    if (table->parent != 0) {
        Symbol* result = symbol_table_find_symbol_type(table->parent, name, symbol_type, in_current_scope);
        *in_current_scope = false;
        return result;
    }
    return 0;
}

void symbol_table_define_function(SymbolTable* table, Parser* parser, Ast_Node_Function* function) 
{
    bool in_current_scope;
    Symbol* func = symbol_table_find_symbol_type(table, function->function_name_id, SymbolType::FUNCTION, &in_current_scope);
    if (func != 0 && in_current_scope) {
        parser_report_semantic_analysis_error(parser, "Function already defined");
        return;
    }
    Symbol s;
    s.symbol_type = SymbolType::FUNCTION;
    s.name = function->function_name_id;
    s.function = function;
    dynamic_array_push_back(&table->symbols, s);
}

void symbol_table_define_variable(SymbolTable* table, Parser* parser, int name_id, Variable_Type::ENUM variable_type) 
{
    bool in_current_scope;
    Symbol* func = symbol_table_find_symbol_type(table, name_id, SymbolType::VARIABLE, &in_current_scope);
    if (func != 0 && in_current_scope) {
        parser_report_semantic_analysis_error(parser, "Variable already define in current scope");
        return;
    }

    Symbol s;
    s.symbol_type = SymbolType::VARIABLE;
    s.variable_type = variable_type;
    s.name = name_id;
    dynamic_array_push_back(&table->symbols, s);
}

void symbol_table_define_type(SymbolTable* table, Parser* parser, int name_id, Variable_Type::ENUM variable_type)
{
    bool in_current_scope;
    Symbol* sym = symbol_table_find_symbol_type(table, name_id, SymbolType::TYPE, &in_current_scope);
    if (sym != 0) {
        panic("Types should not overlap currently!\n");
        return;
    }

    Symbol s;
    s.symbol_type = SymbolType::TYPE;
    s.variable_type = variable_type;
    s.name = name_id;
    dynamic_array_push_back(&table->symbols, s);
}

Variable_Type::ENUM symbol_table_find_type(SymbolTable* table, int name_id) {
    bool in_current_scope;
    Symbol* s = symbol_table_find_symbol_type(table, name_id, SymbolType::TYPE, &in_current_scope);
    if (s == 0) {
        return Variable_Type::ERROR_TYPE;
    }
    else return s->variable_type;
}

Variable_Type::ENUM semantic_analysis_analyse_expression(SymbolTable* parent_table, Parser* parser, Ast_Node_Expression* expression)
{
    expression->symbol_table = parent_table;
    expression->free_symbol_table_on_destroy = false;
    switch (expression->type)
    {
    case ExpressionType::FUNCTION_CALL: 
    {
        bool in_current_scope;
        Symbol* func_symbol = 
            symbol_table_find_symbol_type(expression->symbol_table, expression->variable_name_id, SymbolType::FUNCTION, &in_current_scope);
        if (func_symbol == 0) {
            parser_report_semantic_analysis_error(parser, "Function call to a not defined function!");
        }
        Ast_Node_Function* function = func_symbol->function;
        if (expression->arguments.size != function->parameters.size) {
            parser_report_semantic_analysis_error(parser, "Call arguments and function parameter count do not match");
        }
        for (int i = 0; i < function->parameters.size && i < expression->arguments.size; i++) 
        {
            Ast_Node_Expression* argument = &expression->arguments[i];
            Variable_Type::ENUM argument_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, argument);
            Parameter p = function->parameters[i];
            Variable_Type::ENUM param_type = symbol_table_find_type(expression->symbol_table, p.type_id);
            if (argument_type != param_type || argument_type == Variable_Type::ERROR_TYPE) {
                logg("Arguments type does not match parameter type in function call");
            }
        }
        return symbol_table_find_type(expression->symbol_table, function->return_type_id);
        break;
    }
    case ExpressionType::LITERAL: {
        if (parser->lexer->tokens[expression->literal_token_index].type == Token_Type::BOOLEAN_LITERAL) {
            return Variable_Type::BOOLEAN;
        }
        if (parser->lexer->tokens[expression->literal_token_index].type == Token_Type::INTEGER_LITERAL) {
            return Variable_Type::INTEGER;
        }
        if (parser->lexer->tokens[expression->literal_token_index].type == Token_Type::FLOAT_LITERAL) {
            return Variable_Type::FLOAT;
        }
        panic("This should not happend\n");
        break;
    }
    case ExpressionType::OP_ADD: 
    case ExpressionType::OP_SUBTRACT:
    case ExpressionType::OP_DIVIDE:
    case ExpressionType::OP_MULTIPLY: 
    {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        Variable_Type::ENUM right_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->right);
        if (left_type != right_type) {
            parser_report_semantic_analysis_error(parser, "Left and right values of arithmetic op do not have the same type");
        }
        if (left_type != Variable_Type::INTEGER && left_type != Variable_Type::FLOAT) {
            parser_report_semantic_analysis_error(parser, "Arithmetic operation requires either int or float types on both sides");
        }
        return left_type;
        break;
    }
    case ExpressionType::OP_GREATER_EQUAL: 
    case ExpressionType::OP_GREATER_THAN: 
    case ExpressionType::OP_LESS_EQUAL: 
    case ExpressionType::OP_LESS_THAN: 
    {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        Variable_Type::ENUM right_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->right);
        if (left_type != right_type) {
            parser_report_semantic_analysis_error(parser, "Left and right values of arithmetic op do not have the same type");
        }
        if (left_type != Variable_Type::INTEGER && left_type != Variable_Type::FLOAT) {
            parser_report_semantic_analysis_error(parser, "Arithmetic operation requires either int or float types on both sides");
        }
        return Variable_Type::BOOLEAN;
        break;
    }
    case ExpressionType::OP_MODULO: {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        Variable_Type::ENUM right_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->right);
        if (left_type != right_type) {
            parser_report_semantic_analysis_error(parser, "Left and right values of modulo not have the same type");
        }
        if (left_type != Variable_Type::INTEGER) {
            parser_report_semantic_analysis_error(parser, "Modulo needs integer parameters");
        }
        return left_type;
        break;
    }
    case ExpressionType::OP_BOOLEAN_AND: 
    case ExpressionType::OP_BOOLEAN_OR: {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        Variable_Type::ENUM right_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->right);
        if (left_type != right_type) {
            parser_report_semantic_analysis_error(parser, "Left and right values of boolean op do not have the same type");
        }
        if (left_type != Variable_Type::BOOLEAN) {
            parser_report_semantic_analysis_error(parser, "Boolean opeartions need boolean left and right");
        }
        return Variable_Type::BOOLEAN;
        break;
    }
    case ExpressionType::OP_EQUAL: 
    case ExpressionType::OP_NOT_EQUAL: {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        Variable_Type::ENUM right_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->right);
        if (left_type != right_type) {
            parser_report_semantic_analysis_error(parser, "Left and right values do not have the same type");
        }
        return Variable_Type::BOOLEAN;
        break;
    }
    case ExpressionType::OP_LOGICAL_NOT: {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        if (left_type != Variable_Type::BOOLEAN) {
            parser_report_semantic_analysis_error(parser, "Logical not needs boolean expression");
        }
        return Variable_Type::BOOLEAN;
        break;
    }
    case ExpressionType::OP_NEGATE: {
        Variable_Type::ENUM left_type = semantic_analysis_analyse_expression(expression->symbol_table, parser, expression->left);
        if (left_type != Variable_Type::FLOAT || left_type != Variable_Type::INTEGER) {
            parser_report_semantic_analysis_error(parser, "Negate requires float or integer");
        }
        return left_type;
        break;
    }
    case ExpressionType::VARIABLE_READ: {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_type(expression->symbol_table, 
            expression->variable_name_id, SymbolType::VARIABLE, &in_current_scope);
        if (s == 0) {
            parser_report_semantic_analysis_error(parser, "Expression variable not defined!");
            return Variable_Type::ERROR_TYPE;
        }
        return s->variable_type;
        break;
    }
    default: {
        panic("Not all expression covered!\n");
        break;
    }
    }
    return Variable_Type::ERROR_TYPE;
}

void semantic_analysis_analyse_statement_block(SymbolTable* parent_table, Parser* parser, Ast_Node_Statement_Block* block, bool create_new_scope);
void semantic_analysis_analyse_statement(SymbolTable* parent_table, Parser* parser, Ast_Node_Statement* statement)
{
    // I think i need an analysis if there is a return on all paths (if and elsee
    statement->symbol_table = parent_table;
    statement->free_symbol_table_on_destroy = false;
    switch (statement->type)
    {
    case StatementType::RETURN_STATEMENT: {
        Variable_Type::ENUM return_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        if (return_type != parser->current_function_return_type) {
            parser_report_semantic_analysis_error(parser, "Return type does not match function return type");
        }
        break;
    }
    case StatementType::BREAK: {
        if (parser->loop_depth <= 0) {
            parser_report_semantic_analysis_error(parser, "Break outside of loop");
        }
        break;
    }
    case StatementType::CONTINUE: {
        if (parser->loop_depth <= 0) {
            parser_report_semantic_analysis_error(parser, "Continue outside of loop");
        }
        break;
    }
    case StatementType::EXPRESSION: {
        if (statement->expression.type != ExpressionType::FUNCTION_CALL) {
            parser_report_semantic_analysis_error(parser, "Single expression statement is not a function call!");
        }
        break;
    }
    case StatementType::STATEMENT_BLOCK: {
        semantic_analysis_analyse_statement_block(statement->symbol_table, parser, &statement->statements, true);
        break;
    }
    case StatementType::IF_BLOCK: {
        Variable_Type::ENUM condition_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        if (condition_type != Variable_Type::BOOLEAN) {
            parser_report_semantic_analysis_error(parser, "If condition is not a boolean!");
        }
        semantic_analysis_analyse_statement_block(statement->symbol_table, parser, &statement->statements, true);
        break;
    }
    case StatementType::IF_ELSE_BLOCK: {
        Variable_Type::ENUM condition_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        if (condition_type != Variable_Type::BOOLEAN) {
            parser_report_semantic_analysis_error(parser, "If condition is not a boolean!");
        }
        semantic_analysis_analyse_statement_block(statement->symbol_table, parser, &statement->statements, true);
        semantic_analysis_analyse_statement_block(statement->symbol_table, parser, &statement->else_statements, true);
        break;
    }
    case StatementType::WHILE: {
        Variable_Type::ENUM condition_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        if (condition_type != Variable_Type::BOOLEAN) {
            parser_report_semantic_analysis_error(parser, "While condition is not a boolean!");
        }
        semantic_analysis_analyse_statement_block(statement->symbol_table, parser, &statement->statements, true);
        break;
    }
    case StatementType::VARIABLE_ASSIGNMENT: {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_type(statement->symbol_table, statement->variable_name_id, SymbolType::VARIABLE, &in_current_scope);
        if (s == 0) {
            parser_report_semantic_analysis_error(parser, "Variable assignment, variable not defined!");
        }
        Variable_Type::ENUM assignment_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        if (assignment_type != s->variable_type) {
            parser_report_semantic_analysis_error(parser, "Variable type does not match expression type");
        }
        break;
    }
    case StatementType::VARIABLE_DEFINITION:
    {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_type(statement->symbol_table, statement->variable_name_id, SymbolType::VARIABLE, &in_current_scope);
        if (s != 0 && in_current_scope) {
            parser_report_semantic_analysis_error(parser, "Variable already defined!");
            break;
        }
        Symbol* var_type = symbol_table_find_symbol_type(statement->symbol_table, statement->variable_type_id, SymbolType::TYPE, &in_current_scope);
        if (var_type == 0) {
            parser_report_semantic_analysis_error(parser, "Variable definition failed, variable type is invalid");
            break;
        }
        symbol_table_define_variable(statement->symbol_table, parser, statement->variable_name_id, var_type->variable_type);
        break;
    }
    case StatementType::VARIABLE_DEFINE_ASSIGN:
    {
        bool in_current_scope;
        {
            Symbol* s = symbol_table_find_symbol_type(statement->symbol_table, statement->variable_name_id, SymbolType::VARIABLE, &in_current_scope);
            if (s != 0 && in_current_scope) {
                parser_report_semantic_analysis_error(parser, "Variable already defined!");
                break;
            }
        }
        Symbol* var_type = symbol_table_find_symbol_type(statement->symbol_table, statement->variable_type_id, SymbolType::TYPE, &in_current_scope);
        if (var_type == 0) {
            parser_report_semantic_analysis_error(parser, "Variable definition failed, variable type is invalid");
            break;
        }
        Variable_Type::ENUM assignment_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        if (assignment_type != var_type->variable_type) {
            parser_report_semantic_analysis_error(parser, "Variable type does not match expression type");
        }
        symbol_table_define_variable(statement->symbol_table, parser, statement->variable_name_id, var_type->variable_type);
        break;
    }
    case StatementType::VARIABLE_DEFINE_INFER:
    {
        bool in_current_scope;
        {
            Symbol* s = symbol_table_find_symbol_type(statement->symbol_table, statement->variable_name_id, SymbolType::VARIABLE, &in_current_scope);
            if (s != 0 && in_current_scope) {
                parser_report_semantic_analysis_error(parser, "Variable already defined!");
                break;
            }
        }
        Variable_Type::ENUM assignment_type = semantic_analysis_analyse_expression(statement->symbol_table, parser, &statement->expression);
        symbol_table_define_variable(statement->symbol_table, parser, statement->variable_name_id, assignment_type);
        break;
    }
    default: {
        panic("Should be covered!\n");
        break;
    }
    }

    return;
}

void semantic_analysis_analyse_statement_block(SymbolTable* parent_table, Parser* parser, Ast_Node_Statement_Block* block, bool create_new_scope)
{
    // This should have its own symbol table, think of if/else statements... -> or if else while has its own stuff...
    if (create_new_scope) {
        block->symbol_table = symbol_table_create_new(parent_table);
        block->free_symbol_table_on_destroy = true;
    }
    else {
        block->symbol_table = parent_table;
        block->free_symbol_table_on_destroy = false;
    }
    for (int i = 0; i < block->statements.size; i++) {
        Ast_Node_Statement* statement = &block->statements[i];
        semantic_analysis_analyse_statement(block->symbol_table, parser, statement);
    }
}

void semantic_analysis_analyse_function(SymbolTable* parent_table, Parser* parser, Ast_Node_Function* function)
{
    function->symbol_table = symbol_table_create_new(parent_table);
    function->free_symbol_table_on_destroy = true;
    // Define paramenter variables
    for (int i = 0; i < function->parameters.size; i++)
    {
        Parameter p = function->parameters[i];
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_type(function->symbol_table, p.type_id, SymbolType::TYPE, &in_current_scope);
        if (s == 0) {
            parser_report_semantic_analysis_error(parser, "Variable type is not defined!\n");
            // I define the variable as error-type, so that there arent that many follow up errors
            symbol_table_define_variable(function->symbol_table, parser, p.name_id, Variable_Type::ERROR_TYPE);
            continue;
        }
        symbol_table_define_variable(function->symbol_table, parser, p.name_id, s->variable_type);
    }

    // Set return type
    {
        bool in_current_scope;
        Symbol* s = symbol_table_find_symbol_type(function->symbol_table, function->return_type_id, SymbolType::TYPE, &in_current_scope);
        if (s == 0) {
            parser_report_semantic_analysis_error(parser, "Function return type not valid type!");
            parser->current_function_return_type = Variable_Type::ERROR_TYPE;
        }
        else {
            parser->current_function_return_type = s->variable_type;
        }
    }
    parser->loop_depth = 0;

    semantic_analysis_analyse_statement_block(function->symbol_table, parser, &function->body, false);
}

void parser_semantic_analysis(Parser* parser)
{
    Ast_Node_Root* root = &parser->root;
    root->symbol_table = symbol_table_create_new(0);
    root->free_symbol_table_on_destroy = true;
    // Add tokens for basic datatypes
    {
        int int_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("int"));
        int bool_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("bool"));
        int float_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("float"));
        int void_token_index = lexer_add_or_find_identifier_by_string(parser->lexer, string_create_static("void"));
        symbol_table_define_type(root->symbol_table, parser, int_token_index, Variable_Type::INTEGER);
        symbol_table_define_type(root->symbol_table, parser, bool_token_index, Variable_Type::BOOLEAN);
        symbol_table_define_type(root->symbol_table, parser, float_token_index, Variable_Type::FLOAT);
        symbol_table_define_type(root->symbol_table, parser, void_token_index, Variable_Type::VOID_TYPE);
    }

    for (int i = 0; i < root->functions.size; i++) {
        Ast_Node_Function* function = &root->functions[i];
        symbol_table_define_function(root->symbol_table, parser, function);
    }

    // Do semantic analysis on all functions
    for (int i = 0; i < root->functions.size; i++) {
        Ast_Node_Function* function = &root->functions[i];
        semantic_analysis_analyse_function(root->symbol_table, parser, function);
    }
}
















