#include "syntax_editor.hpp"

#include "../../rendering/text_renderer.hpp"
#include "../../datastructures/string.hpp"
#include "../../math/vectors.hpp"
#include "../../rendering/renderer_2d.hpp"

#include "../../win32/input.hpp"
#include "syntax_colors.hpp"
#include "compiler.hpp"

// PROTOTYPES
void syntax_editor_insert_line(Syntax_Editor* editor, int index, int indentation_level);

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

// Parsing
void syntax_editor_sanitize_cursor(Syntax_Editor* editor);

void syntax_lexer_parse_line(Syntax_Line* line, Syntax_Editor* editor)
{
    auto& text = line->text;
    auto& cursor = editor->cursor_index;
    auto& tokens = line->tokens;
    dynamic_array_reset(&line->tokens);

    // Tokenize Text
    {
        int index = 0;
        Dynamic_Array<int> unmatched_open_parenthesis = dynamic_array_create_empty<int>(1);
        SCOPE_EXIT(dynamic_array_destroy(&unmatched_open_parenthesis));
        while (index < text.size)
        {
            Syntax_Token token;
            token.info.char_start = index;
            memory_zero(&token.info);

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
                token.options.identifier = identifier_pool_add(editor->identifier_pool, string_create_substring_static(&text, start_index, index));

                // Determine if its a keyword
                Syntax_Keyword* keyword = hashtable_find_element(&editor->keyword_table, *token.options.identifier);
                if (keyword != 0) {
                    token.type = Syntax_Token_Type::KEYWORD;
                    token.options.keyword = *keyword;
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
                index += 1;
                while (index < text.size && char_is_digit(text[index])) {
                    index += 1;
                }
                token.type = Syntax_Token_Type::LITERAL;
                token.options.literal_string = identifier_pool_add(editor->identifier_pool, string_create_substring_static(&text, start_index, index));
            }
            else if (string_contains_character(string_create_static("[](){}"), c))
            {
                index += 1;
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

                token.type = Syntax_Token_Type::PARENTHESIS;
                token.options.parenthesis.type = parenthesis;
                token.options.parenthesis.matching_exists = false;
                token.options.parenthesis.matching_index = -1;
                if (parenthesis.is_open) {
                    dynamic_array_push_back(&unmatched_open_parenthesis, line->tokens.size);
                }
                else
                {
                    // Search backwards for matching parenthesis
                    for (int i = unmatched_open_parenthesis.size - 1; i >= 0; i--)
                    {
                        auto& item = line->tokens[unmatched_open_parenthesis[i]];
                        assert(item.type == Syntax_Token_Type::PARENTHESIS, "");
                        auto& open = item.options.parenthesis;
                        assert(open.type.is_open && !open.matching_exists, "");
                        if (open.type.type == parenthesis.type) {
                            open.matching_exists = true;
                            open.matching_index = line->tokens.size;
                            token.options.parenthesis.matching_exists = true;
                            token.options.parenthesis.matching_index = unmatched_open_parenthesis[i];
                            dynamic_array_rollback_to_size(&unmatched_open_parenthesis, i);
                            break;
                        }
                    }
                }
            }
            else
            {
                // Either operator or Error-Item
                auto& op_table = editor->operator_buffer;
                int longest_index = -1;
                int longest_end = -1;
                // Check all operators
                for (int i = 0; i < op_table.size; i++)
                {
                    auto& op = op_table[i];
                    auto& op_str = op.string;
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
                    token.options.op = &op_table[longest_index];
                }
                else {
                    index += 1;
                    token.type = Syntax_Token_Type::UNEXPECTED_CHAR;
                    token.options.unexpected = c;
                }
            }

            token.info.char_length = index - token.info.char_start;
            dynamic_array_push_back(&line->tokens, token);
        }
    }

    // Early exit so I don't have to deal with the 0 tokens case
    if (tokens.size == 0) {
        cursor = 0;
        string_reset(&text);
        return;
    }

    // MISSING: Find/Report Gaps with Parser

    // Find cursor token
    int cursor_token = -1;
    {
        for (int i = 0; i < tokens.size; i++)
        {
            auto& token = tokens[i];
            if (cursor == token.info.char_start) {
                cursor_token = i;
                break;
            }
            else if (cursor < token.info.char_start) {
                // Cursor is on previous token
                if (i == 0) { // Special case when we have a line starting with a space, and the cursor is before the space '| what'
                    cursor_token = i;
                    break;
                }
                cursor_token = i - 1;
                break;
            }
        }
        // Edge case for cursor on last token
        if (cursor_token == -1) {
            cursor_token = tokens.size - 1;
        }
    }

    // Go through tokens and figure out Spacing between Tokens
    Syntax_Token_Type previous_type = Syntax_Token_Type::UNEXPECTED_CHAR;
    for (int i = 0; i < tokens.size; i++)
    {
        auto& token = tokens[i];
        bool previous_needs_space = false;
        bool needs_space = false;
        switch (token.type)
        {
        case Syntax_Token_Type::IDENTIFIER:
        case Syntax_Token_Type::KEYWORD:
        case Syntax_Token_Type::LITERAL:
            if (previous_type == Syntax_Token_Type::IDENTIFIER || previous_type == Syntax_Token_Type::KEYWORD || previous_type == Syntax_Token_Type::LITERAL) {
                previous_needs_space = true;
            }
            break;
        case Syntax_Token_Type::OPERATOR: 
        {
            switch (token.options.op->type)
            {
            case Operator_Type::BINOP: {
                previous_needs_space = token.options.op->space_before;
                needs_space = token.options.op->space_after;
                break;
            }
            case Operator_Type::UNOP:
                // Unop does not require space before/after, e.g. -*ip
                break;
            case Operator_Type::BOTH: {
                // Determining if - or * is Binop or Unop can be quite hard, but I think this is a good approximation
                if (!(previous_type == Syntax_Token_Type::OPERATOR ||
                     (previous_type == Syntax_Token_Type::PARENTHESIS && tokens[i - 1].options.parenthesis.type.is_open) ||
                     (previous_type == Syntax_Token_Type::KEYWORD) || i == 0)) 
                {
                    previous_needs_space = token.options.op->space_before;
                    needs_space = token.options.op->space_after;
                }
            }
            default: panic("");
            }
            break;
        }
        case Syntax_Token_Type::UNEXPECTED_CHAR:
            previous_needs_space = true;
            needs_space = true;
            break;
        case Syntax_Token_Type::PARENTHESIS:
        case Syntax_Token_Type::GAP:
            break;
        default: panic("");
        }

        token.has_space_after = needs_space;
        if (previous_needs_space && i != 0) {
            tokens[i - 1].has_space_after = true;
        }
        previous_type = token.type;
    }

    // Format the original text
    String new_text = string_create_empty(text.size);
    int new_cursor = 0;
    for (int i = 0; i < tokens.size; i++)
    {
        auto& token = tokens[i];
        if (cursor_token == i) {
            new_cursor = new_text.size + (cursor - tokens[cursor_token].start);
        }

        // Append token without any spaces
        switch (token.type)
        {
        case Syntax_Token_Type::IDENTIFIER:
            string_append_string(&new_text, token.options.identifier);
            break;
        case Syntax_Token_Type::KEYWORD:
            string_append_string(&new_text, &editor->keyword_mapping[(int)token.options.keyword]);
            break;
        case Syntax_Token_Type::LITERAL:
            string_append_string(&new_text, token.options.literal_string);
            break;
        case Syntax_Token_Type::OPERATOR:
            string_append_string(&new_text, &token.options.op->string);
            break;
        case Syntax_Token_Type::PARENTHESIS:
            string_append_character(&new_text, parenthesis_to_char(token.options.parenthesis.type));
            break;
        case Syntax_Token_Type::UNEXPECTED_CHAR:
            string_append_character(&new_text, token.options.unexpected);
            break;
        case Syntax_Token_Type::GAP:
            break;
        default: panic("");
        }

        // Do spacing
        if (token.has_space_after) {
            string_append_formated(&new_text, " ");
        }
    }

    // Now I would replace the original text with the new_text, but for testing I won't
    if (false)
    {
        logg(new_text.characters, "");
        logg("\n");
        string_destroy(&new_text);
    }
    else
    {
        if (string_equals(&new_text, &text)) {
            string_destroy(&new_text);
            return;
        }
        if (new_text.size > 8) {
            logg("Error\n");
        }
        logg("Old: \"%s\"\nNew: \"%s\"\n\n", text.characters, new_text.characters);
        editor->cursor_index = new_cursor;
        syntax_editor_sanitize_cursor(editor);
        string_destroy(&text);
        text = new_text;
    }

}

