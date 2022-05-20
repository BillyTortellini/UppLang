#include "source_code.hpp"

#include "../../datastructures/hashtable.hpp"
#include "compiler.hpp"
#include "../../utility/character_info.hpp"

struct Operator_Info
{
    String string;
    Operator_Type type;
    bool space_before;
    bool space_after;
};
Operator_Info syntax_operator_info(Syntax_Operator op);


// Lexer
struct Source_Lexer
{
    Hashtable<String, Syntax_Keyword> keyword_table;
    Identifier_Pool* identifier_pool;
    int line_index;
};

static Source_Lexer lexer;

void lexer_initialize(Identifier_Pool* pool) 
{
    lexer.identifier_pool = pool;
    auto& keyword_table = lexer.keyword_table;
    keyword_table = hashtable_create_empty<String, Syntax_Keyword>(8, hash_string, string_equals);
    for (int i = 0; i < (int)Syntax_Keyword::MAX_ENUM_VALUE; i++) {
        hashtable_insert_element(&keyword_table, syntax_keyword_as_string((Syntax_Keyword)i), (Syntax_Keyword)i);
    }
}

void lexer_shutdown() {
    hashtable_destroy(&lexer.keyword_table);
}

void lexer_tokenize_block(Syntax_Block* block, int indentation)
{
    if (indentation == 0) {
        lexer.line_index = 0;
    }
    auto& info = block->info;
    info.indentation_level = indentation;
    info.line_start = lexer.line_index;
    for (int i = 0; i < block->lines.size; i++)
    {
        auto& line = block->lines[i];
        line->info.index = lexer.line_index;
        lexer_tokenize_syntax_line(line);
        if (line->follow_block != 0) {
            lexer_tokenize_block(line->follow_block, indentation + 1);
        }
        lexer.line_index += 1;
    }
    info.line_start = lexer.line_index;
}

bool is_space_critical(Syntax_Token_Type type) {
    return type == Syntax_Token_Type::IDENTIFIER || type == Syntax_Token_Type::KEYWORD || type == Syntax_Token_Type::LITERAL_NUMBER ||
        type == Syntax_Token_Type::LITERAL_BOOL;
}

