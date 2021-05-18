#include "lexer.hpp"

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
    case Token_Type::STRUCT: return true;
    case Token_Type::NEW: return true;
    case Token_Type::DELETE_TOKEN: return true;
    case Token_Type::BOOLEAN_LITERAL: return true;
    case Token_Type::CAST: return true;
    }
    return false;
}

const char* token_type_to_string(Token_Type::ENUM type)
{
    switch (type)
    {
    case Token_Type::IF: return "IF";
    case Token_Type::ELSE: return "ELSE";
    case Token_Type::FOR: return "FOR";
    case Token_Type::WHILE: return "WHILE";
    case Token_Type::CONTINUE: return "CONTINUE";
    case Token_Type::STRUCT: return "STRUCT";
    case Token_Type::BREAK: return "BREAK";
    case Token_Type::DOT: return "DOT";
    case Token_Type::NEW: return "NEW";
    case Token_Type::DELETE_TOKEN: return "DELETE";
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
    case Token_Type::COMMENT: return "COMMENT";
    case Token_Type::WHITESPACE: return "WHITESPACE";
    case Token_Type::NEW_LINE: return "NEWLINE";
    case Token_Type::RETURN: return "RETURN";
    case Token_Type::CAST: return "CAST";
    }
    panic("Should not happen!");
    return "TOKEN_NOT_KNOWN";
}

TokenAttribute token_attribute_make_empty() {
    TokenAttribute result;
    result.integer_value = 67676767;
    return result;
}

Token token_make(Token_Type::ENUM type, TokenAttribute attribute, Text_Slice position, int index)
{
    Token result;
    result.type = type;
    result.attribute = attribute;
    result.position = position;
    result.source_code_index = index;
    return result;
}

Token token_make(Token_Type::ENUM type, TokenAttribute attribute, int line, int character, int length, int index)
{
    Token result;
    result.type = type;
    result.attribute = attribute;
    result.position = text_slice_make(text_position_make(line, character), text_position_make(line, character+length));
    result.source_code_index = index;
    return result;
}

bool code_parse_comments(Lexer* lexer, String* code, int* index, int* character_pos, int* line_number)
{
    if (*index + 1 >= code->size) return false;
    // Single line comments
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
            Token_Type::COMMENT, 
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
            Token_Type::COMMENT, 
            token_attribute_make_empty(), 
            text_slice_make(text_position_make(line_start, start_char), text_position_make(*line_number, *character_pos)),
            start_index)
        );
        return true;
    }
    return false;
}

bool code_parse_newline(Lexer* lexer, String* code, int* index, int* character_pos, int* line_number)
{
    int i = *index;
    if (i < code->size && code->characters[i] == '\n') 
    {
        dynamic_array_push_back(&lexer->tokens, token_make(
            Token_Type::NEW_LINE, 
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

bool code_parse_whitespace(Lexer* lexer, String* code, int* index, int* character_pos, int* line_number)
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
            Token_Type::WHITESPACE, token_attribute_make_empty(), *line_number, char_start, *character_pos - char_start, start)
        );
        return true;
    }
    return false;
}

void code_skip_whitespace_and_comments(Lexer* lexer, String* code, int* index, int* character_pos, int* line_number)
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

Lexer lexer_create()
{
    Lexer lexer;
    lexer.identifier_index_lookup_table = hashtable_create_empty<String, int>(2048, &string_calculate_hash, &string_equals);
    lexer.identifiers = dynamic_array_create_empty<String>(1024);
    lexer.tokens = dynamic_array_create_empty<Token>(1024);
    lexer.tokens_with_whitespaces = dynamic_array_create_empty<Token>(1024);
    return lexer;
}