void line_print_tokens(Syntax_Line* line, Syntax_Editor* editor)
{
    String output = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&output));
    string_append_formated(&output, "--------------\nTOKENS\n----------------\n");

    for (int i = 0; i < line->tokens.size; i++)
    {
        auto& item = line->tokens[i];
        string_append_formated(&output, "#%d: ", i);
        switch (item.type)
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
            if (item.options.parenthesis.matching_exists) {
                string_append_formated(&output, " Matching at %d", item.options.parenthesis.matching_index);
            }
            else {
                string_append_formated(&output, " NO-MATCHING");
            }
            break;
        case Syntax_Token_Type::OPERATOR:
            string_append_formated(&output, "Operator");
            break;
        case Syntax_Token_Type::LITERAL:
            string_append_formated(&output, "Literal");
            break;
        case Syntax_Token_Type::UNEXPECTED_CHAR:
            string_append_formated(&output, "Unexpected Character");
            break;
        default: panic("");
        }

        String substr = string_create_substring_static(&line->text, item.start, item.start + item.length);
        string_append_formated(&output, " ");
        string_append_string(&output, &substr);

        string_append_formated(&output, "\n");
    }

    logg(output.characters);
}


// Editing
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

void syntax_editor_sanitize_cursor(Syntax_Editor* editor)
{
    if (editor->line_index > editor->lines.size) {
        editor->line_index = editor->lines.size - 1;
    }
    if (editor->line_index < 0) {
        editor->line_index = 0;
    }
    auto& text = editor->lines[editor->line_index].text;
    if (text.size == 0) {
        editor->cursor_index = 0;
        return;
    }
    if (editor->cursor_index > text.size) {
        editor->cursor_index = text.size;
    }
    if (editor->cursor_index < 0) {
        editor->cursor_index = 0;
    }
}

