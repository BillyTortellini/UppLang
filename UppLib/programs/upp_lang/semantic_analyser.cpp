#include "semantic_analyser.hpp"

#include "../../datastructures/string.hpp"
#include "../../utility/hash_functions.hpp"
#include "../../datastructures/hashset.hpp"
#include "../../datastructures/string.hpp"
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

// GLOBALS
bool PRINT_DEPENDENCIES = false;
bool PRINT_TIMING = false;
static Semantic_Analyser semantic_analyser;
static Workload_Executer workload_executer;

// PROTOTYPES
ModTree_Function* modtree_function_create_empty(Datatype_Function* signature, Symbol* symbol, Function_Progress* progress, Symbol_Table* param_table);
void analysis_workload_destroy(Workload_Base* workload);
Expression_Info* semantic_analyser_analyse_expression_any(AST::Expression* expression, Expression_Context context);
Datatype* semantic_analyser_analyse_expression_value(AST::Expression* rc_expression, Expression_Context context, bool no_value_expected = false);
Datatype* semantic_analyser_analyse_expression_type(AST::Expression* rc_expression);
Control_Flow semantic_analyser_analyse_block(AST::Code_Block* code_block, bool polymorphic_symbol_access = false);

Expression_Cast_Info expression_context_apply(
    Datatype* initial_type, Expression_Context context,
    bool log_errors, AST::Expression* error_report_node = 0, Parser::Section error_section = Parser::Section::WHOLE);

bool workload_executer_switch_to_workload(Workload_Base* workload);
void analysis_workload_entry(void* userdata);
void analysis_workload_append_to_string(Workload_Base* workload, String* string);
void workload_executer_wait_for_dependency_resolution();
Dependency_Failure_Info dependency_failure_info_make_none();
void analysis_workload_add_dependency_internal(
    Workload_Base* workload,
    Workload_Base* dependency,
    Dependency_Failure_Info failure_info = dependency_failure_info_make_none());
Datatype_Memory_Info* type_wait_for_size_info_to_finish(Datatype* type, Dependency_Failure_Info failure_info = dependency_failure_info_make_none());
void polymorphic_base_info_destroy(Polymorphic_Base_Info* info);
bool arguments_check_for_naming_errors(Dynamic_Array<AST::Argument*>& arguments, int* out_unnamed_argument_count = 0);
Operator_Context* symbol_table_install_new_operator_context(Symbol_Table* symbol_table);
void analyse_operator_context_changes(Dynamic_Array<AST::Context_Change*> context_changes, Operator_Context* context);



// HELPERS
namespace Helpers
{
    Analysis_Workload_Type get_workload_type(Workload_Import_Resolve* workload) { return Analysis_Workload_Type::IMPORT_RESOLVE; };
    Analysis_Workload_Type get_workload_type(Workload_Module_Analysis* workload) { return Analysis_Workload_Type::MODULE_ANALYSIS; };
    Analysis_Workload_Type get_workload_type(Workload_Definition* workload) { return Analysis_Workload_Type::DEFINITION; };
    Analysis_Workload_Type get_workload_type(Workload_Structure_Body* workload) { return Analysis_Workload_Type::STRUCT_BODY; };
    Analysis_Workload_Type get_workload_type(Workload_Structure_Polymorphic* workload) { return Analysis_Workload_Type::STRUCT_POLYMORPHIC; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Header* workload) { return Analysis_Workload_Type::FUNCTION_HEADER; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Body* workload) { return Analysis_Workload_Type::FUNCTION_BODY; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Cluster_Compile* workload) { return Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE; };
    Analysis_Workload_Type get_workload_type(Workload_Operator_Context_Change* workload) { return Analysis_Workload_Type::OPERATOR_CONTEXT_CHANGE; };
    Analysis_Workload_Type get_workload_type(Workload_Bake_Analysis* workload) { return Analysis_Workload_Type::BAKE_ANALYSIS; };
    Analysis_Workload_Type get_workload_type(Workload_Bake_Execution* workload) { return Analysis_Workload_Type::BAKE_EXECUTION; };
    Analysis_Workload_Type get_workload_type(Workload_Event* workload) { return Analysis_Workload_Type::EVENT; };
};

Workload_Base* upcast(Workload_Base* workload) { return workload; }
Workload_Base* upcast(Workload_Import_Resolve* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Module_Analysis* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Function_Header* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Function_Body* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Function_Cluster_Compile* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Structure_Body* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Structure_Polymorphic* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Definition* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Bake_Analysis* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Bake_Execution* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Event* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Operator_Context_Change* workload) { return &workload->base; }

template <typename T>
T* downcast(Workload_Base* workload) {
    T* result = (T*)workload;
    assert(workload->type == Helpers::get_workload_type(result), "Invalid cast");
    return result;
}



Datatype_Function* hardcoded_type_to_signature(Hardcoded_Type type)
{
    auto& an = compiler.type_system.predefined_types;
    switch (type)
    {
    case Hardcoded_Type::ASSERT_FN: return an.type_assert;
    case Hardcoded_Type::FREE_POINTER: return an.type_free;
    case Hardcoded_Type::MALLOC_SIZE_I32: return an.type_malloc;
    case Hardcoded_Type::TYPE_INFO: return an.type_type_info;
    case Hardcoded_Type::TYPE_OF: return an.type_type_of;

    case Hardcoded_Type::PRINT_BOOL: return an.type_print_bool;
    case Hardcoded_Type::PRINT_I32: return an.type_print_i32;
    case Hardcoded_Type::PRINT_F32: return an.type_print_f32;
    case Hardcoded_Type::PRINT_STRING: return an.type_print_string;
    case Hardcoded_Type::PRINT_LINE: return an.type_print_line;

    case Hardcoded_Type::READ_I32: return an.type_print_i32;
    case Hardcoded_Type::READ_F32: return an.type_print_f32;
    case Hardcoded_Type::READ_BOOL: return an.type_print_bool;
    case Hardcoded_Type::RANDOM_I32: return an.type_random_i32;
    default: panic("HEY");
    }
    return 0;
}

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

Polymorphic_Value polymorphic_value_make_type(Datatype* type) {
    Polymorphic_Value access;
    access.only_datatype_known = true;
    access.options.type = type;
    return access;
}

Polymorphic_Value polymorphic_value_make_constant(Upp_Constant constant) {
    Polymorphic_Value access;
    access.only_datatype_known = false;
    access.options.value = constant;
    return access;
}

