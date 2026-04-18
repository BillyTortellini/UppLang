#include "parser.hpp"

#include "ast.hpp"
#include "syntax_editor.hpp"
#include "compilation_data.hpp"
#include "code_history.hpp"

namespace Parser
{
	void log_error_text_range(const char* msg, Text_Range range);
}

void print_tokens(DynArray<Token> tokens)
{
	printf("\n\n------------------------------\n");
	int indentation = 0;
	for (int i = 0; i < tokens.size; i++)
	{
		Token& token = tokens[i];
		printf("%s ", token_type_as_cstring(token.type));

		bool line_break = false;
		if (token.type == Token_Type::BLOCK_START) {
			indentation += 1;
			line_break = true;
		}
		else if (token.type == Token_Type::BLOCK_END) {
			indentation -= 1;
			line_break = true;;
		}
		else if (token.type == Token_Type::LINE_END) {
			line_break = true;
		}

		if (line_break) {
			printf("\n");
			for (int i = 0; i < indentation; i++) {
				printf("  ");
			}
		}
	}
	printf("\n");
}

struct List_Info
{
	int start;
	int last_seperator;
	Token_Type type;
	bool is_block_list;
};

List_Info list_info_make(int start, int seperator, Token_Type type, bool is_block_list)
{
	List_Info info;
	info.start = start;
	info.last_seperator = seperator;
	info.type = type;
	info.is_block_list = is_block_list;
	return info;
}

// Note: this logs errors in parser...
Text_Range token_get_text_range(Token& token) {
	return text_range_make(text_index_make(token.line, token.start), text_index_make(token.line, token.end));
}

struct Line_Indent_Info
{
	int indentation;
	bool is_empty; // Line is also counted as empty if it only starts with a backslash
};

Line_Indent_Info source_line_get_indent_info(Source_Line* line) 
{
	Line_Indent_Info result;
	result.indentation = 0;
	result.is_empty = true;

	String& text = line->text;
	int index = 0;
	while (index < text.size) 
	{
		if (text[index] == ' ') {
			index += 1;
			continue;
		}
		else if (string_test_char(text, index, '/') && string_test_char(text, index + 1, '/')) {
			break;
		}
		else {
			result.is_empty = false;
			break;
		}
	}
	result.indentation = index/ 4;
	return result;
}

DynArray<Token> tokenize_source_code_and_build_hierarchy(Source_Code* code, Arena* arena, Identifier_Pool* id_pool)
{
	DynArray<Token> tokens = DynArray<Token>::create(arena);
	tokens.push_back(token_make(Token_Type::BLOCK_START, 0, 0, 0));

	int nearby_bundle_index = 0;
	int indentation = 0;
	int prev_indentation = 0;
	int min_continuation_indentation = -1;
	Text_Index last_text_pos = text_index_make(0, 0);
	Token_Type last_line_end_token_type = Token_Type::INVALID;
	for (int line_index = 0; line_index < code->line_count; line_index += 1)
	{
		Source_Line* line = source_code_get_line(code, line_index, nearby_bundle_index);
		Line_Indent_Info indent_info = source_line_get_indent_info(line);
		if (indent_info.is_empty) {
			last_line_end_token_type = Token_Type::INVALID;
			min_continuation_indentation = -1;
			continue; // Skip empty lines
		}
		SCOPE_EXIT(prev_indentation = indent_info.indentation);

		// Insert Block-Start/End tokens on indentation change
		int indentation_before_line = indentation;
		{
			// Remove previous line-end tokens if the indentation changes
			if (indentation != indent_info.indentation) {
				while (tokens.size > 0 && tokens.last().type == Token_Type::LINE_END) {
					tokens.size -= 1;
				}
			}
			bool added_block_end = false;
			while (indentation != indent_info.indentation)
			{
				if (indent_info.indentation > indentation) {
					tokens.push_back(token_make(Token_Type::BLOCK_START, last_text_pos.character, last_text_pos.character, last_text_pos.line));
					indentation += 1;
				}
				else {
					tokens.push_back(token_make(Token_Type::BLOCK_END, last_text_pos.character, last_text_pos.character, last_text_pos.line));
					indentation -= 1;
					added_block_end = true;
				}
			}
			if (added_block_end) {
				tokens.push_back(token_make(Token_Type::LINE_END, last_text_pos.character, last_text_pos.character, last_text_pos.line));
			}
		}

		// Tokenize line
		int token_count_before = tokens.size;
		{
			tokenizer_tokenize_single_line(line->text, &tokens, line_index, true);

			// Add identifiers and string-literals to identifier-pool
			for (int i = token_count_before; i < tokens.size; i++)
			{
				Token& token = tokens[i];
				if (token.type == Token_Type::IDENTIFIER) {
					token.options.string_value = identifier_pool_add(id_pool, string_create_substring_static(&line->text, token.start, token.end));
				}
				else if (token.type == Token_Type::LITERAL_STRING)
				{
					auto checkpoint = arena->make_checkpoint();
					SCOPE_EXIT(checkpoint.rewind());
					String string_buffer = string_create(arena, 256);
					auto substring = string_create_substring_static(&line->text, token.start, token.end);
					tokenizer_parse_string_literal(substring, &string_buffer);
					token.options.string_value = identifier_pool_add(id_pool, string_buffer);
				}
			}
		}

		if (tokens.size == token_count_before) {
			last_line_end_token_type = Token_Type::INVALID;
			min_continuation_indentation = -1; // empty lines reset continuations?
			indentation = indentation_before_line;
			continue;
		}

		// Check if lines should be joined
		bool lines_should_join = false;
		{
			Continuation_Info curr_line_first_info = token_type_get_continuation_info(tokens[token_count_before].type);
			Continuation_Info prev_line_last_info = token_type_get_continuation_info(last_line_end_token_type);

			// Connect lines if prev-line ends in continuation
			if (!lines_should_join && prev_line_last_info.connects_to_next && !curr_line_first_info.is_statement_start) 
			{
				if (min_continuation_indentation == -1) {
					if (indentation > prev_indentation) {
						lines_should_join = true;
						min_continuation_indentation = prev_indentation + 1;
					}
				}
				else if (indentation >= min_continuation_indentation) {
					lines_should_join = true;
				}
			}

			// Connect lines if current-line starts with a connection to last
			if (!lines_should_join && curr_line_first_info.connects_to_previous && !prev_line_last_info.is_statement_start)
			{
				if (min_continuation_indentation == -1) {
					if (indentation > prev_indentation) {
						lines_should_join = true;
						min_continuation_indentation = prev_indentation + 1;
					}
				}
				else if (indentation >= min_continuation_indentation) {
					lines_should_join = true;
				}
			}

			// Connect lines if prev-line ends with open parenthesis
			if (!lines_should_join && prev_line_last_info.is_parenthesis && 
				token_type_get_class(prev_line_last_info.type) == Token_Class::LIST_START && // Parenthesis on prev-line must be start
				!(curr_line_first_info.is_statement_start && prev_line_last_info.type != Token_Type::CURLY_BRACE_OPEN) && // Next must not be statement if ( or [
				indentation > prev_indentation) // Parenthesis always require one indentation jump
			{
				if (min_continuation_indentation == -1) {
					lines_should_join = true;
					min_continuation_indentation = prev_indentation + 1;
				}
				else if (indentation >= min_continuation_indentation) {
					lines_should_join = true;
				}
			}

			// Connect lines if current-line is closing parenthesis
			// or if it's an open parenthesis that is the only token on the line
			if (!lines_should_join && curr_line_first_info.is_parenthesis && (
					token_type_get_class(curr_line_first_info.type) == Token_Class::LIST_END ||
					(token_type_get_class(curr_line_first_info.type) == Token_Class::LIST_START && token_count_before + 1 == tokens.size)
				)) // Parenthesis on curr-line must be end
			{
				if (min_continuation_indentation == -1) {
					if (indentation >= prev_indentation) { // Closing parenthesis can be on same indentation as prev-line
						lines_should_join = true;
					}
				}
				else if (indentation >= min_continuation_indentation) {
					lines_should_join = true;
				}
				else if (indentation == min_continuation_indentation - 1) { // Stop continuation is we arrive back at starting indentation
					lines_should_join = true;
					min_continuation_indentation = -1;
				}
			}
		}
		if (!lines_should_join) {
			min_continuation_indentation = -1;
		}

		// Join lines if necessary (Remove line-seperator tokens)
		if (lines_should_join)
		{
			indentation = indentation_before_line;
			int copy_start_index = token_count_before;
			while (copy_start_index > 0)
			{
				Token& prev = tokens[copy_start_index - 1];
				if (prev.type == Token_Type::BLOCK_START || prev.type == Token_Type::BLOCK_END || prev.type == Token_Type::LINE_END) {
					copy_start_index -= 1;
				}
				else {
					break;
				}
			}
			if (copy_start_index != token_count_before)
			{
				for (int i = 0; i < tokens.size - token_count_before; i++) {
					tokens[copy_start_index + i] = tokens[token_count_before + i];
				}
				tokens.size = tokens.size - (token_count_before - copy_start_index);
			}
		}

		// Store last text-index for block and line_end tokens
		if (tokens.size > 0) {
			Token& last_token = tokens.last();
			last_text_pos = text_index_make(last_token.line, last_token.end);
			last_line_end_token_type = last_token.type;
		}

		// Push line-end token
		tokens.push_back(token_make(Token_Type::LINE_END, last_text_pos.character, last_text_pos.character, last_text_pos.line));
	}

	// Add missing block-end tokens
	while (tokens.size > 0 && tokens.last().type == Token_Type::LINE_END) {
		tokens.size -= 1;
	}
	for (int i = 0; i < indentation + 1; i += 1) {
		tokens.push_back(token_make(Token_Type::BLOCK_END, last_text_pos.character, last_text_pos.character, last_text_pos.line));
	}
	// Push invalid token so access to last block-end + 1 is possible (Not sure if necessary anymore) 
	tokens.push_back(token_make(Token_Type::INVALID, last_text_pos.character, last_text_pos.character, last_text_pos.line));

	return tokens;
}



namespace Parser
{
	// Types
	using namespace AST;

	struct Binop_Link
	{
		Binop binop;
		int token_index;
		Expression* expr;
	};

	struct Parser_Checkpoint
	{
		Arena_Checkpoint permanent_arena_checkpoint;
		Arena_Checkpoint temporary_arena_checkpoint;
		int pos;
		int error_count;
	};

	struct Parser
	{
		Arena* permanent_arena;
		Arena* temporary_arena; // Used for list-parsing
		DynArray<Token> tokens;
		DynArray<Error_Message> errors;
		Compilation_Unit* unit;
		Predefined_IDs* predefined_ids;
		Token error_token;
		int pos; // token-index
	};

	// Globals (Thread-Local so i don't have to rewrite everything, but this doesn't work well with fiber-pool!)
	static thread_local Parser parser;

	Parser_Checkpoint parser_checkpoint_make() {
		Parser_Checkpoint checkpoint;
		checkpoint.error_count = parser.errors.size;
		checkpoint.permanent_arena_checkpoint = parser.permanent_arena->make_checkpoint();
		checkpoint.temporary_arena_checkpoint = parser.temporary_arena->make_checkpoint();
		checkpoint.pos = parser.pos;
		return checkpoint;
	}

	// Parser Functions
	void parser_rollback(Parser_Checkpoint checkpoint)
	{
		parser.errors.rollback_to_size(checkpoint.error_count);
		parser.pos = checkpoint.pos;
		checkpoint.permanent_arena_checkpoint.rewind();
		checkpoint.temporary_arena_checkpoint.rewind();
	}

	template<typename T>
	T* allocate_base(Node* parent, Node_Type type)
	{
		auto result = parser.permanent_arena->allocate<T>();
		memory_zero(result);

		Node* base = &result->base;
		base->parent = parent;
		base->type = type;
		auto& token = parser.tokens[parser.pos];
		base->range.start = text_index_make(token.line, token.start);
		base->range.end = base->range.start;

		return result;
	}



