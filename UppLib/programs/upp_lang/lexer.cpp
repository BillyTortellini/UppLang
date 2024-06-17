#include "lexer.hpp"

#include "../../datastructures/hashtable.hpp"
#include "../../utility/character_info.hpp"
#include "compiler_misc.hpp"
#include "source_code.hpp"

// Helpers
Operator_Info operator_info_make(const char* str, Operator_Type type, bool space_before, bool space_after)
{
    Operator_Info result;
    result.string = string_create_static(str);
    result.type = type;
    result.space_after = space_after;
    result.space_before = space_before;
    return result;
}

Operator_Info syntax_operator_info(Operator op)
{
    switch (op)
    {
    case Operator::ADDITION: return operator_info_make("+", Operator_Type::BINOP, true, true);
    case Operator::SUBTRACTION: return operator_info_make("-", Operator_Type::BOTH, true, true);
    case Operator::DIVISON: return operator_info_make("/", Operator_Type::BINOP, true, true);
    case Operator::MULTIPLY: return operator_info_make("*", Operator_Type::BOTH, true, true);
    case Operator::MODULO: return operator_info_make("%", Operator_Type::BINOP, true, true);
    case Operator::COMMA: return operator_info_make(",", Operator_Type::BINOP, false, true);
    case Operator::DOT: return operator_info_make(".", Operator_Type::BINOP, false, false);
    case Operator::TILDE: return operator_info_make("~", Operator_Type::BINOP, false, false);
    case Operator::TILDE_STAR: return operator_info_make("~*", Operator_Type::BINOP, false, true);
    case Operator::TILDE_STAR_STAR: return operator_info_make("~**", Operator_Type::BINOP, false, true);
    case Operator::COLON: return operator_info_make(":", Operator_Type::BINOP, false, true);
    case Operator::NOT: return operator_info_make("!", Operator_Type::BINOP, false, false);
    case Operator::AMPERSAND: return operator_info_make("&", Operator_Type::UNOP, false, false);
    case Operator::LESS_THAN: return operator_info_make("<", Operator_Type::BINOP, true, true);
    case Operator::GREATER_THAN: return operator_info_make(">", Operator_Type::BINOP, true, true);
    case Operator::LESS_EQUAL: return operator_info_make("<=", Operator_Type::BINOP, true, true);
    case Operator::GREATER_EQUAL: return operator_info_make(">=", Operator_Type::BINOP, true, true);
    case Operator::EQUALS: return operator_info_make("==", Operator_Type::BINOP, true, true);
    case Operator::NOT_EQUALS: return operator_info_make("!=", Operator_Type::BINOP, true, true);
    case Operator::POINTER_EQUALS: return operator_info_make("==*", Operator_Type::BINOP, true, true);
    case Operator::POINTER_NOT_EQUALS: return operator_info_make("!=*", Operator_Type::BINOP, true, true);
    case Operator::DEFINE_COMPTIME: return operator_info_make("::", Operator_Type::BINOP, true, true);
    case Operator::DEFINE_INFER: return operator_info_make(":=", Operator_Type::BINOP, true, true);
    case Operator::AND: return operator_info_make("&&", Operator_Type::BOTH, true, true); // Could also be double dereference &&int_pointer_pointer
    case Operator::OR: return operator_info_make("||", Operator_Type::BINOP, true, true);
    case Operator::ARROW: return operator_info_make("->", Operator_Type::BINOP, true, true);
    case Operator::DOLLAR: return operator_info_make("$", Operator_Type::UNOP, false, false);
    case Operator::ASSIGN: return operator_info_make("=", Operator_Type::BINOP, true, true);
    case Operator::ASSIGN_ADD: return operator_info_make("+=", Operator_Type::BINOP, true, true);
    case Operator::ASSIGN_SUB: return operator_info_make("-=", Operator_Type::BINOP, true, true);
    case Operator::ASSIGN_DIV: return operator_info_make("/=", Operator_Type::BINOP, true, true);
    case Operator::ASSIGN_MULT: return operator_info_make("*=", Operator_Type::BINOP, true, true);
    case Operator::ASSIGN_POINTER: return operator_info_make("=*", Operator_Type::BINOP, true, true);
    case Operator::DEFINE_INFER_POINTER: return operator_info_make(":=*", Operator_Type::BINOP, true, true);
    default: panic("");
    }

    panic("");
    return operator_info_make("what", Operator_Type::BINOP, true, true);
}