// Analysis Info
Analysis_Info* pass_get_base_info(Analysis_Pass* pass, AST::Node* node, Info_Query query) {
    AST_Info_Key key;
    key.pass = pass;
    key.base = node;
    switch (query)
    {
    case Info_Query::CREATE: {
        Analysis_Info* new_info = new Analysis_Info;
        memory_zero(new_info);
        bool inserted = hashtable_insert_element(&semantic_analyser.ast_to_info_mapping, key, new_info);
        assert(inserted, "Must not happen");
        return new_info;
    }
    case Info_Query::CREATE_IF_NULL: {
        // Check if already there
        Analysis_Info** already_there = hashtable_find_element(&semantic_analyser.ast_to_info_mapping, key);
        if (already_there != 0) {
            assert(*already_there != 0, "Somewhere nullptr was inserted into hashmap");
            return *already_there;
        }
        // Otherwise create new
        Analysis_Info* new_info = new Analysis_Info;
        memory_zero(new_info);
        bool inserted = hashtable_insert_element(&semantic_analyser.ast_to_info_mapping, key, new_info);
        assert(inserted, "Must not happen");
        return new_info;
    }
    case Info_Query::READ_NOT_NULL: {
        Analysis_Info** result = hashtable_find_element(&semantic_analyser.ast_to_info_mapping, key);
        assert(result != 0, "Not inserted yet");
        assert(*result != 0, "Somewhere nullptr was inserted into hashmap");
        return *result;
    }
    case Info_Query::TRY_READ: {
        Analysis_Info** result = hashtable_find_element(&semantic_analyser.ast_to_info_mapping, key);
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

Expression_Info* pass_get_node_info(Analysis_Pass* pass, AST::Expression* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->info_expr;
}

Case_Info* pass_get_node_info(Analysis_Pass* pass, AST::Switch_Case* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->info_case;
}

Argument_Info* pass_get_node_info(Analysis_Pass* pass, AST::Argument* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->arg_info;
}

Statement_Info* pass_get_node_info(Analysis_Pass* pass, AST::Statement* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->info_stat;
}

Code_Block_Info* pass_get_node_info(Analysis_Pass* pass, AST::Code_Block* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->info_block;
}

Parameter_Info* pass_get_node_info(Analysis_Pass* pass, AST::Parameter* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->param_info;
}

Path_Lookup_Info* pass_get_node_info(Analysis_Pass* pass, AST::Path_Lookup* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->path_info;
}

Definition_Symbol_Info* pass_get_node_info(Analysis_Pass* pass, AST::Definition_Symbol* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->definition_symbol_info;
}

Symbol_Lookup_Info* pass_get_node_info(Analysis_Pass* pass, AST::Symbol_Lookup* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->symbol_lookup_info;
}

Module_Info* pass_get_node_info(Analysis_Pass* pass, AST::Module* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->module_info;
}

Context_Change_Info* pass_get_node_info(Analysis_Pass* pass, AST::Context_Change* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->context_info;
}



// Helpers
Expression_Info* get_info(AST::Expression* expression, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, expression, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}

Case_Info* get_info(AST::Switch_Case* sw_case, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, sw_case, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}

Argument_Info* get_info(AST::Argument* argument, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, argument, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}

Statement_Info* get_info(AST::Statement* statement, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, statement, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}

Code_Block_Info* get_info(AST::Code_Block* block, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, block, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}

Parameter_Info* get_info(AST::Parameter* param, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, param, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}

Definition_Symbol_Info* get_info(AST::Definition_Symbol* definition, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, definition, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}

Symbol_Lookup_Info* get_info(AST::Symbol_Lookup* lookup, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, lookup, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}

Path_Lookup_Info* get_info(AST::Path_Lookup* node, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, node, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}

Module_Info* get_info(AST::Module* module, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, module, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}

Context_Change_Info* get_info(AST::Context_Change* context_change, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, context_change, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}


// Errors
void semantic_analyser_set_error_flag(bool error_due_to_unknown)
{
    auto& analyser = semantic_analyser;
    if (analyser.current_workload != 0) {
        auto workload = analyser.current_workload;
        if (error_due_to_unknown) {
            if (workload->ignore_unknown_errors) {
                return;
            }
            workload->errors_due_to_unknown_count += 1;
        }
        else {
            workload->real_error_count += 1;
        }

        if (workload->current_expression != 0) {
            workload->current_expression->contains_errors = true;
        }
        if (workload->current_function != 0)
        {
            workload->current_function->is_runnable = false;
            if (!error_due_to_unknown) {
                workload->current_function->contains_errors = true;
            }
        }
    }
}

void log_semantic_error(const char* msg, AST::Node* node, Parser::Section node_section) {
    Semantic_Error error;
    error.msg = msg;
    error.error_node = node;
    error.section = node_section;
    error.information = dynamic_array_create_empty<Error_Information>(2);
    dynamic_array_push_back(&semantic_analyser.errors, error);
    semantic_analyser_set_error_flag(false);
}

void log_semantic_error(const char* msg, AST::Expression* node, Parser::Section node_section = Parser::Section::WHOLE) {
    log_semantic_error(msg, AST::upcast(node), node_section);
}

void log_semantic_error(const char* msg, AST::Statement* node, Parser::Section node_section = Parser::Section::WHOLE) {
    log_semantic_error(msg, AST::upcast(node), node_section);
}

Error_Information error_information_make_empty(Error_Information_Type type) {
    Error_Information info;
    info.type = type;
    return info;
}

void log_error_info_argument_count(int given_argument_count, int expected_argument_count) {
    Error_Information info = error_information_make_empty(Error_Information_Type::ARGUMENT_COUNT);
    info.options.invalid_argument_count.expected = expected_argument_count;
    info.options.invalid_argument_count.given = given_argument_count;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_invalid_member(Datatype_Struct* struct_signature, String* id) {
    Error_Information info = error_information_make_empty(Error_Information_Type::INVALID_MEMBER);
    info.options.invalid_member.member_id = id;
    info.options.invalid_member.struct_signature = struct_signature;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_id(String* id) {
    assert(id != 0, "");
    Error_Information info = error_information_make_empty(Error_Information_Type::ID);
    info.options.id = id;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_symbol(Symbol* symbol) {
    Error_Information info = error_information_make_empty(Error_Information_Type::SYMBOL);
    info.options.symbol = symbol;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_exit_code(Exit_Code code) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXIT_CODE);
    info.options.exit_code = code;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_given_type(Datatype* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::GIVEN_TYPE);
    info.options.type = type;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_expected_type(Datatype* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXPECTED_TYPE);
    info.options.type = type;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_missing_parameter(Function_Parameter parameter) {
    Error_Information info = error_information_make_empty(Error_Information_Type::MISSING_PARAMETER);
    info.options.parameter = parameter;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_function_type(Datatype_Function* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::FUNCTION_TYPE);
    info.options.function = type;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_binary_op_type(Datatype* left_type, Datatype* right_type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::BINARY_OP_TYPES);
    info.options.binary_op_types.left_type = left_type;
    info.options.binary_op_types.right_type = right_type;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_expression_result_type(Expression_Result_Type result_type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXPRESSION_RESULT_TYPE);
    info.options.expression_type = result_type;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_constant_status(const char* msg) {
    Error_Information info = error_information_make_empty(Error_Information_Type::CONSTANT_STATUS);
    info.options.constant_message = msg;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_comptime_msg(const char* comptime_msg) {
    Error_Information info = error_information_make_empty(Error_Information_Type::COMPTIME_MESSAGE);
    info.options.comptime_message = comptime_msg;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_cycle_workload(Workload_Base* workload) {
    Error_Information info = error_information_make_empty(Error_Information_Type::CYCLE_WORKLOAD);
    info.options.cycle_workload = workload;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
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

Function_Progress* polymorphic_function_base_to_function_progress(Polymorphic_Function_Base* base) {
    Function_Progress* base_progress = (Function_Progress*) (((byte*)base) - offsetof(Function_Progress, polymorphic.base));
    assert(base_progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE && base_progress->header_workload->progress == base_progress, "Testing offset magic");
    return base_progress;
}



Analysis_Pass* analysis_pass_allocate(Workload_Base* origin, AST::Node* mapping_node)
{
    Analysis_Pass* result = new Analysis_Pass;
    dynamic_array_push_back(&semantic_analyser.allocated_passes, result);
    result->origin_workload = origin;
    // Add mapping to workload 
    if (mapping_node) {
        Node_Passes* workloads_opt = hashtable_find_element(&semantic_analyser.ast_to_pass_mapping, mapping_node);
        if (workloads_opt == 0) {
            Node_Passes passes;
            passes.base = mapping_node;
            passes.passes = dynamic_array_create_empty<Analysis_Pass*>(1);
            dynamic_array_push_back(&passes.passes, result);
            hashtable_insert_element(&semantic_analyser.ast_to_pass_mapping, mapping_node, passes);
        }
        else {
            dynamic_array_push_back(&workloads_opt->passes, result);
        }
    }
    return result;
}

// Analysis Progress
template<typename T>
T* analysis_progress_allocate_internal()
{
    auto progress = stack_allocator_allocate<T>(&semantic_analyser.progress_allocator);
    memory_zero(progress);
    return progress;
}

template<typename T>
T* workload_executer_allocate_workload(AST::Node* mapping_node)
{
    auto& executer = workload_executer;
    executer.progress_was_made = true;

    // Create new workload
    T* result = new T;
    Workload_Base* workload = &result->base;
    workload->type = Helpers::get_workload_type(result);
    workload->is_finished = false;
    workload->was_started = false;
    workload->cluster = 0;
    workload->dependencies = list_create<Workload_Base*>();
    workload->dependents = list_create<Workload_Base*>();
    workload->reachable_clusters = dynamic_array_create_empty<Workload_Base*>(1);
    workload->block_stack = dynamic_array_create_empty<AST::Code_Block*>(1);
    workload->current_pass = analysis_pass_allocate(workload, mapping_node);
    workload->real_error_count = 0;
    workload->errors_due_to_unknown_count = 0;
    workload->ignore_unknown_errors = false;
    workload->polymorphic_values.data = nullptr;
    workload->polymorphic_values.size = 0;
    workload->symbol_access_level = Symbol_Access_Level::GLOBAL;
    workload->current_function = 0;
    workload->current_expression = 0;
    workload->statement_reachable = true;

    if (semantic_analyser.current_workload != 0) {
        workload->polymorphic_instanciation_depth = semantic_analyser.current_workload->polymorphic_instanciation_depth;
    }
    else {
        workload->polymorphic_instanciation_depth = 0;
    }
    if (semantic_analyser.current_workload != 0 && semantic_analyser.current_workload->current_symbol_table != 0) {
        workload->current_symbol_table = semantic_analyser.current_workload->current_symbol_table;
    }
    else {
        workload->current_symbol_table = semantic_analyser.root_symbol_table;
    }

    // Add to workload queue
    dynamic_array_push_back(&executer.all_workloads, workload);
    dynamic_array_push_back(&executer.runnable_workloads, workload); // Note: There exists a check for dependencies before executing runnable workloads, so this is ok

    return result;
}



// Note: If base progress != 0, then instance information (Progress-Type + instanciation depth) will not be set by this function
Function_Progress* function_progress_create_with_modtree_function(
    Symbol* symbol, AST::Expression* function_node, Datatype_Function* function_type,
    Function_Progress* base_progress = 0, Symbol_Access_Level symbol_access_level = Symbol_Access_Level::GLOBAL)
{
    assert(function_node->type == AST::Expression_Type::FUNCTION, "Has to be function!");

    Symbol_Table* parameter_table = 0;
    if (base_progress == 0) {
        parameter_table = symbol_table_create_with_parent(semantic_analyser.current_workload->current_symbol_table, symbol_access_level);
    }
    else {
        assert(base_progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE, "");
        parameter_table = base_progress->function->parameter_table;
    }

    // Create progress
    auto progress = new Function_Progress;
    dynamic_array_push_back(&semantic_analyser.allocated_function_progresses, progress);
    progress->type = Polymorphic_Analysis_Type::NON_POLYMORPHIC;
    progress->function = modtree_function_create_empty(function_type, symbol, progress, parameter_table);

    // Set Symbol info
    if (symbol != 0) {
        symbol->type = Symbol_Type::FUNCTION;
        symbol->options.function = progress;
    }

    // Add workloads
    if (base_progress == 0) {
        progress->header_workload = workload_executer_allocate_workload<Workload_Function_Header>(upcast(function_node));
        auto header_workload = progress->header_workload;
        header_workload->progress = progress;
        header_workload->function_node = function_node;
        header_workload->base.current_symbol_table = progress->function->parameter_table;
        header_workload->base.current_function = progress->function;
        header_workload->base.symbol_access_level = Symbol_Access_Level::INTERNAL;
    }
    else {
        progress->header_workload = base_progress->header_workload; // Note: Not sure if this is ever necessary, but we link to base progress header
    }

    progress->body_workload = workload_executer_allocate_workload<Workload_Function_Body>(upcast(function_node->options.function.body));
    progress->body_workload->body_node = function_node->options.function.body;
    progress->body_workload->progress = progress;
    progress->body_workload->base.current_symbol_table = parameter_table; // Sets correct symbol table for code
    progress->body_workload->base.current_function = progress->function;
    progress->body_workload->base.symbol_access_level = Symbol_Access_Level::INTERNAL;
    progress->function->code_workload = upcast(progress->body_workload);

    progress->compile_workload = workload_executer_allocate_workload<Workload_Function_Cluster_Compile>(0);
    progress->compile_workload->functions = dynamic_array_create_empty<ModTree_Function*>(1);
    progress->compile_workload->base.current_function = progress->function;
    dynamic_array_push_back(&progress->compile_workload->functions, progress->function);
    progress->compile_workload->progress = progress;

    // Add dependencies between workloads
    if (base_progress == 0) {
        analysis_workload_add_dependency_internal(upcast(progress->body_workload), upcast(progress->header_workload));
    }
    else {
        // Note: We run the body workload only after the base-analysis completely succeeded
        analysis_workload_add_dependency_internal(upcast(progress->body_workload), upcast(base_progress->body_workload));
    }
    analysis_workload_add_dependency_internal(upcast(progress->compile_workload), upcast(progress->body_workload));

    return progress;
}

Workload_Structure_Body* workload_structure_create(AST::Expression* struct_node, Symbol* symbol,
    bool is_polymorphic_instance, Symbol_Access_Level access_level = Symbol_Access_Level::GLOBAL)
{
    assert(struct_node->type == AST::Expression_Type::STRUCTURE_TYPE, "Has to be struct!");
    auto& struct_info = struct_node->options.structure;

    // Create body workload
    auto body_workload = workload_executer_allocate_workload<Workload_Structure_Body>(upcast(struct_node));
    body_workload->arrays_depending_on_struct_size = dynamic_array_create_empty<Datatype_Array*>(1);
    body_workload->struct_type = type_system_make_struct_empty(struct_info.type, (symbol == 0 ? 0 : symbol->id), body_workload);
    body_workload->struct_node = struct_node;
    body_workload->polymorphic_type = Polymorphic_Analysis_Type::NON_POLYMORPHIC;
    body_workload->base.symbol_access_level = access_level;

    if (is_polymorphic_instance) {
        body_workload->polymorphic_type = Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
        body_workload->base.current_symbol_table = 0; // This has to be set to base-analysis symbol table at some point
        // Note: Polymorphic instance infos aren't filled out here!
        return body_workload;
    }

    // Set Symbol
    if (symbol != 0) {
        symbol->type = Symbol_Type::TYPE;
        symbol->options.type = upcast(body_workload->struct_type);
    }

    // Check for polymorphism
    if (struct_info.parameters.size != 0) {
        auto poly_workload = workload_executer_allocate_workload<Workload_Structure_Polymorphic>(upcast(struct_node));
        poly_workload->base.symbol_access_level = access_level;
        poly_workload->body_workload = body_workload;
        body_workload->polymorphic_type = Polymorphic_Analysis_Type::POLYMORPHIC_BASE;
        body_workload->polymorphic.base = poly_workload;
        // Polymorphic base info get's initilaized in header workload
        analysis_workload_add_dependency_internal(upcast(body_workload), upcast(poly_workload));
    }

    return body_workload;
}

Bake_Progress* bake_progress_create(AST::Expression* bake_expr, Datatype* expected_type)
{
    assert(bake_expr->type == AST::Expression_Type::BAKE_EXPR ||
        bake_expr->type == AST::Expression_Type::BAKE_BLOCK, "Must be bake!");

    auto progress = analysis_progress_allocate_internal<Bake_Progress>();
    progress->bake_function = modtree_function_create_empty(type_system_make_function({}), 0, 0, 0);
    progress->result = optional_make_failure<Upp_Constant>();
    progress->result_type = expected_type;

    // Create workloads
    progress->analysis_workload = workload_executer_allocate_workload<Workload_Bake_Analysis>(upcast(bake_expr));
    progress->analysis_workload->bake_node = bake_expr;
    progress->analysis_workload->progress = progress;
    progress->analysis_workload->base.symbol_access_level = Symbol_Access_Level::INTERNAL;
    progress->bake_function->code_workload = upcast(progress->analysis_workload);

    progress->execute_workload = workload_executer_allocate_workload<Workload_Bake_Execution>(0);
    progress->execute_workload->bake_node = bake_expr;
    progress->execute_workload->progress = progress;

    // Add dependencies between workloads
    analysis_workload_add_dependency_internal(upcast(progress->execute_workload), upcast(progress->analysis_workload));

    return progress;
}

Module_Progress* module_progress_create(AST::Module* module, Symbol* symbol, Symbol_Table* parent_symbol_table) {
    // Create progress
    auto progress = analysis_progress_allocate_internal<Module_Progress>();
    progress->symbol = symbol;
    if (symbol != 0) {
        symbol->type = Symbol_Type::MODULE;
        symbol->options.module_progress = progress;
    }

    // Create analysis workload
    progress->module_analysis = workload_executer_allocate_workload<Workload_Module_Analysis>(upcast(module));
    {
        auto analysis = progress->module_analysis;
        analysis->module_node = module;
        analysis->symbol_table = symbol_table_create_with_parent(parent_symbol_table, Symbol_Access_Level::GLOBAL);
        analysis->base.current_symbol_table = analysis->symbol_table;
        analysis->last_import_workload = 0;
        analysis->progress = progress;
        analysis->parent_analysis = 0;
    }

    // Create event workload
    progress->event_symbol_table_ready = workload_executer_allocate_workload<Workload_Event>(0);
    progress->event_symbol_table_ready->description = "Symbol table ready event";
    analysis_workload_add_dependency_internal(upcast(progress->event_symbol_table_ready), upcast(progress->module_analysis));

    return progress;
}

// Create correct workloads for comptime definitions, for non-comptime checks if its a variable or a global and sets the symbol correctly
void analyser_create_symbol_and_workload_for_definition(AST::Definition* definition)
{
    Symbol_Table* current_table = semantic_analyser.current_workload->current_symbol_table;
    assert(!(definition->values.size == 0 && definition->types.size == 0), "Cannot have values and types be 0");
    assert(definition->symbols.size != 0, "Parser shouldn't allow this");

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
        case AST::Expression_Type::STRUCTURE_TYPE: initial_symbol_type = Symbol_Type::TYPE; break;
        default: break;
        }
    }

    for (int i = 0; i < definition->symbols.size; i++) {
        auto info = get_info(definition->symbols[i], true);
        info->symbol = symbol_table_define_symbol(
            current_table, definition->symbols[i]->name, initial_symbol_type, AST::upcast(definition->symbols[i]),
            is_local_variable ? Symbol_Access_Level::INTERNAL : Symbol_Access_Level::GLOBAL
        );
    }

    // Only define symbols for local variables, analysis will happen when the statement is reached
    if (is_local_variable) {
        return;
    }

    // Report errors
    bool error_occured = false;
    for (int i = 1; i < definition->symbols.size; i++) {
        log_semantic_error("Multiple Symbols not allowed for global/comptime definition, e.g. '::'", upcast(definition->symbols[i]));
        error_occured = true;
    }
    for (int i = 1; i < definition->values.size; i++) {
        log_semantic_error("Multiple Values not allowed for global/comptime definition, e.g. '::'", upcast(definition->values[i]));
        error_occured = true;
    }
    if (!is_global_variable && definition->types.size > 0) {
        log_semantic_error("Type not allowed on comptime definition", upcast(definition->types[0]));
        error_occured = true;
    }
    for (int i = 1; i < definition->types.size; i++) {
        log_semantic_error("Multiple types currently not allowed on global/comptime definition", upcast(definition->types[i]));
        error_occured = true;
    }
    if (definition->values.size == 0 && !is_global_variable) {
        log_semantic_error("Comptime definition must have a value", upcast(definition));
        error_occured = true;
    }
    if (error_occured) {
        // Set all defined symbols to error_symbol
        for (int i = 0; i < definition->symbols.size; i++) {
            auto info = get_info(definition->symbols[i]);
            info->symbol->type = Symbol_Type::ERROR_SYMBOL;
        }
        return;
    }

    // Check if we are currently in a module discovery workload
    Workload_Base* symbol_finish_workload = 0;
    if (semantic_analyser.current_workload->type == Analysis_Workload_Type::MODULE_ANALYSIS) {
        symbol_finish_workload = upcast(downcast<Workload_Module_Analysis>(semantic_analyser.current_workload)->progress->event_symbol_table_ready);
    }

    Workload_Base* operator_context_workload = 0;
    if (semantic_analyser.current_workload->current_symbol_table != 0) {
        if (semantic_analyser.current_workload->current_symbol_table->operator_context->workload != 0) {
            operator_context_workload = upcast(semantic_analyser.current_workload->current_symbol_table->operator_context->workload);
        }
    }

    // Create workload for functions, structs and modules directly
    Symbol* symbol = get_info(definition->symbols[0])->symbol;
    if (definition->is_comptime)
    {
        AST::Expression* value = definition->values[0];
        // Check if it's a 'named' construct (function, struct, module)
        switch (value->type)
        {
        case AST::Expression_Type::MODULE:
        {
            auto module_progress = module_progress_create(value->options.module, symbol, current_table);
            symbol->type = Symbol_Type::MODULE;
            symbol->options.module_progress = module_progress;

            // Add dependencies between parent and child module
            if (semantic_analyser.current_workload->type == Analysis_Workload_Type::MODULE_ANALYSIS)
            {
                auto current = downcast<Workload_Module_Analysis>(semantic_analyser.current_workload);
                module_progress->module_analysis->parent_analysis = current;
                // Add Parent-Dependency for Symbol-Ready (e.g. normal workloads can run and do symbol lookups)
                module_progress->module_analysis->last_import_workload = current->last_import_workload;
                if (current->last_import_workload != 0) {
                    analysis_workload_add_dependency_internal(upcast(module_progress->event_symbol_table_ready), upcast(current->last_import_workload));
                }
            }
            return;
        }
        case AST::Expression_Type::FUNCTION: {
            // Note: Creating the progress also sets the symbol type
            auto workload = upcast(function_progress_create_with_modtree_function(symbol, value, 0)->header_workload);
            if (symbol_finish_workload != 0) {
                analysis_workload_add_dependency_internal(workload, symbol_finish_workload);
            }
            if (operator_context_workload != 0) {
                analysis_workload_add_dependency_internal(workload, operator_context_workload);
            }
            return;
        }
        case AST::Expression_Type::STRUCTURE_TYPE: {
            // Note: Creating the progress also sets the symbol type
            auto workload = upcast(workload_structure_create(value, symbol, false));
            if (symbol_finish_workload != 0) {
                analysis_workload_add_dependency_internal(workload, symbol_finish_workload);
            }
            if (operator_context_workload != 0) {
                analysis_workload_add_dependency_internal(workload, operator_context_workload);
            }
            return;
        }
        default: break;
        }
    }

    // Create workload for global variables/comptime definitions
    auto definition_workload = workload_executer_allocate_workload<Workload_Definition>(upcast(definition));
    definition_workload->symbol = symbol;
    definition_workload->is_comptime = definition->is_comptime;
    definition_workload->type_node = 0;
    if (is_global_variable && definition->types.size != 0) {
        definition_workload->type_node = definition->types[0];
    }
    definition_workload->value_node = 0;
    if (definition->values.size != 0) {
        definition_workload->value_node = definition->values[0];
    }
    if (symbol_finish_workload != 0) {
        analysis_workload_add_dependency_internal(upcast(definition_workload), symbol_finish_workload);
    }
    if (operator_context_workload != 0) {
        analysis_workload_add_dependency_internal(upcast(definition_workload), operator_context_workload);
    }

    symbol->type = Symbol_Type::DEFINITION_UNFINISHED;
    symbol->options.definition_workload = definition_workload;
}



// MOD_TREE (Note: Doesn't set symbol to anything!)
ModTree_Function* modtree_function_create_empty(Datatype_Function* signature, Symbol* symbol, Function_Progress* progress, Symbol_Table* param_table)
{
    ModTree_Function* function = new ModTree_Function;
    function->symbol = symbol;
    function->signature = signature;
    function->code_workload = 0;
    function->progress = progress;
    function->parameter_table = param_table;
    function->function_index_plus_one = semantic_analyser.program->functions.size + 1;

    function->called_from = dynamic_array_create_empty<ModTree_Function*>(1);
    function->calls = dynamic_array_create_empty<ModTree_Function*>(1);
    function->contains_errors = false;
    function->is_runnable = true;

    dynamic_array_push_back(&semantic_analyser.program->functions, function);
    return function;
}

void modtree_function_destroy(ModTree_Function* function)
{
    dynamic_array_destroy(&function->called_from);
    dynamic_array_destroy(&function->calls);
    delete function;
}

ModTree_Global* modtree_program_add_global(Datatype* type)
{
    auto type_size = type_wait_for_size_info_to_finish(type);

    auto global = new ModTree_Global;
    global->type = type;
    global->has_initial_value = false;
    global->init_expr = 0;
    global->index = semantic_analyser.program->globals.size;
    global->memory = stack_allocator_allocate_size(&semantic_analyser.global_variable_memory_pool, type_size->size, type_size->alignment);
    dynamic_array_push_back(&semantic_analyser.program->globals, global);
    return global;
}

ModTree_Program* modtree_program_create()
{
    ModTree_Program* result = new ModTree_Program();
    result->main_function = 0;
    result->functions = dynamic_array_create_empty<ModTree_Function*>(16);
    result->globals = dynamic_array_create_empty<ModTree_Global*>(16);
    return result;
}

void modtree_program_destroy()
{
    auto& program = semantic_analyser.program;
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
    const char* message;
    void* data;
    Datatype* data_type;
};

Comptime_Result expression_calculate_comptime_value_internal(AST::Expression* expr);

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

Comptime_Result comptime_result_apply_cast(Comptime_Result value, Cast_Type cast_type, Datatype* result_type)
{
    auto& analyser = semantic_analyser;
    if (value.type == Comptime_Result_Type::NOT_COMPTIME) {
        return comptime_result_make_not_comptime(value.message);
    }
    else if (value.type == Comptime_Result_Type::UNAVAILABLE) {
        return comptime_result_make_unavailable(result_type, value.message);
    }

    Instruction_Type instr_type = (Instruction_Type)-1;
    switch (cast_type)
    {
    case Cast_Type::INVALID: return comptime_result_make_unavailable(result_type, "Expression contains invalid cast"); // Invalid means an error was already logged
    case Cast_Type::NO_CAST: return value;
    case Cast_Type::FLOATS: instr_type = Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE; break;
    case Cast_Type::FLOAT_TO_INT: instr_type = Instruction_Type::CAST_FLOAT_INTEGER; break;
    case Cast_Type::INT_TO_FLOAT: instr_type = Instruction_Type::CAST_INTEGER_FLOAT; break;
    case Cast_Type::INTEGERS: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
    case Cast_Type::ENUM_TO_INT: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
    case Cast_Type::INT_TO_ENUM: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
    case Cast_Type::POINTERS: 
    case Cast_Type::U64_TO_POINTER:
    case Cast_Type::POINTER_TO_U64: {
        // Pointers values can be interchanged with other pointers or with u64 values
        return comptime_result_make_available(value.data, result_type); 
    }
    case Cast_Type::ARRAY_TO_SLICE: {
        Upp_Slice_Base* slice = stack_allocator_allocate<Upp_Slice_Base>(&analyser.comptime_value_allocator);
        slice->data = value.data;
        slice->size = downcast<Datatype_Array>(value.data_type)->element_count;
        return comptime_result_make_available(slice, result_type);
    }
    case Cast_Type::TO_ANY: {
        Upp_Any* any = stack_allocator_allocate<Upp_Any>(&analyser.comptime_value_allocator);
        any->type = result_type->type_handle;
        any->data = value.data;
        return comptime_result_make_available(any, result_type);
    }
    case Cast_Type::FROM_ANY: {
        Upp_Any* given = (Upp_Any*)value.data;
        if (given->type.index >= (u32)compiler.type_system.types.size) {
            return comptime_result_make_not_comptime("Any contained invalid type_id");
        }
        Datatype* any_type = compiler.type_system.types[given->type.index];
        if (!types_are_equal(any_type, value.data_type)) {
            return comptime_result_make_not_comptime("Any type_handle value doesn't match result type, actually not sure if this can happen");
        }
        return comptime_result_make_available(given->data, any_type);
    }
    case Cast_Type::POINTER_NULL_CHECK: {
        void* pointer_value = (void*)value.data;
        bool* result = stack_allocator_allocate<bool>(&analyser.comptime_value_allocator);
        *result = pointer_value != nullptr;
        return comptime_result_make_available(result, upcast(compiler.type_system.predefined_types.bool_type));
    }
    default: panic("");
    }
    if ((int)instr_type == -1) panic("");

    type_wait_for_size_info_to_finish(result_type);
    auto& size_info = result_type->memory_info.value;
    void* result_buffer = stack_allocator_allocate_size(&analyser.comptime_value_allocator, size_info.size, size_info.alignment);
    bytecode_execute_cast_instr(
        instr_type, result_buffer, value.data,
        type_base_to_bytecode_type(result_type),
        type_base_to_bytecode_type(value.data_type)
    );
    return comptime_result_make_available(result_buffer, result_type);
}

Comptime_Result expression_calculate_comptime_value_without_context_cast(AST::Expression* expr)
{
    auto& analyser = semantic_analyser;
    Predefined_Types& types = compiler.type_system.predefined_types;

    auto info = get_info(expr);
    if (info->contains_errors) {
        return comptime_result_make_unavailable(expression_info_get_type(info, true), "Analysis contained errors");
    }

    switch (info->result_type)
    {
    case Expression_Result_Type::CONSTANT: {
        auto& upp_const = info->options.constant;
        return comptime_result_make_available(upp_const.memory, upp_const.type);
    }
    case Expression_Result_Type::TYPE: {
        return comptime_result_make_available(&info->options.type->type_handle, types.type_handle);
    }
    case Expression_Result_Type::NOTHING: {
        return comptime_result_make_not_comptime("Void not comptime!");
    }
    case Expression_Result_Type::FUNCTION: {
        auto function = info->options.function;
        return comptime_result_make_available(&function->function_index_plus_one, upcast(function->signature));
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

    auto& poly_access = semantic_analyser.current_workload->polymorphic_values;
    auto result_type = expression_info_get_type(info, true);
    auto result_type_size = type_wait_for_size_info_to_finish(result_type);
    switch (expr->type)
    {
    case AST::Expression_Type::ARRAY_TYPE:
    case AST::Expression_Type::AUTO_ENUM:
    case AST::Expression_Type::BAKE_BLOCK:
    case AST::Expression_Type::BAKE_EXPR:
    case AST::Expression_Type::ENUM_TYPE:
    case AST::Expression_Type::FUNCTION:
    case AST::Expression_Type::LITERAL_READ:
    case AST::Expression_Type::STRUCTURE_TYPE:
    case AST::Expression_Type::SLICE_TYPE:
    case AST::Expression_Type::MODULE:
        panic("Should be handled above!");
    case AST::Expression_Type::ERROR_EXPR: {
        return comptime_result_make_unavailable(types.error_type, "Analysis contained errors");
    }
    case AST::Expression_Type::TEMPLATE_PARAMETER: {
        return comptime_result_make_unavailable(result_type, "In base analysis the value of the polymorphic symbol is not available!");
    }
    case AST::Expression_Type::PATH_LOOKUP:
    {
        auto symbol = get_info(expr->options.path_lookup)->symbol;
        if (symbol->type == Symbol_Type::POLYMORPHIC_VALUE) {
            assert(poly_access.data != 0,
                "In normal analysis we shouldn't be able to access this and in instance this would be already set to constant");
            return comptime_result_make_unavailable(result_type, "Cannot access polymorphic parameter value in base analysis");
        }
        else if (symbol->type == Symbol_Type::ERROR_SYMBOL) {
            return comptime_result_make_unavailable(types.error_type, "Analysis contained error-type");
        }
        return comptime_result_make_not_comptime("Encountered non-comptime symbol");
    }
    case AST::Expression_Type::BINARY_OPERATION:
    {
        Comptime_Result left_val = expression_calculate_comptime_value_internal(expr->options.binop.left);
        Comptime_Result right_val = expression_calculate_comptime_value_internal(expr->options.binop.right);
        if (left_val.type != Comptime_Result_Type::AVAILABLE || right_val.type != Comptime_Result_Type::AVAILABLE)
        {
            if (left_val.type == Comptime_Result_Type::NOT_COMPTIME) {
                return left_val;
            }
            else if (right_val.type == Comptime_Result_Type::NOT_COMPTIME) {
                return right_val;
            }
            else if (left_val.type == Comptime_Result_Type::UNAVAILABLE) {
                return comptime_result_make_unavailable(result_type, left_val.message);
            }
            else {
                return comptime_result_make_unavailable(result_type, right_val.message);
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
        case AST::Binop::INVALID: return comptime_result_make_unavailable(result_type, "Analysis error encountered");
        default: panic("");
        }

        void* result_buffer = stack_allocator_allocate_size(&analyser.comptime_value_allocator, result_type_size->size, result_type_size->alignment);
        if (bytecode_execute_binary_instr(instr_type, type_base_to_bytecode_type(left_val.data_type), result_buffer, left_val.data, right_val.data)) {
            return comptime_result_make_available(result_buffer, result_type);
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
            instr_type = expr->options.unop.type == AST::Unop::NEGATE ?
                Instruction_Type::UNARY_OP_NEGATE : Instruction_Type::UNARY_OP_NOT;
            break;
        }
        case AST::Unop::POINTER: {
            Comptime_Result expr_result = expression_calculate_comptime_value_internal(expr->options.unop.expr);
            // Special case: This could be a pointer type, and not a address_of operation, so we need to check for this case
            if (expr_result.type == Comptime_Result_Type::UNAVAILABLE) {
                return expr_result;
            }
            return comptime_result_make_not_comptime("Address of not supported for comptime values");
        }
        case AST::Unop::DEREFERENCE:
            return comptime_result_make_not_comptime("Dereferencing not supported for comptime values");
        default: panic("");
        }

        Comptime_Result value = expression_calculate_comptime_value_internal(expr->options.unop.expr);
        if (value.type == Comptime_Result_Type::NOT_COMPTIME) {
            return value;
        }
        else if (value.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(result_type, value.message);
        }

        void* result_buffer = stack_allocator_allocate_size(&analyser.comptime_value_allocator, result_type_size->size, result_type_size->alignment);
        bytecode_execute_unary_instr(instr_type, type_base_to_bytecode_type(value.data_type), result_buffer, value.data);
        return comptime_result_make_available(result_buffer, result_type);
    }
    case AST::Expression_Type::CAST: {
        return expression_calculate_comptime_value_internal(expr->options.cast.operand);
    }
    case AST::Expression_Type::ARRAY_ACCESS:
    {
        Comptime_Result value_array = expression_calculate_comptime_value_internal(expr->options.array_access.array_expr);
        Comptime_Result value_index = expression_calculate_comptime_value_internal(expr->options.array_access.index_expr);
        if (value_array.type == Comptime_Result_Type::NOT_COMPTIME) {
            return value_array;
        }
        else if (value_index.type == Comptime_Result_Type::NOT_COMPTIME) {
            return value_index;
        }
        else if (value_array.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(result_type, value_array.message);
        }
        else if (value_index.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(result_type, value_index.message);
        }
        assert(types_are_equal(value_index.data_type, upcast(types.i32_type)), "Must be i32 currently");

        byte* base_ptr = 0;
        int array_size = 0;
        if (value_array.data_type->type == Datatype_Type::ARRAY) {
            base_ptr = (byte*)value_array.data;
            array_size = downcast<Datatype_Array>(value_array.data_type)->element_count;
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
        return comptime_result_make_available(&base_ptr[element_offset], result_type);
    }
    case AST::Expression_Type::MEMBER_ACCESS: {
        auto& access_info = info->specifics.member_access;
        if (access_info.type == Member_Access_Type::STRUCT_POLYMORHPIC_PARAMETER_ACCESS) {
            assert(access_info.struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE, "In instance this should already be constant");
            return comptime_result_make_unavailable(result_type, "Cannot access polymorphic parameter value in base analysis");
        }

        Comptime_Result value_struct = expression_calculate_comptime_value_internal(expr->options.member_access.expr);
        if (value_struct.type == Comptime_Result_Type::NOT_COMPTIME) {
            return value_struct;
        }
        else if (value_struct.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(result_type, value_struct.message);
        }
        auto member = type_signature_find_member_by_id(value_struct.data_type, expr->options.member_access.name);
        assert(member.available, "I think after analysis this member should exist");
        byte* raw_data = (byte*)value_struct.data;
        return comptime_result_make_available(&raw_data[member.value.offset], result_type);
    }
    case AST::Expression_Type::FUNCTION_CALL: {
        return comptime_result_make_not_comptime("Function calls require #bake to be used as comtime values");
    }
    case AST::Expression_Type::NEW_EXPR: {
        // New is always uninitialized, so it cannot have a comptime value (Future: Maybe new with values)
        return comptime_result_make_not_comptime("New cannot be used in comptime values");
    }
    case AST::Expression_Type::ARRAY_INITIALIZER:
    {
        assert(result_type->type == Datatype_Type::ARRAY, "");
        auto array_type = downcast<Datatype_Array>(result_type);
        Datatype* element_type = array_type->element_type;
        assert(element_type->memory_info.available, "");
        if (!array_type->count_known) {
            return comptime_result_make_unavailable(result_type, "Array count is unknown");
        }

        void* result_buffer = stack_allocator_allocate_size(&analyser.comptime_value_allocator, result_type_size->size, result_type_size->alignment);
        auto array_values = expr->options.array_initializer.values;
        assert(array_values.size == array_type->element_count, "");

        bool values_unavailable = false;
        const char* first_unavailable_msg = nullptr;
        for (int i = 0; i < array_values.size; i++)
        {
            auto value_result = expression_calculate_comptime_value_internal(array_values[i]);
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
            return comptime_result_make_unavailable(result_type, first_unavailable_msg);
        }

        return comptime_result_make_available(result_buffer, result_type);
    }
    case AST::Expression_Type::STRUCT_INITIALIZER:
    {
        // Allocate result type
        void* result_buffer = stack_allocator_allocate_size(&analyser.comptime_value_allocator, result_type_size->size, result_type_size->alignment);
        memory_set_bytes(result_buffer, result_type_size->size, 0);
        assert(result_type->type == Datatype_Type::STRUCT, "");
        auto struct_type = downcast<Datatype_Struct>(result_type);

        auto arguments = expr->options.struct_initializer.arguments;
        if (struct_type->struct_type == AST::Structure_Type::STRUCT) 
        {
            for (int i = 0; i < arguments.size; i++) {
                auto arg = arguments[i];
                auto arg_info = get_info(arg);
                auto& member = struct_type->members[arg_info->argument_index];
                assert(member.type->memory_info.available, "");

                auto arg_result = expression_calculate_comptime_value_internal(arg->value);
                if (arg_result.type != Comptime_Result_Type::AVAILABLE) {
                    return arg_result;
                }

                memory_copy((byte*)result_buffer + member.offset, arg_result.data, member.type->memory_info.value.size);
            }
        }
        else {
            // Handle union
            assert(arguments.size == 1, "Should be true for unions");
            auto arg_info = get_info(arguments[0]);
            auto arg_result = expression_calculate_comptime_value_internal(arguments[0]->value);
            auto& member = struct_type->members[arg_info->argument_index];
            assert(member.type->memory_info.available, "");
            if (arg_result.type != Comptime_Result_Type::AVAILABLE) {
                return arg_result;
            }
            memory_copy((byte*)result_buffer + member.offset, arg_result.data, member.type->memory_info.value.size);

            if (struct_type->struct_type == AST::Structure_Type::UNION) {
                auto& tag_member = struct_type->tag_member;
                int tag_value = arg_info->argument_index + 1;
                memory_copy((byte*)result_buffer + tag_member.offset, &tag_value, tag_member.type->memory_info.value.size);
            }
        }
        return comptime_result_make_available(result_buffer, result_type);
    }
    default: panic("");
    }

    panic("");
    return comptime_result_make_not_comptime("Invalid code path");
}

Comptime_Result expression_calculate_comptime_value_internal(AST::Expression* expr)
{
    auto result_no_context = expression_calculate_comptime_value_without_context_cast(expr);
    if (result_no_context.type != Comptime_Result_Type::AVAILABLE) {
        return result_no_context;
    }

    auto info = get_info(expr);
    if (info->cast_info.deref_count != 0 || info->cast_info.take_address_of) {
        // Cannot handle pointers for comptime currently
        return comptime_result_make_not_comptime("Pointer handling not supported by comptime values");
    }

    return comptime_result_apply_cast(result_no_context, info->cast_info.cast_type, info->cast_info.type_afterwards);
}

Optional<Upp_Constant> expression_calculate_comptime_value(AST::Expression* expr, const char* error_message_on_failure, bool* was_not_available = 0)
{
    if (was_not_available != 0) {
        *was_not_available = false;
    }

    Comptime_Result comptime_result = expression_calculate_comptime_value_internal(expr);
    if (comptime_result.type != Comptime_Result_Type::AVAILABLE) {
        if (comptime_result.type == Comptime_Result_Type::NOT_COMPTIME) {
            log_semantic_error(error_message_on_failure, expr);
            log_error_info_comptime_msg(comptime_result.message);
        }
        else {
            if (was_not_available != 0) {
                *was_not_available = true;
            }
            semantic_analyser_set_error_flag(true);
        }
        return optional_make_failure<Upp_Constant>();
    }

    assert(!type_size_is_unfinished(comptime_result.data_type), "");
    auto bytes = array_create_static<byte>((byte*)comptime_result.data, comptime_result.data_type->memory_info.value.size);
    Constant_Pool_Result result = constant_pool_add_constant(comptime_result.data_type, bytes);
    if (!result.success) {
        log_semantic_error(error_message_on_failure, expr);
        log_error_info_constant_status(result.error_message);
        return optional_make_failure<Upp_Constant>();
    }

    return optional_make_success(result.constant);
}

bool expression_has_memory_address(AST::Expression* expr)
{
    auto info = get_info(expr);
    auto type = info->cast_info.type_afterwards;

    switch (info->result_type)
    {
    case Expression_Result_Type::FUNCTION:
    case Expression_Result_Type::HARDCODED_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_STRUCT:
    case Expression_Result_Type::TYPE:
    case Expression_Result_Type::NOTHING:
    case Expression_Result_Type::CONSTANT: // Constant memory must not be written to. (e.g. 5 = 10)
        return false;
    case Expression_Result_Type::VALUE:
        break;
    default:panic("");
    }

    if (info->cast_info.cast_type == Cast_Type::FROM_ANY) {
        // From Any is basically a pointer dereference
        return true;
    }

    switch (expr->type)
    {
    case AST::Expression_Type::PATH_LOOKUP:
        return true; // If expr->result_type is value its a global or variable read
    case AST::Expression_Type::ARRAY_ACCESS: return expression_has_memory_address(expr->options.array_access.array_expr);
    case AST::Expression_Type::MEMBER_ACCESS: return expression_has_memory_address(expr->options.member_access.expr);
    case AST::Expression_Type::UNARY_OPERATION: {
        // Dereference value must, by definition, have a memory_address
        // There are special cases where memory loss is not detected, e.g. &(new int)
        return expr->options.unop.type == AST::Unop::DEREFERENCE;
    }
    case AST::Expression_Type::ERROR_EXPR:
        // Errors shouldn't generate other errors
        return true;
    }

    // All other are expressions generating temporary results
    return false;
}



// Context
Expression_Context expression_context_make_unknown(bool unknown_due_to_error = false) {
    Expression_Context context;
    context.type = Expression_Context_Type::UNKNOWN;
    context.unknown_due_to_error = unknown_due_to_error;
    return context;
}

Expression_Context expression_context_make_auto_dereference() {
    Expression_Context context;
    context.type = Expression_Context_Type::AUTO_DEREFERENCE;
    return context;
}

Expression_Context expression_context_make_specific_type(Datatype* signature, bool is_assignment = false, Cast_Mode cast_mode = Cast_Mode::AUTO) {
    if (datatype_is_unknown(signature)) {
        return expression_context_make_unknown(true);
    }
    Expression_Context context;
    context.type = Expression_Context_Type::SPECIFIC_TYPE_EXPECTED;
    context.expected_type.type = signature;
    context.expected_type.is_assignment_context = is_assignment;
    context.expected_type.cast_mode = cast_mode;
    return context;
}



// Result
void expression_info_set_value(Expression_Info* info, Datatype* result_type)
{
    info->result_type = Expression_Result_Type::VALUE;
    info->options.value_type = result_type;
    info->cast_info.type_afterwards = result_type;
}

void expression_info_set_error(Expression_Info* info, Datatype* result_type)
{
    info->result_type = Expression_Result_Type::VALUE;
    info->options.value_type = result_type;
    info->cast_info.type_afterwards = result_type;
    info->contains_errors = true;
}

void expression_info_set_function(Expression_Info* info, ModTree_Function* function)
{
    info->result_type = Expression_Result_Type::FUNCTION;
    info->options.function = function;
    info->cast_info.type_afterwards = upcast(function->signature);
}

void expression_info_set_hardcoded(Expression_Info* info, Hardcoded_Type hardcoded)
{
    info->result_type = Expression_Result_Type::HARDCODED_FUNCTION;
    info->options.hardcoded = hardcoded;
    info->cast_info.type_afterwards = upcast(hardcoded_type_to_signature(hardcoded));
}

void expression_info_set_type(Expression_Info* info, Datatype* type)
{
    info->result_type = Expression_Result_Type::TYPE;
    info->options.type = type;
    info->cast_info.type_afterwards = compiler.type_system.predefined_types.type_handle;

    if (type->type == Datatype_Type::STRUCT) {
        auto s = downcast<Datatype_Struct>(type);
        if (s->workload != 0) {
            if (s->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
                info->result_type = Expression_Result_Type::POLYMORPHIC_STRUCT;
                info->options.polymorphic_struct = s->workload->polymorphic.base;
            }
        }
    }
}

void expression_info_set_no_value(Expression_Info* info) {
    info->result_type = Expression_Result_Type::NOTHING;
    info->options.type = compiler.type_system.predefined_types.error_type;
    info->cast_info.type_afterwards = compiler.type_system.predefined_types.error_type;
}

void expression_info_set_constant(Expression_Info* info, Upp_Constant constant) {
    info->result_type = Expression_Result_Type::CONSTANT;
    info->options.constant = constant;
    info->cast_info.type_afterwards = constant.type;
}

void expression_info_set_polymorphic_function(Expression_Info* info, Polymorphic_Function_Base* poly_base, ModTree_Function* instance = 0) {
    info->result_type = Expression_Result_Type::POLYMORPHIC_FUNCTION;
    info->options.polymorphic_function.base = poly_base;
    info->options.polymorphic_function.instance_fn = instance;
    info->cast_info.type_afterwards = compiler.type_system.predefined_types.error_type; // Not sure if this is correct for function pointers, but should be fine
}

void expression_info_set_constant(Expression_Info* info, Datatype* signature, Array<byte> bytes, AST::Node* error_report_node)
{
    auto& analyser = semantic_analyser;
    auto result = constant_pool_add_constant(signature, bytes);
    if (!result.success)
    {
        assert(error_report_node != 0, "Error"); // Error report node may only be null if we know that adding the constant cannot fail.
        log_semantic_error("Value cannot be converted to constant value (Not serializable)", error_report_node);
        log_error_info_constant_status(result.error_message);
        expression_info_set_error(info, signature);
        return;
    }
    expression_info_set_constant(info, result.constant);
}

void expression_info_set_constant_enum(Expression_Info* info, Datatype* enum_type, i32 value) {
    expression_info_set_constant(info, enum_type, array_create_static((byte*)&value, sizeof(i32)), 0);
}

void expression_info_set_constant_i32(Expression_Info* info, i32 value) {
    expression_info_set_constant(info, upcast(compiler.type_system.predefined_types.i32_type), array_create_static((byte*)&value, sizeof(i32)), 0);
}

// Returns result type of a value
Datatype* expression_info_get_type(Expression_Info* info, bool before_context_is_applied)
{
    if (!before_context_is_applied) {
        return info->cast_info.type_afterwards;
    }

    auto& types = compiler.type_system.predefined_types;
    switch (info->result_type)
    {
    case Expression_Result_Type::CONSTANT: info->options.constant.type;
    case Expression_Result_Type::VALUE: return info->options.value_type;
    case Expression_Result_Type::FUNCTION: return upcast(info->options.function->signature);
    case Expression_Result_Type::HARDCODED_FUNCTION: return upcast(hardcoded_type_to_signature(info->options.hardcoded));
    case Expression_Result_Type::TYPE: return types.type_handle;
    case Expression_Result_Type::NOTHING: return upcast(types.empty_struct_type);
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_STRUCT: {
        // Not sure about this at all...
        semantic_analyser_set_error_flag(true);
        return upcast(types.error_type);
    }
    default: panic("");
    }
    return types.error_type;
}



//DEPENDENCY GRAPH
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

Workload_Executer* workload_executer_initialize()
{
    workload_executer.all_workloads = dynamic_array_create_empty<Workload_Base*>(8);
    workload_executer.runnable_workloads = dynamic_array_create_empty<Workload_Base*>(8);
    workload_executer.finished_workloads = dynamic_array_create_empty<Workload_Base*>(8);
    workload_executer.workload_dependencies = hashtable_create_empty<Workload_Pair, Dependency_Information>(8, workload_pair_hash, workload_pair_equals);

    workload_executer.progress_was_made = false;
    return &workload_executer;
}

void workload_executer_destroy()
{
    auto& executer = workload_executer;
    for (int i = 0; i < executer.all_workloads.size; i++) {
        analysis_workload_destroy(executer.all_workloads[i]);
    }
    dynamic_array_destroy(&executer.all_workloads);
    dynamic_array_destroy(&executer.runnable_workloads);
    dynamic_array_destroy(&executer.finished_workloads);

    {
        auto iter = hashtable_iterator_create(&executer.workload_dependencies);
        while (hashtable_iterator_has_next(&iter)) {
            SCOPE_EXIT(hashtable_iterator_next(&iter));
            auto& dep_info = iter.value;
            dynamic_array_destroy(&dep_info->fail_indicators);
        }
        hashtable_destroy(&executer.workload_dependencies);
    }
}

void analysis_workload_destroy(Workload_Base* workload)
{
    switch (workload->type)
    {
    case Analysis_Workload_Type::STRUCT_POLYMORPHIC: {
        auto poly = downcast<Workload_Structure_Polymorphic>(workload);
        polymorphic_base_info_destroy(&poly->info);
        break;
    }
    case Analysis_Workload_Type::STRUCT_BODY: {
        auto str = downcast<Workload_Structure_Body>(workload);
        dynamic_array_destroy(&str->arrays_depending_on_struct_size);
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
    dynamic_array_destroy(&workload->block_stack);
    delete workload;
}



// This will set the read + the non-resolved symbol paths to error
void path_lookup_set_info_to_error_symbol(AST::Path_Lookup* path, Workload_Base* workload)
{
    auto error_symbol = semantic_analyser.predefined_symbols.error_symbol;
    // Set unset path nodes to error
    for (int i = 0; i < path->parts.size; i++) {
        auto part = path->parts[i];
        auto info = pass_get_node_info(workload->current_pass, part, Info_Query::TRY_READ);
        if (info == 0) {
            info = pass_get_node_info(workload->current_pass, part, Info_Query::CREATE);
            info->symbol = error_symbol;
        }
    }

    // Set last symbol read of path to error
    pass_get_node_info(workload->current_pass, path->last, Info_Query::CREATE_IF_NULL)->symbol = error_symbol;

    // Set whole path result to error
    pass_get_node_info(workload->current_pass, path, Info_Query::CREATE_IF_NULL)->symbol = error_symbol;
}

void path_lookup_set_result_symbol(AST::Path_Lookup* path, Symbol* symbol) {

    // Set last symbol_read to symbol
    pass_get_node_info(semantic_analyser.current_workload->current_pass, path->last, Info_Query::CREATE_IF_NULL)->symbol = symbol;
    // Set whole read to symbol
    pass_get_node_info(semantic_analyser.current_workload->current_pass, path, Info_Query::CREATE_IF_NULL)->symbol = symbol;
    // If symbol is not error, add reference to symbol
    if (symbol->type != Symbol_Type::ERROR_SYMBOL) {
        dynamic_array_push_back(&symbol->references, path->last);
    }
}

// Queries symbol table for the id, and waits for all found symbol workloads to finish before continuing
void symbol_lookup_resolve(
    AST::Symbol_Lookup* lookup, Symbol_Table* symbol_table, bool search_parents, Symbol_Access_Level access_level, Dynamic_Array<Symbol*>& results)
{
    // Find all symbols with this id
    symbol_table_query_id(symbol_table, lookup->name, search_parents, access_level, &results);

    // Wait for alias symbols to finish their resolution
    for (int i = 0; i < results.size; i++)
    {
        Symbol* symbol = results[i];
        if (symbol->type == Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL) {
            bool dependency_failed = false;
            analysis_workload_add_dependency_internal(
                semantic_analyser.current_workload, upcast(symbol->options.alias_workload), dependency_failure_info_make(&dependency_failed, lookup));
            workload_executer_wait_for_dependency_resolution();
            if (!dependency_failed) {
                // Replace symbol in results array with alias value
                assert(symbol->type == Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL, "Alias symbols should never change i think");
                results[i] = symbol->options.alias_workload->alias_for_symbol;
                assert(results[i]->type != Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL, "Chained aliases should never happen here!");
            }
        }
    }

    // Remove duplicate symbols (May happen because of aliases in different scopes)
    for (int i = 0; i < results.size; i++) {
        Symbol* symbol = results[i];
        for (int j = i + 1; j < results.size; j++) {
            Symbol* other = results[j];
            if (other == symbol) {
                dynamic_array_swap_remove(&results, j);
                j -= 1; // Since we removed one element
            }
        }
    }
}

// Logs an error if overloaded symbols was found
Symbol* symbol_lookup_resolve_to_single_symbol(
    AST::Symbol_Lookup* lookup, Symbol_Table* symbol_table, bool search_parents, Symbol_Access_Level access_level, bool prefer_module_symbols_on_overload = false)
{
    auto info = pass_get_node_info(semantic_analyser.current_workload->current_pass, lookup, Info_Query::CREATE_IF_NULL);
    auto error = semantic_analyser.predefined_symbols.error_symbol;

    // Find all overloads
    auto results = dynamic_array_create_empty<Symbol*>(1);
    SCOPE_EXIT(dynamic_array_destroy(&results));
    symbol_lookup_resolve(lookup, symbol_table, search_parents, access_level, results);

    // Handle result array
    if (results.size == 0) {
        log_semantic_error("Could not resolve Symbol (No definition found)", upcast(lookup));
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
            Module_Progress* module_progress = 0;
            bool multiple_modules_found = false;
            for (int i = 0; i < results.size; i++) {
                auto symbol = results[i];
                if (symbol->type == Symbol_Type::MODULE) {
                    if (module_progress == 0) {
                        module_progress = symbol->options.module_progress;
                    }
                    else {
                        multiple_modules_found = true;
                    }
                }
            }

            if (module_progress != 0 && !multiple_modules_found) {
                info->symbol = module_progress->symbol;
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

        log_semantic_error("Multiple results found for this symbol, cannot decided", upcast(lookup));
        for (int i = 0; i < results.size; i++) {
            log_error_info_symbol(results[i]);
        }
        info->symbol = error;
    }

    return info->symbol;
}

// Returns 0 if the path could not be resolved
Symbol_Table* path_lookup_resolve_only_path_parts(AST::Path_Lookup* path)
{
    auto& analyser = semantic_analyser;
    auto table = semantic_analyser.current_workload->current_symbol_table;
    auto workload = semantic_analyser.current_workload;
    auto error = semantic_analyser.predefined_symbols.error_symbol;

    // Resolve path
    for (int i = 0; i < path->parts.size - 1; i++)
    {
        auto part = path->parts[i];

        // Find symbol of path part
        Symbol* symbol = symbol_lookup_resolve_to_single_symbol(part, table, i == 0, Symbol_Access_Level::GLOBAL, true);
        if (symbol == error) {
            path_lookup_set_info_to_error_symbol(path, workload);
            return 0;
        }

        // Check if we can continue
        if (symbol->type == Symbol_Type::MODULE)
        {
            auto current = workload->type;
            bool dependency_failure = false;
            auto failure_info = dependency_failure_info_make(&dependency_failure, part);
            if (current == Analysis_Workload_Type::IMPORT_RESOLVE) {
                analysis_workload_add_dependency_internal(workload, upcast(symbol->options.module_progress->module_analysis), failure_info);
            }
            else {
                analysis_workload_add_dependency_internal(workload, upcast(symbol->options.module_progress->event_symbol_table_ready), failure_info);
            }
            workload_executer_wait_for_dependency_resolution();
            if (dependency_failure) {
                path_lookup_set_info_to_error_symbol(path, workload);
                return 0;
            }
            else {
                table = symbol->options.module_progress->module_analysis->symbol_table;
            }
        }
        else
        {
            // Report error and exit
            if (symbol->type == Symbol_Type::DEFINITION_UNFINISHED) {
                // FUTURE: It may be possible that symbol resolution needs to create dependencies itself, which would happen here!
                log_semantic_error("Expected module, not a definition (global/comptime)", upcast(part));
            }
            else {
                log_semantic_error("Expected Module as intermediate path nodes", upcast(part));
                log_error_info_symbol(symbol);
            }
            path_lookup_set_info_to_error_symbol(path, semantic_analyser.current_workload);
            return 0;
        }
    }

    return table;
}

// Note: After resolving overloaded symbols, the caller should 1. Set the symbol_info for the symbol_read, and 2. add a reference to the symbol
//       Returns true if the path was resolved, otherwise false if there was an error while resolving the path...
bool path_lookup_resolve(AST::Path_Lookup* path, Dynamic_Array<Symbol*>& symbols)
{
    auto& analyser = semantic_analyser;
    auto error = semantic_analyser.predefined_symbols.error_symbol;

    // Resolve path
    auto symbol_table = path_lookup_resolve_only_path_parts(path);
    if (symbol_table == 0) {
        return false;
    }

    Symbol_Access_Level access_level = analyser.current_workload->symbol_access_level;
    if (path->parts.size != 1) { // When a path is specified always use the least strong access level
        access_level = Symbol_Access_Level::GLOBAL;
    }

    // Resolve symbol
    symbol_lookup_resolve(path->last, symbol_table, path->parts.size == 1, access_level, symbols);
    get_info(path, true)->symbol = error;
    return true;
}

Symbol* path_lookup_resolve_to_single_symbol(AST::Path_Lookup* path, bool prefer_module_symbols_on_overload)
{
    auto& analyser = semantic_analyser;
    auto error = semantic_analyser.predefined_symbols.error_symbol;

    // Resolve path
    auto symbol_table = path_lookup_resolve_only_path_parts(path);
    if (symbol_table == 0) {
        return error;
    }

    Symbol_Access_Level access_level = analyser.current_workload->symbol_access_level;
    if (path->parts.size != 1) { // When a path is specified always use the least strong access level
        access_level = Symbol_Access_Level::GLOBAL;
    }

    // Resolve symbol
    Symbol* symbol = symbol_lookup_resolve_to_single_symbol(path->last, symbol_table, path->parts.size == 1, access_level, prefer_module_symbols_on_overload);
    path_lookup_set_result_symbol(path, symbol);
    return symbol;
}



// When dependencies generate cycles, the cycle must be broken, and this is done via the boolean parameter given here.
// If this parameter is not null, this indicates that this dependency can be broken, and the caller of this function must handle this case correctly
void analysis_workload_add_dependency_internal(Workload_Base* workload, Workload_Base* dependency, Dependency_Failure_Info failure_info)
{
    auto& executer = workload_executer;
    bool can_be_broken = failure_info.fail_indicator != 0;
    if (can_be_broken) {
        *failure_info.fail_indicator = !dependency->is_finished;
    }
    if (dependency->is_finished) {
        return;
    }

    Workload_Pair pair = workload_pair_create(workload, dependency);
    Dependency_Information* infos = hashtable_find_element(&executer.workload_dependencies, pair);
    if (infos == 0) {
        Dependency_Information info;
        info.dependency_node = list_add_at_end(&workload->dependencies, dependency);
        info.dependent_node = list_add_at_end(&dependency->dependents, workload);
        info.fail_indicators = dynamic_array_create_empty<Dependency_Failure_Info>(1);
        info.can_be_broken = can_be_broken;
        if (can_be_broken) {
            dynamic_array_push_back(&info.fail_indicators, failure_info);
        }
        bool inserted = hashtable_insert_element(&executer.workload_dependencies, pair, info);
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

void workload_executer_move_dependency(Workload_Base* move_from, Workload_Base* move_to, Workload_Base* dependency)
{
    auto graph = &workload_executer;
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

void workload_executer_remove_dependency(Workload_Base* workload, Workload_Base* depends_on, bool allow_add_to_runnables, bool dependency_succeeded)
{
    auto graph = &workload_executer;
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

void analysis_workload_add_cluster_dependency(Workload_Base* add_to_workload, Workload_Base* dependency, Dependency_Failure_Info failure_info = dependency_failure_info_make_none())
{
    auto graph = &workload_executer;
    assert(add_to_workload->type == dependency->type && dependency->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE, "");
    Workload_Base* merge_into = analysis_workload_find_associated_cluster(add_to_workload);
    Workload_Base* merge_from = analysis_workload_find_associated_cluster(dependency);
    if (merge_into == merge_from || merge_from->is_finished) {
        return;
    }

    // Check if clusters form loops after new dependency
    Hashtable<Workload_Base*, bool> visited = hashtable_create_pointer_empty<Workload_Base*, bool>(1);
    SCOPE_EXIT(hashtable_destroy(&visited));
    Dynamic_Array<Workload_Base*> workloads_to_merge = dynamic_array_create_empty<Workload_Base*>(1);
    SCOPE_EXIT(dynamic_array_destroy(&workloads_to_merge));
    bool loop_found = cluster_workload_check_for_cyclic_dependency(merge_from, merge_into, &visited, &workloads_to_merge);
    if (!loop_found) {
        dynamic_array_push_back(&merge_into->reachable_clusters, merge_from);
        analysis_workload_add_dependency_internal(merge_into, merge_from, failure_info);
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
                workload_executer_move_dependency(merge_cluster, merge_into, merge_dependency);
            }
            else {
                workload_executer_remove_dependency(merge_cluster, merge_dependency, false, true);
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
        analysis_workload_add_dependency_internal(merge_cluster, merge_into);
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

void workload_executer_resolve()
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

    double start_time = timer_current_time_in_seconds(compiler.timer);
    double time_in_executer = 0;
    double time_in_loop_resolve = 0;
    double time_per_workload_type[100];
    memory_set_bytes(&time_per_workload_type[0], sizeof(double) * 100, 0);
    double last_timestamp = timer_current_time_in_seconds(compiler.timer);

    auto& executer = workload_executer;
    int round_no = 0;
    while (true)
    {
        SCOPE_EXIT(round_no += 1);
        executer.progress_was_made = false;
        // Print workloads and dependencies
        if (PRINT_DEPENDENCIES)
        {
            String tmp = string_create_empty(256);
            SCOPE_EXIT(string_destroy(&tmp));
            string_append_formated(&tmp, "\n\n--------------------\nWorkload Execution Round %d\n---------------------\n", round_no);
            for (int i = 0; i < executer.runnable_workloads.size; i++)
            {
                auto workload = executer.runnable_workloads[i];
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

            for (int i = 0; i < executer.all_workloads.size; i++)
            {
                if (i == 0) {
                    string_append_formated(&tmp, "\nWorkloads with dependencies:\n");
                }
                Workload_Base* workload = executer.all_workloads[i];
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
        for (int i = 0; i < executer.runnable_workloads.size; i++)
        {
            Workload_Base* workload = executer.runnable_workloads[i];
            if (workload->dependencies.count > 0) {
                continue; // Skip runnable workload
            }
            if (workload->is_finished) {
                continue;
            }
            executer.progress_was_made = true;

            if (PRINT_DEPENDENCIES) {
                String tmp = string_create_empty(128);
                analysis_workload_append_to_string(workload, &tmp);
                logg("Executing workload: %s\n", tmp.characters);
                string_destroy(&tmp);
            }

            // TIMING
            double now = timer_current_time_in_seconds(compiler.timer);
            time_in_executer += now - last_timestamp;
            last_timestamp = now;

            bool finished = workload_executer_switch_to_workload(workload);

            // TIMING
            now = timer_current_time_in_seconds(compiler.timer);
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
                    workload_executer_remove_dependency(dependent, workload, true, true);
                }
                assert(workload->dependents.count == 0, "Remove dependency should already have cleared the list!");
            }
            else {
                assert(!finished, "If there are dependencies, the fiber must still be running!");
            }
        }
        dynamic_array_reset(&executer.runnable_workloads);
        if (executer.progress_was_made) {
            if (PRINT_DEPENDENCIES) {
                logg("Progress was made!");
            }
            continue;
        }

        // Check if all workloads finished
        {
            bool all_finished = true;
            for (int i = 0; i < executer.all_workloads.size; i++) {
                if (!executer.all_workloads[i]->is_finished) {
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
        double now = timer_current_time_in_seconds(compiler.timer);
        time_in_executer += now - last_timestamp;
        last_timestamp = now;

        {
            // Initialization
            Hashtable<Workload_Base*, int> workload_to_layer = hashtable_create_pointer_empty<Workload_Base*, int>(4);
            SCOPE_EXIT(hashtable_destroy(&workload_to_layer));
            Hashset<Workload_Base*> unvisited = hashset_create_pointer_empty<Workload_Base*>(4);
            SCOPE_EXIT(hashset_destroy(&unvisited));
            for (int i = 0; i < executer.all_workloads.size; i++) {
                Workload_Base* workload = executer.all_workloads[i];
                if (!workload->is_finished) {
                    hashset_insert_element(&unvisited, workload);
                }
            }

            bool loop_found = false;
            int loop_node_count = -1;
            Workload_Base* loop_node = 0;
            Workload_Base* loop_node_2 = 0;

            // Breadth first search
            Dynamic_Array<int> layer_start_indices = dynamic_array_create_empty<int>(4);
            SCOPE_EXIT(dynamic_array_destroy(&layer_start_indices));
            Dynamic_Array<Workload_Base*> layers = dynamic_array_create_empty<Workload_Base*>(4);
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
                                    else if (hashtable_find_element(&executer.workload_dependencies, workload_pair_create(dependency, scan_for_loops)) != 0) {
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
                Dynamic_Array<Workload_Base*> workload_cycle = dynamic_array_create_empty<Workload_Base*>(1);
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
                    Dependency_Information infos = *hashtable_find_element(&executer.workload_dependencies, pair);
                    if (infos.can_be_broken) {
                        breakable_dependency_found = true;
                        for (int j = 0; j < infos.fail_indicators.size; j++) {
                            log_semantic_error("Cyclic dependencies detected", upcast(infos.fail_indicators[j].error_report_node));
                            for (int k = 0; k < workload_cycle.size; k++) {
                                Workload_Base* workload = workload_cycle[k];
                                log_error_info_cycle_workload(workload);
                            }
                        }
                        workload_executer_remove_dependency(workload, depends_on, true, false);
                    }
                }
                assert(breakable_dependency_found, "");
                executer.progress_was_made = true;
                if (PRINT_DEPENDENCIES) {
                    logg("Resolved cyclic dependency loop!");
                }
            }
        }

        if (executer.progress_was_made) {
            if (PRINT_DEPENDENCIES) {
                logg("Progress was made!");
            }

            // TIMING
            double now = timer_current_time_in_seconds(compiler.timer);
            time_in_loop_resolve += now - last_timestamp;
            last_timestamp = now;

            continue;
        }

        panic("Loops must have been resolved by now, so some progress needs to be have made..\n");
    }


    if (PRINT_TIMING)
    {
        double end_time = timer_current_time_in_seconds(compiler.timer);
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
            case Analysis_Workload_Type::IMPORT_RESOLVE: str = "Import Resolve   "; break;
            case Analysis_Workload_Type::OPERATOR_CONTEXT_CHANGE: str = "Operator Context Change   "; break;
            case Analysis_Workload_Type::EVENT: str = "Event           "; break;

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

void workload_add_to_runnable_queue_if_possible(Workload_Base* workload)
{
    auto graph = &workload_executer;
    if (!workload->is_finished && workload->dependencies.count == 0) {
        dynamic_array_push_back(&graph->runnable_workloads, workload);
        graph->progress_was_made = true;
    }
}

bool workload_executer_switch_to_workload(Workload_Base* workload)
{
    if (!workload->was_started) {
        workload->fiber_handle = fiber_pool_get_handle(compiler.fiber_pool, analysis_workload_entry, workload);
        workload->was_started = true;
    }
    semantic_analyser.current_workload = workload;
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
    semantic_analyser.current_workload = 0;
    return result;
}

void workload_executer_wait_for_dependency_resolution()
{
    Workload_Base* workload = semantic_analyser.current_workload;
    if (workload->dependencies.count != 0) {
        fiber_pool_switch_to_main_fiber(compiler.fiber_pool);
    }
}

void analysis_workload_append_to_string(Workload_Base* workload, String* string)
{
    switch (workload->type)
    {
    case Analysis_Workload_Type::MODULE_ANALYSIS: {
        auto module = downcast<Workload_Module_Analysis>(workload);
        string_append_formated(string, "Module analysis %s", module->progress->symbol == 0 ? "ROOT" : module->progress->symbol->id->characters);
        break;
    }
    case Analysis_Workload_Type::OPERATOR_CONTEXT_CHANGE: {
        auto module = downcast<Workload_Operator_Context_Change>(workload)->parent_workload;
        string_append_formated(string, "Operator_Context_Change %s", module->progress->symbol == 0 ? "ROOT" : module->progress->symbol->id->characters);
        break;
    }
    case Analysis_Workload_Type::IMPORT_RESOLVE: {
        auto import_node = downcast<Workload_Import_Resolve>(workload)->import_node;
        string_append_formated(string, "Import ");
        if (import_node->type == AST::Import_Type::FILE) {
            string_append_formated(string, "\"%s\"", import_node->file_name->characters);
        }
        else {
            AST::path_lookup_append_to_string(import_node->path, string);
            if (import_node->type == AST::Import_Type::MODULE_SYMBOLS) {
                string_append_formated(string, "~*");
            }
            else if (import_node->type == AST::Import_Type::MODULE_SYMBOLS_TRANSITIVE) {
                string_append_formated(string, "~**");
            }
        }
        break;
    }
    case Analysis_Workload_Type::DEFINITION: {
        auto def = downcast<Workload_Definition>(workload);
        string_append_formated(string, "Definition %s", def->symbol->id->characters);
        if (def->is_comptime) {
            string_append_formated(string, " comptime");
        }
        break;
    }
    case Analysis_Workload_Type::EVENT: {
        string_append_formated(string, "Event %s", downcast<Workload_Event>(workload)->description);
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
        Symbol* symbol = downcast<Workload_Function_Body>(workload)->progress->function->symbol;
        const char* fn_id = symbol == 0 ? "Lambda" : symbol->id->characters;
        string_append_formated(string, "Body \"%s\"", fn_id);
        break;
    }
    case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
    {
        string_append_formated(string, "Cluster-Compile [");
        auto cluster = downcast<Workload_Function_Cluster_Compile>(analysis_workload_find_associated_cluster(workload));
        for (int i = 0; i < cluster->functions.size; i++) {
            Symbol* symbol = cluster->functions[i]->symbol;
            const char* fn_id = symbol == 0 ? "Anonymous" : symbol->id->characters;
            string_append_formated(string, "%s, ", fn_id);
        }
        string_append_formated(string, "]");
        break;
    }
    case Analysis_Workload_Type::FUNCTION_HEADER: {
        Symbol* symbol = downcast<Workload_Function_Header>(workload)->progress->function->symbol;
        const char* fn_id = symbol == 0 ? "Anonymous" : symbol->id->characters;
        string_append_formated(string, "Header \"%s\"", fn_id);
        break;
    }
    case Analysis_Workload_Type::STRUCT_BODY: {
        auto name = downcast<Workload_Structure_Body>(workload)->struct_type->name;
        const char* struct_id = name.available ? name.value->characters : "Anonymous_Struct";
        string_append_formated(string, "Struct-Analysis \"%s\"", struct_id);
        break;
    }
    case Analysis_Workload_Type::STRUCT_POLYMORPHIC: {
        auto name = downcast<Workload_Structure_Polymorphic>(workload)->body_workload->struct_type->name;
        const char* struct_id = name.available ? name.value->characters : "Anonymous_Struct";
        string_append_formated(string, "Struct-Analysis \"%s\"", struct_id);
        break;
    }
    default: panic("");
    }
}

Module_Progress* workload_executer_add_module_discovery(AST::Module* module, bool is_root_module) {
    auto progress = module_progress_create(module, 0, semantic_analyser.root_symbol_table);
    if (is_root_module) {
        semantic_analyser.root_module = progress;
    }
    return progress;
}

Datatype_Memory_Info* type_wait_for_size_info_to_finish(Datatype* type, Dependency_Failure_Info failure_info)
{
    if (!type_size_is_unfinished(type)) {
        return &type->memory_info.value;
    }
    auto waiting_for_type = type;
    while (waiting_for_type->type == Datatype_Type::ARRAY) {
        waiting_for_type = downcast<Datatype_Array>(waiting_for_type)->element_type;
    }
    assert(waiting_for_type->type == Datatype_Type::STRUCT, "");
    auto structure = downcast<Datatype_Struct>(waiting_for_type);

    analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(structure->workload), failure_info);
    workload_executer_wait_for_dependency_resolution();

    if (failure_info.fail_indicator == 0) {
        assert(!type_size_is_unfinished(type), "");
    }
    return &type->memory_info.value;
}



// WORKLOAD EXECUTION AND OTHERS
struct Parameter_Symbol_Lookup
{
    int defined_in_parameter_index;
    String* id;
};

void expression_search_for_implicit_parameters_and_symbol_lookups(
    int defined_in_parameter_index, AST::Expression* expression,
    Dynamic_Array<Implicit_Parameter_Infos>& parameter_locations,
    Dynamic_Array<Parameter_Symbol_Lookup>& symbol_lookups)
{
    if (expression->type == AST::Expression_Type::TEMPLATE_PARAMETER) {
        Implicit_Parameter_Infos location;
        location.defined_in_parameter_index = defined_in_parameter_index;
        location.expression = expression;
        location.id = expression->options.polymorphic_symbol_id;
        dynamic_array_push_back(&parameter_locations, location);
        return;
    }
    if (expression->type == AST::Expression_Type::PATH_LOOKUP) {
        auto path = expression->options.path_lookup;
        if (path->parts.size == 1) { // Skip path lookups, as we are only interested in parameter names usually
            Parameter_Symbol_Lookup lookup;
            lookup.defined_in_parameter_index = defined_in_parameter_index;
            lookup.id = path->last->name;
            dynamic_array_push_back(&symbol_lookups, lookup);
        }
        return;
    }

    // Check if we should stop the recursion
    switch (expression->type)
    {
    case AST::Expression_Type::BAKE_BLOCK:
    case AST::Expression_Type::BAKE_EXPR:
    case AST::Expression_Type::FUNCTION:
    case AST::Expression_Type::MODULE:
        return;
    }

    // Otherwise search all children of the node for further polymorphic_function parameters
    int child_index = 0;
    auto child_node = AST::base_get_child(upcast(expression), child_index);
    while (child_node != 0) {
        if (child_node->type == AST::Node_Type::EXPRESSION) {
            expression_search_for_implicit_parameters_and_symbol_lookups(defined_in_parameter_index, downcast<AST::Expression>(child_node), parameter_locations, symbol_lookups);
        }
        else if (child_node->type == AST::Node_Type::ARGUMENT) {
            auto argument_node = downcast<AST::Argument>(child_node);
            expression_search_for_implicit_parameters_and_symbol_lookups(defined_in_parameter_index, argument_node->value, parameter_locations, symbol_lookups);
        }
        else if (child_node->type == AST::Node_Type::PARAMETER) {
            auto parameter_node = downcast<AST::Parameter>(child_node);
            expression_search_for_implicit_parameters_and_symbol_lookups(defined_in_parameter_index, parameter_node->type, parameter_locations, symbol_lookups);
        }
        child_index += 1;
        child_node = AST::base_get_child(upcast(expression), child_index);
    }
}

void analyse_parameter_type_and_value(Function_Parameter& parameter, AST::Parameter* parameter_node)
{
    parameter.name = optional_make_success(parameter_node->name);

    // Analyse type
    parameter.type = semantic_analyser_analyse_expression_type(parameter_node->type);

    // Check if default value exists
    parameter.has_default_value = parameter_node->default_value.available;
    if (!parameter.has_default_value) {
        parameter.default_value_opt.available = false;
        return;
    }

    // Update symbol access level for default value
    RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->symbol_access_level, Symbol_Access_Level::GLOBAL);

    // Analyse default value
    auto default_value_type = semantic_analyser_analyse_expression_value(
        parameter_node->default_value.value, expression_context_make_specific_type(parameter.type));
    if (parameter_node->is_comptime) {
        log_semantic_error("Comptime parameters cannot have default values", upcast(parameter_node->default_value.value));
        parameter.has_default_value = false;
        parameter.default_value_opt.available = false;
        return;
    }
    if (datatype_is_unknown(parameter.type) || default_value_type != parameter.type) {
        return;
    }

    // Default value must be comptime known and serializable
    parameter.default_value_opt = expression_calculate_comptime_value(parameter_node->default_value.value, "Default values must be comptime");
    return;
}

// Returns not-available if no polymorphic-parameters were found
Optional<Polymorphic_Base_Info> define_parameter_symbols_and_check_for_polymorphism(
    Dynamic_Array<AST::Parameter*> parameter_nodes, Symbol_Table* symbol_table, Function_Progress* progress = 0, AST::Expression* return_type = 0)
{
    auto& types = compiler.type_system.predefined_types;

    // Define parameter symbols and search for symbol-lookups and implicit parameters
    Dynamic_Array<Parameter_Symbol_Lookup> symbol_lookups = dynamic_array_create_empty<Parameter_Symbol_Lookup>(4);
    Dynamic_Array<Implicit_Parameter_Infos> implicit_parameter_infos = dynamic_array_create_empty<Implicit_Parameter_Infos>(1);
    bool delete_implicit_parameters = true;
    SCOPE_EXIT(dynamic_array_destroy(&symbol_lookups));
    SCOPE_EXIT(if (delete_implicit_parameters) { dynamic_array_destroy(&implicit_parameter_infos); });

    int comptime_parameter_count = 0;
    for (int i = 0; i < parameter_nodes.size; i++) {
        auto parameter_node = parameter_nodes[i];

        bool is_comptime = parameter_node->is_comptime || progress == 0;
        Symbol* symbol = symbol_table_define_symbol(
            symbol_table, parameter_node->name, (is_comptime ? Symbol_Type::POLYMORPHIC_VALUE : Symbol_Type::PARAMETER), AST::upcast(parameter_node),
            (is_comptime ? Symbol_Access_Level::POLYMORPHIC : Symbol_Access_Level::INTERNAL)
        );

        if (is_comptime) {
            symbol->options.polymorphic_value.access_index = comptime_parameter_count;
            symbol->options.polymorphic_value.defined_in_parameter_index = i;
            comptime_parameter_count += 1;
        }
        else {
            symbol->options.parameter.function = progress;
            symbol->options.parameter.index_in_polymorphic_signature = i;
            symbol->options.parameter.index_in_non_polymorphic_signature = i - comptime_parameter_count;
        }
        expression_search_for_implicit_parameters_and_symbol_lookups(i, parameter_node->type, implicit_parameter_infos, symbol_lookups);
    }

    // Check for return type
    bool has_return_type = return_type != 0;
    if (has_return_type) {
        expression_search_for_implicit_parameters_and_symbol_lookups(
            parameter_nodes.size, return_type, implicit_parameter_infos, symbol_lookups);
    }

    // Exit if we are non-polymorphic
    if (comptime_parameter_count == 0 && implicit_parameter_infos.size == 0) {
        return optional_make_failure<Polymorphic_Base_Info>();
    }

    // Otherwise create polymorphic base info
    Polymorphic_Base_Info base_info;
    const int parmeter_count = parameter_nodes.size;
    const int return_type_index = has_return_type ? parameter_nodes.size : -1;
    const int polymorphic_value_count = comptime_parameter_count + implicit_parameter_infos.size;
    // Switch progress type to polymorphic base
    base_info.return_type_index = return_type_index;
    base_info.return_type_node = return_type;
    base_info.parameter_nodes = parameter_nodes;
    base_info.symbol_table = symbol_table;
    base_info.parameters = array_create_empty<Polymorphic_Parameter>(parameter_nodes.size + (has_return_type ? 1 : 0));
    base_info.base_parameter_values = array_create_empty<Polymorphic_Value>(polymorphic_value_count);
    base_info.instances = dynamic_array_create_empty<Polymorphic_Instance_Info>(1);
    base_info.implicit_parameter_infos = implicit_parameter_infos;
    delete_implicit_parameters = false;

    // Initialize polymorphic parameter values to error type
    for (int i = 0; i < base_info.base_parameter_values.size; i++) {
        base_info.base_parameter_values[i] = polymorphic_value_make_type(types.error_type);
    }

    // Create parameter infos
    comptime_parameter_count = 0;
    for (int i = 0; i < base_info.parameters.size; i++) {
        auto& parameter = base_info.parameters[i];
        if (i == return_type_index) {
            parameter.is_comptime = false; // The return type itself is never comptime, but it may contain implicit types, e.g. (a: T) -> $T
            parameter.options.index_in_non_polymorphic_signature = -1;
        }
        else {
            auto node = parameter_nodes[i];
            parameter.is_comptime = node->is_comptime || progress == 0;
            if (parameter.is_comptime) {
                parameter.options.value_access_index = comptime_parameter_count;
                comptime_parameter_count += 1;
            }
            else {
                parameter.options.index_in_non_polymorphic_signature = i - comptime_parameter_count;
            }
        }
        parameter.depends_on = dynamic_array_create_null<int>();
        parameter.dependees = dynamic_array_create_null<int>();
        parameter.dependency_count = 0;
        parameter.infos = function_parameter_make_empty();
    }

    // Create symbols for implicit Parameters
    for (int i = 0; i < implicit_parameter_infos.size; i++) {
        auto& implicit = implicit_parameter_infos[i];

        // Create symbol
        Symbol* symbol = symbol_table_define_symbol(
            symbol_table, implicit.id,
            Symbol_Type::POLYMORPHIC_VALUE, AST::upcast(implicit.expression), Symbol_Access_Level::POLYMORPHIC
        );

        // Create type
        // Note: comptime parameter count doesn't increase here, because it is used to seperate comptime from implicit parameters
        auto template_type = type_system_make_template_parameter(symbol, comptime_parameter_count + i, implicit.defined_in_parameter_index);
        implicit.template_parameter = template_type;
        symbol->options.polymorphic_value.access_index = template_type->value_access_index;
        symbol->options.polymorphic_value.defined_in_parameter_index = implicit.defined_in_parameter_index;
        hashtable_insert_element(&semantic_analyser.valid_template_parameters, implicit.expression, template_type);

        // Set base_parameter value to type
        base_info.base_parameter_values[template_type->value_access_index] = polymorphic_value_make_type(upcast(template_type));
    }

    // Add dependencies between parameters based on found symbol-lookups
    Dynamic_Array<Symbol*> symbols = dynamic_array_create_empty<Symbol*>(1);
    SCOPE_EXIT(dynamic_array_destroy(&symbols));
    for (int i = 0; i < symbol_lookups.size; i++)
    {
        auto& lookup = symbol_lookups[i];
        dynamic_array_reset(&symbols);
        symbol_table_query_id(symbol_table, lookup.id, false, Symbol_Access_Level::POLYMORPHIC, &symbols);
        if (symbols.size == 0) {
            continue; // Symbol lookups in header may also go outside of the parameter-table
        }
        else if (symbols.size == 1) {
            auto symbol = symbols[0];
            assert(symbol->type == Symbol_Type::POLYMORPHIC_VALUE, "Other symbols shouldn't be accessible");

            auto& poly_value = symbol->options.polymorphic_value;
            int depends_on_index = poly_value.defined_in_parameter_index;
            if (depends_on_index == lookup.defined_in_parameter_index) { // Check self-dependencies
                if (poly_value.access_index >= comptime_parameter_count) {
                    continue;
                }
            }

            dynamic_array_push_back_check_null(&base_info.parameters[lookup.defined_in_parameter_index].depends_on, depends_on_index);
            dynamic_array_push_back_check_null(&base_info.parameters[depends_on_index].dependees, lookup.defined_in_parameter_index);
            base_info.parameters[lookup.defined_in_parameter_index].dependency_count += 1;
        }
        else {
            // > 2 symbols in the parameter table shouldn't be possible (No overloading for parameters, see define symbol)
            panic("");
        }
    }

    // Generate parameter analysis order
    base_info.parameter_analysis_order = dynamic_array_create_empty<int>(base_info.parameters.size);
    {
        // Add all parameters without dependencies as runnable
        for (int i = 0; i < base_info.parameters.size; i++) {
            auto& parameter = base_info.parameters[i];
            if (parameter.dependency_count == 0) {
                dynamic_array_push_back(&base_info.parameter_analysis_order, i);
            }
        }

        // Generate parameter order
        int runnable_start = 0;
        int runnable_end = base_info.parameter_analysis_order.size;
        int runnable_count = runnable_end - runnable_start;
        while (runnable_count != 0)
        {
            // Add parameters which can run after this workload
            for (int i = runnable_start; i < runnable_end; i++) {
                auto& run = base_info.parameters[base_info.parameter_analysis_order[i]];
                for (int j = 0; j < run.dependees.size; j++) {
                    auto& dependee = base_info.parameters[run.dependees[j]];
                    dependee.dependency_count -= 1;
                    assert(dependee.dependency_count >= 0, "Cannot fall below 0");
                    if (dependee.dependency_count == 0) {
                        dynamic_array_push_back(&base_info.parameter_analysis_order, run.dependees[j]);
                    }
                }
            }

            runnable_start = runnable_end;
            runnable_end = base_info.parameter_analysis_order.size;
            runnable_count = runnable_end - runnable_start;
        }
    }

    // Analyse all parameters in correct order
    semantic_analyser.current_workload->current_symbol_table = symbol_table;
    semantic_analyser.current_workload->symbol_access_level = Symbol_Access_Level::POLYMORPHIC;
    semantic_analyser.current_workload->polymorphic_values = base_info.base_parameter_values;
    for (int i = 0; i < base_info.parameter_analysis_order.size; i++)
    {
        int param_index = base_info.parameter_analysis_order[i];
        auto& param = base_info.parameters[param_index];

        if (param_index == return_type_index) {
            // Return type
            param.infos.type = semantic_analyser_analyse_expression_type(return_type);
        }
        else {
            auto& node = parameter_nodes[param_index];
            analyse_parameter_type_and_value(param.infos, node);
            if (param.is_comptime) {
                base_info.base_parameter_values[param.options.value_access_index] = polymorphic_value_make_type(param.infos.type);
            }
        }
    }

    // Handle cyclic dependencies
    if (base_info.parameter_analysis_order.size != base_info.parameters.size) 
    {
        assert(base_info.parameter_analysis_order.size < base_info.parameters.size, "Must be smaller");

        // Create mask which parameters contain loops
        Array<bool> contains_loop = array_create_empty<bool>(base_info.parameters.size);
        SCOPE_EXIT(array_destroy(&contains_loop));
        for (int i = 0; i < base_info.parameters.size; i++) {
            contains_loop[i] = true;
        }
        for (int i = 0; i < base_info.parameter_analysis_order.size; i++) {
            contains_loop[base_info.parameter_analysis_order[i]] = false;
        }

        // Analyse parameters containing loops (Not analysed before)
        for (int i = 0; i < base_info.parameters.size; i++) {
            auto& param = base_info.parameters[i];
            if (!contains_loop[i]) {
                continue;
            }

            AST::Node* error_report_node = 0;
            if (i == return_type_index) {
                error_report_node = upcast(return_type);
                param.infos.type = semantic_analyser_analyse_expression_type(return_type);
            }
            else {
                auto& node = parameter_nodes[i];
                error_report_node = upcast(node);
                analyse_parameter_type_and_value(param.infos, node);
                if (param.is_comptime) {
                    // Not sure if I want to do that here
                    base_info.base_parameter_values[param.options.value_access_index] = polymorphic_value_make_type(param.infos.type);
                }
            }

            log_semantic_error("Parameter contains circular dependency, to:", error_report_node, Parser::Section::FIRST_TOKEN);
            for (int j = 0; j < param.depends_on.size; j++) {
                int other_index = param.depends_on[j];
                if (!contains_loop[other_index]) {
                    continue;
                }
                if (other_index == return_type_index) {
                    log_error_info_comptime_msg("Return type");
                }
                else {
                    auto other_param = parameter_nodes[other_index];
                    log_error_info_id(other_param->name);
                }
            }
        }
    }

    return optional_make_success(base_info);
}

void polymorphic_base_info_destroy(Polymorphic_Base_Info* info) {
    for (int i = 0; i < info->parameters.size; i++) {
        auto& parameter = info->parameters[i];
        dynamic_array_destroy_check_null(&parameter.dependees);
        dynamic_array_destroy_check_null(&parameter.depends_on);
    }
    for (int i = 0; i < info->instances.size; i++) {
        auto& instance = info->instances[i];
        array_destroy(&instance.instance_parameter_values);
    }
    dynamic_array_destroy(&info->instances);
    dynamic_array_destroy(&info->implicit_parameter_infos);
    dynamic_array_destroy(&info->parameter_analysis_order);
    array_destroy(&info->parameters);
    array_destroy(&info->base_parameter_values);
}

Workload_Import_Resolve* create_import_workload(AST::Import* import_node)
{
    // Check for Syntax-Errors
    if (import_node->type != AST::Import_Type::FILE) {
        if (import_node->path->parts.size == 1 && import_node->alias_name == 0 && import_node->type == AST::Import_Type::SINGLE_SYMBOL) {
            log_semantic_error("Cannot import single symbol, it's already accessible!", upcast(import_node->path));
            return 0;
        }
        if (import_node->path->last->name == import_node->alias_name) {
            log_semantic_error("Using as ... in import requires the name to be different than the original symbol name", upcast(import_node->path));
            return 0;
        }
        if (import_node->path->last->name == 0) {
            // NOTE: This may happen for usage in the Syntax-Editor, look at the parser for more info.
            //       Also i think this is kinda ugly because it's such a special case, but we'll see
            return 0;
        }
        if (import_node->alias_name != 0 && (import_node->type == AST::Import_Type::MODULE_SYMBOLS || import_node->type == AST::Import_Type::MODULE_SYMBOLS_TRANSITIVE)) {
            log_semantic_error("Cannot alias * or ** imports", upcast(import_node->path));
            return 0;
        }
    }

    // Create workload
    auto import_workload = workload_executer_allocate_workload<Workload_Import_Resolve>(upcast(import_node));
    import_workload->import_node = import_node;
    import_workload->symbol = 0;
    import_workload->alias_for_symbol = 0;

    // Define Symbols if there are any
    // NOTE: Symbol_Access_Level::GLOBAL may be wrong when importing a local variable, but I'm not even sure this works
    auto workload = semantic_analyser.current_workload;
    if (import_node->type == AST::Import_Type::SINGLE_SYMBOL) {
        auto name = import_node->path->last->name;
        if (import_node->alias_name != 0) {
            name = import_node->alias_name;
        }
        import_workload->symbol = symbol_table_define_symbol(
            workload->current_symbol_table, name, Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL, upcast(import_node), Symbol_Access_Level::GLOBAL
        );
        import_workload->symbol->options.alias_workload = import_workload;
    }
    else if (import_node->type == AST::Import_Type::FILE && import_node->alias_name != 0) {
        import_workload->symbol = symbol_table_define_symbol(
            workload->current_symbol_table, import_node->alias_name, Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL, upcast(import_node), Symbol_Access_Level::GLOBAL
        );
        import_workload->symbol->options.alias_workload = import_workload;
    }
    return import_workload;
}

void analysis_workload_entry(void* userdata)
{
    Workload_Base* workload = (Workload_Base*)userdata;
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    analyser.current_workload = workload;

    switch (workload->type)
    {
    case Analysis_Workload_Type::MODULE_ANALYSIS:
    {
        auto analysis = downcast<Workload_Module_Analysis>(workload);
        auto module_node = analysis->module_node;

        // Set Symbol table
        get_info(module_node, true)->symbol_table = analysis->symbol_table;
        RESTORE_ON_SCOPE_EXIT(workload->current_symbol_table, analysis->symbol_table);

        // Create import symbols and workloads
        analysis->last_import_workload = 0;
        for (int i = 0; i < module_node->import_nodes.size; i++) {
            auto import_workload = create_import_workload(module_node->import_nodes[i]);
            if (import_workload == 0) {
                continue;
            }
            // Add dependencies
            if (analysis->last_import_workload != 0) {
                analysis_workload_add_dependency_internal(upcast(import_workload), upcast(analysis->last_import_workload));
            }
            analysis->last_import_workload = import_workload;
        }
        if (analysis->last_import_workload != 0) {
            analysis_workload_add_dependency_internal(upcast(analysis->progress->event_symbol_table_ready), upcast(analysis->last_import_workload));
        }

        // Check if operator context changes
        if (module_node->context_changes.size != 0) 
        {
            // Create new operator context
            auto symbol_table = analysis->symbol_table;
            auto parent_context = symbol_table->operator_context;
            auto context = symbol_table_install_new_operator_context(symbol_table);

            // Create new workload
            Workload_Operator_Context_Change* context_change = workload_executer_allocate_workload<Workload_Operator_Context_Change>(nullptr);
            context_change->module_node = analysis->module_node;
            context_change->parent_workload = analysis;
            context_change->context = context;
            symbol_table->operator_context->workload = context_change;
            context->workload = context_change;

            // Add dependencies
            analysis_workload_add_dependency_internal(upcast(context_change), upcast(analysis->progress->event_symbol_table_ready));
            if (parent_context->workload != 0) {
                analysis_workload_add_dependency_internal(upcast(context_change), upcast(parent_context->workload));
            }
        }

        // Create workloads for definitions
        for (int i = 0; i < module_node->definitions.size; i++) {
            analyser_create_symbol_and_workload_for_definition(module_node->definitions[i]);
        }
        break;
    }
    case Analysis_Workload_Type::EVENT: {
        // INFO: Events are only proxies for empty workloads
        break;
    }
    case Analysis_Workload_Type::OPERATOR_CONTEXT_CHANGE: 
    {
        auto change_workload = downcast<Workload_Operator_Context_Change>(workload);
        analyse_operator_context_changes(change_workload->module_node->context_changes, change_workload->context);
        break;
    }
    case Analysis_Workload_Type::IMPORT_RESOLVE:
    {
        auto import_workload = downcast<Workload_Import_Resolve>(workload);
        auto node = import_workload->import_node;

        // Handle file imports
        if (node->type == AST::Import_Type::FILE)
        {
            auto module_progress = compiler_import_and_queue_analysis_workload(node);
            if (module_progress == 0) {
                log_semantic_error("Could not load file", upcast(node));
                import_workload->alias_for_symbol = semantic_analyser.predefined_symbols.error_symbol;
                break;
            }

            // Wait for module discovery to finish
            analysis_workload_add_dependency_internal(workload, upcast(module_progress->module_analysis));
            workload_executer_wait_for_dependency_resolution();

            if (node->alias_name == 0) {
                // Install into current symbol_table
                symbol_table_add_include_table(
                    import_workload->base.current_symbol_table, module_progress->module_analysis->symbol_table, false, Symbol_Access_Level::GLOBAL, upcast(node)
                );
            }
            else {
                import_workload->symbol->type = Symbol_Type::MODULE;
                import_workload->symbol->options.module_progress = module_progress;
            }
            break;
        }
        else if (node->type == AST::Import_Type::SINGLE_SYMBOL) {
            import_workload->alias_for_symbol = path_lookup_resolve_to_single_symbol(node->path, true);
        }
        else { // Import * or **
            Symbol* symbol = path_lookup_resolve_to_single_symbol(node->path, true);
            assert(symbol->type != Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL, "Must not happen here");
            if (symbol->type == Symbol_Type::MODULE)
            {
                auto progress = symbol->options.module_progress;
                // Wait for symbol discovery to finish
                analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(progress->module_analysis));
                workload_executer_wait_for_dependency_resolution();

                // If transitive we need to wait now until the last of their usings finish
                auto last_import = progress->module_analysis->last_import_workload;
                bool failure_indicator = false;
                if (node->type == AST::Import_Type::MODULE_SYMBOLS_TRANSITIVE && last_import != 0 && last_import != import_workload) {
                    analysis_workload_add_dependency_internal(
                        semantic_analyser.current_workload,
                        upcast(progress->module_analysis->last_import_workload),
                        dependency_failure_info_make(&failure_indicator, node->path->last));
                    workload_executer_wait_for_dependency_resolution();
                }

                if (!failure_indicator) {
                    assert(symbol->type == Symbol_Type::MODULE, "Without error symbol type shouldn't change!");
                    // Add import
                    symbol_table_add_include_table(
                        semantic_analyser.current_workload->current_symbol_table,
                        progress->module_analysis->symbol_table,
                        node->type == AST::Import_Type::MODULE_SYMBOLS_TRANSITIVE,
                        Symbol_Access_Level::GLOBAL,
                        upcast(node)
                    );
                }
            }
            else if (symbol->type != Symbol_Type::ERROR_SYMBOL) {
                log_semantic_error("Cannot import from non module", upcast(node));
                log_error_info_symbol(symbol);
            }
        }
        break;
    }
    case Analysis_Workload_Type::DEFINITION:
    {
        auto definition = downcast<Workload_Definition>(workload);
        auto symbol = definition->symbol;

        if (!definition->is_comptime) // Global variable definition
        {
            Expression_Context context = expression_context_make_unknown();
            Datatype* type = 0;
            if (definition->type_node != 0) {
                type = semantic_analyser_analyse_expression_type(definition->type_node);
            }
            if (definition->value_node != 0) {
                if (type != 0) {
                    semantic_analyser_analyse_expression_value(definition->value_node, expression_context_make_specific_type(type));
                }
                else {
                    type = semantic_analyser_analyse_expression_value(definition->value_node, expression_context_make_unknown());
                }
            }

            auto global = modtree_program_add_global(type);
            if (definition->value_node != 0) {
                global->has_initial_value = true;
                global->init_expr = definition->value_node;
                global->definition_workload = downcast<Workload_Definition>(workload);
            }
            symbol->type = Symbol_Type::GLOBAL;
            symbol->options.global = global;
        }
        else // Comptime definition
        {
            auto result = semantic_analyser_analyse_expression_any(definition->value_node, expression_context_make_unknown());
            switch (result->result_type)
            {
            case Expression_Result_Type::VALUE:
            {
                auto comptime = expression_calculate_comptime_value(definition->value_node, "Value must be comptime in comptime definition (:: syntax)");
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
                log_semantic_error("Creating aliases for hardcoded functions currently not supported", AST::upcast(definition->value_node));
                break;
                //symbol->type = Symbol_Type::HARDCODED_FUNCTION;
                //symbol->options.hardcoded = result->options.hardcoded;
                //break;
            }
            case Expression_Result_Type::FUNCTION:
            {
                ModTree_Function* function = result->options.function;
                assert(function->symbol != 0, "Shouldn't happen, we cannot reference a function if it doesn't have a symbol!");
                symbol->type = Symbol_Type::ERROR_SYMBOL;
                log_semantic_error("Creating symbol/function aliases currently not supported", AST::upcast(definition->value_node));
                break;
            }
            case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
                symbol->type = Symbol_Type::ERROR_SYMBOL;
                log_semantic_error("Creating aliases for polymorphic functions not supported!", AST::upcast(definition->value_node));
                break;
            }
            case Expression_Result_Type::NOTHING: {
                symbol->type = Symbol_Type::ERROR_SYMBOL;
                log_semantic_error("Comptime definition expected a value, not nothing (void/no return value)", AST::upcast(definition->value_node));
                break;
            }
            case Expression_Result_Type::TYPE: {
                // TODO: Maybe also disallow this if this is an alias, as above
                symbol->type = Symbol_Type::TYPE;
                symbol->options.type = result->options.type;

                if (result->options.type->type == Datatype_Type::ENUM) {
                    auto enum_type = downcast<Datatype_Enum>(result->options.type);
                    if (enum_type->name == 0) {
                        enum_type->name = symbol->id;
                    }
                }
                break;
            }
            case Expression_Result_Type::POLYMORPHIC_STRUCT: {
                // TODO: Maybe also disallow this if this is an alias, as above
                symbol->type = Symbol_Type::TYPE;
                symbol->options.type = upcast(result->options.polymorphic_struct->body_workload->struct_type);
                break;
            }
            default: panic("");
            }
        }
        break;
    }
    case Analysis_Workload_Type::FUNCTION_HEADER:
    {
        auto header_workload = downcast<Workload_Function_Header>(workload);
        auto progress = header_workload->progress;
        ModTree_Function* function = header_workload->progress->function;
        auto& signature_node = header_workload->function_node->options.function.signature->options.function_signature;
        auto& parameter_nodes = signature_node.parameters;

        Optional<Polymorphic_Base_Info> polymorphic_info_opt = define_parameter_symbols_and_check_for_polymorphism(
            signature_node.parameters, workload->current_symbol_table, progress, 
            (signature_node.return_value.available ? signature_node.return_value.value : nullptr));

        if (polymorphic_info_opt.available)
        {
            // Switch progress type to polymorphic base
            progress->type = Polymorphic_Analysis_Type::POLYMORPHIC_BASE;
            progress->polymorphic.base.base_info = polymorphic_info_opt.value;
            auto& base = progress->polymorphic.base.base_info;

            // Set polymorphic access infos for child workloads
            header_workload->base.symbol_access_level = Symbol_Access_Level::POLYMORPHIC;
            header_workload->base.polymorphic_values = base.base_parameter_values;
            progress->body_workload->base.polymorphic_values = base.base_parameter_values;
            progress->body_workload->base.symbol_access_level = Symbol_Access_Level::INTERNAL;

            // Update function + symbol
            function->is_runnable = false; // Polymorphic base cannot be runnable
            if (function->symbol != 0) {
                function->symbol->type = Symbol_Type::POLYMORPHIC_FUNCTION;
                function->symbol->options.polymorphic_function = &progress->polymorphic.base;
            }

            // Create function signature
            {
                auto signature = type_system_make_function_empty();
                for (int i = 0; i < parameter_nodes.size; i++) {
                    auto& param = base.parameters[i];
                    if (!param.is_comptime) {
                        dynamic_array_push_back(&signature.parameters, param.infos);
                    }
                }
                Datatype* return_type = 0;
                if (signature_node.return_value.available) {
                    return_type = base.parameters[signature_node.parameters.size].infos.type;
                }
                function->signature = type_system_finish_function(signature, return_type);
            }
        }
        else // Handle normal functions
        {
            workload->symbol_access_level = Symbol_Access_Level::POLYMORPHIC;
            auto signature = type_system_make_function_empty();
            for (int i = 0; i < parameter_nodes.size; i++) {
                auto node = parameter_nodes[i];
                assert(!node->is_comptime, "");
                Function_Parameter parameter;
                analyse_parameter_type_and_value(parameter, node);
                dynamic_array_push_back(&signature.parameters, parameter);
            }

            Datatype* return_type = 0;
            if (signature_node.return_value.available) {
                return_type = semantic_analyser_analyse_expression_type(signature_node.return_value.value);
            }
            function->signature = type_system_finish_function(signature, return_type);
        }

        break;
    }
    case Analysis_Workload_Type::FUNCTION_BODY:
    {
        auto body_workload = downcast<Workload_Function_Body>(workload);
        auto code_block = body_workload->body_node;
        auto function = body_workload->progress->function;
        workload->current_function = function;

        // Check if we are polymorphic_function instance and base instance failed
        if (body_workload->progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE)
        {
            Function_Progress* base_progress = polymorphic_function_base_to_function_progress(body_workload->progress->polymorphic.instance_base);
            assert(base_progress->body_workload->base.is_finished, "There must be a wait before this to guarantee this");

            // If base function contains errors, we don't need to analyse the body
            if (base_progress->function->contains_errors) {
                function->contains_errors = true;
                function->is_runnable = false;
                break;
            }
        }

        // Analyse body
        Control_Flow flow = semantic_analyser_analyse_block(code_block);
        if (flow != Control_Flow::RETURNS && function->signature->return_type.available) {
            log_semantic_error("Function is missing a return statement", upcast(code_block), Parser::Section::END_TOKEN);
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
        }

        // Compile/Set error for all functions in cluster
        for (int i = 0; i < cluster->functions.size; i++)
        {
            ModTree_Function* function = cluster->functions[i];
            if (cluster_contains_error) {
                function->is_runnable = false;
            }
            else {
                ir_generator_queue_function(function);
            }
        }
        break;
    }
    case Analysis_Workload_Type::STRUCT_POLYMORPHIC: {
        auto workload_poly = downcast<Workload_Structure_Polymorphic>(workload);
        auto& struct_node = workload_poly->body_workload->struct_node->options.structure;

        // Create new symbol-table, define symbols and analyse parameters
        Symbol_Table* parameter_table = symbol_table_create_with_parent(analyser.current_workload->current_symbol_table, Symbol_Access_Level::GLOBAL);
        workload->symbol_access_level = Symbol_Access_Level::POLYMORPHIC;
        auto poly_info_opt = define_parameter_symbols_and_check_for_polymorphism(struct_node.parameters, parameter_table, 0, 0);
        assert(poly_info_opt.available, "Must be polymorphic");
        auto& info = poly_info_opt.value;
        workload_poly->info = info;

        // Store/Set correct symbol table for base-analysis and instance analysis
        workload_poly->base.current_symbol_table = parameter_table;
        workload_poly->body_workload->base.current_symbol_table = parameter_table;
        workload_poly->body_workload->base.polymorphic_values = info.base_parameter_values;
        workload_poly->body_workload->base.symbol_access_level = Symbol_Access_Level::POLYMORPHIC;
        break;
    }
    case Analysis_Workload_Type::STRUCT_BODY:
    {
        auto workload_structure = downcast<Workload_Structure_Body>(workload);

        auto& struct_node = workload_structure->struct_node->options.structure;
        Datatype_Struct* struct_signature = workload_structure->struct_type;
        auto& members = struct_signature->members;

        // Check if we are a instance analysis
        if (workload_structure->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
            auto& base_struct = workload_structure->polymorphic.instance.parent;
            assert(base_struct->base.is_finished, "At this point the base has to be finished");
            assert(base_struct->body_workload->base.is_finished, "Body base must be also finished");

            // If an error occured in the polymorphic base, set all struct members to error-type
            if (base_struct->base.real_error_count != 0 || base_struct->body_workload->base.real_error_count != 0) {
                for (int i = 0; i < struct_node.members.size; i++) {
                    auto member_node = struct_node.members[i];
                    struct_add_member(struct_signature, member_node->symbols[0]->name, types.error_type);
                }
                type_system_finish_struct(struct_signature);
                break;
            }
        }

        // Analyse all members
        for (int i = 0; i < struct_node.members.size; i++)
        {
            auto member_node = struct_node.members[i];
            for (int j = 0; j < member_node->values.size; j++) {
                log_semantic_error("Cannot add values to struct members (Default values not supported)", AST::upcast(member_node->values[j]));
            }

            // Don't handle multi definitions for now
            if (member_node->symbols.size != 1) {
                log_semantic_error("Multi definition currently not supported in Struct members", AST::upcast(member_node));
                continue;
            }
            if (member_node->types.size != 1) {
                log_semantic_error("Multi definition currently not supported in Struct members", AST::upcast(member_node));
                continue;
            }

            String* name = member_node->symbols[0]->name;
            AST::Expression* type_expression = member_node->types[0];

            Struct_Member member;
            member.id = name;
            member.offset = 0;
            member.type = semantic_analyser_analyse_expression_type(type_expression);

            // Wait for member size to be known 
            {
                bool has_failed = false;
                type_wait_for_size_info_to_finish(member.type, dependency_failure_info_make(&has_failed, 0));
                if (has_failed) {
                    member.type = type_system.predefined_types.error_type;
                    log_semantic_error("Struct contains itself, this can only work with references", upcast(struct_node.members[i]), Parser::Section::IDENTIFIER);
                }
            }
            // Check if name is already in use
            bool name_available = true;
            for (int j = 0; j < members.size && name_available; j++) {
                auto& other = members[j];
                if (other.id == member.id) {
                    log_semantic_error("Member name already in use", upcast(member_node), Parser::Section::IDENTIFIER);
                    name_available = false;
                }
            }
            for (int j = 0; j < struct_node.parameters.size && name_available; j++) {
                auto& param = struct_node.parameters[j];
                if (param->name == member.id) {
                    log_semantic_error("Member name is already taken by parameter", upcast(member_node), Parser::Section::IDENTIFIER);
                    name_available = false;
                }
            }

            if (name_available) {
                dynamic_array_push_back(&members, member);
            }
        }

        type_system_finish_struct(struct_signature);

        // Finish all arrays waiting on this struct 
        for (int i = 0; i < workload_structure->arrays_depending_on_struct_size.size; i++) {
            Datatype_Array* array = workload_structure->arrays_depending_on_struct_size[i];
            type_system_finish_array(array);
        }
        break;
    }
    case Analysis_Workload_Type::BAKE_ANALYSIS:
    {
        auto bake = downcast<Workload_Bake_Analysis>(workload);
        auto function = bake->progress->bake_function;
        auto node = bake->bake_node;
        workload->current_function = function;

        if (node->type == AST::Expression_Type::BAKE_BLOCK)
        {
            auto& code_block = node->options.bake_block;
            auto flow = semantic_analyser_analyse_block(code_block, true);
            if (flow != Control_Flow::RETURNS) {
                log_semantic_error("Missing return statement", AST::upcast(code_block), Parser::Section::END_TOKEN);
            }

            if (bake->progress->result_type == 0) {
                bake->progress->result_type = type_system.predefined_types.error_type;
                bake->progress->bake_function->contains_errors = true;
            }
            function->signature = type_system_make_function({}, bake->progress->result_type);
        }
        else if (node->type == AST::Expression_Type::BAKE_EXPR)
        {
            auto& bake_expr = node->options.bake_expr;
            workload->symbol_access_level = Symbol_Access_Level::POLYMORPHIC;
            auto expression_context = expression_context_make_unknown();
            if (bake->progress->result_type != 0) {
                expression_context = expression_context_make_specific_type(bake->progress->result_type);
            }
            auto result_type = semantic_analyser_analyse_expression_value(bake_expr, expression_context);
            function->signature = type_system_make_function({}, result_type);
        }
        else {
            panic("");
        }
        break;
    }
    case Analysis_Workload_Type::BAKE_EXECUTION:
    {
        auto execute = downcast<Workload_Bake_Execution>(workload);
        auto progress = execute->progress;
        auto& ir_gen = compiler.ir_generator;

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
        auto result_type = progress->bake_function->signature->return_type.value;
        type_wait_for_size_info_to_finish(result_type);

        // Compile
        ir_generator_queue_function(bake_function);
        ir_generator_generate_queued_items(true);

        // Execute
        IR_Function* ir_func = *hashtable_find_element(&ir_gen->function_mapping, bake_function);
        int func_start_instr_index = *hashtable_find_element(&compiler.bytecode_generator->function_locations, ir_func);

        Bytecode_Thread* thread = bytecode_thread_create(10000);
        SCOPE_EXIT(bytecode_thread_destroy(thread));
        bytecode_thread_set_initial_state(thread, func_start_instr_index);
        while (true) {
            bytecode_thread_execute(thread);
            if (thread->exit_code == Exit_Code::TYPE_INFO_WAITING_FOR_TYPE_FINISHED)
            {
                bool cyclic_dependency_occured = false;
                type_wait_for_size_info_to_finish(thread->waiting_for_type_finish_type, dependency_failure_info_make(&cyclic_dependency_occured));
                if (cyclic_dependency_occured) {
                    log_semantic_error("Bake requires type_info which waits on bake, cyclic dependency!", execute->bake_node, Parser::Section::KEYWORD);
                    progress->result = optional_make_failure<Upp_Constant>();
                    return;
                }
            }
            else if (thread->exit_code == Exit_Code::SUCCESS) {
                break;
            }
            else {
                log_semantic_error("Bake function did not return successfully", execute->bake_node, Parser::Section::KEYWORD);
                log_error_info_exit_code(thread->exit_code);
                progress->result = optional_make_failure<Upp_Constant>();
                return;
            }
        }

        // Add result to constant pool
        void* value_ptr = thread->return_register;
        assert(progress->bake_function->signature->return_type.available, "Bake return type must have been stated at this point...");
        Constant_Pool_Result pool_result = constant_pool_add_constant(
            result_type, array_create_static<byte>((byte*)value_ptr, result_type->memory_info.value.size));
        if (!pool_result.success) {
            log_semantic_error("Couldn't serialize bake result", execute->bake_node, Parser::Section::KEYWORD);
            log_error_info_constant_status(pool_result.error_message);
            progress->result = optional_make_failure<Upp_Constant>();
            return;
        }
        progress->result = optional_make_success(pool_result.constant);
        return;
    }
    default: panic("");
    }

    return;
    // OLD EXTERN IMPORTS
    /*
case Analysis_Workload_Type::EXTERN_HEADER_IMPORT:
{
    AST_Node* extern_node = workload.options.extern_header.node;
    String* header_name_id = extern_node->id;
    Optional<C_Import_Package> package = c_importer_import_header(&compiler.c_importer, *header_name_id, &compiler.identifier_pool);
    if (package.available)
    {
        logg("Importing header successfull: %s\n", header_name_id->characters);
        dynamic_array_push_back(&compiler.extern_sources.headers_to_include, header_name_id);
        Hashtable<C_Import_Type*, Datatype*> type_conversion_table = hashtable_create_pointer_empty<C_Import_Type*, Datatype*>(256);
        SCOPE_EXIT(hashtable_destroy(&type_conversion_table));

        AST_Node* import_id_node = extern_node->child_start;
        while (import_id_node != 0)
        {
            SCOPE_EXIT(import_id_node = import_id_node->neighbor);
            String* import_id = import_id_node->id;
            C_Import_Symbol* import_symbol = hashtable_find_element(&package.value.symbol_table.symbols, import_id);
            if (import_symbol == 0) {
                Semantic_Error error;
                error.type = Semantic_Error_Type::EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL;
                error.id = import_id;
                error.error_node = import_id_node;
                log_semantic_error(analyser, error);
                continue;
            }

            switch (import_symbol->type)
            {
            case C_Import_Symbol_Type::TYPE:
            {
                Datatype* type = import_c_type(analyser, import_symbol->data_type, &type_conversion_table);
                if (type->type == Datatype_Type::STRUCT) {
                    hashtable_insert_element(&compiler.extern_sources.extern_type_signatures, type, import_id);
                }
                symbol_table_define_symbol(
                    analyser, workload.options.extern_header.parent_module->symbol_table, import_id,
                    symbol_data_make_type(type), import_id_node
                );
                break;
            }
            case C_Import_Symbol_Type::FUNCTION:
            {
                ModTree_Function* extern_fn = new ModTree_Function;
                extern_fn->parent_module = workload.options.extern_header.parent_module;
                extern_fn->function_type = ModTree_Function_Type::EXTERN_FUNCTION;
                extern_fn->options.extern_function.id = import_id;
                extern_fn->signature = import_c_type(analyser, import_symbol->data_type, &type_conversion_table);
                extern_fn->options.extern_function.function_signature = extern_fn->signature;
                assert(extern_fn->signature->type == Datatype_Type::FUNCTION, "HEY");

                dynamic_array_push_back(&workload.options.extern_header.parent_module->functions, extern_fn);
                extern_fn->symbol = symbol_table_define_symbol(
                    analyser, workload.options.extern_header.parent_module->symbol_table, import_id,
                    symbol_data_make_function(extern_fn), import_id_node
                );
                break;
            }
            case C_Import_Symbol_Type::GLOBAL_VARIABLE: {
                Semantic_Error error;
                error.type = Semantic_Error_Type::MISSING_FEATURE_EXTERN_GLOBAL_IMPORT;
                error.error_node = import_id_node;
                log_semantic_error(analyser, error);
                break;
            }
            default: panic("hey");
            }
        }

        // Import all used type names
        auto iter = hashtable_iterator_create(&package.value.symbol_table.symbols);
        while (hashtable_iterator_has_next(&iter))
        {
            String* id = *iter.key;
            if (symbol_table_find_symbol(workload.options.extern_header.parent_module->symbol_table, id, true, symbol_reference_make_ignore(), analyser) != 0) {
                hashtable_iterator_next(&iter);
                continue;
            }
            C_Import_Symbol* import_sym = iter.value;
            if (import_sym->type == C_Import_Symbol_Type::TYPE)
            {
                Datatype** signature = hashtable_find_element(&type_conversion_table, import_sym->data_type);
                if (signature != 0)
                {
                    Datatype* type = *signature;
                    if (type->type == Datatype_Type::STRUCT) {
                        hashtable_insert_element(&compiler.extern_sources.extern_type_signatures, type, id);
                    }
                    symbol_table_define_symbol(
                        analyser, workload.options.extern_header.parent_module->symbol_table, id,
                        symbol_data_make_type(type), workload.options.extern_header.node
                    );
                }
            }
            hashtable_iterator_next(&iter);
        }
    }
    else {
        Semantic_Error error;
        error.type = Semantic_Error_Type::EXTERN_HEADER_PARSING_FAILED;
        error.error_node = workload.options.extern_header.node;
        log_semantic_error(analyser, error);
    }
    break;
}
case Analysis_Workload_Type::EXTERN_FUNCTION_DECLARATION:
{
    Analysis_Workload_Extern_Function* work = &workload.options.extern_function;
    Symbol_Table* symbol_table = work->parent_module->symbol_table;
    AST_Node* extern_node = work->node;
    Expression_Result_Type result = semantic_analyser_analyse_expression_type(
        analyser, work->parent_module->symbol_table, extern_node->child_start
    );
    switch (result.type)
    {
    case Analysis_Result_Type::SUCCESS:
    {
        if (result.options.type->type != Datatype_Type::FUNCTION) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER;
            error.error_node = extern_node->child_start;
            log_semantic_error(analyser, error);
            break;
        }

        ModTree_Function* extern_fn = new ModTree_Function;
        extern_fn->parent_module = work->parent_module;
        extern_fn->function_type = ModTree_Function_Type::EXTERN_FUNCTION;
        extern_fn->options.extern_function.id = extern_node->id;
        extern_fn->signature = result.options.type;
        extern_fn->options.extern_function.function_signature = extern_fn->signature;
        assert(extern_fn->signature->type == Datatype_Type::FUNCTION, "HEY");
        dynamic_array_push_back(&extern_fn->parent_module->functions, extern_fn);
        dynamic_array_push_back(&compiler.extern_sources.extern_functions, extern_fn->options.extern_function);

        extern_fn->symbol = symbol_table_define_symbol(
            analyser, work->parent_module->symbol_table, extern_fn->options.extern_function.id,
            symbol_data_make_function(extern_fn), work->node
        );
        break;
    }
    case Analysis_Result_Type::DEPENDENCY:
        found_workload_dependency = true;
        found_dependency = result.options.dependency;
        break;
    case Analysis_Result_Type::ERROR_OCCURED:
        break;
    }
    break;
}
*/
}



// EXPRESSIONS
void semantic_analyser_register_function_call(ModTree_Function* call_to)
{
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;

    auto workload = analyser.current_workload;
    if (workload->current_function != 0) {
        dynamic_array_push_back(&workload->current_function->calls, call_to);
        dynamic_array_push_back(&call_to->called_from, workload->current_function);
    }

    auto progress = call_to->progress;
    assert(progress != 0, ("Cannot register a function call to a bake function (only functions where progress is 0)!"));

    switch (analyser.current_workload->type)
    {
    case Analysis_Workload_Type::BAKE_ANALYSIS: {
        analysis_workload_add_dependency_internal(
            upcast(downcast<Workload_Bake_Analysis>(analyser.current_workload)->progress->execute_workload),
            upcast(progress->compile_workload)
        );
        break;
    }
    case Analysis_Workload_Type::FUNCTION_BODY: {
        analysis_workload_add_cluster_dependency(
            upcast(downcast<Workload_Function_Body>(analyser.current_workload)->progress->compile_workload),
            upcast(progress->compile_workload)
        );
        break;
    }
    default: return;
    }
}

Cast_Mode operator_context_get_cast_mode_setting(Operator_Context* context, AST::Context_Setting setting) {
    assert((int)setting >= 0 && (int)setting < AST::CONTEXT_SETTING_CAST_MODE_COUNT, "");
    return context->cast_mode_settings[(int)setting];
}

bool operator_context_get_boolean_setting(Operator_Context* context, AST::Context_Setting setting) {
    assert((int)setting >= AST::CONTEXT_SETTING_CAST_MODE_COUNT && (int)setting < (int)AST::Context_Setting::MAX_ENUM_VALUE, "");
    return context->boolean_settings[(int)setting - AST::CONTEXT_SETTING_CAST_MODE_COUNT];
}

Expression_Info expression_info_make_empty(Expression_Context context)
{
    auto error_type = compiler.type_system.predefined_types.error_type;

    Expression_Info info;
    info.contains_errors = false;
    info.result_type = Expression_Result_Type::VALUE;
    info.options.value_type = error_type;
    info.context = context;
    info.cast_info.type_afterwards = error_type;
    info.cast_info.cast_type = Cast_Type::NO_CAST;
    info.cast_info.deref_count = 0;
    info.cast_info.take_address_of = false;
    info.cast_info.options.error_msg = nullptr;
    return info;
}

Expression_Info analyse_symbol_as_expression(Symbol* symbol, Expression_Context context, AST::Symbol_Lookup* error_report_node)
{
    auto& executer = semantic_analyser.workload_executer;
    auto workload = semantic_analyser.current_workload;
    auto& types = compiler.type_system.predefined_types;
    auto error_type = types.error_type;

    Expression_Info result = expression_info_make_empty(context);

    // Wait for initial symbol dependencies
    bool dependency_failed = false;
    auto failure_info = dependency_failure_info_make(&dependency_failed, error_report_node);
    switch (symbol->type)
    {
    case Symbol_Type::DEFINITION_UNFINISHED:
    {
        analysis_workload_add_dependency_internal(workload, upcast(symbol->options.definition_workload), failure_info);
        break;
    }
    case Symbol_Type::FUNCTION:
    {
        analysis_workload_add_dependency_internal(workload, upcast(symbol->options.function->header_workload), failure_info);
        break;
    }
    case Symbol_Type::POLYMORPHIC_FUNCTION:
    {
        auto progress = polymorphic_function_base_to_function_progress(symbol->options.polymorphic_function);
        analysis_workload_add_dependency_internal(workload, upcast(progress->header_workload), failure_info);
        break;
    }
    case Symbol_Type::TYPE:
    {
        Datatype* type = symbol->options.type;
        if (type->type == Datatype_Type::STRUCT)
        {
            auto struct_workload = downcast<Datatype_Struct>(type)->workload;
            if (struct_workload == 0) {
                assert(!type_size_is_unfinished(type), "");
                break;
            }

            // If it's a polymorphic struct, always wait for the header workload to finish
            if (struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
                analysis_workload_add_dependency_internal(workload, upcast(struct_workload->polymorphic.base), failure_info);
            }
            else if (struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
                analysis_workload_add_dependency_internal(workload, upcast(struct_workload->polymorphic.instance.parent), failure_info);
            }

            // Additionally, we want some workloads to wait until the size has been resolved
            switch (workload->type)
            {
            case Analysis_Workload_Type::DEFINITION: {
                analysis_workload_add_dependency_internal(workload, upcast(struct_workload), failure_info);
                break;
            }
            case Analysis_Workload_Type::BAKE_ANALYSIS: {
                analysis_workload_add_dependency_internal(
                    upcast(downcast<Workload_Bake_Analysis>(workload)->progress->execute_workload),
                    upcast(struct_workload));
                break;
            }
            case Analysis_Workload_Type::FUNCTION_BODY:
            case Analysis_Workload_Type::FUNCTION_HEADER: {
                auto progress = analysis_workload_try_get_function_progress(workload);
                analysis_workload_add_dependency_internal(upcast(progress->compile_workload), upcast(struct_workload));
                break;
            }
            case Analysis_Workload_Type::STRUCT_BODY: break;
            case Analysis_Workload_Type::STRUCT_POLYMORPHIC: break;
            default: panic("Invalid code path");
            }
        }
        break;
    }
    case Symbol_Type::PARAMETER:
    case Symbol_Type::POLYMORPHIC_VALUE: {
        // If i can access this symbol this means the header-analysis must have finished by now
        break;
    }
    default: break;
    }

    // Wait and check if dependency failed
    workload_executer_wait_for_dependency_resolution();
    if (dependency_failed) {
        semantic_analyser_set_error_flag(true);
        expression_info_set_error(&result, error_type);
        return result;
    }

    switch (symbol->type)
    {
    case Symbol_Type::ERROR_SYMBOL: {
        semantic_analyser_set_error_flag(true);
        expression_info_set_error(&result, error_type);
        return result;
    }
    case Symbol_Type::DEFINITION_UNFINISHED: {
        panic("Should not happen, we just waited on this workload to finish!");
    }
    case Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL: {
        panic("Aliases should already be handled, this should only point to a valid symbol");
    }
    case Symbol_Type::HARDCODED_FUNCTION: {
        expression_info_set_hardcoded(&result, symbol->options.hardcoded);
        return result;
    }
    case Symbol_Type::FUNCTION: {
        semantic_analyser_register_function_call(symbol->options.function->function);
        expression_info_set_function(&result, symbol->options.function->function);
        return result;
    }
    case Symbol_Type::GLOBAL: {
        expression_info_set_value(&result, symbol->options.global->type);
        return result;
    }
    case Symbol_Type::TYPE: {
        expression_info_set_type(&result, symbol->options.type);
        return result;
    }
    case Symbol_Type::VARIABLE: {
        assert(workload->type != Analysis_Workload_Type::FUNCTION_HEADER, "Function headers can never access variable symbols!");
        expression_info_set_value(&result, symbol->options.variable_type);
        return result;
    }
    case Symbol_Type::VARIABLE_UNDEFINED: {
        log_semantic_error("Variable not defined at this point", upcast(error_report_node));
        semantic_analyser_set_error_flag(true);
        expression_info_set_error(&result, error_type);
        return result;
    }
    case Symbol_Type::PARAMETER:
    {
        auto& param = symbol->options.parameter;
        auto workload = semantic_analyser.current_workload;

        auto progress = analysis_workload_try_get_function_progress(workload);
        assert(progress != 0, "We should be in function-body workload since normal parameters have internal symbol access");
        expression_info_set_value(&result, progress->function->signature->parameters[param.index_in_non_polymorphic_signature].type);
        return result;
    }
    case Symbol_Type::POLYMORPHIC_VALUE: {
        int access_index = symbol->options.polymorphic_value.access_index;
        auto poly_values = workload->polymorphic_values;
        assert(poly_values.data != 0, "Why can we access non-polymorphic parameter here");

        auto& value = poly_values[access_index];
        if (value.only_datatype_known) {
            semantic_analyser_set_error_flag(true);
            expression_info_set_value(&result, value.options.type);
        }
        else {
            expression_info_set_constant(&result, value.options.value);
        }
        return result;
    }
    case Symbol_Type::COMPTIME_VALUE: {
        expression_info_set_constant(&result, symbol->options.constant);
        return result;
    }
    case Symbol_Type::MODULE: {
        log_semantic_error("Module not valid as expression result", upcast(error_report_node));
        log_error_info_symbol(symbol);
        semantic_analyser_set_error_flag(true);
        expression_info_set_error(&result, error_type);
        return result;
    }
    case Symbol_Type::POLYMORPHIC_FUNCTION: {
        expression_info_set_polymorphic_function(&result, symbol->options.polymorphic_function);
        return result;
    }
    default: panic("HEY");
    }

    return result;
}

enum Callable_Type
{
    FUNCTION,
    POLYMORPHIC, // Either struct or function
    HARDCODED,
    FUNCTION_POINTER,
    STRUCT_INITIALIZER,
};

struct Callable_Polymorphic_Info
{
    Polymorphic_Base_Info* base;
    bool is_function;
    union {
        Polymorphic_Function_Base* polymorphic_function;
        Workload_Structure_Polymorphic* polymorphic_struct;
    } options;
};

struct Callable
{
    Callable_Type type;
    union {
        ModTree_Function* function;
        struct {
            Hardcoded_Type type;
            Datatype_Function* signature;
        } hardcoded;
        Datatype_Function* function_pointer_type;
        Callable_Polymorphic_Info polymorphic;
        Datatype_Struct* structure; // For struct-intitializer
    } options;

    int parameter_count;
    int only_named_arguments_after_index; // This is used for implicit template arguments, which should only be specified by name
};

struct Overload_Candidate
{
    Callable callable;
    Optional<Datatype*> return_value_type;
    Datatype_Function* function_signature; // Only valid for specific callable types, otherwise 0

    // Source info
    Symbol* symbol; // May be null
    Expression_Info expression_info; // May be empty

    // For convenience when resolving overloads
    Datatype* overloading_param_type;
    bool overloading_arg_matches_type;
    bool overloading_arg_can_be_cast;
};

Optional<Overload_Candidate> overload_candidate_try_create_from_expression_info(Expression_Info& info, Symbol* origin_symbol = 0)
{
    Overload_Candidate candidate;
    candidate.symbol = origin_symbol;
    candidate.expression_info = info; // Copies info (Question: Why?)
    candidate.function_signature = 0;
    candidate.return_value_type.available = false;

    Callable& callable = candidate.callable;
    callable.parameter_count = -1;
    callable.only_named_arguments_after_index = -1;

    switch (info.result_type)
    {
    case Expression_Result_Type::NOTHING:
    case Expression_Result_Type::TYPE: {
        return optional_make_failure<Overload_Candidate>();
    }
    case Expression_Result_Type::POLYMORPHIC_STRUCT:
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    {
        auto& poly = callable.options.polymorphic;

        callable.type = Callable_Type::POLYMORPHIC;
        poly.is_function = info.result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION;
        if (poly.is_function) {
            poly.options.polymorphic_function = info.options.polymorphic_function.base;
            poly.base = &info.options.polymorphic_function.base->base_info;
        }
        else {
            poly.options.polymorphic_struct = info.options.polymorphic_struct;
            poly.base = &info.options.polymorphic_struct->info;
        }

        callable.parameter_count = poly.base->parameters.size;
        if (poly.base->return_type_node != 0) {
            callable.parameter_count -= 1; // Return type is included in polymorphic parameter array, but we don't want it for matching
        }
        callable.only_named_arguments_after_index = callable.parameter_count;
        callable.parameter_count += poly.base->implicit_parameter_infos.size; // Implicit parameters can be set with named arguments
        break;
    }
    case Expression_Result_Type::VALUE:
    {
        // TODO: Check if this is comptime known, then we dont need a function pointer call
        auto type = info.options.type;
        if (type->type == Datatype_Type::FUNCTION) {
            callable.type = Callable_Type::FUNCTION_POINTER;
            callable.options.function_pointer_type = candidate.function_signature;
            candidate.function_signature = downcast<Datatype_Function>(type);
        }
        else {
            return optional_make_failure<Overload_Candidate>();
        }
        break;
    }
    case Expression_Result_Type::CONSTANT: {
        auto& constant = info.options.constant;
        if (constant.type->type != Datatype_Type::FUNCTION) {
            return optional_make_failure<Overload_Candidate>();
        }

        int function_index = (int)(*(i64*)constant.memory) - 1;
        if (function_index < 0 || function_index >= semantic_analyser.program->functions.size) {
            return optional_make_failure<Overload_Candidate>();
        }

        callable.type = Callable_Type::FUNCTION;
        callable.options.function = semantic_analyser.program->functions[function_index];
        candidate.function_signature = downcast<Datatype_Function>(constant.type);
        break;
    }
    case Expression_Result_Type::FUNCTION: {
        callable.type = Callable_Type::FUNCTION;
        callable.options.function = info.options.function;
        candidate.function_signature = info.options.function->signature;
        break;
    }
    case Expression_Result_Type::HARDCODED_FUNCTION:
    {
        callable.type = Callable_Type::HARDCODED;
        callable.options.hardcoded.type = info.options.hardcoded;
        callable.options.hardcoded.signature = hardcoded_type_to_signature(info.options.hardcoded);
        callable.parameter_count = callable.options.hardcoded.signature->parameters.size;
        candidate.function_signature = callable.options.hardcoded.signature;
        break;
    }
    default: panic("");
    }

    if (callable.parameter_count == -1) {
        assert(candidate.function_signature != 0, "");
        callable.parameter_count = candidate.function_signature->parameters.size;
        candidate.return_value_type = candidate.function_signature->return_type;
    }
    if (callable.only_named_arguments_after_index == -1) {
        callable.only_named_arguments_after_index = callable.parameter_count;
    }

    return optional_make_success(candidate);
}

Callable callable_create_struct_initializer(Datatype_Struct* struct_type)
{
    Callable result;
    result.type = Callable_Type::STRUCT_INITIALIZER;
    result.options.structure = struct_type;
    result.parameter_count = struct_type->members.size;
    result.only_named_arguments_after_index = result.parameter_count;
    return result;
}

Function_Parameter callable_get_parameter_info(const Callable& callable, int parameter_index)
{
    Function_Parameter result;
    assert(parameter_index >= 0 && parameter_index < callable.parameter_count, "");

    Datatype_Function* signature = 0;
    switch (callable.type)
    {
    case Callable_Type::POLYMORPHIC:
    {
        auto& base_info = *callable.options.polymorphic.base;

        // Check if it's an implicit parameter
        if (parameter_index >= callable.only_named_arguments_after_index) {
            const auto& implicit = base_info.implicit_parameter_infos[parameter_index - callable.only_named_arguments_after_index];
            auto poly_value = base_info.base_parameter_values[implicit.template_parameter->value_access_index];
            assert(poly_value.only_datatype_known, "Should be true for polymorphic base");

            result.has_default_value = true;
            result.default_value_opt = optional_make_failure<Upp_Constant>();
            result.name = optional_make_success(implicit.id);
            result.type = poly_value.options.type;
            if (result.type->contains_type_template) {
                result.type = compiler.type_system.predefined_types.error_type;
            }
            return result;
        }

        return base_info.parameters[parameter_index].infos;
    }
    case Callable_Type::STRUCT_INITIALIZER: {
        auto& member = callable.options.structure->members[parameter_index];
        result.has_default_value = false;
        result.default_value_opt.available = false;
        result.name = optional_make_success(member.id);
        result.type = member.type;
        return result;
    }
    case Callable_Type::FUNCTION: {
        signature = callable.options.function->signature;
        break;
    }
    case Callable_Type::HARDCODED: {
        signature = callable.options.hardcoded.signature;
        break;
    }
    case Callable_Type::FUNCTION_POINTER: {
        signature = callable.options.function_pointer_type;
        break;
    }
    default: panic("");
    }

    assert(signature, "");
    return signature->parameters[parameter_index];
}

// Checks for same identifier arguments, and named/unnamed argument order errors
bool arguments_check_for_naming_errors(Dynamic_Array<AST::Argument*>& arguments, int* out_unnamed_argument_count)
{
    // Detect named arguments errors
    bool argument_error_occured = false;
    bool named_argument_encountered = false;
    int unnamed_argument_count = 0;
    for (int i = 0; i < arguments.size; i++)
    {
        if (!arguments[i]->name.available) {
            unnamed_argument_count += 1;
            if (named_argument_encountered) {
                log_semantic_error("Unnamed arguments must not appear after named arguments!", upcast(arguments[i]));
                argument_error_occured = true;
            }
            continue;
        }

        // Check for duplicate arguments
        named_argument_encountered = true;
        String* a = arguments[i]->name.value;
        for (int j = 0; j < i; j++) {
            if (!arguments[j]->name.available) {
                continue;
            }
            String* b = arguments[j]->name.value;
            if (a == b) {
                log_semantic_error("Named argument was already specified!", upcast(arguments[i]), Parser::Section::IDENTIFIER);
                argument_error_occured = true;
            }
        }
    }

    if (out_unnamed_argument_count != 0) {
        *out_unnamed_argument_count = unnamed_argument_count;
    }
    return argument_error_occured;
}

// Returns true if successfull
bool arguments_match_to_parameters(
    Dynamic_Array<AST::Argument*>& arguments,
    Callable& callable,
    int unnamed_argument_count,
    bool log_errors_and_set_info,
    AST::Node* error_report_node = 0,
    Parser::Section error_report_section = Parser::Section::WHOLE)
{
    // Check if we supply to many arguments
    if (arguments.size > callable.parameter_count) {
        if (log_errors_and_set_info) {
            log_semantic_error("Too many arguments were supplied", error_report_node, error_report_section);
            log_error_info_argument_count(arguments.size, callable.parameter_count);
        }
        return false;
    }

    // Check if we are accessing parameters that should only be accessed with named arguments
    if (unnamed_argument_count > callable.only_named_arguments_after_index) {
        if (log_errors_and_set_info) {
            log_semantic_error("Too many arguments (Implicit parameters need to be accessed by name)", error_report_node, error_report_section);
        }
        return false;
    }

    // Set index for unnamed arguments
    if (log_errors_and_set_info) {
        for (int i = 0; i < unnamed_argument_count; i++) {
            auto argument = arguments[i];
            auto info = pass_get_node_info(semantic_analyser.current_workload->current_pass, argument, Info_Query::CREATE_IF_NULL);
            info->argument_index = i;
        }
    }

    // Match named arguments to parameters
    bool error_occured = false;
    int non_default_value_match_count = 0;
    for (int i = unnamed_argument_count; i < arguments.size; i++)
    {
        // Find parameter with same name
        auto& argument = arguments[i];
        assert(arguments[i]->name.available, "");
        auto id = arguments[i]->name.value;
        bool match_found = false;
        for (int j = 0; j < callable.parameter_count; j++)
        {
            Function_Parameter param = callable_get_parameter_info(callable, j);

            // Check if name matches
            if (!param.name.available) {
                continue;
            }
            if (param.name.value != id) {
                continue;
            }

            // Add match and check for further errors
            match_found = true;
            if (log_errors_and_set_info) {
                auto info = pass_get_node_info(semantic_analyser.current_workload->current_pass, argument, Info_Query::CREATE_IF_NULL);
                info->argument_index = j;
            }
            if (!param.has_default_value) {
                non_default_value_match_count += 1;
            }
            if (j < unnamed_argument_count) {
                error_occured = true;
                if (log_errors_and_set_info) {
                    log_semantic_error("Named argument overlaps with previously given unnamed argument", upcast(argument));
                }
            }
            break;
        }

        if (!match_found) {
            error_occured = true;
            if (log_errors_and_set_info) {
                log_semantic_error("No parameter matches this argument's name", upcast(argument), Parser::Section::IDENTIFIER);
            }
        }
    }

    // Check if all required (non-default) parameters were specified
    if (!error_occured)
    {
        int non_default_count = 0;
        for (int i = unnamed_argument_count; i < callable.parameter_count; i++) {
            if (!callable_get_parameter_info(callable, i).has_default_value) {
                non_default_count += 1;
            }
        }

        if (non_default_count != non_default_value_match_count) {
            error_occured = true;
            if (log_errors_and_set_info) {
                log_semantic_error("Not all required (non-default) parameters were specified", error_report_node, error_report_section);
            }
        }
    }

    return !error_occured;
}

void arguments_analyse_in_unknown_context(Dynamic_Array<AST::Argument*>& arguments) {
    for (int i = 0; i < arguments.size; i++) {
        auto arg = arguments[i];
        auto arg_info = pass_get_node_info(semantic_analyser.current_workload->current_pass, arg, Info_Query::CREATE_IF_NULL);
        if (arg_info->already_analysed) {
            auto value_opt = pass_get_node_info(semantic_analyser.current_workload->current_pass, arg->value, Info_Query::TRY_READ);
            assert(value_opt != 0, "Already analysed must mean that expression info for value exists!");
            continue;
        }
        semantic_analyser_analyse_expression_value(arg->value, expression_context_make_unknown());
    }
}



// The value of the constant must equal the value access index after matching
struct Matching_Constraint
{
    int value_access_index;
    Upp_Constant constant;
};

bool match_templated_type_internal(Datatype* polymorphic_type, Datatype* match_against, int& match_count,
    Hashset<Datatype*>& already_visited, Array<Polymorphic_Value>& implicit_parameter_values,
    Dynamic_Array<Matching_Constraint>& constraints)
{
    if (!polymorphic_type->contains_type_template) {
        return types_are_equal(polymorphic_type, match_against);
    }
    if (hashset_contains(&already_visited, polymorphic_type)) {
        return true;
    }
    hashset_insert_element(&already_visited, polymorphic_type);

    // Note: Not sure about this assert, but I think when evaluating poly arguments in correct order this shouldn't happen
    assert(!match_against->contains_type_template, "");

    auto match_polymorphic_type_to_constant = [&](Datatype_Template_Parameter* poly_type, Upp_Constant constant) -> void {
        auto& poly_value = implicit_parameter_values[poly_type->value_access_index];
        if (poly_type->is_reference || !poly_value.only_datatype_known) {
            // Note: The polymorphic value may already be set (!only_datatype_known) when the poly-parameter was already set explicitly, e.g.
            Matching_Constraint constraint;
            constraint.value_access_index = poly_type->value_access_index;
            constraint.constant = constant;
            dynamic_array_push_back(&constraints, constraint);
        }
        else {
            poly_value = polymorphic_value_make_constant(constant);
            match_count += 1;
        }
    };

    // Check if we found match
    if (polymorphic_type->type == Datatype_Type::TEMPLATE_PARAMETER)
    {
        auto poly_type = downcast<Datatype_Template_Parameter>(polymorphic_type);
        auto pool_result = constant_pool_add_constant(
            compiler.type_system.predefined_types.type_handle, array_create_static_as_bytes(&match_against->type_handle, 1));
        assert(pool_result.success, "Type handle must work as constant!");
        match_polymorphic_type_to_constant(poly_type, pool_result.constant);
        return true; // Don't match references
    }
    else if (polymorphic_type->type == Datatype_Type::STRUCT_INSTANCE_TEMPLATE)
    {
        // Check for errors
        auto struct_template = downcast<Datatype_Struct_Instance_Template>(polymorphic_type);
        if (match_against->type != Datatype_Type::STRUCT) {
            return false;
        }
        auto struct_type = downcast<Datatype_Struct>(match_against);
        if (struct_type->workload == 0) {
            return false;
        }
        if (struct_type->workload->polymorphic_type != Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
            return false;
        }
        auto& struct_instance = struct_type->workload->polymorphic.instance;
        if (struct_instance.parent != struct_template->struct_base) {
            return false;
        }
        auto& instance_parameter_values = struct_instance.parent->info.instances[struct_instance.instance_index].instance_parameter_values;

        // Match polymorphic values to arguments
        assert(struct_template->instance_values.size == instance_parameter_values.size, "");
        for (int i = 0; i < struct_instance.parent->info.parameter_nodes.size; i++) // Don't match implicit parameters, they should resolve by themselves
        {
            auto& template_to_match = struct_template->instance_values[i];
            assert(!instance_parameter_values[i].only_datatype_known, "Instances must only have values");
            Upp_Constant& match_to_value = instance_parameter_values[i].options.value;
            if (!template_to_match.only_datatype_known) // Instance values must match, e.g. Node(int, 5) must not match to Node($T, 6)
            {
                if (!upp_constant_is_equal(template_to_match.options.value, match_to_value)) {
                    return false;
                }
                continue;
            }

            Datatype* match_type = template_to_match.options.type;
            assert(match_type->contains_type_template, "Should be the case for struct_instance_templates");
            if (match_type->type == Datatype_Type::TEMPLATE_PARAMETER) {
                auto param_type = downcast<Datatype_Template_Parameter>(match_type);
                match_polymorphic_type_to_constant(param_type, match_to_value);
            }
            else {
                if (match_to_value.type->type == Datatype_Type::TYPE_HANDLE) {
                    Upp_Type_Handle handle_value = upp_constant_to_value<Upp_Type_Handle>(match_to_value);
                    if (handle_value.index >= (u32)compiler.type_system.types.size) {
                        return false;
                    }
                    Datatype* match_against = compiler.type_system.types[handle_value.index];
                    assert(!match_against->contains_type_template, "The instanciated type shouldn't be polymorphic");
                    bool success = match_templated_type_internal(
                        match_type, match_against, match_count, already_visited, implicit_parameter_values, constraints);
                    if (!success) {
                        return false;
                    }
                }
                else {
                    // Here we would try to match a type to a value, which cannot work
                    return false;
                }
            }
        }

        return true;
    }

    // Exit early if expected types don't match
    if (polymorphic_type->type != match_against->type) {
        return false;
    }

    switch (polymorphic_type->type)
    {
    case Datatype_Type::ARRAY: {
        auto other_array = downcast<Datatype_Array>(match_against);
        auto this_array = downcast<Datatype_Array>(polymorphic_type);

        if (!other_array->count_known) {
            return false; // Something has to be unknown/wrong for this to be true
        }

        // Check if we can match array size
        if (this_array->polymorphic_count_variable != 0) {
            // Match implicit parameter to element count
            auto pool_result = constant_pool_add_constant(
                upcast(compiler.type_system.predefined_types.i32_type), array_create_static_as_bytes(&other_array->element_count, 1));
            assert(pool_result.success, "I32 type must work as constant");
            match_polymorphic_type_to_constant(this_array->polymorphic_count_variable, pool_result.constant);
        }
        else {
            if (!this_array->count_known) { // We should know our own size
                return false;
            }
            if (this_array->element_count != other_array->element_count) {
                return false;
            }
        }

        // Continue matching element type
        return match_templated_type_internal(
            downcast<Datatype_Array>(polymorphic_type)->element_type, downcast<Datatype_Array>(match_against)->element_type,
            match_count, already_visited, implicit_parameter_values, constraints);
    }
    case Datatype_Type::SLICE:
        return match_templated_type_internal(
            downcast<Datatype_Slice>(polymorphic_type)->element_type, downcast<Datatype_Slice>(match_against)->element_type,
            match_count, already_visited, implicit_parameter_values, constraints);
    case Datatype_Type::POINTER:
        return match_templated_type_internal(
            downcast<Datatype_Pointer>(polymorphic_type)->points_to_type, downcast<Datatype_Pointer>(match_against)->points_to_type,
            match_count, already_visited, implicit_parameter_values, constraints);
    case Datatype_Type::STRUCT: {
        auto a = downcast<Datatype_Struct>(polymorphic_type);
        auto b = downcast<Datatype_Struct>(match_against);
        if (a->struct_type != b->struct_type || a->members.size != b->members.size) {
            return false;
        }
        for (int i = 0; i < a->members.size; i++) {
            if (!match_templated_type_internal(a->members[i].type, b->members[i].type, match_count, already_visited, implicit_parameter_values, constraints)) {
                return false;
            }
        }
        return true;
    }
    case Datatype_Type::FUNCTION: {
        auto a = downcast<Datatype_Function>(polymorphic_type);
        auto b = downcast<Datatype_Function>(match_against);
        if (a->parameters.size != b->parameters.size || a->return_type.available != b->return_type.available) {
            return false;
        }
        for (int i = 0; i < a->parameters.size; i++) {
            if (!match_templated_type_internal(a->parameters[i].type, b->parameters[i].type, match_count, already_visited, implicit_parameter_values, constraints)) {
                return false;
            }
        }
        if (a->return_type.available) {
            if (!match_templated_type_internal(a->return_type.value, b->return_type.value, match_count, already_visited, implicit_parameter_values, constraints)) {
                return false;
            }
        }
        return true;
    }
    case Datatype_Type::ENUM:
    case Datatype_Type::ERROR_TYPE:
    case Datatype_Type::VOID_POINTER:
    case Datatype_Type::PRIMITIVE:
    case Datatype_Type::TYPE_HANDLE: panic("Should be handled by previous code-path (E.g. non polymorphic!)");
    case Datatype_Type::STRUCT_INSTANCE_TEMPLATE:
    case Datatype_Type::TEMPLATE_PARAMETER: panic("Previous code path should have handled this!");
    default: panic("");
    }

    panic("");
    return true;
}

bool match_templated_type(Datatype* polymorphic_type, Datatype* match_against, int& match_count,
    Hashset<Datatype*>& already_visited, Array<Polymorphic_Value>& implicit_parameter_values,
    Dynamic_Array<Matching_Constraint>& constraints)
{
    hashset_reset(&already_visited);
    dynamic_array_reset(&constraints);
    bool success = match_templated_type_internal(polymorphic_type, match_against, match_count, already_visited, implicit_parameter_values, constraints);
    if (!success) {
        return false;
    }

    // Check if constraints match
    for (int i = 0; i < constraints.size; i++) {
        const auto& constraint = constraints[i];
        const auto& referenced_constant = implicit_parameter_values[constraint.value_access_index];
        assert(!referenced_constant.only_datatype_known, "Must have been set by now!");
        if (!upp_constant_is_equal(referenced_constant.options.value, constraint.constant)) {
            return false;
        }
    }

    return true;
}

const int MAX_POLYMORPHIC_INSTANCIATION_DEPTH = 10;

bool check_if_polymorphic_instanciation_limit_reached() {
    int instanciation_depth = semantic_analyser.current_workload->polymorphic_instanciation_depth + 1;
    return instanciation_depth > MAX_POLYMORPHIC_INSTANCIATION_DEPTH;
}

bool check_if_polymorphic_callable_contains_errors(Callable_Polymorphic_Info poly_callable)
{
    auto& analyser = semantic_analyser;

    if (poly_callable.is_function) {
        Function_Progress* base_progress = polymorphic_function_base_to_function_progress(poly_callable.options.polymorphic_function);
        if (base_progress->header_workload->base.real_error_count > 0) {
            return true;
        }
        if (base_progress->body_workload->base.is_finished && base_progress->body_workload->base.real_error_count > 0) {
            return true;
        }
    }
    else {
        auto polymorphic_struct = poly_callable.options.polymorphic_struct;
        if (polymorphic_struct->base.real_error_count > 0) {
            return true;
        }
        if (polymorphic_struct->body_workload->base.is_finished && polymorphic_struct->body_workload->base.real_error_count > 0) {
            return true;
        }
    }

    return false;
}

// On successfull instanciation (Not deduplicated) takes ownership of instance-values (If this is the case, instance_values.data is set to null)
int polymorphic_base_instanciate(
    Polymorphic_Base_Info& poly_base,
    Array<Polymorphic_Value>& instance_values,
    Array<Datatype*> instance_parameter_types,
    const Callable_Polymorphic_Info& poly_callable)
{
    const auto& return_type_index = poly_base.return_type_index;

    // Check if we already have an instance with the given values
    int instance_index = -1;
    {
        for (int i = 0; i < poly_base.instances.size; i++)
        {
            auto& test_instance = poly_base.instances[i];
            bool all_matching = true;
            for (int j = 0; j < instance_values.size; j++) {
                auto& value = instance_values[j];
                auto& compare_to = test_instance.instance_parameter_values[j];
                assert(!value.only_datatype_known && !compare_to.only_datatype_known, "");
                if (!upp_constant_is_equal(value.options.value, compare_to.options.value)) {
                    all_matching = false;
                    break;
                }
            }

            if (all_matching) {
                instance_index = i;
                break;
            }
        }
    }

    // Create new instance if necessary
    if (instance_index == -1)
    {
        Polymorphic_Instance_Info instance;
        instance.base_info = &poly_base;
        instance.instance_parameter_values = instance_values;
        instance.is_function_instance = poly_callable.is_function;
        instance_index = poly_base.instances.size;

        if (poly_callable.is_function)
        {
            // Create instance function signature
            Datatype_Function* instance_function_type = 0;
            {
                Datatype_Function unfinished = type_system_make_function_empty();
                for (int i = 0; i < poly_base.parameters.size; i++)
                {
                    auto& base_param = poly_base.parameters[i];
                    if (base_param.is_comptime || i == return_type_index) {
                        continue;
                    }

                    Function_Parameter parameter = function_parameter_make_empty();
                    if (base_param.depends_on.size > 0 || base_param.infos.type->contains_type_template) {
                        parameter.has_default_value = false;
                    }
                    else {
                        parameter.has_default_value = base_param.infos.has_default_value;
                        parameter.default_value_opt = base_param.infos.default_value_opt;
                    }
                    parameter.name = base_param.infos.name;
                    parameter.type = instance_parameter_types[i];
                    assert(!parameter.type->contains_type_template, "");
                    dynamic_array_push_back(&unfinished.parameters, parameter);
                }

                Datatype* return_type = 0;
                if (return_type_index != -1) {
                    return_type = instance_parameter_types[return_type_index];
                }

                instance_function_type = type_system_finish_function(unfinished, return_type);
            }

            auto poly_function = poly_callable.options.polymorphic_function;
            auto poly_progress = polymorphic_function_base_to_function_progress(poly_function);

            // Create new instance progress
            auto instance_progress = function_progress_create_with_modtree_function(
                0, poly_progress->header_workload->function_node, instance_function_type, poly_progress);
            instance_progress->type = Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
            instance_progress->polymorphic.instance_base = poly_function;

            // Update body workload to use instance values
            instance_progress->body_workload->base.polymorphic_instanciation_depth += 1;
            instance_progress->body_workload->base.polymorphic_values = instance_values;
            instance_progress->body_workload->base.current_symbol_table = poly_base.symbol_table;

            // Store reference in instance_info
            instance.options.function_instance = instance_progress;
        }
        else
        {
            auto poly_struct = poly_callable.options.polymorphic_struct;

            // Create new struct instance
            auto body_workload = workload_structure_create(poly_struct->body_workload->struct_node, 0, true, Symbol_Access_Level::POLYMORPHIC);
            body_workload->struct_type->name = poly_struct->body_workload->struct_type->name;
            body_workload->polymorphic_type = Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
            body_workload->polymorphic.instance.instance_index = instance_index;
            body_workload->polymorphic.instance.parent = poly_struct;

            body_workload->base.polymorphic_instanciation_depth += 1;
            body_workload->base.polymorphic_values = instance_values;;
            body_workload->base.current_symbol_table = poly_base.symbol_table;

            analysis_workload_add_dependency_internal(upcast(body_workload), upcast(poly_struct->body_workload));

            instance.options.struct_instance = body_workload;
        }

        dynamic_array_push_back(&poly_base.instances, instance);
        instance_values.data = 0;
        instance_values.size = 0;
    }

    return instance_index;
}




#define SET_ACTIVE_EXPR_INFO(new_info)\
    Expression_Info* _backup_info = semantic_analyser.current_workload->current_expression; \
    semantic_analyser.current_workload->current_expression = new_info; \
    SCOPE_EXIT(semantic_analyser.current_workload->current_expression = _backup_info);

// May return 0 if this is not the case
Datatype_Template_Parameter* expression_is_unset_template_parameter(AST::Expression* expr)
{
    if (expr->type != AST::Expression_Type::TEMPLATE_PARAMETER) {
        return 0;
    }

    auto polymorphic_type_opt = hashtable_find_element(&semantic_analyser.valid_template_parameters, expr);
    if (polymorphic_type_opt == 0) {
        log_semantic_error("Implicit polymorphic parameter only valid in function header!", expr);
        return 0;
    }
    auto poly_type = *polymorphic_type_opt;
    assert(!poly_type->is_reference, "We can never be the reference if we are at the symbol, e.g. $T is not a symbol-read like T");

    auto& poly_values = semantic_analyser.current_workload->polymorphic_values;
    assert(poly_values.data != 0, "");
    const auto& poly_value = poly_values[poly_type->value_access_index];
    if (poly_value.only_datatype_known) {
        return poly_type; // If the constant is not set, return the type for matching
    }
    else {
        return 0; // If the constant is known, then just analyse as a value
    }
}

Expression_Info* semantic_analyser_analyse_expression_internal(AST::Expression* expr, Expression_Context context)
{
    auto& analyser = semantic_analyser;
    auto type_system = &compiler.type_system;
    auto& types = type_system->predefined_types;

    // Initialize expression info
    auto info = get_info(expr, true);
    SET_ACTIVE_EXPR_INFO(info);
    *info = expression_info_make_empty(context);

#define EXIT_VALUE(val) expression_info_set_value(info, val); return info;
#define EXIT_TYPE(type) expression_info_set_type(info, type); return info;
#define EXIT_ERROR(type) expression_info_set_error(info, type); return info;
#define EXIT_HARDCODED(hardcoded) expression_info_set_hardcoded(info, hardcoded); return info;
#define EXIT_FUNCTION(function) expression_info_set_function(info, function); return info;

    switch (expr->type)
    {
    case AST::Expression_Type::ERROR_EXPR: {
        semantic_analyser_set_error_flag(false);// Error due to parsing, dont log error message because we already have parse error messages
        EXIT_ERROR(types.error_type);
    }
    case AST::Expression_Type::FUNCTION_CALL:
    {
        auto& call = expr->options.call;
        auto& arguments = call.arguments;
        info->specifics.function_call_signature = 0;

        // Initialize all argument infos
        for (int i = 0; i < call.arguments.size; i++) {
            auto argument = get_info(call.arguments[i], true);
            argument->is_polymorphic = false;
            argument->argument_index = -1; // Initialize as invalid
            argument->already_analysed = false;
            argument->context_application_missing = false;
        }

        // Check for general argument errors
        int unnamed_argument_count = 0;
        if (arguments_check_for_naming_errors(call.arguments, &unnamed_argument_count)) {
            semantic_analyser_analyse_expression_any(call.expr, expression_context_make_unknown());
            arguments_analyse_in_unknown_context(arguments);
            EXIT_ERROR(types.error_type);
        }

        // Find all overload candidates
        Dynamic_Array<Overload_Candidate> candidates = dynamic_array_create_empty<Overload_Candidate>(1);
        SCOPE_EXIT(dynamic_array_destroy(&candidates));
        if (call.expr->type == AST::Expression_Type::PATH_LOOKUP)
        {
            // Find all overloads
            Dynamic_Array<Symbol*> symbols = dynamic_array_create_empty<Symbol*>(1);
            SCOPE_EXIT(dynamic_array_destroy(&symbols));

            path_lookup_resolve(call.expr->options.path_lookup, symbols);
            if (symbols.size == 0) {
                log_semantic_error("Could not resolve Symbol (No definition found)", upcast(call.expr->options.path_lookup));
                path_lookup_set_info_to_error_symbol(call.expr->options.path_lookup, semantic_analyser.current_workload);
            }

            // Add symbols as overload candidates
            bool encountered_unknown = false;
            for (int i = 0; i < symbols.size; i++)
            {
                auto& symbol = symbols[i];
                if (symbol->type == Symbol_Type::MODULE) {
                    continue;
                }
                auto info = analyse_symbol_as_expression(symbol, expression_context_make_auto_dereference(), call.expr->options.path_lookup->last);
                auto callable_opt = overload_candidate_try_create_from_expression_info(info, symbol);
                if (callable_opt.available) {
                    dynamic_array_push_back(&candidates, callable_opt.value);
                }
                else if (datatype_is_unknown(info.cast_info.type_afterwards)) {
                    encountered_unknown = true;
                }
            }

            // Check success
            if (encountered_unknown) {
                semantic_analyser_set_error_flag(true);
                arguments_analyse_in_unknown_context(arguments);
                EXIT_ERROR(types.error_type);
            }
            if (symbols.size == 1 && candidates.size == 0) {
                log_semantic_error("Symbol is not callable!", upcast(call.expr->options.path_lookup->last));
                log_error_info_symbol(symbols[0]);
                arguments_analyse_in_unknown_context(arguments);
                EXIT_ERROR(types.error_type);
            }
        }
        else
        {
            auto* info = semantic_analyser_analyse_expression_any(call.expr, expression_context_make_auto_dereference());
            auto callable_opt = overload_candidate_try_create_from_expression_info(*info);
            if (callable_opt.available) {
                dynamic_array_push_back(&candidates, callable_opt.value);
            }
            else {
                if (!datatype_is_unknown(info->cast_info.type_afterwards)) {
                    log_semantic_error("Expression is not callable!", upcast(call.expr->options.path_lookup->last));
                    log_error_info_expression_result_type(info->result_type);
                }
                else {
                    semantic_analyser_set_error_flag(true);
                }
                arguments_analyse_in_unknown_context(arguments);
                EXIT_ERROR(types.error_type);
            }
        }

        // Do Overload resolution
        if (candidates.size != 1)
        {
            // Check if we can do overload resultion
            if (candidates.size > 1) {
                for (int i = 0; i < candidates.size; i++) {
                    auto& candidate = candidates[i];
                    if (candidate.callable.type == Callable_Type::POLYMORPHIC) {
                        log_semantic_error("Overload-resolution with polymorphic types currently not implemented", upcast(call.expr->options.path_lookup->last));
                        EXIT_ERROR(types.error_type);
                    }
                }
            }

            // Disambiguate overloads by argument names/count
            if (candidates.size > 1)
            {
                auto& arguments = call.arguments;
                const int named_argument_count = arguments.size - unnamed_argument_count;
                for (int i = 0; i < candidates.size; i++)
                {
                    auto& candidate = candidates[i];
                    if (!arguments_match_to_parameters(arguments, candidate.callable, unnamed_argument_count, false)) {
                        dynamic_array_swap_remove(&candidates, i);
                        i -= 1;
                    }
                }
            }

            // Disambiguate overloads by argument types
            if (candidates.size > 1)
            {
                auto remove_candidates_based_on_better_type_match = [&](Dynamic_Array<Overload_Candidate>& candidates, Datatype* expected_type)
                {
                    bool matching_candidate_exists = false;
                    bool castable_candidate_exists = false;
                    for (int j = 0; j < candidates.size; j++)
                    {
                        auto& candidate = candidates[j];
                        candidate.overloading_arg_can_be_cast = false;
                        candidate.overloading_arg_matches_type = false;
                        if (types_are_equal(candidate.overloading_param_type, expected_type)) {
                            candidate.overloading_arg_matches_type = true;
                            matching_candidate_exists = true;
                        }
                        else {
                            Expression_Cast_Info post_op = expression_context_apply(
                                expected_type, expression_context_make_specific_type(candidate.overloading_param_type), false);
                            if (post_op.cast_type != Cast_Type::INVALID) {
                                candidate.overloading_arg_can_be_cast = true;
                                castable_candidate_exists = true;
                            }
                        }
                    }

                    // Remove candidates that aren't as fit as other candidates
                    for (int j = 0; j < candidates.size; j++)
                    {
                        auto& candidate = candidates[j];
                        bool remove = false;
                        if (candidate.overloading_arg_matches_type) {
                            continue;
                        }
                        else if (candidate.overloading_arg_can_be_cast) {
                            if (matching_candidate_exists) {
                                remove = true;
                            }
                        }
                        else {
                            if (matching_candidate_exists || castable_candidate_exists) {
                                remove = true;
                            }
                        }

                        if (remove) {
                            dynamic_array_swap_remove(&candidates, j);
                            j -= 1;
                        }
                    }
                };

                // For the remaining functions, check which argument types are different, and remove based on those
                for (int i = 0; i < arguments.size && candidates.size > 1; i++)
                {
                    auto arg = arguments[i];
                    auto arg_info = get_info(arg);

                    // Find parameter types of all candidates
                    bool parameter_types_are_different = false;
                    for (int j = 0; j < candidates.size; j++)
                    {
                        auto& candidate = candidates[j];
                        Datatype* param_type = 0;
                        if (arg->name.available) {
                            // Find named argument
                            for (int k = 0; k < candidate.callable.parameter_count; k++) {
                                auto& param = callable_get_parameter_info(candidate.callable, k);
                                if (param.name.available && param.name.value == arg->name.value) {
                                    param_type = param.type;
                                    break;
                                }
                            }
                        }
                        else {
                            param_type = callable_get_parameter_info(candidate.callable, i).type;
                        }
                        assert(param_type != 0, "Must have been found at this point, arguments were checked before!");

                        candidate.overloading_param_type = param_type;
                        if (j != 0 && candidates[0].overloading_param_type != param_type) {
                            parameter_types_are_different = true;
                        }
                    }

                    // Check if we can differentiate the call based on this parameter's type
                    if (!parameter_types_are_different) {
                        continue;
                    }

                    // For each candidate figure out if argument does/doesn't match or can be cast
                    auto argument_type = semantic_analyser_analyse_expression_value(arg->value, expression_context_make_unknown());
                    arg_info->already_analysed = true;
                    arg_info->context_application_missing = true;
                    remove_candidates_based_on_better_type_match(candidates, argument_type);
                }

                // If we still have candidiates, try to differentiate based on return type
                if (candidates.size > 1 && context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
                {
                    auto expected_return_type = context.expected_type.type;
                    bool can_differentiate_based_on_return_type = false;
                    Datatype* last_return_type = 0;
                    for (int i = 0; i < candidates.size; i++) {
                        auto& candidate = candidates[i];
                        if (!candidate.return_value_type.available) {
                            candidate.overloading_param_type = type_system->predefined_types.error_type;
                            continue;
                        }

                        candidate.overloading_param_type = candidate.return_value_type.value;
                        if (!types_are_equal(last_return_type, candidate.overloading_param_type)) {
                            can_differentiate_based_on_return_type = true;
                        }
                        if (last_return_type == 0) {
                            last_return_type = candidate.overloading_param_type;
                        }
                    }

                    if (can_differentiate_based_on_return_type) {
                        remove_candidates_based_on_better_type_match(candidates, expected_return_type);
                    }
                }
            }

            // Report error if overloads couldn't be disambiguated
            if (candidates.size != 1)
            {
                // Log errors
                if (candidates.size > 1) {
                    log_semantic_error("Could not disambiguate between function overloads", call.expr);
                    for (int i = 0; i < candidates.size; i++) {
                        if (candidates[i].function_signature != 0) {
                            log_error_info_function_type(candidates[i].function_signature);
                        }
                    }
                }
                else if (candidates.size == 0) {
                    log_semantic_error("None of the function overloads are valid", call.expr);
                    for (int i = 0; i < candidates.size; i++) {
                        if (candidates[i].function_signature != 0) {
                            log_error_info_function_type(candidates[i].function_signature);
                        }
                    }
                }

                // Analyse remaining arguments as something else
                arguments_analyse_in_unknown_context(arguments);
                EXIT_ERROR(types.error_type);
            }
        }

        // Set expression/Symbol read info
        auto candidate = candidates[0];
        auto call_expr_info = pass_get_node_info(semantic_analyser.current_workload->current_pass, call.expr, Info_Query::CREATE_IF_NULL);
        if (call.expr->type == AST::Expression_Type::PATH_LOOKUP) {
            assert(candidate.symbol != 0, "Must have been set before!");
            path_lookup_set_result_symbol(call.expr->options.path_lookup, candidate.symbol);
            *call_expr_info = candidate.expression_info;
        }

        // Do argument to parameter mapping (May have been done before for overload disambiguation)
        bool success = arguments_match_to_parameters(arguments, candidate.callable, unnamed_argument_count, true, upcast(expr), Parser::Section::ENCLOSURE);
        if (!success) {
            arguments_analyse_in_unknown_context(arguments);
            if (candidate.return_value_type.available) {
                EXIT_ERROR(candidate.return_value_type.value);
            }
            else {
                expression_info_set_no_value(info);
                return info;
            }
        }

        // Further callable type dependend processing (polymorphic instanciation or hardcoded function handling)
        switch (candidate.callable.type)
        {
        case Callable_Type::FUNCTION:
        case Callable_Type::FUNCTION_POINTER: {
            break;
        }
        case Callable_Type::HARDCODED:
        {
            if (candidate.callable.options.hardcoded.type == Hardcoded_Type::TYPE_OF)
            {
                auto& arg = call.arguments[0];
                auto arg_result = semantic_analyser_analyse_expression_any(arg->value, expression_context_make_unknown());
                switch (arg_result->result_type)
                {
                case Expression_Result_Type::VALUE: {
                    EXIT_TYPE(arg_result->options.type);
                }
                case Expression_Result_Type::HARDCODED_FUNCTION: {
                    log_semantic_error("Cannot use type_of on hardcoded functions!", arg->value);
                    EXIT_ERROR(types.error_type);
                }
                case Expression_Result_Type::NOTHING: {
                    log_semantic_error("Expected value", arg->value);
                    EXIT_ERROR(types.error_type);
                }
                case Expression_Result_Type::CONSTANT: {
                    EXIT_TYPE(arg_result->options.constant.type);
                }
                case Expression_Result_Type::FUNCTION: {
                    EXIT_TYPE(upcast(arg_result->options.function->signature));
                }
                case Expression_Result_Type::TYPE: {
                    EXIT_TYPE(upcast(types.type_handle));
                }
                case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
                    log_semantic_error("Type of cannot handle polymorphic functions", arg->value);
                    log_error_info_expression_result_type(arg_result->result_type);
                    EXIT_ERROR(types.error_type);
                }
                case Expression_Result_Type::POLYMORPHIC_STRUCT: {
                    log_semantic_error("Cannot use type_of on polymorphic struct", arg->value);
                    log_error_info_expression_result_type(arg_result->result_type);
                    EXIT_ERROR(types.error_type);
                }
                default: panic("");
                }

                panic("");
                EXIT_ERROR(arg_result->options.type);
            }
            break;
        }
        case Callable_Type::POLYMORPHIC:
        {
            auto& poly_callable = candidate.callable.options.polymorphic;
            auto& poly_base = *candidate.callable.options.polymorphic.base;
            auto& parameter_nodes = poly_base.parameter_nodes;
            const int return_type_index = poly_base.return_type_index;

            if (check_if_polymorphic_instanciation_limit_reached()) {
                log_semantic_error("Polymorphic instanciation limit reached!", call.expr, Parser::Section::FIRST_TOKEN);
                arguments_analyse_in_unknown_context(arguments);
                EXIT_ERROR(types.error_type);
            }
            if (check_if_polymorphic_callable_contains_errors(poly_callable)) {
                EXIT_ERROR(types.error_type);
            }

            // Prepare instanciation data
            bool success = true;
            bool log_error_on_failure = false;
            bool create_struct_template = false;

            Array<Polymorphic_Value> instance_values = array_create_empty<Polymorphic_Value>(poly_base.base_parameter_values.size);
            SCOPE_EXIT(if (instance_values.data != 0) { array_destroy(&instance_values); });
            for (int i = 0; i < instance_values.size; i++) {
                assert(poly_base.base_parameter_values[i].only_datatype_known, "");
                instance_values[i] = poly_base.base_parameter_values[i];
            }

            // Initialize instance parameter types with "normal" param-types of base (Or error if polymorphic)
            Array<Datatype*> instance_parameter_types = array_create_empty<Datatype*>(poly_base.parameters.size);
            for (int i = 0; i < instance_parameter_types.size; i++) {
                auto param_type = poly_base.parameters[i].infos.type;
                if (param_type->contains_type_template) {
                    instance_parameter_types[i] = types.error_type;
                }
                else {
                    instance_parameter_types[i] = param_type;
                }
            }
            SCOPE_EXIT(array_destroy(&instance_parameter_types));

            Analysis_Pass* comptime_evaluation_pass = 0; // Is allocated on demand because in many cases we won't need to reanalyse header parameters
            Dynamic_Array<Matching_Constraint> matching_constraints = dynamic_array_create_empty<Matching_Constraint>(1);
            SCOPE_EXIT(dynamic_array_destroy(&matching_constraints));
            Hashset<Datatype*> matching_helper = hashset_create_pointer_empty<Datatype*>(1);
            SCOPE_EXIT(hashset_destroy(&matching_helper));
            int matched_implicit_parameter_count = 0;

            Array<bool> reanalyse_param_flags = array_create_empty<bool>(poly_base.parameters.size);
            SCOPE_EXIT(array_destroy(&reanalyse_param_flags));
            for (int i = 0; i < reanalyse_param_flags.size; i++) {
                reanalyse_param_flags[i] = false;
            }

            // Check if implicit parameters are set via named arguments
            // e.g. foo :: (a: $T)
            //      foo(15, T=int)
            if (arguments.size > parameter_nodes.size)
            {
                for (int i = 0; i < arguments.size; i++) {
                    auto arg_node = arguments[i];
                    auto arg_info = get_info(arg_node);
                    if (arg_info->argument_index < parameter_nodes.size) {
                        continue;
                    }

                    const auto& implicit = poly_base.implicit_parameter_infos[parameter_nodes.size - arg_info->argument_index];

                    Datatype* value_type = semantic_analyser_analyse_expression_value(arg_node->value, expression_context_make_unknown(false));
                    arg_info->already_analysed = true;
                    arg_info->is_polymorphic = true;

                    if (value_type->contains_type_template) {
                        success = false;
                        create_struct_template = true;
                        instance_values[implicit.template_parameter->value_access_index] = polymorphic_value_make_type(value_type);
                        matched_implicit_parameter_count += 1;
                        continue;
                    }

                    bool was_unavailable = false;
                    auto comptime_result = expression_calculate_comptime_value(
                        arg_node->value, "Parameter is polymorphic, but argument cannot be evaluated at comptime", &was_unavailable);
                    if (comptime_result.available) {
                        instance_values[implicit.template_parameter->value_access_index] = polymorphic_value_make_constant(comptime_result.value);
                        matched_implicit_parameter_count += 1;
                        reanalyse_param_flags[implicit.defined_in_parameter_index] = true;
                    }
                    else {
                        success = false;
                        if (!was_unavailable) {
                            log_error_on_failure = true;
                        }
                    }
                }
            }

            // Analyse arguments (+return type) in correct order
            for (int i = 0; i < poly_base.parameter_analysis_order.size; i++)
            {
                int parameter_index = poly_base.parameter_analysis_order[i];
                auto& base_parameter = poly_base.parameters[parameter_index];

                // Skip if we can just use base-analysis result
                if (!base_parameter.is_comptime && base_parameter.depends_on.size == 0 && !base_parameter.infos.type->contains_type_template) {
                    continue;
                }

                // Handle polymorphic return type
                if (parameter_index == return_type_index)
                {
                    Datatype* return_type = base_parameter.infos.type;
                    if (base_parameter.depends_on.size > 0 || reanalyse_param_flags[parameter_index])
                    {
                        auto workload = semantic_analyser.current_workload;
                        if (comptime_evaluation_pass == 0) {
                            comptime_evaluation_pass = analysis_pass_allocate(workload, upcast(expr));
                        }
                        RESTORE_ON_SCOPE_EXIT(workload->current_pass, comptime_evaluation_pass);
                        RESTORE_ON_SCOPE_EXIT(workload->polymorphic_values, instance_values);
                        RESTORE_ON_SCOPE_EXIT(workload->current_symbol_table, poly_base.symbol_table);
                        RESTORE_ON_SCOPE_EXIT(workload->ignore_unknown_errors, true);
                        return_type = semantic_analyser_analyse_expression_type(poly_base.return_type_node);
                    }

                    // If return type is polymorphic, then try to match with expected type
                    if (return_type->contains_type_template)
                    {
                        // If all required types contain 
                        // x := Dynamic_Array~create(T=int)
                        // x: Dynamic_Array(int) = Dynamic_Array~create()

                        if (context.type != Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
                            log_semantic_error("Call of poly-function requires the return type to be known from context", expr, Parser::Section::ENCLOSURE);
                            success = false;
                            continue;
                        }

                        // Match types
                        bool match_success = match_templated_type(
                            return_type, context.expected_type.type, matched_implicit_parameter_count,
                            matching_helper, instance_values, matching_constraints);
                        if (!match_success) {
                            log_semantic_error("Could not match return-type with context-type", expr, Parser::Section::ENCLOSURE);
                            log_error_info_given_type(context.expected_type.type);
                            log_error_info_expected_type(return_type);
                            success = false;
                        }
                        return_type = context.expected_type.type;
                    }

                    instance_parameter_types[parameter_index] = return_type;
                    continue;
                }

                // Handle normal parameters
                auto& parameter_node = parameter_nodes[parameter_index];
                assert(parameter_node->is_comptime == base_parameter.is_comptime || !poly_callable.is_function, "");

                // Find argument node
                AST::Argument* argument_node = 0;
                if (parameter_index < unnamed_argument_count) {
                    argument_node = arguments[parameter_index];
                }
                else {
                    for (int j = unnamed_argument_count; j < arguments.size; j++) {
                        assert(arguments[j]->name.available, "");
                        if (arguments[j]->name.value == parameter_node->name) {
                            argument_node = arguments[j];
                            break;
                        }
                    }
                    assert(argument_node != 0, "");
                }

                auto arg_info = get_info(argument_node);
                arg_info->is_polymorphic = parameter_node->is_comptime;
                arg_info->already_analysed = false;
                arg_info->context_application_missing = false;

                // Get instance parameter-type
                Datatype* parameter_type = 0;
                if ((base_parameter.depends_on.size > 0 || reanalyse_param_flags[parameter_index]))
                {
                    // Re-analyse base-header to get valid poly-argument type (Since this depends on other parameters)
                    //      e.g. foo :: ($T: Type_Handle, a: T)
                    if (!create_struct_template) {
                        auto workload = semantic_analyser.current_workload;
                        if (comptime_evaluation_pass == 0) {
                            comptime_evaluation_pass = analysis_pass_allocate(workload, upcast(expr));
                        }
                        RESTORE_ON_SCOPE_EXIT(workload->current_pass, comptime_evaluation_pass);
                        RESTORE_ON_SCOPE_EXIT(workload->polymorphic_values, instance_values);
                        RESTORE_ON_SCOPE_EXIT(workload->current_symbol_table, poly_base.symbol_table);
                        RESTORE_ON_SCOPE_EXIT(workload->ignore_unknown_errors, true);
                        parameter_type = semantic_analyser_analyse_expression_type(parameter_node->type);

                        if (datatype_is_unknown(parameter_type) && !parameter_type->contains_type_template) {
                            success = false;
                        }
                    }
                    else {
                        parameter_type = types.error_type;
                    }
                }
                else {
                    parameter_type = base_parameter.infos.type;
                    assert(!datatype_is_unknown(parameter_type) || parameter_type->contains_type_template, "");
                }

                // If this is a comptime argument, calculate comptime value
                if (base_parameter.is_comptime)
                {
                    auto template_type = expression_is_unset_template_parameter(argument_node->value);
                    if (template_type) {
                        create_struct_template = true;
                        instance_values[parameter_index] = polymorphic_value_make_type(upcast(template_type));
                        matched_implicit_parameter_count += 1;
                        continue;
                    }

                    // Analyse Argument and try to get comptime value
                    Datatype* value_type = 0;
                    if (parameter_type->type != Datatype_Type::ERROR_TYPE)
                    {
                        if (types_are_equal(parameter_type, upcast(types.type_handle))) {
                            // In this case we are looking for a comptime value for a type, so well call analyse type
                            value_type = semantic_analyser_analyse_expression_type(argument_node->value);
                        }
                        else {
                            Expression_Context context = expression_context_make_specific_type(parameter_type);
                            if (parameter_type->contains_type_template) {
                                // If parameter type has implicit polymorphic symbols, then we don't want any casts,
                                //    so that we can later match the result-type with the implicit symbols
                                //    e.g. a :: make($x: $T)
                                //         make(15+32 == 12) --> The expression should be analysed without context, and T should be matched to that type
                                context = expression_context_make_unknown();
                            }
                            value_type = semantic_analyser_analyse_expression_value(argument_node->value, context);
                        }
                    }
                    else {
                        assert(create_struct_template || !success, "Only struct template should set type to error-type or an error occured");
                        value_type = semantic_analyser_analyse_expression_value(argument_node->value, expression_context_make_unknown());
                    }
                    arg_info->already_analysed = true;

                    // Check if we need to create a struct template
                    if (value_type->contains_type_template)
                    {
                        assert(!poly_callable.is_function, "");
                        instance_values[parameter_index] = polymorphic_value_make_type(value_type);
                        create_struct_template = true;
                        continue;
                    }

                    // Try to calculate comptime value
                    bool was_unavailable = false;
                    auto comptime_result = expression_calculate_comptime_value(
                        argument_node->value, "Parameter is polymorphic, but argument cannot be evaluated at comptime", &was_unavailable);
                    if (comptime_result.available) {
                        auto constant = comptime_result.value;
                        // Check for struct_template
                        if (constant.type->type == Datatype_Type::TYPE_HANDLE)
                        {
                            Upp_Type_Handle handle = upp_constant_to_value<Upp_Type_Handle>(constant);
                            if (handle.index >= (u32)type_system->types.size) {
                                success = false;
                                log_semantic_error("Invalid constant type handle index", upcast(argument_node));
                                arguments_analyse_in_unknown_context(arguments);
                                EXIT_ERROR(types.error_type);
                            }

                            Datatype* constant_type = type_system->types[handle.index];
                            if (constant_type->contains_type_template) {
                                assert(!poly_callable.is_function, "");
                                instance_values[parameter_index] = polymorphic_value_make_type(constant_type);
                                create_struct_template = true;
                                continue;
                            }
                        }

                        instance_values[base_parameter.options.value_access_index] = polymorphic_value_make_constant(constant);
                    }
                    else {
                        success = false;
                        instance_values[base_parameter.options.value_access_index] = polymorphic_value_make_type(parameter_type);
                        if (!was_unavailable) {
                            log_error_on_failure = true;
                        }
                    }
                }

                // Match implicit parameters
                if (parameter_type->contains_type_template && !create_struct_template)
                {
                    // Analyse argument if this has not been done before
                    Datatype* argument_type;
                    if (arg_info->already_analysed) {
                        argument_type = get_info(argument_node->value)->cast_info.type_afterwards;
                    }
                    else {
                        argument_type = semantic_analyser_analyse_expression_value(argument_node->value, expression_context_make_unknown());
                        arg_info->already_analysed = true;
                    }
                    arg_info->context_application_missing = true; // Note: If matching succeeds, the types should be equal, this just adds another check if that's true

                    // Match types
                    bool match_success = match_templated_type(
                        parameter_type, argument_type, matched_implicit_parameter_count,
                        matching_helper, instance_values, matching_constraints);
                    if (!match_success) {
                        log_semantic_error("Could not match argument with implicit-polymorphic symbols!", upcast(argument_node));
                        log_error_info_given_type(argument_type);
                        log_error_info_expected_type(parameter_type);
                        success = false;
                    }
                    parameter_type = argument_type;
                }

                if (success) {
                    instance_parameter_types[parameter_index] = parameter_type;
                }
            }

            // Create struct template if requested
            if (create_struct_template && success)
            {
                assert(!poly_callable.is_function, "");
                auto poly_struct = poly_callable.options.polymorphic_struct;

                auto result_template_type = type_system_make_struct_instance_template(poly_struct, instance_values);
                instance_values.data = 0; // Since we transfer ownership we should signal that we don't want to delete this
                EXIT_TYPE(upcast(result_template_type));
            }

            // Return if there were errors/values not available
            if (!success) {
                if (log_error_on_failure) {
                    log_semantic_error("Some values couldn't be calculated at comptime!", AST::upcast(expr), Parser::Section::ENCLOSURE);
                }
                arguments_analyse_in_unknown_context(arguments);
                EXIT_ERROR(types.error_type);
            }
            else {
                assert(matched_implicit_parameter_count == poly_base.implicit_parameter_infos.size, "Should be the case if everything worked");
            }

            // Create instance
            int instance_index = polymorphic_base_instanciate(poly_base, instance_values, instance_parameter_types, poly_callable);

            // Exit now if we are instanciating struct
            if (!poly_callable.is_function) {
                EXIT_TYPE(upcast(poly_base.instances[instance_index].options.struct_instance->struct_type));
            }

            // Re-assign argument to parameter mapping with new instance function-signature (For code-gen)
            for (int i = 0; i < arguments.size; i++) {
                auto arg = arguments[i];
                auto info = get_info(arg);
                if (info->is_polymorphic) {
                    continue;
                }
                info->argument_index = poly_base.parameters[info->argument_index].options.index_in_non_polymorphic_signature;
            }

            // Store instanciation info in expression_info
            assert(call_expr_info->result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION && poly_callable.is_function, "");
            call_expr_info->options.polymorphic_function.base = poly_callable.options.polymorphic_function;
            ModTree_Function* function = poly_base.instances[instance_index].options.function_instance->function;
            call_expr_info->options.polymorphic_function.instance_fn = function;
            candidate.function_signature = function->signature;
            semantic_analyser_register_function_call(function);
            break;
        }
        case Callable_Type::STRUCT_INITIALIZER: {
            panic("Shouldn't happen in this function!\n");
        }
        default: panic("");
        }

        // Store final function signature for code generation
        call_expr_info->specifics.function_call_signature = candidate.function_signature;

        // Analyse arguments
        auto function_signature = candidate.function_signature;
        assert(function_signature != 0, "");
        auto& params = function_signature->parameters;
        for (int i = 0; i < arguments.size; i++)
        {
            auto arg = arguments[i];
            auto arg_info = get_info(arg);

            // Check if already handled
            if (arg_info->is_polymorphic) {
                continue;
            }
            if (arg_info->context_application_missing) {
                auto value_info = get_info(arg->value);
                auto arg_context = expression_context_make_specific_type(params[arg_info->argument_index].type);
                value_info->cast_info = expression_context_apply(
                    expression_info_get_type(value_info, true), arg_context, true, arg->value);
                continue;
            }
            if (arg_info->already_analysed) { // Inclues both already analysed during overload disambiguation and polymorphic_function values
                continue;
            }

            // Analyse argument
            semantic_analyser_analyse_expression_value(arg->value, expression_context_make_specific_type(params[arg_info->argument_index].type));
        }
        if (function_signature->return_type.available) {
            EXIT_VALUE(function_signature->return_type.value);
        }
        else {
            expression_info_set_no_value(info);
            return info;
        }

        panic("Not a valid code path, the if before should terminate!");
        EXIT_ERROR(types.error_type);
    }
    case AST::Expression_Type::PATH_LOOKUP:
    {
        auto path = expr->options.path_lookup;

        // Resolve symbol
        Symbol* symbol = path_lookup_resolve_to_single_symbol(path, false);
        assert(symbol != 0, "In error cases this should be set to error, never 0!");

        // Analyse symbol
        *info = analyse_symbol_as_expression(symbol, context, path->last);
        return info;
    }
    case AST::Expression_Type::TEMPLATE_PARAMETER:
    {
        auto polymorphic_type_opt = hashtable_find_element(&semantic_analyser.valid_template_parameters, expr);
        if (polymorphic_type_opt == 0) {
            log_semantic_error("Implicit polymorphic parameter only valid in function header!", expr);
            EXIT_ERROR(types.error_type);
        }
        auto poly_type = *polymorphic_type_opt;
        assert(!poly_type->is_reference, "We can never be the reference if we are at the symbol, e.g. $T is not a symbol-read like T");

        // If value is already set, return the value
        auto& poly_values = analyser.current_workload->polymorphic_values;
        assert(poly_values.data != 0, "We should only access these expressions in polymorphic contexts (Headers)");
        if (!poly_values[poly_type->value_access_index].only_datatype_known) {
            expression_info_set_constant(info, poly_values[poly_type->value_access_index].options.value);
            return info;
        }

        EXIT_TYPE(upcast(poly_type));
    }
    case AST::Expression_Type::CAST:
    {
        auto cast = &expr->options.cast;
        Expression_Context operand_context;
        if (cast->to_type.available) {
            auto destination_type = semantic_analyser_analyse_expression_type(cast->to_type.value);
            operand_context = expression_context_make_specific_type(destination_type, false, Cast_Mode::EXPLICIT);
        }
        else
        {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
                operand_context = expression_context_make_specific_type(context.expected_type.type, false, Cast_Mode::IMPLICIT);
            }
            else {
                log_semantic_error("No context is available for auto cast", expr);
                operand_context = expression_context_make_unknown(true);
            }
        }

        auto result_type = semantic_analyser_analyse_expression_value(cast->operand, operand_context);
        EXIT_VALUE(result_type);
    }
    case AST::Expression_Type::LITERAL_READ:
    {
        auto& read = expr->options.literal_read;
        void* value_ptr;
        Datatype* literal_type;
        void* null_pointer = 0;
        Upp_String string_buffer;

        // Missing: float, nullptr
        switch (read.type)
        {
        case Literal_Type::BOOLEAN:
            literal_type = upcast(types.bool_type);
            value_ptr = &read.options.boolean;
            break;
        case Literal_Type::FLOAT_VAL:
            literal_type = upcast(types.f32_type);
            value_ptr = &read.options.float_val;
            break;
        case Literal_Type::INTEGER:
            literal_type = upcast(types.i32_type);
            value_ptr = &read.options.int_val;
            break;
        case Literal_Type::STRING: {
            String* string = read.options.string;
            string_buffer.character_buffer.data = string->characters;
            string_buffer.character_buffer.size = string->capacity;
            string_buffer.size = string->size;

            literal_type = upcast(types.string_type);
            value_ptr = &string_buffer;
            break;
        }
        case Literal_Type::NULL_VAL:
        {
            literal_type = types.void_pointer_type;
            value_ptr = &null_pointer;
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
            {
                if (context.expected_type.type->type == Datatype_Type::POINTER ||
                    context.expected_type.type->type == Datatype_Type::VOID_POINTER ||
                    context.expected_type.type->type == Datatype_Type::FUNCTION)
                {
                    literal_type = context.expected_type.type;
                }
            }
            break;
        }
        default: panic("");
        }
        expression_info_set_constant(info, literal_type, array_create_static<byte>((byte*)value_ptr, literal_type->memory_info.value.size), AST::upcast(expr));
        return info;
    }
    case AST::Expression_Type::ENUM_TYPE:
    {
        auto& members = expr->options.enum_members;
        Datatype_Enum* enum_type = type_system_make_enum_empty(0);
        int next_member_value = 1; // Note: Enum values all start at 1, so 0 represents an invalid enum
        for (int i = 0; i < members.size; i++)
        {
            auto& member_node = members[i];
            if (member_node->value.available)
            {
                semantic_analyser_analyse_expression_value(member_node->value.value, expression_context_make_specific_type(upcast(types.i32_type)));
                auto constant = expression_calculate_comptime_value(member_node->value.value, "Enum value must be comptime known");
                if (constant.available) {
                    next_member_value = upp_constant_to_value<i32>(constant.value);
                }
            }

            Enum_Item member;
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
                    log_semantic_error("Enum member name is already in use", AST::upcast(expr));
                    log_error_info_id(other->name);
                }
                if (other->value == member->value) {
                    log_semantic_error("Enum value is already taken by previous member", AST::upcast(expr));
                    log_error_info_id(other->name);
                }
            }
        }
        type_system_finish_enum(enum_type);
        EXIT_TYPE(upcast(enum_type));
    }
    case AST::Expression_Type::MODULE: {
        log_semantic_error("Module not valid in this context", AST::upcast(expr));
        EXIT_ERROR(types.error_type);
    }
    case AST::Expression_Type::FUNCTION: {
        // Create new function progress and wait for header analyis to finish
        auto progress = function_progress_create_with_modtree_function(0, expr, 0, nullptr, Symbol_Access_Level::POLYMORPHIC);
        progress->header_workload->base.polymorphic_values = analyser.current_workload->polymorphic_values;
        progress->body_workload->base.polymorphic_values = analyser.current_workload->polymorphic_values;
        analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(progress->header_workload));
        workload_executer_wait_for_dependency_resolution();

        // Return function
        semantic_analyser_register_function_call(progress->function);
        EXIT_FUNCTION(progress->function);
    }
    case AST::Expression_Type::STRUCTURE_TYPE: {
        auto workload = workload_structure_create(expr, 0, false, Symbol_Access_Level::POLYMORPHIC);
        workload->base.polymorphic_values = analyser.current_workload->polymorphic_values;
        EXIT_TYPE(upcast(workload->struct_type));
    }
    case AST::Expression_Type::BAKE_BLOCK:
    case AST::Expression_Type::BAKE_EXPR: {
        // Create bake progress and wait for it to finish
        Datatype* expected_type = 0;
        if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
            expected_type = context.expected_type.type;
        }
        auto progress = bake_progress_create(expr, expected_type);
        progress->analysis_workload->base.polymorphic_values = analyser.current_workload->polymorphic_values;
        analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(progress->execute_workload));
        workload_executer_wait_for_dependency_resolution();

        // Handle result
        auto bake_result = &progress->result;
        if (bake_result->available) {
            expression_info_set_constant(info, bake_result->value);
            return info;
        }
        else {
            auto return_type = progress->bake_function->signature->return_type;
            if (return_type.available) {
                EXIT_ERROR(return_type.value);
            }
            else {
                EXIT_ERROR(types.error_type);
            }
        }
        panic("invalid_code_path");
        EXIT_ERROR(types.error_type);
    }
    case AST::Expression_Type::FUNCTION_SIGNATURE:
    {
        auto& sig = expr->options.function_signature;
        auto unfinished = type_system_make_function_empty();
        int non_comptime_parameter_counter = 0;
        for (int i = 0; i < sig.parameters.size; i++)
        {
            auto& param_node = sig.parameters[i];
            if (param_node->is_comptime) {
                log_semantic_error("Comptime parameters are only allowed in functions, not in signatures!", AST::upcast(param_node));
                continue;
            }
            Function_Parameter parameter = function_parameter_make_empty();
            analyse_parameter_type_and_value(parameter, param_node);
            dynamic_array_push_back(&unfinished.parameters, parameter);
        }
        Datatype* return_type = 0;
        if (sig.return_value.available) {
            return_type = semantic_analyser_analyse_expression_type(sig.return_value.value);
        }
        EXIT_TYPE(upcast(type_system_finish_function(unfinished, return_type)));
    }
    case AST::Expression_Type::ARRAY_TYPE:
    {
        auto& array_node = expr->options.array_type;

        // Analyse size expression
        semantic_analyser_analyse_expression_value(
            array_node.size_expr, expression_context_make_specific_type(upcast(types.i32_type))
        );

        // Calculate comptime size
        int array_size = 0; // Note: Here I actually mean the element count, not the data-type size
        bool array_size_known = false;
        auto comptime = expression_calculate_comptime_value(array_node.size_expr, "Array size must be know at compile time");
        if (comptime.available) {
            array_size_known = true;
            array_size = upp_constant_to_value<int>(comptime.value);
            if (array_size <= 0) {
                log_semantic_error("Array size must be greater than zero", array_node.size_expr);
                array_size_known = false;
            }
        }

        Datatype* element_type = semantic_analyser_analyse_expression_type(array_node.type_expr);
        auto result = type_system_make_array(element_type, array_size_known, array_size);

        // Handle implicit polymorphic symbols for array size, e.g. foo :: (a: [$C]int)
        auto template_parameter = expression_is_unset_template_parameter(array_node.size_expr);
        if (template_parameter != 0) {
            result->polymorphic_count_variable = template_parameter;
            result->base.contains_type_template = true;
        }

        EXIT_TYPE(upcast(result));
    }
    case AST::Expression_Type::SLICE_TYPE:
    {
        EXIT_TYPE(upcast(type_system_make_slice(semantic_analyser_analyse_expression_type(expr->options.slice_type))));
    }
    case AST::Expression_Type::NEW_EXPR:
    {
        auto& new_node = expr->options.new_expr;
        Datatype* allocated_type = semantic_analyser_analyse_expression_type(new_node.type_expr);
        // Wait for type size since ir-generator needs it to function properly
        type_wait_for_size_info_to_finish(allocated_type);

        Datatype* result_type = 0;
        if (new_node.count_expr.available) {
            result_type = upcast(type_system_make_slice(allocated_type));
            semantic_analyser_analyse_expression_value(new_node.count_expr.value, expression_context_make_specific_type(upcast(types.i32_type)));
        }
        else {
            result_type = upcast(type_system_make_pointer(allocated_type));
        }
        EXIT_VALUE(result_type);
    }
    case AST::Expression_Type::STRUCT_INITIALIZER:
    {
        auto& init_node = expr->options.struct_initializer;
        auto& arguments = init_node.arguments;

        // Find struct type
        Datatype* type = 0;
        if (init_node.type_expr.available) {
            type = semantic_analyser_analyse_expression_type(init_node.type_expr.value);
        }
        else {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
                type = context.expected_type.type;
            }
            else {
                if (!(context.type == Expression_Context_Type::UNKNOWN && context.unknown_due_to_error)) {
                    log_semantic_error("Could not determine struct type from context", expr, Parser::Section::FIRST_TOKEN);
                }
                else {
                    semantic_analyser_set_error_flag(true);
                }
                EXIT_ERROR(types.error_type);
            }
        }

        if (datatype_is_unknown(type)) {
            arguments_analyse_in_unknown_context(arguments);
            EXIT_ERROR(type);
        }

        // Check type errors
        if (type->type != Datatype_Type::STRUCT) {
            log_semantic_error("Struct initializer requires structure type", expr);
            log_error_info_given_type(type);
            arguments_analyse_in_unknown_context(arguments);
            EXIT_ERROR(type);
        }
        type_wait_for_size_info_to_finish(type);

        auto struct_signature = downcast<Datatype_Struct>(type);
        auto& members = struct_signature->members;

        // Special case for UNIONS, where only one member can be specified
        if (struct_signature->struct_type != AST::Structure_Type::STRUCT)
        {
            if (arguments.size == 1)
            {
                auto arg = arguments[0];
                auto arg_info = get_info(arg, true);
                arg_info->argument_index = -1;
                arg_info->is_polymorphic = false;

                if (!arg->name.available) {
                    log_semantic_error("Union initializer requires a named argument, not an unnamed one", AST::upcast(arg), Parser::Section::FIRST_TOKEN);
                    arguments_analyse_in_unknown_context(init_node.arguments);
                    EXIT_ERROR(upcast(struct_signature));
                }

                // Find corresponding member
                for (int i = 0; i < members.size; i++) {
                    if (members[i].id == arg->name.value) {
                        arg_info->argument_index = i;
                        break;
                    }
                }

                // Handle errors
                if (arg_info->argument_index == -1) {
                    log_semantic_error("Union does not contain a member with this name", AST::upcast(arg), Parser::Section::FIRST_TOKEN);
                    arguments_analyse_in_unknown_context(init_node.arguments);
                    EXIT_ERROR(upcast(struct_signature));
                }
                auto& member = members[arg_info->argument_index];
                if (member.offset == struct_signature->tag_member.offset) {
                    log_semantic_error("Cannot set the tag value in initializer", AST::upcast(init_node.arguments[0]));
                    arguments_analyse_in_unknown_context(init_node.arguments);
                    EXIT_ERROR(upcast(struct_signature));
                }

                semantic_analyser_analyse_expression_value(arg->value, expression_context_make_specific_type(member.type));
                EXIT_VALUE(upcast(struct_signature));
            }
            else if (init_node.arguments.size > 1) {
                log_semantic_error("Only one value must be given for union initializer", expr);
                arguments_analyse_in_unknown_context(init_node.arguments);
                EXIT_ERROR(upcast(struct_signature));
            }
            else {
                log_semantic_error("One initializer value is required in union initializer", expr, Parser::Section::ENCLOSURE);
                EXIT_ERROR(upcast(struct_signature));
            }
        }

        // Match arguments
        int unnamed_argument_count;
        bool argument_name_error = arguments_check_for_naming_errors(arguments, &unnamed_argument_count);
        if (argument_name_error) {
            arguments_analyse_in_unknown_context(arguments);
            EXIT_ERROR(upcast(struct_signature));
        }
        bool argument_matching_error = !arguments_match_to_parameters(
            arguments, callable_create_struct_initializer(struct_signature), unnamed_argument_count, true, upcast(expr), Parser::Section::ENCLOSURE);
        if (argument_matching_error) {
            arguments_analyse_in_unknown_context(arguments);
            EXIT_ERROR(upcast(struct_signature));
        }

        // Analyse arguments
        for (int i = 0; i < arguments.size; i++) {
            auto& arg = arguments[i];
            auto arg_info = get_info(arg);
            semantic_analyser_analyse_expression_value(arg->value, expression_context_make_specific_type(members[arg_info->argument_index].type));
        }

        EXIT_VALUE(upcast(struct_signature));
    }
    case AST::Expression_Type::ARRAY_INITIALIZER:
    {
        auto& init_node = expr->options.array_initializer;
        Datatype* element_type = 0;
        if (init_node.type_expr.available) {
            element_type = semantic_analyser_analyse_expression_type(init_node.type_expr.value);
        }
        else
        {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
            {
                if (context.expected_type.type->type == Datatype_Type::ARRAY) {
                    element_type = downcast<Datatype_Array>(context.expected_type.type)->element_type;
                }
                else if (context.expected_type.type->type == Datatype_Type::SLICE) {
                    element_type = downcast<Datatype_Slice>(context.expected_type.type)->element_type;
                }
                else {
                    element_type = 0;
                }
            }
            if (element_type == 0) {
                log_semantic_error("Could not determine array element type from context", expr);
                element_type = types.error_type;
            }
        }

        int array_element_count = init_node.values.size;
        // There are no 0-sized arrays, only 0-sized slices. So if we encounter an empty initializer, e.g. type.[], we return an empty slice
        if (array_element_count == 0) {
            EXIT_VALUE(upcast(type_system_make_slice(element_type)));
        }

        for (int i = 0; i < init_node.values.size; i++) {
            semantic_analyser_analyse_expression_value(init_node.values[i], expression_context_make_specific_type(element_type));
        }
        EXIT_VALUE(upcast(type_system_make_array(element_type, true, array_element_count)));
    }
    case AST::Expression_Type::ARRAY_ACCESS:
    {
        auto& access_node = expr->options.array_access;
        auto array_type = semantic_analyser_analyse_expression_value(
            access_node.array_expr, expression_context_make_auto_dereference()
        );

        Datatype* element_type = 0;
        if (array_type->type == Datatype_Type::ARRAY) {
            element_type = downcast<Datatype_Array>(array_type)->element_type;
        }
        else if (array_type->type == Datatype_Type::SLICE) {
            element_type = downcast<Datatype_Slice>(array_type)->element_type;
        }
        else if (datatype_is_unknown(array_type)) {
            semantic_analyser_set_error_flag(true);
            element_type = types.error_type;
        }
        else {
            log_semantic_error("Array access can only currently happen on array or slice types", expr);
            log_error_info_given_type(array_type);
            element_type = types.error_type;
        }

        semantic_analyser_analyse_expression_value(
            access_node.index_expr, expression_context_make_specific_type(upcast(types.i32_type))
        );
        EXIT_VALUE(element_type);
    }
    case AST::Expression_Type::MEMBER_ACCESS:
    {
        auto& member_node = expr->options.member_access;
        auto access_expr_info = semantic_analyser_analyse_expression_any(member_node.expr, expression_context_make_auto_dereference());
        auto& ids = compiler.predefined_ids;

        info->specifics.member_access.type = Member_Access_Type::OTHER;
        auto search_struct_type_for_polymorphic_parameter_access = [&](Datatype_Struct* struct_type) -> Optional<Polymorphic_Value> {
            if (struct_type->workload == 0) {
                return optional_make_failure<Polymorphic_Value>();
            }

            auto struct_workload = struct_type->workload;
            Polymorphic_Base_Info* polymorphic = 0;
            if (struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
                polymorphic = &struct_workload->polymorphic.base->info;
            }
            else if (struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
                polymorphic = &struct_workload->polymorphic.instance.parent->info;
            }
            else {
                return optional_make_failure<Polymorphic_Value>();
            }

            // Try to find structure parameter with this name
            int value_access_index = -1;
            for (int i = 0; i < polymorphic->parameters.size; i++) {
                auto& parameter = polymorphic->parameters[i];
                if (!parameter.infos.name.available) {
                    continue;
                }
                if (parameter.infos.name.value == member_node.name) {
                    value_access_index = parameter.options.value_access_index;
                    break;
                }
            }
            // Search implicit parameters
            for (int i = 0; i < polymorphic->implicit_parameter_infos.size && value_access_index == -1; i++) {
                auto& implicit = polymorphic->implicit_parameter_infos[i];
                if (implicit.id == member_node.name) {
                    value_access_index = implicit.template_parameter->value_access_index;
                    break;
                }
            }

            if (value_access_index != -1) {
                info->specifics.member_access.type = Member_Access_Type::STRUCT_POLYMORHPIC_PARAMETER_ACCESS;
                info->specifics.member_access.index = value_access_index;
                info->specifics.member_access.struct_workload = struct_workload;

                return optional_make_success(struct_workload->base.polymorphic_values[value_access_index]);
            }

            return optional_make_failure<Polymorphic_Value>();
        };

        switch (access_expr_info->result_type)
        {
        case Expression_Result_Type::TYPE:
        {
            Datatype* base_type = access_expr_info->options.type;
            if (datatype_is_unknown(base_type)) {
                semantic_analyser_set_error_flag(true);
                EXIT_ERROR(types.error_type);
            }

            // Handle struct parameter access and union tag access
            if (base_type->type == Datatype_Type::STRUCT)
            {
                auto structure = downcast<Datatype_Struct>(base_type);
                auto poly_parameter_access = search_struct_type_for_polymorphic_parameter_access(structure);
                if (poly_parameter_access.available) {
                    auto value = poly_parameter_access.value;
                    if (value.only_datatype_known) {
                        EXIT_TYPE(value.options.type);
                    }
                    else {
                        expression_info_set_constant(info, value.options.value);
                        return info;
                    }
                }

                // Enable . access for union types to access tag, e.g. if get_tag(addr) == Address.ipv4
                if (structure->struct_type == AST::Structure_Type::UNION) {
                    base_type = downcast<Datatype_Struct>(base_type)->tag_member.type;
                }
            }

            if (base_type->type != Datatype_Type::ENUM) {
                log_semantic_error("Member access for given type not possible", member_node.expr);
                log_error_info_given_type(base_type);
                EXIT_ERROR(types.error_type);
            }
            auto enum_type = downcast<Datatype_Enum>(base_type);
            auto& members = enum_type->members;

            Enum_Item* found = 0;
            for (int i = 0; i < members.size; i++) {
                auto member = &members[i];
                if (member->name == member_node.name) {
                    found = member;
                    break;
                }
            }

            int value = 0;
            if (found == 0) {
                log_semantic_error("Enum/Union does not contain this member", member_node.expr);
                log_error_info_id(member_node.name);
            }
            else {
                value = found->value;
            }
            expression_info_set_constant_enum(info, upcast(enum_type), value);
            return info;
        }
        case Expression_Result_Type::NOTHING: {
            log_semantic_error("Cannot use member access ('x.y') on nothing", member_node.expr);
            log_error_info_expression_result_type(access_expr_info->result_type);
            EXIT_ERROR(types.error_type);
        }
        case Expression_Result_Type::FUNCTION:
        case Expression_Result_Type::HARDCODED_FUNCTION:
        case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
            log_semantic_error("Cannot use member access ('x.y') on functions", member_node.expr);
            log_error_info_expression_result_type(access_expr_info->result_type);
            EXIT_ERROR(types.error_type);
        }
        case Expression_Result_Type::POLYMORPHIC_STRUCT: {
            log_semantic_error("Cannot access members of uninstanciated polymorphic struct", member_node.expr);
            log_error_info_expression_result_type(access_expr_info->result_type);
            EXIT_ERROR(types.error_type);
        }
        case Expression_Result_Type::VALUE:
        case Expression_Result_Type::CONSTANT:
        {
            auto base_type = access_expr_info->cast_info.type_afterwards;
            if (datatype_is_unknown(base_type)) {
                semantic_analyser_set_error_flag(true);
                EXIT_ERROR(types.error_type);
            }

            if (base_type->type == Datatype_Type::STRUCT)
            {
                type_wait_for_size_info_to_finish(base_type);
                auto structure = downcast<Datatype_Struct>(base_type);
                auto& members = structure->members;
                for (int i = 0; i < members.size; i++) {
                    Struct_Member* member = &members[i];
                    if (member->id == member_node.name) {
                        info->specifics.member_access.type = Member_Access_Type::STRUCT_MEMBER_ACCESS;
                        info->specifics.member_access.index = i;
                        EXIT_VALUE(member->type);
                    }
                }

                auto poly_parameter_access = search_struct_type_for_polymorphic_parameter_access(structure);
                if (poly_parameter_access.available) {
                    auto value = poly_parameter_access.value;
                    if (value.only_datatype_known) {
                        EXIT_TYPE(value.options.type);
                    }
                    else {
                        expression_info_set_constant(info, value.options.value);
                        return info;
                    }
                }

                log_semantic_error("Struct doesn't contain a member with this name", expr);
                log_error_info_id(member_node.name);
                EXIT_ERROR(types.error_type);
            }
            else if (base_type->type == Datatype_Type::ARRAY || base_type->type == Datatype_Type::SLICE)
            {
                if (member_node.name != ids.size && member_node.name != ids.data) {
                    log_semantic_error("Arrays/Slices only have either .size or .data as members", expr);
                    log_error_info_id(member_node.name);
                    EXIT_ERROR(types.error_type);
                }

                if (base_type->type == Datatype_Type::ARRAY)
                {
                    auto array = downcast<Datatype_Array>(base_type);
                    if (member_node.name == ids.size) {
                        if (array->count_known) {
                            expression_info_set_constant_i32(info, array->element_count);
                        }
                        else {
                            EXIT_ERROR(upcast(types.i32_type));
                        }
                        return info;
                    }
                    else
                    { // Data access
                        EXIT_VALUE(upcast(type_system_make_pointer(array->element_type)));
                    }
                }
                else // Slice
                {
                    auto slice = downcast<Datatype_Slice>(base_type);
                    Struct_Member member;
                    if (member_node.name == ids.size) {
                        member = slice->size_member;
                    }
                    else {
                        member = slice->data_member;
                    }
                    EXIT_VALUE(member.type);
                }
            }
            else
            {
                log_semantic_error("Given type does not define member access", expr);
                log_error_info_given_type(base_type);
                EXIT_ERROR(types.error_type);
            }
            panic("");
            break;
        }
        default: panic("");
        }
        panic("Should not happen");
        EXIT_ERROR(types.error_type);
    }
    case AST::Expression_Type::AUTO_ENUM:
    {
        String* id = expr->options.auto_enum;
        if (context.type != Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
            log_semantic_error("Could not determine context for auto enum", expr);
            EXIT_ERROR(types.error_type);
        }
        if (context.expected_type.type->type != Datatype_Type::ENUM) {
            log_semantic_error("Context requires a type that is not an enum, so .NAME syntax is not valid", expr);
            log_error_info_given_type(context.expected_type.type);
            EXIT_ERROR(types.error_type);
        }

        auto& members = downcast<Datatype_Enum>(context.expected_type.type)->members;
        Enum_Item* found = 0;
        for (int i = 0; i < members.size; i++) {
            auto member = &members[i];
            if (member->name == id) {
                found = member;
                break;
            }
        }

        int value = 0;
        if (found == 0) {
            log_semantic_error("Enum does not contain this member", expr);
            log_error_info_id(id);
        }
        else {
            value = found->value;
        }

        expression_info_set_constant_enum(info, context.expected_type.type, value);
        return info;
    }
    case AST::Expression_Type::UNARY_OPERATION:
    {
        auto& unary_node = expr->options.unop;
        switch (unary_node.type)
        {
        case AST::Unop::NEGATE:
        case AST::Unop::NOT:
        {
            bool is_negate = unary_node.type == AST::Unop::NEGATE;
            auto operand_type = semantic_analyser_analyse_expression_value(
                unary_node.expr,
                is_negate ? expression_context_make_auto_dereference() : expression_context_make_specific_type(upcast(types.bool_type))
            );

            if (is_negate)
            {
                bool valid = false;
                if (operand_type->type == Datatype_Type::PRIMITIVE) {
                    auto primitive = downcast<Datatype_Primitive>(operand_type);
                    valid = primitive->is_signed && primitive->primitive_type != Primitive_Type::BOOLEAN;
                }
                if (!valid) {
                    log_semantic_error("Type not valid for negate operator", expr);
                    log_error_info_given_type(operand_type);
                }
            }

            EXIT_VALUE(is_negate ? operand_type : upcast(types.bool_type));
        }
        case AST::Unop::POINTER:
        {
            // TODO: I think I can check if the context is a specific type + pointer and continue with child type
            auto operand_result = semantic_analyser_analyse_expression_any(unary_node.expr, expression_context_make_unknown());

            // Handle constant type_handles correctly
            if (operand_result->result_type == Expression_Result_Type::CONSTANT &&
                operand_result->options.constant.type->type == Datatype_Type::TYPE_HANDLE)
            {
                auto handle = upp_constant_to_value<Upp_Type_Handle>(operand_result->options.constant);
                if ((int)handle.index < 0 || (int)handle.index >= type_system->types.size) {
                    log_semantic_error("Constant type handle is invalid", unary_node.expr);
                }
                EXIT_TYPE(upcast(type_system_make_pointer(type_system->types[handle.index])));
            }

            switch (operand_result->result_type)
            {
            case Expression_Result_Type::VALUE:
            case Expression_Result_Type::CONSTANT:
            {
                Datatype* operand_type = operand_result->cast_info.type_afterwards;
                if (datatype_is_unknown(operand_type)) {
                    semantic_analyser_set_error_flag(true);
                    EXIT_ERROR(operand_type);
                }
                if (!expression_has_memory_address(unary_node.expr)) {
                    log_semantic_error("Cannot get memory address of a temporary value", expr);
                }
                EXIT_VALUE(upcast(type_system_make_pointer(operand_type)));
            }
            case Expression_Result_Type::TYPE: {
                EXIT_TYPE(upcast(type_system_make_pointer(operand_result->options.type)));
            }
            case Expression_Result_Type::NOTHING: {
                log_semantic_error("Cannot get pointer to nothing", expr);
                EXIT_ERROR(types.error_type);
                break;
            }
            case Expression_Result_Type::FUNCTION:
            case Expression_Result_Type::POLYMORPHIC_FUNCTION:
            case Expression_Result_Type::HARDCODED_FUNCTION: {
                log_semantic_error("Cannot get pointer to a function (Function pointers don't require *)", expr);
                EXIT_ERROR(types.error_type);
            }
            case Expression_Result_Type::POLYMORPHIC_STRUCT: {
                log_semantic_error("Cannot get pointer to a polymorphic struct (Must be instanciated)", expr);
                EXIT_ERROR(types.error_type);
            }
            default: panic("");
            }
            panic("");
            break;
        }
        case AST::Unop::DEREFERENCE:
        {
            auto operand_type = semantic_analyser_analyse_expression_value(unary_node.expr, expression_context_make_unknown());
            Datatype* result_type = types.error_type;
            if (operand_type->type != Datatype_Type::POINTER || types_are_equal(operand_type, types.void_pointer_type)) {
                log_semantic_error("Cannot dereference non-pointer value", expr);
                log_error_info_given_type(operand_type);
            }
            else {
                result_type = downcast<Datatype_Pointer>(operand_type)->points_to_type;
            }
            EXIT_VALUE(result_type);
        }
        default:panic("");
        }
        panic("");
        EXIT_ERROR(types.error_type);
    }
    case AST::Expression_Type::BINARY_OPERATION:
    {
        auto& binop_node = expr->options.binop;

        // Determine what operands are valid
        bool int_valid = false;
        bool float_valid = false;
        bool bool_valid = false;
        bool ptr_valid = false;
        bool result_type_is_bool = false;
        bool enum_valid = false;
        bool type_type_valid = false;
        Expression_Context operand_context;
        switch (binop_node.type)
        {
        case AST::Binop::ADDITION:
        case AST::Binop::SUBTRACTION:
        case AST::Binop::MULTIPLICATION:
        case AST::Binop::DIVISION:
            float_valid = true;
            int_valid = true;
            operand_context = expression_context_make_auto_dereference();
            break;
        case AST::Binop::MODULO:
            int_valid = true;
            operand_context = expression_context_make_auto_dereference();
            break;
        case AST::Binop::GREATER:
        case AST::Binop::GREATER_OR_EQUAL:
        case AST::Binop::LESS:
        case AST::Binop::LESS_OR_EQUAL:
            float_valid = true;
            int_valid = true;
            result_type_is_bool = true;
            enum_valid = true;
            operand_context = expression_context_make_auto_dereference();
            break;
        case AST::Binop::POINTER_EQUAL:
        case AST::Binop::POINTER_NOT_EQUAL:
            ptr_valid = true;
            operand_context = expression_context_make_unknown();
            result_type_is_bool = true;
            break;
        case AST::Binop::EQUAL:
        case AST::Binop::NOT_EQUAL:
            float_valid = true;
            int_valid = true;
            bool_valid = true;
            ptr_valid = true;
            enum_valid = true;
            type_type_valid = true;
            operand_context = expression_context_make_auto_dereference();
            result_type_is_bool = true;
            break;
        case AST::Binop::AND:
        case AST::Binop::OR:
            bool_valid = true;
            result_type_is_bool = true;
            operand_context = expression_context_make_specific_type(upcast(types.bool_type));
            break;
        case AST::Binop::INVALID:
            operand_context = expression_context_make_unknown();
            break;
        default: panic("");
        }

        // Evaluate operands
        auto left_type = semantic_analyser_analyse_expression_value(binop_node.left, operand_context);
        // NOTE: For now, the left type dictates what type we are expecting, this will change with operator overloading
        auto right_type = semantic_analyser_analyse_expression_value(binop_node.right, expression_context_make_specific_type(left_type));

        // Check for unknowns
        if (datatype_is_unknown(left_type) || datatype_is_unknown(right_type)) {
            EXIT_ERROR(types.error_type);
        }

        // Try implicit casting if types dont match
        Datatype* operand_type = left_type;
        bool types_are_valid = true;

        // Check if given type is valid
        if (types_are_valid)
        {
            if (operand_type->type == Datatype_Type::POINTER) {
                types_are_valid = ptr_valid;
            }
            else if (operand_type->type == Datatype_Type::VOID_POINTER) {
                types_are_valid = ptr_valid;
            }
            else if (operand_type->type == Datatype_Type::PRIMITIVE)
            {
                auto primitive = downcast<Datatype_Primitive>(operand_type);
                if (primitive->primitive_type == Primitive_Type::INTEGER && !int_valid) {
                    types_are_valid = false;
                }
                if (primitive->primitive_type == Primitive_Type::FLOAT && !float_valid) {
                    types_are_valid = false;
                }
                if (primitive->primitive_type == Primitive_Type::BOOLEAN && !bool_valid) {
                    types_are_valid = false;
                }
            }
            else if (operand_type->type == Datatype_Type::ENUM) {
                types_are_valid = enum_valid;
            }
            else if (operand_type->type == Datatype_Type::TYPE_HANDLE) {
                types_are_valid = type_type_valid;
            }
            else {
                types_are_valid = false;
            }
        }

        if (!types_are_valid) {
            log_semantic_error("Types aren't valid for binary operation", expr);
            log_error_info_binary_op_type(left_type, right_type);
        }
        EXIT_VALUE(result_type_is_bool ? upcast(types.bool_type) : operand_type);
    }
    default: {
        panic("Not all expression covered!\n");
        break;
    }
    }

    panic("HEY");
    EXIT_ERROR(types.error_type);

#undef EXIT_VALUE
#undef EXIT_TYPE
#undef EXIT_ERROR
#undef EXIT_HARDCODED
#undef EXIT_FUNCTION
}


Cast_Type check_cast_type_no_auto_operations(Expression_Cast_Info& result, Datatype* source_type, Datatype* destination_type, Cast_Mode cast_mode)
{
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;

    // Check for simple cases
    if (datatype_is_unknown(source_type) || datatype_is_unknown(destination_type)) {
        return Cast_Type::UNKNOWN;
    }
    if (types_are_equal(source_type, destination_type)) return Cast_Type::NO_CAST;

    // Figure out type of cast + what mode is allowed
    Cast_Type cast_type = Cast_Type::INVALID;
    Cast_Mode allowed_mode = Cast_Mode::NONE;

    // Check built-in cast types
    switch (source_type->type)
    {
    case Datatype_Type::ARRAY:
    {
        if (destination_type->type == Datatype_Type::SLICE) {
            auto source_array = downcast<Datatype_Array>(source_type);
            auto dest_slice = downcast<Datatype_Slice>(destination_type);
            if (types_are_equal(source_array->element_type, dest_slice->element_type)) {
                cast_type = Cast_Type::ARRAY_TO_SLICE;
                allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::ARRAY_TO_SLICE);
            }
        }
        break;
    }
    case Datatype_Type::ENUM:
    {
        if (destination_type->type == Datatype_Type::PRIMITIVE)
        {
            auto primitive = downcast<Datatype_Primitive>(destination_type);
            if (primitive->primitive_type == Primitive_Type::INTEGER) {
                cast_type = Cast_Type::ENUM_TO_INT;
                allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::ENUM_TO_INT);
            }
        }
        break;
    }
    case Datatype_Type::FUNCTION:
    {
        if (destination_type->type == Datatype_Type::FUNCTION)
        {
            // Note: Casting between two different function signatures only works if they have the same parameter/return types.
            //       In C this would always result in the same type, but in upp the parameter names/default values change the function type
            auto src_fn = downcast<Datatype_Function>(source_type);
            auto dst_fn = downcast<Datatype_Function>(destination_type);
            bool cast_valid = true;

            if (src_fn->parameters.size != dst_fn->parameters.size) {
                cast_valid = false;
            }
            else {
                for (int i = 0; i < src_fn->parameters.size; i++) {
                    auto& param1 = src_fn->parameters[i];
                    auto& param2 = dst_fn->parameters[i];
                    if (!types_are_equal(param1.type, param2.type)) {
                        cast_valid = false;
                    }
                }
            }
            if (src_fn->return_type.available != dst_fn->return_type.available) {
                cast_valid = false;
            }
            else if (src_fn->return_type.available) {
                cast_valid = types_are_equal(src_fn->return_type.value, dst_fn->return_type.value);
            }

            if (cast_valid) {
                cast_type = Cast_Type::POINTERS; // Not sure if we want to have different types for this
                allowed_mode = Cast_Mode::AUTO; // Maybe we want to add this to the expression context at some point?
            }
        }
        else if (destination_type->type == Datatype_Type::VOID_POINTER)
        {
            cast_type = Cast_Type::POINTERS; // Not sure if we want to have different types for this
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::FUNCTION_POINTER_TO_VOID);
        }
        else if (destination_type->type == Datatype_Type::PRIMITIVE) {
            auto primitive = downcast<Datatype_Primitive>(destination_type);
            if (primitive->primitive_type == Primitive_Type::BOOLEAN) {
                cast_type = Cast_Type::POINTER_NULL_CHECK;
                allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::FUNCTION_POINTER_TO_BOOL);
            }
        }
        else if (types_are_equal(destination_type, upcast(types.u64_type))) {
            cast_type = Cast_Type::POINTER_TO_U64;
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::POINTER_TO_U64);
        }
        break;
    }
    case Datatype_Type::POINTER:
    {
        if (destination_type->type == Datatype_Type::VOID_POINTER) {
            cast_type = Cast_Type::POINTERS;
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::POINTER_TO_VOID_POINTER);
        }
        else if (destination_type->type == Datatype_Type::POINTER) {
            cast_type = Cast_Type::POINTERS;
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::POINTER_TO_POINTER);
        }
        else if (types_are_equal(destination_type, upcast(types.u64_type))) {
            cast_type = Cast_Type::POINTER_TO_U64;
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::POINTER_TO_U64);
        }
        else if (destination_type->type == Datatype_Type::PRIMITIVE) {
            auto primitive = downcast<Datatype_Primitive>(destination_type);
            if (primitive->primitive_type == Primitive_Type::BOOLEAN) {
                cast_type = Cast_Type::POINTER_NULL_CHECK;
                allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::POINTER_TO_BOOL);
            }
        }
        break;
    }
    case Datatype_Type::VOID_POINTER:
    {
        if (destination_type->type == Datatype_Type::POINTER) {
            cast_type = Cast_Type::POINTERS;
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::VOID_POINTER_TO_POINTER);
        }
        else if (destination_type->type == Datatype_Type::FUNCTION) {
            cast_type = Cast_Type::POINTERS;
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::VOID_TO_FUNCTION_POINTER);
        }
        else if (destination_type->type == Datatype_Type::PRIMITIVE) {
            auto primitive = downcast<Datatype_Primitive>(destination_type);
            if (primitive->primitive_type == Primitive_Type::BOOLEAN) {
                cast_type = Cast_Type::POINTER_NULL_CHECK;
                allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::VOID_POINTER_TO_BOOL);
            }
        }
        else if (types_are_equal(destination_type, upcast(types.u64_type))) {
            cast_type = Cast_Type::POINTER_TO_U64;
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::POINTER_TO_U64);
        }
        break;
    }
    case Datatype_Type::PRIMITIVE:
    {
        auto src_primitive = downcast<Datatype_Primitive>(source_type);
        if (destination_type->type == Datatype_Type::PRIMITIVE)
        {
            auto dst_primitive = downcast<Datatype_Primitive>(destination_type);
            auto src_size = source_type->memory_info.value.size;
            auto dst_size = destination_type->memory_info.value.size;

            // Figure out allowed mode and cast type
            if (src_primitive->primitive_type == Primitive_Type::INTEGER && dst_primitive->primitive_type == Primitive_Type::INTEGER)
            {
                cast_type = Cast_Type::INTEGERS;
                if (src_primitive->is_signed == dst_primitive->is_signed)
                {
                    if (dst_size > src_size) {
                        allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::INTEGER_SIZE_UPCAST);
                    }
                    else {
                        allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::INTEGER_SIZE_DOWNCAST);
                    }
                }
                else {
                    // Signed to unsigned
                    if (dst_size == src_size) {
                        if (src_primitive->is_signed) {
                            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::INTEGER_SIGNED_TO_UNSIGNED);
                        }
                        else {
                            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::INTEGER_UNSIGNED_TO_SIGNED);
                        }
                    }
                    else {
                        // Currently we don't allow both a signed cast (Between signed and unsigned) and a size cast in one, e.g. u8 -> i32, i64 -> u16
                        allowed_mode = Cast_Mode::NONE;
                    }
                }
            }
            else if (dst_primitive->primitive_type == Primitive_Type::FLOAT && src_primitive->primitive_type == Primitive_Type::INTEGER)
            {
                cast_type = Cast_Type::INT_TO_FLOAT;
                allowed_mode = Cast_Mode::AUTO;
            }
            else if (dst_primitive->primitive_type == Primitive_Type::INTEGER && src_primitive->primitive_type == Primitive_Type::FLOAT)
            {
                cast_type = Cast_Type::FLOAT_TO_INT;
                allowed_mode = Cast_Mode::IMPLICIT;
            }
            else if (dst_primitive->primitive_type == Primitive_Type::FLOAT && src_primitive->primitive_type == Primitive_Type::FLOAT)
            {
                cast_type = Cast_Type::FLOATS;
                if (dst_size > src_size) {
                    allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::FLOAT_SIZE_UPCAST);
                }
                else {
                    allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::FLOAT_SIZE_DOWNCAST);
                }
            }
            else { // Booleans can never be cast
                allowed_mode = Cast_Mode::NONE;
            }
        }
        else if (destination_type->type == Datatype_Type::ENUM)
        {
            if (src_primitive->primitive_type == Primitive_Type::INTEGER) {
                cast_type = Cast_Type::INT_TO_ENUM;
                allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::INT_TO_ENUM);
            }
        }
        else if (types_are_equal(source_type, upcast(types.u64_type)) && 
                (destination_type->type == Datatype_Type::POINTER || 
                destination_type->type == Datatype_Type::VOID_POINTER) ||
                destination_type->type == Datatype_Type::FUNCTION) 
        {
            cast_type = Cast_Type::U64_TO_POINTER;
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::U64_TO_POINTER);
        }
        break;
    }
    default: break;
    }

    // Check any casting
    if (cast_type == Cast_Type::INVALID)
    {
        if (types_are_equal(destination_type, upcast(types.any_type)))
        {
            cast_type = Cast_Type::TO_ANY;
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::TO_ANY);
        }
        else if (types_are_equal(source_type, upcast(types.any_type))) {
            cast_type = Cast_Type::FROM_ANY;
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::FROM_ANY);
        }
    }

    // Check for custom casts
    if (cast_type == Cast_Type::INVALID)
    {
        Datatype_Pair pair;
        pair.from = source_type;
        pair.to = destination_type;
        Custom_Cast* custom_cast = hashtable_find_element(&operator_context->custom_casts, pair);
        if (custom_cast != 0)
        {
            cast_type = Cast_Type::CUSTOM_CAST;
            allowed_mode = custom_cast->cast_mode;
            result.options.custom_cast_function = custom_cast->function;
            // Note: It may be bad to call register_function_call here (Because it could be a unwanted side-effect during e.g. operator overloading)
            semantic_analyser_register_function_call(custom_cast->function);
        }
    }

    // Check for polymorphic casts
    if (cast_type == Cast_Type::INVALID && 
        operator_context->custom_casts_polymorphic.size != 0 &&
        !check_if_polymorphic_instanciation_limit_reached())
    {
        Hashset<Datatype*> already_visited = hashset_create_pointer_empty<Datatype*>(1);
        Dynamic_Array<Matching_Constraint> constraints = dynamic_array_create_empty<Matching_Constraint>(1);
        SCOPE_EXIT(hashset_destroy(&already_visited));
        SCOPE_EXIT(dynamic_array_destroy(&constraints));
        for (int i = 0; i < operator_context->custom_casts_polymorphic.size && cast_type == Cast_Type::INVALID; i++)
        {
            const auto& custom_cast = operator_context->custom_casts_polymorphic[i];
            auto& poly_base = custom_cast.function->base_info;
            Polymorphic_Parameter& return_param = poly_base.parameters[poly_base.return_type_index];
            assert(custom_cast.argument_type->contains_type_template, "");

            // Early exit if return type is known and does not match
            if (return_param.depends_on.size == 0 && !types_are_equal(return_param.infos.type, destination_type)) {
                continue;
            }

            // Check if argument would match with poly-type
            Array<Polymorphic_Value> poly_values = array_create_empty<Polymorphic_Value>(poly_base.base_parameter_values.size);
            SCOPE_EXIT(if (poly_values.data != 0) { array_destroy(&poly_values); });
            int match_count = 0;
            bool match_success = match_templated_type(custom_cast.argument_type, source_type, match_count, already_visited, poly_values, constraints);
            if (!match_success) {
                continue;
            }

            // Get return type (may require re-analysis)
            Datatype* cast_return_type;
            if (return_param.depends_on.size == 0) {
                cast_return_type = return_param.infos.type; // Use base type in this case
                assert(!cast_return_type->contains_type_template, "Should be checked before being added to custom cast array");
            }
            else {
                // Reanalysis is required here
                auto workload = semantic_analyser.current_workload;
                Analysis_Pass* reanalysis_pass = analysis_pass_allocate(workload, upcast(poly_base.return_type_node));
                RESTORE_ON_SCOPE_EXIT(workload->current_pass, reanalysis_pass);
                RESTORE_ON_SCOPE_EXIT(workload->polymorphic_values, poly_values);
                RESTORE_ON_SCOPE_EXIT(workload->current_symbol_table, poly_base.symbol_table);
                RESTORE_ON_SCOPE_EXIT(workload->ignore_unknown_errors, true);
                cast_return_type = semantic_analyser_analyse_expression_type(poly_base.return_type_node);
            }

            // Stop if it's not the type we are looking for
            if (!types_are_equal(cast_return_type, destination_type)) {
                continue;
            }

            // Instanciate polymorphic function
            Callable_Polymorphic_Info poly_callable;
            poly_callable.base = &custom_cast.function->base_info;
            poly_callable.is_function = true;
            poly_callable.options.polymorphic_function = custom_cast.function;
            Array<Datatype*> instance_parameter_types = array_create_empty<Datatype*>(poly_base.parameters.size);
            SCOPE_EXIT(array_destroy(&instance_parameter_types));
            memory_set_bytes(instance_parameter_types.data, instance_parameter_types.size * sizeof(Datatype*), 0);
            instance_parameter_types[0] = source_type;
            instance_parameter_types[poly_base.return_type_index] = destination_type;
            int instance_index = polymorphic_base_instanciate(poly_base, poly_values, instance_parameter_types, poly_callable);

            // Success
            cast_type = Cast_Type::CUSTOM_CAST;
            allowed_mode = custom_cast.cast_mode;
            result.options.custom_cast_function = poly_base.instances[instance_index].options.function_instance->function;
            // Note: It may be bad to call register_function_call here (Because it could be a unwanted side-effect during e.g. operator overloading)
            semantic_analyser_register_function_call(result.options.custom_cast_function);
            break;
        }
    }

    // Check if we are in allowed cast mode
    if ((int)allowed_mode > (int)cast_mode) {
        return Cast_Type::INVALID;
    }
    return cast_type;
}

