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
enum class Operator_Type
{
    BINOP,
    UNOP,
    BOTH,
};

struct Syntax_Operator
{
    String string;
    Operator_Type type;
    bool space_before;
    bool space_after;
};

struct Operator_Mapping
{
    Syntax_Operator* addition;
    Syntax_Operator* subtraction;
    Syntax_Operator* divison;
    Syntax_Operator* multiply;
    Syntax_Operator* modulo;
    Syntax_Operator* comma;
    Syntax_Operator* dot;
    Syntax_Operator* tilde;
    Syntax_Operator* colon;
    Syntax_Operator* assign;
    Syntax_Operator* not;
    Syntax_Operator* ampersand;
    Syntax_Operator* less_than;
    Syntax_Operator* greater_than;

    Syntax_Operator* less_equal;
    Syntax_Operator* greater_equal;
    Syntax_Operator* equals;
    Syntax_Operator* not_equals;
    Syntax_Operator* pointer_equals;
    Syntax_Operator* pointer_not_equals;

    Syntax_Operator* define_comptime;
    Syntax_Operator* define_infer;

    Syntax_Operator* and;
    Syntax_Operator* or;
    Syntax_Operator* arrow;
};

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
    LITERAL,
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
        Syntax_Operator* op;
        String* identifier;
        String* literal_string;
        Syntax_Keyword keyword;
        char unexpected;
        struct {
            Parenthesis type;
            bool matching_exists;
            int matching_index;
        } parenthesis;
    } options;
};

// EDITOR
enum class Editor_Mode
{
    NORMAL,
    INPUT,
};

struct Syntax_Line
{
    String text;
    Dynamic_Array<Syntax_Token> tokens;
    int indentation_level;
};

struct Syntax_Editor
{
    // Editing
    Editor_Mode mode;
    Dynamic_Array<Syntax_Line> lines;
    int cursor_index; // Dependent on Editor_Mode
    int line_index;

