#include "editor_analysis_info.hpp"

#include "constant_pool.hpp"
#include "ast.hpp"
#include "symbol_table.hpp"
#include "source_code.hpp"
#include "compiler.hpp"
#include "semantic_analyser.hpp"
#include "syntax_colors.hpp"
#include "../../win32/timing.hpp"
#include "../../utility/rich_text.hpp"
#include "bytecode_generator.hpp"
#include "ir_code.hpp"
#include "c_backend.hpp"



// Prototypes
u64 hash_call_signature(Call_Signature** callable_p);
bool equals_call_signature(Call_Signature** app, Call_Signature** bpp);



Token_Range token_range_last_token(Token_Range range, Source_Code* code) 
{
    if (token_index_equal(range.start, range.end)) return range;
    if (range.end.token > 0) {
        range.start = range.end;
        range.start.token -= 1;
        return range;
    }
    return token_range_make(range.end, range.end);
}

Token_Range token_range_first_token(Token_Range range, Source_Code* code) 
{
    if (token_index_equal(range.start, range.end)) return range;
    auto line = source_code_get_line(code, range.start.line);
    if (range.start.token + 1 <= line->tokens.size) {
        range.end = range.start;
        range.end.token = range.start.token + 1;
        return range;
    }
    return token_range_make(range.start, range.start);
}

void add_semantic_info(
	int analysis_item_index, Editor_Info_Type type, Editor_Info_Option option, Analysis_Pass* pass, Compilation_Data* compilation_data)
{
	// Add Editor_Info
	Editor_Info info;
	info.type = type;
	info.options = option;
	info.analysis_item_index = analysis_item_index;
	info.pass = pass;
	dynamic_array_push_back(&compilation_data->semantic_infos, info);
}

int add_code_analysis_item(Token_Range token_range, Source_Code* code, int tree_depth, Compilation_Data* compilation_data)
{
	int analysis_item_index = compilation_data->next_analysis_item_index;
	compilation_data->next_analysis_item_index += 1;

    Text_Range range = token_range_to_text_range(token_range, code);
    for (int i = range.start.line; i <= range.end.line; i++)
    {
        auto line = source_code_get_line(code, i);
        int start_index = range.start.character;
        int end_index = range.end.character;
        if (i != range.start.line) {
            start_index = 0;
        }
        if (i != range.end.line) {
            end_index = line->text.size;
        }

        Editor_Info_Reference result;
        result.start_char = start_index;
        result.end_char = end_index;
        result.tree_depth = tree_depth;
		// These values are calculated afterwards
		result.editor_info_mapping_count = 0;
		result.editor_info_mapping_start_index = -1;
		result.item_index = analysis_item_index;
        dynamic_array_push_back(&line->item_infos, result);
    }

	return analysis_item_index;
};

void add_markup(Token_Range range, Source_Code* code, int tree_depth, vec3 color, Compilation_Data* compilation_data)
{
	int analysis_item_index = add_code_analysis_item(range, code, tree_depth, compilation_data);
	Editor_Info_Option option;
	option.markup_color = color;
	add_semantic_info(analysis_item_index, Editor_Info_Type::MARKUP, option, nullptr, compilation_data);
}



