#include "ast.hpp"

#include "compiler.hpp"

namespace AST
{
	Node* base_get_child(Node* node, int child_index)
	{
		int index = 0;
#define FILL(x) { if (child_index == index) {return &x->base;} else {index += 1;}}
#define FILL_OPTIONAL(x) if (x.available) {FILL(x.value);}
#define FILL_ARRAY(x) {if (child_index < index + x.size) {return &x[child_index - index]->base;} else {index += x.size;}}
		switch (node->type)
		{
		case Node_Type::SWITCH_CASE: {
			auto sw_case = (Switch_Case*)node;
			FILL_OPTIONAL(sw_case->value);
			FILL_OPTIONAL(sw_case->variable_definition);
			FILL(sw_case->block);
			break;
		}
		case Node_Type::PARAMETER: {
			auto param = (Parameter*)node;
			FILL(param->symbol);
			FILL_OPTIONAL(param->type);
			break;
		}
		case Node_Type::SIGNATURE: {
			auto signature = (Signature*)node;
			FILL_ARRAY(signature->parameters);
			break;
		}
		case Node_Type::GET_OVERLOAD_ARGUMENT: {
			auto arg = (Get_Overload_Argument*)node;
			FILL_OPTIONAL(arg->type_expr);
			break;
		}
		case Node_Type::PATH_LOOKUP: {
			auto path = (Path_Lookup*)node;
			FILL_ARRAY(path->parts);
			break;
		}
		case Node_Type::ENUM_MEMBER: {
			auto enum_member = (Enum_Member_Node*)node;
			FILL_OPTIONAL(enum_member->value);
			break;
		}
		case Node_Type::CALL_NODE: {
			auto args = (Call_Node*)node;
			FILL_ARRAY(args->arguments);
			FILL_ARRAY(args->subtype_initializers);
			FILL_ARRAY(args->uninitialized_tokens);
			break;
		}
		case Node_Type::STRUCT_MEMBER: {
			auto member = (Structure_Member_Node*)node;
			if (member->is_expression) {
				FILL(member->options.expression);
			}
			else {
				FILL_ARRAY(member->options.subtype_members);
			}
			break;
		}
		case Node_Type::ARGUMENT: {
			auto arg = (Argument*)node;
			FILL(arg->value);
			break;
		}
		case Node_Type::CODE_BLOCK: {
			auto block = (Code_Block*)node;
			FILL_ARRAY(block->statements);
			break;
		}
		case Node_Type::SYMBOL_NODE: {
			break;
		}
		case Node_Type::DEFINITION: 
		{
			auto def = (Definition*)node;
			switch (def->type)
			{
			case Definition_Type::VARIABLE:
			case Definition_Type::GLOBAL:
			case Definition_Type::CONSTANT: 
			{
				auto& value = def->options.value;
				FILL(value.symbol);
				FILL_OPTIONAL(value.datatype_expr);
				FILL_OPTIONAL(value.value_expr);
				break;
			}
			case Definition_Type::FUNCTION: 
			{
				auto& fn = def->options.function;
				FILL(fn.symbol);
				FILL(fn.signature);
				if (fn.body.is_expression) {
					FILL(fn.body.expr);
				}
				else {
					FILL(fn.body.block);
				}
				break;
			}
			case Definition_Type::STRUCT: 
			{
				auto& struct_node = def->options.structure;
				FILL(struct_node.symbol);
				FILL(struct_node.signature);
				FILL_ARRAY(struct_node.members);
				break;
			}
			case Definition_Type::ENUM:
			{
				auto& enum_node = def->options.enumeration;
				FILL(enum_node.symbol);
				FILL_ARRAY(enum_node.members);
				break;
			}
			case Definition_Type::IMPORT:
			{
				auto& import = def->options.import;
				if (import.operator_type != Import_Operator::FILE_IMPORT) {
					FILL(import.options.path);
				}
				FILL_OPTIONAL(import.alias_name);
				break;
			}
			case Definition_Type::EXTERN:
			{
				auto extern_import = def->options.extern_import;
				switch (extern_import.type)
				{
				case Extern_Type::FUNCTION: {
					FILL(extern_import.options.function.symbol);
					FILL(extern_import.options.function.type_expr);
					break;
				}
				case Extern_Type::GLOBAL: {
					FILL(extern_import.options.global_lookup);
					break;
				}
				case Extern_Type::STRUCT: {
					FILL(extern_import.options.struct_type_lookup);
					break;
				}
				case Extern_Type::COMPILER_SETTING: {
					break;
				}
				default: panic("");
				}
				break;
			}
			case Definition_Type::MODULE:
			{
				auto& module = def->options.module;
				FILL_OPTIONAL(module.symbol);
				FILL_ARRAY(module.definitions);
				break;
			}
			case Definition_Type::CUSTOM_OPERATOR:
			{
				auto op = def->options.custom_operator;
				FILL(op.call_node);
				break;
			}
			default: panic("");
			}
			break;
		}
		case Node_Type::SUBTYPE_INITIALIZER: {
			auto init = (Subtype_Initializer*)node;
			FILL(init->call_node);
			break;
		}
		case Node_Type::EXPRESSION:
		{
			auto expr = (Expression*)node;
			switch (expr->type)
			{
			case Expression_Type::BINARY_OPERATION: {
				auto& binop = expr->options.binop;
				FILL(binop.left);
				FILL(binop.right);
				break;
			}
			case Expression_Type::UNARY_OPERATION: {
				auto& unop = expr->options.unop;
				FILL(unop.expr);
				break;
			}
			case Expression_Type::PATTERN_VARIABLE: {
				FILL(expr->options.pattern_variable_symbol);
				break;
			}
			case Expression_Type::CAST: {
				auto& cast = expr->options.cast;
				FILL(cast.call_node);
				break;
			}
			case Expression_Type::PATH_LOOKUP: {
				FILL(expr->options.path_lookup);
				break;
			}
			case Expression_Type::LITERAL_READ: {
				break;
			}
			case Expression_Type::ARRAY_ACCESS: {
				auto& access = expr->options.array_access;
				FILL(access.array_expr);
				FILL(access.index_expr);
				break;
			}
			case Expression_Type::MEMBER_ACCESS: {
				auto& access = expr->options.member_access;
				FILL(access.expr);
				break;
			}
			case Expression_Type::BASETYPE_ACCESS: {
				FILL(expr->options.basetype_access_expr);
				break;
			}
			case Expression_Type::SUBTYPE_ACCESS: {
				FILL(expr->options.subtype_access.expr);
				break;
			}
			case Expression_Type::STRUCT_INITIALIZER: {
				auto& init = expr->options.struct_initializer;
				FILL_OPTIONAL(init.type_expr);
				FILL(init.call_node);
				break;
			}
			case Expression_Type::BAKE: {
				auto& body = expr->options.bake_body;
				if (body.is_expression) {
					FILL(body.expr);
				}
				else {
					FILL(body.block)
				}
				break;
			}
			case Expression_Type::INSTANCIATE: {
				auto& instanciate = expr->options.instanciate;
				FILL(instanciate.path_lookup);
				FILL(instanciate.call_node);
				FILL_OPTIONAL(instanciate.return_type);
				break;
			}
			case Expression_Type::GET_OVERLOAD: {
				FILL(expr->options.get_overload.path);
				FILL_ARRAY(expr->options.get_overload.arguments);
				break;
			}
			case Expression_Type::ARRAY_INITIALIZER: {
				auto& init = expr->options.array_initializer;
				FILL_OPTIONAL(init.type_expr);
				FILL_ARRAY(init.values);
				break;
			}
			case Expression_Type::ARRAY_TYPE: {
				auto& array = expr->options.array_type;
				FILL(array.size_expr);
				FILL(array.type_expr);
				break;
			}
			case Expression_Type::SLICE_TYPE: {
				auto& slice = expr->options.slice_type;
				FILL(slice);
				break;
			}
			case Expression_Type::POINTER_TYPE: {
				FILL(expr->options.pointer_type.child_type);
				break;
			}
			case Expression_Type::AUTO_ENUM: {
				break;
			}
			case Expression_Type::INFERRED_FUNCTION:
			{
				auto& body = expr->options.inferred_function_body;
				if (body.is_expression) {
					FILL(body.expr);
				}
				else {
					FILL(body.block)
				}
				break;
			}
			case Expression_Type::ERROR_EXPR: {
				break;
			}
			case Expression_Type::FUNCTION_CALL: {
				auto& call = expr->options.call;
				FILL(call.expr);
				FILL(call.call_node);
				break;
			}
			case Expression_Type::FUNCTION_POINTER_TYPE: {
				FILL(expr->options.function_pointer_signature);
				break;
			}
			default: panic("");
			}
			break;
		}
		case Node_Type::STATEMENT:
		{
			auto stat = (Statement*)node;
			switch (stat->type)
			{
			case Statement_Type::DEFINITION: {
				auto def = stat->options.definition;
				FILL(def);
				break;
			}
			case Statement_Type::BLOCK: {
				auto block = stat->options.block;
				FILL(block);
				break;
			}
			case Statement_Type::ASSIGNMENT: {
				auto ass = stat->options.assignment;
				FILL(ass.left_side);
				FILL(ass.right_side);
				break;
			}
			case Statement_Type::BINOP_ASSIGNMENT: {
				auto ass = stat->options.binop_assignment;
				FILL(ass.left_side);
				FILL(ass.right_side);
				break;
			}
			case Statement_Type::EXPRESSION_STATEMENT: {
				auto expr = stat->options.expression;
				FILL(expr);
				break;
			}
			case Statement_Type::DEFER: {
				auto defer = stat->options.defer_block;
				FILL(defer);
				break;
			}
			case Statement_Type::DEFER_RESTORE: {
				auto defer = stat->options.defer_restore;
				FILL(defer.left_side);
				FILL(defer.right_side);
				break;
			}
			case Statement_Type::IF_STATEMENT: {
				auto if_stat = stat->options.if_statement;
				FILL(if_stat.condition);
				FILL(if_stat.block);
				FILL_OPTIONAL(if_stat.else_block);
				break;
			}
			case Statement_Type::WHILE_STATEMENT: {
				auto while_stat = stat->options.while_statement;
				FILL_OPTIONAL(while_stat.condition);
				FILL(while_stat.block);
				break;
			}
			case Statement_Type::FOREACH_LOOP: {
				auto loop = stat->options.foreach_loop;
				FILL(loop.loop_variable_definition);
				FILL_OPTIONAL(loop.index_variable_definition);
				FILL(loop.expression);
				FILL(loop.body_block);
				break;
			}
			case Statement_Type::FOR_LOOP: {
				auto loop = stat->options.for_loop;
				FILL(loop.loop_variable_definition);
				FILL_OPTIONAL(loop.loop_variable_type);
				FILL(loop.initial_value);
				FILL(loop.condition);
				FILL(loop.increment_statement);
				FILL(loop.body_block);
				break;
			}
			case Statement_Type::BREAK_STATEMENT: {
				break;
			}
			case Statement_Type::CONTINUE_STATEMENT: {
				break;
			}
			case Statement_Type::RETURN_STATEMENT: {
				auto ret = stat->options.return_value;
				FILL_OPTIONAL(ret);
				break;
			}
			case Statement_Type::SWITCH_STATEMENT: {
				FILL(stat->options.switch_statement.condition);
				FILL_ARRAY(stat->options.switch_statement.cases);
				break;
			}
			default: panic("HEY");
			}
			break;
		}
		default: panic("");
		}
		return 0;
#undef FILL
#undef FILL_OPTIONAL
#undef FILL_ARRAY
	}

