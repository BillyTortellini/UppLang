#include "editor_analysis_info.hpp"

#include "constant_pool.hpp"
#include "ast.hpp"
#include "symbol_table.hpp"
#include "source_code.hpp"
#include "compiler.hpp"
#include "semantic_analyser.hpp"
#include "syntax_colors.hpp"
#include "../../win32/timing.hpp"

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

void add_code_analysis_item(Code_Analysis_Item_Type type, Code_Analysis_Item_Option option, Token_Range token_range, Source_Code* code, int tree_depth)
{
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
        result.type = type;
        result.options = option;
        result.start_char = start_index;
        result.tree_depth = tree_depth;
        result.end_char = end_index;
        dynamic_array_push_back(&line->item_infos, result);
    }
};


void find_editor_infos_recursive(
    AST::Node* node, Compilation_Unit* unit, Dynamic_Array<Analysis_Pass*> active_passes, int tree_depth)
{
    auto code = unit->code;

    // Check if passes are changed at this node
    auto node_passes_opt = hashtable_find_element(&compiler.analysis_data->ast_to_pass_mapping, node);
    if (node_passes_opt != nullptr) {
        active_passes = node_passes_opt->passes;
    }
    Analysis_Pass* pass = nullptr;
    assert(active_passes.size > 0, "");
    if (active_passes.size == 1) {
        pass = active_passes[0];
    }
    else {
        pass = active_passes[1];
    }

    switch (node->type)
    {
    case AST::Node_Type::MODULE: {
        auto info = pass_get_node_info(pass, AST::downcast<AST::Module>(node), Info_Query::TRY_READ);
        if (info == 0) { break; }
        Symbol_Table_Range table_range;
        table_range.range = token_range_to_text_range(node->bounding_range, code);
        table_range.symbol_table = info->symbol_table;
        table_range.tree_depth = tree_depth;
        dynamic_array_push_back(&code->symbol_table_ranges, table_range);
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

        auto block = pass_get_node_info(pass, block_node, Info_Query::TRY_READ);
        if (block == 0) { break; }
        Symbol_Table_Range table_range;
        table_range.range = token_range_to_text_range(node->bounding_range, code);
        table_range.symbol_table = block->symbol_table;
        table_range.tree_depth = tree_depth;
        dynamic_array_push_back(&code->symbol_table_ranges, table_range);
        break;
    }
    case AST::Node_Type::EXPRESSION: 
    {
        auto expr = downcast<AST::Expression>(node);
        Expression_Info* info = pass_get_node_info(pass, expr, Info_Query::TRY_READ);

        if (expr->type == AST::Expression_Type::FUNCTION) {
            if (pass->origin_workload->type == Analysis_Workload_Type::FUNCTION_HEADER) {
                Symbol_Table* table = ((Workload_Function_Header*)(pass->origin_workload))->progress->function->options.normal.parameter_table;
                Symbol_Table_Range table_range;
                table_range.range = token_range_to_text_range(node->bounding_range, code);
                table_range.symbol_table = table;
                table_range.tree_depth = tree_depth;
                dynamic_array_push_back(&code->symbol_table_ranges, table_range);
            }
        }

        if (info != nullptr)
        {
            if (expr->type == AST::Expression_Type::MEMBER_ACCESS)
            {
                auto value_info = pass_get_node_info(pass, expr->options.member_access.expr, Info_Query::TRY_READ);
                if (value_info != nullptr)
                {
                    Code_Analysis_Item_Option option;
                    option.member_access.final_type = value_info->cast_info.result_type;
                    option.member_access.initial_type = value_info->cast_info.initial_type;
                    option.member_access.access_type = info->specifics.member_access.type;
                    add_code_analysis_item(Code_Analysis_Item_Type::MEMBER_ACCESS, option, token_range_last_token(node->range, code), code, tree_depth);
                }
            }
            else if (expr->type == AST::Expression_Type::AUTO_ENUM)
            {
                auto type = datatype_get_non_const_type(info->cast_info.initial_type);
                if (type->type == Datatype_Type::ENUM) {
                    Code_Analysis_Item_Option option;
                    option.auto_enum_type = downcast<Datatype_Enum>(type);
                    add_code_analysis_item(Code_Analysis_Item_Type::AUTO_ENUM, option, token_range_last_token(node->range, code), code, tree_depth);
                }
            }

            // Expression info
            Code_Analysis_Item_Option option;
            option.expression_info.after_cast_type  = info->cast_info.result_type;
            option.expression_info.before_cast_type = info->cast_info.initial_type;
            option.expression_info.cast_type = info->cast_info.cast_type;
            add_code_analysis_item(Code_Analysis_Item_Type::EXPRESSION_INFO, option, node->range, code, tree_depth);
        }

        break;
    }
    case AST::Node_Type::STRUCT_MEMBER: {
        auto member = downcast<AST::Structure_Member_Node>(node);
        Code_Analysis_Item_Option option;
        option.markup_color = member->is_expression ? Syntax_Color::MEMBER : Syntax_Color::SUBTYPE;
        add_code_analysis_item(Code_Analysis_Item_Type::MARKUP, option, token_range_first_token(node->range, code), code, tree_depth);
        break;
    }
    case AST::Node_Type::ENUM_MEMBER: {
        Code_Analysis_Item_Option option;
        option.markup_color = Syntax_Color::ENUM_MEMBER;
        add_code_analysis_item(Code_Analysis_Item_Type::MARKUP, option, token_range_first_token(node->range, code), code, tree_depth);
        break;
    }
    case AST::Node_Type::ARGUMENT: {
        int index = 0;
        auto parent_args = downcast<AST::Arguments>(node->parent);
        for (int i = 0; i < parent_args->arguments.size; i++) {
            if (upcast(parent_args->arguments[i]) == node) {
                index = i;
                break;
            }
        }

        Code_Analysis_Item_Option option;
        option.argument_index = index;
        add_code_analysis_item(Code_Analysis_Item_Type::ARGUMENT_NODE, option, node->range, code, tree_depth);
        break;
    }
    case AST::Node_Type::ARGUMENTS: 
    {
        auto arguments = downcast<AST::Arguments>(node);
        Parameter_Matching_Info* info = pass_get_node_info(pass, arguments, Info_Query::TRY_READ);

        if (info != nullptr) {
            Code_Analysis_Item_Option option;
            option.call_information = info;
            add_code_analysis_item(Code_Analysis_Item_Type::CALL_INFORMATION, option, node->range, code, tree_depth);
        }
        break;
    }
    case AST::Node_Type::DEFINITION_SYMBOL: 
    case AST::Node_Type::SYMBOL_LOOKUP: 
    case AST::Node_Type::PARAMETER: 
    {
        bool is_definition = false;
        Token_Range range = token_range_first_token(node->range, code);
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
            auto param = downcast<AST::Parameter>(node);
            Token_Index start_token = node->range.start;
            if (param->is_comptime) {
                start_token.token += 1;
            }
            if (param->is_mutable) {
                start_token.token += 1;
            }
            auto& tokens = source_code_get_line(code, start_token.line)->tokens;
            range.start.line = start_token.line;
            range.end.line = start_token.line;
            range.start.token = math_maximum(start_token.token, tokens.size);
            range.end.token = math_maximum(start_token.token + 1, tokens.size);
            is_definition = true;

            auto info = pass_get_node_info(pass, AST::downcast<AST::Parameter>(node), Info_Query::TRY_READ);
            symbol = info == 0 ? 0 : info->symbol;

            break;
        }
        default: panic("");
        }

        if (symbol != nullptr) {
            // Add symbol lookup info
            Code_Analysis_Item_Option option;
            option.symbol_info.symbol = symbol;
            option.symbol_info.is_definition = is_definition;
            option.symbol_info.pass = pass;
            option.symbol_info.lookup = lookup;
            add_code_analysis_item(Code_Analysis_Item_Type::SYMBOL_LOOKUP, option, range, code, tree_depth);
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
}

void compiler_analysis_update_source_code_information()
{
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

        if (unit->module_progress == nullptr) {
            continue;
        }

        find_editor_infos_recursive(upcast(unit->root), unit, dynamic_array_create<Analysis_Pass*>(), 0);

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

            Code_Analysis_Item_Option option;
            option.error_index = errors.size - 1;
            add_code_analysis_item(Code_Analysis_Item_Type::ERROR_ITEM, option, error.range, unit->code, 0);
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
        error_info.semantic_error_index = -1;
        error_info.text_index = token_range_to_text_range(ranges[0], unit->code).start;
        dynamic_array_push_back(&errors, error_info);

        // Add visible ranges
        for (int j = 0; j < ranges.size; j++) {
            Code_Analysis_Item_Option option;
            option.error_index = errors.size - 1;
            add_code_analysis_item(Code_Analysis_Item_Type::ERROR_ITEM, option, ranges[j], unit->code, 0);
        }
    }
}