void find_editor_infos_recursive(
	AST::Node* node, Compilation_Unit* unit, Dynamic_Array<Analysis_Pass*>& active_passes, int tree_depth, Compilation_Data* compilation_data)
{
	auto code = unit->code;
	auto type_system = compilation_data->type_system;

	// Add additional passes to active-passes array
	auto node_passes_opt = hashtable_find_element(&compilation_data->ast_to_pass_mapping, node);
	int active_pass_count_before = active_passes.size;
	if (node_passes_opt != nullptr) {
		auto new_passes = node_passes_opt->passes;
		for (int i = 0; i < new_passes.size; i++) {
			dynamic_array_push_back(&active_passes, new_passes[i]);
		}
	}

	switch (node->type)
	{
	case AST::Node_Type::MODULE:
	{
		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];
			auto info = pass_get_node_info(pass, AST::downcast<AST::Module>(node), Info_Query::TRY_READ, compilation_data);
			if (info == 0) { break; }
			Symbol_Table_Range table_range;
			table_range.range = token_range_to_text_range(node->bounding_range, code);
			table_range.symbol_table = info->upp_module->symbol_table;
			table_range.tree_depth = tree_depth;
			table_range.pass = pass;
			dynamic_array_push_back(&code->symbol_table_ranges, table_range);
		}
		break;
	}
	case AST::Node_Type::CODE_BLOCK:
	{
		auto block_node = AST::downcast<AST::Code_Block>(node);
		if (block_node->block_id.available) {
			Block_ID_Range id_range;
			id_range.range = token_range_to_text_range(node->bounding_range, code);
			id_range.block_id = block_node->block_id.value;
			dynamic_array_push_back(&code->block_id_range, id_range);
		}

		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];
			auto block = pass_get_node_info(pass, block_node, Info_Query::TRY_READ, compilation_data);
			if (block == 0) { continue; }
			Symbol_Table_Range table_range;
			table_range.range = token_range_to_text_range(node->bounding_range, code);
			table_range.symbol_table = block->symbol_table;
			table_range.tree_depth = tree_depth;
			table_range.pass = pass;
			dynamic_array_push_back(&code->symbol_table_ranges, table_range);
		}
		break;
	}
	case AST::Node_Type::EXPRESSION:
	{
		auto expr = downcast<AST::Expression>(node);
		if (expr->type == AST::Expression_Type::AUTO_ENUM) {
			add_markup(token_range_last_token(node->range, code), code, tree_depth, Syntax_Color::ENUM_MEMBER, compilation_data);
		}
		if (active_passes.size == 0) break;

		int analysis_item_index = add_code_analysis_item(node->range, code, tree_depth, compilation_data);
		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];

			if (expr->type == AST::Expression_Type::FUNCTION) {
				if (pass->origin_workload->type == Analysis_Workload_Type::FUNCTION_HEADER) {
					Symbol_Table* table = ((Workload_Function_Header*)(pass->origin_workload))->progress->parameter_table;
					Symbol_Table_Range table_range;
					table_range.range = token_range_to_text_range(node->bounding_range, code);
					table_range.symbol_table = table;
					table_range.tree_depth = tree_depth;
					table_range.pass = pass;
					dynamic_array_push_back(&code->symbol_table_ranges, table_range);
				}
			}

			Expression_Info* info = pass_get_node_info(pass, expr, Info_Query::TRY_READ, compilation_data);
			if (info == nullptr) continue;
			// Special Case: Member-Accesses should always generate an Expression_Info, so we can still have code-completion even with errors
			if (!info->is_valid && expr->type != AST::Expression_Type::MEMBER_ACCESS) {
				continue;
			}

			Editor_Info_Option option;
			option.expression.expr = expr;
			option.expression.info = info;
			option.expression.is_member_access = false;
			option.expression.analysis_pass = pass;

			if (expr->type == AST::Expression_Type::AUTO_ENUM)
			{
				auto type = expression_info_get_type(info, true, type_system);
				if (type->type == Datatype_Type::ENUM) {
					option.expression.is_member_access = true;
					option.expression.member_access_info.value_type = type;
					option.expression.member_access_info.has_definition = false;
					option.expression.member_access_info.member_definition_unit = nullptr;
					Datatype_Enum* enum_type = downcast<Datatype_Enum>(type);
					if (enum_type->definition_node != nullptr)
					{
						option.expression.member_access_info.has_definition = true;
						option.expression.member_access_info.member_definition_unit = compiler_find_ast_compilation_unit(
							compilation_data->compiler, enum_type->definition_node
						);
						option.expression.member_access_info.definition_index = token_index_to_text_index(
							enum_type->definition_node->range.start, option.expression.member_access_info.member_definition_unit->code, true
						);
					}
				}
			}
			else if (expr->type == AST::Expression_Type::MEMBER_ACCESS)
			{
				option.expression.is_member_access = true;
				option.expression.member_access_info.has_definition = false;
				option.expression.member_access_info.value_type = nullptr;

				auto value_info = pass_get_node_info(pass, expr->options.member_access.expr, Info_Query::TRY_READ, compilation_data);
				if (value_info != nullptr && value_info->is_valid) {
					option.expression.member_access_info.value_type = value_info->cast_info.result_type;
					if (value_info->result_type == Expression_Result_Type::DATATYPE) {
						option.expression.member_access_info.value_type = value_info->options.datatype;
					}
				}
				// Note: Info->is_valid does not really mean a lot anymore
				// if (!info->is_valid) break;

				auto& access_info = info->specifics.member_access;
				AST::Node* goto_node = nullptr;
				switch (access_info.type)
				{
				case Member_Access_Type::STRUCT_SUBTYPE:
				case Member_Access_Type::STRUCT_UP_OR_DOWNCAST:
				case Member_Access_Type::STRUCT_POLYMORHPIC_PARAMETER_ACCESS:
					break;
				case Member_Access_Type::STRUCT_MEMBER_ACCESS:
				{
					if (access_info.options.member.definition_node == nullptr) break;
					goto_node = access_info.options.member.definition_node;
					break;
				}
				case Member_Access_Type::ENUM_MEMBER_ACCESS: {
					Datatype* type = option.expression.member_access_info.value_type;
					if (type == nullptr) break;
					if (type->type != Datatype_Type::ENUM) break;
					auto enum_type = downcast<Datatype_Enum>(type);
					goto_node = enum_type->definition_node;
					break;
				}
				default: panic("");
				}

				if (goto_node != nullptr)
				{
					option.expression.member_access_info.has_definition = true;
					option.expression.member_access_info.member_definition_unit = compiler_find_ast_compilation_unit(compilation_data->compiler, goto_node);
					option.expression.member_access_info.definition_index = token_index_to_text_index(
						goto_node->range.start, option.expression.member_access_info.member_definition_unit->code, true
					);
				}
			}

			// Expression info
			add_semantic_info(analysis_item_index, Editor_Info_Type::EXPRESSION_INFO, option, pass, compilation_data);
		}

		break;
	}
	case AST::Node_Type::STRUCT_MEMBER: {
		auto member = downcast<AST::Structure_Member_Node>(node);
		add_markup(
			token_range_first_token(node->range, code), code, tree_depth, 
			member->is_expression ? Syntax_Color::MEMBER : Syntax_Color::SUBTYPE, 
			compilation_data
		);
		break;
	}
	case AST::Node_Type::ENUM_MEMBER: {
		add_markup(token_range_first_token(node->range, code), code, tree_depth, Syntax_Color::ENUM_MEMBER, compilation_data);
		break;
	}
	case AST::Node_Type::CALL_NODE:
	{
		if (active_passes.size == 0) break;
		auto arguments = downcast<AST::Call_Node>(node);

		int analysis_item_index = add_code_analysis_item(node->range, code, tree_depth, compilation_data);
		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];
			Call_Info* call_info = pass_get_node_info(pass, arguments, Info_Query::TRY_READ, compilation_data);
			if (call_info != nullptr) {
				Editor_Info_Option option;
				option.call_info.callable_call = call_info;
				option.call_info.call_node = arguments;
				add_semantic_info(analysis_item_index, Editor_Info_Type::CALL_INFORMATION, option, pass, compilation_data);
			}
		}
		break;
	}
	case AST::Node_Type::ARGUMENT:
	{
		// Add named argument highlighting
		auto arg = downcast<AST::Argument>(node);
		if (arg->name.available) {
			add_markup(token_range_first_token(node->range, code), code, tree_depth, Syntax_Color::VARIABLE, compilation_data);
		}

		// Find argument index
		auto arguments = downcast<AST::Call_Node>(node->parent);
		int arg_index = -1;
		for (int i = 0; i < arguments->arguments.size; i++) {
			if (upcast(arguments->arguments[i]) == node) {
				arg_index = i;
				break;
			}
		}

		Editor_Info_Option option;
		option.argument_info.call_node = arguments;
		option.argument_info.argument_index = arg_index;
		int analysis_item_index = add_code_analysis_item(node->range, code, tree_depth, compilation_data);
		add_semantic_info(analysis_item_index, Editor_Info_Type::ARGUMENT, option, nullptr, compilation_data);
		break;
	}
	case AST::Node_Type::CONTEXT_CHANGE:
	{
		auto line = source_code_get_line(code, node->range.start.line);
		if (node->range.start.token + 1 < line->tokens.size) {
			if (line->tokens[node->range.start.token + 1].type == Token_Type::IDENTIFIER) {
				Token_Range range = token_range_make(node->range.start, node->range.start);
				range.start.token += 1;
				range.end.token += 2;
				add_markup(range, code, tree_depth, Syntax_Color::VARIABLE, compilation_data);
			}
		}
		break;
	}
	case AST::Node_Type::DEFINITION_SYMBOL:
	case AST::Node_Type::SYMBOL_LOOKUP:
	case AST::Node_Type::PARAMETER:
	{
		bool is_definition = false;
		Token_Range range = token_range_first_token(node->range, code);
		if (node->type == AST::Node_Type::PARAMETER) 
		{
			auto param = downcast<AST::Parameter>(node);
			int offset = 0;
			if (param->is_return_type) break;
			if (param->is_comptime) offset += 1;
			
			auto& tokens = source_code_get_line(code, range.start.line)->tokens;
			range.start.token = math_minimum(range.start.token + offset, tokens.size);
			range.end.token = math_minimum(range.start.token + offset + 1, tokens.size);
			is_definition = true;

			add_markup(range, code, tree_depth, Syntax_Color::VALUE_DEFINITION, compilation_data);
		}

		int analysis_item_index = add_code_analysis_item(range, code, tree_depth, compilation_data);

		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];

			Symbol* symbol = nullptr;
			AST::Symbol_Lookup* lookup = nullptr;
			bool add_color = true;
			switch (node->type)
			{
			case AST::Node_Type::DEFINITION_SYMBOL: {
				is_definition = true;
				auto info = pass_get_node_info(pass, AST::downcast<AST::Definition_Symbol>(node), Info_Query::TRY_READ, compilation_data);
				symbol = info == 0 ? 0 : info->symbol;
				break;
			}
			case AST::Node_Type::SYMBOL_LOOKUP: {
				lookup = AST::downcast<AST::Symbol_Lookup>(node);
				if (lookup->is_root_module) {
					add_color = false;
				}
				auto info = pass_get_node_info(pass, lookup, Info_Query::TRY_READ, compilation_data);
				symbol = info == nullptr ? 0 : info->symbol;
				break;
			}
			case AST::Node_Type::PARAMETER:
			{
				auto info = pass_get_node_info(pass, AST::downcast<AST::Parameter>(node), Info_Query::TRY_READ, compilation_data);
				symbol = info == nullptr ? 0 : info->symbol;
				break;
			}
			default: panic("");
			}

			if (symbol != nullptr) {
				// Add symbol lookup info
				Editor_Info_Option option;
				option.symbol_info.symbol = symbol;
				option.symbol_info.is_definition = is_definition;
				option.symbol_info.pass = pass;
				option.symbol_info.add_color = add_color;
				add_semantic_info(analysis_item_index, Editor_Info_Type::SYMBOL_LOOKUP, option, pass, compilation_data);
			}
			else if (node->type == AST::Node_Type::PARAMETER) {
				Editor_Info_Option option;
				option.markup_color = Syntax_Color::VALUE_DEFINITION;
				add_semantic_info(analysis_item_index, Editor_Info_Type::MARKUP, option, pass, compilation_data);
			}
		}
		break;
	}
	}

	// Recurse to children
	int index = 0;
	auto child = AST::base_get_child(node, index);
	while (child != 0)
	{
		find_editor_infos_recursive(child, unit, active_passes, tree_depth + 1, compilation_data);
		index += 1;
		child = AST::base_get_child(node, index);
	}

	dynamic_array_rollback_to_size(&active_passes, active_pass_count_before);
}