	// Error reporting
	// Start is inclusive token index, end is exclusive token index
	void log_error_text_range(const char* msg, Text_Range range)
	{
		Error_Message err;
		err.msg = msg;
		err.range = range;
		parser.errors.push_back(err);
	}

	void log_error(const char* msg, int start, int end)
	{
		auto& tokens = parser.tokens;
		assert(start >= 0 && start < tokens.size, "I think this is a condition that should always be the case");
		assert(end >= start, "");

		Token& start_token = parser.tokens[start];
		Text_Index text_start = text_index_make(start_token.line, start_token.start);

		end = math_minimum(end, (int)tokens.size - 1);
		Token end_token = tokens[end - 1]; // End is exclusive
		Text_Index text_end = text_index_make(end_token.line, end_token.end);
		if (end <= start) {
			text_start = text_end;
		}

		log_error_text_range(msg, text_range_make(text_start, text_end));
	}

	void log_error_to_pos(const char* msg, int end_token) {
		log_error(msg, parser.pos, end_token);
	}

	void log_error_range_offset(const char* msg, int token_count) {
		log_error_to_pos(msg, parser.pos + token_count);
	}



	// Token functions
	Token* get_token_by_index(int index)
	{
		if (index < 0 || index >= parser.tokens.size) {
			return &parser.error_token;
		}
		return &parser.tokens[index];
	}

	Token* get_token(int offset = 0) {
		return get_token_by_index(parser.pos + offset);
	}

	void advance_token() {
		parser.pos += 1;
	}

	bool test_token(Token_Type type, int offset = 0) {
		return get_token(offset)->type == type;
	}

	bool test_token(Token_Type t0, Token_Type t1, int offset = 0) {
		return
			test_token(t0, offset) &&
			test_token(t1, offset + 1);
	}

	bool test_token(Token_Type t0, Token_Type t1, Token_Type t2, int offset = 0) {
		return
			test_token(t0, offset) &&
			test_token(t1, offset + 1) &&
			test_token(t2, offset + 2);
	}

	bool test_token(Token_Type t0, Token_Type t1, Token_Type t2, Token_Type t3, int offset = 0) {
		return
			test_token(t0, offset) &&
			test_token(t1, offset + 1) &&
			test_token(t2, offset + 2) &&
			test_token(t3, offset + 3);
	}

	bool on_follow_block() {
		return test_token(Token_Type::CURLY_BRACE_OPEN) || test_token(Token_Type::BLOCK_START) || test_token(Token_Type::SCOPE);
	}


	void node_calculate_bounding_range(AST::Node* node)
	{
		auto& bounding_range = node->bounding_range;
		bounding_range = node->range;
		int index = 0;
		auto child = AST::base_get_child(node, index);
		while (child != 0)
		{
			auto child_range = child->bounding_range;
			if (!text_index_in_order(bounding_range.start, child_range.start)) {
				bounding_range.start = child_range.start;
			}
			if (!text_index_in_order(child_range.end, bounding_range.end)) {
				bounding_range.end = child_range.end;
			}

			index += 1;
			child = AST::base_get_child(node, index);
		}
	}

	void node_finalize_range(AST::Node* node)
	{
		// NOTE: This function did more in the past with hierarchical text-structure
		// Now it does 3 things: 
		//      * Sanity checks
		//      * Sets the end of the node
		//      * Calcualtes bounding-ranges (for editor) (Could also be done once at the end of parsing, maybe I do that?)
		auto& range = node->range;

		// Set end of node
		Token& token = parser.tokens[parser.pos - 1];
		range.end = text_index_make(token.line, token.end);
		if (!text_index_in_order(range.start, range.end)) {
			range.end = range.start;
		}

		// Calculate bounding range
		node_calculate_bounding_range(node);

		// Sanity-Check that start/end is in order
		bool in_order = text_index_in_order(range.start, range.end);
		assert(in_order, "Ranges must be in order");
	}

	void skip_until_next_follow_block()
	{
		if (on_follow_block()) {
			return;
		}

		auto checkpoint = parser.temporary_arena->make_checkpoint();
		SCOPE_EXIT(checkpoint.rewind());
		DynArray<Token_Type> parenthesis_stack = DynArray<Token_Type>::create(parser.temporary_arena);
		
		auto& pos = parser.pos;
		int start = pos;
		bool found = false;
		while (true)
		{
			// Exit conditions
			if (on_follow_block()) {
				found = true;
				break;
			}
			if (test_token(Token_Type::BLOCK_END) || test_token(Token_Type::LINE_END) || test_token(Token_Type::SCOPE) ||
				(parenthesis_stack.size == 0 && test_token(Token_Type::CURLY_BRACE_CLOSED)))
			{
				found = false;
				break;
			}

			// Parenthesis handling
			Token_Type type = get_token()->type;
			if (parenthesis_stack.size != 0 && token_type_get_partner(type) == parenthesis_stack.last()) {
				parenthesis_stack.size -= 1;
				pos += 1;
				continue;
			}

			auto token_class = token_type_get_class(type);
			if (token_class == Token_Class::LIST_START) {
				parenthesis_stack.push_back(type);
			}
			pos += 1;
		}

		if (found) {
			log_error("Could not parse tokens", start, pos);
		}
		else {
			pos = start;
		}
	}

	// Parsing Helpers
	struct List_Iter
	{
		// Different list types: block_start, (), {}, []
		Token_Type type;
		bool is_valid;
		int last_item_start;
		bool force_semicolon_as_seperator;

		static List_Iter create(bool force_semicolon_as_seperator = false)
		{
			List_Iter iter;
			iter.is_valid = false;
			iter.type = get_token()->type;
			iter.last_item_start = parser.pos;
			iter.force_semicolon_as_seperator = force_semicolon_as_seperator;

			switch (iter.type)
			{
			case Token_Type::PARENTHESIS_OPEN:
			case Token_Type::CURLY_BRACE_OPEN:
			case Token_Type::BRACKET_OPEN:
			case Token_Type::BLOCK_START:
			{
				advance_token();
				iter.is_valid = true;
				break;
			}
			case Token_Type::SCOPE:
			{
				iter.type = Token_Type::BLOCK_START;
				if (test_token(Token_Type::BLOCK_START, 1)) {
					advance_token();
					advance_token();
					iter.is_valid = true;
				}
				else if (test_token(Token_Type::BLOCK_START, Token_Type::IDENTIFIER, 1)) {
					advance_token();
					advance_token();
					advance_token();
					iter.is_valid = true;
				}
				else {
					iter.is_valid = false;
				}
				break;
			}
			default: {
				iter.is_valid = false;
				break;
			}
			}

			iter.last_item_start = parser.pos;
			return iter;
		}

		bool on_end_token()
		{
			if (!is_valid) {
				return true;
			}

			Token* token = get_token();
			if (type == Token_Type::BLOCK_START) {
				return token->type == Token_Type::BLOCK_END;
			}

			return
				token->type == token_type_get_partner(type) ||
				token->type == Token_Type::LINE_END ||
				token->type == Token_Type::BLOCK_END ||
				token->type == Token_Type::BLOCK_START;
		}

		bool on_seperator()
		{
			if (!is_valid) {
				return false;
			}

			Token_Type current_type = get_token()->type;
			switch (type)
			{
			case Token_Type::BRACKET_OPEN:
			case Token_Type::PARENTHESIS_OPEN: return current_type == (force_semicolon_as_seperator ? Token_Type::SEMI_COLON : Token_Type::COMMA);
			case Token_Type::CURLY_BRACE_OPEN: return current_type == Token_Type::SEMI_COLON;
			case Token_Type::BLOCK_START:      return current_type == Token_Type::SEMI_COLON || current_type == Token_Type::LINE_END;
			default: panic("");
			}
			return false;
		}

		// This logs an error if we aren't on a seperator
		void goto_next()
		{
			if (!is_valid) {
				return;
			}
			SCOPE_EXIT(last_item_start = parser.pos);

			if (on_seperator())
			{
				if (last_item_start == parser.pos) {
					log_error("Expected item", last_item_start, last_item_start);
				}
				advance_token();
				return;
			}
			else if (on_end_token()) {
				if (last_item_start == parser.pos) {
					log_error("Expected item", last_item_start, last_item_start);
				}
				return;
			}

			// Otherwise find continuation and log error (Either end of the parenthesis or next seperator)
			int& pos = parser.pos;
			int start_pos = pos;
			// Special case handling
			if (type == Token_Type::BLOCK_START && get_token()->type == Token_Type::BLOCK_START)
			{
				// If we have parsed an item and landed on block start, try to parse it first before skipping the block
				if (pos != last_item_start) {
					return;
				}

				int block_count = 1;
				pos += 1;
				while (block_count != 0)
				{
					Token_Type token_type = get_token()->type;
					if (token_type == Token_Type::BLOCK_START) {
						block_count += 1;
					}
					else if (token_type == Token_Type::BLOCK_END) {
						block_count -= 1;
					}
					pos += 1;
				}
			}
			else
			{
				auto checkpoint = parser.temporary_arena->make_checkpoint();
				SCOPE_EXIT(checkpoint.rewind());
				DynArray<Token_Type> parenthesis_stack = DynArray<Token_Type>::create(parser.temporary_arena);
				parenthesis_stack.push_back(type);

				while (true)
				{
					Token_Type token_type = get_token()->type;
					if (token_type == Token_Type::BLOCK_START || token_type == Token_Type::BLOCK_END || token_type == Token_Type::LINE_END) {
						break;
					}

					if (on_seperator()) {
						break;
					}
					if (token_type == token_type_get_partner(type))
					{
						parenthesis_stack.size -= 1;
						if (parenthesis_stack.size == 0) {
							break;
						}
					}
					else if (token_type_get_class(token_type) == Token_Class::LIST_START) {
						parenthesis_stack.push_back(token_type);
					}

					pos += 1;
				}
			}

			log_error("Could not parse tokens", start_pos, pos);

			if (on_seperator()) {
				advance_token();
				return;
			}
		}

		void finish()
		{
			int& pos = parser.pos;
			int start_pos = pos;
			while (!on_end_token()) {
				pos += 1;
			}
			if (start_pos != pos) {
				log_error("Could not parse tokens", start_pos, pos);
			}

			// Don't advance over block-ends/line-ends
			if (get_token()->type == token_type_get_partner(type)) {
				advance_token();
			}
			else {
				log_error("Expected list-end token (Parenthesis)", pos, pos);
			}
		}
	};

	typedef bool (*stopper_fn)(Token_Type);
	typedef Node* (*list_item_parse_fn)(Node* parent);

	void parse_list_items(Node* parent, list_item_parse_fn parse_fn, DynArray<Node*>& append_to)
	{
		if (!(get_token()->type == Token_Type::SCOPE || token_type_get_class(get_token()->type) == Token_Class::LIST_START)) {
			return;
		}

		List_Iter iter = List_Iter::create();
		while (!iter.on_end_token())
		{
			Node* parsed_item = parse_fn(parent);
			if (parsed_item != nullptr) {
				append_to.push_back(parsed_item);
			}
			iter.goto_next();
		}
		iter.finish();
	}

	template<typename T>
	Array<T*> parse_list_items_as_array(Node* parent, list_item_parse_fn parse_fn)
	{
		auto checkpoint = parser.temporary_arena->make_checkpoint();
		SCOPE_EXIT(checkpoint.rewind());
		DynArray<AST::Node*> values = DynArray<AST::Node*>::create(parser.temporary_arena);
		parse_list_items(parent, parse_fn, values);

		Array<T*> result = parser.permanent_arena->allocate_array<T*>(values.size);
		for (int i = 0; i < values.size; i++) {
			result[i] = downcast<T>(values[i]);
		}
		return result;
	}

