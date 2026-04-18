#include "tokenizer.hpp"

#include "../../datastructures/hashtable.hpp"
#include "../../utility/character_info.hpp"
#include "source_code.hpp"

// Tokenizer
struct Tokenizer
{
    Hashtable<String, Token_Type> keyword_table;
    Hashtable<String, Token_Type> operator_table;
};

static Tokenizer tokenizer;

const char* token_type_as_cstring(Token_Type token_type)
{
    // Initialize token strings
    switch (token_type)
    {
    case Token_Type::IDENTIFIER: return "_IDENTIFIER_";
    case Token_Type::COMMENT: return "_COMMENT_";
    case Token_Type::INVALID: return "_INVALID"; 

    case Token_Type::BLOCK_START: return "_BLOCK_START_";
    case Token_Type::BLOCK_END: return "_BLOCK_END_";
    case Token_Type::LINE_END: return "LINE_END";

    case Token_Type::LITERAL_INTEGER: return "_LITERAL_INTEGER_";
    case Token_Type::LITERAL_FLOAT: return "_LITERAL_FLOAT_";
    case Token_Type::LITERAL_STRING: return "_LITERAL_STRING_";

    case Token_Type::LITERAL_TRUE: return "true";
    case Token_Type::LITERAL_FALSE: return "false";
    case Token_Type::LITERAL_NIL: return "nil";

    case Token_Type::_OPERATORS_START_: return "_OPERATORS_START_";
    case Token_Type::PLUS: return "+";
    case Token_Type::MINUS: return "-";
    case Token_Type::SLASH: return "/";
    case Token_Type::ASTERIX: return "*";
    case Token_Type::PERCENTAGE: return "%";

    case Token_Type::EQUALS: return "==";
    case Token_Type::NOT_EQUALS: return "!=";
    case Token_Type::POINTER_EQUALS: return "*==";
    case Token_Type::POINTER_NOT_EQUALS: return "*!=";
    case Token_Type::LESS_THAN: return "<";
    case Token_Type::GREATER_THAN: return ">";
    case Token_Type::LESS_EQUAL: return "<=";
    case Token_Type::GREATER_EQUAL: return ">=";

    case Token_Type::AND: return "&&";
    case Token_Type::OR: return "||";
    case Token_Type::NOT: return "!";

    case Token_Type::PARENTHESIS_OPEN: return "(";
    case Token_Type::PARENTHESIS_CLOSED: return ")";
    case Token_Type::BRACKET_OPEN: return "[";
    case Token_Type::BRACKET_CLOSED: return "]";
    case Token_Type::CURLY_BRACE_OPEN: return "{";
    case Token_Type::CURLY_BRACE_CLOSED: return "}";

    case Token_Type::COMMA: return ",";
    case Token_Type::DOT: return ".";
    case Token_Type::TILDE: return "~";
    case Token_Type::TILDE_STAR: return "~*";
    case Token_Type::TILDE_STAR_STAR: return "~**";
    case Token_Type::COLON: return ":";
    case Token_Type::SEMI_COLON: return ";";
    case Token_Type::APOSTROPHE: return "'";
    case Token_Type::QUESTION_MARK: return "?";
    case Token_Type::OPTIONAL_POINTER: return "?*";
    case Token_Type::POSTFIX_CALL_ARROW: return "->";
    case Token_Type::SUBTYPE_ACCESS: return ".>";
    case Token_Type::BASETYPE_ACCESS: return ".<";
    case Token_Type::ADDRESS_OF: return "-*";
    case Token_Type::DEREFERENCE: return "-&";
    case Token_Type::OPTIONAL_DEREFERENCE: return "-?&";
    case Token_Type::FUNCTION_ARROW: return "=>";
    case Token_Type::DOLLAR: return "$";
    case Token_Type::ASSIGN: return "=";
    case Token_Type::ASSIGN_ADD: return "+=";
    case Token_Type::ASSIGN_SUB: return "-=";
    case Token_Type::ASSIGN_MULT: return "*=";
    case Token_Type::ASSIGN_DIV: return "/=";
    case Token_Type::ASSIGN_MODULO: return "%=";
    case Token_Type::UNINITIALIZED: return "_";

    case Token_Type::_OPERATORS_END_: return "_OPERATORS_END_";

    case Token_Type::_KEYWORDS_START_: return "_KEYWORDS_START_";

    case Token_Type::FUNCTION_KEYWORD: return "fn";
    case Token_Type::MODULE: return "module";
    case Token_Type::STRUCT: return "struct";
    case Token_Type::UNION: return "union";
    case Token_Type::ENUM: return "enum";
    case Token_Type::VAR: return "var";
    case Token_Type::GLOBAL_KEYWORD: return "global";
    case Token_Type::CONST_KEYWORD: return "const";
    case Token_Type::OPERATORS: return "operators";
    case Token_Type::IMPORT: return "import";
    case Token_Type::AS: return "as";
    case Token_Type::EXTERN: return "exter";
    case Token_Type::RETURN: return "return";
    case Token_Type::BREAK: return "break";
    case Token_Type::CONTINUE: return "continue";
    case Token_Type::IF: return "if";
    case Token_Type::ELSE: return "else";
    case Token_Type::LOOP: return "loop";
    case Token_Type::IN_KEYWORD: return "in";
    case Token_Type::SWITCH: return "switch";
    case Token_Type::DEFAULT: return "default";
    case Token_Type::DEFER: return "defer";
    case Token_Type::DEFER_RESTORE: return "defer_restore";
    case Token_Type::CAST: return "cast";
    case Token_Type::SCOPE: return "scope";

    case Token_Type::BAKE: return "#bake";
    case Token_Type::INSTANCIATE: return "#instanciate";
    case Token_Type::GET_OVERLOAD: return "#get_overload";
    case Token_Type::GET_OVERLOAD_POLY: return "#get_overload_poly";

    case Token_Type::_KEYWORDS_END_: return "_KEYWORDS_END_";

    default: panic("");
    }

    panic("");
    return "";
}