	void expression_append_to_string(AST::Expression* expr, String* str)
	{
		switch (expr->type)
		{
		case Expression_Type::BINARY_OPERATION: 
		{
			string_append(str, "Binop \"");
			switch (expr->options.binop.type)
			{
			case AST::Binop::ADDITION: string_append(str, "+"); break;
			case AST::Binop::SUBTRACTION: string_append(str, "-"); break;
			case AST::Binop::DIVISION: string_append(str, "/"); break;
			case AST::Binop::MULTIPLICATION: string_append(str, "*"); break;
			case AST::Binop::MODULO: string_append(str, "%"); break;
			case AST::Binop::AND: string_append(str, "&&"); break;
			case AST::Binop::OR: string_append(str, "||"); break;
			case AST::Binop::EQUAL: string_append(str, "=="); break;
			case AST::Binop::NOT_EQUAL: string_append(str, "!="); break;
			case AST::Binop::LESS: string_append(str, "<"); break;
			case AST::Binop::LESS_OR_EQUAL: string_append(str, "<="); break;
			case AST::Binop::GREATER: string_append(str, ">"); break;
			case AST::Binop::GREATER_OR_EQUAL: string_append(str, ">="); break;
			case AST::Binop::POINTER_EQUAL: string_append(str, "*=="); break;
			case AST::Binop::POINTER_NOT_EQUAL: string_append(str, "*!="); break;
			case AST::Binop::INVALID: string_append(str, "INVALID"); break;
			default: panic("");
			}
			string_append(str, "\"");
			break;
		}
		case Expression_Type::UNARY_OPERATION:
		{
			string_append(str, "Unop \""); break;
			switch (expr->options.unop.type)
			{
			case Unop::NOT: string_append(str, "!"); break;
			case Unop::ADDRESS_OF: string_append(str, "*"); break;
			case Unop::DEREFERENCE: string_append(str, "&"); break;
			case Unop::NEGATE: string_append(str, "-"); break;
			case Unop::NULL_CHECK: string_append(str, "?"); break;
			case Unop::OPTIONAL_DEREFERENCE: string_append(str, "-?&"); break;
			default: panic("");
			}
			string_append(str, "Unop \""); break;
			break;
		}
		case Expression_Type::PATTERN_VARIABLE: string_append(str, "Pattern_Variable"); break;
		case Expression_Type::FUNCTION_CALL: string_append_formated(str, "Function Call"); break;
		case Expression_Type::CAST: string_append_formated(str, "Cast"); break;
		case Expression_Type::BAKE: string_append_formated(str, "Bake Expr"); break;
		case Expression_Type::INSTANCIATE: string_append_formated(str, "#instanciate"); break;
		case Expression_Type::GET_OVERLOAD: string_append_formated(str, "#get_overload"); break;
		case Expression_Type::PATH_LOOKUP: string_append_formated(str, "Lookup "); break;
		case Expression_Type::LITERAL_READ: {
			string_append_formated(str, "Literal \"");
			auto& read = expr->options.literal_read;
			switch (read.type) {
			case Literal_Type::BOOLEAN: string_append_formated(str, read.options.boolean ? "true" : "false"); break;
			case Literal_Type::INTEGER: string_append_formated(str, "%d", read.options.int_val); break;
			case Literal_Type::FLOAT_VAL: string_append_formated(str, "%f", read.options.float_val); break;
			case Literal_Type::NULL_VAL: string_append_formated(str, "null"); break;
			case Literal_Type::STRING: string_append_formated(str, "%s", read.options.string->characters); break;
			default: panic("");
			}
			string_append_formated(str, "\"");
			break;
		}
		case Expression_Type::ARRAY_ACCESS: string_append_formated(str, "Array_Access"); break;
		case Expression_Type::MEMBER_ACCESS: string_append_formated(str, "Member_Access"); break;
		case Expression_Type::SUBTYPE_ACCESS: string_append_formated(str, "Subtype_Access"); break;
		case Expression_Type::BASETYPE_ACCESS: string_append_formated(str, "Basetype_Access"); break;
		case Expression_Type::INFERRED_FUNCTION: string_append_formated(str, "Inferred_Function"); break;
		case Expression_Type::FUNCTION_POINTER_TYPE: string_append_formated(str, "Function_Signature"); break;
		case Expression_Type::ARRAY_TYPE: string_append_formated(str, "Array Type"); break;
		case Expression_Type::SLICE_TYPE: string_append_formated(str, "Slice Type"); break;
		case Expression_Type::POINTER_TYPE: {
			if (expr->options.pointer_type.is_optional) {
				string_append_formated(str, "Optional Pointer Type");
			}
			else {
				string_append_formated(str, "Pointer Type");
			}
			break;
		}
		case Expression_Type::ERROR_EXPR: string_append_formated(str, "Error"); break;
		case Expression_Type::STRUCT_INITIALIZER: string_append_formated(str, "Struct Initializer"); break;
		case Expression_Type::ARRAY_INITIALIZER: string_append_formated(str, "Array Initializer"); break;
		case Expression_Type::AUTO_ENUM: string_append_formated(str, "Auto-Enum"); break;
		default: panic("");
		}
	}