	// Skips tokens until type, or some other stopper was encountered
	void skip_tokens_until_token_type_found(Token_Type target_type)
	{
		auto helper_on_goal = [&]() {
		};
		auto checkpoint = parser.temporary_arena->make_checkpoint();
		SCOPE_EXIT(checkpoint.rewind());
		DynArray<Token_Type> parenthesis_stack = DynArray<Token_Type>::create(parser.temporary_arena);

		int index = parser.pos;
		while (index < parser.tokens.size)
		{
			Token_Type token_type = parser.tokens[index].type;
			if (token_type == Token_Type::COMMA ||
				token_type == Token_Type::SEMI_COLON ||
				token_type == Token_Type::BLOCK_START || // We don't skip over blocks I think
				token_type == Token_Type::BLOCK_END ||  // Again, don't skip over blocks?
				token_type == Token_Type::LINE_END ||
				token_type == Token_Type::CURLY_BRACE_OPEN) 
			{
				return;
			}

			// Parenthesis handling
			if (parenthesis_stack.size != 0 && token_type_get_partner(token_type) == parenthesis_stack.last()) {
				parenthesis_stack.size -= 1;
				index += 1;
				continue;
			}

			if (token_type == target_type && parenthesis_stack.size == 0) 
			{
				if (parser.pos != index) {
					log_error_to_pos("Could not parse tokens, expected specific token", index);
				}
				parser.pos = index;
				return;
			}

			auto token_class = token_type_get_class(token_type);
			if (token_class == Token_Class::LIST_START) {
				parenthesis_stack.push_back(token_type);
			}

			index += 1;
		}
	}