Expression_Cast_Info semantic_analyser_check_if_cast_possible(Datatype* source_type, Datatype* destination_type, Cast_Mode cast_mode, bool auto_operations_valid = true)
{
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;

    Expression_Cast_Info result;
    result.cast_type = Cast_Type::INVALID;
    result.deref_count = 0;
    result.take_address_of = false;
    result.options.error_msg = nullptr;
    result.type_afterwards = destination_type;

    // Pointer null check (Done before auto dereference/address_of)
    if (types_are_equal(destination_type, upcast(types.bool_type)))
    {
        Cast_Mode allowed_mode = Cast_Mode::NONE;
        if (source_type->type == Datatype_Type::POINTER) {
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::POINTER_TO_BOOL);
        }
        else if (source_type->type == Datatype_Type::VOID_POINTER) {
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::VOID_POINTER_TO_BOOL);
        }
        else if (source_type->type == Datatype_Type::FUNCTION) {
            allowed_mode = operator_context_get_cast_mode_setting(operator_context, AST::Context_Setting::FUNCTION_POINTER_TO_BOOL);
        }

        // Check if pointer null check is allowed
        if ((int)allowed_mode <= (int)cast_mode)
        {
            // Check for ambiguities with bool-pointers
            if (source_type->type == Datatype_Type::POINTER)
            {
                auto pointer_type = downcast<Datatype_Pointer>(source_type);
                Datatype* points_to_type = pointer_type->points_to_type;
                while (points_to_type->type == Datatype_Type::POINTER) {
                    points_to_type = downcast<Datatype_Pointer>(points_to_type)->points_to_type;
                }

                // Disallow pointer null check for pointer to bool, as this only causes ambiguity
                if (types_are_equal(points_to_type, upcast(types.bool_type))) {
                    result.cast_type = Cast_Type::INVALID;
                    result.options.error_msg = "Cannot do pointer null check here, because there is ambiguities with bool-pointers";
                    result.type_afterwards = upcast(types.bool_type);
                    return result;
                }
            }

            result.cast_type = Cast_Type::POINTER_NULL_CHECK;
            result.type_afterwards = upcast(types.bool_type);
            return result;
        }
    }

    // Test if we can cast without auto-address of/dereference
    result.cast_type = check_cast_type_no_auto_operations(result, source_type, destination_type, cast_mode);
    if (result.cast_type != Cast_Type::INVALID) {
        return result;
    }

    // Otherwise check if casts are possible after auto dereference/address-of (Can be helpful for custom casts)
    bool auto_dereference = auto_operations_valid && operator_context_get_boolean_setting(operator_context, AST::Context_Setting::AUTO_DEREFERENCE);
    bool auto_address_of = auto_operations_valid && operator_context_get_boolean_setting(operator_context, AST::Context_Setting::AUTO_ADDRESS_OF);
    if (auto_dereference && source_type->type == Datatype_Type::POINTER)
    {
        Datatype_Pointer* pointer_type = downcast<Datatype_Pointer>(source_type);
        result.deref_count += 1;
        while (true)
        {
            auto points_to = pointer_type->points_to_type;
            result.cast_type = check_cast_type_no_auto_operations(result, points_to, destination_type, cast_mode);
            if (result.cast_type != Cast_Type::INVALID) {
                return result;
            }

            if (points_to->type != Datatype_Type::POINTER) {
                break;
            }
            else {
                pointer_type = downcast<Datatype_Pointer>(points_to);
                result.deref_count += 1;
            }
        }

        result.deref_count = 0;
    }

    if (auto_address_of)
    {
        auto address_of_type = upcast(type_system_make_pointer(source_type));
        result.cast_type = check_cast_type_no_auto_operations(result, address_of_type, destination_type, cast_mode);
        if (result.cast_type != Cast_Type::INVALID) {
            result.take_address_of = true;
            return result;
        }

        result.take_address_of = false;
    }

    result.cast_type = Cast_Type::INVALID;
    return result;
}