	void base_append_to_string(Node* base, String* str)
	{
		switch (base->type)
		{
		case Node_Type::SYMBOL_NODE: {
			string_append_formated(str, "SYMBOL_NODE %s", ((Symbol_Node*)base)->name->characters);
			break;
		}
		case Node_Type::DEFINITION: 
		{
			string_append_formated(str, "DEFINITION ");
			auto def = (Definition*)base;
			switch (def->type)
			{
			case Definition_Type::MODULE: string_append(str, "module"); break;
			case Definition_Type::VARIABLE: string_append(str, "var"); break;
			case Definition_Type::GLOBAL: string_append(str, "global"); break;
			case Definition_Type::CONSTANT: string_append(str, "const"); break; 
			case Definition_Type::FUNCTION: string_append(str, "fn"); break; 
			case Definition_Type::ENUM: string_append(str, "enum"); break; 
			case Definition_Type::CUSTOM_OPERATOR: string_append(str, "operator"); break; 
			case Definition_Type::STRUCT: 
			{
				auto& struct_node = def->options.structure;
				string_append(str, struct_node.is_union ? "union " : "struct "); 
				break;
			}
			case Definition_Type::IMPORT:
			{
				auto& import = def->options.import;
				string_append_formated(str, "IMPORT ");
				if (import.operator_type == Import_Operator::FILE_IMPORT) {
					string_append_formated(str, "\"%s\" ", import.options.file_name->characters);
				}
				else if (import.operator_type == Import_Operator::MODULE_IMPORT) {
					string_append_formated(str, "~* ");
				}
				else if (import.operator_type == Import_Operator::MODULE_IMPORT_TRANSITIVE) {
					string_append_formated(str, "~** ");
				}
				break;
			}
			case Definition_Type::EXTERN: 
			{
				auto& extern_import = def->options.extern_import;

				switch (extern_import.type)
				{
				case Extern_Type::FUNCTION: string_append(str, "fn"); break;
				case Extern_Type::GLOBAL: string_append(str, "global"); break;
				case Extern_Type::COMPILER_SETTING: 
				{
					switch (extern_import.options.setting.type)
					{
					case Extern_Compiler_Setting::DEFINITION: string_append(str, "Macro/Definition");
					case Extern_Compiler_Setting::HEADER_FILE: string_append(str, "Header file");
					case Extern_Compiler_Setting::LIBRARY: string_append(str, "Library");
					case Extern_Compiler_Setting::LIBRARY_DIRECTORY: string_append(str, "Lib-Directory");
					case Extern_Compiler_Setting::INCLUDE_DIRECTORY: string_append(str, "Include-Directory");
					case Extern_Compiler_Setting::SOURCE_FILE: string_append(str, "Source-File");
					default: panic("");
					}
					string_append(str, extern_import.options.setting.value->characters);
					break;
				}
				case Extern_Type::STRUCT: string_append(str, "struct"); break;
				default: panic("");
				}
				break;
			}
			default: panic("");
			}
			break;
		}
		case Node_Type::CALL_NODE: {
			string_append_formated(str, "CALL_NODE");
			break;
		}
		case Node_Type::SUBTYPE_INITIALIZER: {
			auto init = (Subtype_Initializer*)base;
			string_append(str, "SUBTYPE_INIT");
			if (init->name.available) {
				string_append_character(str, ' ');
				string_append(str, init->name.value->characters);
			}
			break;
		}
		case Node_Type::PATH_LOOKUP:
			string_append_formated(str, "PATH_LOOKUP ");
			break;
		case Node_Type::GET_OVERLOAD_ARGUMENT:
			string_append_formated(str, "GET_OVERLOAD_ARG ");
			break;
		case Node_Type::SWITCH_CASE: string_append_formated(str, "SWITCH_CASE"); break;
		case Node_Type::CODE_BLOCK: string_append_formated(str, "CODE_BLOCK"); break;
		case Node_Type::ARGUMENT: {
			string_append_formated(str, "ARGUMENT");
			auto arg = (Argument*)base;
			if (arg->name.available) {
				string_append_formated(str, " ");
				string_append_string(str, arg->name.value);
			}
			break;
		}
		case Node_Type::ENUM_MEMBER: {
			auto mem = (Enum_Member_Node*)base;
			string_append_formated(str, "ENUM_MEMBER ");
			string_append_string(str, mem->name);
			break;
		}
		case Node_Type::STRUCT_MEMBER: {
			auto mem = (Structure_Member_Node*)base;
			string_append_formated(str, "STRUCT_MEMBER ");
			string_append_string(str, mem->name);
			break;
		}
		case Node_Type::PARAMETER: {
			auto param = (Parameter*)base;
			string_append_formated(str, "PARAMETER ");
			break;
		}
		case Node_Type::SIGNATURE: {
			string_append_formated(str, "SIGNATURE ");
			break;
		}
		case Node_Type::EXPRESSION:
		{
			auto expr = (Expression*)base;
			switch (expr->type)
			{
			case Expression_Type::BINARY_OPERATION: string_append_formated(str, "BINARY_OPERATION"); break;
			case Expression_Type::UNARY_OPERATION: string_append_formated(str, "UNARY_OPERATION"); break;
			case Expression_Type::PATTERN_VARIABLE: string_append_formated(str, "PATTERN_VARIABLE"); break;
			case Expression_Type::FUNCTION_CALL: string_append_formated(str, "FUNCTION_CALL"); break;
			case Expression_Type::CAST: string_append_formated(str, "CAST"); break;
			case Expression_Type::BAKE: string_append_formated(str, "BAKE"); break;
			case Expression_Type::INSTANCIATE: string_append_formated(str, "INSTANCIATE"); break;
			case Expression_Type::GET_OVERLOAD: string_append_formated(str, "GET_OVERLOAD"); break;
			case Expression_Type::PATH_LOOKUP: string_append_formated(str, "EXPR_LOOKUP "); break;
			case Expression_Type::LITERAL_READ: {
				string_append_formated(str, "LITERAL_READ ");
				auto& read = expr->options.literal_read;
				switch (read.type) {
				case Literal_Type::BOOLEAN: string_append_formated(str, read.options.boolean ? "true" : "false"); break;
				case Literal_Type::INTEGER: string_append_formated(str, "%d", read.options.int_val); break;
				case Literal_Type::FLOAT_VAL: string_append_formated(str, "%f", read.options.float_val); break;
				case Literal_Type::NULL_VAL: string_append_formated(str, "null"); break;
				case Literal_Type::STRING: string_append_formated(str, "%s", read.options.string->characters); break;
				default: panic("");
				}
				break;
			}
			case Expression_Type::ARRAY_ACCESS: string_append_formated(str, "ARRAY_ACCESS"); break;
			case Expression_Type::MEMBER_ACCESS: string_append_formated(str, "MEMBER_ACCESS"); break;
			case Expression_Type::SUBTYPE_ACCESS: {
				string_append_formated(str, "SUBTYPE_ACCESS"); 
				string_append_string(str, expr->options.subtype_access.name);
				break;
			}
			case Expression_Type::BASETYPE_ACCESS: string_append_formated(str, "BASETYPE_ACCESS"); break;
			case Expression_Type::INFERRED_FUNCTION: string_append_formated(str, "INFERRED_FUNCTION"); break;
			case Expression_Type::FUNCTION_POINTER_TYPE: string_append_formated(str, "FUNCTION_POINTER_TYPE"); break;
			case Expression_Type::ARRAY_TYPE: string_append_formated(str, "ARRAY_TYPE"); break;
			case Expression_Type::SLICE_TYPE: string_append_formated(str, "SLICE_TYPE"); break;
			case Expression_Type::POINTER_TYPE: {
				if (expr->options.pointer_type.is_optional) {
					string_append_formated(str, "OPTIONAL_POINTER_TYPE");
				}
				else {
					string_append_formated(str, "POINTER_TYPE");
				}
				break;
			}
			case Expression_Type::ERROR_EXPR: string_append_formated(str, "ERROR_EXPR"); break;
			case Expression_Type::STRUCT_INITIALIZER: string_append_formated(str, "STRUCT_INITIALIZER"); break;
			case Expression_Type::ARRAY_INITIALIZER: string_append_formated(str, "ARRAY_INITIZALIZER"); break;
			case Expression_Type::AUTO_ENUM: string_append_formated(str, "AUTO_ENUM"); break;
			default: panic("");
			}
			break;
		}
		case Node_Type::STATEMENT:
		{
			auto stat = (Statement*)base;
			switch (stat->type)
			{
			case Statement_Type::DEFINITION: string_append_formated(str, "STAT_DEF"); break;
			case Statement_Type::BLOCK: string_append_formated(str, "STAT_BLOCK"); break;
			case Statement_Type::ASSIGNMENT: {
				string_append_formated(str, "ASSIGNMENT");
				break;
			}
			case Statement_Type::BINOP_ASSIGNMENT: string_append_formated(str, "BINOP_ASSIGNMENT"); break;
			case Statement_Type::EXPRESSION_STATEMENT: string_append_formated(str, "EXPRESSION_STATEMENT"); break;
			case Statement_Type::DEFER: string_append_formated(str, "DEFER"); break;
			case Statement_Type::DEFER_RESTORE: string_append_formated(str, "DEFER_RESTORE"); break;
			case Statement_Type::IF_STATEMENT: string_append_formated(str, "IF_STATEMENT"); break;
			case Statement_Type::WHILE_STATEMENT: string_append_formated(str, "WHILE_STATEMENT"); break;
			case Statement_Type::FOR_LOOP: string_append_formated(str, "FOR_LOOP"); break;
			case Statement_Type::FOREACH_LOOP: string_append_formated(str, "FOREACH_LOOP"); break;
			case Statement_Type::SWITCH_STATEMENT: string_append_formated(str, "SWITCH_STATEMENT"); break;
			case Statement_Type::BREAK_STATEMENT: string_append_formated(str, "BREAK_STATEMENT"); break;
			case Statement_Type::CONTINUE_STATEMENT: string_append_formated(str, "CONTINUE_STATEMENT"); break;
			case Statement_Type::RETURN_STATEMENT: string_append_formated(str, "RETURN_STATEMENT"); break;
			default:panic("");
			}
			break;
		}
		default:panic("");
		}
	}