Token_Class token_type_get_class(Token_Type type)
{
	switch (type)
	{
		case Token_Type::BLOCK_START:
		case Token_Type::PARENTHESIS_OPEN:
		case Token_Type::BRACKET_OPEN:   
		case Token_Type::CURLY_BRACE_OPEN:
			return Token_Class::LIST_START;

		case Token_Type::BLOCK_END:
		case Token_Type::PARENTHESIS_CLOSED: 
		case Token_Type::BRACKET_CLOSED:     
		case Token_Type::CURLY_BRACE_CLOSED: 
			return Token_Class::LIST_END;

		case Token_Type::COMMA:
		case Token_Type::LINE_END: 
		case Token_Type::SEMI_COLON:
			return Token_Class::SEPERATOR;
	}

	return Token_Class::OTHER;
}

bool token_type_is_operator(Token_Type type) {
    return (int)type > (int)Token_Type::_OPERATORS_START_ && (int)type < (int)Token_Type::_OPERATORS_END_;
}

bool token_type_is_keyword(Token_Type type) {
    return (int)type > (int)Token_Type::_KEYWORDS_START_ && (int)type < (int)Token_Type::_KEYWORDS_END_;
}

Token_Type token_type_get_partner(Token_Type type)
{
	switch (type)
	{
	case Token_Type::BLOCK_START:        return Token_Type::BLOCK_END;
	case Token_Type::BLOCK_END:          return Token_Type::BLOCK_START;
	case Token_Type::PARENTHESIS_OPEN:   return Token_Type::PARENTHESIS_CLOSED;
	case Token_Type::PARENTHESIS_CLOSED: return Token_Type::PARENTHESIS_OPEN; 
	case Token_Type::BRACKET_OPEN:       return Token_Type::BRACKET_CLOSED; 
	case Token_Type::BRACKET_CLOSED:     return Token_Type::BRACKET_OPEN;  
	case Token_Type::CURLY_BRACE_OPEN:   return Token_Type::CURLY_BRACE_CLOSED;
	case Token_Type::CURLY_BRACE_CLOSED: return Token_Type::CURLY_BRACE_OPEN;  
	}

	return Token_Type::INVALID;
}


