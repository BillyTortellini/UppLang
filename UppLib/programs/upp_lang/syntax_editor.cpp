#include "syntax_editor.hpp"

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/renderer_2d.hpp"

#include "../../win32/input.hpp"
#include "syntax_colors.hpp"
#include "compiler.hpp"



// Prototypes
struct Text_Renderer;
struct Rendering_Core;
struct Input;
struct Renderer_2D;
struct Syntax_Editor;
struct Syntax_Line;
struct Identifier_Pool;



// Datatypes
#define SYNTAX_OPERATOR_COUNT 30
enum class Syntax_Operator
{
    ADDITION,
    SUBTRACTION,
    DIVISON,
    MULTIPLY,
    MODULO,
    COMMA,
    DOT,
    TILDE,
    COLON,
    NOT,
    AMPERSAND,
    LESS_THAN,
    GREATER_THAN,
    LESS_EQUAL,
    GREATER_EQUAL,
    EQUALS,
    NOT_EQUALS,
    POINTER_EQUALS,
    POINTER_NOT_EQUALS,
    DEFINE_COMPTIME,
    DEFINE_INFER,
    AND,
    OR,
    ARROW,
    DOLLAR,
    ASSIGN,
    ASSIGN_ADD,
    ASSIGN_SUB,
    ASSIGN_MULT,
    ASSIGN_DIV
};

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

enum class Syntax_Keyword
{
    RETURN,
    BREAK,
    CONTINUE,
    IF,
    ELSE,
    WHILE,
    SWITCH,
    CASE,
    DEFAULT,
    MODULE,
    NEW,
    STRUCT,
    UNION,
    C_UNION,
    ENUM,
    DELETE_KEYWORD,
    DEFER,
    CAST,
    CAST_RAW,
    CAST_PTR,

    MAX_ENUM_VALUE,
};

enum class Parenthesis_Type
{
    PARENTHESIS,
    BRACKETS,   // []
    BRACES,     // {} 
};

struct Parenthesis
{
    Parenthesis_Type type;
    bool is_open;
};

struct Token_Info
{
    // Character information
    int char_start;
    int char_end;

    // Render Info, not actual spaces
    bool format_space_before;
    bool format_space_after;

    // Actual position for rendering in the line
    int screen_pos;
    int screen_size;
};

enum class Syntax_Token_Type
{
    IDENTIFIER,
    KEYWORD,
    LITERAL_NUMBER,
    LITERAL_STRING,
    LITERAL_BOOL,
    OPERATOR,
    PARENTHESIS,
    UNEXPECTED_CHAR, // Unexpected Character, like | or ; \...
    GAP,
    DUMMY, // All empty lines have 1 tokenized dummy token, so i dont have to worry about dumb stuff
};

struct Syntax_Token
{
    Syntax_Token_Type type;
    Token_Info info;
    union {
        Syntax_Operator op;
        String* identifier;
        String* literal_number;
        struct {
            String* string; // With ""
            bool has_closure;
        } literal_string;
        bool literal_bool;
        Syntax_Keyword keyword;
        char unexpected;
        Parenthesis parenthesis;
    } options;
};



// EDITOR
struct Syntax_Block;
struct Syntax_Line
{
    String text;
    Dynamic_Array<Syntax_Token> tokens;
    Syntax_Block* parent_block;
    Syntax_Block* follow_block;
};

struct Syntax_Block
{
    Syntax_Line* parent_line; // 0 for root
    Dynamic_Array<Syntax_Line*> lines; // Must be non-zero
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