Expression_Cast_Info expression_context_apply(
    Datatype* initial_type, Expression_Context context, bool log_errors, AST::Expression* error_report_node, Parser::Section error_section)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;

    // Set active expression info
    Expression_Info* _backup_info = semantic_analyser.current_workload->current_expression;
    SCOPE_EXIT(semantic_analyser.current_workload->current_expression = _backup_info);
    if (error_report_node != 0 && log_errors) {
        semantic_analyser.current_workload->current_expression = get_info(error_report_node);
    }

    assert(!(context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED && datatype_is_unknown(context.expected_type.type)),
        "Should be checked when in context_make_specific_type");

    Expression_Cast_Info result;
    result.type_afterwards = initial_type;
    result.deref_count = 0;
    result.take_address_of = false;
    result.cast_type = Cast_Type::INVALID;
    result.options.error_msg = "";

    switch (context.type)
    {
    case Expression_Context_Type::UNKNOWN: {
        result.cast_type = Cast_Type::NO_CAST;
        return result;
    }
    case Expression_Context_Type::AUTO_DEREFERENCE: {
        if (!operator_context_get_boolean_setting(operator_context, AST::Context_Setting::AUTO_DEREFERENCE)) {
            result.cast_type = Cast_Type::NO_CAST;
            return result;
        }
        result.type_afterwards = initial_type;
        while (result.type_afterwards->type == Datatype_Type::POINTER) {
            result.deref_count += 1;
            result.type_afterwards = downcast<Datatype_Pointer>(result.type_afterwards)->points_to_type;
        }
        result.cast_type = Cast_Type::NO_CAST;
        return result;
    }
    case Expression_Context_Type::SPECIFIC_TYPE_EXPECTED: {
        result = semantic_analyser_check_if_cast_possible(
            initial_type, context.expected_type.type, context.expected_type.cast_mode,
            !(context.expected_type.is_assignment_context && context.expected_type.type->type == Datatype_Type::POINTER)
        );

        // Check for errors
        if (log_errors)
        {
            if (result.cast_type == Cast_Type::INVALID)
            {
                semantic_analyser_set_error_flag(false);
                log_semantic_error("Cannot cast to required type", error_report_node, error_section);
                if (result.options.error_msg != 0) {
                    log_error_info_comptime_msg(result.options.error_msg);
                }
                log_error_info_given_type(initial_type);
                log_error_info_expected_type(context.expected_type.type);
            }
            else if (result.cast_type == Cast_Type::UNKNOWN) {
                semantic_analyser_set_error_flag(true);
            }
        }

        return result;
    }
    default: panic("");
    }

    return result;
}