struct Semantic_Info_Comparator
{
	bool operator()(const Editor_Info& a, const Editor_Info& b)
	{
		if (a.analysis_item_index == b.analysis_item_index) {
			return a.pass < b.pass;
		}
		return a.analysis_item_index < b.analysis_item_index;
	}
};

Upp_Module* compilation_unit_to_module(Compilation_Unit* compilation_unit, Compilation_Data* compilation_data)
{
	Node_Passes* passes = hashtable_find_element(&compilation_data->ast_to_pass_mapping, upcast(compilation_unit->root));
	if (passes == nullptr) return nullptr;
	if (passes->passes.size == 0) return nullptr;
	AST_Info_Key key;
	key.base = upcast(compilation_unit->root);
	key.pass = passes->passes[0];
	Analysis_Info** info = hashtable_find_element(&compilation_data->ast_to_info_mapping, key);
	if (info == nullptr) return nullptr;
	return (*info)->module_info.upp_module;
}

void compilation_data_update_source_code_information(Compilation_Data* compilation_data)
{
	compilation_data->next_analysis_item_index = 0;
	auto& errors = compilation_data->compiler_errors;
	dynamic_array_reset(&errors);
	Compiler* compiler = compilation_data->compiler;

	for (int i = 0; i < compiler->compilation_units.size; i++)
	{
		auto unit = compiler->compilation_units[i];

		// Reset analysis data for unit
		dynamic_array_reset(&unit->code->block_id_range);
		dynamic_array_reset(&unit->code->symbol_table_ranges);
		unit->code->root_table = nullptr;
		for (int i = 0; i < unit->code->line_count; i++) {
			dynamic_array_reset(&source_code_get_line(unit->code, i)->item_infos);
		}

		// Store nodes
		dynamic_array_append_other(&compilation_data->allocated_nodes, &unit->allocated_nodes);
		dynamic_array_reset(&unit->allocated_nodes);

		if (compilation_unit_to_module(unit, compilation_data) == nullptr) {
			continue;
		}

		Dynamic_Array<Analysis_Pass*> active_passes = dynamic_array_create<Analysis_Pass*>(0);
		SCOPE_EXIT(dynamic_array_destroy(&active_passes));
		find_editor_infos_recursive(upcast(unit->root), unit, active_passes, 0, compilation_data);

		// Add parser errors
		for (int i = 0; i < unit->parser_errors.size; i++)
		{
			const auto& error = unit->parser_errors[i];
			Text_Range range = token_range_to_text_range(error.range, unit->code);

			Compiler_Error_Info error_info;
			error_info.message = error.msg;
			error_info.unit = unit;
			error_info.semantic_error_index = -1;
			error_info.text_index = range.start;
			dynamic_array_push_back(&errors, error_info);

			int analysis_item_index = add_code_analysis_item(error.range, unit->code, 0, compilation_data);
			Editor_Info_Option option;
			option.error_index = errors.size - 1;
			add_semantic_info(analysis_item_index, Editor_Info_Type::ERROR_ITEM, option, nullptr, compilation_data);
		}
	}

	// Add semantic errors
	Dynamic_Array<Token_Range> ranges = dynamic_array_create<Token_Range>();
	SCOPE_EXIT(dynamic_array_destroy(&ranges));
	for (int i = 0; i < compilation_data->semantic_errors.size; i++)
	{
		const auto& error = compilation_data->semantic_errors[i];

		auto unit = compiler_find_ast_compilation_unit(compilation_data->compiler, error.error_node);

		dynamic_array_reset(&ranges);
		Parser::ast_base_get_section_token_range(unit->code, error.error_node, error.section, &ranges);
		assert(ranges.size != 0, "");

		Compiler_Error_Info error_info;
		error_info.message = error.msg;
		error_info.unit = unit;
		error_info.semantic_error_index = i;
		error_info.text_index = token_range_to_text_range(ranges[0], unit->code).start;
		dynamic_array_push_back(&errors, error_info);

		// Add visible ranges
		for (int j = 0; j < ranges.size; j++) {
			int analysis_item_index = add_code_analysis_item(ranges[j], unit->code, 0, compilation_data);
			Editor_Info_Option option;
			option.error_index = errors.size - 1;
			add_semantic_info(analysis_item_index, Editor_Info_Type::ERROR_ITEM, option, nullptr, compilation_data);
		}
	}

	// Set mapping infos between Analysis-Items and Semantic_Infos
	{
		// Sort semantic-infos, so that same analysis-items are next to each other
		auto& semantic_infos = compilation_data->semantic_infos;
		const int analysis_item_count = compilation_data->next_analysis_item_index;
		dynamic_array_sort(&compilation_data->semantic_infos, Semantic_Info_Comparator());

		// Find start and length of all semantic infos
		Array<int> item_info_start_indices = array_create<int>(analysis_item_count);
		Array<int> item_info_count = array_create<int>(analysis_item_count);
		SCOPE_EXIT(array_destroy(&item_info_start_indices));
		SCOPE_EXIT(array_destroy(&item_info_count));
		for (int i = 0; i < analysis_item_count; i++) {
			item_info_start_indices[i] = -1;
			item_info_count[i] = 0;
		}

		int last_index = -1;
		for (int i = 0; i < semantic_infos.size; i++) {
			auto& info = semantic_infos[i];
			if (info.analysis_item_index != last_index) {
				item_info_start_indices[info.analysis_item_index] = i;
				assert(item_info_count[info.analysis_item_index] == 0, "After sorting this should be the case");
			}
			last_index = info.analysis_item_index;
			item_info_count[info.analysis_item_index] += 1;
		}

		// Update analysis-item infos in source-code
		for (int i = 0; i < compiler->compilation_units.size; i++)
		{
			auto unit = compiler->compilation_units[i];
			for (int j = 0; j < unit->code->line_count; j++) 
			{
				auto& analysis_items = source_code_get_line(unit->code, j)->item_infos;
				for (int k = 0; k < analysis_items.size; k++) 
				{
					auto& item = analysis_items[k];
					item.editor_info_mapping_start_index = item_info_start_indices[item.item_index];
					item.editor_info_mapping_count = item_info_count[item.item_index];
					// Remove item if mapping count is null (Not sure if this is wanted behavior)
					if (item.editor_info_mapping_count == 0) {
						dynamic_array_swap_remove(&analysis_items, k);
						k -= 1;
					}
				}
			}
		}
	}
}

