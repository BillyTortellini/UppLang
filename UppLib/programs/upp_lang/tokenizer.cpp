#include "tokenizer.hpp"

#include "../../datastructures/hashtable.hpp"
#include "../../utility/character_info.hpp"

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
    case Token_Type::LITERAL_NULL: return "null";

    case Token_Type::_OPERATORS_START_: return "_OPERATORS_START_";
    case Token_Type::ADDITION: return "+";
    case Token_Type::SUBTRACTION: return "-";
    case Token_Type::DIVISON: return "*";
    case Token_Type::MULTIPLY: return "/";
    case Token_Type::MODULO: return "%";

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
    case Token_Type::DOT_CALL: return "->";
    case Token_Type::ADDRESS_OF: return "-*";
    case Token_Type::DEREFERENCE: return "-&";
    case Token_Type::OPTIONAL_DEREFERENCE: return "-?&";
    case Token_Type::DEFINE_COMPTIME: return "::";
    case Token_Type::DEFINE_INFER: return ":=";
    case Token_Type::ARROW: return "=>";
    case Token_Type::DOLLAR: return "$";
    case Token_Type::ASSIGN: return "=";
    case Token_Type::ASSIGN_ADD: return "+=";
    case Token_Type::ASSIGN_SUB: return "-=";
    case Token_Type::ASSIGN_MULT: return "*=";
    case Token_Type::ASSIGN_DIV: return "/=";
    case Token_Type::ASSIGN_MODULO: return "%=";
    case Token_Type::UNINITIALIZED: return "_";
    case Token_Type::CONCATENATE_LINES: return "\\";

    case Token_Type::_OPERATORS_END_: return "_OPERATORS_END_";

    case Token_Type::_KEYWORDS_START_: return "_KEYWORDS_START_";

    case Token_Type::FUNCTION_KEYWORD: return "fn";
    case Token_Type::FUNCTION_POINTER_KEYWORD: return "fn_ptr";
    case Token_Type::RETURN: return "return";
    case Token_Type::BREAK: return "break";
    case Token_Type::CONTINUE: return "continue";
    case Token_Type::IF: return "if";
    case Token_Type::ELSE: return "else";
    case Token_Type::LOOP: return "loop";
    case Token_Type::SWITCH: return "switch";
    case Token_Type::DEFAULT: return "default";
    case Token_Type::MODULE: return "module";
    case Token_Type::NEW: return "new";
    case Token_Type::STRUCT: return "struct";
    case Token_Type::UNION: return "union";
    case Token_Type::ENUM: return "enum";
    case Token_Type::DELETE_KEYWORD: return "delete";
    case Token_Type::DEFER: return "defer";
    case Token_Type::DEFER_RESTORE: return "defer_restore";
    case Token_Type::CAST: return "cast";
    case Token_Type::BAKE: return "#bake";
    case Token_Type::INSTANCIATE: return "#instanciate";
    case Token_Type::GET_OVERLOAD: return "#get_overload";
    case Token_Type::GET_OVERLOAD_POLY: return "#get_overload_poly";
    case Token_Type::EXPLICIT_BLOCK: return "#block";
    case Token_Type::IMPORT: return "import";
    case Token_Type::AS: return "as";
    case Token_Type::IN_KEYWORD: return "in";
    case Token_Type::EXTERN: return "extern";

    case Token_Type::ADD_BINOP: return "#add_binop";
    case Token_Type::ADD_UNOP: return "#add_unop";
    case Token_Type::ADD_CAST: return "#add_cast";
    case Token_Type::ADD_ARRAY_ACCESS: return "#add_array_access";
    case Token_Type::ADD_ITERATOR: return "#add_iterator";

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
        insert_keyword_token(Token_Type::LITERAL_NULL);
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


void tokenizer_tokenize_line(String text, DynArray<Token>* tokens, int line_index, bool remove_comments)
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
