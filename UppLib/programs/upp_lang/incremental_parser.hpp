#pragma once

#include "compilation_data.hpp"
#include "tokenizer.hpp"

namespace NEW_AST
{
	struct AST;
	struct Expression;
	struct Node;

	enum class Node_Type : u8
	{
		// Definitions
		DEFINITION_MODULE,
		DEFINITION_FUNCTION,
		DEFINITION_VALUE_VARIABLE,
		DEFINITION_VALUE_GLOBAL,
		DEFINITION_VALUE_COMPTIME,

		// Statements
		STATEMENT_DEFINITION,
		STATEMENT_ASSIGNMENT,
		STATEMENT_EXPRESSION,

		// Expressions
		EXPRESSION_FUNCTION_CALL,
		EXPRESSION_PATH_LOOKUP,
		EXPRESSION_LITERAL,
		EXPRESSION_UNOP,
		EXPRESSION_BINOP,

		// Misc
		PATH_LOOKUP,
		SYMBOL_NODE, // Either part of symbol-lookup or of a definition
        CODE_BLOCK,  
        SIGNATURE,   // Function signature
		PARAMETER,
		ARGUMENT,
		CALL_NODE
	};

    enum class Binop_Type
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
        INVALID,
    };

    enum class Unop_Type
    {
        NOT,                  // !
        NEGATE,               // -
        ADDRESS_OF,           // -*
        DEREFERENCE,          // -&
    };

	union Node_Info
	{
		String* id;
		Binop_Type binop_type;
		Unop_Type unop_type;
	};

	struct Node_List
	{
		AST* ast;
		int child_start_index;
		int size;

		Node at(int index);
	};

	struct Definition;

	struct Definition_List
	{
		AST* ast;
		int child_start_index;
		int size;

		Definition at(int index);
	};

	struct Expression_List
	{
		AST* ast;
		int child_start_index;
		int size;

		Expression at(int index);
	};

	struct Node
	{
		AST* ast;
		int index;

		Node_Type type();
		Node parent();
		Node_List children();

		template<typename T> 
		T downcast()
		{
			T result;
			result.ast = ast;
			result.index = index;
			result.check_type_valid();
			return result;
		}
		void check_type_valid();
	};

	struct Symbol_Node
	{
		Node node;

		void check_type_valid();
	};

	struct Definition_Module
	{
		Node node;

		Symbol_Node symbol_node();
		Definition_List definitions();
		void check_type_valid();
	};

	struct Definition_Function
	{
		Node node;

		Symbol_Node symbol_node();
		Definition_List definitions();
		void check_type_valid();
	};

	// Note: SOA (Structure-of-Arrays) datatype
	struct AST
	{
		Arena* arena;
		void* buffer;
		int size;
		int capacity;

		static AST create(Arena* arena);
		void reserve(int new_capacity);
		Node get_root();

		// Helpers
		Node_Type*	get_node_type(int node_index);
		int*		get_parent_index(int node_index);
		int*		get_child_start_index(int node_index);
		int*		get_child_count(int node_index);
		Node_Info*	get_node_info(int node_index);
		Text_Range* get_text_range(int node_index);
		Text_Range* get_bounding_range(int node_index);
	};

}