void lexer_tokenize_syntax_line(Syntax_Line* line)
{
    auto& text = line->text;
    auto& tokens = line->tokens;
    dynamic_array_reset(&line->tokens);

    if (syntax_line_is_comment(line)) {
        Syntax_Token token;
        memory_zero(&token.info);
        token.info.char_start = 0;
        token.info.char_end = line->text.size;
        token.type = Syntax_Token_Type::COMMENT;
        token.options.comment = identifier_pool_add(lexer.identifier_pool, string_create_substring_static(&text, 0, text.size));
        dynamic_array_push_back(&line->tokens, token);
        return;
    }

    int index = 0;
    while (index < text.size)
    {
        Syntax_Token token;
        memory_zero(&token.info);
        token.info.char_start = index;

        char c = text[index];
        if (char_is_letter(c) || c == '#')
        {
            // Identifier/Keyword
            int start_index = index;
            index += 1;
            while (index < text.size && char_is_valid_identifier(text[index])) {
                index += 1;
            }
            token.type = Syntax_Token_Type::IDENTIFIER;
            token.options.identifier = identifier_pool_add(lexer.identifier_pool, string_create_substring_static(&text, start_index, index));

            // Determine if its a keyword
            Syntax_Keyword* keyword = hashtable_find_element(&lexer.keyword_table, *token.options.identifier);
            if (keyword != 0) {
                token.type = Syntax_Token_Type::KEYWORD;
                token.options.keyword = *keyword;
            }
            else if (string_equals_cstring(token.options.identifier, "true")) {
                token.type = Syntax_Token_Type::LITERAL_BOOL;
                token.options.literal_bool = true;
            }
            else if (string_equals_cstring(token.options.identifier, "false")) {
                token.type = Syntax_Token_Type::LITERAL_BOOL;
                token.options.literal_bool = false;
            }
        }
        else if (c == '"')
        {
            int start_index = index;
            index += 1;
            token.options.literal_string.has_closure = false;
            while (index < text.size) {
                if (text[index] == '"') {
                    token.options.literal_string.has_closure = true;
                    index += 1;
                    break;
                }
                index += 1;
            }
            token.type = Syntax_Token_Type::LITERAL_STRING;
            token.options.literal_string.string = identifier_pool_add(lexer.identifier_pool, string_create_substring_static(&text, start_index, index));
        }
        else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            index += 1;
            continue;
        }
        else if (char_is_digit(c))
        {
            // Number literal
            int start_index = index;
            index += 1;
            // We require char_is_valid_identifier because token stringify would put a space between 5a, which cannot be deleted
            while (index < text.size && (char_is_digit(text[index]) || char_is_valid_identifier(text[index]))) {
                index += 1;
            }
            if (string_test_char(text, index, '.')) {
                index += 1;
                while (index < text.size && (char_is_digit(text[index]) || char_is_valid_identifier(text[index]))) {
                    index += 1;
                }
            }

            token.type = Syntax_Token_Type::LITERAL_NUMBER;
            token.options.literal_number = identifier_pool_add(lexer.identifier_pool, string_create_substring_static(&text, start_index, index));
        }
        else if (char_is_parenthesis(c))
        {
            index += 1;
            token.type = Syntax_Token_Type::PARENTHESIS;
            token.options.parenthesis = char_to_parenthesis(c);
        }
        else if (index + 1 < text.size && c == '/' && text[index + 1] == '/') {
            token.type = Syntax_Token_Type::COMMENT;
            token.options.comment = identifier_pool_add(lexer.identifier_pool, string_create_substring_static(&text, index, text.size));
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
                auto& op_str = syntax_operator_info((Syntax_Operator)i).string;
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
                token.type = Syntax_Token_Type::OPERATOR;
                token.options.op = (Syntax_Operator)longest_index;
            }
            else {
                index += 1;
                token.type = Syntax_Token_Type::UNEXPECTED_CHAR;
                token.options.unexpected = c;
            }
        }

        token.info.char_end = index;
        dynamic_array_push_back(&tokens, token);
    }

    // Add dummy token so we never have 0 token lines
    if (tokens.size == 0) {
        dynamic_array_push_back(&tokens, syntax_token_make_dummy());
    }
}