int character_index_to_token(Dynamic_Array<Token>* tokens, int char_index, bool after_cursor)
{
    for (int i = tokens->size - 1; i >= 0; i--) {
        auto& token = (*tokens)[i];
        if ((after_cursor && char_index >= token.start_index) ||
            (!after_cursor && char_index > token.start_index)) {
            return i;
        }
    }
    return 0;
}

String syntax_keyword_as_string(Keyword keyword)
{
    switch (keyword)
    {
    case Keyword::IMPORT: return string_create_static("import");
    case Keyword::AS: return string_create_static("as");
    case Keyword::CONTEXT: return string_create_static("context");
    case Keyword::BAKE: return string_create_static("#bake");
    case Keyword::BREAK: return string_create_static("break");
    case Keyword::CASE: return string_create_static("case");
    case Keyword::CAST: return string_create_static("cast");
    case Keyword::CAST_RAW: return string_create_static("cast_raw");
    case Keyword::CAST_PTR: return string_create_static("cast_ptr");
    case Keyword::CONTINUE: return string_create_static("continue");
    case Keyword::C_UNION: return string_create_static("c_union");
    case Keyword::DEFAULT: return string_create_static("default");
    case Keyword::DEFER: return string_create_static("defer");
    case Keyword::DELETE_KEYWORD: return string_create_static("delete");
    case Keyword::ELSE: return string_create_static("else");
    case Keyword::IF: return string_create_static("if");
    case Keyword::MODULE: return string_create_static("module");
    case Keyword::NEW: return string_create_static("new");
    case Keyword::ENUM: return string_create_static("enum");
    case Keyword::RETURN: return string_create_static("return");
    case Keyword::STRUCT: return string_create_static("struct");
    case Keyword::SWITCH: return string_create_static("switch");
    case Keyword::UNION: return string_create_static("union");
    case Keyword::WHILE: return string_create_static("while");
    default:panic("");
    }
    return string_create_static("HEY");
}

char parenthesis_to_char(Parenthesis p)
{
    switch (p.type)
    {
    case Parenthesis_Type::BRACES:
        return p.is_open ? '{' : '}';
    case Parenthesis_Type::BRACKETS:
        return p.is_open ? '[' : ']';
    case Parenthesis_Type::PARENTHESIS:
        return p.is_open ? '(' : ')';
    default: panic("");
    }
    return ' ';
}

bool char_is_parenthesis(char c) {
    return string_contains_character(string_create_static("[]{}()"), c);
}

Parenthesis char_to_parenthesis(char c)
{
    Parenthesis parenthesis;
    switch (c)
    {
    case '[': parenthesis.is_open = true;  parenthesis.type = Parenthesis_Type::BRACKETS; break;
    case ']': parenthesis.is_open = false; parenthesis.type = Parenthesis_Type::BRACKETS; break;
    case '{': parenthesis.is_open = true;  parenthesis.type = Parenthesis_Type::BRACES; break;
    case '}': parenthesis.is_open = false; parenthesis.type = Parenthesis_Type::BRACES; break;
    case '(': parenthesis.is_open = true;  parenthesis.type = Parenthesis_Type::PARENTHESIS; break;
    case ')': parenthesis.is_open = false; parenthesis.type = Parenthesis_Type::PARENTHESIS; break;
    default:
        panic("");
    }
    return parenthesis;
}

bool char_is_space_critical(char c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        (c == '_');
}

String token_type_as_string(Token_Type type)
{
    switch (type)
    {
    case Token_Type::IDENTIFIER: return string_create_static("IDENTIFIER");
            case Token_Type::KEYWORD: return string_create_static("KEYWORD");
            case Token_Type::LITERAL: return string_create_static("LITERAL");
            case Token_Type::OPERATOR: return string_create_static("OPERATOR");
            case Token_Type::PARENTHESIS: return string_create_static("PARENTHESIS");
            case Token_Type::INVALID: return string_create_static("INVALID");
            case Token_Type::COMMENT: return string_create_static("COMMENT");
    }
    panic("Hey");
    return string_create_static("Hey");
}