    // Rendering
    Input* input;
    Rendering_Core* rendering_core;
    Renderer_2D* renderer_2D;
    Text_Renderer* text_renderer;
    vec2 character_size;
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



// Parsing
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
    return type == Syntax_Token_Type::IDENTIFIER || type == Syntax_Token_Type::KEYWORD || type == Syntax_Token_Type::LITERAL_NUMBER;
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

template<typename T>
T dynamic_array_last(Dynamic_Array<T>* array) {
    return (*array)[array->size - 1];
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
            if (block->lines.size > 1) {

            }
            else {
                auto follow = line->follow_block;
                line->follow_block = 0;
                syntax_block_destroy(combine_with->follow_block);
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

namespace Parser {
    void parser_execute();
    void initialize();
    void destroy();
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
    Parser::parser_execute();
}

void syntax_editor_create(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Input* input)
{
    memory_zero(&syntax_editor);
    syntax_editor.cursor_index = 0;
    syntax_editor.mode = Editor_Mode::INPUT;
    syntax_editor.root_block = syntax_block_create(0);
    syntax_editor.cursor_line = syntax_editor.root_block->lines[0];

    syntax_editor.identifier_pool = new Identifier_Pool;
    *syntax_editor.identifier_pool = identifier_pool_create();

    syntax_editor.text_renderer = text_renderer;
    syntax_editor.rendering_core = rendering_core;
    syntax_editor.renderer_2D = renderer_2D;
    syntax_editor.input = input;

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
    identifier_pool_destroy(editor.identifier_pool);
    delete editor.identifier_pool;
    array_destroy(&editor.keyword_mapping);
    hashtable_destroy(&editor.keyword_table);

    Parser::destroy();
}



// Rendering
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

void syntax_editor_render_line(Syntax_Line* line, int indentation, int line_index)
{
    auto editor = &syntax_editor;
    int pos = 0;
    int cursor_pos = 0;
    auto cursor = editor->cursor_index;
    auto& tokens = line->tokens;
    int indent_offset = indentation * 4;

    // Token rendering
    for (int i = 0; i < tokens.size; i++)
    {
        auto& token = tokens[i];
        auto& info = token.info;

        pos += info.format_space_before ? 1 : 0;
        info.screen_pos = pos;
        String str = syntax_token_as_string(token);
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
        syntax_editor_draw_string(str, color, line_index, pos + indent_offset);
        pos += str.size + (info.format_space_after ? 1 : 0);
    }

    // Draw Cursor
    if (line == editor->cursor_line)
    {
        auto& info = get_cursor_token().info;
        int cursor_pos = info.screen_pos + (cursor - info.char_start);
        if (editor->mode == Editor_Mode::NORMAL)
        {
            int box_start = info.screen_pos;
            int box_end = math_maximum(info.screen_pos + info.screen_size, box_start + 1);
            for (int i = box_start; i < box_end; i++) {
                syntax_editor_draw_character_box(vec3(0.2f), line_index, i + indent_offset);
            }
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line_index, box_start + indent_offset);
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line_index, box_end + indent_offset);
        }
        else {
            if (info.format_space_before && cursor == info.char_start) {
                cursor_pos -= 1;
            }
            if (info.format_space_after && cursor > info.char_end) {
                cursor_pos = info.screen_pos + info.screen_size + 1;
            }
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line_index, cursor_pos + indent_offset);
        }

        // Draw Text-Representation @ the bottom of the screen 
        if (true) {
            int line_index = 2.0f / editor->character_size.y - 1;
            syntax_editor_draw_string(line->text, Syntax_Color::TEXT, line_index, 0);
            if (editor->mode == Editor_Mode::NORMAL) {
                syntax_editor_draw_character_box(Syntax_Color::COMMENT, line_index, cursor);
            }
            else {
                syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line_index, cursor);
            }
        }
    }

    // Text rendering
    // syntax_editor_draw_string(editor, line.text, Syntax_Color::TEXT, line_index, 0);
    // cursor_pos = cursor;
}

void syntax_editor_render()
{
    auto& editor = syntax_editor;
    auto& cursor = syntax_editor.cursor_index;

    // Prepare Render
    editor.character_size.y = text_renderer_cm_to_relative_height(editor.text_renderer, editor.rendering_core, 0.8f);
    editor.character_size.x = text_renderer_get_cursor_advance(editor.text_renderer, editor.character_size.y);

    // Render lines
    auto line = editor.root_block->lines[0];
    int indentation = 0;
    int index = 0;
    while (true)
    {
        line_tokenize_text(line);
        line_format_text_from_tokens(line);
        syntax_editor_sanitize_cursor();
        syntax_editor_render_line(line, indentation, index);

        // Iterate to next line
        auto next_line = navigate_next_line(line);
        if (next_line == line) break;
        if (next_line->parent_block != line->parent_block) {
            if (line->follow_block != 0) indentation += 1;
            else indentation -= 1;
        }
        line = next_line;
        index += 1;
    }

    // Render Primitives
    renderer_2D_render(editor.renderer_2D, editor.rendering_core);
    text_renderer_render(editor.text_renderer, editor.rendering_core);
}








namespace Parser
{
    // Types
    namespace AST
    {
        struct Expression;
        struct Statement;
        struct Code_Block;
        struct Definition;

        enum class Binop
        {
            ADDITION,
            SUBTRACTION,
            DIVISION,
            MULTIPLICATION,
            MODULO,
            AND,
            OR,
            EQUAL,
            NOT_EQUAL,
            LESS,
            LESS_OR_EQUAL,
            GREATER,
            GREATER_OR_EQUAL,
            POINTER_EQUAL,
            POINTER_NOT_EQUAL,
        };

        enum class Unop
        {
            NOT, // !
            NEGATE, // -
            POINTER, // *
            ADDRESS_OF, // &
        };

        enum class Cast_Type
        {
            PTR_TO_RAW,
            RAW_TO_PTR,
            TYPE_TO_TYPE,
        };

        enum class Literal_Type
        {
            STRING,
            NUMBER,
            BOOLEAN,
        };

        enum class Base_Type
        {
            EXPRESSION,
            STATEMENT,
            DEFINITION, // ::, :=, : ... =, ...: ...
            CODE_BLOCK,
            MODULE,

            // Helpers
            ARGUMENT, // a(15, 32, a = 200)
            PARAMETER, 
        };

        struct Base
        {
            Base_Type type;
            Base* parent;
        };

        struct Module
        {
            Base base;
            Dynamic_Array<Definition*> definitions;
        };