int tokenizer_initialize()
{
    // Initialize keyword lookup table
    {
        tokenizer.keyword_table = hashtable_create_empty<String, Token_Type>(1, hash_string, string_equals);
        auto insert_keyword_token = [](Token_Type token_type) {
            String string = string_create_static(token_type_as_cstring(token_type));
            bool worked = hashtable_insert_element(&tokenizer.keyword_table, string, token_type);
            assert(worked, "all keywords must be unique!");
            };

        for (int i = (int)Token_Type::_KEYWORDS_START_ + 1; i < (int)Token_Type::_KEYWORDS_END_; i++) {
            insert_keyword_token((Token_Type)i);
        }

        // We also have true, false, and null in the keyword table
        insert_keyword_token(Token_Type::LITERAL_NIL);
        insert_keyword_token(Token_Type::LITERAL_TRUE);
        insert_keyword_token(Token_Type::LITERAL_FALSE);
    }

    // Initializer operator lookup table
    {
        tokenizer.operator_table = hashtable_create_empty<String, Token_Type>(1, hash_string, string_equals);
        for (int i = (int)Token_Type::_OPERATORS_START_ + 1; i < (int)Token_Type::_OPERATORS_END_; i++)
        {
            bool worked = hashtable_insert_element(
                &tokenizer.operator_table,
                string_create_static(token_type_as_cstring((Token_Type)i)),
                (Token_Type)i
            );
            assert(worked, "operator strings must be unique");
        }
    }

    return 0;
}

static int _initialization_trick = tokenizer_initialize();

Token token_make(Token_Type type, int start, int end, int line) {
    Token token;
    token.type = type;
    token.start = start;
    token.end = end;
    token.line = line;
    return token;
}