String token_get_string(Token token, String text)
{
    if (token.type == Token_Type::OPERATOR) {
        return syntax_operator_info(token.options.op).string;
    }
    else {
        return string_create_substring_static(&text, token.start_index, token.end_index);
    }
}



// Lexer
struct Source_Lexer
{
    Hashtable<String, Keyword> keyword_table;
    Identifier_Pool* identifier_pool;
    String line_buffer;
    int line_index;
};

static Source_Lexer lexer;

void lexer_initialize(Identifier_Pool* pool)
{
    lexer.identifier_pool = pool;
    lexer.line_buffer = string_create_empty(128);
    auto& keyword_table = lexer.keyword_table;
    keyword_table = hashtable_create_empty<String, Keyword>(8, hash_string, string_equals);
    for (int i = 0; i < (int)Keyword::MAX_ENUM_VALUE; i++) {
        hashtable_insert_element(&keyword_table, syntax_keyword_as_string((Keyword)i), (Keyword)i);
    }
}

void lexer_shutdown() {
    hashtable_destroy(&lexer.keyword_table);
    string_destroy(&lexer.line_buffer);
}

void lexer_tokenize_text_as_comment(String text, Dynamic_Array<Token>* tokens)
{
    dynamic_array_reset(tokens);
    if (text.size == 0) return;
    Token token;
    token.start_index = 0;
    token.end_index = text.size;
    token.type = Token_Type::COMMENT;
    dynamic_array_push_back(tokens, token);
}

