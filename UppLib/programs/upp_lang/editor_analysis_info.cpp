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

void add_semantic_info(int analysis_item_index, Semantic_Info_Type type, Semantic_Info_Option option, Analysis_Pass* pass)
{
	auto analysis_data = compiler.analysis_data;

	// Add Semantic_Info
	Semantic_Info info;
	info.type = type;
	info.options = option;
	info.analysis_item_index = analysis_item_index;
	info.pass = pass;
	dynamic_array_push_back(&analysis_data->semantic_infos, info);
}

int add_code_analysis_item(Token_Range token_range, Source_Code* code, int tree_depth)
{
	auto analysis_data = compiler.analysis_data;
	int analysis_item_index = analysis_data->next_analysis_item_index;
	analysis_data->next_analysis_item_index += 1;

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

        Code_Analysis_Item result;
        result.start_char = start_index;
        result.end_char = end_index;
        result.tree_depth = tree_depth;
		// These values are calculated afterwards
		result.semantic_info_mapping_count = 0;
		result.semantic_info_mapping_start_index = -1;
		result.item_index = analysis_item_index;
        dynamic_array_push_back(&line->item_infos, result);
    }

	return analysis_item_index;
};

void add_markup(Token_Range range, Source_Code* code, int tree_depth, vec3 color)
{
	int analysis_item_index = add_code_analysis_item(range, code, tree_depth);
	Semantic_Info_Option option;
	option.markup_color = color;
	add_semantic_info(analysis_item_index, Semantic_Info_Type::MARKUP, option, nullptr);
}