Expression_Info* semantic_analyser_analyse_expression_any(AST::Expression* expression, Expression_Context context)
{
    auto& type_system = compiler.type_system;
    auto result = semantic_analyser_analyse_expression_internal(expression, context);
    SET_ACTIVE_EXPR_INFO(result);

    // Apply context if we are dealing with values
    if (result->result_type != Expression_Result_Type::VALUE && result->result_type != Expression_Result_Type::CONSTANT) return result;
    result->cast_info = expression_context_apply(expression_info_get_type(result, true), context, true, expression);
    return result;
}

Datatype* semantic_analyser_analyse_expression_type(AST::Expression* expression)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto result = semantic_analyser_analyse_expression_any(expression, expression_context_make_auto_dereference());
    SET_ACTIVE_EXPR_INFO(result);

    switch (result->result_type)
    {
    case Expression_Result_Type::TYPE:
        return result->options.type;
    case Expression_Result_Type::CONSTANT:
    case Expression_Result_Type::VALUE:
    {
        if (datatype_is_unknown(result->cast_info.type_afterwards)) {
            if (result->cast_info.type_afterwards->type != Datatype_Type::TEMPLATE_PARAMETER &&
                result->cast_info.type_afterwards->type != Datatype_Type::STRUCT_INSTANCE_TEMPLATE) {
                semantic_analyser_set_error_flag(true);
            }
            return result->cast_info.type_afterwards;
        }
        if (!types_are_equal(result->cast_info.type_afterwards, types.type_handle))
        {
            log_semantic_error("Expression cannot be converted to type", expression);
            log_error_info_given_type(result->cast_info.type_afterwards);
            return types.error_type;
        }

        // Otherwise try to convert to constant
        Upp_Constant constant;
        if (result->result_type == Expression_Result_Type::VALUE) {
            auto comptime_opt = expression_calculate_comptime_value(expression, "Expression is a type, but it isn't known at compile time");
            Datatype* result_type = types.error_type;
            if (comptime_opt.available) {
                constant = comptime_opt.value;
            }
            else {
                return types.error_type;
            }
        }
        else {
            constant = result->options.constant;
        }

        Datatype* final_type;
        auto type_index = upp_constant_to_value<u32>(constant);
        if (type_index >= (u32)type_system.internal_type_infos.size) {
            // Note: Always log this error, because this should never happen!
            log_semantic_error("Expression contains invalid type handle", expression);
            final_type = upcast(types.error_type);
        }
        else {
            final_type = type_system.types[type_index];
        }
        expression_info_set_type(result, final_type);
        return final_type;
    }
    case Expression_Result_Type::NOTHING: {
        log_semantic_error("Expected a type, given nothing", expression);
        log_error_info_expression_result_type(result->result_type);
        return types.error_type;
    }
    case Expression_Result_Type::POLYMORPHIC_STRUCT: {
        log_semantic_error("Expected a specific type, given polymorphic struct", expression);
        log_error_info_expression_result_type(result->result_type);
        return types.error_type;
    }
    case Expression_Result_Type::HARDCODED_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::FUNCTION: {
        log_semantic_error("Expected a type, given a function", expression);
        log_error_info_expression_result_type(result->result_type);
        return types.error_type;
    }
    default: panic("");
    }

    panic("Shouldn't happen");
    return types.error_type;
}