        struct Definition
        {
            Base base;
            bool is_comptime; // :: instead of :=
            String* name;
            Optional<Expression*> type;
            Optional<Expression*> value;
        };

        struct Argument
        {
            Base base;
            Optional<String*> name;
            Expression* value;
        };

        struct Parameter
        {
            Base base;
            bool is_comptime; // $ at the start
            String* name;
            Expression* type;
            Optional<Expression*> default_value;
        };

        struct Code_Block
        {
            Base base;
            Dynamic_Array<Statement*> statements;
        };

        enum class Structure_Type {
            STRUCT,
            UNION,
            C_UNION
        };

        enum class Expression_Type
        {
            // Value Generation
            BINARY_OPERATION,
            UNARY_OPERATION,
            FUNCTION_CALL,
            NEW_EXPR,
            CAST,
            /*
            ARRAY_INITIALIZER,
            STRUCT_INITIALIZER,
            AUTO_ENUM,
            TYPE_INFO,
            TYPE_OF,
            */

            // Memory Reads
            SYMBOL_READ, 
            LITERAL_READ,
            ARRAY_ACCESS,
            MEMBER_ACCESS,

            // Types/Definitions
            MODULE,
            FUNCTION,
            FUNCTION_SIGNATURE,

            STRUCTURE_TYPE, // Struct, union, c_union
            ENUM_TYPE,
            ARRAY_TYPE,
            SLICE_TYPE,

            ERROR_EXPR,
        };

        struct Expression
        {
            Base base;
            Expression_Type type;
            union
            {
                struct {
                    Expression* left;
                    Expression* right;
                    Binop type;
                } binop;
                struct {
                    Unop type;
                    Expression* unop;
                } unop;
                struct {
                    Expression* expr;
                    Dynamic_Array<Argument*> arguments;
                } call;
                struct {
                    Expression* type_expr;
                    Optional<Expression*> count_expr;
                } new_expr;
                struct {
                    Cast_Type type;
                    Optional<Expression*> to_type;
                    Expression* operand;
                } cast;
                String* symbol_read;
                struct {
                    Literal_Type type;
                    union {
                        String* string;
                        int number;
                        bool boolean;
                    } options;
                } literal_read;
                struct {
                    Expression* array_expr;
                    Expression* index_expr;
                } array_access;
                String* member_name;
                Module* module;
                struct {
                    Expression* signature;
                    Code_Block* body;
                } function;
                struct {
                    Dynamic_Array<Parameter*> parameters;
                    Optional<Expression*> return_value;
                } function_signature;
                struct {
                    Expression* size_expr;
                    Expression* type_expr;
                } array_type;
                Expression* slice_type;
                struct {
                    Dynamic_Array<Definition*> members;
                    Structure_Type type;
                } structure;
                Dynamic_Array<Definition*> enum_members;
            } options;
        };

        struct Switch_Case
        {
            Optional<Expression*> value;
            Code_Block* block;
        };

        enum class Statement_Type
        {
            DEFINITION,
            BLOCK,
            ASSIGNMENT,
            EXPRESSION_STATEMENT,
            // Keyword Statements
            DEFER,
            IF_STATEMENT,
            WHILE_STATEMENT,
            SWITCH_STATEMENT,
            BREAK_STATEMENT,
            CONTINUE_STATEMENT,
            RETURN_STATEMENT,
            DELETE_STATEMENT,
        };

        struct Statement
        {
            Base base;
            Statement_Type type;
            union
            {
                Expression* expression;
                Code_Block* block;
                Definition* definition;
                struct {
                    Expression* left_side;
                    Expression* right_side;
                } assignment;
                Code_Block* defer_block;
                struct {
                    Expression* condition;
                    Code_Block* block;
                    Optional<Code_Block*> else_block;
                } if_statement;
                struct {
                    Expression* condition;
                    Code_Block* block;
                } while_statement;
                struct {
                    Expression* condition;
                    Dynamic_Array<Switch_Case> cases;
                } switch_statement;
                String* break_name;
                String* continue_name;
                Optional<Expression*> return_value;
                Expression* delete_expr;
            } options;
        };