	void base_append_to_string_recursive(Node* base, String* str, int indentation)
	{
		base_append_to_string(base, str);
		
		int child_count = 0;
		while (base_get_child(base, child_count) != nullptr) {
			child_count += 1;
		}

		if (child_count == 1) {
			string_append_formated(str, ": ");
			base_append_to_string_recursive(base_get_child(base, 0), str, indentation + 1);
		}
		else 
		{
			string_append_formated(str, "\n");
			int child_index = 0;
			while (true)
			{
				AST::Node* child = base_get_child(base, child_index);
				if (child == nullptr) break;
				child_index += 1;

				for (int i = 0; i < indentation + 1; i++) {
					string_append_formated(str, "  ");
				}
				base_append_to_string_recursive(child, str, indentation + 1);
			}
		}
	}

	void base_print(Node* node)
	{
		String text = string_create(1024);
		SCOPE_EXIT(string_destroy(&text));
		base_append_to_string_recursive(node, &text, 0);
		logg("AST:\n------------------------\n%s\n", text.characters);
	}

	int binop_priority(Binop binop)
	{
		switch (binop)
		{
		case Binop::AND: return 0;
		case Binop::OR: return 1;
		case Binop::POINTER_EQUAL: return 2;
		case Binop::POINTER_NOT_EQUAL: return 2;
		case Binop::EQUAL: return 2;
		case Binop::NOT_EQUAL: return 2;
		case Binop::GREATER: return 3;
		case Binop::GREATER_OR_EQUAL: return 3;
		case Binop::LESS: return 3;
		case Binop::LESS_OR_EQUAL: return 3;
		case Binop::ADDITION: return 4;
		case Binop::SUBTRACTION: return 4;
		case Binop::MULTIPLICATION: return 5;
		case Binop::DIVISION: return 5;
		case Binop::MODULO: return 6;
		default: panic("");
		}
		panic("");
		return 0;
	}