void tokenizer_tokenize_single_line(String text, DynArray<Token>* tokens, int line_index, bool remove_comments)
{
    int index = 0;
    while (index < text.size)
    {
        Token token = token_make(Token_Type::INVALID, index, 0, line_index);

        char c = text[index];
        // Identifier/Keyword start
        if (char_is_letter(c) || c == '#')
        {
            bool starts_with_hashtag = c == '#';
            index += 1;
            while (index < text.size && char_is_valid_identifier(text[index])) {
                index += 1;
            }

            String substring = string_create_substring_static(&text, token.start, index);
            Token_Type* keyword = hashtable_find_element(&tokenizer.keyword_table, substring);
            if (keyword != nullptr) {
                token.type = *keyword;
            }
            else {
                token.type = starts_with_hashtag ? Token_Type::INVALID : Token_Type::IDENTIFIER;
                token.options.string_value = nullptr;
            }
        }
        // String literal start
        else if (c == '"')
        {
            index += 1; // Skip over "
            bool found_valid_end = false;
            while (index < text.size) 
            {
                if (text[index] == '"') 
                {
                    found_valid_end = true;
                    index += 1;
                    break;
                }
                else if (text[index] == '\\') {
                    index += 1; // Skip escaped character
                }
                index += 1;
            }

            token.type = found_valid_end ? Token_Type::LITERAL_STRING : Token_Type::INVALID;
            token.options.string_value = nullptr;
        }
        // Whitespaces are currently skipped (This is handled by parser)
        else if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            index += 1;
            continue;
        }
        // Integer/Float start
        else if (char_is_digit(c))
        {
            token.type = Token_Type::LITERAL_INTEGER;

            bool is_hexadecimal = false;
            if (c == '0' && index + 1 < text.size && text[index + 1] == 'x') {
                is_hexadecimal = true;
                index += 2;
            }

            // Buffer is used for integer and float parsing
            const int BUFFER_SIZE = 128;
            char buffer[BUFFER_SIZE];

            int used_buffer = 0;
            auto put_char = [&](char c) {
                if (used_buffer >= BUFFER_SIZE) {
                    return;
                }
                buffer[used_buffer] = c;
                used_buffer += 1;
            };

            // Pre comma digits
            while (index < text.size)
            {
                char c = text[index];

                if (c == '_') { // _ is allowed as spacing in number literals
                    index += 1;
                    continue;
                }
                else if (is_hexadecimal) 
                {
                    if (char_get_hexadecimal_value(c) == -1) {
                        break;
                    }
                }
                else if (!char_is_digit(c)) {
                    break;
                }

                put_char(c);
                index += 1;
            }

            // Post comma digits
            if (string_test_char(text, index, '.') && !is_hexadecimal)
            {
                token.type = Token_Type::LITERAL_FLOAT;
                put_char('.');
                index += 1;
                while (index < text.size)
                {
                    char c = text[index];
                    if (!char_is_digit(c) && c != '_') {
                        break;
                    }
                    put_char(c);
                    index += 1;
                }
            }

            // After a valid number no alphanumerical thing can, follow, e.g. "323abc" should not parse as a number...
            while (index < text.size && char_is_letter(text[index])) {
                token.type = Token_Type::INVALID;
                index += 1;
            }

            // Check if buffer was large enough
            put_char('\0'); // Insert null-terminator
            if (used_buffer >= BUFFER_SIZE) {
                token.type = Token_Type::INVALID;
            }

            // Parse string to value
            if (token.type == Token_Type::LITERAL_INTEGER) {
                token.options.integer_value = strtoll(buffer, nullptr, is_hexadecimal ? 16 : 10);
            }
            else if (token.type == Token_Type::LITERAL_FLOAT) {
                token.options.float_value = strtod(buffer, nullptr);
            }
        }
        // Comment start
        else if (c == '/' && index + 1 < text.size && text[index + 1] == '/')
        {
            token.type = Token_Type::COMMENT;
            index = text.size;
        }
        // Operator start
        else
        {
            // Find longest matching operator (Current max. operator length = 3)
            int found_length = 1;
            token.type = Token_Type::INVALID;
            for (int length = 1; length <= 3; length += 1) 
            {
                if (index + length > text.size) break;
                String str = string_create_substring_static(&text, index, index + length);
                Token_Type* matching_op = hashtable_find_element(&tokenizer.operator_table, str);
                if (matching_op != nullptr) {
                    token.type = *matching_op;
                    found_length = length;
                }
            }
            index += found_length;
        }

        token.end = index;
        tokens->push_back(token);
    }

    if (remove_comments && tokens->size > 0 && tokens->last().type == Token_Type::COMMENT) {
        tokens->size -= 1;
    }
}

void tokenizer_parse_string_literal(String literal, String* append_to)
{
    // Note: We expect the literal to contain " and ", so we start at 1
	for (int i = 1; i < literal.size - 1; i++)
	{
        char c = literal[i];
		if (c == '\\')
		{
            char next = literal[i + 1];
			switch (next)
			{
			case 'n':
				string_append_character(append_to, '\n');
				break;
			case 'r':
				string_append_character(append_to, '\r');
				break;
			case 't':
				string_append_character(append_to, '\t');
				break;
			case '\\':
				string_append_character(append_to, '\\');
				break;
			case '\'':
				string_append_character(append_to, '\'');
				break;
			case '\"':
				string_append_character(append_to, '\"');
				break;
			case '\n':
				break;
			default:
                panic("Shouldnt happen if we got this from previous tokenization");
				break;
			}

            i += 1; // Character was already used
            continue;
		}

		string_append_character(append_to, c);
	}
}