u64 ast_info_key_hash(AST_Info_Key* key) {
	return hash_combine(hash_pointer(key->base), hash_pointer(key->pass));
}

bool ast_info_equals(AST_Info_Key* a, AST_Info_Key* b) {
	return a->base == b->base && a->pass == b->pass;
}

Compilation_Data* compilation_data_create(Compiler* compiler)
{
	Compilation_Data* result = new Compilation_Data;

	// Create datastructures
	{
		result->compiler = compiler;
		result->compiler_errors = dynamic_array_create<Compiler_Error_Info>(); // List of parser and semantic errors

		// Initialize Data
		result->function_slots = dynamic_array_create<Function_Slot>();
		result->semantic_errors = dynamic_array_create<Semantic_Error>();

		result->ast_to_pass_mapping = hashtable_create_pointer_empty<AST::Node*, Node_Passes>(16);
		result->ast_to_info_mapping = hashtable_create_empty<AST_Info_Key, Analysis_Info*>(16, ast_info_key_hash, ast_info_equals);
		result->pattern_variable_expression_mapping = hashtable_create_pointer_empty<AST::Expression*, Pattern_Variable*>(16);

		result->error_symbol = nullptr; // Initialized after this block
		result->global_allocator = nullptr;
		result->system_allocator = nullptr;
		result->code_block_comptimes = hashtable_create_pointer_empty<AST::Code_Block*, Symbol_Table*>(1);

		// Allocations
		result->arena = Arena::create(2048);
		result->allocated_symbol_tables = dynamic_array_create<Symbol_Table*>();
		result->allocated_symbols = dynamic_array_create<Symbol*>();
		result->allocated_passes = dynamic_array_create<Analysis_Pass*>();
		result->allocated_custom_operator_tables = dynamic_array_create<Custom_Operator_Table*>();
		result->allocated_nodes = dynamic_array_create<AST::Node*>();
		result->call_signatures = hashset_create_empty<Call_Signature*>(0, hash_call_signature, equals_call_signature);
		result->custom_operator_deduplication = hashtable_create_empty<Custom_Operator, Custom_Operator*>(16, hash_custom_operator, equals_custom_operator);

		result->semantic_infos = dynamic_array_create<Editor_Info>();
		result->next_analysis_item_index = 0;

		// Initialize stages
		result->constant_pool = constant_pool_create(result);
		result->type_system = type_system_create(result);
		result->extern_sources = extern_sources_create();
		result->program = modtree_program_create();
		result->workload_executer = workload_executer_create(compiler->fiber_pool, result);
		// result->ir_generator = ir_generator_create(result); HAD TO MOVE
		result->bytecode_generator = bytecode_generator_create(result);
		result->c_generator = c_generator_create(result);
		c_compiler_initialize(); // Initializiation is cached, so calling this multiple times doesn't matter
	}

	// Initialize default objects
	{
		Compilation_Data* compilation_data = result;
		auto& type_system = compilation_data->type_system;
		auto& types = type_system->predefined_types;
		auto identifier_pool = &compiler->identifier_pool;

		// Create root tables and predefined Symbols 
		auto pool_lock_value = identifier_pool_lock_aquire(identifier_pool);
		SCOPE_EXIT(identifier_pool_lock_release(pool_lock_value));
		Identifier_Pool_Lock* pool_lock = &pool_lock_value;
		{
			// Create root table and root table symbols
			{
				auto root_table = symbol_table_create(compilation_data);
				compilation_data->root_symbol_table = root_table;

				// Define int, float, bool, string
				Symbol* result = symbol_table_define_symbol(
					root_table, identifier_pool_add(pool_lock, string_create_static("int")), Symbol_Type::DATATYPE, 0, Symbol_Access_Level::GLOBAL,
					compilation_data
				);
				result->options.datatype = upcast(types.i32_type);

				result = symbol_table_define_symbol(
					root_table, identifier_pool_add(pool_lock, string_create_static("float")), Symbol_Type::DATATYPE, 0, Symbol_Access_Level::GLOBAL,
					compilation_data
				);
				result->options.datatype = upcast(types.f32_type);

				result = symbol_table_define_symbol(
					root_table, identifier_pool_add(pool_lock, string_create_static("bool")), Symbol_Type::DATATYPE, 0, Symbol_Access_Level::GLOBAL,
					compilation_data
				);
				result->options.datatype = upcast(types.bool_type);

				result = symbol_table_define_symbol(
					root_table, identifier_pool_add(pool_lock, string_create_static("string")), Symbol_Type::DATATYPE, 0, Symbol_Access_Level::GLOBAL,
					compilation_data
				);
				result->options.datatype = upcast(types.string);
			}

			// Create builtin-module
			{
				Symbol_Table* builtin_table = symbol_table_create(compilation_data);
				Symbol* builtin_symbol = symbol_table_define_symbol(
					builtin_table, identifier_pool_add(pool_lock, string_create_static("_BUILTIN_MODULE_")), 
					Symbol_Type::MODULE, nullptr, Symbol_Access_Level::GLOBAL,
					compilation_data
				);

				Upp_Module* builtin_module = compilation_data->arena.allocate<Upp_Module>();
				builtin_module->node = nullptr;
				builtin_module->is_file_module = false;
				builtin_module->options.module_symbol = builtin_symbol;
				builtin_module->symbol_table = builtin_table;

				builtin_symbol->options.upp_module = builtin_module;
				compilation_data->builtin_module = builtin_module;
			}

			// Create symbols in builtin table
			Symbol_Table* builtin_table = compilation_data->builtin_module->symbol_table;
			auto define_type_symbol = [&](const char* name, Datatype* type) -> Symbol* {
				Symbol* result = symbol_table_define_symbol(
					builtin_table, 
					identifier_pool_add(pool_lock, string_create_static(name)), 
					Symbol_Type::DATATYPE, 0, Symbol_Access_Level::GLOBAL, compilation_data
				);
				result->options.datatype = type;
				return result;
			};

			define_type_symbol("c_char", upcast(types.c_char));
			define_type_symbol("c_string", upcast(types.c_string));
			define_type_symbol("u8", upcast(types.u8_type));
			define_type_symbol("u16", upcast(types.u16_type));
			define_type_symbol("u32", upcast(types.u32_type));
			define_type_symbol("u64", upcast(types.u64_type));
			define_type_symbol("i8", upcast(types.i8_type));
			define_type_symbol("i16", upcast(types.i16_type));
			define_type_symbol("i32", upcast(types.i32_type));
			define_type_symbol("i64", upcast(types.i64_type));
			define_type_symbol("f32", upcast(types.f32_type));
			define_type_symbol("f64", upcast(types.f64_type));
			define_type_symbol("string", types.string);
			define_type_symbol("Allocator", upcast(types.allocator));
			define_type_symbol("Type_Handle", types.type_handle);
			define_type_symbol("Type_Info", upcast(types.type_information_type));
			define_type_symbol("Any", upcast(types.any_type));
			define_type_symbol("_", upcast(types.empty_struct_type));
			define_type_symbol("address", upcast(types.address));
			define_type_symbol("isize", upcast(types.isize));
			define_type_symbol("usize", upcast(types.usize));
			define_type_symbol("bytes", upcast(types.bytes));

			auto define_hardcoded_symbol = [&](const char* name, Hardcoded_Type type) -> Symbol* {
				Symbol* result = symbol_table_define_symbol(
					builtin_table, identifier_pool_add(pool_lock, string_create_static(name)), 
					Symbol_Type::HARDCODED_FUNCTION, 0, Symbol_Access_Level::GLOBAL,
					compilation_data
				);
				result->options.hardcoded = type;
				return result;
			};
			define_hardcoded_symbol("print_bool", Hardcoded_Type::PRINT_BOOL);
			define_hardcoded_symbol("print_i32", Hardcoded_Type::PRINT_I32);
			define_hardcoded_symbol("print_f32", Hardcoded_Type::PRINT_F32);
			define_hardcoded_symbol("print_string", Hardcoded_Type::PRINT_STRING);
			define_hardcoded_symbol("print_line", Hardcoded_Type::PRINT_LINE);
			define_hardcoded_symbol("read_i32", Hardcoded_Type::PRINT_I32);
			define_hardcoded_symbol("read_f32", Hardcoded_Type::READ_F32);
			define_hardcoded_symbol("read_bool", Hardcoded_Type::READ_BOOL);
			define_hardcoded_symbol("type_of", Hardcoded_Type::TYPE_OF);
			define_hardcoded_symbol("type_info", Hardcoded_Type::TYPE_INFO);

			define_hardcoded_symbol("memory_copy", Hardcoded_Type::MEMORY_COPY);
			define_hardcoded_symbol("memory_zero", Hardcoded_Type::MEMORY_ZERO);
			define_hardcoded_symbol("memory_compare", Hardcoded_Type::MEMORY_COMPARE);

			define_hardcoded_symbol("assert", Hardcoded_Type::ASSERT_FN);
			define_hardcoded_symbol("panic", Hardcoded_Type::PANIC_FN);
			define_hardcoded_symbol("size_of", Hardcoded_Type::SIZE_OF);
			define_hardcoded_symbol("align_of", Hardcoded_Type::ALIGN_OF);
			define_hardcoded_symbol("return_type", Hardcoded_Type::RETURN_TYPE);
			define_hardcoded_symbol("struct_tag", Hardcoded_Type::STRUCT_TAG);

			define_hardcoded_symbol("bitwise_not", Hardcoded_Type::BITWISE_NOT);
			define_hardcoded_symbol("bitwise_and", Hardcoded_Type::BITWISE_AND);
			define_hardcoded_symbol("bitwise_or", Hardcoded_Type::BITWISE_OR);
			define_hardcoded_symbol("bitwise_xor", Hardcoded_Type::BITWISE_XOR);
			define_hardcoded_symbol("bitwise_shift_left", Hardcoded_Type::BITWISE_SHIFT_LEFT);
			define_hardcoded_symbol("bitwise_shift_right", Hardcoded_Type::BITWISE_SHIFT_RIGHT);

			// NOTE: Error symbol is required so that unresolved symbol-reads can point to something,
			//       but it shouldn't be possible to reference the error symbol by name, so the 
			//       current little 'hack' is to use the identifier 0_ERROR and because it starts with a 0, it
			//       can never be used as a symbol read. Other approaches would be to have a custom symbol-table for the error symbol
			//       that isn't connected to anything or not adding the error symbol to any table.
			compilation_data->error_symbol = define_type_symbol("0_ERROR_SYMBOL", types.unknown_type);
			compilation_data->error_symbol->type = Symbol_Type::ERROR_SYMBOL;

			// Add global allocator symbol
			auto global_allocator_symbol = symbol_table_define_symbol(
				builtin_table,
				identifier_pool_add(pool_lock, string_create_static("global_allocator")),
				Symbol_Type::GLOBAL, 0, Symbol_Access_Level::GLOBAL,
				compilation_data
			);
			compilation_data->global_allocator = modtree_program_add_global_assert_type_finished(
				compilation_data,
				upcast(type_system_make_pointer(type_system, upcast(types.allocator))), global_allocator_symbol, false
			);
			global_allocator_symbol->options.global = compilation_data->global_allocator;

			// Add system allocator
			auto default_allocator_symbol = symbol_table_define_symbol(
				builtin_table,
				identifier_pool_add(pool_lock, string_create_static("system_allocator")),
				Symbol_Type::GLOBAL, 0, Symbol_Access_Level::GLOBAL, compilation_data
			);
			compilation_data->system_allocator = modtree_program_add_global_assert_type_finished(
				compilation_data, upcast(types.allocator), default_allocator_symbol, false
			);
			default_allocator_symbol->options.global = compilation_data->system_allocator;
		}

		// Predefined Callables (Hardcoded and context change)
		{
			auto& hardcoded_signatures = compilation_data->hardcoded_function_signatures;
			auto& context_signatures   = compilation_data->context_change_type_signatures;
			auto& ids = identifier_pool->predefined_ids;
			auto make_id = [&](const char* name) -> String* {
				return identifier_pool_add(&pool_lock_value, string_create_static(name));
			};

			Call_Signature* call_signature = call_signature_create_empty();
			compilation_data->empty_call_signature = call_signature_register(call_signature, compilation_data);



			// CAST
			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("to"), types.type_handle, false, false, false);
			call_signature_add_parameter(call_signature, make_id("from"), types.type_handle, false, false, false);
			call_signature_add_return_type(call_signature, upcast(types.empty_pattern_variable), compilation_data);
			compilation_data->cast_signature = call_signature;



			// Context functions
			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("function"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("auto_cast"), upcast(types.bool_type), false, false, false);
			call_signature_add_parameter(call_signature, make_id("call_by_reference"), upcast(types.bool_type), false, false, false);
			call_signature_add_parameter(call_signature, make_id("return_by_reference"), upcast(types.bool_type), false, false, false);
			context_signatures[(int)Custom_Operator_Type::CAST] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("binop"), upcast(types.string), true, false, false);
			call_signature_add_parameter(call_signature, make_id("function"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("commutative"), upcast(types.bool_type), false, false, false);
			call_signature_add_parameter(call_signature, make_id("use_pointer_for_left"),  upcast(types.bool_type), false, false, false);
			call_signature_add_parameter(call_signature, make_id("use_pointer_for_right"), upcast(types.bool_type), false, false, false);
			context_signatures[(int)Custom_Operator_Type::BINOP] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("unop"), upcast(types.string), true, false, false);
			call_signature_add_parameter(call_signature, make_id("function"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("use_pointer"),  upcast(types.bool_type), false, false, false);
			context_signatures[(int)Custom_Operator_Type::UNOP] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("function"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("use_pointer"), upcast(types.bool_type), false, false, false);
			call_signature_add_parameter(call_signature, make_id("use_pointer_for_index"), upcast(types.bool_type), false, false, false);
			context_signatures[(int)Custom_Operator_Type::ARRAY_ACCESS] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("create"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("has_next"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("next"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("get_value"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(call_signature, make_id("use_pointer"), upcast(types.bool_type), false, false, false);
			context_signatures[(int)Custom_Operator_Type::ITERATOR] = call_signature_register(call_signature, compilation_data);



			// HARDCODED FUNCTIONS
			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("condition"), upcast(types.bool_type), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::ASSERT_FN] = call_signature_register(call_signature, compilation_data);

			hardcoded_signatures[(int)Hardcoded_Type::PANIC_FN] = compilation_data->empty_call_signature;

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_return_type(call_signature, types.type_handle, compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::TYPE_OF] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("type"), upcast(types.type_handle), true, false, false);
			call_signature_add_return_type(call_signature, upcast(types.usize), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::SIZE_OF] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("type"), upcast(types.type_handle), true, false, false);
			call_signature_add_return_type(call_signature, upcast(types.u32_type), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::ALIGN_OF] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_return_type(call_signature, upcast(types.empty_pattern_variable), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::RETURN_TYPE] = call_signature;

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("type"), upcast(types.type_handle), true, false, false);
			call_signature_add_return_type(call_signature, upcast(type_system_make_pointer(type_system, upcast(types.type_information_type))), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::TYPE_INFO] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_return_type(call_signature, upcast(types.empty_pattern_variable), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::STRUCT_TAG] = call_signature_register(call_signature, compilation_data);



			// Memory functions
			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("destination"), upcast(types.address), true, false, false);
			call_signature_add_parameter(call_signature, make_id("source"), upcast(types.address), true, false, false);
			call_signature_add_parameter(call_signature, make_id("size"), upcast(types.usize), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::MEMORY_COPY] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("destination"), upcast(types.address), true, false, false);
			call_signature_add_parameter(call_signature, make_id("size"), upcast(types.usize), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::MEMORY_ZERO] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("a"), upcast(types.address), true, false, false);
			call_signature_add_parameter(call_signature, make_id("b"), upcast(types.address), true, false, false);
			call_signature_add_parameter(call_signature, make_id("size"), upcast(types.usize), true, false, false);
			call_signature_add_return_type(call_signature, upcast(types.bool_type), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::MEMORY_COMPARE] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("size"), upcast(types.usize), true, false, false);
			call_signature_add_return_type(call_signature, upcast(types.address), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::SYSTEM_ALLOC] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("data"), upcast(types.address), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::SYSTEM_FREE] = call_signature_register(call_signature, compilation_data);


			// Basic IO-Functions
			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.bool_type), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::PRINT_BOOL] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.i32_type), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::PRINT_I32] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.f32_type), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::PRINT_F32] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.string), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::PRINT_STRING] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			hardcoded_signatures[(int)Hardcoded_Type::PRINT_LINE] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_return_type(call_signature, upcast(types.i32_type), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::READ_I32] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_return_type(call_signature, upcast(types.f32_type), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::READ_F32] = call_signature_register(call_signature, compilation_data);

			call_signature = call_signature_create_empty();
			call_signature_add_return_type(call_signature, upcast(types.bool_type), compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::READ_BOOL] = call_signature_register(call_signature, compilation_data);



			// Bitwise functions
			auto bitwise_binop = call_signature_create_empty();
			call_signature_add_parameter(bitwise_binop, make_id("a"), upcast(types.empty_pattern_variable), true, false, false);
			call_signature_add_parameter(bitwise_binop, make_id("b"), upcast(types.empty_pattern_variable), true, false, false);
			bitwise_binop = call_signature_register(bitwise_binop, compilation_data);
			hardcoded_signatures[(int)Hardcoded_Type::BITWISE_AND] = bitwise_binop;
			hardcoded_signatures[(int)Hardcoded_Type::BITWISE_OR] = bitwise_binop;
			hardcoded_signatures[(int)Hardcoded_Type::BITWISE_XOR] = bitwise_binop;
			hardcoded_signatures[(int)Hardcoded_Type::BITWISE_SHIFT_LEFT] = bitwise_binop;
			hardcoded_signatures[(int)Hardcoded_Type::BITWISE_SHIFT_RIGHT] = bitwise_binop;

			call_signature = call_signature_create_empty();
			call_signature_add_parameter(call_signature, make_id("value"), upcast(types.empty_pattern_variable), true, false, false);
			hardcoded_signatures[(int)Hardcoded_Type::BITWISE_NOT] = call_signature_register(call_signature, compilation_data);
		}
	}

	result->ir_generator = ir_generator_create(result); // Was moved because this requires system allocator and global allocator to be available
	result->root_workload = workload_executer_add_root_workload(result);

	return result;
}