void lexer_parse_string(Lexer* lexer, String* code)
{
    String identifier_string = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&identifier_string));

    dynamic_array_reset(&lexer->tokens);
    dynamic_array_reset(&lexer->tokens_with_whitespaces);
    dynamic_array_reset(&lexer->identifiers);
    hashtable_reset(&lexer->identifier_index_lookup_table);

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
        case '.':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::DOT, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ';':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::SEMICOLON, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ',':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::COMMA, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '(':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::OPEN_PARENTHESIS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ')':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::CLOSED_PARENTHESIS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '{':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::OPEN_BRACES, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '}':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::CLOSED_BRACES, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '[':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::OPEN_BRACKETS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ']':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::CLOSED_BRACKETS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '+':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::OP_PLUS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '*':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::OP_STAR, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '/':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::OP_SLASH, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '%':
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::OP_PERCENT, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
            // Check for ambiguities between one and two characters (< and <=, = and ==, ! and !=, ...)
        case '=':
            if (next_character == '=') {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::COMPARISON_EQUAL, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::OP_ASSIGNMENT, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '-':
            if (next_character == '>') {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::ARROW, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::OP_MINUS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '<':
            if (next_character == '=') {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::COMPARISON_LESS_EQUAL, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::COMPARISON_LESS, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '>':
            if (next_character == '=') {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::COMPARISON_GREATER_EQUAL, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::COMPARISON_GREATER, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '!':
            if (next_character == '=') {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::COMPARISON_NOT_EQUAL, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::LOGICAL_NOT, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '&':
            if (next_character == '&') {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::LOGICAL_AND, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::LOGICAL_BITWISE_AND, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case '|':
            if (next_character == '|') {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::LOGICAL_OR, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::LOGICAL_BITWISE_OR, token_attribute_make_empty(), line_number, character_pos, 1, index));
            character_pos++;
            index++;
            continue;
        case ':':
            if (next_character == ':') {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::DOUBLE_COLON, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            else if (next_character == '=') {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::INFER_ASSIGN, token_attribute_make_empty(), line_number, character_pos, 2, index));
                index += 2;
                character_pos += 2;
                continue;
            }
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::COLON, token_attribute_make_empty(), line_number, character_pos, 1, index));
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
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::FLOAT_LITERAL, attribute, line_number, character_pos, character_length, index));
                index += character_length;
                character_pos += character_length;
                continue;
            }
            else {
                // Add integer Token
                TokenAttribute attribute;
                attribute.integer_value = int_value;
                int character_length = pre_comma_end_index - pre_comma_start_index + 1;
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::INTEGER_LITERAL, attribute, line_number, character_pos, character_length, index));
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
            dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::ERROR_TOKEN, token_attribute_make_empty(), line_number, character_pos, error_length, index));
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
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::IF, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "else")) {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::ELSE, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "for")) {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::FOR, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "while")) {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::WHILE, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "continue")) {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::CONTINUE, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "break")) {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::BREAK, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "return")) {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::RETURN, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "struct")) {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::STRUCT, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "cast")) {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::CAST, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "new")) {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::NEW, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "delete")) {
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::DELETE_TOKEN, token_attribute_make_empty(),
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "true")) {
                TokenAttribute attribute;
                attribute.bool_value = true;
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::BOOLEAN_LITERAL, attribute,
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else if (string_equals_cstring(&identifier_string, "false")) {
                TokenAttribute attribute;
                attribute.bool_value = false;
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::BOOLEAN_LITERAL, attribute,
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
            else {
                // Identifier is acutally a identifier, not a keyword
                TokenAttribute attribute;
                int* identifier_id = hashtable_find_element(&lexer->identifier_index_lookup_table, identifier_string);
                if (identifier_id != 0) {
                    attribute.identifier_number = *identifier_id;
                }
                else {
                    String identifier_string_copy = string_create(identifier_string.characters);
                    dynamic_array_push_back(&lexer->identifiers, identifier_string_copy);
                    attribute.identifier_number = lexer->identifiers.size - 1;
                    // Identifer needs to be added to table
                    hashtable_insert_element(&lexer->identifier_index_lookup_table, identifier_string_copy, attribute.identifier_number);
                }

                // Add token
                dynamic_array_push_back(&lexer->tokens, token_make(Token_Type::IDENTIFIER, attribute,
                    line_number, character_pos, identifier_string_length, index));
                index += identifier_string_length;
                character_pos += identifier_string_length;
            }
        }
    }

    // Make tokens with non_whitespaces
    for (int i = 0; i < lexer->tokens.size; i++) {
        if (lexer->tokens[i].type == Token_Type::WHITESPACE ||
            lexer->tokens[i].type == Token_Type::NEW_LINE ||
            lexer->tokens[i].type == Token_Type::COMMENT) {
            continue;
        }
        dynamic_array_push_back(&lexer->tokens_with_whitespaces, lexer->tokens[i]);
    }
    DynamicArray<Token> swap = lexer->tokens_with_whitespaces;
    lexer->tokens_with_whitespaces = lexer->tokens;
    lexer->tokens = swap;
}

void lexer_destroy(Lexer* lexer)
{
    for (int i = 0; i < lexer->identifiers.size; i++) {
        string_destroy(&lexer->identifiers[i]);
    }
    dynamic_array_destroy(&lexer->identifiers);
    dynamic_array_destroy(&lexer->tokens);
    dynamic_array_destroy(&lexer->tokens_with_whitespaces);
    hashtable_destroy(&lexer->identifier_index_lookup_table);
}

String lexer_identifer_to_string(Lexer* lexer, int index) {
    return lexer->identifiers[index];
}

void lexer_print(Lexer* lexer)
{
    String msg = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&msg));
    string_append_formated(&msg, "Tokens: \n");
    for (int i = 0; i < lexer->tokens_with_whitespaces.size; i++) 
    {
        Token token = lexer->tokens_with_whitespaces[i];
        string_append_formated(&msg, "\t %s (Line %d, Pos %d, size: %d)",
            token_type_to_string(token.type), token.position.start.line, token.position.start.character,
            math_maximum(token.position.end.character - token.position.start.character, 0));
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

void lexer_print_identifiers(Lexer* lexer)
{
    String msg = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&msg));
    string_append_formated(&msg, "Identifiers: ");
    for (int i = 0; i < lexer->identifiers.size; i++) {
        string_append_formated(&msg, "\n\t%d: %s", i, lexer->identifiers[i].characters);
    }
    string_append_formated(&msg, "\n");
    logg("%s", msg.characters);
}