Datatype* semantic_analyser_analyse_expression_value(AST::Expression* expression, Expression_Context context, bool no_value_expected)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;

    auto result = semantic_analyser_analyse_expression_any(expression, context);
    SET_ACTIVE_EXPR_INFO(result);

    // Handle nothing/void
    {
        if (result->result_type == Expression_Result_Type::NOTHING && !no_value_expected) {
            log_semantic_error("Expected value from expression, but got void/nothing", expression);
            result->result_type = Expression_Result_Type::VALUE;
            result->contains_errors = true;
            result->options.value_type = compiler.type_system.predefined_types.error_type;
            context.type = Expression_Context_Type::UNKNOWN;
        }
        else if (result->result_type != Expression_Result_Type::NOTHING && !datatype_is_unknown(result->cast_info.type_afterwards) && no_value_expected) {
            log_semantic_error("Value is not used", expression);
            result->result_type = Expression_Result_Type::VALUE;
            result->contains_errors = true;
            result->options.value_type = compiler.type_system.predefined_types.error_type;
            context.type = Expression_Context_Type::UNKNOWN;
        }
        else if (result->result_type == Expression_Result_Type::NOTHING && no_value_expected) {
            // Here we set the result type to error, but we don't treat it as an error (Note: This only affects how the ir-code currently generates code)
            result->result_type = Expression_Result_Type::VALUE;
            result->contains_errors = false;
            result->options.value_type = upcast(compiler.type_system.predefined_types.empty_struct_type);
            result->cast_info.type_afterwards = upcast(compiler.type_system.predefined_types.empty_struct_type);
            context.type = Expression_Context_Type::UNKNOWN;
            result->cast_info.type_afterwards;
        }
    }

    switch (result->result_type)
    {
    case Expression_Result_Type::CONSTANT:
    case Expression_Result_Type::VALUE: {
        return result->cast_info.type_afterwards; // Here context was already applied (See analyse_expression_any), so we return
    }
    case Expression_Result_Type::TYPE: {
        expression_info_set_constant(result, types.type_handle, array_create_static_as_bytes(&result->options.type->type_handle, 1), AST::upcast(expression));
        break;
    }
    case Expression_Result_Type::FUNCTION:
    {
        // Function pointer read
        break;
    }
    case Expression_Result_Type::HARDCODED_FUNCTION:
    {
        log_semantic_error("Cannot take address of hardcoded function", expression);
        return types.error_type;
    }
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    {
        log_semantic_error("Cannot convert polymorphic function to function pointer", expression);
        return types.error_type;
    }
    case Expression_Result_Type::POLYMORPHIC_STRUCT:
    {
        log_semantic_error("Cannot convert polymorphic struct to type_handle", expression);
        return types.error_type;
    }
    case Expression_Result_Type::NOTHING: panic("Should be handled in previous code path");
    default: panic("");
    }

    result->cast_info = expression_context_apply(expression_info_get_type(result, true), context, true, expression);
    return result->cast_info.type_afterwards;
}