void lexer_tokenize_text(String text, Dynamic_Array<Token>* tokens)
{
    dynamic_array_reset(tokens);
    int index = 0;
    while (index < text.size)
    {
        Token token;
        token.start_index = index;

        char c = text[index];
        if (char_is_letter(c) || c == '#')
        {
            // Identifier/Keyword
            int start_index = index;
            index += 1;
            while (index < text.size && char_is_valid_identifier(text[index])) {
                index += 1;
            }
            token.type = Token_Type::IDENTIFIER;
            token.options.identifier = identifier_pool_add(lexer.identifier_pool, string_create_substring_static(&text, start_index, index));

            // Determine if its a keyword
            Keyword* keyword = hashtable_find_element(&lexer.keyword_table, *token.options.identifier);
            if (keyword != 0) {
                token.type = Token_Type::KEYWORD;
                token.options.keyword = *keyword;
            }
            else if (string_equals_cstring(token.options.identifier, "null")) {
                token.type = Token_Type::LITERAL;
                token.options.literal_value.type = Literal_Type::NULL_VAL;
                token.options.literal_value.options.null_ptr = 0;
            }
            else if (string_equals_cstring(token.options.identifier, "true")) {
                token.type = Token_Type::LITERAL;
                token.options.literal_value.type = Literal_Type::BOOLEAN;
                token.options.literal_value.options.boolean = true;
            }
            else if (string_equals_cstring(token.options.identifier, "false")) {
                token.type = Token_Type::LITERAL;
                token.options.literal_value.type = Literal_Type::BOOLEAN;
                token.options.literal_value.options.boolean = false;
            }
        }
        else if (c == '"')
        {
            int start_index = index;
            index += 1;
            bool found_end = false;
            while (index < text.size) {
                if (text[index] == '"') {
                    found_end = true;
                    index += 1;
                    break;
                }
                index += 1;
            }

            String* parsed_string = 0;
            if (found_end)
            {
                String substr = string_create_substring_static(&text, start_index + 1, index);
                auto result_str = string_create_empty(substr.size);
                SCOPE_EXIT(string_destroy(&result_str));

                // Parse String literal
                bool invalid_escape_found = false;
                bool last_was_escape = false;
                for (int i = 0; i < substr.size; i++)
                {
                    char c = substr.characters[i];
                    if (last_was_escape)
                    {
                        switch (c)
                        {
                        case 'n':
                            string_append_character(&result_str, '\n');
                            break;
                        case 'r':
                            string_append_character(&result_str, '\r');
                            break;
                        case 't':
                            string_append_character(&result_str, '\t');
                            break;
                        case '\\':
                            string_append_character(&result_str, '\\');
                            break;
                        case '\'':
                            string_append_character(&result_str, '\'');
                            break;
                        case '\"':
                            string_append_character(&result_str, '\"');
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
                        if (c == '\"') {
                            break;
                        }
                        last_was_escape = c == '\\';
                        if (!last_was_escape) {
                            string_append_character(&result_str, c);
                        }
                    }
                }
                if (!invalid_escape_found) {
                    parsed_string = identifier_pool_add(lexer.identifier_pool, result_str);
                }
            }

            if (parsed_string != 0) {
                token.type = Token_Type::LITERAL;
                token.options.literal_value.type = Literal_Type::STRING;
                token.options.literal_value.options.string = parsed_string;
            }
            else {
                token.type = Token_Type::INVALID;
            }
        }
        else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            index += 1;
            continue;
        }
        else if (char_is_digit(c))
        {
            // Number literal
            int start_index = index;

            // We require char_is_valid_identifier because token stringify would put a space between 5a, which cannot be deleted
            bool is_valid_number = true;
            bool is_float_val = false;
            int int_val = 0;
            float float_val = 0;
            // Pre comma digits
            while (index < text.size)
            {
                if (char_is_valid_identifier(text[index]) && !char_is_digit(text[index])) {
                    is_valid_number = false;
                }
                else if (char_is_digit(text[index])) {
                    int_val = int_val * 10;
                    int_val += (text[index] - '0');
                }
                else {
                    break;
                }
                index += 1;
            }

            if (string_test_char(text, index, '.') && is_valid_number)
            {
                index += 1;
                // After comma digits
                is_float_val = true;
                float_val = (float)int_val;
                float multiplier = 0.1f;
                while (index < text.size)
                {
                    if (char_is_valid_identifier(text[index]) && !char_is_digit(text[index])) {
                        is_valid_number = false;
                    }
                    else if (char_is_digit(text[index])) {
                        float_val = float_val + multiplier * (text[index] - '0');
                        multiplier *= 0.1f;
                    }
                    else {
                        break;
                    }
                    index += 1;
                }
            }

            if (is_valid_number) {
                token.type = Token_Type::LITERAL;
                if (is_float_val) {
                    token.options.literal_value.type = Literal_Type::FLOAT_VAL;
                    token.options.literal_value.options.float_val = float_val;
                }
                else {
                    token.options.literal_value.type = Literal_Type::INTEGER;
                    token.options.literal_value.options.int_val = int_val;
                }
            }
            else {
                token.type = Token_Type::INVALID;
            }
        }
        else if (char_is_parenthesis(c))
        {
            index += 1;
            token.type = Token_Type::PARENTHESIS;
            token.options.parenthesis = char_to_parenthesis(c);
        }
        else if (index + 1 < text.size && c == '/' && text[index + 1] == '/')
        {
            token.type = Token_Type::COMMENT;
            index = text.size;
        }
        else
        {
            // Either operator or Error-Item
            int longest_index = -1;
            int longest_end = -1;
            // Check all operators
            for (int i = 0; i < SYNTAX_OPERATOR_COUNT; i++)
            {
                auto op_str = syntax_operator_info((Operator)i).string;
                bool matches = true;

                int end = index;
                // Check if all op characters match
                for (int j = 0; j < op_str.size; j++)
                {
                    char op_char = op_str[j];
                    // skip unnecessary characters between
                    while (end < text.size && string_contains_character(string_create_static(" \n\r\t"), text[end])) {
                        end += 1;
                    }
                    if (end >= text.size || text[end] != op_char) {
                        matches = false;
                        break;
                    }
                    end += 1;
                }
                if (matches && end > longest_end) {
                    longest_end = end;
                    longest_index = i;
                }
            }

            if (longest_end != -1) {
                index = longest_end;
                token.type = Token_Type::OPERATOR;
                token.options.op = (Operator)longest_index;
            }
            else {
                index += 1;
                token.type = Token_Type::INVALID;
            }
        }

        token.end_index = index;
        dynamic_array_push_back(tokens, token);
    }
}