	void custom_operator_type_append_to_string(Custom_Operator_Type type, String* string)
	{
		switch (type)
		{
		case Custom_Operator_Type::ARRAY_ACCESS: string_append(string, "ARRAY_ACCESS"); break;
		case Custom_Operator_Type::BINOP: string_append(string, "BINARY_OPERATOR"); break;
		case Custom_Operator_Type::UNOP: string_append(string, "UNARY_OPERATOR"); break;
		case Custom_Operator_Type::CAST: string_append(string, "CAST"); break;
		case Custom_Operator_Type::ITERATOR: string_append(string, "ITERATOR"); break;
		default: panic("");
		}
	}

	void path_lookup_append_to_string(Path_Lookup* path, String* string)
	{
		for (int i = 0; i < path->parts.size; i++) {
			string_append_formated(string, "%s", path->parts[i]->name->characters);
			if (i != path->parts.size - 1) {
				string_append_character(string, '~');
			}
		}
	}

	namespace Helpers
	{
		bool type_correct(Subtype_Initializer* base) {
			return base->base.type == Node_Type::SUBTYPE_INITIALIZER;
		}
		bool type_correct(Call_Node* base) {
			return base->base.type == Node_Type::CALL_NODE;
		}
		bool type_correct(Structure_Member_Node* base) {
			return base->base.type == Node_Type::STRUCT_MEMBER;
		}
		bool type_correct(Get_Overload_Argument* base) {
			return base->base.type == Node_Type::GET_OVERLOAD_ARGUMENT;
		}
		bool type_correct(Path_Lookup* base) {
			return base->base.type == Node_Type::PATH_LOOKUP;
		}
		bool type_correct(Definition* base) {
			return base->base.type == Node_Type::DEFINITION;
		}
		bool type_correct(Symbol_Node* base) {
			return base->base.type == Node_Type::SYMBOL_NODE;
		}
		bool type_correct(Switch_Case* base) {
			return base->base.type == Node_Type::SWITCH_CASE;
		}
		bool type_correct(Statement* base) {
			return base->base.type == Node_Type::STATEMENT;
		}
		bool type_correct(Argument* base) {
			return base->base.type == Node_Type::ARGUMENT;
		}
		bool type_correct(Parameter* base) {
			return base->base.type == Node_Type::PARAMETER;
		}
		bool type_correct(Signature* base) {
			return base->base.type == Node_Type::SIGNATURE;
		}
		bool type_correct(Expression* base) {
			return base->base.type == Node_Type::EXPRESSION;
		}
		bool type_correct(Enum_Member_Node* base) {
			return base->base.type == Node_Type::ENUM_MEMBER;
		}
		bool type_correct(Code_Block* base) {
			return base->base.type == Node_Type::CODE_BLOCK;
		}
	}

