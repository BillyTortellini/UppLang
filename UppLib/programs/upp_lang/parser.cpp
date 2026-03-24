#include "parser.hpp"

#include "ast.hpp"
#include "syntax_editor.hpp"
#include "compiler.hpp"
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

DynArray<Token> tokenize_source_code_and_build_hierarchy(Source_Code* code, Arena* arena, Identifier_Pool* id_pool)
{
	String string_buffer = string_create(256);
	SCOPE_EXIT(string_destroy(&string_buffer));

	DynArray<Token> tokens = DynArray<Token>::create(arena, 256);
	tokens.push_back(token_make(Token_Type::BLOCK_START, 0, 0, 0));

	// First pass
	int indentation = 0;
	Text_Index last_text_pos = text_index_make(0, 0);
	bool concatenate_to_last_line = false;
	for (int bundle_index = 0; bundle_index < code->bundles.size; bundle_index += 1)
	{
		Line_Bundle& bundle = code->bundles[bundle_index];
		for (int in_bundle_index = 0; in_bundle_index < bundle.lines.size; in_bundle_index += 1)
		{
			Source_Line& line = bundle.lines[in_bundle_index];
			int line_index = in_bundle_index + bundle.first_line_index;

			bool next_concatenate = false;
			SCOPE_EXIT(concatenate_to_last_line = next_concatenate);
			bool line_is_empty = true;
			int line_indentation = 0;
			{
				int leading_whitespace_count = 0;
				for (int i = 0; i < line.text.size; i += 1) 
				{
					char c = line.text[i];
					if (c == '/' && i + 1 < line.text.size && line.text[i + 1] == '/') {
						line_is_empty = true;
						break;
					}

					if (c == ' ') {
						leading_whitespace_count += 1;
					}
					else {
						line_is_empty = false;
						break;
					}
				}
				line_indentation = leading_whitespace_count / 4;
			}
			if (line_is_empty) {
				continue;
			}

			// Insert Block-Start/End tokens
			bool added_block_end = false;
			while (indentation != line_indentation && !concatenate_to_last_line)
			{
				while (tokens.last().type == Token_Type::LINE_END) {
					tokens.size -= 1;
				}
				
				if (line_indentation > indentation) {
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

			// Tokenize line
			int token_count_before = tokens.size;
			tokenizer_tokenize_single_line(line.text, &tokens, line_index, true);
			if (tokens.last().type == Token_Type::CONCATENATE_LINES) {
				tokens.size -= 1;
				next_concatenate = true;
			}
			else {
				Token& last_token = tokens.last();
				last_text_pos = text_index_make(last_token.line, last_token.end);
				tokens.push_back(token_make(Token_Type::LINE_END, last_text_pos.character, last_text_pos.character, last_text_pos.line));
			}

			// Parse string literals and identifiers
			for (int i = token_count_before; i < tokens.size; i++)
			{
				Token& token = tokens[i];

				if (token.type == Token_Type::IDENTIFIER) {
					token.options.string_value = identifier_pool_add(id_pool, string_create_substring_static(&line.text, token.start, token.end));
					continue;
				}
				else if (token.type == Token_Type::LITERAL_STRING) {
					auto substring = string_create_substring_static(&line.text, token.start, token.end);
					string_reset(&string_buffer);
					tokenizer_parse_string_literal(substring, &string_buffer);
					token.options.string_value = identifier_pool_add(id_pool, string_buffer);
					continue;
				}
			}
		}
	}

	// Add missing block-end tokens
	while (tokens.last().type == Token_Type::LINE_END) {
		tokens.size -= 1;
	}
	for (int i = 0; i < indentation + 1; i += 1) {
		tokens.push_back(token_make(Token_Type::BLOCK_END, last_text_pos.character, last_text_pos.character, last_text_pos.line));
	}
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

	struct Parse_State
	{
		int pos; // Token-Index
		int allocated_count;
		int error_count;
	};

	struct Parser
	{
		Arena* arena;
		DynArray<Token> tokens;
		Compilation_Unit* unit;
		Predefined_IDs* predefined_ids;
		Token error_token;
		Parse_State state;
	};

	// Globals (Thread-Local so i don't have to rewrite everything, but this doesn't work well with fiber-pool!)
	static thread_local Parser parser;

	// Parser Functions
	void parser_rollback(Parse_State checkpoint)
	{
		auto& nodes = parser.unit->allocated_nodes;
		auto& errors = parser.unit->parser_errors;

		assert(checkpoint.allocated_count <= nodes.size, "");
		for (int i = checkpoint.allocated_count; i < nodes.size; i++) {
			AST::base_destroy(nodes[i]);
		}
		dynamic_array_rollback_to_size(&nodes, checkpoint.allocated_count);
		dynamic_array_rollback_to_size(&errors, checkpoint.error_count);
		parser.state = checkpoint;
	}

	template<typename T>
	T* allocate_base(Node* parent, Node_Type type)
	{
		// PERF: A block allocator could probably be used here
		auto result = new T;
		memory_zero(result);
		dynamic_array_push_back(&parser.unit->allocated_nodes, &result->base);
		parser.state.allocated_count = parser.unit->allocated_nodes.size;

		Node* base = &result->base;
		base->parent = parent;
		base->type = type;
		auto& token = parser.tokens[parser.state.pos];
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
		dynamic_array_push_back(&parser.unit->parser_errors, err);
		parser.state.error_count = parser.unit->parser_errors.size;
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
		log_error(msg, parser.state.pos, end_token);
	}

	void log_error_range_offset(const char* msg, int token_count) {
		log_error_to_pos(msg, parser.state.pos + token_count);
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
		return get_token_by_index(parser.state.pos + offset);
	}

	void advance_token() {
		parser.state.pos += 1;
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
		return test_token(Token_Type::CURLY_BRACE_OPEN) || test_token(Token_Type::BLOCK_START) || test_token(Token_Type::EXPLICIT_BLOCK);
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
		Token& token = parser.tokens[parser.state.pos - 1];
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

	void find_next_follow_block()
	{
		if (on_follow_block()) {
			return;
		}

		DynArray<Token_Type> parenthesis_stack = DynArray<Token_Type>::create(parser.arena);
		auto& pos = parser.state.pos;
		int start = pos;
		bool found = false;
		while (true)
		{
			// Exit conditions
			if (on_follow_block()) {
				found = true;
				break;
			}
			if (test_token(Token_Type::BLOCK_END) || test_token(Token_Type::LINE_END) || test_token(Token_Type::EXPLICIT_BLOCK) ||
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

		static List_Iter create()
		{
			List_Iter iter;
			iter.is_valid = false;
			iter.type = get_token()->type;
			iter.last_item_start = parser.state.pos;

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
			case Token_Type::EXPLICIT_BLOCK:
			{
				if (test_token(Token_Type::BLOCK_START, 1)) {
					advance_token();
					iter.is_valid = true;
				}
				else if (test_token(Token_Type::BLOCK_START, Token_Type::IDENTIFIER, 1)) {
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

			iter.last_item_start = parser.state.pos;
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
			case Token_Type::PARENTHESIS_OPEN: return current_type == Token_Type::COMMA;
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
			SCOPE_EXIT(last_item_start = parser.state.pos);

			if (on_seperator())
			{
				if (last_item_start == parser.state.pos) {
					log_error("Expected item", last_item_start, last_item_start);
				}
				advance_token();
				return;
			}
			else if (on_end_token()) {
				if (last_item_start == parser.state.pos) {
					log_error("Expected item", last_item_start, last_item_start);
				}
				return;
			}

			// Otherwise find continuation and log error (Either end of the parenthesis or next seperator)
			int& pos = parser.state.pos;
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
				auto checkpoint = parser.arena->make_checkpoint();
				DynArray<Token_Type> parenthesis_stack = DynArray<Token_Type>::create(parser.arena);
				parenthesis_stack.push_back(type);
				SCOPE_EXIT(parser.arena->rewind_to_checkpoint(checkpoint));
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
			int& pos = parser.state.pos;
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
	typedef void (*add_list_item_to_parent_fn)(Node* parent, Node* child);

	void parse_list_items(Node* parent, list_item_parse_fn parse_fn, add_list_item_to_parent_fn add_to_parent_fn)
	{
		if (get_token()->type == Token_Type::EXPLICIT_BLOCK || token_type_get_class(get_token()->type) != Token_Class::LIST_START) {
			return;
		}

		List_Iter iter = List_Iter::create();
		while (!iter.on_end_token())
		{
			Node* parsed_item = parse_fn(parent);
			if (parsed_item != nullptr) {
				add_to_parent_fn(parent, parsed_item);
			}
			iter.goto_next();
		}
		iter.finish();
	}

	void parse_comma_seperated_list_items(Node* parent, list_item_parse_fn parse_fn, add_list_item_to_parent_fn add_to_parent_fn, stopper_fn stopper_fn)
	{
		auto on_end_token = [&](bool in_parenthesis) -> bool 
		{
			auto t = get_token()->type;
			if (!in_parenthesis) {
				if (stopper_fn(t) || token_type_get_class(t) == Token_Class::LIST_END || t == Token_Type::SEMI_COLON) {
					return true;
				}
			}

			return
				t == Token_Type::LINE_END ||
				t == Token_Type::BLOCK_END ||
				t == Token_Type::BLOCK_START ||
				t == Token_Type::SEMI_COLON ||
				t == Token_Type::EXPLICIT_BLOCK;
		};

		if (on_end_token(false)) {
			return;
		}

		int pos = parser.state.pos;
		while (true)
		{
			// Parse item
			Node* node = parse_fn(parent);
			if (node != nullptr) 
			{
				add_to_parent_fn(parent, node);
				if (on_end_token(false)) {
					return;
				}
			}
			else {
				log_error("Could not parse item", parser.state.pos, parser.state.pos + 1);
			}

			// Goto next comma or list end
			int start_pos = pos;
			bool found_end = false;
			{
				auto checkpoint = parser.arena->make_checkpoint();
				SCOPE_EXIT(parser.arena->rewind_to_checkpoint(checkpoint));
				DynArray<Token_Type> parenthesis_stack = DynArray<Token_Type>::create(parser.arena);

				while (!test_token(Token_Type::COMMA))
				{
					if (on_end_token(parenthesis_stack.size > 0)) {
						found_end = true;
						break;
					}

					Token_Type type = get_token()->type;
					Token_Class token_class = token_type_get_class(type);
					if (token_class == Token_Class::LIST_START) {
						parenthesis_stack.push_back(type);
					}
					else if (token_class == Token_Class::LIST_END) {
						if (token_type_get_partner(parenthesis_stack.last()) == type) {
							parenthesis_stack.size -= 1;
						}
					}
					pos += 1;
				}
			}

			if (start_pos > pos) {
				log_error("Could not parse tokens", start_pos, pos);
			}
			if (found_end) {
				return;
			}

			assert(test_token(Token_Type::COMMA), "");
			advance_token();
		}
	}

	void find_next_comma_item()
	{
		auto helper_on_goal = [&]() {
			return
				test_token(Token_Type::COMMA) ||
				test_token(Token_Type::BLOCK_START) ||
				test_token(Token_Type::EXPLICIT_BLOCK) ||
				test_token(Token_Type::CURLY_BRACE_CLOSED) ||
				test_token(Token_Type::PARENTHESIS_CLOSED) ||
				test_token(Token_Type::BLOCK_END) ||
				test_token(Token_Type::LINE_END) ||
				test_token(Token_Type::SEMI_COLON);
		};
		DynArray<Token_Type> parenthesis_stack = DynArray<Token_Type>::create(parser.arena);
		auto& pos = parser.state.pos;
		int start = pos;
		bool found = false;
		while (true)
		{
			// Exit conditions
			if (on_follow_block()) {
				found = true;
				break;
			}
			if (test_token(Token_Type::BLOCK_END) || test_token(Token_Type::LINE_END) || test_token(Token_Type::EXPLICIT_BLOCK) ||
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



	// MACROS
#define CHECKPOINT_SETUP \
        if (parser.state.pos >= parser.tokens.size) {return nullptr;}\
        auto checkpoint = parser.state;\
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
	void code_block_add_child(AST::Node* parent, AST::Node* child);
	Node* wrapper_parse_statement_or_context_change(AST::Node* parent);

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

		if (!test_token(Token_Type::IDENTIFIER) && !test_token(Token_Type::TILDE))
		{
			return nullptr;
		}

		CHECKPOINT_SETUP;

		Path_Lookup* path = allocate_base<Path_Lookup>(parent, Node_Type::PATH_LOOKUP);
		path->parts = dynamic_array_create<Symbol_Lookup*>(1);
		path->is_dot_call_lookup = false;

		if (test_token(Token_Type::TILDE)) {
			Symbol_Lookup* lookup = allocate_base<Symbol_Lookup>(upcast(path), Node_Type::SYMBOL_LOOKUP);
			dynamic_array_push_back(&path->parts, lookup);
			lookup->is_root_module = true;
			lookup->name = ids.root_module;
			advance_token();
			node_finalize_range(upcast(lookup));
		}

		while (true)
		{
			// Add symbol-lookup Node
			Symbol_Lookup* lookup = allocate_base<Symbol_Lookup>(upcast(path), Node_Type::SYMBOL_LOOKUP);
			dynamic_array_push_back(&path->parts, lookup);

			// Check if we actually have an identifier, or if it's an 'empty' path, like "A~B~"
			// INFO: We have this empty path so that the syntax editor is able to show that a node is missing here!
			if (test_token(Token_Type::IDENTIFIER)) {
				lookup->name = get_token()->options.string_value;
				advance_token();
			}
			else {
				log_error("Expected identifier", parser.state.pos - 1, parser.state.pos); // Put error on the ~
				lookup->name = ids.empty_string;
			}
			node_finalize_range(upcast(lookup));

			if (test_token(Token_Type::TILDE)) {
				advance_token();
			}
			else {
				break;
			}
		}

		path->last = path->parts[path->parts.size - 1];
		PARSE_SUCCESS(path);
	}

	Path_Lookup* parse_path_lookup_or_error(Node* parent)
	{
		auto& ids = parser.predefined_ids;

		if (!test_token(Token_Type::IDENTIFIER) && !test_token(Token_Type::TILDE))
		{
			Path_Lookup* path = allocate_base<Path_Lookup>(parent, Node_Type::PATH_LOOKUP);
			path->parts = dynamic_array_create<Symbol_Lookup*>(1);
			path->is_dot_call_lookup = false;
			Symbol_Lookup* lookup = allocate_base<Symbol_Lookup>(upcast(path), Node_Type::SYMBOL_LOOKUP);
			dynamic_array_push_back(&path->parts, lookup);
			path->last = lookup;
			lookup->name = ids->empty_string;
			node_finalize_range(upcast(lookup));
			node_finalize_range(upcast(path));
			return path;
		}
		return parse_path_lookup(parent);
	}

	// Always returns node, even if no arguments were found...
	// So one should always test for ( or { before calling this...
	Call_Node* parse_call_node(Node* parent)
	{
		auto call_node = allocate_base<Call_Node>(parent, Node_Type::CALL_NODE);
		call_node->arguments = dynamic_array_create<Argument*>();
		call_node->subtype_initializers = dynamic_array_create<Subtype_Initializer*>();
		call_node->uninitialized_tokens = dynamic_array_create<Expression*>();

		if (!test_token(Token_Type::PARENTHESIS_OPEN)) {
			log_error_range_offset("Expected parenthesis for parameters", 0);
			PARSE_SUCCESS(call_node);
		}

		auto add_to_arguments = [](Node* parent, Node* child)
			{
				auto args = downcast<Call_Node>(parent);
				if (child->type == Node_Type::ARGUMENT) {
					dynamic_array_push_back(&args->arguments, downcast<Argument>(child));
				}
				else if (child->type == Node_Type::SUBTYPE_INITIALIZER) {
					dynamic_array_push_back(&args->subtype_initializers, downcast<Subtype_Initializer>(child));
				}
				else if (child->type == Node_Type::EXPRESSION) {
					auto expr = downcast<Expression>(child);
					assert(expr->type == Expression_Type::ERROR_EXPR, "");
					dynamic_array_push_back(&args->uninitialized_tokens, expr);
				}
				else {
					panic("");
				}
			};
		parse_list_items(
			upcast(call_node),
			parse_argument_or_subtype_initializer_or_uninitialzed,
			add_to_arguments
		);

		PARSE_SUCCESS(call_node);
	}

	namespace Block_Items
	{
		Node* parse_module_item(AST::Node* parent);
		Enum_Member_Node* parse_enum_member(AST::Node* parent);
		Switch_Case* parse_switch_case(AST::Node* parent);
		Structure_Member_Node* parse_struct_member(AST::Node* parent);
		Definition_Symbol* parse_definition_symbol(AST::Node* parent);
		Node* parse_statement_or_context_change(AST::Node* parent);
	};

	// The wrappers are just type-less versions for use in parse_list
	Node* wrapper_parse_module_item(AST::Node* parent) { return Block_Items::parse_module_item(parent); }
	Node* wrapper_parse_enum_member(AST::Node* parent) { return upcast(Block_Items::parse_enum_member(parent)); }
	Node* wrapper_parse_switch_case(AST::Node* parent) { return upcast(Block_Items::parse_switch_case(parent)); }
	Node* wrapper_parse_struct_member(AST::Node* parent) { return upcast(Block_Items::parse_struct_member(parent)); }
	Node* wrapper_parse_definition_symbol(AST::Node* parent) { return upcast(Block_Items::parse_definition_symbol(parent)); }
	Node* wrapper_parse_statement_or_context_change(AST::Node* parent) { return Block_Items::parse_statement_or_context_change(parent); }
	Node* wrapper_parse_expression_or_error(AST::Node* parent) { return upcast(parse_expression_or_error_expr(parent)); }
	Node* wrapper_parse_parameter(AST::Node* parent) { return upcast(parse_parameter(parent)); }

	void code_block_add_child(AST::Node* parent, AST::Node* child)
	{
		AST::Code_Block* block = downcast<AST::Code_Block>(parent);
		if (child->type == AST::Node_Type::STATEMENT) {
			dynamic_array_push_back(&block->statements, downcast<AST::Statement>(child));
		}
		else if (child->type == AST::Node_Type::CONTEXT_CHANGE) {
			dynamic_array_push_back(&block->custom_operators, downcast<AST::Custom_Operator_Node>(child));
		}
		else {
			panic("");
		}
	}

	void module_add_child(AST::Node* parent, AST::Node* child)
	{
		AST::Module* module = downcast<AST::Module>(parent);
		if (child->type == AST::Node_Type::DEFINITION) {
			dynamic_array_push_back(&module->definitions, downcast<AST::Definition>(child));
		}
		else if (child->type == AST::Node_Type::IMPORT) {
			dynamic_array_push_back(&module->import_nodes, downcast<Import>(child));
		}
		else if (child->type == AST::Node_Type::EXTERN_IMPORT) {
			dynamic_array_push_back(&module->extern_imports, downcast<Extern_Import>(child));
		}
		else if (child->type == AST::Node_Type::CONTEXT_CHANGE) {
			dynamic_array_push_back(&module->custom_operators, downcast<Custom_Operator_Node>(child));
		}
		else {
			panic("");
		}
	}



	// Block Parsing
	namespace Block_Items
	{
		Definition_Symbol* parse_definition_symbol(Node* parent)
		{
			if (!test_token(Token_Type::IDENTIFIER)) {
				return nullptr;
			}
			auto node = allocate_base<Definition_Symbol>(parent, Node_Type::DEFINITION_SYMBOL);
			node->name = get_token()->options.string_value;
			advance_token();
			PARSE_SUCCESS(node);
		}

		AST::Extern_Import* parse_extern_import(AST::Node* parent)
		{
			auto& ids = *parser.predefined_ids;

			auto start = parser.state.pos;
			if (!test_token(Token_Type::EXTERN)) {
				return 0;
			}

			CHECKPOINT_SETUP;
			auto result = allocate_base<Extern_Import>(parent, Node_Type::EXTERN_IMPORT);
			advance_token();

			if (test_token(Token_Type::IDENTIFIER))
			{
				String* id = get_token()->options.string_value;
				if (id == ids.function) {
					result->type = Extern_Type::FUNCTION;
				}
				else if (id == ids.global) {
					result->type = Extern_Type::GLOBAL;
				}
				else if (id == ids.lib) {
					result->type = Extern_Type::COMPILER_SETTING;
					result->options.setting.type = Extern_Compiler_Setting::LIBRARY;
				}
				else if (id == ids.lib_dir) {
					result->type = Extern_Type::COMPILER_SETTING;
					result->options.setting.type = Extern_Compiler_Setting::LIBRARY_DIRECTORY;
				}
				else if (id == ids.source) {
					result->type = Extern_Type::COMPILER_SETTING;
					result->options.setting.type = Extern_Compiler_Setting::SOURCE_FILE;
				}
				else if (id == ids.header) {
					result->type = Extern_Type::COMPILER_SETTING;
					result->options.setting.type = Extern_Compiler_Setting::HEADER_FILE;
				}
				else if (id == ids.header_dir) {
					result->type = Extern_Type::COMPILER_SETTING;
					result->options.setting.type = Extern_Compiler_Setting::INCLUDE_DIRECTORY;
				}
				else if (id == ids.definition) {
					result->type = Extern_Type::COMPILER_SETTING;
					result->options.setting.type = Extern_Compiler_Setting::DEFINITION;
				}
				else {
					log_error_range_offset("Identifier after extern must be one of: function, global, source, lib, lib_dir", 1);
					result->type = Extern_Type::INVALID;
					return result;
				}
			}
			else if (test_token(Token_Type::STRUCT)) {
				result->type = Extern_Type::STRUCT;
			}
			else {
				log_error("Expected extern-type after extern keyword!", start, start + 1);
				result->type = Extern_Type::INVALID;
				return result;
			}
			advance_token();

			switch (result->type)
			{
			case Extern_Type::FUNCTION:
			case Extern_Type::GLOBAL:
			{
				if (!test_token(Token_Type::IDENTIFIER)) {
					log_error_range_offset("Expected identifier", 1);
					result->type = Extern_Type::INVALID;
					return result;
				}
				String* id = get_token()->options.string_value;
				advance_token();

				if (!test_token(Token_Type::COLON)) {
					log_error_range_offset("Expected : after identifier", 1);
					result->type = Extern_Type::INVALID;
					return result;
				}
				advance_token();

				AST::Expression* expr = parse_expression_or_error_expr(upcast(result));

				if (result->type == Extern_Type::FUNCTION) {
					result->options.function.id = id;
					result->options.function.type_expr = expr;
				}
				else {
					result->options.global.id = id;
					result->options.global.type_expr = expr;
				}
				break;
			}
			case Extern_Type::STRUCT: {
				result->options.struct_type_expr = parse_expression_or_error_expr(upcast(result));
				break;
			}
			case Extern_Type::COMPILER_SETTING:
			{
				if (!test_token(Token_Type::LITERAL_STRING)) {
					log_error_range_offset("Expected string literal", 1);
					result->type = Extern_Type::INVALID;
					return result;
				}

				String* path = get_token()->options.string_value;
				advance_token();
				result->options.setting.value = path;
				break;
			}
			default: panic("");
			}

			return result;
		}

		// Block Item functions
		Import* parse_import(Node* parent)
		{
			auto& ids = *parser.predefined_ids;

			CHECKPOINT_SETUP;
			if (!test_token(Token_Type::IMPORT, 0)) {
				CHECKPOINT_EXIT;
				return 0;
			}

			auto result = allocate_base<Import>(parent, Node_Type::IMPORT);
			result->alias_name = optional_make_failure<Definition_Symbol*>();
			result->options.path = nullptr;
			result->import_type = Import_Type::SYMBOLS;
			result->operator_type = Import_Operator::SINGLE_SYMBOL;
			advance_token();

			if (test_token(Token_Type::IDENTIFIER))
			{
				auto id = get_token()->options.string_value;
				if (id == ids.operators) {
					result->import_type = Import_Type::OPERATORS;
					advance_token();
				}
				else if (id == ids.dot_calls) {
					result->import_type = Import_Type::DOT_CALLS;
					advance_token();
				}
			}

			// Special path for import ~* and import ~**, because they are custom tokens
			if (test_token(Token_Type::TILDE_STAR) || test_token(Token_Type::TILDE_STAR_STAR))
			{
				result->operator_type =
					test_token(Token_Type::TILDE_STAR) ? Import_Operator::MODULE_IMPORT : Import_Operator::MODULE_IMPORT_TRANSITIVE;

				Path_Lookup* path_lookup = allocate_base<Path_Lookup>(upcast(result), Node_Type::PATH_LOOKUP);
				path_lookup->is_dot_call_lookup = false;
				path_lookup->parts = dynamic_array_create<Symbol_Lookup*>(1);

				Symbol_Lookup* symbol_lookup = allocate_base<Symbol_Lookup>(upcast(path_lookup), Node_Type::SYMBOL_LOOKUP);
				symbol_lookup->is_root_module = true;
				symbol_lookup->name = ids.root_module;
				advance_token();

				dynamic_array_push_back(&path_lookup->parts, symbol_lookup);
				path_lookup->last = symbol_lookup;
				node_finalize_range(upcast(symbol_lookup));
				node_finalize_range(upcast(path_lookup));

				result->options.path = path_lookup;
				PARSE_SUCCESS(result);
			}

			// Check if it's a file import
			if (test_token(Token_Type::LITERAL_STRING)) {
				result->operator_type = Import_Operator::FILE_IMPORT;
				result->options.file_name = get_token()->options.string_value;
				advance_token();
			}
			else
			{
				result->operator_type = Import_Operator::SINGLE_SYMBOL;
				result->options.path = parse_path_lookup_or_error(upcast(result));
				if (test_token(Token_Type::TILDE_STAR)) {
					result->operator_type = Import_Operator::MODULE_IMPORT;
					advance_token();
				}
				else if (test_token(Token_Type::TILDE_STAR_STAR)) {
					result->operator_type = Import_Operator::MODULE_IMPORT_TRANSITIVE;
					advance_token();
				}
			}

			if (test_token(Token_Type::AS) && test_token(Token_Type::IDENTIFIER, 1)) {
				advance_token();
				result->alias_name = optional_make_success(parse_definition_symbol(upcast(result)));
			}
			PARSE_SUCCESS(result);
		}

		Custom_Operator_Node* parse_context_change(Node* parent)
		{
			auto& ids = *parser.predefined_ids;

			Custom_Operator_Type custom_op_type;
			switch (get_token()->type)
			{
			case Token_Type::ADD_ARRAY_ACCESS: custom_op_type = Custom_Operator_Type::ARRAY_ACCESS; break;
			case Token_Type::ADD_CAST: custom_op_type = Custom_Operator_Type::CAST; break;
			case Token_Type::ADD_UNOP: custom_op_type = Custom_Operator_Type::UNOP; break;
			case Token_Type::ADD_BINOP: custom_op_type = Custom_Operator_Type::BINOP; break;
			case Token_Type::ADD_ITERATOR: custom_op_type = Custom_Operator_Type::ITERATOR; break;
			default: return nullptr;
			}

			CHECKPOINT_SETUP;
			auto result = allocate_base<Custom_Operator_Node>(parent, Node_Type::CONTEXT_CHANGE);
			advance_token();
			result->type = custom_op_type;
			result->call_node = parse_call_node(upcast(result));
			PARSE_SUCCESS(result);
		}

		Definition* parse_definition(Node* parent)
		{
			if (!test_token(Token_Type::IDENTIFIER) && !test_token(Token_Type::PARENTHESIS_OPEN, Token_Type::IDENTIFIER) && 
				!test_token(Token_Type::DEFINE_COMPTIME) &&
				!test_token(Token_Type::COLON) && ! test_token(Token_Type::DEFINE_INFER)) {
				return nullptr;
			}

			CHECKPOINT_SETUP;

			auto result = allocate_base<Definition>(parent, AST::Node_Type::DEFINITION);
			result->is_comptime = false;
			result->symbols = dynamic_array_create<Definition_Symbol*>();
			result->types   = dynamic_array_create<AST::Expression*>();
			result->values  = dynamic_array_create<AST::Expression*>();

			auto add_to_symbols = [](Node* parent, Node* child) {
				assert(parent->type == Node_Type::DEFINITION && child->type == Node_Type::DEFINITION_SYMBOL, "");
				dynamic_array_push_back(&downcast<Definition>(parent)->symbols, downcast<Definition_Symbol>(child));
			};
			auto add_to_types = [](Node* parent, Node* child) {
				assert(parent->type == Node_Type::DEFINITION && child->type == Node_Type::EXPRESSION, "");
				dynamic_array_push_back(&downcast<Definition>(parent)->types, downcast<Expression>(child));
			};
			auto add_to_values = [](Node* parent, Node* child) {
				assert(parent->type == Node_Type::DEFINITION && child->type == Node_Type::EXPRESSION, "");
				dynamic_array_push_back(&downcast<Definition>(parent)->values, downcast<Expression>(child));
			};

			if (test_token(Token_Type::IDENTIFIER)) {
				dynamic_array_push_back(&result->symbols, parse_definition_symbol(upcast(result)));
			}
			else if (test_token(Token_Type::PARENTHESIS_OPEN)) 
			{
				parse_list_items(upcast(result), wrapper_parse_definition_symbol, add_to_symbols);
			}
			else if (test_token(Token_Type::COLON) || test_token(Token_Type::DEFINE_COMPTIME) || test_token(Token_Type::DEFINE_COMPTIME)) {
				Definition_Symbol* error_sym = allocate_base<Definition_Symbol>(parent, AST::Node_Type::DEFINITION_SYMBOL);
				error_sym->name = parser.predefined_ids->invalid_symbol_name;
				node_finalize_range(upcast(error_sym));
				dynamic_array_push_back(&result->symbols, error_sym);
			}

			// Check if there is a colon :, or a := or an ::
			if (test_token(Token_Type::COLON))
			{
				advance_token();
				if (test_token(Token_Type::PARENTHESIS_OPEN)) {
					parse_list_items(upcast(result), wrapper_parse_expression_or_error, add_to_types);
				}
				else {
					dynamic_array_push_back(&result->types, parse_expression_or_error_expr(upcast(result)));
				}

				if (test_token(Token_Type::ASSIGN)) { // : ... =
					result->is_comptime = false;
					advance_token();
				}
				else if (test_token(Token_Type::COLON)) { // : ... :
					result->is_comptime = true;
					advance_token();
				}
				else {
					PARSE_SUCCESS(result);
				}
			}
			else if (test_token(Token_Type::DEFINE_COMPTIME)) { // x :: 
				advance_token();
				result->is_comptime = true;
			}
			else if (test_token(Token_Type::DEFINE_INFER)) { // x :=
				advance_token();
				result->is_comptime = false;
			}
			else {
				CHECKPOINT_EXIT;
			}

			if (test_token(Token_Type::PARENTHESIS_OPEN)) {
				parse_list_items(upcast(result), wrapper_parse_expression_or_error, add_to_values);
			}
			else {
				dynamic_array_push_back(&result->values, parse_expression_or_error_expr(upcast(result)));
			}

			PARSE_SUCCESS(result);
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
				result->options.subtype_members = dynamic_array_create<Structure_Member_Node*>();
				auto add_to_subtype = [](Node* parent, Node* child) {
					dynamic_array_push_back(&downcast<Structure_Member_Node>(parent)->options.subtype_members, downcast<Structure_Member_Node>(child));
					};
				parse_list_items(upcast(result), wrapper_parse_struct_member, add_to_subtype);
				PARSE_SUCCESS(result);
			}

			result->options.expression = parse_expression_or_error_expr(upcast(result));
			PARSE_SUCCESS(result);
		}

		AST::Node* parse_module_item(AST::Node* parent)
		{
			auto definition = parse_definition(parent);
			if (definition != 0) {
				return AST::upcast(definition);
			}
			auto import_node = parse_import(parent);
			if (import_node != 0) {
				return AST::upcast(import_node);
			}
			auto context_change = parse_context_change(parent);
			if (context_change != 0) {
				return AST::upcast(context_change);
			}
			auto extern_import = parse_extern_import(parent);
			if (extern_import != 0) {
				return AST::upcast(extern_import);
			}
			return nullptr;
		}

		Switch_Case* parse_switch_case(Node* parent)
		{
			auto result = allocate_base<Switch_Case>(parent, Node_Type::SWITCH_CASE);
			result->value = optional_make_failure<Expression*>();
			result->variable_definition = optional_make_failure<AST::Definition_Symbol*>();

			// Check for default case
			if (test_token(Token_Type::DEFAULT)) {
				advance_token();
			}
			else
			{
				result->value.available = true;
				result->value.value = parse_expression_or_error_expr(upcast(result));
				if (test_token(Token_Type::ARROW))
				{
					advance_token();
					if (test_token(Token_Type::IDENTIFIER)) {
						result->variable_definition = optional_make_success(parse_definition_symbol(upcast(result)));
					}
					else {
						log_error_range_offset("Expected identifier after arrow", 0);
					}
				}
			}

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
			if (expr == 0) {
				CHECKPOINT_EXIT;
			}

			auto add_to_left_side = [](Node* parent, Node* child) {
				dynamic_array_push_back(&downcast<Statement>(parent)->options.assignment.left_side, downcast<Expression>(child));
			};
			auto add_to_right_side = [](Node* parent, Node* child) {
				dynamic_array_push_back(&downcast<Statement>(parent)->options.assignment.right_side, downcast<Expression>(child));
			};

			//if (test_token(Token_Type::COMMA))
			//{
			//	// Assume that it's a multi-assignment
			//	result->type = Statement_Type::ASSIGNMENT;
			//	result->options.assignment.left_side = dynamic_array_create<Expression*>(1);
			//	result->options.assignment.right_side = dynamic_array_create<Expression*>(1);
			//	dynamic_array_push_back(&result->options.assignment.left_side, expr);
			//	advance_token();

			//	// Parse remaining left_side expressions
			//	parse_list_items(upcast(result), wrapper_parse_expression_or_error, add_to_left_side);

			//	// Check if assignment found, otherwise error
			//	if (!test_token(Token_Type::ASSIGN)) {
			//		CHECKPOINT_EXIT;
			//	}
			//	advance_token();

			//	// Parse right side
			//	parse_list_items(upcast(result), wrapper_parse_expression_or_error, add_to_right_side);
			//	PARSE_SUCCESS(result);
			//}
			if (test_token(Token_Type::ASSIGN))
			{
				result->type = Statement_Type::ASSIGNMENT;
				result->options.assignment.left_side = dynamic_array_create<Expression*>(1);
				result->options.assignment.right_side = dynamic_array_create<Expression*>(1);
				dynamic_array_push_back(&result->options.assignment.left_side, expr);
				advance_token();

				// Parse right side
				dynamic_array_push_back(&result->options.assignment.right_side, parse_expression_or_error_expr(upcast(result)));
				// parse_list_items(upcast(result), wrapper_parse_expression_or_error, add_to_right_side);
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

			switch (get_token()->type)
			{
			case Token_Type::IMPORT:
			{
				auto import = parse_import(upcast(result));
				if (import == 0) {
					CHECKPOINT_EXIT;
				}
				result->type = Statement_Type::IMPORT;
				result->options.import_node = import;
				PARSE_SUCCESS(result);
			}
			case Token_Type::PARENTHESIS_OPEN:
			case Token_Type::IDENTIFIER:
			case Token_Type::DEFINE_INFER:
			case Token_Type::DEFINE_COMPTIME:
			case Token_Type::COLON:
			{
				auto definition = parse_definition(&result->base);
				if (definition != 0) {
					result->type = Statement_Type::DEFINITION;
					result->options.definition = definition;
					PARSE_SUCCESS(result);
				}
				break;
			}
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
					implicit_else_block->statements = dynamic_array_create<Statement*>(1);
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

					dynamic_array_push_back(&implicit_else_block->statements, new_if_stat);
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
				else if (test_token(Token_Type::PARENTHESIS_OPEN, Token_Type::IDENTIFIER) && 
					(test_token(Token_Type::COLON, 2) || test_token(Token_Type::DEFINE_INFER, 2))) 
				{
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
					result->options.foreach_loop.loop_variable_definition = parse_definition_symbol(upcast(result));
					if (test_token(Token_Type::COMMA)) {
						advance_token();
						result->options.foreach_loop.index_variable_definition.available = true;
						result->options.foreach_loop.index_variable_definition.value = parse_definition_symbol(upcast(result));
					}
					assert(test_token(Token_Type::IN_KEYWORD), "");
					advance_token();
					result->options.foreach_loop.expression = parse_expression_or_error_expr(upcast(result));
					result->options.foreach_loop.body_block = parse_code_block(upcast(result), upcast(result->options.foreach_loop.loop_variable_definition));
				}
				else // For loop
				{
					assert(test_token(Token_Type::PARENTHESIS_OPEN), "");
					List_Iter iter = List_Iter::create();
					assert(iter.is_valid && !iter.on_end_token(), "");

					result->options.for_loop.loop_variable_definition = parse_definition_symbol(upcast(result));
					if (test_token(Token_Type::COLON)) 
					{
						result->options.for_loop.loop_variable_type.available = true;
						result->options.for_loop.loop_variable_type.value = parse_expression_or_error_expr(upcast(result));

						if (test_token(Token_Type::ASSIGN)) {
							advance_token();
							result->options.for_loop.initial_value = parse_expression_or_error_expr(upcast(result));
						}
						else {
							log_error_range_offset("Expected = token", 0);
							result->options.for_loop.initial_value = create_error_expression(upcast(result));
						}
					}
					else 
					{
						assert(test_token(Token_Type::DEFINE_INFER), "");
						advance_token();
						result->options.for_loop.loop_variable_type.available = false;
						result->options.for_loop.initial_value = parse_expression_or_error_expr(upcast(result));
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
				code_block->statements = dynamic_array_create<Statement*>(1);
				code_block->custom_operators = dynamic_array_create<Custom_Operator_Node*>();
				code_block->block_id = optional_make_failure<String*>();

				dynamic_array_push_back(&code_block->statements, statement);
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
				switch_stat.cases = dynamic_array_create<Switch_Case*>(1);
				find_next_follow_block();
				switch_stat.label = parse_block_label_or_use_related_node_id(upcast(switch_stat.condition));
				auto add_to_switch = [](Node* parent, Node* child) {
					dynamic_array_push_back(&downcast<Statement>(parent)->options.switch_statement.cases, downcast<Switch_Case>(child));
				};
				parse_list_items(upcast(result), wrapper_parse_switch_case, add_to_switch);
				PARSE_SUCCESS(result);
			}
			case Token_Type::DELETE_KEYWORD: {
				advance_token();
				result->type = Statement_Type::DELETE_STATEMENT;
				result->options.delete_expr = parse_expression_or_error_expr(&result->base);
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
			}

			if (on_follow_block())
			{
				result->type = Statement_Type::BLOCK;
				result->options.block = parse_code_block(upcast(result), nullptr);
				PARSE_SUCCESS(result);
			}


			// Otherwise try to parse expression or assignment statement
			// Delete result, which is an allocated statement node
			result->type = Statement_Type::IMPORT; // So that rollback doesn't read uninitialized values
			parser_rollback(checkpoint);

			return parse_assignment_or_expression_statement(parent);
		}

		AST::Node* parse_statement_or_context_change(Node* parent)
		{
			// Check for context change
			auto context_change = parse_context_change(parent);
			if (context_change != 0) {
				return upcast(context_change);
			}

			return upcast(parse_statement(parent));
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
			if (test_token(Token_Type::DEFINE_COMPTIME)) {
				advance_token();
				result->value = optional_make_success(parse_expression_or_error_expr(&result->base));
			}
			PARSE_SUCCESS(result);
		}
	};

	// Used by code-block and switch statement
	Optional<String*> parse_block_label_or_use_related_node_id(AST::Node* related_node)
	{
		Optional<String*> result = optional_make_failure<String*>();
		if (test_token(Token_Type::EXPLICIT_BLOCK, Token_Type::IDENTIFIER)) {
			result = optional_make_success(get_token(1)->options.string_value);
		}
		else if (related_node != nullptr)
		{
			if (related_node->type == AST::Node_Type::EXPRESSION) {
				auto expr = downcast<AST::Expression>(related_node);
				if (expr->type == AST::Expression_Type::PATH_LOOKUP) {
					result = optional_make_success<String*>(expr->options.path_lookup->last->name);
				}
			}
			else if (related_node->type == AST::Node_Type::DEFINITION_SYMBOL) {
				result = optional_make_success<String*>(downcast<AST::Definition_Symbol>(related_node)->name);
			}
		}
		return result;
	}

	// Always returns success, but if there is no follow block, there are errors
	Code_Block* parse_code_block(Node* parent, AST::Node* related_node)
	{
		auto result = allocate_base<Code_Block>(parent, Node_Type::CODE_BLOCK);
		result->statements = dynamic_array_create<Statement*>();
		result->custom_operators = dynamic_array_create<Custom_Operator_Node*>();

		find_next_follow_block();
		result->block_id = parse_block_label_or_use_related_node_id(related_node);
		if (on_follow_block())
		{
			parse_list_items(
				upcast(result), wrapper_parse_statement_or_context_change, code_block_add_child
			);
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
		if (!test_token(Token_Type::DOT, Token_Type::IDENTIFIER, Token_Type::ASSIGN) &&
			!test_token(Token_Type::DOT, Token_Type::ASSIGN))
		{
			return 0;
		}

		CHECKPOINT_SETUP;
		auto result = allocate_base<Subtype_Initializer>(parent, Node_Type::SUBTYPE_INITIALIZER);
		advance_token();

		// Parser name if available
		result->name.available = false;
		if (test_token(Token_Type::IDENTIFIER)) {
			result->name = optional_make_success(get_token(0)->options.string_value);
			advance_token();
		}

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
		result->default_value.available = false;
		result->type = optional_make_failure<AST::Expression*>();
		result->default_value = optional_make_failure<AST::Expression*>();

		// Parse identifier and optional mutators
		if (test_token(Token_Type::DOLLAR)) {
			result->is_comptime = true;
			advance_token();
		}
		if (!test_token(Token_Type::IDENTIFIER)) CHECKPOINT_EXIT;
		result->name = get_token(0)->options.string_value;
		advance_token();

		if (!test_token(Token_Type::COLON)) {
			PARSE_SUCCESS(result);
		}

		advance_token(); // Skip :
		result->type = optional_make_success(parse_expression_or_error_expr((Node*)result));

		if (test_token(Token_Type::ASSIGN)) {
			advance_token();
			result->default_value = optional_make_success(parse_expression_or_error_expr((Node*)result));
		}
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
		case Token_Type::SUBTRACTION:
		case Token_Type::NOT:
		{
			Unop unop = test_token(Token_Type::SUBTRACTION) ? Unop::NEGATE : Unop::NOT;
			advance_token();
			result->type = Expression_Type::UNARY_OPERATION;
			result->options.unop.type = unop;
			result->options.unop.expr = parse_single_expression_or_error(&result->base);
			PARSE_SUCCESS(result);
		}
		case Token_Type::MULTIPLY:
		case Token_Type::OPTIONAL_POINTER:
		{
			result->type = Expression_Type::POINTER_TYPE;
			result->options.pointer_type.is_optional = test_token(Token_Type::OPTIONAL_POINTER);
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
			if (test_token(Token_Type::ARROW)) {
				advance_token();
				instanciate.return_type = optional_make_success(parse_expression_or_error_expr(upcast(result)));
			}
			PARSE_SUCCESS(result);
		}
		case Token_Type::GET_OVERLOAD:
		case Token_Type::GET_OVERLOAD_POLY:
		{
			bool is_poly = test_token(Token_Type::GET_OVERLOAD_POLY);
			int start = parser.state.pos;

			advance_token();
			result->type = Expression_Type::GET_OVERLOAD;
			result->options.get_overload.is_poly = is_poly;
			result->options.get_overload.path = optional_make_failure<AST::Path_Lookup*>();
			result->options.get_overload.arguments = dynamic_array_create<AST::Get_Overload_Argument*>();

			AST::Path_Lookup* path = parse_path_lookup(upcast(result));
			if (path != 0) {
				result->options.get_overload.path = optional_make_success(path);
			}
			else {
				log_error("#get_overload expected a path_lookup", start, start + 1);
			}

			if (test_token(Token_Type::PARENTHESIS_OPEN))
			{
				auto add_to_expr = [](AST::Node* parent, AST::Node* child) {
					AST::Expression* expr = downcast<AST::Expression>(parent);
					AST::Get_Overload_Argument* arg = downcast<AST::Get_Overload_Argument>(child);
					dynamic_array_push_back(&expr->options.get_overload.arguments, arg);
					};
				parse_list_items(upcast(result), parse_overload_argument, add_to_expr);
			}
			else {
				log_error("#get_overload expected '(' after path_lookup", start, start + 1);
			}

			if (test_token(Token_Type::ARROW)) {
				AST::Get_Overload_Argument* return_type_argument = allocate_base<AST::Get_Overload_Argument>(upcast(result), AST::Node_Type::GET_OVERLOAD_ARGUMENT);
				advance_token();
				return_type_argument->id = ids.return_type_name;
				return_type_argument->type_expr = optional_make_success(parse_expression_or_error_expr(upcast(return_type_argument)));
				node_finalize_range(upcast(return_type_argument));
				dynamic_array_push_back(&result->options.get_overload.arguments, return_type_argument);
			}

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
				call_node->arguments = dynamic_array_create<Argument*>(1);
				call_node->subtype_initializers = dynamic_array_create<Subtype_Initializer*>();
				call_node->uninitialized_tokens = dynamic_array_create<Expression*>();

				Argument* argument = allocate_base<Argument>(upcast(call_node), Node_Type::ARGUMENT);;
				argument->name.available = false;
				argument->value = parse_single_expression_or_error(upcast(argument));
				dynamic_array_push_back(&call_node->arguments, argument);

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
			if (!test_token(Token_Type::IDENTIFIER, 1)) {
				CHECKPOINT_EXIT;
			}

			result->type = Expression_Type::PATTERN_VARIABLE;
			advance_token();
			result->options.pattern_variable_name = get_token()->options.string_value;
			advance_token();
			PARSE_SUCCESS(result);
		}
		case Token_Type::DOT:
		{
			advance_token();
			if (test_token(Token_Type::FUNCTION_KEYWORD))
			{
				result->type = Expression_Type::FUNCTION;
				result->options.function.signature = optional_make_failure<AST::Expression*>();
				advance_token();

				if (on_follow_block()) {
					result->options.function.body.is_expression = false;
					result->options.function.body.block = parse_code_block(&result->base, 0);
					PARSE_SUCCESS(result);
				}
				result->options.function.body.is_expression = true;
				result->options.function.body.expr = parse_expression_or_error_expr(upcast(result));
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
				init.values = dynamic_array_create<Expression*>(1);
				auto add_to_values = [](Node* parent, Node* child) {
					dynamic_array_push_back(&downcast<Expression>(parent)->options.array_initializer.values, downcast<Expression>(child));
					};
				parse_list_items(upcast(result), wrapper_parse_expression_or_error, add_to_values);
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
					log_error("Missing member name", parser.state.pos - 1, parser.state.pos);
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
		case Token_Type::LITERAL_NULL:
		{
			result->type = Expression_Type::LITERAL_READ;
			result->options.literal_read.type = Literal_Type::NULL_VAL;
			result->options.literal_read.options.null_ptr = nullptr;
			advance_token();
			PARSE_SUCCESS(result);
		}
		case Token_Type::NEW:
		{
			result->type = Expression_Type::NEW_EXPR;
			result->options.new_expr.count_expr.available = false;
			advance_token();
			// Note: count expression currently not supported for new
			// if (test_parenthesis_offset('{', 0)) {
			// 	advance_token();
			// 	result->options.new_expr.count_expr = optional_make_success(parse_expression_or_error_expr(&result->base));
			// 	if (!finish_parenthesis<Parenthesis_Type::BRACES>()) CHECKPOINT_EXIT;
			// }
			result->options.new_expr.type_expr = parse_expression_or_error_expr(&result->base);
			PARSE_SUCCESS(result);
		}
		case Token_Type::STRUCT:
		case Token_Type::UNION:
		{
			result->type = Expression_Type::STRUCTURE_TYPE;
			result->options.structure.members = dynamic_array_create<Structure_Member_Node*>();
			result->options.structure.parameters = dynamic_array_create<Parameter*>();
			result->options.structure.is_union = test_token(Token_Type::UNION);
			advance_token();

			// Parse struct parameters
			if (test_token(Token_Type::PARENTHESIS_OPEN))
			{
				auto add_parameter = [](Node* parent, Node* child) {
					dynamic_array_push_back(&downcast<Expression>(parent)->options.structure.parameters, downcast<Parameter>(child));
					};
				parse_list_items(upcast(result), wrapper_parse_parameter, add_parameter);
			}

			if (!on_follow_block()) {
				log_error_range_offset("Expected follow block", 0);
				PARSE_SUCCESS(result);
			}

			auto add_member = [](Node* parent, Node* child) {
				dynamic_array_push_back(&downcast<Expression>(parent)->options.structure.members, downcast<Structure_Member_Node>(child));
				};
			parse_list_items(upcast(result), wrapper_parse_struct_member, add_member);
			PARSE_SUCCESS(result);
		}
		case Token_Type::ENUM:
		{
			result->type = Expression_Type::ENUM_TYPE;
			result->options.enum_members = dynamic_array_create<Enum_Member_Node*>();
			advance_token();

			if (!on_follow_block()) {
				log_error_range_offset("Expected follow block", 0);
				PARSE_SUCCESS(result);
			}

			auto add_member = [](Node* parent, Node* child) {
				dynamic_array_push_back(&downcast<Expression>(parent)->options.enum_members, downcast<Enum_Member_Node>(child));
				};
			parse_list_items(upcast(result), wrapper_parse_enum_member, add_member);
			PARSE_SUCCESS(result);
		}
		case Token_Type::MODULE:
		{
			auto module = allocate_base<Module>(&result->base, Node_Type::MODULE);
			module->definitions = dynamic_array_create<Definition*>(1);
			module->import_nodes = dynamic_array_create<Import*>(1);
			module->custom_operators = dynamic_array_create<Custom_Operator_Node*>(1);
			advance_token();

			result->type = Expression_Type::MODULE;
			result->options.module = module;

			find_next_follow_block();
			if (!on_follow_block()) {
				log_error_range_offset("Expected follow block", 0);
			}
			else {
				parse_list_items(upcast(module), wrapper_parse_module_item, module_add_child);
			}

			node_finalize_range(upcast(module));
			PARSE_SUCCESS(result);
		}
		case Token_Type::FUNCTION_POINTER_KEYWORD:
		case Token_Type::FUNCTION_KEYWORD:
		{
			AST::Expression* signature = result;
			if (test_token(Token_Type::FUNCTION_KEYWORD)) {
				result->type = Expression_Type::FUNCTION;
				signature = allocate_base<AST::Expression>(upcast(result), Node_Type::EXPRESSION);
				result->options.function.signature = optional_make_success(signature);
			}
			else {
				signature = result;
			}

			// Parse signature
			advance_token();
			signature->type = Expression_Type::FUNCTION_SIGNATURE;
			signature->options.signature_parameters = dynamic_array_create<Parameter*>();
			if (test_token(Token_Type::PARENTHESIS_OPEN))
			{
				auto add_parameter = [](Node* parent, Node* child) {
					dynamic_array_push_back(&downcast<Expression>(parent)->options.signature_parameters, downcast<Parameter>(child));
					};
				parse_list_items(upcast(signature), wrapper_parse_parameter, add_parameter);

				if (test_token(Token_Type::ARROW))
				{
					advance_token();
					auto return_param = allocate_base<Parameter>(parent, Node_Type::PARAMETER);
					return_param->default_value = optional_make_failure<AST::Expression*>();
					return_param->is_comptime = false;
					return_param->is_return_type = true;
					return_param->name = ids.return_type_name;
					return_param->type = optional_make_success(parse_expression_or_error_expr(upcast(result)));
					dynamic_array_push_back(&signature->options.signature_parameters, return_param);
				}
			}
			node_finalize_range(upcast(signature));

			// Parse function body if the expression is not just a type
			if (result->type == Expression_Type::FUNCTION) {
				result->options.function.body.is_expression = false;
				result->options.function.body.block = parse_code_block(upcast(result), nullptr);
			}
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
				init.values = dynamic_array_create<Expression*>(1);
				auto add_to_values = [](Node* parent, Node* child) {
					dynamic_array_push_back(&downcast<Expression>(parent)->options.array_initializer.values, downcast<Expression>(child));
					};
				parse_list_items(upcast(result), wrapper_parse_expression_or_error, add_to_values);
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
					log_error("Missing member name", parser.state.pos - 1, parser.state.pos);
					result->options.member_access.name = ids.empty_string;
				}
				PARSE_SUCCESS(result);
			}
			CHECKPOINT_EXIT;
		}
		case Token_Type::DOT_CALL:
		{
			advance_token();

			bool is_cast = false;
			AST::Call_Node* call_node = nullptr;
			if (test_token(Token_Type::CAST))
			{
				is_cast = true;
				result->type = Expression_Type::CAST;
				result->options.cast.is_dot_call = true;
				result->options.cast.call_node = nullptr;
				advance_token();
				if (!test_token(Token_Type::PARENTHESIS_OPEN)) {
					call_node = allocate_base<Call_Node>(upcast(result), Node_Type::CALL_NODE);
					call_node->arguments = dynamic_array_create<Argument*>();
					call_node->subtype_initializers = dynamic_array_create<Subtype_Initializer*>();
					call_node->uninitialized_tokens = dynamic_array_create<Expression*>();
				}
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
			}

			// Parse arguments (If not cast without parenthesis)
			if (call_node == nullptr) {
				call_node = parse_call_node(upcast(result));
			}

			// Add expr as first unnamed argument
			AST::Argument* argument = allocate_base<Argument>(upcast(call_node), AST::Node_Type::ARGUMENT);
			argument->name = optional_make_failure<String*>();
			argument->value = child;
			argument->base.range = child->base.range;
			argument->base.bounding_range = child->base.bounding_range;
			dynamic_array_insert_ordered(&call_node->arguments, argument, 0);
			node_finalize_range(upcast(call_node)); // For bounding range, as the call node now also includes the first argument (Not sure if I want this)

			if (is_cast) {
				result->options.cast.call_node = call_node;
			}
			else {
				result->options.call.call_node = call_node;
			}

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
			link.token_index = parser.state.pos;
			switch (get_token()->type)
			{
			case Token_Type::ADDITION: link.binop = Binop::ADDITION; break;
			case Token_Type::SUBTRACTION: link.binop = Binop::SUBTRACTION; break;
			case Token_Type::MULTIPLY:link.binop = Binop::MULTIPLICATION; break;
			case Token_Type::DIVISON:link.binop = Binop::DIVISION; break;
			case Token_Type::MODULO:link.binop = Binop::MODULO; break;
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

	void execute_clean(Compilation_Unit* unit, Identifier_Pool* identifier_pool, Arena* arena)
	{
		auto checkpoint = arena->make_checkpoint();
		SCOPE_EXIT(arena->rewind_to_checkpoint(checkpoint));

		// Reset allocation data
		{
			for (int i = 0; i < unit->allocated_nodes.size; i++) {
				AST::base_destroy(unit->allocated_nodes[i]);
			}
			dynamic_array_reset(&unit->allocated_nodes);
			dynamic_array_reset(&unit->parser_errors);
			unit->root = 0;
		}

		// Initialize parser
		parser.arena = arena;
		parser.unit = unit;
		parser.error_token = token_make(Token_Type::INVALID, 0, 0, 0);
		parser.predefined_ids = &identifier_pool->predefined_ids;

		// Tokenize code
		parser.tokens = tokenize_source_code_and_build_hierarchy(unit->code, arena, identifier_pool);
		// print_tokens(parser.tokens);

		// Initialize state
		parser.state.pos = 0;
		parser.state.allocated_count = parser.unit->allocated_nodes.size;
		parser.state.error_count = parser.unit->parser_errors.size;

		// Parse root
		auto root = allocate_base<Module>(0, Node_Type::MODULE);
		parser.unit->root = root;
		root->definitions = dynamic_array_create<Definition*>();
		root->import_nodes = dynamic_array_create<Import*>();
		root->custom_operators = dynamic_array_create<Custom_Operator_Node*>();
		parse_list_items(upcast(root), wrapper_parse_module_item, module_add_child);
		root->base.range = text_range_make(text_index_make(0, 0), text_index_make_line_end(unit->code, unit->code->line_count - 1));
		root->base.bounding_range = root->base.range;

		unit->root = root;
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