// OPERATOR CONTEXT
Operator_Context* symbol_table_install_new_operator_context(Symbol_Table* symbol_table)
{
    // Create new operator context
    auto context = new Operator_Context;
    dynamic_array_push_back(&semantic_analyser.allocated_operator_contexts, context);

    // Root context
    auto parent_context = symbol_table->operator_context;
    if (parent_context == 0)
    {
        context->custom_casts_polymorphic = dynamic_array_create_empty<Custom_Cast_Polymorphic>(1);
        context->custom_casts = hashtable_create_empty<Datatype_Pair, Custom_Cast>(1, datatype_pair_hash, datatype_pair_equals);
        for (int i = 0; i < AST::CONTEXT_SETTING_CAST_MODE_COUNT; i++) {
            context->cast_mode_settings[i] = Cast_Mode::EXPLICIT;
        }
        for (int i = 0; i < AST::CONTEXT_SETTING_BOOLEAN_COUNT; i++) {
            context->boolean_settings[i] = false;
        }
    }
    else
    {
        // Copy settings from parent context
        *context = *parent_context; // Shallow copy first, then fill the new hashtable
        context->custom_casts_polymorphic = dynamic_array_create_empty<Custom_Cast_Polymorphic>(parent_context->custom_casts_polymorphic.size);
        dynamic_array_append_other(&context->custom_casts_polymorphic, &parent_context->custom_casts_polymorphic);
        context->custom_casts = hashtable_create_empty<Datatype_Pair, Custom_Cast>(parent_context->custom_casts.element_count, datatype_pair_hash, datatype_pair_equals);
        {
            auto iter = hashtable_iterator_create(&parent_context->custom_casts);
            while (hashtable_iterator_has_next(&iter)) {
                hashtable_insert_element(&context->custom_casts, *iter.key, *iter.value);
                hashtable_iterator_next(&iter);
            }
        }
    }

    context->workload = 0;
    symbol_table->operator_context = context;
    return context;
}

struct Operator_Context_Value
{
    bool is_cast_mode;
    union {
        Cast_Mode* cast_mode;
        bool* bool_value;
    } options;
};

Operator_Context_Value operator_context_value_from_setting(Operator_Context* context, AST::Context_Setting setting)
{
    Operator_Context_Value result;
    if ((int)setting < AST::CONTEXT_SETTING_CAST_MODE_COUNT) {
        result.is_cast_mode = true;
        result.options.cast_mode = &context->cast_mode_settings[(int)setting];
    }
    else {
        result.is_cast_mode = false;
        result.options.bool_value = &context->boolean_settings[(int)setting - AST::CONTEXT_SETTING_CAST_MODE_COUNT];
    }
    return result;
}

void analyse_operator_context_changes(Dynamic_Array<AST::Context_Change*> context_changes, Operator_Context* context)
{
    auto& ids = compiler.predefined_ids;
    auto& types = compiler.type_system.predefined_types;

    for (int i = 0; i < context_changes.size; i++)
    {
        auto change_node = context_changes[i];
        auto info = get_info(change_node, true);
        info->is_valid_for_import = false;

        switch (change_node->type)
        {
        case AST::Context_Change_Type::IMPORT_CONTEXT:
        {
            auto& path = change_node->options.context_import_path;
            auto symbol = path_lookup_resolve_to_single_symbol(path, true);

            // Check if symbol is module
            if (symbol->type != Symbol_Type::MODULE)
            {
                if (symbol->type == Symbol_Type::ERROR_SYMBOL) {
                    continue;
                }
                log_semantic_error("Operator context import requires a module to be passed, but other symbol was specified", upcast(path));
                log_error_info_symbol(symbol);
                continue;
            }

            // Wait for other context changes to finish (So we can copy it's results)
            auto module_analysis = symbol->options.module_progress->module_analysis;
            {
                bool dependency_failed = false;
                analysis_workload_add_dependency_internal(
                    semantic_analyser.current_workload, upcast(module_analysis),
                    dependency_failure_info_make(&dependency_failed, change_node->options.context_import_path->last)
                );
                workload_executer_wait_for_dependency_resolution();
                if (dependency_failed) {
                    continue; // If there were cyclic dependencies, just skip importing the options
                }
            }

            auto other_context = module_analysis->symbol_table->operator_context;
            if (other_context->workload != 0)
            {
                bool dependency_failed = false;
                analysis_workload_add_dependency_internal(
                    semantic_analyser.current_workload,
                    upcast(other_context->workload),
                    dependency_failure_info_make(&dependency_failed, change_node->options.context_import_path->last)
                );
                workload_executer_wait_for_dependency_resolution();
                if (dependency_failed) {
                    continue; // If there were cyclic dependencies, just skip importing the options
                }
            }


            // Skip imports if there are no imports in that module
            if (module_analysis->module_node->context_changes.size == 0) {
                continue;
            }


            // Apply all changes from other module here
            for (int j = 0; j < module_analysis->module_node->context_changes.size; j++)
            {
                auto other_change = module_analysis->module_node->context_changes[j];
                auto other_change_info = pass_get_node_info(other_context->workload->base.current_pass, other_change, Info_Query::READ_NOT_NULL);
                if (!other_change_info->is_valid_for_import) {
                    continue;
                }

                switch (other_change->type)
                {
                case AST::Context_Change_Type::CONTEXT_FUNCTION_CALL: {
                    if (other_change_info->is_polymorphic_custom_cast) {
                        dynamic_array_push_back(&context->custom_casts_polymorphic, other_context->custom_casts_polymorphic[other_change_info->options.polymorphic_cast_index]);
                    }
                    else {
                        Custom_Cast* cast = hashtable_find_element(&other_context->custom_casts, other_change_info->options.custom_cast_pair);
                        assert(cast != 0, "Must have been inserted, on error the info should indicate that it's not valid for import");
                        hashtable_insert_element(&context->custom_casts, other_change_info->options.custom_cast_pair, *cast);
                    }
                    break;
                }
                case AST::Context_Change_Type::SETTING_CHANGE: 
                {
                    if (other_change->options.change.is_invalid) {
                        continue;
                    }
                    Operator_Context_Value copy_to_value = operator_context_value_from_setting(context, other_change->options.change.setting);
                    Operator_Context_Value copy_from_value = operator_context_value_from_setting(other_context, other_change->options.change.setting);
                    assert(copy_to_value.is_cast_mode == copy_from_value.is_cast_mode, "");

                    if (copy_to_value.is_cast_mode) {
                        *copy_to_value.options.cast_mode = *copy_from_value.options.cast_mode;
                    }
                    else {
                        *copy_to_value.options.bool_value = *copy_from_value.options.bool_value;
                    }
                    break;
                }
                case AST::Context_Change_Type::IMPORT_CONTEXT:
                    // Context imports are somewhat not transitive (e.g. since setting-change imports use the last value set, this could have been set by a import...)
                    break;
                default: panic("");
                }

            }

            break;
        }
        case AST::Context_Change_Type::SETTING_CHANGE:
        {
            // Check if cast identifier is valid
            auto& setting = change_node->options.change;
            if (setting.is_invalid) {
                log_semantic_error("Invalid name for operator context change", upcast(change_node), Parser::Section::IDENTIFIER);
                continue;
            }

            // Analyse setting value
            Operator_Context_Value value_to_change = operator_context_value_from_setting(context, setting.setting);
            auto expr = setting.expression;
            if (value_to_change.is_cast_mode)
            {
                semantic_analyser_analyse_expression_value(expr, expression_context_make_specific_type(upcast(types.cast_mode)));
                auto result = expression_calculate_comptime_value(expr, "Cast_Mode has to be comptime known");
                if (!result.available) {
                    continue;
                }

                // Check if enum value is valid
                Cast_Mode cast_mode = upp_constant_to_value<Cast_Mode>(result.value);
                if ((int)cast_mode <= 0 || (int)cast_mode > (int)Cast_Mode::NONE) {
                    log_semantic_error("Cast mode value is invalid", expr);
                    continue;
                }

                // Store value
                *value_to_change.options.cast_mode = cast_mode;
            }
            else
            {
                semantic_analyser_analyse_expression_value(expr, expression_context_make_specific_type(upcast(types.bool_type)));
                auto result = expression_calculate_comptime_value(expr, "Operator context value has to be comptime known");
                if (!result.available) {
                    continue;
                }

                bool bool_value = upp_constant_to_value<bool>(result.value);
                *value_to_change.options.bool_value = bool_value;
            }

            info->is_valid_for_import = true;
            break;
        }
        case AST::Context_Change_Type::CONTEXT_FUNCTION_CALL:
        {
            // Syntax: context add_custom_cast(cast_function = fn, cast_mode = Cast_Mode.IMPLICIT)
            auto& context_call = change_node->options.call;
            auto& arguments = context_call.arguments;

            if (context_call.function == AST::Context_Function::ADD_CUSTOM_CAST)
            {
                // Check for general argument errors, and find corresponding arguments
                AST::Expression* function_argument_node = nullptr;
                AST::Expression* cast_mode_argument_node = nullptr;
                {
                    int unnamed_argument_count = 0;
                    if (arguments_check_for_naming_errors(arguments, &unnamed_argument_count)) {
                        arguments_analyse_in_unknown_context(arguments);
                        break;
                    }

                    Callable callable;
                    callable.type = Callable_Type::FUNCTION_POINTER;
                    callable.only_named_arguments_after_index = 2;
                    callable.parameter_count = 2;
                    callable.options.function_pointer_type = types.type_add_custom_cast;
                    if (!arguments_match_to_parameters(arguments, callable, unnamed_argument_count, true, upcast(change_node), Parser::Section::ENCLOSURE)) {
                        break;
                    }

                    assert(arguments.size == 2, "Should be true if previous check worked");
                    AST::Expression* ordered_arguments[2];
                    ordered_arguments[get_info(arguments[0], false)->argument_index] = arguments[0]->value;
                    ordered_arguments[get_info(arguments[1], false)->argument_index] = arguments[1]->value;
                    function_argument_node = ordered_arguments[0];
                    cast_mode_argument_node = ordered_arguments[1];
                }

                // Analyse cast mode
                Cast_Mode cast_mode = Cast_Mode::NONE;
                bool errors_occured = false;
                {
                    semantic_analyser_analyse_expression_value(cast_mode_argument_node, expression_context_make_specific_type(upcast(types.cast_mode)));
                    auto result = expression_calculate_comptime_value(cast_mode_argument_node, "Cast_Mode has to be comptime known in custom cast");
                    if (result.available) {
                        // Check if enum value is valid
                        cast_mode = upp_constant_to_value<Cast_Mode>(result.value);
                        if ((int)cast_mode <= 0 || (int)cast_mode > (int)Cast_Mode::NONE) {
                            log_semantic_error("Cast mode value is invalid", cast_mode_argument_node);
                            errors_occured = true;
                            cast_mode = Cast_Mode::NONE;
                        }
                    }
                    else {
                        errors_occured = true;
                    }

                    if (!errors_occured && cast_mode == Cast_Mode::NONE) {
                        log_semantic_error("Using Cast_Mode::NONE for custom casts does nothing", cast_mode_argument_node);
                        errors_occured = true;
                    }
                }

                // Analyse function expression
                Expression_Info* expr_info = semantic_analyser_analyse_expression_any(function_argument_node, expression_context_make_unknown());
                switch (expr_info->result_type)
                {
                case Expression_Result_Type::FUNCTION:
                {
                    ModTree_Function* function = expr_info->options.function;

                    // Check if function-signature is valid for custom casts
                    Datatype_Pair pair;
                    {
                        auto signature = function->signature;
                        if (signature->parameters.size != 1) {
                            log_semantic_error("For custom casts, the function signature must only have one argument", function_argument_node);
                            break;
                        }
                        pair.from = signature->parameters[0].type;

                        if (!signature->return_type.available) {
                            log_semantic_error("For custom casts, the function signature must have a return-type", function_argument_node);
                            break;
                        }
                        pair.to = signature->return_type.value;
                    }

                    // Add this cast to operator context if it doesn't already exist
                    if (errors_occured) {
                        break;
                    }
                    Custom_Cast custom_cast;
                    custom_cast.cast_mode = cast_mode;
                    custom_cast.function = function;
                    bool worked = hashtable_insert_element(&context->custom_casts, pair, custom_cast);
                    if (!worked) {
                        log_semantic_error("Custom cast already exists for given types", function_argument_node);
                        log_error_info_given_type(pair.from);
                        log_error_info_given_type(pair.to);
                    }
                    else {
                        info->is_valid_for_import = true;
                        info->is_polymorphic_custom_cast = false;
                        info->options.custom_cast_pair = pair;
                    }
                    break;
                }
                case Expression_Result_Type::POLYMORPHIC_FUNCTION:
                {
                    // Check if poly-function has valid signature
                    auto poly_function = expr_info->options.polymorphic_function.base->base_info;
                    Datatype* param_type = 0;
                    {
                        if (poly_function.return_type_index == -1) {
                            log_semantic_error("Custom cast requires function to have return type", function_argument_node);
                            break;
                        }
                        auto& return_value = poly_function.parameters[poly_function.return_type_index];
                        bool error = false;
                        for (int j = 0; j < poly_function.implicit_parameter_infos.size; j++) {
                            auto& implicit_info = poly_function.implicit_parameter_infos[j];
                            if (implicit_info.defined_in_parameter_index == poly_function.return_type_index) {
                                log_semantic_error("Custom cast requires poly function return type to not implicit parameters", function_argument_node);
                                error = true;
                                break;
                            }
                        }
                        if (error) {
                            break;
                        }
                        if (poly_function.parameters.size != 2) {
                            log_semantic_error("Custom cast requires function to have exactly 1 parameter", function_argument_node);
                            break;
                        }
                        auto& param = poly_function.parameters[0];
                        if (param.depends_on.size != 0) {
                            log_semantic_error("Custom cast requires polymorphic function argument to not have dependencies on return-type", function_argument_node);
                            break;
                        }
                        if (param.is_comptime) {
                            log_semantic_error("Custom cast requires polymorphic function argument to not be comptime", function_argument_node);
                            break;
                        }
                        param_type = param.infos.type;
                        if (!param_type->contains_type_template) {
                            log_semantic_error("Custom cast requires polymorphic function argument contain a type-template", function_argument_node);
                            break;
                        }
                    }

                    if (errors_occured) {
                        break;
                    }
                    Custom_Cast_Polymorphic custom_cast;
                    custom_cast.cast_mode = cast_mode;
                    custom_cast.function = expr_info->options.polymorphic_function.base;
                    custom_cast.argument_type = param_type;
                    info->is_valid_for_import = true;
                    info->is_polymorphic_custom_cast = true;
                    info->options.polymorphic_cast_index = context->custom_casts_polymorphic.size;
                    dynamic_array_push_back(&context->custom_casts_polymorphic, custom_cast);
                    break;
                }
                default: {
                    errors_occured = true;
                    log_semantic_error("For custom casts, cast_function argument must be a function", function_argument_node, Parser::Section::FIRST_TOKEN);
                    log_error_info_expression_result_type(expr_info->result_type);
                    break;
                }
                }
            }
            else {
                assert(context_call.function == AST::Context_Function::ADD_OPERATOR_OVERLOAD, "");

                // context add_operator_overload(Upp_Operator.ADDITION, custom_function, commutative=false)

                // Check for general argument errors, and find corresponding arguments
                AST::Expression* operator_argument_node = nullptr;
                AST::Expression* function_argument_node = nullptr;
                AST::Expression* commutative_argument_node = nullptr; // May stay null
                {
                    int unnamed_argument_count = 0;
                    if (arguments_check_for_naming_errors(arguments, &unnamed_argument_count)) {
                        arguments_analyse_in_unknown_context(arguments);
                        break;
                    }

                    Callable callable;
                    callable.type = Callable_Type::FUNCTION_POINTER;
                    callable.only_named_arguments_after_index = 3;
                    callable.parameter_count = 3;
                    callable.options.function_pointer_type = types.type_add_operator_overload;
                    if (!arguments_match_to_parameters(arguments, callable, unnamed_argument_count, true, upcast(change_node), Parser::Section::ENCLOSURE)) {
                        break;
                    }

                    for (int j = 0; j < arguments.size; j++) {
                        auto arg_info = get_info(arguments[j]);
                        if (arg_info->argument_index == 0) {
                            operator_argument_node = arguments[j]->value;
                        }
                        else if (arg_info->argument_index == 1) {
                            function_argument_node = arguments[j]->value;
                        }
                        else if (arg_info->argument_index == 2) {
                            commutative_argument_node = arguments[j]->value;
                        }
                        else {
                            panic("");
                        }
                    }
                }

                // Analyse operator 
                bool errors_occured = false;
                Upp_Operator overloaded_operator;
                {
                    semantic_analyser_analyse_expression_value(operator_argument_node,expression_context_make_specific_type(upcast(types.upp_operator)));
                    auto value = expression_calculate_comptime_value(operator_argument_node, "Upp_Operator must be comptime for overload");
                    if (value.available) {
                        overloaded_operator = upp_constant_to_value<Upp_Operator>(value.value);
                        if ((int)overloaded_operator <= 0 || (int)overloaded_operator >= (int)Upp_Operator::MAX_ENUM_VALUE) {
                            errors_occured = true;
                            log_semantic_error("Value for Upp_Operator was not a valid enum value", operator_argument_node);
                        }
                    }
                    else {
                        errors_occured = true;
                    }
                }

                // Analyse commutative
                bool is_commutative = false;
                if (commutative_argument_node != 0)
                {
                    semantic_analyser_analyse_expression_value(commutative_argument_node, expression_context_make_specific_type(upcast(types.bool_type)));
                    auto value = expression_calculate_comptime_value(operator_argument_node, "'Commutative' argument must be comptime for overload");
                    if (value.available) {
                        is_commutative = upp_constant_to_value<bool>(value.value);
                    }
                    else {
                        errors_occured = true;
                    }
                }

                // Analyse function argument
                Expression_Info* expr_info = semantic_analyser_analyse_expression_any(function_argument_node, expression_context_make_unknown());
                switch (expr_info->result_type)
                {
                case Expression_Result_Type::FUNCTION:
                {
                    ModTree_Function* function = expr_info->options.function;

                    // Check if function-signature is valid for custom casts
                    Datatype_Pair pair;
                    {
                        auto signature = function->signature;
                        if (signature->parameters.size != 1) {
                            log_semantic_error("For custom casts, the function signature must only have one argument", function_argument_node);
                            break;
                        }
                        pair.from = signature->parameters[0].type;

                        if (!signature->return_type.available) {
                            log_semantic_error("For custom casts, the function signature must have a return-type", function_argument_node);
                            break;
                        }
                        pair.to = signature->return_type.value;
                    }

                    // Add this cast to operator context if it doesn't already exist
                    if (errors_occured) {
                        break;
                    }
                    Custom_Cast custom_cast;
                    custom_cast.cast_mode = cast_mode;
                    custom_cast.function = function;
                    bool worked = hashtable_insert_element(&context->custom_casts, pair, custom_cast);
                    if (!worked) {
                        log_semantic_error("Custom cast already exists for given types", function_argument_node);
                        log_error_info_given_type(pair.from);
                        log_error_info_given_type(pair.to);
                    }
                    else {
                        info->is_valid_for_import = true;
                        info->is_polymorphic_custom_cast = false;
                        info->options.custom_cast_pair = pair;
                    }
                    break;
                }
                default: 
                    errors_occured = true;
                    log_semantic_error("For custom casts, cast_function argument must be a function", function_argument_node, Parser::Section::FIRST_TOKEN);
                    log_error_info_expression_result_type(expr_info->result_type);
                    break;
                }
            }

            break;
        }
        default: panic("");
        }
    }
}



// STATEMENTS
bool code_block_is_while(AST::Code_Block* block)
{
    if (block != 0 && block->base.parent->type == AST::Node_Type::STATEMENT) {
        auto parent = (AST::Statement*) block->base.parent;
        return parent->type == AST::Statement_Type::WHILE_STATEMENT;
    }
    return false;
}

bool code_block_is_defer(AST::Code_Block* block)
{
    if (block != 0 && block->base.parent->type == AST::Node_Type::STATEMENT) {
        auto parent = (AST::Statement*) block->base.parent;
        return parent->type == AST::Statement_Type::DEFER;
    }
    return false;
}

bool inside_defer()
{
    // TODO: Probably doesn't work inside a bake!
    assert(semantic_analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_BODY ||
        semantic_analyser.current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS, "Must be in body otherwise no workload exists");
    auto& block_stack = semantic_analyser.current_workload->block_stack;
    for (int i = block_stack.size - 1; i > 0; i--)
    {
        auto block = block_stack[i];
        if (code_block_is_defer(block)) {
            return true;
        }
    }
    return false;
}

Control_Flow semantic_analyser_analyse_statement(AST::Statement* statement)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto& analyser = semantic_analyser;
    auto info = get_info(statement, true);
    info->flow = Control_Flow::SEQUENTIAL;