void lexer_reconstruct_line_text(Syntax_Line* line, int* editor_cursor)
{
    /*
    Algorithm:
        Tokenize Text
        Determine critical Spaces
        Determine Cursor-Token + Cursor offset (For reconstructing the cursor after formating)
        Determine Render Information before/after each Token
        Format Text based on tokens, update token-text mapping
        Set cursor
    */

    auto& text = line->text;
    auto& tokens = line->tokens;
    int cursor = -10;
    if (editor_cursor != 0) {
        cursor = *editor_cursor;
    }

    // Find critical spaces
    Array<int> critical_spaces = array_create_empty<int>(tokens.size + 1);
    SCOPE_EXIT(array_destroy(&critical_spaces));
    for (int i = 0; i < critical_spaces.size; i++)
    {
        auto& spaces = critical_spaces[i];

        // Gather information
        bool prev_is_critical = i > 0 ? is_space_critical(tokens[i - 1].type) : false;
        bool curr_is_critical = i < tokens.size ? is_space_critical(tokens[i].type) : false;;
        bool space_before_cursor = string_test_char(text, cursor - 1, ' ');
        bool space_after_cursor = string_test_char(text, cursor, ' ');
        // Space before/after is only necessary if the cursor is between the tokens 
        {
            int prev_end = i > 0 ? tokens[i - 1].info.char_end : 0;
            int curr_start = i < tokens.size ? tokens[i].info.char_start : text.size;
            if (!(cursor >= prev_end && cursor <= curr_start)) {
                space_before_cursor = false;
                space_after_cursor = false;
            }
        }

        // Determine critical spaces
        if (prev_is_critical && curr_is_critical)
        {
            spaces = 1;
            if (space_before_cursor && space_after_cursor) {
                spaces = 2;
            }
        }
        else if (prev_is_critical && !curr_is_critical)
        {
            spaces = 0;
            if (space_before_cursor) {
                spaces = 1;
            }
        }
        else if (!prev_is_critical && curr_is_critical)
        {
            spaces = 0;
            if (space_after_cursor) {
                spaces = 1;
            }
        }
        else {
            spaces = 0;
        }
    }

    // Find cursor token + offset
    int cursor_token = 0;
    int cursor_offset = 0;
    if (editor_cursor != 0)
    {
        cursor_token = syntax_line_character_to_token_index(line, *editor_cursor);
        {
            // Adjust the cursor offset for ignored characters inside tokens
            // "*  = |="  -> "*=|="
            auto& info = tokens[cursor_token].info;
            cursor_offset = cursor - info.char_start;

            String between = string_create_substring_static(&text, info.char_start, info.char_end);
            String should_be = syntax_token_as_string(tokens[cursor_token]);
            int should_index = 0;
            int between_index = 0;
            while (should_index < should_be.size)
            {
                assert(between_index < between.size, "");
                if (between[between_index] == should_be[should_index]) {
                    between_index++;
                    should_index++;
                }
                else {
                    if (cursor < info.char_start + between_index) {
                        cursor_offset--;
                    }
                    between_index++;
                }
            }

            if (info.char_start + cursor_offset > info.char_end) {
                cursor_offset = info.char_end - info.char_start + 1;
            }
        }
    }

    // Go through tokens and figure out Render-Spacing
    for (int i = 0; i < tokens.size; i++)
    {
        auto& token = tokens[i];
        auto& info = tokens[i].info;
        info.format_space_after = false;
        info.format_space_before = false;
        Syntax_Token_Type previous_type = i > 0 ? previous_type = tokens[i - 1].type : Syntax_Token_Type::UNEXPECTED_CHAR;
        Syntax_Token_Type next_type = i + 1 < tokens.size ? tokens[i + 1].type : Syntax_Token_Type::UNEXPECTED_CHAR;

        switch (token.type)
        {
        case Syntax_Token_Type::IDENTIFIER:
        case Syntax_Token_Type::LITERAL_NUMBER:
        case Syntax_Token_Type::LITERAL_STRING:
        case Syntax_Token_Type::LITERAL_BOOL:
        case Syntax_Token_Type::KEYWORD:
        case Syntax_Token_Type::DUMMY:
            break;
        case Syntax_Token_Type::COMMENT:
            info.format_space_before = i != 0;
            break;
        case Syntax_Token_Type::PARENTHESIS:
            if (!token.options.parenthesis.is_open && is_space_critical(next_type) && token.options.parenthesis.type != Parenthesis_Type::BRACKETS) {
                info.format_space_after = true;
            }
            break;
        case Syntax_Token_Type::OPERATOR:
        {
            auto& op_info = syntax_operator_info(token.options.op);
            switch (op_info.type)
            {
            case Operator_Type::BINOP: {
                info.format_space_before = op_info.space_before;
                info.format_space_after = op_info.space_after;
                break;
            }
            case Operator_Type::UNOP:
                info.format_space_before = is_space_critical(previous_type);
                break;
            case Operator_Type::BOTH: {
                // Determining if - or * is Binop or Unop can be quite hard, but I think this is a good approximation
                if (!(previous_type == Syntax_Token_Type::OPERATOR ||
                    (previous_type == Syntax_Token_Type::PARENTHESIS && tokens[i - 1].options.parenthesis.is_open) ||
                    (previous_type == Syntax_Token_Type::PARENTHESIS && tokens[i - 1].options.parenthesis.type == Parenthesis_Type::BRACKETS) ||
                    (previous_type == Syntax_Token_Type::KEYWORD) || i == 0))
                {
                    info.format_space_before = op_info.space_before;
                    info.format_space_after = op_info.space_after;
                }
                break;
            }
            default: panic("");
            }
            break;
        }
        case Syntax_Token_Type::UNEXPECTED_CHAR:
            info.format_space_after = true;
            info.format_space_before = true;
            break;
        default: panic("");
        }

        if (previous_type == Syntax_Token_Type::KEYWORD && !is_space_critical(token.type)) {
            info.format_space_before = true;
        }
    }

    // Add render spacing for critical spaces
    {
        if (critical_spaces[0] != 0) {
            tokens[0].info.format_space_before = true;
        }
        if (critical_spaces[critical_spaces.size - 1] != 0) {
            tokens[tokens.size - 1].info.format_space_after = true;
        }
        for (int i = 0; i < tokens.size - 1; i++) {
            auto& spaces = critical_spaces[i + 1];
            auto& before = tokens[i];
            auto& after = tokens[i + 1];
            if (spaces == 1) {
                before.info.format_space_after = true;
            }
            else if (spaces == 2) {
                before.info.format_space_after = true;
                after.info.format_space_before = true;
            }
        }
    }

    // Recreate text from tokens, updating char start and char length
    string_reset(&text);
    if (critical_spaces[0] != 0) string_append_character(&text, ' ');
    for (int i = 0; i < tokens.size; i++)
    {
        auto& token = tokens[i];
        auto& info = token.info;
        if (cursor_token == i && editor_cursor != 0) {
            *editor_cursor = text.size + cursor_offset;
        }
        token.info.char_start = text.size;
        String str = syntax_token_as_string(token);
        string_append_string(&text, &str);
        token.info.char_end = text.size;

        for (int j = 0; j < critical_spaces[i + 1]; j++) {
            string_append_character(&text, ' ');
        }
    }
}