void compilation_data_finish_semantic_analysis(Compilation_Data* compilation_data)
{
	auto& type_system = compilation_data->type_system;
	auto& ids = compilation_data->compiler->identifier_pool.predefined_ids;
	auto program = compilation_data->program;

	Arena arena = Arena::create();
	SCOPE_EXIT(arena.destroy());

	// Semantic context just for error logging
	Semantic_Context error_context = semantic_context_make(
		compilation_data, nullptr, compilation_data->root_symbol_table, Symbol_Access_Level::GLOBAL, nullptr, &arena
	);
	Semantic_Context* semantic_context = &error_context;

	// Check if main is defined
	program->main_function = 0;
	DynArray<Symbol*> main_symbols = symbol_table_query_id(
		compilation_unit_to_module(compilation_data->main_unit, compilation_data)->symbol_table, ids.main, 
		symbol_query_info_make(Symbol_Access_Level::GLOBAL, Import_Type::NONE, false), &arena
	);
	if (main_symbols.size == 0) {
		log_semantic_error(semantic_context, "Main function not defined", upcast(compilation_data->main_unit->root), Node_Section::END_TOKEN);
		return;
	}
	if (main_symbols.size > 1) {
		for (int i = 0; i < main_symbols.size; i++) {
			auto symbol = main_symbols[i];
			log_semantic_error(semantic_context, "Multiple main functions found!", symbol->definition_node, Node_Section::FIRST_TOKEN);
		}
		return;
	}

	Symbol* main_symbol = main_symbols[0];
	if (main_symbol->type != Symbol_Type::FUNCTION) {
		log_semantic_error(semantic_context, "Main Symbol must be a function", main_symbol->definition_node, Node_Section::FIRST_TOKEN);
		log_error_info_symbol(semantic_context, main_symbol);
		return;
	}
	if (main_symbol->options.function->signature != compilation_data->empty_call_signature) {
		log_semantic_error(semantic_context, "Main function does not have correct signature", main_symbol->definition_node, Node_Section::FIRST_TOKEN);
		log_error_info_symbol(semantic_context, main_symbol);
		return;
	}
	program->main_function = main_symbol->options.function;

	if (!compilation_data_errors_occured(compilation_data)) {
		assert(program->main_function->is_runnable, "");
	}
}