DynArray<Token> tokenize_partial_code(
    Source_Code* code, Text_Index index, Arena* arena, int& token_index, bool handle_line_continuations, bool remove_comments)
{
    int nearby_bundle_index = 0;
	DynArray<Token> tokens = DynArray<Token>::create(arena);
	token_index = 0;
	if (index.line < 0 || index.line >= code->line_count) return tokens;

    // Handle line-continuations
    int line_start_index = index.line;
    int line_end_index = index.line;
    if (handle_line_continuations)
    {
        auto helper_lines_can_connect = [&](int prev_line_index, int curr_line_index) -> bool
        {
            auto checkpoint = arena->make_checkpoint();
            SCOPE_EXIT(checkpoint.rewind());
            DynArray<Token> prev_tokens = DynArray<Token>::create(arena);
            DynArray<Token> curr_tokens = DynArray<Token>::create(arena);

            Source_Line* prev_line = source_code_get_line(code, prev_line_index, nearby_bundle_index);
            Source_Line* curr_line = source_code_get_line(code, curr_line_index, nearby_bundle_index);
            tokenizer_tokenize_single_line(prev_line->text, &prev_tokens, prev_line_index, true);
            tokenizer_tokenize_single_line(curr_line->text, &curr_tokens, curr_line_index, true);
            if (prev_tokens.size == 0 || curr_tokens.size == 0) { // Empty lines don't connect
                return false;
            }

            // Note: In parser indentation is also considered, but here we need to make approximations because we go backwards
            Continuation_Info prev_last_info  = token_type_get_continuation_info(prev_tokens[prev_tokens.size - 1].type);
            Continuation_Info curr_first_info = token_type_get_continuation_info(curr_tokens[0].type);
            if (prev_last_info.connects_to_next) return true;
            if (curr_first_info.connects_to_previous) return true;
            if (prev_last_info.is_parenthesis && token_type_get_class(prev_last_info.type) == Token_Class::LIST_START) return true;
            if (curr_first_info.is_parenthesis && token_type_get_class(curr_first_info.type) == Token_Class::LIST_END) return true;
            if (curr_first_info.is_parenthesis && token_type_get_class(curr_first_info.type) == Token_Class::LIST_START && curr_tokens.size == 1) return true;

            return false;
        };

        // Got backwards while lines connect
        while (line_start_index > 0 && helper_lines_can_connect(line_start_index - 1, line_start_index)) {
            line_start_index -= 1;
        }
        // Go forwards while lines connect
        while (line_end_index + 1 < code->line_count && helper_lines_can_connect(line_end_index, line_end_index + 1)) {
            line_end_index += 1;
        }
    }

    // Tokenize all relevant lines
    for (int i = line_start_index; i <= line_end_index; i += 1)
    {
        int line_token_start = tokens.size;
	    tokenizer_tokenize_single_line(source_code_get_line(code, i)->text, &tokens, index.line, remove_comments);

        if (i == index.line) 
        {
	        token_index = math_maximum(0, (int)tokens.size - 1);
	        for (int i = line_token_start; i < tokens.size; i++) {
	        	Token& token = tokens[i];
	        	if (token.start <= index.character) {
	        		token_index = i;
	        	}
	        	else {
	        		break;
	        	}
	        }
        }

    }
	return tokens;
}

// Returns start/end index (Inclusive), -1 if not found
ivec2 tokens_get_parenthesis_range(DynArray<Token> tokens, int start, Token_Type type, Arena* arena)
{
	auto checkpoint = arena->make_checkpoint();
	SCOPE_EXIT(checkpoint.rewind());

	Token_Class token_class = token_type_get_class(type);
	assert(token_class == Token_Class::LIST_START || token_class == Token_Class::LIST_END, "");
	if (token_class == Token_Class::LIST_END) {
		type = token_type_get_partner(type);
	}

	// Find parenthesis start
	while (start > 0 && start < tokens.size)
	{
		if (tokens[start].type == type) {
			break;
		}
		start -= 1;
	}
	if (start < 0) { // Exit if no start could be found
		return ivec2(-1, -1);
	}

	// Find end parenthesis
	DynArray<Token_Type> parenthesis_stack = DynArray<Token_Type>::create(arena);
	parenthesis_stack.push_back(type);
	int end = start + 1;
	while (end < tokens.size) 
	{
		Token& token = tokens[end];
		Token_Class token_class = token_type_get_class(token.type);
		if (token_class == Token_Class::LIST_START) {
			parenthesis_stack.push_back(token.type);
		}
		else if (token_type_get_partner(token.type) == parenthesis_stack.last()) {
			parenthesis_stack.size -= 1;
			if (parenthesis_stack.size == 0) {
				break;
			}
		}
		end += 1;
	}
	if (end >= tokens.size) {
		end = -1;
	}

	return ivec2(start, end);
}