// Creation
Syntax_Line* syntax_line_create(Syntax_Block* parent_block, int block_index) {
    assert(parent_block != 0, "");
    auto line = new Syntax_Line;
    line->tokens = dynamic_array_create_empty<Syntax_Token>(1);
    dynamic_array_push_back(&line->tokens, syntax_token_make_dummy());
    line->text = string_create_empty(1);
    line->follow_block = 0;
    line->parent_block = parent_block;
    dynamic_array_insert_ordered(&parent_block->lines, line, block_index);
    return line;
}

Syntax_Block* syntax_block_create(Syntax_Line* parent_line) {
    Syntax_Block* block = new Syntax_Block;
    block->lines = dynamic_array_create_empty<Syntax_Line*>(1);
    syntax_line_create(block, 0);
    block->parent_line = parent_line;
    if (parent_line != 0) {
        parent_line->follow_block = block;
    }
    return block;
}

void syntax_line_destroy(Syntax_Line* line) {
    if (line->follow_block != 0) {
        syntax_block_destroy(line->follow_block);
        line->follow_block = 0;
    }
    dynamic_array_destroy(&line->tokens);
    string_destroy(&line->text);
    delete line;
}

void syntax_block_destroy(Syntax_Block* block) {
    for (int i = 0; i < block->lines.size; i++) {
        syntax_line_destroy(block->lines[i]);
    }
    dynamic_array_destroy(&block->lines);
    delete block;
}

void syntax_block_fill_from_text(String* text, int* index, Syntax_Block* block, int indentation_level)
{
    // Get all characters into the string
    if (*index >= text->size) return;
    Syntax_Line* prev_line = block->lines[0];
    bool first_time = true;

    // Parse all lines
    while (*index < text->size)
    {
        // Find line indentation level
        int line_start_index = *index;
        int line_indent = 0;
        while (*index < text->size && text->characters[*index] == '\t') {
            line_indent += 1;
            *index += 1;
        }

        Syntax_Line* line = 0;
        if (line_indent < indentation_level) {
            *index = line_start_index;
            return;
        }
        else if (line_indent == indentation_level) {
            if (first_time) {
                line = block->lines[0];
            }
            else {
                line = syntax_line_create(block, block->lines.size);
            }
        }
        else {
            auto new_block = syntax_block_create(prev_line);
            new_block->info.indentation_level = indentation_level + 1;
            *index = line_start_index;
            syntax_block_fill_from_text(text, index, new_block, indentation_level + 1);
            continue;
        }
        first_time = false;
        prev_line = line;

        while (*index < text->size)
        {
            char c = text->characters[*index];
            if (c == '\n') {
                *index += 1;
                break;
            }
            if (c == '\t' || c == '\r') {
                *index += 1;
                continue;
            }
            string_append_character(&line->text, c);
            *index += 1;
        }
    }
}

Syntax_Block* syntax_block_create_from_string(String text)
{
    Syntax_Block* result = syntax_block_create(0);
    result->info.indentation_level = 0;
    int index = 0;
    syntax_block_fill_from_text(&text, &index, result, 0);
    return result;
}