void compilation_data_destroy(Compilation_Data* data)
{
	ir_generator_destroy(data->ir_generator);
	bytecode_generator_destroy(data->bytecode_generator);
	workload_executer_destroy(data->workload_executer);

	dynamic_array_destroy(&data->compiler_errors);
	constant_pool_destroy(data->constant_pool);
	type_system_destroy(data->type_system);
	extern_sources_destroy(&data->extern_sources);
	c_generator_destroy(data->c_generator);

	modtree_program_destroy(data->program);
	dynamic_array_destroy(&data->function_slots);

	dynamic_array_destroy(&data->semantic_infos);
	hashtable_destroy(&data->custom_operator_deduplication);
	hashtable_destroy(&data->code_block_comptimes);

	for (int i = 0; i < data->allocated_nodes.size; i++) {
		AST::Node* node = data->allocated_nodes[i];
		AST::base_destroy(node);
	}
	dynamic_array_destroy(&data->allocated_nodes);

	for (int i = 0; i < data->semantic_errors.size; i++) {
		dynamic_array_destroy(&data->semantic_errors[i].information);
	}
	dynamic_array_destroy(&data->semantic_errors);

	{
		auto iter = hashtable_iterator_create(&data->ast_to_info_mapping);
		while (hashtable_iterator_has_next(&iter)) {
			Analysis_Info* info = *iter.value;
			AST_Info_Key* key = iter.key;
			delete info;
			hashtable_iterator_next(&iter);
		}
		hashtable_destroy(&data->ast_to_info_mapping);
	}
	{
		auto iter = hashtable_iterator_create(&data->ast_to_pass_mapping);
		while (hashtable_iterator_has_next(&iter)) {
			Node_Passes* workloads = iter.value;
			dynamic_array_destroy(&workloads->passes);
			hashtable_iterator_next(&iter);
		}
		hashtable_destroy(&data->ast_to_pass_mapping);
	}
	{
		for (int i = 0; i < data->allocated_passes.size; i++) {
			delete data->allocated_passes[i];
		}
		dynamic_array_destroy(&data->allocated_passes);
	}
	hashtable_destroy(&data->pattern_variable_expression_mapping);

	for (int i = 0; i < data->allocated_custom_operator_tables.size; i++) {
		auto context = data->allocated_custom_operator_tables[i];
		hashtable_destroy(&context->installed_operators);
		delete data->allocated_custom_operator_tables[i];
	}
	dynamic_array_destroy(&data->allocated_custom_operator_tables);

	data->arena.destroy();


	// Symbol tables + workloads
	for (int i = 0; i < data->allocated_symbol_tables.size; i++) {
		symbol_table_destroy(data->allocated_symbol_tables[i]);
	}
	dynamic_array_destroy(&data->allocated_symbol_tables);

	for (int i = 0; i < data->allocated_symbols.size; i++) {
		symbol_destroy(data->allocated_symbols[i]);
	}
	dynamic_array_destroy(&data->allocated_symbols);

	for (auto iter = data->call_signatures.make_iter(); iter.has_next(); iter.next()) {
		call_signature_destroy(*iter.value);
	}
	hashset_destroy(&data->call_signatures);

	delete data;
}



