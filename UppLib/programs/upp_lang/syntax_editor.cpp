#include "syntax_editor.hpp"

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/renderer_2d.hpp"
#include "../../utility/file_io.hpp"

#include "../../win32/input.hpp"
#include "syntax_colors.hpp"
#include "compiler.hpp"
#include "ast.hpp"
#include "rc_analyser.hpp"
#include "semantic_analyser.hpp"
#include "parser.hpp"

// Datatypes
enum class Operator_Type
{
    BINOP,
    UNOP,
    BOTH,
};

struct Operator_Info
{
    String string;
    Operator_Type type;
    bool space_before;
    bool space_after;
};

enum class Editor_Mode
{
    NORMAL,
    INPUT,
};

struct Syntax_Editor
{
    // Editing
    Editor_Mode mode;
    Syntax_Block* root_block;
    int cursor_index; 
    Syntax_Line* cursor_line;

    Hashtable<String, Syntax_Keyword> keyword_table;
    Array<String> keyword_mapping;
    Identifier_Pool* identifier_pool;

    String context_text;

    // Rendering
    Input* input;
    Rendering_Core* rendering_core;
    Renderer_2D* renderer_2D;
    Text_Renderer* text_renderer;
    vec2 character_size;

    Compiler* compiler;
};

enum class Input_Command_Type
{
    IDENTIFIER_LETTER,
    NUMBER_LETTER,
    DELIMITER_LETTER,
    SPACE,
    ENTER,
    ENTER_REMOVE_ONE_INDENT,
    EXIT_INSERT_MODE,
    BACKSPACE,
    ADD_INDENTATION,
    REMOVE_INDENTATION,
    MOVE_LEFT,
    MOVE_RIGHT,
};

struct Input_Command
{
    Input_Command_Type type;
    char letter;
};

enum class Normal_Command
{
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_UP,
    MOVE_DOWN,
    MOVE_LINE_START,
    MOVE_LINE_END,
    ADD_LINE_ABOVE,
    ADD_LINE_BELOW,
    INSERT_BEFORE,
    INSERT_AFTER,
    INSERT_AT_LINE_START,
    INSERT_AT_LINE_END,
    CHANGE_TOKEN,
    DELETE_TOKEN,
};




// Globals
static Syntax_Editor syntax_editor;

// PROTOTYPES
void syntax_editor_sanitize_cursor();
int syntax_line_index(Syntax_Line* line);
void syntax_editor_save_text_file(const char* filename);

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
    case Syntax_Operator::HASHTAG: return operator_info_make("#", Operator_Type::UNOP, false, false);
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

// Helpers
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

bool char_is_digit(int c) {
    return (c >= '0' && c <= '9');
}