void syntax_block_append_to_string(Syntax_Block* block, String* string, int indentation)
{
    for (int i = 0; i < block->lines.size; i++)
    {
        auto& line = block->lines[i];
        for (int j = 0; j < indentation; j++) {
            string_append_character(string, '\t');
        }
        string_append_string(string, &line->text);
        string_append_character(string, '\n');
        if (line->follow_block != 0) {
            syntax_block_append_to_string(line->follow_block, string, indentation + 1);
        }
    }
}

void syntax_block_sanity_check(Syntax_Block* block)
{
    assert(block->lines.size > 0, "");
    for (int i = 0; i < block->lines.size; i++) {
        auto line = block->lines[i];
        assert(line->parent_block == block, "");
        if (line->follow_block != 0) {
            assert(line->follow_block->parent_line == line, "");
            syntax_block_sanity_check(line->follow_block);
        }
    }
}




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

Operator_Info syntax_operator_info(Syntax_Operator op)
{
    switch (op)
    {
    case Syntax_Operator::ADDITION: return operator_info_make("+", Operator_Type::BINOP, true, true);
    case Syntax_Operator::SUBTRACTION: return operator_info_make("-", Operator_Type::BOTH, true, true);
    case Syntax_Operator::DIVISON: return operator_info_make("/", Operator_Type::BINOP, true, true);
    case Syntax_Operator::MULTIPLY: return operator_info_make("*", Operator_Type::BOTH, true, true);
    case Syntax_Operator::MODULO: return operator_info_make("%", Operator_Type::BINOP, true, true);
    case Syntax_Operator::COMMA: return operator_info_make(",", Operator_Type::BINOP, false, true);
    case Syntax_Operator::DOT: return operator_info_make(".", Operator_Type::BINOP, false, false);
    case Syntax_Operator::TILDE: return operator_info_make("~", Operator_Type::BINOP, false, false);
    case Syntax_Operator::COLON: return operator_info_make(":", Operator_Type::BINOP, false, true);
    case Syntax_Operator::NOT: return operator_info_make("!", Operator_Type::BINOP, false, false);
    case Syntax_Operator::AMPERSAND: return operator_info_make("&", Operator_Type::UNOP, false, false);
    case Syntax_Operator::LESS_THAN: return operator_info_make("<", Operator_Type::BINOP, true, true);
    case Syntax_Operator::GREATER_THAN: return operator_info_make(">", Operator_Type::BINOP, true, true);
    case Syntax_Operator::LESS_EQUAL: return operator_info_make("<=", Operator_Type::BINOP, true, true);
    case Syntax_Operator::GREATER_EQUAL: return operator_info_make(">=", Operator_Type::BINOP, true, true);
    case Syntax_Operator::EQUALS: return operator_info_make("==", Operator_Type::BINOP, true, true);
    case Syntax_Operator::NOT_EQUALS: return operator_info_make("!=", Operator_Type::BINOP, true, true);
    case Syntax_Operator::POINTER_EQUALS: return operator_info_make("*==", Operator_Type::BINOP, true, true);
    case Syntax_Operator::POINTER_NOT_EQUALS: return operator_info_make("*!=", Operator_Type::BINOP, true, true);
    case Syntax_Operator::DEFINE_COMPTIME: return operator_info_make("::", Operator_Type::BINOP, true, true);
    case Syntax_Operator::DEFINE_INFER: return operator_info_make(":=", Operator_Type::BINOP, true, true);
    case Syntax_Operator::AND: return operator_info_make("&&", Operator_Type::BINOP, true, true);
    case Syntax_Operator::OR: return operator_info_make("||", Operator_Type::BINOP, true, true);
    case Syntax_Operator::ARROW: return operator_info_make("->", Operator_Type::BINOP, true, true);
    case Syntax_Operator::DOLLAR: return operator_info_make("$", Operator_Type::UNOP, false, false);
    case Syntax_Operator::ASSIGN: return operator_info_make("=", Operator_Type::BINOP, true, true);
    case Syntax_Operator::ASSIGN_ADD: return operator_info_make("+=", Operator_Type::BINOP, true, true);
    case Syntax_Operator::ASSIGN_SUB: return operator_info_make("-=", Operator_Type::BINOP, true, true);
    case Syntax_Operator::ASSIGN_DIV: return operator_info_make("/=", Operator_Type::BINOP, true, true);
    case Syntax_Operator::ASSIGN_MULT: return operator_info_make("*=", Operator_Type::BINOP, true, true);
    default: panic("");
    }

    panic("");
    return operator_info_make("what", Operator_Type::BINOP, true, true);
}