        void base_destroy(Base* node)
        {
            switch (node->type)
            {
            case Base_Type::ARGUMENT: {
                auto arg = (Argument*)node;
                break;
            }
            case Base_Type::CODE_BLOCK: {
                auto block = (Code_Block*)node;
                if (block->statements.data != 0) {
                    dynamic_array_destroy(&block->statements);
                }
                break;
            }
            case Base_Type::DEFINITION: {
                auto def = (Definition*)node;
                break;
            }
            case Base_Type::MODULE: {
                auto module = (Module*)node;
                if (module->definitions.data != 0) {
                    dynamic_array_destroy(&module->definitions);
                }
                break;
            }
            case Base_Type::EXPRESSION: {
                auto expr = (Expression*)node;
                switch (expr->type)
                {
                case Expression_Type::FUNCTION_CALL: {
                    auto& call = expr->options.call;
                    if (call.arguments.data != 0) {
                        dynamic_array_destroy(&call.arguments);
                    }
                    break;
                }
                case Expression_Type::FUNCTION_SIGNATURE: {
                    auto& sig = expr->options.function_signature;
                    if (sig.parameters.data != 0) {
                        dynamic_array_destroy(&sig.parameters);
                    }
                    break;
                }
                case Expression_Type::STRUCTURE_TYPE: {
                    auto& members = expr->options.structure.members;
                    if (members.data != 0) {
                        dynamic_array_destroy(&members);
                    }
                    break;
                }
                case Expression_Type::ENUM_TYPE: {
                    auto& members = expr->options.enum_members;
                    if (members.data != 0) {
                        dynamic_array_destroy(&members);
                    }
                    break;
                }
                }
                break;
            }
            case Base_Type::STATEMENT: {
                auto stat = (Statement*)node;
                switch (stat->type)
                {
                case Statement_Type::SWITCH_STATEMENT: {
                    auto cases = stat->options.switch_statement.cases;
                    if (cases.data != 0) {
                        dynamic_array_destroy(&cases);
                    }
                    break;
                }
                }
                break;
            }
            default: panic("");
            }
            delete node;
        }
    }

    using namespace AST;

    struct Parse_Position
    {
        Syntax_Block* block;
        int line_index;
        int token_index;
        int allocated_count;
    };

    struct Parser
    {
        Parse_Position pos;
        Dynamic_Array<AST::Base*> allocated;
        AST::Module* root;
    };

    // Globals
    static Parser parser;

    // Functions
    void parser_rollback(Parse_Position checkpoint)
    {
        for (int i = checkpoint.allocated_count; i < parser.allocated.size; i++) {
            AST::base_destroy(parser.allocated[i]);
        }
        dynamic_array_rollback_to_size(&parser.allocated, checkpoint.allocated_count);
        parser.pos = checkpoint;
    }

    void reset()
    {
        parser.root = 0;

        Parse_Position pos;
        pos.block = syntax_editor.root_block;
        pos.line_index = 0;
        pos.token_index = 0;
        pos.allocated_count = 0;
        parser_rollback(pos);
    }

    void initialize()
    {
        parser.allocated = dynamic_array_create_empty<AST::Base*>(32);
        reset();
    }

    void destroy()
    {
        reset();
        dynamic_array_destroy(&parser.allocated);
    }

    // Allocations
    template<typename T>
    T* allocate_base(Base* parent, Base_Type type)
    {
        auto result = new T;
        memory_zero(result);
        Base* base = &result->base;
        base->parent = parent;
        base->type = type;
        dynamic_array_push_back(&parser.allocated, &result->base);
        parser.pos.allocated_count = parser.allocated.size;
        return result;
    }

    // Gets may return 0
    Syntax_Token* get_token(int offset) {
        auto& editor = syntax_editor;
        auto& pos = parser.pos;
        if (pos.line_index >= pos.block->lines.size) return 0;
        auto& tokens = pos.block->lines[pos.line_index]->tokens;
        int tok_index = pos.token_index + offset;
        if (tok_index >= tokens.size) return 0;
        return &tokens[tok_index];
    }

    void advance_token() {
        parser.pos.token_index += 1;
    }

    void advance_line() {
        parser.pos.line_index += 1;
    }


    // Helpers
    bool test_token_offset(Syntax_Token_Type type, int offset) {
        auto token = get_token(offset);
        if (token == 0) return false;
        return token->type == type;
    }

    bool test_token(Syntax_Token_Type type) {
        return test_token_offset(type, 0);
    }

    bool test_token_2(Syntax_Token_Type t0, Syntax_Token_Type t1) {
        return test_token_offset(t0, 0) && test_token_offset(t1, 1);
    }

    bool test_token_3(Syntax_Token_Type t0, Syntax_Token_Type t1, Syntax_Token_Type t2) {
        return test_token_offset(t0, 0) && test_token_offset(t1, 1) && test_token_offset(t2, 2);
    }

    bool test_token_4(Syntax_Token_Type t0, Syntax_Token_Type t1, Syntax_Token_Type t2, Syntax_Token_Type t3) {
        return test_token_offset(t0, 0) && test_token_offset(t1, 1) && test_token_offset(t2, 2) && test_token_offset(t3, 3);
    }

    bool test_operator_offset(Syntax_Operator op, int offset) {
        if (!test_token_offset(Syntax_Token_Type::OPERATOR, offset))
            return false;
        return get_token(offset)->options.op == op;
    }

    bool test_operator(Syntax_Operator op) {
        return test_operator_offset(op, 0);
    }

    bool test_keyword_offset(Syntax_Keyword keyword, int offset) {
        if (!test_token_offset(Syntax_Token_Type::KEYWORD, offset))
            return false;
        return get_token(offset)->options.keyword == keyword;
    }
    bool test_parenthesis_offset(char c, int offset) {
        Parenthesis p = char_to_parenthesis(c);
        if (!test_token_offset(Syntax_Token_Type::PARENTHESIS, offset))
            return false;
        auto given = get_token(offset)->options.parenthesis;
        return given.is_open == p.is_open && given.type == p.type;
    }