    Definition* upcast_definition(Definition_Custom_Operator* node) {
		return (Definition*) ( ((char*) node) - offsetof(Definition, options.custom_operator));
    }

    Definition* upcast_definition(Definition_Function* node) {
		return (Definition*) ( ((char*) node) - offsetof(Definition, options.function));
    }

    Definition* upcast_definition(Definition_Struct* node) {
		return (Definition*) ( ((char*) node) - offsetof(Definition, options.structure));
    }

    Definition* upcast_definition(Definition_Value* node) {
		return (Definition*) ( ((char*) node) - offsetof(Definition, options.value));
    }

    Definition* upcast_definition(Definition_Import* node) {
		return (Definition*) ( ((char*) node) - offsetof(Definition, options.function));
    }

    Definition* upcast_definition(Definition_Extern_Import* node) {
		return (Definition*) ( ((char*) node) - offsetof(Definition, options.extern_import));
    }

    Definition* upcast_definition(Definition_Enum* node) {
		return (Definition*) ( ((char*) node) - offsetof(Definition, options.enumeration));
    }

    Definition* upcast_definition(Definition_Module* node) {
		return (Definition*) ( ((char*) node) - offsetof(Definition, options.module));
    }

	Node* upcast(Definition* node) { return &node->base; }
    Node* upcast(Get_Overload_Argument* node) { return &node->base; }
	Node* upcast(Symbol_Node* node) { return &node->base; }
    Node* upcast(Switch_Case* node) { return &node->base; }
    Node* upcast(Statement* node) { return &node->base; }
    Node* upcast(Signature* node) { return &node->base; }
    Node* upcast(Argument* node) { return &node->base; }
    Node* upcast(Parameter* node) { return &node->base; }
    Node* upcast(Expression* node) { return &node->base; }
    Node* upcast(Enum_Member_Node* node) { return &node->base; }
    Node* upcast(Path_Lookup* node) { return &node->base; }
    Node* upcast(Code_Block* node) { return &node->base; }
    Node* upcast(Structure_Member_Node* node) { return &node->base; }
    Node* upcast(Call_Node* node) { return &node->base; }
    Node* upcast(Subtype_Initializer* node) { return &node->base; }

    Node* upcast(Function_Body node) {
		if (node.is_expression) {
			return &node.expr->base;
		}
		return &node.block->base;
	}

	Node* upcast(Definition_Custom_Operator* node) { return upcast(upcast_definition(node)); }
    Node* upcast(Definition_Function* node) { return upcast(upcast_definition(node)); } 
    Node* upcast(Definition_Struct* node) { return upcast(upcast_definition(node)); }
    Node* upcast(Definition_Value* node) { return upcast(upcast_definition(node)); }
    Node* upcast(Definition_Import* node) { return upcast(upcast_definition(node)); }
    Node* upcast(Definition_Extern_Import* node) { return upcast(upcast_definition(node)); }
    Node* upcast(Definition_Enum* node) { return upcast(upcast_definition(node)); }
    Node* upcast(Definition_Module* node) { return upcast(upcast_definition(node)); }

	Symbol_Node* Path_Lookup::last() {
		assert(parts.size > 0, "");
		return parts[parts.size - 1];
	}
}