String syntax_keyword_as_string(Syntax_Keyword keyword)
{
    switch (keyword)
    {
    case Syntax_Keyword::IMPORT: return string_create_static("#import");
    case Syntax_Keyword::BAKE: return string_create_static("#bake");
    case Syntax_Keyword::BREAK: return string_create_static("break");
    case Syntax_Keyword::CASE: return string_create_static("case");
    case Syntax_Keyword::CAST: return string_create_static("cast");
    case Syntax_Keyword::CAST_RAW: return string_create_static("cast_raw");
    case Syntax_Keyword::CAST_PTR: return string_create_static("cast_ptr");
    case Syntax_Keyword::CONTINUE: return string_create_static("continue");
    case Syntax_Keyword::C_UNION: return string_create_static("c_union");
    case Syntax_Keyword::DEFAULT: return string_create_static("default");
    case Syntax_Keyword::DEFER: return string_create_static("defer");
    case Syntax_Keyword::DELETE_KEYWORD: return string_create_static("delete");
    case Syntax_Keyword::ELSE: return string_create_static("else");
    case Syntax_Keyword::IF: return string_create_static("if");
    case Syntax_Keyword::MODULE: return string_create_static("module");
    case Syntax_Keyword::NEW: return string_create_static("new");
    case Syntax_Keyword::ENUM: return string_create_static("enum");
    case Syntax_Keyword::RETURN: return string_create_static("return");
    case Syntax_Keyword::STRUCT: return string_create_static("struct");
    case Syntax_Keyword::SWITCH: return string_create_static("switch");
    case Syntax_Keyword::UNION: return string_create_static("union");
    case Syntax_Keyword::WHILE: return string_create_static("while");
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

String syntax_token_as_string(Syntax_Token token)
{
    switch (token.type)
    {
    case Syntax_Token_Type::IDENTIFIER:
        return *token.options.identifier;
    case Syntax_Token_Type::KEYWORD:
        return syntax_keyword_as_string(token.options.keyword);
    case Syntax_Token_Type::LITERAL_NUMBER:
        return *token.options.literal_number;
    case Syntax_Token_Type::LITERAL_STRING:
        return *token.options.literal_string.string;
    case Syntax_Token_Type::LITERAL_BOOL:
        return string_create_static(token.options.literal_bool ? "true" : "false");
    case Syntax_Token_Type::OPERATOR:
        return syntax_operator_info(token.options.op).string;
    case Syntax_Token_Type::PARENTHESIS: {
        switch (token.options.parenthesis.type) {
        case Parenthesis_Type::BRACES:
            return token.options.parenthesis.is_open ? string_create_static("{") : string_create_static("}");
        case Parenthesis_Type::BRACKETS:
            return token.options.parenthesis.is_open ? string_create_static("[") : string_create_static("]");
        case Parenthesis_Type::PARENTHESIS:
            return token.options.parenthesis.is_open ? string_create_static("(") : string_create_static(")");
        default: panic("");
        }
        return string_create_static("0");
    }
    case Syntax_Token_Type::UNEXPECTED_CHAR:
        return string_create_static_with_size(&token.options.unexpected, 1);
    case Syntax_Token_Type::COMMENT:
        return *token.options.comment;
    case Syntax_Token_Type::DUMMY:
        return string_create_static("");
    default: panic("");
    }
    panic("");
    return string_create_static("ERROR");
}

Syntax_Token syntax_token_make_dummy() {
    Syntax_Token dummy;
    dummy.type = Syntax_Token_Type::DUMMY;
    dummy.info.char_start = 0;
    dummy.info.char_end = 0;
    dummy.info.format_space_after = false;
    dummy.info.format_space_before = false;
    dummy.info.screen_pos = 0;
    dummy.info.screen_size = 1;
    return dummy;
}

void syntax_line_print_tokens(Syntax_Line* line)
{
    String output = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&output));
    string_append_formated(&output, "--------------\nTOKENS\n----------------\n");

    for (int i = 0; i < line->tokens.size; i++)
    {
        auto& token = line->tokens[i];
        string_append_formated(&output, "#%d: ", i);
        switch (token.type)
        {
        case Syntax_Token_Type::IDENTIFIER:
            string_append_formated(&output, "Identifier");
            break;
        case Syntax_Token_Type::KEYWORD:
            string_append_formated(&output, "Keyword");
            break;
        case Syntax_Token_Type::COMMENT:
            string_append_formated(&output, "Comment");
            break;
        case Syntax_Token_Type::PARENTHESIS:
            string_append_formated(&output, "Parenthesis");
            break;
        case Syntax_Token_Type::OPERATOR:
            string_append_formated(&output, "Operator");
            break;
        case Syntax_Token_Type::LITERAL_NUMBER:
            string_append_formated(&output, "Literal");
            break;
        case Syntax_Token_Type::LITERAL_STRING:
            string_append_formated(&output, "Literal_String");
            break;
        case Syntax_Token_Type::UNEXPECTED_CHAR:
            string_append_formated(&output, "Unexpected Character");
            break;
        default: panic("");
        }

        String substr = syntax_token_as_string(token);
        string_append_formated(&output, " ");
        string_append_string(&output, &substr);

        string_append_formated(&output, "\n");
    }

    logg(output.characters);
}