    // Prototypes
    Definition* parse_definition(Base* parent);
    Statement* parse_statement(Base* parent);
    Expression* parse_expression(Base* parent);
    Expression* parse_expression_or_error_expr(Base* parent);
    Expression* parse_single_expression(Base* parent);

    //Parsing
#define CHECKPOINT_SETUP \
        auto checkpoint = parser.pos;\
        bool _error_exit = false;\
        SCOPE_EXIT(if (_error_exit) parser_rollback(checkpoint););

#define CHECKPOINT_EXIT {_error_exit = true; return 0;}

    typedef bool(*token_predicate_fn)(Syntax_Token* token);
    Optional<Parse_Position> find_error_recovery_token(token_predicate_fn predicate, bool skip_blocks) 
    {
        Dynamic_Array<Parenthesis> parenthesis_stack = dynamic_array_create_empty<Parenthesis>(1);
        SCOPE_EXIT(dynamic_array_destroy(&parenthesis_stack));

        Parse_Position pos = parser.pos;
        auto& lines = pos.block->lines;
        if (pos.line_index >= lines.size) return optional_make_failure<Parse_Position>();

        Syntax_Line* line = lines[pos.line_index];
        Dynamic_Array<Syntax_Token>* tokens = &line->tokens;
        while (true) 
        {
            if (pos.token_index >= tokens->size) 
            {
                if (!(skip_blocks || parenthesis_stack.size != 0)) {
                    return optional_make_failure<Parse_Position>();
                }
                // Parenthesis aren't allowed to reach over blocks if there is no follow_block 
                if (line->follow_block == 0) {
                    return optional_make_failure<Parse_Position>();
                }
                if (pos.line_index + 1 >= lines.size) {
                    return optional_make_failure<Parse_Position>();
                }
                pos.line_index = pos.line_index + 1;
                pos.token_index = 0;
                line = lines[pos.line_index];
                tokens = &line->tokens;
            }

            Syntax_Token* token = &(*tokens)[pos.token_index];
            if (parenthesis_stack.size == 0 && predicate(token)) {
                return optional_make_success(pos);
            }
            if (token->type == Syntax_Token_Type::PARENTHESIS)
            {
                auto parenthesis = token->options.parenthesis;
                if (parenthesis.is_open) {
                    dynamic_array_push_back(&parenthesis_stack, parenthesis);
                }
                else if (parenthesis_stack.size > 0){
                    auto last = dynamic_array_last(&parenthesis_stack);
                    if (last.type == parenthesis.type) {
                        dynamic_array_rollback_to_size(&parenthesis_stack, parenthesis_stack.size - 1);
                    }
                }
            }
            pos.token_index += 1;
        }
    }

    bool parse_position_in_order(Parse_Position a, Parse_Position b) {
        assert(a.block == b.block, "Only works for positions in the same block");
        if (a.line_index < b.line_index) return true;
        if (a.line_index > b.line_index) return false;
        return a.token_index < b.token_index;
    }