void find_editor_infos_recursive(
	AST::Node* node, Compilation_Unit* unit, Dynamic_Array<Analysis_Pass*>& active_passes, int tree_depth)
{
	auto code = unit->code;

	// Add additional passes to active-passes array
	auto node_passes_opt = hashtable_find_element(&compiler.analysis_data->ast_to_pass_mapping, node);
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
			auto info = pass_get_node_info(pass, AST::downcast<AST::Module>(node), Info_Query::TRY_READ);
			if (info == 0) { break; }
			Symbol_Table_Range table_range;
			table_range.range = token_range_to_text_range(node->bounding_range, code);
			table_range.symbol_table = info->symbol_table;
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
			auto block = pass_get_node_info(pass, block_node, Info_Query::TRY_READ);
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
			add_markup(token_range_last_token(node->range, code), code, tree_depth, Syntax_Color::ENUM_MEMBER);
		}
		if (active_passes.size == 0) break;

		int analysis_item_index = add_code_analysis_item(node->range, code, tree_depth);
		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];

			if (expr->type == AST::Expression_Type::FUNCTION) {
				if (pass->origin_workload->type == Analysis_Workload_Type::FUNCTION_HEADER) {
					Symbol_Table* table = ((Workload_Function_Header*)(pass->origin_workload))->progress->function->options.normal.parameter_table;
					Symbol_Table_Range table_range;
					table_range.range = token_range_to_text_range(node->bounding_range, code);
					table_range.symbol_table = table;
					table_range.tree_depth = tree_depth;
					table_range.pass = pass;
					dynamic_array_push_back(&code->symbol_table_ranges, table_range);
				}
			}

			Expression_Info* info = pass_get_node_info(pass, expr, Info_Query::TRY_READ);
			if (info == nullptr) continue;
			// Special Case: Member-Accesses should always generate an Expression_Info, so we can still have code-completion even with errors
			if (!info->is_valid && expr->type != AST::Expression_Type::MEMBER_ACCESS) {
				continue;
			}

			Semantic_Info_Option option;
			option.expression.expr = expr;
			option.expression.info = info;
			option.expression.is_member_access = false;

			if (expr->type == AST::Expression_Type::AUTO_ENUM)
			{
				auto type = datatype_get_non_const_type(info->cast_info.initial_type);
				if (type->type == Datatype_Type::ENUM) {
					option.expression.is_member_access = true;
					option.expression.member_access_info.value_type = type;
					option.expression.member_access_info.has_definition = false;
					option.expression.member_access_info.member_definition_unit = nullptr;
					Datatype_Enum* enum_type = downcast<Datatype_Enum>(type);
					if (enum_type->definition_node != nullptr)
					{
						option.expression.member_access_info.has_definition = true;
						option.expression.member_access_info.member_definition_unit = compiler_find_ast_compilation_unit(enum_type->definition_node);
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

				auto value_info = pass_get_node_info(pass, expr->options.member_access.expr, Info_Query::TRY_READ);
				if (value_info != nullptr && value_info->is_valid) {
					option.expression.member_access_info.value_type = value_info->cast_info.result_type;
					if (value_info->result_type == Expression_Result_Type::TYPE) {
						option.expression.member_access_info.value_type = value_info->options.type;
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
					type = datatype_get_non_const_type(type);
					if (type->type != Datatype_Type::ENUM) break;
					auto enum_type = downcast<Datatype_Enum>(type);
					goto_node = enum_type->definition_node;
					break;
				}
				case Member_Access_Type::DOT_CALL_AS_MEMBER:
				case Member_Access_Type::DOT_CALL:
				{
					ModTree_Function* fn = access_info.options.dot_call_function;
					if (fn == nullptr) break;
					switch (fn->function_type)
					{
					case ModTree_Function_Type::NORMAL: {
						goto_node = upcast(fn->options.normal.progress->header_workload->function_node);
						break;
					}
					case ModTree_Function_Type::BAKE: {
						// This does not happen in goto definition on member access
						break;
					}
					case ModTree_Function_Type::EXTERN: {
						if (fn->options.extern_definition->symbol != nullptr) {
							goto_node = fn->options.extern_definition->symbol->definition_node;
						}
						break;
					}
					default: panic("");
					}
					break;
				}
				default: panic("");
				}

				if (goto_node != nullptr)
				{
					option.expression.member_access_info.has_definition = true;
					option.expression.member_access_info.member_definition_unit = compiler_find_ast_compilation_unit(goto_node);
					option.expression.member_access_info.definition_index = token_index_to_text_index(
						goto_node->range.start, option.expression.member_access_info.member_definition_unit->code, true
					);
				}
			}

			// Expression info
			add_semantic_info(analysis_item_index, Semantic_Info_Type::EXPRESSION_INFO, option, pass);
		}

		break;
	}
	case AST::Node_Type::STRUCT_MEMBER: {
		auto member = downcast<AST::Structure_Member_Node>(node);
		add_markup(token_range_first_token(node->range, code), code, tree_depth, member->is_expression ? Syntax_Color::MEMBER : Syntax_Color::SUBTYPE);
		break;
	}
	case AST::Node_Type::ENUM_MEMBER: {
		add_markup(token_range_first_token(node->range, code), code, tree_depth, Syntax_Color::ENUM_MEMBER);
		break;
	}
	case AST::Node_Type::CALL_NODE:
	{
		if (active_passes.size == 0) break;
		auto arguments = downcast<AST::Call_Node>(node);

		int analysis_item_index = add_code_analysis_item(node->range, code, tree_depth);
		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];
			Callable_Call* call = pass_get_node_info(pass, arguments, Info_Query::TRY_READ);
			if (call != nullptr) {
				Semantic_Info_Option option;
				option.call_info.callable_call = call;
				option.call_info.call_node = arguments;
				add_semantic_info(analysis_item_index, Semantic_Info_Type::CALL_INFORMATION, option, pass);
			}
		}
		break;
	}
	case AST::Node_Type::ARGUMENT:
	{
		// Add named argument highlighting
		auto arg = downcast<AST::Argument>(node);
		if (arg->name.available) {
			add_markup(token_range_first_token(node->range, code), code, tree_depth, Syntax_Color::VARIABLE);
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

		Semantic_Info_Option option;
		option.argument_info.call_node = arguments;
		option.argument_info.argument_index = arg_index;
		int analysis_item_index = add_code_analysis_item(node->range, code, tree_depth);
		add_semantic_info(analysis_item_index, Semantic_Info_Type::ARGUMENT, option, nullptr);
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
				add_markup(range, code, tree_depth, Syntax_Color::VARIABLE);
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
		if (node->type == AST::Node_Type::PARAMETER) {
			auto param = downcast<AST::Parameter>(node);
			int offset = 0;
			if (param->is_comptime) offset += 1;
			if (param->is_mutable) offset += 1;
			
			auto& tokens = source_code_get_line(code, range.start.line)->tokens;
			range.start.token = math_minimum(range.start.token + offset, tokens.size);
			range.end.token = math_minimum(range.start.token + offset + 1, tokens.size);
			is_definition = true;

			add_markup(range, code, tree_depth, Syntax_Color::VALUE_DEFINITION);
		}

		int analysis_item_index = add_code_analysis_item(range, code, tree_depth);

		for (int i = 0; i < active_passes.size; i++)
		{
			auto pass = active_passes[i];

			Symbol* symbol = nullptr;
			AST::Symbol_Lookup* lookup = nullptr;
			switch (node->type)
			{
			case AST::Node_Type::DEFINITION_SYMBOL: {
				is_definition = true;
				auto info = pass_get_node_info(pass, AST::downcast<AST::Definition_Symbol>(node), Info_Query::TRY_READ);
				symbol = info == 0 ? 0 : info->symbol;
				break;
			}
			case AST::Node_Type::SYMBOL_LOOKUP: {
				lookup = AST::downcast<AST::Symbol_Lookup>(node);
				auto info = pass_get_node_info(pass, lookup, Info_Query::TRY_READ);
				symbol = info == nullptr ? 0 : info->symbol;
				break;
			}
			case AST::Node_Type::PARAMETER:
			{
				auto info = pass_get_node_info(pass, AST::downcast<AST::Parameter>(node), Info_Query::TRY_READ);
				symbol = info == nullptr ? 0 : info->symbol;
				break;
			}
			default: panic("");
			}

			if (symbol != nullptr) {
				// Add symbol lookup info
				Semantic_Info_Option option;
				option.symbol_info.symbol = symbol;
				option.symbol_info.is_definition = is_definition;
				option.symbol_info.pass = pass;
				add_semantic_info(analysis_item_index, Semantic_Info_Type::SYMBOL_LOOKUP, option, pass);
			}
			else if (node->type == AST::Node_Type::PARAMETER) {
				Semantic_Info_Option option;
				option.markup_color = Syntax_Color::VALUE_DEFINITION;
				add_semantic_info(analysis_item_index, Semantic_Info_Type::MARKUP, option, pass);
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
		find_editor_infos_recursive(child, unit, active_passes, tree_depth + 1);
		index += 1;
		child = AST::base_get_child(node, index);
	}

	dynamic_array_rollback_to_size(&active_passes, active_pass_count_before);
}

struct Semantic_Info_Comparator
{
	bool operator()(const Semantic_Info& a, const Semantic_Info& b)
	{
		if (a.analysis_item_index == b.analysis_item_index) {
			return a.pass < b.pass;
		}
		return a.analysis_item_index < b.analysis_item_index;
	}
};

void compiler_analysis_update_source_code_information()
{
	auto analysis_data = compiler.analysis_data;
	analysis_data->next_analysis_item_index = 0;
	auto& errors = compiler.analysis_data->compiler_errors;
	dynamic_array_reset(&errors);

	for (int i = 0; i < compiler.compilation_units.size; i++)
	{
		auto unit = compiler.compilation_units[i];

		// Reset analysis data for unit
		dynamic_array_reset(&unit->code->block_id_range);
		dynamic_array_reset(&unit->code->symbol_table_ranges);
		unit->code->root_table = nullptr;
		for (int i = 0; i < unit->code->line_count; i++) {
			dynamic_array_reset(&source_code_get_line(unit->code, i)->item_infos);
		}

		// Store nodes
		dynamic_array_append_other(&compiler.analysis_data->allocated_nodes, &unit->allocated_nodes);
		dynamic_array_reset(&unit->allocated_nodes);

		if (unit->module_progress == nullptr) {
			continue;
		}

		Dynamic_Array<Analysis_Pass*> active_passes = dynamic_array_create<Analysis_Pass*>(0);
		SCOPE_EXIT(dynamic_array_destroy(&active_passes));
		find_editor_infos_recursive(upcast(unit->root), unit, active_passes, 0);

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

			int analysis_item_index = add_code_analysis_item(error.range, unit->code, 0);
			Semantic_Info_Option option;
			option.error_index = errors.size - 1;
			add_semantic_info(analysis_item_index, Semantic_Info_Type::ERROR_ITEM, option, nullptr);
		}
	}

	// Add semantic errors
	Dynamic_Array<Token_Range> ranges = dynamic_array_create<Token_Range>();
	SCOPE_EXIT(dynamic_array_destroy(&ranges));
	for (int i = 0; i < compiler.analysis_data->semantic_errors.size; i++)
	{
		const auto& error = compiler.analysis_data->semantic_errors[i];

		auto unit = compiler_find_ast_compilation_unit(error.error_node);

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
			int analysis_item_index = add_code_analysis_item(ranges[j], unit->code, 0);
			Semantic_Info_Option option;
			option.error_index = errors.size - 1;
			add_semantic_info(analysis_item_index, Semantic_Info_Type::ERROR_ITEM, option, nullptr);
		}
	}

	// Set mapping infos between Analysis-Items and Semantic_Infos
	{
		// Sort semantic-infos, so that same analysis-items are next to each other
		auto& semantic_infos = compiler.analysis_data->semantic_infos;
		const int analysis_item_count = compiler.analysis_data->next_analysis_item_index;
		dynamic_array_sort(&compiler.analysis_data->semantic_infos, Semantic_Info_Comparator());

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
		for (int i = 0; i < compiler.compilation_units.size; i++)
		{
			auto unit = compiler.compilation_units[i];
			for (int j = 0; j < unit->code->line_count; j++) 
			{
				auto& analysis_items = source_code_get_line(unit->code, j)->item_infos;
				for (int k = 0; k < analysis_items.size; k++) 
				{
					auto& item = analysis_items[k];
					item.semantic_info_mapping_start_index = item_info_start_indices[item.item_index];
					item.semantic_info_mapping_count = item_info_count[item.item_index];
					// Remove item if mapping count is null (Not sure if this is wanted behavior)
					if (item.semantic_info_mapping_count == 0) {
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

Compiler_Analysis_Data* compiler_analysis_data_create()
{
	Compiler_Analysis_Data* result = new Compiler_Analysis_Data;

	result->compiler_errors = dynamic_array_create<Compiler_Error_Info>(); // List of parser and semantic errors
	result->constant_pool = constant_pool_create();
	result->type_system = type_system_create();
	result->extern_sources = extern_sources_create();

	// Semantic analyser
	result->program = modtree_program_create();
	result->function_slots = dynamic_array_create<Function_Slot>();
	result->semantic_errors = dynamic_array_create<Semantic_Error>();

	result->ast_to_pass_mapping = hashtable_create_pointer_empty<AST::Node*, Node_Passes>(16);
	result->ast_to_info_mapping = hashtable_create_empty<AST_Info_Key, Analysis_Info*>(16, ast_info_key_hash, ast_info_equals);
	result->pattern_variable_expression_mapping = hashtable_create_pointer_empty<AST::Expression*, Pattern_Variable*>(16);
	result->root_module = nullptr;

	// Workload executer
	result->all_workloads = dynamic_array_create<Workload_Base*>();

	// Allocations
	result->arena = Arena::create(2048);
	result->allocated_symbol_tables = dynamic_array_create<Symbol_Table*>();
	result->allocated_symbols = dynamic_array_create<Symbol*>();
	result->allocated_passes = dynamic_array_create<Analysis_Pass*>();
	result->allocated_function_progresses = dynamic_array_create<Function_Progress*>();
	result->allocated_operator_contexts = dynamic_array_create<Operator_Context*>();
	result->allocated_dot_calls = dynamic_array_create<Dynamic_Array<Dot_Call_Info>*>();
	result->allocated_nodes = dynamic_array_create<AST::Node*>();
	result->call_signatures = hashset_create_empty<Call_Signature*>(0, hash_call_signature, equals_call_signature);

	result->semantic_infos = dynamic_array_create<Semantic_Info>();
	result->next_analysis_item_index = 0;

	return result;
}

void compiler_analysis_data_destroy(Compiler_Analysis_Data* data)
{
	dynamic_array_destroy(&data->compiler_errors);
	constant_pool_destroy(&data->constant_pool);
	type_system_destroy(&data->type_system);
	extern_sources_destroy(&data->extern_sources);

	modtree_program_destroy(data->program);
	dynamic_array_destroy(&data->function_slots);

	dynamic_array_destroy(&data->semantic_infos);

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

	for (int i = 0; i < data->allocated_function_progresses.size; i++) {
		function_progress_destroy(data->allocated_function_progresses[i]);
	}
	dynamic_array_destroy(&data->allocated_function_progresses);

	for (int i = 0; i < data->allocated_operator_contexts.size; i++) {
		auto context = data->allocated_operator_contexts[i];
		dynamic_array_destroy(&context->context_imports);

		hashtable_destroy(&context->custom_operators);
		delete data->allocated_operator_contexts[i];
	}
	dynamic_array_destroy(&data->allocated_operator_contexts);

	for (int i = 0; i < data->allocated_dot_calls.size; i++) {
		auto dot_calls = data->allocated_dot_calls[i];
		dynamic_array_destroy(dot_calls);
		delete dot_calls;
	}
	dynamic_array_destroy(&data->allocated_dot_calls);

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

	for (int i = 0; i < data->all_workloads.size; i++) {
		analysis_workload_destroy(data->all_workloads[i]);
	}
	dynamic_array_destroy(&data->all_workloads);

	for (auto iter = data->call_signatures.make_iter(); iter.has_next(); iter.next()) {
		call_signature_destroy(*iter.value);
	}
	hashset_destroy(&data->call_signatures);

	delete data;
}

Dynamic_Array<Dot_Call_Info>* compiler_analysis_data_allocate_dot_calls(Compiler_Analysis_Data* data, int capacity)
{
	Dynamic_Array<Dot_Call_Info> initial = dynamic_array_create<Dot_Call_Info>(capacity);
	Dynamic_Array<Dot_Call_Info>* result = new Dynamic_Array<Dot_Call_Info>;
	*result = initial;

	dynamic_array_push_back(&data->allocated_dot_calls, result);
	return result;
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

Call_Parameter* call_signature_add_return_type(Call_Signature* signature, Datatype* datatype)
{
	call_signature_add_parameter(signature, compiler.identifier_pool.predefined_ids.return_type_name, datatype, false, false, true);
	assert(signature->return_type_index == -1, "");
	signature->return_type_index = signature->parameters.size - 1;
	return &signature->parameters[signature->parameters.size - 1];
}

Call_Signature* call_signature_register(Call_Signature* signature)
{
	assert(!signature->is_registered, "");

	// Deduplicate
	Call_Signature** dedup = hashset_find(&compiler.analysis_data->call_signatures, signature);
	if (dedup != nullptr) {
		call_signature_destroy(signature);
		return *dedup;
	}

	signature->is_registered = true;
	hashset_insert_element(&compiler.analysis_data->call_signatures, signature);
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
		if (format->remove_const_from_function_params) {
			param_type = datatype_get_non_const_type(param_type);
		}
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