// Editing
bool syntax_line_is_multi_line_comment(Syntax_Line* line)
{
    auto comment_start = string_create_static("//");
    if (string_equals(&line->text, &comment_start)) return true;
    return false;
}

bool syntax_line_is_comment(Syntax_Line* line)
{
    auto comment_start = string_create_static("//");
    if (string_compare_substring(&line->text, 0, &comment_start)) return true;
    auto parent = line->parent_block->parent_line;
    if (parent == 0) return false;
    return syntax_line_is_multi_line_comment(parent);
}

bool syntax_line_is_empty(Syntax_Line* line) {
    if (line->text.size == 0) return true;
    if (syntax_line_is_comment(line)) return true;
    return false;
}


int syntax_line_index(Syntax_Line* line) {
    auto block = line->parent_block;
    for (int i = 0; i < block->lines.size; i++) {
        if (block->lines[i] == line) return i;
    }
    panic("Hey");
    return 0;
}

void syntax_line_move(Syntax_Line* line, Syntax_Block* block, int index) {
    // remove from current block
    Syntax_Block* old_block = line->parent_block;
    int line_index = syntax_line_index(line);
    if (old_block == block && index == line_index) return;
    if (old_block == block && index > line_index) {
        index -= 1;
    }
    dynamic_array_remove_ordered(&old_block->lines, line_index);
    if (old_block->lines.size == 0) {
        if (old_block->parent_line != 0) {
            old_block->parent_line->follow_block = 0;
        }
        syntax_block_destroy(old_block);
    }
    dynamic_array_insert_ordered(&block->lines, line, index);
    line->parent_block = block;
}

Syntax_Line* syntax_line_prev_line(Syntax_Line* line) {
    auto line_index = syntax_line_index(line);
    auto block = line->parent_block;
    if (line_index <= 0) {
        if (line->parent_block->parent_line == 0) return line;
        return line->parent_block->parent_line;
    }
    auto upper = line->parent_block->lines[line_index - 1];
    while (upper->follow_block != 0) {
        upper = dynamic_array_last(&upper->follow_block->lines);
    }
    return upper;
}

Syntax_Line* syntax_line_next_line(Syntax_Line* line) {
    if (line->follow_block != 0) {
        return line->follow_block->lines[0];
    }
    auto original = line;
    while (true)
    {
        auto block = line->parent_block;
        auto index = syntax_line_index(line);
        if (index + 1 < block->lines.size) {
            return block->lines[index + 1];
        }
        line = line->parent_block->parent_line;
        if (line == 0) {
            return original;
        }
    }
}

void syntax_line_remove_token(Syntax_Line* line, int index)
{
    auto& tokens = line->tokens;
    assert(tokens.size > 0 && index < tokens.size, "");
    dynamic_array_remove_ordered(&tokens, index);
    if (tokens.size == 0) {
        dynamic_array_push_back(&tokens, syntax_token_make_dummy());
    }
}