    Parameter* parse_parameter(Base* parent)
    {
        CHECKPOINT_SETUP;
        auto result = allocate_base<Parameter>(parent, Base_Type::PARAMETER);
        result->is_comptime = false;
        if (test_operator(Syntax_Operator::DOLLAR)) {
            result->is_comptime = true;
            advance_token();
        }

        if (!test_token(Syntax_Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
        result->name = get_token(0)->options.identifier;
        advance_token();

        if (!test_operator(Syntax_Operator::COLON)) CHECKPOINT_EXIT;
        advance_token();
        result->type = parse_expression_or_error_expr((Base*)result);

        if (test_operator(Syntax_Operator::ASSIGN)) {
            result->type = parse_expression_or_error_expr((Base*)result);
        }
        return result;
    }

    Statement* parse_statement(Base* parent)
    {
        CHECKPOINT_SETUP;
        auto result = allocate_base<Statement>(parent, Base_Type::STATEMENT);

        auto definition = parse_definition(&result->base);
        if (definition != 0) {
            result->type = Statement_Type::DEFINITION;
            result->options.definition = definition;
            return result;
        }

        auto expr = parse_expression(&result->base);
        if (expr != 0) {
            result->type = Statement_Type::EXPRESSION_STATEMENT;
            result->options.expression = expr;
            return result;
        }

        CHECKPOINT_EXIT;
    }

    Code_Block* parse_code_block(Base* parent)
    {
        // Check if we are at the start of a block
        auto rewind_pos = parser.pos;
        {
            auto& p = parser.pos;
            if (p.line_index >= p.block->lines.size) return 0;
            auto& line = p.block->lines[p.line_index];
            auto& tokens = line->tokens;
            if (line->follow_block == 0) return 0; // No block following this line
            if (p.token_index < tokens.size) return 0; // Not at end of line
            p.line_index = 0;
            p.token_index = 0;
            p.block = line->follow_block;
        }

        // Parse all statements
        auto& pos = parser.pos;
        auto result = allocate_base<Code_Block>(parent, Base_Type::EXPRESSION);
        result->statements = dynamic_array_create_empty<Statement*>(1);
        while (pos.line_index < pos.block->lines.size)
        {
            auto& line = pos.block->lines[pos.line_index];
            // Test for empty line (Dummy token)
            if (line->text.size == 0) {
                pos.line_index += 1;
                // TODO: Test for anonymous block (Empty line + follow block)
                continue;
            }
            // Parse everything available inside a block
            auto statement = parse_statement(&result->base);
            if (statement == 0) {
                // Logg error here
                advance_line();
            }
            else {
                dynamic_array_push_back(&result->statements, statement);
            }
        }

        // Set parse index
        parser.pos.block = rewind_pos.block;
        parser.pos.line_index = rewind_pos.line_index + 1;
        parser.pos.token_index = 0;
        return result;
    }

    Expression* parse_single_expression_or_error(Base* parent)
    {
        auto expr = parse_single_expression(parent);
        if (expr != 0) return expr;
        expr = allocate_base<AST::Expression>(parent, AST::Base_Type::EXPRESSION);
        expr->type = Expression_Type::ERROR_EXPR;
        return expr;
    }

    template<Parenthesis_Type type>
    bool successfull_parenthesis_exit()
    {
        auto parenthesis_pos = find_error_recovery_token(
            [](Syntax_Token* t) -> bool
            { return t->type == Syntax_Token_Type::PARENTHESIS &&
            !t->options.parenthesis.is_open && t->options.parenthesis.type == type; },
            true
        );
        if (!parenthesis_pos.available) {
            return false;
        }
        parser.pos = parenthesis_pos.value;
        advance_token();
        return true;
    }

    Expression* parse_single_expression(Base* parent)
    {
        CHECKPOINT_SETUP;
        auto result = allocate_base<Expression>(parent, Base_Type::EXPRESSION);

        /*
            Prefix Operations:
                Negate  - a          X
                Not     ! a          X
                Deref   & a          X
                Pointer * a          X
                Cast    cast<u64> a  X
                Array   [expr]a      X
                Slice   []a          X
            Operands:
                Fn-Type () [-> expr] X
                Expr    (a+...)      X
                Lit     true         X
                Read    a            X
            Postfix Operations
                Mem     a.
                Array   a[]
                Call    a()
        */
        // Unops
        if (test_token(Syntax_Token_Type::OPERATOR))
        {
            Unop unop;
            bool valid = true;
            switch (get_token(0)->options.op)
            {
            case Syntax_Operator::SUBTRACTION: unop = Unop::NEGATE; break;
            case Syntax_Operator::NOT: unop = Unop::NOT; break;
            case Syntax_Operator::AMPERSAND: unop = Unop::ADDRESS_OF; break;
            case Syntax_Operator::MULTIPLY: unop = Unop::POINTER; break;
            default: valid = false;
            }
            if (valid) {
                advance_token();
                result->type = Expression_Type::UNARY_OPERATION;
                result->options.unop.type = unop;
                result->options.unop.unop = parse_single_expression_or_error(&result->base);
                return result;
            }
        }

        // Casts
        {
            bool is_cast = true;
            Cast_Type type;
            if (test_keyword_offset(Syntax_Keyword::CAST, 0)) {
                type = Cast_Type::TYPE_TO_TYPE;
            }
            else if (test_keyword_offset(Syntax_Keyword::CAST_PTR, 0)) {
                type = Cast_Type::RAW_TO_PTR;
            }
            else if (test_keyword_offset(Syntax_Keyword::CAST_RAW, 0)) {
                type = Cast_Type::PTR_TO_RAW;
            }
            else {
                is_cast = false;
            }
            if (is_cast)
            {
                advance_token();
                result->type = Expression_Type::CAST;
                auto& cast = result->options.cast;
                cast.type = type;
                if (test_parenthesis_offset('{', 0))
                {
                    advance_token();
                    cast.to_type = optional_make_success(parse_single_expression_or_error(&result->base));
                    if (!successfull_parenthesis_exit<Parenthesis_Type::BRACES>()) CHECKPOINT_EXIT;
                }

                cast.operand = parse_single_expression_or_error(&result->base);
                return result;
            }
        }

        // Array/Slice
        if (test_parenthesis_offset('[', 0))
        {
            advance_token();
            if (test_parenthesis_offset(']', 0)) {
                advance_token();
                result->type = Expression_Type::SLICE_TYPE;
                result->options.slice_type = parse_single_expression_or_error(&result->base);
                return result;
            }

            result->type = Expression_Type::ARRAY_TYPE;
            result->options.array_type.size_expr = parse_expression_or_error_expr(&result->base);
            if (!test_parenthesis_offset(']', 0)) {
                // TODO: Log error
                auto parenthesis_pos = find_error_recovery_token(
                    [](Syntax_Token* t) -> bool
                    { return t->type == Syntax_Token_Type::PARENTHESIS &&
                    !t->options.parenthesis.is_open && t->options.parenthesis.type == Parenthesis_Type::BRACKETS; },
                    true
                );
                if (!parenthesis_pos.available) {
                    CHECKPOINT_EXIT;
                }
                else {
                    parser.pos = parenthesis_pos.value;
                    advance_token();
                }
            }
            result->options.array_type.type_expr = parse_single_expression_or_error(&result->base);
            return result;
        }

        // Bases
        if (test_token(Syntax_Token_Type::IDENTIFIER)) {
            result->type = Expression_Type::SYMBOL_READ;
            result->options.symbol_read = get_token(0)->options.identifier;
            advance_token();
            return result;
        }

        // Literals
        if (test_token(Syntax_Token_Type::LITERAL_NUMBER)) {
            auto str = get_token(0)->options.literal_number;
            int value = 0;
            bool valid = true;
            for (int i = 0; i < str->size; i++) {
                auto c = str->characters[i];
                if (!(c >= '0' && c <= '9')) {
                    valid = false;
                    break;
                }
                value = value * 10;
                value += (c - '0');
            }
            advance_token();
            if (!valid) {
                result->type = Expression_Type::ERROR_EXPR;
                return result;
            }

            result->type = Expression_Type::LITERAL_READ;
            result->options.literal_read.type = Literal_Type::NUMBER;
            result->options.literal_read.options.number = value;
            return result;
        }
        if (test_token(Syntax_Token_Type::LITERAL_STRING)) {
            result->type = Expression_Type::LITERAL_READ;
            result->options.literal_read.type = Literal_Type::STRING;
            result->options.literal_read.options.string = get_token(0)->options.literal_string.string;
            advance_token();
            return result;
        }
        if (test_token(Syntax_Token_Type::LITERAL_BOOL)) {
            result->type = Expression_Type::LITERAL_READ;
            result->options.literal_read.type = Literal_Type::BOOLEAN;
            result->options.literal_read.options.boolean = get_token(0)->options.literal_bool;
            advance_token();
            return result;
        }

        // Parse functions + signatures
        if (test_parenthesis_offset('(', 0) && (
            test_parenthesis_offset(')', 1) ||
            (test_token_offset(Syntax_Token_Type::IDENTIFIER, 1) && test_operator_offset(Syntax_Operator::COLON, 2)) ||
            (test_operator_offset(Syntax_Operator::DOLLAR, 1) && test_token_offset(Syntax_Token_Type::IDENTIFIER, 2))
            ))
        {
            result->type = Expression_Type::FUNCTION_SIGNATURE;
            auto& signature = result->options.function_signature;
            signature.parameters = dynamic_array_create_empty<Parameter*>(1);
            signature.return_value.available = false;
            advance_token(); // Skip (

            // Parse Parameters
            while (true)
            {
                if (test_parenthesis_offset(')', 0)) {
                    advance_token();
                    break;
                }
                auto definition = parse_parameter((Base*)result);
                if (definition != 0)
                {
                    dynamic_array_push_back(&signature.parameters, definition);
                    if (test_operator(Syntax_Operator::COLON)) {
                        advance_token();
                        continue;
                    }
                    if (test_parenthesis_offset(')', 0)) {
                        continue;
                    }
                }

                // ERROR Recovery
                auto comma_pos = find_error_recovery_token(
                    [](Syntax_Token* t) -> bool
                    { return t->type == Syntax_Token_Type::OPERATOR && t->options.op == Syntax_Operator::COMMA; },
                    true
                );
                auto parenthesis_pos = find_error_recovery_token(
                    [](Syntax_Token* t) -> bool
                    { return t->type == Syntax_Token_Type::PARENTHESIS &&
                    !t->options.parenthesis.is_open && t->options.parenthesis.type == Parenthesis_Type::PARENTHESIS; },
                    true
                );
                enum class Error_Start { COMMA, PARENTHESIS, NOT_FOUND } tactic;
                tactic = Error_Start::NOT_FOUND;
                if (comma_pos.available) {
                    tactic = Error_Start::COMMA;
                }
                if (parenthesis_pos.available) {
                    tactic = Error_Start::PARENTHESIS;
                    if (comma_pos.available && parse_position_in_order(comma_pos.value, parenthesis_pos.value)) {
                        tactic = Error_Start::COMMA;
                    }
                }

                if (tactic == Error_Start::COMMA) {
                    parser.pos = comma_pos.value;
                    advance_token();
                }
                else if (tactic == Error_Start::PARENTHESIS) {
                    parser.pos = parenthesis_pos.value;
                }
                else {
                    CHECKPOINT_EXIT;
                }
            }

            // Parse Return value
            if (test_operator(Syntax_Operator::ARROW)) {
                advance_token();
                signature.return_value = optional_make_success(parse_expression_or_error_expr((Base*)result));
            }

            // Check if its a function or just a function signature
            auto code_block = parse_code_block((Base*)result);
            if (code_block == 0) {
                return result;
            }

            auto signature_expr = result;
            result = allocate_base<Expression>(parent, Base_Type::EXPRESSION);
            result->type = Expression_Type::FUNCTION;
            signature_expr->base.parent = (Base*)result;

            auto& function = result->options.function;
            function.body = code_block;
            function.signature = signature_expr;
            return result;
        }

        if (test_parenthesis_offset('(', 0))
        {
            parser_rollback(checkpoint); // This is pretty stupid, but needed to reset result
            advance_token();
            result = parse_expression_or_error_expr(parent);
            if (!successfull_parenthesis_exit<Parenthesis_Type::PARENTHESIS>()) CHECKPOINT_EXIT;
            return result;
        }

        // Keyword expressions
        if (test_keyword_offset(Syntax_Keyword::NEW, 0)) 
        {
            result->type = Expression_Type::NEW_EXPR;
            result->options.new_expr.count_expr.available = false;
            advance_token();
            if (test_parenthesis_offset('[', 0)) {
                result->options.new_expr.count_expr = optional_make_success(parse_expression_or_error_expr(&result->base));
                if (!successfull_parenthesis_exit<Parenthesis_Type::BRACKETS>()) CHECKPOINT_EXIT;
            }
            result->options.new_expr.type_expr = parse_expression_or_error_expr(&result->base);
            return result;
        }
        if (test_keyword_offset(Syntax_Keyword::STRUCT, 0) ||
            test_keyword_offset(Syntax_Keyword::C_UNION, 0) ||
            test_keyword_offset(Syntax_Keyword::UNION, 0)) 
        {
            result->type = Expression_Type::STRUCTURE_TYPE;
            result->options.structure.members = dynamic_array_create_empty<Definition*>(1);
        }
        if (test_keyword_offset(Syntax_Keyword::ENUM, 0)) {

        }
        if (test_keyword_offset(Syntax_Keyword::MODULE, 0)) {

        }

        // Post operators

    }

    Expression* parse_expression(Base* parent)
    {
        CHECKPOINT_SETUP;
        auto result = allocate_base<Expression>(parent, Base_Type::EXPRESSION);

        if (test_token(Syntax_Token_Type::IDENTIFIER)) {
            result->type = Expression_Type::SYMBOL_READ;
            result->options.symbol_read = get_token(0)->options.identifier;
            advance_token();
            return result;
        }


        CHECKPOINT_EXIT;
    }

    Expression* parse_expression_or_error_expr(Base* parent)
    {
        auto expr = parse_expression(parent);
        if (expr != 0) return expr;
        expr = allocate_base<AST::Expression>(parent, AST::Base_Type::EXPRESSION);
        expr->type = Expression_Type::ERROR_EXPR;
        return expr;
    }

    Definition* parse_definition(Base* parent)
    {
        CHECKPOINT_SETUP;
        auto result = allocate_base<Definition>(parent, AST::Base_Type::DEFINITION);
        result->is_comptime = false;

        if (!test_token(Syntax_Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
        result->name = get_token(0)->options.identifier;
        advance_token();

        if (test_operator(Syntax_Operator::COLON))
        {
            advance_token();
            result->type = optional_make_success(parse_expression_or_error_expr((Base*)result));

            bool is_assign = test_operator(Syntax_Operator::ASSIGN);
            if (is_assign || test_operator(Syntax_Operator::COLON)) {
                result->is_comptime = !is_assign;
                advance_token();
                result->value = optional_make_success(parse_expression_or_error_expr((Base*)result));
            }
        }
        else if (test_operator(Syntax_Operator::DEFINE_COMPTIME)) {
            advance_token();
            result->is_comptime = true;
            result->value = optional_make_success(parse_expression_or_error_expr((Base*)result));
        }
        else if (test_operator(Syntax_Operator::DEFINE_INFER)) {
            advance_token();
            result->is_comptime = false;
            result->value = optional_make_success(parse_expression_or_error_expr((Base*)result));
        }
        else {
            CHECKPOINT_EXIT;
        }

        // Definitions are one line long, so everything afterwards here is an error
        // TODO: Report error
        advance_line();

        return result;
    }

    Module* parse_module(Base* parent, Syntax_Block* block)
    {
        auto result = allocate_base<Module>(parent, Base_Type::MODULE);
        result->definitions = dynamic_array_create_empty<Definition*>(1);

        // Parse block lines
        auto& pos = parser.pos;
        auto backup_pos = pos;
        SCOPE_EXIT(
            backup_pos.allocated_count = pos.allocated_count;
        pos = backup_pos;
        );

        pos.block = block;
        pos.line_index = 0;
        pos.token_index = 0;

        auto& lines = pos.block->lines;
        while (pos.line_index < lines.size)
        {
            // Try parse module things (Currently only definitions)
            // TODO: Parse file-loads, extern imports and probably top level bakes
            auto line = lines[pos.line_index];
            if (line->text.size == 0) {
                pos.line_index += 1;
                continue;
            }

            auto definition = parse_definition((Base*)result);
            if (definition == 0) {
                // Log error for given line
                advance_line();
                continue;
            }
            dynamic_array_push_back(&result->definitions, definition);
        }
        return result;
    }

    // Parsing
    void parser_execute()
    {
        reset();
        parser.root = parse_module(0, syntax_editor.root_block);
    }

#undef CHECKPOINT_EXIT
#undef CHECKPOINT_SETUP
}