    Hashtable<String, Syntax_Keyword> keyword_table;
    Array<String> keyword_mapping;
    Operator_Mapping operator_mapping;
    Array<Syntax_Operator> operator_buffer;
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
void syntax_editor_insert_line(int index, int indentation_level);
void syntax_editor_sanitize_cursor();



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

String syntax_token_as_string(Syntax_Token token)
{
    auto& editor = syntax_editor;
	switch (token.type)
	{
	case Syntax_Token_Type::IDENTIFIER:
		return *token.options.identifier;
	case Syntax_Token_Type::KEYWORD:
		return editor.keyword_mapping[(int)token.options.keyword];
	case Syntax_Token_Type::LITERAL:
		return *token.options.literal_string;
	case Syntax_Token_Type::OPERATOR:
		return token.options.op->string;
	case Syntax_Token_Type::PARENTHESIS: {
		switch (token.options.parenthesis.type.type) {
		case Parenthesis_Type::BRACES:
			return token.options.parenthesis.type.is_open ?  string_create_static("{") : string_create_static("}");
		case Parenthesis_Type::BRACKETS:
			return token.options.parenthesis.type.is_open ?  string_create_static("[") : string_create_static("]");
		case Parenthesis_Type::PARENTHESIS:
			return token.options.parenthesis.type.is_open ?  string_create_static("(") : string_create_static(")");
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
	return type == Syntax_Token_Type::IDENTIFIER || type == Syntax_Token_Type::KEYWORD || type == Syntax_Token_Type::LITERAL;
}

bool string_test_char(String str, int char_index, char c)
{
    if (char_index > str.size) return false;
    return str[char_index] == c;
}

int get_cursor_token_index() 
{
	auto& cursor = syntax_editor.cursor_index;
	auto& tokens = syntax_editor.lines[syntax_editor.line_index].tokens;

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
	auto& tokens = syntax_editor.lines[syntax_editor.line_index].tokens;
    return tokens[get_cursor_token_index()];
}


// Parsing
void syntax_lexer_parse_line(Syntax_Line* line)
{
	auto& text = line->text;
	auto& cursor = syntax_editor.cursor_index;
	auto& tokens = line->tokens;
	dynamic_array_reset(&line->tokens);

	/*
	Algorithm:
		Tokenize Text
		Determine Cursor-Token + Cursor offset
        Determine Essential Spaces
		-LATER: Determine Gaps based on parsing
		Determine render-space before/after each Token
		Trim Text based on token-info, update token-text mapping
		Set cursor-char based on prev. cursor-token
		Render line based on Token render-spacing
	*/

	// Tokenize Text
	{
		int index = 0;
		Dynamic_Array<int> unmatched_open_parenthesis = dynamic_array_create_empty<int>(1);
		SCOPE_EXIT(dynamic_array_destroy(&unmatched_open_parenthesis));
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
				token.type = Syntax_Token_Type::LITERAL;
				token.options.literal_string = identifier_pool_add(syntax_editor.identifier_pool, string_create_substring_static(&text, start_index, index));
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
				auto& op_table = syntax_editor.operator_buffer;
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

			token.info.char_end = index;
			dynamic_array_push_back(&line->tokens, token);
		}
	}

	// Early exit so I don't have to deal with the 0 tokens case
	if (tokens.size == 0) {
		cursor = 0;
		string_reset(&text);
        Syntax_Token dummy;
        dummy.type = Syntax_Token_Type::DUMMY;
        dummy.info.char_start = 0;
        dummy.info.char_end = 0;
        dummy.info.format_space_after = false;
        dummy.info.format_space_before = false;
        dummy.info.screen_pos = 0;
        dummy.info.screen_size = 1;
        dynamic_array_push_back(&tokens, dummy);
		return;
	}

    // Find critical spaces
    Array<int> critical_spaces = array_create_empty<int>(tokens.size + 1);
    SCOPE_EXIT(array_destroy(&critical_spaces));
    for (int i = 0; i < critical_spaces.size; i++)
    {
        auto& spaces = critical_spaces[i];

        // Gather information
        bool prev_is_critical = i > 0 ? is_space_critical(tokens[i-1].type) : false;
        bool curr_is_critical = i < tokens.size ? is_space_critical(tokens[i].type) : false;;
        bool space_before_cursor = string_test_char(text, cursor - 1, ' ');
        bool space_after_cursor = string_test_char(text, cursor, ' ');
        // Space before/after is only necessary if the cursor is between the tokens 
        {
            int prev_end = i > 0 ? tokens[i-1].info.char_end : 0;
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
    if (&syntax_editor.lines[syntax_editor.line_index] == line)
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
        case Syntax_Token_Type::LITERAL:
        case Syntax_Token_Type::PARENTHESIS:
        case Syntax_Token_Type::GAP:
        case Syntax_Token_Type::KEYWORD:
            break;
        case Syntax_Token_Type::OPERATOR:
        {
            switch (token.options.op->type)
            {
            case Operator_Type::BINOP: {
                info.format_space_before = token.options.op->space_before;
                info.format_space_after = token.options.op->space_after;
                break;
            }
            case Operator_Type::UNOP:
                info.format_space_before = is_space_critical(previous_type);
                break;
            case Operator_Type::BOTH: {
                // Determining if - or * is Binop or Unop can be quite hard, but I think this is a good approximation
                if (!(previous_type == Syntax_Token_Type::OPERATOR ||
                    (previous_type == Syntax_Token_Type::PARENTHESIS && tokens[i - 1].options.parenthesis.type.is_open) ||
                    (previous_type == Syntax_Token_Type::KEYWORD) || i == 0))
                {
                    info.format_space_before = token.options.op->space_before;
                    info.format_space_after = token.options.op->space_after;
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

    // Add render spacing for essential tokens
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
        cursor = new_cursor;
        syntax_editor_sanitize_cursor();
        string_destroy(&text);
        text = new_text;
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
            if (token.options.parenthesis.matching_exists) {
                string_append_formated(&output, " Matching at %d", token.options.parenthesis.matching_index);
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

        String substr = syntax_token_as_string(token);
        string_append_formated(&output, " ");
        string_append_string(&output, &substr);

        string_append_formated(&output, "\n");
    }

    logg(output.characters);
}


// Editing
void syntax_editor_sanitize_cursor()
{
    auto& editor = syntax_editor;
    auto& lines = editor.lines;
    auto& line = editor.line_index;
    auto& text = editor.lines[line].text;
    auto& cursor = editor.cursor_index;

    line = math_clamp(line, 0, lines.size - 1);
    cursor = math_clamp(cursor, 0, editor.mode == Editor_Mode::INPUT ? text.size : math_maximum(0, text.size - 1));
    if (editor.mode == Editor_Mode::NORMAL) {
        cursor = get_cursor_token().info.char_start;
    }
}

void normal_mode_handle_command(Normal_Command command)
{
    auto& editor = syntax_editor;
    auto& current_line = editor.lines[editor.line_index];
    auto& cursor = editor.cursor_index;
    auto& mode = editor.mode;
    auto& tokens = current_line.tokens;

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
        cursor = current_line.text.size;
        mode = Editor_Mode::INPUT;
        break;
    }
    case Normal_Command::INSERT_AT_LINE_START: {
        cursor = 0;
        mode = Editor_Mode::INPUT;
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

void insert_mode_handle_command(Input_Command input)
{
    auto& mode = syntax_editor.mode;
    auto& line = syntax_editor.lines[syntax_editor.line_index];
    auto& text = line.text;
    auto& cursor = syntax_editor.cursor_index;

    assert(mode == Editor_Mode::INPUT, "");
    syntax_editor_sanitize_cursor();

    // Handle Universal Inputs
    if (input.type == Input_Command_Type::EXIT_INSERT_MODE) {
        mode = Editor_Mode::NORMAL;
        syntax_editor_sanitize_cursor();
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
        string_insert_character_before(&text, input.letter, cursor);
        cursor += 1;
        break;
    case Input_Command_Type::SPACE:
        string_insert_character_before(&text, ' ', cursor);
        cursor += 1;
        break;
    case Input_Command_Type::BACKSPACE:
        string_remove_character(&text, cursor - 1);
        cursor -= 1;
        break;
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
}

void syntax_editor_insert_line(int index, int indentation_level)
{
    Syntax_Line line;
    line.tokens = dynamic_array_create_empty<Syntax_Token>(1);
    line.text = string_create_empty(1);
    line.indentation_level = indentation_level;
    dynamic_array_insert_ordered(&syntax_editor.lines, line, index);
}

void operator_mapping_set(Syntax_Operator** op, const char* str, Operator_Type type, bool space_before, bool space_after, int buffer_index)
{
    auto& buffer = syntax_editor.operator_buffer;
    assert(buffer_index < buffer.size, "");
    buffer[buffer_index].string = string_create_static(str);
    buffer[buffer_index].type = type;
    buffer[buffer_index].space_after = space_after;
    buffer[buffer_index].space_before = space_before;
    *op = &buffer[buffer_index];
}

void syntax_editor_create(Rendering_Core* rendering_core, Text_Renderer* text_renderer, Renderer_2D* renderer_2D, Input* input)
{
    memory_zero(&syntax_editor);
    syntax_editor.cursor_index = 0;
    syntax_editor.mode = Editor_Mode::INPUT;
    syntax_editor.line_index = 0;
    syntax_editor.lines = dynamic_array_create_empty<Syntax_Line>(1);
    syntax_editor_insert_line(0, 0);

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

        auto& op_map = syntax_editor.operator_mapping;
        int operator_count = sizeof(Operator_Mapping) / sizeof(Syntax_Operator*);
        syntax_editor.operator_buffer = array_create_empty<Syntax_Operator>(operator_count);

        int buffer_index = 0;
        operator_mapping_set(&op_map.addition, "+", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.subtraction, "-", Operator_Type::BOTH, true, true, buffer_index++);
        operator_mapping_set(&op_map.divison, "/", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.multiply, "*", Operator_Type::BOTH, true, true, buffer_index++);
        operator_mapping_set(&op_map.modulo, "%", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.comma, ",", Operator_Type::BINOP, false, true, buffer_index++);
        operator_mapping_set(&op_map.dot, ".", Operator_Type::BINOP, false, false, buffer_index++);
        operator_mapping_set(&op_map.tilde, "~", Operator_Type::BINOP, false, false, buffer_index++);
        operator_mapping_set(&op_map.colon, ":", Operator_Type::BINOP, false, true, buffer_index++);
        operator_mapping_set(&op_map.assign, "=", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.not, "!", Operator_Type::UNOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.ampersand, "&", Operator_Type::UNOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.less_than, "<", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.greater_than, ">", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.less_equal, "<=", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.greater_equal, ">=", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.equals, "==", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.not_equals, "!=", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.pointer_equals, "*==", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.pointer_not_equals, "*!=", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.define_comptime, "::", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.define_infer, ":=", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.and, "&&", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map. or , "||", Operator_Type::BINOP, true, true, buffer_index++);
        operator_mapping_set(&op_map.arrow, "->", Operator_Type::BINOP, true, true, buffer_index++);
        assert(operator_count == buffer_index, "");
    }
}

void syntax_editor_destroy()
{
    auto& editor = syntax_editor;
    for (int i = 0; i < editor.lines.size; i++)
    {
        auto& line = editor.lines[i];
        string_destroy(&line.text);
        dynamic_array_destroy(&line.tokens);
    }
    identifier_pool_destroy(editor.identifier_pool);
    delete editor.identifier_pool;
    dynamic_array_destroy(&editor.lines);
    array_destroy(&editor.operator_buffer);
    array_destroy(&editor.keyword_mapping);
    hashtable_destroy(&editor.keyword_table);
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

void syntax_editor_render_line(int line_index)
{
    auto editor = &syntax_editor;
    int pos = 0;
    int cursor_pos = 0;
    auto cursor = editor->cursor_index;
    auto& line = editor->lines[line_index];
    auto& tokens = line.tokens;

    // Token rendering
    for (int i = 0; i < tokens.size; i++)
    {
        auto& token = tokens[i];
        auto& info = token.info;

        pos += info.format_space_before ? 1 : 0;
        info.screen_pos = pos;
        String str = syntax_token_as_string(token);
        info.screen_size = str.size;
        syntax_editor_draw_string(str, token.type == Syntax_Token_Type::KEYWORD ? Syntax_Color::KEYWORD : Syntax_Color::TEXT, line_index, pos);
        pos += str.size + (info.format_space_after ? 1 : 0);
    }

    // Draw Cursor
    if (line_index == editor->line_index && tokens.size > 0)
    {
        auto& info = get_cursor_token().info;
        int cursor_pos = info.screen_pos + (cursor - info.char_start);
        if (editor->mode == Editor_Mode::NORMAL)
        {
            int box_start = info.screen_pos;
            int box_end = info.screen_pos + info.screen_size;
            for (int i = box_start; i < box_end; i++) {
                syntax_editor_draw_character_box(vec3(0.2f), line_index, i);
            }
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line_index, box_start);
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line_index, box_end);
        }
        else {
            if (info.format_space_before && cursor == info.char_start) {
                cursor_pos -= 1;
            }
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line_index, cursor_pos);
        }
    }

    // Draw Text-Representation 2 lines below
    if (true) {
        line_index += 2;
        syntax_editor_draw_string(line.text, Syntax_Color::TEXT, line_index, 0);
        if (editor->mode == Editor_Mode::NORMAL) {
            syntax_editor_draw_character_box(Syntax_Color::COMMENT, line_index, cursor);
        }
        else {
            syntax_editor_draw_cursor_line(Syntax_Color::COMMENT, line_index, cursor);
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
    for (int i = 0; i < editor.lines.size; i++) {
        syntax_lexer_parse_line(&editor.lines[i]);
        syntax_editor_sanitize_cursor();
        //line_print_tokens(&editor->lines[i], editor);
        syntax_editor_render_line(i);
    }

    // Render Primitives
    renderer_2D_render(editor.renderer_2D, editor.rendering_core);
    text_renderer_render(editor.text_renderer, editor.rendering_core);
}