void normal_mode_handle_command(Syntax_Editor* editor, Normal_Command command)
{
    auto& current_line = editor->lines[editor->line_index];
    //auto& tokens = current_line.tokens;
    auto& cursor = editor->cursor_index;
    switch (command)
    {
    case Normal_Command::INSERT_AFTER: {
        editor->mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::INSERT_BEFORE: {
        editor->mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::MOVE_LEFT: {
        editor->cursor_index -= 1;
        syntax_editor_sanitize_cursor(editor);
        break;
    }
    case Normal_Command::MOVE_RIGHT: {
        editor->cursor_index += 1;
        syntax_editor_sanitize_cursor(editor);
        break;
    }
                                   /*
                                   case Normal_Command::DELETE_TOKEN: {
                                       syntax_editor_remove_cursor_token(editor);
                                       break;
                                   }
                                   case Normal_Command::CHANGE_TOKEN: {
                                       if (tokens.size == 0) return;
                                       syntax_editor_remove_cursor_token(editor);
                                       editor->mode = Editor_Mode::INPUT;
                                       editor->insert_mode = Insert_Mode::BEFORE;
                                       break;
                                   }
                                   case Normal_Command::INSERT_AT_LINE_END: {
                                       cursor = tokens.size;
                                       editor->mode = Editor_Mode::INPUT;
                                       editor->insert_mode = Insert_Mode::BEFORE;
                                       break;
                                   }
                                   case Normal_Command::INSERT_AT_LINE_START: {
                                       cursor = 0;
                                       editor->mode = Editor_Mode::INPUT;
                                       editor->insert_mode = Insert_Mode::BEFORE;
                                       break;
                                   }
                                   case Normal_Command::MOVE_LINE_START: {
                                       editor->cursor_index = 0;
                                       break;
                                   }
                                   case Normal_Command::MOVE_LINE_END: {
                                       editor->cursor_index = tokens.size;
                                       break;
                                   }
                                   case Normal_Command::ADD_LINE_ABOVE: {
                                       syntax_editor_insert_line(editor, editor->line_index, current_line.indentation_level);
                                       editor->cursor_index = 0;
                                       editor->mode = Editor_Mode::INPUT;
                                       editor->insert_mode = Insert_Mode::BEFORE;
                                       break;
                                   }
                                   case Normal_Command::ADD_LINE_BELOW: {
                                       syntax_editor_insert_line(editor, editor->line_index + 1, current_line.indentation_level);
                                       editor->line_index += 1;
                                       editor->cursor_index = 0;
                                       editor->mode = Editor_Mode::INPUT;
                                       editor->insert_mode = Insert_Mode::BEFORE;
                                       break;
                                   }
                                   case Normal_Command::MOVE_UP: {
                                       if (editor->line_index == 0) return;
                                       int current_pos = syntax_editor_find_visual_cursor_pos(editor, editor->cursor_index, editor->line_index, editor->mode);
                                       int new_line_index = editor->line_index - 1;
                                       auto& new_line = editor->lines[new_line_index];
                                       // Find nearest position
                                       {
                                           int min_dist = 10000;
                                           int min_pos = 0;
                                           for (int i = 0; i <= new_line.tokens.size; i++) {
                                               int new_pos = syntax_editor_find_visual_cursor_pos(editor, i, new_line_index, editor->mode);
                                               int dist = math_absolute(new_pos - current_pos);
                                               if (dist < min_dist) {
                                                   min_pos = i;
                                                   min_dist = dist;
                                               }
                                           }
                                           editor->cursor_index = min_pos;
                                           editor->line_index = new_line_index;
                                       }
                                       break;
                                   }
                                   case Normal_Command::MOVE_DOWN: {
                                       if (editor->line_index + 1 >= editor->lines.size) return;
                                       int current_pos = syntax_editor_find_visual_cursor_pos(editor, editor->cursor_index, editor->line_index, editor->mode);
                                       int new_line_index = editor->line_index + 1;
                                       auto& new_line = editor->lines[new_line_index];
                                       // Find nearest position
                                       {
                                           int min_dist = 10000;
                                           int min_pos = 0;
                                           for (int i = 0; i <= new_line.tokens.size; i++) {
                                               int new_pos = syntax_editor_find_visual_cursor_pos(editor, i, new_line_index, editor->mode);
                                               int dist = math_absolute(new_pos - current_pos);
                                               if (dist < min_dist) {
                                                   min_pos = i;
                                                   min_dist = dist;
                                               }
                                           }
                                           editor->cursor_index = min_pos;
                                           editor->line_index = new_line_index;
                                       }
                                       break;
                                   }
                                   default: panic("");
                                   */
    default:
        break;
    }
}

void insert_mode_handle_command(Syntax_Editor* editor, Input_Command input)
{
    auto& line = editor->lines[editor->line_index];
    auto& text = line.text;
    auto& cursor = editor->cursor_index;
    assert(editor->mode == Editor_Mode::INPUT, "");
    syntax_editor_sanitize_cursor(editor);

    // Handle Universal Inputs
    if (input.type == Input_Command_Type::EXIT_INSERT_MODE) {
        editor->mode = Editor_Mode::NORMAL;
        syntax_editor_sanitize_cursor(editor);
        return;
    }
    /*
    if (input.type == Input_Command_Type::ENTER) {
        syntax_editor_insert_line(editor, editor->line_index + 1, editor->lines[editor->line_index].indentation_level);
        editor->line_index = editor->line_index + 1;
        editor->cursor_index = 0;
        return;
    }
    if (input.type == Input_Command_Type::ENTER_REMOVE_ONE_INDENT) {
        int indent = math_maximum(0, editor->lines[editor->line_index].indentation_level - 1);
        syntax_editor_insert_line(editor, editor->line_index + 1, indent);
        editor->line_index = editor->line_index + 1;
        editor->cursor_index = 0;
        return;
    }
    if (input.type == Input_Command_Type::ADD_INDENTATION) {
        if (cursor == 0 && editor->insert_mode == Insert_Mode::BEFORE) {
            line.indentation_level += 1;
        }
        return;
    }
    if (input.type == Input_Command_Type::REMOVE_INDENTATION) {
        if (cursor == 0 && editor->insert_mode == Insert_Mode::BEFORE && line.indentation_level > 0) {
            line.indentation_level -= 1;
        }
        return;
    }
    */

    switch (input.type)
    {
    case Input_Command_Type::DELIMITER_LETTER:
        string_insert_character_before(&text, input.letter, editor->cursor_index);
        editor->cursor_index += 1;
        break;
    case Input_Command_Type::SPACE:
        string_insert_character_before(&text, ' ', editor->cursor_index);
        editor->cursor_index += 1;
        break;
    case Input_Command_Type::BACKSPACE:
        string_remove_character(&text, editor->cursor_index - 1);
        editor->cursor_index -= 1;
        break;
    case Input_Command_Type::IDENTIFIER_LETTER:
        string_insert_character_before(&text, input.letter, editor->cursor_index);
        editor->cursor_index += 1;
        break;
    case Input_Command_Type::NUMBER_LETTER:
        string_insert_character_before(&text, input.letter, editor->cursor_index);
        editor->cursor_index += 1;
        break;
    default: break;
    }
    syntax_editor_sanitize_cursor(editor);
}

void syntax_editor_update(Syntax_Editor* editor, Input* input)
{
    for (int i = 0; i < input->key_messages.size; i++)
    {
        Key_Message msg = input->key_messages[i];
        if (editor->mode == Editor_Mode::INPUT)
        {
            Input_Command input;
            if (msg.character == 32 && msg.key_down) {
                input.type = Input_Command_Type::SPACE;
            }
            else if (msg.key_code == Key_Code::L && msg.key_down && msg.ctrl_down) {
                input.type = Input_Command_Type::EXIT_INSERT_MODE;
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
            insert_mode_handle_command(editor, input);
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
            normal_mode_handle_command(editor, command);
        }
    }
}

void syntax_editor_insert_line(Syntax_Editor* editor, int index, int indentation_level)
{
    Syntax_Line line;
    line.tokens = dynamic_array_create_empty<Syntax_Token>(1);
    line.text = string_create_empty(1);
    line.indentation_level = indentation_level;
    dynamic_array_insert_ordered(&editor->lines, line, index);
}

void operator_mapping_set(Syntax_Editor* editor, Syntax_Operator** op, const char* str, Operator_Type type, bool space_before, bool space_after, int buffer_index)
{
    assert(buffer_index < editor->operator_buffer.size, "");
    editor->operator_buffer[buffer_index].string = string_create_static(str);
    editor->operator_buffer[buffer_index].type = type;
    editor->operator_buffer[buffer_index].space_after = space_after;
    editor->operator_buffer[buffer_index].space_before = space_before;
    *op = &editor->operator_buffer[buffer_index];
}

Syntax_Editor* syntax_editor_create(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D)
{
    Syntax_Editor* result = new Syntax_Editor;
    result->cursor_index = 0;
    result->mode = Editor_Mode::INPUT;
    result->line_index = 0;
    result->lines = dynamic_array_create_empty<Syntax_Line>(1);
    syntax_editor_insert_line(result, 0, 0);

    result->identifier_pool = new Identifier_Pool;
    *result->identifier_pool = identifier_pool_create();

    result->text_renderer = text_renderer;
    result->rendering_core = rendering_core;
    result->renderer_2D = renderer_2D;

    // Add mapping infos
    {
        auto& keyword_table = result->keyword_table;
        auto& keyword_mapping = result->keyword_mapping;

        keyword_table = hashtable_create_empty<String, Syntax_Keyword>(8, hash_string, string_equals);
        keyword_mapping = array_create_empty<String>((int)Syntax_Keyword::MAX_ENUM_VALUE);

        keyword_mapping[(int)Syntax_Keyword::BREAK] = string_create_static("break");
        keyword_mapping[(int)Syntax_Keyword::CASE] = string_create_static("case");
        keyword_mapping[(int)Syntax_Keyword::CAST] = string_create_static("cast");
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

        auto& op_map = result->operator_mapping;
        int operator_count = sizeof(Operator_Mapping) / sizeof(Syntax_Operator*);
        result->operator_buffer = array_create_empty<Syntax_Operator>(operator_count);

        int buffer_index = 0;
        operator_mapping_set(result, &op_map.addition, "+", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.subtraction, "-", Operator_Type::BOTH, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.divison, "/", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.multiply, "*", Operator_Type::BOTH, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.modulo, "%", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.comma, ",", Operator_Type::BINOP, false, true, buffer_index++);
        operator_mapping_set(result, &op_map.dot, ".", Operator_Type::BINOP, false, false, buffer_index++);
        operator_mapping_set(result, &op_map.tilde, "~", Operator_Type::BINOP, false, false, buffer_index++);
        operator_mapping_set(result, &op_map.colon, ":", Operator_Type::BINOP, false, true, buffer_index++);
        operator_mapping_set(result, &op_map.assign, "=", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.not, "!", Operator_Type::UNOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.ampersand, "&", Operator_Type::UNOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.less_than, "<", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.greater_than, ">", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.less_equal, "<=", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.greater_equal, ">=", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.equals, "==", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.not_equals, "!=", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.pointer_equals, "*==", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.pointer_not_equals, "*!=", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.define_comptime, "::", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.define_infer, ":=", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.and, "&&", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map. or , "||", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(result, &op_map.arrow, "->", Operator_Type::BINOP, true, true, buffer_index++);
        assert(operator_count == buffer_index, "");
    }

    return result;
}

void syntax_editor_destroy(Syntax_Editor* editor)
{
    for (int i = 0; i < editor->lines.size; i++)
    {
        auto& line = editor->lines[i];
        string_destroy(&line.text);
        dynamic_array_destroy(&line.tokens);
    }
    identifier_pool_destroy(editor->identifier_pool);
    delete editor->identifier_pool;
    dynamic_array_destroy(&editor->lines);
    array_destroy(&editor->operator_buffer);
    array_destroy(&editor->keyword_mapping);
    hashtable_destroy(&editor->keyword_table);
    delete editor;
}



// Rendering
void syntax_editor_draw_underline(Syntax_Editor* editor, int line, int character, int length, vec3 color)
{
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
    vec2 size = char_size * vec2((float)length, 1 / 8.0f);
    vec2 edge_point = vec2(-1.0f, 1.0f) * scaling_factor;
    vec2 cursor = edge_point + vec2(character * char_size.x, -(line + 1) * char_size.y);
    cursor = cursor + size * vec2(0.5f, 0.5f);
    renderer_2D_add_rectangle(editor->renderer_2D, cursor, size, color, 0.0f);
}

void syntax_editor_draw_cursor_line(Syntax_Editor* editor, vec3 color, int line, int character)
{
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

void syntax_editor_draw_character_box(Syntax_Editor* editor, vec3 color, int line, int character)
{
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

void syntax_editor_draw_string(Syntax_Editor* editor, String string, vec3 color, int line, int character)
{
    vec2 pos = vec2(-1.0f, 1.0f) + vec2(character, -(line + 1)) * editor->character_size;
    text_renderer_set_color(editor->text_renderer, color);
    text_renderer_add_text(editor->text_renderer, &string, pos, editor->character_size.y, 1.0f);
}

void syntax_editor_render_line(Syntax_Editor* editor, int line_index)
{
    auto& line = editor->lines[line_index];
    syntax_editor_draw_string(editor, line.text, Syntax_Color::TEXT, line_index, 0);

    // Render Cursor
    if (line_index == editor->line_index) {
        int cursor_pos = editor->cursor_index;
        if (editor->mode == Editor_Mode::NORMAL) {
            syntax_editor_draw_character_box(editor, Syntax_Color::COMMENT, line_index, cursor_pos);
        }
        else {
            syntax_editor_draw_cursor_line(editor, Syntax_Color::COMMENT, line_index, cursor_pos);
        }
    }
}

void syntax_editor_render(Syntax_Editor* editor)
{
    auto& cursor = editor->cursor_index;

    // Prepare Render
    editor->character_size.y = text_renderer_cm_to_relative_height(editor->text_renderer, editor->rendering_core, 0.8f);
    editor->character_size.x = text_renderer_get_cursor_advance(editor->text_renderer, editor->character_size.y);

    // Render lines
    for (int i = 0; i < editor->lines.size; i++) {
        syntax_lexer_parse_line(&editor->lines[i], editor);
        //line_print_tokens(&editor->lines[i], editor);
        syntax_editor_render_line(editor, i);
    }

    // Render Primitives
    renderer_2D_render(editor->renderer_2D, editor->rendering_core);
    text_renderer_render(editor->text_renderer, editor->rendering_core);
}