void call_signature_destroy(Call_Signature* signature)
{
	for (int j = 0; j < signature->parameters.size; j++) {
		auto& param = signature->parameters[j];
		dynamic_array_destroy(&param.dependencies);
	}
	dynamic_array_destroy(&signature->parameters);
	delete signature;
}

u64 hash_call_signature(Call_Signature** callable_p)
{
	Call_Signature* signature = *callable_p;
	u64 hash = hash_i32(&signature->return_type_index);
	hash = hash_combine(hash, hash_i32(&signature->parameters.size));
	for (int i = 0; i < signature->parameters.size; i++)
	{
		auto& param = signature->parameters[i];
		hash = hash_combine(hash, hash_pointer(param.datatype));
		hash = hash_combine(hash, hash_pointer(param.name));
		hash = hash_bool(hash, param.required);
		hash = hash_bool(hash, param.requires_named_addressing);
		hash = hash_bool(hash, param.must_not_be_set);

		hash = hash_combine(hash, hash_i32(&param.comptime_variable_index));
		hash = hash_combine(hash, hash_i32(&param.dependencies.size));
		for (int j = 0; j < param.dependencies.size; j++) {
			hash = hash_combine(hash, hash_i32(&param.dependencies[j]));
		}
		hash = hash_bool(hash, param.contains_pattern_variable_definition);

		hash = hash_bool(hash, param.default_value_exists);
		hash = hash_combine(hash, hash_pointer(param.default_value_expr));
		hash = hash_combine(hash, hash_pointer(param.default_value_pass));
	}
	return hash;
}