#define EXIT(flow_result) { info->flow = flow_result; return flow_result; };

    switch (statement->type)
    {
    case AST::Statement_Type::RETURN_STATEMENT:
    {
        auto& return_stat = statement->options.return_value;
        ModTree_Function* current_function = semantic_analyser.current_workload->current_function;
        assert(current_function != 0, "No statements outside of function body");
        Optional<Datatype*> expected_return_type = optional_make_failure<Datatype*>();
        if (current_function->signature != 0) {
            expected_return_type = current_function->signature->return_type;
        }
        if (analyser.current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS) {
            auto bake_progress = downcast<Workload_Bake_Analysis>(analyser.current_workload)->progress;
            if (bake_progress->result_type != 0) {
                expected_return_type = optional_make_success(bake_progress->result_type);
            }
        }

        if (return_stat.available)
        {
            Expression_Context context = expression_context_make_unknown();
            if (expected_return_type.available) {
                context = expression_context_make_specific_type(expected_return_type.value);
            }

            auto return_type = semantic_analyser_analyse_expression_value(return_stat.value, context);
            bool is_unknown = datatype_is_unknown(return_type);
            if (expected_return_type.available) {
                is_unknown = is_unknown || datatype_is_unknown(expected_return_type.value);
            }

            if (analyser.current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS)
            {
                auto bake_progress = downcast<Workload_Bake_Analysis>(analyser.current_workload)->progress;
                if (bake_progress->result_type == 0) {
                    bake_progress->result_type = return_type;
                }
            }
            else
            {
                if (!expected_return_type.available) {
                    log_semantic_error("Function does not have a return value", return_stat.value);
                }
                else if (!types_are_equal(expected_return_type.value, return_type) && !is_unknown) {
                    log_semantic_error("Return type does not match the declared return type", statement);
                    log_error_info_given_type(return_type);
                    log_error_info_expected_type(expected_return_type.value);
                }
            }
        }
        else
        {
            if (analyser.current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS) {
                log_semantic_error("Must return a value in bake", statement);
            }
            else
            {
                if (expected_return_type.available) {
                    log_semantic_error("Function requires a return value", statement);
                    log_error_info_expected_type(expected_return_type.value);
                }
            }
        }

        if (inside_defer()) {
            log_semantic_error("Cannot return in a defer block", statement, Parser::Section::KEYWORD);
        }
        EXIT(Control_Flow::RETURNS);
    }
    case AST::Statement_Type::BREAK_STATEMENT:
    case AST::Statement_Type::CONTINUE_STATEMENT:
    {
        bool is_continue = statement->type == AST::Statement_Type::CONTINUE_STATEMENT;
        String* search_id = is_continue ? statement->options.continue_name : statement->options.break_name;
        AST::Code_Block* found_block = 0;
        auto& block_stack = semantic_analyser.current_workload->block_stack;
        for (int i = block_stack.size - 1; i > 0; i--) // INFO: Block 0 is always the function body, which cannot be a target of break/continue
        {
            auto id = block_stack[i]->block_id;
            if (id.available && id.value == search_id) {
                found_block = block_stack[i];
                break;
            }
        }

        if (found_block == 0)
        {
            log_semantic_error("Label not found", statement);
            log_error_info_id(search_id);
            EXIT(Control_Flow::RETURNS);
        }
        else
        {
            info->specifics.block = found_block;
            if (is_continue && !code_block_is_while(found_block)) {
                log_semantic_error("Continue can only be used on loops", statement);
                EXIT(Control_Flow::SEQUENTIAL);
            }
        }

        if (!is_continue)
        {
            // Mark all previous Code-Blocks as Sequential flow, since they contain a path to a break
            auto& block_stack = semantic_analyser.current_workload->block_stack;
            for (int i = block_stack.size - 1; i >= 0; i--)
            {
                auto block = block_stack[i];
                auto prev = get_info(block);
                if (!prev->control_flow_locked && semantic_analyser.current_workload->statement_reachable) {
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
        semantic_analyser_analyse_block(statement->options.defer_block);
        if (inside_defer()) {
            log_semantic_error("Currently nested defers aren't allowed", statement);
            EXIT(Control_Flow::SEQUENTIAL);
        }
        EXIT(Control_Flow::SEQUENTIAL);
    }
    case AST::Statement_Type::EXPRESSION_STATEMENT:
    {
        auto& expression_node = statement->options.expression;
        if (expression_node->type != AST::Expression_Type::FUNCTION_CALL) {
            log_semantic_error("Expression statement must be a function call", statement);
        }
        // Note(Martin): This is a special case, in expression statements the expression may not have a value
        semantic_analyser_analyse_expression_value(expression_node, expression_context_make_unknown(), true);
        EXIT(Control_Flow::SEQUENTIAL);
    }
    case AST::Statement_Type::BLOCK:
    {
        auto flow = semantic_analyser_analyse_block(statement->options.block);
        EXIT(flow);
    }
    case AST::Statement_Type::IF_STATEMENT:
    {
        auto& if_node = statement->options.if_statement;

        auto condition_type = semantic_analyser_analyse_expression_value(
            if_node.condition, expression_context_make_specific_type(upcast(types.bool_type))
        );
        auto true_flow = semantic_analyser_analyse_block(statement->options.if_statement.block);
        Control_Flow false_flow;
        if (if_node.else_block.available) {
            false_flow = semantic_analyser_analyse_block(statement->options.if_statement.else_block.value);
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

        auto switch_type = semantic_analyser_analyse_expression_value(switch_node.condition, expression_context_make_auto_dereference());
        if (switch_type->type == Datatype_Type::STRUCT && downcast<Datatype_Struct>(switch_type)->struct_type == AST::Structure_Type::UNION) {
            switch_type = downcast<Datatype_Struct>(switch_type)->tag_member.type;
        }
        else if (switch_type->type != Datatype_Type::ENUM)
        {
            log_semantic_error("Switch only works on either enum or union types", switch_node.condition);
            log_error_info_given_type(switch_type);
        }

        Expression_Context case_context = switch_type->type == Datatype_Type::ENUM ?
            expression_context_make_specific_type(switch_type) : expression_context_make_unknown();
        Control_Flow switch_flow = Control_Flow::SEQUENTIAL;
        bool default_found = false;
        for (int i = 0; i < switch_node.cases.size; i++)
        {
            auto& case_node = switch_node.cases[i];
            auto case_info = get_info(case_node, true);
            case_info->is_valid = false;
            auto case_flow = semantic_analyser_analyse_block(case_node->block);
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

            if (case_node->value.available)
            {
                auto case_type = semantic_analyser_analyse_expression_value(case_node->value.value, case_context);
                // Calculate case value
                auto comptime = expression_calculate_comptime_value(case_node->value.value, "Switch case must be known at compile time");
                if (comptime.available) {
                    case_info->is_valid = true;
                    case_info->case_value = upp_constant_to_value<int>(comptime.value);
                }
                else {
                    case_info->is_valid = false;
                    case_info->case_value = -1;
                    semantic_analyser_set_error_flag(true);
                }
            }
            else
            {
                // Default case
                if (default_found) {
                    log_semantic_error("Only one default section allowed in switch", statement);
                }
                default_found = true;
            }
        }

        // Check if given cases are unique
        int unique_count = 0;
        for (int i = 0; i < switch_node.cases.size; i++)
        {
            auto case_node = switch_node.cases[i];
            auto case_info = get_info(case_node);
            if (!case_info->is_valid) continue;

            bool is_unique = true;
            for (int j = i + 1; j < statement->options.switch_statement.cases.size; j++)
            {
                auto& other_case = statement->options.switch_statement.cases[j];
                auto other_info = get_info(other_case);
                if (!other_info->is_valid) continue;
                if (case_info->case_value == other_info->case_value) {
                    log_semantic_error("Case is not unique", AST::upcast(other_case));
                    is_unique = false;
                    break;
                }
            }
            if (is_unique) {
                unique_count++;
            }
        }

        // Check if all possible cases are handled
        if (!default_found && switch_type->type == Datatype_Type::ENUM) {
            if (unique_count < downcast<Datatype_Enum>(switch_type)->members.size) {
                log_semantic_error("Not all cases are handled by switch", statement);
            }
        }
        return switch_flow;
    }
    case AST::Statement_Type::WHILE_STATEMENT:
    {
        auto& while_node = statement->options.while_statement;
        semantic_analyser_analyse_expression_value(while_node.condition, expression_context_make_specific_type(upcast(types.bool_type)));

        auto flow = semantic_analyser_analyse_block(while_node.block);
        if (flow == Control_Flow::RETURNS) {
            EXIT(flow);
        }
        EXIT(Control_Flow::SEQUENTIAL); // While is always sequential, since the condition may be wrong
    }
    case AST::Statement_Type::DELETE_STATEMENT:
    {
        auto delete_type = semantic_analyser_analyse_expression_value(statement->options.delete_expr, expression_context_make_unknown());
        if (datatype_is_unknown(delete_type)) {
            semantic_analyser_set_error_flag(true);
            EXIT(Control_Flow::SEQUENTIAL);
        }
        if (delete_type->type != Datatype_Type::POINTER && delete_type->type != Datatype_Type::SLICE) {
            log_semantic_error("Delete is only valid on pointer or slice types", statement->options.delete_expr);
            log_error_info_given_type(delete_type);
        }
        EXIT(Control_Flow::SEQUENTIAL);
    }
    case AST::Statement_Type::ASSIGNMENT:
    {
        auto& assignment_node = statement->options.assignment;
        info->specifics.is_struct_split = false;

        if (assignment_node.right_side.size == 1) // Could be Broadcast
        {
            // Analyse left side
            Datatype* left_side_type = 0;
            bool left_side_all_same_type = true;
            for (int i = 0; i < assignment_node.left_side.size; i++)
            {
                auto left = assignment_node.left_side[i];
                auto left_type = semantic_analyser_analyse_expression_value(left, expression_context_make_unknown());
                if (!expression_has_memory_address(left)) {
                    log_semantic_error("Cannot assign to a temporary value", upcast(left));
                }
                if (left_side_type == 0) {
                    left_side_type = left_type;
                }
                else if (left_side_type != left_type) {
                    left_side_all_same_type = false;
                }
            }

            // Analyse right side
            if (assignment_node.left_side.size > 1)
            {
                // Broadcast or Struct-Split
                Datatype* right_type = semantic_analyser_analyse_expression_value(assignment_node.right_side[0], expression_context_make_unknown());
                if (right_type->type == Datatype_Type::STRUCT && downcast<Datatype_Struct>(right_type)->struct_type == AST::Structure_Type::STRUCT)
                {
                    info->specifics.is_struct_split = true;
                    // Found struct split, check if all members have correct type
                    auto& members = downcast<Datatype_Struct>(right_type)->members;
                    if (members.size < assignment_node.left_side.size) {
                        log_semantic_error("Struct-Split not working, not enough members to fill all left side values", upcast(assignment_node.right_side[0]));
                    }
                    else if (members.size > assignment_node.left_side.size) {
                        log_semantic_error("More struct members then left side values!", upcast(assignment_node.right_side[0]));
                    }

                    // Check all types
                    for (int i = 0; i < assignment_node.left_side.size && i < members.size; i++) {
                        auto left_info = get_info(assignment_node.left_side[i]);
                        if (left_info->result_type != Expression_Result_Type::VALUE) {
                            continue; // This will have thrown an error earlier
                        }
                        auto left_type = expression_info_get_type(left_info, false);
                        auto member_type = members[i].type;
                        if (member_type != left_type) {
                            log_semantic_error("Struct split not working, types don't match", upcast(assignment_node.left_side[i]));
                            log_error_info_expected_type(left_type);
                            log_error_info_given_type(member_type);
                        }
                    }
                }
            }
            else {
                // Normal value assignment
                semantic_analyser_analyse_expression_value(assignment_node.right_side[0], expression_context_make_specific_type(left_side_type, true));
            }
        }
        else
        {
            for (int i = 0; i < assignment_node.left_side.size; i++) {
                auto left_node = assignment_node.left_side[i];
                auto left_type = semantic_analyser_analyse_expression_value(left_node, expression_context_make_unknown());
                if (!expression_has_memory_address(left_node)) {
                    log_semantic_error("Cannot assign to a temporary value", upcast(left_node));
                }

                if (i < assignment_node.right_side.size) {
                    semantic_analyser_analyse_expression_value(assignment_node.right_side[i], expression_context_make_specific_type(left_type, true));
                }
            }

            // Analyse missed right values
            for (int i = assignment_node.left_side.size; i < assignment_node.right_side.size; i++) {
                semantic_analyser_analyse_expression_value(assignment_node.right_side[i], expression_context_make_unknown());
                log_semantic_error("Not enough values on left side", upcast(assignment_node.right_side[i]));
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
        info->specifics.is_struct_split = false;

        // Check if this was already handled at block start
        if (definition->is_comptime) {
            EXIT(Control_Flow::SEQUENTIAL);
        }

        // Log errors if there are more symbols then values/types
        for (int i = 0; i < symbol_nodes.size; i++) {
            bool error = false;
            if (i >= types.size && types.size > 2) {
                log_semantic_error("No type exists for symbol", upcast(symbol_nodes[i]));
                error = true;
            }
            if (i >= values.size && values.size > 2) {
                log_semantic_error("No value exists for symbol", upcast(symbol_nodes[i]));
                error = true;
            }
            if (error) {
                auto symbol = get_info(symbol_nodes[i])->symbol;
                symbol->type = Symbol_Type::VARIABLE;
                symbol->options.variable_type = type_system.predefined_types.error_type;
            }
        }

        // Analyse all types
        bool types_exist = types.size != 0;
        for (int i = 0; i < types.size; i++)
        {
            auto type_expr = types[i];
            auto type = semantic_analyser_analyse_expression_type(type_expr);
            if (definition->types.size == 1) { // Broadcast type to all symbols
                for (int j = 0; j < symbol_nodes.size; j++) {
                    auto symbol = get_info(symbol_nodes[j])->symbol;
                    symbol->type = Symbol_Type::VARIABLE;
                    symbol->options.variable_type = type;
                }
            }
            else if (i < symbol_nodes.size) { // One-to-One type to symbol 
                auto symbol = get_info(symbol_nodes[i])->symbol;
                symbol->type = Symbol_Type::VARIABLE;
                symbol->options.variable_type = type;
            }
            else {
                log_semantic_error("No symbol/variable defined for this type (To many types!)", upcast(type_expr));
            }
        }

        // Analyse values
        for (int i = 0; i < values.size; i++)
        {
            auto& value_expr = values[i];
            if (values.size == 1 && symbol_nodes.size > 1)
            {
                // Broadcast/Struct-split
                auto value_type = semantic_analyser_analyse_expression_value(definition->values[0], expression_context_make_unknown());
                bool is_split = value_type->type == Datatype_Type::STRUCT && downcast<Datatype_Struct>(value_type)->struct_type == AST::Structure_Type::STRUCT;
                info->specifics.is_struct_split = is_split;

                // On split, report error for excess/missing symbols
                if (is_split) {
                    auto& members = downcast<Datatype_Struct>(value_type)->members;
                    for (int j = members.size; j < symbol_nodes.size; j++) {
                        log_semantic_error("Struct does not have enough values to fill this variable", upcast(symbol_nodes[j]));
                    }
                    if (symbol_nodes.size < members.size) {
                        log_semantic_error("More symbols are required for struct broadcast, all members need to have a symbol", upcast(value_expr));
                    }
                }

                // Check if all defined types match
                for (int j = 0; j < symbol_nodes.size; j++)
                {
                    auto symbol = get_info(symbol_nodes[j])->symbol;
                    assert(symbol->type == Symbol_Type::VARIABLE || symbol->type == Symbol_Type::VARIABLE_UNDEFINED, "");

                    // Get expected type
                    Datatype* given_type;
                    if (is_split) {
                        auto& members = downcast<Datatype_Struct>(value_type)->members;
                        if (j < members.size) {
                            given_type = members[j].type;
                        }
                        else {
                            log_semantic_error("Too many symbols/struct does not have this many members", value_expr);
                            symbol->type = Symbol_Type::VARIABLE;
                            symbol->options.variable_type = type_system.predefined_types.error_type;
                            continue;
                        }
                    }
                    else {
                        given_type = value_type;
                    }

                    // Check if definition type and expected type matches
                    if (symbol->type == Symbol_Type::VARIABLE_UNDEFINED || (symbol->type == Symbol_Type::VARIABLE && datatype_is_unknown(symbol->options.variable_type))) {
                        symbol->type = Symbol_Type::VARIABLE;
                        symbol->options.variable_type = given_type;
                    }
                    else if (symbol->options.variable_type != given_type) {
                        log_semantic_error("Value type does not match defined type!", value_expr);
                        log_error_info_given_type(given_type);
                        log_error_info_expected_type(symbol->options.variable_type);
                    }
                }
            }
            else
            {
                // Assign each value to the given symbol
                Expression_Context context = expression_context_make_unknown();
                if (i < symbol_nodes.size) {
                    auto symbol = get_info(symbol_nodes[i])->symbol;
                    if (symbol->type == Symbol_Type::VARIABLE && !datatype_is_unknown(symbol->options.variable_type)) {
                        context = expression_context_make_specific_type(symbol->options.variable_type);
                    }
                }
                else {
                    log_semantic_error("Too many expressions. No variable symbol is given for this expression", value_expr);
                }

                // Analyse value
                auto value_type = semantic_analyser_analyse_expression_value(value_expr, context);

                // Set variable type if it wasn't already set
                if (i < symbol_nodes.size) {
                    auto symbol = get_info(symbol_nodes[i])->symbol;
                    if (symbol->type == Symbol_Type::VARIABLE_UNDEFINED) {
                        symbol->type = Symbol_Type::VARIABLE;
                        symbol->options.variable_type = value_type;
                    }
                }
            }
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

Control_Flow semantic_analyser_analyse_block(AST::Code_Block* block, bool polymorphic_symbol_access)
{
    auto block_info = get_info(block, true);
    block_info->control_flow_locked = false;
    block_info->flow = Control_Flow::SEQUENTIAL;

    // Create symbol table for block
    block_info->symbol_table = symbol_table_create_with_parent(
        semantic_analyser.current_workload->current_symbol_table,
        polymorphic_symbol_access ? Symbol_Access_Level::POLYMORPHIC : Symbol_Access_Level::INTERNAL);
    RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->current_symbol_table, block_info->symbol_table);

    // Create symbols and workloads for definitions inside the block
    {
        auto progress = semantic_analyser.current_workload->current_function->progress;
        bool dont_define_comptimes = false;
        if (progress != 0) {
            dont_define_comptimes = progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
        }
        for (int i = 0; i < block->statements.size; i++) {
            if (block->statements[i]->type == AST::Statement_Type::DEFINITION) {
                auto definition = block->statements[i]->options.definition;
                if (!(definition->is_comptime && dont_define_comptimes)) {
                    analyser_create_symbol_and_workload_for_definition(definition);
                }
            }
        }
    }

    // Afterwards handle import statements (This order is the same as in modules, first the symbols get defined, then the imports are handled)
    {
        // Note: This is almost the same as in workload_entry for Module import
        Workload_Import_Resolve* last_import_workload = 0;
        for (int i = 0; i < block->statements.size; i++) {
            if (block->statements[i]->type == AST::Statement_Type::IMPORT) {
                auto import = block->statements[i]->options.import_node;
                auto import_workload = create_import_workload(import);
                if (import_workload == 0) {
                    continue;
                }
                if (last_import_workload != 0) {
                    analysis_workload_add_dependency_internal(upcast(import_workload), upcast(last_import_workload));
                }
                last_import_workload = import_workload;
            }
        }

        // If any imports were found, wait for all imports to finish before analysing block
        if (last_import_workload != 0) {
            analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(last_import_workload));
            workload_executer_wait_for_dependency_resolution();
        }
    }

    // Handle operator context after imports
    if (block->context_changes.size != 0)
    {
        auto operator_context = symbol_table_install_new_operator_context(block_info->symbol_table);
        analyse_operator_context_changes(block->context_changes, operator_context);
    }

    // Check if block id is unique
    auto& block_stack = semantic_analyser.current_workload->block_stack;
    if (block->block_id.available)
    {
        for (int i = 0; i < block_stack.size; i++) {
            auto prev = block_stack[i];
            if (prev != 0 && prev->block_id.available && prev->block_id.value == block->block_id.value) {
                log_semantic_error("Block label already in use", &block->base);
            }
        }
    }

    // Analyse statements
    int rewind_block_count = block_stack.size;
    bool rewind_reachable = semantic_analyser.current_workload->statement_reachable;
    SCOPE_EXIT(dynamic_array_rollback_to_size(&block_stack, rewind_block_count));
    SCOPE_EXIT(semantic_analyser.current_workload->statement_reachable = rewind_reachable);
    dynamic_array_push_back(&block_stack, block);
    for (int i = 0; i < block->statements.size; i++)
    {
        Control_Flow flow = semantic_analyser_analyse_statement(block->statements[i]);
        if (flow != Control_Flow::SEQUENTIAL) {
            semantic_analyser.current_workload->statement_reachable = false;
            if (!block_info->control_flow_locked) {
                block_info->flow = flow;
                block_info->control_flow_locked = true;
            }
        }
    }
    return block_info->flow;
}



// ANALYSER
void semantic_analyser_finish()
{
    auto& type_system = compiler.type_system;
    auto& ids = compiler.predefined_ids;

    // Check if main is defined
    semantic_analyser.program->main_function = 0;
    Dynamic_Array<Symbol*>* main_symbols = hashtable_find_element(&semantic_analyser.root_module->module_analysis->symbol_table->symbols, ids.main);
    if (main_symbols == 0) {
        log_semantic_error("Main function not defined", (AST::Node*)nullptr);
        return;
    }
    if (main_symbols->size > 1) {
        for (int i = 0; i < main_symbols->size; i++) {
            auto symbol = (*main_symbols)[i];
            log_semantic_error("Multiple main functions found!", symbol->definition_node);
        }
        return;
    }

    Symbol* main_symbol = (*main_symbols)[0];
    if (main_symbol->type != Symbol_Type::FUNCTION) {
        log_semantic_error("Main Symbol must be a function", main_symbol->definition_node);
        log_error_info_symbol(main_symbol);
        return;
    }
    if (!types_are_equal(upcast(main_symbol->options.function->function->signature), upcast(type_system.predefined_types.type_print_line))) {
        log_semantic_error("Main function does not have correct signature", main_symbol->definition_node);
        log_error_info_symbol(main_symbol);
        return;
    }
    semantic_analyser.program->main_function = main_symbol->options.function->function;
}

void function_progress_destroy(Function_Progress* progress) {
    if (progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
        auto& base = progress->polymorphic.base;
        polymorphic_base_info_destroy(&base.base_info);
    }
    delete progress;
}

void semantic_analyser_reset()
{
    auto& type_system = compiler.type_system;

    // Reset analyser data
    {
        semantic_analyser.root_module = 0;
        semantic_analyser.current_workload = 0;
        hashtable_reset(&semantic_analyser.valid_template_parameters);

        // Errors
        for (int i = 0; i < semantic_analyser.errors.size; i++) {
            dynamic_array_destroy(&semantic_analyser.errors[i].information);
        }
        dynamic_array_reset(&semantic_analyser.errors);

        // Allocators
        stack_allocator_reset(&semantic_analyser.comptime_value_allocator);
        stack_allocator_reset(&semantic_analyser.progress_allocator);
        stack_allocator_reset(&semantic_analyser.global_variable_memory_pool);

        // Modtree-Program
        for (int i = 0; i < semantic_analyser.allocated_function_progresses.size; i++) {
            function_progress_destroy(semantic_analyser.allocated_function_progresses[i]);
        }
        dynamic_array_reset(&semantic_analyser.allocated_function_progresses);

        for (int i = 0; i < semantic_analyser.allocated_operator_contexts.size; i++) {
            delete semantic_analyser.allocated_operator_contexts[i];
        }
        dynamic_array_reset(&semantic_analyser.allocated_operator_contexts);

        if (semantic_analyser.program != 0) {
            modtree_program_destroy();
        }
        semantic_analyser.program = modtree_program_create();

        // Workload Executer
        workload_executer_destroy();
        semantic_analyser.workload_executer = workload_executer_initialize();

        // Symbol Tables
        for (int i = 0; i < semantic_analyser.allocated_symbol_tables.size; i++) {
            symbol_table_destroy(semantic_analyser.allocated_symbol_tables[i]);
        }
        dynamic_array_reset(&semantic_analyser.allocated_symbol_tables);

        for (int i = 0; i < semantic_analyser.allocated_symbols.size; i++) {
            symbol_destroy(semantic_analyser.allocated_symbols[i]);
        }
        dynamic_array_reset(&semantic_analyser.allocated_symbols);

        // AST-Mappings
        {
            auto iter = hashtable_iterator_create(&semantic_analyser.ast_to_info_mapping);
            while (hashtable_iterator_has_next(&iter)) {
                delete* iter.value;
                hashtable_iterator_next(&iter);
            }
            hashtable_reset(&semantic_analyser.ast_to_info_mapping);
        }
        {
            auto iter = hashtable_iterator_create(&semantic_analyser.ast_to_pass_mapping);
            while (hashtable_iterator_has_next(&iter)) {
                Node_Passes* passes = iter.value;
                dynamic_array_destroy(&passes->passes);
                hashtable_iterator_next(&iter);
            }
            hashtable_reset(&semantic_analyser.ast_to_pass_mapping);
        }
        {
            for (int i = 0; i < semantic_analyser.allocated_passes.size; i++) {
                delete semantic_analyser.allocated_passes[i];
            }
            dynamic_array_reset(&semantic_analyser.allocated_passes);
        }
    }

    // Create root table and default operator context
    {
        auto root_table = symbol_table_create();
        semantic_analyser.root_symbol_table = root_table;
        symbol_table_install_new_operator_context(root_table);
    }

    // Create predefined Symbols 
    {
        auto root = semantic_analyser.root_symbol_table;
        auto pool = &compiler.identifier_pool;
        auto& symbols = semantic_analyser.predefined_symbols;
        auto& types = compiler.type_system.predefined_types;

        auto define_type_symbol = [&](const char* name, Datatype* type) -> Symbol* {
            Symbol* result = symbol_table_define_symbol(
                root, identifier_pool_add(pool, string_create_static(name)), Symbol_Type::TYPE, 0, Symbol_Access_Level::GLOBAL);
            result->options.type = type;
            if (type->type == Datatype_Type::STRUCT) {
                downcast<Datatype_Struct>(type)->name = optional_make_success(result->id);
            }
            return result;
        };
        symbols.type_bool = define_type_symbol("bool", upcast(types.bool_type));
        symbols.type_int = define_type_symbol("int", upcast(types.i32_type));
        symbols.type_float = define_type_symbol("float", upcast(types.f32_type));
        symbols.type_u8 = define_type_symbol("u8", upcast(types.u8_type));
        symbols.type_u16 = define_type_symbol("u16", upcast(types.u16_type));
        symbols.type_u32 = define_type_symbol("u32", upcast(types.u32_type));
        symbols.type_u64 = define_type_symbol("u64", upcast(types.u64_type));
        symbols.type_i8 = define_type_symbol("i8", upcast(types.i8_type));
        symbols.type_i16 = define_type_symbol("i16", upcast(types.i16_type));
        symbols.type_i32 = define_type_symbol("i32", upcast(types.i32_type));
        symbols.type_i64 = define_type_symbol("i64", upcast(types.i64_type));
        symbols.type_f32 = define_type_symbol("f32", upcast(types.f32_type));
        symbols.type_f64 = define_type_symbol("f64", upcast(types.f64_type));
        symbols.type_byte = define_type_symbol("byte", upcast(types.u8_type));
        symbols.type_string = define_type_symbol("String", upcast(types.string_type));
        symbols.type_type = define_type_symbol("Type_Handle", types.type_handle);
        symbols.type_type_information = define_type_symbol("Type_Info", upcast(types.type_information_type));
        symbols.type_any = define_type_symbol("Any", upcast(types.any_type));
        symbols.type_empty = define_type_symbol("_", upcast(types.empty_struct_type));
        symbols.type_empty = define_type_symbol("void_pointer", upcast(types.void_pointer_type));

        auto define_hardcoded_symbol = [&](const char* name, Hardcoded_Type type) -> Symbol* {
            Symbol* result = symbol_table_define_symbol(
                root, identifier_pool_add(pool, string_create_static(name)), Symbol_Type::HARDCODED_FUNCTION, 0, Symbol_Access_Level::GLOBAL);
            result->options.hardcoded = type;
            return result;
        };
        symbols.hardcoded_print_bool = define_hardcoded_symbol("print_bool", Hardcoded_Type::PRINT_BOOL);
        symbols.hardcoded_print_i32 = define_hardcoded_symbol("print_i32", Hardcoded_Type::PRINT_I32);
        symbols.hardcoded_print_f32 = define_hardcoded_symbol("print_f32", Hardcoded_Type::PRINT_F32);
        symbols.hardcoded_print_string = define_hardcoded_symbol("print_string", Hardcoded_Type::PRINT_STRING);
        symbols.hardcoded_print_line = define_hardcoded_symbol("print_line", Hardcoded_Type::PRINT_LINE);
        symbols.hardcoded_read_i32 = define_hardcoded_symbol("read_i32", Hardcoded_Type::PRINT_I32);
        symbols.hardcoded_read_f32 = define_hardcoded_symbol("read_f32", Hardcoded_Type::READ_F32);
        symbols.hardcoded_read_bool = define_hardcoded_symbol("read_bool", Hardcoded_Type::READ_BOOL);
        symbols.hardcoded_random_i32 = define_hardcoded_symbol("random_i32", Hardcoded_Type::RANDOM_I32);
        symbols.hardcoded_type_of = define_hardcoded_symbol("type_of", Hardcoded_Type::TYPE_OF);
        symbols.hardcoded_type_info = define_hardcoded_symbol("type_info", Hardcoded_Type::TYPE_INFO);
        symbols.hardcoded_assert = define_hardcoded_symbol("assert", Hardcoded_Type::ASSERT_FN);

        // NOTE: Error symbol is required so that unresolved symbol-reads can point to something,
        //       but it shouldn't be possible to reference the error symbol by name, so the 
        //       current little 'hack' is to use the identifier 0_ERROR and because it starts with a 0, it
        //       can never be used as a symbol read. Other approaches would be to have a custom symbol-table for the error symbol
        //       that isn't connected to anything.
        symbols.error_symbol = define_type_symbol("0_ERROR_SYMBOL", types.error_type);
        symbols.error_symbol->type = Symbol_Type::ERROR_SYMBOL;
    }
}

u64 ast_info_key_hash(AST_Info_Key* key) {
    return hash_combine(hash_pointer(key->base), hash_pointer(key->pass));
}

bool ast_info_equals(AST_Info_Key* a, AST_Info_Key* b) {
    return a->base == b->base && a->pass == b->pass;
}

Semantic_Analyser* semantic_analyser_initialize()
{
    semantic_analyser.errors = dynamic_array_create_empty<Semantic_Error>(64);
    semantic_analyser.comptime_value_allocator = stack_allocator_create_empty(2048);
    semantic_analyser.workload_executer = workload_executer_initialize();
    semantic_analyser.allocated_function_progresses = dynamic_array_create_empty<Function_Progress*>(1);
    semantic_analyser.allocated_operator_contexts = dynamic_array_create_empty<Operator_Context*>(1);
    semantic_analyser.progress_allocator = stack_allocator_create_empty(2048);
    semantic_analyser.global_variable_memory_pool = stack_allocator_create_empty(1024);
    semantic_analyser.program = 0;
    semantic_analyser.ast_to_pass_mapping = hashtable_create_pointer_empty<AST::Node*, Node_Passes>(1);
    semantic_analyser.symbol_lookup_visited = hashset_create_pointer_empty<Symbol_Table*>(1);
    semantic_analyser.allocated_passes = dynamic_array_create_empty<Analysis_Pass*>(1);
    semantic_analyser.ast_to_info_mapping = hashtable_create_empty<AST_Info_Key, Analysis_Info*>(1, ast_info_key_hash, ast_info_equals);
    semantic_analyser.allocated_symbol_tables = dynamic_array_create_empty<Symbol_Table*>(16);
    semantic_analyser.allocated_symbols = dynamic_array_create_empty<Symbol*>(16);
    semantic_analyser.valid_template_parameters = hashtable_create_pointer_empty<AST::Expression*, Datatype_Template_Parameter*>(1);
    return &semantic_analyser;
}

void semantic_analyser_destroy()
{
    hashset_destroy(&semantic_analyser.symbol_lookup_visited);
    hashtable_destroy(&semantic_analyser.valid_template_parameters);
    stack_allocator_destroy(&semantic_analyser.global_variable_memory_pool);

    for (int i = 0; i < semantic_analyser.errors.size; i++) {
        dynamic_array_destroy(&semantic_analyser.errors[i].information);
    }
    dynamic_array_destroy(&semantic_analyser.errors);

    for (int i = 0; i < semantic_analyser.allocated_symbol_tables.size; i++) {
        symbol_table_destroy(semantic_analyser.allocated_symbol_tables[i]);
    }
    dynamic_array_destroy(&semantic_analyser.allocated_symbol_tables);

    for (int i = 0; i < semantic_analyser.allocated_symbols.size; i++) {
        symbol_destroy(semantic_analyser.allocated_symbols[i]);
    }
    dynamic_array_destroy(&semantic_analyser.allocated_symbols);

    {
        auto iter = hashtable_iterator_create(&semantic_analyser.ast_to_info_mapping);
        while (hashtable_iterator_has_next(&iter)) {
            delete* iter.value;
            hashtable_iterator_next(&iter);
        }
        hashtable_destroy(&semantic_analyser.ast_to_info_mapping);
    }
    {
        auto iter = hashtable_iterator_create(&semantic_analyser.ast_to_pass_mapping);
        while (hashtable_iterator_has_next(&iter)) {
            Node_Passes* workloads = iter.value;
            dynamic_array_destroy(&workloads->passes);
            hashtable_iterator_next(&iter);
        }
        hashtable_destroy(&semantic_analyser.ast_to_pass_mapping);
    }
    {
        for (int i = 0; i < semantic_analyser.allocated_passes.size; i++) {
            delete semantic_analyser.allocated_passes[i];
        }
        dynamic_array_destroy(&semantic_analyser.allocated_passes);
    }

    for (int i = 0; i < semantic_analyser.allocated_function_progresses.size; i++) {
        function_progress_destroy(semantic_analyser.allocated_function_progresses[i]);
    }
    dynamic_array_destroy(&semantic_analyser.allocated_function_progresses);

    for (int i = 0; i < semantic_analyser.allocated_operator_contexts.size; i++) {
        auto context = semantic_analyser.allocated_operator_contexts[i];
        hashtable_destroy(&context->custom_casts);
        dynamic_array_destroy(&context->custom_casts_polymorphic);
        delete semantic_analyser.allocated_operator_contexts[i];
    }
    dynamic_array_destroy(&semantic_analyser.allocated_operator_contexts);

    stack_allocator_destroy(&semantic_analyser.comptime_value_allocator);
    stack_allocator_destroy(&semantic_analyser.progress_allocator);

    if (semantic_analyser.program != 0) {
        modtree_program_destroy();
    }
}



// ERRORS
void semantic_error_append_to_string(Semantic_Error e, String* string)
{
    string_append_formated(string, e.msg);

    for (int k = 0; k < e.information.size; k++)
    {
        Error_Information* info = &e.information[k];
        switch (info->type)
        {
        case Error_Information_Type::CYCLE_WORKLOAD:
            string_append_formated(string, "\n  ");
            analysis_workload_append_to_string(info->options.cycle_workload, string);
            break;
        case Error_Information_Type::COMPTIME_MESSAGE:
            string_append_formated(string, "\n  Comptime msg: %s", info->options.comptime_message);
            break;
        case Error_Information_Type::ARGUMENT_COUNT:
            string_append_formated(string, "\n  Given argument count: %d, required: %d",
                info->options.invalid_argument_count.given, info->options.invalid_argument_count.expected);
            break;
        case Error_Information_Type::MISSING_PARAMETER:
            string_append_formated(string, "\n  Missing ");
            if (info->options.parameter.name.available) {
                string_append_formated(string, "\"%s\": ", info->options.parameter.name.value->characters);
            }
            datatype_append_to_string(string, info->options.parameter.type);
            break;
        case Error_Information_Type::ID:
            string_append_formated(string, "\n  Name: %s", info->options.id->characters);
            break;
        case Error_Information_Type::INVALID_MEMBER: {
            string_append_formated(string, "\n  Accessed member name: %s", info->options.invalid_member.member_id->characters);
            string_append_formated(string, "\n  Available struct members ");
            auto& members = info->options.invalid_member.struct_signature->members;
            for (int i = 0; i < members.size; i++) {
                Struct_Member* member = &members[i];
                string_append_formated(string, "\n\t\t%s", member->id->characters);
            }
            break;
        }
        case Error_Information_Type::SYMBOL: {
            string_append_formated(string, "\n  Symbol: ");
            symbol_append_to_string(info->options.symbol, string);
            break;
        }
        case Error_Information_Type::EXIT_CODE: {
            string_append_formated(string, "\n  Exit_Code: ");
            exit_code_append_to_string(string, info->options.exit_code);
            break;
        }
        case Error_Information_Type::GIVEN_TYPE:
            string_append_formated(string, "\n  Given Type:    ");
            datatype_append_to_string(string, info->options.type);
            break;
        case Error_Information_Type::EXPECTED_TYPE:
            string_append_formated(string, "\n  Expected Type: ");
            datatype_append_to_string(string, info->options.type);
            break;
        case Error_Information_Type::FUNCTION_TYPE:
            string_append_formated(string, "\n  Function Type: ");
            datatype_append_to_string(string, info->options.type);
            break;
        case Error_Information_Type::BINARY_OP_TYPES:
            string_append_formated(string, "\n  Left Operand type:  ");
            datatype_append_to_string(string, info->options.binary_op_types.left_type);
            string_append_formated(string, "\n  Right Operand type: ");
            datatype_append_to_string(string, info->options.binary_op_types.right_type);
            break;
        case Error_Information_Type::EXPRESSION_RESULT_TYPE:
        {
            string_append_formated(string, "\nGiven: ");
            switch (info->options.expression_type)
            {
            case Expression_Result_Type::NOTHING:
                string_append_formated(string, "Nothing/void");
                break;
            case Expression_Result_Type::HARDCODED_FUNCTION:
                string_append_formated(string, "Hardcoded function");
                break;
            case Expression_Result_Type::POLYMORPHIC_FUNCTION:
                string_append_formated(string, "Polymorphic function");
                break;
            case Expression_Result_Type::POLYMORPHIC_STRUCT:
                string_append_formated(string, "Polymorphic struct");
                break;
            case Expression_Result_Type::CONSTANT:
                string_append_formated(string, "Constant");
                break;
            case Expression_Result_Type::VALUE:
                string_append_formated(string, "Value");
                break;
            case Expression_Result_Type::FUNCTION:
                string_append_formated(string, "Function");
                break;
            case Expression_Result_Type::TYPE:
                string_append_formated(string, "Type");
                break;
            default: panic("");
            }
            break;
        }
        case Error_Information_Type::CONSTANT_STATUS:
            string_append_formated(string, "\n  Couldn't serialize constant: %s", info->options.constant_message);
            break;
        default: panic("");
        }
    }
}