bool char_is_letter(int c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

bool char_is_valid_identifier(int c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        (c == '_');
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
    auto& editor = syntax_editor;
    switch (token.type)
    {
    case Syntax_Token_Type::IDENTIFIER:
        return *token.options.identifier;
    case Syntax_Token_Type::KEYWORD:
        return editor.keyword_mapping[(int)token.options.keyword];
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
    case Syntax_Token_Type::GAP:
        return string_create_static(" ");
    case Syntax_Token_Type::DUMMY:
        return string_create_static("");
    default: panic("");
    }
    panic("");
    return string_create_static("ERROR");
}

bool is_space_critical(Syntax_Token_Type type) {
    return type == Syntax_Token_Type::IDENTIFIER || type == Syntax_Token_Type::KEYWORD || type == Syntax_Token_Type::LITERAL_NUMBER ||
        type == Syntax_Token_Type::LITERAL_BOOL;
}

bool string_test_char(String str, int char_index, char c)
{
    if (char_index > str.size) return false;
    return str[char_index] == c;
}

int get_cursor_token_index()
{
    auto& cursor = syntax_editor.cursor_index;
    auto& tokens = syntax_editor.cursor_line->tokens;

    int cursor_token = 0;
    for (int i = tokens.size - 1; i >= 0; i--)
    {
        auto& token = tokens[i];
        if (cursor >= token.info.char_start) {
            cursor_token = i;
            break;
        }
    }
    return cursor_token;
}

Syntax_Token get_cursor_token()
{
    auto& cursor = syntax_editor.cursor_index;
    auto& tokens = syntax_editor.cursor_line->tokens;
    return tokens[get_cursor_token_index()];
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

bool syntax_position_on_line(Syntax_Position pos) {
    return pos.line_index < pos.block->lines.size && pos.line_index >= 0;
}

bool syntax_position_on_token(Syntax_Position pos) {
    if (!syntax_position_on_line(pos)) {
        return false;
    }
    auto line = pos.block->lines[pos.line_index];
    if (line->text.size == 0) return false;
    return pos.token_index < line->tokens.size && pos.token_index >= 0;
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

bool syntax_position_in_order(Syntax_Position a, Syntax_Position b) 
{
    while (a.block != b.block)
    {
        if (a.block->info.indentation_level > b.block->info.indentation_level) {
            a = syntax_line_get_start_pos(syntax_position_get_line(a)->parent_block->parent_line);
        }
        else {
            b = syntax_line_get_start_pos(syntax_position_get_line(b)->parent_block->parent_line);
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



// Lexer
void line_tokenize_text(Syntax_Line* line)
{
    auto& text = line->text;
    auto& tokens = line->tokens;
    dynamic_array_reset(&line->tokens);

    int index = 0;
    while (index < text.size)
    {
        Syntax_Token token;
        memory_zero(&token.info);
        token.info.char_start = index;

        char c = text[index];
        if (char_is_letter(c))
        {
            // Identifier/Keyword
            int start_index = index;
            index += 1;
            while (index < text.size && char_is_valid_identifier(text[index])) {
                index += 1;
            }
            token.type = Syntax_Token_Type::IDENTIFIER;
            token.options.identifier = identifier_pool_add(syntax_editor.identifier_pool, string_create_substring_static(&text, start_index, index));

            // Determine if its a keyword
            Syntax_Keyword* keyword = hashtable_find_element(&syntax_editor.keyword_table, *token.options.identifier);
            if (keyword != 0) {
                token.type = Syntax_Token_Type::KEYWORD;
                token.options.keyword = *keyword;
            }
            else if (string_equals_cstring(token.options.identifier, "true")){
                token.type = Syntax_Token_Type::LITERAL_BOOL;
                token.options.literal_bool = true;
            }
            else if (string_equals_cstring(token.options.identifier, "false")){
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
            token.options.literal_string.string = identifier_pool_add(syntax_editor.identifier_pool, string_create_substring_static(&text, start_index, index));
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
            while (index < text.size && (char_is_digit(text[index]) || char_is_valid_identifier(text[index]))) {
                index += 1;
            }
            token.type = Syntax_Token_Type::LITERAL_NUMBER;
            token.options.literal_number = identifier_pool_add(syntax_editor.identifier_pool, string_create_substring_static(&text, start_index, index));
        }
        else if (char_is_parenthesis(c))
        {
            index += 1;
            token.type = Syntax_Token_Type::PARENTHESIS;
            token.options.parenthesis = char_to_parenthesis(c);
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

void line_format_text_from_tokens(Syntax_Line* line)
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
    auto& cursor = syntax_editor.cursor_index;
    auto& tokens = line->tokens;

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
            if (!(cursor >= prev_end && cursor <= curr_start) || syntax_editor.mode == Editor_Mode::NORMAL) {
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
    if (syntax_editor.cursor_line == line)
    {
        cursor_token = get_cursor_token_index();
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
        case Syntax_Token_Type::GAP:
        case Syntax_Token_Type::KEYWORD:
        case Syntax_Token_Type::DUMMY:
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

    // Format the original text, updating char start and char length
    String new_text = string_create_empty(text.size);
    int new_cursor = 0;
    if (critical_spaces[0] != 0) string_append_character(&text, ' ');
    for (int i = 0; i < tokens.size; i++)
    {
        auto& token = tokens[i];
        auto& info = token.info;
        if (cursor_token == i) {
            new_cursor = new_text.size + cursor_offset;
        }
        token.info.char_start = new_text.size;
        String str = syntax_token_as_string(token);
        string_append_string(&new_text, &str);
        token.info.char_end = new_text.size;

        for (int j = 0; j < critical_spaces[i + 1]; j++) {
            string_append_character(&new_text, ' ');
        }
    }

    // Replace original text with formated text
    {
        string_destroy(&text);
        text = new_text;
        if (syntax_editor.cursor_line == line) {
            cursor = new_cursor;
            syntax_editor_sanitize_cursor();
        }
    }
}

void line_print_tokens(Syntax_Line* line)
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
            //string_append_formated(&output, item.options.identifier->characters);
            break;
        case Syntax_Token_Type::KEYWORD:
            string_append_formated(&output, "Keyword");
            //string_append_string(&output, &editor->keyword_mapping[(int)item.options.keyword]);
            break;
        case Syntax_Token_Type::GAP:
            string_append_formated(&output, "GAP");
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

void syntax_block_destroy(Syntax_Block* block);
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

Syntax_Line* navigate_prev_line(Syntax_Line* line) {
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

Syntax_Line* navigate_next_line(Syntax_Line* line) {
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



void line_remove_token(Syntax_Line* line, int index)
{
    auto& tokens = line->tokens;
    assert(tokens.size > 0 && index < tokens.size, "");
    dynamic_array_remove_ordered(&tokens, index);
    if (tokens.size == 0) {
        dynamic_array_push_back(&tokens, syntax_token_make_dummy());
    }
}

void syntax_editor_sanitize_cursor()
{
    auto& editor = syntax_editor;
    assert(editor.cursor_line != 0, "");
    auto& text = editor.cursor_line->text;
    auto& cursor = editor.cursor_index;
    cursor = math_clamp(cursor, 0, editor.mode == Editor_Mode::INPUT ? text.size : math_maximum(0, text.size - 1));
    if (editor.mode == Editor_Mode::NORMAL) {
        cursor = get_cursor_token().info.char_start;
    }
}

void normal_mode_handle_command(Normal_Command command)
{
    auto& editor = syntax_editor;
    auto& line = editor.cursor_line;
    auto& block = line->parent_block;
    auto& cursor = editor.cursor_index;
    auto& mode = editor.mode;
    auto& tokens = line->tokens;

    bool tokens_changed = false;
    switch (command)
    {
    case Normal_Command::INSERT_AFTER: {
        mode = Editor_Mode::INPUT;
        cursor = get_cursor_token().info.char_end;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::INSERT_BEFORE: {
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::MOVE_LEFT: {
        int cursor_token = math_maximum(get_cursor_token_index() - 1, 0);
        if (cursor_token < tokens.size) {
            cursor = tokens[cursor_token].info.char_start;
        }
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::MOVE_RIGHT: {
        int cursor_token = math_minimum(get_cursor_token_index() + 1, tokens.size);
        if (cursor_token < tokens.size) {
            cursor = tokens[cursor_token].info.char_start;
        }
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::INSERT_AT_LINE_END: {
        cursor = line->text.size;
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::INSERT_AT_LINE_START: {
        cursor = 0;
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::DELETE_TOKEN: {
        auto index = get_cursor_token_index();
        line_remove_token(line, index);
        if (index > 0) {
            cursor = tokens[index - 1].info.char_end + 1;
        }
        tokens_changed = true;
        break;
    }
    case Normal_Command::CHANGE_TOKEN: {
        line_remove_token(line, get_cursor_token_index());
        tokens_changed = true;
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::MOVE_LINE_START: {
        cursor = 0;
        break;
    }
    case Normal_Command::MOVE_LINE_END: {
        cursor = tokens[tokens.size - 1].info.char_start;
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::ADD_LINE_ABOVE:
    case Normal_Command::ADD_LINE_BELOW: {
        bool below = command == Normal_Command::ADD_LINE_BELOW;
        line = syntax_line_create(line->parent_block, syntax_line_index(line) + (below ? 1 : 0));
        cursor = 0;
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::MOVE_UP: {
        line = navigate_prev_line(line);
        syntax_editor_sanitize_cursor();
        break;
    }
    case Normal_Command::MOVE_DOWN: {
        line = navigate_next_line(line);
        syntax_editor_sanitize_cursor();
        break;
    }
    default: panic("");
    }

    if (tokens_changed) {
        int cursor_backup = cursor;
        line_format_text_from_tokens(line);
        line_tokenize_text(line); // To rejoin operators, like ": int =" -> ":="
        cursor = cursor_backup;
        syntax_editor_sanitize_cursor();
    }
}

void insert_mode_handle_command(Input_Command input)
{
    auto& mode = syntax_editor.mode;
    auto& line = syntax_editor.cursor_line;
    auto& text = line->text;
    auto& cursor = syntax_editor.cursor_index;

    assert(mode == Editor_Mode::INPUT, "");
    syntax_editor_sanitize_cursor();

    // Handle Universal Inputs
    if (input.type == Input_Command_Type::EXIT_INSERT_MODE) {
        mode = Editor_Mode::NORMAL;
        //cursor -= 1;
        syntax_editor_sanitize_cursor();
        return;
    }
    if (input.type == Input_Command_Type::ENTER) 
    {
        auto old_line = line;
        auto& old_text = old_line->text;
        line = syntax_line_create(line->parent_block, syntax_line_index(line) + 1);
        auto cutout = string_create_substring_static(&old_text, cursor, old_text.size);
        string_append_string(&line->text, &cutout);
        string_truncate(&old_text, cursor);

        if (old_line->follow_block != 0) {
            line->follow_block = old_line->follow_block;
            old_line->follow_block = 0;
            line->follow_block->parent_line = line;
        }

        cursor = 0;
        return;
    }
    if (input.type == Input_Command_Type::ADD_INDENTATION) 
    {
        auto old_line = line;
        auto& old_text = old_line->text;
        auto cutout = string_create_substring_static(&old_text, cursor, old_text.size);
        int line_index = syntax_line_index(line);

        if (cursor == 0 && line_index > 0) 
        {
            Syntax_Line* add_to_line = line->parent_block->lines[line_index - 1];
            if (add_to_line->follow_block == 0) 
            {
                Syntax_Block* block = syntax_block_create(add_to_line);
                syntax_line_destroy(block->lines[0]);
                block->lines[0] = line;
                auto old_block = line->parent_block;
                line->parent_block = block;
                dynamic_array_remove_ordered(&old_block->lines, line_index);
            }
            else {
                syntax_line_move(line, add_to_line->follow_block, add_to_line->follow_block->lines.size);
            }
            return;
        }

        if (line->follow_block == 0) syntax_block_create(line);
        else if (line->follow_block->lines[0]->text.size != 0){
            syntax_line_create(line->follow_block, 0);
        }
        line = line->follow_block->lines[0];

        string_insert_string(&line->text, &cutout, 0);
        string_truncate(&old_text, cursor);
        cursor = 0;
        return;
    }
    if (input.type == Input_Command_Type::ENTER_REMOVE_ONE_INDENT) 
    {
        auto old_line = line;
        auto& old_text = old_line->text;

        auto cutout = string_create_substring_static(&old_text, cursor, old_text.size);
        if (line->parent_block->parent_line == 0) return;
        line = line->parent_block->parent_line;
        line = syntax_line_create(line->parent_block, syntax_line_index(line) + 1);

        string_insert_string(&line->text, &cutout, 0);
        string_truncate(&old_text, cursor);
        cursor = 0;

        if (old_line->follow_block != 0) {
            line->follow_block = old_line->follow_block;
            old_line->follow_block = 0;
            line->follow_block->parent_line = line;
        }

        return;
    }
    if (input.type == Input_Command_Type::REMOVE_INDENTATION) 
    {
        auto old_block = line->parent_block;
        auto& block = line->parent_block;
        if (block->parent_line == 0) return;

        int parent_line_index = syntax_line_index(block->parent_line);
        dynamic_array_remove_ordered(&block->lines, syntax_line_index(line));
        block = block->parent_line->parent_block;
        dynamic_array_insert_ordered(&block->lines, line, parent_line_index + 1);

        if (old_block->lines.size == 0) {
            old_block->parent_line->follow_block = 0;
            syntax_block_destroy(old_block);
        }
        return;
    }

    if (input.type == Input_Command_Type::MOVE_LEFT) {
        cursor = math_maximum(0, cursor - 1);
        return;
    }
    if (input.type == Input_Command_Type::MOVE_RIGHT) {
        cursor = math_minimum(line->text.size, cursor + 1);
        return;
    }

    switch (input.type)
    {
    case Input_Command_Type::DELIMITER_LETTER:
    {
        bool insert_double_after = false;
        bool skip_auto_input = false;
        char double_char = ' ';
        if (char_is_parenthesis(input.letter))
        {
            Parenthesis p = char_to_parenthesis(input.letter);
            // Check if line is properly parenthesised with regards to the one token I currently am on
            // This is easy to check, but design wise I feel like I only want to check the token I am on
            if (p.is_open)
            {
                int open_count = 0;
                int closed_count = 0;
                for (int i = 0; i < text.size; i++) {
                    char c = text[i];
                    if (!char_is_parenthesis(c)) continue;
                    Parenthesis found = char_to_parenthesis(c);
                    if (found.type == p.type) {
                        if (found.is_open) open_count += 1;
                        else closed_count += 1;
                    }
                }
                insert_double_after = open_count == closed_count;
                if (insert_double_after) {
                    p.is_open = false;
                    double_char = parenthesis_to_char(p);
                }
            }
            else {
                skip_auto_input = cursor < text.size&& text[cursor] == input.letter;
            }
        }
        if (input.letter == '"') {
            if (cursor < text.size && text[cursor] == '"') {
                skip_auto_input = true;
            }
            else {
                int count = 0;
                for (int i = 0; i < text.size; i++) {
                    if (text[i] == '"') count += 1;
                }
                if (count % 2 == 0) {
                    insert_double_after = true;
                    double_char = '"';
                }
            }
        }
        if (skip_auto_input) {
            cursor += 1;
            break;
        }
        if (insert_double_after) {
            string_insert_character_before(&text, double_char, cursor);
        }
        string_insert_character_before(&text, input.letter, cursor);
        cursor += 1;
        break;
    }
    case Input_Command_Type::SPACE:
        string_insert_character_before(&text, ' ', cursor);
        cursor += 1;
        break;
    case Input_Command_Type::BACKSPACE: 
    {
        if (cursor > 0) {
            string_remove_character(&text, cursor - 1);
            cursor -= 1;
            break;
        }
        // Cases: First line in a block or not
        auto line_index = syntax_line_index(line);
        auto block = line->parent_block;
        Syntax_Line* combine_with = navigate_prev_line(line);
        if (combine_with == line) {
            break;
        }

        cursor = combine_with->text.size;
        string_append_string(&combine_with->text, &line->text);
        string_reset(&line->text);

        if (line->follow_block != 0)
        {
            if (line_index == 0 && block->lines.size > 1) {

            }
            else {
                auto follow = line->follow_block;
                line->follow_block = 0;
                combine_with->follow_block = follow;
                follow->parent_line = combine_with;
            }
        }
        else {
            if (block->lines.size > 1) {
                syntax_line_destroy(line);
                dynamic_array_remove_ordered(&block->lines, line_index);
            }
            else {
                syntax_block_destroy(block);
                combine_with->follow_block = 0;
            }
        }

        line = combine_with;
        //line.indentation_level = math_maximum(0, line.indentation_level - 1);
        // TODO: Merge this line with last one...
        break;
    }
    case Input_Command_Type::IDENTIFIER_LETTER:
        string_insert_character_before(&text, input.letter, cursor);
        cursor += 1;
        break;
    case Input_Command_Type::NUMBER_LETTER:
        string_insert_character_before(&text, input.letter, cursor);
        cursor += 1;
        break;
    default: break;
    }
    syntax_editor_sanitize_cursor();
}

void check_block_integrity(Syntax_Block* block)
{
    assert(!(block->parent_line == 0 && block != syntax_editor.root_block), "");
    assert(block->lines.size > 0, "");
    for (int i = 0; i < block->lines.size; i++) {
        auto line = block->lines[i];
        assert(line->parent_block == block, "");
        if (line->follow_block != 0) {
            assert(line->follow_block->parent_line == line, "");
            check_block_integrity(line->follow_block);
        }
    }

}

void syntax_editor_update()
{
    auto& input = syntax_editor.input;
    auto& mode = syntax_editor.mode;
    for (int i = 0; i < input->key_messages.size; i++)
    {
        Key_Message msg = input->key_messages[i];
        if (mode == Editor_Mode::INPUT)
        {
            Input_Command input;
            if (msg.character == 32 && msg.key_down) {
                input.type = Input_Command_Type::SPACE;
            }
            else if (msg.key_code == Key_Code::L && msg.key_down && msg.ctrl_down) {
                input.type = Input_Command_Type::EXIT_INSERT_MODE;
            }
            else if (msg.key_code == Key_Code::ARROW_LEFT && msg.key_down) {
                input.type = Input_Command_Type::MOVE_LEFT;
            }
            else if (msg.key_code == Key_Code::ARROW_RIGHT && msg.key_down) {
                input.type = Input_Command_Type::MOVE_RIGHT;
            }
            else if (msg.key_code == Key_Code::BACKSPACE && msg.key_down) {
                input.type = Input_Command_Type::BACKSPACE;
            }
            else if (msg.key_code == Key_Code::RETURN && msg.key_down) {
                if (msg.shift_down) {
                    input.type = Input_Command_Type::ENTER_REMOVE_ONE_INDENT;
                }
                else {
                    input.type = Input_Command_Type::ENTER;
                }
            }
            else if (char_is_letter(msg.character) || msg.character == '_') {
                input.type = Input_Command_Type::IDENTIFIER_LETTER;
                input.letter = msg.character;
            }
            else if (char_is_digit(msg.character)) {
                input.type = Input_Command_Type::NUMBER_LETTER;
                input.letter = msg.character;
            }
            else if (msg.key_code == Key_Code::TAB && msg.key_down) {
                if (msg.shift_down) {
                    input.type = Input_Command_Type::REMOVE_INDENTATION;
                }
                else {
                    input.type = Input_Command_Type::ADD_INDENTATION;
                }
            }
            else if (msg.key_down && msg.character != -1) {
                if (string_contains_character(characters_get_non_identifier_non_whitespace(), msg.character)) {
                    input.type = Input_Command_Type::DELIMITER_LETTER;
                    input.letter = msg.character;
                }
                else {
                    continue;
                }
            }
            else {
                continue;
            }
            insert_mode_handle_command(input);
        }
        else
        {
            Normal_Command command;
            if (msg.key_code == Key_Code::L && msg.key_down) {
                command = Normal_Command::MOVE_RIGHT;
            }
            else if (msg.key_code == Key_Code::H && msg.key_down) {
                command = Normal_Command::MOVE_LEFT;
            }
            else if (msg.key_code == Key_Code::J && msg.key_down) {
                command = Normal_Command::MOVE_DOWN;
            }
            else if (msg.key_code == Key_Code::K && msg.key_down) {
                command = Normal_Command::MOVE_UP;
            }
            else if (msg.key_code == Key_Code::O && msg.key_down) {
                if (msg.shift_down) {
                    command = Normal_Command::ADD_LINE_ABOVE;
                }
                else {
                    command = Normal_Command::ADD_LINE_BELOW;
                }
            }
            else if (msg.key_code == Key_Code::NUM_0 && msg.key_down) {
                command = Normal_Command::MOVE_LINE_START;
            }
            else if (msg.key_code == Key_Code::NUM_4 && msg.key_down && msg.shift_down) {
                command = Normal_Command::MOVE_LINE_END;
            }
            else if (msg.key_code == Key_Code::A && msg.key_down) {
                if (msg.shift_down) {
                    command = Normal_Command::INSERT_AT_LINE_END;
                }
                else {
                    command = Normal_Command::INSERT_AFTER;
                }
            }
            else if (msg.key_code == Key_Code::I && msg.key_down) {
                if (msg.shift_down) {
                    command = Normal_Command::INSERT_AT_LINE_START;
                }
                else {
                    command = Normal_Command::INSERT_BEFORE;
                }
            }
            else if (msg.key_code == Key_Code::R && msg.key_down) {
                command = Normal_Command::CHANGE_TOKEN;
            }
            else if (msg.key_code == Key_Code::X && msg.key_down) {
                command = Normal_Command::DELETE_TOKEN;
            }
            else {
                continue;
            }
            normal_mode_handle_command(command);
        }
    }
    check_block_integrity(syntax_editor.root_block);

    auto module = Parser::execute(syntax_editor.root_block);
    auto& compiler = syntax_editor.compiler;

    if (syntax_editor.input->key_pressed[(int)Key_Code::S] && syntax_editor.input->key_down[(int)Key_Code::CTRL]) {
        syntax_editor_save_text_file("upp_code/editor_text.upp");
        logg("\nSaved file\n");
    }

    if (syntax_editor.input->key_pressed[(int)Key_Code::F5])
    {
        AST::base_print(&module->base);
        compiler_compile(compiler, module, true);
        for (int i = 0; i < compiler->rc_analyser->errors.size; i++) {
            Symbol_Error error = compiler->rc_analyser->errors[i];
            logg("Symbol error: Redefinition of \"%s\"\n", error.existing_symbol->id->characters);
        }
        {
            String tmp = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&tmp));
            for (int i = 0; i < compiler->semantic_analyser->errors.size; i++)
            {
                Semantic_Error e = compiler->semantic_analyser->errors[i];
                semantic_error_append_to_string(e, &tmp);
                logg("Semantic Error: %s\n", tmp.characters);
                string_reset(&tmp);
            }
        }
        if (!compiler_errors_occured(compiler)) {
            compiler_execute(compiler);
        }
    }
    else {
        compiler_compile(compiler, module, false);
    }
}

void parse_text_syntax_block(String* text, int* index, Syntax_Block* block, int indentation_level)
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
            *index = line_start_index;
            parse_text_syntax_block(text, index, new_block, indentation_level + 1);
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

void syntax_editor_load_text_file(const char* filename)
{
    auto& editor = syntax_editor;
    Optional<String> content = file_io_load_text_file("upp_code/editor_text.upp");
    SCOPE_EXIT(file_io_unload_text_file(&content););

    String result;
    if (content.available) {
        result = content.value;
    }
    else {
        result = string_create_static("main :: (x : int) -> void \n{\n\n}");
    }

    syntax_block_destroy(editor.root_block);
    editor.root_block = syntax_block_create(0);
    editor.cursor_line = editor.root_block->lines[0];
    int index = 0;
    parse_text_syntax_block(&result, &index, editor.root_block, 0);
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

void syntax_editor_save_text_file(const char* filename)
{
    String whole_text = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&whole_text));
    syntax_block_append_to_string(syntax_editor.root_block, &whole_text, 0);
    file_io_write_file(filename, array_create_static((byte*)whole_text.characters, whole_text.size));
}

void syntax_editor_create(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Input* input, Timer* timer)
{
    memory_zero(&syntax_editor);
    syntax_editor.cursor_index = 0;
    syntax_editor.mode = Editor_Mode::INPUT;
    syntax_editor.root_block = syntax_block_create(0);
    syntax_editor.cursor_line = syntax_editor.root_block->lines[0];
    syntax_editor.context_text = string_create_empty(256);

    syntax_editor_load_text_file("upp_code/editor_text.upp");

    syntax_editor.text_renderer = text_renderer;
    syntax_editor.rendering_core = rendering_core;
    syntax_editor.renderer_2D = renderer_2D;
    syntax_editor.input = input;

    syntax_editor.compiler = new Compiler;
    *syntax_editor.compiler = compiler_create(timer);
    syntax_editor.identifier_pool = &syntax_editor.compiler->identifier_pool;

    // Add mapping infos
    {
        auto& keyword_table = syntax_editor.keyword_table;
        auto& keyword_mapping = syntax_editor.keyword_mapping;

        keyword_table = hashtable_create_empty<String, Syntax_Keyword>(8, hash_string, string_equals);
        keyword_mapping = array_create_empty<String>((int)Syntax_Keyword::MAX_ENUM_VALUE);

        keyword_mapping[(int)Syntax_Keyword::BREAK] = string_create_static("break");
        keyword_mapping[(int)Syntax_Keyword::CASE] = string_create_static("case");
        keyword_mapping[(int)Syntax_Keyword::CAST] = string_create_static("cast");
        keyword_mapping[(int)Syntax_Keyword::CAST_RAW] = string_create_static("cast_raw");
        keyword_mapping[(int)Syntax_Keyword::CAST_PTR] = string_create_static("cast_ptr");
        keyword_mapping[(int)Syntax_Keyword::CONTINUE] = string_create_static("continue");
        keyword_mapping[(int)Syntax_Keyword::C_UNION] = string_create_static("c_union");
        keyword_mapping[(int)Syntax_Keyword::DEFAULT] = string_create_static("default");
        keyword_mapping[(int)Syntax_Keyword::DEFER] = string_create_static("defer");
        keyword_mapping[(int)Syntax_Keyword::DELETE_KEYWORD] = string_create_static("delete");
        keyword_mapping[(int)Syntax_Keyword::ELSE] = string_create_static("else");
        keyword_mapping[(int)Syntax_Keyword::IF] = string_create_static("if");
        keyword_mapping[(int)Syntax_Keyword::MODULE] = string_create_static("module");
        keyword_mapping[(int)Syntax_Keyword::NEW] = string_create_static("new");
        keyword_mapping[(int)Syntax_Keyword::ENUM] = string_create_static("enum");
        keyword_mapping[(int)Syntax_Keyword::RETURN] = string_create_static("return");
        keyword_mapping[(int)Syntax_Keyword::STRUCT] = string_create_static("struct");
        keyword_mapping[(int)Syntax_Keyword::SWITCH] = string_create_static("switch");
        keyword_mapping[(int)Syntax_Keyword::UNION] = string_create_static("union");
        keyword_mapping[(int)Syntax_Keyword::WHILE] = string_create_static("while");

        for (int i = 0; i < keyword_mapping.size; i++) {
            hashtable_insert_element(&keyword_table, keyword_mapping[i], (Syntax_Keyword)i);
        }
    }

    Parser::initialize();
}

void syntax_editor_destroy()
{
    auto& editor = syntax_editor;
    syntax_block_destroy(editor.root_block);
    array_destroy(&editor.keyword_mapping);
    hashtable_destroy(&editor.keyword_table);
    compiler_destroy(editor.compiler);
    string_destroy(&syntax_editor.context_text);
    delete editor.compiler;
    Parser::destroy();
}



// Rendering
vec2 text_to_screen_coord(int line, int character)
{
    auto editor = &syntax_editor;
    float w = editor->rendering_core->render_information.viewport_width;
    float h = editor->rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 char_size = editor->character_size * scaling_factor;
    vec2 line_start = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = line_start + vec2(character * char_size.x, -(line + 1) * char_size.y);
    return cursor;
}

void syntax_editor_draw_underline(int line, int character, int length, vec3 color)
{
    float w = syntax_editor.rendering_core->render_information.viewport_width;
    float h = syntax_editor.rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 char_size = syntax_editor.character_size * scaling_factor;
    vec2 size = char_size * vec2((float)length, 1 / 8.0f);
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * char_size.x, -(line + 1) * char_size.y);
    cursor = cursor + size * vec2(0.5f, 0.5f);
    renderer_2D_add_rectangle(syntax_editor.renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_cursor_line(vec3 color, int line, int character)
{
    auto editor = &syntax_editor;
    float w = editor->rendering_core->render_information.viewport_width;
    float h = editor->rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 char_size = editor->character_size * scaling_factor;
    vec2 size = char_size * vec2(0.1f, 1.0f);
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * char_size.x, -(line + 1) * char_size.y);
    cursor = cursor + size * vec2(0.5f, 0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_character_box(vec3 color, int line, int character)
{
    auto editor = &syntax_editor;
    float w = editor->rendering_core->render_information.viewport_width;
    float h = editor->rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 size = editor->character_size * scaling_factor;
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * size.x, -line * size.y);
    cursor = cursor + size * vec2(0.5f, -0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_string(String string, vec3 color, int line, int character)
{
    auto editor = &syntax_editor;
    vec2 pos = vec2(-1.0f, 1.0f) + vec2(character, -(line + 1)) * editor->character_size;
    text_renderer_set_color(editor->text_renderer, color);
    text_renderer_add_text(editor->text_renderer, &string, pos, editor->character_size.y, 1.0f);
}

void syntax_editor_draw_string_in_box(String string, vec3 text_color, vec3 box_color, int line, int character, float text_size)
{
    int text_width = 0;
    int max_text_width = 0;
    int text_height = 1;
    for (int i = 0; i < string.size; i++) {
        auto c = string[i];
        if (c == '\n') {
            text_width = 0;
            text_height += 1;
        }
        else {
            text_width += 1;
            max_text_width = math_maximum(max_text_width, text_width);
        }
    }

    auto editor = &syntax_editor;
    vec2 pos = vec2(-1.0f, 1.0f) + vec2(character, -line) * editor->character_size + vec2(0.0f, -editor->character_size.y * text_size * text_height);
    text_renderer_set_color(editor->text_renderer, text_color);
    text_renderer_add_text(editor->text_renderer, &string, pos, editor->character_size.y * text_size, 1.0f);

    float w = editor->rendering_core->render_information.viewport_width;
    float h = editor->rendering_core->render_information.viewport_height;
    vec2 scaling_factor;
    if (w > h) {
        scaling_factor = vec2(w / h, 1.0f);
    }
    else {
        scaling_factor = vec2(1.0f, h / w);
    }
    vec2 size = editor->character_size * scaling_factor;
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * size.x, -line * size.y);
    size = size * vec2(max_text_width, text_height) * text_size;
    cursor = cursor + size * vec2(0.5f, -0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, box_color, 0.0f);

}



void syntax_editor_layout_line(Syntax_Line* line, int line_index)
{
    // Layout Tokens
    auto& tokens = line->tokens;
    int pos = line->parent_block->info.indentation_level * 4;
    line->info.index = line_index;
    for (int i = 0; i < tokens.size; i++)
    {
        auto& token = tokens[i];
        auto& info = token.info;
        String str = syntax_token_as_string(token);

        pos += info.format_space_before ? 1 : 0;
        info.screen_pos = pos;
        info.screen_size = str.size;
        vec3 color = Syntax_Color::TEXT;
        if (token.type == Syntax_Token_Type::KEYWORD) {
            color = Syntax_Color::KEYWORD;
        }
        else if (token.type == Syntax_Token_Type::LITERAL_STRING) {
            color = Syntax_Color::STRING;
        }
        else if (token.type == Syntax_Token_Type::LITERAL_NUMBER) {
            color = Syntax_Color::LITERAL_NUMBER;
        }
        info.screen_color = color;

        pos += str.size + (info.format_space_after ? 1 : 0);
    }
    line->info.line_end = pos;
}

void syntax_editor_layout_block(Syntax_Block* block, int indentation, int* line_index)
{
    auto& info = block->info;
    info.line_start = *line_index;
    info.indentation_level = indentation;
    for (int i = 0; i < block->lines.size; i++)
    {
        auto& line = block->lines[i];
        line_tokenize_text(line);
        line_format_text_from_tokens(line);
        syntax_editor_layout_line(line, *line_index);
        *line_index += 1;
        if (line->follow_block != 0) {
            syntax_editor_layout_block(line->follow_block, indentation + 1, line_index);
        }
    }
    info.line_end = *line_index;
}

void syntax_line_render(Syntax_Line* line)
{
    auto& tokens = line->tokens;
    for (int i = 0; i < tokens.size; i++)
    {
        auto& token = tokens[i];
        String str = syntax_token_as_string(token);
        text_renderer_set_color(syntax_editor.text_renderer, token.info.screen_color);
        syntax_editor_draw_string(str, token.info.screen_color, line->info.index, token.info.screen_pos);
    }
}

void syntax_block_render(Syntax_Block* block)
{
    for (int i = 0; i < block->lines.size; i++)
    {
        auto& line = block->lines[i];
        syntax_line_render(line);
        if (line->follow_block != 0) {
            syntax_block_render(line->follow_block);
        }
    }

    auto& info = block->info;
    if (info.indentation_level != 0)
    {
        auto offset = syntax_editor.character_size * vec2(0.5f, 1.0f);
        vec2 start = text_to_screen_coord(info.line_start, (info.indentation_level - 1) * 4) + offset;
        vec2 end = text_to_screen_coord(info.line_end, (info.indentation_level - 1) * 4) + offset;
        start.y -= syntax_editor.character_size.y * 0.1;
        end.y += syntax_editor.character_size.y * 0.1;
        renderer_2D_add_line(syntax_editor.renderer_2D, start, end, vec3(0.4f), 3, 0.0f);
        vec2 l_end = end + vec2(syntax_editor.character_size.x * 0.5f, 0.0f);
        renderer_2D_add_line(syntax_editor.renderer_2D, end, l_end, vec3(0.4f), 3, 0.0f);
    }
}

void syntax_editor_base_set_section_color(AST::Base* base, Parser::Section section, vec3 color)
{
    assert(base != 0, "");
    auto ranges = dynamic_array_create_empty<Syntax_Range>(1);
    SCOPE_EXIT(dynamic_array_destroy(&ranges));

    auto& node = base;
    Parser::ast_base_get_section_token_range(node, section, &ranges);
    for (int i = 0; i < ranges.size; i++)
    {
        auto range = ranges[i];
        // Set end to previous line if its at start of next line
        if (range.end.token_index == 0 && range.end.line_index > 0)
        {
            range.end.line_index -= 1;
            range.end.token_index = syntax_position_get_line(range.end)->tokens.size;
        }
        // TODO: Currently looping over all tokens in a range only works if start and end is in one line
        if (range.start.block == range.end.block && range.start.line_index == range.end.line_index)
        {
            Syntax_Position iter = range.start;
            while (syntax_position_on_token(iter) && iter.token_index < range.end.token_index) {
                syntax_position_get_token(iter)->info.screen_color = color;
                iter.token_index += 1;
            }
        }
    }
}

void syntax_editor_find_syntax_highlights(AST::Base* base)
{
    switch (base->type)
    {
    case AST::Base_Type::DEFINITION: {
        auto definition = (AST::Definition*) base;
        if (definition->symbol != 0) {
            auto color = symbol_type_to_color(definition->symbol->type);
            syntax_editor_base_set_section_color(base, Parser::Section::IDENTIFIER, color);
        }
        break;
    }
    case AST::Base_Type::SYMBOL_READ: {
        auto read = (AST::Symbol_Read*) base;
        if (read->resolved_symbol != 0) {
            auto color = symbol_type_to_color(read->resolved_symbol->type);
            syntax_editor_base_set_section_color(base, Parser::Section::IDENTIFIER, color);
        }
        break;
    }
    }

    // Do syntax highlighting for children
    int index = 0;
    auto child = AST::base_get_child(base, index);
    while (child != 0)
    {
        syntax_editor_find_syntax_highlights(child);
        index += 1;
        child = AST::base_get_child(base, index);
    }
}

void syntax_editor_render()
{
    auto& editor = syntax_editor;
    auto& cursor = syntax_editor.cursor_index;

    // Prepare Render
    editor.character_size.y = text_renderer_cm_to_relative_height(editor.text_renderer, editor.rendering_core, 0.8f);
    editor.character_size.x = text_renderer_get_cursor_advance(editor.text_renderer, editor.character_size.y);

    // Layout Source Code
    syntax_editor_sanitize_cursor();
    int index = 0;
    syntax_editor_layout_block(editor.root_block, 0, &index);

    // Draw Cursor
    {
        Syntax_Line* line = syntax_editor.cursor_line;
        auto& info = get_cursor_token().info;
        int cursor_pos = info.screen_pos + (cursor - info.char_start);
        if (editor.mode == Editor_Mode::NORMAL)
        {
            int box_start = info.screen_pos;
            int box_end = math_maximum(info.screen_pos + info.screen_size, box_start + 1);
            for (int i = box_start; i < box_end; i++) {
                syntax_editor_draw_character_box(vec3(0.2f), line->info.index, i);
            }
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line->info.index, box_start);
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line->info.index, box_end);
        }
        else {
            if (info.format_space_before && cursor == info.char_start) {
                cursor_pos -= 1;
            }
            if (info.format_space_after && cursor > info.char_end) {
                cursor_pos = info.screen_pos + info.screen_size + 1;
            }
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line->info.index, cursor_pos);
        }

        // Draw Text-Representation @ the bottom of the screen 
        if (true)
        {
            int line_index = 2.0f / editor.character_size.y - 1;
            syntax_editor_draw_string(line->text, Syntax_Color::TEXT, line_index, 0);
            if (editor.mode == Editor_Mode::NORMAL) {
                syntax_editor_draw_character_box(Syntax_Color::COMMENT, line_index, cursor);
            }
            else {
                syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line_index, cursor);
            }
        }
    }

    bool show_context_info = false;
    auto& context = syntax_editor.context_text;
    string_reset(&context);

    // Draw error messages
    {
        auto error_ranges = dynamic_array_create_empty<Syntax_Range>(1);
        SCOPE_EXIT(dynamic_array_destroy(&error_ranges));
        Syntax_Position cursor_pos;
        cursor_pos.block = syntax_editor.cursor_line->parent_block;
        cursor_pos.line_index = syntax_line_index(syntax_editor.cursor_line);
        cursor_pos.token_index = get_cursor_token_index();
        for (int i = 0; i < editor.compiler->semantic_analyser->errors.size; i++)
        {
            auto& error = editor.compiler->semantic_analyser->errors[i];
            auto& node = error.error_node;
            dynamic_array_reset(&error_ranges);
            if (node == 0) continue;
            Parser::ast_base_get_section_token_range(node, semantic_error_get_section(error), &error_ranges);
            for (int j = 0; j < error_ranges.size; j++)
            {
                auto range = error_ranges[j];
                if (range.end.token_index == 0 && range.end.line_index > 0)
                {
                    range.end.line_index -= 1;
                    range.end.token_index = syntax_position_get_line(range.end)->tokens.size;
                }
                if (range.start.block == range.end.block && range.start.line_index == range.end.line_index)
                {
                    auto line = syntax_position_get_line(range.start);
                    if (!syntax_position_on_token(range.start)) {
                        continue;
                    }
                    int start = syntax_position_get_token(range.start)->info.screen_pos;
                    int end = line->info.line_end;
                    if (syntax_position_on_token(range.end)) {
                        auto end_info = syntax_position_get_token(range.end)->info;
                        end = end_info.screen_pos + end_info.screen_size;
                    }
                    syntax_editor_draw_underline(line->info.index, start, end - start, vec3(1.0f, 0.0f, 0.0f));
                }
                if (syntax_position_in_order(range.start, cursor_pos) && syntax_position_in_order(cursor_pos, range.end)) {
                    show_context_info = true;
                    string_reset(&context);
                    semantic_error_append_to_string(error, &context);
                }
            }
        }
    }

    // Draw context
    if (show_context_info || true)
    {
        auto& info = get_cursor_token().info;
        int cursor_char = info.screen_pos + (cursor - info.char_start);
        int cursor_line = editor.cursor_line->info.index;
        syntax_editor_draw_string_in_box(context, vec3(1.0f), vec3(0.2f), cursor_line + 1, cursor_char, 0.8f);
    }

    // Syntax Highlighting
    syntax_editor_find_syntax_highlights(&editor.compiler->main_source->source->base);

    // Render Source Code
    syntax_block_render(editor.root_block);

    // Render Primitives
    renderer_2D_render(editor.renderer_2D, editor.rendering_core);
    text_renderer_render(editor.text_renderer, editor.rendering_core);
}
