#include "semantic_analyser.hpp"

#include "../../datastructures/string.hpp"
#include "../../utility/hash_functions.hpp"
#include "../../datastructures/hashset.hpp"
#include "../../datastructures/list.hpp"
#include "../../datastructures/dependency_graph.hpp"
#include "../../utility/file_io.hpp"

#include "compiler.hpp"
#include "type_system.hpp"
#include "bytecode_generator.hpp"
#include "bytecode_interpreter.hpp"
#include "compiler_misc.hpp"
#include "ast.hpp"
#include "ir_code.hpp"
#include "parser.hpp"
#include "source_code.hpp"
#include "symbol_table.hpp"
#include "syntax_colors.hpp"
#include "editor_analysis_info.hpp"

// GLOBALS
bool PRINT_DEPENDENCIES = false;
bool PRINT_TIMING = false;

// PROTOTYPES
ModTree_Function* modtree_function_create_empty(Call_Signature* signature, String* name, Compilation_Data* compilation_data);
ModTree_Function* modtree_function_create_normal(
	Call_Signature* signature, Symbol* symbol, Function_Progress* progress, Symbol_Table* param_table, Compilation_Data* compilation_data
);
void analysis_workload_destroy(Workload_Base* workload);
Expression_Info* semantic_analyser_analyse_expression_any(AST::Expression* expression, Expression_Context context, Semantic_Context* semantic_context);
Datatype* semantic_analyser_analyse_expression_value(
	AST::Expression* expression, Expression_Context context, Semantic_Context* semantic_context, bool allow_nothing = false, bool allow_poly_pattern = false);
Datatype* semantic_analyser_analyse_expression_type(AST::Expression* expression, Semantic_Context* semantic_context, bool allow_poly_pattern = false);
Control_Flow semantic_analyser_analyse_block(AST::Code_Block* code_block, Semantic_Context* semantic_context, bool polymorphic_symbol_access = false);
Expression_Info analyse_symbol_as_expression(
	Symbol* symbol, Expression_Context context, AST::Symbol_Lookup* error_report_node, Semantic_Context* semantic_context);
bool expression_is_auto_expression_with_preferred_type(AST::Expression* expression);
bool expression_is_auto_expression(AST::Expression* expression, Compilation_Data* compilation_data);
Poly_Instance* poly_header_instanciate(
	Call_Info* call_info, AST::Node* error_report_node, Node_Section error_report_section, Semantic_Context* semantic_context);

void expression_context_apply(
	Expression_Info* info, Expression_Context context, Semantic_Context* semantic_context, AST::Expression* expression = nullptr, Node_Section error_section = Node_Section::WHOLE);
void semantic_analyser_register_function_call(ModTree_Function* call_to, Semantic_Context* semantic_context);

bool workload_executer_switch_to_workload(Workload_Executer* executer, Workload_Base* workload);
void analysis_workload_entry(void* userdata);
void analysis_workload_append_to_string(Workload_Base* workload, String* string);
void workload_executer_wait_for_dependency_resolution(Semantic_Context* semantic_context);
Dependency_Failure_Info dependency_failure_info_make_none();
void analysis_workload_add_dependency_internal(
	Workload_Executer* executer, Workload_Base* workload, Workload_Base* dependency,
	Dependency_Failure_Info failure_info = dependency_failure_info_make_none()
);
Datatype_Memory_Info* type_wait_for_size_info_to_finish(
	Datatype* type,
	Semantic_Context* context,
	Dependency_Failure_Info failure_info = dependency_failure_info_make_none()
);
void poly_header_destroy(Poly_Header* info);
Custom_Operator_Table* symbol_table_install_custom_operator_table(
	Symbol_Table* symbol_table, Dynamic_Array<AST::Custom_Operator_Node*> custom_operators, bool create_workload, Semantic_Context* semantic_context
);
void analyse_custom_operator_node(
	AST::Custom_Operator_Node* custom_operator_node, Custom_Operator_Table* custom_operator_table, Semantic_Context* semantic_context);
u64 custom_operator_key_hash(Custom_Operator_Key* key);
bool cast_type_result_is_temporary(Cast_Type cast_type, bool initial_value_temporary);
DynArray<Reachable_Operator_Table> symbol_table_query_reachable_operator_tables(
	Symbol_Table* symbol_table, Custom_Operator_Type op_type, Semantic_Context* semantic_context
);
Call_Info call_info_make_with_empty_arguments(Call_Origin origin, Arena* arena);
Call_Origin call_origin_make_cast(Compilation_Data* compilation_data);
Call_Origin call_origin_make(Poly_Header* poly_header);



// Up/Downcasts
namespace Helpers
{
    Analysis_Workload_Type get_workload_type(Workload_Root* workload) { return Analysis_Workload_Type::ROOT; };
    Analysis_Workload_Type get_workload_type(Workload_Module_Analysis* workload) { return Analysis_Workload_Type::MODULE_ANALYSIS; };
    Analysis_Workload_Type get_workload_type(Workload_Definition* workload) { return Analysis_Workload_Type::DEFINITION; };
    Analysis_Workload_Type get_workload_type(Workload_Structure_Body* workload) { return Analysis_Workload_Type::STRUCT_BODY; };
    Analysis_Workload_Type get_workload_type(Workload_Structure_Polymorphic* workload) { return Analysis_Workload_Type::STRUCT_POLYMORPHIC; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Header* workload) { return Analysis_Workload_Type::FUNCTION_HEADER; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Body* workload) { return Analysis_Workload_Type::FUNCTION_BODY; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Cluster_Compile* workload) { return Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE; };
    Analysis_Workload_Type get_workload_type(Workload_Custom_Operator* workload) { return Analysis_Workload_Type::OPERATOR_CONTEXT_CHANGE; };
    Analysis_Workload_Type get_workload_type(Workload_Bake_Analysis* workload) { return Analysis_Workload_Type::BAKE_ANALYSIS; };
    Analysis_Workload_Type get_workload_type(Workload_Bake_Execution* workload) { return Analysis_Workload_Type::BAKE_EXECUTION; };
};

Workload_Base* upcast(Workload_Base* workload) { return workload; }
Workload_Base* upcast(Workload_Root* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Module_Analysis* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Function_Header* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Function_Body* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Function_Cluster_Compile* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Structure_Body* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Structure_Polymorphic* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Definition* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Bake_Analysis* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Bake_Execution* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Custom_Operator* workload) { return &workload->base; }

template <typename T>
T* downcast(Workload_Base* workload) {
    T* result = (T*)workload;
    assert(workload->type == Helpers::get_workload_type(result), "Invalid cast");
    return result;
}



// Helpers
Dependency_Failure_Info dependency_failure_info_make_none() {
    Dependency_Failure_Info info;
    info.error_report_node = 0;
    info.fail_indicator = 0;
    return info;
}

Dependency_Failure_Info dependency_failure_info_make(bool* fail_indicator, AST::Symbol_Lookup* error_report_node = 0) {
    Dependency_Failure_Info info;
    info.error_report_node = error_report_node;
    info.fail_indicator = fail_indicator;
    return info;
}

Pattern_Variable_State pattern_variable_state_make_set(Upp_Constant constant) {
    Pattern_Variable_State access;
    access.type = Pattern_Variable_State_Type::SET;
    access.options.value = constant;
    return access;
}

Pattern_Variable_State pattern_variable_state_make_unset() {
    Pattern_Variable_State access;
    access.type = Pattern_Variable_State_Type::UNSET;
    return access;
}

Pattern_Variable_State pattern_variable_state_make_pattern(Datatype* pattern_type) {
	assert(pattern_type->contains_pattern, "");
    Pattern_Variable_State access;
    access.type = Pattern_Variable_State_Type::PATTERN;
	access.options.pattern_type = pattern_type;
    return access;
}

Function_Progress* analysis_workload_try_get_function_progress(Workload_Base* workload)
{
    switch (workload->type) {
    case Analysis_Workload_Type::FUNCTION_BODY: return downcast<Workload_Function_Body>(workload)->progress; break;
    case Analysis_Workload_Type::FUNCTION_HEADER: return downcast<Workload_Function_Header>(workload)->progress; break;
    case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE: return downcast<Workload_Function_Cluster_Compile>(workload)->progress; break;
    }
    return 0;
}



// Analysis-Pass
Analysis_Pass* analysis_pass_allocate(Workload_Base* origin, AST::Node* mapping_node, Compilation_Data* compilation_data)
{
    Analysis_Pass* result = new Analysis_Pass;
    dynamic_array_push_back(&compilation_data->allocated_passes, result);
    result->origin_workload = origin;

    // Add mapping to workload 
    if (mapping_node) {
        Node_Passes* workloads_opt = hashtable_find_element(&compilation_data->ast_to_pass_mapping, mapping_node);
        if (workloads_opt == 0) {
            Node_Passes passes;
            passes.base = mapping_node;
            passes.passes = dynamic_array_create<Analysis_Pass*>(1);
            dynamic_array_push_back(&passes.passes, result);
            hashtable_insert_element(&compilation_data->ast_to_pass_mapping, mapping_node, passes);
        }
        else {
            dynamic_array_push_back(&workloads_opt->passes, result);
        }
    }
    return result;
}

Analysis_Info* pass_get_base_info(Analysis_Pass* pass, AST::Node* node, Info_Query query, Compilation_Data* compilation_data) 
{
    AST_Info_Key key;
    key.pass = pass;
    key.base = node;

    auto& info_mapping = compilation_data->ast_to_info_mapping;
    switch (query)
    {
    case Info_Query::CREATE: {
        Analysis_Info* new_info = new Analysis_Info;
        memory_zero(new_info);
        bool inserted = hashtable_insert_element(&info_mapping, key, new_info);
        assert(inserted, "Must not happen");
        return new_info;
    }
    case Info_Query::CREATE_IF_NULL: {
        // Check if already there
        Analysis_Info** already_there = hashtable_find_element(&info_mapping, key);
        if (already_there != 0) {
            assert(*already_there != 0, "Somewhere nullptr was inserted into hashmap");
            return *already_there;
        }
        // Otherwise create new
        Analysis_Info* new_info = new Analysis_Info;
        memory_zero(new_info);
        bool inserted = hashtable_insert_element(&info_mapping, key, new_info);
        assert(inserted, "Must not happen");
        return new_info;
    }
    case Info_Query::READ_NOT_NULL: {
        Analysis_Info** result = hashtable_find_element(&info_mapping, key);
        assert(result != 0, "Not inserted yet");
        assert(*result != 0, "Somewhere nullptr was inserted into hashmap");
        return *result;
    }
    case Info_Query::TRY_READ: {
        Analysis_Info** result = hashtable_find_element(&info_mapping, key);
        if (result == 0) {
            return 0;
        }
        assert(*result != 0, "Somewhere nullptr was inserted into hashmap");
        return *result;
    }
    default: panic("");
    }
    return 0;
}

Expression_Info* pass_get_node_info(Analysis_Pass* pass, AST::Expression* node, Info_Query query, Compilation_Data* compilation_data) {
    return &pass_get_base_info(pass, AST::upcast(node), query, compilation_data)->info_expr;
}

Case_Info* pass_get_node_info(Analysis_Pass* pass, AST::Switch_Case* node, Info_Query query, Compilation_Data* compilation_data) {
    return &pass_get_base_info(pass, AST::upcast(node), query, compilation_data)->info_case;
}

Statement_Info* pass_get_node_info(Analysis_Pass* pass, AST::Statement* node, Info_Query query, Compilation_Data* compilation_data) {
    return &pass_get_base_info(pass, AST::upcast(node), query, compilation_data)->info_stat;
}

Code_Block_Info* pass_get_node_info(Analysis_Pass* pass, AST::Code_Block* node, Info_Query query, Compilation_Data* compilation_data) {
    return &pass_get_base_info(pass, AST::upcast(node), query, compilation_data)->info_block;
}

Parameter_Info* pass_get_node_info(Analysis_Pass* pass, AST::Parameter* node, Info_Query query, Compilation_Data* compilation_data) {
    return &pass_get_base_info(pass, AST::upcast(node), query, compilation_data)->param_info;
}

Path_Lookup_Info* pass_get_node_info(Analysis_Pass* pass, AST::Path_Lookup* node, Info_Query query, Compilation_Data* compilation_data) {
    return &pass_get_base_info(pass, AST::upcast(node), query, compilation_data)->path_info;
}

Definition_Symbol_Info* pass_get_node_info(Analysis_Pass* pass, AST::Definition_Symbol* node, Info_Query query, Compilation_Data* compilation_data) {
    return &pass_get_base_info(pass, AST::upcast(node), query, compilation_data)->definition_symbol_info;
}

Symbol_Lookup_Info* pass_get_node_info(Analysis_Pass* pass, AST::Symbol_Lookup* node, Info_Query query, Compilation_Data* compilation_data) {
    return &pass_get_base_info(pass, AST::upcast(node), query, compilation_data)->symbol_lookup_info;
}

Module_Info* pass_get_node_info(Analysis_Pass* pass, AST::Module* node, Info_Query query, Compilation_Data* compilation_data) {
    return &pass_get_base_info(pass, AST::upcast(node), query, compilation_data)->module_info;
}

Call_Info* pass_get_node_info(Analysis_Pass* pass, AST::Call_Node* node, Info_Query query, Compilation_Data* compilation_data) {
	return &pass_get_base_info(pass, AST::upcast(node), query, compilation_data)->call_info;
}



// Analysis-Info
Expression_Info* get_info(AST::Expression* expression, Semantic_Context* context, bool create = false) {
    return pass_get_node_info(context->current_pass, expression, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL, context->compilation_data);
}

Case_Info* get_info(AST::Switch_Case* sw_case, Semantic_Context* context, bool create = false) {
    return pass_get_node_info(context->current_pass, sw_case, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL, context->compilation_data);
}

Statement_Info* get_info(AST::Statement* statement, Semantic_Context* context, bool create = false) {
    return pass_get_node_info(context->current_pass, statement, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL, context->compilation_data);
}

Code_Block_Info* get_info(AST::Code_Block* block, Semantic_Context* context, bool create = false) {
    return pass_get_node_info(context->current_pass, block, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL, context->compilation_data);
}

Parameter_Info* get_info(AST::Parameter* param, Semantic_Context* context, bool create = false) {
    return pass_get_node_info(context->current_pass, param, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL, context->compilation_data);
}

Definition_Symbol_Info* get_info(AST::Definition_Symbol* definition, Semantic_Context* context, bool create = false) {
    return pass_get_node_info(context->current_pass, definition, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL, context->compilation_data);
}

Symbol_Lookup_Info* get_info(AST::Symbol_Lookup* lookup, Semantic_Context* context, bool create = false) {
    return pass_get_node_info(context->current_pass, lookup, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL, context->compilation_data);
}

Path_Lookup_Info* get_info(AST::Path_Lookup* node, Semantic_Context* context, bool create = false) {
    return pass_get_node_info(context->current_pass, node, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL, context->compilation_data);
}

Module_Info* get_info(AST::Module* module, Semantic_Context* context, bool create = false) {
    return pass_get_node_info(context->current_pass, module, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL, context->compilation_data);
}

Call_Info* get_info(AST::Call_Node* arguments, Semantic_Context* context, bool create = false) {
    return pass_get_node_info(context->current_pass, arguments, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL, context->compilation_data);
}



// Error logging
struct Error_Checkpoint
{
    Workload_Base* workload;
    int real_error_count;
    int errors_due_to_unknown_count;
    Expression_Info* current_expr;
};

struct Error_Checkpoint_Info
{
    bool real_errors_occured;
    bool unknown_errors_occured;
};

Error_Checkpoint error_checkpoint_start(Semantic_Context* semantic_context)
{
    auto workload = semantic_context->current_workload;
    assert(workload != nullptr, "");

    Error_Checkpoint result;
    result.real_error_count = workload->real_error_count;
    result.errors_due_to_unknown_count = workload->errors_due_to_unknown_count;
    result.workload = workload;
    result.current_expr = semantic_context->current_expression;
    semantic_context->current_expression = nullptr;
    workload->error_checkpoint_count += 1;
    return result;
}

Error_Checkpoint_Info error_checkpoint_end(Error_Checkpoint checkpoint, Semantic_Context* semantic_context)
{
    auto workload = semantic_context->current_workload;
    assert(workload != nullptr, "");
    assert(workload == checkpoint.workload, "");
    assert(workload->error_checkpoint_count > 0, "");
    workload->error_checkpoint_count -= 1;

    Error_Checkpoint_Info result;
    result.real_errors_occured = workload->real_error_count != checkpoint.real_error_count;
    result.unknown_errors_occured = workload->errors_due_to_unknown_count != checkpoint.errors_due_to_unknown_count;
    workload->real_error_count = checkpoint.real_error_count;
    workload->errors_due_to_unknown_count = checkpoint.errors_due_to_unknown_count;
    semantic_context->current_expression = checkpoint.current_expr;
    return result;
}

void semantic_analyser_set_error_flag(bool error_due_to_unknown, Semantic_Context* semantic_context)
{
	auto workload = semantic_context->current_workload;
    if (workload == 0) {
        return;
    }

    // Raise error count
    if (error_due_to_unknown) {
        workload->errors_due_to_unknown_count += 1;
    }
    else {
        workload->real_error_count += 1;
    }

    // Update function and expression error infos
    if (semantic_context->current_expression != 0) {
        semantic_context->current_expression->is_valid = false;
    }
    if (semantic_context->current_function != 0 && workload->error_checkpoint_count == 0)
    {
        semantic_context->current_function->is_runnable = false;
        if (!error_due_to_unknown) {
            semantic_context->current_function->contains_errors = true;
        }
    }
}

void log_semantic_error(Semantic_Context* semantic_context, const char* msg, AST::Node* node, Node_Section node_section) 
{
    Semantic_Error error;
    error.msg = msg;
    error.error_node = node;
    error.section = node_section;
    error.information = dynamic_array_create<Error_Information>();

	auto workload = semantic_context->current_workload;
    if (!(workload != 0 && workload->error_checkpoint_count > 0)) { // Ignore errors during error checkpoint?
        dynamic_array_push_back(&semantic_context->compilation_data->semantic_errors, error);
    }
    semantic_analyser_set_error_flag(false, semantic_context);
}

void log_semantic_error(Semantic_Context* semantic_context, const char* msg, AST::Expression* node, Node_Section node_section = Node_Section::WHOLE) {
    log_semantic_error(semantic_context, msg, AST::upcast(node), node_section);
}

void log_semantic_error(Semantic_Context* semantic_context, const char* msg, AST::Statement* node, Node_Section node_section = Node_Section::WHOLE) {
    log_semantic_error(semantic_context, msg, AST::upcast(node), node_section);
}

void log_semantic_error_outside(Semantic_Context* semantic_context, const char* msg, AST::Node* node, Node_Section node_section) {
	log_semantic_error(semantic_context, msg, node, node_section);
}

Error_Information error_information_make_empty(Error_Information_Type type) {
    Error_Information info;
    info.type = type;
    return info;
}

void add_error_info_to_last_error(Semantic_Context* semantic_context, Error_Information info)
{
    auto workload = semantic_context->current_workload;
    if (workload != nullptr && workload->error_checkpoint_count > 0) {
        return;
    }

    auto& errors = semantic_context->compilation_data->semantic_errors;
    assert(errors.size > 0, "");
    dynamic_array_push_back(&errors[errors.size-1].information, info);
}

void log_error_info_argument_count(Semantic_Context* semantic_context, int given_argument_count, int expected_argument_count) {
    Error_Information info = error_information_make_empty(Error_Information_Type::ARGUMENT_COUNT);
    info.options.invalid_argument_count.expected = expected_argument_count;
    info.options.invalid_argument_count.given = given_argument_count;
    add_error_info_to_last_error(semantic_context, info);
}

void log_error_info_id(Semantic_Context* semantic_context, String* id) {
    assert(id != 0, "");
    Error_Information info = error_information_make_empty(Error_Information_Type::ID);
    info.options.id = id;
    add_error_info_to_last_error(semantic_context, info);
}

void log_error_info_symbol(Semantic_Context* context, Symbol* symbol) {
    Error_Information info = error_information_make_empty(Error_Information_Type::SYMBOL);
    info.options.symbol = symbol;
    add_error_info_to_last_error(context, info);
}

void log_error_info_exit_code(Semantic_Context* context, Exit_Code code) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXIT_CODE);
    info.options.exit_code = code;
    add_error_info_to_last_error(context, info);
}

void log_error_info_given_type(Semantic_Context* context, Datatype* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::GIVEN_TYPE);
    info.options.type = type;
    add_error_info_to_last_error(context, info);
}

void log_error_info_expected_type(Semantic_Context* context, Datatype* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXPECTED_TYPE);
    info.options.type = type;
    add_error_info_to_last_error(context, info);
}

void log_error_info_function_type(Semantic_Context* context, Datatype_Function_Pointer* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::FUNCTION_TYPE);
    info.options.function = type;
    add_error_info_to_last_error(context, info);
}

void log_error_info_binary_op_type(Semantic_Context* context, Datatype* left_type, Datatype* right_type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::BINARY_OP_TYPES);
    info.options.binary_op_types.left_type = left_type;
    info.options.binary_op_types.right_type = right_type;
    add_error_info_to_last_error(context, info);
}

void log_error_info_expression_result_type(Semantic_Context* context, Expression_Result_Type result_type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXPRESSION_RESULT_TYPE);
    info.options.expression_type = result_type;
    add_error_info_to_last_error(context, info);
}

void log_error_info_constant_status(Semantic_Context* context, const char* msg) {
    Error_Information info = error_information_make_empty(Error_Information_Type::CONSTANT_STATUS);
    info.options.constant_message = msg;
    add_error_info_to_last_error(context, info);
}

void log_error_info_comptime_msg(Semantic_Context* context, const char* comptime_msg) {
    Error_Information info = error_information_make_empty(Error_Information_Type::COMPTIME_MESSAGE);
    info.options.comptime_message = comptime_msg;
    add_error_info_to_last_error(context, info);
}

void log_error_info_cycle_workload(Semantic_Context* context, Workload_Base* workload) {
    Error_Information info = error_information_make_empty(Error_Information_Type::CYCLE_WORKLOAD);
    info.options.cycle_workload = workload;
    add_error_info_to_last_error(context, info);
}



// Progress/Workload creation
struct Workload_Entry_Info
{
    Workload_Base* workload;
    Compilation_Data* compilation_data;
};

template<typename T>
T* workload_executer_allocate_workload(Workload_Base* parent_workload, Compilation_Data* compilation_data)
{
	auto& executer = *compilation_data->workload_executer;
	executer.progress_was_made = true;

    // Create new workload
	T* result = compilation_data->arena.allocate<T>();
    Workload_Base* workload = &result->base;
    workload->type = Helpers::get_workload_type(result);
    workload->is_finished = false;
    workload->was_started = false;
    workload->cluster = 0;
    workload->dependencies = list_create<Workload_Base*>();
    workload->dependents = list_create<Workload_Base*>();
    workload->reachable_clusters = dynamic_array_create<Workload_Base*>(1);

    workload->real_error_count = 0;
    workload->errors_due_to_unknown_count = 0;
    workload->error_checkpoint_count = 0;

    workload->poly_parent_workload = parent_workload;
	workload->active_pattern_variable_states = array_create_static<Pattern_Variable_State>(nullptr, 0);
	workload->active_pattern_variable_states_origin = nullptr;
	// Only root has parent_workload == nullptr
	workload->polymorphic_instanciation_depth = parent_workload == nullptr ? 0 : parent_workload->polymorphic_instanciation_depth;

    // Add to workload queue
    dynamic_array_push_back(&executer.all_workloads, workload);
	// Note: There exists a check for dependencies before executing runnable workloads, so this is ok
    dynamic_array_push_back(&executer.runnable_workloads, workload); 

    return result;
}

// Note: If base progress != 0, then instance information (Progress-Type + instanciation depth) will not be set by this function
Function_Progress* function_progress_create_with_modtree_function(
    Symbol* symbol, AST::Expression* function_node, Call_Signature* signature, Semantic_Context* semantic_context,
    Function_Progress* base_progress = 0, Symbol_Access_Level symbol_access_level = Symbol_Access_Level::GLOBAL)
{
	auto compilation_data = semantic_context->compilation_data;
	Workload_Executer* executer = compilation_data->workload_executer;
    assert(function_node->type == AST::Expression_Type::FUNCTION, "Has to be function!");

    Symbol_Table* parameter_table = 0;
    if (base_progress == 0) {
        parameter_table = symbol_table_create_with_parent(semantic_context->current_symbol_table, symbol_access_level, compilation_data);
    }
    else {
        assert(base_progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE, "");
		parameter_table = base_progress->parameter_table;
    }

    // Create progress
	auto progress = compilation_data->arena.allocate<Function_Progress>();
    progress->type = Polymorphic_Analysis_Type::NON_POLYMORPHIC;
    progress->function = modtree_function_create_normal(signature, symbol, progress, parameter_table, compilation_data);
	progress->parameter_table = parameter_table;
	progress->function_node = function_node;

    // Set Symbol info
    if (symbol != 0) {
        symbol->type = Symbol_Type::FUNCTION;
        symbol->options.function = progress->function;
    }
    else if (base_progress != 0){
        progress->function->name = base_progress->function->name;
    }

    // Add workloads
    if (base_progress == 0) 
	{
        progress->header_workload = workload_executer_allocate_workload<Workload_Function_Header>(semantic_context->current_workload, compilation_data);
        auto header_workload = progress->header_workload;
        header_workload->progress = progress;
		header_workload->poly_header = nullptr;
    }
    else {
		// Note: Not sure if this is ever necessary, but we link to base progress header
		// But! We don't set this as parent-workload, as instances should not access header values
        progress->header_workload = base_progress->header_workload;
    }

    progress->body_workload = workload_executer_allocate_workload<Workload_Function_Body>(semantic_context->current_workload, compilation_data);
    progress->body_workload->progress = progress;

    progress->compile_workload = workload_executer_allocate_workload<Workload_Function_Cluster_Compile>(semantic_context->current_workload, compilation_data);
	progress->compile_workload->progress = progress;
    progress->compile_workload->functions = dynamic_array_create<ModTree_Function*>(1);
    dynamic_array_push_back(&progress->compile_workload->functions, progress->function);

    // Add dependencies between workloads
    if (base_progress == 0) {
        analysis_workload_add_dependency_internal(executer, upcast(progress->body_workload), upcast(progress->header_workload));
    }
    else {
        // Note: We run the body workload only after the base-analysis completely succeeded
		// Actually, this could currently be removed, but we'll keep it in for now
        analysis_workload_add_dependency_internal(executer, upcast(progress->body_workload), upcast(base_progress->body_workload));
    }
    analysis_workload_add_dependency_internal(executer, upcast(progress->compile_workload), upcast(progress->body_workload));

    return progress;
}

// Creates member-types and sub-types, and also checks if the member names are valid...
// Note: We want to know the subtypes so that subtype creation can check the subtypes of a non-analysed struct...
void add_struct_members_empty_recursive(
    Datatype_Struct* structure, Workload_Structure_Body* workload, Dynamic_Array<AST::Structure_Member_Node*> member_nodes, bool report_errors, 
    Dynamic_Array<AST::Parameter*> struct_params, Semantic_Context* semantic_context)
{
	auto type_system = semantic_context->compilation_data->type_system;
	auto& types = type_system->predefined_types;

    assert(structure->members.size == 0, "");
    assert(structure->subtypes.size == 0, "");

    for (int i = 0; i < member_nodes.size; i++) 
    {
        auto member_node = member_nodes[i];
        if (member_node->is_expression) {
            struct_add_member(structure, member_node->name, types.unknown_type, upcast(member_node));
        }
        else {
			auto subtype = type_system_make_struct_empty(type_system, member_node->name, false, structure, workload);
            add_struct_members_empty_recursive(subtype, workload, member_node->options.subtype_members, report_errors, struct_params, semantic_context);
        }

        // Check if this name is already defined on the same level
        if (report_errors)
        {
            // Check other members
            for (int j = 0; j < i; j++) {
                auto other = member_nodes[j];
                if (other->name == member_node->name) {
                    log_semantic_error(
						semantic_context, 
						"Struct member name is already defined in previous member", 
						upcast(member_node), Node_Section::IDENTIFIER
					);
                }
            }

            // Check struct params
            for (int j = 0; j < struct_params.size; j++) {
                auto param = struct_params[j];
                if (param->name == member_node->name) {
                    log_semantic_error(
						semantic_context, "Struct member name is already defined as struct parameter name", 
						upcast(member_node), Node_Section::IDENTIFIER
					);
                }
            }

            // Check parent_name
			if (structure->parent_struct != nullptr && member_node->name == structure->parent_struct->name) {
                log_semantic_error(
					semantic_context, 
					"Struct member name has the same name as struct/subtype name", 
					upcast(member_node), Node_Section::IDENTIFIER
				);
            }
        }
    }
}

Workload_Structure_Body* workload_structure_create(AST::Expression* struct_node, Symbol* symbol,
    bool is_polymorphic_instance, Semantic_Context* semantic_context, Symbol_Access_Level access_level = Symbol_Access_Level::GLOBAL)
{
    assert(struct_node->type == AST::Expression_Type::STRUCTURE_TYPE, "Has to be struct!");
    auto& struct_info = struct_node->options.structure;
	auto compilation_data = semantic_context->compilation_data;
	auto& ids = compilation_data->compiler->identifier_pool.predefined_ids;
	auto type_system = compilation_data->type_system;

    // Create body workload
    auto body_workload = workload_executer_allocate_workload<Workload_Structure_Body>(semantic_context->current_workload, compilation_data);
	body_workload->analysis_pass = nullptr;
    body_workload->struct_node = struct_node;
    body_workload->struct_type = type_system_make_struct_empty(
		type_system, (symbol == 0 ? ids.anon_struct : symbol->id), struct_info.is_union, nullptr, body_workload
	);
    add_struct_members_empty_recursive(
        body_workload->struct_type, body_workload, struct_info.members, !is_polymorphic_instance, struct_info.parameters, semantic_context
    );
    if (struct_info.is_union && body_workload->struct_type->subtypes.size > 0 && !is_polymorphic_instance) {
        log_semantic_error(semantic_context, "Union must not contain subtypes", upcast(struct_node), Node_Section::KEYWORD);
    }
    body_workload->polymorphic_type = Polymorphic_Analysis_Type::NON_POLYMORPHIC;
	body_workload->symbol_table = semantic_context->current_symbol_table;
	body_workload->symbol_access_level = access_level;

    if (is_polymorphic_instance) {
        body_workload->polymorphic_type = Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
        body_workload->symbol_table = nullptr; // This has to be set to base-analysis symbol table at some point
        // Note: Polymorphic instance infos aren't filled out here!
        return body_workload;
    }

    // Set Symbol
    if (symbol != 0) {
        symbol->type = Symbol_Type::DATATYPE;
        symbol->options.datatype = upcast(body_workload->struct_type);
    }

    // Check for polymorphism
    if (struct_info.parameters.size != 0) 
	{
        auto poly_workload = workload_executer_allocate_workload<Workload_Structure_Polymorphic>(semantic_context->current_workload, compilation_data);
        poly_workload->body_workload = body_workload;

        body_workload->polymorphic_type = Polymorphic_Analysis_Type::POLYMORPHIC_BASE;
        body_workload->polymorphic.base = poly_workload;

        // Polymorphic base info get's initilaized in header workload
        analysis_workload_add_dependency_internal(semantic_context->compilation_data->workload_executer, upcast(body_workload), upcast(poly_workload));
    }

    return body_workload;
}

Bake_Progress* bake_progress_create(AST::Expression* bake_expr, Datatype* expected_type, Semantic_Context* semantic_context)
{
    assert(bake_expr->type == AST::Expression_Type::BAKE, "Must be bake!");
	auto compilation_data = semantic_context->compilation_data;
	auto& ids = compilation_data->compiler->identifier_pool.predefined_ids;

	// Create progress
	auto progress = compilation_data->arena.allocate<Bake_Progress>();
    progress->bake_function = modtree_function_create_empty(compilation_data->empty_call_signature, ids.bake_function, compilation_data);
    progress->bake_function->function_type = ModTree_Function_Type::BAKE;
    progress->bake_function->options.bake = progress;
	progress->bake_node = bake_expr;
    progress->result = optional_make_failure<Upp_Constant>();
    progress->result_type = expected_type;
	progress->symbol_table = semantic_context->current_symbol_table;

    // Create workloads
    progress->analysis_workload = workload_executer_allocate_workload<Workload_Bake_Analysis>(semantic_context->current_workload, compilation_data);
    progress->analysis_workload->progress = progress;
    progress->execute_workload = workload_executer_allocate_workload<Workload_Bake_Execution>(semantic_context->current_workload, compilation_data);
    progress->execute_workload->progress = progress;
	progress->analysis_pass = analysis_pass_allocate(upcast(progress->analysis_workload), upcast(bake_expr), compilation_data);

    // Add dependencies between workloads
    analysis_workload_add_dependency_internal(compilation_data->workload_executer, upcast(progress->execute_workload), upcast(progress->analysis_workload));

    return progress;
}



// MOD_TREE (Note: Doesn't set symbol to anything!)
ModTree_Function* modtree_function_create_empty(Call_Signature* signature, String* name, Compilation_Data* compilation_data)
{
	auto& slots = compilation_data->function_slots;

	ModTree_Function* function = new ModTree_Function;
	function->signature = signature;
	function->function_slot_index = slots.size;
	function->name = name;

	Function_Slot slot;
	slot.index = slots.size;
	slot.modtree_function = function;
	slot.ir_function = nullptr;
	slot.bytecode_start_instruction = -1;
	slot.bytecode_end_instruction = -1;
	dynamic_array_push_back(&slots, slot);

	function->called_from = dynamic_array_create<ModTree_Function*>();
	function->calls = dynamic_array_create<ModTree_Function*>();
	function->contains_errors = false;
	function->is_runnable = true;

	dynamic_array_push_back(&compilation_data->program->functions, function);
	return function;
}

ModTree_Function* modtree_function_create_normal(
	Call_Signature* signature, Symbol* symbol, Function_Progress* progress, Symbol_Table* param_table, Compilation_Data* compilation_data)
{
	auto& ids = compilation_data->compiler->identifier_pool.predefined_ids;

	ModTree_Function* function = modtree_function_create_empty(signature, symbol == nullptr ? ids.lambda_function : symbol->id, compilation_data);
	function->function_type = ModTree_Function_Type::NORMAL;
	function->options.normal.progress = progress;
	function->options.normal.symbol = symbol;
	return function;
}

void modtree_function_destroy(ModTree_Function* function)
{
	dynamic_array_destroy(&function->called_from);
	dynamic_array_destroy(&function->calls);
	delete function;
}

ModTree_Global* modtree_program_add_global_assert_type_finished(Compilation_Data* compilation_data, Datatype* datatype, Symbol* symbol, bool is_extern)
{
	assert(datatype->memory_info.available, "");
	auto& memory_info = datatype->memory_info.value;

	auto program = compilation_data->program;

	auto global = new ModTree_Global;
	global->symbol = symbol;
	global->type = datatype;
	global->has_initial_value = false;
	global->init_expr = 0;
	global->is_extern = is_extern;
	global->index = program->globals.size;
	global->memory = compilation_data->arena.allocate_raw(memory_info.size, memory_info.alignment);
	dynamic_array_push_back(&program->globals, global);

	return global;
}

ModTree_Global* modtree_program_add_global(Semantic_Context* semantic_context, Datatype* datatype, Symbol* symbol, bool is_extern)
{
	type_wait_for_size_info_to_finish(datatype, semantic_context);
	return modtree_program_add_global_assert_type_finished(semantic_context->compilation_data, datatype, symbol, is_extern);
}

ModTree_Program* modtree_program_create()
{
	ModTree_Program* result = new ModTree_Program();
	result->main_function = 0;
	result->functions = dynamic_array_create<ModTree_Function*>(16);
	result->globals = dynamic_array_create<ModTree_Global*>(16);
	return result;
}

void modtree_program_destroy(ModTree_Program* program)
{
	for (int i = 0; i < program->globals.size; i++) {
		delete program->globals[i];
	}
	dynamic_array_destroy(&program->globals);
	for (int i = 0; i < program->functions.size; i++) {
		modtree_function_destroy(program->functions[i]);
	}
	dynamic_array_destroy(&program->functions);
	delete program;
}



// Comptime Values 
enum class Comptime_Result_Type
{
	AVAILABLE,
	UNAVAILABLE, // The expression is comptime, but not evaluable due to the context (E.g. errors, Polymorphic parameters)
	NOT_COMPTIME,
};

struct Comptime_Result
{
	Comptime_Result_Type type;
	void* data;
	Datatype* data_type;
	const char* message;
};

Comptime_Result expression_calculate_comptime_value_internal(AST::Expression* expr, Semantic_Context* semantic_context);

Comptime_Result comptime_result_make_available(void* data, Datatype* type) {
	Comptime_Result result;
	result.type = Comptime_Result_Type::AVAILABLE;
	result.data = data;
	result.data_type = type;
	result.message = "";
	return result;
}

Comptime_Result comptime_result_make_unavailable(Datatype* type, const char* message) {
	Comptime_Result result;
	result.type = Comptime_Result_Type::UNAVAILABLE;
	result.data_type = type;
	result.data = 0;
	result.message = message;
	return result;
}

Comptime_Result comptime_result_make_not_comptime(const char* message) {
	Comptime_Result result;
	result.type = Comptime_Result_Type::NOT_COMPTIME;
	result.data = 0;
	result.data_type = 0;
	result.message = message;
	return result;
}

Comptime_Result comptime_result_apply_cast(Comptime_Result value, const Cast_Info& cast_info, Semantic_Context* semantic_context)
{
	auto& arena = *semantic_context->scratch_arena;
	Datatype* result_type = cast_info.result_type;
	Type_System* type_system = semantic_context->compilation_data->type_system;

	if (value.type == Comptime_Result_Type::NOT_COMPTIME) {
		return comptime_result_make_not_comptime(value.message);
	}
	else if (value.type == Comptime_Result_Type::UNAVAILABLE) {
		return comptime_result_make_unavailable(result_type, value.message);
	}

	Instruction_Type instr_type = (Instruction_Type)-1;
	switch (cast_info.cast_type)
	{
	case Cast_Type::INVALID: return comptime_result_make_unavailable(result_type, "Expression contains invalid cast"); // Invalid means an error was already logged
	case Cast_Type::NO_CAST: return value;
	case Cast_Type::ADDRESS_OF: return comptime_result_make_not_comptime("Auto address-of can not be evaluated by comptime calculation");
	case Cast_Type::DEREFERENCE: return comptime_result_make_not_comptime("Dereference cannot be evaluated by comptime calculation");
	case Cast_Type::FLOATS: instr_type = Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE; break;
	case Cast_Type::FLOAT_TO_INT: instr_type = Instruction_Type::CAST_FLOAT_INTEGER; break;
	case Cast_Type::INT_TO_FLOAT: instr_type = Instruction_Type::CAST_INTEGER_FLOAT; break;
	case Cast_Type::ENUMS: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
	case Cast_Type::INTEGERS: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
	case Cast_Type::ENUM_TO_INT: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
	case Cast_Type::INT_TO_ENUM: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
	case Cast_Type::POINTERS:
	case Cast_Type::ADDRESS_TO_POINTER:
	case Cast_Type::POINTER_TO_ADDRESS: {
		// Pointers values can be interchanged with other pointers or with address values
		return comptime_result_make_available(value.data, result_type);
	}
	case Cast_Type::TO_SUB_TYPE:
	case Cast_Type::TO_BASE_TYPE: 
	case Cast_Type::ARRAY_TO_SLICE: {
		Upp_Slice_Base* slice = arena.allocate<Upp_Slice_Base>();
		slice->data = value.data;
		slice->size = downcast<Datatype_Array>(value.data_type)->element_count;
		return comptime_result_make_available(slice, result_type);
	}
	case Cast_Type::TO_ANY: {
		Upp_Any* any = arena.allocate<Upp_Any>();
		any->type = result_type->type_handle;
		any->data = value.data;
		return comptime_result_make_available(any, result_type);
	}
	case Cast_Type::FROM_ANY: {
		Upp_Any* given = (Upp_Any*)value.data;
		if (given->type.index >= (u32)type_system->types.size) {
			return comptime_result_make_not_comptime("Any contained invalid type_id");
		}
		Datatype* any_type = type_system->types[given->type.index];
		if (!types_are_equal(any_type, value.data_type)) {
			return comptime_result_make_not_comptime("Any type_handle value doesn't match result type, actually not sure if this can happen");
		}
		return comptime_result_make_available(given->data, any_type);
	}
	case Cast_Type::CUSTOM_CAST: {
		return comptime_result_make_not_comptime("Custom casts cannot be calculated at comptime");
	}
	default: panic("");
	}
	if ((int)instr_type == -1) panic("");

	type_wait_for_size_info_to_finish(result_type, semantic_context);
	auto& size_info = result_type->memory_info.value;
	void* result_buffer = arena.allocate_raw(size_info.size, size_info.alignment);
	bytecode_execute_cast_instr(
		instr_type, result_buffer, value.data,
		type_base_to_bytecode_type(result_type),
		type_base_to_bytecode_type(value.data_type)
	);
	return comptime_result_make_available(result_buffer, result_type);
}

Comptime_Result expression_calculate_comptime_value_without_context_cast(AST::Expression* expr, Semantic_Context* semantic_context);
Comptime_Result calculate_struct_initializer_comptime_recursive(
	Semantic_Context* semantic_context, Datatype* datatype, void* struct_buffer, AST::Call_Node* call_node)
{
	auto& arena = *semantic_context->scratch_arena;
	auto call_info = get_info(call_node, semantic_context);
	if (call_info->origin.type == Call_Origin_Type::UNION_INITIALIZER) {
		return comptime_result_make_not_comptime("Union comptime values are not possible");
	}
	assert(call_info->origin.type == Call_Origin_Type::STRUCT_INITIALIZER, "");
	if (!call_info->argument_matching_success) {
		return comptime_result_make_unavailable(datatype, "Init matcher was invalid");
	}

	for (int i = 0; i < call_info->parameter_values.size; i++)
	{
		auto& param_value = call_info->parameter_values[i];
		void* member_memory = nullptr;
		switch (param_value.value_type)
		{
		case Parameter_Value_Type::ARGUMENT_EXPRESSION:
		{
			auto& expr = call_info->argument_infos[param_value.options.argument_index].expression;
			assert(expr != nullptr, "");

			Comptime_Result result = expression_calculate_comptime_value_internal(expr, semantic_context);
			if (result.type != Comptime_Result_Type::AVAILABLE) {
				return result;
			}
			member_memory = result.data;
			break;
		}
		case Parameter_Value_Type::NOT_SET: {
			return comptime_result_make_unavailable(datatype, "Struct-inititalizer parameter not set");
		}
		case Parameter_Value_Type::COMPTIME_VALUE: {
			member_memory = param_value.options.constant.memory;
		}
		case Parameter_Value_Type::DATATYPE_KNOWN: {
			return comptime_result_make_unavailable(datatype, "Struct-inititalizer parameter only datatype known");
		}
		default: panic("");
		}

		auto& member = call_info->origin.options.structure->members[i];
		memcpy(((byte*)struct_buffer) + member.offset, member_memory, member.type->memory_info.value.size);
	}

	for (int i = 0; i < call_node->subtype_initializers.size; i++) {
		auto initializer = call_node->subtype_initializers[i];
		Comptime_Result result = calculate_struct_initializer_comptime_recursive(semantic_context, datatype, struct_buffer, initializer->call_node);
		if (result.type != Comptime_Result_Type::AVAILABLE) {
			return result;
		}
	}

	return comptime_result_make_available(struct_buffer, datatype);
}

Comptime_Result expression_calculate_comptime_value_without_context_cast(AST::Expression* expr, Semantic_Context* semantic_context)
{
	auto type_system = semantic_context->compilation_data->type_system;
	Predefined_Types& types = type_system->predefined_types;
	auto& arena = *semantic_context->scratch_arena;

	auto info = get_info(expr, semantic_context);
	auto result_datatype = expression_info_get_type(info, true, type_system);
	if (!info->is_valid) {
		return comptime_result_make_unavailable(result_datatype, "Analysis contained errors");
	}
	else if (datatype_is_unknown(result_datatype)) {
		return comptime_result_make_unavailable(result_datatype, "Analysis contained errors");
	}

	switch (info->result_type)
	{
	case Expression_Result_Type::CONSTANT: {
		auto& upp_const = info->options.constant;
		return comptime_result_make_available(upp_const.memory, upp_const.type);
	}
	case Expression_Result_Type::DATATYPE: {
		return comptime_result_make_available(&info->options.datatype->type_handle, types.type_handle);
	}
	case Expression_Result_Type::NOTHING: {
		return comptime_result_make_not_comptime("Void not comptime!");
	}
	case Expression_Result_Type::FUNCTION:
	{
		auto function = info->options.function;
		auto pointer_type = type_system_make_function_pointer(type_system, function->signature);
		i64* result_buffer = (i64*)arena.allocate_raw(
			pointer_type->base.memory_info.value.size,
			pointer_type->base.memory_info.value.alignment
		);
		*result_buffer = function->function_slot_index + 1;
		return comptime_result_make_available(result_buffer, upcast(pointer_type));
	}
	case Expression_Result_Type::POLYMORPHIC_PATTERN: {
		return comptime_result_make_not_comptime("Polymorphic-Pattern is not comptime");
	}
	case Expression_Result_Type::POLYMORPHIC_STRUCT: {
		return comptime_result_make_not_comptime("Cannot make comptime type_handle out of polymorphic struct");
	}
	case Expression_Result_Type::POLYMORPHIC_FUNCTION:
	case Expression_Result_Type::HARDCODED_FUNCTION: {
		return comptime_result_make_not_comptime("Cannot take comptime value of polymorphic/hardcoded functions");
	}
	case Expression_Result_Type::VALUE:
		break; // Rest of function
	default:panic("");
	}

	auto result_type_size = type_wait_for_size_info_to_finish(result_datatype, semantic_context);
	switch (expr->type)
	{
	case AST::Expression_Type::ARRAY_TYPE:
	case AST::Expression_Type::AUTO_ENUM:
	case AST::Expression_Type::BAKE:
	case AST::Expression_Type::ENUM_TYPE:
	case AST::Expression_Type::FUNCTION:
	case AST::Expression_Type::LITERAL_READ:
	case AST::Expression_Type::STRUCTURE_TYPE:
	case AST::Expression_Type::SLICE_TYPE:
	case AST::Expression_Type::POINTER_TYPE:
	case AST::Expression_Type::MODULE:
		panic("Should be handled above!");
	case AST::Expression_Type::ERROR_EXPR: {
		return comptime_result_make_unavailable(types.unknown_type, "Analysis contained errors");
	}
	case AST::Expression_Type::PATTERN_VARIABLE: {
		// Not-set, otherwise expression result-type would be comptime or pattern-type
		return comptime_result_make_unavailable(result_datatype, "In base analysis the value of the polymorphic symbol is not available!");
	}
	case AST::Expression_Type::PATH_LOOKUP:
	{
		auto symbol = get_info(expr->options.path_lookup, semantic_context)->symbol;
		if (symbol->type == Symbol_Type::PATTERN_VARIABLE)
		{
			Array<Pattern_Variable_State> active_pattern_values =
				pattern_variable_find_instance_workload(
					symbol->options.pattern_variable, semantic_context->current_workload
				)->active_pattern_variable_states;
			assert(active_pattern_values.data != 0,
				"In normal analysis we shouldn't be able to access this and in instance this would be already set");
			return comptime_result_make_unavailable(result_datatype, "Cannot access polymorphic parameter value in base analysis");
		}
		else if (symbol->type == Symbol_Type::ERROR_SYMBOL) {
			return comptime_result_make_unavailable(types.unknown_type, "Analysis contained error-type");
		}
		return comptime_result_make_not_comptime("Encountered non-comptime symbol");
	}
	case AST::Expression_Type::BINARY_OPERATION:
	{
		if (info->specifics.overload.function != 0) {
			return comptime_result_make_not_comptime("Overloaded operators cannot be calculated at comptime");
		}

		Comptime_Result left_val = expression_calculate_comptime_value_internal(expr->options.binop.left, semantic_context);
		Comptime_Result right_val = expression_calculate_comptime_value_internal(expr->options.binop.right, semantic_context);
		if (left_val.type != Comptime_Result_Type::AVAILABLE || right_val.type != Comptime_Result_Type::AVAILABLE)
		{
			if (left_val.type == Comptime_Result_Type::NOT_COMPTIME) {
				return left_val;
			}
			else if (right_val.type == Comptime_Result_Type::NOT_COMPTIME) {
				return right_val;
			}
			else if (left_val.type == Comptime_Result_Type::UNAVAILABLE) {
				return comptime_result_make_unavailable(result_datatype, left_val.message);
			}
			else {
				return comptime_result_make_unavailable(result_datatype, right_val.message);
			}
		}

		Instruction_Type instr_type;
		switch (expr->options.binop.type)
		{
		case AST::Binop::ADDITION: instr_type = Instruction_Type::BINARY_OP_ADDITION; break;
		case AST::Binop::SUBTRACTION: instr_type = Instruction_Type::BINARY_OP_SUBTRACTION; break;
		case AST::Binop::DIVISION: instr_type = Instruction_Type::BINARY_OP_DIVISION; break;
		case AST::Binop::MULTIPLICATION: instr_type = Instruction_Type::BINARY_OP_MULTIPLICATION; break;
		case AST::Binop::MODULO: instr_type = Instruction_Type::BINARY_OP_MODULO; break;
		case AST::Binop::AND: instr_type = Instruction_Type::BINARY_OP_AND; break;
		case AST::Binop::OR: instr_type = Instruction_Type::BINARY_OP_OR; break;
		case AST::Binop::EQUAL: instr_type = Instruction_Type::BINARY_OP_EQUAL; break;
		case AST::Binop::NOT_EQUAL: instr_type = Instruction_Type::BINARY_OP_NOT_EQUAL; break;
		case AST::Binop::LESS: instr_type = Instruction_Type::BINARY_OP_LESS_THAN; break;
		case AST::Binop::LESS_OR_EQUAL: instr_type = Instruction_Type::BINARY_OP_LESS_EQUAL; break;
		case AST::Binop::GREATER: instr_type = Instruction_Type::BINARY_OP_GREATER_THAN; break;
		case AST::Binop::GREATER_OR_EQUAL: instr_type = Instruction_Type::BINARY_OP_GREATER_EQUAL; break;
		case AST::Binop::POINTER_EQUAL: instr_type = Instruction_Type::BINARY_OP_EQUAL; break;
		case AST::Binop::POINTER_NOT_EQUAL: instr_type = Instruction_Type::BINARY_OP_NOT_EQUAL; break;
		case AST::Binop::INVALID: return comptime_result_make_unavailable(result_datatype, "Analysis error encountered");
		default: panic("");
		}

		void* result_buffer = arena.allocate_raw(result_type_size->size, result_type_size->alignment);
		if (bytecode_execute_binary_instr(instr_type, type_base_to_bytecode_type(left_val.data_type), result_buffer, left_val.data, right_val.data)) {
			return comptime_result_make_available(result_buffer, result_datatype);
		}
		else {
			return comptime_result_make_not_comptime("Bytecode instruction execution failed, e.g. division by zero or others");
		}
		break;
	}
	case AST::Expression_Type::UNARY_OPERATION:
	{
		Instruction_Type instr_type;
		switch (expr->options.unop.type)
		{
		case AST::Unop::NOT:
		case AST::Unop::NEGATE: {
			if (info->specifics.overload.function != 0) {
				return comptime_result_make_not_comptime("Operator overloads are not comptime");
			}
			instr_type = expr->options.unop.type == AST::Unop::NEGATE ?
				Instruction_Type::UNARY_OP_NEGATE : Instruction_Type::UNARY_OP_NOT;
			break;
		}
		case AST::Unop::ADDRESS_OF: {
			return comptime_result_make_not_comptime("Address of not supported for comptime values");
		}
		case AST::Unop::OPTIONAL_DEREFERENCE:
		case AST::Unop::DEREFERENCE:
			return comptime_result_make_not_comptime("Dereferencing not supported for comptime values");
		default: panic("");
		}

		Comptime_Result value = expression_calculate_comptime_value_internal(expr->options.unop.expr, semantic_context);
		if (value.type == Comptime_Result_Type::NOT_COMPTIME) {
			return value;
		}
		else if (value.type == Comptime_Result_Type::UNAVAILABLE) {
			return comptime_result_make_unavailable(result_datatype, value.message);
		}

		void* result_buffer = arena.allocate_raw(result_type_size->size, result_type_size->alignment);
		bytecode_execute_unary_instr(instr_type, type_base_to_bytecode_type(value.data_type), result_buffer, value.data);
		return comptime_result_make_available(result_buffer, result_datatype);
	}
	case AST::Expression_Type::CAST: 
	{
		auto call_info = get_info(expr->options.call.call_node, false);
		if (!call_info->instanciated) {
			return comptime_result_make_not_comptime("Cast contained errors");
		}

		auto arg_expr = call_info->argument_infos[call_info->parameter_values[0].options.argument_index].expression;
		Comptime_Result value_before_cast = expression_calculate_comptime_value_internal(arg_expr, semantic_context);
		if (value_before_cast.type != Comptime_Result_Type::AVAILABLE) {
			return value_before_cast;
		}
		return comptime_result_apply_cast(value_before_cast, call_info->instanciation_data.cast_info, semantic_context);
	}
	case AST::Expression_Type::ARRAY_ACCESS:
	{
		if (info->specifics.overload.function != 0) {
			return comptime_result_make_not_comptime("Overloaded operators are not comptime");
		}

		Comptime_Result value_array = expression_calculate_comptime_value_internal(expr->options.array_access.array_expr, semantic_context);
		Comptime_Result value_index = expression_calculate_comptime_value_internal(expr->options.array_access.index_expr, semantic_context);
		if (value_array.type == Comptime_Result_Type::NOT_COMPTIME) {
			return value_array;
		}
		else if (value_index.type == Comptime_Result_Type::NOT_COMPTIME) {
			return value_index;
		}
		else if (value_array.type == Comptime_Result_Type::UNAVAILABLE) {
			return comptime_result_make_unavailable(result_datatype, value_array.message);
		}
		else if (value_index.type == Comptime_Result_Type::UNAVAILABLE) {
			return comptime_result_make_unavailable(result_datatype, value_index.message);
		}
		assert(types_are_equal(value_index.data_type, upcast(types.i32_type)), "Must be i32 currently");

		byte* base_ptr = 0;
		int array_size = 0;
		if (value_array.data_type->type == Datatype_Type::ARRAY) {
			base_ptr = (byte*)value_array.data;
			array_size = (int)downcast<Datatype_Array>(value_array.data_type)->element_count;
		}
		else if (value_array.data_type->type == Datatype_Type::SLICE) {
			Upp_Slice_Base* slice = (Upp_Slice_Base*)value_array.data;
			base_ptr = (byte*)slice->data;
			array_size = slice->size;
		}
		else {
			panic("");
		}

		int index = *(int*)value_index.data;
		int element_offset = index * result_type_size->size;
		if (index >= array_size || index < 0) {
			return comptime_result_make_not_comptime("Array out of bounds access");
		}
		if (!memory_is_readable(base_ptr, result_type_size->size)) {
			return comptime_result_make_not_comptime("Slice/Array access to invalid memory");
		}
		return comptime_result_make_available(&base_ptr[element_offset], result_datatype);
	}
	case AST::Expression_Type::MEMBER_ACCESS:
	{
		auto& access_info = info->specifics.member_access;
		switch (access_info.type)
		{
		case Member_Access_Type::STRUCT_MEMBER_ACCESS:
		{
			Comptime_Result value_struct = expression_calculate_comptime_value_internal(expr->options.member_access.expr, semantic_context);
			if (value_struct.type == Comptime_Result_Type::NOT_COMPTIME) {
				return value_struct;
			}
			else if (value_struct.type == Comptime_Result_Type::UNAVAILABLE) {
				return comptime_result_make_unavailable(result_datatype, value_struct.message);
			}

			assert(access_info.type == Member_Access_Type::STRUCT_MEMBER_ACCESS, "");
			byte* raw_data = (byte*)value_struct.data;
			return comptime_result_make_available(&raw_data[access_info.options.member.offset], result_datatype);
		}
		case Member_Access_Type::STRUCT_POLYMORHPIC_PARAMETER_ACCESS: {
			assert(
				access_info.options.poly_access.struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE, 
				"In instance this should already be constant"
			);
			return comptime_result_make_unavailable(result_datatype, "Cannot access polymorphic parameter value in base analysis");
		}
		case Member_Access_Type::STRUCT_SUBTYPE: {
			panic("Should be handled by type_type!");
			return comptime_result_make_unavailable(result_datatype, "Invalid code-path!");
		}
		case Member_Access_Type::STRUCT_UP_OR_DOWNCAST: {
			Comptime_Result value_struct = expression_calculate_comptime_value_internal(expr->options.member_access.expr, semantic_context);
			if (value_struct.type != Comptime_Result_Type::AVAILABLE) {
				return value_struct;
			}
			return comptime_result_make_available(value_struct.data, result_datatype);
		}
		case Member_Access_Type::ENUM_MEMBER_ACCESS: {
			panic("Should be handled by type_type!");
			return comptime_result_make_unavailable(result_datatype, "Invalid code-path, should be handled by constant-path");
		}
		default: panic("");
		}

		panic("error");
		return comptime_result_make_unavailable(result_datatype, "error");
	}
	case AST::Expression_Type::FUNCTION_CALL: {
		return comptime_result_make_not_comptime("Function calls require #bake to be used as comtime values");
	}
	case AST::Expression_Type::INSTANCIATE: {
		return comptime_result_make_not_comptime("Instanciate must be successful to use as comptime value");
	}
	case AST::Expression_Type::GET_OVERLOAD: {
		return comptime_result_make_not_comptime("#get_overload was not successfull, so we don't have a comptime function here");
	}
	case AST::Expression_Type::NEW_EXPR: {
		// New is always uninitialized, so it cannot have a comptime value (Future: Maybe new with values)
		return comptime_result_make_not_comptime("New cannot be used in comptime values");
	}
	case AST::Expression_Type::ARRAY_INITIALIZER:
	{
		if (expr->options.array_initializer.values.size == 0) 
		{
			assert(result_datatype->type == Datatype_Type::SLICE, "");
			if (datatype_is_unknown(downcast<Datatype_Slice>(result_datatype)->element_type)) {
				return comptime_result_make_unavailable(types.unknown_type, "Array type is unknown");
			}
			void* result_buffer = arena.allocate_raw(result_type_size->size, result_type_size->alignment);
			Upp_Slice_Base* slice = (Upp_Slice_Base*)result_buffer;
			slice->size = 0;
			slice->data = nullptr;
			return comptime_result_make_available(result_buffer, result_datatype);
		}

		assert(result_datatype->type == Datatype_Type::ARRAY, "");
		auto array_type = downcast<Datatype_Array>(result_datatype);
		Datatype* element_type = array_type->element_type;
		if (datatype_is_unknown(element_type)) {
			return comptime_result_make_unavailable(element_type, "Array type is unknown");
		}
		assert(element_type->memory_info.available, "");
		if (!array_type->count_known) {
			return comptime_result_make_unavailable(result_datatype, "Array count is unknown");
		}

		void* result_buffer = arena.allocate_raw(result_type_size->size, result_type_size->alignment);
		auto array_values = expr->options.array_initializer.values;
		assert(array_values.size == array_type->element_count, "");

		bool values_unavailable = false;
		const char* first_unavailable_msg = nullptr;
		for (int i = 0; i < array_values.size; i++)
		{
			auto value_result = expression_calculate_comptime_value_internal(array_values[i], semantic_context);
			if (value_result.type != Comptime_Result_Type::AVAILABLE) {
				if (value_result.type == Comptime_Result_Type::NOT_COMPTIME) {
					return value_result;
				}
				values_unavailable = true;
				if (!values_unavailable) {
					first_unavailable_msg = value_result.message;
				}
				continue;
			}

			// Copy result into the array buffer
			{
				byte* raw = (byte*)result_buffer;
				int element_offset = element_type->memory_info.value.size * i;
				memory_copy(&raw[element_offset], value_result.data, element_type->memory_info.value.size);
			}
		}

		if (values_unavailable) {
			return comptime_result_make_unavailable(result_datatype, first_unavailable_msg);
		}

		return comptime_result_make_available(result_buffer, result_datatype);
	}
	case AST::Expression_Type::STRUCT_INITIALIZER:
	{
		auto call_info = get_info(expr->options.struct_initializer.call_node, semantic_context);
		if (call_info->origin.type == Call_Origin_Type::SLICE_INITIALIZER) {
			return comptime_result_make_unavailable(result_datatype, "Comptime slice initializer not implemented yet :P ");
		}

		void* result_buffer = arena.allocate_raw(result_type_size->size, result_type_size->alignment);
		memory_set_bytes(result_buffer, result_type_size->size, 0);

		// First, set all tags to correct values
		Datatype_Struct* structure = downcast<Datatype_Struct>(result_datatype);
		while (structure->parent_struct != nullptr)
		{
			int tag_value = structure->subtype_index + 1;
			int* tag_pointer = (int*)(((byte*)result_buffer) + structure->parent_struct->tag_member.offset);
			*tag_pointer = tag_value;
			structure = structure->parent_struct;
		}

		Comptime_Result result = calculate_struct_initializer_comptime_recursive(
			semantic_context, result_datatype, result_buffer, expr->options.struct_initializer.call_node
		);
		if (result.type != Comptime_Result_Type::AVAILABLE) {
			return result;
		}
		return comptime_result_make_available(result_buffer, result_datatype);
	}
	default: panic("");
	}

	panic("");
	return comptime_result_make_not_comptime("Invalid code path");
}

Comptime_Result expression_calculate_comptime_value_internal(AST::Expression* expr, Semantic_Context* semantic_context)
{
	auto info = get_info(expr, semantic_context);
	auto result_no_context = expression_calculate_comptime_value_without_context_cast(expr, semantic_context);
	if (result_no_context.type != Comptime_Result_Type::AVAILABLE) {
		return result_no_context;
	}

	return comptime_result_apply_cast(result_no_context, info->cast_info, semantic_context);
}

Optional<Upp_Constant> expression_calculate_comptime_value(
	AST::Expression* expr, const char* error_message_on_failure, Semantic_Context* semantic_context, bool* was_not_available = 0)
{
	if (was_not_available != 0) {
		*was_not_available = false;
	}

	auto compilation_data = semantic_context->compilation_data;
	Arena& arena = *semantic_context->scratch_arena;
	auto checkpoint = arena.make_checkpoint();
	SCOPE_EXIT(arena.rewind_to_checkpoint(checkpoint));

	Comptime_Result comptime_result = expression_calculate_comptime_value_internal(expr, semantic_context);
	if (comptime_result.type != Comptime_Result_Type::AVAILABLE) {
		if (comptime_result.type == Comptime_Result_Type::NOT_COMPTIME) {
			log_semantic_error(semantic_context, error_message_on_failure, expr);
			log_error_info_comptime_msg(semantic_context, comptime_result.message);
		}
		else {
			if (was_not_available != 0) {
				*was_not_available = true;
			}
			semantic_analyser_set_error_flag(true, semantic_context);
		}
		return optional_make_failure<Upp_Constant>();
	}

	assert(!type_size_is_unfinished(comptime_result.data_type), "");
	auto bytes = array_create_static<byte>((byte*)comptime_result.data, comptime_result.data_type->memory_info.value.size);
	Constant_Pool_Result result = constant_pool_add_constant(compilation_data->constant_pool, comptime_result.data_type, bytes);
	if (!result.success) {
		log_semantic_error(semantic_context, error_message_on_failure, expr);
		log_error_info_constant_status(semantic_context, result.options.error_message);
		return optional_make_failure<Upp_Constant>();
	}

	return optional_make_success(result.options.constant);
}

Datatype* upp_constant_as_datatype(Upp_Constant constant, Type_System* type_system) 
{
	auto& types = type_system->predefined_types;
	assert(datatype_is_primitive_class(constant.type, Primitive_Class::TYPE_HANDLE), "");

	// Note: Constant pool already checks type handles for correctness
	Upp_Type_Handle type_handle = upp_constant_to_value<Upp_Type_Handle>(constant);
	return type_system->types[type_handle.index];
}


// Context
Expression_Context expression_context_make_unspecified() {
	Expression_Context context;
	context.type = Expression_Context_Type::NOT_SPECIFIED;
	context.datatype = nullptr;
	return context;
}

Expression_Context expression_context_make_dereference() {
	Expression_Context context;
	context.type = Expression_Context_Type::AUTO_DEREFERENCE;
	context.datatype = nullptr;
	return context;
}

Expression_Context expression_context_make_error() {
	Expression_Context context;
	context.type = Expression_Context_Type::ERROR_OCCURED;
	context.datatype = nullptr;
	return context;
}

Expression_Context expression_context_make_specific_type(Datatype* signature) {
	if (datatype_is_unknown(signature)) {
		return expression_context_make_error();
	}
	Expression_Context context;
	context.type = Expression_Context_Type::SPECIFIC_TYPE_EXPECTED;
	context.datatype = signature;
	return context;
}

Parameter_Value parameter_value_make_unset() {
	Parameter_Value result;
	result.value_type = Parameter_Value_Type::NOT_SET;
	return result;
}

Parameter_Value parameter_value_make_datatype_known(Datatype* datatype) {
	Parameter_Value result;
	result.value_type = Parameter_Value_Type::DATATYPE_KNOWN;
	result.options.datatype = datatype;
	return result;
}

Parameter_Value parameter_value_make_comptime(Upp_Constant& constant) {
	Parameter_Value result;
	result.value_type = Parameter_Value_Type::COMPTIME_VALUE;
	result.options.constant = constant;
	return result;
}

Datatype* parameter_value_get_datatype(Parameter_Value& value, Call_Info* call_info, Semantic_Context* semantic_context, bool* out_is_temporary = nullptr)
{
	auto type_system = semantic_context->compilation_data->type_system;
	auto types = type_system->predefined_types;

	bool is_temporary = true;
	SCOPE_EXIT(if (out_is_temporary != nullptr) *out_is_temporary = is_temporary;);
	switch (value.value_type)
	{
	case Parameter_Value_Type::NOT_SET: panic(""); break;
	case Parameter_Value_Type::ARGUMENT_EXPRESSION: 
	{
		auto& arg_info = call_info->argument_infos[value.options.argument_index];
		auto info = expression_info_get_value_info(get_info(arg_info.expression, semantic_context), type_system);
		is_temporary = info.result_value_is_temporary;
		return info.result_type;
	}
	case Parameter_Value_Type::DATATYPE_KNOWN: {
		is_temporary = true;
		return value.options.datatype;
	}
	case Parameter_Value_Type::COMPTIME_VALUE: {
		is_temporary = false;
		return value.options.constant.type;
	}
	default: panic("");
	}
	panic("");
	return nullptr;
}

void call_info_set_parameter_to_expression(Call_Info* call_info, int param_index, int argument_index, AST::Expression* expression)
{
	auto& param_value = call_info->parameter_values[param_index];
	param_value.value_type = Parameter_Value_Type::ARGUMENT_EXPRESSION;
	param_value.options.argument_index = argument_index;

	auto& arg_info = call_info->argument_infos[argument_index];
	arg_info.expression = expression;
	arg_info.name = optional_make_failure<String*>();
	arg_info.parameter_index = param_index;
}



// Expression_Info
Cast_Info cast_info_make_simple(Datatype* result_type, Cast_Type cast_type = Cast_Type::NO_CAST) {
	Cast_Info cast_info;
	cast_info.result_type = result_type;
	cast_info.cast_type = cast_type;
	cast_info.custom_cast_function = nullptr;
	return cast_info;
}

Expression_Info expression_info_make_empty(Expression_Context context, Type_System* type_system)
{
	auto types = type_system->predefined_types;

	Expression_Info result;
	result.context = context;
	result.result_type = Expression_Result_Type::NOTHING;
	result.is_valid = false;
	result.cast_info = cast_info_make_simple(types.invalid_type, Cast_Type::NO_CAST);
	return result;
}

void expression_info_set_value(Expression_Info* info, Datatype* result_type, bool is_temporary)
{
	assert(!result_type->contains_pattern, "I guess this should only be true for set_poly_pattern");
	info->result_type = Expression_Result_Type::VALUE;
	info->is_valid = true;
	info->options.value.datatype = result_type;
	info->options.value.is_temporary = is_temporary;
	info->cast_info = cast_info_make_simple(result_type);
}

void expression_info_set_error(Expression_Info* info, Datatype* result_type, Semantic_Context* semantic_context)
{
	auto types = semantic_context->compilation_data->type_system->predefined_types;
	expression_info_set_value(info, types.unknown_type, false);
	info->is_valid = false;
	semantic_analyser_set_error_flag(true, semantic_context);
}

void expression_info_set_function(Expression_Info* info, ModTree_Function* function, Semantic_Context* semantic_context)
{
	auto type_system = semantic_context->compilation_data->type_system;
	auto& types = type_system->predefined_types;

	info->result_type = Expression_Result_Type::FUNCTION;
	info->is_valid = true;
	info->options.function = function;
	if (function->function_type == ModTree_Function_Type::NORMAL && function->options.normal.progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
		info->cast_info = cast_info_make_simple(types.invalid_type);
	}
	else {
		info->cast_info = cast_info_make_simple(upcast(type_system_make_function_pointer(type_system, function->signature)));
	}
}

void expression_info_set_hardcoded(Expression_Info* info, Hardcoded_Type hardcoded)
{
	info->result_type = Expression_Result_Type::HARDCODED_FUNCTION;
	info->is_valid = true;
	info->options.hardcoded = hardcoded;
}

void expression_info_set_datatype(Expression_Info* info, Datatype* datatype, Type_System* type_system)
{
	auto& types = type_system->predefined_types;

	assert(!datatype->contains_pattern, ""); // Otherwise this should be a polymorphic pattern
	info->result_type = Expression_Result_Type::DATATYPE;
	info->is_valid = true;
	info->options.datatype = datatype;
	info->cast_info = cast_info_make_simple(types.type_handle);
}

void expression_info_set_polymorphic_pattern(Expression_Info* info, Datatype* pattern_type, Type_System* type_system)
{
	auto& types = type_system->predefined_types;
	assert(pattern_type->contains_pattern, ""); // Otherwise this should be a normal type...
	info->result_type = Expression_Result_Type::POLYMORPHIC_PATTERN;
	info->is_valid = true;
	info->options.polymorphic_pattern = pattern_type;
	info->cast_info = cast_info_make_simple(pattern_type);
}

void expression_info_set_no_value(Expression_Info* info)
{
	info->result_type = Expression_Result_Type::NOTHING;
	info->is_valid = true;
}

void expression_info_set_polymorphic_function(Expression_Info* info, Poly_Function poly_function)
{
	info->result_type = Expression_Result_Type::POLYMORPHIC_FUNCTION;
	info->is_valid = true;
	info->options.poly_function = poly_function;
}

void expression_info_set_polymorphic_struct(Expression_Info* info, Workload_Structure_Polymorphic* poly_struct_workload)
{
	info->result_type = Expression_Result_Type::POLYMORPHIC_STRUCT;
	info->is_valid = true;
	info->options.polymorphic_struct = poly_struct_workload;
}

void expression_info_set_constant(Expression_Info* info, Upp_Constant constant) 
{
	info->result_type = Expression_Result_Type::CONSTANT;
	info->is_valid = true;
	info->options.constant = constant;
	info->cast_info = cast_info_make_simple(constant.type);
}

void expression_info_set_constant(
	Expression_Info* info, Datatype* signature, Array<byte> bytes, AST::Node* error_report_node, Semantic_Context* semantic_context)
{
	auto result = constant_pool_add_constant(semantic_context->compilation_data->constant_pool, signature, bytes);
	if (!result.success)
	{
		assert(error_report_node != 0, "Error"); // Error report node may only be null if we know that adding the constant cannot fail.
		log_semantic_error(semantic_context, "Value cannot be converted to constant value (Not serializable)", error_report_node);
		log_error_info_constant_status(semantic_context, result.options.error_message);
		expression_info_set_error(info, signature, semantic_context);
		return;
	}
	expression_info_set_constant(info, result.options.constant);
}

void expression_info_set_constant_enum(Expression_Info* info, Datatype* enum_type, i32 value, Semantic_Context* semantic_context) {
	expression_info_set_constant(
		info, enum_type, array_create_static((byte*)&value, sizeof(i32)), nullptr, semantic_context
	);
}

void expression_info_set_constant_i32(Expression_Info* info, i32 value, Semantic_Context* semantic_context) {
	auto& types = semantic_context->compilation_data->type_system->predefined_types;
	expression_info_set_constant(
		info, upcast(types.i32_type), array_create_static((byte*)&value, sizeof(i32)), nullptr, semantic_context
	);
}

void expression_info_set_constant_usize(Expression_Info* info, usize value, Semantic_Context* semantic_context) {
	auto& types = semantic_context->compilation_data->type_system->predefined_types;
	expression_info_set_constant(
		info, upcast(types.usize), array_create_static((byte*)&value, sizeof(usize)), nullptr, semantic_context
	);
}

void expression_info_set_constant_u32(Expression_Info* info, u32 value, Semantic_Context* semantic_context) {
	auto& types = semantic_context->compilation_data->type_system->predefined_types;

	expression_info_set_constant(
		info, upcast(types.u32_type), array_create_static((byte*)&value, sizeof(u32)), nullptr, semantic_context
	);
}

Expression_Value_Info expression_info_get_value_info(Expression_Info* info, Type_System* type_system)
{
	auto& types = type_system->predefined_types;

	Expression_Value_Info value_info;
	switch (info->result_type)
	{
	case Expression_Result_Type::VALUE: {
		value_info.initial_type = info->options.value.datatype;
		value_info.initial_value_is_temporary = info->options.value.is_temporary;
		break;
	}
	case Expression_Result_Type::DATATYPE: {
		value_info.initial_type = types.type_handle;
		value_info.initial_value_is_temporary = true;
		break;
	}
	case Expression_Result_Type::CONSTANT: {
		value_info.initial_type = info->options.constant.type;
		value_info.initial_value_is_temporary = true;
		break;
	}
	case Expression_Result_Type::FUNCTION: {
		value_info.initial_type = upcast(type_system_make_function_pointer(type_system, info->options.function->signature));
		value_info.initial_value_is_temporary = true;
		break;
	}
	case Expression_Result_Type::NOTHING: {
		value_info.initial_type = upcast(types.empty_struct_type);
		value_info.initial_value_is_temporary = true;
		break;
	}
	case Expression_Result_Type::POLYMORPHIC_PATTERN: {
		value_info.initial_type = info->options.polymorphic_pattern;
		value_info.initial_value_is_temporary = true;
		break;
	}
	case Expression_Result_Type::POLYMORPHIC_STRUCT: 
	case Expression_Result_Type::HARDCODED_FUNCTION: 
	case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
		value_info.initial_type = types.invalid_type;
		value_info.initial_value_is_temporary = true;
		break;
	}
	default: panic("");
	}
	
	if (info->cast_info.cast_type == Cast_Type::NO_CAST) {
		value_info.result_type = value_info.initial_type;
		value_info.result_value_is_temporary = value_info.initial_value_is_temporary;
	}
	else {
		value_info.result_type = info->cast_info.result_type;
		value_info.result_value_is_temporary = cast_type_result_is_temporary(info->cast_info.cast_type, value_info.initial_value_is_temporary);
	}

	return value_info;
}

Datatype* expression_info_get_type(Expression_Info* info, bool before_context_is_applied, Type_System* type_system)
{
	if (!before_context_is_applied) {
		return info->cast_info.result_type;
	}
	return expression_info_get_value_info(info, type_system).initial_type;
}

bool expression_has_memory_address(AST::Expression* expr, Semantic_Context* semantic_context) {
	return 
		!expression_info_get_value_info(
			get_info(expr, semantic_context), semantic_context->compilation_data->type_system
		).result_value_is_temporary;
}



// SYMBOL/PATH LOOKUP

// This will set the read + the non-resolved symbol paths to error
void path_lookup_set_info_to_error_symbol(AST::Path_Lookup* path, Semantic_Context* semantic_context)
{
	auto compilation_data = semantic_context->compilation_data;
	auto pass = semantic_context->current_pass;
	auto error_symbol = compilation_data->error_symbol;

	// Set unset path nodes to error
	for (int i = 0; i < path->parts.size; i++) {
		pass_get_node_info(pass, path->parts[i], Info_Query::CREATE_IF_NULL, compilation_data)->symbol = error_symbol;
	}
	// Set final path result to error
	pass_get_node_info(pass, path, Info_Query::CREATE_IF_NULL, compilation_data)->symbol = error_symbol;
}

void path_lookup_set_result_symbol(AST::Path_Lookup* path, Symbol* symbol, Semantic_Context* semantic_context) 
{
	auto pass = semantic_context->current_pass;
	auto compilation_data = semantic_context->compilation_data;

	// Set last symbol_read to symbol
	pass_get_node_info(pass, path->last, Info_Query::CREATE_IF_NULL, compilation_data)->symbol = symbol;
	// Set whole read to symbol
	pass_get_node_info(pass, path, Info_Query::CREATE_IF_NULL, compilation_data)->symbol = symbol;
	// If symbol is not error, add reference to symbol
	if (symbol->type != Symbol_Type::ERROR_SYMBOL) {
		dynamic_array_push_back(&symbol->references, path->last);
	}
}

// Logs an error if overloaded symbols was found
Symbol* symbol_lookup_resolve_to_single_symbol(
	AST::Symbol_Lookup* lookup, Symbol_Table* symbol_table, Symbol_Query_Info query_info, Semantic_Context* semantic_context,
	bool prefer_module_symbols_on_overload = false, bool resolve_aliases = true)
{
	Compilation_Data* compilation_data = semantic_context->compilation_data;
	// Note: if this fails: we expect paths and lookups to be initialized to error symbol with path_lookup_set_info_to_error
	auto info = pass_get_node_info(semantic_context->current_pass, lookup, Info_Query::READ_NOT_NULL, compilation_data);
	auto error = compilation_data->error_symbol;

	if (lookup->is_root_module) {
		info->symbol = compilation_data->builtin_module->options.module_symbol;
		return info->symbol;
	}

	// Find all overloads
	Arena* arena = semantic_context->scratch_arena;
	auto checkpoint = arena->make_checkpoint();
	SCOPE_EXIT(arena->rewind_to_checkpoint(checkpoint));

	DynArray<Symbol*> results = symbol_table_query_id(symbol_table, lookup->name, query_info, arena);
	if (resolve_aliases) {
		symbol_table_query_resolve_aliases(results);
	}

	// Handle result array
	if (results.size == 0) {
		log_semantic_error(semantic_context, "Could not resolve Symbol (No definition found)", upcast(lookup));
		info->symbol = error;
	}
	else if (results.size == 1) {
		info->symbol = results[0];
		dynamic_array_push_back(&info->symbol->references, lookup);
	}
	else // size > 1
	{
		if (prefer_module_symbols_on_overload)
		{
			Symbol* found_symbol = 0;
			bool multiple_modules_found = false;
			for (int i = 0; i < results.size; i++) {
				auto symbol = results[i];
				if (symbol->type == Symbol_Type::MODULE) {
					if (found_symbol == 0) {
						found_symbol = symbol;
					}
					else {
						multiple_modules_found = true;
					}
				}
			}

			if (found_symbol != 0 && !multiple_modules_found) {
				info->symbol = found_symbol;
				dynamic_array_push_back(&info->symbol->references, lookup);
				return info->symbol;
			}
		}
		else
		{
			// If there is only one non-module symbol, take that one
			Symbol* non_module_symbol = 0;
			bool multiple_non_module_symbols_found = false;
			for (int i = 0; i < results.size; i++) {
				auto symbol = results[i];
				if (symbol->type != Symbol_Type::MODULE) {
					if (non_module_symbol == 0) {
						non_module_symbol = symbol;
					}
					else {
						multiple_non_module_symbols_found = true;
					}
				}
			}
			if (non_module_symbol != 0 && !multiple_non_module_symbols_found) {
				info->symbol = non_module_symbol;
				dynamic_array_push_back(&info->symbol->references, lookup);
				return info->symbol;
			}
		}

		log_semantic_error(semantic_context, "Multiple results found for this symbol, cannot decided", upcast(lookup));
		for (int i = 0; i < results.size; i++) {
			log_error_info_symbol(semantic_context, results[i]);
		}
		info->symbol = error;
	}

	return info->symbol;
}

// Returns 0 if the path could not be resolved
Symbol_Table* path_lookup_resolve_only_path_parts(
	AST::Path_Lookup* path, Symbol_Query_Info& out_final_symbol_query_info, Semantic_Context* semantic_context)
{
	// Initialize info to error symbol
	path_lookup_set_info_to_error_symbol(path, semantic_context);

	auto table = semantic_context->current_symbol_table;
	auto pass  = semantic_context->current_pass;
	auto error = semantic_context->compilation_data->error_symbol;

	out_final_symbol_query_info = symbol_query_info_make(
		semantic_context->symbol_access_level, path->is_dot_call_lookup ? Import_Type::SYMBOLS : Import_Type::DOT_CALLS, true
	);
	if (path->parts.size != 1) { // When a path is specified always use the weakest access level
		out_final_symbol_query_info.access_level = Symbol_Access_Level::GLOBAL;
		out_final_symbol_query_info.import_search_type = Import_Type::NONE;
	}

	// Resolve path
	for (int i = 0; i < path->parts.size - 1; i++)
	{
		auto part = path->parts[i];

		// Find symbol of path part
		Symbol_Query_Info query_info = symbol_query_info_make(
			Symbol_Access_Level::GLOBAL, (i == 0 ? Import_Type::SYMBOLS : Import_Type::NONE), i == 0
		);
		Symbol* symbol = symbol_lookup_resolve_to_single_symbol(part, table, query_info, semantic_context, true, true);

		// Check for errors
		if (symbol->type != Symbol_Type::MODULE)
		{
			if (symbol->type == Symbol_Type::ERROR_SYMBOL) {
				return nullptr; // Error should have already been reported
			}

			// Report error and exit
			if (symbol->type == Symbol_Type::DEFINITION_UNFINISHED) {
				// FUTURE: It may be possible that symbol resolution needs to create dependencies itself, which would happen here!
				log_semantic_error(semantic_context, "Expected module, not a definition (global/comptime)", upcast(part));
			}
			else {
				log_semantic_error(semantic_context, "Expected Module as intermediate path nodes", upcast(part));
				log_error_info_symbol(semantic_context, symbol);
			}
			return nullptr;
		}

		table = symbol->options.upp_module->symbol_table;
	}

	return table;
}

// Note: After resolving overloaded symbols, the caller should 
//    1. Set the symbol_info for the symbol_read
//    2. add a reference to the symbol
// Returns true if the path was resolved, otherwise false if there was an error while resolving the path...
DynArray<Symbol*> path_lookup_resolve(AST::Path_Lookup* path, Arena* arena, bool resolve_aliases, Semantic_Context* semantic_context)
{
	auto compilation_data = semantic_context->compilation_data;

	Symbol_Query_Info query_info;
	auto symbol_table = path_lookup_resolve_only_path_parts(path, query_info, semantic_context);
	if (symbol_table == nullptr) {
		return DynArray<Symbol*>::create(arena);
	}

	// Handle special case of ~
	if (path->last->is_root_module) {
		DynArray<Symbol*> results = DynArray<Symbol*>::create(arena, 1);
		results.push_back(compilation_data->builtin_module->options.module_symbol);
		return results;
	}

	// Resolve symbol
	auto results = symbol_table_query_id(symbol_table, path->last->name, query_info, arena);
	if (resolve_aliases) {
		symbol_table_query_resolve_aliases(results);
	}
	return results;
}

Symbol* path_lookup_resolve_to_single_symbol(
	AST::Path_Lookup* path, bool prefer_module_symbols_on_overload, Semantic_Context* semantic_context, bool resolve_aliases = true)
{
	path_lookup_set_info_to_error_symbol(path, semantic_context);
	auto error = semantic_context->compilation_data->error_symbol;

	// Resolve path
	Symbol_Query_Info query_info;
	auto symbol_table = path_lookup_resolve_only_path_parts(path, query_info, semantic_context);
	if (symbol_table == nullptr) {
		return error;
	}

	Symbol* symbol = symbol_lookup_resolve_to_single_symbol(
		path->last, symbol_table, query_info, semantic_context, prefer_module_symbols_on_overload, resolve_aliases
	);
	path_lookup_set_result_symbol(path, symbol, semantic_context);
	return symbol;
}



//DEPENDENCY GRAPH/WORKLOAD EXECUTION
Workload_Pair workload_pair_create(Workload_Base* workload, Workload_Base* depends_on) {
	Workload_Pair result;
	result.depends_on = depends_on;
	result.workload = workload;
	return result;
}

u64 workload_pair_hash(Workload_Pair* pair) {
	return hash_memory(array_create_static_as_bytes(pair, 1));
}

bool workload_pair_equals(Workload_Pair* p1, Workload_Pair* p2) {
	return p1->depends_on == p2->depends_on && p1->workload == p2->workload;
}

Workload_Executer* workload_executer_create(Fiber_Pool* fiber_pool, Compilation_Data* compilation_data)
{
	Workload_Executer* workload_executer = new Workload_Executer;
	workload_executer->compilation_data = compilation_data;
	workload_executer->fiber_pool = fiber_pool;
	workload_executer->all_workloads      = dynamic_array_create<Workload_Base*>();
	workload_executer->runnable_workloads = dynamic_array_create<Workload_Base*>();
	workload_executer->finished_workloads = dynamic_array_create<Workload_Base*>();
	workload_executer->workload_dependencies = hashtable_create_empty<Workload_Pair, Dependency_Information>(8, workload_pair_hash, workload_pair_equals);
	workload_executer->progress_was_made = false;
	return workload_executer;
}

void workload_executer_destroy(Workload_Executer* executer)
{
	for (int i = 0; i < executer->all_workloads.size; i++) {
		analysis_workload_destroy(executer->all_workloads[i]);
	}

	dynamic_array_destroy(&executer->all_workloads);
	dynamic_array_destroy(&executer->runnable_workloads);
	dynamic_array_destroy(&executer->finished_workloads);

	{
		auto iter = hashtable_iterator_create(&executer->workload_dependencies);
		while (hashtable_iterator_has_next(&iter)) {
			SCOPE_EXIT(hashtable_iterator_next(&iter));
			auto& dep_info = iter.value;
			dynamic_array_destroy(&dep_info->fail_indicators);
		}
		hashtable_destroy(&executer->workload_dependencies);
	}

	delete executer;
}

void analysis_workload_destroy(Workload_Base* workload)
{
	switch (workload->type)
	{
	case Analysis_Workload_Type::FUNCTION_HEADER: {
		auto function_header = downcast<Workload_Function_Header>(workload);
		if (function_header->poly_header != nullptr) {
			poly_header_destroy(function_header->poly_header);
		}
		break;
	}
	case Analysis_Workload_Type::STRUCT_POLYMORPHIC: {
		auto poly = downcast<Workload_Structure_Polymorphic>(workload);
		poly_header_destroy(poly->poly_header);
		break;
	}
	case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE: {
		auto cluster = downcast<Workload_Function_Cluster_Compile>(workload);
		dynamic_array_destroy(&cluster->functions);
		break;
	}
	default: break;
	}

	list_destroy(&workload->dependencies);
	list_destroy(&workload->dependents);
	dynamic_array_destroy(&workload->reachable_clusters);
}

void analysis_workload_add_dependency_internal(
	Workload_Executer* executer, Workload_Base* workload, Workload_Base* dependency, Dependency_Failure_Info failure_info)
{
	bool can_be_broken = failure_info.fail_indicator != 0;
	if (can_be_broken) {
		*failure_info.fail_indicator = !dependency->is_finished;
	}
	if (dependency->is_finished) {
		return;
	}

	Workload_Pair pair = workload_pair_create(workload, dependency);
	Dependency_Information* infos = hashtable_find_element(&executer->workload_dependencies, pair);
	if (infos == 0) {
		Dependency_Information info;
		info.dependency_node = list_add_at_end(&workload->dependencies, dependency);
		info.dependent_node = list_add_at_end(&dependency->dependents, workload);
		info.fail_indicators = dynamic_array_create<Dependency_Failure_Info>(1);
		info.can_be_broken = can_be_broken;
		if (can_be_broken) {
			dynamic_array_push_back(&info.fail_indicators, failure_info);
		}
		bool inserted = hashtable_insert_element(&executer->workload_dependencies, pair, info);
		assert(inserted, "");
	}
	else {
		if (can_be_broken) {
			dynamic_array_push_back(&infos->fail_indicators, failure_info);
		}
		else {
			infos->can_be_broken = false;
		}
	}
}

void workload_executer_move_dependency(Workload_Executer* executer, Workload_Base* move_from, Workload_Base* move_to, Workload_Base* dependency)
{
	auto graph = executer;
	assert(move_from != move_to, "");

	// Remove old dependency
	Workload_Pair original_pair = workload_pair_create(move_from, dependency);
	Dependency_Information info = *hashtable_find_element(&graph->workload_dependencies, original_pair);
	hashtable_remove_element(&graph->workload_dependencies, original_pair);
	list_remove_node(&move_from->dependencies, info.dependency_node);
	list_remove_node(&dependency->dependents, info.dependent_node);

	// Add new dependency, reusing old information in the process
	Workload_Pair new_pair = workload_pair_create(move_to, dependency);
	Dependency_Information* new_infos = hashtable_find_element(&graph->workload_dependencies, new_pair);
	if (new_infos == 0) {
		Dependency_Information new_info;
		new_info.dependency_node = list_add_at_end(&move_to->dependencies, dependency);
		new_info.dependent_node = list_add_at_end(&dependency->dependents, move_to);
		new_info.fail_indicators = info.fail_indicators; // Note: Takes ownership
		new_info.can_be_broken = info.can_be_broken;
		hashtable_insert_element(&graph->workload_dependencies, new_pair, new_info);
	}
	else {
		dynamic_array_append_other(&new_infos->fail_indicators, &info.fail_indicators);
		if (new_infos->can_be_broken) {
			new_infos->can_be_broken = info.can_be_broken;
		}
		dynamic_array_destroy(&info.fail_indicators);
	}
}

void workload_executer_remove_dependency(
	Workload_Executer* executer, Workload_Base* workload, Workload_Base* depends_on, bool allow_add_to_runnables, bool dependency_succeeded)
{
	auto graph = executer;
	Workload_Pair pair = workload_pair_create(workload, depends_on);
	Dependency_Information* info = hashtable_find_element(&graph->workload_dependencies, pair);
	assert(info != 0, "");
	list_remove_node(&workload->dependencies, info->dependency_node);
	list_remove_node(&depends_on->dependents, info->dependent_node);

	// Signal all fail indicators to have passed
	for (int i = 0; i < info->fail_indicators.size; i++) {
		*(info->fail_indicators[i].fail_indicator) = !dependency_succeeded;
	}
	dynamic_array_destroy(&info->fail_indicators);

	bool worked = hashtable_remove_element(&graph->workload_dependencies, pair);
	if (allow_add_to_runnables && workload->dependencies.count == 0) {
		dynamic_array_push_back(&graph->runnable_workloads, workload);
	}
}

Workload_Base* analysis_workload_find_associated_cluster(Workload_Base* workload)
{
	assert(workload->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE, "");
	if (workload->cluster == 0) {
		return workload;
	}
	workload->cluster = analysis_workload_find_associated_cluster(workload->cluster);
	return workload->cluster;
}

bool cluster_workload_check_for_cyclic_dependency(
	Workload_Base* workload, Workload_Base* start_workload,
	Hashtable<Workload_Base*, bool>* visited, Dynamic_Array<Workload_Base*>* workloads_to_merge)
{
	// Check if we already visited
	{
		bool* contains_loop = hashtable_find_element(visited, workload);
		if (contains_loop != 0) {
			return *contains_loop;
		}
	}
	hashtable_insert_element(visited, workload, false); // The boolean value nodes later if we actually find a loop
	bool loop_found = false;
	for (int i = 0; i < workload->reachable_clusters.size; i++)
	{
		Workload_Base* reachable = analysis_workload_find_associated_cluster(workload->reachable_clusters[i]);
		if (reachable == start_workload) {
			loop_found = true;
		}
		else {
			bool transitiv_reachable = cluster_workload_check_for_cyclic_dependency(reachable, start_workload, visited, workloads_to_merge);
			if (transitiv_reachable) loop_found = true;
		}
	}
	if (loop_found) {
		bool* current_value = hashtable_find_element(visited, workload);
		*current_value = true;
		dynamic_array_push_back(workloads_to_merge, workload);
	}
	return loop_found;
}

void analysis_workload_add_cluster_dependency(
	Workload_Executer* executer, Workload_Base* add_to_workload, Workload_Base* dependency, 
	Dependency_Failure_Info failure_info = dependency_failure_info_make_none())
{
	auto graph = executer;
	assert(add_to_workload->type == dependency->type && dependency->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE, "");
	Workload_Base* merge_into = analysis_workload_find_associated_cluster(add_to_workload);
	Workload_Base* merge_from = analysis_workload_find_associated_cluster(dependency);
	if (merge_into == merge_from || merge_from->is_finished) {
		return;
	}

	// Check if clusters form loops after new dependency
	Hashtable<Workload_Base*, bool> visited = hashtable_create_pointer_empty<Workload_Base*, bool>(1);
	SCOPE_EXIT(hashtable_destroy(&visited));
	Dynamic_Array<Workload_Base*> workloads_to_merge = dynamic_array_create<Workload_Base*>(1);
	SCOPE_EXIT(dynamic_array_destroy(&workloads_to_merge));
	bool loop_found = cluster_workload_check_for_cyclic_dependency(merge_from, merge_into, &visited, &workloads_to_merge);
	if (!loop_found) {
		dynamic_array_push_back(&merge_into->reachable_clusters, merge_from);
		analysis_workload_add_dependency_internal(executer, merge_into, merge_from, failure_info);
		return;
	}

	// Merge all workloads into merge_into workload
	for (int i = 0; i < workloads_to_merge.size; i++)
	{
		Workload_Base* merge_cluster = workloads_to_merge[i];
		assert(merge_cluster != merge_into, "");
		// Remove all dependent connections from the merge
		auto node = merge_cluster->dependencies.head;
		while (node != 0)
		{
			Workload_Base* merge_dependency = node->value;
			// Check if we need the dependency
			bool keep_dependency = true;
			if (merge_dependency == merge_into || merge_dependency == merge_from) {
				keep_dependency = false;
			}
			else
			{
				bool* contains_loop = hashtable_find_element(&visited, merge_dependency);
				if (contains_loop != 0) {
					keep_dependency = !*contains_loop;
				}
			}

			auto next = node->next;
			if (keep_dependency) {
				workload_executer_move_dependency(executer, merge_cluster, merge_into, merge_dependency);
			}
			else {
				workload_executer_remove_dependency(executer, merge_cluster, merge_dependency, false, true);
			}
			node = next;
		}
		list_reset(&merge_cluster->dependencies);

		// Merge all analysis item values
		{
			auto& functions_into = downcast<Workload_Function_Cluster_Compile>(merge_into)->functions;
			auto& functions_from = downcast<Workload_Function_Cluster_Compile>(merge_cluster)->functions;
			dynamic_array_append_other(&functions_into, &functions_from);
			dynamic_array_reset(&functions_from);
		}

		// Add reachables to merged
		dynamic_array_append_other(&merge_into->reachable_clusters, &merge_cluster->reachable_clusters);
		dynamic_array_reset(&merge_cluster->reachable_clusters);
		analysis_workload_add_dependency_internal(executer, merge_cluster, merge_into);
		merge_cluster->cluster = merge_into;
	}

	// Prune reachables
	for (int i = 0; i < merge_into->reachable_clusters.size; i++)
	{
		Workload_Base* reachable = analysis_workload_find_associated_cluster(merge_into->reachable_clusters[i]);
		if (reachable == merge_into) {
			// Remove self references
			dynamic_array_swap_remove(&merge_into->reachable_clusters, i);
			i = i - 1;
		}
		else
		{
			// Remove doubles
			for (int j = i + 1; j < merge_into->reachable_clusters.size; j++) {
				if (merge_into->reachable_clusters[j] == reachable) {
					dynamic_array_swap_remove(&merge_into->reachable_clusters, j);
				}
			}
		}
	}
}

bool analysis_workload_find_cycle(
	Workload_Base* current_workload, int current_depth, Workload_Base* start_workload, int desired_cycle_size,
	Dynamic_Array<Workload_Base*>* loop_nodes, Hashtable<Workload_Base*, int>* valid_workloads)
{
	if (hashtable_find_element(valid_workloads, current_workload) == 0) {
		return false;
	}
	if (current_workload->is_finished) return false;
	if (current_depth > desired_cycle_size) return false;
	if (current_workload == start_workload) {
		assert(current_depth == desired_cycle_size, "");
		dynamic_array_push_back(loop_nodes, current_workload);
		return true;
	}

	List_Node<Workload_Base*>* node = current_workload->dependencies.head;
	while (node != 0)
	{
		SCOPE_EXIT(node = node->next);
		Workload_Base* dependency = node->value;
		if (analysis_workload_find_cycle(dependency, current_depth + 1, start_workload, desired_cycle_size, loop_nodes, valid_workloads)) {
			dynamic_array_push_back(loop_nodes, current_workload);
			return true;
		}
	}
	return false;
}

Semantic_Context semantic_context_make(
	Compilation_Data* compilation_data, Workload_Base* workload, 
	Symbol_Table* symbol_table, Symbol_Access_Level symbol_access_level, 
	Analysis_Pass* analysis_pass, Arena* scratch_arena)
{
	Semantic_Context result;

    result.compilation_data = compilation_data;
    result.current_workload = workload;
	result.scratch_arena = scratch_arena;
	result.current_symbol_table = symbol_table;
	result.symbol_access_level = symbol_access_level;
	result.current_pass = analysis_pass;
	result.current_expression = nullptr;
	result.current_function = nullptr;
	result.statement_reachable = true;
	result.block_stack = DynArray<AST::Code_Block*>::create(scratch_arena);

	return result;
}

void workload_executer_resolve(Workload_Executer* executer, Compilation_Data* compilation_data)
{
	/*
	Resolve is a double loop:
	loop
		loop run runnable workloads while they exist

		if no runnable workloads exist and not finished
			Check for circular dependencies
				if they exist, resolve them (E.g. set symbols to error, set member type to error, ...)
				else illegal_code_path
	*/

	double start_time = timer_current_time_in_seconds();
	double time_in_executer = 0;
	double time_in_loop_resolve = 0;
	double time_per_workload_type[100];
	memory_set_bytes(&time_per_workload_type[0], sizeof(double) * 100, 0);
	double last_timestamp = timer_current_time_in_seconds();

	Arena scratch_arena = Arena::create(0);
	SCOPE_EXIT(scratch_arena.destroy());
	Semantic_Context error_logging_context = semantic_context_make(
		compilation_data, nullptr, nullptr, Symbol_Access_Level::GLOBAL, nullptr, &scratch_arena
	);
	
	auto& all_workloads = executer->all_workloads;
	int round_no = 0;
	while (true)
	{
		SCOPE_EXIT(round_no += 1);
		executer->progress_was_made = false;
		// Print workloads and dependencies
		if (PRINT_DEPENDENCIES)
		{
			String tmp = string_create_empty(256);
			SCOPE_EXIT(string_destroy(&tmp));
			string_append_formated(&tmp, "\n\n--------------------\nWorkload Execution Round %d\n---------------------\n", round_no);
			for (int i = 0; i < executer->runnable_workloads.size; i++)
			{
				auto workload = executer->runnable_workloads[i];
				if (i == 0) {
					string_append_formated(&tmp, "Runnable workloads:\n");
				}
				if (workload->dependencies.count > 0) continue;
				if (workload->is_finished) continue;
				string_append_formated(&tmp, "  ");
				analysis_workload_append_to_string(workload, &tmp);
				string_append_formated(&tmp, "\n");

				// Append dependents
				List_Node<Workload_Base*>* dependent_node = workload->dependents.head;
				while (dependent_node != 0) {
					SCOPE_EXIT(dependent_node = dependent_node->next);
					Workload_Base* dependent = dependent_node->value;
					string_append_formated(&tmp, "    ");
					analysis_workload_append_to_string(dependent, &tmp);
					string_append_formated(&tmp, "\n");
				}
			}

			for (int i = 0; i < all_workloads.size; i++)
			{
				if (i == 0) {
					string_append_formated(&tmp, "\nWorkloads with dependencies:\n");
				}
				Workload_Base* workload = all_workloads[i];
				if (workload->is_finished || workload->dependencies.count == 0) continue;
				string_append_formated(&tmp, "  ");
				analysis_workload_append_to_string(workload, &tmp);
				string_append_formated(&tmp, "\n");
				// Print dependencies
				{
					List_Node<Workload_Base*>* dependency_node = workload->dependencies.head;
					if (dependency_node != 0) {
						string_append_formated(&tmp, "    Depends On:\n");
					}
					while (dependency_node != 0) {
						SCOPE_EXIT(dependency_node = dependency_node->next);
						Workload_Base* dependency = dependency_node->value;
						string_append_formated(&tmp, "      ");
						analysis_workload_append_to_string(dependency, &tmp);
						string_append_formated(&tmp, "\n");
					}
				}
				// Dependents
				{
					List_Node<Workload_Base*>* dependent_node = workload->dependents.head;
					if (dependent_node != 0) {
						string_append_formated(&tmp, "    Dependents:\n");
					}
					while (dependent_node != 0) {
						SCOPE_EXIT(dependent_node = dependent_node->next);
						Workload_Base* dependent = dependent_node->value;
						string_append_formated(&tmp, "      ");
						analysis_workload_append_to_string(dependent, &tmp);
						string_append_formated(&tmp, "\n");
					}
				}

			}

			logg("%s", tmp.characters);
		}

		// Execute runnable workloads
		for (int i = 0; i < executer->runnable_workloads.size; i++)
		{
			Workload_Base* workload = executer->runnable_workloads[i];
			if (workload->dependencies.count > 0) {
				continue; // Skip runnable workload
			}
			if (workload->is_finished) {
				continue;
			}
			executer->progress_was_made = true;

			if (PRINT_DEPENDENCIES) {
				String tmp = string_create_empty(128);
				analysis_workload_append_to_string(workload, &tmp);
				logg("Executing workload: %s\n", tmp.characters);
				string_destroy(&tmp);
			}

			// TIMING
			double now = timer_current_time_in_seconds();
			time_in_executer += now - last_timestamp;
			last_timestamp = now;

			bool finished = workload_executer_switch_to_workload(executer, workload);

			// TIMING
			now = timer_current_time_in_seconds();
			time_per_workload_type[(int)workload->type] += now - last_timestamp;
			last_timestamp = now;

			// Note: After a workload executes, it may have added new dependencies to itself
			if (workload->dependencies.count == 0)
			{
				assert(finished, "When on dependencies remain, the fiber should have exited normally!\n");
				workload->is_finished = true;
				List_Node<Workload_Base*>* node = workload->dependents.head;
				// Loop over all dependents and remove this workload from that list
				while (node != 0) {
					Workload_Base* dependent = node->value;
					node = node->next; // INFO: This is required before remove_dependency, since remove will remove nodes from the list
					workload_executer_remove_dependency(executer, dependent, workload, true, true);
				}
				assert(workload->dependents.count == 0, "Remove dependency should already have cleared the list!");
			}
			else {
				assert(!finished, "If there are dependencies, the fiber must still be running!");
			}
		}
		dynamic_array_reset(&executer->runnable_workloads);
		if (executer->progress_was_made) {
			if (PRINT_DEPENDENCIES) {
				logg("Progress was made!");
			}
			continue;
		}

		// Check if all workloads finished
		{
			bool all_finished = true;
			for (int i = 0; i < all_workloads.size; i++) {
				if (!all_workloads[i]->is_finished) {
					all_finished = false;
					break;
				}
			}
			if (all_finished) {
				break;
			}
		}

		/*
			Circular Dependency Detection:
			 1. Do breadth first search to find smallest possible loop/if loop exists
			 2. If a loop was found, do a depth first search to reconstruct the loop (Could probably be done better)
			 3. Resolve the loop (Log Error, set some of the dependencies to error)
		*/
		// TIMING
		double now = timer_current_time_in_seconds();
		time_in_executer += now - last_timestamp;
		last_timestamp = now;

		{
			// Initialization
			Hashtable<Workload_Base*, int> workload_to_layer = hashtable_create_pointer_empty<Workload_Base*, int>(4);
			SCOPE_EXIT(hashtable_destroy(&workload_to_layer));
			Hashset<Workload_Base*> unvisited = hashset_create_pointer_empty<Workload_Base*>(4);
			SCOPE_EXIT(hashset_destroy(&unvisited));
			for (int i = 0; i < all_workloads.size; i++) {
				Workload_Base* workload = all_workloads[i];
				if (!workload->is_finished) {
					hashset_insert_element(&unvisited, workload);
				}
			}

			bool loop_found = false;
			int loop_node_count = -1;
			Workload_Base* loop_node = 0;
			Workload_Base* loop_node_2 = 0;

			// Breadth first search
			Dynamic_Array<int> layer_start_indices = dynamic_array_create<int>(4);
			SCOPE_EXIT(dynamic_array_destroy(&layer_start_indices));
			Dynamic_Array<Workload_Base*> layers = dynamic_array_create<Workload_Base*>(4);
			SCOPE_EXIT(dynamic_array_destroy(&layers));

			// Note: Since we are dealing with possibly unconnected graphs (Not all workloads are connected) this is a double loop
			while (!loop_found)
			{
				// Remove all nodes that are already confirmed to have no cycles (E.g nodes from last loop run)
				for (int i = 0; i < layers.size; i++) {
					hashset_remove_element(&unvisited, layers[i]);
				}
				if (unvisited.element_count == 0) break;
				dynamic_array_reset(&layer_start_indices);
				dynamic_array_reset(&layers);
				hashtable_reset(&workload_to_layer);

				Workload_Base* start = *hashset_iterator_create(&unvisited).value;
				dynamic_array_push_back(&layer_start_indices, layers.size);
				dynamic_array_push_back(&layers, start);
				dynamic_array_push_back(&layer_start_indices, layers.size);

				int current_layer = 0;
				while (true && !loop_found)
				{
					SCOPE_EXIT(current_layer++);
					int layer_start = layer_start_indices[current_layer];
					int layer_end = layer_start_indices[current_layer + 1];
					if (layer_start == layer_end) break;
					for (int i = layer_start; i < layer_end && !loop_found; i++)
					{
						Workload_Base* scan_for_loops = layers[i];
						assert(!scan_for_loops->is_finished, "");
						List_Node<Workload_Base*>* node = scan_for_loops->dependencies.head;
						while (node != 0 && !loop_found)
						{
							SCOPE_EXIT(node = node->next;);
							Workload_Base* dependency = node->value;
							if (!hashset_contains(&unvisited, dependency)) {
								// Node is clear because we checked it on a previous cycle
								continue;
							}
							int* found_layer = hashtable_find_element(&workload_to_layer, dependency);
							if (found_layer == nullptr) {
								hashtable_insert_element(&workload_to_layer, dependency, current_layer + 1);
								dynamic_array_push_back(&layers, dependency);
							}
							else
							{
								int dependency_layer = *found_layer;
								if (dependency_layer > current_layer) {
									// This means the workload is already queued for the next layer
								}
								else if (dependency_layer == current_layer) {
									// Here we need to check for self loops and loops with the breadth-first depth
									if (dependency == scan_for_loops) { // Self dependency
										loop_found = true;
										loop_node_count = 1;
									}
									else if (hashtable_find_element(&executer->workload_dependencies, workload_pair_create(dependency, scan_for_loops)) != 0) {
										loop_found = true;
										loop_node_count = 2;
									}
								}
								else {
									// Definitly found a loop
									loop_found = true;
									loop_node_count = current_layer - dependency_layer + 1;
								}

								// Recheck and set loop nodes
								if (loop_found) {
									loop_node = scan_for_loops;
									loop_node_2 = dependency;
									break;
								}
							}
						}
					}
					dynamic_array_push_back(&layer_start_indices, layers.size);
				}
			}

			// Handle the loop
			if (loop_found)
			{
				// Reconstruct Loop
				Dynamic_Array<Workload_Base*> workload_cycle = dynamic_array_create<Workload_Base*>(1);
				SCOPE_EXIT(dynamic_array_destroy(&workload_cycle));
				if (loop_node_count != 1) {
					// Depth first search
					bool found_loop_again = analysis_workload_find_cycle(loop_node_2, 1, loop_node, loop_node_count, &workload_cycle, &workload_to_layer);
					assert(found_loop_again, "");
					dynamic_array_reverse_order(&workload_cycle);
				}
				else {
					dynamic_array_push_back(&workload_cycle, loop_node);
				}

				// Resolve and report error
				bool breakable_dependency_found = false;
				for (int i = 0; i < workload_cycle.size; i++)
				{
					Workload_Base* workload = workload_cycle[i];
					Workload_Base* depends_on = i + 1 == workload_cycle.size ? workload_cycle[0] : workload_cycle[i + 1];
					Workload_Pair pair = workload_pair_create(workload, depends_on);
					Dependency_Information infos = *hashtable_find_element(&executer->workload_dependencies, pair);
					if (infos.can_be_broken) {
						breakable_dependency_found = true;
						for (int j = 0; j < infos.fail_indicators.size; j++) {
							log_semantic_error(&error_logging_context, "Cyclic dependencies detected", upcast(infos.fail_indicators[j].error_report_node));
							for (int k = 0; k < workload_cycle.size; k++) {
								Workload_Base* workload = workload_cycle[k];
								log_error_info_cycle_workload(&error_logging_context, workload);
							}
						}
						workload_executer_remove_dependency(executer, workload, depends_on, true, false);
					}
				}
				assert(breakable_dependency_found, "");
				executer->progress_was_made = true;
				if (PRINT_DEPENDENCIES) {
					logg("Resolved cyclic dependency loop!");
				}
			}
		}

		if (executer->progress_was_made) {
			if (PRINT_DEPENDENCIES) {
				logg("Progress was made!");
			}

			// TIMING
			double now = timer_current_time_in_seconds();
			time_in_loop_resolve += now - last_timestamp;
			last_timestamp = now;

			continue;
		}

		panic("Loops must have been resolved by now, so some progress needs to be have made..\n");
	}


	if (PRINT_TIMING)
	{
		double end_time = timer_current_time_in_seconds();
		//logg("Time in Bake Analysis    %3.4f")
		logg("Time in executer         %3.4fms\n", time_in_executer * 1000);
		logg("Time in loop-resolve     %3.4fms\n", time_in_loop_resolve * 1000);
		for (int i = 0; i < 10; i++) {
			Analysis_Workload_Type type = (Analysis_Workload_Type)i;
			const char* str = "";
			switch (type) {
			case Analysis_Workload_Type::BAKE_ANALYSIS: str = "Bake Analysis   "; break;
			case Analysis_Workload_Type::BAKE_EXECUTION: str = "Bake Execute    "; break;
			case Analysis_Workload_Type::DEFINITION: str = "Definition      "; break;
			case Analysis_Workload_Type::MODULE_ANALYSIS: str = "Module Analysis "; break;
			case Analysis_Workload_Type::OPERATOR_CONTEXT_CHANGE: str = "Operator Context Change   "; break;

			case Analysis_Workload_Type::FUNCTION_HEADER: str = "Header          "; break;
			case Analysis_Workload_Type::FUNCTION_BODY: str = "Body            "; break;
			case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE: str = "Cluster Compile "; break;

			case Analysis_Workload_Type::STRUCT_BODY: str = "Struct Body Analysis "; break;
			case Analysis_Workload_Type::STRUCT_POLYMORPHIC: str = "Struct Polymorphic Header "; break;
			default: panic("hey");
			}
			logg("Time in %s %3.4fms\n", str, time_per_workload_type[i] * 1000);
		}
		logg("SUUM:                    %3.4fms\n\n", (end_time - start_time) * 1000);
	}
}

void workload_add_to_runnable_queue_if_possible(Workload_Executer* executer, Workload_Base* workload)
{
	if (!workload->is_finished && workload->dependencies.count == 0) {
		dynamic_array_push_back(&executer->runnable_workloads, workload);
		executer->progress_was_made = true;
	}
}

bool workload_executer_switch_to_workload(Workload_Executer* executer, Workload_Base* workload)
{
	Workload_Entry_Info entry_info;
	entry_info.compilation_data = executer->compilation_data;
	entry_info.workload = workload;

	if (!workload->was_started) {
		workload->fiber_handle = fiber_pool_get_handle(executer->fiber_pool, analysis_workload_entry, &entry_info);
		workload->was_started = true;
	}
	bool result = fiber_pool_switch_to_handel(workload->fiber_handle);

	if (PRINT_DEPENDENCIES) {
		auto tmp = string_create_empty(1);
		analysis_workload_append_to_string(workload, &tmp);
		if (workload->dependencies.count == 0) {
			SCOPE_EXIT(string_destroy(&tmp));
			logg("FINISHED: %s\n", tmp.characters);
		}
		else
		{
			// Print dependencies
			List_Node<Workload_Base*>* dependency_node = workload->dependencies.head;
			if (dependency_node != 0) {
				string_append_formated(&tmp, "    Depends On:\n");
			}
			while (dependency_node != 0) {
				SCOPE_EXIT(dependency_node = dependency_node->next);
				Workload_Base* dependency = dependency_node->value;
				string_append_formated(&tmp, "      ");
				analysis_workload_append_to_string(dependency, &tmp);
				string_append_formated(&tmp, "\n");
			}
			logg("WAITING: %s\n", tmp.characters);
		}
	}

	return result;
}

void workload_executer_wait_for_dependency_resolution(Semantic_Context* semantic_context)
{
	Workload_Base* workload = semantic_context->current_workload;
	if (workload == nullptr) return;
	if (workload->dependencies.count != 0) {
		fiber_pool_switch_to_main_fiber(semantic_context->compilation_data->compiler->fiber_pool);
	}
}

void analysis_workload_append_to_string(Workload_Base* workload, String* string)
{
	switch (workload->type)
	{
	case Analysis_Workload_Type::MODULE_ANALYSIS: {
		auto module = downcast<Workload_Module_Analysis>(workload);
		string_append(string, "Module analysis");
		break;
	}
	case Analysis_Workload_Type::OPERATOR_CONTEXT_CHANGE: {
		auto change = downcast<Workload_Custom_Operator>(workload);
		string_append(string, "Operator Context Change Type: ");
		AST::custom_operator_type_append_to_string(change->type_to_analyse, string);
		break;
	}
	case Analysis_Workload_Type::DEFINITION: {
		auto def = downcast<Workload_Definition>(workload);
		string_append_formated(string, "Definition %s", def->symbol->id->characters);
		if (def->is_extern_import) {
			string_append_formated(string, " extern_import");
		}
		else {
			if (def->options.normal.is_comptime) {
				string_append_formated(string, " comptime");
			}
		}
		break;
	}
	case Analysis_Workload_Type::BAKE_ANALYSIS: {
		string_append_formated(string, "Bake-Analysis");
		break;
	}
	case Analysis_Workload_Type::BAKE_EXECUTION: {
		string_append_formated(string, "Bake-Execution");
		break;
	}
	case Analysis_Workload_Type::FUNCTION_BODY: {
		Symbol* symbol = 0;
		auto function = downcast<Workload_Function_Body>(workload)->progress->function;
		string_append_formated(string, "Body \"%s\"", function->name->characters);
		break;
	}
	case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
	{
		string_append_formated(string, "Cluster-Compile [");
		auto cluster = downcast<Workload_Function_Cluster_Compile>(analysis_workload_find_associated_cluster(workload));
		for (int i = 0; i < cluster->functions.size; i++) {
			auto function = cluster->functions[i];
			string_append_formated(string, "%s, ", function->name->characters);
		}
		string_append_formated(string, "]");
		break;
	}
	case Analysis_Workload_Type::FUNCTION_HEADER: {
		auto function = downcast<Workload_Function_Header>(workload)->progress->function;
		string_append_formated(string, "Header \"%s\"", function->name->characters);
		break;
	}
	case Analysis_Workload_Type::STRUCT_BODY: {
		auto struct_id = downcast<Workload_Structure_Body>(workload)->struct_type->name->characters;
		string_append_formated(string, "Struct-Analysis \"%s\"", struct_id);
		break;
	}
	case Analysis_Workload_Type::STRUCT_POLYMORPHIC: {
		auto struct_id = downcast<Workload_Structure_Polymorphic>(workload)->body_workload->struct_type->name->characters;
		string_append_formated(string, "Struct-Analysis \"%s\"", struct_id);
		break;
	}
	default: panic("");
	}
}

Workload_Root* workload_executer_add_root_workload(Compilation_Data* compilation_data)
{
	auto root = workload_executer_allocate_workload<Workload_Root>(nullptr, compilation_data);
	root->base.is_finished = true;
	root->base.was_started = true;
	return root;
}

Workload_Module_Analysis* workload_executer_add_module_discovery(AST::Module* module_node, Compilation_Data* compilation_data)
{
	Workload_Module_Analysis* workload = workload_executer_allocate_workload<Workload_Module_Analysis>(
		upcast(compilation_data->root_workload), compilation_data
	);
	workload->module_node = module_node;
	return workload;
}

Datatype_Memory_Info* type_wait_for_size_info_to_finish(Datatype* type, Semantic_Context* semantic_context, Dependency_Failure_Info failure_info)
{
	if (!type_size_is_unfinished(type)) {
		return &type->memory_info.value;
	}

	auto executer = semantic_context->compilation_data->workload_executer;
	auto waiting_for_type = type;
	while (waiting_for_type->type == Datatype_Type::ARRAY) {
		waiting_for_type = downcast<Datatype_Array>(waiting_for_type)->element_type;
	}
	assert(waiting_for_type->type == Datatype_Type::STRUCT, "");
	auto structure = downcast<Datatype_Struct>(waiting_for_type);

	analysis_workload_add_dependency_internal(
		executer, semantic_context->current_workload, upcast(structure->workload), failure_info
	);
	workload_executer_wait_for_dependency_resolution(semantic_context);

	if (failure_info.fail_indicator == 0) {
		assert(!type_size_is_unfinished(type), "");
	}
	return &type->memory_info.value;
}



// CASTING/PATTERN_TYPES/EXPRESSION CONTEXT
struct Matching_Constraint
{
	Datatype_Pattern_Variable* pattern_variable;
	Upp_Constant value;
};

struct Pattern_Match_Result
{
	DynArray<Matching_Constraint> constraints;
	int max_match_depth;
};

Pattern_Match_Result pattern_match_result_make_empty(Arena* arena)
{
	Pattern_Match_Result result;
	result.constraints = DynArray<Matching_Constraint>::create(arena);
	result.max_match_depth = 0;
	return result;
}

bool pattern_type_match_against(
	Pattern_Match_Result& result, int match_depth, Datatype* pattern_type, Datatype* match_against, Semantic_Context* semantic_context)
{
	auto compilation_data = semantic_context->compilation_data;
	auto& type_system = semantic_context->compilation_data->type_system;
	auto& types = type_system->predefined_types;

	if (!pattern_type->contains_pattern && !match_against->contains_pattern) {
		return types_are_equal(pattern_type, match_against);
	}
	assert(!match_against->contains_pattern, "This cannot happen, because we test for this in previous code-path");

	result.max_match_depth = math_maximum(result.max_match_depth, match_depth);
	match_depth += 1;

	// Check if we found match
	if (pattern_type->type == Datatype_Type::PATTERN_VARIABLE)
	{
		auto pool_result = constant_pool_add_constant(
			compilation_data->constant_pool, types.type_handle, array_create_static_as_bytes(&match_against->type_handle, 1)
		);
		assert(pool_result.success, "Type handle must work as constant!");

		Matching_Constraint constraint;
		constraint.pattern_variable = downcast<Datatype_Pattern_Variable>(pattern_type);
		constraint.value = pool_result.options.constant;
		result.constraints.push_back(constraint);

		return true;
	}
	else if (pattern_type->type == Datatype_Type::STRUCT_PATTERN)
	{
		// Check for errors
		auto struct_pattern = downcast<Datatype_Struct_Pattern>(pattern_type);
		Datatype_Struct* match_against_struct = nullptr;
		if (match_against->type == Datatype_Type::STRUCT) {
			match_against_struct = downcast<Datatype_Struct>(match_against);
		}
		else {
			return false;
		}

		if (match_against_struct->workload == 0) {
			return false;
		}
		if (match_against_struct->workload->polymorphic_type != Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
			return false;
		}
		auto& match_against_instance = match_against_struct->workload->polymorphic.instance;
		if (match_against_instance.poly_instance->header != struct_pattern->instance->header) {
			return false;
		}

		auto& set_variable_states = match_against_instance.poly_instance->variable_states;
		auto& pattern_variable_states = struct_pattern->instance->variable_states;
		assert(set_variable_states.size == pattern_variable_states.size, "");
		for (int i = 0; i < set_variable_states.size; i++)
		{
			auto& pattern_state = pattern_variable_states[i];
			auto& set_state = set_variable_states[i];
			assert(set_state.type == Pattern_Variable_State_Type::SET, "Otherwise this wouldn't be a normal struct");
			auto& match_with_value = set_state.options.value;
			switch (pattern_state.type)
			{
			case Pattern_Variable_State_Type::SET: {
				if (!upp_constant_is_equal(pattern_state.options.value, match_with_value)) {
					return false;
				}
				break;
			}
			case Pattern_Variable_State_Type::UNSET: { // Nothing to do if unset
				break;
			}
			case Pattern_Variable_State_Type::PATTERN:
			{
				Datatype* pattern_type = pattern_state.options.pattern_type;
				if (pattern_type->type == Datatype_Type::PATTERN_VARIABLE)
				{
					Matching_Constraint constraint;
					constraint.pattern_variable = downcast<Datatype_Pattern_Variable>(pattern_type);
					constraint.value = match_with_value;
					result.constraints.push_back(constraint);
				}
				else if (types_are_equal(match_with_value.type, types.type_handle))
				{
					// Otherwise the match-with-value should be a type-handle
					Upp_Type_Handle handle = upp_constant_to_value<Upp_Type_Handle>(match_with_value);
					assert((int)handle.index < type_system->types.size, "Instanciated struct should have valid type-handle");
					Datatype* match_against = type_system->types[handle.index];
					assert(!datatype_is_unknown(match_against), "Instanciated struct should be unknown before instanciated with unknown i guess");
					assert(!match_against->contains_pattern, "Instanciated struct has pattern type, seems weird, check this if this happens");
					bool success = pattern_type_match_against(result, match_depth, pattern_type, match_against, semantic_context);
					if (!success) return false;
				}
				else {
					return false; // Trying to match a non-typehandle value, e.g. 2 with a type-tree
				}

				break;
			}
			default: panic("");
			}
		}

		return true;
	}

	// Exit early if expected types don't match
	if (pattern_type->type != match_against->type) {
		return false;
	}

	switch (pattern_type->type)
	{
	case Datatype_Type::ARRAY: 
	{
		auto other_array = downcast<Datatype_Array>(match_against);
		auto this_array = downcast<Datatype_Array>(pattern_type);

		if (!other_array->count_known) {
			return false; // Something has to be unknown/wrong for this to be true
		}

		// Check if we can match array size
		if (this_array->count_variable_type != 0)
		{
			// Match implicit parameter to element count
			usize size = other_array->element_count;
			auto pool_result = constant_pool_add_constant(
				compilation_data->constant_pool,
				upcast(types.usize),
				array_create_static_as_bytes(&size, 1)
			);
			assert(pool_result.success, "U64 type must work as constant");

			Matching_Constraint constraint;
			constraint.pattern_variable = this_array->count_variable_type;
			constraint.value = pool_result.options.constant;
			result.constraints.push_back(constraint);
		}
		else 
		{
			if (!this_array->count_known) { // We should know our own size
				return false;
			}
			if (this_array->element_count != other_array->element_count) {
				return false;
			}
		}

		// Continue matching element type
		return pattern_type_match_against(
			result, match_depth, downcast<Datatype_Array>(pattern_type)->element_type, downcast<Datatype_Array>(match_against)->element_type,
			semantic_context
		);
	}
	case Datatype_Type::SLICE: {
		return pattern_type_match_against(
			result, match_depth, downcast<Datatype_Slice>(pattern_type)->element_type, downcast<Datatype_Slice>(match_against)->element_type,
			semantic_context
		);
	}
	case Datatype_Type::POINTER: {
		auto pattern_pointer = downcast<Datatype_Pointer>(pattern_type);
		auto match_pointer = downcast<Datatype_Pointer>(match_against);
		if (pattern_pointer->is_optional != match_pointer->is_optional) return false;
		return pattern_type_match_against(result, match_depth, pattern_pointer->element_type, match_pointer->element_type, semantic_context);
	}
	case Datatype_Type::STRUCT: {
		auto a = downcast<Datatype_Struct>(pattern_type);
		auto b = downcast<Datatype_Struct>(match_against);
		// Here we have two different struct types, so they shouldn't be matchable.
		// Not even sure when this case happens, maybe with anonymous structs?
		// I don't quite understand when this case should happen, but in my mind this is always false
		return false;
	}
	case Datatype_Type::FUNCTION_POINTER:
	{
		auto af = downcast<Datatype_Function_Pointer>(pattern_type);
		auto bf = downcast<Datatype_Function_Pointer>(match_against);
		auto a = af->signature;
		auto b = bf->signature;
		if (a->parameters.size != b->parameters.size || a->return_type_index != b->return_type_index) return false;
		for (int i = 0; i < a->parameters.size; i++) {
			auto& ap = a->parameters[i];
			auto& bp = b->parameters[i];
			if (ap.name != bp.name) return false;
			if (!pattern_type_match_against(result, match_depth, ap.datatype, bp.datatype, semantic_context)) {
				return false;
			}
		}
		return true;
	}
	case Datatype_Type::ENUM:
	case Datatype_Type::UNKNOWN_TYPE:
	case Datatype_Type::PRIMITIVE: panic("Should be handled by previous code-path (E.g. non polymorphic!)"); break;
	case Datatype_Type::STRUCT_PATTERN:
	case Datatype_Type::PATTERN_VARIABLE: panic("Previous code path should have handled this!");
	default: panic("");
	}

	panic("");
	return true;
}

// Returns false if constraints don't match
bool pattern_match_result_check_constraints_pairwise(Pattern_Match_Result& results)
{
	for (int i = 0; i < results.constraints.size; i++)
	{
		Matching_Constraint& a = results.constraints[i];
		for (int j = i + 1; j < results.constraints.size; j++)
		{
			Matching_Constraint& b = results.constraints[j];
			if (a.pattern_variable->variable != b.pattern_variable->variable) continue;
			if (!upp_constant_is_equal(a.value, b.value)) {
				return false;
			}
		}
	}
	return true;
}

bool pattern_match_result_check_constraints_with_states(Pattern_Match_Result& results, Array<Pattern_Variable_State> states)
{
	for (int i = 0; i < results.constraints.size; i++)
	{
		Matching_Constraint& constraint = results.constraints[i];
		Pattern_Variable* variable = constraint.pattern_variable->variable;
		auto& state = states[variable->value_access_index];

		if (state.type != Pattern_Variable_State_Type::SET) continue;
		if (!upp_constant_is_equal(constraint.value, state.options.value)) {
			return false;
		}
	}
	return true;
}

bool cast_type_result_is_temporary(Cast_Type cast_type, bool initial_value_temporary)
{
	switch (cast_type)
	{
	// We treat the basic conversions as creating new values (Although we may want to think about that)
	case Cast_Type::INTEGERS:
	case Cast_Type::FLOATS:
	case Cast_Type::ENUMS:
	case Cast_Type::FLOAT_TO_INT:
	case Cast_Type::INT_TO_FLOAT:
	case Cast_Type::ENUM_TO_INT:
	case Cast_Type::INT_TO_ENUM:
	case Cast_Type::POINTERS:
	case Cast_Type::POINTER_TO_ADDRESS:
	case Cast_Type::ADDRESS_TO_POINTER:
		return true;

	case Cast_Type::DEREFERENCE: return false;
	case Cast_Type::ADDRESS_OF: return true;
	case Cast_Type::FROM_ANY: return false;

	case Cast_Type::TO_SUB_TYPE:
	case Cast_Type::TO_BASE_TYPE:
		return initial_value_temporary;

	// The more complex casts are usually always temporary
	case Cast_Type::ARRAY_TO_SLICE:
	case Cast_Type::TO_ANY:
	case Cast_Type::CUSTOM_CAST:
		return true;

	case Cast_Type::NO_CAST: return initial_value_temporary;
	case Cast_Type::UNKNOWN: return false;
	case Cast_Type::INVALID: return false;
	default: panic("");
	}
	return false;
}

int cast_type_get_overloading_priority(Cast_Type cast_type)
{
	switch (cast_type)
	{
	case Cast_Type::NO_CAST: return 10;
	case Cast_Type::UNKNOWN: return 0;
	case Cast_Type::INVALID: return 0;

	case Cast_Type::TO_BASE_TYPE:
		return 8;

	// Basic type conversions
	case Cast_Type::INTEGERS:
	case Cast_Type::FLOATS:
	case Cast_Type::ENUMS:
	case Cast_Type::FLOAT_TO_INT:
	case Cast_Type::INT_TO_FLOAT:
	case Cast_Type::ENUM_TO_INT:
	case Cast_Type::INT_TO_ENUM:
		return 7;

	case Cast_Type::POINTERS:
	case Cast_Type::POINTER_TO_ADDRESS:
	case Cast_Type::ADDRESS_TO_POINTER:
		return 5;

	case Cast_Type::TO_SUB_TYPE:
	case Cast_Type::ARRAY_TO_SLICE:
	case Cast_Type::TO_ANY:
		return 5;

	case Cast_Type::FROM_ANY:
		return 3;

	case Cast_Type::ADDRESS_OF: 
	case Cast_Type::DEREFERENCE: 
		return 2;
	// The more complex casts are usually always temporary
	case Cast_Type::CUSTOM_CAST:
		return 1;
	default: panic("");
	}
	return false;
}

// Auto dereference or address-of
Cast_Type check_if_type_modifier_update_valid(Type_Modifier_Info src_mods, Type_Modifier_Info dst_mods, bool source_is_temporary)
{
	Cast_Type cast_type = Cast_Type::INVALID;
	if (src_mods.pointer_level > dst_mods.pointer_level && dst_mods.base_type->type != Datatype_Type::PATTERN_VARIABLE) 
	{
		// Auto dereference (Only when matching with non $T poly-value, e.g. **[]$T or *Node($T)
		cast_type = Cast_Type::DEREFERENCE;
	}
	else if (src_mods.pointer_level + 1 == dst_mods.pointer_level && !source_is_temporary)
	{
		// Auto address-of
		cast_type = Cast_Type::ADDRESS_OF;
	}
	else if (src_mods.pointer_level == dst_mods.pointer_level){
		cast_type = Cast_Type::NO_CAST;
	}
	else {
		cast_type = Cast_Type::INVALID;
	}

	// Check optional flags
	if (cast_type != Cast_Type::INVALID) 
	{
		for (int i = 0; i < src_mods.pointer_level; i++) {
			bool src_is_optional = (src_mods.optional_flags & (1 << i)) != 0;
			bool dst_is_optional = (dst_mods.optional_flags & (1 << i)) != 0;
			if (src_is_optional && !dst_is_optional) {
				cast_type = Cast_Type::INVALID;
				break;
			}
		}
	}

	return cast_type;
}

Cast_Info check_if_cast_possible(
	Datatype* from_type, Datatype* to_type, bool value_is_temporary, bool is_auto_cast, Semantic_Context* semantic_context)
{
	auto& types = semantic_context->compilation_data->type_system->predefined_types;

	Cast_Info result;
	result.cast_type = Cast_Type::INVALID;
	result.custom_cast_function = nullptr;
	result.result_type = to_type;

	Datatype* src = from_type;
	Datatype* dst = to_type;

	// Disallow pattern type casting (Should be handled by seperate code-path)
	if (to_type->contains_pattern) {
		return result;
	}

	// Check for no-cast and unknown cast
	if (datatype_is_unknown(src) || datatype_is_unknown(dst)) {
		result.cast_type = Cast_Type::UNKNOWN;
		// if (!called_from_editor) {
			semantic_analyser_set_error_flag(true, semantic_context);
		// }
		return result;
	}
	if (types_are_equal(from_type, to_type)) {
		result.cast_type = Cast_Type::NO_CAST;
		return result;
	}

#define CAST_SUCCESS(x) { result.cast_type = x; return result; }

	// Check for built-in casts (not auto casts)
	if (!is_auto_cast)
	{
		// Check for pointer casts (+ from/to address and between function pointers)
		{
			bool src_is_opt = false;
			bool dst_is_opt = false;
			bool src_is_ptr = datatype_is_pointer(src, &src_is_opt);
			bool dst_is_ptr = datatype_is_pointer(dst, &dst_is_opt);

			// Check for from/to address
			if (src_is_ptr && datatype_is_primitive_class(dst, Primitive_Class::ADDRESS)) {
				CAST_SUCCESS(Cast_Type::POINTER_TO_ADDRESS);
			}
			else if (dst_is_ptr && datatype_is_primitive_class(src, Primitive_Class::ADDRESS)) {
				CAST_SUCCESS(Cast_Type::ADDRESS_TO_POINTER);
			}
			else if (datatype_is_primitive_class(dst, Primitive_Class::C_STRING) && datatype_is_primitive_class(src, Primitive_Class::ADDRESS)) {
				CAST_SUCCESS(Cast_Type::ADDRESS_TO_POINTER);
			}
			else if (datatype_is_primitive_class(src, Primitive_Class::C_STRING) && datatype_is_primitive_class(dst, Primitive_Class::ADDRESS)) {
				CAST_SUCCESS(Cast_Type::POINTER_TO_ADDRESS);
			}
			else if (src_is_ptr && dst_is_ptr) {
				CAST_SUCCESS(Cast_Type::POINTERS);
			}
		}

		// Check primitive casts (integers, floats, address, enums)
		{
			// Check if types are equal
			Datatype* src = from_type;
			Datatype* dst = to_type;

			// Handle primitive casts
			if (src->type == Datatype_Type::PRIMITIVE || src->type == Datatype_Type::ENUM)
			{
				if (src->type == Datatype_Type::PRIMITIVE && dst->type == Datatype_Type::ENUM)
				{
					auto src_primitive = downcast<Datatype_Primitive>(src);
					if (src_primitive->primitive_class == Primitive_Class::INTEGER) {
						CAST_SUCCESS(Cast_Type::INT_TO_ENUM);
					}
				}
				else if (src->type == Datatype_Type::ENUM && dst->type == Datatype_Type::PRIMITIVE)
				{
					auto dst_primitive = downcast<Datatype_Primitive>(dst);
					if (dst_primitive->primitive_class == Primitive_Class::INTEGER) {
						CAST_SUCCESS(Cast_Type::ENUM_TO_INT);
					}
				}
				else if (src->type == Datatype_Type::ENUM && dst->type == Datatype_Type::ENUM)
				{
					CAST_SUCCESS(Cast_Type::ENUMS);
				}
				else if (src->type == Datatype_Type::PRIMITIVE && dst->type == Datatype_Type::PRIMITIVE)
				{
					auto src_class = downcast<Datatype_Primitive>(src)->primitive_class;
					auto dst_class = downcast<Datatype_Primitive>(dst)->primitive_class;

					// Figure out allowed mode and cast type
					if (src_class == Primitive_Class::INTEGER && dst_class == Primitive_Class::INTEGER) {
						CAST_SUCCESS(Cast_Type::INTEGERS);
					}
					else if (src_class == Primitive_Class::INTEGER && dst_class == Primitive_Class::FLOAT) {
						CAST_SUCCESS(Cast_Type::INT_TO_FLOAT);
					}
					else if (src_class == Primitive_Class::FLOAT && dst_class == Primitive_Class::INTEGER) {
						CAST_SUCCESS(Cast_Type::FLOAT_TO_INT);
					}
					else if (src_class == Primitive_Class::FLOAT && dst_class == Primitive_Class::FLOAT) {
						CAST_SUCCESS(Cast_Type::FLOATS);
					}
					else if (src_class == Primitive_Class::INTEGER && dst_class == Primitive_Class::ADDRESS) {
						CAST_SUCCESS(Cast_Type::INTEGERS);
					}
					else if (src_class == Primitive_Class::ADDRESS && dst_class == Primitive_Class::INTEGER) {
						CAST_SUCCESS(Cast_Type::INTEGERS);
					}
				}
			}
		}

		// Check to-subtype cast
		if (src->type == Datatype_Type::STRUCT && dst->type == Datatype_Type::STRUCT)
		{
			auto src_struct = downcast<Datatype_Struct>(src);
			auto dst_struct = downcast<Datatype_Struct>(dst);
			while (dst_struct != nullptr && dst_struct != src_struct) {
				dst_struct = dst_struct->parent_struct;
			}
			if (dst_struct == src_struct)
			{
				CAST_SUCCESS(Cast_Type::TO_SUB_TYPE);
			}
		}
	}

	// Check built-in auto casts (array-to-slice, function-pointers, to-optional-pointer, any-cast, to-base-type)
	{
		// Array-to-Slice
		if (src->type == Datatype_Type::ARRAY && dst->type == Datatype_Type::SLICE)
		{
			Datatype* array_element = downcast<Datatype_Array>(src)->element_type;
			Datatype* slice_element = downcast<Datatype_Slice>(dst)->element_type;
			if (types_are_equal(array_element, slice_element))
			{
				CAST_SUCCESS(Cast_Type::ARRAY_TO_SLICE);
			}
		}

		// To/From any
		if (types_are_equal(dst, upcast(types.any_type))) 
		{
			CAST_SUCCESS(Cast_Type::TO_ANY);
		}
		else if (types_are_equal(src, upcast(types.any_type))) 
		{
			CAST_SUCCESS(Cast_Type::FROM_ANY);
		}

		// Function-Pointer conversions
		if (src->type == Datatype_Type::FUNCTION_POINTER && dst->type == Datatype_Type::FUNCTION_POINTER)
		{
			// Note: 
			// In Upp function signatures are already different if the parameter _names_ are different, even though the types are the same
			// Casting between two different function signatures only works if they have the same parameter/return types.
			// In C this would always result in the same type, but in upp the parameter names/default values change the function type
			Call_Signature* src_sig = downcast<Datatype_Function_Pointer>(src)->signature;
			Call_Signature* dst_sig = downcast<Datatype_Function_Pointer>(dst)->signature;
			auto& src_params = src_sig->parameters;
			auto& dst_params = dst_sig->parameters;
			bool cast_valid = true;

			if (src_sig->return_type_index != dst_sig->return_type_index || src_params.size != dst_params.size) {
				cast_valid = false;
			}
			else
			{
				for (int i = 0; i < src_params.size; i++) {
					auto& param1 = src_params[i];
					auto& param2 = dst_params[i];
					if (!types_are_equal(param1.datatype, param2.datatype)) {
						cast_valid = false;
					}
				}
			}

			if (cast_valid) {
				CAST_SUCCESS(Cast_Type::POINTERS);
			}
		}

		// Type-Mods update (Address-of, dereference, to_optional_pointer and to_base)
		Type_Modifier_Info src_info = datatype_get_modifier_info(src);
		Type_Modifier_Info dst_info = datatype_get_modifier_info(dst);
		if (src_info.base_type == dst_info.base_type)
		{
			bool update_possible = true;
			Cast_Type result_cast_type = Cast_Type::POINTERS;

			// Check if struct-subtypes match
			if (update_possible && src_info.struct_subtype != dst_info.struct_subtype)
			{
				Datatype_Struct* subtype = src_info.struct_subtype;
				while (subtype != nullptr && subtype != dst_info.struct_subtype) {
					subtype = subtype->parent_struct;
				}
				update_possible = subtype == dst_info.struct_subtype;
				if (src_info.pointer_level == 0 && dst_info.pointer_level == 0) {
					result_cast_type = Cast_Type::TO_BASE_TYPE;
				}
			}

			if (src_info.pointer_level != dst_info.pointer_level && is_auto_cast) {
				result_cast_type = check_if_type_modifier_update_valid(src_info, dst_info, value_is_temporary);
				if (result_cast_type == Cast_Type::INVALID) {
					update_possible = false;
				}
			}

			if (update_possible)
			{
				CAST_SUCCESS(result_cast_type);
			}
		}
	}

	// Find all custom-casts that may be valid (E.g. type-mods/polymorphic matching may not be correct)
	if (semantic_context->current_symbol_table == nullptr) {
		result.cast_type = Cast_Type::NO_CAST;
		return result;
	}
	auto scratch_arena = semantic_context->scratch_arena;
	auto checkpoint = scratch_arena->make_checkpoint();
	SCOPE_EXIT(scratch_arena->rewind_to_checkpoint(checkpoint));
	DynArray<Custom_Operator_Install> operators = DynArray<Custom_Operator_Install>::create(scratch_arena);
	{
		DynArray<Reachable_Operator_Table> reachable_operator_tables = symbol_table_query_reachable_operator_tables(
			semantic_context->current_symbol_table, Custom_Operator_Type::CAST, semantic_context
		);
		Custom_Operator_Key key;
		key.type = Custom_Operator_Type::CAST;
		key.options.custom_cast.from_type = datatype_get_undecorated(src, true, true, true, true);
		key.options.custom_cast.to_type = datatype_get_undecorated(dst, true, true, true, true);
		Custom_Operator_Key poly_key;
		poly_key.type = Custom_Operator_Type::CAST;
		poly_key.options.custom_cast.from_type = nullptr;
		poly_key.options.custom_cast.to_type = nullptr;
		for (int i = 0; i < reachable_operator_tables.size; i++)
		{
			auto& table = reachable_operator_tables[i];
			auto& ops = table.operator_table->installed_operators;
			DynArray<Custom_Operator_Install>* installs = hashtable_find_element(&ops, key);
			if (installs != nullptr) {
				for (int j = 0; j < installs->size; j++) {
					operators.push_back((*installs)[j]);
				}
			}
			installs = hashtable_find_element(&ops, poly_key);
			if (installs != nullptr) {
				for (int j = 0; j < installs->size; j++) {
					operators.push_back((*installs)[j]);
				}
			}
		}
	}

	// Deduplicate operators
	for (int i = 0; i < operators.size; i++)
	{
		auto op_install = operators[i];
		for (int j = i + 1; j < operators.size; j++) {
			auto& other_op = operators[j];
			if (op_install.custom_operator == other_op.custom_operator) {
				operators.swap_remove(j);
				j -= 1;
				continue;
			}
		}
	}

	// Remove invalid types (Polymorphic matching not valid or different type modifiers)
	auto pattern_checkpoint = scratch_arena->make_checkpoint();
	for (int i = 0; i < operators.size; i++)
	{
		auto& op = operators[i];
		auto& custom_cast = op.custom_operator->options.custom_cast;
		auto signature = custom_cast.function->function->signature;

		Datatype* arg_type = signature->parameters[0].datatype;
		Datatype* return_type = signature->return_type().value;
		if (custom_cast.call_by_reference) {
			arg_type = downcast<Datatype_Pointer>(arg_type)->element_type;
			if (value_is_temporary) {
				// TODO: Do something here?

			}
		}

		// Check if auto-cast mode matches
		bool remove = false;
		if (!custom_cast.auto_cast && is_auto_cast) {
			remove = true;
		}

		// Check if src type matches
		if (!remove)
		{
			if (arg_type->contains_pattern)
			{
				scratch_arena->rewind_to_checkpoint(pattern_checkpoint);
				Pattern_Match_Result result = pattern_match_result_make_empty(scratch_arena);
				bool match_success = pattern_type_match_against(result, 0, arg_type, src, semantic_context);
				if (!match_success) {
					remove = true;
				}
				else {
					remove = remove || !pattern_match_result_check_constraints_pairwise(result);
				}
			}
			else {
				remove = remove || !types_are_equal(arg_type, src);
			}
		}
		
		// Check if dst type matches
		if (!remove)
		{
			if (return_type->contains_pattern)
			{
				scratch_arena->rewind_to_checkpoint(pattern_checkpoint);
				Pattern_Match_Result result = pattern_match_result_make_empty(scratch_arena);
				bool match_success = pattern_type_match_against(result, 0, return_type, dst, semantic_context);
				if (!match_success) {
					remove = true;
				}
				else {
					remove = remove || !pattern_match_result_check_constraints_pairwise(result);
				}
			}
			else {
				remove = remove || !types_are_equal(return_type, dst);
			}
		}

		// Remove operator if necessary
		if (remove) {
			operators.swap_remove(i);
			i -= 1;
			continue;
		}
	}

	// Resolve overloads (FUTURE)
	if (operators.size != 1) {
		result.cast_type = Cast_Type::INVALID;
		return result;
	}

	// Instanciate polymorphic function if necessary
	Custom_Operator* op = operators[0].custom_operator;
	result.cast_type = Cast_Type::CUSTOM_CAST;
	result.result_type = dst;
	result.custom_cast_function = nullptr;
	// if (called_from_editor) 
	if (semantic_context->current_workload == nullptr) {
		return result;
	}

	if (op->options.custom_cast.function->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) 
	{
		Poly_Header* header = op->options.custom_cast.function->poly_function.poly_header;
		Call_Info call_info = call_info_make_with_empty_arguments(call_origin_make(header), &semantic_context->compilation_data->arena);
		call_info.parameter_values[0] = parameter_value_make_datatype_known(src);
		Poly_Instance* instance = poly_header_instanciate(&call_info, upcast(operators[0].node), Node_Section::FIRST_TOKEN, semantic_context);
		if (instance != nullptr) {
			result.custom_cast_function = instance->options.function_instance->function;
		}
	}
	else 
	{
		result.custom_cast_function = op->options.custom_cast.function->function;
	}

	return result;
	
#undef SUCCESS_NOT_AUTOCAST
#undef CAST_SUCCESS
}

bool expression_apply_cast_if_possible(
	AST::Expression* expression, Datatype* to_type, bool is_auto_cast, bool* out_result_is_temp, Semantic_Context* semantic_context)
{
	if (out_result_is_temp != nullptr) {
		*out_result_is_temp = true;
	}

	Expression_Info* expr_info = get_info(expression, semantic_context);
	Expression_Value_Info value_info = expression_info_get_value_info(expr_info, semantic_context->compilation_data->type_system);
	Cast_Info new_cast_info = check_if_cast_possible(
		value_info.initial_type, to_type, value_info.initial_value_is_temporary, is_auto_cast, semantic_context
	);
	if (new_cast_info.cast_type == Cast_Type::UNKNOWN) {
		semantic_analyser_set_error_flag(true, semantic_context);
	}
	if (new_cast_info.cast_type != Cast_Type::INVALID) 
	{
		expr_info->cast_info = new_cast_info;
		if (out_result_is_temp != nullptr) {
			*out_result_is_temp = cast_type_result_is_temporary(new_cast_info.cast_type, value_info.initial_value_is_temporary);
		}
		return true;
	}

	return false;
}



// ARGUMENTS/PARAMETER MATCHING + OVERLOADING
Call_Origin call_origin_make(Poly_Header* poly_header) 
{
	Call_Origin origin;
	origin.type = poly_header->is_function ? Call_Origin_Type::POLY_FUNCTION : Call_Origin_Type::POLY_STRUCT;
	if (poly_header->is_function) {
		origin.type = Call_Origin_Type::POLY_FUNCTION;
		origin.options.poly_function = poly_header->origin.function_progress->poly_function;
	}
	else {
		origin.type = Call_Origin_Type::POLY_STRUCT;
		origin.options.poly_struct = poly_header->origin.struct_workload;
	}
	origin.signature = poly_header->signature;
	return origin;
}

Call_Origin call_origin_make(ModTree_Function* function) 
{
	Call_Origin origin;
	origin.type = Call_Origin_Type::FUNCTION;
	origin.options.function = function;
	origin.signature = function->signature;
	return origin;
}

Call_Origin call_origin_make(Datatype_Function_Pointer* function_ptr_type) 
{
	Call_Origin origin;
	origin.type = Call_Origin_Type::FUNCTION_POINTER;
	origin.options.function_pointer = function_ptr_type;
	origin.signature = function_ptr_type->signature;
	return origin;
}

Call_Origin call_origin_make(Hardcoded_Type hardcoded_type, Compilation_Data* compilation_data) 
{
	Call_Origin origin;
	origin.type = Call_Origin_Type::HARDCODED;
	origin.options.hardcoded = hardcoded_type;
	origin.signature = compilation_data->hardcoded_function_signatures[(int)hardcoded_type];
	return origin;
}

Call_Origin call_origin_make(Custom_Operator_Type context_change_type, Compilation_Data* compilation_data)
{
	Call_Origin origin;
	origin.type = Call_Origin_Type::CONTEXT_CHANGE;
	origin.options.context_change_type = context_change_type;
	origin.signature = compilation_data->context_change_type_signatures[(int)context_change_type];
	return origin;
}

Call_Origin call_origin_make(Datatype_Struct* structure, Semantic_Context* semantic_context) 
{
	Call_Origin origin;
	origin.type = structure->is_union ? Call_Origin_Type::STRUCT_INITIALIZER : Call_Origin_Type::UNION_INITIALIZER;
	origin.options.structure = structure;

	// Create initializer signature if not already done (FUTURE: May cause problems with multithreading)
	if (structure->initializer_signature_cached == nullptr) 
	{
		// Wait for struct-workload so members are analysed
		if (semantic_context->current_workload == nullptr) {
			assert(structure->workload == nullptr || structure->workload->base.is_finished, "");
		}
		if (structure->workload != nullptr) {
			auto executer = semantic_context->compilation_data->workload_executer;
			analysis_workload_add_dependency_internal(executer, semantic_context->current_workload, upcast(structure->workload));
			workload_executer_wait_for_dependency_resolution(semantic_context);
		}
		assert(structure->base.memory_info.available, "");

		// Create new signature
		bool is_union_initializer = structure->is_union;
		Call_Signature* signature = call_signature_create_empty();
		for (int i = 0; i < structure->members.size; i++) {
			auto& member = structure->members[i];
			call_signature_add_parameter(signature, member.id, member.type, !is_union_initializer, is_union_initializer, false);
		}
		signature = call_signature_register(signature, semantic_context->compilation_data);

		// Store signature
		structure->initializer_signature_cached = signature;
	}

	origin.signature = structure->initializer_signature_cached;
	return origin;
}

Call_Origin call_origin_make(Datatype_Slice* slice_type, Compilation_Data* compilation_data)
{
	if (slice_type->slice_initializer_signature_cached == nullptr) 
	{
		auto& ids = compilation_data->compiler->identifier_pool.predefined_ids;
		Call_Signature* signature = call_signature_create_empty();
		call_signature_add_parameter(signature, ids.data, slice_type->data_member.type, true, false, false);
		call_signature_add_parameter(signature, ids.size, slice_type->size_member.type, true, false, false);
		signature = call_signature_register(signature, compilation_data);
		slice_type->slice_initializer_signature_cached = signature;
	}

	Call_Origin origin;
	origin.type = Call_Origin_Type::SLICE_INITIALIZER;
	origin.options.slice_type = slice_type;
	origin.signature = slice_type->slice_initializer_signature_cached;
	return origin;
}

Call_Origin call_origin_make_cast(Compilation_Data* compilation_data)
{
	Call_Origin origin;
	origin.type = Call_Origin_Type::CAST;
	origin.signature = compilation_data->cast_signature;
	return origin;
}

Call_Origin call_origin_make_error(Compilation_Data* compilation_data)
{
	Call_Origin origin;
	origin.type = Call_Origin_Type::ERROR_OCCURED;
	origin.signature = compilation_data->empty_call_signature;
	return origin;
}




Call_Info call_info_make_with_empty_arguments(Call_Origin origin, Arena* arena)
{
	Call_Info call_info;
	call_info.origin = origin;
	call_info.argument_matching_success = false;
	call_info.call_node = nullptr;
	call_info.instanciated = false;
	call_info.argument_infos = array_create_static<Argument_Info>(nullptr, 0);
	call_info.parameter_values = arena->allocate_array<Parameter_Value>(origin.signature->parameters.size);
	for (int i = 0; i < call_info.parameter_values.size; i++) {
		call_info.parameter_values[i] = parameter_value_make_unset();
	}
	return call_info;
}

void call_info_add_arguments_from_call_node(Call_Info* call_info, AST::Call_Node* call_node, Arena* arena)
{
	call_info->call_node = call_node;
	call_info->argument_infos = arena->allocate_array<Argument_Info>(call_node->arguments.size);
	for (int i = 0; i < call_info->argument_infos.size; i++)
	{
		auto& arg_info = call_info->argument_infos[i];
		auto& arg = call_node->arguments[i];
		arg_info.expression = arg->value;
		arg_info.name = arg->name;
		arg_info.parameter_index = -1;
	}
}

Call_Info call_info_make_from_call_node(AST::Call_Node* call_node, Call_Origin origin, Arena* arena) 
{
	Call_Info result = call_info_make_with_empty_arguments(origin, arena);
	call_info_add_arguments_from_call_node(&result, call_node, arena);
	return result;
}

Call_Info call_info_make_error(AST::Call_Node* call_node, Compilation_Data* compilation_data) {
	return call_info_make_from_call_node(call_node, call_origin_make_error(compilation_data), &compilation_data->arena);
}

// May return error-occured if no origin is valid...
Call_Origin call_origin_from_expression_info(Expression_Info& info, Compilation_Data* compilation_data)
{
	auto type_system = compilation_data->type_system;

	// Figure out call type
	switch (info.result_type)
	{
	case Expression_Result_Type::POLYMORPHIC_PATTERN:
	case Expression_Result_Type::NOTHING:
	case Expression_Result_Type::DATATYPE: return call_origin_make_error(compilation_data);
	case Expression_Result_Type::POLYMORPHIC_STRUCT: return call_origin_make(info.options.polymorphic_struct->poly_header);
	case Expression_Result_Type::POLYMORPHIC_FUNCTION: return call_origin_make(info.options.poly_function.poly_header);
	case Expression_Result_Type::FUNCTION: return call_origin_make(info.options.function);
	case Expression_Result_Type::HARDCODED_FUNCTION: return call_origin_make(info.options.hardcoded, compilation_data);
	case Expression_Result_Type::CONSTANT:
	{
		auto& constant = info.options.constant;
		auto type = constant.type;
		if (type->type != Datatype_Type::FUNCTION_POINTER) {
			return call_origin_make_error(compilation_data);
		}

		int function_slot_index = (int)(*(i64*)constant.memory) - 1;
		auto& slots = compilation_data->function_slots;
		if (function_slot_index < 0 || function_slot_index >= slots.size) {
			return call_origin_make_error(compilation_data);
		}

		auto function = slots[function_slot_index].modtree_function;
		if (function == nullptr) {
			// This is the case if we somehow managed to call a ir-generated function (entry, allocate, deallocate, reallocate)
			// which shouldn't happen in normal circumstances, but can be made to happen (By baking a function pointer with an integer id)
			return call_origin_make_error(compilation_data);
		}
		return call_origin_make(function);
	}
	case Expression_Result_Type::VALUE:
	{
		auto type = expression_info_get_type(&info, false, type_system);
		if (type->type != Datatype_Type::FUNCTION_POINTER) {
			return call_origin_make_error(compilation_data);
		}
		auto function_ptr_type = downcast<Datatype_Function_Pointer>(type);
		return call_origin_make(function_ptr_type);
	}
	default: panic("");
	}

	return call_origin_make_error(compilation_data);
}

void analyse_parameter_value_if_not_already_done(
	Call_Info* call_info, Parameter_Value* param_value, Semantic_Context* semantic_context, Expression_Context context, bool allow_poly_pattern = false)
{
	if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED && param_value->value_type != Parameter_Value_Type::ARGUMENT_EXPRESSION) 
	{
		panic("This should be checked beforehand");
		return;
	}

	if (param_value->value_type != Parameter_Value_Type::ARGUMENT_EXPRESSION) return;
	assert(param_value->options.argument_index != -1, "");

	// Check if already analysed
	auto arg_expr = call_info->argument_infos[param_value->options.argument_index].expression;
	auto info_opt = pass_get_node_info(semantic_context->current_pass, arg_expr, Info_Query::TRY_READ, semantic_context->compilation_data);
	if (info_opt != nullptr) 
	{
		expression_context_apply(info_opt, context, semantic_context, arg_expr, Node_Section::WHOLE);
	}
	else 
	{
		semantic_analyser_analyse_expression_value(arg_expr, context, semantic_context, false, allow_poly_pattern);
	}
}

void call_info_analyse_all_arguments(
	Call_Info* call_info, bool allow_poly_pattern, Semantic_Context* semantic_context, bool handle_subtype_initializer_as_error = false)
{
	for (int i = 0; i < call_info->argument_infos.size; i++)
	{
		auto& argument = call_info->argument_infos[i];
		auto info_opt = pass_get_node_info(semantic_context->current_pass, argument.expression, Info_Query::TRY_READ, semantic_context->compilation_data);
		if (info_opt) continue; // Already analysed

		Expression_Context context = expression_context_make_error(); // If no error occured the context should always be available
		if (argument.parameter_index != -1) {
			auto param_type = call_info->origin.signature->parameters[argument.parameter_index].datatype;
			if (!param_type->contains_pattern) {
				context = expression_context_make_specific_type(param_type);
			}
		}
		else {
			semantic_analyser_set_error_flag(true, semantic_context);
		}

		semantic_analyser_analyse_expression_value(argument.expression, context, semantic_context, false, allow_poly_pattern);
	}

	// Note: subtype-initializer already reported errors during parameter matching, so we don't do that here
	if (handle_subtype_initializer_as_error && call_info->call_node != 0)
	{
		for (int i = 0; i < call_info->call_node->subtype_initializers.size; i++)
		{
			auto subtype_init = call_info->call_node->subtype_initializers[i];
			Call_Info* call_info = pass_get_node_info(
				semantic_context->current_pass, subtype_init->call_node, Info_Query::TRY_READ, semantic_context->compilation_data
			);
			if (call_info == nullptr) {
				call_info = get_info(subtype_init->call_node, semantic_context, true);
				*call_info = call_info_make_error(subtype_init->call_node, semantic_context->compilation_data);
			}
			call_info_analyse_all_arguments(call_info, true, semantic_context, handle_subtype_initializer_as_error);
		}
	}
}

// Parameter matching
struct Overload_Candidate
{
	Call_Info call_info;

	// Source info (For storing infos after overload has been resolved)
	Symbol* symbol; // May be null
	Expression_Info expression_info; // May be empty, required to set correct expression info after overload resolution

	// For convenience when resolving overloads
	Datatype* active_type;
	int overloading_arg_cast_priority;

	// For poly overload resolution
	bool poly_type_matches;
	bool poly_type_requires_modifier_update;
	int  poly_type_match_depth;
};

// Returns true if successfull
bool arguments_match_to_parameters(Call_Info& call_info, Semantic_Context* semantic_context, bool is_instanciate = false)
{
	call_info.argument_matching_success = true;
	assert(call_info.call_node != nullptr, "");

	auto call_node = call_info.call_node;
	if (call_info.origin.type != Call_Origin_Type::STRUCT_INITIALIZER)
	{
		for (int i = 0; i < call_node->subtype_initializers.size; i++) {
			auto sub_init = call_node->subtype_initializers[i];
			log_semantic_error(semantic_context, "Subtype_initializer only valid on struct-initializers", upcast(sub_init), Node_Section::FIRST_TOKEN);
			call_info.argument_matching_success = false;
		}
	}
	const bool allow_uninitialized_token =
		call_info.origin.type == Call_Origin_Type::STRUCT_INITIALIZER ||
		call_info.origin.type == Call_Origin_Type::POLY_STRUCT;
	if (!allow_uninitialized_token)
	{
		for (int i = 0; i < call_node->uninitialized_tokens.size; i++) {
			auto token_expr = call_node->uninitialized_tokens[i];
			log_semantic_error(semantic_context, "Uninitialized-token only valid for struct-initializers", upcast(token_expr), Node_Section::FIRST_TOKEN);
			call_info.argument_matching_success = false;
		}
	}

	auto& args = call_info.argument_infos;
	auto& params = call_info.origin.signature->parameters;

	// Match arguments to parameters and check for errors
	bool named_argument_encountered = false;
	int unnamed_argument_count = 0;
	for (int i = 0; i < args.size; i++)
	{
		auto& arg = args[i];
		AST::Node* error_node = upcast(arg.expression);
		if (error_node->parent->type == AST::Node_Type::ARGUMENT) {
			error_node = error_node->parent; // Use parent argument node as error node if necessary
		}

		int param_index = -1;
		if (arg.name.available)
		{
			named_argument_encountered = true;

			// Search parameters for name
			for (int j = 0; j < params.size; j++) {
				auto& param = params[j];
				if (param.name == arg.name.value) {
					param_index = j;
					break;
				}
			}

			if (param_index == -1) {
				log_semantic_error(semantic_context, "Argument name does not match any parameter name", error_node, Node_Section::IDENTIFIER);
				call_info.argument_matching_success = false;
				continue;
			}
		}
		else // Unnamed arguments
		{
			if (named_argument_encountered) {
				log_semantic_error(semantic_context, "Unnamed call_node must not appear after named call_node!", error_node, Node_Section::FIRST_TOKEN);
				call_info.argument_matching_success = false;
				continue;
			}

			param_index = unnamed_argument_count;
			unnamed_argument_count += 1;
			if (param_index >= params.size) {
				log_semantic_error(semantic_context, "Call_Signature does not accept this many unnamed parameters", error_node, Node_Section::FIRST_TOKEN);
				call_info.argument_matching_success = false;
				continue;
			}
			else if (params[param_index].requires_named_addressing) {
				log_semantic_error(semantic_context, "This parameter requires named addressing", error_node, Node_Section::FIRST_TOKEN);
				call_info.argument_matching_success = false;
				continue;
			}
		}

		auto& param_info = params[param_index];
		auto& param_value = call_info.parameter_values[param_index];
		if (param_info.must_not_be_set)
		{
			if (param_index == call_info.origin.signature->return_type_index && !arg.name.available) {
				log_semantic_error(semantic_context, "Call_Signature doesn't accept this many unnamed call_node", error_node, Node_Section::FIRST_TOKEN);
			}
			else {
				log_semantic_error(semantic_context, "Parameter must not be set", error_node, Node_Section::FIRST_TOKEN);
			}
			call_info.argument_matching_success = false;
		}
		else if (param_value.value_type != Parameter_Value_Type::NOT_SET) {
			log_semantic_error(semantic_context, "Argument was already specified", error_node, Node_Section::FIRST_TOKEN);
			call_info.argument_matching_success = false;
		}
		else {
			arg.parameter_index = param_index;
			param_value.value_type = Parameter_Value_Type::ARGUMENT_EXPRESSION;
			param_value.options.argument_index = i;
		}
	}

	// Check if all required parameters were specified
	if (call_info.argument_matching_success && !is_instanciate && !(allow_uninitialized_token && call_node->uninitialized_tokens.size > 0))
	{
		bool missing_parameter_reported = false;
		for (int i = 0; i < params.size; i++)
		{
			auto& param = params[i];
			if (call_info.parameter_values[i].value_type == Parameter_Value_Type::NOT_SET && param.required)
			{
				if (!missing_parameter_reported) {
					log_semantic_error(semantic_context, "Missing parameters", upcast(call_node), Node_Section::ENCLOSURE);
					missing_parameter_reported = true;
				}
				log_error_info_id(semantic_context, param.name);
				call_info.argument_matching_success = false;
			}
		}
	}

	return call_info.argument_matching_success;
}

// Note: Not all arguments are analysed here, and if error-occured call.matching_succeeded is false
Call_Info* overloading_analyse_call_expression_and_resolve_overloads(
	Semantic_Context* semantic_context, AST::Expression* expression, Datatype* expected_return_type = nullptr)
{
	assert(expression->type == AST::Expression_Type::FUNCTION_CALL || expression->type == AST::Expression_Type::INSTANCIATE, "");
	auto compilation_data = semantic_context->compilation_data;
	auto type_system = compilation_data->type_system;
	Arena& scratch_arena = *semantic_context->scratch_arena;
	auto checkpoint = scratch_arena.make_checkpoint();
	SCOPE_EXIT(scratch_arena.rewind_to_checkpoint(checkpoint));

	// Differentiate between #instanciate and normal call
	const bool is_instanciate = expression->type == AST::Expression_Type::INSTANCIATE;
	bool is_dot_call = false;
	AST::Path_Lookup* path_lookup = nullptr;
	AST::Expression* call_expr = nullptr;
	AST::Call_Node* call_node = nullptr;
	if (is_instanciate)
	{
		auto& instanciate = expression->options.instanciate;
		path_lookup = instanciate.path_lookup;
		call_node = instanciate.call_node;
	}
	else
	{
		auto& call_info = expression->options.call;
		call_node = call_info.call_node;
		call_expr = call_info.expr;
		if (call_expr->type == AST::Expression_Type::PATH_LOOKUP) {
			path_lookup = call_expr->options.path_lookup;
		}
	}

	Call_Info* call_info = get_info(call_node, semantic_context, true);

	// Find all overload candidates
	DynArray<Overload_Candidate> candidates = DynArray<Overload_Candidate>::create(&scratch_arena, 0);
	auto helper_add_overload_candidate = [&](Call_Origin call_origin) -> Overload_Candidate* {
		Overload_Candidate candidate;
		candidate.symbol = nullptr;
		candidate.call_info = call_info_make_from_call_node(call_node, call_origin, &scratch_arena);
		candidates.push_back(candidate);
		candidate.expression_info.result_type = (Expression_Result_Type)-1;
		return &candidates.last();
	};

	// Find all overload candidates
	if (path_lookup != nullptr)
	{
		// Find all symbol-overloads
		DynArray<Symbol*> symbols = path_lookup_resolve(path_lookup, &scratch_arena, true, semantic_context);
		if (symbols.size == 0) {
			// Error is already reported here
			log_semantic_error(semantic_context, "Could not resolve Symbol (No definition found)", upcast(path_lookup->last), Node_Section::FIRST_TOKEN);
			log_error_info_id(semantic_context, path_lookup->last->name);
			*call_info = call_info_make_error(call_node, semantic_context->compilation_data);
			return call_info;
		}

		// Convert symbols to overload candidates
		bool encountered_unknown = false;
		for (int i = 0; i < symbols.size; i++)
		{
			auto& symbol = symbols[i];
			if (symbol->type == Symbol_Type::MODULE) {
				continue;
			}

			auto info = analyse_symbol_as_expression(symbol, expression_context_make_dereference(), path_lookup->last, semantic_context);
			if (info.result_type == Expression_Result_Type::VALUE || info.result_type == Expression_Result_Type::CONSTANT) {
				if (datatype_is_unknown(expression_info_get_type(&info, false, type_system))) {
					encountered_unknown = true;
				}
			}
			auto call_origin = call_origin_from_expression_info(info, semantic_context->compilation_data);
			if (call_origin.type == Call_Origin_Type::ERROR_OCCURED) continue;

			// Create candidate
			Overload_Candidate* candidate = helper_add_overload_candidate(call_origin);
			candidate->symbol = symbol;
			candidate->expression_info = info;
		}

		// Check success
		if (encountered_unknown) {
			semantic_analyser_set_error_flag(true, semantic_context);
			*call_info = call_info_make_error(call_node, semantic_context->compilation_data);
			return call_info;
		}
		if (candidates.size == 0) {
			log_semantic_error(semantic_context, "None of the symbol-overloads are callable!", upcast(path_lookup->last));
			for (int i = 0; i < symbols.size; i++) {
				log_error_info_symbol(semantic_context, symbols[i]);
			}
			*call_info = call_info_make_error(call_node, semantic_context->compilation_data);
			return call_info;
		}
	}
	else // Normal function call expression
	{
		Expression_Info* call_expr_info = semantic_analyser_analyse_expression_any(call_expr, expression_context_make_dereference(), semantic_context);
		Call_Origin call_origin = call_origin_from_expression_info(*call_expr_info, semantic_context->compilation_data);
		if (call_origin.type == Call_Origin_Type::ERROR_OCCURED)
		{
			if (!datatype_is_unknown(expression_info_get_type(call_expr_info, false, type_system))) {
				log_semantic_error(semantic_context, "Expression is not callable!", upcast(call_expr));
				log_error_info_expression_result_type(semantic_context, call_expr_info->result_type);
			}
			else {
				semantic_analyser_set_error_flag(true, semantic_context);
			}

			*call_info = call_info_make_error(call_node, semantic_context->compilation_data);
			return call_info;
		}

		helper_add_overload_candidate(call_origin);
	}

	auto& helper_set_callable_to_candidate = [&](Overload_Candidate& candidate) {
		// Update expression infos
		if (path_lookup != nullptr)
		{
			assert(candidate.symbol != nullptr, "");
			path_lookup_set_result_symbol(path_lookup, candidate.symbol, semantic_context);
			// Note: we only need to store expr-info if we have a path-lookup and a call expression,
			//		in the other cases the value is already set
			if (call_expr != nullptr)
			{
				auto call_expr_info = pass_get_node_info(
					semantic_context->current_pass, call_expr, Info_Query::CREATE_IF_NULL, semantic_context->compilation_data
				);
				*call_expr_info = candidate.expression_info;
				assert((int)candidate.expression_info.result_type != -1, "Candidate must have valid expression info");
			}
		}

		*call_info = candidate.call_info;
		Arena& persistent_arena = semantic_context->compilation_data->arena;
		Array<Parameter_Value> persistent_values = persistent_arena.allocate_array<Parameter_Value>(call_info->parameter_values.size);
		memory_copy(persistent_values.data, call_info->parameter_values.data, call_info->parameter_values.size * sizeof(Parameter_Value));
		call_info->parameter_values = persistent_values;
		Array<Argument_Info> persistent_infos = persistent_arena.allocate_array<Argument_Info>(call_info->argument_infos.size);
		memory_copy(persistent_infos.data, call_info->argument_infos.data, call_info->argument_infos.size * sizeof(Argument_Info));
		call_info->argument_infos = persistent_infos;

		if (candidate.call_info.origin.type == Call_Origin_Type::FUNCTION) {
			semantic_analyser_register_function_call(candidate.call_info.origin.options.function, semantic_context);
		}
	};

	// Do argument-parameter mapping if we only have one candidate + Early-Exit if it's not working
	// This is done to get better error messages
	if (candidates.size == 1)
	{
		auto& candidate = candidates[0];
		if (!arguments_match_to_parameters(candidate.call_info, semantic_context, is_instanciate))
		{
			helper_set_callable_to_candidate(candidate);
			return call_info;
		}
	}

	// Do Overload resolution
	// Differentiate candidates based on matching and argument types
	DynArray<int> analysed_argument_indices = DynArray<int>::create(&scratch_arena);
	if (candidates.size > 1)
	{
		// For #instanciate filter non-polymorphic functions
		if (is_instanciate)
		{
			for (int i = 0; i < candidates.size; i++) {
				auto& candidate = candidates[i];
				if (candidate.call_info.origin.type != Call_Origin_Type::POLY_FUNCTION) {
					candidates.swap_remove(i);
					i -= 1;
				}
			}
		}

		// Match arguments for overloads and remove candidates that don't match
		if (candidates.size > 1)
		{
			// Disable error logging for overload resolution
			auto error_checkpoint = error_checkpoint_start(semantic_context);
			SCOPE_EXIT(error_checkpoint_end(error_checkpoint, semantic_context));

			for (int i = 0; i < candidates.size; i++) {
				auto& candidate = candidates[i];
				if (!arguments_match_to_parameters(candidate.call_info, semantic_context, is_instanciate)) {
					candidates.swap_remove(i);
					i -= 1;
				}
			}
		}

		// Checks all candidates active type, and removes candidates where active-type doesn't match
		//		arg_type as closely as other candidates
		//		Note: We are assuming that the arg-type is never temporary, as this is not a criterion for overloading
		auto remove_candidates_based_on_better_type_match = [&](Datatype* arg_type, bool is_return_type)
			{
				int max_cast_priority = -1;
				int max_poly_depth = -1;
				bool poly_type_without_mod_update_exists = false;

				// Evaluate/Rank candidates
				for (int i = 0; i < candidates.size; i++)
				{
					auto& candidate = candidates[i];
					candidate.overloading_arg_cast_priority = 0;
					candidate.poly_type_matches = false;
					candidate.poly_type_requires_modifier_update = false;
					candidate.poly_type_match_depth = 0;

					Datatype* given_type = arg_type;
					Datatype* match_to_type = candidate.active_type;

					// Special code path for polymorphic values (Do pattern matching)
					if (match_to_type->contains_pattern)
					{
						// We don't update type-mods on return type
						if (!is_return_type) 
						{
							Type_Modifier_Info src_mods = datatype_get_modifier_info(given_type);
							if (src_mods.struct_subtype != nullptr) { // Keep subtype
								src_mods.base_type = upcast(src_mods.struct_subtype);
							}
							Type_Modifier_Info dst_mods = datatype_get_modifier_info(match_to_type);
							Cast_Type cast_type = check_if_type_modifier_update_valid(src_mods, dst_mods, false);
							if (cast_type == Cast_Type::INVALID) {
								continue;
							}
							else if (cast_type != Cast_Type::NO_CAST) {
								candidate.poly_type_requires_modifier_update = true;
							}
							match_to_type = type_system_make_type_with_modifiers(
								type_system, src_mods.base_type, dst_mods.pointer_level, dst_mods.optional_flags
							);
						}

						// Check if patterns match
						Pattern_Match_Result match_result = pattern_match_result_make_empty(&scratch_arena);
						bool can_match = pattern_type_match_against(match_result, 0, match_to_type, given_type, semantic_context);
						if (can_match) {
							if (pattern_match_result_check_constraints_pairwise(match_result)) {
								candidate.poly_type_matches = true;
							}
						}
						if (candidate.poly_type_matches) {
							max_poly_depth = math_maximum(max_poly_depth, match_result.max_match_depth);
						}

						if (!candidate.poly_type_requires_modifier_update) {
							poly_type_without_mod_update_exists = true;
						}
						continue;
					}

					// If it's the return type we need to check that the return value is castable to the expected value
					if (is_return_type) {
						Datatype* swap = given_type;
						given_type = match_to_type;
						match_to_type = swap;
					}

					// Check if cast is possible
					Cast_Info cast_info = check_if_cast_possible(given_type, match_to_type, false, true, semantic_context);
					candidate.overloading_arg_cast_priority = cast_type_get_overloading_priority(cast_info.cast_type);
					max_cast_priority = math_maximum(max_cast_priority, candidate.overloading_arg_cast_priority);
				}

				// Remove candidates that aren't as fit as other candidates
				for (int i = 0; i < candidates.size; i++)
				{
					auto& candidate = candidates[i];
					bool remove = false;

					if (candidate.active_type == nullptr) { continue; }
					else if (candidate.active_type->contains_pattern)
					{
						if (!candidate.poly_type_matches) {
							remove = true;
						}
						else if (candidate.poly_type_requires_modifier_update && poly_type_without_mod_update_exists) {
							remove = true;
						}
						else if (candidate.poly_type_match_depth < max_poly_depth) {
							remove = true;
						}
					}
					else
					{
						remove = candidate.overloading_arg_cast_priority < max_cast_priority;
					}

					if (remove) {
						candidates.swap_remove(i);
						i -= 1;
					}
				}
			};

		// For the remaining functions, check which argument types are different, and remove based on those
		for (int i = 0; i < call_node->arguments.size && candidates.size > 1; i++)
		{
			AST::Expression* argument_expression = call_node->arguments[i]->value;
			if (expression_is_auto_expression(argument_expression, compilation_data) && 
				!expression_is_auto_expression_with_preferred_type(argument_expression)) 
			{
				continue;
			}

			// Check if parameter types differ between overloads (And set active_index for further comparison)
			bool types_are_different = false;
			Datatype* prev_type = nullptr;
			for (int j = 0; j < candidates.size; j++)
			{
				auto& candidate = candidates[j];
				auto& param_info = candidate.call_info.origin.signature->parameters[candidate.call_info.argument_infos[i].parameter_index];
				candidate.active_type = param_info.datatype;

				// For #instanciate we can only compare the comptime variables
				if (is_instanciate && param_info.comptime_variable_index != -1) {
					types_are_different = false;
					break;
				}

				// Check if types are different
				if (prev_type == nullptr) {
					prev_type = candidate.active_type;
				}
				else if (!types_are_equal(prev_type, candidate.active_type)) {
					types_are_different = true;
				}
			}

			if (!types_are_different) {
				continue;
			}

			// Remove candidates based on argument type
			auto argument_type = semantic_analyser_analyse_expression_value(argument_expression, expression_context_make_unspecified(), semantic_context);
			analysed_argument_indices.push_back(i);
			remove_candidates_based_on_better_type_match(argument_type, false);
		}

		// If we still have candidates, try to differentiate based on return type
		if (candidates.size > 1 && expected_return_type != nullptr)
		{
			bool can_differentiate_based_on_return_type = false;
			Datatype* last_return_type = 0;
			for (int i = 0; i < candidates.size; i++)
			{
				auto& candidate = candidates[i];

				// Remove candidates which don't have return type
				if (candidate.call_info.origin.signature->return_type_index == -1) {
					candidates.swap_remove(i);
					i -= 1;
					continue;
				}

				// Otherwise set return type as active type
				candidate.active_type = candidate.call_info.origin.signature->return_type().value;
				if (last_return_type == nullptr) {
					last_return_type = candidate.active_type;
				}
				else if (!types_are_equal(last_return_type, candidate.active_type)) {
					can_differentiate_based_on_return_type = true;
				}
			}

			if (candidates.size > 1 && can_differentiate_based_on_return_type) {
				remove_candidates_based_on_better_type_match(expected_return_type, true);
			}
		}

		// Prefer non-polymorphic functions over polymorphic functions (Specializations)
		if (candidates.size > 1 && !is_instanciate)
		{
			bool non_polymorphic_exists = false;
			bool polymorphic_exists = false;
			for (int i = 0; i < candidates.size; i++)
			{
				auto& candidate = candidates[i];
				bool is_polymorphic =
					candidate.call_info.origin.type == Call_Origin_Type::POLY_STRUCT ||
					candidate.call_info.origin.type == Call_Origin_Type::POLY_FUNCTION;
					
				if (is_polymorphic) {
					polymorphic_exists = true;
				}
				else {
					non_polymorphic_exists = true;
				}
			}

			if (polymorphic_exists && non_polymorphic_exists)
			{
				for (int i = 0; i < candidates.size; i++)
				{
					auto& candidate = candidates[i];
					bool is_polymorphic =
						candidate.call_info.origin.type == Call_Origin_Type::POLY_STRUCT ||
						candidate.call_info.origin.type == Call_Origin_Type::POLY_FUNCTION;
					if (is_polymorphic)
					{
						candidates.swap_remove(i);
						i -= 1;
					}
				}
			}
		}
	}

	// Check for success
	if (candidates.size != 1)
	{
		// Log errors
		if (candidates.size > 1) {
			log_semantic_error(semantic_context, "Could not disambiguate between function overloads", expression);
		}
		else if (candidates.size == 0) {
			log_semantic_error(semantic_context, "None of the function overloads are valid", expression);
		}	
		*call_info = call_info_make_error(call_node, semantic_context->compilation_data);
		return call_info;
	}

	// Set expression/Symbol read info
	helper_set_callable_to_candidate(candidates[0]);

	// Note: casts are applied during analyse_all_arguments (or something similar), where analyse_argument_expression_if_not_already_done 

	return call_info;
}



// POLYMORPHIC HEADER PARSING
u64 hash_pattern_variable_state(Pattern_Variable_State* state)
{
	i64 type_val = (i64)state->type;
	u64 hash = hash_i64(&type_val);
	switch (state->type)
	{
	case Pattern_Variable_State_Type::SET: {
		hash = hash_combine(hash, hash_i32(&state->options.value.constant_index));
		break;
	}
	case Pattern_Variable_State_Type::PATTERN: {
		hash = hash_combine(hash, hash_pointer(state->options.pattern_type));
	}
	case Pattern_Variable_State_Type::UNSET:   hash = hash_combine(hash, 43634634); break;
	default: panic("");
	}
	return hash;
}

bool equals_pattern_variable_state(Pattern_Variable_State* ap, Pattern_Variable_State* bp)
{
	auto& a = *ap;
	auto& b = *bp;
	if (a.type != b.type) return false;
	switch (a.type)
	{
	case Pattern_Variable_State_Type::SET: return upp_constant_is_equal(a.options.value, b.options.value);
	case Pattern_Variable_State_Type::PATTERN: return types_are_equal(a.options.pattern_type, b.options.pattern_type);
	case Pattern_Variable_State_Type::UNSET:   break;
	default: panic("");
	}
	return true;
}

u64 hash_poly_instance(Poly_Instance** instance_ptr)
{
	Poly_Instance* instance = *instance_ptr;
	u64 hash = hash_pointer(instance->header);
	hash = hash_combine(hash, instance->variable_states.size);
	for (int i = 0; i < instance->variable_states.size; i++) {
		hash = hash_combine(hash, hash_pattern_variable_state(&instance->variable_states[i]));
	}
	hash = hash_combine(hash, hash_i32(&instance->partial_pattern_instances.size));
	for (int i = 0; i < instance->partial_pattern_instances.size; i++) {
		Datatype* partial_type = instance->partial_pattern_instances[i];
		hash = hash_combine(hash, hash_pointer(partial_type));
	}
	return hash;
}

bool equals_poly_instance(Poly_Instance** ap, Poly_Instance** bp)
{
	auto a = *ap;
	auto b = *bp;
	if (a->header != b->header ||
		a->type == Poly_Instance_Type::STRUCT_PATTERN || b->type == Poly_Instance_Type::STRUCT_PATTERN ||
		a->variable_states.size != b->variable_states.size ||
		a->partial_pattern_instances.size != b->partial_pattern_instances.size) return false;
	for (int i = 0; i < a->variable_states.size; i++)
	{
		if (!equals_pattern_variable_state(&a->variable_states[i], &b->variable_states[i])) return false;
	}
	for (int i = 0; i < a->partial_pattern_instances.size; i++)
	{
		if (!types_are_equal(a->partial_pattern_instances[i], b->partial_pattern_instances[i])) return false;
	}
	return true;
}

struct Parameter_Symbol_Lookup
{
	int parameter_index;
	String* lookup_id;
	AST::Symbol_Lookup* node;
};

void expression_search_for_symbol_lookups_and_pattern_variables(
	int parameter_index, AST::Expression* expression,
	Dynamic_Array<Parameter_Symbol_Lookup>& symbol_lookups,
	Dynamic_Array<Pattern_Variable>& pattern_variables)
{
	if (expression->type == AST::Expression_Type::PATH_LOOKUP) {
		auto path = expression->options.path_lookup;
		if (path->parts.size == 1) { // Skip path lookups, as we are only interested in parameter names usually
			Parameter_Symbol_Lookup lookup;
			lookup.parameter_index = parameter_index;
			lookup.lookup_id = path->last->name;
			lookup.node = path->last;
			dynamic_array_push_back(&symbol_lookups, lookup);
		}
		return;
	}
	else if (expression->type == AST::Expression_Type::PATTERN_VARIABLE)
	{
		Pattern_Variable variable;
		variable.definition_node = upcast(expression);
		variable.is_comptime_parameter = false;
		variable.value_access_index = pattern_variables.size;
		variable.defined_in_parameter_index = parameter_index;
		variable.name = expression->options.pattern_variable_name;

		// Values that are set later
		variable.symbol = nullptr; // Will be defined later
		variable.origin = nullptr;
		variable.pattern_variable_type = nullptr;
		dynamic_array_push_back(&pattern_variables, variable);
		return;
	}

	// Check if we should stop the recursion
	switch (expression->type)
	{
	case AST::Expression_Type::BAKE:
	case AST::Expression_Type::FUNCTION:
	case AST::Expression_Type::MODULE:
		return;
	}

	// Otherwise search all children of the node for further polymorphic_function parameters
	int child_index = 0;
	auto child_node = AST::base_get_child(upcast(expression), child_index);
	while (child_node != 0) {
		if (child_node->type == AST::Node_Type::EXPRESSION) {
			expression_search_for_symbol_lookups_and_pattern_variables(
				parameter_index, downcast<AST::Expression>(child_node), symbol_lookups, pattern_variables);
		}
		else if (child_node->type == AST::Node_Type::CALL_NODE) {
			auto args = downcast<AST::Call_Node>(child_node);
			for (int i = 0; i < args->arguments.size; i++) {
				expression_search_for_symbol_lookups_and_pattern_variables(
					parameter_index, args->arguments[i]->value, symbol_lookups, pattern_variables);
			}
		}
		else if (child_node->type == AST::Node_Type::ARGUMENT) {
			auto argument_node = downcast<AST::Argument>(child_node);
			expression_search_for_symbol_lookups_and_pattern_variables(
				parameter_index, argument_node->value, symbol_lookups, pattern_variables);
		}
		else if (child_node->type == AST::Node_Type::PARAMETER) {
			auto parameter_node = downcast<AST::Parameter>(child_node);
			if (!parameter_node->type.available) continue;
			expression_search_for_symbol_lookups_and_pattern_variables(
				parameter_index, parameter_node->type.value, symbol_lookups, pattern_variables);
		}
		child_index += 1;
		child_node = AST::base_get_child(upcast(expression), child_index);
	}
}

// Note: parameter types may be polymorphic when calling this function
void analyse_parameter_type_and_value(Call_Parameter& parameter, AST::Parameter* parameter_node, bool allow_pattern_type, Semantic_Context* semantic_context)
{
	auto workload = semantic_context->current_workload;
	auto& types = semantic_context->compilation_data->type_system->predefined_types;
	parameter.required = true;
	if (parameter.must_not_be_set) {
		parameter.required = false;
	}

	// Analyse type
	parameter.name = parameter_node->name;
	if (parameter_node->type.available) {
		parameter.datatype = semantic_analyser_analyse_expression_type(parameter_node->type.value, semantic_context);
	}
	else
	{
		if (!parameter_node->type.available && !parameter_node->is_comptime) {
			log_semantic_error(
				semantic_context, "Parameter type must be specified if it isn't a comptime parameter!", 
				upcast(parameter_node), Node_Section::IDENTIFIER
			);
		}
		parameter.datatype = types.type_handle;
	}

	// Check if default value exists
	parameter.default_value_exists = parameter_node->default_value.available;
	if (!parameter.default_value_exists) {
		parameter.default_value_expr = nullptr;
		parameter.default_value_pass = nullptr;
		return;
	}
	else if (parameter.datatype->contains_pattern) {
		log_semantic_error(semantic_context, "Parameters with Poly-Patterns cannot have default values currently", upcast(parameter_node->default_value.value));
		parameter.default_value_exists = false;
		return;
	}
	else if (parameter_node->is_comptime) {
		log_semantic_error(semantic_context, "Comptime parameters cannot have default values", upcast(parameter_node->default_value.value));
		parameter.default_value_exists = false;
		return;
	}

	// Analyse default value (With global symbol access)
	parameter.required = false;
	parameter.default_value_expr = parameter_node->default_value.value;
	parameter.default_value_pass = semantic_context->current_pass;
	RESTORE_ON_SCOPE_EXIT(semantic_context->symbol_access_level, Symbol_Access_Level::GLOBAL);
	semantic_analyser_analyse_expression_value(
		parameter_node->default_value.value, expression_context_make_specific_type(parameter.datatype), semantic_context
	);
}

// Defines all necessary symbols in symbol-table, and looks for polymorphism
Poly_Header* poly_header_analyse(
	Dynamic_Array<AST::Parameter*> parameter_nodes, Symbol_Table* parameter_table, String* name, Semantic_Context* semantic_context,
	Function_Progress* progress = 0, Workload_Structure_Polymorphic* poly_struct = 0)
{
	auto type_system = semantic_context->compilation_data->type_system;
	auto& types = type_system->predefined_types;
	auto compilation_data = semantic_context->compilation_data;

	int return_type_index = -1;
	if (parameter_nodes.size > 0 && parameter_nodes[parameter_nodes.size - 1]->is_return_type) {
		return_type_index = parameter_nodes.size - 1;
	}

	// Poly_Header structure is always generated
	Poly_Header* result_header = new Poly_Header;
	Poly_Header& header = *result_header;
	header.name = name;
	header.parameter_nodes = parameter_nodes;
	header.parameter_table = parameter_table;
	header.is_function = progress != 0;
	header.partial_pattern_count = 0;

	assert(name != nullptr, "Name should be available for polymorhphic functions/structs");
	if (poly_struct != nullptr) {
		header.is_function = false;
		header.origin.struct_workload = poly_struct;
	}
	else if (progress != nullptr) {
		header.is_function = true;
		header.origin.function_progress = progress;
		header.origin.function_progress->header_workload->poly_header = result_header;
	}
	else {
		panic("Poly-Header must be either from function or from struct");
	}

	header.instances = hashset_create_empty<Poly_Instance*>(0, hash_poly_instance, equals_poly_instance);
	header.pattern_variables = dynamic_array_create<Pattern_Variable>();
	header.base_analysis_states = array_create_static<Pattern_Variable_State>(nullptr, 0);
	header.signature = call_signature_create_empty();
	header.signature->return_type_index = return_type_index;

	// Define parameter symbols and search for symbol-lookups and pattern values
	Dynamic_Array<Parameter_Symbol_Lookup> symbol_lookups = dynamic_array_create<Parameter_Symbol_Lookup>();
	SCOPE_EXIT(dynamic_array_destroy(&symbol_lookups));
	int comptime_param_count = 0;
	for (int i = 0; i < parameter_nodes.size; i++)
	{
		auto parameter_node = parameter_nodes[i];
		Call_Parameter* parameter_info = call_signature_add_parameter(
			header.signature, parameter_node->name, nullptr, false, false, i == return_type_index);

		// Define symbol (Note: return_type can still be defined as symbol, because id is unique, e.g. "!return_type")
		bool is_comptime = parameter_node->is_comptime || progress == nullptr; // In poly-struct all parameters are comptime
		Symbol* symbol = symbol_table_define_symbol(
			parameter_table, parameter_node->name, (is_comptime ? Symbol_Type::PATTERN_VARIABLE : Symbol_Type::PARAMETER), AST::upcast(parameter_node),
			(is_comptime ? Symbol_Access_Level::POLYMORPHIC : Symbol_Access_Level::INTERNAL), semantic_context->compilation_data
		);
		get_info(parameter_node, semantic_context, true)->symbol = symbol;

		if (progress == nullptr && !parameter_node->is_comptime) {
			log_semantic_error(semantic_context, "Parameters of struct header must be comptime, e.g. use $", upcast(parameter_node), Node_Section::IDENTIFIER);
		}

		// Set symbol/param infos
		if (is_comptime)
		{
			parameter_info->comptime_variable_index = header.pattern_variables.size;
			comptime_param_count += 1;

			Pattern_Variable variable;
			variable.defined_in_parameter_index = i;
			variable.definition_node = upcast(parameter_node);
			variable.is_comptime_parameter = true;
			variable.origin = &header;
			variable.pattern_variable_type = nullptr; // Filled out later
			variable.symbol = symbol;
			variable.name = parameter_node->name;
			variable.value_access_index = header.pattern_variables.size;
			dynamic_array_push_back(&header.pattern_variables, variable);
		}
		else
		{
			parameter_info->comptime_variable_index = -1;

			assert(progress != nullptr, "");
			symbol->options.parameter.function = progress;
			symbol->options.parameter.index_in_polymorphic_signature = i;
			symbol->options.parameter.index_in_non_polymorphic_signature = i - comptime_param_count;
			assert(symbol->options.parameter.index_in_non_polymorphic_signature >= 0, "");
		}

		// Find implicit parameters/lookups
		int before_count = header.pattern_variables.size;
		if (parameter_node->type.available) {
			expression_search_for_symbol_lookups_and_pattern_variables(i, parameter_node->type.value, symbol_lookups, header.pattern_variables);
		}

		// Add self-dependencies
		for (int i = before_count; i < header.pattern_variables.size; i++) {
			dynamic_array_push_back(&parameter_info->dependencies, i);
		}
	}

	// Initialize Pattern-Variables (Create symbols + Corresponding Datatype)
	for (int i = 0; i < header.pattern_variables.size; i++)
	{
		auto& pattern_variable = header.pattern_variables[i];
		pattern_variable.origin = &header;
		pattern_variable.value_access_index = i;

		// Update misc data
		if (!pattern_variable.is_comptime_parameter)
		{
			header.signature->parameters[pattern_variable.defined_in_parameter_index].contains_pattern_variable_definition = true;
			hashtable_insert_element(
				&compilation_data->pattern_variable_expression_mapping,
				downcast<AST::Expression>(pattern_variable.definition_node),
				&pattern_variable
			);
		}

		// Finish symbol
		if (pattern_variable.symbol == nullptr) {
			pattern_variable.symbol = symbol_table_define_symbol(
				parameter_table, pattern_variable.name, Symbol_Type::PATTERN_VARIABLE,
				pattern_variable.definition_node, Symbol_Access_Level::POLYMORPHIC, compilation_data
			);
		}
		pattern_variable.symbol->options.pattern_variable = &pattern_variable;

		// Create type
		pattern_variable.pattern_variable_type = type_system_make_pattern_variable_type(type_system, &pattern_variable);

		// Add implicit parameters
		auto param = call_signature_add_parameter(
			header.signature, pattern_variable.name, upcast(pattern_variable.pattern_variable_type), false, true, false);
		param->comptime_variable_index = i;
	}

	// Create pattern values for base analysis
	header.base_analysis_states = array_create<Pattern_Variable_State>(header.pattern_variables.size);
	for (int i = 0; i < header.base_analysis_states.size; i++) {
		header.base_analysis_states[i] = pattern_variable_state_make_pattern(upcast(header.pattern_variables[i].pattern_variable_type));
	}

	// Find dependencies between parameters using the found symbol-lookups
	Symbol_Query_Info symbol_query_info = symbol_query_info_make(Symbol_Access_Level::POLYMORPHIC, Import_Type::NONE, false);
	for (int i = 0; i < symbol_lookups.size; i++)
	{
		auto& lookup = symbol_lookups[i];

		Arena* scratch_arena = semantic_context->scratch_arena;
		Arena_Checkpoint checkpoint = scratch_arena->make_checkpoint();
		SCOPE_EXIT(scratch_arena->rewind_to_checkpoint(checkpoint));
		DynArray<Symbol*> symbols = symbol_table_query_id(
			parameter_table, lookup.lookup_id, symbol_query_info, scratch_arena
		);
		if (symbols.size == 0) {
			continue;
		}

		assert(symbols.size == 1, "> 2 symbols in parameter table shouldn't be possible (No overloading for parameters, see define symbol)");
		assert(symbols[0]->type == Symbol_Type::PATTERN_VARIABLE, "Symbol lookup is only in parameter table, so we shouln't have other values!");
		auto depends_on_var = symbols[0]->options.pattern_variable;
		if (depends_on_var->is_comptime_parameter && depends_on_var->defined_in_parameter_index == lookup.parameter_index) {
			log_semantic_error(semantic_context, "Comptime parameter type cannot depend on value of itself", upcast(lookup.node));
		}
		dynamic_array_push_back(
			&header.signature->parameters[lookup.parameter_index].dependencies,
			symbols[0]->options.pattern_variable->value_access_index
		);
	}

	// Analyse all parameter types
	auto workload = semantic_context->current_workload;
	assert(
		workload->active_pattern_variable_states.data == nullptr,
		"Function-Header/Poly-Struct-Header should not have values set, only bakes/lambdas/anonymous structs"
	);
	semantic_context->current_symbol_table = parameter_table;
	semantic_context->symbol_access_level = Symbol_Access_Level::POLYMORPHIC;
	workload->active_pattern_variable_states = header.base_analysis_states;
	workload->active_pattern_variable_states_origin = result_header;
	for (int i = 0; i < parameter_nodes.size; i++) {
		auto& param = header.signature->parameters[i];
		analyse_parameter_type_and_value(param, parameter_nodes[i], true, semantic_context);
		if (param.datatype->contains_partial_pattern) {
			param.partial_pattern_index = header.partial_pattern_count;
			header.partial_pattern_count += 1;
		}
	}

	header.signature = call_signature_register(header.signature, compilation_data);

	// Set all parameter-states to unset, as base-analysis shouldn't be able to use them
	for (int i = 0; i < header.pattern_variables.size; i++) {
		header.base_analysis_states[i] = pattern_variable_state_make_unset();
	}

	// Reset poly-values if caller does other work
	workload->active_pattern_variable_states = array_create_static<Pattern_Variable_State>(nullptr, 0);
	workload->active_pattern_variable_states_origin = nullptr;

	return result_header;
}

void poly_header_destroy(Poly_Header* header)
{
	array_destroy(&header->base_analysis_states);
	dynamic_array_destroy(&header->pattern_variables);
	for (auto iter = header->instances.make_iter(); iter.has_next(); iter.next())
	{
		Poly_Instance* instance = *iter.value;
		array_destroy(&instance->variable_states);
		array_destroy(&instance->partial_pattern_instances);
		delete instance;
	}
	hashset_destroy(&header->instances);
	delete header;
}

// Checks if any of the poly_parent workloads of this workload has the corresponding active_pattner_values set to active
Workload_Base* pattern_variable_find_instance_workload(
	Pattern_Variable* variable,
	Workload_Base* search_start_workload)
{
	auto workload = search_start_workload;
	int counter = 0;
	while (workload != nullptr)
	{
		if (workload->active_pattern_variable_states_origin == variable->origin) {
			return workload;
		}

		workload = workload->poly_parent_workload;
		assert(workload == nullptr || workload != workload->poly_parent_workload, "Should be terminated with nullptr");
		counter += 1;
		assert(counter < 1000, "I don't think we should hit this limit, this seems like an endless loop");
	}

	return nullptr;
}

void expression_info_set_to_pattern_variable(
	Expression_Info* result_info, Pattern_Variable* variable, bool is_symbol_lookup, Semantic_Context* semantic_context)
{
	auto type_system = semantic_context->compilation_data->type_system;
	auto& types = type_system->predefined_types;

	auto instance_workload = pattern_variable_find_instance_workload(variable, semantic_context->current_workload);
	Array<Pattern_Variable_State> active_pattern_values = instance_workload->active_pattern_variable_states;
	assert(
		active_pattern_values.data != 0,
		"If the symbol was found (e.g. correct symbol-table lookup 'permissions'), then we should be able to access it"
	);

	auto& value = active_pattern_values[variable->value_access_index];
	switch (value.type)
	{
	case Pattern_Variable_State_Type::SET: {
		expression_info_set_constant(result_info, value.options.value);
		break;
	}
	case Pattern_Variable_State_Type::UNSET:
	{
		semantic_analyser_set_error_flag(true, semantic_context);
		// Assert that we are in a function-body (BASE) analysis, otherwise panic
		auto workload = semantic_context->current_workload;
		assert(
			!((workload->type == Analysis_Workload_Type::FUNCTION_HEADER || workload->type == Analysis_Workload_Type::STRUCT_POLYMORPHIC) &&
				instance_workload == workload),
			"Header analysis must not encounter unset values"
		);
		expression_info_set_value(result_info, types.unknown_type, false);
		break;
	}
	case Pattern_Variable_State_Type::PATTERN:
	{
		Datatype* pattern = value.options.pattern_type;
		assert(pattern->contains_pattern, "");
		expression_info_set_polymorphic_pattern(result_info, value.options.pattern_type, type_system);
		if (pattern->type == Datatype_Type::PATTERN_VARIABLE)
		{
			auto var_type = downcast<Datatype_Pattern_Variable>(pattern);
			if (is_symbol_lookup && !var_type->is_reference) {
				expression_info_set_polymorphic_pattern(result_info, upcast(var_type->mirrored_type), type_system);
			}
		}
		break;
	}
	default: panic("");
	}
}



// POLYMORPHIC INSTANCIATION
struct Variable_Dependents
{
	DynArray<int> dependents;
};

struct Parameter_Run_Info
{
	int dependency_count;          // Count of variable-dependencies
	int dependency_on_self_definition_count; // How many variables are defined in this parameter (e.g. a: Node($C, $T) --> 2)
	int index_in_waiting_array; // tracks position in to_run array, if -1 it's already queue (Or doesn't need analysis)
	bool has_dependency_on_pattern_state;
};

struct Parameter_Dependency_Graph
{
	Array<Variable_Dependents> variable_dependents;
	Array<Parameter_Run_Info> run_infos;
	DynArray<int> waiting_indices; // Note: does not contain parameters that were not set
	DynArray<int> runnable_indices;

	// Helper data (references?)
	Array<Pattern_Variable_State> variable_states;
	DynArray<Matching_Constraint>* matching_constraints;
};

Parameter_Dependency_Graph parameter_dependency_graph_create_empty(
	Poly_Header* poly_header, Arena* arena, Array<Pattern_Variable_State> variable_states, DynArray<Matching_Constraint>* matching_constraints)
{
	Parameter_Dependency_Graph result;
	result.waiting_indices = DynArray<int>::create(arena, poly_header->signature->parameters.size);
	result.runnable_indices = DynArray<int>::create(arena, poly_header->signature->parameters.size);
	result.variable_dependents = arena->allocate_array<Variable_Dependents>(poly_header->pattern_variables.size);
	for (int i = 0; i < result.variable_dependents.size; i++) {
		result.variable_dependents[i].dependents = DynArray<int>::create(arena);
	}
	result.run_infos = arena->allocate_array<Parameter_Run_Info>(poly_header->signature->parameters.size);
	result.variable_states = variable_states;
	result.matching_constraints = matching_constraints;
	return result;
}

void parameter_dependency_graph_set_parameter_runnable(Parameter_Dependency_Graph* graph, int param_index, bool check_dependency_count)
{
	auto& run_info = graph->run_infos[param_index];
	if (run_info.index_in_waiting_array == -1) return; // Is already queued

	if (check_dependency_count && run_info.dependency_count - run_info.dependency_on_self_definition_count > 0) return;

	graph->runnable_indices.push_back(param_index);
	graph->waiting_indices.swap_remove(run_info.index_in_waiting_array);
	// Update info of item that was swapped
	if (run_info.index_in_waiting_array < graph->waiting_indices.size) {
		graph->run_infos[graph->waiting_indices[run_info.index_in_waiting_array]].index_in_waiting_array = run_info.index_in_waiting_array;
	}
	run_info.index_in_waiting_array = -1;
}

void parameter_dependency_graph_set_variable_to_pattern(
	Parameter_Dependency_Graph* graph, Pattern_Variable* variable, Datatype* pattern_type)
{
	assert(pattern_type->contains_pattern, "");

	auto& variable_states = graph->variable_states;
	auto& state = variable_states[variable->value_access_index];
	assert(state.type == Pattern_Variable_State_Type::UNSET, "Can other stuff happen here?");
	state = pattern_variable_state_make_pattern(pattern_type);
	auto dependents = graph->variable_dependents[variable->value_access_index].dependents;
	for (int i = 0; i < dependents.size; i++)
	{
		auto param_index = dependents[i];
		auto& run_info = graph->run_infos[param_index];
		run_info.dependency_count -= 1;
		run_info.has_dependency_on_pattern_state = true;
		parameter_dependency_graph_set_parameter_runnable(graph, param_index, true);
	}
}

void parameter_dependency_graph_set_variable_value(
	Parameter_Dependency_Graph* graph, Pattern_Variable* variable, Upp_Constant value, Compilation_Data* compilation_data)
{
	auto type_system = compilation_data->type_system;
	auto& types = type_system->predefined_types;
	auto& variable_states = graph->variable_states;
	
	// Check for edge case where we set a pattern-type (Not sure if this ever happens)
	if (value.type == types.type_handle)
	{
		Datatype* datatype = upp_constant_as_datatype(value, type_system);
		if (datatype->contains_pattern) {
			parameter_dependency_graph_set_variable_to_pattern(graph, variable, datatype);
			return;
		}
	}

	auto& state = variable_states[variable->value_access_index];
	switch (state.type)
	{
	case Pattern_Variable_State_Type::SET: { // Already set (Happens when set explicitly)
		Matching_Constraint constraint;
		constraint.pattern_variable = variable->pattern_variable_type;
		constraint.value = value;
		graph->matching_constraints->push_back(constraint);
		break;
	}
	case Pattern_Variable_State_Type::UNSET:
	{
		state = pattern_variable_state_make_set(value);
		auto dependents = graph->variable_dependents[variable->value_access_index].dependents;
		for (int i = 0; i < dependents.size; i++)
		{
			auto param_index = dependents[i];
			auto& run_info = graph->run_infos[param_index];
			run_info.dependency_count -= 1;
			parameter_dependency_graph_set_parameter_runnable(graph, param_index, true);
		}
		break;
	}
	case Pattern_Variable_State_Type::PATTERN:
	{
		// Not sure this can even happen currently because we don't match types if we have struct-pattern
		panic("Just have a panic to see if this happens");
		break;
	}
	default: panic("");
	}
}

Poly_Instance* poly_header_instanciate_with_variable_states(
	Poly_Header* poly_header, Array<Pattern_Variable_State> states,
	Array<Datatype*> partial_pattern_instances, Array<Datatype*> parameter_types_instanciated,
	Call_Info* call_info, AST::Node* error_report_node, Node_Section error_report_section, Semantic_Context* semantic_context)
{
	auto type_system = semantic_context->compilation_data->type_system;

	bool create_struct_pattern = false;
	bool is_partial_struct_pattern = false;
	bool struct_pattern_contains_pattern_variable_definition = false;
	for (int i = 0; i < states.size; i++)
	{
		auto& state_type = states[i].type;
		if (state_type == Pattern_Variable_State_Type::UNSET) {
			create_struct_pattern = true;
			is_partial_struct_pattern = true;
		}
		else if (state_type == Pattern_Variable_State_Type::PATTERN) {
			create_struct_pattern = true;
			if (states[i].options.pattern_type->contains_pattern_variable_definition) {
				struct_pattern_contains_pattern_variable_definition = true;
			}
		}
	}

	bool all_partial_patterns_set = true;
	for (int i = 0; i < partial_pattern_instances.size; i++) {
		if (partial_pattern_instances[i] == nullptr) {
			all_partial_patterns_set = false;
		}
	}

	// Check if instanciation failed
	// Note entirely sure about these checks yet...
	if (poly_header->is_function && create_struct_pattern) {
		log_semantic_error(semantic_context, "Not all polymorphic parameters could be deduced in instanciation", error_report_node, error_report_section);
		return nullptr;
	}
	if (!create_struct_pattern && !all_partial_patterns_set) {
		log_semantic_error(semantic_context, "Not all partial-pattern types could be deduced in instanciation", error_report_node, error_report_section);
		return nullptr;
	}

	// Deduplicate instance if it isn't a struct pattern
	if (create_struct_pattern)
	{
		// Because struct-patterns aren't deduplicated, we don't need to store the partial patterns
		array_destroy(&partial_pattern_instances);
		partial_pattern_instances = array_create_static<Datatype*>(nullptr, 0);
	}
	else
	{
		// Deduplicate instance
		Poly_Instance query_instance;
		query_instance.type = Poly_Instance_Type::FUNCTION; // Note: This is not used in poly_function_equals, we just need it not to be struct-pattern
		query_instance.variable_states = states;
		query_instance.header = poly_header;
		query_instance.partial_pattern_instances = partial_pattern_instances;

		// Note: Hashing instances only requires variable-states, header and partial-patterns, so we don't need to init the other values
		Poly_Instance** found = hashset_find(&poly_header->instances, &query_instance);
		if (found != nullptr)
		{
			array_destroy(&states);
			array_destroy(&partial_pattern_instances);
			if (call_info != nullptr)
			{
				call_info->instanciated = true;
				Poly_Instance* instance = *found;
				switch (instance->type)
				{
				case Poly_Instance_Type::FUNCTION: {
					call_info->instanciation_data.function = instance->options.function_instance->function;
					assert(call_info->origin.type == Call_Origin_Type::POLY_FUNCTION, "");
					break;
				}
				case Poly_Instance_Type::STRUCTURE: {
					call_info->instanciation_data.struct_instance = instance->options.struct_instance->struct_type;
					assert(call_info->origin.type == Call_Origin_Type::POLY_STRUCT, "");
					break;
				}
				case Poly_Instance_Type::STRUCT_PATTERN: {
					panic("Struct pattern's don't get deduplicated, how did I end up here?");
					break;
				}
				default: panic("");
				}
			}
			return *found;
		}
	}

	// Otherwise create new instances
	Poly_Instance* instance = new Poly_Instance;
	instance->header = poly_header;
	instance->variable_states = states;
	instance->partial_pattern_instances = partial_pattern_instances;

	if (create_struct_pattern)
	{
		assert(!poly_header->is_function, "hey");
		instance->type = Poly_Instance_Type::STRUCT_PATTERN;
		instance->options.struct_pattern = type_system_make_struct_pattern(
			type_system, instance, is_partial_struct_pattern, struct_pattern_contains_pattern_variable_definition
		);
		if (call_info != nullptr) {
			call_info->instanciation_data.struct_pattern = instance->options.struct_pattern;
			call_info->instanciated = true;
		}
	}
	else if (poly_header->is_function)
	{
		assert(call_info != 0, "");
		int return_type_index = poly_header->signature->return_type_index;

		// Create instance function signature
		Call_Signature* instance_signature = call_signature_create_empty();
		auto& base_parameters = poly_header->signature->parameters;
		for (int i = 0; i < base_parameters.size; i++)
		{
			auto& param = base_parameters[i];
			if (param.comptime_variable_index != -1) continue; // comptime parameter or pattern-variable

			Datatype* param_type = param.datatype;
			if (param_type->contains_pattern) {
				param_type = parameter_types_instanciated[i];
				assert(param_type != nullptr && !param_type->contains_pattern, "must be true");
			}

			call_signature_add_parameter(
				instance_signature, param.name, param_type,
				param.required, false, i == poly_header->signature->return_type_index
			);
			if (i == poly_header->signature->return_type_index) {
				instance_signature->return_type_index = instance_signature->parameters.size - 1;
			}
		}
		instance_signature = call_signature_register(instance_signature, semantic_context->compilation_data);

		auto base_function_progress = poly_header->origin.function_progress;

		// Create new instance progress
		auto instance_progress = function_progress_create_with_modtree_function(
			nullptr, base_function_progress->function_node, instance_signature, semantic_context, base_function_progress
		);
		instance_progress->type = Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
		instance_progress->poly_function = base_function_progress->poly_function;
		instance_progress->parameter_table = poly_header->parameter_table;

		// Update body workload to use instance values
		instance_progress->body_workload->base.polymorphic_instanciation_depth += 1;
		instance_progress->body_workload->base.active_pattern_variable_states = instance->variable_states;
		instance_progress->body_workload->base.active_pattern_variable_states_origin = poly_header;

		instance->type = Poly_Instance_Type::FUNCTION;
		instance->options.function_instance = instance_progress;
		semantic_analyser_register_function_call(instance_progress->function, semantic_context);

		call_info->instanciation_data.function = instance_progress->function;
		call_info->instanciated = true;
	}
	else // !poly_header.is_function
	{
		auto base_struct_workload = poly_header->origin.struct_workload;

		// Create new struct instance
		auto body_workload = workload_structure_create(
			base_struct_workload->body_workload->struct_node, nullptr, true, semantic_context, Symbol_Access_Level::POLYMORPHIC
		);
		body_workload->struct_type->name = base_struct_workload->body_workload->struct_type->name;
		body_workload->polymorphic_type = Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
		body_workload->polymorphic.instance.poly_instance = instance;
		body_workload->polymorphic.instance.parent = base_struct_workload;
		body_workload->symbol_table = poly_header->parameter_table;

		body_workload->base.polymorphic_instanciation_depth += 1;
		body_workload->base.active_pattern_variable_states = instance->variable_states;
		body_workload->base.active_pattern_variable_states_origin = poly_header;

		analysis_workload_add_dependency_internal(
			semantic_context->compilation_data->workload_executer, upcast(body_workload), upcast(base_struct_workload->body_workload)
		);

		instance->type = Poly_Instance_Type::STRUCTURE;
		instance->options.struct_instance = body_workload;

		if (call_info != nullptr) {
			call_info->instanciation_data.struct_instance = body_workload->struct_type;
			call_info->instanciated = true;
		}
	}

	bool success = hashset_insert_element(&poly_header->instances, instance);
	assert(success, "Otherwise dedup would have happened");
	return instance;
}

// This function tries to create a non-pattern type from pattern-type with the given pattern variables,
//	and returns nullptr if not successfull (Variable-states not set yet, index-variable invalid or pattern not instanciatable)
Datatype* datatype_pattern_instanciate(
	Datatype* pattern_type, Array<Pattern_Variable_State> states, 
	AST::Node* error_report_node, Node_Section error_report_section, Semantic_Context* semantic_context)
{
	auto& type_system = semantic_context->compilation_data->type_system;
	auto& types = type_system->predefined_types;

	if (!pattern_type->contains_pattern) {
		return pattern_type;
	}
	if (pattern_type->contains_partial_pattern) {
		log_semantic_error(semantic_context, "Datatype instanciate failed: Can't instanciate partial pattern", error_report_node, error_report_section);
		return nullptr;
	}

	switch (pattern_type->type)
	{
	case Datatype_Type::PATTERN_VARIABLE:
	{
		auto variable = downcast<Datatype_Pattern_Variable>(pattern_type)->variable;
		auto& state = states[variable->value_access_index];
		if (state.type != Pattern_Variable_State_Type::SET) {
			log_semantic_error(semantic_context, "Datatype instanciate failed: Pattern variable is not set", error_report_node, error_report_section);
			log_error_info_id(semantic_context, variable->name);
			return nullptr;
		}

		auto constant = state.options.value;
		auto constant_type = constant.type;
		if (!types_are_equal(constant_type, types.type_handle)) {
			log_semantic_error(semantic_context, "Datatype instanciate failed: Pattern variable is not a type handle", error_report_node, error_report_section);
			log_error_info_id(semantic_context, variable->name);
			return nullptr;
		}
		return upp_constant_as_datatype(constant, type_system);
	}
	case Datatype_Type::STRUCT_PATTERN:
	{
		auto& scratch_arena = *semantic_context->scratch_arena;
		auto checkpoint = scratch_arena.make_checkpoint();
		SCOPE_EXIT(scratch_arena.rewind_to_checkpoint(checkpoint));

		Datatype_Struct_Pattern* struct_pattern = downcast<Datatype_Struct_Pattern>(pattern_type);
		Poly_Header* poly_header = struct_pattern->instance->header;
		assert(poly_header->signature->return_type_index == -1, "");

		// Create new callable call
		Call_Info call_info = call_info_make_with_empty_arguments(call_origin_make(poly_header), &scratch_arena);

		auto& param_values = call_info.parameter_values;
		for (int i = 0; i < param_values.size; i++)
		{
			auto& param_info = poly_header->signature->parameters[i];
			auto& param_value = param_values[i];
			param_value.value_type = Parameter_Value_Type::NOT_SET;

			assert(param_info.comptime_variable_index != -1, "In poly-struct all parameters have comptime variables");
			if (param_info.requires_named_addressing) { // Don't set parameters indirectly
				continue;
			}

			auto variable = &struct_pattern->instance->header->pattern_variables[param_info.comptime_variable_index];
			auto& var_state = struct_pattern->instance->variable_states[param_info.comptime_variable_index];
			switch (var_state.type)
			{
			case Pattern_Variable_State_Type::SET: {
				param_value = parameter_value_make_comptime(var_state.options.value);
				continue; // Already set, so we don't have anything to do
			}
			case Pattern_Variable_State_Type::UNSET: {
				log_semantic_error(semantic_context, "Datatype instanciate failed: Pattern variable is unset", error_report_node, error_report_section);
				log_error_info_id(semantic_context, variable->name);
				return nullptr; // Return null, as this function only works if all states are set...
			}
			case Pattern_Variable_State_Type::PATTERN: break; // Rest of block
			default: panic("");
			}

			auto pattern_type = var_state.options.pattern_type;
			if (pattern_type->type == Datatype_Type::PATTERN_VARIABLE)
			{
				auto variable = downcast<Datatype_Pattern_Variable>(pattern_type)->variable;
				auto& var_state = states[variable->value_access_index];
				if (var_state.type != Pattern_Variable_State_Type::SET) {
					log_semantic_error(semantic_context, "Datatype instanciate failed: Pattern variable is unset", error_report_node, error_report_section);
					log_error_info_id(semantic_context, variable->name);
					return nullptr;
				}
				param_value = parameter_value_make_comptime(var_state.options.value);
			}
			else
			{
				Datatype* instanciated_pattern_type = datatype_pattern_instanciate(
					pattern_type, states, error_report_node, error_report_section, semantic_context
				);
				if (instanciated_pattern_type == nullptr) return nullptr;
				assert(!instanciated_pattern_type->contains_pattern, "");

				auto result = constant_pool_add_constant(
					semantic_context->compilation_data->constant_pool,
					types.type_handle,
					array_create_static_as_bytes(&instanciated_pattern_type->type_handle, 1)
				);
				assert(result.success, "Type-handle should always work!");
				param_value = parameter_value_make_comptime(result.options.constant);
			}
		}

		auto struct_instance = poly_header_instanciate(&call_info, error_report_node, error_report_section, semantic_context);
		if (struct_instance == nullptr) {
			log_semantic_error(semantic_context, "Instanciating struct template failed", error_report_node, error_report_section);
			return nullptr; // Errors are reported at this point
		}
		assert(struct_instance->type == Poly_Instance_Type::STRUCTURE, "Should be the case, as all parameters are set?");
		return upcast(struct_instance->options.struct_instance->struct_type);
	}
	case Datatype_Type::FUNCTION_POINTER:
	{
		auto function_pointer = downcast<Datatype_Function_Pointer>(pattern_type);
		auto signature = function_pointer->signature;
		// Note: create function takes ownership, so only delete this if we have instanciation error
		Call_Signature* new_signature = call_signature_create_empty();
		new_signature->return_type_index = signature->return_type_index;
		for (int i = 0; i < signature->parameters.size; i++)
		{
			auto& param = signature->parameters[i];
			Datatype* param_instance_type = datatype_pattern_instanciate(
				param.datatype, states, error_report_node, error_report_section, semantic_context
			);
			if (param_instance_type == nullptr) {
				call_signature_destroy(new_signature);
				return nullptr;
			}
			call_signature_add_parameter(
				new_signature, param.name, param_instance_type, param.required, param.requires_named_addressing, param.must_not_be_set
			);
		}
		new_signature = call_signature_register(new_signature, semantic_context->compilation_data);
		auto result_type = type_system_make_function_pointer(type_system, new_signature);
		return upcast(result_type);
	}
	case Datatype_Type::ARRAY:
	{
		auto array = downcast<Datatype_Array>(pattern_type);

		// Instanciate element type
		Datatype* element_type = datatype_pattern_instanciate(array->element_type, states, error_report_node, error_report_section, semantic_context);
		if (element_type == nullptr) return nullptr;

		// Handle polymorphic count variable
		usize element_count = array->element_count;
		bool count_known = array->count_known;
		if (array->count_variable_type != nullptr)
		{
			auto& state = states[array->count_variable_type->variable->value_access_index];
			if (state.type != Pattern_Variable_State_Type::SET) {
				log_semantic_error(
					semantic_context, "Datatype instanciate failed: Array count pattern variable is unset", error_report_node, error_report_section
				);
				log_error_info_id(semantic_context, array->count_variable_type->variable->name);
				return nullptr;
			}
			auto constant = state.options.value;
			auto constant_type = constant.type;
			if (constant_type->type != Datatype_Type::PRIMITIVE) {
				log_semantic_error(
					semantic_context, "Datatype instanciate failed: Array count pattern variable is not an integer", error_report_node, error_report_section
				);
				log_error_info_id(semantic_context, array->count_variable_type->variable->name);
				return nullptr;
			}
			auto primitive = downcast<Datatype_Primitive>(constant_type);
			if (primitive->primitive_class != Primitive_Class::INTEGER) {
				log_semantic_error(
					semantic_context, "Datatype instanciate failed: Array count pattern variable is not an integer", error_report_node, error_report_section
				);
				log_error_info_id(semantic_context, array->count_variable_type->variable->name);
				return nullptr;
			}

			// Different integer sizes
			bool less_equal_zero = false;
			if (primitive->is_signed)
			{
				if (constant_type->memory_info.value.size == 1) {
					i8 value = *((i8*)constant.memory);
					less_equal_zero = value <= 0;
					element_count = (usize)value;
				}
				else if (constant_type->memory_info.value.size == 2) {
					i16 value = *((i16*)constant.memory);
					less_equal_zero = value <= 0;
					element_count = (usize)value;
				}
				else if (constant_type->memory_info.value.size == 4) {
					i32 value = *((i32*)constant.memory);
					less_equal_zero = value <= 0;
					element_count = (usize)value;
				}
				else if (constant_type->memory_info.value.size == 8) {
					i64 value = *((i64*)constant.memory);
					less_equal_zero = value <= 0;
					element_count = (usize)value;
				}
				else {
					panic("");
				}
			}
			else
			{
				if (constant_type->memory_info.value.size == 1) {
					element_count = (usize)(*((u8*)constant.memory));
				}
				else if (constant_type->memory_info.value.size == 2) {
					element_count = (usize)(*((u16*)constant.memory));
				}
				else if (constant_type->memory_info.value.size == 4) {
					element_count = (usize)(*((u32*)constant.memory));
				}
				else if (constant_type->memory_info.value.size == 8) {
					element_count = (usize)(*((u64*)constant.memory));
				}
				else {
					panic("");
				}
			}

			if (element_count == 0 || less_equal_zero) {
				log_semantic_error(
					semantic_context, "Datatype instanciate failed: Array count pattern variable integer is <= 0", error_report_node, error_report_section
				);
				log_error_info_id(semantic_context, array->count_variable_type->variable->name);
				return nullptr;
			}
			count_known = true;
		}

		return type_system_make_array(type_system, element_type, count_known, (int)element_count, nullptr);
	}
	case Datatype_Type::SLICE: {
		Datatype* child_type = datatype_pattern_instanciate(
			downcast<Datatype_Slice>(pattern_type)->element_type, states, error_report_node, error_report_section, semantic_context
		);
		if (child_type == nullptr) return nullptr;
		return upcast(type_system_make_slice(type_system, child_type));
	}
	case Datatype_Type::POINTER: {
		auto pointer = downcast<Datatype_Pointer>(pattern_type);
		Datatype* child_type = datatype_pattern_instanciate(
			pointer->element_type, states, error_report_node, error_report_section, semantic_context
		);
		if (child_type == nullptr) return nullptr;
		return upcast(type_system_make_pointer(type_system, child_type, pointer->is_optional));
	}
	}

	panic("Invalid code path, Datatype shouldn't contain pattern otherwise!");
	return nullptr;
}

// Return null if not successfull
// Analyses all set parameters if successfull
Poly_Instance* poly_header_instanciate(
	Call_Info* call_info, AST::Node* error_report_node, Node_Section error_report_section, Semantic_Context* semantic_context)
{
	Poly_Header* poly_header;
	if (call_info->origin.type == Call_Origin_Type::POLY_STRUCT) {
		poly_header = call_info->origin.options.poly_struct->poly_header;
	}
	else if (call_info->origin.type == Call_Origin_Type::POLY_FUNCTION) {
		poly_header = call_info->origin.options.poly_function.poly_header;
	}
	else {
		panic("");
	}

	auto compilation_data = semantic_context->compilation_data;
	auto type_system = compilation_data->type_system;
	auto& types = type_system->predefined_types;
	Arena& arena = *semantic_context->scratch_arena;
	auto checkpoint = arena.make_checkpoint();
	SCOPE_EXIT(arena.rewind_to_checkpoint(checkpoint));

	auto& parameter_values = call_info->parameter_values;
	auto& parameter_nodes = poly_header->parameter_nodes;
	const int return_type_index = poly_header->signature->return_type_index;

	// Check for errors (Instanciation limit or header has errors)
	{
		auto workload = semantic_context->current_workload;

		// Check instanciation limit
		{
			const int MAX_POLYMORPHIC_INSTANCIATION_DEPTH = 10;
			int instanciation_depth = workload->polymorphic_instanciation_depth + 1;
			if (instanciation_depth > MAX_POLYMORPHIC_INSTANCIATION_DEPTH) {
				log_semantic_error(semantic_context, "Polymorphic instanciation limit reached!", error_report_node, error_report_section);
				return nullptr;
			}
		}

		// Note: Only check if header contains errors, we don't check base-analysis (As this can cause issues with dependencies)
		bool base_contains_errors = false;
		{
			Workload_Base* header_workload = nullptr;
			if (poly_header->is_function) {
				Function_Progress* base_progress = poly_header->origin.function_progress;
				header_workload = upcast(base_progress->header_workload);
			}
			else {
				Workload_Structure_Polymorphic* poly_struct = poly_header->origin.struct_workload;
				header_workload = upcast(poly_struct);
			}

			// Check if header has errors
			assert(header_workload->is_finished, "Header must be finished before we can instanciate");
			if (header_workload->real_error_count > 0 || header_workload->errors_due_to_unknown_count > 0) {
				base_contains_errors = true;
			}
		}

		if (base_contains_errors) {
			semantic_analyser_set_error_flag(true, semantic_context);
			return nullptr;
		}
	}

	// Prepare instanciation data
	bool success = true;
	bool encounted_unknown = false;

	Array<Pattern_Variable_State> variable_states = array_create<Pattern_Variable_State>(poly_header->pattern_variables.size);
	SCOPE_EXIT(if (variable_states.data != 0) { array_destroy(&variable_states); });
	for (int i = 0; i < variable_states.size; i++) {
		assert(poly_header->base_analysis_states[i].type == Pattern_Variable_State_Type::UNSET, "");
		variable_states[i] = pattern_variable_state_make_unset();
	}

	Array<Datatype*> partial_pattern_instances = array_create<Datatype*>(poly_header->partial_pattern_count);
	SCOPE_EXIT(if (partial_pattern_instances.data != 0) { array_destroy(&partial_pattern_instances); });
	for (int i = 0; i < partial_pattern_instances.size; i++) {
		partial_pattern_instances[i] = nullptr;
	}

	Array<Datatype*> parameter_types_instanced = arena.allocate_array<Datatype*>(poly_header->signature->parameters.size);
	for (int i = 0; i < parameter_types_instanced.size; i++) {
		parameter_types_instanced[i] = nullptr;
		auto& param_type = poly_header->signature->parameters[i].datatype;
		if (!param_type->contains_pattern) {
			parameter_types_instanced[i] = param_type;
		}
	}

	// Setup datastructures
	Pattern_Match_Result pattern_match_result = pattern_match_result_make_empty(&arena);
	Parameter_Dependency_Graph graph = parameter_dependency_graph_create_empty(poly_header, &arena, variable_states, &pattern_match_result.constraints);
	int last_constraint_count = 0;

	// Initialize dependency graph infos
	for (int param_index = 0; param_index < poly_header->signature->parameters.size; param_index++)
	{
		auto& param_info = poly_header->signature->parameters[param_index];
		auto& run_info = graph.run_infos[param_index];
		auto& param_value = parameter_values[param_index];

		// Skip explicitly set variables, there is a custom code-path for them
		if (param_info.comptime_variable_index != -1 && param_info.requires_named_addressing) {
			continue;
		}

		run_info.has_dependency_on_pattern_state = false;
		run_info.dependency_count = 0;
		run_info.dependency_on_self_definition_count = 0;
		run_info.index_in_waiting_array = -1;

		// Find dependencies
		for (int i = 0; i < param_info.dependencies.size; i++)
		{
			auto depends_on_var = &poly_header->pattern_variables[param_info.dependencies[i]];
			auto& var_state = variable_states[depends_on_var->value_access_index];
			if (depends_on_var->defined_in_parameter_index == param_index) {
				run_info.dependency_on_self_definition_count += 1;
			}

			run_info.dependency_count += 1;
			graph.variable_dependents[depends_on_var->value_access_index].dependents.push_back(param_index);
		}

		// Add to correct array
		assert(run_info.index_in_waiting_array == -1, "Should be true after init");
		if (run_info.dependency_count - run_info.dependency_on_self_definition_count == 0)
		{
			graph.runnable_indices.push_back(param_index);
		}
		else {
			run_info.index_in_waiting_array = (int)graph.waiting_indices.size;
			graph.waiting_indices.push_back(param_index);
		}
	}

	// Handle explicitly set matching-variables first, e.g. add(15, 12, T = int), with add :: (a: $T, b: T)
	for (int i = parameter_nodes.size; i < poly_header->signature->parameters.size; i++)
	{
		// See how argument-to-parameter mapping is done to get this, note: parameter_nodes, because param_matching_info does not have return type
		auto& param_info = poly_header->signature->parameters[i];
		auto& param_value = parameter_values[i];
		if (param_value.value_type == Parameter_Value_Type::NOT_SET) continue;
		assert(param_info.comptime_variable_index != -1 && param_info.requires_named_addressing, "");
		assert(
			param_value.value_type != Parameter_Value_Type::DATATYPE_KNOWN,
			"Setting implicit parameters should not be done this way, as we need to calculate the comptime value"
		);

		auto& pattern_variable = poly_header->pattern_variables[param_info.comptime_variable_index];
		assert(variable_states[pattern_variable.value_access_index].type == Pattern_Variable_State_Type::UNSET, "");

		// Analyse argument in unknown context
		analyse_parameter_value_if_not_already_done(call_info, &param_value, semantic_context, expression_context_make_unspecified(), true);

		// Extra code-path for setting variables to patterns, e.g. Node(V = $T)
		Datatype* param_type = parameter_value_get_datatype(param_value, call_info, semantic_context);
		if (param_type->contains_pattern) {
			parameter_dependency_graph_set_variable_to_pattern(&graph, &pattern_variable, param_type);
			continue;
		}
		else if (datatype_is_unknown(param_type)) {
			success = false;
			encounted_unknown = true;
			continue;
		}
		parameter_types_instanced[i] = param_type;

		// Check if parameter_value is comptime
		if (param_value.value_type == Parameter_Value_Type::COMPTIME_VALUE) {
			parameter_dependency_graph_set_variable_value(&graph, &pattern_variable, param_value.options.constant, compilation_data);
			continue;
		}

		// Otherwise
		assert(param_value.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "");
		auto arg_expr = call_info->argument_infos[param_value.options.argument_index].expression;
		auto comptime_result = expression_calculate_comptime_value(
			arg_expr, "Explicitly setting the pattern-variable value requires a polymorphic value", semantic_context);
		if (comptime_result.available) {
			parameter_dependency_graph_set_variable_value(&graph, &pattern_variable, comptime_result.value, compilation_data);
		}
		else {
			success = false;
			log_semantic_error(semantic_context, "Argument was not comptime", arg_expr, Node_Section::FIRST_TOKEN);
		}
	}

	// Analyse all arguments
	while (graph.waiting_indices.size > 0 || graph.runnable_indices.size > 0)
	{
		// Run runnable arguments
		while (graph.runnable_indices.size > 0)
		{
			int runnable_index = graph.runnable_indices.last();
			graph.runnable_indices.swap_remove((int)graph.runnable_indices.size - 1);

			auto& run_info = graph.run_infos[runnable_index];
			auto& param_info = poly_header->signature->parameters[runnable_index];
			auto& param_value = parameter_values[runnable_index];
			const bool is_return_type = runnable_index == poly_header->signature->return_type_index;

			// Instanciate parameter-type if there are no dependencies anymore
			Datatype* parameter_type = param_info.datatype;
			if (param_info.datatype->contains_pattern && run_info.dependency_count == 0 &&
				!run_info.has_dependency_on_pattern_state && !param_info.datatype->contains_partial_pattern)
			{
				parameter_type = datatype_pattern_instanciate(
					parameter_type, variable_states, error_report_node, error_report_section, semantic_context
				);
				if (parameter_type != nullptr)
				{
					parameter_types_instanced[runnable_index] = parameter_type;
					if (param_info.partial_pattern_index != -1) {
						partial_pattern_instances[param_info.partial_pattern_index] = parameter_type;
					}
				}
				else {
					// An error during instanciation is already logged in the instanciate call
					success = false;
					parameter_type = param_info.datatype;
				}
			}

			// Skip parameter if not set
			if (param_value.value_type == Parameter_Value_Type::NOT_SET) {
				continue;
			}

			// Analyse argument if given
			if (param_value.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION)
			{
				auto arg_expr = call_info->argument_infos[param_value.options.argument_index].expression;

				Expression_Context context = expression_context_make_unspecified();
				if (!parameter_type->contains_pattern) {
					context = expression_context_make_specific_type(parameter_type);
				}
				analyse_parameter_value_if_not_already_done(call_info, &param_value, semantic_context, context, param_info.comptime_variable_index != -1);

				// Do auto-dereference/address of if applicable before matching
				if (parameter_type->contains_pattern) 
				{
					auto expr_info = get_info(arg_expr, semantic_context);
					Expression_Value_Info value_info = expression_info_get_value_info(expr_info, type_system);
					Type_Modifier_Info src_info = datatype_get_modifier_info(value_info.initial_type);
					if (src_info.struct_subtype != nullptr) { // Keep subtype
						src_info.base_type = upcast(src_info.struct_subtype);
					}
					Type_Modifier_Info dst_info = datatype_get_modifier_info(parameter_type);
					Cast_Type cast_type = check_if_type_modifier_update_valid(src_info, dst_info, value_info.initial_value_is_temporary);
					if (cast_type != Cast_Type::INVALID) {
						expr_info->cast_info = cast_info_make_simple(
							type_system_make_type_with_modifiers(type_system, src_info.base_type, dst_info.pointer_level, dst_info.optional_flags),
							cast_type
						);
					}
				}
			}

			// Handle struct-pattern variables
			bool argument_is_temporary = true;
			Datatype* argument_type = parameter_value_get_datatype(param_value, call_info, semantic_context, &argument_is_temporary);
			if (argument_type->contains_pattern)
			{
				assert(param_info.comptime_variable_index != -1, "");
				auto variable = &poly_header->pattern_variables[param_info.comptime_variable_index];
				parameter_dependency_graph_set_variable_to_pattern(&graph, variable, argument_type);
				continue;
			}

			// Match parameter to pattern
			if (parameter_type->contains_pattern)
			{
				// Match pattern
				auto& constraints = pattern_match_result.constraints;
				int constraint_count_before = (int) constraints.size;
				if (pattern_type_match_against(pattern_match_result, 0, parameter_type, argument_type, semantic_context))
				{
					// Store type infos for instanciations
					parameter_types_instanced[runnable_index] = argument_type;
					if (param_info.partial_pattern_index != -1) {
						partial_pattern_instances[param_info.partial_pattern_index] = argument_type;
					}

					// Set all variables that were matched to values
					int new_size = constraints.size; // Required because set_variable_value may add constraints
					for (int i = constraint_count_before; i < new_size; i++)
					{
						auto& constraint = constraints[i];
						if (constraint.pattern_variable->is_reference) continue;
						parameter_dependency_graph_set_variable_value(&graph, constraint.pattern_variable->variable, constraint.value, compilation_data);
					}
				}
				else
				{
					success = false;
					if (datatype_is_unknown(argument_type)) {
						encounted_unknown = true;
					}
					else {
						log_semantic_error(semantic_context, "Could not match given type to pattern", error_report_node, error_report_section);
						log_error_info_id(semantic_context, param_info.name);
						log_error_info_given_type(semantic_context, argument_type);
						log_error_info_expected_type(semantic_context, parameter_type);
					}
				}
			}
			else
			{
				// Since this instanciate analyses all arguments, we need to do type-checking
				// For expressions this is already done in the context, but for non-expression arguments this needs to be checked
				if (!is_return_type && !types_are_equal(parameter_type, argument_type))
				{
					success = false;
					if (datatype_is_unknown(argument_type)) {
						encounted_unknown = true;
					}
					else {
						log_semantic_error(semantic_context, "Argument type does not match parameter type", error_report_node, error_report_section);
						log_error_info_id(semantic_context, param_info.name);
						log_error_info_given_type(semantic_context, argument_type);
						log_error_info_expected_type(semantic_context, parameter_type);
					}
				}
			}

			// Calculate comptime value if required
			if (param_info.comptime_variable_index == -1) continue;
			if (datatype_is_unknown(parameter_type)) {
				encounted_unknown = true;
				success = false;
				continue;
			}

			auto variable = &poly_header->pattern_variables[param_info.comptime_variable_index];
			switch (param_value.value_type)
			{
			case Parameter_Value_Type::COMPTIME_VALUE:
			{
				parameter_dependency_graph_set_variable_value(&graph, variable, param_value.options.constant, compilation_data);
				break;
			}
			case Parameter_Value_Type::ARGUMENT_EXPRESSION:
			{
				auto arg_expr = call_info->argument_infos[param_value.options.argument_index].expression;
				bool was_not_available = false;
				auto comptime_result = expression_calculate_comptime_value(
					arg_expr, "Explicitly setting the pattern-variable value requires a polymorphic value", semantic_context, &was_not_available
				);
				if (comptime_result.available) {
					parameter_dependency_graph_set_variable_value(&graph, variable, comptime_result.value, compilation_data);
				}
				else {
					success = false;
					if (!was_not_available) {
						log_semantic_error(semantic_context, "Argument was not comptime", arg_expr, Node_Section::FIRST_TOKEN);
					}
					else {
						encounted_unknown = true;
					}
				}
				break;
			}
			case Parameter_Value_Type::DATATYPE_KNOWN:
			{
				success = false;
				log_semantic_error(semantic_context, "Parmeter requires comptime value, but only datatype is know", error_report_node, error_report_section);
				log_error_info_id(semantic_context, param_info.name);
				break;
			}
			default: panic("NOT set should have been skipped");
			}
		}

		// Handle unrunnable parameters (Cyclic dependency or dependent variable could not be resolved)
		if (graph.waiting_indices.size > 0 && graph.runnable_indices.size == 0)
		{
			// We analyse parameters from left-to-right if we have unresolvable variables
			int smallest_index_to_run = INT_MAX;
			for (int i = 0; i < graph.waiting_indices.size; i++) {
				int to_run = graph.waiting_indices[i];
				if (to_run < smallest_index_to_run) {
					smallest_index_to_run = to_run;
				}
			}
			auto& run_info = graph.run_infos[smallest_index_to_run];
			assert(run_info.index_in_waiting_array != -1, "Shouldn't have been run yet");
			parameter_dependency_graph_set_parameter_runnable(&graph, smallest_index_to_run, false);
		}
	}

	// Check if all constraints are valid
	if (!encounted_unknown) {
		if (!pattern_match_result_check_constraints_with_states(pattern_match_result, variable_states)) {
			success = false;
			log_semantic_error(semantic_context, "Matching failed, some constraints did not hold up", error_report_node, error_report_section);
		}
	}

	// Return if there were errors/values not available
	if (!success) {
		return nullptr;
	}

	auto result = poly_header_instanciate_with_variable_states(
		poly_header, variable_states, partial_pattern_instances, parameter_types_instanced,
		call_info, error_report_node, error_report_section, semantic_context
	);
	variable_states.data = nullptr; // Don't cleanup, as line above takes ownership
	partial_pattern_instances.data = nullptr;
	return result;
}



// MODULE ANALYSIS
struct Import_Info
{
	AST::Import* import_node;
	Symbol_Table* symbol_table;
};

struct Module_Content
{
	DynArray<Import_Info> file_imports;
	DynArray<Import_Info> module_imports;
	DynArray<Import_Info> symbol_imports;
	DynArray<Import_Info> context_or_dot_call_imports;
	DynArray<Upp_Module*> modules;
};

Module_Content module_content_create(Arena* arena)
{
	Module_Content result;
	result.file_imports = DynArray<Import_Info>::create(arena);
	result.module_imports = DynArray<Import_Info>::create(arena);
	result.symbol_imports = DynArray<Import_Info>::create(arena);
	result.context_or_dot_call_imports = DynArray<Import_Info>::create(arena);
	result.modules = DynArray<Upp_Module*>::create(arena);
	return result;
}

void module_content_add_import_node(Module_Content& module_content, AST::Import* import_node, Symbol_Table* symbol_table)
{
	DynArray<Import_Info>* add_to = nullptr;
	switch (import_node->operator_type)
	{
	case AST::Import_Operator::FILE_IMPORT: add_to = &module_content.file_imports; break;
	case AST::Import_Operator::MODULE_IMPORT:
	case AST::Import_Operator::MODULE_IMPORT_TRANSITIVE: add_to = &module_content.module_imports; break;
	case AST::Import_Operator::SINGLE_SYMBOL: add_to = &module_content.symbol_imports; break;
	default: panic("");;
	}

	assert(import_node->import_type != Import_Type::NONE, "None is only used for lookups, parser should not create that");
	if (import_node->import_type != Import_Type::SYMBOLS) {
		add_to = &module_content.context_or_dot_call_imports;
	}

	Import_Info info;
	info.import_node = import_node;
	info.symbol_table = symbol_table;
	add_to->push_back(info);
}

void analyser_create_symbol_and_workload_for_definition(AST::Definition* definition, Module_Content* content, Semantic_Context* context);

Upp_Module* analyse_module_node(AST::Module* module_node, Module_Content* module_content, Semantic_Context* semantic_context)
{
	auto compilation_data = semantic_context->compilation_data;

	// Create module
	Upp_Module* upp_module = compilation_data->arena.allocate<Upp_Module>();
	upp_module->node = module_node;
	upp_module->symbol_table = symbol_table_create_with_parent(
		semantic_context->current_symbol_table, Symbol_Access_Level::GLOBAL, semantic_context->compilation_data
	);
	upp_module->is_file_module = false;
	upp_module->options.module_symbol = nullptr;
	module_content->modules.push_back(upp_module);

	// Store infos
	get_info(module_node, semantic_context, true)->upp_module = upp_module;

	// Create custom_operator_table workload now, because parent-relations are stored when the symbol-tables are created
	symbol_table_install_custom_operator_table(upp_module->symbol_table, upp_module->node->custom_operators, true, semantic_context);
	RESTORE_ON_SCOPE_EXIT(semantic_context->current_symbol_table, upp_module->symbol_table);

	// Store all imports
	for (int i = 0; i < module_node->import_nodes.size; i++) {
		module_content_add_import_node(*module_content, module_node->import_nodes[i], semantic_context->current_symbol_table);
	}

	// Create/Handle extern imports
	for (int i = 0; i < module_node->extern_imports.size; i++)
	{
		auto extern_import = module_node->extern_imports[i];
		Workload_Definition* definition_workload = nullptr;
		switch (extern_import->type)
		{
		case AST::Extern_Type::STRUCT:
		case AST::Extern_Type::GLOBAL:
		case AST::Extern_Type::FUNCTION:
		{
			// Create Symbol
			Symbol* symbol = 0;
			{
				String* id;
				if (extern_import->type == AST::Extern_Type::STRUCT) {
					id = 0;
				}
				else if (extern_import->type == AST::Extern_Type::FUNCTION) {
					id = extern_import->options.function.id;
				}
				else if (extern_import->type == AST::Extern_Type::GLOBAL) {
					id = extern_import->options.global.id;
				}
				else {
					panic("");
				}
				if (id != 0) {
					symbol = symbol_table_define_symbol(
						upp_module->symbol_table, id, Symbol_Type::DEFINITION_UNFINISHED, upcast(extern_import), Symbol_Access_Level::GLOBAL,
						compilation_data
					);
				}
			}

			// Create workload
			auto definition_workload = workload_executer_allocate_workload<Workload_Definition>(semantic_context->current_workload, compilation_data);
			definition_workload->symbol = symbol;
			definition_workload->symbol_table = upp_module->symbol_table;
			definition_workload->is_extern_import = true;
			definition_workload->options.extern_import = extern_import;
			definition_workload->analysis_pass = semantic_context->current_pass;

			// Add dependencies and finish
			if (symbol != 0) {
				symbol->options.definition_workload = definition_workload;
			}
			break;
		}
		case AST::Extern_Type::COMPILER_SETTING:
		{
			Dynamic_Array<String*>& values = compilation_data->extern_sources.compiler_settings[(int)extern_import->options.setting.type];
			String* id = extern_import->options.setting.value;

			// Check if unique
			bool found = false;
			for (int i = 0; i < values.size; i++) {
				if (values[i] == id) {
					found = true;
					break;
				}
			}

			if (!found) {
				dynamic_array_push_back(&values, id);
			}
			break;
		}
		case AST::Extern_Type::INVALID: break;
		default: panic("");
		}
	}

	// Create definition workloads (Recursive)
	for (int i = 0; i < module_node->definitions.size; i++) {
		auto definition_node = module_node->definitions[i];
		analyser_create_symbol_and_workload_for_definition(definition_node, module_content, semantic_context);
	}

	return upp_module;
}

// Create correct workloads for comptime definitions, for non-comptime checks if its a variable or a global and sets the symbol correctly
void analyser_create_symbol_and_workload_for_definition(AST::Definition* definition, Module_Content* module_content, Semantic_Context* semantic_context)
{
	Symbol_Table* symbol_table = semantic_context->current_symbol_table;
	assert(!(definition->values.size == 0 && definition->types.size == 0), "Cannot have values and types be 0");
	assert(definition->symbols.size != 0, "Parser shouldn't allow this");
	assert(!(module_content == nullptr && definition->is_comptime), "");

	// Define all symbols
	bool is_local_variable = !definition->is_comptime && definition->base.parent->type == AST::Node_Type::STATEMENT;
	bool is_global_variable = !definition->is_comptime && !is_local_variable;

	// Figure out initial symbol type
	Symbol_Type initial_symbol_type = is_local_variable ? Symbol_Type::VARIABLE_UNDEFINED : Symbol_Type::ERROR_SYMBOL;
	if (definition->is_comptime && definition->values.size == 1) {
		auto value_expr = definition->values[0];
		switch (value_expr->type)
		{
		case AST::Expression_Type::MODULE: initial_symbol_type = Symbol_Type::MODULE; break;
		case AST::Expression_Type::FUNCTION: initial_symbol_type = Symbol_Type::FUNCTION; break;
		case AST::Expression_Type::STRUCTURE_TYPE: initial_symbol_type = Symbol_Type::DATATYPE; break;
		default: break;
		}
	}

	for (int i = 0; i < definition->symbols.size; i++) {
		auto info = get_info(definition->symbols[i], semantic_context, true);
		info->symbol = symbol_table_define_symbol(
			symbol_table, definition->symbols[i]->name, initial_symbol_type, AST::upcast(definition->symbols[i]),
			(is_local_variable ? Symbol_Access_Level::INTERNAL : Symbol_Access_Level::GLOBAL), 
			semantic_context->compilation_data
		);
	}

	// For local variables only symbol is defined, and analysis happens when the statement is processed
	if (is_local_variable) {
		return;
	}

	// Report errors
	bool error_occured = false;
	for (int i = 1; i < definition->symbols.size; i++) {
		log_semantic_error(semantic_context, "Multiple Symbols not allowed for global/comptime definition, e.g. '::'", upcast(definition->symbols[i]));
		error_occured = true;
	}
	for (int i = 1; i < definition->values.size; i++) {
		log_semantic_error(semantic_context, "Multiple Values not allowed for global/comptime definition, e.g. '::'", upcast(definition->values[i]));
		error_occured = true;
	}
	if (!is_global_variable && definition->types.size > 0) {
		log_semantic_error(semantic_context, "Type not allowed on comptime definition", upcast(definition->types[0]));
		error_occured = true;
	}
	for (int i = 1; i < definition->types.size; i++) {
		log_semantic_error(semantic_context, "Multiple types currently not allowed on global/comptime definition", upcast(definition->types[i]));
		error_occured = true;
	}
	if (definition->values.size == 0 && !is_global_variable) {
		log_semantic_error(semantic_context, "Comptime definition must have a value", upcast(definition));
		error_occured = true;
	}
	if (error_occured) {
		// Set all defined symbols to error_symbol
		for (int i = 0; i < definition->symbols.size; i++) {
			auto info = get_info(definition->symbols[i], semantic_context);
			info->symbol->type = Symbol_Type::ERROR_SYMBOL;
		}
		return;
	}

	// Create workload for functions, structs and modules directly
	Symbol* symbol = get_info(definition->symbols[0], semantic_context)->symbol;
	if (definition->is_comptime)
	{
		AST::Expression* value = definition->values[0];
		// Check if it's a 'named' construct (function, struct, module)
		switch (value->type)
		{
		case AST::Expression_Type::MODULE:
		{
			Upp_Module* upp_module = analyse_module_node(value->options.module, module_content, semantic_context);
			symbol->type = Symbol_Type::MODULE;
			symbol->options.upp_module = upp_module;
			upp_module->is_file_module = false;
			upp_module->options.module_symbol = symbol;
			return;
		}
		case AST::Expression_Type::FUNCTION: {
			// Note: Creating the progress also sets the symbol type
			function_progress_create_with_modtree_function(symbol, value, nullptr, semantic_context);
			return;
		}
		case AST::Expression_Type::STRUCTURE_TYPE: {
			// Note: Creating the workload also sets the symbol type
			workload_structure_create(value, symbol, false, semantic_context);
			return;
		}
		default: break;
		}
	}

	// Create workload for global variables/comptime definitions
	auto definition_workload = workload_executer_allocate_workload<Workload_Definition>(
		semantic_context->current_workload, semantic_context->compilation_data
	);
	definition_workload->symbol = symbol;
	definition_workload->is_extern_import = false;
	definition_workload->analysis_pass = semantic_context->current_pass;
	auto& def = definition_workload->options.normal;
	def.is_comptime = definition->is_comptime;
	def.type_node = nullptr;
	if (is_global_variable && definition->types.size != 0) {
		def.type_node = definition->types[0];
	}
	def.value_node = nullptr;
	if (definition->values.size != 0) {
		def.value_node = definition->values[0];
	}

	symbol->type = Symbol_Type::DEFINITION_UNFINISHED;
	symbol->options.definition_workload = definition_workload;
}

struct Symbol_Import
{
	Symbol* symbol;
	AST::Import* import_node;
	Symbol_Table* symbol_table;
	bool currently_analysing;
	bool is_circular_start;
};

Symbol* symbol_import_resolve_recursive(
	Symbol_Import& symbol_import, DynArray<Symbol_Import>& symbol_imports, bool* circular_chain_found, Semantic_Context* semantic_context)
{
	auto error_symbol = semantic_context->compilation_data->error_symbol;

	Symbol* symbol = symbol_import.symbol;
	// Return if a previous symbol-import chain has already analysed this symbol
	if (symbol_import.symbol->type != Symbol_Type::ALIAS_UNFINISHED) return symbol;
	symbol_import.currently_analysing = true;

	assert(symbol_import.import_node->operator_type != AST::Import_Operator::FILE_IMPORT, "");
	AST::Path_Lookup* path = symbol_import.import_node->options.path;
	path_lookup_set_info_to_error_symbol(path, semantic_context);
	Symbol_Table* symbol_table = symbol_import.symbol_table;
	for (int i = 0; i < path->parts.size; i++)
	{
		AST::Symbol_Lookup* path_part = path->parts[i];
		Symbol_Query_Info query_info = symbol_query_info_make(Symbol_Access_Level::GLOBAL, Import_Type::SYMBOLS, true);
		if (i != 0) {
			query_info.import_search_type = Import_Type::NONE;
			query_info.search_parents = false;
		}
		Symbol* part_symbol = symbol_lookup_resolve_to_single_symbol(
			path_part, symbol_table, query_info, semantic_context, (i < path->parts.size - 1), false
		);

		// Handle alias chains
		if (part_symbol->type == Symbol_Type::ALIAS_UNFINISHED) 
		{
			auto& other_import = symbol_imports[part_symbol->options.unfinished_alias_index];
			// Check if we have circular dependency
			if (other_import.currently_analysing) 
			{
				*circular_chain_found = true;
				other_import.is_circular_start = true;
				log_semantic_error(semantic_context, "Circular alias chain detected", upcast(path_part), Node_Section::FIRST_TOKEN);
				symbol->type = Symbol_Type::ALIAS;
				symbol->options.alias_for = error_symbol;
				return symbol;
			}

			part_symbol = symbol_import_resolve_recursive(other_import, symbol_imports, circular_chain_found, semantic_context);
			if (*circular_chain_found) 
			{
				log_semantic_error(semantic_context, "Circular alias chain detected", upcast(path_part), Node_Section::FIRST_TOKEN);
				symbol->type = Symbol_Type::ALIAS;
				symbol->options.alias_for = error_symbol;
				if (symbol_import.is_circular_start) {
					*circular_chain_found = false;
				}
				return symbol;
			}
		}

		if (part_symbol->type == Symbol_Type::ERROR_SYMBOL) 
		{
			symbol->type = Symbol_Type::ALIAS;
			symbol->options.alias_for = part_symbol;
			return symbol;
		}

		if (i == path->parts.size - 1)
		{
			// Check if we are at the end of the path import
			get_info(path, false)->symbol = part_symbol;
			symbol->type = Symbol_Type::ALIAS;
			symbol->options.alias_for = part_symbol;
			return symbol;
		}
		else
		{
			// Otherwise we are in the middle of the path
			if (part_symbol->type != Symbol_Type::MODULE) {
				log_semantic_error(semantic_context, "Expected module in the middle of path", upcast(path_part), Node_Section::WHOLE);
				symbol->type = Symbol_Type::ALIAS;
				symbol->options.alias_for = error_symbol;
				return symbol;
			}

			symbol_table = part_symbol->options.upp_module->symbol_table;
		}
	}

	panic("Invalid code path");
	return nullptr;
}

void module_content_analyse_items(Module_Content& module_content, Semantic_Context* semantic_context)
{
	auto workload = semantic_context->current_workload;
	Arena* scratch_arena = semantic_context->scratch_arena;
	auto compilation_data = semantic_context->compilation_data;
	auto compiler = compilation_data->compiler;
	
	// Resolve all file-imports
	for (int i = 0; i < module_content.file_imports.size; i++)
	{
		Import_Info info = module_content.file_imports[i];
		Symbol_Table* import_symbol_table = info.symbol_table;
		AST::Import* import_node = info.import_node;
		assert(import_node->operator_type == AST::Import_Operator::FILE_IMPORT, "");

		Symbol* module_import_symbol = nullptr;
		if (import_node->alias_name.available)
		{
			module_import_symbol = symbol_table_define_symbol(
				import_symbol_table, import_node->alias_name.value->name, Symbol_Type::ERROR_SYMBOL,
				upcast(import_node->alias_name.value), Symbol_Access_Level::GLOBAL, semantic_context->compilation_data
			);
			get_info(import_node->alias_name.value, semantic_context, true)->symbol = module_import_symbol;
		}
		else {
			log_semantic_error(semantic_context, "File import requires name, e.g. import \"...\" as Name", upcast(import_node), Node_Section::FIRST_TOKEN);
		}

		if (import_node->import_type != Import_Type::SYMBOLS) {
			log_semantic_error(
				semantic_context, "File import must not specify an import option (dot_call or context)", 
				upcast(import_node), Node_Section::IDENTIFIER
			);
		}

		// Load file
		Compilation_Unit* compilation_unit = compiler_import_file(compiler, import_node);
		if (compilation_unit == nullptr)
		{
			log_semantic_error(semantic_context, "Could not load file", upcast(import_node), Node_Section::FIRST_TOKEN);
			continue;
		}

		// Parse file if not done yet
		if (compilation_unit->root == nullptr) {
			compiler_parse_unit(compilation_unit, compilation_data);
		}

		// Analyse module if not done already
		Upp_Module* import_module = nullptr;
		Node_Passes* node_passes = hashtable_find_element(&semantic_context->compilation_data->ast_to_pass_mapping, upcast(compilation_unit->root));
		if (node_passes == nullptr) // File was not yet analysed
		{
			Analysis_Pass* import_module_pass = analysis_pass_allocate(semantic_context->current_workload, upcast(compilation_unit->root), compilation_data);
			RESTORE_ON_SCOPE_EXIT(semantic_context->current_pass, import_module_pass);
			RESTORE_ON_SCOPE_EXIT(semantic_context->current_symbol_table, semantic_context->compilation_data->root_symbol_table);
			RESTORE_ON_SCOPE_EXIT(semantic_context->symbol_access_level, Symbol_Access_Level::GLOBAL);
			import_module = analyse_module_node(compilation_unit->root, &module_content, semantic_context);
		}
		else
		{
			assert(node_passes->passes.size == 1, "Modules should only be analysed at most once currently");
			import_module = pass_get_base_info(
				node_passes->passes[0], upcast(compilation_unit->root), Info_Query::TRY_READ, compilation_data
			)->module_info.upp_module;
		}

		// Update Symbol info
		if (module_import_symbol != nullptr)
		{
			module_import_symbol->type = Symbol_Type::MODULE;
			module_import_symbol->options.upp_module = import_module;
		}
	}

	// Resolve all module-imports (import A~*, B~*)
	for (int i = 0; i < module_content.module_imports.size; i++)
	{
		Import_Info info = module_content.module_imports[i];
		Symbol_Table* import_symbol_table = info.symbol_table;
		AST::Import* import_node = info.import_node;
		assert(import_node->operator_type == AST::Import_Operator::MODULE_IMPORT || 
			import_node->operator_type == AST::Import_Operator::MODULE_IMPORT_TRANSITIVE, ""
		);
		assert(import_node->import_type == Import_Type::SYMBOLS, "Otherwise the imports would be in other module_content array");

		if (import_node->alias_name.available) {
			log_semantic_error(semantic_context, "Cannot alias ~* or ~** imports", upcast(import_node), Node_Section::FIRST_TOKEN);
		}

		RESTORE_ON_SCOPE_EXIT(semantic_context->current_symbol_table, import_symbol_table);
		RESTORE_ON_SCOPE_EXIT(semantic_context->symbol_access_level, Symbol_Access_Level::GLOBAL);
		Symbol* symbol = path_lookup_resolve_to_single_symbol(import_node->options.path, true, semantic_context, true);
		if (symbol->type != Symbol_Type::MODULE) {
			if (symbol->type != Symbol_Type::ERROR_SYMBOL) {
				log_semantic_error(semantic_context, "Symbol is not a module", upcast(import_node->options.path->last), Node_Section::FIRST_TOKEN);
			}
			continue;
		}

		const bool is_transitive = import_node->operator_type == AST::Import_Operator::MODULE_IMPORT_TRANSITIVE;
		symbol_table_add_import(
			import_symbol_table, symbol->options.upp_module->symbol_table, 
			import_node->import_type, is_transitive, Symbol_Access_Level::GLOBAL, semantic_context,
			upcast(import_node), Node_Section::FIRST_TOKEN
		);
	}

	// Create all alias symbols (Single symbol imports like import A~Node, import B~C~foo as bar)
	DynArray<Symbol_Import> symbol_imports = DynArray<Symbol_Import>::create(scratch_arena);
	for (int i = 0; i < module_content.symbol_imports.size; i++)
	{
		Import_Info info = module_content.symbol_imports[i];
		Symbol_Table* import_symbol_table = info.symbol_table;
		AST::Import* import_node = info.import_node;
		assert(import_node->operator_type == AST::Import_Operator::SINGLE_SYMBOL, "");
		auto path = import_node->options.path;

		// Check for errors
		if (import_node->import_type != Import_Type::SYMBOLS) {
			log_semantic_error(
				semantic_context, "Symbol import must not specify an import option (dot_call or context)", 
				upcast(import_node), Node_Section::IDENTIFIER
			);
		}
		if (path->parts.size == 1 && !import_node->alias_name.available) {
			log_semantic_error(
				semantic_context, "Cannot import single symbol, as it is already accessible",
				upcast(import_node), Node_Section::FIRST_TOKEN
			);
			continue;
		}
		if (path->parts.size == 1 && import_node->alias_name.available && import_node->alias_name.value->name == path->last->name) {
			log_semantic_error(
				semantic_context, "This does nothing, as available symbol is imported with same name"
				, upcast(import_node), Node_Section::FIRST_TOKEN
			);
			continue;
		}
		if (path->last->name == nullptr) {
			// NOTE: This may happen for usage in the Syntax-Editor, look at the parser for more info.
			//       Also i think this is kinda ugly because it's such a special case, but we'll see
			continue;
		}

		// Define new symbol
		AST::Node* definition_node = upcast(path->last);
		String* name = path->last->name;
		if (import_node->alias_name.available)
		{
			name = import_node->alias_name.value->name;
			definition_node = upcast(import_node->alias_name.value);
		}
		Symbol* new_symbol = symbol_table_define_symbol(
			import_symbol_table, name, Symbol_Type::ALIAS_UNFINISHED, definition_node, Symbol_Access_Level::GLOBAL, semantic_context->compilation_data
		);
		new_symbol->options.unfinished_alias_index = (int) symbol_imports.size;
		if (import_node->alias_name.available) {
			get_info(import_node->alias_name.value, semantic_context, true)->symbol = new_symbol;
		}

		Symbol_Import symbol_import;
		symbol_import.symbol_table = import_symbol_table;
		symbol_import.import_node = import_node;
		symbol_import.symbol = new_symbol;
		symbol_import.currently_analysing = false;
		symbol_import.is_circular_start = false;
		symbol_imports.push_back(symbol_import);
	}

	// Resolve all alias symbols 
	for (int i = 0; i < symbol_imports.size; i++)
	{
		bool circular_dependency_found = false;
		symbol_import_resolve_recursive(symbol_imports[i], symbol_imports, &circular_dependency_found, semantic_context);
	}

	// Resolve all dot_call or context imports
	for (int i = 0; i < module_content.context_or_dot_call_imports.size; i++)
	{
		Import_Info info = module_content.context_or_dot_call_imports[i];
		Symbol_Table* import_symbol_table = info.symbol_table;
		AST::Import* import_node = info.import_node;
		auto path = import_node->options.path;
		assert(import_node->import_type == Import_Type::DOT_CALLS || import_node->import_type == Import_Type::OPERATORS, "");

		if (import_node->alias_name.available) {
			log_semantic_error(semantic_context, "Cannot alias dot_call or context import", upcast(import_node), Node_Section::FIRST_TOKEN);
		}

		RESTORE_ON_SCOPE_EXIT(semantic_context->current_symbol_table, import_symbol_table);
		RESTORE_ON_SCOPE_EXIT(semantic_context->symbol_access_level, Symbol_Access_Level::GLOBAL);
		Symbol* symbol = path_lookup_resolve_to_single_symbol(path, true, semantic_context, true);
		if (symbol->type != Symbol_Type::MODULE) {
			if (symbol->type != Symbol_Type::ERROR_SYMBOL) {
				log_semantic_error(semantic_context, "Symbol is not a module", upcast(path->last), Node_Section::FIRST_TOKEN);
			}
			continue;
		}

		const bool is_transitive = import_node->operator_type == AST::Import_Operator::MODULE_IMPORT_TRANSITIVE;
		symbol_table_add_import(
			import_symbol_table, symbol->options.upp_module->symbol_table, 
			import_node->import_type, is_transitive, Symbol_Access_Level::GLOBAL, semantic_context,
			upcast(import_node), Node_Section::FIRST_TOKEN
		);
	}
}



// WORKLOAD ENTRY
void analyse_structure_member_nodes_recursive(
	Datatype_Struct* structure, Dynamic_Array<AST::Structure_Member_Node*> member_nodes, Semantic_Context* semantic_context)
{
	auto& types = semantic_context->compilation_data->type_system->predefined_types;

	int member_index = 0;
	int subtype_index = 0;
	for (int i = 0; i < member_nodes.size; i++)
	{
		auto member_node = member_nodes[i];
		if (member_node->is_expression)
		{
			auto& member = structure->members[member_index];
			member_index += 1;
			member.type = semantic_analyser_analyse_expression_type(member_node->options.expression, semantic_context);

			// Wait for member size to be known 
			{
				bool has_failed = false;
				type_wait_for_size_info_to_finish(member.type, semantic_context, dependency_failure_info_make(&has_failed, 0));
				if (has_failed) {
					member.type = types.unknown_type;
					log_semantic_error(
						semantic_context, "Struct contains itself, this can only work with references",
						upcast(member_node), Node_Section::IDENTIFIER
					);
				}
			}
		}
		else
		{
			Datatype_Struct* subtype = structure->subtypes[subtype_index];
			subtype_index += 1;
			analyse_structure_member_nodes_recursive(subtype, member_node->options.subtype_members, semantic_context);
		}
	}
	assert(member_index == structure->members.size, "");
	assert(subtype_index == structure->subtypes.size, "");
}

void analysis_workload_entry(void* userdata)
{
	Workload_Entry_Info* entry_info = (Workload_Entry_Info*)userdata;
	Compilation_Data* compilation_data = entry_info->compilation_data;
	Workload_Base* workload = entry_info->workload;
	auto& type_system = compilation_data->type_system;
	auto& types = type_system->predefined_types;

	Arena scratch_arena = Arena::create();
	SCOPE_EXIT(scratch_arena.destroy());

	switch (workload->type)
	{
	case Analysis_Workload_Type::MODULE_ANALYSIS:
	{
		auto module_workload = downcast<Workload_Module_Analysis>(workload);
		auto module_node = module_workload->module_node;

		Semantic_Context semantic_context = semantic_context_make(
			compilation_data, workload, compilation_data->root_symbol_table, Symbol_Access_Level::GLOBAL,
			analysis_pass_allocate(workload, upcast(module_node), compilation_data),
			&scratch_arena
		);

		// Analyse module and sub-modules recursive
		Module_Content module_content = module_content_create(&scratch_arena);
		Upp_Module* main_module = analyse_module_node(module_node, &module_content, &semantic_context);
		main_module->is_file_module = true;
		main_module->options.compilation_unit = compilation_data->main_unit;
		module_content_analyse_items(module_content, &semantic_context);

		break;
	}
	case Analysis_Workload_Type::OPERATOR_CONTEXT_CHANGE:
	{
		auto change_workload = downcast<Workload_Custom_Operator>(workload);

		Semantic_Context semantic_context = semantic_context_make(
			compilation_data, workload, change_workload->symbol_table, Symbol_Access_Level::GLOBAL,
			change_workload->analysis_pass, &scratch_arena
		);

		for (int i = 0; i < change_workload->change_nodes.size; i++)
		{
			auto change_node = change_workload->change_nodes[i];
			if (change_workload->type_to_analyse != change_node->type) continue;

			analyse_custom_operator_node(change_node, change_workload->operator_table, &semantic_context);
		}
		break;
	}
	case Analysis_Workload_Type::DEFINITION:
	{
		auto definition_workload = downcast<Workload_Definition>(workload);
		auto symbol = definition_workload->symbol;

		Semantic_Context local_semantic_context = semantic_context_make(
			compilation_data, workload, compilation_data->root_symbol_table, Symbol_Access_Level::GLOBAL,
			definition_workload->analysis_pass, &scratch_arena
		);
		Semantic_Context* semantic_context = &local_semantic_context;

		if (!definition_workload->is_extern_import)
		{
			auto& def = definition_workload->options.normal;
			if (!def.is_comptime) // Global variable definition
			{
				Expression_Context context = expression_context_make_unspecified();
				Datatype* type = 0;
				if (def.type_node != 0) {
					type = semantic_analyser_analyse_expression_type(def.type_node, semantic_context);
				}
				if (def.value_node != 0)
				{
					Expression_Context context = expression_context_make_unspecified();
					if (type != nullptr) {
						context = expression_context_make_specific_type(type);
					}
					type = semantic_analyser_analyse_expression_value(def.value_node, context, semantic_context);
				}

				auto global = modtree_program_add_global(semantic_context, type, symbol, false);
				if (def.value_node != 0) {
					global->has_initial_value = true;
					global->init_expr = def.value_node;
					global->definition_workload = downcast<Workload_Definition>(workload);
				}
				symbol->type = Symbol_Type::GLOBAL;
				symbol->options.global = global;
			}
			else // Comptime definition
			{
				auto result = semantic_analyser_analyse_expression_any(def.value_node, expression_context_make_unspecified(), semantic_context);
				switch (result->result_type)
				{
				case Expression_Result_Type::VALUE:
				{
					auto comptime = expression_calculate_comptime_value(
						def.value_node, "Value must be comptime in comptime definition (:: syntax)", semantic_context
					);
					if (!comptime.available) {
						symbol->type = Symbol_Type::ERROR_SYMBOL;
						return;
					}
					symbol->type = Symbol_Type::COMPTIME_VALUE;
					symbol->options.constant = comptime.value;
					break;
				}
				case Expression_Result_Type::CONSTANT: {
					symbol->type = Symbol_Type::COMPTIME_VALUE;
					symbol->options.constant = result->options.constant;
					break;
				}
				case Expression_Result_Type::HARDCODED_FUNCTION:
				{
					symbol->type = Symbol_Type::ERROR_SYMBOL;
					log_semantic_error(semantic_context, "Creating aliases for hardcoded functions currently not supported", AST::upcast(def.value_node));
					break;
					//symbol->type = Symbol_Type::HARDCODED_FUNCTION;
					//symbol->options.hardcoded = result->options.hardcoded;
					//break;
				}
				case Expression_Result_Type::FUNCTION:
				{
					ModTree_Function* function = result->options.function;
					symbol->type = Symbol_Type::ERROR_SYMBOL;
					log_semantic_error(semantic_context, "Creating symbol/function aliases currently not supported", AST::upcast(def.value_node));
					break;
				}
				case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
					symbol->type = Symbol_Type::ERROR_SYMBOL;
					log_semantic_error(semantic_context, "Creating aliases for polymorphic functions not supported!", AST::upcast(def.value_node));
					break;
				}
				case Expression_Result_Type::POLYMORPHIC_PATTERN: {
					symbol->type = Symbol_Type::ERROR_SYMBOL;
					log_semantic_error(semantic_context, "Creating aliases for polymorphic patterns currently not supported!", AST::upcast(def.value_node));
					break;
				}
				case Expression_Result_Type::NOTHING: {
					symbol->type = Symbol_Type::ERROR_SYMBOL;
					log_semantic_error(semantic_context, "Comptime definition expected a value, not nothing (void/no return value)", AST::upcast(def.value_node));
					break;
				}
				case Expression_Result_Type::DATATYPE: 
				{
					// TODO: Maybe also disallow this if this is an alias, as above
					symbol->type = Symbol_Type::DATATYPE;
					symbol->options.datatype = result->options.datatype;

					if (result->options.datatype->type == Datatype_Type::ENUM) {
						auto enum_type = downcast<Datatype_Enum>(result->options.datatype);
						if (enum_type->name == 0) {
							enum_type->name = symbol->id;
						}
					}
					break;
				}
				case Expression_Result_Type::POLYMORPHIC_STRUCT: {
					// TODO: Maybe also disallow this if this is an alias, as above
					symbol->type = Symbol_Type::DATATYPE;
					symbol->options.datatype = upcast(result->options.polymorphic_struct->body_workload->struct_type);
					break;
				}
				default: panic("");
				}
			}
		}
		else // is_extern_import
		{
			AST::Extern_Import* import = definition_workload->options.extern_import;
			switch (import->type)
			{
			case AST::Extern_Type::FUNCTION:
			{
				auto function_type = semantic_analyser_analyse_expression_type(import->options.function.type_expr, semantic_context);

				// Check for errors
				if (datatype_is_unknown(function_type)) {
					symbol->type = Symbol_Type::ERROR_SYMBOL;
					break;
				}
				else if (function_type->type != Datatype_Type::FUNCTION_POINTER) {
					log_semantic_error(semantic_context, "Extern function type must be function", import->options.function.type_expr);
					symbol->type = Symbol_Type::ERROR_SYMBOL;
					break;
				}
				auto function_ptr = downcast<Datatype_Function_Pointer>(function_type);

				// Check if function already exists in extern functions...
				// TODO: Deduplication could be done with hashset?
				auto& extern_functions = compilation_data->extern_sources.extern_functions;
				{
					bool found = false;
					for (int i = 0; i < extern_functions.size; i++) {
						auto extern_fn = extern_functions[i];
						if (extern_fn->options.extern_definition->symbol->id == symbol->id && extern_fn->signature == function_ptr->signature)
						{
							found = true;
							symbol->type = Symbol_Type::FUNCTION;
							symbol->options.function = extern_fn;
							break;
						}
					}
					if (found) {
						break;
					}
				}

				ModTree_Function* extern_fn = modtree_function_create_empty(function_ptr->signature, symbol->id, compilation_data);
				extern_fn->function_type = ModTree_Function_Type::EXTERN;
				extern_fn->options.extern_definition = definition_workload;

				symbol->type = Symbol_Type::FUNCTION;
				symbol->options.function = extern_fn;

				dynamic_array_push_back(&extern_functions, extern_fn);

				break;
			}
			case AST::Extern_Type::GLOBAL:
			{
				Datatype* datatype = semantic_analyser_analyse_expression_type(import->options.global.type_expr, semantic_context);
				if (datatype_is_unknown(datatype)) {
					symbol->type = Symbol_Type::ERROR_SYMBOL;
					break;
				}

				// Deduplicate if we already added this global
				{
					auto program = compilation_data->program;

					bool found = false;
					for (int i = 0; i < program->globals.size; i++) {
						auto global = program->globals[i];
						if (!global->is_extern) continue;
						if (global->symbol->id == symbol->id && types_are_equal(global->type, datatype)) {
							symbol->type = Symbol_Type::GLOBAL;
							symbol->options.global = global;
							found = true;
							break;
						}
					}
					if (found) {
						break;
					}
				}

				auto global = modtree_program_add_global(semantic_context, datatype, symbol, true);
				symbol->type = Symbol_Type::GLOBAL;
				symbol->options.global = global;

				break;
			}
			case AST::Extern_Type::STRUCT:
			{
				Datatype* type = semantic_analyser_analyse_expression_type(import->options.struct_type_expr, semantic_context);
				if (datatype_is_unknown(type)) {
					break;
				}

				if (type->type != Datatype_Type::STRUCT) {
					log_semantic_error(semantic_context, "extern struct must be followed by a struct type", import->options.struct_type_expr);
					break;
				}

				downcast<Datatype_Struct>(type)->is_extern_struct = true;
				break;
			}
			default: panic("The other options shouldn't generate a definition workload");
			}
		}

		break;
	}
	case Analysis_Workload_Type::FUNCTION_HEADER:
	{
		auto header_workload = downcast<Workload_Function_Header>(workload);
		Function_Progress* function_progress = header_workload->progress;

		// Handle infered function headers
		auto& function_node_info = function_progress->function_node->options.function;
		if (!function_node_info.signature.available)
		{
			// Expression_Type::Function will set the signature based on context
			Call_Signature* signature = function_progress->function->signature;
			assert(signature != nullptr, "Must have been set by expression before");
			header_workload->poly_header = nullptr;

			// Define all parameter symbols from derived signature
			auto symbol_table = function_progress->parameter_table;
			for (int i = 0; i < signature->parameters.size; i++)
			{
				auto& param = signature->parameters[i];
				Symbol* symbol = symbol_table_define_symbol(
					symbol_table, param.name, Symbol_Type::PARAMETER, upcast(function_progress->function_node), Symbol_Access_Level::INTERNAL,
					compilation_data
				);
				symbol->options.parameter.function = header_workload->progress;
				symbol->options.parameter.index_in_non_polymorphic_signature = i;
				symbol->options.parameter.index_in_polymorphic_signature = i;
			}

			// TODO: This could be done when encountering lambdas, and functions could just not have
			// a header progress... For infered functions the header workload does not do anything,
			//		it could be better to just not have a header workload at all...
			break;
		}
		auto signature_node = function_node_info.signature.value;

		// Create semantic context
		Semantic_Context local_context = semantic_context_make(
			compilation_data, workload, function_progress->parameter_table, Symbol_Access_Level::INTERNAL,
			analysis_pass_allocate(workload, upcast(signature_node), compilation_data),
			&scratch_arena
		);
		local_context.current_function = function_progress->function;
		Semantic_Context* semantic_context = &local_context;

		// Create analysis progress
		auto progress = header_workload->progress;
		ModTree_Function* function = header_workload->progress->function;
		auto& parameter_nodes = signature_node->options.signature_parameters;
		assert(function->function_type == ModTree_Function_Type::NORMAL, "");

		Poly_Header* poly_header = poly_header_analyse(
			parameter_nodes, semantic_context->current_symbol_table, function->name, semantic_context, progress, nullptr
		);
		header_workload->poly_header = poly_header;

		// Check if function is polymorphic
		function->signature = poly_header->signature;

		const bool is_polymorphic = poly_header->pattern_variables.size > 0 || poly_header->partial_pattern_count > 0;
		if (is_polymorphic)
		{
			// Switch progress type to polymorphic base
			progress->type = Polymorphic_Analysis_Type::POLYMORPHIC_BASE;
			progress->poly_function.base_progress = progress;
			progress->poly_function.poly_header = poly_header;

			// Set poly-values for base-analysis
			header_workload->base.active_pattern_variable_states = poly_header->base_analysis_states;
			header_workload->base.active_pattern_variable_states_origin = poly_header;
			progress->body_workload->base.poly_parent_workload = upcast(progress->header_workload); // So body workload can access poly-values

			// Update function + symbol
			function->is_runnable = false; // Polymorphic base cannot be runnable
			if (function->options.normal.symbol != 0) {
				function->options.normal.symbol->type = Symbol_Type::POLYMORPHIC_FUNCTION;
				function->options.normal.symbol->options.poly_function = progress->poly_function;
			}
		}

		break;
	}
	case Analysis_Workload_Type::FUNCTION_BODY:
	{
		auto body_workload = downcast<Workload_Function_Body>(workload);
		auto progress = body_workload->progress;
		auto function = body_workload->progress->function;
		auto& body_node = progress->function_node->options.function.body;

		body_workload->body_pass = analysis_pass_allocate(
			workload, (body_node.is_expression ? upcast(body_node.expr) : upcast(body_node.block)), compilation_data
		);
		Semantic_Context local_context = semantic_context_make(
			compilation_data, workload, progress->parameter_table, Symbol_Access_Level::INTERNAL, body_workload->body_pass,&scratch_arena
		);
		local_context.current_function = function;
		Semantic_Context* semantic_context = &local_context;

		if (body_node.is_expression)
		{
			Expression_Context context = expression_context_make_unspecified();
			if (function->signature->return_type().available) {
				context = expression_context_make_specific_type(function->signature->return_type().value);
			}
			semantic_analyser_analyse_expression_any(body_node.expr, context, semantic_context);
		}
		else
		{
			auto code_block = body_node.block;
			Control_Flow flow = semantic_analyser_analyse_block(code_block, semantic_context);
			if (flow != Control_Flow::RETURNS && function->signature->return_type().available)
			{
				Node_Section error_report_section = Node_Section::END_TOKEN;
				AST::Node* error_report_node = upcast(code_block);
				if (function->function_type == ModTree_Function_Type::NORMAL) {
					auto symbol = function->options.normal.symbol;
					if (symbol != nullptr && symbol->definition_node != nullptr) {
						error_report_node = symbol->definition_node;
						error_report_section = Node_Section::FIRST_TOKEN;
					}
				}
				log_semantic_error(semantic_context, "Function is missing a return statement", error_report_node, error_report_section);
			}
		}

		break;
	}
	case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
	{
		auto cluster = downcast<Workload_Function_Cluster_Compile>(workload);

		// Check if the cluster contains errors
		bool cluster_contains_error = false;
		for (int i = 0; i < cluster->functions.size; i++) {
			ModTree_Function* function = cluster->functions[i];
			if (function->contains_errors) {
				cluster_contains_error = true;
				break;
			}
			for (int j = 0; j < function->calls.size; j++) {
				ModTree_Function* calls = function->calls[j];
				if (calls->contains_errors) {
					cluster_contains_error = true;
				}
			}
		}

		// Compile/Set error for all functions in cluster
		for (int i = 0; i < cluster->functions.size; i++)
		{
			ModTree_Function* function = cluster->functions[i];
			if (cluster_contains_error) {
				function->is_runnable = false;
			}
			else {
				ir_generator_queue_function(compilation_data, function);
			}
		}
		break;
	}
	case Analysis_Workload_Type::STRUCT_POLYMORPHIC:
	{
		auto workload_poly = downcast<Workload_Structure_Polymorphic>(workload);
		auto body_workload = workload_poly->body_workload;
		auto& struct_node = body_workload->struct_node->options.structure;

		Semantic_Context local_context = semantic_context_make(
			compilation_data, workload, body_workload->symbol_table, body_workload->symbol_access_level,
			analysis_pass_allocate(workload, upcast(body_workload->struct_node), compilation_data),
			&scratch_arena
		);
		Semantic_Context* semantic_context = &local_context;

		// Create new symbol-table, define symbols and analyse parameters
		Symbol_Table* parameter_table = symbol_table_create_with_parent(
			semantic_context->current_symbol_table, Symbol_Access_Level::GLOBAL, compilation_data
		);
		semantic_context->symbol_access_level = Symbol_Access_Level::POLYMORPHIC;
		auto poly_header_info = poly_header_analyse(
			struct_node.parameters, parameter_table, workload_poly->body_workload->struct_type->name, semantic_context, 0, workload_poly
		);
		workload_poly->poly_header = poly_header_info;

		// Set poly-values for base-analysis
		workload_poly->base.active_pattern_variable_states = poly_header_info->base_analysis_states;
		workload_poly->base.active_pattern_variable_states_origin = poly_header_info;

		// Store/Set correct symbol table for base-analysis and instance analysis
		body_workload->base.poly_parent_workload = upcast(workload_poly); // So body workload can access poly-values
		body_workload->symbol_table = parameter_table;
		body_workload->symbol_access_level = Symbol_Access_Level::POLYMORPHIC;
		body_workload->analysis_pass = semantic_context->current_pass; // Base-pass body workload can use the same pass as header
		break;
	}
	case Analysis_Workload_Type::STRUCT_BODY:
	{
		auto workload_structure = downcast<Workload_Structure_Body>(workload);

		auto& struct_node = workload_structure->struct_node->options.structure;
		Datatype_Struct* structure = workload_structure->struct_type;

		if (workload_structure->analysis_pass == nullptr) {
			workload_structure->analysis_pass = analysis_pass_allocate(workload, upcast(workload_structure->struct_node), compilation_data);
		}

		Semantic_Context local_context = semantic_context_make(
			compilation_data, workload, workload_structure->symbol_table, workload_structure->symbol_access_level,
			workload_structure->analysis_pass, &scratch_arena
		);
		Semantic_Context* semantic_context = &local_context;

		// Analyse all members
		analyse_structure_member_nodes_recursive(structure, struct_node.members, semantic_context);
		type_system_finish_struct(type_system, structure);
		break;
	}
	case Analysis_Workload_Type::BAKE_ANALYSIS:
	{
		auto bake = downcast<Workload_Bake_Analysis>(workload);
		auto progress = bake->progress;
		auto function = progress->bake_function;
		auto bake_body =progress->bake_node->options.bake_body;

		Semantic_Context local_context = semantic_context_make(
			compilation_data, workload, progress->symbol_table, Symbol_Access_Level::GLOBAL,
			progress->analysis_pass, &scratch_arena
		);
		Semantic_Context* semantic_context = &local_context;
		semantic_context->current_function = function;

		if (bake_body.is_expression)
		{
			auto& bake_expr = bake_body.expr;
			semantic_context->symbol_access_level = Symbol_Access_Level::POLYMORPHIC;
			auto expression_context = expression_context_make_unspecified();
			if (bake->progress->result_type != 0) {
				expression_context = expression_context_make_specific_type(bake->progress->result_type);
			}
			bake->progress->result_type = semantic_analyser_analyse_expression_value(bake_expr, expression_context, semantic_context);
		}
		else
		{
			auto code_block = bake_body.block;
			auto flow = semantic_analyser_analyse_block(code_block, semantic_context, true);
			if (flow != Control_Flow::RETURNS) {
				log_semantic_error(semantic_context, "Missing return statement", AST::upcast(code_block), Node_Section::END_TOKEN);
			}
		}

		// Set bake function signature (I guess for ir-code generation)
		if (bake->progress->result_type == 0) {
			bake->progress->result_type = types.unknown_type;
			bake->progress->bake_function->contains_errors = true;
		}
		Call_Signature* signature = call_signature_create_empty();
		call_signature_add_return_type(signature, bake->progress->result_type, compilation_data);
		function->signature = call_signature_register(signature, compilation_data);

		break;
	}
	case Analysis_Workload_Type::BAKE_EXECUTION:
	{
		auto execute = downcast<Workload_Bake_Execution>(workload);
		auto progress = execute->progress;
		auto& ir_gen = compilation_data->ir_generator;

		Semantic_Context local_context = semantic_context_make(
			compilation_data, workload, progress->symbol_table, Symbol_Access_Level::GLOBAL,
			progress->analysis_pass, &scratch_arena
		);
		Semantic_Context* semantic_context = &local_context;
		semantic_context->current_function = progress->bake_function;

		auto bake_function = execute->progress->bake_function;

		// Check if function compilation succeeded
		if (bake_function->contains_errors || !bake_function->is_runnable) {
			progress->result = optional_make_failure<Upp_Constant>();
			return;
		}
		for (int i = 0; i < bake_function->calls.size; i++) {
			if (!bake_function->calls[i]->is_runnable) {
				bake_function->is_runnable = false;
				progress->result = optional_make_failure<Upp_Constant>();
				return;
			}
		}

		// Wait for result type size
		auto result_type = progress->bake_function->signature->return_type().value;
		type_wait_for_size_info_to_finish(result_type, semantic_context);

		// Compile
		ir_generator_queue_function(compilation_data, bake_function);
		ir_generator_generate_queued_items(compilation_data, true);

		// Execute
		auto& slots = compilation_data->function_slots;
		auto& slot = slots[bake_function->function_slot_index];
		int func_start_instr_index = slot.bytecode_start_instruction;

		Bytecode_Thread* thread = bytecode_thread_create(compilation_data, 10000);
		SCOPE_EXIT(bytecode_thread_destroy(thread));
		bytecode_thread_set_initial_state(thread, func_start_instr_index);
		while (true)
		{
			bytecode_thread_execute(thread);
			if (thread->exit_code.type == Exit_Code_Type::TYPE_INFO_WAITING_FOR_TYPE_FINISHED)
			{
				bool cyclic_dependency_occured = false;
				type_wait_for_size_info_to_finish(
					thread->waiting_for_type_finish_type, semantic_context, dependency_failure_info_make(&cyclic_dependency_occured)
				);
				if (cyclic_dependency_occured) {
					log_semantic_error(
						semantic_context, "Bake requires type_info which waits on bake, cyclic dependency!", progress->bake_node, Node_Section::KEYWORD
					);
					progress->result = optional_make_failure<Upp_Constant>();
					return;
				}
			}
			else if (thread->exit_code.type == Exit_Code_Type::SUCCESS) {
				break;
			}
			else {
				log_semantic_error(semantic_context, "Bake function did not return successfully", progress->bake_node, Node_Section::KEYWORD);
				log_error_info_exit_code(semantic_context, thread->exit_code);
				progress->result = optional_make_failure<Upp_Constant>();
				return;
			}
		}

		// Add result to constant pool
		void* value_ptr = thread->return_register;
		assert(progress->bake_function->signature->return_type().available, "Bake return type must have been stated at this point...");
		Constant_Pool_Result pool_result = constant_pool_add_constant(
			compilation_data->constant_pool, result_type, array_create_static<byte>((byte*)value_ptr, result_type->memory_info.value.size)
		);
		if (!pool_result.success) {
			log_semantic_error(semantic_context, "Couldn't serialize bake result", progress->bake_node, Node_Section::KEYWORD);
			log_error_info_constant_status(semantic_context, pool_result.options.error_message);
			progress->result = optional_make_failure<Upp_Constant>();
			return;
		}
		progress->result = optional_make_success(pool_result.options.constant);
		return;
	}
	default: panic("");
	}

	return;
}



// EXPRESSIONS UPDATES
void semantic_analyser_register_function_call(ModTree_Function* call_to, Semantic_Context* semantic_context)
{
	auto workload = semantic_context->current_workload;
	auto current_function = semantic_context->current_function;
	if (current_function != nullptr) {
		dynamic_array_push_back(&current_function->calls, call_to);
		dynamic_array_push_back(&call_to->called_from, current_function);
	}

	if (call_to->function_type == ModTree_Function_Type::EXTERN) {
		return;
	}
	assert(call_to->function_type == ModTree_Function_Type::NORMAL, "Cannot have a call to a bake function");
	auto progress = call_to->options.normal.progress;

	auto workload_executer = semantic_context->compilation_data->workload_executer;
	switch (workload->type)
	{
	case Analysis_Workload_Type::BAKE_ANALYSIS: {
		analysis_workload_add_dependency_internal(
			workload_executer,
			upcast(downcast<Workload_Bake_Analysis>(workload)->progress->execute_workload),
			upcast(progress->compile_workload)
		);
		break;
	}
	case Analysis_Workload_Type::FUNCTION_BODY: {
		analysis_workload_add_cluster_dependency(
			workload_executer,
			upcast(downcast<Workload_Function_Body>(workload)->progress->compile_workload),
			upcast(progress->compile_workload)
		);
		break;
	}
	default: return;
	}
}

Expression_Info analyse_symbol_as_expression(
	Symbol* symbol, Expression_Context context, AST::Symbol_Lookup* error_report_node, Semantic_Context* semantic_context)
{
	auto workload = semantic_context->current_workload;
	auto compilation_data = semantic_context->compilation_data;
	auto executer = compilation_data->workload_executer;
	auto type_system = compilation_data->type_system;
	auto& types = type_system->predefined_types;
	auto unknown_type = types.unknown_type;

	Expression_Info result = expression_info_make_empty(context, type_system);

	// Wait for initial symbol dependencies
	bool dependency_failed = false;
	auto failure_info = dependency_failure_info_make(&dependency_failed, error_report_node);
	switch (symbol->type)
	{
	case Symbol_Type::DEFINITION_UNFINISHED:
	{
		analysis_workload_add_dependency_internal(executer, workload, upcast(symbol->options.definition_workload), failure_info);
		break;
	}
	case Symbol_Type::FUNCTION:
	{
		auto fn = symbol->options.function;
		if (fn->function_type == ModTree_Function_Type::NORMAL) {
			analysis_workload_add_dependency_internal(executer, workload, upcast(fn->options.normal.progress->header_workload), failure_info);
		}
		break;
	}
	case Symbol_Type::POLYMORPHIC_FUNCTION:
	{
		auto progress = symbol->options.poly_function.base_progress;
		analysis_workload_add_dependency_internal(executer, workload, upcast(progress->header_workload), failure_info);
		break;
	}
	case Symbol_Type::DATATYPE:
	{
		Datatype* type = symbol->options.datatype;
		if (type->type == Datatype_Type::STRUCT)
		{
			auto struct_workload = downcast<Datatype_Struct>(type)->workload;
			if (struct_workload == 0) {
				assert(!type_size_is_unfinished(type), "");
				break;
			}

			// If it's a polymorphic struct, always wait for the header workload to finish
			if (struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
				analysis_workload_add_dependency_internal(executer, workload, upcast(struct_workload->polymorphic.base), failure_info);
			}
			else if (struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
				analysis_workload_add_dependency_internal(executer, workload, upcast(struct_workload->polymorphic.instance.parent), failure_info);
			}

			// Additionally, we want some workloads to wait until the size has been resolved
			switch (workload->type)
			{
			case Analysis_Workload_Type::DEFINITION: {
				analysis_workload_add_dependency_internal(executer, workload, upcast(struct_workload), failure_info);
				break;
			}
			case Analysis_Workload_Type::BAKE_ANALYSIS: {
				analysis_workload_add_dependency_internal(
					executer,
					upcast(downcast<Workload_Bake_Analysis>(workload)->progress->execute_workload),
					upcast(struct_workload));
				break;
			}
			case Analysis_Workload_Type::FUNCTION_BODY:
			case Analysis_Workload_Type::FUNCTION_HEADER: {
				auto progress = analysis_workload_try_get_function_progress(workload);
				analysis_workload_add_dependency_internal(executer, upcast(progress->compile_workload), upcast(struct_workload));
				break;
			}
			case Analysis_Workload_Type::STRUCT_BODY: break;
			case Analysis_Workload_Type::STRUCT_POLYMORPHIC: break;
			case Analysis_Workload_Type::OPERATOR_CONTEXT_CHANGE: break;
			default: panic("Invalid code path");
			}
		}
		break;
	}
	case Symbol_Type::PARAMETER:
	case Symbol_Type::PATTERN_VARIABLE: {
		// If i can access this symbol this means the header-analysis must have finished, or we are inside parameter analysis
		break;
	}
	default: break;
	}

	// Wait and check if dependency failed
	workload_executer_wait_for_dependency_resolution(semantic_context);
	if (dependency_failed) {
		semantic_analyser_set_error_flag(true, semantic_context);
		expression_info_set_error(&result, unknown_type, semantic_context);
		return result;
	}

	switch (symbol->type)
	{
	case Symbol_Type::ERROR_SYMBOL: {
		semantic_analyser_set_error_flag(true, semantic_context);
		expression_info_set_error(&result, unknown_type, semantic_context);
		return result;
	}
	case Symbol_Type::DEFINITION_UNFINISHED: {
		panic("Should not happen, we just waited on this workload to finish!");
	}
	case Symbol_Type::ALIAS_UNFINISHED:
	case Symbol_Type::ALIAS: {
		panic("Aliases should already be handled, this should only point to a valid symbol");
	}
	case Symbol_Type::HARDCODED_FUNCTION: {
		expression_info_set_hardcoded(&result, symbol->options.hardcoded);
		return result;
	}
	case Symbol_Type::FUNCTION: {
		semantic_analyser_register_function_call(symbol->options.function, semantic_context);
		expression_info_set_function(&result, symbol->options.function, semantic_context);
		return result;
	}
	case Symbol_Type::GLOBAL: {
		expression_info_set_value(&result, symbol->options.global->type, false);
		return result;
	}
	case Symbol_Type::DATATYPE:
	{
		// Note: Polymorphic structs are also stored as symbol_type::type, but
		//	they have a unique expression_result_type, so we need special handling here
		//	Maybe we could just add a special symbol type for this...
		auto datatype = symbol->options.datatype;
		if (datatype->type == Datatype_Type::STRUCT) {
			auto s = downcast<Datatype_Struct>(datatype);
			if (s->workload != 0) {
				if (s->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
					expression_info_set_polymorphic_struct(&result, s->workload->polymorphic.base);
					return result;
				}
			}
		}

		expression_info_set_datatype(&result, symbol->options.datatype, type_system);
		return result;
	}
	case Symbol_Type::VARIABLE: {
		assert(workload->type != Analysis_Workload_Type::FUNCTION_HEADER, "Function headers can never access variable symbols!");
		expression_info_set_value(&result, symbol->options.variable_type, false);
		return result;
	}
	case Symbol_Type::VARIABLE_UNDEFINED: {
		log_semantic_error(semantic_context, "Variable not defined at this point", upcast(error_report_node));
		expression_info_set_error(&result, unknown_type, semantic_context);
		return result;
	}
	case Symbol_Type::PARAMETER:
	{
		auto& param = symbol->options.parameter;
		auto workload = semantic_context->current_workload;

		// We should be in a function body workload
		auto progress = analysis_workload_try_get_function_progress(workload);
		assert(progress != 0, "We should be in function-body workload since normal parameters have internal symbol access");
		Datatype* param_type = nullptr;
		if (progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE)
		{
			param_type = progress->poly_function.poly_header->signature->parameters[param.index_in_polymorphic_signature].datatype;
			if (param_type->contains_pattern) {
				param_type = types.unknown_type;
			}
		}
		else {
			param_type = progress->function->signature->parameters[param.index_in_non_polymorphic_signature].datatype;
		}

		assert(!param_type->contains_pattern, "");
		expression_info_set_value(&result, param_type, true);
		return result;
	}
	case Symbol_Type::PATTERN_VARIABLE:
	{
		expression_info_set_to_pattern_variable(&result, symbol->options.pattern_variable, true, semantic_context);
		return result;
	}
	case Symbol_Type::COMPTIME_VALUE: {
		expression_info_set_constant(&result, symbol->options.constant);
		return result;
	}
	case Symbol_Type::MODULE: {
		log_semantic_error(semantic_context, "Module not valid as argument_expression result", upcast(error_report_node));
		log_error_info_symbol(semantic_context, symbol);
		expression_info_set_error(&result, unknown_type, semantic_context);
		return result;
	}
	case Symbol_Type::POLYMORPHIC_FUNCTION: {
		expression_info_set_polymorphic_function(&result, symbol->options.poly_function);
		return result;
	}
	default: panic("HEY");
	}

	return result;
}


union Literal_Dummy
{
   	void* value_nullptr;
   	Upp_String value_string;
   	u8 value_u8;
   	u16 value_u16;
   	u32 value_u32;
   	u64 value_u64;
   	i8  value_i8;
   	i16 value_i16;
   	i32 value_i32;
   	i64 value_i64;
   	f32 value_f32;
   	f64 value_f64;
};

#define SET_ACTIVE_EXPR_INFO(new_info)\
    Expression_Info* _backup_info = semantic_context->current_expression; \
    semantic_context->current_expression = new_info; \
    SCOPE_EXIT(semantic_context->current_expression = _backup_info);

bool expression_is_auto_expression(AST::Expression* expression, Compilation_Data* compilation_data)
{
	auto& ids = compilation_data->compiler->identifier_pool.predefined_ids;

	auto type = expression->type;
	if (type == AST::Expression_Type::CAST) 
	{
		// cast signature: cast(value, to, from, option)
		auto args = expression->options.cast.call_node->arguments;
		// Check if from is specified
		bool named_args_started = false;
		for (int i = 0; i < args.size; i++) {
			auto arg = args[i];
			if (arg->name.available) 
			{
				named_args_started = true;
				if (arg->name.value == ids.from) {
					return false;
				}
			}
			if (!named_args_started && i == 2) {
				return false;
			}
		}
	}

	return type == AST::Expression_Type::AUTO_ENUM ||
		(type == AST::Expression_Type::ARRAY_INITIALIZER && !expression->options.array_initializer.type_expr.available) ||
		(type == AST::Expression_Type::STRUCT_INITIALIZER && !expression->options.struct_initializer.type_expr.available) ||
		(type == AST::Expression_Type::LITERAL_READ &&
			(expression->options.literal_read.type == Literal_Type::NULL_VAL ||
				expression->options.literal_read.type == Literal_Type::INTEGER ||
				expression->options.literal_read.type == Literal_Type::FLOAT_VAL));
}

bool expression_is_auto_expression_with_preferred_type(AST::Expression* expression)
{
	auto type = expression->type;
	return (type == AST::Expression_Type::LITERAL_READ &&
		(expression->options.literal_read.type == Literal_Type::INTEGER ||
			expression->options.literal_read.type == Literal_Type::FLOAT_VAL));
}

// Allowed direction determines if initializers are allowed to contain subtype and base-type initializers (value=0), 
// or only subtype (value=1) or base_type(value=-1)
// return the final_subtype
Datatype_Struct* analyse_member_initializer_recursive(
	AST::Call_Node* call_node, Datatype_Struct* structure, int allowed_direction, Semantic_Context* semantic_context)
{
	auto compilation_data = semantic_context->compilation_data;
	auto& types = compilation_data->type_system->predefined_types;

	// Create call_info
	Call_Info* call_info = get_info(call_node, semantic_context, true);
	*call_info = call_info_make_from_call_node(call_node, call_origin_make(structure, semantic_context), &semantic_context->compilation_data->arena);
	call_info->instanciation_data.initializer_info.subtype_valid = allowed_direction != -1;
	call_info->instanciation_data.initializer_info.supertype_valid = allowed_direction != 1;
	call_info->instanciated = true;

	// Match arguments to struct members
	arguments_match_to_parameters(*call_info, semantic_context);
	call_info_analyse_all_arguments(call_info, semantic_context, false, false);

	auto helper_analyse_subtype_init_unknown = [&](AST::Subtype_Initializer* subtype_init)
	{
		Call_Info* call_info = get_info(subtype_init->call_node, semantic_context, true);
		*call_info = call_info_make_error(subtype_init->call_node, compilation_data);
		call_info_analyse_all_arguments(call_info, false, semantic_context, true);
	};

	// Go through subtype-initializers and call function recursively
	bool subtype_initializer_found = false;
	bool supertype_initializer_found = false;
	Datatype_Struct* return_value = structure;
	for (int i = 0; i < call_node->subtype_initializers.size; i++)
	{
		auto& init_node = call_node->subtype_initializers[i];

		// Check if it's a subtype or supertype initializer
		int subtype_index = -1;
		bool is_supertype_init = false;
		if (init_node->name.available)
		{
			for (int j = 0; j < structure->subtypes.size; j++) {
				auto sub_content = structure->subtypes[j];
				if (sub_content->name == init_node->name.value) {
					subtype_index = j;
					break;
				}
			}

			// Check if it's supertype name
			if (subtype_index == -1 && structure->parent_struct != nullptr) {
				if (structure->parent_struct->name == init_node->name.value) {
					is_supertype_init = true;
				}
			}
		}
		else {
			is_supertype_init = true;
		}

		if (is_supertype_init)
		{
			if (structure->parent_struct == nullptr)
			{
				log_semantic_error(
					semantic_context,
					"Base-Type initializer invalid in this context, struct type is already the base type", upcast(init_node), Node_Section::FIRST_TOKEN
				);
				helper_analyse_subtype_init_unknown(init_node);
				break;
			}
			else if (allowed_direction == 1 || supertype_initializer_found) {
				log_semantic_error(
					semantic_context,
					"Cannot re-specify base-type members, this has already been done in this struct-initializer", upcast(init_node), Node_Section::FIRST_TOKEN
				);
				helper_analyse_subtype_init_unknown(init_node);
				break;
			}
			else {
				analyse_member_initializer_recursive(init_node->call_node, structure->parent_struct, -1, semantic_context);
				supertype_initializer_found = true;
				break;
			}
		}
		else if (subtype_index != -1)
		{
			// Analyse subtype
			if (subtype_initializer_found) {
				log_semantic_error(
					semantic_context,
					"Cannot re-specify subtype, this has already been done in this struct-initializer", upcast(init_node), Node_Section::FIRST_TOKEN
				);
				helper_analyse_subtype_init_unknown(init_node);
				break;
			}
			else if (allowed_direction == -1) {
				log_semantic_error(
					semantic_context,
					"Cannot re-specify subtype, this has already been done on another struct-initializer level", upcast(init_node), Node_Section::FIRST_TOKEN
				);
				helper_analyse_subtype_init_unknown(init_node);
				break;
			}
			else
			{
				return_value = analyse_member_initializer_recursive(init_node->call_node, structure->subtypes[subtype_index], 1, semantic_context);
				subtype_initializer_found = true;
			}
		}
		else {
			log_semantic_error(semantic_context, "Name is neither supertype nor subtype!", upcast(init_node), Node_Section::IDENTIFIER);
			helper_analyse_subtype_init_unknown(init_node);
		}
	}

	// Check for further errors
	bool found_ignore_symbol = call_node->uninitialized_tokens.size > 0;
	if (!found_ignore_symbol)
	{
		if (structure->parent_struct != 0 && !supertype_initializer_found && allowed_direction == 0) 
		{
			bool parent_has_members = false;
			Datatype_Struct* parent = structure->parent_struct;
			while (parent != 0) {
				if (parent->members.size != 0) {
					parent_has_members = true;
					break;
				}
				parent = parent->parent_struct;
			}
			if (parent_has_members) {
				log_semantic_error(
					semantic_context,
					"Base-Type members were not specified, use base-type initializer '. = {}' for this!", 
					upcast(call_node), Node_Section::ENCLOSURE
				);
			}
		}
		if (structure->subtypes.size > 0 && !subtype_initializer_found && allowed_direction == 0) {
			log_semantic_error(
				semantic_context, 
				"Subtype was not specified, use subtype initializer '.SubName = {}' for this!", 
				upcast(call_node), Node_Section::ENCLOSURE
			);
		}
	}

	call_info->origin.options.structure = return_value; // Return value should point to final subtype, as ir-code generates the tags with this
	return return_value;
}

// The difference to just using expression_context(usize) is that this applies integer casts automatically
void analyse_index_accept_all_ints_as_usize(AST::Expression* expr, Semantic_Context* semantic_context, bool allow_poly_pattern = false)
{
	auto& types = semantic_context->compilation_data->type_system->predefined_types;
	if (expr->type == AST::Expression_Type::LITERAL_READ && expr->options.literal_read.type == Literal_Type::INTEGER) {
		semantic_analyser_analyse_expression_value(expr, expression_context_make_specific_type(upcast(types.usize)), semantic_context);
		return;
	}

	Datatype* result_type = semantic_analyser_analyse_expression_value(expr, expression_context_make_unspecified(), false, allow_poly_pattern);
	if (allow_poly_pattern) {
		auto info = get_info(expr, semantic_context);
		if (info->result_type == Expression_Result_Type::POLYMORPHIC_PATTERN) {
			return;
		}
	}
	if (datatype_is_unknown(result_type)) return;
	if (types_are_equal(result_type, upcast(types.usize))) return;

	auto info = get_info(expr, semantic_context);
	RESTORE_ON_SCOPE_EXIT(semantic_context->current_expression, info);
	assert(info->cast_info.cast_type == Cast_Type::NO_CAST, "without context no cast should be applied");

	// Cast all integers to usize
	bool is_auto_cast = true;
	if (result_type->type == Datatype_Type::PRIMITIVE) {
		auto primitive = downcast<Datatype_Primitive>(result_type);
		is_auto_cast = primitive->primitive_class != Primitive_Class::INTEGER;
	}

	if (!expression_apply_cast_if_possible(expr, upcast(types.usize), is_auto_cast, nullptr, semantic_context)) {
		log_semantic_error(semantic_context, "Expected index type (integer or value castable to usize)", expr);
		info->cast_info = cast_info_make_simple(upcast(types.usize), Cast_Type::INVALID);
	}
}

Expression_Info* semantic_analyser_analyse_expression_internal(AST::Expression* expr, Expression_Context context, Semantic_Context* semantic_context)
{
	Compilation_Data* compilation_data = semantic_context->compilation_data;
	auto type_system = compilation_data->type_system;
	auto& types = type_system->predefined_types;

	// Initialize expression info
	auto info = get_info(expr, semantic_context, true);
	SET_ACTIVE_EXPR_INFO(info);
	*info = expression_info_make_empty(context, type_system);

#define EXIT_VALUE(val, is_temporary) expression_info_set_value(info, val, is_temporary); return info;
#define EXIT_TYPE(type) expression_info_set_datatype(info, type, type_system); return info;
#define EXIT_ERROR(type) expression_info_set_error(info, type, semantic_context); return info;
#define EXIT_HARDCODED(hardcoded) expression_info_set_hardcoded(info, hardcoded); return info;
#define EXIT_FUNCTION(function) expression_info_set_function(info, function, semantic_context); return info;
#define EXIT_TYPE_OR_POLY(type) \
	Datatype* _result_type = type; \
	if (_result_type->contains_pattern) { \
		expression_info_set_polymorphic_pattern(info, _result_type, type_system); \
	} \
	else { \
		expression_info_set_datatype(info, _result_type, type_system); \
	} \
	return info;

	switch (expr->type)
	{
	case AST::Expression_Type::ERROR_EXPR: {
		// Error due to parsing, dont log error message because we already have parse error messages
		semantic_analyser_set_error_flag(false, semantic_context);
		EXIT_ERROR(types.unknown_type);
	}
	case AST::Expression_Type::FUNCTION_CALL:
	{
		auto& call_node = expr->options.call;
		Datatype* expected_return_type = nullptr;
		if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
			expected_return_type = context.datatype;
		}
		Call_Info* call_info = overloading_analyse_call_expression_and_resolve_overloads(semantic_context, expr, expected_return_type);
		info->specifics.call_info = call_info;

		auto helper_set_info_to_call_return_type = [&](bool error_occured)
		{
			if (error_occured) {
				call_info_analyse_all_arguments(call_info, true, semantic_context, true);
			}

			auto signature = call_info->origin.signature;
			if (call_info->origin.type == Call_Origin_Type::POLY_FUNCTION && call_info->instanciated) {
				signature = call_info->instanciation_data.function->signature;
			}
			if (signature->return_type_index == -1) {
				if (error_occured) {
					expression_info_set_error(info, types.unknown_type, semantic_context);
				}
				else {
					expression_info_set_no_value(info);
				}
				return;
			}
			Datatype* return_type = signature->return_type().value;
			if (return_type->contains_pattern) {
				expression_info_set_error(info, types.unknown_type, semantic_context);
			}
			else {
				expression_info_set_value(info, return_type, true);
			}
		};

		if (!call_info->argument_matching_success)
		{
			helper_set_info_to_call_return_type(true);
			return info;
		}

		// Store expected return type in parameter_values (Used for polymorphic instanciation)
		if (call_info->origin.signature->return_type_index != -1 && context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
			call_info->parameter_values[call_info->origin.signature->return_type_index] = parameter_value_make_datatype_known(context.datatype);
		}

		// Handle hardcoded and polymorphic functions
		switch (call_info->origin.type)
		{
		case Call_Origin_Type::HARDCODED:
		{
			// Handle specific hardcoded-types
			switch (call_info->origin.options.hardcoded)
			{
			case Hardcoded_Type::TYPE_OF:
			{
				auto& param_value = call_info->parameter_values[0];
				assert(call_info->parameter_values.size == 2, "With return type we should have 2 parameters");
				analyse_parameter_value_if_not_already_done(call_info, &param_value, semantic_context, expression_context_make_unspecified(), false);
				EXIT_TYPE(parameter_value_get_datatype(param_value, call_info, semantic_context));
			}
			case Hardcoded_Type::SIZE_OF:
			case Hardcoded_Type::ALIGN_OF:
			{
				bool is_size_of = call_info->origin.options.hardcoded == Hardcoded_Type::SIZE_OF;
				assert(call_info->parameter_values.size == 2, "");
				auto param_value = &call_info->parameter_values[0];

				analyse_parameter_value_if_not_already_done(call_info, param_value, semantic_context, expression_context_make_specific_type(types.type_handle));
				Datatype* expr_type = parameter_value_get_datatype(*param_value, call_info, semantic_context);
				if (datatype_is_unknown(expr_type)) {
					if (is_size_of) {
						expression_info_set_constant_usize(info, 1, semantic_context);
					}
					else {
						expression_info_set_constant_u32(info, 1, semantic_context);
					}
					return info;
				}

				assert(param_value->value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "Should be the case in function call!");
				auto value_expr = call_info->argument_infos[param_value->options.argument_index].expression;
				auto result = expression_calculate_comptime_value(value_expr, "size_of/align_of requires comptime type-handle", semantic_context);
				if (!result.available) {
					if (is_size_of) {
						expression_info_set_constant_usize(info, 1, semantic_context);
					}
					else {
						expression_info_set_constant_u32(info, 1, semantic_context);
					}
					return info;
				}

				Upp_Type_Handle handle = upp_constant_to_value<Upp_Type_Handle>(result.value);
				auto& types_array = type_system->types;
				if (handle.index >= (u32)types_array.size)
				{
					log_semantic_error(semantic_context, "Invalid type-handle value", value_expr);
					if (is_size_of) {
						expression_info_set_constant_usize(info, 1, semantic_context);
					}
					else {
						expression_info_set_constant_u32(info, 1, semantic_context);
					}
					return info;
				}

				auto type = types_array[handle.index];
				type_wait_for_size_info_to_finish(type, semantic_context);
				auto& memory = type->memory_info.value;
				if (is_size_of) {
					expression_info_set_constant_usize(info, memory.size, semantic_context);
				}
				else {
					expression_info_set_constant_u32(info, memory.alignment, semantic_context);
				}
				return info;
			}
			case Hardcoded_Type::RETURN_TYPE:
			{
				assert(call_info->parameter_values.size == 1, "");
				if (semantic_context->current_workload->type != Analysis_Workload_Type::FUNCTION_BODY) {
					log_semantic_error(semantic_context, "return_type() function needs to be called inside function_body", expr, Node_Section::FIRST_TOKEN);
					EXIT_ERROR(types.unknown_type);
				}
				Call_Signature* signature = downcast<Workload_Function_Body>(semantic_context->current_workload)->progress->function->signature;
				if (signature->return_type_index == -1) {
					log_semantic_error(semantic_context, "return_type() function needs to have a return type", expr, Node_Section::FIRST_TOKEN);
					EXIT_ERROR(types.unknown_type);
				}
				auto return_type = signature->return_type().value;
				if (return_type->contains_pattern) {
					// This should mean we are in base analysis
					EXIT_TYPE(types.unknown_type);
				}
				EXIT_TYPE(return_type);
			}
			case Hardcoded_Type::STRUCT_TAG:
			{
				assert(call_info->parameter_values.size == 2, "");
				auto& param_value = call_info->parameter_values[0];
				analyse_parameter_value_if_not_already_done(call_info, &param_value, semantic_context, expression_context_make_dereference());
				bool is_temporary = false;
				Datatype* datatype = parameter_value_get_datatype(param_value, call_info, semantic_context, &is_temporary);

				// Check if type is a struct
				if (datatype->type != Datatype_Type::STRUCT)
				{
					log_semantic_error(semantic_context, "struct_tag() function expects a structure as parameter", expr, Node_Section::ENCLOSURE);
					log_error_info_given_type(semantic_context, datatype);
					EXIT_ERROR(types.unknown_type);
				}
				Datatype_Struct* structure = downcast<Datatype_Struct>(datatype);

				// Check tag access
				if (structure->subtypes.size == 0)
				{
					log_semantic_error(semantic_context, "struct_tag() function expects a structure as parameter", expr, Node_Section::ENCLOSURE);
					log_error_info_given_type(semantic_context, datatype);
					EXIT_ERROR(types.unknown_type);
				}
				EXIT_VALUE(structure->tag_member.type, is_temporary);
			}
			case Hardcoded_Type::BITWISE_NOT:
			{
				assert(call_info->parameter_values.size == 2, "");

				auto& param_value = call_info->parameter_values[0];
				auto arg_expr = call_info->argument_infos[param_value.options.argument_index].expression;
				analyse_parameter_value_if_not_already_done(call_info, &param_value, semantic_context, expression_context_make_dereference());
				Datatype* type = parameter_value_get_datatype(param_value, call_info, semantic_context);

				bool type_valid = type->type == Datatype_Type::PRIMITIVE;
				Datatype_Primitive* primitive = nullptr;
				if (type_valid) {
					primitive = downcast<Datatype_Primitive>(type);
					type_valid = primitive->primitive_class == Primitive_Class::INTEGER;
				}
				if (!type_valid) {
					log_semantic_error(semantic_context, "Type for bitwise not must be an integer", arg_expr);
					log_error_info_given_type(semantic_context, type);
					EXIT_VALUE(upcast(types.i32_type), true);
				}
				call_info->instanciation_data.bitwise_primitive_type = primitive;
				call_info->instanciated = true;

				EXIT_VALUE(type, true);
			}
			case Hardcoded_Type::BITWISE_AND:
			case Hardcoded_Type::BITWISE_OR:
			case Hardcoded_Type::BITWISE_XOR:
			case Hardcoded_Type::BITWISE_SHIFT_LEFT:
			case Hardcoded_Type::BITWISE_SHIFT_RIGHT:
			{
				assert(call_info->parameter_values.size == 3, "");
				call_info->instanciation_data.bitwise_primitive_type = types.i32_type;

				// Analyse first expression
				auto& param_values = call_info->parameter_values;
				auto expr_a = call_info->argument_infos[param_values[0].options.argument_index].expression;
				analyse_parameter_value_if_not_already_done(call_info, &param_values[0], semantic_context, expression_context_make_dereference());
				Datatype* type_a = parameter_value_get_datatype(param_values[0], call_info, semantic_context);

				bool type_valid = type_a->type == Datatype_Type::PRIMITIVE;
				Datatype_Primitive* primitive = nullptr;
				if (type_valid) {
					primitive = downcast<Datatype_Primitive>(type_a);
					type_valid = primitive->primitive_class == Primitive_Class::INTEGER;
				}
				if (!type_valid) {
					log_semantic_error(semantic_context, "Type for bitwise operation must be an integer", expr_a);
					log_error_info_given_type(semantic_context, type_a);
					call_info_analyse_all_arguments(call_info, false, semantic_context, true);
					EXIT_VALUE(upcast(types.i32_type), true);
				}
				call_info->instanciation_data.bitwise_primitive_type = primitive;
				call_info->instanciated = true;

				auto expr_b = call_info->argument_infos[param_values[1].options.argument_index].expression;
				analyse_parameter_value_if_not_already_done(call_info, &param_values[1], semantic_context, expression_context_make_specific_type(type_a));

				EXIT_VALUE(type_a, true);
			}
			}

			// If we are here the code-generation stages will handle the call
			break;
		}
		case Call_Origin_Type::POLY_FUNCTION:
		case Call_Origin_Type::POLY_STRUCT:
		{
			// Instanciate
			Poly_Instance* instance = poly_header_instanciate(call_info, upcast(expr), Node_Section::ENCLOSURE, semantic_context);
			if (instance == nullptr) { // Errors should have already been reported at this point
				helper_set_info_to_call_return_type(true);
				return info;
			}

			switch (instance->type)
			{
			case Poly_Instance_Type::FUNCTION:
			{
				semantic_analyser_register_function_call(instance->options.function_instance->function, semantic_context);
				helper_set_info_to_call_return_type(false);
				return info;
			}
			case Poly_Instance_Type::STRUCTURE: {
				EXIT_TYPE(upcast(instance->options.struct_instance->struct_type));
			}
			case Poly_Instance_Type::STRUCT_PATTERN: {
				expression_info_set_polymorphic_pattern(info, upcast(instance->options.struct_pattern), type_system);
				return info;
			}
			default: panic("");
			}
			panic("Should not happen");
			break;
		}
		}

		// Analyse all arguments
		call_info_analyse_all_arguments(call_info, false, semantic_context, true);
		helper_set_info_to_call_return_type(false);
		return info;
	}
	case AST::Expression_Type::INSTANCIATE:
	{
		// Instanciate works by doing a normal poly_header_instanciate, 
		//	but only comptime parameters and infered parameters are required, whereas the other's must not be set
		auto& instanciate = expr->options.instanciate;

		// Analyse return type first, as this is used in overload resolution
		Datatype* expected_return_type = nullptr;
		if (instanciate.return_type.available) {
			expected_return_type = semantic_analyser_analyse_expression_type(instanciate.return_type.value, false);
		}

		// Analyse path-lookup, overload resolving and parameter matching
		Call_Info* call_info = overloading_analyse_call_expression_and_resolve_overloads(semantic_context, expr, expected_return_type);
		if (call_info->origin.type != Call_Origin_Type::POLY_FUNCTION) 
		{
			if (call_info->origin.type != Call_Origin_Type::ERROR_OCCURED) {
				log_semantic_error(semantic_context, "#instanciate expects a call to a polymorphic function", expr, Node_Section::FIRST_TOKEN);
			}
			call_info_analyse_all_arguments(call_info, false, semantic_context, true);
			EXIT_ERROR(types.unknown_type);
		}
		if (!call_info->argument_matching_success) {
			call_info_analyse_all_arguments(call_info, false, semantic_context, true);
			EXIT_ERROR(types.unknown_type);
		}

		// Store expected return type in parameter_values (Used for polymorphic instanciation)
		if (expected_return_type != nullptr && call_info->origin.signature->return_type_index != -1)
		{
			if (call_info->origin.signature->return_type_index != -1)
			{
				call_info->parameter_values[call_info->origin.signature->return_type_index] = 
					parameter_value_make_datatype_known(expected_return_type);
			}
			else
			{
				log_semantic_error(
					semantic_context,
					"#instanciate return type was specified, but poly-function does not have a return type",
					expr, Node_Section::FIRST_TOKEN
				);
			}
		}

		// Analyse all normal parameters as types in instanciate, and comptime params as comptimes
		Poly_Header* poly_header = call_info->origin.options.poly_function.poly_header;
		bool encountered_unknown = false;
		for (int i = 0; i < call_info->argument_infos.size; i++)
		{
			auto& arg_info = call_info->argument_infos[i];
			auto& param_info = poly_header->signature->parameters[arg_info.parameter_index];
			auto& param_value = call_info->parameter_values[arg_info.parameter_index];

			// Analyse comptime values normally (Done in instanciate)
			if (param_info.comptime_variable_index != -1) continue;

			assert(param_value.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "Shouldn't be different");
			// Note: Overloading should not have analysed these arguments, so this should work without any re-analysis issues
			Datatype* datatype = semantic_analyser_analyse_expression_type(arg_info.expression, false);
			if (datatype_is_unknown(datatype)) {
				encountered_unknown = true;
			}
			param_value = parameter_value_make_datatype_known(datatype);
		}
		if (encountered_unknown) {
			call_info_analyse_all_arguments(call_info, false, semantic_context, true);
			semantic_analyser_set_error_flag(true, semantic_context);
			EXIT_ERROR(types.unknown_type);
		}

		// Instanciate poly-header
		auto instance = poly_header_instanciate(call_info, upcast(expr), Node_Section::FIRST_TOKEN, semantic_context);
		if (instance == nullptr) {
			EXIT_ERROR(types.unknown_type);
		}
		assert(instance->type == Poly_Instance_Type::FUNCTION, "Poly-function should only instanciate to function");
		EXIT_FUNCTION(instance->options.function_instance->function);
	}
	case AST::Expression_Type::GET_OVERLOAD:
	{
		// Get overload works by specifying the types of certain parameters.
		//	It returns the function where all specified param-types match
		// e.g. #get_overload add(a=int, b=int)
		// Note: argument names are required, but types are not required

		auto& get_overload = expr->options.get_overload;

		Arena* scratch_arena = semantic_context->scratch_arena;
		auto checkpoint = scratch_arena->make_checkpoint();
		SCOPE_EXIT(scratch_arena->rewind_to_checkpoint(checkpoint));

		// Analyse arguments
		auto& args = get_overload.arguments;
		Array<Datatype*> arg_types = scratch_arena->allocate_array<Datatype*>(args.size); // Null if no datatype specified
		bool encountered_unknown = false;
		for (int i = 0; i < args.size; i++)
		{
			auto arg = args[i];
			if (arg->type_expr.available) {
				// TODO: Maybe we want to allow poly-patterns, which would allow Node(_) as arg-types,
				//	     but then we need matching between two poly-patterns...
				arg_types[i] = semantic_analyser_analyse_expression_type(arg->type_expr.value, false);
				if (datatype_is_unknown(arg_types[i])) {
					encountered_unknown = true;
					arg_types[i] = nullptr;
				}
			}
			else {
				arg_types[i] = nullptr;
			}
		}

		// Find all overload symbols
		DynArray<Symbol*> symbols = DynArray<Symbol*>::create(scratch_arena);
		// Overload.path is not avaialble only if there was a parsing error, so we don't need error-reporting here
		if (get_overload.path.available) {
			symbols = path_lookup_resolve(get_overload.path.value, scratch_arena, true, semantic_context);
		}
		else {
			semantic_analyser_set_error_flag(true, semantic_context);
			EXIT_ERROR(types.unknown_type);
		}
		if (symbols.size == 0) {
			log_semantic_error(semantic_context, "Could not find symbol for given path", upcast(get_overload.path.value));
			EXIT_ERROR(types.unknown_type);
		}

		// Filter overloads (And prefer overloads where all parameters are specified)
		Expression_Info result_info = expression_info_make_empty(expression_context_make_unspecified(), type_system);
		bool all_parameter_match_with_return_found = false;
		bool all_parameter_match_found = false;
		for (int i = 0; i < symbols.size; i++)
		{
			auto symbol = symbols[i];
			Expression_Info info = analyse_symbol_as_expression(
				symbol, expression_context_make_unspecified(), get_overload.path.value->last, semantic_context
			);

			// Get signature
			bool remove_symbol = false;
			Call_Signature* signature = nullptr;
			if (info.result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION) {
				signature = info.options.poly_function.poly_header->signature;
				remove_symbol = remove_symbol || !get_overload.is_poly;
			}
			else if (info.result_type == Expression_Result_Type::FUNCTION) {
				signature = info.options.function->signature;
				remove_symbol = remove_symbol || get_overload.is_poly;
			}
			else {
				// #get_overload only works for polymorphic function and functions
				remove_symbol = true;
			}

			// Compare given argument types with parameter types
			int normal_parameters_matched = 0;
			bool return_type_matched = false;
			for (int j = 0; j < args.size && !remove_symbol; j++)
			{
				String* name = args[j]->id;
				Datatype* arg_type = arg_types[j];

				// Find parameter with matching name
				int param_index = -1;
				for (int k = 0; k < signature->parameters.size; k++) {
					auto& param = signature->parameters[k];
					if (param.name == name) {
						param_index = k;
						break;
					}
				}

				// Remove overload if it doesn't contain a parameter with given name
				if (param_index == -1) {
					remove_symbol = true;
					break;
				}

				// Arg-type may be null in #get_overload (E.g. type is not specified)
				if (arg_type == nullptr) {
					normal_parameters_matched += 1;
					continue;
				}

				auto& param_info = signature->parameters[param_index];
				auto param_type = param_info.datatype;
				if (param_info.requires_named_addressing) { // Cannot check pattern-variable types
					remove_symbol = true;
					break;
				}
				if (param_index == signature->return_type_index) {
					return_type_matched = true;
				}

				if (param_type->contains_pattern) {
					auto checkpoint = scratch_arena->make_checkpoint();
					Pattern_Match_Result match_result = pattern_match_result_make_empty(scratch_arena);
					bool matches = pattern_type_match_against(match_result, 0, param_type, arg_type, semantic_context);
					if (matches) {
						matches = pattern_match_result_check_constraints_pairwise(match_result);
					}
					scratch_arena->rewind_to_checkpoint(checkpoint);

					remove_symbol = remove_symbol || !matches;
				}
				else {
					remove_symbol = remove_symbol || !types_are_equal(param_type, arg_type);
				}
				normal_parameters_matched += 1;
			}

			// Check if all parameters were matched
			bool all_parameters_matched = false;
			if (!remove_symbol && signature != nullptr)
			{
				int signature_normal_parameter_count = 0;
				for (int i = 0; i < signature->parameters.size; i++) {
					auto& param_info = signature->parameters[i];
					if (i == signature->return_type_index || param_info.requires_named_addressing) continue;
					signature_normal_parameter_count += 1;
				}
				all_parameters_matched = normal_parameters_matched == signature_normal_parameter_count;
			}

			bool remove = remove_symbol;
			if (all_parameter_match_with_return_found && !(all_parameters_matched && return_type_matched)) {
				remove = true;
			}
			if (all_parameter_match_found && !all_parameters_matched) {
				remove = true;
			}

			if (remove) {
				symbols.swap_remove(i);
				i -= 1;
				continue;
			}

			// Store result
			result_info = info;

			// Remove all previous results if it is the first time we find an all_parameter match
			bool remove_previous = false;
			if (all_parameters_matched && return_type_matched && !all_parameter_match_with_return_found) {
				remove_previous = true;
			}
			else if (all_parameters_matched && !all_parameter_match_found) {
				remove_previous = true;
			}

			if (all_parameters_matched) {
				all_parameter_match_found = true;
			}
			if (all_parameters_matched && return_type_matched) {
				all_parameter_match_with_return_found = true;
			}
			if (remove_previous) {
				symbols.remove_range_ordered(0, i);
				i = 0;
			}
		}

		if (symbols.size == 0) {
			log_semantic_error(semantic_context, "#get_overload failed, no symbol matched given parameters and types", upcast(get_overload.path.value));
			EXIT_ERROR(types.unknown_type);
		}
		else if (symbols.size > 1)
		{
			if (encountered_unknown) {
				semantic_analyser_set_error_flag(true, semantic_context);
				EXIT_ERROR(types.unknown_type);
			}
			log_semantic_error(semantic_context, "#get_overload failed to distinguish symbols with given parameters/types", upcast(get_overload.path.value));
			for (int i = 0; i < symbols.size; i++) {
				log_error_info_symbol(semantic_context, symbols[i]);
			}
			EXIT_ERROR(types.unknown_type);
		}

		path_lookup_set_result_symbol(get_overload.path.value, symbols[0], semantic_context);
		auto symbol = symbols[0];
		*info = result_info;
		return info;
	}
	case AST::Expression_Type::PATH_LOOKUP:
	{
		auto path = expr->options.path_lookup;

		// Resolve symbol
		Symbol* symbol = path_lookup_resolve_to_single_symbol(path, false, semantic_context, true);
		assert(symbol != 0, "In error cases this should be set to error, never 0!");

		// Analyse symbol
		*info = analyse_symbol_as_expression(symbol, context, path->last, semantic_context);
		return info;
	}
	case AST::Expression_Type::PATTERN_VARIABLE:
	{
		auto workload = semantic_context->current_workload;
		Pattern_Variable** variable_opt = nullptr;
		if (workload != nullptr) {
			variable_opt = hashtable_find_element(&compilation_data->pattern_variable_expression_mapping, expr);
		}
		if (variable_opt == 0) {
			log_semantic_error(semantic_context, "Implicit polymorphic parameter only valid in function header!", expr);
			EXIT_ERROR(types.unknown_type);
		}

		Pattern_Variable* variable = *variable_opt;
		assert(!variable->pattern_variable_type->is_reference,
			"We can never be the reference if we are at the definition argument_expression, e.g. $T is not a symbol-read like T");

		// If value is already set, return the value (Should happen in header-re-analysis)
		expression_info_set_to_pattern_variable(info, variable, false, semantic_context);
		return info;
	}
	case AST::Expression_Type::CAST:
	{
		AST::Call_Node* call_node = expr->options.cast.call_node;
		Call_Info* call_info = get_info(call_node, semantic_context, true);
		*call_info = call_info_make_from_call_node(call_node, call_origin_make_cast(compilation_data), &compilation_data->arena);
		if (!arguments_match_to_parameters(*call_info, semantic_context)) {
			call_info_analyse_all_arguments(call_info, false, semantic_context, true);
			EXIT_ERROR(types.unknown_type);
		}
		call_info->instanciation_data.cast_info = cast_info_make_simple(types.unknown_type, Cast_Type::INVALID);

		// Signature: (value, to, from)
		assert(call_info->parameter_values.size == 4, "With return type should be 5 values");
		auto& param_value     = call_info->parameter_values[0];
		auto& param_to        = call_info->parameter_values[1];
		auto& param_from      = call_info->parameter_values[2];
		assert(param_value.value_type != Parameter_Value_Type::NOT_SET, "Parser should not allow this");

		auto helper_analyse_type_param = [&](Parameter_Value& param_value) -> Datatype* 
		{
			if (param_value.value_type == Parameter_Value_Type::NOT_SET) return nullptr;
			assert(param_value.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "We are in cast expression");

			auto& arg = call_info->argument_infos[param_value.options.argument_index];
			return semantic_analyser_analyse_expression_type(arg.expression, false);
		};

		Datatype* datatype_to     = helper_analyse_type_param(param_to);
		Datatype* datatype_from   = helper_analyse_type_param(param_from);

		if (datatype_to == nullptr) {
			if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
				datatype_to = context.datatype;
			}
			else {
				log_semantic_error(
					semantic_context,
					"Cast requires context to know the result-type, otherwise the \"to\" parameter must be set",
					upcast(call_node), Node_Section::ENCLOSURE
				);
				datatype_to = types.unknown_type;
			}
		}

		// Analyse value
		Expression_Context context = expression_context_make_unspecified();
		if (datatype_from != nullptr) {
			context = expression_context_make_specific_type(datatype_from);
		}
		analyse_parameter_value_if_not_already_done(call_info, &param_value, semantic_context, context, false);
		bool param_is_temporary = false;
		datatype_from = parameter_value_get_datatype(param_value, call_info, semantic_context, &param_is_temporary);

		// Check cast type
		Cast_Info cast_info = check_if_cast_possible(datatype_from, datatype_to, param_is_temporary, false, semantic_context);
		if (cast_info.cast_type == Cast_Type::INVALID) 
		{
			log_semantic_error(semantic_context, "Given types cannot be cast", upcast(call_node), Node_Section::ENCLOSURE);
			log_error_info_given_type(semantic_context, datatype_from);
			log_error_info_expected_type(semantic_context, datatype_to);
			EXIT_ERROR(datatype_to);
		}

		call_info->instanciated = true;
		call_info->instanciation_data.cast_info = cast_info;

		bool value_is_temp = 
			expression_info_get_value_info(
				get_info(call_info->argument_infos[param_value.options.argument_index].expression, semantic_context), type_system
			).result_value_is_temporary;
		EXIT_VALUE(datatype_to, cast_type_result_is_temporary(cast_info.cast_type, value_is_temp));
	}
	case AST::Expression_Type::LITERAL_READ:
	{
		auto& read = expr->options.literal_read;
		void* value_ptr;
		Datatype* literal_type;
		Literal_Dummy dummy; 
		dummy.value_nullptr = nullptr;

		switch (read.type)
		{
		case Literal_Type::BOOLEAN:
			literal_type = upcast(types.bool_type);
			value_ptr = &read.options.boolean;
			break;
		case Literal_Type::FLOAT_VAL:
		{
			if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED && types_are_equal(context.datatype, upcast(types.f64_type)))
			{
				literal_type = upcast(types.f64_type);
				value_ptr = &read.options.float_val;
			}
			else
			{
				literal_type = upcast(types.f32_type);
				dummy.value_f32 = (float)read.options.float_val;
				value_ptr = &dummy.value_f32;
			}
			break;
		}
		case Literal_Type::INTEGER:
		{
			bool check_for_auto_conversion = false;
			if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
			{
				Datatype* expected = context.datatype;
				if (expected->type == Datatype_Type::PRIMITIVE)
				{
					auto primitive_class = downcast<Datatype_Primitive>(expected)->primitive_class;
					if (primitive_class == Primitive_Class::INTEGER) {
						check_for_auto_conversion = true;
					}
					else if (primitive_class == Primitive_Class::FLOAT)
					{
						if (expected->memory_info.value.size == 4) {
							literal_type = upcast(types.f32_type);
							dummy.value_f32 = (float)read.options.int_val;
							value_ptr = &dummy.value_f32;
						}
						else if (expected->memory_info.value.size == 8) {
							literal_type = upcast(types.f64_type);
							dummy.value_f64 = (double)read.options.int_val;
							value_ptr = &dummy.value_f64;
						}
						else {
							panic("Float with non 4/8 size?");
						}
						break;
					}
				}
			}

			if (check_for_auto_conversion)
			{
				Datatype* expected = context.datatype;
				bool is_signed = downcast<Datatype_Primitive>(expected)->is_signed;
				int size = expected->memory_info.value.size;

				bool size_is_valid = true;
				if (is_signed)
				{
					i64 value = read.options.int_val;
					switch (size)
					{
					case 1: {
						literal_type = upcast(types.i8_type);
						dummy.value_i8 = (i8)value;
						value_ptr = &dummy.value_i8;
						size_is_valid = value <= INT8_MAX && value >= INT8_MIN;
						break;
					}
					case 2: {
						literal_type = upcast(types.i16_type);
						dummy.value_i16 = (i16)value;
						value_ptr = &dummy.value_i16;
						size_is_valid = value <= INT16_MAX && value >= INT16_MIN;
						break;
					}
					case 4: {
						literal_type = upcast(types.i32_type);
						dummy.value_i32 = (i32)value;
						value_ptr = &dummy.value_i32;
						size_is_valid = value <= INT32_MAX && value >= INT32_MIN;
						break;
					}
					case 8: {
						literal_type = upcast(types.i64_type);
						dummy.value_i64 = (i64)value;
						value_ptr = &dummy.value_i64;
						size_is_valid = true; // Cannot check size as i64 is the max value of the lexer
						break;
					}
					default: panic("");
					}
				}
				else
				{
					if (read.options.int_val < 0) {
						log_semantic_error(semantic_context, "Using a negative literal in an unsigned context requires a cast", expr);
						EXIT_ERROR(context.datatype);
					}

					u64 value = (u64)read.options.int_val;
					switch (size)
					{
					case 1: {
						literal_type = upcast(types.u8_type);
						dummy.value_u8 = (u8)value;
						value_ptr = &dummy.value_u8;
						size_is_valid = value <= UINT8_MAX;
						break;
					}
					case 2: {
						literal_type = upcast(types.u16_type);
						dummy.value_u16 = (u16)value;
						value_ptr = &dummy.value_u16;
						size_is_valid = value <= UINT16_MAX;
						break;
					}
					case 4: {
						literal_type = upcast(types.u32_type);
						dummy.value_u32 = (u32)value;
						value_ptr = &dummy.value_u32;
						size_is_valid = value <= UINT32_MAX;
						break;
					}
					case 8: {
						literal_type = upcast(types.u64_type);
						dummy.value_u64 = (u64)value;
						value_ptr = &dummy.value_u64;
						size_is_valid = value <= UINT64_MAX;
						break;
					}
					default: panic("");
					}
				}

				if (!size_is_valid)
				{
					log_semantic_error(
						semantic_context, "Literal value is outside the range of expected type. To still use this value a cast is required", expr
					);
					EXIT_ERROR(context.datatype);
				}
				literal_type = context.datatype;
			}
			else
			{
				literal_type = upcast(types.i32_type);
				dummy.value_i32 = (i32)read.options.int_val;
				value_ptr = &dummy.value_i32;
			}
			break;
		}
		case Literal_Type::STRING: {
			dummy.value_string = upp_string_from_id(read.options.string);
			literal_type = upcast(types.string);
			value_ptr = &dummy.value_string;
			break;
		}
		case Literal_Type::NULL_VAL:
		{
			literal_type = upcast(types.address);
			dummy.value_nullptr = nullptr;
			value_ptr = &dummy.value_nullptr;
			if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
			{
				Datatype* expected = context.datatype;
				bool is_optional_pointer = false;
				bool is_pointer = datatype_is_pointer(expected, &is_optional_pointer);

				// Special handling for null, so that we can assign null to values
				// in cast_pointer null and cast_pointer{*int}null
				if (is_pointer && is_optional_pointer) {
					literal_type = context.datatype;
				}
			}
			break;
		}
		default: panic("");
		}
		expression_info_set_constant(
			info, literal_type, array_create_static<byte>((byte*)value_ptr, 
			literal_type->memory_info.value.size), AST::upcast(expr),
			semantic_context
		);
		return info;
	}
	case AST::Expression_Type::ENUM_TYPE:
	{
		auto& members = expr->options.enum_members;

		String* enum_name = compilation_data->compiler->identifier_pool.predefined_ids.anon_enum;
		if (expr->base.parent->type == AST::Node_Type::DEFINITION) {
			AST::Definition* definition = downcast<AST::Definition>(expr->base.parent);
			if (definition->is_comptime && definition->symbols.size == 1) {
				enum_name = definition->symbols[0]->name;
			}
		}

		Datatype_Enum* enum_type = type_system_make_enum_empty(type_system, enum_name, upcast(expr));
		int next_member_value = 1; // Note: Enum values all start at 1, so 0 represents an invalid enum
		for (int i = 0; i < members.size; i++)
		{
			auto& member_node = members[i];
			if (member_node->value.available)
			{
				semantic_analyser_analyse_expression_value(
					member_node->value.value, expression_context_make_specific_type(upcast(types.i32_type)), semantic_context
				);
				auto constant = expression_calculate_comptime_value(member_node->value.value, "Enum value must be comptime known", semantic_context);
				if (constant.available) {
					next_member_value = upp_constant_to_value<i32>(constant.value);
				}
			}

			Enum_Member member;
			member.name = member_node->name;
			member.value = next_member_value;
			next_member_value++;
			dynamic_array_push_back(&enum_type->members, member);
		}

		// Check for member errors
		for (int i = 0; i < enum_type->members.size; i++)
		{
			auto member = &enum_type->members[i];
			for (int j = i + 1; j < enum_type->members.size; j++)
			{
				auto other = &enum_type->members[j];
				if (other->name == member->name) {
					log_semantic_error(semantic_context, "Enum member name is already in use", AST::upcast(expr));
					log_error_info_id(semantic_context, other->name);
				}
				if (other->value == member->value) {
					log_semantic_error(semantic_context, "Enum value is already taken by previous member", AST::upcast(expr));
					log_error_info_id(semantic_context, other->name);
				}
			}
		}
		type_system_finish_enum(type_system, enum_type);
		EXIT_TYPE(upcast(enum_type));
	}
	case AST::Expression_Type::MODULE: {
		log_semantic_error(semantic_context, "Module not valid in this context", AST::upcast(expr));
		EXIT_ERROR(types.unknown_type);
	}
	case AST::Expression_Type::FUNCTION:
	{
		// Create new function progress
		auto progress = function_progress_create_with_modtree_function(
			0, expr, 0, semantic_context, nullptr, Symbol_Access_Level::POLYMORPHIC
		);

		// Handle infered function types
		auto function = expr->options.function;
		if (!function.signature.available)
		{
			progress->function->signature = compilation_data->empty_call_signature;
			bool log_error = true;
			if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
			{
				if (context.datatype->type == Datatype_Type::FUNCTION_POINTER) {
					progress->function->signature = downcast<Datatype_Function_Pointer>(context.datatype)->signature;
					log_error = false;
				}
			}
			if (log_error) {
				log_semantic_error(semantic_context, "Inferred function type requires context, which is not available", expr, Node_Section::FIRST_TOKEN);
			}
		}

		// Wait for header analysis to finish
		auto executer = compilation_data->workload_executer;
		analysis_workload_add_dependency_internal(executer, semantic_context->current_workload, upcast(progress->header_workload));
		workload_executer_wait_for_dependency_resolution(semantic_context);

		// Return function
		semantic_analyser_register_function_call(progress->function, semantic_context);
		EXIT_FUNCTION(progress->function);
	}
	case AST::Expression_Type::STRUCTURE_TYPE: {
		auto workload = workload_structure_create(expr, 0, false, semantic_context, Symbol_Access_Level::POLYMORPHIC);
		EXIT_TYPE(upcast(workload->struct_type));
	}
	case AST::Expression_Type::BAKE:
	{
		// Create bake progress and wait for it to finish
		Datatype* expected_type = 0;
		if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
			expected_type = context.datatype;
		}
		auto progress = bake_progress_create(expr, expected_type, semantic_context);
		analysis_workload_add_dependency_internal(
			compilation_data->workload_executer, semantic_context->current_workload, upcast(progress->execute_workload)
		);
		workload_executer_wait_for_dependency_resolution(semantic_context);

		// Handle result
		auto bake_result = &progress->result;
		if (bake_result->available) {
			expression_info_set_constant(info, bake_result->value);
			return info;
		}
		else
		{
			auto return_type = progress->bake_function->signature->return_type();
			if (return_type.available) {
				EXIT_ERROR(return_type.value);
			}
			else {
				EXIT_ERROR(types.unknown_type);
			}
		}
		panic("invalid_code_path");
		EXIT_ERROR(types.unknown_type);
	}
	case AST::Expression_Type::FUNCTION_SIGNATURE:
	{
		auto& parameters = expr->options.signature_parameters;
		Call_Signature* signature = call_signature_create_empty();
		for (int i = 0; i < parameters.size; i++)
		{
			auto& param_node = parameters[i];
			if (param_node->is_comptime) {
				log_semantic_error(semantic_context, "Comptime parameters are only allowed in functions, not in signatures!", AST::upcast(param_node));
				continue;
			}
			Call_Parameter* param = call_signature_add_parameter(signature, param_node->name, nullptr, true, false, param_node->is_return_type);
			analyse_parameter_type_and_value(*param, param_node, true, semantic_context);
			if (param_node->is_return_type) {
				signature->return_type_index = signature->parameters.size - 1;
			}
		}
		signature = call_signature_register(signature, compilation_data);
		EXIT_TYPE_OR_POLY(upcast(type_system_make_function_pointer(type_system, signature)));
	}
	case AST::Expression_Type::ARRAY_TYPE:
	{
		auto& array_node = expr->options.array_type;

		// Analyse type expression
		Datatype* element_type = semantic_analyser_analyse_expression_type(array_node.type_expr, semantic_context, true);

		// Analyse size expression (Which may be polymorhic)
		analyse_index_accept_all_ints_as_usize(array_node.size_expr, semantic_context, true);
		auto size_info = get_info(array_node.size_expr, semantic_context);
		if (size_info->result_type == Expression_Result_Type::POLYMORPHIC_PATTERN)
		{
			auto pattern_type = size_info->options.polymorphic_pattern;
			if (pattern_type->type != Datatype_Type::PATTERN_VARIABLE) {
				log_semantic_error(semantic_context, "Array-Size does not take pattern-type-tree, only single pattern values", expr, Node_Section::ENCLOSURE);
				EXIT_TYPE_OR_POLY(upcast(type_system_make_array(type_system, element_type, false, 1, nullptr)))
			}

			Datatype_Pattern_Variable* variable = downcast<Datatype_Pattern_Variable>(pattern_type);
			EXIT_TYPE_OR_POLY(upcast(type_system_make_array(type_system, element_type, false, 1, variable)));
		}

		// Otherwise array-size needs to be comptime known
		usize array_size = 0; // Note: Here I actually mean the element count, not the data-type size
		bool array_size_known = false;

		auto comptime = expression_calculate_comptime_value(array_node.size_expr, "Array size must be know at compile time", semantic_context);
		if (comptime.available)
		{
			array_size_known = true;
			array_size = upp_constant_to_value<usize>(comptime.value);
			if (array_size >= (1 << 20)) {
				log_semantic_error(semantic_context, "Array size is probably overflowing (e.g. negative)", array_node.size_expr);
				array_size_known = false;
				array_size = 1;
			}
			else if (array_size == 0) {
				log_semantic_error(semantic_context, "Array size must not be 0", array_node.size_expr);
				array_size_known = false;
				array_size = 1;
			}
		}
		EXIT_TYPE_OR_POLY(upcast(type_system_make_array(type_system, element_type, array_size_known, (int)array_size)))
	}
	case AST::Expression_Type::SLICE_TYPE: {
		auto element_type = semantic_analyser_analyse_expression_type(expr->options.slice_type, semantic_context, true);
		auto slice_type = type_system_make_slice(type_system, element_type);
		EXIT_TYPE_OR_POLY(upcast(slice_type));
	}
	case AST::Expression_Type::POINTER_TYPE: {
		auto& ptr = expr->options.pointer_type;
		auto result = type_system_make_pointer(
			type_system,
			semantic_analyser_analyse_expression_type(ptr.child_type, semantic_context, true), 
			ptr.is_optional
		);
		EXIT_TYPE_OR_POLY(upcast(result));
	}
	case AST::Expression_Type::NEW_EXPR:
	{
		auto& new_node = expr->options.new_expr;
		Datatype* allocated_type = semantic_analyser_analyse_expression_type(new_node.type_expr, semantic_context);
		// Wait for type size since ir-generator needs it to function properly
		type_wait_for_size_info_to_finish(allocated_type, semantic_context);

		Datatype* result_type = 0;
		if (new_node.count_expr.available) {
			result_type = upcast(type_system_make_slice(type_system, allocated_type));
			analyse_index_accept_all_ints_as_usize(new_node.count_expr.value, semantic_context);
		}
		else {
			result_type = upcast(type_system_make_pointer(type_system, allocated_type));
		}
		EXIT_VALUE(result_type, true);
	}
	case AST::Expression_Type::STRUCT_INITIALIZER:
	{
		auto& init_node = expr->options.struct_initializer;

		Datatype* type_for_init = nullptr;
		if (init_node.type_expr.available) {
			type_for_init = semantic_analyser_analyse_expression_type(init_node.type_expr.value, semantic_context);
		}
		else 
		{
			if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
				type_for_init = context.datatype;
			}
			else 
			{
				if (context.type != Expression_Context_Type::ERROR_OCCURED) {
					log_semantic_error(
						semantic_context, "Could not determine type for auto struct initializer from context", 
						expr, Node_Section::WHOLE_NO_CHILDREN
					);
				}
				type_for_init = types.unknown_type;
			}
		}

		// Make sure that type is a struct
		type_wait_for_size_info_to_finish(type_for_init, semantic_context);
		if (type_for_init->type == Datatype_Type::STRUCT)
		{
			Datatype_Struct* struct_type = downcast<Datatype_Struct>(type_for_init);
			if (!struct_type->is_union)
			{
				Datatype_Struct* final_type = analyse_member_initializer_recursive(init_node.call_node, struct_type, 0, semantic_context);
				EXIT_VALUE(upcast(final_type), true);
			}
			else
			{
				// Union initializer
				Call_Info* call_info = get_info(init_node.call_node, semantic_context, true);
				*call_info = call_info_make_from_call_node(
					init_node.call_node, call_origin_make(struct_type, semantic_context), &compilation_data->arena
				);
				call_info->instanciation_data.initializer_info.subtype_valid = false;
				call_info->instanciation_data.initializer_info.supertype_valid = false;
				call_info->instanciated = true;

				// Match arguments
				arguments_match_to_parameters(*call_info, semantic_context);
				call_info_analyse_all_arguments(call_info, false, semantic_context, true);
				if (init_node.call_node->arguments.size == 0) {
					log_semantic_error(semantic_context, "Union initializer expects a value", upcast(init_node.call_node), Node_Section::ENCLOSURE);
				}
				for (int i = 1; i < init_node.call_node->arguments.size; i++) {
					log_semantic_error(semantic_context, 
						"Union initializer requires exactly one argument", upcast(init_node.call_node->arguments[i])
					);
				}

				EXIT_VALUE(upcast(struct_type), true);
			}
		}
		else if (type_for_init->type == Datatype_Type::SLICE)
		{
			Datatype_Slice* slice_type = downcast<Datatype_Slice>(type_for_init);
			Call_Info* call_info = get_info(init_node.call_node, semantic_context, true);
			*call_info = call_info_make_from_call_node(init_node.call_node, call_origin_make(slice_type, compilation_data), &compilation_data->arena);
			arguments_match_to_parameters(*call_info, semantic_context);
			call_info_analyse_all_arguments(call_info, false, semantic_context, true);
			EXIT_VALUE(upcast(slice_type), true);
		}
		else
		{
			if (!datatype_is_unknown(type_for_init)) {
				log_semantic_error(semantic_context, "Struct initializer requires struct type for initialization", expr, Node_Section::WHOLE_NO_CHILDREN);
				log_error_info_given_type(semantic_context, type_for_init);
				type_for_init = types.unknown_type;
			}

			Call_Info* call_info = get_info(init_node.call_node, semantic_context, true);
			*call_info = call_info_make_error(init_node.call_node, compilation_data);
			call_info_analyse_all_arguments(call_info, false, semantic_context, true);
		}

		EXIT_ERROR(type_for_init);
	}
	case AST::Expression_Type::ARRAY_INITIALIZER:
	{
		auto& init_node = expr->options.array_initializer;
		Datatype* element_type = 0;
		if (init_node.type_expr.available) {
			element_type = semantic_analyser_analyse_expression_type(init_node.type_expr.value, semantic_context);
		}
		else
		{
			if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
			{
				Datatype* expected = context.datatype;
				if (expected->type == Datatype_Type::ARRAY) {
					element_type = downcast<Datatype_Array>(expected)->element_type;
				}
				else if (expected->type == Datatype_Type::SLICE) {
					element_type = downcast<Datatype_Slice>(expected)->element_type;
				}
				else {
					log_semantic_error(semantic_context, "Expected type for array-initializer should be array or slice", expr);
					log_error_info_given_type(semantic_context, expected);
					element_type = types.unknown_type;
				}
			}
			else 
			{
				if (context.type != Expression_Context_Type::ERROR_OCCURED) {
					log_semantic_error(semantic_context, "Could not determine array element type from context", expr);
				}
				element_type = types.unknown_type;
			}
		}

		int array_element_count = init_node.values.size;
		// There are no 0-sized arrays, only 0-sized slices. So if we encounter an empty initializer, e.g. type.[], we return an empty slice
		if (array_element_count == 0) {
			Datatype* result_type = upcast(type_system_make_slice(type_system, element_type));
			EXIT_VALUE(result_type, true);
		}

		for (int i = 0; i < init_node.values.size; i++) {
			semantic_analyser_analyse_expression_value(
				init_node.values[i], expression_context_make_specific_type(element_type), semantic_context
			);
		}
		Datatype* result_type = upcast(type_system_make_array(type_system, element_type, true, array_element_count));
		EXIT_VALUE(result_type, true);
	}
	case AST::Expression_Type::ARRAY_ACCESS:
	{
		info->specifics.overload.function = 0;
		info->specifics.overload.switch_left_and_right = false;

		auto& access_node = expr->options.array_access;
		Datatype* array_type = semantic_analyser_analyse_expression_value(
			access_node.array_expr, expression_context_make_dereference(), semantic_context
		);
		bool value_is_temporary = expression_info_get_value_info(get_info(access_node.array_expr, semantic_context), type_system).result_value_is_temporary;
		if (datatype_is_unknown(array_type)) {
			semantic_analyser_analyse_expression_value(access_node.index_expr, expression_context_make_error(), semantic_context);
			EXIT_ERROR(types.unknown_type);
		}

		// Normal array or slice access
		if (array_type->type == Datatype_Type::ARRAY || array_type->type == Datatype_Type::SLICE)
		{
			analyse_index_accept_all_ints_as_usize(access_node.index_expr, semantic_context);
			if (array_type->type == Datatype_Type::ARRAY) 
			{
				Datatype* element_type = downcast<Datatype_Array>(array_type)->element_type;
				EXIT_VALUE(element_type, value_is_temporary);
			}
			else 
			{
				EXIT_VALUE(downcast<Datatype_Slice>(array_type)->element_type, false);
			}
		}

		// Check for operator overloads
		// auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;
		// if (!type_is_valid)
		// {
		// 	result_is_temporary = true; // When calling a custom function the return type is for sure temporary
		// 	Custom_Operator_Key key;
		// 	key.type = Context_Change_Type::ARRAY_ACCESS;
		// 	key.options.array_access.array_type = array_type;
		// 	Custom_Operator* overload = symbol_table_query_custom_operator(operator_context, key);
		// 	if (overload != 0)
		// 	{
		// 		auto& custom_access = overload->array_access;
		// 		assert(!custom_access.is_polymorphic, "");
		// 		auto function = custom_access.options.function;
		// 		semantic_analyser_analyse_expression_value(
		// 			access_node.index_expr,
		// 			expression_context_make_specific_type(upcast(function->signature->parameters[1].datatype))
		// 		);
		// 		type_is_valid = true;
		// 		expected_mods = custom_access.options.function->signature->parameters[0].datatype->mods;
		// 		assert(function->signature->return_type().available, "");
		// 		result_type = function->signature->return_type().value;
		// 		info->specifics.overload.function = function;
		// 	}
		// }

		// // Check for polymorphic operator overload on []
		// if (!type_is_valid && array_type->type == Datatype_Type::STRUCT)
		// {
		// 	auto struct_type = downcast<Datatype_Struct>(array_type);
		// 	if (struct_type->workload != 0 && struct_type->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE)
		// 	{
		// 		Custom_Operator_Key key;
		// 		key.type = Context_Change_Type::ARRAY_ACCESS;
		// 		key.options.array_access.array_type = upcast(struct_type->workload->polymorphic.instance.parent->body_workload->struct_type);

		// 		Custom_Operator* overload = symbol_table_query_custom_operator(operator_context, key);
		// 		if (overload != 0)
		// 		{
		// 			auto& array_access = overload->array_access;
		// 			assert(array_access.is_polymorphic, "Must be the case for base structure");

		// 			// Create call
		// 			Arena* scratch_arena = &semantic_analyser.current_workload->scratch_arena;
		// 			auto checkpoint = scratch_arena->make_checkpoint();
		// 			SCOPE_EXIT(scratch_arena->rewind_to_checkpoint(checkpoint));

		// 			auto poly_header = overload->array_access.options.poly_function.poly_header;
		// 			Call_Info call_info = call_info_make_with_empty_arguments(call_origin_make(poly_header), scratch_arena);
		// 			call_info.argument_infos = scratch_arena->allocate_array<Argument_Info>(2);

		// 			// Set parameter values
		// 			call_info_set_parameter_to_expression(&call_info, 0, 0, access_node.array_expr);
		// 			call_info_set_parameter_to_expression(&call_info, 1, 1, access_node.index_expr);
		// 			if (poly_header->signature->return_type_index != -1 && context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
		// 				call_info.parameter_values[poly_header->signature->return_type_index] = parameter_value_make_datatype_known(context.datatype);
		// 			}

		// 			// Instanciate
		// 			Poly_Instance* instance = poly_header_instanciate(&call_info, upcast(expr), Node_Section::ENCLOSURE);
		// 			if (instance != nullptr)
		// 			{
		// 				assert(instance->type == Poly_Instance_Type::FUNCTION, "");
		// 				auto function = instance->options.function_instance->function;
		// 				info->specifics.overload.function = function;
		// 				auto return_type = function->signature->return_type();
		// 				if (!return_type.available) {
		// 					log_semantic_error(semantic_context, "Array-Access without return type invalid", upcast(expr), Node_Section::ENCLOSURE);
		// 					type_is_valid = false;
		// 					expression_info_set_no_value(info);
		// 					return info;
		// 				}
		// 				EXIT_VALUE(return_type.value, true);
		// 			}
		// 		}
		// 	}
		// }

		log_semantic_error(semantic_context, "Type not valid for array access", access_node.array_expr);
		log_error_info_given_type(semantic_context, array_type);
		EXIT_ERROR(types.unknown_type);
	}
	case AST::Expression_Type::MEMBER_ACCESS:
	{
		auto& member_node = expr->options.member_access;
		// Note: We assume that this is a normal member access for initializiation
		// This has some special impliciations in editor, as analysis-items are generated for member-accesses,
		// and expression_info.valid is ignored there
		info->specifics.member_access.type = Member_Access_Type::STRUCT_MEMBER_ACCESS;
		info->specifics.member_access.options.member = struct_member_make(types.unknown_type, member_node.name, nullptr, 0, nullptr);

		auto access_expr_info = semantic_analyser_analyse_expression_any(member_node.expr, expression_context_make_dereference(), semantic_context);
		auto& ids = compilation_data->compiler->identifier_pool.predefined_ids;

		auto search_struct_type_for_polymorphic_parameter_access = [&](Datatype_Struct* struct_type) -> Optional<Upp_Constant>
			{
				if (struct_type->workload == 0) {
					return optional_make_failure<Upp_Constant>();
				}

				auto struct_workload = struct_type->workload;
				Poly_Header* poly_header = 0;
				if (struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
					// Accessing values of base-struct not possible
					return optional_make_failure<Upp_Constant>(); // Not polymorphic
				}
				else if (struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
					poly_header = struct_workload->polymorphic.instance.parent->poly_header;
				}
				else {
					return optional_make_failure<Upp_Constant>(); // Not polymorphic
				}

				// Try to find structure parameter with this base_name
				int value_access_index = -1;
				for (int i = 0; i < poly_header->signature->parameters.size; i++) {
					auto& parameter = poly_header->signature->parameters[i];
					if (parameter.name == member_node.name) {
						value_access_index = parameter.comptime_variable_index;
						break;
					}
				}

				if (value_access_index != -1) {
					info->specifics.member_access.type = Member_Access_Type::STRUCT_POLYMORHPIC_PARAMETER_ACCESS;
					info->specifics.member_access.options.poly_access.index = value_access_index;
					info->specifics.member_access.options.poly_access.struct_workload = struct_workload;

					auto& value = struct_workload->base.active_pattern_variable_states[value_access_index];
					assert(value.type == Pattern_Variable_State_Type::SET, "Struct instance value must be set");
					return optional_make_success(value.options.value);
				}

				return optional_make_failure<Upp_Constant>();
			};

		switch (access_expr_info->result_type)
		{
		case Expression_Result_Type::DATATYPE:
		{
			Datatype* datatype = access_expr_info->options.datatype;

			if (datatype_is_unknown(datatype)) {
				semantic_analyser_set_error_flag(true, semantic_context);
				EXIT_ERROR(types.unknown_type);
			}

			// Handle Struct-Subtypes and polymorphic value access, e.g. Node.Expression / Node(int).T
			if (datatype->type == Datatype_Type::STRUCT)
			{
				Datatype_Struct* structure = downcast<Datatype_Struct>(datatype);

				// Check if it's a polymorphic parameter access
				auto poly_parameter_access = search_struct_type_for_polymorphic_parameter_access(structure);
				if (poly_parameter_access.available) {
					expression_info_set_constant(info, poly_parameter_access.value);
					return info;
				}

				// Check if it's a valid subtype
				Datatype_Struct* subtype = nullptr;
				for (int i = 0; i < structure->subtypes.size; i++) {
					if (structure->subtypes[i]->name == member_node.name) {
						subtype = structure->subtypes[i];
						break;
					}
				}

				if (subtype != nullptr) 
				{
					Datatype* result = upcast(subtype);
					info->specifics.member_access.type = Member_Access_Type::STRUCT_SUBTYPE;
					EXIT_TYPE(result);
				}
			}

			// If not a struct then enum is also possible
			if (datatype->type != Datatype_Type::ENUM) {
				log_semantic_error(semantic_context, "Member access for given type not possible", member_node.expr);
				log_error_info_given_type(semantic_context, datatype);
				EXIT_ERROR(types.unknown_type);
			}

			info->specifics.member_access.type = Member_Access_Type::ENUM_MEMBER_ACCESS;
			auto enum_type = downcast<Datatype_Enum>(datatype);
			auto& members = enum_type->members;

			Enum_Member* found = 0;
			for (int i = 0; i < members.size; i++) {
				auto member = &members[i];
				if (member->name == member_node.name) {
					found = member;
					break;
				}
			}

			int value = 0;
			if (found == 0) {
				log_semantic_error(semantic_context, "Enum/Union does not contain this member", member_node.expr);
				log_error_info_id(semantic_context, member_node.name);
			}
			else {
				value = found->value;
			}
			expression_info_set_constant_enum(info, upcast(enum_type), value, semantic_context);
			return info;
		}
		case Expression_Result_Type::NOTHING: {
			log_semantic_error(semantic_context, "Cannot use member access ('x.y') on nothing", member_node.expr);
			log_error_info_expression_result_type(semantic_context, access_expr_info->result_type);
			EXIT_ERROR(types.unknown_type);
		}
		case Expression_Result_Type::FUNCTION:
		case Expression_Result_Type::HARDCODED_FUNCTION:
		case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
			log_semantic_error(semantic_context, "Cannot use member access ('x.y') on functions", member_node.expr);
			log_error_info_expression_result_type(semantic_context, access_expr_info->result_type);
			EXIT_ERROR(types.unknown_type);
		}
		case Expression_Result_Type::POLYMORPHIC_STRUCT: {
			log_semantic_error(semantic_context, "Cannot access members of uninstanciated polymorphic struct", member_node.expr);
			log_error_info_expression_result_type(semantic_context, access_expr_info->result_type);
			EXIT_ERROR(types.unknown_type);
		}
		case Expression_Result_Type::POLYMORPHIC_PATTERN: {
			log_semantic_error(semantic_context, "Cannot use member access on polymorphic-pattern", member_node.expr);
			log_error_info_expression_result_type(semantic_context, access_expr_info->result_type);
			EXIT_ERROR(types.unknown_type);
		}
		case Expression_Result_Type::VALUE:
		case Expression_Result_Type::CONSTANT:
		{
			auto& access_info = info->specifics.member_access;

			auto value_info = expression_info_get_value_info(access_expr_info, type_system);
			auto datatype = value_info.result_type;
			bool result_is_temporary = value_info.result_value_is_temporary;

			// Early exit
			if (datatype_is_unknown(datatype)) {
				semantic_analyser_set_error_flag(true, semantic_context);
				EXIT_ERROR(types.unknown_type);
			}

			// Check for normal member accesses (Struct members + array/slice members) (Not overloads)
			if (datatype->type == Datatype_Type::STRUCT)
			{
				Datatype_Struct* structure = downcast<Datatype_Struct>(datatype);

				// Search for poly_parameter access
				auto poly_parameter_access = search_struct_type_for_polymorphic_parameter_access(structure);
				if (poly_parameter_access.available) {
					expression_info_set_constant(info, poly_parameter_access.value);
					return info;
				}
				type_wait_for_size_info_to_finish(datatype, semantic_context);

				// Check member access
				for (int i = 0; i < structure->members.size; i++)
				{
					auto& member = structure->members[i];
					if (member.id == member_node.name)
					{
						access_info.type = Member_Access_Type::STRUCT_MEMBER_ACCESS;
						access_info.options.member = member;
						EXIT_VALUE(access_info.options.member.type, result_is_temporary);
					}
				}

				// Check subtype access
				for (int i = 0; i < structure->subtypes.size; i++)
				{
					auto subtype = structure->subtypes[i];
					if (subtype->name == member_node.name)
					{
						access_info.type = Member_Access_Type::STRUCT_UP_OR_DOWNCAST;
						Datatype* result_type = upcast(structure->subtypes[i]);
						EXIT_VALUE(result_type, result_is_temporary);
					}
				}

				// Check parenttype (upcast) access
				if (structure->parent_struct != nullptr && structure->parent_struct->name == member_node.name)
				{
					access_info.type = Member_Access_Type::STRUCT_UP_OR_DOWNCAST;
					auto result_type = upcast(structure->parent_struct);
					EXIT_VALUE(result_type, result_is_temporary);
				}
			}

			if ((datatype->type == Datatype_Type::ARRAY || datatype->type == Datatype_Type::SLICE) &&
				(member_node.name == ids.size || member_node.name == ids.data))
			{
				if (datatype->type == Datatype_Type::ARRAY)
				{
					auto array = downcast<Datatype_Array>(datatype);
					if (member_node.name == ids.size) {
						if (array->count_known) {
							expression_info_set_constant_usize(info, array->element_count, semantic_context);
						}
						else {
							EXIT_ERROR(upcast(types.usize));
						}
						return info;
					}
					else
					{ // Data access
						EXIT_VALUE(upcast(type_system_make_pointer(type_system, array->element_type)), true);
					}
				}
				else // Slice
				{
					auto slice = downcast<Datatype_Slice>(datatype);
					Struct_Member member;
					if (member_node.name == ids.size) {
						member = slice->size_member;
					}
					else {
						member = slice->data_member;
					}

					info->specifics.member_access.type = Member_Access_Type::STRUCT_MEMBER_ACCESS;
					info->specifics.member_access.options.member = member;
					EXIT_VALUE(member.type, result_is_temporary);
				}
			}

			// Error if no member access was found
			log_semantic_error(semantic_context, "Member access is not valid", expr);
			log_error_info_given_type(semantic_context, value_info.result_type);
			log_error_info_id(semantic_context, member_node.name);
			EXIT_ERROR(types.unknown_type);
		}
		default: panic("");
		}
		panic("Should not happen");
		EXIT_ERROR(types.unknown_type);
	}
	case AST::Expression_Type::AUTO_ENUM:
	{
		String* id = expr->options.auto_enum;
		if (context.type != Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
			if (context.type != Expression_Context_Type::ERROR_OCCURED) {
				log_semantic_error(semantic_context, "Could not determine context for auto enum", expr);
			}
			EXIT_ERROR(types.unknown_type);
		}
		Datatype* expected = context.datatype;

		if (expected->type != Datatype_Type::ENUM) {
			log_semantic_error(semantic_context, "Context requires a type that is not an enum, so .NAME syntax is not valid", expr);
			log_error_info_given_type(semantic_context, context.datatype);
			EXIT_ERROR(types.unknown_type);
		}

		auto& members = downcast<Datatype_Enum>(expected)->members;
		Enum_Member* found = 0;
		for (int i = 0; i < members.size; i++) {
			auto member = &members[i];
			if (member->name == id) {
				found = member;
				break;
			}
		}

		int value = 0;
		if (found == 0) {
			log_semantic_error(semantic_context, "Enum does not contain this member", expr);
			log_error_info_id(semantic_context, id);
		}
		else {
			value = found->value;
		}

		expression_info_set_constant_enum(info, expected, value, semantic_context);
		return info;
	}
	case AST::Expression_Type::UNARY_OPERATION:
	{
		auto& unary_node = expr->options.unop;
		info->specifics.overload.function = 0;
		info->specifics.overload.switch_left_and_right = false;

		switch (unary_node.type)
		{
		case AST::Unop::NEGATE:
		case AST::Unop::NOT:
		{
			bool is_negate = unary_node.type == AST::Unop::NEGATE;

			// Check for literals (Float and int should adjust to correct size with negate)
			Expression_Context operand_context = expression_context_make_dereference();
			if (is_negate && context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED && context.datatype->type == Datatype_Type::PRIMITIVE)
			{
				auto primitive = downcast<Datatype_Primitive>(context.datatype);
				if (primitive->primitive_type != Primitive_Type::BOOLEAN) {
					operand_context = context;
				}
			}

			Datatype* datatype = semantic_analyser_analyse_expression_value(unary_node.expr, operand_context, semantic_context);
			if (datatype_is_unknown(datatype)) {
				EXIT_ERROR(types.unknown_type);
			}

			// Check for primitive operand
			if (is_negate)
			{
				if (datatype->type == Datatype_Type::PRIMITIVE) 
				{
					auto primitive = downcast<Datatype_Primitive>(datatype);
					if (primitive->is_signed && primitive->primitive_type != Primitive_Type::BOOLEAN) {
						EXIT_VALUE(datatype, true)
					}
					else {
						log_semantic_error(semantic_context, "Negate only works on signed primitive values", expr, Node_Section::FIRST_TOKEN);
						EXIT_ERROR(types.unknown_type);
					}
				}
			}
			else {
				if (types_are_equal(datatype, upcast(types.bool_type))) {
					EXIT_VALUE(upcast(types.bool_type), true);
				}
			}

			// Check for custom unop
			// {
			// 	Custom_Operator_Key key;
			// 	key.type = Context_Change_Type::UNARY_OPERATOR;
			// 	key.options.unop.unop = is_negate ? AST::Unop::NEGATE : AST::Unop::NOT;
			// 	key.options.unop.type = operand_type;

			// 	auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;
			// 	Custom_Operator* overload = symbol_table_query_custom_operator(operator_context, key);
			// 	if (overload != 0)
			// 	{
			// 		type_is_valid = true;
			// 		result_type = overload->unop.function->signature->return_type().value;
			// 		expected_mods = overload->unop.function->signature->parameters[0].datatype->mods;
			// 		info->specifics.overload.function = overload->unop.function;
			// 		semantic_analyser_register_function_call(overload->unop.function);
			// 	}
			// }

			log_semantic_error(semantic_context, "Operand type not valid for unop", unary_node.expr, Node_Section::FIRST_TOKEN);
			log_error_info_given_type(semantic_context, datatype);
			if (is_negate) {
				EXIT_ERROR(types.unknown_type);
			}
			EXIT_ERROR(upcast(types.bool_type));
		}
		case AST::Unop::ADDRESS_OF:
		{
			auto datatype = semantic_analyser_analyse_expression_value(unary_node.expr, expression_context_make_unspecified(), semantic_context);
			auto value_info = expression_info_get_value_info(get_info(unary_node.expr, semantic_context), type_system);
			if (value_info.result_value_is_temporary) {
				log_semantic_error(semantic_context, "Cannot take address of temporary value", expr, Node_Section::WHOLE_NO_CHILDREN);
			}
			EXIT_VALUE(upcast(type_system_make_pointer(type_system, datatype, false)), true);
		}
		case AST::Unop::NULL_CHECK:
		{
			auto datatype = semantic_analyser_analyse_expression_value(unary_node.expr, expression_context_make_unspecified(), semantic_context);
			if (datatype->type != Datatype_Type::POINTER) 
			{
				if (!datatype_is_unknown(datatype)) {
					log_semantic_error(semantic_context, "Null check only valid on pointer types", expr);
					log_error_info_given_type(semantic_context, datatype);
				}
				else {
					semantic_analyser_set_error_flag(true, semantic_context);
				}
			}

			EXIT_VALUE(upcast(types.bool_type), false);
		}
		case AST::Unop::DEREFERENCE:
		case AST::Unop::OPTIONAL_DEREFERENCE:
		{
			auto datatype = semantic_analyser_analyse_expression_value(unary_node.expr, expression_context_make_unspecified(), semantic_context);
			if (datatype->type != Datatype_Type::POINTER) 
			{
				if (!datatype_is_unknown(datatype)) {
					log_semantic_error(semantic_context, "Cannot dereference non-pointer value", expr);
					log_error_info_given_type(semantic_context, datatype);
				}
				else {
					semantic_analyser_set_error_flag(true, semantic_context);
				}
				EXIT_VALUE(types.unknown_type, false);
			}

			Datatype_Pointer* pointer = downcast<Datatype_Pointer>(datatype);
			bool is_optional_dereference = unary_node.type == AST::Unop::OPTIONAL_DEREFERENCE;
			if (pointer->is_optional && !is_optional_dereference) {
				log_semantic_error(semantic_context, "Cannot dereference optional pointer, use -?& instead", expr);
				log_error_info_given_type(semantic_context, datatype);
			}
			else if (!pointer->is_optional && is_optional_dereference) {
				log_semantic_error(semantic_context, "Cannot dereference normal pointer with optional dereference, use -& instead", expr);
				log_error_info_given_type(semantic_context, datatype);
			}

			EXIT_VALUE(pointer->element_type, false);
		}
		default:panic("");
		}
		panic("");
		EXIT_ERROR(types.unknown_type);
	}
	case AST::Expression_Type::BINARY_OPERATION:
	{
		info->specifics.overload.function = 0;
		auto& binop_node = expr->options.binop;
		const bool is_pointer_comparison = binop_node.type == AST::Binop::POINTER_EQUAL || binop_node.type == AST::Binop::POINTER_NOT_EQUAL;
		const bool is_comparison =
			binop_node.type == AST::Binop::EQUAL ||
			binop_node.type == AST::Binop::NOT_EQUAL ||
			binop_node.type == AST::Binop::LESS ||
			binop_node.type == AST::Binop::LESS_OR_EQUAL ||
			binop_node.type == AST::Binop::GREATER ||
			binop_node.type == AST::Binop::GREATER_OR_EQUAL ||
			is_pointer_comparison;

		// Evaluate operands
		Datatype* left_type;
		Datatype* right_type;
		{
			// If we are dealing with auto-expression, make sure to analyse in the right order
			bool left_requires_context = expression_is_auto_expression(binop_node.left, compilation_data);
			bool right_requires_context = expression_is_auto_expression(binop_node.right, compilation_data);

			Expression_Context unknown_context = expression_context_make_dereference();
			if (is_pointer_comparison) {
				unknown_context = expression_context_make_unspecified();
			}

			if (left_requires_context && right_requires_context) 
			{
				if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
					left_type = semantic_analyser_analyse_expression_value(
						binop_node.left, expression_context_make_specific_type(context.datatype), semantic_context
					);
					right_type = semantic_analyser_analyse_expression_value(
						binop_node.right, expression_context_make_specific_type(context.datatype), semantic_context
					);
				}
				else {
					left_type = semantic_analyser_analyse_expression_value(binop_node.left, unknown_context, semantic_context);
					right_type = semantic_analyser_analyse_expression_value(binop_node.right, unknown_context, semantic_context);
				}
			}
			else if ((!left_requires_context && !right_requires_context)) {
				left_type = semantic_analyser_analyse_expression_value(binop_node.left, unknown_context, semantic_context);
				right_type = semantic_analyser_analyse_expression_value(binop_node.right, unknown_context, semantic_context);
			}
			else if (left_requires_context && !right_requires_context)
			{
				right_type = semantic_analyser_analyse_expression_value(binop_node.right, unknown_context, semantic_context);
				if (is_pointer_comparison) 
				{
					left_type = semantic_analyser_analyse_expression_value(binop_node.left, expression_context_make_specific_type(right_type), semantic_context);
				}
				else 
				{
					if (types_are_equal(upcast(types.address), right_type)) {
						left_type = semantic_analyser_analyse_expression_value(binop_node.left, expression_context_make_specific_type(upcast(types.isize)), semantic_context);
					}
					else {
						left_type = semantic_analyser_analyse_expression_value(binop_node.left, expression_context_make_specific_type(right_type), semantic_context);
					}
				}
			}
			else if (!left_requires_context && right_requires_context)
			{
				left_type = semantic_analyser_analyse_expression_value(binop_node.left, unknown_context, semantic_context);
				if (is_pointer_comparison) {
					right_type = semantic_analyser_analyse_expression_value(binop_node.right, expression_context_make_specific_type(left_type), semantic_context);
				}
				else 
				{
					if (types_are_equal(upcast(types.address), left_type)) {
						right_type = semantic_analyser_analyse_expression_value(binop_node.right, expression_context_make_specific_type(upcast(types.isize)), semantic_context);
					}
					else {
						right_type = semantic_analyser_analyse_expression_value(binop_node.right, expression_context_make_specific_type(left_type), semantic_context);
					}
				}
			}
		}

		// Check for unknowns
		if (datatype_is_unknown(left_type) || datatype_is_unknown(right_type)) {
			EXIT_ERROR(types.unknown_type);
		}

		// Handle pointer comparisons
		if (is_pointer_comparison)
		{
			bool unused = false;
			if (!types_are_equal(left_type, right_type)) {
				log_semantic_error(semantic_context, "Pointer comparison only works if both types are the same", expr, Node_Section::WHOLE);
				log_error_info_given_type(semantic_context, left_type);
				log_error_info_given_type(semantic_context, right_type);
			}
			else if (!datatype_is_pointer(left_type, &unused)) {
				log_semantic_error(semantic_context, "Value must be pointer for pointer-comparison", expr, Node_Section::WHOLE);
				log_error_info_given_type(semantic_context, left_type);
			}
			EXIT_VALUE(upcast(types.bool_type), true);
		}

		// Check if binop is a primitive operation (ints, floats, bools)
		if (types_are_equal(left_type, right_type))
		{
			Datatype* result_type = left_type;
			bool int_valid = false;
			bool float_valid = false;
			bool bool_valid = false;
			bool enum_valid = false;
			bool type_type_valid = false;
			bool address_valid = false;
			switch (binop_node.type)
			{
			case AST::Binop::ADDITION:
			case AST::Binop::SUBTRACTION:
			case AST::Binop::MULTIPLICATION:
			case AST::Binop::DIVISION:
				float_valid = true;
				int_valid = true;
				break;
			case AST::Binop::MODULO:
				int_valid = true;
				break;
			case AST::Binop::GREATER:
			case AST::Binop::GREATER_OR_EQUAL:
			case AST::Binop::LESS:
			case AST::Binop::LESS_OR_EQUAL:
				float_valid = true;
				int_valid = true;
				result_type = upcast(types.bool_type);
				enum_valid = true;
				address_valid = true;
				break;
			case AST::Binop::POINTER_EQUAL:
			case AST::Binop::POINTER_NOT_EQUAL: {
				panic("Should have been handled before");
				break;
			}
			case AST::Binop::EQUAL:
			case AST::Binop::NOT_EQUAL:
				float_valid = true;
				int_valid = true;
				bool_valid = true;
				enum_valid = true;
				type_type_valid = true;
				address_valid = true;
				result_type = upcast(types.bool_type);
				break;
			case AST::Binop::AND:
			case AST::Binop::OR:
				bool_valid = true;
				result_type = upcast(types.bool_type);
				break;
			case AST::Binop::INVALID:
				break;
			default: panic("");
			}

			bool types_are_valid = false;
			Datatype* operand_type = left_type;
			if (operand_type->type == Datatype_Type::PRIMITIVE)
			{
				auto primitive = downcast<Datatype_Primitive>(operand_type);
				switch (primitive->primitive_class)
				{
				case Primitive_Class::INTEGER:     types_are_valid = int_valid; break;
				case Primitive_Class::FLOAT:       types_are_valid = float_valid; break;
				case Primitive_Class::BOOLEAN:     types_are_valid = bool_valid; break;
				case Primitive_Class::ADDRESS:     types_are_valid = address_valid; break;
				case Primitive_Class::TYPE_HANDLE: types_are_valid = type_type_valid; break;
				default: panic("");
				}
			}
			else if (operand_type->type == Datatype_Type::ENUM) {
				types_are_valid = enum_valid;
			}

			if (types_are_valid)
			{
				EXIT_VALUE(result_type, true);
			}
		}

		// Handle pointer-arithmetic (address +/- isize/usize, address - address)
		{
			// Note: IR-Generator has code path that checks for these cases, and handles them seperately
			bool left_is_address = types_are_equal(left_type, upcast(types.address));
			bool right_is_address = types_are_equal(right_type, upcast(types.address));
			bool left_is_int =
				left_type->type == Datatype_Type::PRIMITIVE &&
				downcast<Datatype_Primitive>(left_type)->primitive_class == Primitive_Class::INTEGER;
			bool right_is_int =
				right_type->type == Datatype_Type::PRIMITIVE &&
				downcast<Datatype_Primitive>(right_type)->primitive_class == Primitive_Class::INTEGER;
			if (left_is_address && right_is_address)
			{
				if (binop_node.type == AST::Binop::SUBTRACTION) {
					EXIT_VALUE(upcast(types.isize), true);
				}
			}
			else if ((left_is_address && right_is_int) || (right_is_address && left_is_int)) {
				if (binop_node.type == AST::Binop::ADDITION || binop_node.type == AST::Binop::SUBTRACTION) {
					EXIT_VALUE(upcast(types.address), true);
				}
			}
		}

		// Check for operator overloads if it isn't a primitive operation
		// auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;
		// if (!types_are_valid)
		// {
		// 	Custom_Operator_Key key;
		// 	key.type = Context_Change_Type::BINARY_OPERATOR;
		// 	key.options.binop.binop = binop_node.type;
		// 	key.options.binop.left_type = left_type;
		// 	key.options.binop.right_type = right_type;
		// 	auto overload = symbol_table_query_custom_operator(operator_context, key);
		// 	if (overload != nullptr)
		// 	{
		// 		auto& custom_binop = overload->binop;
		// 		if (custom_binop.switch_left_and_right) {
		// 			expected_mods_left = custom_binop.function->signature->parameters[1].datatype->mods;
		// 			expected_mods_right = custom_binop.function->signature->parameters[0].datatype->mods;
		// 		}
		// 		else {
		// 			expected_mods_left = custom_binop.function->signature->parameters[0].datatype->mods;
		// 			expected_mods_right = custom_binop.function->signature->parameters[1].datatype->mods;
		// 		}
		// 		info->specifics.overload.function = custom_binop.function;
		// 		info->specifics.overload.switch_left_and_right = custom_binop.switch_left_and_right;
		// 		semantic_analyser_register_function_call(custom_binop.function);

		// 		types_are_valid = true;
		// 		result_type = custom_binop.function->signature->return_type().value;
		// 	}
		// }

		log_semantic_error(semantic_context, "Types aren't valid for binary operation", expr);
		log_error_info_binary_op_type(semantic_context, left_type, right_type);
		EXIT_ERROR(types.unknown_type);
	}
	default: {
		panic("Not all argument_expression covered!\n");
		break;
	}
	}

	panic("HEY");
	EXIT_ERROR(types.unknown_type);

#undef EXIT_VALUE
#undef EXIT_TYPE
#undef EXIT_ERROR
#undef EXIT_HARDCODED
#undef EXIT_FUNCTION
#undef EXIT_TYPE_OR_POLY
}

void expression_context_apply(
	Expression_Info* info, Expression_Context context, Semantic_Context* semantic_context, AST::Expression* expression, Node_Section error_section)
{
	auto& type_system = semantic_context->compilation_data->type_system;
	auto& types = type_system->predefined_types;

	// Set active expression info
	RESTORE_ON_SCOPE_EXIT(semantic_context->current_expression, info);
	assert(
		!(context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED && datatype_is_unknown(context.datatype)),
		"Should be checked when in context_make_specific_type"
	);

	Expression_Value_Info value_info = expression_info_get_value_info(info, type_system);

	switch (context.type)
	{
	case Expression_Context_Type::NOT_SPECIFIED:
	case Expression_Context_Type::ERROR_OCCURED: {
		info->cast_info = cast_info_make_simple(value_info.initial_type, Cast_Type::NO_CAST);
		return;
	}
	case Expression_Context_Type::SPECIFIC_TYPE_EXPECTED:
	{
		if (!expression_apply_cast_if_possible(expression, context.datatype, true, nullptr, semantic_context)) 
		{
			log_semantic_error(semantic_context, "Cannot cast to required type", expression, error_section);
			log_error_info_given_type(semantic_context, value_info.initial_type);
			log_error_info_expected_type(semantic_context, context.datatype);
			info->cast_info = cast_info_make_simple(context.datatype, Cast_Type::INVALID);
		}

		return;
	}
	case Expression_Context_Type::AUTO_DEREFERENCE:
	{
		Datatype* datatype = value_info.initial_type;
		bool was_pointer = datatype->type == Datatype_Type::POINTER;
		bool optional_encountered = false;
		while (datatype->type == Datatype_Type::POINTER)
		{
			Datatype_Pointer* pointer = downcast<Datatype_Pointer>(datatype);
			if (pointer->is_optional && !optional_encountered) {
				log_semantic_error(semantic_context, "Optional Pointer found in auto-dereference context", expression, error_section);
				log_error_info_given_type(semantic_context, value_info.initial_type);
				optional_encountered = true;
			}
			datatype = pointer->element_type;
		}

		if (was_pointer) {
			info->cast_info = cast_info_make_simple(datatype, Cast_Type::DEREFERENCE);
		}
		return;
	}
	default: panic("");
	}
}

Expression_Info* semantic_analyser_analyse_expression_any(AST::Expression* expression, Expression_Context context, Semantic_Context* semantic_context)
{
	auto result = semantic_analyser_analyse_expression_internal(expression, context, semantic_context);
	SET_ACTIVE_EXPR_INFO(result);

	// Apply context if we are dealing with values
	if (result->result_type == Expression_Result_Type::VALUE || result->result_type == Expression_Result_Type::CONSTANT) {
		expression_context_apply(result, context, semantic_context, expression);
	}
	return result;
}

// Can return a templated type if allow_poly_pattern is true
Datatype* semantic_analyser_analyse_expression_type(AST::Expression* expression, Semantic_Context* semantic_context, bool allow_poly_pattern)
{
	auto& type_system = semantic_context->compilation_data->type_system;
	auto& types = type_system->predefined_types;
	auto result = semantic_analyser_analyse_expression_any(expression, expression_context_make_dereference(), semantic_context);
	SET_ACTIVE_EXPR_INFO(result);

	switch (result->result_type)
	{
	case Expression_Result_Type::DATATYPE: {
		assert(!result->options.datatype->contains_pattern, "Result type would need to be different, e.g. poly-pattern");
		return result->options.datatype;
	}
	case Expression_Result_Type::POLYMORPHIC_PATTERN:
	{
		if (!allow_poly_pattern)
		{
			log_semantic_error(semantic_context, "Expected a normal type, but got a polymorphic pattern", expression);
			log_error_info_expression_result_type(semantic_context, result->result_type);
			return types.unknown_type;
		}
		return result->options.polymorphic_pattern;
	}
	case Expression_Result_Type::CONSTANT:
	case Expression_Result_Type::VALUE:
	{
		Datatype* result_type = expression_info_get_type(result, false, type_system);
		if (datatype_is_unknown(result_type)) {
			semantic_analyser_set_error_flag(true, semantic_context);
			return result_type;
		}
		if (!types_are_equal(result_type, types.type_handle))
		{
			log_semantic_error(semantic_context, "Expression cannot be converted to type", expression);
			log_error_info_given_type(semantic_context, result_type);
			return types.unknown_type;
		}

		// Otherwise try to convert to constant
		Upp_Constant constant;
		if (result->result_type == Expression_Result_Type::VALUE) {
			auto comptime_opt = expression_calculate_comptime_value(expression, "Expression is a type, but it isn't known at compile time", semantic_context);
			Datatype* result_type = types.unknown_type;
			if (comptime_opt.available) {
				constant = comptime_opt.value;
			}
			else {
				return types.unknown_type;
			}
		}
		else {
			constant = result->options.constant;
		}

		Datatype* final_type;
		auto type_index = upp_constant_to_value<u32>(constant);
		if (type_index >= (u32)type_system->internal_type_infos.size) {
			// Note: Always log this error, because this should never happen!
			log_semantic_error(semantic_context, "Expression contains invalid type handle", expression);
			final_type = upcast(types.unknown_type);
		}
		else {
			final_type = type_system->types[type_index];
		}
		expression_info_set_datatype(result, final_type, type_system);
		return final_type;
	}
	case Expression_Result_Type::NOTHING: {
		log_semantic_error(semantic_context, "Expected a type, given nothing", expression);
		log_error_info_expression_result_type(semantic_context, result->result_type);
		return types.unknown_type;
	}
	case Expression_Result_Type::POLYMORPHIC_STRUCT:
	{
		log_semantic_error(semantic_context, "Expected a specific type, given polymorphic struct. To get a pattern type, use StructName(_)", expression);
		log_error_info_expression_result_type(semantic_context, result->result_type);
		return types.unknown_type;
	}
	case Expression_Result_Type::HARDCODED_FUNCTION:
	case Expression_Result_Type::POLYMORPHIC_FUNCTION:
	case Expression_Result_Type::FUNCTION: {
		log_semantic_error(semantic_context, "Expected a type, given a function", expression);
		log_error_info_expression_result_type(semantic_context, result->result_type);
		return types.unknown_type;
	}
	default: panic("");
	}

	panic("Shouldn't happen");
	return types.unknown_type;
}

Datatype* semantic_analyser_analyse_expression_value(
	AST::Expression* expression, Expression_Context context, Semantic_Context* semantic_context, bool allow_nothing, bool allow_poly_pattern)
{
	auto& type_system = semantic_context->compilation_data->type_system;
	auto& types = type_system->predefined_types;

	auto result = semantic_analyser_analyse_expression_any(expression, context, semantic_context);
	SET_ACTIVE_EXPR_INFO(result);

	switch (result->result_type)
	{
	case Expression_Result_Type::CONSTANT:
	case Expression_Result_Type::VALUE: {
		return expression_info_get_type(result, false, type_system); // Here context was already applied (See analyse_expression_any), so we return
	}
	case Expression_Result_Type::DATATYPE: {
		expression_info_set_constant(
			result, types.type_handle, array_create_static_as_bytes(&result->options.datatype->type_handle, 1), AST::upcast(expression), 
			semantic_context
		);
		break;
	}
	case Expression_Result_Type::POLYMORPHIC_PATTERN:
	{
		if (!allow_poly_pattern) {
			log_semantic_error(semantic_context, "Polymorphic-Pattern cannot be used as value", expression);
			log_error_info_given_type(semantic_context, result->options.polymorphic_pattern);
			return types.unknown_type;
		}
		return result->options.polymorphic_pattern;
	}
	case Expression_Result_Type::FUNCTION:
	{
		// Function pointer read
		break;
	}
	case Expression_Result_Type::HARDCODED_FUNCTION:
	{
		log_semantic_error(semantic_context, "Cannot take address of hardcoded function", expression);
		return types.unknown_type;
	}
	case Expression_Result_Type::POLYMORPHIC_FUNCTION:
	{
		log_semantic_error(semantic_context, "Cannot convert polymorphic function to function pointer", expression);
		return types.unknown_type;
	}
	case Expression_Result_Type::POLYMORPHIC_STRUCT:
	{
		log_semantic_error(semantic_context, "Cannot convert polymorphic struct to type_handle", expression);
		return types.unknown_type;
	}
	case Expression_Result_Type::NOTHING: 
	{
		if (!allow_nothing) {
			log_semantic_error(semantic_context, "Expected value, got function with empty return", expression);
			return types.unknown_type;
		}
		return upcast(types.empty_struct_type);
	}
	default: panic("");
	}

	expression_context_apply(result, context, semantic_context, expression);
	return expression_info_get_type(result, false, type_system);
}



// OPERATOR CONTEXT
DynArray<Reachable_Operator_Table> symbol_table_query_reachable_operator_tables(
	Symbol_Table* symbol_table, Custom_Operator_Type op_type, Semantic_Context* semantic_context)
{
	auto scratch_arena = semantic_context->scratch_arena;
	auto compilation_data = semantic_context->compilation_data;

	// Find all reachable tables
	DynArray<Reachable_Table> reachable_tables = symbol_table_query_all_reachable_tables(
		symbol_table, symbol_query_info_make(Symbol_Access_Level::GLOBAL, Import_Type::OPERATORS, true), scratch_arena
	);

	// Get custom operator tables from reachable tables (And deduplicate)
	DynArray<Reachable_Operator_Table> operator_tables = DynArray<Reachable_Operator_Table>::create(scratch_arena);
	for (int i = 0; i < reachable_tables.size; i++)
	{
		auto& reachable = reachable_tables[i];
		auto operator_table = reachable.table->custom_operator_table;
		if (operator_table == nullptr) continue;

		// Check if operator table contains operator with given type
		if (!operator_table->contains_operator[(int)op_type]) continue;

		// Deduplicate
		bool found = false;
		for (int j = 0; j < operator_tables.size; j++) {
			auto& other = operator_tables[j];
			if (other.operator_table == operator_table) {
				found = true;
				other.depth = math_minimum(other.depth, reachable.depth);
				break;
			}
		}
		if (found) continue;

		// Otherwise add operator table
		Reachable_Operator_Table new_table;
		new_table.depth = reachable.depth;
		new_table.operator_table = operator_table;
		operator_tables.push_back(new_table);

		auto workload = operator_table->workloads[(int)op_type];
		if (workload != nullptr) {
			analysis_workload_add_dependency_internal(compilation_data->workload_executer, semantic_context->current_workload, upcast(workload));
		}
	}
	workload_executer_wait_for_dependency_resolution(semantic_context);

	return operator_tables;
}

u64 custom_operator_key_hash(Custom_Operator_Key* key)
{
	int type_as_int = (int)key->type;
	u64 hash = hash_i32(&type_as_int);
	switch (key->type)
	{
	case Custom_Operator_Type::CAST: {
		hash = hash_combine(hash, hash_pointer(key->options.custom_cast.from_type));
		hash = hash_combine(hash, hash_pointer(key->options.custom_cast.to_type));
		break;
	}
	}
	return hash;
}

bool custom_operator_key_equals(Custom_Operator_Key* a, Custom_Operator_Key* b) 
{
	if (a->type != b->type) {
		return false;
	}
	switch (a->type)
	{
	case Custom_Operator_Type::CAST: {
		return 
			a->options.custom_cast.from_type == b->options.custom_cast.from_type &&
			a->options.custom_cast.to_type == b->options.custom_cast.to_type;
	}
	}

	return true;
}

Custom_Operator_Table* symbol_table_install_custom_operator_table(
	Symbol_Table* symbol_table, Dynamic_Array<AST::Custom_Operator_Node*> custom_operators, bool create_workload, Semantic_Context* semantic_context)
{
	auto compilation_data = semantic_context->compilation_data;

	if (custom_operators.size == 0) return symbol_table->custom_operator_table;

	// Create new table 
	auto operator_table = new Custom_Operator_Table;
	operator_table->installed_operators = hashtable_create_empty<Custom_Operator_Key, DynArray<Custom_Operator_Install>>(
		1, custom_operator_key_hash, custom_operator_key_equals
	);
	for (int i = 0; i < (int)Custom_Operator_Type::MAX_ENUM_VALUE; i++) {
		operator_table->workloads[i] = nullptr;
		operator_table->contains_operator[i] = false;
	}

	dynamic_array_push_back(&compilation_data->allocated_custom_operator_tables, operator_table);
	symbol_table->custom_operator_table = operator_table;

	// Check which operators are included and create workloads
	auto parent_workload = semantic_context->current_workload;
	for (int i = 0; i < custom_operators.size; i++)
	{
		auto change = custom_operators[i];
		operator_table->contains_operator[(int)change->type] = true;
		if (!create_workload) continue;
		if (operator_table->workloads[(int)change->type] != 0) { // Only create one workload per change type
			continue;
		}

		auto workload = workload_executer_allocate_workload<Workload_Custom_Operator>(semantic_context->current_workload, compilation_data);
		workload->analysis_pass = semantic_context->current_pass;
		workload->operator_table = operator_table;
		workload->type_to_analyse = change->type;
		workload->symbol_table = symbol_table;
		workload->change_nodes = custom_operators;
		operator_table->workloads[(int)change->type] = workload;
	}

	return operator_table;
}

void analyse_custom_operator_node(
	AST::Custom_Operator_Node* custom_operator_node, Custom_Operator_Table* custom_operator_table, Semantic_Context* semantic_context)
{
	auto compilation_data = semantic_context->compilation_data;
	auto& ids = compilation_data->compiler->identifier_pool;
	auto& types = compilation_data->type_system->predefined_types;
	bool success = true;

	Call_Info* call_info = get_info(custom_operator_node->call_node, semantic_context, true);;
	*call_info = call_info_make_from_call_node(
		custom_operator_node->call_node, call_origin_make(custom_operator_node->type, compilation_data), &compilation_data->arena
	);
	if (!arguments_match_to_parameters(*call_info, semantic_context)) {
		success = false;
		call_info_analyse_all_arguments(call_info, false, semantic_context, true);
		return;
	}

	// Returns enum value as integer or -1 if error
	auto helper_analyse_parameter_as_comptime_enum = [&success, &call_info, &semantic_context](int param_index, int min_enum_value, int max_enum_value) -> int {
		// Get param/argument info
		auto& param_value = call_info->parameter_values[param_index];
		auto& param_info = call_info->origin.signature->parameters[param_index];
		if (param_value.value_type == Parameter_Value_Type::NOT_SET) return -1;
		assert(param_value.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "");
		if (param_value.options.argument_index == -1) return -1;
		auto& argument_info = call_info->argument_infos[param_value.options.argument_index];

		// Analyse argument (must not be analysed beforehand)
		assert(!param_info.datatype->contains_pattern, "Should be an enum type");
		assert(param_info.datatype->type == Datatype_Type::ENUM, "");
		semantic_analyser_analyse_expression_value(
			argument_info.expression, expression_context_make_specific_type(param_info.datatype), semantic_context
		);

		// Calculate comptime result
		auto result = expression_calculate_comptime_value(argument_info.expression, "Argument has to be comptime known", semantic_context);
		if (!result.available) {
			success = false;
			return -1;
		}

		// Check if enum value is valid
		i32 enum_value = upp_constant_to_value<i32>(result.value);
		if (enum_value < min_enum_value || enum_value >= max_enum_value) {
			log_semantic_error(semantic_context, "Enum value is invalid", argument_info.expression);
			success = false;
			return -1;
		}
		return enum_value;
	};

	auto helper_analyse_parameter_as_comptime_bool = [&success, &call_info, &types, &semantic_context](int param_index, bool default_value) -> bool {
		// Get param/argument info
		auto& param_value = call_info->parameter_values[param_index];
		auto& param_info = call_info->origin.signature->parameters[param_index];
		if (param_value.value_type == Parameter_Value_Type::NOT_SET) return default_value;
		assert(param_value.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "");
		if (param_value.options.argument_index == -1) return default_value;
		auto& argument_info = call_info->argument_infos[param_value.options.argument_index];

		// Analyse argument
		assert(!param_info.datatype->contains_pattern, "Should be bool type");
		assert(types_are_equal(param_info.datatype, upcast(types.bool_type)), "");
		semantic_analyser_analyse_expression_value(
			argument_info.expression, expression_context_make_specific_type(param_info.datatype), semantic_context
		);

		// Calculate comptime
		auto result = expression_calculate_comptime_value(argument_info.expression, "Argument has to be comptime known", semantic_context);
		if (!result.available) {
			success = false;
			return default_value;
		}
		return upp_constant_to_value<bool>(result.value) != 0;
	};

	auto helper_analyse_parameter_as_function =
		[&](int parameter_index, int expected_parameter_count, bool expected_return_value) -> Function_Progress*
	{
		// Get param/argument info
		auto& param_value = call_info->parameter_values[parameter_index];
		auto& param_info = call_info->origin.signature->parameters[parameter_index];
		assert(param_value.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "If argument matching succeeded, this should be set");
		assert(param_value.options.argument_index != -1, "");
		auto& argument_info = call_info->argument_infos[param_value.options.argument_index];

		auto expr_info = semantic_analyser_analyse_expression_any(argument_info.expression, expression_context_make_unspecified(), semantic_context);
		if (expr_info->result_type == Expression_Result_Type::VALUE && datatype_is_unknown(expr_info->options.value.datatype)) {
			success = false;
			return nullptr;
		}
		if (expr_info->result_type != Expression_Result_Type::FUNCTION && expr_info->result_type != Expression_Result_Type::POLYMORPHIC_FUNCTION) {
			success = false;
			log_semantic_error(semantic_context, "Argument must be a function or poly-function", argument_info.expression);
			return nullptr;
		}

		if (expr_info->result_type == Expression_Result_Type::FUNCTION) 
		{
			ModTree_Function* function = expr_info->options.function;
			auto signature = function->signature;
			int param_count = signature->parameters.size;
			bool has_return_type = signature->return_type_index != -1;

			if (function->function_type != ModTree_Function_Type::NORMAL) {
				log_semantic_error(semantic_context, "Extern functions are currently supported for custom casts", argument_info.expression);
				success = false;
				return nullptr;
			}

			if (has_return_type) { param_count -= 1; }
			if (param_count != expected_parameter_count) {
				log_semantic_error(semantic_context, "Function does not have the required number of arguments for overload", argument_info.expression);
				log_error_info_argument_count(semantic_context, param_count, expected_parameter_count);
				success = false;
			}

			if (expected_return_value != has_return_type) {
				if (expected_return_value) {
					log_semantic_error(semantic_context, "Function does not have a return value", argument_info.expression);
				}
				else {
					log_semantic_error(semantic_context, "Function must not have a return value", argument_info.expression);
				}
				success = false;
			}

			return function->options.normal.progress;
		}
		else 
		{
			Poly_Function poly_function = expr_info->options.poly_function;
			auto signature = poly_function.poly_header->signature;
			bool has_return_type = signature->return_type_index != -1;
			int param_count = 0;

			// Check parameters (Must not be comptime)
			for (int i = 0; i < signature->parameters.size; i++)
			{
				auto& param_info = signature->parameters[i];
				if (i == signature->return_type_index) continue; // Skip return type
				if (param_info.requires_named_addressing) continue; // Skip implicit parameters
				bool is_required_param = param_count < expected_parameter_count;
				param_count += 1;

				// Check non-polymorphic
				if (param_info.comptime_variable_index != -1) 
				{
					log_semantic_error(semantic_context, "Function must not have comptime parameters", argument_info.expression);
					success = false;
				}

				// Check datatype
				if (is_required_param)
				{
					Datatype* datatype = param_info.datatype;
					Type_Modifier_Info modifier_info = datatype_get_modifier_info(datatype);
					if (modifier_info.base_type->type == Datatype_Type::PATTERN_VARIABLE) {
						log_semantic_error(semantic_context, "Function parameter must not be pattern-variable", argument_info.expression);
						success = false;
					}
				}
			}

			if (param_count != expected_parameter_count) {
				log_semantic_error(semantic_context, "Function did not have the required number of arguments", argument_info.expression);
				log_error_info_argument_count(semantic_context, param_count, expected_parameter_count);
				success = false;
			}

			return poly_function.base_progress;
		}

		panic("Invalid code path");
		success = false;
		return nullptr;
	};

	switch (custom_operator_node->type)
	{
	case Custom_Operator_Type::CAST:
	{
		// #add_cast(cast_fn, auto_cast, call_by_reference, return_by_reference)
		//     cast_fn :: (from: From_Type) -> To_Type
		bool auto_cast              = helper_analyse_parameter_as_comptime_bool(1, false);
		bool from_by_reference      = helper_analyse_parameter_as_comptime_bool(2, false);
		bool to_by_reference        = helper_analyse_parameter_as_comptime_bool(3, false);
		Function_Progress* progress = helper_analyse_parameter_as_function(0, 1, true);

		auto helper_is_normal_pointer = [](Datatype* datatype) -> bool {
			if (datatype->type != Datatype_Type::POINTER) return false;
			return !downcast<Datatype_Pointer>(datatype)->is_optional;
		};

		Datatype* from_type = nullptr;
		Datatype* to_type   = nullptr;
		if (success)
		{
			assert(progress != nullptr, "");
			bool is_polymorphic = progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE;
			ModTree_Function* function = progress->function;
			auto& parameters = function->signature->parameters;
			// Check for errors
			// One parameter, one return, no comptime parameters
			int parameter_count = 0;
			bool contains_comptime_param = false;
			for (int i = 0; i < parameters.size; i++)
			{
				auto& param = parameters[i];
				if (i == function->signature->return_type_index) {
					to_type = param.datatype;
					break;
				}
				if (param.requires_named_addressing) continue;
				if (param.comptime_variable_index != -1) {
					contains_comptime_param = true;
				}
				if (i == 0) {
					from_type = param.datatype;
				}
				parameter_count += 1;
			}

			if (to_type == nullptr || from_type == nullptr || contains_comptime_param || parameter_count != 1)
			{
				log_semantic_error(semantic_context, "Function signature not suitable for custom_cast", upcast(custom_operator_node), Node_Section::FIRST_TOKEN);
				success = false;
				break;
			}
			if (from_by_reference && !helper_is_normal_pointer(from_type)) {
				log_semantic_error(semantic_context, "from_by_reference set, but from-type is not a normal pointer", upcast(custom_operator_node), Node_Section::FIRST_TOKEN);
				success = false;
				break;
			}
			if (to_by_reference && !helper_is_normal_pointer(to_type)) {
				log_semantic_error(semantic_context, "to_by_reference is set, but to-type is not a normal pointer", upcast(custom_operator_node), Node_Section::FIRST_TOKEN);
				success = false;
				break;
			}
		}

		if (!success) break;

		// Create custom operator and deduplicate
		Custom_Operator* custom_operator = nullptr;
		{
			Custom_Operator op;
			op.type = Custom_Operator_Type::CAST;
			op.options.custom_cast.function = progress;
			op.options.custom_cast.call_by_reference = from_by_reference;
			op.options.custom_cast.return_by_reference   = to_by_reference;
			op.options.custom_cast.auto_cast         = auto_cast;
			Custom_Operator** dedup = hashtable_find_element(&compilation_data->custom_operator_deduplication, op);
			if (dedup == nullptr) {
				custom_operator = compilation_data->arena.allocate<Custom_Operator>();
				*custom_operator = op;
				bool success = hashtable_insert_element(&compilation_data->custom_operator_deduplication, op, custom_operator);
				assert(success, "");
			}
			else {
				custom_operator = *dedup;
			}
		}

		// Install operator with key
		from_type = datatype_get_undecorated(from_type, true, true, true, true);
		to_type   = datatype_get_undecorated(to_type,   true, true, true, true);
		if (from_type->contains_pattern || to_type->contains_pattern) {
			from_type = nullptr;
			to_type = nullptr;
		}

		Custom_Operator_Key key;
		key.type = Custom_Operator_Type::CAST;
		key.options.custom_cast.from_type = from_type;
		key.options.custom_cast.to_type = to_type;

		Custom_Operator_Install install;
		install.custom_operator = custom_operator;
		install.node = custom_operator_node;

		DynArray<Custom_Operator_Install>* installs = hashtable_find_element(&custom_operator_table->installed_operators, key);
		if (installs == nullptr) {
			DynArray<Custom_Operator_Install> new_installs = DynArray<Custom_Operator_Install>::create(&compilation_data->arena, 1);
			new_installs.push_back(install);
			hashtable_insert_element(&custom_operator_table->installed_operators, key, new_installs);
		}
		else {
			installs->push_back(install);
		}

		break;
	}
	default: {
		log_semantic_error(semantic_context, "Other custom-operators are not yet implemented", upcast(custom_operator_node), Node_Section::FIRST_TOKEN);
		call_info_analyse_all_arguments(call_info, false, semantic_context, true);
		break;
	}
	}

	// // Returns unknown on error and sets error flag
	// auto poly_function_check_first_argument = [&](
	// 	Poly_Header* poly_header, AST::Node* error_report_node, Type_Mods* type_mods, bool allow_non_struct_template) -> Datatype*
	// {
	// 	*type_mods = type_mods_make(0, 0, 0, 0);
	// 	if (poly_header->parameter_nodes.size == 0) {
	// 		log_semantic_error(semantic_context, "Poly function must have at least one argument for custom operator", error_report_node);
	// 		success = false;
	// 		return types.unknown_type;
	// 	}

	// 	auto& param = poly_header->signature->parameters[0];
	// 	Datatype* type = param.datatype->base_type;
	// 	*type_mods = param.datatype->mods;
	// 	if (type->type != Datatype_Type::STRUCT_PATTERN)
	// 	{
	// 		if (!allow_non_struct_template) {
	// 			log_semantic_error(semantic_context, "Poly function first argument has to be a polymorphic struct", error_report_node);
	// 			success = false;
	// 			return types.unknown_type;
	// 		}
	// 		else {
	// 			return type;
	// 		}
	// 	}

	// 	// Note: we return the base type here, probably should use the workload instead...
	// 	auto struct_pattern = downcast<Datatype_Struct_Pattern>(type);
	// 	return upcast(struct_pattern->instance->header->origin.struct_workload->body_workload->struct_type);
	// };
	//  auto poly_function_check_argument_count_and_comptime = [&](
	//  	Poly_Header* poly_header, AST::Node* error_report_node, int required_parameter_count, bool comptime_param_allowed)
	//  {
	//  	// Check that no parameters are comptime (Because we may instanciate from code)
	//  	auto& params = poly_header->signature->parameters;
	//  	int normal_parameter_count = 0;
	//  	bool keep_count = true;
	//  	for (int i = 0; i < params.size; i++)
	//  	{
	//  		auto& param = params[i];
	//  		// Count normal parameters
	//  		if (keep_count && !param.must_not_be_set && !param.requires_named_addressing) {
	//  			normal_parameter_count += 1;
	//  		}
	//  		else {
	//  			keep_count = false;
	//  		}

	//  		if (!comptime_param_allowed && param.comptime_variable_index != -1 && !param.requires_named_addressing) {
	//  			log_semantic_error(semantic_context, "Poly function must not contain comptime parameters for custom operator", error_report_node);
	//  			success = false;
	//  			break;
	//  		}
	//  	}

	//  	if (required_parameter_count != -1 && normal_parameter_count != required_parameter_count)
	//  	{
	//  		log_semantic_error(semantic_context, "Poly function does not have the required parameter count for this custom operator", error_report_node);
	//  		success = false;
	//  	}
	// };

	// switch (change_node->type)
	// {
	// case Context_Change_Type::IMPORT:
	// {
	// 	auto& path = change_node->options.import_path;
	// 	auto symbol = path_lookup_resolve_to_single_symbol(path, Lookup_Type::NORMAL, true);

	// 	// Check if symbol is module
	// 	if (symbol->type != Symbol_Type::MODULE)
	// 	{
	// 		if (symbol->type == Symbol_Type::ERROR_SYMBOL) {
	// 			return;
	// 		}
	// 		log_semantic_error(semantic_context, "Operator context import requires a module to be passed, but other symbol was specified", upcast(path));
	// 		log_error_info_symbol(semantic_context, symbol);
	// 		return;
	// 	}
	// 	Upp_Module* upp_module = symbol->options.upp_module;

	// 	// Check if we already have this import
	// 	auto other_context = upp_module->symbol_table->operator_context;
	// 	for (int i = 0; i < context->context_imports.size; i++) {
	// 		if (context->context_imports[i] == other_context) {
	// 			return;
	// 		}
	// 	}
	// 	if (other_context == context) {
	// 		return;
	// 	}

	// 	dynamic_array_push_back(&context->context_imports, other_context);
	// 	return;
	// }
	// case Context_Change_Type::CAST_OPTION:
	// {
	// 	auto cast_option_expr = call_info->argument_infos[call_info->parameter_values[0].options.argument_index].expression;
	// 	auto cast_mode_expr = call_info->argument_infos[call_info->parameter_values[1].options.argument_index].expression;
	// 	Cast_Option option = (Cast_Option)helper_analyse_as_comptime_enum(0, (int)Cast_Option::MAX_ENUM_VALUE);
	// 	Cast_Mode cast_mode = (Cast_Mode)helper_analyse_as_comptime_enum(1, (int)Cast_Mode::MAX_ENUM_VALUE);

	// 	if (success)
	// 	{
	// 		Custom_Operator_Key key;
	// 		key.type = Context_Change_Type::CAST_OPTION;
	// 		key.options.cast_option = option;
	// 		Custom_Operator op;
	// 		op.cast_mode = cast_mode;
	// 		hashtable_insert_element(&context->custom_operators, key, op);
	// 	}
	// 	break;
	// }
	// case Context_Change_Type::BINARY_OPERATOR:
	// {
	// 	auto& param_binop = call_info->parameter_values[0];
	// 	auto& param_function = call_info->parameter_values[1];
	// 	auto& param_commutative = call_info->parameter_values[2];

	// 	// Find binop type
	// 	AST::Binop binop = AST::Binop::ADDITION;
	// 	if (param_binop.value_type != Parameter_Value_Type::NOT_SET)
	// 	{
	// 		assert(param_binop.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "");
	// 		auto binop_expr = call_info->argument_infos[param_binop.options.argument_index].expression;
	// 		if (binop_expr->type == AST::Expression_Type::LITERAL_READ && binop_expr->options.literal_read.type == Literal_Type::STRING)
	// 		{
	// 			auto expr_info = get_info(binop_expr, true);
	// 			expression_info_set_value(expr_info, upcast(types.c_string), true);
	// 			helper_parameter_set_analysed(param_binop);

	// 			auto binop_str = binop_expr->options.literal_read.options.string;
	// 			if (binop_str->size == 1)
	// 			{
	// 				int c = binop_str->characters[0];
	// 				if (c == '+') {
	// 					binop = AST::Binop::ADDITION;
	// 				}
	// 				else if (c == '-') {
	// 					binop = AST::Binop::SUBTRACTION;
	// 				}
	// 				else if (c == '*') {
	// 					binop = AST::Binop::MULTIPLICATION;
	// 				}
	// 				else if (c == '/') {
	// 					binop = AST::Binop::DIVISION;
	// 				}
	// 				else if (c == '%') {
	// 					binop = AST::Binop::MODULO;
	// 				}
	// 				else {
	// 					log_semantic_error(semantic_context, "Binop c_string must be one of +,-,*,/,%", upcast(binop_expr));
	// 					success = false;
	// 				}
	// 			}
	// 			else {
	// 				log_semantic_error(semantic_context, "Binop c_string must be one of +,-,*,/,%", upcast(binop_expr));
	// 				success = false;
	// 			}
	// 		}
	// 		else {
	// 			log_semantic_error(semantic_context, "Binop type must be c_string literal", upcast(binop_expr));
	// 			success = false;
	// 			semantic_analyser_analyse_expression_value(binop_expr, expression_context_make_specific_type(types.c_string));
	// 			helper_parameter_set_analysed(param_binop);
	// 		}
	// 	}

	// 	// Check commutative
	// 	bool is_commutative = analyse_parameter_as_comptime_bool(2);

	// 	// Check function
	// 	ModTree_Function* function = nullptr;
	// 	if (param_function.value_type != Parameter_Value_Type::NOT_SET)
	// 	{
	// 		assert(param_function.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "");
	// 		auto function_expr = call_info->argument_infos[param_function.options.argument_index].expression;
	// 		Expression_Info* info = semantic_analyser_analyse_expression_any(function_expr, expression_context_make_unspecified());
	// 		helper_parameter_set_analysed(param_function);
	// 		Type_Mods unused;
	// 		function = analyse_expression_info_as_function(function_expr, info, 2, true, &unused);
	// 	}

	// 	// Finalize result
	// 	if (success && function != nullptr) // nullptr check because of compiler warning...
	// 	{
	// 		Custom_Operator op;
	// 		op.binop.function = function;
	// 		op.binop.switch_left_and_right = false;
	// 		op.binop.left_mods = function->signature->parameters[0].datatype->mods;
	// 		op.binop.right_mods = function->signature->parameters[1].datatype->mods;
	// 		Custom_Operator_Key key;
	// 		key.type = Context_Change_Type::BINARY_OPERATOR;
	// 		key.options.binop.binop = binop;
	// 		key.options.binop.left_type = function->signature->parameters[0].datatype->base_type;
	// 		key.options.binop.right_type = function->signature->parameters[1].datatype->base_type;
	// 		hashtable_insert_element(&context->custom_operators, key, op);

	// 		if (is_commutative) {
	// 			Custom_Operator commutative_op = op;
	// 			commutative_op.binop.switch_left_and_right = true;
	// 			commutative_op.binop.left_mods = op.binop.right_mods;
	// 			commutative_op.binop.right_mods = op.binop.left_mods;
	// 			Custom_Operator_Key commutative_key = key;
	// 			commutative_key.options.binop.left_type = key.options.binop.right_type;
	// 			commutative_key.options.binop.right_type = key.options.binop.left_type;
	// 			hashtable_insert_element(&context->custom_operators, commutative_key, commutative_op);
	// 		}
	// 	}
	// 	else {
	// 		call_info_analyse_all_arguments(call_info, false, true, false);
	// 	}

	// 	break;
	// }
	// case Context_Change_Type::UNARY_OPERATOR:
	// {
	// 	auto& param_unop = call_info->parameter_values[0];
	// 	auto& param_function = call_info->parameter_values[1];

	// 	// Analyse unop type
	// 	AST::Unop unop = AST::Unop::NEGATE;
	// 	if (param_unop.value_type != Parameter_Value_Type::NOT_SET)
	// 	{
	// 		assert(param_unop.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "");
	// 		auto unop_expr = call_info->argument_infos[param_unop.options.argument_index].expression;
	// 		if (unop_expr->type == AST::Expression_Type::LITERAL_READ && unop_expr->options.literal_read.type == Literal_Type::STRING)
	// 		{
	// 			auto expr_info = get_info(unop_expr, true);
	// 			expression_info_set_value(expr_info, upcast(types.c_string), true);
	// 			helper_parameter_set_analysed(param_unop);

	// 			auto unop_str = unop_expr->options.literal_read.options.string;
	// 			if (unop_str->size == 1)
	// 			{
	// 				int c = unop_str->characters[0];
	// 				if (c == '-') {
	// 					unop = AST::Unop::NEGATE;
	// 				}
	// 				else if (c == '!') {
	// 					unop = AST::Unop::NOT;
	// 				}
	// 				else {
	// 					log_semantic_error(semantic_context, "Unop c_string must be either ! or -", upcast(unop_expr));
	// 					success = false;
	// 				}
	// 			}
	// 			else {
	// 				log_semantic_error(semantic_context, "Unop c_string must be either ! or -", upcast(unop_expr));
	// 				success = false;
	// 			}
	// 		}
	// 		else {
	// 			log_semantic_error(semantic_context, "Unop type must be c_string literal", upcast(unop_expr));
	// 			success = false;
	// 			semantic_analyser_analyse_expression_value(unop_expr, expression_context_make_specific_type(types.c_string));
	// 			helper_parameter_set_analysed(param_unop);
	// 		}
	// 	}

	// 	// Analyse function
	// 	ModTree_Function* function = nullptr;
	// 	Type_Mods mods = type_mods_make(false, 0, 0, 0);
	// 	if (param_function.value_type != Parameter_Value_Type::NOT_SET)
	// 	{
	// 		assert(param_function.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "");
	// 		auto function_expr = call_info->argument_infos[param_function.options.argument_index].expression;
	// 		Expression_Info* info = semantic_analyser_analyse_expression_any(function_expr, expression_context_make_unspecified());
	// 		helper_parameter_set_analysed(param_function);
	// 		function = analyse_expression_info_as_function(function_expr, info, 1, true, &mods);
	// 	}

	// 	// Finalize result
	// 	if (success && function != nullptr) // null-check so that compiler doesn't show warning
	// 	{
	// 		Custom_Operator op;
	// 		op.unop.function = function;
	// 		op.unop.mods = mods;
	// 		Custom_Operator_Key key;
	// 		key.type = Context_Change_Type::UNARY_OPERATOR;
	// 		key.options.unop.unop = unop;
	// 		key.options.unop.type = function->signature->parameters[0].datatype->base_type;
	// 		hashtable_insert_element(&context->custom_operators, key, op);
	// 	}
	// 	else {
	// 		call_info_analyse_all_arguments(call_info, false, true, false);
	// 	}
	// 	break;
	// }
	// case Context_Change_Type::CAST:
	// {
	// 	auto& param_function = call_info->parameter_values[0];
	// 	auto& param_cast_mode = call_info->parameter_values[1];

	// 	Cast_Mode cast_mode = (Cast_Mode)helper_analyse_as_comptime_enum(1, (int)Cast_Mode::MAX_ENUM_VALUE);

	// 	// Analyse function
	// 	Custom_Operator op;
	// 	Custom_Operator_Key key;
	// 	key.type = Context_Change_Type::CAST;
	// 	op.custom_cast.cast_mode = cast_mode;

	// 	if (param_function.value_type != Parameter_Value_Type::NOT_SET)
	// 	{
	// 		assert(param_function.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "");
	// 		auto function_expr = call_info->argument_infos[param_function.options.argument_index].expression;
	// 		Expression_Info* fn_info = semantic_analyser_analyse_expression_any(function_expr, expression_context_make_unspecified());
	// 		helper_parameter_set_analysed(param_function);
	// 		if (fn_info->result_type == Expression_Result_Type::FUNCTION)
	// 		{
	// 			ModTree_Function* function = analyse_expression_info_as_function(function_expr, fn_info, 1, true, &op.custom_cast.mods);
	// 			if (function != nullptr) {
	// 				op.custom_cast.is_polymorphic = false;
	// 				op.custom_cast.options.function = function;
	// 				key.options.custom_cast.from_type = function->signature->parameters[0].datatype->base_type;
	// 				key.options.custom_cast.to_type = function->signature->return_type().value;
	// 			}
	// 		}
	// 		else if (fn_info->result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION)
	// 		{
	// 			Poly_Header* poly_header = fn_info->options.poly_function.poly_header;
	// 			op.custom_cast.is_polymorphic = true;
	// 			op.custom_cast.options.poly_function = fn_info->options.poly_function;
	// 			key.options.custom_cast.from_type = poly_function_check_first_argument(poly_header, upcast(function_expr), &op.custom_cast.mods, false);
	// 			poly_function_check_argument_count_and_comptime(poly_header, upcast(function_expr), 1, false);

	// 			// Check return type
	// 			auto return_type_opt = poly_header->signature->return_type();
	// 			if (return_type_opt.available)
	// 			{
	// 				// If the return type is a normal type, just use it as key, otherwise use nullptr
	// 				auto return_type = return_type_opt.value;
	// 				if (return_type->contains_pattern) {
	// 					key.options.custom_cast.to_type = nullptr;
	// 				}
	// 				else {
	// 					key.options.custom_cast.to_type = return_type;
	// 				}
	// 			}
	// 			else {
	// 				success = false;
	// 				log_semantic_error(semantic_context, "For custom casts polymorphic function must have a return type", function_expr);
	// 			}
	// 		}
	// 		else if (fn_info->result_type == Expression_Result_Type::VALUE && datatype_is_unknown(expression_info_get_type(fn_info, false))) {
	// 			success = false;
	// 		}
	// 		else {
	// 			success = false;
	// 			log_semantic_error(semantic_context, "Function argument must be either a normal or polymorphic function", function_expr);
	// 		}
	// 	}

	// 	if (success) {
	// 		hashtable_insert_element(&context->custom_operators, key, op);
	// 	}
	// 	else {
	// 		call_info_analyse_all_arguments(call_info, false, true, false);
	// 	}
	// 	break;
	// }
	// case Context_Change_Type::ARRAY_ACCESS:
	// {
	// 	auto& param_function = call_info->parameter_values[0];

	// 	Custom_Operator op;
	// 	memory_zero(&op);
	// 	Custom_Operator_Key key;
	// 	key.type = Context_Change_Type::ARRAY_ACCESS;

	// 	if (param_function.value_type != Parameter_Value_Type::NOT_SET)
	// 	{
	// 		assert(param_function.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "");
	// 		auto function_expr = call_info->argument_infos[param_function.options.argument_index].expression;
	// 		auto fn_info = semantic_analyser_analyse_expression_any(function_expr, expression_context_make_unspecified());
	// 		helper_parameter_set_analysed(param_function);
	// 		if (fn_info->result_type == Expression_Result_Type::FUNCTION)
	// 		{
	// 			ModTree_Function* function = analyse_expression_info_as_function(function_expr, fn_info, 2, true, &op.array_access.mods);
	// 			op.array_access.is_polymorphic = false;
	// 			op.array_access.options.function = function;
	// 			if (function != nullptr) {
	// 				key.options.array_access.array_type = function->signature->parameters[0].datatype->base_type;
	// 				op.array_access.mods = function->signature->parameters[0].datatype->mods;
	// 			}
	// 		}
	// 		else if (fn_info->result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION)
	// 		{
	// 			Poly_Header* poly_header = fn_info->options.poly_function.poly_header;
	// 			op.array_access.is_polymorphic = true;
	// 			op.array_access.options.poly_function = fn_info->options.poly_function;
	// 			key.options.array_access.array_type = poly_function_check_first_argument(poly_header, upcast(function_expr), &op.array_access.mods, false);
	// 			poly_function_check_argument_count_and_comptime(poly_header, upcast(function_expr), 2, false);
	// 		}
	// 		else if (fn_info->result_type == Expression_Result_Type::VALUE && datatype_is_unknown(expression_info_get_type(fn_info, false))) {
	// 			success = false;
	// 		}
	// 		else {
	// 			success = false;
	// 			log_semantic_error(semantic_context, "Function argument must be either a normal or polymorphic function", function_expr);
	// 		}
	// 	}

	// 	if (success) {
	// 		hashtable_insert_element(&context->custom_operators, key, op);
	// 	}
	// 	else {
	// 		call_info_analyse_all_arguments(call_info, false, true, false);
	// 	}

	// 	break;
	// }
	// case Context_Change_Type::ITERATOR:
	// {
	// 	Custom_Operator_Key key;
	// 	key.type = Context_Change_Type::ITERATOR;
	// 	Custom_Operator op;
	// 	auto& iter = op.iterator;
	// 	iter.is_polymorphic = false; // Depends on the type of the create function expression

	// 	Datatype* iterator_type = types.unknown_type; // Note: Iterator type is not available for polymorphic functions
	// 	auto& param_create_fn = call_info->parameter_values[0];
	// 	if (param_create_fn.value_type != Parameter_Value_Type::NOT_SET)
	// 	{
	// 		assert(param_create_fn.value_type == Parameter_Value_Type::ARGUMENT_EXPRESSION, "");
	// 		auto function_expr = call_info->argument_infos[param_create_fn.options.argument_index].expression;
	// 		auto fn_info = semantic_analyser_analyse_expression_any(function_expr, expression_context_make_unspecified());
	// 		helper_parameter_set_analysed(param_create_fn);
	// 		if (fn_info->result_type == Expression_Result_Type::FUNCTION)
	// 		{
	// 			ModTree_Function* function = fn_info->options.function;
	// 			iter.options.normal.create = function;
	// 			auto& parameters = function->signature->parameters;
	// 			int param_count = parameters.size - (function->signature->return_type_index == -1 ? 0 : 1);
	// 			if (param_count == 1) {
	// 				key.options.iterator.datatype = parameters[0].datatype->base_type;
	// 				op.iterator.iterable_mods = parameters[0].datatype->mods;
	// 				if (types_are_equal(key.options.iterator.datatype, types.unknown_type)) {
	// 					success = false;
	// 				}
	// 			}
	// 			else {
	// 				log_semantic_error(semantic_context, "Iterator create function must have exactly one argument", upcast(function_expr));
	// 				success = false;
	// 			}

	// 			if (function->signature->return_type().available) {
	// 				iterator_type = function->signature->return_type().value;
	// 				if (datatype_is_unknown(iterator_type)) {
	// 					success = false;
	// 				}
	// 			}
	// 			else {
	// 				log_semantic_error(semantic_context, "iterator_create function must return a value (iterator)", upcast(function_expr));
	// 				success = false;
	// 			}
	// 		}
	// 		else if (fn_info->result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION)
	// 		{
	// 			Poly_Header* poly_header = fn_info->options.poly_function.poly_header;
	// 			iter.is_polymorphic = true;
	// 			iter.options.polymorphic.fn_create = fn_info->options.poly_function;

	// 			poly_function_check_argument_count_and_comptime(poly_header, upcast(function_expr), 1, false);
	// 			key.options.iterator.datatype = poly_function_check_first_argument(poly_header, upcast(function_expr), &op.iterator.iterable_mods, false);

	// 			// Function must have return type
	// 			if (poly_header->signature->return_type_index == -1) {
	// 				log_semantic_error(semantic_context, "iterator_create function must return a value (iterator)", upcast(function_expr));
	// 				success = false;
	// 			}
	// 		}
	// 		else if (fn_info->result_type == Expression_Result_Type::VALUE && datatype_is_unknown(expression_info_get_type(fn_info, false))) {
	// 			success = false;
	// 		}
	// 		else {
	// 			log_semantic_error(semantic_context, "add_iterator argument must be a function", upcast(function_expr));
	// 			success = false;
	// 		}
	// 	}

	// 	// Function_index: 1 = has_next, 2 = next, 3 = get_value
	// 	auto analyse_iter_fn = [&](Parameter_Value& param_value, ModTree_Function** function_pointer_to_set,
	// 		Poly_Function* poly_pointer_to_set, bool should_have_return_type, bool return_should_be_boolean)
	// 		{
	// 			if (param_value.value_type == Parameter_Value_Type::NOT_SET || !success) return;

	// 			auto fn_expr = call_info->argument_infos[param_value.options.argument_index].expression;
	// 			auto fn_info = semantic_analyser_analyse_expression_any(fn_expr, expression_context_make_unspecified());
	// 			helper_parameter_set_analysed(param_value);

	// 			if (fn_info->result_type == Expression_Result_Type::FUNCTION)
	// 			{
	// 				ModTree_Function* function = fn_info->options.function;
	// 				if (!iter.is_polymorphic) {
	// 					*function_pointer_to_set = function;
	// 				}
	// 				else {
	// 					log_semantic_error(semantic_context, "Expected polymorphic function, as iter create function is also polymorphic", upcast(fn_expr));
	// 					success = false;
	// 				}

	// 				auto& parameters = function->signature->parameters;
	// 				int param_count = parameters.size - (function->signature->return_type_index == -1 ? 0 : 1);
	// 				if (param_count == 1)
	// 				{
	// 					Datatype* arg_type = parameters[0].datatype->base_type;
	// 					if (datatype_is_unknown(arg_type)) {
	// 						success = false;
	// 					}
	// 					else if (!(types_are_equal(arg_type->base_type, iterator_type->base_type) &&
	// 						datatype_check_if_auto_casts_to_other_mods(arg_type, iterator_type->mods, false)))
	// 					{
	// 						log_semantic_error(semantic_context, 
	// 							"Function parameter type must be compatible with iterator type (Create function return type)",
	// 							upcast(fn_expr)
	// 						);
	// 						log_error_info_given_type(semantic_context, arg_type);
	// 						log_error_info_expected_type(semantic_context, iterator_type);
	// 						success = false;
	// 					}
	// 				}
	// 				else {
	// 					log_semantic_error(semantic_context, "Function must have exactly one argument", upcast(fn_expr));
	// 					success = false;
	// 				}

	// 				// Check return value
	// 				if (function->signature->return_type().available)
	// 				{
	// 					if (!should_have_return_type) {
	// 						log_semantic_error(semantic_context, "Function should not have a return value", upcast(fn_expr));
	// 						success = false;
	// 					}
	// 					else {
	// 						if (return_should_be_boolean && !types_are_equal(upcast(types.bool_type), function->signature->return_type().value)) {
	// 							log_semantic_error(semantic_context, "Function return type should be bool", upcast(fn_expr));
	// 							success = false;
	// 						}
	// 					}
	// 				}
	// 				else {
	// 					if (should_have_return_type) {
	// 						log_semantic_error(semantic_context, "Function should have a return value", upcast(fn_expr));
	// 						success = false;
	// 					}
	// 				}
	// 			}
	// 			else if (fn_info->result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION)
	// 			{
	// 				Poly_Header* poly_header = fn_info->options.poly_function.poly_header;
	// 				if (iter.is_polymorphic) {
	// 					*poly_pointer_to_set = fn_info->options.poly_function;
	// 				}
	// 				else {
	// 					log_semantic_error(semantic_context, "Expected normal (non-polymorphic) function for has_next function", upcast(fn_expr));
	// 					success = false;
	// 					return;
	// 				}

	// 				// Check parameters
	// 				poly_function_check_argument_count_and_comptime(poly_header, upcast(fn_expr), 1, false);
	// 				Type_Mods unused;
	// 				Datatype* arg_type = poly_function_check_first_argument(poly_header, upcast(fn_expr), &unused, false);
	// 				if (!success) return;

	// 				// // Note: We don't know the iterator type from the make function, as this is currently not provided by the
					//      polymorphic base analysis. So here we only check that everything works
	// 				if (types_are_equal(arg_type->base_type, types.unknown_type)) {
	// 					success = false;
	// 				}

	// 				// Check return type
	// 				auto return_type_opt = poly_header->signature->return_type();
	// 				if (return_type_opt.available)
	// 				{
	// 					Datatype* return_type = return_type_opt.value;
	// 					if (!should_have_return_type) {
	// 						log_semantic_error(semantic_context, "Function should not have a return value", upcast(fn_expr));
	// 						success = false;
	// 					}
	// 					else {
	// 						if (return_should_be_boolean && !types_are_equal(upcast(types.bool_type), return_type)) {
	// 							log_semantic_error(semantic_context, "Function return type should be bool", upcast(fn_expr));
	// 							success = false;
	// 						}
	// 					}
	// 				}
	// 				else {
	// 					if (should_have_return_type) {
	// 						log_semantic_error(semantic_context, "Function should have a return value", upcast(fn_expr));
	// 						success = false;
	// 					}
	// 				}
	// 			}
	// 			else if (fn_info->result_type == Expression_Result_Type::VALUE && datatype_is_unknown(expression_info_get_type(fn_info, false))) {
	// 				success = false;
	// 			}
	// 			else {
	// 				log_semantic_error(semantic_context, "Argument must be a function", upcast(fn_expr));
	// 				success = false;
	// 			}
	// 		};

	// 	analyse_iter_fn(call_info->parameter_values[1], &op.iterator.options.normal.has_next, &op.iterator.options.polymorphic.has_next, true, true);
	// 	analyse_iter_fn(call_info->parameter_values[2], &op.iterator.options.normal.next, &op.iterator.options.polymorphic.next, false, false);
	// 	analyse_iter_fn(call_info->parameter_values[3], &op.iterator.options.normal.get_value, &op.iterator.options.polymorphic.get_value, true, false);

	// 	if (success) {
	// 		hashtable_insert_element(&context->custom_operators, key, op);
	// 	}
	// 	else {
	// 		call_info_analyse_all_arguments(call_info, false, true, false);
	// 	}
	// 	break;
	// }
	// case Context_Change_Type::INVALID: break;
	// default: panic("");
	// }
}



// STATEMENTS
bool code_block_is_loop(AST::Code_Block * block)
{
	if (block != 0 && block->base.parent->type == AST::Node_Type::STATEMENT) {
		auto parent = (AST::Statement*)block->base.parent;
		return
			parent->type == AST::Statement_Type::WHILE_STATEMENT ||
			parent->type == AST::Statement_Type::FOR_LOOP ||
			parent->type == AST::Statement_Type::FOREACH_LOOP;
	}
	return false;
}

bool code_block_is_defer(AST::Code_Block * block)
{
	if (block != 0 && block->base.parent->type == AST::Node_Type::STATEMENT) {
		auto parent = (AST::Statement*)block->base.parent;
		return parent->type == AST::Statement_Type::DEFER;
	}
	return false;
}

bool inside_defer(Semantic_Context* semantic_context)
{
	// TODO: Probably doesn't work inside a bake!
	auto& block_stack = semantic_context->block_stack;
	for (int i = block_stack.size - 1; i > 0; i--)
	{
		auto block = block_stack[i];
		if (code_block_is_defer(block)) {
			return true;
		}
	}
	return false;
}

Control_Flow semantic_analyser_analyse_statement(AST::Statement* statement, Semantic_Context* semantic_context)
{
	auto compilation_data = semantic_context->compilation_data;
	auto current_workload = semantic_context->current_workload;
	auto& type_system = compilation_data->type_system;
	auto& types = type_system->predefined_types;
	auto info = get_info(statement, semantic_context, true);
	info->flow = Control_Flow::SEQUENTIAL;
#define EXIT(flow_result) { info->flow = flow_result; return flow_result; };

	switch (statement->type)
	{
	case AST::Statement_Type::RETURN_STATEMENT:
	{
		auto& return_stat = statement->options.return_value;
		ModTree_Function* current_function = semantic_context->current_function;
		assert(current_function != 0, "No statements outside of function body");
		Optional<Datatype*> expected_return_type = optional_make_failure<Datatype*>();
		if (current_function->signature != 0) {
			expected_return_type = current_function->signature->return_type();
			if (expected_return_type.available && expected_return_type.value->contains_pattern) {
				expected_return_type = optional_make_success(types.unknown_type);
			}
		}
		if (current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS) {
			auto bake_progress = downcast<Workload_Bake_Analysis>(current_workload)->progress;
			if (bake_progress->result_type != 0) {
				expected_return_type = optional_make_success(bake_progress->result_type);
			}
		}

		if (return_stat.available)
		{
			Expression_Context context = expression_context_make_unspecified();
			if (expected_return_type.available) {
				context = expression_context_make_specific_type(expected_return_type.value);
			}

			auto return_type = semantic_analyser_analyse_expression_value(return_stat.value, context, semantic_context);
			bool is_unknown = datatype_is_unknown(return_type);
			if (expected_return_type.available) {
				is_unknown = is_unknown || datatype_is_unknown(expected_return_type.value);
			}

			if (current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS)
			{
				auto bake_progress = downcast<Workload_Bake_Analysis>(current_workload)->progress;
				if (bake_progress->result_type == 0) {
					bake_progress->result_type = return_type;
				}
			}
			else
			{
				if (!expected_return_type.available) {
					log_semantic_error(semantic_context, "Function does not have a return value", return_stat.value);
				}
				else if (!types_are_equal(expected_return_type.value, return_type) && !is_unknown) {
					log_semantic_error(semantic_context, "Return type does not match the declared return type", statement);
					log_error_info_given_type(semantic_context, return_type);
					log_error_info_expected_type(semantic_context, expected_return_type.value);
				}
			}
		}
		else
		{
			if (current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS) {
				log_semantic_error(semantic_context, "Must return a value in bake", statement);
			}
			else
			{
				if (expected_return_type.available) {
					log_semantic_error(semantic_context, "Function requires a return value", statement);
					log_error_info_expected_type(semantic_context, expected_return_type.value);
				}
			}
		}

		if (inside_defer(semantic_context)) {
			log_semantic_error(semantic_context, "Cannot return in a defer block", statement, Node_Section::KEYWORD);
		}
		EXIT(Control_Flow::RETURNS);
	}
	case AST::Statement_Type::BREAK_STATEMENT:
	case AST::Statement_Type::CONTINUE_STATEMENT:
	{
		bool is_continue = statement->type == AST::Statement_Type::CONTINUE_STATEMENT;
		Optional<String*> search_id_opt = is_continue ? statement->options.continue_name : statement->options.break_name;
		AST::Code_Block* found_block = 0;
		auto& block_stack = semantic_context->block_stack;

		// INFO: Block 0 is always the function body, which cannot be a target of break/continue
		if (search_id_opt.available)
		{
			for (int i = block_stack.size - 1; i > 0; i--) {
				auto id = block_stack[i]->block_id;
				if (id.available && id.value == search_id_opt.value) {
					found_block = block_stack[i];
					break;
				}
			}
		}
		else
		{
			for (int i = block_stack.size - 1; i > 0; i--)
			{
				auto block = block_stack[i];
				// Note: Break and continue without a id only work on loops
				if (!code_block_is_loop(block)) continue;
				found_block = block_stack[i];
				break;
			}
		}

		if (found_block == 0)
		{
			if (search_id_opt.available) {
				log_semantic_error(semantic_context, "Block with given Label not found", statement);
				log_error_info_id(semantic_context, search_id_opt.value);
			}
			else {
				log_semantic_error(semantic_context, "No surrounding block supports this operation", statement);
			}
			EXIT(Control_Flow::RETURNS);
		}
		else
		{
			info->specifics.block = found_block;
			if (is_continue && !code_block_is_loop(found_block)) {
				log_semantic_error(semantic_context, "Continue can only be used on loops", statement);
				EXIT(Control_Flow::SEQUENTIAL);
			}
		}

		if (!is_continue)
		{
			// Mark all previous Code-Blocks as Sequential flow, since they contain a path to a break
			auto& block_stack = semantic_context->block_stack;
			for (int i = block_stack.size - 1; i >= 0; i--)
			{
				auto block = block_stack[i];
				auto prev = get_info(block, semantic_context);
				if (!prev->control_flow_locked && semantic_context->statement_reachable) {
					prev->control_flow_locked = true;
					prev->flow = Control_Flow::SEQUENTIAL;
				}
				if (block == found_block) break;
			}
		}
		EXIT(Control_Flow::STOPS);
	}
	case AST::Statement_Type::DEFER:
	{
		semantic_analyser_analyse_block(statement->options.defer_block, semantic_context);
		if (inside_defer(semantic_context)) {
			log_semantic_error(semantic_context, "Currently nested defers aren't allowed", statement);
			EXIT(Control_Flow::SEQUENTIAL);
		}
		EXIT(Control_Flow::SEQUENTIAL);
	}
	case AST::Statement_Type::DEFER_RESTORE:
	{
		auto& restore = statement->options.defer_restore;
		if (inside_defer(semantic_context)) {
			log_semantic_error(semantic_context, "Currently nested defers aren't allowed", statement, Node_Section::FIRST_TOKEN);
		}

		auto left_type = semantic_analyser_analyse_expression_value(restore.left_side, expression_context_make_unspecified(), semantic_context);

		// Check for errors
		if (!expression_has_memory_address(restore.left_side, semantic_context)) {
			log_semantic_error(semantic_context, "Cannot assign to a temporary value", upcast(restore.left_side));
		}

		semantic_analyser_analyse_expression_value(restore.right_side, expression_context_make_specific_type(left_type), semantic_context);
		EXIT(Control_Flow::SEQUENTIAL);
	}
	case AST::Statement_Type::EXPRESSION_STATEMENT:
	{
		auto& expression_node = statement->options.expression;
		// Note(Martin): This is a special case, in expression statements the expression may not have a value
		semantic_analyser_analyse_expression_value(expression_node, expression_context_make_unspecified(), semantic_context, true);
		EXIT(Control_Flow::SEQUENTIAL);
	}
	case AST::Statement_Type::BLOCK:
	{
		auto flow = semantic_analyser_analyse_block(statement->options.block, semantic_context);
		EXIT(flow);
	}
	case AST::Statement_Type::IF_STATEMENT:
	{
		auto& if_node = statement->options.if_statement;

		auto condition_type = semantic_analyser_analyse_expression_value(
			if_node.condition, expression_context_make_specific_type(upcast(types.bool_type)),
			semantic_context
		);
		auto true_flow = semantic_analyser_analyse_block(statement->options.if_statement.block, semantic_context);
		Control_Flow false_flow;
		if (if_node.else_block.available) {
			false_flow = semantic_analyser_analyse_block(statement->options.if_statement.else_block.value, semantic_context);
		}
		else {
			EXIT(Control_Flow::SEQUENTIAL;) // If no else, if is always sequential, since it's possible to skip the block
		}

		// Combine flows as given by conditional flow rules
		if (true_flow == false_flow) {
			return true_flow;
		}
		if (true_flow == Control_Flow::SEQUENTIAL || false_flow == Control_Flow::SEQUENTIAL) {
			EXIT(Control_Flow::SEQUENTIAL);
		}
		EXIT(Control_Flow::STOPS);
	}
	case AST::Statement_Type::SWITCH_STATEMENT:
	{
		auto& switch_node = statement->options.switch_statement;
		auto& switch_info = get_info(statement, semantic_context)->specifics.switch_statement;
		switch_info.structure = nullptr;

		auto datatype = semantic_analyser_analyse_expression_value(switch_node.condition, expression_context_make_dereference(), semantic_context);

		// Check switch value
		Datatype_Struct* structure = nullptr;
		if (datatype->type == Datatype_Type::STRUCT)
		{
			type_wait_for_size_info_to_finish(datatype, semantic_context);
			structure = downcast<Datatype_Struct>(datatype);
			if (structure->subtypes.size != 0) {
				datatype = structure->tag_member.type;
				switch_info.structure = structure;
			}
			else {
				log_semantic_error(semantic_context, "Switch value must be a struct with subtypes or an enum!", switch_node.condition);
				datatype = types.unknown_type;
				structure = nullptr;
			}
		}

		if (datatype_is_unknown(datatype)) {
			semantic_analyser_set_error_flag(true, semantic_context);
		}
		else if (datatype->type != Datatype_Type::ENUM)
		{
			log_semantic_error(semantic_context, "Switch only works on either enum or struct subtypes", switch_node.condition);
			log_error_info_given_type(semantic_context, datatype);
		}

		Expression_Context case_context = datatype->type == Datatype_Type::ENUM ?
			expression_context_make_specific_type(datatype) : expression_context_make_unspecified();
		Control_Flow switch_flow = Control_Flow::SEQUENTIAL;
		bool default_found = false;
		for (int i = 0; i < switch_node.cases.size; i++)
		{
			auto& case_node = switch_node.cases[i];
			auto case_info = get_info(case_node, semantic_context, true);
			case_info->is_valid = false;
			case_info->variable_symbol = 0;
			case_info->case_value = -1;

			// Analyse case value
			if (case_node->value.available)
			{
				auto case_type = semantic_analyser_analyse_expression_value(case_node->value.value, case_context, semantic_context);
				// Calculate case value
				auto comptime = expression_calculate_comptime_value(case_node->value.value, "Switch case must be known at compile time", semantic_context);
				if (comptime.available)
				{
					int case_value = upp_constant_to_value<int>(comptime.value);
					if (datatype->type == Datatype_Type::ENUM)
					{
						auto enum_member = enum_type_find_member_by_value(downcast<Datatype_Enum>(datatype), case_value);
						if (enum_member.available) {
							case_info->is_valid = true;
							case_info->case_value = case_value;
						}
						else {
							log_semantic_error(semantic_context, "Case value is not a valid enum member", case_node->value.value);
							log_error_info_expected_type(semantic_context, datatype);
						}
					}
				}
				else {
					case_info->is_valid = false;
					case_info->case_value = -1;
					semantic_analyser_set_error_flag(true, semantic_context);
				}
			}
			else
			{
				// Default case
				if (default_found) {
					log_semantic_error(semantic_context, "Only one default section allowed in switch", statement);
				}
				default_found = true;
			}

			// If a variable name is given, create a new symbol for it
			Symbol_Table* restore_table = semantic_context->current_symbol_table;
			SCOPE_EXIT(semantic_context->current_symbol_table = restore_table);
			if (case_node->variable_definition.available)
			{
				Symbol_Table* case_table = symbol_table_create_with_parent(restore_table, Symbol_Access_Level::INTERNAL, compilation_data);
				semantic_context->current_symbol_table = case_table;
				Symbol* var_symbol = symbol_table_define_symbol(
					case_table, case_node->variable_definition.value->name, Symbol_Type::VARIABLE, upcast(case_node->variable_definition.value),
					Symbol_Access_Level::INTERNAL, compilation_data
				);
				var_symbol->options.variable_type = types.unknown_type;
				case_info->variable_symbol = var_symbol;

				if (structure != nullptr)
				{
					if (case_info->is_valid)
					{
						// Variable is a pointer to the subtype
						Datatype* result_subtype = upcast(structure->subtypes[case_info->case_value - 1]);
						var_symbol->options.variable_type = result_subtype;
					}
				}
				else {
					if (!datatype_is_unknown(datatype)) {
						log_semantic_error(semantic_context, 
							"Case variables are only valid if the switch value is a struct with subtypes", 
							upcast(case_node), Node_Section::END_TOKEN
						);
					}
				}
			}

			// Analyse block and block flow
			auto case_flow = semantic_analyser_analyse_block(case_node->block, semantic_context);
			if (i == 0) {
				switch_flow = case_flow;
			}
			else
			{
				// Combine flows according to the Conditional Branch rules
				if (switch_flow != case_flow) {
					if (switch_flow == Control_Flow::SEQUENTIAL || case_flow == Control_Flow::SEQUENTIAL) {
						switch_flow = Control_Flow::SEQUENTIAL;
					}
					else {
						switch_flow = Control_Flow::STOPS;
					}
				}
			}
		}

		// Check if given cases are unique
		int unique_count = 0;
		for (int i = 0; i < switch_node.cases.size; i++)
		{
			auto case_node = switch_node.cases[i];
			auto case_info = get_info(case_node, semantic_context);
			if (!case_info->is_valid) continue;

			bool is_unique = true;
			for (int j = i + 1; j < statement->options.switch_statement.cases.size; j++)
			{
				auto& other_case = statement->options.switch_statement.cases[j];
				auto other_info = get_info(other_case, semantic_context);
				if (!other_info->is_valid) continue;
				if (case_info->case_value == other_info->case_value) {
					log_semantic_error(semantic_context, "Case is not unique", AST::upcast(other_case));
					is_unique = false;
					break;
				}
			}
			if (is_unique) {
				unique_count++;
			}
		}

		// Check if all possible cases are handled
		if (!default_found && datatype->type == Datatype_Type::ENUM) {
			if (unique_count < downcast<Datatype_Enum>(datatype)->members.size) {
				log_semantic_error(semantic_context, "Not all cases are handled by switch", statement, Node_Section::KEYWORD);
			}
		}
		return switch_flow;
	}
	case AST::Statement_Type::WHILE_STATEMENT:
	{
		auto& while_node = statement->options.while_statement;
		semantic_analyser_analyse_expression_value(
			while_node.condition, expression_context_make_specific_type(upcast(types.bool_type)), semantic_context
		);

		semantic_analyser_analyse_block(while_node.block, semantic_context);
		EXIT(Control_Flow::SEQUENTIAL); // Loops are always sequential, since the condition may not be met before the first iteration
	}
	case AST::Statement_Type::FOR_LOOP:
	{
		auto& for_loop = statement->options.for_loop;

		// Create new table for loop variable
		auto symbol_table = symbol_table_create_with_parent(semantic_context->current_symbol_table, Symbol_Access_Level::INTERNAL, compilation_data);
		info->specifics.for_loop.symbol_table = symbol_table;

		// Analyse loop variable 
		{
			Symbol* symbol = symbol_table_define_symbol(
				symbol_table, for_loop.loop_variable_definition->name, 
				Symbol_Type::VARIABLE, upcast(for_loop.loop_variable_definition), Symbol_Access_Level::INTERNAL, compilation_data
			);
			get_info(for_loop.loop_variable_definition, semantic_context, true)->symbol = symbol;

			info->specifics.for_loop.loop_variable_symbol = symbol;
			Expression_Context context = expression_context_make_unspecified();
			if (for_loop.loop_variable_type.available) {
				context = expression_context_make_specific_type(
					semantic_analyser_analyse_expression_type(for_loop.loop_variable_type.value, semantic_context)
				);
			}
			symbol->options.variable_type = semantic_analyser_analyse_expression_value(for_loop.initial_value, context, semantic_context);
		}
		// Use new symbol table for condition + increment
		RESTORE_ON_SCOPE_EXIT(semantic_context->current_symbol_table, symbol_table);

		// Analyse condition
		semantic_analyser_analyse_expression_value(for_loop.condition, expression_context_make_specific_type(upcast(types.bool_type)), semantic_context);

		// Analyse increment statement
		{
			auto flow = semantic_analyser_analyse_statement(for_loop.increment_statement, semantic_context);
			assert(flow == Control_Flow::SEQUENTIAL, "");
		}

		// Analyse block
		semantic_analyser_analyse_block(for_loop.body_block, semantic_context);
		EXIT(Control_Flow::SEQUENTIAL); // Loops are always sequential, since the condition may not be met before the first iteration
	}
	case AST::Statement_Type::FOREACH_LOOP:
	{
		auto& for_loop = statement->options.foreach_loop;
		auto& loop_info = info->specifics.foreach_loop;
		loop_info.index_variable_symbol = nullptr;
		loop_info.loop_variable_symbol = nullptr;
		loop_info.is_custom_op = false;

		// Create new table for loop variable
		auto symbol_table = symbol_table_create_with_parent(semantic_context->current_symbol_table, Symbol_Access_Level::INTERNAL, compilation_data);
		loop_info.symbol_table = symbol_table;

		// Create loop variable symbol
		Symbol* symbol = symbol_table_define_symbol(
			symbol_table, for_loop.loop_variable_definition->name, Symbol_Type::VARIABLE,
			upcast(for_loop.loop_variable_definition), Symbol_Access_Level::INTERNAL,
			compilation_data
		);
		get_info(for_loop.loop_variable_definition, semantic_context, true)->symbol = symbol;
		loop_info.loop_variable_symbol = symbol;
		symbol->options.variable_type = types.unknown_type; // Should be updated by further code

		// Analyse expression
		Datatype* expr_type = semantic_analyser_analyse_expression_value(for_loop.expression, expression_context_make_dereference(), semantic_context);

		if (datatype_is_unknown(expr_type)) {
			semantic_analyser_set_error_flag(true, semantic_context);
		}

		if (expr_type->type == Datatype_Type::ARRAY || expr_type->type == Datatype_Type::SLICE)
		{
			Datatype* array_type = expr_type;
			Datatype* element_type = nullptr;
			if (array_type->type == Datatype_Type::ARRAY) {
				element_type = downcast<Datatype_Array>(array_type)->element_type;
			}
			else if (array_type->type == Datatype_Type::SLICE) {
				element_type = downcast<Datatype_Slice>(array_type)->element_type;
			}
			else {
				log_semantic_error(semantic_context, "Currently only arrays and slices are supported for foreach loop", for_loop.expression);
				EXIT(Control_Flow::SEQUENTIAL);
			}

			symbol->options.variable_type = upcast(type_system_make_pointer(type_system, element_type));
		}

		// Check for custom iterators
		// if (!already_handled)
		// {
		// 	// Check for custom iterator
		// 	auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;

		// 	Custom_Operator_Key key;
		// 	key.type = Context_Change_Type::ITERATOR;
		// 	key.options.iterator.datatype = iterable_type;
		// 	Custom_Operator* op = symbol_table_query_custom_operator(operator_context, key);

		// 	// Check for polymorphic overload
		// 	if (op == nullptr && expr_type->type == Datatype_Type::STRUCT) {
		// 		auto struct_type = downcast<Datatype_Struct>(expr_type);
		// 		if (struct_type->workload != nullptr && struct_type->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
		// 			key.options.iterator.datatype = upcast(struct_type->workload->polymorphic.instance.parent->body_workload->struct_type);
		// 			op = symbol_table_query_custom_operator(operator_context, key);
		// 		}
		// 	}

		// 	if (op != nullptr)
		// 	{
		// 		expected_mods = op->iterator.iterable_mods;
		// 		loop_info.is_custom_op = true;

		// 		loop_info.custom_op.fn_create = nullptr;
		// 		loop_info.custom_op.fn_get_value = nullptr;
		// 		loop_info.custom_op.fn_has_next = nullptr;
		// 		loop_info.custom_op.fn_next = nullptr;

		// 		bool success = true;
		// 		if (op->iterator.is_polymorphic)
		// 		{
		// 			// Create call
		// 			Arena* scratch_arena = &semantic_analyser.current_workload->scratch_arena;
		// 			auto checkpoint = scratch_arena->make_checkpoint();
		// 			SCOPE_EXIT(scratch_arena->rewind_to_checkpoint(checkpoint));

		// 			auto helper_make_call = [&](Poly_Function& poly_function, int argument_info_count) -> Call_Info {
		// 				Poly_Header* poly_header = poly_function.poly_header;
		// 				Call_Info call_info = call_info_make_with_empty_arguments(call_origin_make(poly_function.poly_header), scratch_arena);
		// 				call_info.argument_infos = scratch_arena->allocate_array<Argument_Info>(argument_info_count);
		// 				return call_info;
		// 				};
		// 			auto helper_instanciate_call = [&](Call_Info& call_info) -> ModTree_Function* {
		// 				Poly_Instance* instance = poly_header_instanciate(
		// 					&call_info, upcast(for_loop.expression), Node_Section::FIRST_TOKEN
		// 				);
		// 				if (instance != nullptr) {
		// 					assert(instance->type == Poly_Instance_Type::FUNCTION, "");
		// 					return instance->options.function_instance->function;
		// 				}
		// 				return nullptr;
		// 				};

		// 			// Instanciate create function
		// 			Call_Info call_info = helper_make_call(op->iterator.options.polymorphic.fn_create, 1);
		// 			call_info_set_parameter_to_expression(&call_info, 0, 0, for_loop.expression);
		// 			loop_info.custom_op.fn_create = helper_instanciate_call(call_info);

		// 			// Find iterator type
		// 			Datatype* iterator_type = nullptr;
		// 			if (loop_info.custom_op.fn_create != nullptr)
		// 			{
		// 				auto return_type_opt = loop_info.custom_op.fn_create->signature->return_type();
		// 				if (return_type_opt.available) {
		// 					iterator_type = return_type_opt.value;
		// 				}
		// 			}

		// 			// Instanciate other functions
		// 			if (iterator_type != nullptr)
		// 			{
		// 				scratch_arena->rewind_to_checkpoint(checkpoint);
		// 				call_info = helper_make_call(op->iterator.options.polymorphic.has_next, 0);
		// 				call_info.parameter_values[0] = parameter_value_make_datatype_known(iterator_type, false);
		// 				loop_info.custom_op.fn_has_next = helper_instanciate_call(call_info);

		// 				scratch_arena->rewind_to_checkpoint(checkpoint);
		// 				call_info = helper_make_call(op->iterator.options.polymorphic.next, 0);
		// 				call_info.parameter_values[0] = parameter_value_make_datatype_known(iterator_type, false);
		// 				loop_info.custom_op.fn_next = helper_instanciate_call(call_info);

		// 				scratch_arena->rewind_to_checkpoint(checkpoint);
		// 				call_info = helper_make_call(op->iterator.options.polymorphic.get_value, 0);
		// 				call_info.parameter_values[0] = parameter_value_make_datatype_known(iterator_type, false);
		// 				loop_info.custom_op.fn_get_value = helper_instanciate_call(call_info);
		// 			}

		// 			success = success &&
		// 				iterator_type != nullptr &&
		// 				loop_info.custom_op.fn_create != nullptr &&
		// 				loop_info.custom_op.fn_get_value != nullptr &&
		// 				loop_info.custom_op.fn_has_next != nullptr &&
		// 				loop_info.custom_op.fn_next != nullptr;

		// 			// Note: Not quite sure if we need to re-check parameters mods and return types, but I guess it cannot hurt...
		// 			if (success)
		// 			{
		// 				// Check create function
		// 				if (datatype_is_unknown(iterator_type)) {
		// 					success = false;
		// 				}

		// 				ModTree_Function* functions[3] = { loop_info.custom_op.fn_get_value, loop_info.custom_op.fn_has_next, loop_info.custom_op.fn_next };
		// 				for (int i = 0; i < 3; i++)
		// 				{
		// 					ModTree_Function* function = functions[i];
		// 					if (function->signature->parameters.size != 1) {
		// 						log_semantic_error(semantic_context, "Instanciated function did not have exactly 1 parameter", upcast(for_loop.expression));
		// 						success = false;
		// 						continue;
		// 					}

		// 					Datatype* param_type = function->signature->parameters[0].datatype;
		// 					if (!types_are_equal(param_type->base_type, iterator_type->base_type)) {
		// 						log_semantic_error(semantic_context, "Instanciated function parameter type did not match iterator type", upcast(for_loop.expression));
		// 						success = false;
		// 						continue;
		// 					}

		// 					if (!datatype_check_if_auto_casts_to_other_mods(iterator_type, param_type->mods, false)) {
		// 						log_semantic_error(semantic_context, "Instanciated function parameter mods were not compatible with iterator_type mods", upcast(for_loop.expression));
		// 						success = false;
		// 						continue;
		// 					}
		// 				}

		// 				if (!loop_info.custom_op.fn_get_value->signature->return_type().available) {
		// 					log_semantic_error(semantic_context, "Get value function instanciation did not return a value", upcast(for_loop.expression));
		// 					success = false;
		// 				}
		// 			}
		// 		}
		// 		else
		// 		{
		// 			loop_info.custom_op.fn_create = op->iterator.options.normal.create;
		// 			loop_info.custom_op.fn_has_next = op->iterator.options.normal.has_next;
		// 			loop_info.custom_op.fn_next = op->iterator.options.normal.next;
		// 			loop_info.custom_op.fn_get_value = op->iterator.options.normal.get_value;
		// 		}

		// 		if (success)
		// 		{
		// 			auto& op = loop_info.custom_op;
		// 			semantic_analyser_register_function_call(op.fn_create);
		// 			semantic_analyser_register_function_call(op.fn_get_value);
		// 			semantic_analyser_register_function_call(op.fn_next);
		// 			semantic_analyser_register_function_call(op.fn_has_next);
		// 			symbol->options.variable_type = op.fn_get_value->signature->return_type().value;

		// 			int it_ptr_lvl = op.fn_create->signature->return_type().value->mods.pointer_level;
		// 			loop_info.custom_op.has_next_pointer_diff = it_ptr_lvl - op.fn_has_next->signature->parameters[0].datatype->mods.pointer_level;
		// 			loop_info.custom_op.next_pointer_diff = it_ptr_lvl - op.fn_next->signature->parameters[0].datatype->mods.pointer_level;
		// 			loop_info.custom_op.get_value_pointer_diff = it_ptr_lvl - op.fn_get_value->signature->parameters[0].datatype->mods.pointer_level;
		// 		}
		// 		else {
		// 			loop_info.is_custom_op = false;
		// 		}
		// 	}
		// 	else
		// 	{
		// 		log_semantic_error(semantic_context, "Cannot loop over given datatype", for_loop.expression);
		// 		log_error_info_given_type(semantic_context, expr_type);
		// 	}
		// }

		// Create index variable if available
		if (for_loop.index_variable_definition.available)
		{
			// Use current symbol table so collisions are handled
			RESTORE_ON_SCOPE_EXIT(semantic_context->current_symbol_table, symbol_table);
			Symbol* index_symbol = symbol_table_define_symbol(
				symbol_table, for_loop.index_variable_definition.value->name, Symbol_Type::VARIABLE,
				upcast(for_loop.index_variable_definition.value), Symbol_Access_Level::INTERNAL,
				compilation_data
			);
			get_info(for_loop.index_variable_definition.value, semantic_context, true)->symbol = index_symbol;
			loop_info.index_variable_symbol = index_symbol;
			index_symbol->options.variable_type = upcast(types.usize);
		}
		RESTORE_ON_SCOPE_EXIT(semantic_context->current_symbol_table, symbol_table);

		// Analyse body
		semantic_analyser_analyse_block(for_loop.body_block, semantic_context);
		EXIT(Control_Flow::SEQUENTIAL);
	}
	case AST::Statement_Type::DELETE_STATEMENT:
	{
		auto delete_type = semantic_analyser_analyse_expression_value(
			statement->options.delete_expr, expression_context_make_unspecified(), semantic_context
		);
		if (datatype_is_unknown(delete_type)) {
			semantic_analyser_set_error_flag(true, semantic_context);
			EXIT(Control_Flow::SEQUENTIAL);
		}
		if (delete_type->type != Datatype_Type::POINTER && delete_type->type != Datatype_Type::SLICE) {
			log_semantic_error(semantic_context, "Delete is only valid on pointer or slice types", statement->options.delete_expr);
			log_error_info_given_type(semantic_context, delete_type);
		}
		EXIT(Control_Flow::SEQUENTIAL);
	}
	case AST::Statement_Type::BINOP_ASSIGNMENT:
	{
		auto& assignment = statement->options.binop_assignment;

		// Initialize info as non-overloaded (primitive binop)
		info->specifics.overload.function = 0;
		info->specifics.overload.switch_arguments = false;

		// Analyse expr side
		Datatype* left_type = semantic_analyser_analyse_expression_value(assignment.left_side, expression_context_make_dereference(), semantic_context);
		if (!expression_has_memory_address(assignment.left_side, semantic_context)) {
			log_semantic_error(
				semantic_context, "Left side must have a memory address/cannot be a temporary value for assignment", 
				upcast(statement), Node_Section::WHOLE_NO_CHILDREN
			);
		}

		Expression_Context right_context = expression_context_make_dereference();
		if (left_type->type == Datatype_Type::PRIMITIVE && downcast<Datatype_Primitive>(left_type)->primitive_class == Primitive_Class::INTEGER) {
			right_context = expression_context_make_specific_type(left_type);
		}
		// Analyse right side
		Datatype* right_type = semantic_analyser_analyse_expression_value(assignment.right_side, right_context, semantic_context);

		// Check for unknowns
		if (datatype_is_unknown(left_type) || datatype_is_unknown(right_type)) {
			EXIT(Control_Flow::SEQUENTIAL);
		}

		// Check if binop is a primitive operation (ints, floats, bools, pointers)
		bool types_are_valid = false;
		if (types_are_equal(left_type, right_type))
		{
			bool int_valid = false;
			bool float_valid = false;
			switch (assignment.binop)
			{
			case AST::Binop::ADDITION:
			case AST::Binop::SUBTRACTION:
			case AST::Binop::MULTIPLICATION:
			case AST::Binop::DIVISION:
				float_valid = true;
				int_valid = true;
				break;
			case AST::Binop::MODULO:
				int_valid = true;
				break;
			default:
				panic("Shouldn't happen in Binop-Assignment");
				break;
			}

			Datatype* operand_type = left_type;
			if (operand_type->type == Datatype_Type::PRIMITIVE)
			{
				auto primitive = downcast<Datatype_Primitive>(operand_type);
				if (primitive->primitive_class == Primitive_Class::INTEGER && int_valid) {
					EXIT(Control_Flow::SEQUENTIAL);
				}
				else if (primitive->primitive_class == Primitive_Class::FLOAT && float_valid) {
					EXIT(Control_Flow::SEQUENTIAL);
				}
			}
		}

		// Check for pointer-arithmetic
		if (types_are_equal(left_type, upcast(types.address)))
		{
			bool right_is_integer = types_are_equal(right_type, upcast(types.usize)) || types_are_equal(right_type, upcast(types.isize));
			if (right_is_integer && (assignment.binop == AST::Binop::ADDITION || assignment.binop == AST::Binop::SUBTRACTION)) {
				EXIT(Control_Flow::SEQUENTIAL);
			}
		}

		// Check for operator overloads if it isn't a primitive operation
		// auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;
		// if (!types_are_valid)
		// {
		// 	Custom_Operator_Key key;
		// 	key.type = Context_Change_Type::BINARY_OPERATOR;
		// 	key.options.binop.binop = assignment.binop;
		// 	key.options.binop.left_type = left_type;
		// 	key.options.binop.right_type = right_type;
		// 	auto overload = symbol_table_query_custom_operator(operator_context, key);
		// 	if (overload != nullptr)
		// 	{
		// 		auto function = overload->binop.function;
		// 		assert(function->signature->return_type().available, "");
		// 		if (!types_are_equal(left_type, function->signature->return_type().value)) {
		// 			log_semantic_error(semantic_context, 
		// 				"Overload for this binop not valid, as assignment requires return type to be the same",
		// 				upcast(statement), Node_Section::WHOLE_NO_CHILDREN
		// 			);
		// 		}
		// 		info->specifics.overload.function = function;
		// 		info->specifics.overload.switch_arguments = overload->binop.switch_left_and_right;
		// 		semantic_analyser_register_function_call(function);
		// 		types_are_valid = true;
		// 	}
		// }

		log_semantic_error(semantic_context, "Types aren't valid for binary operation", upcast(statement), Node_Section::WHOLE_NO_CHILDREN);
		log_error_info_binary_op_type(semantic_context, left_type, right_type);
		EXIT(Control_Flow::SEQUENTIAL);
		break;
	}
	case AST::Statement_Type::ASSIGNMENT:
	{
		auto& assignment_node = statement->options.assignment;

		// Analyse expr side
		Datatype* all_left_types = 0;
		bool left_side_all_same_type = true;
		for (int i = 0; i < assignment_node.left_side.size; i++)
		{
			auto expr = assignment_node.left_side[i];
			auto left_type = semantic_analyser_analyse_expression_value(expr, expression_context_make_unspecified(), semantic_context);

			// Check for errors
			if (!expression_has_memory_address(expr, semantic_context)) {
				log_semantic_error(semantic_context, "Cannot assign to a temporary value", upcast(expr));
			}

			if (all_left_types == 0) {
				all_left_types = left_type;
			}
			else if (!types_are_equal(all_left_types, left_type)) {
				left_side_all_same_type = false;
			}
		}

		if (assignment_node.right_side.size == 1 && left_side_all_same_type) {
			// Broadcast
			semantic_analyser_analyse_expression_value(
				assignment_node.right_side[0], expression_context_make_specific_type(all_left_types), semantic_context
			);
		}
		else
		{
			if (assignment_node.left_side.size != assignment_node.right_side.size) {
				log_semantic_error(
					semantic_context, "Left side and right side of assignment have different count", 
					upcast(statement), Node_Section::WHOLE_NO_CHILDREN
				);
			}

			// Analyse right side
			for (int i = 0; i < assignment_node.right_side.size; i++) {
				auto right_node = assignment_node.right_side[i];
				Expression_Context context;
				if (i < assignment_node.left_side.size) {
					auto left_node = assignment_node.left_side[i];
					context = expression_context_make_specific_type(expression_info_get_type(get_info(left_node, semantic_context), false, type_system));
				}
				else {
					context = expression_context_make_error();
				}
				semantic_analyser_analyse_expression_value(right_node, context, semantic_context);
			}
		}

		EXIT(Control_Flow::SEQUENTIAL);
	}
	case AST::Statement_Type::IMPORT: {
		// Already handled on block start...
		EXIT(Control_Flow::SEQUENTIAL);
	}
	case AST::Statement_Type::DEFINITION:
	{
		auto definition = statement->options.definition;
		auto& types = definition->types;
		auto& symbol_nodes = definition->symbols;
		auto& values = definition->values;
		auto& predefined_types = type_system->predefined_types;

		// Check if this was already handled at block start
		if (definition->is_comptime) {
			EXIT(Control_Flow::SEQUENTIAL);
		}

		// Initialize symbols
		for (int i = 0; i < symbol_nodes.size; i++) {
			auto symbol = get_info(symbol_nodes[i], semantic_context)->symbol;
			symbol->type = Symbol_Type::VARIABLE_UNDEFINED;
			symbol->options.variable_type = predefined_types.unknown_type;

			if (i >= types.size && types.size > 1) {
				log_semantic_error(semantic_context, "No type was specified for this symbol", upcast(symbol_nodes[i]), Node_Section::IDENTIFIER);
				symbol->type = Symbol_Type::ERROR_SYMBOL;
			}
			if (i >= values.size && values.size > 1) {
				log_semantic_error(semantic_context, "No value was specified for this symbol", upcast(symbol_nodes[i]), Node_Section::IDENTIFIER);
				symbol->type = Symbol_Type::ERROR_SYMBOL;
			}
		}

		// Analyse types
		bool all_types_are_equal = true;
		Datatype* equal_type = 0;
		for (int i = 0; i < types.size; i++)
		{
			// Analyse type
			auto type_node = types[i];
			Datatype* type = semantic_analyser_analyse_expression_type(type_node, semantic_context);

			// Check for errors
			bool is_pointer = datatype_is_pointer(type);

			// Check if all types are the same
			if (equal_type == 0) {
				equal_type = type;
			}
			else if (!types_are_equal(equal_type, type)) {
				all_types_are_equal = false;
			}

			// Set types of variables
			if (types.size == 1) // Type broadcast
			{
				for (int j = 0; j < symbol_nodes.size; j++) {
					auto symbol = get_info(symbol_nodes[j], semantic_context)->symbol;
					symbol->options.variable_type = type;
				}
			}
			else
			{
				if (i < symbol_nodes.size) {
					auto symbol = get_info(symbol_nodes[i], semantic_context)->symbol;
					symbol->options.variable_type = type;
				}
				else {
					log_semantic_error(semantic_context, "No symbol exists on the left side of definition for this type", type_node);
				}
			}
		}

		// Analyse values
		for (int i = 0; i < values.size; i++)
		{
			// Figure out context
			Expression_Context context = expression_context_make_unspecified();
			if (types.size == 0)
			{
				context = expression_context_make_unspecified();
			}
			else if (types.size == 1) 
			{
				auto type_info = get_info(types[0], semantic_context);
				if (type_info->result_type == Expression_Result_Type::DATATYPE) {
					context = expression_context_make_specific_type(type_info->options.datatype);
				}
				else {
					context = expression_context_make_error();
				}
			}
			else  // types.size > 1
			{
				if (values.size == 1) { // Value broadcast
					if (all_types_are_equal) {
						context = expression_context_make_specific_type(equal_type);
					}
					else {
						log_semantic_error(semantic_context, "For value broadcast all specified types must be equal", values[i]);
						context = expression_context_make_error();
					}
				}
				else
				{
					if (i < types.size) {
						auto type_info = get_info(types[i], semantic_context);
						if (type_info->result_type == Expression_Result_Type::DATATYPE) {
							context = expression_context_make_specific_type(type_info->options.datatype);
						}
						else {
							context = expression_context_make_error();
						}
					}
					else {
						log_semantic_error(semantic_context, "No type is specified in the definition for this value", values[i]);
						context = expression_context_make_error();
					}
				}
			}

			// Analyse value
			Datatype* value_type = semantic_analyser_analyse_expression_value(values[i], context, semantic_context);

			// Update symbol type
			if (values.size == 1) // Value broadcast
			{
				if (types.size == 0) {
					for (int j = 0; j < symbol_nodes.size; j++) {
						auto symbol = get_info(symbol_nodes[j], semantic_context)->symbol;
						symbol->options.variable_type = value_type;
					}
				}
			}
			else
			{
				if (i < symbol_nodes.size) {
					if (types.size == 0) {
						auto symbol = get_info(symbol_nodes[i], semantic_context)->symbol;
						symbol->options.variable_type = value_type;
					}
				}
				else {
					log_semantic_error(semantic_context, "No symbol/variable exists for this value", values[i]);
				}
			}
		}

		// Set symbols to defined
		for (int i = 0; i < symbol_nodes.size; i++) {
			get_info(symbol_nodes[i], semantic_context)->symbol->type = Symbol_Type::VARIABLE;
		}

		EXIT(Control_Flow::SEQUENTIAL);
	}
	default: {
		panic("Should be covered!\n");
		break;
	}
	}
	panic("HEY");
	EXIT(Control_Flow::SEQUENTIAL);
#undef EXIT
}

Control_Flow semantic_analyser_analyse_block(AST::Code_Block* block, Semantic_Context* semantic_context, bool polymorphic_symbol_access)
{
	auto compilation_data = semantic_context->compilation_data;

	auto block_info = get_info(block, semantic_context, true);
	block_info->control_flow_locked = false;
	block_info->flow = Control_Flow::SEQUENTIAL;

	// Create symbol table for block
	block_info->symbol_table = symbol_table_create_with_parent(
		semantic_context->current_symbol_table,
		polymorphic_symbol_access ? Symbol_Access_Level::POLYMORPHIC : Symbol_Access_Level::INTERNAL,
		compilation_data
	);
	RESTORE_ON_SCOPE_EXIT(semantic_context->current_symbol_table, block_info->symbol_table);

	// Check if this code block was already analysed by another instance (FUTURE: Can cause problems if multithreaded)
	Symbol_Table** analysed_opt = hashtable_find_element(&compilation_data->code_block_comptimes, block);
	if (analysed_opt != nullptr)
	{
		// Add include table to already analysed comptime definitions
		symbol_table_add_import(
			block_info->symbol_table, *analysed_opt, Import_Type::SYMBOLS, true, Symbol_Access_Level::GLOBAL, semantic_context,
			upcast(block), Node_Section::FIRST_TOKEN
		);
		symbol_table_add_import(
			block_info->symbol_table, *analysed_opt, Import_Type::DOT_CALLS, true, Symbol_Access_Level::GLOBAL, semantic_context,
			upcast(block), Node_Section::FIRST_TOKEN
		);
		// We don't add operator import because operators are analysed for each instance seperately

		// Add local variable symbols
		for (int i = 0; i < block->statements.size; i++)
		{
			auto statement = block->statements[i];
			if (statement->type != AST::Statement_Type::DEFINITION) continue;
			auto definition = statement->options.definition;
			if (definition->is_comptime) continue;
			analyser_create_symbol_and_workload_for_definition(definition, nullptr, semantic_context);
		}
	}
	else
	{
		bool inserted = hashtable_insert_element(&compilation_data->code_block_comptimes, block, block_info->symbol_table);
		assert(inserted, "");

		// Create block content
		Arena* scratch_arena = semantic_context->scratch_arena;
		auto checkpoint = scratch_arena->make_checkpoint();
		SCOPE_EXIT(scratch_arena->rewind_to_checkpoint(checkpoint));
		Module_Content block_content = module_content_create(scratch_arena);

		// Add workloads for all Definitions and Imports
		for (int i = 0; i < block->statements.size; i++)
		{
			auto statement = block->statements[i];
			if (statement->type == AST::Statement_Type::IMPORT) {
				module_content_add_import_node(block_content, statement->options.import_node, semantic_context->current_symbol_table);
			}
			if (statement->type != AST::Statement_Type::DEFINITION) continue;
			auto definition = statement->options.definition;
			analyser_create_symbol_and_workload_for_definition(definition, &block_content, semantic_context);
		}
		module_content_analyse_items(block_content, semantic_context);
	}

	// Install custom operators and analyse them
	auto custom_operator_table = symbol_table_install_custom_operator_table(block_info->symbol_table, block->custom_operators, false, semantic_context);
	for (int i = 0; i < block->custom_operators.size; i++) {
		analyse_custom_operator_node(block->custom_operators[i], custom_operator_table, semantic_context);
	}

	// Check if block id is unique
	auto& block_stack = semantic_context->block_stack;
	if (block->block_id.available)
	{
		for (int i = 0; i < block_stack.size; i++) {
			auto prev = block_stack[i];
			if (prev != 0 && prev->block_id.available && prev->block_id.value == block->block_id.value) {
				log_semantic_error(semantic_context, "Block label already in use", &block->base);
			}
		}
	}

	// Analyse statements
	int rewind_block_count = block_stack.size;
	bool rewind_reachable = semantic_context->statement_reachable;
	SCOPE_EXIT(block_stack.rollback_to_size(rewind_block_count));
	SCOPE_EXIT(semantic_context->statement_reachable = rewind_reachable);
	block_stack.push_back(block);
	for (int i = 0; i < block->statements.size; i++)
	{
		Control_Flow flow = semantic_analyser_analyse_statement(block->statements[i], semantic_context);
		if (flow != Control_Flow::SEQUENTIAL) {
			semantic_context->statement_reachable = false;
			if (!block_info->control_flow_locked) {
				block_info->flow = flow;
				block_info->control_flow_locked = true;
			}
		}
	}
	return block_info->flow;
}



// ERRORS
void error_information_append_to_rich_text(
	const Error_Information& info, Compilation_Data* compilation_data, Rich_Text::Rich_Text * text, Datatype_Format format
)
{
	auto type_system = compilation_data->type_system;
	Rich_Text::set_text_color(text, Syntax_Color::TEXT);
	switch (info.type)
	{
	case Error_Information_Type::CYCLE_WORKLOAD: {
		auto string = Rich_Text::start_line_manipulation(text);
		analysis_workload_append_to_string(info.options.cycle_workload, string);
		Rich_Text::stop_line_manipulation(text);
		break;
	}
	case Error_Information_Type::COMPTIME_MESSAGE:
		Rich_Text::append_formated(text, "Comptime msg: %s", info.options.comptime_message);
		break;
	case Error_Information_Type::ARGUMENT_COUNT:
		Rich_Text::append_formated(text, "Given argument count: %d, required: %d",
			info.options.invalid_argument_count.given, info.options.invalid_argument_count.expected);
		break;
	case Error_Information_Type::ID:
		Rich_Text::append_formated(text, "ID: %s", info.options.id->characters);
		break;
	case Error_Information_Type::SYMBOL: {
		Rich_Text::append_formated(text, "Symbol: ");
		Rich_Text::set_text_color(text, symbol_to_color(info.options.symbol, true));
		auto string = Rich_Text::start_line_manipulation(text);
		symbol_append_to_string(info.options.symbol, string);
		Rich_Text::stop_line_manipulation(text);
		break;
	}
	case Error_Information_Type::EXIT_CODE: {
		Rich_Text::append_formated(text, "Exit_Code: ");
		auto string = Rich_Text::start_line_manipulation(text);
		exit_code_append_to_string(string, info.options.exit_code);
		Rich_Text::stop_line_manipulation(text);
		break;
	}
	case Error_Information_Type::GIVEN_TYPE:
		Rich_Text::append_formated(text, "Given Type:    ");
		datatype_append_to_rich_text(info.options.type, type_system, text, format);
		break;
	case Error_Information_Type::EXPECTED_TYPE:
		Rich_Text::append_formated(text, "Expected Type: ");
		datatype_append_to_rich_text(info.options.type, type_system, text, format);
		break;
	case Error_Information_Type::FUNCTION_TYPE:
		Rich_Text::append_formated(text, "Function Type: ");
		datatype_append_to_rich_text(info.options.type, type_system, text, format);
		break;
	case Error_Information_Type::BINARY_OP_TYPES:
		Rich_Text::append_formated(text, "Left: ");
		datatype_append_to_rich_text(info.options.binary_op_types.left_type, type_system, text, format);
		Rich_Text::set_text_color(text);
		Rich_Text::append_formated(text, ", Right: ");
		datatype_append_to_rich_text(info.options.binary_op_types.right_type, type_system, text, format);
		break;
	case Error_Information_Type::EXPRESSION_RESULT_TYPE:
	{
		Rich_Text::append(text, "Given: ");
		switch (info.options.expression_type)
		{
		case Expression_Result_Type::NOTHING:
			Rich_Text::append(text, "Nothing/void");
			break;
		case Expression_Result_Type::POLYMORPHIC_PATTERN:
			Rich_Text::append(text, "Polymorphic Pattern");
			break;
		case Expression_Result_Type::HARDCODED_FUNCTION:
			Rich_Text::append(text, "Hardcoded function");
			break;
		case Expression_Result_Type::POLYMORPHIC_FUNCTION:
			Rich_Text::append(text, "Polymorphic function");
			break;
		case Expression_Result_Type::POLYMORPHIC_STRUCT:
			Rich_Text::append(text, "Polymorphic struct");
			break;
		case Expression_Result_Type::CONSTANT:
			Rich_Text::append(text, "Constant");
			break;
		case Expression_Result_Type::VALUE:
			Rich_Text::append(text, "Value");
			break;
		case Expression_Result_Type::FUNCTION:
			Rich_Text::append(text, "Function");
			break;
		case Expression_Result_Type::DATATYPE:
			Rich_Text::append(text, "Type");
			break;
		default: panic("");
		}
		break;
	}
	case Error_Information_Type::CONSTANT_STATUS:
		Rich_Text::append_formated(text, "Couldn't serialize constant: %s", info.options.constant_message);
		break;
	default: panic("");
	}
}

void error_information_append_to_string(
	const Error_Information& info, Compilation_Data* compilation_data,
	String* string, Datatype_Format format
)
{
	Rich_Text::Rich_Text text = Rich_Text::create(vec3(1.0f));
	SCOPE_EXIT(Rich_Text::destroy(&text));
	Rich_Text::add_line(&text);
	error_information_append_to_rich_text(info, compilation_data, &text, format);
	Rich_Text::append_to_string(&text, string, 2);
}

void semantic_analyser_append_semantic_errors_to_string(Compilation_Data * compilation_data, String * string, int indentation)
{
	auto& errors = compilation_data->semantic_errors;
	for (int i = 0; i < errors.size; i++)
	{
		auto& e = errors[i];
		for (int k = 0; k < indentation; k++) {
			string_append(string, "    ");
		}

		string_append(string, e.msg);
		string_append(string, "\n");
		for (int j = 0; j < e.information.size; j++) {
			auto& info = e.information[j];
			string_append(string, "\n");
			for (int k = 0; k < indentation + 1; k++) {
				string_append(string, "    ");
			}
			error_information_append_to_string(info, compilation_data, string);
			string_append(string, "\t");
		}
	}
}