int syntax_line_character_to_token_index(Syntax_Line* line, int char_index)
{
    auto& tokens = line->tokens;
    int cursor_token = 0;
    for (int i = tokens.size - 1; i >= 0; i--)
    {
        auto& token = tokens[i];
        if (char_index >= token.info.char_start) {
            cursor_token = i;
            break;
        }
    }
    return cursor_token;
}



// Navigation
bool syntax_position_on_line(Syntax_Position pos) {
    return pos.line_index < pos.block->lines.size&& pos.line_index >= 0;
}

bool syntax_position_on_token(Syntax_Position pos) {
    if (!syntax_position_on_line(pos)) {
        return false;
    }
    auto line = pos.block->lines[pos.line_index];
    if (line->text.size == 0) return false;
    return pos.token_index < line->tokens.size&& pos.token_index >= 0;
}

Syntax_Line* syntax_position_get_line(Syntax_Position pos)
{
    assert(syntax_position_on_line(pos), "");
    return pos.block->lines[pos.line_index];
}

Syntax_Token* syntax_position_get_token(Syntax_Position pos)
{
    assert(syntax_position_on_token(pos), "");
    return &syntax_position_get_line(pos)->tokens[pos.token_index];
}

Syntax_Position syntax_line_get_start_pos(Syntax_Line* line)
{
    Syntax_Position pos;
    pos.block = line->parent_block;
    pos.line_index = syntax_line_index(line);
    pos.token_index = 0;
    return pos;
}

Syntax_Position syntax_line_get_end_pos(Syntax_Line* line)
{
    Syntax_Position pos;
    pos.block = line->parent_block;
    pos.line_index = syntax_line_index(line);
    pos.token_index = line->tokens.size;
    return pos;
}

bool syntax_position_in_order(Syntax_Position a, Syntax_Position b)
{
    assert(syntax_position_on_line(a) && syntax_position_on_line(b), "");
    while (a.block != b.block)
    {
        if (a.block->info.indentation_level > b.block->info.indentation_level) {
            a = syntax_line_get_end_pos(a.block->parent_line);
        }
        else {
            b = syntax_line_get_end_pos(b.block->parent_line);
        }
    }
    if (a.line_index < b.line_index) {
        return true;
    }
    if (a.line_index > b.line_index) {
        return false;
    }
    if (a.token_index == b.token_index) return true;
    return a.token_index < b.token_index;
}

bool syntax_position_equal(Syntax_Position a, Syntax_Position b) {
    return a.block == b.block && a.line_index == b.line_index && a.token_index == b.token_index;
}

bool syntax_range_contains(Syntax_Range range, Syntax_Position pos) {
    return syntax_position_in_order(range.start, pos) && syntax_position_in_order(pos, range.end);
}

Syntax_Position syntax_position_advance_one_line(Syntax_Position a)
{
    assert(syntax_position_on_line(a), "");
    auto line = syntax_position_get_line(a);
    auto next_line = syntax_line_next_line(line);
    if (next_line == line) return a;
    Syntax_Position next;
    next.line_index = syntax_line_index(next_line);
    next.block = next_line->parent_block;
    next.token_index = 0;
    return next;
}

Syntax_Position syntax_position_advance_one_token(Syntax_Position a)
{
    assert(syntax_position_on_line(a), "");
    auto line = syntax_position_get_line(a);
    if (a.token_index < line->tokens.size) {
        a.token_index += 1;
        return a;
    }
    return syntax_position_advance_one_line(a);
}

Syntax_Position syntax_position_sanitize(Syntax_Position pos)
{
    assert(pos.block != 0, "Must not happen");
    pos.line_index = math_maximum(0, pos.line_index);
    pos.token_index = math_maximum(0, pos.token_index);
    if (pos.line_index >= pos.block->lines.size) {
        pos.line_index = pos.block->lines.size - 1;
        pos.token_index = dynamic_array_last(&pos.block->lines)->tokens.size;
        return pos;
    }
    auto line = syntax_position_get_line(pos);
    if (pos.token_index > line->tokens.size) {
        pos.token_index = line->tokens.size;
    }
    return pos;
}