	// MACROS
#define CHECKPOINT_SETUP \
        if (parser.pos >= parser.tokens.size) {return nullptr;}\
        auto checkpoint = parser_checkpoint_make(); \
        bool _error_exit = false;\
        SCOPE_EXIT(if (_error_exit) parser_rollback(checkpoint);); 
#define CHECKPOINT_EXIT {_error_exit = true; return nullptr;}
#define PARSE_SUCCESS(val) { \
        node_finalize_range(&val->base); \
        return val; \
    }



	// Prototypes
	Optional<String*> parse_block_label_or_use_related_node_id(AST::Node* related_node);
	Code_Block* parse_code_block(Node* parent, AST::Node* related_node);
	Path_Lookup* parse_path_lookup(Node* parent);
	Expression* parse_expression(Node* parent);
	Expression* parse_expression_or_error_expr(Node* parent);
	Expression* parse_single_expression(Node* parent);
	Expression* parse_single_expression_or_error(Node* parent);
	Argument* parse_argument(Node* parent);
	Subtype_Initializer* parse_subtype_initializer(Node* parent);
	Parameter* parse_parameter(Node* parent);
	Signature* parse_signature(Node* parent, bool allow_return_type);

	Expression* create_error_expression(Node* parent)
	{
		Expression* expr = allocate_base<AST::Expression>(parent, AST::Node_Type::EXPRESSION);
		expr->type = Expression_Type::ERROR_EXPR;
		node_finalize_range(upcast(expr));
		return expr;
	}

	Statement* create_error_statement(Node* parent)
	{
		Statement* error_statement = allocate_base<AST::Statement>(parent, AST::Node_Type::STATEMENT);
		error_statement->type = Statement_Type::EXPRESSION_STATEMENT;
		error_statement->options.expression = create_error_expression(upcast(error_statement));
		node_finalize_range(upcast(error_statement));
		return error_statement;
	}

	// Returns an error-expression if uninitialized
	Node* parse_argument_or_subtype_initializer_or_uninitialzed(AST::Node* parent)
	{
		if (test_token(Token_Type::UNINITIALIZED))
		{
			auto error_expr = allocate_base<AST::Expression>(parent, AST::Node_Type::EXPRESSION);
			error_expr->type = Expression_Type::ERROR_EXPR;
			advance_token();
			node_finalize_range(upcast(error_expr));
			return upcast(error_expr);
		}

		Node* result = upcast(parse_subtype_initializer(parent));
		if (result != nullptr) return result;

		return upcast(parse_argument(parent));
	}

	AST::Node* parse_overload_argument(AST::Node* parent)
	{
		if (!test_token(Token_Type::IDENTIFIER)) {
			return nullptr;
		}

		auto result = allocate_base<AST::Get_Overload_Argument>(parent, AST::Node_Type::GET_OVERLOAD_ARGUMENT);
		result->id = get_token()->options.string_value;
		result->type_expr = optional_make_failure<AST::Expression*>();
		advance_token();
		if (test_token(Token_Type::ASSIGN)) {
			advance_token();
			result->type_expr = optional_make_success(parse_expression_or_error_expr(upcast(result)));
		}

		node_finalize_range(upcast(result));
		return upcast(result);
	}

	Path_Lookup* parse_path_lookup(Node* parent)
	{
		auto& ids = *parser.predefined_ids;

		if (!test_token(Token_Type::IDENTIFIER) && !test_token(Token_Type::TILDE)) {
			return nullptr;
		}
		CHECKPOINT_SETUP;

		Path_Lookup* path = allocate_base<Path_Lookup>(parent, Node_Type::PATH_LOOKUP);
		path->is_dot_call_lookup = false;

		auto arena_checkpoint = parser.temporary_arena->make_checkpoint();
		DynArray<Symbol_Node*> parts = DynArray<Symbol_Node*>::create(parser.temporary_arena);
		path->parts = array_create_empty<Symbol_Node*>();
		SCOPE_EXIT(
			path->parts = parser.permanent_arena->allocate_array<Symbol_Node*>(parts.size); 
			memory_copy(path->parts.data, parts.buffer.data, parts.size * sizeof(Symbol_Node*));
			arena_checkpoint.rewind();
		);

		if (test_token(Token_Type::TILDE)) {
			Symbol_Node* lookup = allocate_base<Symbol_Node>(upcast(path), Node_Type::SYMBOL_NODE);
			lookup->name = ids.root_module;
			lookup->is_root_lookup = true;
			lookup->is_definition = false;
			advance_token();
			node_finalize_range(upcast(lookup));
			parts.push_back(lookup);
		}

		while (test_token(Token_Type::IDENTIFIER))
		{
			Symbol_Node* lookup = allocate_base<Symbol_Node>(upcast(path), Node_Type::SYMBOL_NODE);
			lookup->name = get_token()->options.string_value;
			lookup->is_root_lookup = false;
			lookup->is_definition = false;
			advance_token();
			node_finalize_range(upcast(lookup));
			parts.push_back(lookup);

			if (test_token(Token_Type::TILDE)) 
			{
				advance_token();
				if (!test_token(Token_Type::IDENTIFIER)) {
					log_error("Expected identifier", parser.pos - 1, parser.pos); // Put error on the ~
					break;
				}
			}
			else {
				break;
			}
		}

		PARSE_SUCCESS(path);
	}

	Path_Lookup* parse_path_lookup_or_error(Node* parent)
	{
		auto& ids = parser.predefined_ids;

		if (!test_token(Token_Type::IDENTIFIER) && !test_token(Token_Type::TILDE))
		{
			Path_Lookup* path = allocate_base<Path_Lookup>(parent, Node_Type::PATH_LOOKUP);
			path->parts = parser.permanent_arena->allocate_array<Symbol_Node*>(1);
			path->is_dot_call_lookup = false;
			Symbol_Node* lookup = allocate_base<Symbol_Node>(upcast(path), Node_Type::SYMBOL_NODE);
			lookup->is_root_lookup = false;
			lookup->is_definition = false;
			path->parts[0] = lookup;
			lookup->name = ids->empty_string;
			node_finalize_range(upcast(lookup));
			node_finalize_range(upcast(path));
			return path;
		}
		return parse_path_lookup(parent);
	}

	// Always returns node, even if no arguments were found...
	// So one should always test for ( or { before calling this...
	Call_Node* parse_call_node(Node* parent, Argument* postfix_call_argument = nullptr)
	{
		auto call_node = allocate_base<Call_Node>(parent, Node_Type::CALL_NODE);
		call_node->arguments = array_create_empty<Argument*>();
		call_node->subtype_initializers = array_create_empty<Subtype_Initializer*>();
		call_node->uninitialized_tokens = array_create_empty<Expression*>();

		if (!test_token(Token_Type::PARENTHESIS_OPEN)) {
			log_error_range_offset("Expected parenthesis for parameters", 0);
			PARSE_SUCCESS(call_node);
		}

		auto checkpoint = parser.temporary_arena->make_checkpoint();
		DynArray<Node*> children = DynArray<Node*>::create(parser.temporary_arena);
		SCOPE_EXIT(checkpoint.rewind());
		if (postfix_call_argument != nullptr) {
			children.push_back(upcast(postfix_call_argument));
		}

		parse_list_items(
			upcast(call_node), parse_argument_or_subtype_initializer_or_uninitialzed, children
		);

		int argument_count = 0;
		int subtype_count = 0;
		int uninitialized_count = 0;
		for (int i = 0; i < children.size; i++) 
		{
			Node* child = children[i];
			if (child->type == Node_Type::ARGUMENT) {
				argument_count += 1;
			}
			else if (child->type == Node_Type::SUBTYPE_INITIALIZER) {
				subtype_count += 1;
			}
			else if (child->type == Node_Type::EXPRESSION) {
				uninitialized_count += 1;
			}
			else {
				panic("");
			}
		}

		call_node->arguments = parser.permanent_arena->allocate_array<Argument*>(argument_count);
		call_node->subtype_initializers = parser.permanent_arena->allocate_array<Subtype_Initializer*>(subtype_count);
		call_node->uninitialized_tokens = parser.permanent_arena->allocate_array<Expression*>(uninitialized_count);
		argument_count = 0;
		subtype_count = 0;
		uninitialized_count = 0;

		for (int i = 0; i < children.size; i++) 
		{
			Node* child = children[i];
			if (child->type == Node_Type::ARGUMENT) {
				call_node->arguments[argument_count] = downcast<Argument>(child);
				argument_count += 1;
			}
			else if (child->type == Node_Type::SUBTYPE_INITIALIZER) {
				call_node->subtype_initializers[subtype_count] = downcast<Subtype_Initializer>(child);
				subtype_count += 1;
			}
			else if (child->type == Node_Type::EXPRESSION) {
				call_node->uninitialized_tokens[uninitialized_count] = downcast<Expression>(child);
				uninitialized_count += 1;
			}
			else {
				panic("");
			}
		}

		PARSE_SUCCESS(call_node);
	}

	Enum_Member_Node* parse_enum_member(AST::Node* parent);
	Switch_Case* parse_switch_case(AST::Node* parent);
	Structure_Member_Node* parse_struct_member(AST::Node* parent);
	Symbol_Node* parse_symbol_node(AST::Node* parent);
	Statement* parse_statement(Node* parent);
	Definition* parse_definition(Node* parent);

	// The wrappers are just type-less versions (Return Node*) for use in parse_list
	Node* wrapper_parse_statement(Node* parent) { return upcast(parse_statement(parent)); }
	Node* wrapper_parse_enum_member(AST::Node* parent) { return upcast(parse_enum_member(parent)); }
	Node* wrapper_parse_switch_case(AST::Node* parent) { return upcast(parse_switch_case(parent)); }
	Node* wrapper_parse_struct_member(AST::Node* parent) { return upcast(parse_struct_member(parent)); }
	Node* wrapper_parse_symbol_node(AST::Node* parent) { return upcast(parse_symbol_node(parent)); }
	Node* wrapper_parse_expression_or_error(AST::Node* parent) { return upcast(parse_expression_or_error_expr(parent)); }
	Node* wrapper_parse_parameter(AST::Node* parent) { return upcast(parse_parameter(parent)); }
	Node* wrapper_parse_definition(Node* parent) { return upcast(parse_definition(parent)); }

	Symbol_Node* parse_symbol_node(Node* parent)
	{
		if (!test_token(Token_Type::IDENTIFIER)) {
			return nullptr;
		}
		auto node = allocate_base<Symbol_Node>(parent, Node_Type::SYMBOL_NODE);
		node->name = get_token()->options.string_value;
		node->is_root_lookup = false;
		node->is_definition = true; 
		advance_token();
		PARSE_SUCCESS(node);
	}

	Symbol_Node* parse_symbol_node_or_error(Node* parent)
	{
		auto node = allocate_base<Symbol_Node>(parent, Node_Type::SYMBOL_NODE);
		node->is_root_lookup = false;
		if (test_token(Token_Type::IDENTIFIER)) {
			node->name = get_token()->options.string_value;
			advance_token();
		}
		else {
			log_error_range_offset("Expected identifier", 0);
			node->name = parser.predefined_ids->invalid_symbol_name;
		}
		PARSE_SUCCESS(node);
	}

	Definition* parse_definition(Node* parent)
	{
		auto& ids = *parser.predefined_ids;
		CHECKPOINT_SETUP;

		Token_Type token_type = get_token()->type;
		switch (token_type)
		{
		case Token_Type::VAR:
		case Token_Type::CONST_KEYWORD:
		case Token_Type::GLOBAL_KEYWORD:
		{
			Definition* result = allocate_base<Definition>(parent, Node_Type::DEFINITION);
			advance_token();
			if (token_type == Token_Type::VAR) {
				result->type = Definition_Type::VARIABLE;
			}
			else if (token_type == Token_Type::CONST_KEYWORD) {
				result->type = Definition_Type::CONSTANT;
			}
			else {
				result->type = Definition_Type::GLOBAL;
			}

			// Parse name, type and value
			Definition_Value& value = result->options.value;
			value.symbol = parse_symbol_node_or_error(upcast(result));
			value.datatype_expr.available = false;
			value.value_expr.available = false;
			skip_tokens_until_token_type_found(Token_Type::COLON);
			if (test_token(Token_Type::COLON)) {
				advance_token();
				value.datatype_expr = optional_make_success(parse_expression_or_error_expr(upcast(result)));
			}
			skip_tokens_until_token_type_found(Token_Type::ASSIGN);
			if (test_token(Token_Type::ASSIGN)) {
				advance_token();
				value.value_expr = optional_make_success(parse_expression_or_error_expr(upcast(result)));
			}

			PARSE_SUCCESS(result);
		}
		case Token_Type::FUNCTION_KEYWORD:
		{
			Definition* result = allocate_base<Definition>(parent, Node_Type::DEFINITION);
			result->type = Definition_Type::FUNCTION;
			Definition_Function& function = result->options.function;
			advance_token();

			function.symbol = parse_symbol_node_or_error(upcast(result));
			skip_tokens_until_token_type_found(Token_Type::PARENTHESIS_OPEN);
			function.signature = parse_signature(upcast(result), true);
			if (on_follow_block()) {
				function.body.is_expression = false;
				function.body.block = parse_code_block(upcast(result), nullptr);
			}
			else {
				function.body.is_expression = true;
				function.body.expr = parse_expression_or_error_expr(upcast(result));
			}

			PARSE_SUCCESS(result);
		}
		case Token_Type::MODULE:
		{
			Definition* result = allocate_base<Definition>(parent, Node_Type::DEFINITION);
			result->type = Definition_Type::MODULE;
			Definition_Module& module = result->options.module;
			advance_token();

			module.symbol = optional_make_success(parse_symbol_node_or_error(upcast(result)));
			skip_until_next_follow_block();
			module.definitions = parse_list_items_as_array<Definition>(upcast(result), wrapper_parse_definition);

			PARSE_SUCCESS(result);
		}
		case Token_Type::STRUCT:
		case Token_Type::UNION:
		{
			Definition* result = allocate_base<Definition>(parent, Node_Type::DEFINITION);
			result->type = Definition_Type::STRUCT;
			Definition_Struct& structure = result->options.structure;
			structure.is_union = token_type == Token_Type::UNION;
			advance_token();

			structure.symbol = parse_symbol_node_or_error(upcast(result));
			skip_tokens_until_token_type_found(Token_Type::PARENTHESIS_OPEN);
			structure.signature = parse_signature(upcast(result), false);
			skip_until_next_follow_block();
			structure.members = parse_list_items_as_array<Structure_Member_Node>(upcast(result), wrapper_parse_struct_member);

			PARSE_SUCCESS(result);
		}
		case Token_Type::ENUM:
		{
			Definition* result = allocate_base<Definition>(parent, Node_Type::DEFINITION);
			result->type = Definition_Type::ENUM;
			Definition_Enum& enumeration = result->options.enumeration;
			advance_token();

			enumeration.symbol = parse_symbol_node_or_error(upcast(result));
			skip_until_next_follow_block();
			enumeration.members = parse_list_items_as_array<Enum_Member_Node>(upcast(result), wrapper_parse_enum_member);

			PARSE_SUCCESS(result);
		}
		case Token_Type::IMPORT:
		{
			Definition* result = allocate_base<Definition>(parent, Node_Type::DEFINITION);
			result->type = Definition_Type::IMPORT;
			Definition_Import& import_node = result->options.import;
			advance_token();
	
			import_node.alias_name.available = false;
			import_node.import_type = Import_Type::SYMBOLS;
			import_node.operator_type = Import_Operator::SINGLE_SYMBOL;
			import_node.options.path = nullptr;
	
			if (test_token(Token_Type::IDENTIFIER))
			{
				auto id = get_token()->options.string_value;
				if (id == ids.operators) {
					import_node.import_type = Import_Type::OPERATORS;
					advance_token();
				}
				else if (id == ids.dot_calls) {
					import_node.import_type = Import_Type::DOT_CALLS;
					advance_token();
				}
			}
	
			// Special path for import ~* and import ~**, because they are custom tokens
			if (test_token(Token_Type::TILDE_STAR) || test_token(Token_Type::TILDE_STAR_STAR))
			{
				import_node.operator_type =
					test_token(Token_Type::TILDE_STAR) ? Import_Operator::MODULE_IMPORT : Import_Operator::MODULE_IMPORT_TRANSITIVE;
	
				Path_Lookup* path_lookup = allocate_base<Path_Lookup>(upcast(result), Node_Type::PATH_LOOKUP);
				path_lookup->is_dot_call_lookup = false;
				path_lookup->parts = parser.permanent_arena->allocate_array<Symbol_Node*>(1);
				Symbol_Node* lookup = allocate_base<Symbol_Node>(upcast(result), Node_Type::SYMBOL_NODE);
				lookup->is_root_lookup = true;
				lookup->name = ids.root_module;
				path_lookup->parts[0] = lookup;
				advance_token();
				node_finalize_range(upcast(lookup));
				node_finalize_range(upcast(path_lookup));
	
				import_node.options.path = path_lookup;
				PARSE_SUCCESS(result);
			}
	
			// Check if it's a file import
			if (test_token(Token_Type::LITERAL_STRING)) 
			{
				import_node.operator_type = Import_Operator::FILE_IMPORT;
				import_node.options.file_import.node_unit = parser.unit;
				import_node.options.file_import.relative_path = get_token()->options.string_value;
				advance_token();
			}
			else
			{
				import_node.operator_type = Import_Operator::SINGLE_SYMBOL;
				import_node.options.path = parse_path_lookup_or_error(upcast(result));
				if (test_token(Token_Type::TILDE_STAR)) {
					import_node.operator_type = Import_Operator::MODULE_IMPORT;
					advance_token();
				}
				else if (test_token(Token_Type::TILDE_STAR_STAR)) {
					import_node.operator_type = Import_Operator::MODULE_IMPORT_TRANSITIVE;
					advance_token();
				}
			}
	
			if (test_token(Token_Type::AS) && test_token(Token_Type::IDENTIFIER, 1)) {
				advance_token();
				import_node.alias_name = optional_make_success(parse_symbol_node(upcast(result)));
			}
			PARSE_SUCCESS(result);
		}
		case Token_Type::EXTERN:
		{
			Definition* result = allocate_base<Definition>(parent, Node_Type::DEFINITION);
			result->type = Definition_Type::EXTERN;
			Definition_Extern_Import& extern_node = result->options.extern_import;
			int start = parser.pos;
			advance_token();

			if (test_token(Token_Type::IDENTIFIER))
			{
				String* id = get_token()->options.string_value;
				if (id == ids.global) {
					extern_node.type = Extern_Type::GLOBAL;
				}
				else if (id == ids.lib) {
					extern_node.type = Extern_Type::COMPILER_SETTING;
					extern_node.options.setting.type = Extern_Compiler_Setting::LIBRARY;
				}
				else if (id == ids.lib_dir) {
					extern_node.type = Extern_Type::COMPILER_SETTING;
					extern_node.options.setting.type = Extern_Compiler_Setting::LIBRARY_DIRECTORY;
				}
				else if (id == ids.source) {
					extern_node.type = Extern_Type::COMPILER_SETTING;
					extern_node.options.setting.type = Extern_Compiler_Setting::SOURCE_FILE;
				}
				else if (id == ids.header) {
					extern_node.type = Extern_Type::COMPILER_SETTING;
					extern_node.options.setting.type = Extern_Compiler_Setting::HEADER_FILE;
				}
				else if (id == ids.header_dir) {
					extern_node.type = Extern_Type::COMPILER_SETTING;
					extern_node.options.setting.type = Extern_Compiler_Setting::INCLUDE_DIRECTORY;
				}
				else if (id == ids.definition) {
					extern_node.type = Extern_Type::COMPILER_SETTING;
					extern_node.options.setting.type = Extern_Compiler_Setting::DEFINITION;
				}
				else {
					log_error_range_offset("Identifier after extern must be one of: function, global, source, lib, lib_dir", 1);
					CHECKPOINT_EXIT;
				}
			}
			else if (test_token(Token_Type::FUNCTION_KEYWORD)) {
				extern_node.type = Extern_Type::FUNCTION;
			}
			else if (test_token(Token_Type::STRUCT)) {
				extern_node.type = Extern_Type::STRUCT;
			}
			else {
				log_error("Expected extern-type after extern keyword!", start, start + 1);
				CHECKPOINT_EXIT;
			}
			advance_token();
	
			switch (extern_node.type)
			{
			case Extern_Type::FUNCTION:
			{
				extern_node.options.function.symbol = parse_symbol_node_or_error(upcast(result));
				extern_node.options.function.type_expr = parse_expression_or_error_expr(upcast(result));
				break;
			}
			case Extern_Type::GLOBAL:
			{
				extern_node.options.global_lookup = parse_path_lookup_or_error(upcast(result));
				break;
			}
			case Extern_Type::STRUCT: {
				extern_node.options.struct_type_lookup = parse_path_lookup_or_error(upcast(result));
				break;
			}
			case Extern_Type::COMPILER_SETTING:
			{
				if (!test_token(Token_Type::LITERAL_STRING)) {
					log_error_range_offset("Expected string literal", 1);
					CHECKPOINT_EXIT;
				}
				extern_node.options.setting.value = get_token()->options.string_value;
				advance_token();
				break;
			}
			default: panic("");
			}
	
			PARSE_SUCCESS(result);
		}
		case Token_Type::OPERATORS:
		{
			Definition* result = allocate_base<Definition>(parent, Node_Type::DEFINITION);
			result->type = Definition_Type::CUSTOM_OPERATOR;
			Definition_Custom_Operator& custom_op = result->options.custom_operator;
			custom_op.type = Custom_Operator_Type::INVALID;
			advance_token();

			if (test_token(Token_Type::IDENTIFIER)) 
			{
				String* id = get_token()->options.string_value;
				if (id == ids.add_array_access) {
					custom_op.type = Custom_Operator_Type::ARRAY_ACCESS;
				}
				else if (id == ids.add_binop) {
					custom_op.type = Custom_Operator_Type::BINOP;
				}
				else if (id == ids.add_unop) {
					custom_op.type = Custom_Operator_Type::UNOP;
				}
				else if (id == ids.add_cast) {
					custom_op.type = Custom_Operator_Type::CAST;
				}
				else if (id == ids.add_iterator) {
					custom_op.type = Custom_Operator_Type::ITERATOR;
				}
				else {
					log_error_range_offset("Expected valid option, e.g. operators add_binop", 0);
				}
			}
			else {
				log_error_range_offset("Expected Identifier", 0);
			}

			custom_op.call_node = parse_call_node(upcast(result));
			PARSE_SUCCESS(result);
		}
		}

		CHECKPOINT_EXIT;
	}

	Structure_Member_Node* parse_struct_member(Node* parent)
	{
		CHECKPOINT_SETUP;
		if (!test_token(Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;

		auto result = allocate_base<Structure_Member_Node>(parent, AST::Node_Type::STRUCT_MEMBER);
		result->is_expression = true;
		result->options.expression = nullptr;
		result->name = get_token()->options.string_value;
		advance_token();

		if (!test_token(Token_Type::COLON)) {
			log_error_range_offset("Expected : after struct member name!", 0);
			result->options.expression = create_error_expression(upcast(result));
			PARSE_SUCCESS(result);
		}
		advance_token();

		if (on_follow_block())
		{
			result->is_expression = false;
			result->options.subtype_members = parse_list_items_as_array<Structure_Member_Node>(upcast(result), wrapper_parse_struct_member);
			PARSE_SUCCESS(result);
		}

		result->options.expression = parse_expression_or_error_expr(upcast(result));
		PARSE_SUCCESS(result);
	}

	Switch_Case* parse_switch_case(Node* parent)
	{
		auto result = allocate_base<Switch_Case>(parent, Node_Type::SWITCH_CASE);
		result->value = optional_make_failure<Expression*>();
		result->variable_definition = optional_make_failure<AST::Symbol_Node*>();

		// Check for default case
		if (test_token(Token_Type::DEFAULT)) {
			advance_token();
		}
		else
		{
			result->value.available = true;
			result->value.value = parse_expression_or_error_expr(upcast(result));
			if (test_token(Token_Type::FUNCTION_ARROW))
			{
				advance_token();
				if (test_token(Token_Type::IDENTIFIER)) {
					result->variable_definition = optional_make_success(parse_symbol_node(upcast(result)));
				}
				else {
					log_error_range_offset("Expected identifier after arrow", 0);
				}
			}
		}

		skip_until_next_follow_block();
		result->block = parse_code_block(&result->base, 0);
		// Set block label (Switch cases need special treatment because they 'Inherit' the label from the switch
		assert(parent->type == Node_Type::STATEMENT && ((Statement*)parent)->type == Statement_Type::SWITCH_STATEMENT, "");
		result->block->block_id = ((Statement*)parent)->options.switch_statement.label;
		PARSE_SUCCESS(result);
	}

	// This function exists because for loops only allow assignments (+ binop assign) + expression in their last part...
	AST::Statement* parse_assignment_or_expression_statement(Node* parent)
	{
		auto& ids = *parser.predefined_ids;

		CHECKPOINT_SETUP;
		auto result = allocate_base<Statement>(parent, Node_Type::STATEMENT);

		auto expr = parse_expression(&result->base);
		if (expr == nullptr) {
			CHECKPOINT_EXIT;
		}

		if (test_token(Token_Type::ASSIGN))
		{
			result->type = Statement_Type::ASSIGNMENT;
			result->options.assignment.left_side = expr;
			advance_token();
			result->options.assignment.right_side = parse_expression_or_error_expr(upcast(result));
			PARSE_SUCCESS(result);
		}
		else if (test_token(Token_Type::ASSIGN_ADD) || test_token(Token_Type::ASSIGN_SUB) ||
			test_token(Token_Type::ASSIGN_MULT) || test_token(Token_Type::ASSIGN_DIV) || test_token(Token_Type::ASSIGN_MODULO))
		{
			result->type = Statement_Type::BINOP_ASSIGNMENT;
			auto& assign = result->options.binop_assignment;
			if (test_token(Token_Type::ASSIGN_ADD)) {
				assign.binop = Binop::ADDITION;
			}
			else if (test_token(Token_Type::ASSIGN_SUB)) {
				assign.binop = Binop::SUBTRACTION;
			}
			else if (test_token(Token_Type::ASSIGN_MULT)) {
				assign.binop = Binop::MULTIPLICATION;
			}
			else if (test_token(Token_Type::ASSIGN_DIV)) {
				assign.binop = Binop::DIVISION;
			}
			else if (test_token(Token_Type::ASSIGN_MODULO)) {
				assign.binop = Binop::MODULO;
			}
			else {
				panic("");
			}
			advance_token();

			assign.left_side = expr;
			assign.right_side = parse_expression_or_error_expr(upcast(result));
			PARSE_SUCCESS(result);
		}

		result->type = Statement_Type::EXPRESSION_STATEMENT;
		result->options.expression = expr;
		PARSE_SUCCESS(result);
	}

	AST::Statement* parse_statement(Node* parent)
	{
		auto& ids = *parser.predefined_ids;

		CHECKPOINT_SETUP;
		auto result = allocate_base<Statement>(parent, Node_Type::STATEMENT);

		// Check if definition statement
		{
			Definition* definition = parse_definition(upcast(result));
			if (definition != nullptr) {
				result->type = Statement_Type::DEFINITION;
				result->options.definition = definition;
				PARSE_SUCCESS(result);
			}
		}

		switch (get_token()->type)
		{
		case Token_Type::IF:
		{
			advance_token();
			result->type = Statement_Type::IF_STATEMENT;
			auto& if_stat = result->options.if_statement;
			if_stat.condition = parse_expression_or_error_expr(&result->base);
			if_stat.block = parse_code_block(&result->base, upcast(if_stat.condition));
			if_stat.else_block.available = false;

			auto last_if_stat = result;
			// Parse else-if chain
			while (test_token(Token_Type::ELSE, 0) && test_token(Token_Type::IF, 1))
			{
				auto implicit_else_block = allocate_base<AST::Code_Block>(&last_if_stat->base, Node_Type::CODE_BLOCK);
				implicit_else_block->statements = parser.permanent_arena->allocate_array<Statement*>(1);
				implicit_else_block->block_id = optional_make_failure<String*>();

				auto new_if_stat = allocate_base<AST::Statement>(&last_if_stat->base, Node_Type::STATEMENT);
				advance_token();
				advance_token();
				new_if_stat->type = Statement_Type::IF_STATEMENT;
				auto& new_if = new_if_stat->options.if_statement;
				new_if.condition = parse_expression_or_error_expr(&new_if_stat->base);
				new_if.block = parse_code_block(&last_if_stat->base, upcast(new_if.condition));
				new_if.else_block.available = false;
				node_finalize_range(upcast(implicit_else_block));
				node_finalize_range(upcast(new_if_stat));

				implicit_else_block->statements[0] = new_if_stat;
				last_if_stat->options.if_statement.else_block = optional_make_success(implicit_else_block);
				last_if_stat = new_if_stat;
			}
			if (test_token(Token_Type::ELSE, 0))
			{
				advance_token();
				last_if_stat->options.if_statement.else_block = optional_make_success(parse_code_block(&last_if_stat->base, 0));
			}
			// NOTE: Here we return result, not last if stat
			PARSE_SUCCESS(result);
		}
		case Token_Type::LOOP:
		{
			advance_token();
			bool error_logged = false;

			// Figure out loop type
			result->type = Statement_Type::WHILE_STATEMENT;
			if (test_token(Token_Type::IDENTIFIER, Token_Type::IN_KEYWORD) ||
				test_token(Token_Type::IDENTIFIER, Token_Type::COMMA, Token_Type::IDENTIFIER, Token_Type::IN_KEYWORD)) {
				result->type = Statement_Type::FOREACH_LOOP;
			}
			else if (test_token(Token_Type::PARENTHESIS_OPEN, Token_Type::VAR)) {
				result->type = Statement_Type::FOR_LOOP;
			}

			if (result->type == Statement_Type::WHILE_STATEMENT)
			{
				// Normal loop (Change for/while statems to just loop)
				result->type = Statement_Type::WHILE_STATEMENT;
				result->options.while_statement.condition.available = false;
				AST::Expression* condition = nullptr;
				if (!on_follow_block()) {
					condition = parse_expression_or_error_expr(upcast(result));
					result->options.while_statement.condition = optional_make_success(condition);
				}
				result->options.while_statement.block = parse_code_block(upcast(result), upcast(condition));
			}
			else if (result->type == Statement_Type::FOREACH_LOOP)
			{
				result->options.foreach_loop.loop_variable_definition = parse_symbol_node(upcast(result));
				if (test_token(Token_Type::COMMA)) {
					advance_token();
					result->options.foreach_loop.index_variable_definition.available = true;
					result->options.foreach_loop.index_variable_definition.value = parse_symbol_node(upcast(result));
				}
				assert(test_token(Token_Type::IN_KEYWORD), "");
				advance_token();
				result->options.foreach_loop.expression = parse_expression_or_error_expr(upcast(result));
				result->options.foreach_loop.body_block = parse_code_block(upcast(result), upcast(result->options.foreach_loop.loop_variable_definition));
			}
			else // For loop
			{
				assert(test_token(Token_Type::PARENTHESIS_OPEN), "");
				List_Iter iter = List_Iter::create(true);
				assert(iter.is_valid && !iter.on_end_token(), "");
				assert(test_token(Token_Type::VAR), "");
				advance_token(); // Skip var

				result->options.for_loop.loop_variable_definition = parse_symbol_node_or_error(upcast(result));
				if (test_token(Token_Type::COLON)) {
					result->options.for_loop.loop_variable_type.available = true;
					result->options.for_loop.loop_variable_type.value = parse_expression_or_error_expr(upcast(result));
				}

				if (test_token(Token_Type::ASSIGN)) {
					advance_token();
					result->options.for_loop.initial_value = parse_expression_or_error_expr(upcast(result));
				}
				else {
					log_error_range_offset("Expected = token", 0);
					result->options.for_loop.initial_value = create_error_expression(upcast(result));
				}

				// Parse loop condition and increment
				result->options.for_loop.increment_statement = nullptr;
				result->options.for_loop.condition = nullptr;
				iter.goto_next();
				if (!iter.on_end_token()) 
				{
					result->options.for_loop.condition = parse_expression_or_error_expr(upcast(result));
					iter.goto_next();
					if (!iter.on_end_token()) {
						result->options.for_loop.increment_statement = parse_assignment_or_expression_statement(upcast(result));
					}
				}
				if (result->options.for_loop.condition == nullptr) {
					result->options.for_loop.condition = create_error_expression(upcast(result));
				}
				if (result->options.for_loop.increment_statement == nullptr) {
					result->options.for_loop.increment_statement = create_error_statement(upcast(result));
				}
				iter.finish();

				result->options.for_loop.body_block = parse_code_block(upcast(result), upcast(result->options.for_loop.loop_variable_definition));
			}

			PARSE_SUCCESS(result);
		}
		case Token_Type::DEFER:
		{
			advance_token();
			result->type = Statement_Type::DEFER;

			if (on_follow_block()) {
				result->options.defer_block = parse_code_block(upcast(result), nullptr);
				PARSE_SUCCESS(result);
			}

			// Else we have single statement defer
			AST::Statement* statement = parse_statement(upcast(result));
			if (statement == nullptr) {
				CHECKPOINT_EXIT;
			}

			// Generate new code block, set parent/child correctly, set ranges correctly
			Code_Block* code_block = allocate_base<Code_Block>(upcast(result), Node_Type::CODE_BLOCK);
			code_block->statements = parser.permanent_arena->allocate_array<Statement*>(1);
			code_block->block_id = optional_make_failure<String*>();

			code_block->statements[0] = statement;
			statement->base.parent = upcast(code_block);
			code_block->base.range = statement->base.range;
			code_block->base.bounding_range = statement->base.bounding_range;

			result->options.defer_block = code_block;
			PARSE_SUCCESS(result);
		}
		case Token_Type::DEFER_RESTORE:
		{
			advance_token();
			result->type = Statement_Type::DEFER_RESTORE;
			result->options.defer_restore.left_side = parse_expression_or_error_expr(upcast(result));
			if (!test_token(Token_Type::ASSIGN)) {
				auto error_expr = allocate_base<AST::Expression>(upcast(result), AST::Node_Type::EXPRESSION);
				log_error_range_offset("Expected assignment after argument_expression", 0);
				error_expr->type = Expression_Type::ERROR_EXPR;
				node_finalize_range(upcast(error_expr));
				result->options.defer_restore.right_side = error_expr;
				PARSE_SUCCESS(result);
			}

			advance_token();
			result->options.defer_restore.right_side = parse_expression_or_error_expr(upcast(result));
			PARSE_SUCCESS(result);
		}
		case Token_Type::SWITCH:
		{
			advance_token();
			result->type = Statement_Type::SWITCH_STATEMENT;
			auto& switch_stat = result->options.switch_statement;
			switch_stat.condition = parse_expression_or_error_expr(&result->base);
			skip_until_next_follow_block();
			switch_stat.label = parse_block_label_or_use_related_node_id(upcast(switch_stat.condition));
			switch_stat.cases = parse_list_items_as_array<Switch_Case>(upcast(result), wrapper_parse_switch_case);
			PARSE_SUCCESS(result);
		}
		case Token_Type::RETURN: {
			advance_token();
			result->type = Statement_Type::RETURN_STATEMENT;
			auto expr = parse_expression(&result->base);
			result->options.return_value.available = false;
			if (expr != 0) {
				result->options.return_value = optional_make_success(expr);
			}
			PARSE_SUCCESS(result);
		}
		case Token_Type::CONTINUE: {
			advance_token();
			result->type = Statement_Type::CONTINUE_STATEMENT;
			result->options.continue_name = optional_make_failure<String*>();
			if (test_token(Token_Type::IDENTIFIER)) {
				result->options.continue_name = optional_make_success(get_token(0)->options.string_value);
				advance_token();
			}
			PARSE_SUCCESS(result);
		}
		case Token_Type::BREAK: {
			advance_token();
			result->type = Statement_Type::BREAK_STATEMENT;
			result->options.break_name = optional_make_failure<String*>();
			if (test_token(Token_Type::IDENTIFIER)) {
				result->options.break_name = optional_make_success(get_token(0)->options.string_value);
				advance_token();
			}
			PARSE_SUCCESS(result);
		}
		case Token_Type::SCOPE: {
			result->type = Statement_Type::BLOCK;
			result->options.block = parse_code_block(upcast(result), nullptr);
			PARSE_SUCCESS(result);
			break;
		}
		}

		if (on_follow_block())
		{
			log_error_range_offset("New block requires the scope keyword", 0);
			result->type = Statement_Type::BLOCK;
			result->options.block = parse_code_block(upcast(result), nullptr);
			PARSE_SUCCESS(result);
		}

		// Otherwise try to parse expression or assignment statement
		parser_rollback(checkpoint); // Rollback first
		return parse_assignment_or_expression_statement(parent);
	}

	Enum_Member_Node* parse_enum_member(Node* parent)
	{
		if (!test_token(Token_Type::IDENTIFIER)) {
			return 0;
		}

		CHECKPOINT_SETUP;
		auto result = allocate_base<Enum_Member_Node>(parent, Node_Type::ENUM_MEMBER);
		result->name = get_token()->options.string_value;
		advance_token();
		if (test_token(Token_Type::ASSIGN)) {
			advance_token();
			result->value = optional_make_success(parse_expression_or_error_expr(&result->base));
		}
		PARSE_SUCCESS(result);
	}

	// Used by code-block and switch statement
	Optional<String*> parse_block_label_or_use_related_node_id(AST::Node* related_node)
	{
		Optional<String*> result = optional_make_failure<String*>();
		if (test_token(Token_Type::SCOPE, Token_Type::IDENTIFIER)) {
			result = optional_make_success(get_token(1)->options.string_value);
		}
		else if (related_node != nullptr)
		{
			if (related_node->type == AST::Node_Type::EXPRESSION) {
				auto expr = downcast<AST::Expression>(related_node);
				if (expr->type == AST::Expression_Type::PATH_LOOKUP && expr->options.path_lookup->parts.size > 0) {
					result = optional_make_success<String*>(expr->options.path_lookup->parts[expr->options.path_lookup->parts.size - 1]->name);
				}
			}
			else if (related_node->type == AST::Node_Type::SYMBOL_NODE) {
				result = optional_make_success<String*>(downcast<AST::Symbol_Node>(related_node)->name);
			}
		}
		return result;
	}

	// Always returns success, but if there is no follow block, there are errors
	Code_Block* parse_code_block(Node* parent, AST::Node* related_node)
	{
		auto result = allocate_base<Code_Block>(parent, Node_Type::CODE_BLOCK);

		skip_until_next_follow_block();
		result->block_id = parse_block_label_or_use_related_node_id(related_node);
		if (on_follow_block()) {
			result->statements = parse_list_items_as_array<Statement>(upcast(result), wrapper_parse_statement);
		}
		else {
			log_error_range_offset("Expected code block", 0);
		}

		PARSE_SUCCESS(result);
	}

	Argument* parse_argument(Node* parent)
	{
		CHECKPOINT_SETUP;
		auto result = allocate_base<Argument>(parent, Node_Type::ARGUMENT);
		if (test_token(Token_Type::IDENTIFIER) && test_token(Token_Type::ASSIGN, 1)) {
			result->name = optional_make_success(get_token()->options.string_value);
			advance_token();
			advance_token();
			result->value = parse_expression_or_error_expr(&result->base);
			PARSE_SUCCESS(result);
		}
		result->value = parse_expression_or_error_expr(&result->base);
		PARSE_SUCCESS(result);
	}

	Subtype_Initializer* parse_subtype_initializer(Node* parent)
	{
		if (!test_token(Token_Type::SUBTYPE_ACCESS, Token_Type::IDENTIFIER, Token_Type::ASSIGN) &&
			!test_token(Token_Type::BASETYPE_ACCESS, Token_Type::ASSIGN))
		{
			return 0;
		}

		CHECKPOINT_SETUP;
		auto result = allocate_base<Subtype_Initializer>(parent, Node_Type::SUBTYPE_INITIALIZER);
		result->name.available = false;

		if (test_token(Token_Type::SUBTYPE_ACCESS)) {
			advance_token();
			result->name = optional_make_success(get_token(0)->options.string_value);
		}
		advance_token();

		assert(test_token(Token_Type::ASSIGN), "Should be true after previous if");
		advance_token();  // Skip =

		result->call_node = parse_call_node(upcast(result));
		PARSE_SUCCESS(result);
	}

	Parameter* parse_parameter(Node* parent)
	{
		CHECKPOINT_SETUP;
		auto result = allocate_base<Parameter>(parent, Node_Type::PARAMETER);
		result->is_comptime = false;
		result->is_return_type = false;
		result->type.available = false;

		// Parse identifier and optional mutators
		if (test_token(Token_Type::DOLLAR)) {
			result->is_comptime = true;
			advance_token();
		}
		result->symbol = parse_symbol_node_or_error(upcast(result));

		if (!test_token(Token_Type::COLON)) {
			PARSE_SUCCESS(result);
		}

		advance_token(); // Skip :
		result->type = optional_make_success(parse_expression_or_error_expr((Node*)result));

		PARSE_SUCCESS(result);
	}

	Signature* parse_signature(Node* parent, bool allow_return_type)
	{
		CHECKPOINT_SETUP;

		auto result = allocate_base<Signature>(parent, Node_Type::SIGNATURE);
		if (!test_token(Token_Type::PARENTHESIS_OPEN)) {
			PARSE_SUCCESS(result);
		}

		// parse_list_items is used instead of parse_list_items_as_array because we may need to add the return-type later
		auto arena_checkpoint = parser.temporary_arena->make_checkpoint();
		SCOPE_EXIT(arena_checkpoint.rewind());
		DynArray<Node*> normal_params = DynArray<Node*>::create(parser.temporary_arena);
		parse_list_items(parent, wrapper_parse_parameter, normal_params);

		if (allow_return_type && test_token(Token_Type::FUNCTION_ARROW)) 
		{
			advance_token();
			auto return_param = allocate_base<Parameter>(parent, Node_Type::PARAMETER);
			return_param->is_comptime = false;
			return_param->is_return_type = true;
			return_param->symbol = allocate_base<Symbol_Node>(upcast(return_param), Node_Type::SYMBOL_NODE);
			return_param->symbol->name = parser.predefined_ids->return_type_name;
			return_param->symbol->is_root_lookup = false;
			node_finalize_range(upcast(return_param->symbol));
			return_param->type = optional_make_success(parse_expression_or_error_expr(upcast(result)));
			normal_params.push_back(upcast(return_param));
		}

		result->parameters = parser.permanent_arena->allocate_array<Parameter*>(normal_params.size);
		memory_copy(result->parameters.data, normal_params.buffer.data, sizeof(Parameter*) * normal_params.size);

		PARSE_SUCCESS(result);
	}

	Expression* parse_single_expression_no_postop(Node* parent)
	{
		auto& ids = *parser.predefined_ids;

		// DOCU: Parses Pre-Ops + Bases, but no postops
		CHECKPOINT_SETUP;
		auto result = allocate_base<Expression>(parent, Node_Type::EXPRESSION);

		switch (get_token()->type)
		{
			// Unops
		case Token_Type::MINUS:
		case Token_Type::NOT:
		{
			Unop unop = test_token(Token_Type::MINUS) ? Unop::NEGATE : Unop::NOT;
			advance_token();
			result->type = Expression_Type::UNARY_OPERATION;
			result->options.unop.type = unop;
			result->options.unop.expr = parse_single_expression_or_error(&result->base);
			PARSE_SUCCESS(result);
		}
		case Token_Type::ASTERIX:
		case Token_Type::OPTIONAL_POINTER:
		{
			bool is_asterix = test_token(Token_Type::ASTERIX);
			if (is_asterix && test_token(Token_Type::FUNCTION_KEYWORD, 1)) { // Function pointer, e.g. *fn(a:int,b:int)=>int
				result->type = Expression_Type::FUNCTION_POINTER_TYPE;
				result->options.function_pointer_signature = parse_signature(upcast(result), true);
				PARSE_SUCCESS(result);
			}

			result->type = Expression_Type::POINTER_TYPE;
			result->options.pointer_type.is_optional = !is_asterix;
			advance_token();
			result->options.pointer_type.child_type = parse_single_expression_or_error(&result->base);
			PARSE_SUCCESS(result);
		}
		case Token_Type::BAKE:
		{
			result->type = Expression_Type::BAKE;
			advance_token();
			if (on_follow_block()) {
				result->options.bake_body.is_expression = false;
				result->options.bake_body.block = parse_code_block(&result->base, 0);
			}
			else {
				result->options.bake_body.is_expression = true;
				result->options.bake_body.expr = parse_single_expression_or_error(&result->base);
			}
			PARSE_SUCCESS(result);
		}
		case Token_Type::INSTANCIATE:
		{
			advance_token();
			result->type = Expression_Type::INSTANCIATE;
			auto& instanciate = result->options.instanciate;
			instanciate.path_lookup = parse_path_lookup(upcast(result));
			instanciate.return_type = optional_make_failure<AST::Expression*>();
			if (instanciate.path_lookup == nullptr) {
				CHECKPOINT_EXIT;
			}
			if (!test_token(Token_Type::PARENTHESIS_OPEN)) {
				CHECKPOINT_EXIT;
			}
			instanciate.call_node = parse_call_node(upcast(result));
			if (test_token(Token_Type::FUNCTION_ARROW)) {
				advance_token();
				instanciate.return_type = optional_make_success(parse_expression_or_error_expr(upcast(result)));
			}
			PARSE_SUCCESS(result);
		}
		case Token_Type::GET_OVERLOAD:
		case Token_Type::GET_OVERLOAD_POLY:
		{
			bool is_poly = test_token(Token_Type::GET_OVERLOAD_POLY);
			int start = parser.pos;

			advance_token();
			result->type = Expression_Type::GET_OVERLOAD;
			result->options.get_overload.is_poly = is_poly;
			result->options.get_overload.path = parse_path_lookup_or_error(upcast(result));

			// Parse Arguments
			auto arena_checkpoint = parser.temporary_arena->make_checkpoint();
			SCOPE_EXIT(arena_checkpoint.rewind());
			DynArray<Node*> normal_params = DynArray<Node*>::create(parser.temporary_arena);
			parse_list_items(parent, parse_overload_argument, normal_params);

			if (test_token(Token_Type::FUNCTION_ARROW)) 
			{
				AST::Get_Overload_Argument* return_type_argument = allocate_base<AST::Get_Overload_Argument>(upcast(result), AST::Node_Type::GET_OVERLOAD_ARGUMENT);
				advance_token();
				return_type_argument->id = ids.return_type_name;
				return_type_argument->type_expr = optional_make_success(parse_expression_or_error_expr(upcast(return_type_argument)));
				node_finalize_range(upcast(return_type_argument));
				normal_params.push_back(upcast(return_type_argument));
			}

			result->options.get_overload.arguments = parser.permanent_arena->allocate_array<Get_Overload_Argument*>(normal_params.size);
			memory_copy(result->options.get_overload.arguments.data, normal_params.buffer.data, sizeof(Parameter*) * normal_params.size);

			PARSE_SUCCESS(result);
		}
		case Token_Type::CAST:
		{
			result->type = Expression_Type::CAST;
			auto& cast = result->options.cast;
			cast.is_dot_call = false;
			advance_token();

			if (test_token(Token_Type::PARENTHESIS_OPEN)) {
				// cast(value, to, from, option)
				cast.call_node = parse_call_node(upcast(result));
			}
			else
			{
				// cast x
				Call_Node* call_node = allocate_base<Call_Node>(upcast(result), Node_Type::CALL_NODE);
				call_node->arguments = parser.permanent_arena->allocate_array<Argument*>(1);
				call_node->subtype_initializers = array_create_empty<Subtype_Initializer*>();
				call_node->uninitialized_tokens = array_create_empty<Expression*>();

				Argument* argument = allocate_base<Argument>(upcast(call_node), Node_Type::ARGUMENT);;
				argument->name.available = false;
				argument->value = parse_single_expression_or_error(upcast(argument));
				call_node->arguments[0] = argument;

				node_finalize_range(upcast(argument));
				node_finalize_range(upcast(call_node));
				cast.call_node = call_node;
			}

			PARSE_SUCCESS(result);
		}
		case Token_Type::BRACKET_OPEN:
		{
			if (test_token(Token_Type::BRACKET_CLOSED, 1)) {
				result->type = Expression_Type::SLICE_TYPE;
				advance_token();
				advance_token();
				result->options.slice_type = parse_single_expression_or_error(&result->base);
				PARSE_SUCCESS(result);
			}

			result->type = Expression_Type::ARRAY_TYPE;
			List_Iter list = List_Iter::create();
			result->options.array_type.size_expr = parse_expression_or_error_expr(&result->base);
			list.finish();
			result->options.array_type.type_expr = parse_single_expression_or_error(&result->base);
			PARSE_SUCCESS(result);
		}
		case Token_Type::DOLLAR:
		{
			result->type = Expression_Type::PATTERN_VARIABLE;
			advance_token();
			result->options.pattern_variable_symbol = parse_symbol_node_or_error(upcast(result));
			PARSE_SUCCESS(result);
		}
		case Token_Type::DOT:
		{
			advance_token();
			if (test_token(Token_Type::FUNCTION_KEYWORD))
			{
				result->type = Expression_Type::INFERRED_FUNCTION;
				advance_token();

				if (on_follow_block()) {
					result->options.inferred_function_body.is_expression = false;
					result->options.inferred_function_body.block = parse_code_block(&result->base, 0);
					PARSE_SUCCESS(result);
				}
				result->options.inferred_function_body.is_expression = true;
				result->options.inferred_function_body.expr = parse_expression_or_error_expr(upcast(result));
				PARSE_SUCCESS(result);
			}
			else if (test_token(Token_Type::PARENTHESIS_OPEN)) // Struct Initializer
			{
				result->type = Expression_Type::STRUCT_INITIALIZER;
				auto& init = result->options.struct_initializer;
				init.type_expr = optional_make_failure<Expression*>();
				init.call_node = parse_call_node(upcast(result));
				PARSE_SUCCESS(result);
			}
			else if (test_token(Token_Type::BRACKET_OPEN)) // Array Initializer
			{
				result->type = Expression_Type::ARRAY_INITIALIZER;
				auto& init = result->options.array_initializer;
				init.type_expr = optional_make_failure<Expression*>();
				init.values = parse_list_items_as_array<Expression>(upcast(result), wrapper_parse_expression_or_error); // Not sure if _or_error is correct here
				PARSE_SUCCESS(result);
			}
			else
			{
				result->type = Expression_Type::AUTO_ENUM;
				if (test_token(Token_Type::IDENTIFIER)) {
					result->options.auto_enum = get_token(0)->options.string_value;
					advance_token();
				}
				else {
					log_error("Missing member name", parser.pos - 1, parser.pos);
					result->options.auto_enum = ids.empty_string;
				}
				PARSE_SUCCESS(result);
			}
			CHECKPOINT_EXIT;
		}
		case Token_Type::LITERAL_STRING:
		{
			result->type = Expression_Type::LITERAL_READ;
			result->options.literal_read.type = Literal_Type::STRING;
			result->options.literal_read.options.string = get_token()->options.string_value;
			advance_token();
			PARSE_SUCCESS(result);
		}
		case Token_Type::LITERAL_INTEGER:
		{
			result->type = Expression_Type::LITERAL_READ;
			result->options.literal_read.type = Literal_Type::INTEGER;
			result->options.literal_read.options.int_val = get_token()->options.integer_value;
			advance_token();
			PARSE_SUCCESS(result);
		}
		case Token_Type::LITERAL_FLOAT:
		{
			result->type = Expression_Type::LITERAL_READ;
			result->options.literal_read.type = Literal_Type::FLOAT_VAL;
			result->options.literal_read.options.float_val = get_token()->options.float_value;
			advance_token();
			PARSE_SUCCESS(result);
		}
		case Token_Type::LITERAL_FALSE:
		case Token_Type::LITERAL_TRUE:
		{
			result->type = Expression_Type::LITERAL_READ;
			result->options.literal_read.type = Literal_Type::BOOLEAN;
			result->options.literal_read.options.boolean = test_token(Token_Type::LITERAL_TRUE);
			advance_token();
			PARSE_SUCCESS(result);
		}
		case Token_Type::LITERAL_NIL:
		{
			result->type = Expression_Type::LITERAL_READ;
			result->options.literal_read.type = Literal_Type::NULL_VAL;
			result->options.literal_read.options.null_ptr = nullptr;
			advance_token();
			PARSE_SUCCESS(result);
		}
		case Token_Type::IDENTIFIER:
		case Token_Type::TILDE: {
			result->type = Expression_Type::PATH_LOOKUP;
			result->options.path_lookup = parse_path_lookup_or_error(upcast(result));
			PARSE_SUCCESS(result);
		}
		case Token_Type::PARENTHESIS_OPEN: {
			List_Iter list_iter = List_Iter::create();
			result = parse_expression_or_error_expr(parent);
			list_iter.finish();
			PARSE_SUCCESS(result);
		}
		}

		CHECKPOINT_EXIT;
	}

	Expression* parse_post_operator_internal(Expression* child)
	{
		auto& ids = *parser.predefined_ids;

		// DOCU: Internal means that we don't add the result of this expression to the parameter child,
		//       but rather to the parent of the child
		CHECKPOINT_SETUP;

		auto result = allocate_base<Expression>(child->base.parent, Node_Type::EXPRESSION);
		switch (get_token()->type)
		{
		case Token_Type::DOT:
		{
			advance_token();
			if (test_token(Token_Type::PARENTHESIS_OPEN)) // Struct Initializer
			{
				result->type = Expression_Type::STRUCT_INITIALIZER;
				auto& init = result->options.struct_initializer;
				init.type_expr = optional_make_success(child);
				init.call_node = parse_call_node(upcast(result));
				PARSE_SUCCESS(result);
			}
			if (test_token(Token_Type::BRACKET_OPEN)) // Array Initializer
			{
				result->type = Expression_Type::ARRAY_INITIALIZER;
				auto& init = result->options.array_initializer;
				init.type_expr = optional_make_success(child);
				init.values = parse_list_items_as_array<Expression>(upcast(result), wrapper_parse_expression_or_error);
				PARSE_SUCCESS(result);
			}
			else
			{
				result->type = Expression_Type::MEMBER_ACCESS;
				result->options.member_access.expr = child;
				if (test_token(Token_Type::IDENTIFIER)) {
					result->options.member_access.name = get_token(0)->options.string_value;
					advance_token();
				}
				else {
					log_error("Missing member name", parser.pos - 1, parser.pos);
					result->options.member_access.name = ids.empty_string;
				}
				PARSE_SUCCESS(result);
			}
			CHECKPOINT_EXIT;
		}
		case Token_Type::SUBTYPE_ACCESS: 
		{
			result->type = Expression_Type::SUBTYPE_ACCESS;
			result->options.subtype_access.expr = child;
			advance_token();
			if (test_token(Token_Type::IDENTIFIER)) {
				result->options.subtype_access.name = get_token(0)->options.string_value;
				advance_token();
			}
			else {
				log_error("Missing member name", parser.pos - 1, parser.pos);
				result->options.subtype_access.name = ids.empty_string;
			}
			PARSE_SUCCESS(result);
		}
		case Token_Type::BASETYPE_ACCESS: 
		{
			result->type = Expression_Type::BASETYPE_ACCESS;
			result->options.basetype_access_expr = child;
			advance_token();
			PARSE_SUCCESS(result);
		}
		case Token_Type::POSTFIX_CALL_ARROW:
		{
			advance_token();

			AST::Call_Node** fill_call_node = nullptr;
			if (test_token(Token_Type::CAST))
			{
				result->type = Expression_Type::CAST;
				result->options.cast.is_dot_call = true;
				result->options.cast.call_node = nullptr;
				advance_token();
				fill_call_node = &result->options.cast.call_node;
			}
			else
			{
				result->type = Expression_Type::FUNCTION_CALL;
				auto& call = result->options.call;
				call.is_dot_call = true;

				Expression* call_expr = allocate_base<Expression>(upcast(result), AST::Node_Type::EXPRESSION);
				call_expr->type = AST::Expression_Type::PATH_LOOKUP;
				call_expr->options.path_lookup = parse_path_lookup_or_error(upcast(call_expr));
				call_expr->options.path_lookup->is_dot_call_lookup = true;
				node_finalize_range(upcast(call_expr));
				call.expr = call_expr;
				fill_call_node = &call.call_node;
			}

			AST::Argument* argument = allocate_base<Argument>(nullptr, AST::Node_Type::ARGUMENT);
			argument->name = optional_make_failure<String*>();
			argument->value = child;
			argument->base.range = child->base.range;
			argument->base.bounding_range = child->base.bounding_range;
			*fill_call_node = parse_call_node(upcast(result), argument);
			argument->base.parent = upcast(*fill_call_node);

			PARSE_SUCCESS(result);
		}
		case Token_Type::BRACKET_OPEN:
		{
			List_Iter iter = List_Iter::create();
			result->type = Expression_Type::ARRAY_ACCESS;
			result->options.array_access.array_expr = child;
			result->options.array_access.index_expr = parse_expression_or_error_expr(&result->base);
			iter.finish();
			PARSE_SUCCESS(result);
		}
		case Token_Type::PARENTHESIS_OPEN:
		{
			result->type = Expression_Type::FUNCTION_CALL;
			auto& call = result->options.call;
			call.is_dot_call = false;
			call.expr = child;
			call.call_node = parse_call_node(upcast(result));
			PARSE_SUCCESS(result);
		}
		case Token_Type::ADDRESS_OF:
		case Token_Type::DEREFERENCE:
		case Token_Type::OPTIONAL_DEREFERENCE:
		case Token_Type::QUESTION_MARK:
		{
			Unop unop = (Unop)-1;
			if (test_token(Token_Type::ADDRESS_OF)) {
				unop = Unop::ADDRESS_OF;
			}
			else if (test_token(Token_Type::DEREFERENCE)) {
				unop = Unop::DEREFERENCE;
			}
			else if (test_token(Token_Type::OPTIONAL_DEREFERENCE)) {
				unop = Unop::OPTIONAL_DEREFERENCE;
			}
			else if (test_token(Token_Type::QUESTION_MARK)) {
				unop = Unop::NULL_CHECK;
			}
			else {
				panic("");
			}

			result->type = Expression_Type::UNARY_OPERATION;
			result->options.unop.type = unop;
			result->options.unop.expr = child;
			advance_token();
			PARSE_SUCCESS(result);
		}
		}

		CHECKPOINT_EXIT;
	}

	// Does not parse binop-chains, 
	Expression* parse_single_expression(Node* parent)
	{
		// DOCU: This function parses: Pre-Ops + Node + Post-Op
		Expression* child = parse_single_expression_no_postop(parent);
		if (child == 0) return child;
		Expression* post_op = parse_post_operator_internal(child);
		while (post_op != 0) {
			child->base.parent = &post_op->base;
			child = post_op;
			post_op = parse_post_operator_internal(child);
		}
		return child;
	}

	Expression* parse_single_expression_or_error(Node* parent)
	{
		auto expr = parse_single_expression(parent);
		if (expr != 0) {
			return expr;
		}
		log_error_range_offset("Expected Single Expression", 0);
		expr = allocate_base<AST::Expression>(parent, AST::Node_Type::EXPRESSION);
		expr->type = Expression_Type::ERROR_EXPR;
		PARSE_SUCCESS(expr);
	}

	Expression* parse_priority_level(Expression* expr, int priority_level, Dynamic_Array<Binop_Link>* links, int* index)
	{
		while (*index < links->size)
		{
			auto& link = (*links)[*index];
			auto op_prio = binop_priority(link.binop);
			if (op_prio > priority_level) {
				expr = parse_priority_level(expr, priority_level + 1, links, index);
			}
			else if (op_prio == priority_level)
			{
				*index = *index + 1;
				Expression* result = allocate_base<Expression>(0, Node_Type::EXPRESSION);
				result->type = Expression_Type::BINARY_OPERATION;
				result->options.binop.type = link.binop;
				result->options.binop.left = expr;
				result->options.binop.right = parse_priority_level(link.expr, priority_level + 1, links, index);
				result->options.binop.left->base.parent = &result->base;
				result->options.binop.right->base.parent = &result->base;
				expr = result;

				Token* token = get_token_by_index(link.token_index);
				auto& range = result->base.range;
				range.start = text_index_make(token->line, token->start);
				range.end = text_index_make(token->line, token->end);
				node_calculate_bounding_range(AST::upcast(result->options.binop.left));
				node_calculate_bounding_range(AST::upcast(result));
			}
			else {
				break;
			}
		}
		return expr;
	}

	Expression* parse_expression(Node* parent)
	{
		CHECKPOINT_SETUP;
		Expression* start_expr = parse_single_expression(parent);
		if (start_expr == 0) return 0;

		Dynamic_Array<Binop_Link> links = dynamic_array_create<Binop_Link>();
		SCOPE_EXIT(dynamic_array_destroy(&links));
		while (true)
		{
			Binop_Link link;
			link.binop = Binop::INVALID;
			link.token_index = parser.pos;
			switch (get_token()->type)
			{
			case Token_Type::PLUS: link.binop = Binop::ADDITION; break;
			case Token_Type::MINUS: link.binop = Binop::SUBTRACTION; break;
			case Token_Type::ASTERIX:link.binop = Binop::MULTIPLICATION; break;
			case Token_Type::SLASH:link.binop = Binop::DIVISION; break;
			case Token_Type::PERCENTAGE:link.binop = Binop::MODULO; break;
			case Token_Type::AND:link.binop = Binop::AND; break;
			case Token_Type::OR:link.binop = Binop::OR; break;
			case Token_Type::GREATER_THAN:link.binop = Binop::GREATER; break;
			case Token_Type::GREATER_EQUAL:link.binop = Binop::GREATER_OR_EQUAL; break;
			case Token_Type::LESS_THAN:link.binop = Binop::LESS; break;
			case Token_Type::LESS_EQUAL:link.binop = Binop::LESS_OR_EQUAL; break;
			case Token_Type::EQUALS:link.binop = Binop::EQUAL; break;
			case Token_Type::NOT_EQUALS:link.binop = Binop::NOT_EQUAL; break;
			case Token_Type::POINTER_EQUALS:link.binop = Binop::POINTER_EQUAL; break;
			case Token_Type::POINTER_NOT_EQUALS:link.binop = Binop::POINTER_NOT_EQUAL; break;
			}
			if (link.binop == Binop::INVALID) {
				break;
			}
			advance_token();
			link.expr = parse_single_expression_or_error(parent);
			dynamic_array_push_back(&links, link);
		}

		// Now build the overload tree
		if (links.size == 0) PARSE_SUCCESS(start_expr);
		int index = 0;
		Expression* result = parse_priority_level(start_expr, 0, &links, &index);
		result->base.parent = parent;
		return result; // INFO: Don't use PARSE SUCCESS, since this would overwrite the token-ranges set by parse_priority_level
	}

	Expression* parse_expression_or_error_expr(Node* parent)
	{
		auto expr = parse_expression(parent);
		if (expr != 0) {
			return expr;
		}
		log_error_range_offset("Expected Expression", 0);
		return create_error_expression(parent);
	}

#undef CHECKPOINT_EXIT
#undef CHECKPOINT_SETUP
#undef PARSE_SUCCESS

    void execute_clean(Compilation_Unit* unit, Identifier_Pool* identifier_pool, Arena* permanent_arena, Arena* temporary_arena)
	{
		auto checkpoint = temporary_arena->make_checkpoint();
		SCOPE_EXIT(checkpoint.rewind());

		// Note: Another arena is required for errors, because the permanent_arena can be rewinded during parsing
		Arena error_arena = Arena::create();
		SCOPE_EXIT(error_arena.destroy());
		parser.errors = DynArray<Error_Message>::create(&error_arena); // Only temporary, we copy at the end

		// Initialize parser
		parser.permanent_arena = permanent_arena;
		parser.temporary_arena = temporary_arena;
		parser.unit = unit;
		parser.error_token = token_make(Token_Type::INVALID, 0, 0, 0);
		parser.predefined_ids = &identifier_pool->predefined_ids;
		parser.pos = 0;

		// Tokenize code
		parser.tokens = tokenize_source_code_and_build_hierarchy(unit->code, temporary_arena, identifier_pool);
		// print_tokens(parser.tokens);

		// Parse root
		AST::Definition* root_def = allocate_base<Definition>(nullptr, Node_Type::DEFINITION);
		root_def->type = Definition_Type::MODULE;
		root_def->options.module.symbol.available = false;
		root_def->options.module.definitions = parse_list_items_as_array<Definition>(upcast(root_def), wrapper_parse_definition);
		root_def->base.range = text_range_make(text_index_make(0, 0), text_index_make_line_end(unit->code, unit->code->line_count - 1));
		root_def->base.bounding_range = root_def->base.range;
		unit->root = &root_def->options.module;

		// Copy errors from tmp arena to permanent arena
		unit->parser_errors = permanent_arena->allocate_array<Error_Message>(parser.errors.size);
		memory_copy(unit->parser_errors.data, parser.errors.buffer.data, sizeof(Error_Message) * parser.errors.size);
	}

	// AST queries based on Token-Indices
	DynArray<Text_Range> ast_base_get_section_token_range(Source_Code* code, AST::Node* base, Node_Section section, Arena* arena)
	{
		auto range = base->range;
		DynArray<Text_Range> ranges = DynArray<Text_Range>::create(arena);

		switch (section)
		{
		case Node_Section::NONE: break;
		case Node_Section::WHOLE:
		{
			if (base->type == AST::Node_Type::EXPRESSION && downcast<AST::Expression>(base)->type == AST::Expression_Type::FUNCTION_CALL) {
				ranges.push_back(base->bounding_range);
				break;
			}
			ranges.push_back( range);
			break;
		}
		case Node_Section::WHOLE_NO_CHILDREN:
		{
			Text_Range sub_range;
			sub_range.start = range.start;
			int index = 0;
			// Note: This operates under the assumption that base_get_child returns children in the correct order
			//       which e.g. isn't true for things like modules or code-block with multiple child types
			auto child = AST::base_get_child(base, index);
			while (child != 0)
			{
				auto child_range = child->range;
				if (text_index_equal(sub_range.start, child_range.start))
				{
					sub_range.end = child_range.start;
					// Extra check, as bounding range may differ from normal range (E.g. child starts before parent range)
					if (text_index_in_order(sub_range.start, sub_range.end) == 1) {
						ranges.push_back(sub_range);
					}
				}
				sub_range.start = child_range.end;

				index += 1;
				child = AST::base_get_child(base, index);
			}
			if (text_index_equal(sub_range.start, range.end))
			{
				sub_range.end = range.end;
				// Extra check, as bounding range may differ from normal range
				if (text_index_in_order(sub_range.start, sub_range.end)) {
					ranges.push_back(sub_range);
				}
			}
			if (ranges.size == 0) {
				ranges.push_back(range);
			}
			break;
		}
		case Node_Section::IDENTIFIER:
		{
			int token_index = 0;
			DynArray<Token> tokens = tokenize_partial_code(code, base->range.start, arena, token_index, true, false);
			for (int i = token_index; i < tokens.size; i++) {
				Token& token = tokens[i];
				if (token.type == Token_Type::IDENTIFIER) {
					ranges.push_back(text_range_make(text_index_make(token.line, token.start), text_index_make(token.line, token.end)));
					break;
				}
			}
			break;
		}
		case Node_Section::ENCLOSURE:
		{
			int token_index = 0;
			DynArray<Token> tokens = tokenize_partial_code(code, base->range.start, arena, token_index, true, false);

			int start_index = -1;
			for (int i = token_index; i < tokens.size; i++) {
				Token& token = tokens[i];
				auto token_class = token_type_get_class(token.type);
				if (token_class == Token_Class::LIST_START || token_class == Token_Class::LIST_END) {
					start_index = i;
					break;
				}
			}
			if (start_index == -1) {
				break;
			}

			// Find start/end parenthesis
			ivec2 start_end = tokens_get_parenthesis_range(tokens, start_index, tokens[start_index].type, arena);

			// Convert to text-ranges
			if (start_end.x != -1) {
				Token& t = tokens[start_end.x];
				ranges.push_back(text_range_make(text_index_make(t.line, t.start), text_index_make(t.line, t.end)));
			}
			if (start_end.y != -1) {
				Token& t = tokens[start_end.y];
				ranges.push_back(text_range_make(text_index_make(t.line, t.start), text_index_make(t.line, t.end)));
			}

			break;
		}
		case Node_Section::KEYWORD:
		{
			int token_index = 0;
			DynArray<Token> tokens = tokenize_partial_code(code, base->range.start, arena, token_index, true, false);
			for (int i = token_index; i < tokens.size; i++) {
				Token& token = tokens[i];
				if (token_type_is_keyword(token.type)) {
					ranges.push_back(text_range_make(text_index_make(token.line, token.start), text_index_make(token.line, token.end)));
					break;
				}
			}
			break;
		}
		case Node_Section::FIRST_TOKEN: 
		{
			int token_index = 0;
			DynArray<Token> tokens = tokenize_partial_code(code, base->range.start, arena, token_index, true, false);
			if (token_index < tokens.size) {
				Token& token = tokens[token_index];
				ranges.push_back(text_range_make(text_index_make(token.line, token.start), text_index_make(token.line, token.end)));
			}
			break;
		}
		case Node_Section::END_TOKEN: {
			int token_index = 0;
			DynArray<Token> tokens = tokenize_partial_code(code, base->range.end, arena, token_index, true, false);
			if (token_index < tokens.size) {
				Token& token = tokens[token_index];
				if (!(token.line == base->range.end.line && token.start == base->range.end.character)) {
					ranges.push_back(text_range_make(text_index_make(token.line, token.start), text_index_make(token.line, token.end)));
					break;
				}
			}
			if (token_index -1 > 0 && token_index - 1 < tokens.size) {
				Token& token = tokens[token_index - 1];
				ranges.push_back(text_range_make(text_index_make(token.line, token.start), text_index_make(token.line, token.end)));
			}
			break;
		}
		default: panic("");
		}

		// For handling empty ranges
		if (ranges.size == 0) {
			Text_Range range;
			range.start = base->range.start;
			range.end = base->range.start;
			ranges.push_back(range);
		}

		return ranges;
	}
}