u64 ast_info_key_hash(AST_Info_Key* key) {
    return hash_combine(hash_pointer(key->base), hash_pointer(key->pass));
}

bool ast_info_equals(AST_Info_Key* a, AST_Info_Key* b) {
    return a->base == b->base && a->pass == b->pass;
}

Compiler_Analysis_Data* compiler_analysis_data_create(Timer* timer)
{
    Compiler_Analysis_Data* result = new Compiler_Analysis_Data;

    result->compiler_errors = dynamic_array_create<Compiler_Error_Info>(); // List of parser and semantic errors
    result->constant_pool = constant_pool_create();
    result->type_system = type_system_create(timer);
    result->extern_sources = extern_sources_create();

    // Semantic analyser
    result->program = modtree_program_create();
    result->function_slots = dynamic_array_create<Function_Slot>();
    result->semantic_errors = dynamic_array_create<Semantic_Error>();

    result->ast_to_pass_mapping = hashtable_create_pointer_empty<AST::Node*, Node_Passes>(16);
    result->ast_to_info_mapping = hashtable_create_empty<AST_Info_Key, Analysis_Info*>(16, ast_info_key_hash, ast_info_equals);
    result->root_module = nullptr;

    // Workload executer
    result->all_workloads = dynamic_array_create<Workload_Base*>();

    // Allocations
    result->global_variable_memory_pool = stack_allocator_create_empty(2048);
    result->progress_allocator = stack_allocator_create_empty(2048);
    result->allocated_symbol_tables = dynamic_array_create<Symbol_Table*>();
    result->allocated_symbols = dynamic_array_create<Symbol*>();
    result->allocated_passes = dynamic_array_create<Analysis_Pass*>();
    result->allocated_function_progresses = dynamic_array_create<Function_Progress*>();
    result->allocated_operator_contexts = dynamic_array_create<Operator_Context*>();

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

    for (int i = 0; i < data->semantic_errors.size; i++) {
        dynamic_array_destroy(&data->semantic_errors[i].information);
    }
    dynamic_array_destroy(&data->semantic_errors);

    {
        auto iter = hashtable_iterator_create(&data->ast_to_info_mapping);
        while (hashtable_iterator_has_next(&iter)) {
            Analysis_Info* info = *iter.value;
            AST_Info_Key* key = iter.key;
            if (key->base->type == AST::Node_Type::ARGUMENTS) {
                parameter_matching_info_destroy(&info->parameter_matching_info);
            }
            delete* iter.value;
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

    stack_allocator_destroy(&data->progress_allocator);
    stack_allocator_destroy(&data->global_variable_memory_pool);


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

    delete data;
}