Continuation_Info continuation_info_make(Token_Type type, bool connects_to_previous, bool connects_to_next, bool is_statement_start, bool is_parenthesis) 
{
	Continuation_Info info;
	info.type = type;
	info.connects_to_next = connects_to_next;
	info.connects_to_previous = connects_to_previous;
	info.is_statement_start = is_statement_start;
	info.is_parenthesis = is_parenthesis;
	return info;
}

Continuation_Info token_type_get_continuation_info(Token_Type type)
{
	switch (type)
	{
    case Token_Type::PLUS:
    case Token_Type::SLASH:
    case Token_Type::PERCENTAGE:
    case Token_Type::EQUALS:
    case Token_Type::NOT_EQUALS:
    case Token_Type::POINTER_EQUALS:
    case Token_Type::POINTER_NOT_EQUALS:
    case Token_Type::LESS_THAN: 
    case Token_Type::GREATER_THAN: 
    case Token_Type::LESS_EQUAL: 
    case Token_Type::GREATER_EQUAL: 
    case Token_Type::AND:
    case Token_Type::OR:
    case Token_Type::COLON:
    case Token_Type::POSTFIX_CALL_ARROW:
    case Token_Type::FUNCTION_ARROW:
		return continuation_info_make(type, true, true, false, false);

    case Token_Type::SEMI_COLON:
    case Token_Type::COMMA:
    case Token_Type::DOT:
    case Token_Type::SUBTYPE_ACCESS:
    case Token_Type::BASETYPE_ACCESS:
    case Token_Type::ASSIGN:
    case Token_Type::ASSIGN_ADD:
    case Token_Type::ASSIGN_SUB:
    case Token_Type::ASSIGN_MULT:
    case Token_Type::ASSIGN_DIV:
    case Token_Type::ASSIGN_MODULO:
    case Token_Type::TILDE:
    case Token_Type::ASTERIX:  // Minus and Asterix would cause confusion if it connects to last line, as it is also a unop
    case Token_Type::MINUS:
		return continuation_info_make(type, false, true, false, false);

    case Token_Type::PARENTHESIS_OPEN:
    case Token_Type::BRACKET_OPEN:
    case Token_Type::CURLY_BRACE_OPEN:
		return continuation_info_make(type, false, false, false, true);
    case Token_Type::PARENTHESIS_CLOSED:
    case Token_Type::BRACKET_CLOSED:
    case Token_Type::CURLY_BRACE_CLOSED:
		return continuation_info_make(type, false, false, false, true);

    case Token_Type::FUNCTION_KEYWORD:
    case Token_Type::MODULE:
    case Token_Type::STRUCT:
    case Token_Type::UNION:
    case Token_Type::ENUM:
    case Token_Type::VAR:
    case Token_Type::GLOBAL_KEYWORD:
    case Token_Type::CONST_KEYWORD:
    case Token_Type::OPERATORS:
    case Token_Type::IMPORT:
    case Token_Type::AS:
    case Token_Type::EXTERN:
    case Token_Type::RETURN:
    case Token_Type::BREAK:
    case Token_Type::CONTINUE:
    case Token_Type::IF:
    case Token_Type::ELSE:
    case Token_Type::LOOP:
    case Token_Type::IN_KEYWORD:
    case Token_Type::DEFER:
    case Token_Type::DEFER_RESTORE:
		return continuation_info_make(type, false, false, true, false);
	}
	return continuation_info_make(type, false, false, false, false);
}