bool equals_call_signature(Call_Signature** app, Call_Signature** bpp)
{
	Call_Signature* a = *app;
	Call_Signature* b = *bpp;

	if (a->return_type_index != b->return_type_index) return false;
	if (a->parameters.size != b->parameters.size) return false;
	for (int i = 0; i < a->parameters.size; i++)
	{
		auto& pa = a->parameters[i];
		auto& pb = b->parameters[i];

		if (!types_are_equal(pa.datatype, pb.datatype)) return false;

		if (pa.name != pb.name || pa.required != pb.required ||
			pa.requires_named_addressing != pb.requires_named_addressing ||
			pa.must_not_be_set != pb.must_not_be_set) return false;

		if (pa.comptime_variable_index != pb.comptime_variable_index ||
			pa.dependencies.size != pb.dependencies.size || pa.contains_pattern_variable_definition != pb.contains_pattern_variable_definition)
			return false;

		for (int j = 0; j < pa.dependencies.size; j++) {
			if (pa.dependencies[j] != pb.dependencies[j]) return false;
		}

		if (pa.default_value_exists != pb.default_value_exists ||
			pa.default_value_expr != pb.default_value_expr ||
			pa.default_value_pass != pb.default_value_pass)
			return false;
	}

	return true;
}

Call_Signature* call_signature_create_empty()
{
	Call_Signature* result = new Call_Signature;
	result->parameters = dynamic_array_create<Call_Parameter>();
	result->return_type_index = -1;
	result->is_registered = false;
	return result;
}

Call_Parameter* call_signature_add_parameter(
	Call_Signature* signature, String* name, Datatype* datatype,
	bool required, bool requires_named_addressing, bool must_not_be_set)
{
	assert(!signature->is_registered, "");

	Call_Parameter param;
	param.name = name;
	param.datatype = datatype;
	param.required = required;
	param.requires_named_addressing = requires_named_addressing;
	param.must_not_be_set = must_not_be_set;

	param.comptime_variable_index = -1;
	param.partial_pattern_index = -1;
	param.dependencies = dynamic_array_create<int>();
	param.contains_pattern_variable_definition = false;

	param.default_value_exists = false;
	param.default_value_expr = nullptr;
	param.default_value_pass = nullptr;

	dynamic_array_push_back(&signature->parameters, param);
	return &signature->parameters[signature->parameters.size - 1];
}

Call_Parameter* call_signature_add_return_type(Call_Signature* signature, Datatype* datatype, Compilation_Data* compilation_data)
{
	auto& ids = compilation_data->compiler->identifier_pool.predefined_ids;
	call_signature_add_parameter(signature, ids.return_type_name, datatype, false, false, true);
	assert(signature->return_type_index == -1, "");
	signature->return_type_index = signature->parameters.size - 1;
	return &signature->parameters[signature->parameters.size - 1];
}

Call_Signature* call_signature_register(Call_Signature* signature, Compilation_Data* compilation_data)
{
	assert(!signature->is_registered, "");

	// Deduplicate
	Call_Signature** dedup = hashset_find(&compilation_data->call_signatures, signature);
	if (dedup != nullptr) {
		call_signature_destroy(signature);
		return *dedup;
	}

	signature->is_registered = true;
	hashset_insert_element(&compilation_data->call_signatures, signature);
	return signature;
}

void call_signature_append_to_rich_text(Call_Signature* signature, Rich_Text::Rich_Text* text, Datatype_Format* format, Type_System* type_system)
{
	auto& parameters = signature->parameters;
	Rich_Text::append_formated(text, "(");

	int highlight_index = format->highlight_parameter_index;
	format->highlight_parameter_index = 0;
	bool require_colon = false;
	for (int i = 0; i < parameters.size; i++)
	{
		auto& param = parameters[i];

		// Skip parameters that shouldn't be printed
		if (signature->return_type_index == i) continue;
		if (param.must_not_be_set) continue;

		if (require_colon) {
			Rich_Text::append_formated(text, ", ");
		}
		require_colon = true;

		// Print param name + type with possible highlight
		if (highlight_index == i) {
			Rich_Text::set_bg(text, format->highlight_color);
		}
		if (param.comptime_variable_index != -1) {
			Rich_Text::append_character(text, '$');
		}
		Rich_Text::set_text_color(text, Syntax_Color::VALUE_DEFINITION);
		Rich_Text::append_formated(text, "%s: ", param.name->characters);

		auto param_type = param.datatype;
		datatype_append_to_rich_text(param_type, type_system, text, *format);
		Rich_Text::set_text_color(text, Syntax_Color::TEXT);

		if (highlight_index == i) {
			Rich_Text::stop_bg(text);
		}
		if (param.default_value_exists) {
			Rich_Text::append_formated(text, " = ...");
		}
	}
	Rich_Text::append_formated(text, ")");

	if (signature->return_type_index != -1) {
		Rich_Text::append(text, " => ");
		datatype_append_to_rich_text(signature->parameters[signature->return_type_index].datatype, type_system, text, *format);
		Rich_Text::set_text_color(text, Syntax_Color::TEXT);
	}
}

void call_signature_append_to_string(String* string, Type_System* type_system, Call_Signature* signature, Datatype_Format format)
{
	Rich_Text::Rich_Text text = Rich_Text::create(vec3(1.0f));
	SCOPE_EXIT(Rich_Text::destroy(&text));
	Rich_Text::add_line(&text);
	call_signature_append_to_rich_text(signature, &text, &format, type_system);
	Rich_Text::append_to_string(&text, string, 2);
}

Optional<Datatype*> Call_Signature::return_type()
{
	assert(is_registered, "");
	if (return_type_index == -1) return optional_make_failure<Datatype*>();
	return optional_make_success(parameters[return_type_index].datatype);
}
