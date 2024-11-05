#include "semantic_analyser.hpp"

#include "../../datastructures/string.hpp"
#include "../../utility/hash_functions.hpp"
#include "../../datastructures/hashset.hpp"
#include "../../datastructures/string.hpp"
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

// GLOBALS
bool PRINT_DEPENDENCIES = false;
bool PRINT_TIMING = false;
static Semantic_Analyser semantic_analyser;
static Workload_Executer workload_executer;

// PROTOTYPES
ModTree_Function* modtree_function_create_empty(Datatype_Function* signature, String* name);
ModTree_Function* modtree_function_create_normal(Datatype_Function* signature, Symbol* symbol, Function_Progress* progress, Symbol_Table* param_table);
void analysis_workload_destroy(Workload_Base* workload);
Expression_Info* semantic_analyser_analyse_expression_any(AST::Expression* expression, Expression_Context context);
Datatype* semantic_analyser_analyse_expression_value(AST::Expression* rc_expression, Expression_Context context, bool no_value_expected = false);
Datatype* semantic_analyser_analyse_expression_type(AST::Expression* rc_expression);
Control_Flow semantic_analyser_analyse_block(AST::Code_Block* code_block, bool polymorphic_symbol_access = false);
Expression_Cast_Info semantic_analyser_check_if_cast_possible(
    bool is_temporary_value, Datatype* source_type, Datatype* destination_type, Cast_Mode cast_mode, AST::Expression* expr = nullptr);
Cast_Mode operator_context_get_cast_mode_option(Operator_Context* context, Cast_Option option);
Expression_Cast_Info cast_info_make_empty(Datatype* initial_type, bool is_temporary_value);

Expression_Cast_Info expression_context_apply(
    Datatype* initial_type, Expression_Context context,
    bool log_errors, AST::Expression* expression = 0, Parser::Section error_section = Parser::Section::WHOLE);
void semantic_analyser_register_function_call(ModTree_Function* call_to);

void parameter_matching_info_destroy(Parameter_Matching_Info* info);
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
Operator_Context* symbol_table_install_new_operator_context_and_add_workloads(Symbol_Table* symbol_table, Dynamic_Array<AST::Context_Change*> context_changes, Workload_Base* wait_for_workload);
bool try_updating_type_mods(Expression_Cast_Info& cast_info, Type_Mods expected_mods, const char** out_error_msg = nullptr);
bool try_updating_expression_type_mods(AST::Expression* expr, Type_Mods expected_mods, const char** out_error_msg = nullptr);
void analyse_operator_context_change(AST::Context_Change* change_node, Operator_Context* context);
Custom_Operator* operator_context_query_custom_operator(Operator_Context* context, Custom_Operator_Key key);



// Up/Downcasts
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



// Helpers
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

    case Hardcoded_Type::BITWISE_NOT: return an.type_bitwise_unop;
    case Hardcoded_Type::BITWISE_AND: 
    case Hardcoded_Type::BITWISE_OR:
    case Hardcoded_Type::BITWISE_XOR:
    case Hardcoded_Type::BITWISE_SHIFT_LEFT: 
    case Hardcoded_Type::BITWISE_SHIFT_RIGHT: return an.type_bitwise_binop;
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

u64 ast_info_key_hash(AST_Info_Key* key) {
    return hash_combine(hash_pointer(key->base), hash_pointer(key->pass));
}

bool ast_info_equals(AST_Info_Key* a, AST_Info_Key* b) {
    return a->base == b->base && a->pass == b->pass;
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
    Function_Progress* base_progress = (Function_Progress*) (((byte*)base) - offsetof(Function_Progress, polymorphic.function_base));
    assert(base_progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE && base_progress->header_workload->progress == base_progress, "Testing offset magic");
    return base_progress;
}

Function_Progress* polymorphic_base_info_to_function_progress(Polymorphic_Base_Info* base_info) {
    assert(base_info->is_function, "");
    return polymorphic_function_base_to_function_progress((Polymorphic_Function_Base*)base_info);
}

Workload_Structure_Polymorphic* polymorphic_base_info_to_struct_workload(Polymorphic_Base_Info* base_info) {
    assert(!base_info->is_function, "");
    Workload_Structure_Polymorphic* workload = (Workload_Structure_Polymorphic*) (((byte*)base_info) - offsetof(Workload_Structure_Polymorphic, info));
    assert(workload->base.type == Analysis_Workload_Type::STRUCT_POLYMORPHIC, "");
    return workload;
}






// Analysis-Pass
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
            passes.passes = dynamic_array_create<Analysis_Pass*>(1);
            dynamic_array_push_back(&passes.passes, result);
            hashtable_insert_element(&semantic_analyser.ast_to_pass_mapping, mapping_node, passes);
        }
        else {
            dynamic_array_push_back(&workloads_opt->passes, result);
        }
    }
    return result;
}

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

Parameter_Matching_Info* pass_get_node_info(Analysis_Pass* pass, AST::Arguments* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->parameter_matching_info;
}

// Analysis-Info
Expression_Info* get_info(AST::Expression* expression, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, expression, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}

Case_Info* get_info(AST::Switch_Case* sw_case, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, sw_case, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
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

Parameter_Matching_Info* get_info(AST::Arguments* node, bool create = false) {
    return pass_get_node_info(semantic_analyser.current_workload->current_pass, node, create ? Info_Query::CREATE : Info_Query::READ_NOT_NULL);
}



// Error logging
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
    error.information = dynamic_array_create<Error_Information>(2);
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



// Progress/Workload creation
template<typename T>
T* workload_executer_allocate_workload(AST::Node* mapping_node, Analysis_Pass* pass=0)
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
    workload->reachable_clusters = dynamic_array_create<Workload_Base*>(1);
    workload->block_stack = dynamic_array_create<AST::Code_Block*>(1);
    if (pass != 0) {
        workload->current_pass = pass;
    }
    else {
        workload->current_pass = analysis_pass_allocate(workload, mapping_node);
    }
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

template<typename T>
T* analysis_progress_allocate_internal()
{
    auto progress = stack_allocator_allocate<T>(&semantic_analyser.progress_allocator);
    memory_zero(progress);
    return progress;
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
        parameter_table = base_progress->function->options.normal.parameter_table;
    }

    // Create progress
    auto progress = new Function_Progress;
    dynamic_array_push_back(&semantic_analyser.allocated_function_progresses, progress);
    progress->type = Polymorphic_Analysis_Type::NON_POLYMORPHIC;
    progress->function = modtree_function_create_normal(function_type, symbol, progress, parameter_table);

    // Set Symbol info
    if (symbol != 0) {
        symbol->type = Symbol_Type::FUNCTION;
        symbol->options.function = progress->function;
    }

    // Add workloads
    if (base_progress == 0) {
        progress->header_workload = workload_executer_allocate_workload<Workload_Function_Header>(upcast(function_node));
        auto header_workload = progress->header_workload;
        header_workload->progress = progress;
        header_workload->function_node = function_node;
        header_workload->base.current_symbol_table = parameter_table;
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
    progress->function->options.normal.progress = progress;

    progress->compile_workload = workload_executer_allocate_workload<Workload_Function_Cluster_Compile>(0);
    progress->compile_workload->functions = dynamic_array_create<ModTree_Function*>(1);
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

// Creates member-types and sub-types, and also checks if the member names are valid...
// Note: We want to know the subtypes so that subtype creation can check the subtypes of a non-analysed struct...
void add_struct_members_empty_recursive(
    Struct_Content* content, Dynamic_Array<AST::Structure_Member_Node*> member_nodes, bool report_errors, 
    String* parent_name, Dynamic_Array<AST::Parameter*> struct_params)
{
    assert(content->members.size == 0, "");
    assert(content->subtypes.size == 0, "");

    for (int i = 0; i < member_nodes.size; i++) 
    {
        auto member_node = member_nodes[i];
        if (member_node->is_expression) {
            struct_add_member(content, member_node->name, compiler.type_system.predefined_types.unknown_type);
        }
        else {
            auto subtype = struct_add_subtype(content, member_node->name);
            add_struct_members_empty_recursive(subtype, member_node->options.subtype_members, report_errors, content->name, struct_params);
        }

        // Check if this name is already defined on the same level
        if (report_errors)
        {
            // Check other members
            for (int j = 0; j < i; j++) {
                auto other = member_nodes[j];
                if (other->name == member_node->name) {
                    log_semantic_error("Struct member name is already defined in previous member", upcast(member_node), Parser::Section::IDENTIFIER);
                }
            }

            // Check struct params
            for (int j = 0; j < struct_params.size; j++) {
                auto param = struct_params[j];
                if (param->name == member_node->name) {
                    log_semantic_error("Struct member name is already defined as struct parameter name", upcast(member_node), Parser::Section::IDENTIFIER);
                }
            }

            // Check parent_name
            if (member_node->name == parent_name) {
                log_semantic_error("Struct member name has the same name as struct/subtype name", upcast(member_node), Parser::Section::IDENTIFIER);
            }
        }
    }
}

Workload_Structure_Body* workload_structure_create(AST::Expression* struct_node, Symbol* symbol,
    bool is_polymorphic_instance, Symbol_Access_Level access_level = Symbol_Access_Level::GLOBAL)
{
    assert(struct_node->type == AST::Expression_Type::STRUCTURE_TYPE, "Has to be struct!");
    auto& struct_info = struct_node->options.structure;

    // Create body workload
    auto body_workload = workload_executer_allocate_workload<Workload_Structure_Body>(upcast(struct_node));
    body_workload->struct_type = type_system_make_struct_empty(struct_info.type, (symbol == 0 ? compiler.predefined_ids.anon_struct : symbol->id), body_workload);
    add_struct_members_empty_recursive(
        &body_workload->struct_type->content, struct_info.members, !is_polymorphic_instance, nullptr, struct_info.parameters
    );
    if (struct_info.type == AST::Structure_Type::UNION && body_workload->struct_type->content.subtypes.size > 0 && !is_polymorphic_instance) {
        log_semantic_error("Union must not contain subtypes", upcast(struct_node), Parser::Section::KEYWORD);
    }
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
        auto poly_workload = workload_executer_allocate_workload<Workload_Structure_Polymorphic>(upcast(struct_node), body_workload->base.current_pass);
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
    progress->bake_function = modtree_function_create_empty(type_system_make_function({}), compiler.predefined_ids.bake_function);
    progress->bake_function->function_type = ModTree_Function_Type::BAKE;
    progress->bake_function->options.bake = progress;
    progress->result = optional_make_failure<Upp_Constant>();
    progress->result_type = expected_type;

    // Create workloads
    progress->analysis_workload = workload_executer_allocate_workload<Workload_Bake_Analysis>(upcast(bake_expr));
    progress->analysis_workload->bake_node = bake_expr;
    progress->analysis_workload->progress = progress;
    progress->analysis_workload->base.symbol_access_level = Symbol_Access_Level::INTERNAL;

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
void analyser_create_symbol_and_workload_for_definition(AST::Definition* definition, Workload_Base* wait_for_workload)
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

    // For local variables only symbol is defined, and analysis happens when the statement is processed
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
            if (wait_for_workload != 0) {
                analysis_workload_add_dependency_internal(workload, wait_for_workload);
            }
            return;
        }
        case AST::Expression_Type::STRUCTURE_TYPE: {
            // Note: Creating the progress also sets the symbol type
            auto workload = upcast(workload_structure_create(value, symbol, false));
            if (wait_for_workload != 0) {
                analysis_workload_add_dependency_internal(workload, wait_for_workload);
            }
            return;
        }
        default: break;
        }
    }

    // Create workload for global variables/comptime definitions
    AST::Node* mapping_node = definition->is_comptime ? upcast(definition->values[0]) : upcast(definition); // Otherwise syntax-editor will look at wrong path when looking at definition symbol
    auto definition_workload = workload_executer_allocate_workload<Workload_Definition>(mapping_node);
    definition_workload->symbol = symbol;
    definition_workload->is_extern_import = false;
    auto& def = definition_workload->options.normal;
    def.is_comptime = definition->is_comptime;
    def.assignment_type = definition->assignment_type;
    def.type_node = 0;
    if (is_global_variable && definition->types.size != 0) {
        def.type_node = definition->types[0];
    }
    def.value_node = 0;
    if (definition->values.size != 0) {
        def.value_node = definition->values[0];
    }
    if (wait_for_workload != 0) {
        analysis_workload_add_dependency_internal(upcast(definition_workload), wait_for_workload);
    }

    symbol->type = Symbol_Type::DEFINITION_UNFINISHED;
    symbol->options.definition_workload = definition_workload;
}



// MOD_TREE (Note: Doesn't set symbol to anything!)
ModTree_Function* modtree_function_create_empty(Datatype_Function* signature, String* name)
{
    ModTree_Function* function = new ModTree_Function;
    function->signature = signature;
    function->function_index_plus_one = semantic_analyser.program->functions.size + 1;
    function->name = name;

    function->called_from = dynamic_array_create<ModTree_Function*>(1);
    function->calls = dynamic_array_create<ModTree_Function*>(1);
    function->contains_errors = false;
    function->is_runnable = true;

    dynamic_array_push_back(&semantic_analyser.program->functions, function);
    return function;
}

ModTree_Function* modtree_function_create_normal(Datatype_Function* signature, Symbol* symbol, Function_Progress* progress, Symbol_Table* param_table)
{
    auto& ids = compiler.predefined_ids;

    ModTree_Function* function = modtree_function_create_empty(signature, symbol == nullptr ? ids.lambda_function : symbol->id);
    function->function_type = ModTree_Function_Type::NORMAL;
    function->options.normal.parameter_table = param_table;
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

ModTree_Global* modtree_program_add_global(Datatype* type, Symbol* symbol, bool is_extern)
{
    auto type_size = type_wait_for_size_info_to_finish(type);

    auto global = new ModTree_Global;
    global->symbol = symbol;
    global->type = type;
    global->has_initial_value = false;
    global->init_expr = 0;
    global->is_extern = is_extern;
    global->index = semantic_analyser.program->globals.size;
    global->memory = stack_allocator_allocate_size(&semantic_analyser.global_variable_memory_pool, type_size->size, type_size->alignment);
    dynamic_array_push_back(&semantic_analyser.program->globals, global);
    return global;
}

ModTree_Program* modtree_program_create()
{
    ModTree_Program* result = new ModTree_Program();
    result->main_function = 0;
    result->functions = dynamic_array_create<ModTree_Function*>(16);
    result->globals = dynamic_array_create<ModTree_Global*>(16);
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
    case Cast_Type::CUSTOM_CAST: {
        return comptime_result_make_not_comptime("Custom casts cannot be calculated at comptime");
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

Comptime_Result expression_calculate_comptime_value_without_context_cast(AST::Expression* expr);
Comptime_Result calculate_struct_initializer_comptime_recursive(Datatype* datatype, void* struct_buffer, AST::Arguments* arguments)
{
    auto& analyser = semantic_analyser;
    auto param_infos = get_info(arguments);
    assert(param_infos->call_type == Call_Type::STRUCT_INITIALIZER, "");
    auto& init_info = param_infos->options.struct_init;
    if (!init_info.valid) {
        return comptime_result_make_unavailable(datatype, "Init info was invalid");
    }

    for (int i = 0; i < param_infos->matched_parameters.size; i++) 
    {
        auto& param_info = param_infos->matched_parameters[i];
        if (!param_info.is_set) continue;
        assert(param_info.expression != 0 && param_info.argument_index >= 0, "");
        auto& member = init_info.content->members[i];

        Comptime_Result result = expression_calculate_comptime_value_internal(param_info.expression);
        if (result.type != Comptime_Result_Type::AVAILABLE) {
            return result;
        }
        memcpy(((byte*)struct_buffer) +  member.offset, result.data, result.data_type->memory_info.value.size);
    }

    for (int i = 0; i < arguments->subtype_initializers.size; i++) {
        auto initializer = arguments->subtype_initializers[i];
        Comptime_Result result = calculate_struct_initializer_comptime_recursive(datatype, struct_buffer, initializer->arguments);
        if (result.type != Comptime_Result_Type::AVAILABLE) {
            return result;
        }
    }

    return comptime_result_make_available(struct_buffer, datatype);
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
    case Expression_Result_Type::DOT_CALL: {
        return comptime_result_make_not_comptime("Dot calls must be evaluated with bake for comptime values");
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
    case AST::Expression_Type::CONST_TYPE:
    case AST::Expression_Type::MODULE:
        panic("Should be handled above!");
    case AST::Expression_Type::ERROR_EXPR: {
        return comptime_result_make_unavailable(types.unknown_type, "Analysis contained errors");
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
            return comptime_result_make_unavailable(types.unknown_type, "Analysis contained error-type");
        }
        return comptime_result_make_not_comptime("Encountered non-comptime symbol");
    }
    case AST::Expression_Type::BINARY_OPERATION:
    {
        if (info->specifics.overload.function != 0) {
            return comptime_result_make_not_comptime("Overloaded operators cannot be calculated at comptime");
        }

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
            if (info->specifics.overload.function != 0) {
                return comptime_result_make_not_comptime("Operator overloads are not comptime");
            }
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
        if (info->specifics.overload.function != 0) {
            return comptime_result_make_not_comptime("Overloaded operators are not comptime");
        }

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
    case AST::Expression_Type::MEMBER_ACCESS: 
    {
        auto& access_info = info->specifics.member_access;
        switch (access_info.type)
        {
        case Member_Access_Type::STRUCT_MEMBER_ACCESS: 
        {
            Comptime_Result value_struct = expression_calculate_comptime_value_internal(expr->options.member_access.expr);
            if (value_struct.type == Comptime_Result_Type::NOT_COMPTIME) {
                return value_struct;
            }
            else if (value_struct.type == Comptime_Result_Type::UNAVAILABLE) {
                return comptime_result_make_unavailable(result_type, value_struct.message);
            }

            assert(access_info.type == Member_Access_Type::STRUCT_MEMBER_ACCESS, "");
            byte* raw_data = (byte*)value_struct.data;
            return comptime_result_make_available(&raw_data[access_info.options.member.offset], result_type);
        }
        case Member_Access_Type::DOT_CALL:
        case Member_Access_Type::DOT_CALL_AS_MEMBER: {
            return comptime_result_make_unavailable(result_type, "Dot call member is not comptime available");
        }
        case Member_Access_Type::OPTIONAL_PTR_ACCESS: 
        {
            Datatype* opt_type = expression_info_get_type(get_info(expr->options.optional_check_value), false);
            if (access_info.options.optional_deref_count > 0) {
                return comptime_result_make_unavailable(result_type, "Cannot dereference pointer to get retrieve optional");
            }
            Comptime_Result result = expression_calculate_comptime_value_internal(expr->options.optional_check_value);
            if (result.type != Comptime_Result_Type::AVAILABLE) {
                return result;
            }

            void* ptr = *(void**)result.data;
            bool* result_bool = (bool*) stack_allocator_allocate_size(&analyser.comptime_value_allocator, 1, 1);
            *result_bool = ptr != nullptr;
            return comptime_result_make_available(result_bool, result_type);
        }
        case Member_Access_Type::STRUCT_POLYMORHPIC_PARAMETER_ACCESS: {
            assert(access_info.options.poly_access.struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE, "In instance this should already be constant");
            return comptime_result_make_unavailable(result_type, "Cannot access polymorphic parameter value in base analysis");
        }
        case Member_Access_Type::STRUCT_SUBTYPE: {
            panic("Should be handled by type_type!");
            return comptime_result_make_unavailable(result_type, "Invalid code-path!");
        }
        case Member_Access_Type::STRUCT_UP_OR_DOWNCAST: {
            Comptime_Result value_struct = expression_calculate_comptime_value_internal(expr->options.member_access.expr);
            if (value_struct.type != Comptime_Result_Type::AVAILABLE) {
                return value_struct;
            }
            return comptime_result_make_available(value_struct.data, result_type);
        }
        case Member_Access_Type::ENUM_MEMBER_ACCESS: {
            panic("Should be handled by type_type!");
            return comptime_result_make_unavailable(result_type, "Invalid code-path, should be handled by constant-path");
        }
        default: panic("");
        }

        panic("error");
        return comptime_result_make_unavailable(result_type, "error");
    }
    case AST::Expression_Type::FUNCTION_CALL: {
        return comptime_result_make_not_comptime("Function calls require #bake to be used as comtime values");
    }
    case AST::Expression_Type::INSTANCIATE: {
        return comptime_result_make_not_comptime("Instanciate must be successful to use as comptime value");
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
        auto& init_info = get_info(expr->options.struct_initializer.arguments)->options.struct_init;
        void* result_buffer = stack_allocator_allocate_size(&analyser.comptime_value_allocator, result_type_size->size, result_type_size->alignment);
        memory_set_bytes(result_buffer, result_type_size->size, 0);

        // First, set all tags to correct values
        {
            Datatype_Struct* structure = downcast<Datatype_Struct>(info->cast_info.initial_type->base_type);
            Struct_Content* content = &structure->content;
            Subtype_Index* subtype = info->cast_info.initial_type->mods.subtype_index;
            for (int i = 0; i < subtype->indices.size; i++) {
                int tag_value = subtype->indices[i].index + 1;
                assert(content->subtypes.size > 0, "");
                
                int* tag_pointer = (int*)(((byte*)result_buffer) + content->tag_member.offset);
                *tag_pointer = tag_value;
                content = content->subtypes[tag_value - 1];
            }
        }

        Comptime_Result result = calculate_struct_initializer_comptime_recursive(result_type, result_buffer, expr->options.struct_initializer.arguments);
        if (result.type != Comptime_Result_Type::AVAILABLE) {
            return result;
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
    if (info->cast_info.deref_count != 0) {
        // Cannot handle pointers for comptime currently
        return comptime_result_make_not_comptime("Pointer handling not supported by comptime values");
    }

    return comptime_result_apply_cast(result_no_context, info->cast_info.cast_type, info->cast_info.result_type);
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
        log_error_info_constant_status(result.options.error_message);
        return optional_make_failure<Upp_Constant>();
    }

    return optional_make_success(result.options.constant);
}

bool expression_has_memory_address(AST::Expression* expr) {
    return !get_info(expr)->cast_info.result_value_is_temporary;
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

Expression_Context expression_context_make_specific_type(Datatype* signature, Cast_Mode cast_mode = Cast_Mode::IMPLICIT) {
    if (datatype_is_unknown(signature)) {
        return expression_context_make_unknown(true);
    }
    Expression_Context context;
    context.type = Expression_Context_Type::SPECIFIC_TYPE_EXPECTED;
    context.expected_type.type = signature;
    context.expected_type.cast_mode = cast_mode;
    return context;
}



// Result
void expression_info_set_value(Expression_Info* info, Datatype* result_type, bool is_temporary)
{
    info->result_type = Expression_Result_Type::VALUE;
    info->options.value_type = result_type;
    info->cast_info.initial_type = result_type;
    info->cast_info.result_type = result_type;
    info->cast_info.initial_value_is_temporary = is_temporary;
    info->cast_info.result_value_is_temporary = is_temporary;
}

void expression_info_set_dot_call(Expression_Info* info, AST::Expression* first_argument, Custom_Operator* op)
{
    info->result_type = Expression_Result_Type::DOT_CALL;
    info->options.dot_call.first_argument = first_argument;
    assert(op != 0, "");
    info->options.dot_call.is_polymorphic = op->dot_call.is_polymorphic;
    if (op->dot_call.is_polymorphic) {
        info->options.dot_call.options.polymorphic.base = op->dot_call.options.poly_base;
        info->options.dot_call.options.polymorphic.instance = nullptr;
    }
    else {
        info->options.dot_call.options.function = op->dot_call.options.function;
    }
    info->cast_info.result_type = compiler.type_system.predefined_types.unknown_type;
    info->cast_info.initial_type = compiler.type_system.predefined_types.unknown_type;
    info->cast_info.initial_value_is_temporary = false;
    info->cast_info.result_value_is_temporary = false;
}

void expression_info_set_error(Expression_Info* info, Datatype* result_type)
{
    info->result_type = Expression_Result_Type::VALUE;
    info->options.value_type = result_type;
    info->cast_info.result_type = result_type;
    info->cast_info.initial_type = result_type;
    info->contains_errors = true;
    info->cast_info.initial_value_is_temporary = false;
    info->cast_info.result_value_is_temporary = false;
}

void expression_info_set_function(Expression_Info* info, ModTree_Function* function)
{
    info->result_type = Expression_Result_Type::FUNCTION;
    info->options.function = function;
    info->cast_info.result_type = upcast(function->signature);
    info->cast_info.initial_type = upcast(function->signature);
    info->cast_info.initial_value_is_temporary = true;
    info->cast_info.result_value_is_temporary = true;
}

void expression_info_set_hardcoded(Expression_Info* info, Hardcoded_Type hardcoded)
{
    info->result_type = Expression_Result_Type::HARDCODED_FUNCTION;
    info->options.hardcoded = hardcoded;
    info->cast_info.result_type = upcast(hardcoded_type_to_signature(hardcoded));
    info->cast_info.initial_type = info->cast_info.result_type;
    info->cast_info.initial_value_is_temporary = true;
    info->cast_info.result_value_is_temporary = true;
}

void expression_info_set_type(Expression_Info* info, Datatype* type)
{
    info->result_type = Expression_Result_Type::TYPE;
    info->options.type = type;
    info->cast_info.result_type = compiler.type_system.predefined_types.type_handle;
    info->cast_info.initial_type = info->cast_info.result_type;
    info->cast_info.initial_value_is_temporary = true;
    info->cast_info.result_value_is_temporary = true;

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
    info->options.type = compiler.type_system.predefined_types.unknown_type;
    info->cast_info.result_type = compiler.type_system.predefined_types.unknown_type;
    info->cast_info.initial_type = info->cast_info.result_type;
    info->cast_info.initial_value_is_temporary = true;
    info->cast_info.result_value_is_temporary = true;
}

void expression_info_set_polymorphic_function(Expression_Info* info, Polymorphic_Function_Base* poly_base, ModTree_Function* instance = 0) {
    info->result_type = Expression_Result_Type::POLYMORPHIC_FUNCTION;
    info->options.polymorphic_function.base = poly_base;
    info->options.polymorphic_function.instance_fn = instance;
    info->cast_info.result_type = compiler.type_system.predefined_types.unknown_type; // Not sure if this is correct for function pointers, but should be fine
    info->cast_info.initial_type = info->cast_info.result_type;
    info->cast_info.initial_value_is_temporary = true;
    info->cast_info.result_value_is_temporary = true;
}

void expression_info_set_constant(Expression_Info* info, Upp_Constant constant) {
    info->result_type = Expression_Result_Type::CONSTANT;
    info->options.constant = constant;
    info->cast_info.result_type = constant.type;
    info->cast_info.initial_type = info->cast_info.result_type;
    info->cast_info.initial_value_is_temporary = true;
    info->cast_info.result_value_is_temporary = true;
}

void expression_info_set_constant(Expression_Info* info, Datatype* signature, Array<byte> bytes, AST::Node* error_report_node)
{
    auto& analyser = semantic_analyser;
    auto result = constant_pool_add_constant(signature, bytes);
    if (!result.success)
    {
        assert(error_report_node != 0, "Error"); // Error report node may only be null if we know that adding the constant cannot fail.
        log_semantic_error("Value cannot be converted to constant value (Not serializable)", error_report_node);
        log_error_info_constant_status(result.options.error_message);
        expression_info_set_error(info, signature);
        return;
    }
    expression_info_set_constant(info, result.options.constant);
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
    if (before_context_is_applied) {
        return info->cast_info.initial_type;
    }
    return info->cast_info.result_type;
}



// SYMBOL/PATH LOOKUP

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
    auto results = dynamic_array_create<Symbol*>(1);
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

Workload_Executer* workload_executer_initialize()
{
    workload_executer.all_workloads = dynamic_array_create<Workload_Base*>(8);
    workload_executer.runnable_workloads = dynamic_array_create<Workload_Base*>(8);
    workload_executer.finished_workloads = dynamic_array_create<Workload_Base*>(8);
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
        info.fail_indicators = dynamic_array_create<Dependency_Failure_Info>(1);
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
    Dynamic_Array<Workload_Base*> workloads_to_merge = dynamic_array_create<Workload_Base*>(1);
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
        string_append(string, "Module analysis ");
        string_append(string, module->progress->symbol == 0 ? "ROOT" : module->progress->symbol->id->characters);
        break;
    }
    case Analysis_Workload_Type::OPERATOR_CONTEXT_CHANGE: {
        auto change = downcast<Workload_Operator_Context_Change>(workload);
        string_append(string, "Operator Context Change Type: ");
        AST::context_change_type_append_to_string(change->context_type_to_analyse, string);
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
        auto struct_id = downcast<Workload_Structure_Body>(workload)->struct_type->content.name->characters;
        string_append_formated(string, "Struct-Analysis \"%s\"", struct_id);
        break;
    }
    case Analysis_Workload_Type::STRUCT_POLYMORPHIC: {
        auto struct_id = downcast<Workload_Structure_Polymorphic>(workload)->body_workload->struct_type->content.name->characters;
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



// PARAMETER MATCHING/Overloading
Parameter_Matching_Info parameter_matching_info_create_empty(Call_Type call_type = (Call_Type)-1, int expected_parameter_count = 0) {
    Parameter_Matching_Info info;
    info.call_type = call_type;
    info.arguments = nullptr;
    info.matched_parameters = dynamic_array_create<Parameter_Match>(expected_parameter_count);
    info.has_return_value = false;
    info.return_type = compiler.type_system.predefined_types.unknown_type;
    return info;
}

void parameter_matching_info_destroy(Parameter_Matching_Info* info) {
    dynamic_array_destroy(&info->matched_parameters);
}

void parameter_matching_info_add_param(Parameter_Matching_Info* info, String* name, bool required, bool requires_named_addressing, Datatype* param_type) {
    Parameter_Match match;
    match.name = name;
    match.required = required;
    match.requires_named_addressing = requires_named_addressing;
    match.param_type = param_type;

    match.is_set = false;
    match.argument_index = -1;
    match.expression = nullptr;
    match.argument_type = compiler.type_system.predefined_types.unknown_type;
    match.argument_is_temporary_value = false;

    match.state = Parameter_State::NOT_ANALYSED;
    match.reanalyse_param_type_flag = false;
    match.ignore_during_code_generation = false;
    dynamic_array_push_back(&info->matched_parameters, match);
}

void parameter_matching_info_add_analysed_param(Parameter_Matching_Info* info, AST::Expression* expr)
{
    auto expr_info = get_info(expr);
    parameter_matching_info_add_param(info, compiler.predefined_ids.value, true, false, nullptr);
    auto& param_info = info->matched_parameters[info->matched_parameters.size - 1];
    param_info.state = Parameter_State::ANALYSED;
    param_info.is_set = true;
    param_info.expression = expr;
    param_info.argument_type = expr_info->cast_info.result_type;
    param_info.argument_is_temporary_value = expr_info->cast_info.result_value_is_temporary;
}

void parameter_matching_info_add_unanalysed_param(Parameter_Matching_Info* info,AST::Expression* expr)
{
    parameter_matching_info_add_param(info, compiler.predefined_ids.value, true, false, nullptr);
    auto& param_info = info->matched_parameters[info->matched_parameters.size - 1];
    param_info.is_set = true;
    param_info.expression = expr;
}

void parameter_matching_info_add_only_type_param(Parameter_Matching_Info* info, Datatype* argument_type, bool is_temporary)
{
    parameter_matching_info_add_param(info, compiler.predefined_ids.value, true, false, nullptr);
    auto& param_info = info->matched_parameters[info->matched_parameters.size - 1];
    param_info.is_set = true;
    param_info.expression = nullptr;
    param_info.argument_type = argument_type;
    param_info.argument_is_temporary_value = is_temporary;
}

Datatype* analyse_parameter_if_not_already_done(Parameter_Match* info, Expression_Context context)
{
    if (info->state == Parameter_State::ANALYSED || !info->is_set) {
        return info->argument_type;
    }

    info->state = Parameter_State::ANALYSED;
    info->argument_type = semantic_analyser_analyse_expression_value(info->expression, context);
    info->argument_is_temporary_value = get_info(info->expression)->cast_info.initial_value_is_temporary;

    return info->argument_type;
}

void analyse_arguments_in_unknown_context(AST::Arguments* arguments) {
    for (int i = 0; i < arguments->arguments.size; i++) {
        auto& expr = arguments->arguments[i]->value;
        auto info = pass_get_node_info(semantic_analyser.current_workload->current_pass, expr, Info_Query::TRY_READ);
        if (info == 0) {
            semantic_analyser_analyse_expression_value(expr, expression_context_make_unknown());
        }
    }
    for (int i = 0; i < arguments->subtype_initializers.size; i++) {
        analyse_arguments_in_unknown_context(arguments->subtype_initializers[i]->arguments);
    }
}

void parameter_matching_analyse_in_unknown_context(Parameter_Matching_Info* matching_info)
{
    for (int i = 0; i < matching_info->matched_parameters.size; i++) {
        auto& param_info = matching_info->matched_parameters[i];
        if (param_info.state == Parameter_State::NOT_ANALYSED && param_info.expression != 0) {
            Expression_Context context = expression_context_make_unknown();
            if (param_info.param_type != 0) {
                context = expression_context_make_specific_type(param_info.param_type);
            }
            semantic_analyser_analyse_expression_value(param_info.expression, context);
        }
    }

    // Analyse arguments that weren't matched to parameters...
    if (matching_info->arguments != 0) {
        analyse_arguments_in_unknown_context(matching_info->arguments);
    }
}

// Parameter matching
struct Overload_Candidate
{
    Parameter_Matching_Info matching_info;

    // Source info
    Symbol* symbol; // May be null
    Expression_Info expression_info; // May be empty, required to set correct expression info after overload resolution

    // For convenience when resolving overloads
    Datatype* active_type;
    bool overloading_arg_matches_type;
    bool overloading_arg_type_mods_compatible;
    bool overloading_arg_can_be_cast;
};

Optional<Overload_Candidate> overload_candidate_try_create_from_expression_info(Expression_Info& info, AST::Expression* expr, Symbol* origin_symbol = 0)
{
    Overload_Candidate candidate;
    candidate.symbol = origin_symbol;
    candidate.expression_info = info; // Copies info (Question: Why?)
    candidate.matching_info = parameter_matching_info_create_empty();

    Polymorphic_Base_Info* poly_base_info = nullptr;
    Datatype_Function* function_type = nullptr;
    bool is_dotcall = false;

    // Figure out call type
    switch (info.result_type)
    {
    case Expression_Result_Type::NOTHING:
    case Expression_Result_Type::TYPE: {
        return optional_make_failure<Overload_Candidate>();
    }
    case Expression_Result_Type::POLYMORPHIC_STRUCT: {
        candidate.matching_info.call_type = Call_Type::POLYMORPHIC_STRUCT;
        candidate.matching_info.options.poly_struct= info.options.polymorphic_struct;
        poly_base_info = &info.options.polymorphic_struct->info;
        break;
    }
    case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
        candidate.matching_info.call_type = Call_Type::POLYMORPHIC_FUNCTION;
        candidate.matching_info.options.poly_function = info.options.polymorphic_function.base;
        poly_base_info = &info.options.polymorphic_function.base->base;
        break;
    }
    case Expression_Result_Type::FUNCTION: {
        candidate.matching_info.call_type = Call_Type::FUNCTION;
        candidate.matching_info.options.function = info.options.function;
        function_type = info.options.function->signature;
        break;
    }
    case Expression_Result_Type::CONSTANT: {
        auto& constant = info.options.constant;
        auto type = datatype_get_non_const_type(constant.type);
        if (type->type != Datatype_Type::FUNCTION) {
            return optional_make_failure<Overload_Candidate>();
        }

        int function_index = (int)(*(i64*)constant.memory) - 1;
        if (function_index < 0 || function_index >= semantic_analyser.program->functions.size) {
            return optional_make_failure<Overload_Candidate>();
        }

        ModTree_Function* function = semantic_analyser.program->functions[function_index];
        candidate.matching_info.call_type = Call_Type::FUNCTION;
        candidate.matching_info.options.function = function;
        function_type = function->signature;
        break;
    }
    case Expression_Result_Type::VALUE:
    {
        // TODO: Check if this is comptime known, then we dont need a function pointer call
        auto type = datatype_get_non_const_type(info.options.type);
        if (type->type != Datatype_Type::FUNCTION) {
            return optional_make_failure<Overload_Candidate>();
        }
        function_type = downcast<Datatype_Function>(type);
        if (function_type->is_optional) {
            return optional_make_failure<Overload_Candidate>();
        }
        candidate.matching_info.call_type = Call_Type::FUNCTION_POINTER;
        candidate.matching_info.options.pointer_call = function_type;
        break;
    }
    case Expression_Result_Type::HARDCODED_FUNCTION:
    {
        candidate.matching_info.call_type = Call_Type::HARDCODED;
        candidate.matching_info.options.hardcoded = info.options.hardcoded;
        function_type = hardcoded_type_to_signature(info.options.hardcoded);
        break;
    }
    case Expression_Result_Type::DOT_CALL:
    {
        auto& dot_call = info.options.dot_call;
        is_dotcall = true;
        if (dot_call.is_polymorphic) {
            candidate.matching_info.call_type = Call_Type::POLYMORPHIC_DOT_CALL;
            candidate.matching_info.options.poly_dotcall = dot_call.options.polymorphic.base;
            poly_base_info = &dot_call.options.polymorphic.base->base;
        }
        else {
            candidate.matching_info.call_type = Call_Type::DOT_CALL;
            candidate.matching_info.options.dot_call_function = info.options.dot_call.options.function;
            function_type = info.options.dot_call.options.function->signature;
        }
        break;
    }
    default: panic("");
    }

    // Add parameter matching infos
    if (function_type != 0)
    {
        for (int i = 0; i < function_type->parameters.size; i++) {
            auto param = function_type->parameters[i];
            parameter_matching_info_add_param(&candidate.matching_info, param.name, !param.default_value_exists, false, param.type);
        }
        candidate.matching_info.has_return_value = function_type->return_type.available;
        if (function_type->return_type.available) {
            candidate.matching_info.has_return_value = true;
            candidate.matching_info.return_type = function_type->return_type.value;
        }
        else {
            candidate.matching_info.has_return_value = false;
            candidate.matching_info.return_type = compiler.type_system.predefined_types.unknown_type;
        }
    }

    if (poly_base_info != 0)
    {
        int last = poly_base_info->parameters.size;
        if (poly_base_info->return_type_index != -1) {
            last -= 1;
        }
        // Add normal parameters
        for (int i = 0; i < last; i++)
        {
            auto& param = poly_base_info->parameters[i];
            bool required = true;
            Datatype* datatype = nullptr;
            if (param.depends_on.size == 0 && !param.is_comptime) {
                datatype = param.infos.type;
                required = !param.infos.default_value_exists;
            }

            parameter_matching_info_add_param(&candidate.matching_info, param.infos.name, required, false, datatype);
        }

        // Add implicit parameters
        for (int i = 0; i < poly_base_info->implicit_parameter_infos.size; i++)
        {
            auto& impl_info = poly_base_info->implicit_parameter_infos[i];
            if (impl_info.defined_in_parameter_index != poly_base_info->return_type_index) {
                candidate.matching_info.matched_parameters[impl_info.defined_in_parameter_index].param_type = nullptr;
                candidate.matching_info.matched_parameters[impl_info.defined_in_parameter_index].required = true;
            }
            parameter_matching_info_add_param(&candidate.matching_info, impl_info.id, false, true, nullptr);
        }

        // Add return type if available?
        if (poly_base_info->return_type_index != -1) 
        {
            candidate.matching_info.has_return_value = true;
            auto param = poly_base_info->parameters[poly_base_info->return_type_index];
            if (param.depends_on.size == 0 && !param.infos.type->contains_type_template) {
                candidate.matching_info.return_type = param.infos.type;
            }
            else {
                candidate.matching_info.return_type = compiler.type_system.predefined_types.unknown_type;
            }
        }
        else {
            candidate.matching_info.has_return_value = false;
            candidate.matching_info.return_type = compiler.type_system.predefined_types.unknown_type;
        }
    }

    if (is_dotcall)
    {
        assert(candidate.matching_info.matched_parameters.size > 0, "");
        auto& first = candidate.matching_info.matched_parameters[0];
        first.is_set = true;
        first.argument_index = -1;
        first.required = true;

        assert(expr->type == AST::Expression_Type::MEMBER_ACCESS, "");
        auto arg_info = get_info(expr->options.member_access.expr);
        first.state = Parameter_State::ANALYSED;
        first.param_type = expression_info_get_type(arg_info, false);
        first.expression = expr->options.member_access.expr;
        first.argument_type = arg_info->cast_info.result_type;
        first.argument_is_temporary_value = arg_info->cast_info.result_value_is_temporary;
    }

    return optional_make_success(candidate);
}

// Returns true if successfull
bool arguments_match_to_parameters(AST::Arguments* args, Parameter_Matching_Info* matching_info, bool log_errors)
{
    auto& param_infos = matching_info->matched_parameters;
    matching_info->arguments = args;


    bool is_struct_initializer = matching_info->call_type == Call_Type::STRUCT_INITIALIZER;
    if (!is_struct_initializer && log_errors) 
    {
        bool error_occured = false;
        for (int i = 0; i < args->subtype_initializers.size; i++) {
            auto sub_init = args->subtype_initializers[i];
            log_semantic_error("Subtype_initializer only valid on struct-initializers", upcast(sub_init), Parser::Section::FIRST_TOKEN);
            analyse_arguments_in_unknown_context(sub_init->arguments);
            error_occured = true;
        }
        for (int i = 0; i < args->uninitialized_tokens.size; i++) {
            auto token_expr = args->uninitialized_tokens[i];
            log_semantic_error("Uninitialized-token only valid for subtype-initializers", upcast(token_expr), Parser::Section::FIRST_TOKEN);
            error_occured = true;
        }
    }

    bool is_dot_call = matching_info->call_type == Call_Type::DOT_CALL || matching_info->call_type == Call_Type::POLYMORPHIC_DOT_CALL;
    if (is_dot_call) {
        assert(param_infos.size > 0, "");
        param_infos[0].argument_index = -1;
        param_infos[0].is_set = true;
        param_infos[0].ignore_during_code_generation = false;
        param_infos[0].state = Parameter_State::ANALYSED;
    }

    // Match arguments to parameters and check for errors
    bool argument_error_occured = false;
    bool named_argument_encountered = false;
    int unnamed_argument_count = 0;
    for (int i = 0; i < args->arguments.size; i++)
    {
        auto& arg = args->arguments[i];

        if (arg->name.available)
        {
            named_argument_encountered = true;

            // Search parameters for name
            int param_index = -1;
            for (int j = 0; j < param_infos.size; j++) {
                auto& info = param_infos[j];
                if (info.name == arg->name.value) {
                    param_index = j;
                    break;
                }
            }

            // Set parameter info if found
            if (param_index != -1)
            {
                auto& param_info = param_infos[param_index];
                if (!param_info.is_set) {
                    param_info.is_set = true;
                    param_info.argument_index = i;
                    param_info.expression = arg->value;
                }
                else {
                    if (log_errors) {
                        log_semantic_error("Argument was already specified", upcast(arg), Parser::Section::IDENTIFIER);
                    }
                    argument_error_occured = true;
                }
            }
            else {
                if (log_errors) {
                    log_semantic_error("Argument name does not match any parameter name", upcast(arg), Parser::Section::IDENTIFIER);
                }
                argument_error_occured = true;
            }
        }
        else
        {
            // Unnamed arguments
            if (named_argument_encountered) {
                if (log_errors) {
                    log_semantic_error("Unnamed arguments must not appear after named arguments!", upcast(arg));
                }
                argument_error_occured = true;
                continue;
            }

            int param_index = unnamed_argument_count + (is_dot_call ? 1 : 0);
            unnamed_argument_count += 1;
            if (param_index < param_infos.size)
            {
                auto& info = param_infos[param_index];
                if (info.requires_named_addressing) {
                    if (log_errors) {
                        log_semantic_error("This parameter requires named addressing, so argument must be named", upcast(arg));
                    }
                    argument_error_occured = true;
                }
                else {
                    info.is_set = true;
                    info.argument_index = i;
                    info.expression = arg->value;
                }
            }
            else
            {
                if (log_errors) {
                    log_semantic_error("Argument index is larger than parameter count", upcast(arg));
                }
                argument_error_occured = true;
            }
        }
    }

    // Check if all required parameters were specified
    if (!argument_error_occured && !(is_struct_initializer && args->uninitialized_tokens.size > 0))
    {
        bool missing_parameter_reported = false;
        for (int i = 0; i < param_infos.size; i++)
        {
            auto& param_info = param_infos[i];
            if (!param_info.is_set && param_info.required)
            {
                if (log_errors) {
                    if (!missing_parameter_reported) {
                        log_semantic_error("Missing parameters", upcast(args), Parser::Section::ENCLOSURE);
                        missing_parameter_reported = true;
                    }
                    log_error_info_id(param_info.name);
                }
                argument_error_occured = true;
            }
        }
    }

    return !argument_error_occured;
}






// POLYMORPHIC HEADER PARSING
struct Parameter_Symbol_Lookup
{
    int defined_in_parameter_index;
    String* id;
};

bool check_if_expression_contains_implicit_parameters(AST::Expression* expression)
{
    if (expression->type == AST::Expression_Type::TEMPLATE_PARAMETER) {
        return true;
    }

    // Check if we should stop the recursion
    switch (expression->type)
    {
    case AST::Expression_Type::BAKE_BLOCK:
    case AST::Expression_Type::BAKE_EXPR:
    case AST::Expression_Type::FUNCTION:
    case AST::Expression_Type::MODULE:
        return false;
    }

    // Otherwise search all children of the node for further implicit parameters
    int child_index = 0;
    auto child_node = AST::base_get_child(upcast(expression), child_index);
    while (child_node != 0)
    {
        if (child_node->type == AST::Node_Type::EXPRESSION) {
            if (check_if_expression_contains_implicit_parameters(downcast<AST::Expression>(child_node))) {
                return true;
            }
        }
        else if (child_node->type == AST::Node_Type::ARGUMENTS) {
            auto args = downcast<AST::Arguments>(child_node);
            for (int i = 0; i < args->arguments.size; i++) {
                check_if_expression_contains_implicit_parameters(args->arguments[i]->value);
            }
        }
        else if (child_node->type == AST::Node_Type::ARGUMENT) {
            auto argument_node = downcast<AST::Argument>(child_node);
            if (check_if_expression_contains_implicit_parameters(argument_node->value)) {
                return true;
            }
        }
        else if (child_node->type == AST::Node_Type::PARAMETER) {
            auto parameter_node = downcast<AST::Parameter>(child_node);
            if (check_if_expression_contains_implicit_parameters(parameter_node->type)) {
                return true;
            }
        }
        child_index += 1;
        child_node = AST::base_get_child(upcast(expression), child_index);
    }

    return false;
}

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
        else if (child_node->type == AST::Node_Type::ARGUMENTS) {
            auto args = downcast<AST::Arguments>(child_node);
            for (int i = 0; i < args->arguments.size; i++) {
                expression_search_for_implicit_parameters_and_symbol_lookups(defined_in_parameter_index, args->arguments[i]->value, parameter_locations, symbol_lookups);
            }
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
    parameter.name = parameter_node->name;

    // Analyse type
    parameter.type = semantic_analyser_analyse_expression_type(parameter_node->type);
    if (!parameter_node->is_comptime && !parameter_node->is_mutable) {
        parameter.type = type_system_make_constant(parameter.type);
    }

    // Check if default value exists
    parameter.default_value_exists = parameter_node->default_value.available;
    if (!parameter.default_value_exists) {
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
        parameter.default_value_exists = false;
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
    Dynamic_Array<AST::Parameter*> parameter_nodes, Symbol_Table* symbol_table, String* name, Function_Progress* progress = 0, AST::Expression* return_type = 0)
{
    auto& types = compiler.type_system.predefined_types;

    // Define parameter symbols and search for symbol-lookups and implicit parameters
    Dynamic_Array<Parameter_Symbol_Lookup> symbol_lookups = dynamic_array_create<Parameter_Symbol_Lookup>(4);
    Dynamic_Array<Implicit_Parameter_Infos> implicit_parameter_infos = dynamic_array_create<Implicit_Parameter_Infos>(1);
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
    base_info.name = name;
    assert(name != nullptr, "Should be true for non-polymorphic function");
    base_info.return_type_index = return_type_index;
    base_info.return_type_node = return_type;
    base_info.parameter_nodes = parameter_nodes;
    base_info.symbol_table = symbol_table;
    base_info.parameters = array_create<Polymorphic_Parameter>(parameter_nodes.size + (has_return_type ? 1 : 0));
    base_info.base_parameter_values = array_create<Polymorphic_Value>(polymorphic_value_count);
    base_info.instances = dynamic_array_create<Polymorphic_Instance_Info>(1);
    base_info.implicit_parameter_infos = implicit_parameter_infos;
    base_info.is_function = progress != 0;
    delete_implicit_parameters = false;

    // Initialize polymorphic parameter values to error type
    for (int i = 0; i < base_info.base_parameter_values.size; i++) {
        base_info.base_parameter_values[i] = polymorphic_value_make_type(types.unknown_type);
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
        parameter.depends_on = dynamic_array_create<int>();
        parameter.dependees = dynamic_array_create<int>();
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
    Dynamic_Array<Symbol*> symbols = dynamic_array_create<Symbol*>(1);
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

            dynamic_array_push_back(&base_info.parameters[lookup.defined_in_parameter_index].depends_on, depends_on_index);
            dynamic_array_push_back(&base_info.parameters[depends_on_index].dependees, lookup.defined_in_parameter_index);
            base_info.parameters[lookup.defined_in_parameter_index].dependency_count += 1;
        }
        else {
            // > 2 symbols in the parameter table shouldn't be possible (No overloading for parameters, see define symbol)
            panic("");
        }
    }

    // Generate parameter analysis order
    base_info.parameter_analysis_order = dynamic_array_create<int>(base_info.parameters.size);
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
        Array<bool> contains_loop = array_create<bool>(base_info.parameters.size);
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
        dynamic_array_destroy(&parameter.dependees);
        dynamic_array_destroy(&parameter.depends_on);
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



// POLYMORPHIC INSTANCIATION
struct Matching_Constraint
{
    int value_access_index;
    Upp_Constant constant;
};

struct Matching_Info
{
    int match_count;
    Hashset<Datatype*> already_visited;
    Array<Polymorphic_Value> implicit_parameter_values;
    Dynamic_Array<Matching_Constraint> constraints;
};

bool match_templated_type_internal(Datatype* polymorphic_type, Datatype* match_against, Matching_Info* info)
{
    if (!polymorphic_type->contains_type_template) {
        return types_are_equal(polymorphic_type, match_against);
    }
    if (hashset_contains(&info->already_visited, polymorphic_type)) {
        return true;
    }
    hashset_insert_element(&info->already_visited, polymorphic_type);

    // Note: Not sure about this assert, but I think when evaluating poly arguments in correct order this shouldn't happen
    assert(!match_against->contains_type_template, "");

    auto match_polymorphic_type_to_constant = [&](Datatype_Template_Parameter* poly_type, Upp_Constant constant) -> void {
        auto& poly_value = info->implicit_parameter_values[poly_type->value_access_index];
        if (poly_type->is_reference || !poly_value.only_datatype_known) {
            // Note: The polymorphic value may already be set (!only_datatype_known) when the poly-parameter was already set explicitly, e.g.
            Matching_Constraint constraint;
            constraint.value_access_index = poly_type->value_access_index;
            constraint.constant = constant;
            dynamic_array_push_back(&info->constraints, constraint);
        }
        else {
            poly_value = polymorphic_value_make_constant(constant);
            info->match_count += 1;
        }
    };

    // Check if we found match
    if (polymorphic_type->type == Datatype_Type::TEMPLATE_PARAMETER)
    {
        auto poly_type = downcast<Datatype_Template_Parameter>(polymorphic_type);
        auto pool_result = constant_pool_add_constant(
            compiler.type_system.predefined_types.type_handle, array_create_static_as_bytes(&match_against->type_handle, 1));
        assert(pool_result.success, "Type handle must work as constant!");
        match_polymorphic_type_to_constant(poly_type, pool_result.options.constant);
        return true; // Don't match references
    }
    else if (polymorphic_type->type == Datatype_Type::STRUCT_INSTANCE_TEMPLATE)
    {
        // Check for errors
        auto struct_template = downcast<Datatype_Struct_Instance_Template>(polymorphic_type);
        Datatype_Struct* struct_type = nullptr;
        if (match_against->type == Datatype_Type::STRUCT) {
            struct_type = downcast<Datatype_Struct>(match_against);
        }
        else {
            return false;
        }

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
            if (match_type->type == Datatype_Type::UNKNOWN_TYPE) {
                continue; // In struct template instances some implicit parameters may be error type (NOTE: This isn't tested and may be totally wrong)
            }
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
                    bool success = match_templated_type_internal(match_type, match_against, info);
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
            match_polymorphic_type_to_constant(this_array->polymorphic_count_variable, pool_result.options.constant);
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
            downcast<Datatype_Array>(polymorphic_type)->element_type, downcast<Datatype_Array>(match_against)->element_type, info
        );
    }
    case Datatype_Type::OPTIONAL_TYPE: {
        return match_templated_type_internal(
            downcast<Datatype_Optional>(polymorphic_type)->child_type, downcast<Datatype_Optional>(match_against)->child_type, info
        );
    }
    case Datatype_Type::SLICE:
        return match_templated_type_internal(
            downcast<Datatype_Slice>(polymorphic_type)->element_type, downcast<Datatype_Slice>(match_against)->element_type, info
        );
    case Datatype_Type::POINTER:
        return match_templated_type_internal(
            downcast<Datatype_Pointer>(polymorphic_type)->element_type, downcast<Datatype_Pointer>(match_against)->element_type, info
        );
    case Datatype_Type::CONSTANT:
        // Note: Maybe something more sophisticated could be used for constant/subtypes...
        return match_templated_type_internal(
            downcast<Datatype_Constant>(polymorphic_type)->element_type, downcast<Datatype_Constant>(match_against)->element_type, info
        );
    case Datatype_Type::SUBTYPE:
    {
        auto subtype_poly = downcast<Datatype_Subtype>(polymorphic_type);
        auto subtype_against = downcast<Datatype_Subtype>(match_against);

        if (subtype_poly->subtype_index != subtype_against->subtype_index || subtype_poly->subtype_name != subtype_against->subtype_name) {
            return false;
        }
        return match_templated_type_internal(subtype_poly->base_type, subtype_against->base_type, info);
    }
    case Datatype_Type::STRUCT: {
        auto a = downcast<Datatype_Struct>(polymorphic_type);
        auto b = downcast<Datatype_Struct>(match_against);
        // I don't quite understand when this case should happen, but in my mind this is always false
        // if (a->struct_type != b->struct_type || a->members.size != b->members.size) {
        //     return false;
        // }
        // for (int i = 0; i < a->members.size; i++) {
        //     if (!match_templated_type_internal(a->members[i].type, b->members[i].type, info)) {
        //         return false;
        //     }
        // }
        return false;
    }
    case Datatype_Type::FUNCTION: {
        auto a = downcast<Datatype_Function>(polymorphic_type);
        auto b = downcast<Datatype_Function>(match_against);
        if (a->parameters.size != b->parameters.size || a->return_type.available != b->return_type.available) {
            return false;
        }
        for (int i = 0; i < a->parameters.size; i++) {
            if (!match_templated_type_internal(a->parameters[i].type, b->parameters[i].type, info)) {
                return false;
            }
        }
        if (a->return_type.available) {
            if (!match_templated_type_internal(a->return_type.value, b->return_type.value, info)) {
                return false;
            }
        }
        return true;
    }
    case Datatype_Type::ENUM:
    case Datatype_Type::UNKNOWN_TYPE:
    case Datatype_Type::PRIMITIVE:
    case Datatype_Type::TYPE_HANDLE: panic("Should be handled by previous code-path (E.g. non polymorphic!)");
    case Datatype_Type::STRUCT_INSTANCE_TEMPLATE:
    case Datatype_Type::TEMPLATE_PARAMETER: panic("Previous code path should have handled this!");
    default: panic("");
    }

    panic("");
    return true;
}

bool match_templated_type(Datatype* polymorphic_type, Datatype* match_against, Matching_Info* info)
{
    hashset_reset(&info->already_visited);
    dynamic_array_reset(&info->constraints);
    bool success = match_templated_type_internal(polymorphic_type, match_against, info);
    if (!success) {
        return false;
    }

    // Check if constraints match
    for (int i = 0; i < info->constraints.size; i++) {
        const auto& constraint = info->constraints[i];
        const auto& referenced_constant = info->implicit_parameter_values[constraint.value_access_index];
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

enum class Instanciation_Result_Type
{
    FUNCTION,
    STRUCTURE,
    STRUCT_INSTANCE_TEMPLATE,
    ERROR,
};

struct Instanciation_Result
{
    Instanciation_Result_Type type;
    union {
        ModTree_Function* function;
        Datatype_Struct* struct_type;
        Datatype_Struct_Instance_Template* instance_template;
    } options;
};

Instanciation_Result instanciation_result_make_error() {
    Instanciation_Result result;
    result.type = Instanciation_Result_Type::ERROR;
    return result;
}

Instanciation_Result instanciate_polymorphic_callable(
    Parameter_Matching_Info* param_matching_info, Expression_Context context,
    AST::Node* instanciation_node, Parser::Section error_report_section = Parser::Section::WHOLE)
{
    // Get poly-base
    Polymorphic_Base_Info* poly_base = nullptr;
    if (param_matching_info->call_type == Call_Type::POLYMORPHIC_FUNCTION) {
        poly_base = &param_matching_info->options.poly_function->base;
    }
    else if (param_matching_info->call_type == Call_Type::POLYMORPHIC_DOT_CALL) {
        poly_base = &param_matching_info->options.poly_dotcall->base;
    }
    else if (param_matching_info->call_type == Call_Type::POLYMORPHIC_STRUCT) {
        poly_base = &param_matching_info->options.poly_struct->info;
    }
    else {
        panic("");
    }

    auto& types = compiler.type_system.predefined_types;
    auto& parameter_nodes = poly_base->parameter_nodes;
    const int return_type_index = poly_base->return_type_index;

    // Check for errors (Instanciation limit + base is error-free)
    {
        if (check_if_polymorphic_instanciation_limit_reached()) {
            log_semantic_error("Polymorphic instanciation limit reached!", instanciation_node, error_report_section);
            parameter_matching_analyse_in_unknown_context(param_matching_info);
            return instanciation_result_make_error();
        }

        bool base_contains_errors = false;
        if (poly_base->is_function) {
            Function_Progress* base_progress = polymorphic_base_info_to_function_progress(poly_base);
            if (base_progress->header_workload->base.real_error_count > 0) {
                base_contains_errors = true;
            }
            if (base_progress->body_workload->base.is_finished && base_progress->body_workload->base.real_error_count > 0) {
                base_contains_errors = true;
            }
        }
        else {
            auto polymorphic_struct = polymorphic_base_info_to_struct_workload(poly_base);
            if (polymorphic_struct->base.real_error_count > 0) {
                base_contains_errors = true;
            }
            if (polymorphic_struct->body_workload->base.is_finished && polymorphic_struct->body_workload->base.real_error_count > 0) {
                base_contains_errors = true;
            }
        }

        if (base_contains_errors) {
            parameter_matching_analyse_in_unknown_context(param_matching_info);
            return instanciation_result_make_error();
        }
    }

    // Prepare instanciation data
    bool success = true;
    bool log_error_on_failure = false;

    Array<Polymorphic_Value> instance_values = array_create<Polymorphic_Value>(poly_base->base_parameter_values.size);
    SCOPE_EXIT(if (instance_values.data != 0) { array_destroy(&instance_values); });
    for (int i = 0; i < instance_values.size; i++) {
        assert(poly_base->base_parameter_values[i].only_datatype_known, "");
        instance_values[i] = poly_base->base_parameter_values[i];
    }

    Matching_Info matching_info;
    matching_info.constraints = dynamic_array_create<Matching_Constraint>();
    matching_info.already_visited = hashset_create_pointer_empty<Datatype*>(1);
    matching_info.implicit_parameter_values = instance_values;
    matching_info.match_count = 0;
    SCOPE_EXIT(dynamic_array_destroy(&matching_info.constraints));
    SCOPE_EXIT(hashset_destroy(&matching_info.already_visited));

    Analysis_Pass* comptime_evaluation_pass = 0; // Is allocated on demand because in many cases we won't need to reanalyse header parameters
    AST::Node* pass_creation_node;

    if (poly_base->is_function) {
        pass_creation_node = upcast(polymorphic_base_info_to_function_progress(poly_base)->header_workload->function_node);
    }
    else {
        pass_creation_node = upcast(polymorphic_base_info_to_struct_workload(poly_base)->body_workload->struct_node);
    }

    // Handle struct template instances (Custom code-path, may need to be improved in the future)
    if (!poly_base->is_function)
    {
        bool create_struct_template = false;
        auto workload = semantic_analyser.current_workload;
        if (workload->type == Analysis_Workload_Type::STRUCT_POLYMORPHIC || workload->type == Analysis_Workload_Type::FUNCTION_HEADER) {
            // Search all arguments for implicit expressions, e.g. $T, if found, then we analyse as struct template
            for (int i = 0; i < param_matching_info->matched_parameters.size; i++)
            {
                auto& param_info = param_matching_info->matched_parameters[i];
                if (param_info.expression == 0) {
                    continue;
                }
                if (check_if_expression_contains_implicit_parameters(param_info.expression)) {
                    create_struct_template = true;
                    break;
                }
            }
        }

        if (create_struct_template)
        {
            for (int i = 0; i < instance_values.size; i++) {
                instance_values[i] = polymorphic_value_make_type(types.unknown_type);
            }

            // Currently, in struct_template every value is either comptime or a templated-type
            for (int i = 0; i < param_matching_info->matched_parameters.size; i++)
            {
                auto& param_info = param_matching_info->matched_parameters[i];
                const int value_index = i;

                if (!param_info.is_set) {
                    if (param_info.required) {
                        success = false;
                        break;
                    }
                    else {
                        continue;
                    }
                }

                // Analyse argument
                Expression_Context context;
                if (param_info.requires_named_addressing) {
                    // Implicitly defined parameters
                    context = expression_context_make_unknown();
                }
                else
                {
                    auto& base_parameter = poly_base->parameters[i];
                    if (!base_parameter.is_comptime && base_parameter.depends_on.size == 0 && !base_parameter.infos.type->contains_type_template) {
                        context = expression_context_make_specific_type(base_parameter.infos.type);
                    }
                    else {
                        context = expression_context_make_unknown();
                    }
                }
                auto argument_type = analyse_parameter_if_not_already_done(&param_info, context);

                if (argument_type->contains_type_template) {
                    instance_values[value_index] = polymorphic_value_make_type(argument_type);
                }
                else if (datatype_is_unknown(argument_type)) {
                    success = false;
                }
                else
                {
                    // Calculate constant value
                    auto constant_result = expression_calculate_comptime_value(param_info.expression, "Struct arguments must be comptime");
                    if (constant_result.available)
                    {
                        auto& constant = constant_result.value;
                        if (datatype_get_non_const_type(constant.type)->type == Datatype_Type::TYPE_HANDLE)
                        {
                            Upp_Type_Handle handle = upp_constant_to_value<Upp_Type_Handle>(constant);
                            if (handle.index >= (u32)compiler.type_system.types.size) {
                                success = false;
                                log_semantic_error("Invalid constant type handle index", upcast(param_info.expression));
                                continue;
                            }

                            Datatype* constant_type = compiler.type_system.types[handle.index];
                            if (constant_type->contains_type_template) {
                                instance_values[value_index] = polymorphic_value_make_type(constant_type);
                                create_struct_template = true;
                                continue;
                            }
                        }

                        instance_values[value_index] = polymorphic_value_make_constant(constant_result.value);
                    }
                    else {
                        success = false;
                    }
                }
            }

            if (!success) {
                return instanciation_result_make_error();
            }
            else
            {
                auto poly_struct = polymorphic_base_info_to_struct_workload(poly_base);
                auto result_template_type = type_system_make_struct_instance_template(poly_struct, instance_values);
                instance_values.data = 0; // Since we transfer ownership we should signal that we don't want to delete this

                Instanciation_Result result;
                result.type = Instanciation_Result_Type::STRUCT_INSTANCE_TEMPLATE;
                result.options.instance_template = result_template_type;
                return result;
            }
        }
    }



    Datatype* final_return_type = types.unknown_type;
    if (return_type_index != -1)
    {
        auto& base_return = poly_base->parameters[return_type_index];
        if (base_return.depends_on.size == 0 && !base_return.infos.type->contains_type_template) {
            final_return_type = base_return.infos.type;
        }
    }

    // Analyse directly-set Implicit-Parameters
    // e.g. foo :: (a: $T)
    //      foo(15, T=int)
    bool reanalyse_return_type = false;
    for (int i = parameter_nodes.size; i < param_matching_info->matched_parameters.size; i++)
    {
        auto& param_info = param_matching_info->matched_parameters[i];
        if (!param_info.is_set) continue;
        const auto& implicit = poly_base->implicit_parameter_infos[i - parameter_nodes.size]; // See how argument-to-parameter mapping is done to get this
        assert(param_info.expression != 0, "Cannot set implicit parameters when doing in-compiler instanciation");

        param_info.ignore_during_code_generation = true;
        analyse_parameter_if_not_already_done(&param_info, expression_context_make_unknown());

        bool was_unavailable = false;
        auto comptime_result = expression_calculate_comptime_value(
            param_info.expression, "Parameter is polymorphic, but argument cannot be evaluated at comptime", &was_unavailable);
        if (comptime_result.available)
        {
            instance_values[implicit.template_parameter->value_access_index] = polymorphic_value_make_constant(comptime_result.value);
            matching_info.match_count += 1;

            // Mark all references to this type for re-analysis
            if (implicit.defined_in_parameter_index == return_type_index) {
                reanalyse_return_type = true;
            }
            else {
                param_matching_info->matched_parameters[implicit.defined_in_parameter_index].reanalyse_param_type_flag = true;
            }
        }
        else {
            success = false;
            if (!was_unavailable) {
                log_error_on_failure = true;
            }
        }
    }

    // Analyse arguments (+return type) in correct order
    for (int i = 0; i < poly_base->parameter_analysis_order.size; i++)
    {
        int parameter_index = poly_base->parameter_analysis_order[i];
        auto& base_parameter = poly_base->parameters[parameter_index];

        // Handle polymorphic return type
        if (parameter_index == return_type_index)
        {
            Datatype* return_type = base_parameter.infos.type;
            if (base_parameter.depends_on.size > 0 || reanalyse_return_type)
            {
                auto workload = semantic_analyser.current_workload;
                if (comptime_evaluation_pass == 0) {
                    comptime_evaluation_pass = analysis_pass_allocate(workload, pass_creation_node);
                }
                RESTORE_ON_SCOPE_EXIT(workload->current_pass, comptime_evaluation_pass);
                RESTORE_ON_SCOPE_EXIT(workload->polymorphic_values, instance_values);
                RESTORE_ON_SCOPE_EXIT(workload->current_symbol_table, poly_base->symbol_table);
                RESTORE_ON_SCOPE_EXIT(workload->ignore_unknown_errors, true);
                return_type = semantic_analyser_analyse_expression_type(poly_base->return_type_node);
            }

            // If return type is polymorphic, then try to match with expected type
            if (return_type->contains_type_template)
            {
                // If all required types contain 
                // x := Dynamic_Array~create(T=int)
                // x: Dynamic_Array(int) = Dynamic_Array~create()

                if (context.type != Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
                    log_semantic_error("Call of poly-function requires the return type to be known from context", instanciation_node, Parser::Section::ENCLOSURE);
                    success = false;
                    continue;
                }

                // Match types
                bool match_success = match_templated_type(return_type, context.expected_type.type, &matching_info);
                if (!match_success) {
                    log_semantic_error("Could not match return-type with context-type", instanciation_node, Parser::Section::ENCLOSURE);
                    log_error_info_given_type(context.expected_type.type);
                    log_error_info_expected_type(return_type);
                    success = false;
                }
                return_type = context.expected_type.type;
            }

            final_return_type = return_type;
            continue;
        }

        // Handle normal parameters

        // Find argument for parameter
        auto& param_info = param_matching_info->matched_parameters[parameter_index];

        // Skip if we can just use base-analysis result
        if (!base_parameter.is_comptime && base_parameter.depends_on.size == 0 && !base_parameter.infos.type->contains_type_template) {
            auto type = analyse_parameter_if_not_already_done(&param_info, expression_context_make_specific_type(base_parameter.infos.type));
            if (datatype_is_unknown(type) && param_info.expression != nullptr) {
                // Here we have a special case for #instanciate to work (Non-comptime parameters are faked in argument-info)
                success = false;
            }
            continue;
        }

        auto& parameter_node = parameter_nodes[parameter_index];
        param_info.ignore_during_code_generation = parameter_node->is_comptime;
        assert(parameter_node->is_comptime == base_parameter.is_comptime || !poly_base->is_function, "");

        // Get instance parameter-type (Reanalyse header type if necessary)
        Datatype* parameter_type = 0;
        if (base_parameter.depends_on.size > 0 || param_info.reanalyse_param_type_flag)
        {
            // Re-analyse base-header to get valid poly-argument type (Since this depends on other parameters)
            //      e.g. foo :: ($T: Type_Handle, a: T)
            auto workload = semantic_analyser.current_workload;
            if (comptime_evaluation_pass == 0) {
                comptime_evaluation_pass = analysis_pass_allocate(workload, pass_creation_node);
            }
            RESTORE_ON_SCOPE_EXIT(workload->current_pass, comptime_evaluation_pass);
            RESTORE_ON_SCOPE_EXIT(workload->polymorphic_values, instance_values);
            RESTORE_ON_SCOPE_EXIT(workload->current_symbol_table, poly_base->symbol_table);
            RESTORE_ON_SCOPE_EXIT(workload->ignore_unknown_errors, true);
            parameter_type = semantic_analyser_analyse_expression_type(parameter_node->type);

            if (datatype_is_unknown(parameter_type) && !parameter_type->contains_type_template) {
                success = false;
            }
        }
        else {
            parameter_type = base_parameter.infos.type;
            assert(!datatype_is_unknown(parameter_type) || parameter_type->contains_type_template, "");
        }

        // Analyse argument
        if (parameter_type->contains_type_template) {
            context = expression_context_make_unknown();
        }
        else {
            context = expression_context_make_specific_type(parameter_type);
        }
        auto argument_type = analyse_parameter_if_not_already_done(&param_info, context);
        if (datatype_is_unknown(argument_type) || argument_type->contains_type_template) { // Not sure about contains type template...
            success = false;
        }

        // Update type_mods 
        if (parameter_type->contains_type_template && success)
        {
            // Handle null expressions (Caused by #instanciate)
            if (param_info.expression != nullptr)
            {
                if (try_updating_expression_type_mods(param_info.expression, parameter_type->mods)) {
                    argument_type = get_info(param_info.expression)->cast_info.result_type;
                }
                else {
                    log_semantic_error("Type mods of parameter and argument did not match", param_info.expression);
                    success = false;
                }
            }
            else
            {
                Expression_Cast_Info cast_info = cast_info_make_empty(argument_type, param_info.argument_is_temporary_value);
                cast_info.cast_type = Cast_Type::NO_CAST;
                cast_info.deref_count = 0;
                cast_info.result_type = argument_type;
                cast_info.initial_type = argument_type;
                if (try_updating_type_mods(cast_info, parameter_type->mods)) {
                    argument_type = cast_info.result_type;
                }
                else {
                    log_semantic_error("Type mods of parameter and argument did not match", instanciation_node, error_report_section);
                    success = false;
                }
            }
            param_info.argument_type = argument_type;
        }

        // If this is a comptime argument, calculate comptime value
        if (base_parameter.is_comptime && success)
        {
            if (param_info.expression == 0) {
                // Note: If we wanted this feature, we need to add a comptime value to parameter match.
                // This way the compiler could instanciate comptime parameters without requiring an Expression to exist
                success = false;
            }
            else
            {
                // Try to calculate comptime value
                bool was_unavailable = false;
                auto comptime_result = expression_calculate_comptime_value(
                    param_info.expression, "Parameter is polymorphic, but argument cannot be evaluated at comptime", &was_unavailable);
                if (comptime_result.available) {
                    auto constant = comptime_result.value;
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
        }

        // Match implicit parameters
        if (parameter_type->contains_type_template && success)
        {
            // Match types
            bool match_success = match_templated_type(parameter_type, argument_type, &matching_info);
            if (!match_success) {
                log_semantic_error(
                    "Could not match argument with implicit-polymorphic symbols!",
                    param_info.expression == 0 ? instanciation_node : upcast(param_info.expression)
                );
                log_error_info_given_type(argument_type);
                log_error_info_expected_type(parameter_type);
                success = false;
            }
            else {
                parameter_type = argument_type;
            }
        }

        if (success) {
            param_info.param_type = parameter_type;
        }
    }



    // Return if there were errors/values not available
    if (!success) {
        if (log_error_on_failure) {
            log_semantic_error("Some values couldn't be calculated at comptime!", instanciation_node, error_report_section);
        }
        parameter_matching_analyse_in_unknown_context(param_matching_info);
        return instanciation_result_make_error();
    }
    else {
        assert(matching_info.match_count == poly_base->implicit_parameter_infos.size, "Should be the case if everything worked");
    }

    // Check if we already have an instance with the given values
    int instance_index = -1;
    {
        for (int i = 0; i < poly_base->instances.size; i++)
        {
            auto& test_instance = poly_base->instances[i];
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
        instance.base_info = poly_base;
        instance.instance_parameter_values = instance_values;
        instance_index = poly_base->instances.size;

        if (poly_base->is_function)
        {
            // Create instance function signature
            Datatype_Function* instance_function_type = 0;
            {
                Dynamic_Array<Function_Parameter> parameters = dynamic_array_create<Function_Parameter>();
                for (int i = 0; i < poly_base->parameters.size; i++)
                {
                    auto& base_param = poly_base->parameters[i];
                    if (base_param.is_comptime || i == return_type_index) {
                        continue;
                    }
                    auto& match_info = param_matching_info->matched_parameters[i];

                    Function_Parameter parameter = function_parameter_make_empty();
                    if (base_param.depends_on.size > 0 || base_param.infos.type->contains_type_template)
                    {
                        // If parameter has dependencies we need to use the instanciated type
                        parameter.type = match_info.param_type;
                        parameter.default_value_exists = false;
                    }
                    else {
                        parameter.default_value_exists = base_param.infos.default_value_exists;
                        parameter.default_value_opt = base_param.infos.default_value_opt;
                        parameter.type = base_param.infos.type;
                    }
                    parameter.name = base_param.infos.name;
                    assert(!parameter.type->contains_type_template, "");
                    dynamic_array_push_back(&parameters, parameter);
                }

                Datatype* return_type = 0;
                if (return_type_index != -1) {
                    return_type = final_return_type;
                }

                instance_function_type = type_system_make_function(parameters, return_type);
            }

            auto poly_progress = polymorphic_base_info_to_function_progress(poly_base);

            // Create new instance progress
            auto instance_progress = function_progress_create_with_modtree_function(
                0, poly_progress->header_workload->function_node, instance_function_type, poly_progress);
            instance_progress->type = Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
            instance_progress->polymorphic.instance_poly_base = &poly_progress->polymorphic.function_base;

            // Update body workload to use instance values
            instance_progress->body_workload->base.polymorphic_instanciation_depth += 1;
            instance_progress->body_workload->base.polymorphic_values = instance_values;
            instance_progress->body_workload->base.current_symbol_table = poly_base->symbol_table;

            instance.options.function_instance = instance_progress;
        }
        else
        {
            auto poly_struct = polymorphic_base_info_to_struct_workload(poly_base);

            // Create new struct instance
            auto body_workload = workload_structure_create(poly_struct->body_workload->struct_node, 0, true, Symbol_Access_Level::POLYMORPHIC);
            body_workload->struct_type->content.name = poly_struct->body_workload->struct_type->content.name;
            body_workload->polymorphic_type = Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
            body_workload->polymorphic.instance.instance_index = instance_index;
            body_workload->polymorphic.instance.parent = poly_struct;

            body_workload->base.polymorphic_instanciation_depth += 1;
            body_workload->base.polymorphic_values = instance_values;;
            body_workload->base.current_symbol_table = poly_base->symbol_table;

            analysis_workload_add_dependency_internal(upcast(body_workload), upcast(poly_struct->body_workload));

            instance.options.struct_instance = body_workload;
        }

        dynamic_array_push_back(&poly_base->instances, instance);
        instance_values.data = 0;
        instance_values.size = 0;
    }

    if (poly_base->is_function)
    {
        Instanciation_Result result;
        result.type = Instanciation_Result_Type::FUNCTION;
        result.options.function = poly_base->instances[instance_index].options.function_instance->function;
        semantic_analyser_register_function_call(result.options.function);
        return result;
    }
    else
    {
        Instanciation_Result result;
        result.type = Instanciation_Result_Type::STRUCTURE;
        result.options.struct_type = poly_base->instances[instance_index].options.struct_instance->struct_type;
        return result;
    }
}




// WORKLOAD ENTRY
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

void analyse_structure_member_nodes_recursive(Struct_Content* content, Dynamic_Array<AST::Structure_Member_Node*> member_nodes)
{
    auto& types = compiler.type_system.predefined_types;

    int member_index = 0;
    int subtype_index = 0;
    for (int i = 0; i < member_nodes.size; i++)
    {
        auto member_node = member_nodes[i];
        if (member_node->is_expression)
        {
            auto& member = content->members[member_index];
            member_index += 1;
            member.type = semantic_analyser_analyse_expression_type(member_node->options.expression);

            // Wait for member size to be known 
            {
                bool has_failed = false;
                type_wait_for_size_info_to_finish(member.type, dependency_failure_info_make(&has_failed, 0));
                if (has_failed) {
                    member.type = types.unknown_type;
                    log_semantic_error("Struct contains itself, this can only work with references", upcast(member_node), Parser::Section::IDENTIFIER);
                }
            }
        }
        else
        {
            Struct_Content* subtype = content->subtypes[subtype_index];
            subtype_index += 1;
            analyse_structure_member_nodes_recursive(subtype, member_node->options.subtype_members);
        }
    }
    assert(member_index == content->members.size, "");
    assert(subtype_index == content->subtypes.size, "");
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

        // Check if operator context changes exist
        if (module_node->context_changes.size != 0) {
            auto context = symbol_table_install_new_operator_context_and_add_workloads(
                analysis->symbol_table, module_node->context_changes, upcast(analysis->progress->event_symbol_table_ready)
            );
        }

        // Create workloads for definitions
        for (int i = 0; i < module_node->definitions.size; i++) {
            analyser_create_symbol_and_workload_for_definition(module_node->definitions[i], upcast(analysis->progress->event_symbol_table_ready));
        }

        // Create/Handle extern imports
        for (int i = 0; i < module_node->extern_imports.size; i++)
        {
            auto symbol_table = analysis->symbol_table;
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
                            symbol_table, id, Symbol_Type::DEFINITION_UNFINISHED, upcast(extern_import), Symbol_Access_Level::GLOBAL
                        );
                    }
                }

                // Create workload
                auto definition_workload = workload_executer_allocate_workload<Workload_Definition>(upcast(extern_import));
                definition_workload->symbol = symbol;
                definition_workload->is_extern_import = true;
                definition_workload->options.extern_import = extern_import;

                // Add dependencies and finish
                analysis_workload_add_dependency_internal(upcast(definition_workload), upcast(analysis->progress->event_symbol_table_ready));
                if (symbol != 0) {
                    symbol->options.definition_workload = definition_workload;
                }
                break;
            }
            case AST::Extern_Type::COMPILER_SETTING:
            {
                Dynamic_Array<String*>& values = compiler.extern_sources.compiler_settings[(int)extern_import->options.setting.type];
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
        break;
    }
    case Analysis_Workload_Type::EVENT: {
        // INFO: Events are only proxies for empty workloads
        break;
    }
    case Analysis_Workload_Type::OPERATOR_CONTEXT_CHANGE:
    {
        auto change_workload = downcast<Workload_Operator_Context_Change>(workload);
        for (int i = 0; i < change_workload->change_nodes.size; i++)
        {
            auto change_node = change_workload->change_nodes[i];
            if (change_workload->context_type_to_analyse == change_node->type) {
                analyse_operator_context_change(change_node, change_workload->context);
            }
        }
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
        auto definition_workload = downcast<Workload_Definition>(workload);
        auto symbol = definition_workload->symbol;

        if (!definition_workload->is_extern_import)
        {
            auto& def = definition_workload->options.normal;
            if (!def.is_comptime) // Global variable definition
            {
                Expression_Context context = expression_context_make_unknown();
                Datatype* type = 0;
                if (def.type_node != 0) {
                    type = semantic_analyser_analyse_expression_type(def.type_node);
                    if (def.assignment_type == AST::Assignment_Type::DEREFERENCE && type->mods.pointer_level > 0) {
                        log_semantic_error("Type must not be a pointer-type with normal value definition ': foo ='", def.type_node);
                        type = type_system_make_type_with_mods(
                            type->base_type, type_mods_make(type->mods.is_constant, 0, 0, 0, type->mods.subtype_index)
                        );
                    }
                    else if (def.assignment_type == AST::Assignment_Type::POINTER && type->mods.pointer_level == 0) {
                        log_semantic_error("Type must be a pointer-type with pointer definition ': foo =*'", def.type_node);
                        type = upcast(type_system_make_pointer(type));
                    }
                }
                if (def.value_node != 0)
                {
                    if (type != 0) {
                        semantic_analyser_analyse_expression_value(def.value_node, expression_context_make_specific_type(type));
                    }
                    else
                    {
                        Expression_Context context = expression_context_make_unknown();
                        if (def.assignment_type == AST::Assignment_Type::DEREFERENCE) {
                            context = expression_context_make_auto_dereference();
                        }
                        type = semantic_analyser_analyse_expression_value(def.value_node, context);

                        if (def.assignment_type == AST::Assignment_Type::POINTER && type->mods.pointer_level == 0) {
                            if (try_updating_expression_type_mods(def.value_node, type_mods_make(type->mods.is_constant, 1, 0, 0, type->mods.subtype_index))) {
                                type = get_info(def.value_node)->cast_info.result_type;
                            }
                            else {
                                log_semantic_error("Pointer assignment ':=*' expected a value that is a pointer!", def.value_node);
                                type = upcast(type_system_make_pointer(type));
                            }
                        }
                    }
                }

                // Note: Constant tag gets removed when infering the type, e.g. x := 15
                if (def.type_node == 0) {
                    type = datatype_get_non_const_type(type);
                }

                auto global = modtree_program_add_global(type, symbol, false);
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
                auto result = semantic_analyser_analyse_expression_any(def.value_node, expression_context_make_unknown());
                switch (result->result_type)
                {
                case Expression_Result_Type::VALUE:
                {
                    auto comptime = expression_calculate_comptime_value(def.value_node, "Value must be comptime in comptime definition (:: syntax)");
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
                    log_semantic_error("Creating aliases for hardcoded functions currently not supported", AST::upcast(def.value_node));
                    break;
                    //symbol->type = Symbol_Type::HARDCODED_FUNCTION;
                    //symbol->options.hardcoded = result->options.hardcoded;
                    //break;
                }
                case Expression_Result_Type::FUNCTION:
                {
                    ModTree_Function* function = result->options.function;
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    log_semantic_error("Creating symbol/function aliases currently not supported", AST::upcast(def.value_node));
                    break;
                }
                case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    log_semantic_error("Creating aliases for polymorphic functions not supported!", AST::upcast(def.value_node));
                    break;
                }
                case Expression_Result_Type::DOT_CALL: {
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    log_semantic_error("Cannot use dot-call in definition", AST::upcast(def.value_node));
                    break;
                }
                case Expression_Result_Type::NOTHING: {
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    log_semantic_error("Comptime definition expected a value, not nothing (void/no return value)", AST::upcast(def.value_node));
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
        }
        else // is_extern_import
        {
            AST::Extern_Import* import = definition_workload->options.extern_import;
            switch (import->type)
            {
            case AST::Extern_Type::FUNCTION:
            {
                auto function_type = semantic_analyser_analyse_expression_type(import->options.function.type_expr);

                // Check for errors
                if (datatype_is_unknown(function_type)) {
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    break;
                }
                else if (function_type->type != Datatype_Type::FUNCTION) {
                    log_semantic_error("Extern function type must be function", import->options.function.type_expr);
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    break;
                }

                // Check if function already exists in extern functions...
                auto& extern_functions = compiler.extern_sources.extern_functions;
                {
                    bool found = false;
                    for (int i = 0; i < extern_functions.size; i++) {
                        auto extern_fn = extern_functions[i];
                        if (extern_fn->options.extern_definition->symbol->id == symbol->id &&
                            types_are_equal(upcast(extern_fn->signature), function_type))
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


                ModTree_Function* extern_fn = modtree_function_create_empty(downcast<Datatype_Function>(function_type), symbol->id);
                extern_fn->function_type = ModTree_Function_Type::EXTERN;
                extern_fn->options.extern_definition = definition_workload;

                symbol->type = Symbol_Type::FUNCTION;
                symbol->options.function = extern_fn;

                dynamic_array_push_back(&extern_functions, extern_fn);

                break;
            }
            case AST::Extern_Type::GLOBAL:
            {
                Datatype* datatype = semantic_analyser_analyse_expression_type(import->options.global.type_expr);
                if (datatype_is_unknown(datatype)) {
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    break;
                }

                // Deduplicate if we already added this global
                {
                    bool found = false;
                    for (int i = 0; i < semantic_analyser.program->globals.size; i++) {
                        auto global = semantic_analyser.program->globals[i];
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

                auto global = modtree_program_add_global(datatype, symbol, true);
                symbol->type = Symbol_Type::GLOBAL;
                symbol->options.global = global;

                break;
            }
            case AST::Extern_Type::STRUCT:
            {
                Datatype* type = semantic_analyser_analyse_expression_type(import->options.struct_type_expr);
                if (datatype_is_unknown(type)) {
                    break;
                }

                if (type->type != Datatype_Type::STRUCT) {
                    log_semantic_error("extern struct must be followed by a struct type", import->options.struct_type_expr);
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
        auto progress = header_workload->progress;
        ModTree_Function* function = header_workload->progress->function;
        auto& signature_node = header_workload->function_node->options.function.signature->options.function_signature;
        auto& parameter_nodes = signature_node.parameters;
        assert(function->function_type == ModTree_Function_Type::NORMAL, "");

        Optional<Polymorphic_Base_Info> polymorphic_info_opt = define_parameter_symbols_and_check_for_polymorphism(
            signature_node.parameters, workload->current_symbol_table, function->name, progress,
            (signature_node.return_value.available ? signature_node.return_value.value : nullptr)
        );

        if (polymorphic_info_opt.available)
        {
            // Switch progress type to polymorphic base
            progress->type = Polymorphic_Analysis_Type::POLYMORPHIC_BASE;
            progress->polymorphic.function_base.base = polymorphic_info_opt.value;
            auto& base = progress->polymorphic.function_base.base;

            // Set polymorphic access infos for child workloads
            header_workload->base.symbol_access_level = Symbol_Access_Level::POLYMORPHIC;
            header_workload->base.polymorphic_values = base.base_parameter_values;
            progress->body_workload->base.polymorphic_values = base.base_parameter_values;
            progress->body_workload->base.symbol_access_level = Symbol_Access_Level::INTERNAL;

            // Update function + symbol
            function->is_runnable = false; // Polymorphic base cannot be runnable
            if (function->options.normal.symbol != 0) {
                function->options.normal.symbol->type = Symbol_Type::POLYMORPHIC_FUNCTION;
                function->options.normal.symbol->options.polymorphic_function = &progress->polymorphic.function_base;
            }

            // Create function signature
            {
                Dynamic_Array<Function_Parameter> parameters = dynamic_array_create<Function_Parameter>(parameter_nodes.size);
                for (int i = 0; i < parameter_nodes.size; i++) {
                    auto& param = base.parameters[i];
                    if (!param.is_comptime) {
                        dynamic_array_push_back(&parameters, param.infos);
                    }
                }
                Datatype* return_type = 0;
                if (signature_node.return_value.available) {
                    return_type = base.parameters[signature_node.parameters.size].infos.type;
                }
                function->signature = type_system_make_function(parameters, return_type);
            }
        }
        else // Handle normal functions
        {
            workload->symbol_access_level = Symbol_Access_Level::POLYMORPHIC;
            Dynamic_Array<Function_Parameter> parameters = dynamic_array_create<Function_Parameter>(parameter_nodes.size);
            for (int i = 0; i < parameter_nodes.size; i++) {
                auto node = parameter_nodes[i];
                assert(!node->is_comptime, "");
                Function_Parameter parameter;
                analyse_parameter_type_and_value(parameter, node);
                dynamic_array_push_back(&parameters, parameter);
            }

            Datatype* return_type = 0;
            if (signature_node.return_value.available) {
                return_type = semantic_analyser_analyse_expression_type(signature_node.return_value.value);
            }
            function->signature = type_system_make_function(parameters, return_type);
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
            Function_Progress* base_progress = polymorphic_function_base_to_function_progress(body_workload->progress->polymorphic.instance_poly_base);
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
        auto poly_info_opt = define_parameter_symbols_and_check_for_polymorphism(
            struct_node.parameters, parameter_table, workload_poly->body_workload->struct_type->content.name, 0, 0
        );
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

        // Check if we are a instance analysis
        if (workload_structure->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
            auto& base_struct = workload_structure->polymorphic.instance.parent;
            assert(base_struct->base.is_finished, "At this point the base has to be finished");
            assert(base_struct->body_workload->base.is_finished, "Body base must be also finished");

            // If an error occured in the polymorphic base, set all struct members to error-type
            if (base_struct->base.real_error_count != 0 || base_struct->body_workload->base.real_error_count != 0) {
                // Note: All members/subtypes are already added, but as error type...
                type_system_finish_struct(struct_signature);
                break;
            }
        }

        // Analyse all members
        analyse_structure_member_nodes_recursive(&struct_signature->content, struct_node.members);
        type_system_finish_struct(struct_signature);
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
                bake->progress->result_type = type_system.predefined_types.unknown_type;
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
        while (true)
        {
            bytecode_thread_execute(thread);
            if (thread->exit_code.type == Exit_Code_Type::TYPE_INFO_WAITING_FOR_TYPE_FINISHED)
            {
                bool cyclic_dependency_occured = false;
                type_wait_for_size_info_to_finish(thread->waiting_for_type_finish_type, dependency_failure_info_make(&cyclic_dependency_occured));
                if (cyclic_dependency_occured) {
                    log_semantic_error("Bake requires type_info which waits on bake, cyclic dependency!", execute->bake_node, Parser::Section::KEYWORD);
                    progress->result = optional_make_failure<Upp_Constant>();
                    return;
                }
            }
            else if (thread->exit_code.type == Exit_Code_Type::SUCCESS) {
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
            log_error_info_constant_status(pool_result.options.error_message);
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
void semantic_analyser_register_function_call(ModTree_Function* call_to)
{
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;

    auto workload = analyser.current_workload;
    if (workload->current_function != 0) {
        dynamic_array_push_back(&workload->current_function->calls, call_to);
        dynamic_array_push_back(&call_to->called_from, workload->current_function);
    }

    if (call_to->function_type == ModTree_Function_Type::EXTERN) {
        return;
    }
    assert(call_to->function_type == ModTree_Function_Type::NORMAL, "Cannot have a call to a bake function");
    auto progress = call_to->options.normal.progress;

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

Expression_Info expression_info_make_empty(Expression_Context context)
{
    auto unknown_type = compiler.type_system.predefined_types.unknown_type;

    Expression_Info info;
    info.contains_errors = false;
    info.result_type = Expression_Result_Type::VALUE;
    info.options.value_type = unknown_type;
    info.context = context;
    info.cast_info.result_type = unknown_type;
    info.cast_info.initial_type = unknown_type;
    info.cast_info.cast_type = Cast_Type::NO_CAST;
    info.cast_info.deref_count = 0;
    info.cast_info.options.error_msg = nullptr;
    return info;
}

Expression_Cast_Info cast_info_make_empty(Datatype* initial_type, bool is_temporary_value)
{
    Expression_Cast_Info cast_info;
    cast_info.cast_type = Cast_Type::NO_CAST;
    cast_info.deref_count = 0;
    cast_info.initial_type = initial_type;
    cast_info.result_type = initial_type;
    cast_info.initial_value_is_temporary = is_temporary_value;
    cast_info.result_value_is_temporary = is_temporary_value;
    cast_info.options.error_msg = nullptr;
    return cast_info;
}

bool try_updating_type_mods(Expression_Cast_Info& cast_info, Type_Mods expected_mods, const char** out_error_msg)
{
    if (cast_info.result_type->mods.pointer_level == expected_mods.pointer_level &&
        cast_info.result_type->mods.constant_flags == expected_mods.constant_flags &&
        cast_info.result_type->mods.optional_flags == expected_mods.optional_flags &&
        cast_info.result_type->mods.is_constant == expected_mods.is_constant &&
        cast_info.result_type->mods.subtype_index == expected_mods.subtype_index)
    {
        return true;
    }

    const char* dummy;
    if (out_error_msg == 0) {
        out_error_msg = &dummy;
    }

    Type_Mods given = cast_info.initial_type->mods;
    Type_Mods expected = expected_mods;

    // Check if pointers are compatible
    if (given.pointer_level != expected.pointer_level)
    {
        int max_pointer_level = given.pointer_level;
        if (!cast_info.initial_value_is_temporary) {
            max_pointer_level += 1;
        }
        if (expected.pointer_level > max_pointer_level) {
            if (expected.pointer_level == given.pointer_level + 1 && cast_info.initial_value_is_temporary) {
                *out_error_msg = "Cannot automatically take address of temporaray value";
            }
            else {
                *out_error_msg = "Pointer level difference is too high";
            }
            return false;
        }
        // We cannot change the pointer level after a cast (Currently, but I guess deref afterwards would still be ok)
        if (cast_info.cast_type != Cast_Type::NO_CAST) {
            *out_error_msg = "Pointer level after cast does not match";
            return false;
        }
        // Check if we are dereferencing optional pointers
        for (int i = 0; i < given.pointer_level - expected.pointer_level; i++) {
            int ptr_level = given.pointer_level - i;
            if (type_mods_pointer_is_optional(given, ptr_level - 1)) {
                *out_error_msg = "Requires a dereference of an optional pointer";
                return false; // Cannot dereference optional flags
            }
        }
    }

    // Check if constant flags are compatible
    if (expected.constant_flags != given.constant_flags)
    {
        // Note: It is important that the const-ness of _pointed_to_ elements is not different,
        //       but two non-pointer values can be exchanged freely.
        // Examples: const *char -> *char       Valid (pass by value)
        //           int         -> const int   Valid 
        //           const int   -> int         Valid
        //           *const int  -> *int        Invalid
        for (int i = 0; i < expected_mods.pointer_level; i++)
        {
            bool expected_is_const;
            bool given_is_const;
            if (i == 0) {
                expected_is_const = expected.is_constant;
                given_is_const = given.is_constant;
            }
            else {
                expected_is_const = type_mods_pointer_is_constant(expected_mods, i - 1);
                given_is_const = type_mods_pointer_is_constant(cast_info.initial_type->mods, i - 1);
            }
            if (!expected_is_const && given_is_const) {
                *out_error_msg = "Cast from constant pointer to non-constant pointer";
                return false; // Cannot cast from const pointer to non-const-pointer, e.g. Error: *const int -> *int
            }
        }
    }

    // Check if optional flags are compatible
    if (expected.optional_flags != given.optional_flags)
    {
        for (int i = 0; i < expected_mods.pointer_level; i++)
        {
            bool expected_is_opt = type_mods_pointer_is_optional(expected, i);
            bool given_is_opt = type_mods_pointer_is_optional(given, i);
            if (given_is_opt && !expected_is_opt) {
                *out_error_msg = "Cast from optional pointer to non-optional pointer";
                return false; // Cannot cast from optional to non-optional pointer
            }
        }
    }

    // Check if subtypes are compatible
    if (expected_mods.subtype_index != cast_info.initial_type->mods.subtype_index)
    {
        auto& given = cast_info.initial_type->mods.subtype_index->indices;
        auto& expected = expected_mods.subtype_index->indices;

        if (given.size <= expected.size) {
            *out_error_msg = "Cannot downcast/switch to other subtype automatically";
            return false; // Cannot downcast, or switch to other subtype
        }

        // Check if all subtypes match to given...
        for (int i = 0; i < expected.size; i++) {
            if (given[i].index != expected[i].index || given[i].name != expected[i].name) {
                *out_error_msg = "Subtypes don't match";
                return false;
            }
        }
    }

    // 3. Set correct type after cast/dereference op
    Datatype* result_type = cast_info.initial_type->base_type;
    cast_info.result_type = type_system_make_type_with_mods(cast_info.initial_type->base_type, expected_mods);
    cast_info.deref_count = cast_info.initial_type->mods.pointer_level - expected_mods.pointer_level;
    if (cast_info.deref_count > 0) {
        cast_info.result_value_is_temporary = false;
    }
    else if (cast_info.deref_count == 0) {
        cast_info.result_value_is_temporary = cast_info.initial_value_is_temporary;
    }
    else {
        cast_info.result_value_is_temporary = true; // The pointer from address of is definitly temporary
    }

    return true;
};

bool try_updating_expression_type_mods(AST::Expression* expr, Type_Mods expected_mods, const char** out_error_msg) {
    auto info = get_info(expr);
    return try_updating_type_mods(info->cast_info, expected_mods, out_error_msg);
};

bool datatype_check_if_auto_casts_to_other_mods(Datatype* datatype, Type_Mods expected_mods, bool is_temporary, const char** out_error_msg = nullptr)
{
    Expression_Cast_Info cast_info = cast_info_make_empty(datatype, is_temporary);
    return try_updating_type_mods(cast_info, expected_mods, out_error_msg);
};

Expression_Info analyse_symbol_as_expression(Symbol* symbol, Expression_Context context, AST::Symbol_Lookup* error_report_node)
{
    auto& executer = semantic_analyser.workload_executer;
    auto workload = semantic_analyser.current_workload;
    auto& types = compiler.type_system.predefined_types;
    auto unknown_type = types.unknown_type;

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
        auto fn = symbol->options.function;
        if (fn->function_type == ModTree_Function_Type::NORMAL) {
            analysis_workload_add_dependency_internal(workload, upcast(fn->options.normal.progress->header_workload), failure_info);
        }
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
        expression_info_set_error(&result, unknown_type);
        return result;
    }

    switch (symbol->type)
    {
    case Symbol_Type::ERROR_SYMBOL: {
        semantic_analyser_set_error_flag(true);
        expression_info_set_error(&result, unknown_type);
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
        semantic_analyser_register_function_call(symbol->options.function);
        expression_info_set_function(&result, symbol->options.function);
        return result;
    }
    case Symbol_Type::GLOBAL: {
        expression_info_set_value(&result, symbol->options.global->type, false);
        return result;
    }
    case Symbol_Type::TYPE: {
        expression_info_set_type(&result, symbol->options.type);
        return result;
    }
    case Symbol_Type::VARIABLE: {
        assert(workload->type != Analysis_Workload_Type::FUNCTION_HEADER, "Function headers can never access variable symbols!");
        expression_info_set_value(&result, symbol->options.variable_type, false);
        return result;
    }
    case Symbol_Type::VARIABLE_UNDEFINED: {
        log_semantic_error("Variable not defined at this point", upcast(error_report_node));
        semantic_analyser_set_error_flag(true);
        expression_info_set_error(&result, unknown_type);
        return result;
    }
    case Symbol_Type::PARAMETER:
    {
        auto& param = symbol->options.parameter;
        auto workload = semantic_analyser.current_workload;

        auto progress = analysis_workload_try_get_function_progress(workload);
        assert(progress != 0, "We should be in function-body workload since normal parameters have internal symbol access");
        expression_info_set_value(&result, progress->function->signature->parameters[param.index_in_non_polymorphic_signature].type, false);
        return result;
    }
    case Symbol_Type::POLYMORPHIC_VALUE: {
        int access_index = symbol->options.polymorphic_value.access_index;
        auto poly_values = workload->polymorphic_values;
        assert(poly_values.data != 0, "Why can we access non-polymorphic parameter here");

        auto& value = poly_values[access_index];
        if (value.only_datatype_known) {
            semantic_analyser_set_error_flag(true);
            expression_info_set_value(&result, value.options.type, false);
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
        expression_info_set_error(&result, unknown_type);
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








#define SET_ACTIVE_EXPR_INFO(new_info)\
    Expression_Info* _backup_info = semantic_analyser.current_workload->current_expression; \
    semantic_analyser.current_workload->current_expression = new_info; \
    SCOPE_EXIT(semantic_analyser.current_workload->current_expression = _backup_info);

bool expression_is_auto_expression(AST::Expression* expression)
{
    auto type = expression->type;
    return type == AST::Expression_Type::AUTO_ENUM ||
        (type == AST::Expression_Type::ARRAY_INITIALIZER && !expression->options.array_initializer.type_expr.available) ||
        (type == AST::Expression_Type::STRUCT_INITIALIZER && !expression->options.struct_initializer.type_expr.available) ||
        (type == AST::Expression_Type::CAST && !expression->options.cast.to_type.available) ||
        (type == AST::Expression_Type::LITERAL_READ &&
            (expression->options.literal_read.type == Literal_Type::NULL_VAL ||
                expression->options.literal_read.type == Literal_Type::INTEGER ||
                expression->options.literal_read.type == Literal_Type::FLOAT_VAL));
}

void analyse_member_initializers_in_unknown_context_recursive(AST::Arguments* args) 
{
    auto matching_info = get_info(args, true);
    *matching_info = parameter_matching_info_create_empty(Call_Type::STRUCT_INITIALIZER, 0);
    matching_info->options.struct_init.valid = false;
    for (int i = 0; i < args->arguments.size; i++) {
        semantic_analyser_analyse_expression_value(args->arguments[i]->value, expression_context_make_unknown(true));
    }
    for (int i = 0; i < args->subtype_initializers.size; i++) {
        analyse_member_initializers_in_unknown_context_recursive(args->subtype_initializers[i]->arguments);
    }
}

// Allowed direction determines if initializers are allowed to contain subtype and base-type initializers (value=0), 
// or only subtype (value=1) or base_type(value=-1)
void analyse_member_initializer_recursive(
    AST::Arguments* arguments, Datatype_Struct* structure, Struct_Content* content, int allowed_direction, Subtype_Index** final_subtype_index)
{
    auto& types = compiler.type_system.predefined_types;

    // Match arguments to struct members
    auto matching_info = get_info(arguments, true);
    *matching_info = parameter_matching_info_create_empty(Call_Type::STRUCT_INITIALIZER, content->members.size);
    matching_info->options.struct_init.content = content;
    matching_info->options.struct_init.structure = structure;
    matching_info->options.struct_init.subtype_valid = allowed_direction != -1;
    matching_info->options.struct_init.supertype_valid = allowed_direction != 1;
    matching_info->options.struct_init.valid = true;

    for (int i = 0; i < content->members.size; i++) {
        auto& member = content->members[i];
        parameter_matching_info_add_param(matching_info, member.id, true, false, member.type);
    }

    if (arguments_match_to_parameters(arguments, matching_info, true)) {
        for (int i = 0; i < matching_info->matched_parameters.size; i++) {
            auto& member = content->members[i];
            const auto& param_info = matching_info->matched_parameters[i];
            if (!param_info.is_set) { // Possible if we have ignore-tokens
                continue;
            }
            semantic_analyser_analyse_expression_value(arguments->arguments[param_info.argument_index]->value, expression_context_make_specific_type(member.type));
        }
    }
    else {
        parameter_matching_analyse_in_unknown_context(matching_info);
    }
    
    // Go through subtype-initializers and call function recursively
    bool subtype_initializer_found = false;
    bool supertype_initializer_found = false;
    auto parent_content = struct_content_get_parent(content);
    for (int i = 0; i < arguments->subtype_initializers.size; i++) 
    {
        auto& init_node = arguments->subtype_initializers[i];

        // Check if it's a subtype or supertype initializer
        int subtype_index = -1;
        bool is_supertype_init = false;
        if (init_node->name.available)
        {
            for (int j = 0; j < content->subtypes.size; j++) {
                auto sub_content = content->subtypes[j];
                if (sub_content->name == init_node->name.value) {
                    subtype_index = j;
                    break;
                }
            }

            // Check if it's supertype name
            if (subtype_index == -1 && parent_content != 0) {
                if (parent_content->name == init_node->name.value) {
                    is_supertype_init = true;
                }
            }
        }
        else {
            is_supertype_init = true;
        }

        if (is_supertype_init)
        {
            if (parent_content == 0)
            {
                log_semantic_error(
                    "Base-Type initializer invalid in this context, struct type is already the base type", upcast(init_node), Parser::Section::FIRST_TOKEN);
                analyse_member_initializers_in_unknown_context_recursive(init_node->arguments);
                break;
            }
            else if (allowed_direction == 1 || supertype_initializer_found) {
                log_semantic_error(
                    "Cannot re-specify base-type members, this has already been done in this struct-initializer", upcast(init_node), Parser::Section::FIRST_TOKEN);
                analyse_member_initializers_in_unknown_context_recursive(init_node->arguments);
                break;
            }
            else {
                analyse_member_initializer_recursive(init_node->arguments, structure, parent_content, -1, final_subtype_index);
                supertype_initializer_found = true;
                break;
            }
        }
        else if (subtype_index != -1)
        {
            // Analyse subtype
            if (subtype_initializer_found) {
                log_semantic_error(
                    "Cannot re-specify subtype, this has already been done in this struct-initializer", upcast(init_node), Parser::Section::FIRST_TOKEN);
                analyse_member_initializers_in_unknown_context_recursive(init_node->arguments);
                break;
            }
            else if (allowed_direction == -1) {
                log_semantic_error(
                    "Cannot re-specify subtype, this has already been done on another struct-initializer level", upcast(init_node), Parser::Section::FIRST_TOKEN);
                analyse_member_initializers_in_unknown_context_recursive(init_node->arguments);
                break;
            }
            else
            {
                *final_subtype_index = content->subtypes[subtype_index]->index;
                analyse_member_initializer_recursive(init_node->arguments, structure, content->subtypes[subtype_index], 1, final_subtype_index);
                subtype_initializer_found = true;
            }
        }
        else {
            log_semantic_error("Name is neither supertype nor subtype!", upcast(init_node), Parser::Section::IDENTIFIER);
            analyse_member_initializers_in_unknown_context_recursive(init_node->arguments);
        }
    }

    // Check for further errors
    bool found_ignore_symbol = arguments->uninitialized_tokens.size > 0;
    if (!found_ignore_symbol)
    {
        if (parent_content != 0 && !supertype_initializer_found && allowed_direction == 0) {
            log_semantic_error(
                "Base-Type members were not specified, use base-type initializer '. = {}' for this!", upcast(arguments), Parser::Section::ENCLOSURE);
        }
        if (content->subtypes.size > 0 && !subtype_initializer_found && allowed_direction == 0) {
            log_semantic_error("Subtype was not specified, use subtype initializer '.SubName = {}' for this!", upcast(arguments), Parser::Section::ENCLOSURE);
        }
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

#define EXIT_VALUE(val, is_temporaray) expression_info_set_value(info, val, is_temporaray); return info;
#define EXIT_TYPE(type) expression_info_set_type(info, type); return info;
#define EXIT_ERROR(type) expression_info_set_error(info, type); return info;
#define EXIT_HARDCODED(hardcoded) expression_info_set_hardcoded(info, hardcoded); return info;
#define EXIT_FUNCTION(function) expression_info_set_function(info, function); return info;

    switch (expr->type)
    {
    case AST::Expression_Type::ERROR_EXPR: {
        semantic_analyser_set_error_flag(false);// Error due to parsing, dont log error message because we already have parse error messages
        EXIT_ERROR(types.unknown_type);
    }
    case AST::Expression_Type::FUNCTION_CALL:
    {
        auto& call = expr->options.call;
        info->specifics.function_call_signature = 0;

        // Fill matching info (Includes overload resolution)
        Parameter_Matching_Info* matching_info = nullptr;
        if (call.expr->type == AST::Expression_Type::PATH_LOOKUP)
        {
            // Find all overload candidates
            Dynamic_Array<Overload_Candidate> candidates = dynamic_array_create<Overload_Candidate>();
            SCOPE_EXIT(
                for (int i = 0; i < candidates.size; i++) {
                    parameter_matching_info_destroy(&candidates[i].matching_info);
                }
                dynamic_array_destroy(&candidates)
            );

            // Find all overloads
            Dynamic_Array<Symbol*> symbols = dynamic_array_create<Symbol*>();
            SCOPE_EXIT(dynamic_array_destroy(&symbols));

            path_lookup_resolve(call.expr->options.path_lookup, symbols);
            if (symbols.size == 0) {
                log_semantic_error("Could not resolve Symbol (No definition found)", upcast(call.expr->options.path_lookup));
                path_lookup_set_info_to_error_symbol(call.expr->options.path_lookup, semantic_analyser.current_workload);
                analyse_arguments_in_unknown_context(call.arguments);
                EXIT_ERROR(types.unknown_type);
            }

            // Convert symbols to overload candidates
            bool encountered_unknown = false;
            for (int i = 0; i < symbols.size; i++)
            {
                auto& symbol = symbols[i];
                if (symbol->type == Symbol_Type::MODULE) {
                    continue;
                }
                auto info = analyse_symbol_as_expression(symbol, expression_context_make_auto_dereference(), call.expr->options.path_lookup->last);
                auto overload_opt = overload_candidate_try_create_from_expression_info(info, call.expr, symbol);
                if (overload_opt.available) {
                    dynamic_array_push_back(&candidates, overload_opt.value);
                }
                else if (datatype_is_unknown(info.cast_info.result_type)) {
                    encountered_unknown = true;
                }
            }

            // Check success
            if (encountered_unknown) { // Why do we have this?
                semantic_analyser_set_error_flag(true);
                analyse_arguments_in_unknown_context(call.arguments);
                EXIT_ERROR(types.unknown_type);
            }
            if (symbols.size == 1 && candidates.size == 0) {
                log_semantic_error("Symbol is not callable!", upcast(call.expr->options.path_lookup->last));
                log_error_info_symbol(symbols[0]);
                analyse_arguments_in_unknown_context(call.arguments);
                EXIT_ERROR(types.unknown_type);
            }

            // Do parameter-to-argument mapping for all candidates
            if (candidates.size == 1)
            {
                auto& candidate = candidates[0];
                if (!arguments_match_to_parameters(call.arguments, &candidate.matching_info, true)) 
                {
                    parameter_matching_analyse_in_unknown_context(&candidate.matching_info);
                    *get_info(call.arguments, true) = candidate.matching_info;
                    candidate.matching_info.matched_parameters.data = nullptr;
                    candidate.matching_info.matched_parameters.size = 0;
                    candidate.matching_info.matched_parameters.capacity = 0;
                    if (candidate.matching_info.has_return_value) {
                        EXIT_ERROR(candidate.matching_info.return_type);
                    }
                    EXIT_ERROR(types.unknown_type);
                }
            }
            else if (candidates.size > 1) 
            {
                for (int i = 0; i < candidates.size; i++) {
                    auto& candidate = candidates[i];
                    if (!arguments_match_to_parameters(call.arguments, &candidate.matching_info, false)) {
                        parameter_matching_info_destroy(&candidate.matching_info);
                        dynamic_array_swap_remove(&candidates, i);
                        i -= 1;
                    }
                }
            }

            // Log error if polymorphic candidates exist (Currently not implemented)
            for (int i = 0; i < candidates.size && candidates.size > 1; i++) {
                auto& candidate = candidates[i];
                if (candidate.matching_info.call_type == Call_Type::POLYMORPHIC_DOT_CALL || 
                    candidate.matching_info.call_type == Call_Type::POLYMORPHIC_FUNCTION ||
                    candidate.matching_info.call_type == Call_Type::POLYMORPHIC_STRUCT) 
                {
                    log_semantic_error("Overload-resolution with polymorphic types currently not implemented", upcast(call.expr->options.path_lookup->last));
                    analyse_arguments_in_unknown_context(call.arguments);
                    EXIT_ERROR(types.unknown_type);
                }
            }

            // Disambiguate overloads by argument types + return type
            Dynamic_Array<int> arguments_missing_casts = dynamic_array_create<int>();
            SCOPE_EXIT(dynamic_array_destroy(&arguments_missing_casts));
            if (candidates.size > 1)
            {
                auto remove_candidates_based_on_better_type_match = 
                    [](Dynamic_Array<Overload_Candidate>& candidates, bool arg_is_temporary, Datatype* arg_type)
                {
                    bool matching_candidate_exists = false;
                    bool type_mods_compatible_exists = false;
                    bool castable_exists = false;
                    for (int j = 0; j < candidates.size; j++)
                    {
                        auto& candidate = candidates[j];
                        candidate.overloading_arg_can_be_cast = false;
                        candidate.overloading_arg_matches_type = false;
                        candidate.overloading_arg_matches_type = false;
                        Datatype* param_type = candidate.active_type;
                        if (types_are_equal(param_type, arg_type)) {
                            candidate.overloading_arg_matches_type = true;
                            matching_candidate_exists = true;
                        }

                        if (!candidate.overloading_arg_matches_type) {
                            Expression_Cast_Info cast_info = cast_info_make_empty(arg_type, arg_is_temporary);
                            if (try_updating_type_mods(cast_info, param_type->mods)) {
                                candidate.overloading_arg_type_mods_compatible = true;
                                type_mods_compatible_exists = true;
                            }
                        }

                        if (!candidate.overloading_arg_type_mods_compatible && !candidate.overloading_arg_matches_type) {
                            Expression_Cast_Info cast_info = 
                                semantic_analyser_check_if_cast_possible(arg_is_temporary, arg_type, param_type, Cast_Mode::IMPLICIT);
                            if (cast_info.cast_type != Cast_Type::INVALID) {
                                candidate.overloading_arg_can_be_cast = true;
                                castable_exists = true;
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
                        else if (candidate.overloading_arg_type_mods_compatible) {
                            if (matching_candidate_exists) {
                                remove = true;
                            }
                        }
                        else if (candidate.overloading_arg_can_be_cast){
                            if (matching_candidate_exists || type_mods_compatible_exists) {
                                remove = true;
                            }
                        }
                        else {
                            if (matching_candidate_exists || type_mods_compatible_exists || castable_exists) {
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
                for (int i = 0; i < call.arguments->arguments.size && candidates.size > 1; i++)
                {
                    auto argument = call.arguments->arguments[i];

                    // Check if parameter types differ between overloads (And set active_index for further comparison)
                    bool argument_usable = false;
                    Datatype* prev_type = nullptr;
                    for (int j = 0; j < candidates.size; j++)
                    {
                        auto& candidate = candidates[j];
                        Parameter_Match* match = nullptr;
                        // Find named argument
                        for (int k = 0; k < candidate.matching_info.matched_parameters.size; k++) {
                            auto& param = candidate.matching_info.matched_parameters[k];
                            if (param.argument_index == i) {
                                candidate.active_type = param.param_type;
                                match = &param;
                                break;
                            }
                        }

                        assert(match != nullptr, "");
                        if (match->param_type == nullptr) {
                            argument_usable = false;
                            break;
                        }

                        if (j == 0) {
                            prev_type = match->param_type;
                        }
                        else if (!types_are_equal(prev_type,  match->param_type)) {
                            argument_usable = true;
                        }
                    }

                    // Check if we can differentiate the call based on this parameter's type
                    if (!argument_usable) {
                        continue;
                    }

                    // For each candidate figure out if argument does/doesn't match or can be cast
                    auto argument_type = semantic_analyser_analyse_expression_value(argument->value, expression_context_make_unknown());
                    dynamic_array_push_back(&arguments_missing_casts, i);
                    remove_candidates_based_on_better_type_match(candidates, get_info(argument->value)->cast_info.initial_value_is_temporary, argument_type);
                }

                // If we still have candidates, try to differentiate based on return type
                if (candidates.size > 1 && context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
                {
                    auto expected_return_type = context.expected_type.type;
                    bool can_differentiate_based_on_return_type = false;
                    Datatype* last_return_type = 0;
                    for (int i = 0; i < candidates.size; i++) 
                    {
                        auto& candidate = candidates[i];
                        if (!candidate.matching_info.has_return_value) {
                            candidate.active_type = type_system->predefined_types.unknown_type; // Maybe we should remove candidate right here...
                            continue;
                        }

                        candidate.active_type = candidate.matching_info.return_type;
                        if (i == 0) {
                            last_return_type = candidate.active_type;
                        }
                        else if (!types_are_equal(last_return_type, candidate.active_type)) {
                            can_differentiate_based_on_return_type = true;
                        }
                    }

                    if (can_differentiate_based_on_return_type) {
                        remove_candidates_based_on_better_type_match(candidates, true, expected_return_type);
                    }
                }
            }

            // Check for success
            if (candidates.size == 1)
            {
                // Set expression/Symbol read info
                auto& candidate = candidates[0];
                auto call_expr_info = pass_get_node_info(semantic_analyser.current_workload->current_pass, call.expr, Info_Query::CREATE_IF_NULL);
                *call_expr_info = candidate.expression_info;
                assert(candidate.symbol != 0, "Must have been set before!");
                path_lookup_set_result_symbol(call.expr->options.path_lookup, candidate.symbol);

                // Apply casts where necessary
                for (int i = 0; i < arguments_missing_casts.size; i++)
                {
                    auto argument = call.arguments->arguments[arguments_missing_casts[i]];
                    Parameter_Match* param_info = 0;
                    for (int j = 0; j < candidate.matching_info.matched_parameters.size; j++) {
                        auto info = candidate.matching_info.matched_parameters[j];
                        if (info.argument_index == i) {
                            param_info = &info;
                            break;
                        }
                    }
                    assert(param_info != 0, "");

                    auto arg_info = get_info(argument->value);
                    arg_info->cast_info = semantic_analyser_check_if_cast_possible(
                        arg_info->cast_info.initial_value_is_temporary, arg_info->cast_info.initial_type, param_info->param_type, Cast_Mode::IMPLICIT
                    );
                    assert(arg_info->cast_info.cast_type != Cast_Type::INVALID, "must be true!");

                    param_info->state = Parameter_State::ANALYSED;
                    param_info->argument_is_temporary_value = arg_info->cast_info.result_value_is_temporary;
                    param_info->argument_type = arg_info->cast_info.result_type;
                }

                auto arguments_info = get_info(call.arguments, true);
                *arguments_info = candidate.matching_info;
                matching_info = arguments_info;
                candidate.matching_info.matched_parameters.size = 0;
                candidate.matching_info.matched_parameters.capacity = 0;
                candidate.matching_info.matched_parameters.data = nullptr;
            }
            else
            {
                // Log errors
                if (candidates.size > 1) {
                    log_semantic_error("Could not disambiguate between function overloads", call.expr);
                }
                else if (candidates.size == 0) {
                    log_semantic_error("None of the function overloads are valid", call.expr);
                }

                // Analyse remaining arguments as something else
                analyse_arguments_in_unknown_context(call.arguments);
                EXIT_ERROR(types.unknown_type);
            }
        }
        else
        {
            auto info = semantic_analyser_analyse_expression_any(call.expr, expression_context_make_auto_dereference());
            auto callable_opt = overload_candidate_try_create_from_expression_info(*info, call.expr);
            if (!callable_opt.available) 
            {
                if (!datatype_is_unknown(info->cast_info.result_type)) {
                    log_semantic_error("Expression is not callable!", upcast(call.expr));
                    log_error_info_expression_result_type(info->result_type);
                }
                else {
                    semantic_analyser_set_error_flag(true);
                }
                analyse_arguments_in_unknown_context(call.arguments);
                EXIT_ERROR(types.unknown_type);
            }

            auto arguments_info = get_info(call.arguments, true);
            *arguments_info = callable_opt.value.matching_info;
            matching_info = arguments_info;

            // Do Parameter-to-Argument mapping
            if (!arguments_match_to_parameters(call.arguments, matching_info, true)) {
                parameter_matching_analyse_in_unknown_context(matching_info);
                if (matching_info->has_return_value) {
                    EXIT_ERROR(matching_info->return_type);
                }
                EXIT_ERROR(types.unknown_type);
            }
        }

        // Handle hardcoded and polymorphic functions
        switch (matching_info->call_type)
        {
        case Call_Type::FUNCTION: info->specifics.function_call_signature = matching_info->options.function->signature; break;
        case Call_Type::DOT_CALL: info->specifics.function_call_signature = matching_info->options.dot_call_function->signature; break;
        case Call_Type::FUNCTION_POINTER: info->specifics.function_call_signature = matching_info->options.pointer_call; break;

        case Call_Type::HARDCODED: 
        {
            // Handle type-of call
            switch (matching_info->options.hardcoded)
            {
            case Hardcoded_Type::TYPE_OF:
            {
                auto& arg = call.arguments->arguments[0];
                auto arg_result = semantic_analyser_analyse_expression_any(arg->value, expression_context_make_unknown());
                switch (arg_result->result_type)
                {
                case Expression_Result_Type::VALUE: {
                    EXIT_TYPE(arg_result->options.type);
                }
                case Expression_Result_Type::HARDCODED_FUNCTION: {
                    log_semantic_error("Cannot use type_of on hardcoded functions!", arg->value);
                    EXIT_ERROR(types.unknown_type);
                }
                case Expression_Result_Type::NOTHING: {
                    log_semantic_error("Expected value", arg->value);
                    EXIT_ERROR(types.unknown_type);
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
                case Expression_Result_Type::DOT_CALL: {
                    log_semantic_error("Type of does not work on dot-calls", arg->value);
                    log_error_info_expression_result_type(arg_result->result_type);
                    EXIT_ERROR(types.unknown_type);
                }
                case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
                    log_semantic_error("Type of cannot handle polymorphic functions", arg->value);
                    log_error_info_expression_result_type(arg_result->result_type);
                    EXIT_ERROR(types.unknown_type);
                }
                case Expression_Result_Type::POLYMORPHIC_STRUCT: {
                    log_semantic_error("Cannot use type_of on polymorphic struct", arg->value);
                    log_error_info_expression_result_type(arg_result->result_type);
                    EXIT_ERROR(types.unknown_type);
                }
                default: panic("");
                }

                panic("");
                EXIT_ERROR(arg_result->options.type);
            }
            case Hardcoded_Type::BITWISE_NOT: 
            {
                info->specifics.bitwise_primitive_type = types.i32_type;
                if (!matching_info->matched_parameters[0].is_set) {
                    parameter_matching_analyse_in_unknown_context(matching_info);
                    EXIT_VALUE(upcast(types.i32_type), true);
                }

                auto arg_expr = matching_info->matched_parameters[0].expression;
                Datatype* type = analyse_parameter_if_not_already_done(&matching_info->matched_parameters[0], expression_context_make_auto_dereference());
                type = datatype_get_non_const_type(type);
                bool type_valid = type->type == Datatype_Type::PRIMITIVE;
                Datatype_Primitive* primitive = nullptr;
                if (type_valid) {
                    primitive = downcast<Datatype_Primitive>(type);
                    type_valid = primitive->primitive_type == Primitive_Type::INTEGER;
                }
                if (!type_valid) {
                    log_semantic_error("Type for bitwise not must be an integer", arg_expr);
                    log_error_info_given_type(type);
                    parameter_matching_analyse_in_unknown_context(matching_info);
                    EXIT_VALUE(upcast(types.i32_type), true);
                }
                info->specifics.bitwise_primitive_type = primitive;

                EXIT_VALUE(type, true);
            }
            case Hardcoded_Type::BITWISE_AND: 
            case Hardcoded_Type::BITWISE_OR: 
            case Hardcoded_Type::BITWISE_XOR: 
            case Hardcoded_Type::BITWISE_SHIFT_LEFT: 
            case Hardcoded_Type::BITWISE_SHIFT_RIGHT: 
            {
                info->specifics.bitwise_primitive_type = types.i32_type;
                if (!matching_info->matched_parameters[0].is_set || !matching_info->matched_parameters[1].is_set) {
                    parameter_matching_analyse_in_unknown_context(matching_info);
                    EXIT_VALUE(upcast(types.i32_type), true);
                }

                auto expr_a = matching_info->matched_parameters[0].expression;
                Datatype* type_a = analyse_parameter_if_not_already_done(&matching_info->matched_parameters[0], expression_context_make_auto_dereference());
                type_a = datatype_get_non_const_type(type_a);

                bool type_valid = type_a->type == Datatype_Type::PRIMITIVE;
                Datatype_Primitive* primitive = nullptr;
                if (type_valid) {
                    primitive = downcast<Datatype_Primitive>(type_a);
                    type_valid = primitive->primitive_type == Primitive_Type::INTEGER;
                }
                if (!type_valid) {
                    log_semantic_error("Type for bitwise operation must be an integer", expr_a);
                    log_error_info_given_type(type_a);
                    parameter_matching_analyse_in_unknown_context(matching_info);
                    EXIT_VALUE(upcast(types.i32_type), true);
                }
                info->specifics.bitwise_primitive_type = primitive;

                auto expr_b = matching_info->matched_parameters[1].expression;
                Datatype* type_b = analyse_parameter_if_not_already_done(&matching_info->matched_parameters[1], expression_context_make_specific_type(type_a));
                type_b = datatype_get_non_const_type(type_b);

                EXIT_VALUE(type_a, true);
            }
            }

            info->specifics.function_call_signature = hardcoded_type_to_signature(matching_info->options.hardcoded);
            break;
        }

        case Call_Type::POLYMORPHIC_FUNCTION:
        case Call_Type::POLYMORPHIC_STRUCT:
        case Call_Type::POLYMORPHIC_DOT_CALL:  
        {
            // Instanciate
            Instanciation_Result instance_result = instanciate_polymorphic_callable(matching_info, context, upcast(expr), Parser::Section::ENCLOSURE);
            switch (instance_result.type)
            {
            case Instanciation_Result_Type::FUNCTION:
            {
                // Store instanciation info in expression_info
                auto function = instance_result.options.function;
                auto call_expr_info = get_info(call.expr);
                if (call_expr_info->result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION) {
                    call_expr_info->options.polymorphic_function.instance_fn = function;
                }
                else if (call_expr_info->result_type == Expression_Result_Type::DOT_CALL) {
                    assert(call_expr_info->options.dot_call.is_polymorphic, "");
                    call_expr_info->options.dot_call.options.polymorphic.instance = function;
                }
                else {
                    panic("Hey");
                }
                semantic_analyser_register_function_call(function);

                // Store final function signature for code generation
                info->specifics.function_call_signature = function->signature;

                // Exit
                if (function->signature->return_type.available) {
                    EXIT_VALUE(function->signature->return_type.value, true);
                }
                else {
                    expression_info_set_no_value(info);
                    return info;
                }
                break;
            }
            case Instanciation_Result_Type::ERROR: {
                EXIT_ERROR(types.unknown_type);
            }
            case Instanciation_Result_Type::STRUCTURE: {
                EXIT_TYPE(upcast(instance_result.options.struct_type));
            }
            case Instanciation_Result_Type::STRUCT_INSTANCE_TEMPLATE: {
                EXIT_TYPE(upcast(instance_result.options.instance_template));
            }
            default: panic("");
            }

            panic("invalid code path");
            break;
        }

        case Call_Type::CONTEXT_OPTION:
        case Call_Type::INSTANCIATE:
        case Call_Type::STRUCT_INITIALIZER: {
            panic("Should not happen in normal function call!");
            break;
        }
        default: panic("");
        }

        // Analyse arguments
        auto function_signature = info->specifics.function_call_signature;
        assert(function_signature != 0, "");
        auto& params = function_signature->parameters;
        for (int i = 0; i < matching_info->matched_parameters.size; i++)
        {
            auto param_info = matching_info->matched_parameters[i];
            if (param_info.ignore_during_code_generation) {
                continue;
            }
            analyse_parameter_if_not_already_done(&param_info, expression_context_make_specific_type(function_signature->parameters[i].type));
        }

        // Exit with return type (if available)
        {
            if (function_signature->return_type.available) {
                EXIT_VALUE(function_signature->return_type.value, true);
            }
            else {
                expression_info_set_no_value(info);
                return info;
            }

            panic("Not a valid code path, the if before should terminate!");
            EXIT_ERROR(types.unknown_type);
        }
    }
    case AST::Expression_Type::INSTANCIATE:
    {
        auto& instanciate = expr->options.instanciate;
        auto& argument_nodes = expr->options.instanciate.arguments->arguments;
        if (argument_nodes.size == 0) {
            log_semantic_error("Instanciate must have at least one argument, first argument is the polymorphic function!", expr);
            analyse_arguments_in_unknown_context(instanciate.arguments);
            EXIT_ERROR(types.unknown_type);
        }

        auto first_expr = argument_nodes[0]->value;
        if (argument_nodes[0]->name.available) {
            log_semantic_error("Instanciate first argument must not have a name", upcast(argument_nodes[0]), Parser::Section::FIRST_TOKEN);
            analyse_arguments_in_unknown_context(instanciate.arguments);
            EXIT_ERROR(types.unknown_type);
        }

        // Analyse first argument
        auto expression_info = semantic_analyser_analyse_expression_any(first_expr, expression_context_make_unknown());
        if (expression_info->result_type != Expression_Result_Type::POLYMORPHIC_FUNCTION) {
            log_semantic_error("#instanciate only works on polymorphic functions", first_expr);
            log_error_info_expression_result_type(expression_info->result_type);
            analyse_arguments_in_unknown_context(instanciate.arguments);
            EXIT_ERROR(types.unknown_type);
        }

        // Generate matching info for further parameters
        Polymorphic_Base_Info* poly_base = &expression_info->options.polymorphic_function.base->base;
        auto matching_info = get_info(instanciate.arguments);
        *matching_info = parameter_matching_info_create_empty(Call_Type::INSTANCIATE, argument_nodes.size);
        matching_info->options.poly_function = expression_info->options.polymorphic_function.base;

        auto find_and_add_parameter = [](Parameter_Matching_Info* info, AST::Arguments* arguments, String* param_name) -> bool
        {
            AST::Argument* argument_node = nullptr;
            int index = 0;
            // Note: We start at index 1, as the first argument is always the function to instanciate
            for (int i = 1; i < arguments->arguments.size; i++)
            {
                auto arg = arguments->arguments[i];
                if (arg->name.available && arg->name.value == param_name) {
                    argument_node = arg;
                    index = i;
                    break;
                }
            }

            // Check if argument could be found
            if (argument_node == 0) {
                parameter_matching_info_add_param(info, param_name, true, true, nullptr);
                return false;
            }

            // Initialize arg info and add it to array
            parameter_matching_info_add_param(info, param_name, true, true, nullptr);
            auto& arg_info = info->matched_parameters[info->matched_parameters.size - 1];
            arg_info.is_set = true;
            arg_info.argument_index = index;
            arg_info.expression = argument_node->value;
            return true;
        };

        // Check that all comptime parameters are set
        bool parameters_missing = false;
        for (int i = 0; i < poly_base->parameter_nodes.size; i++)
        {
            auto& poly_parameter = poly_base->parameters[i];

            if (!poly_parameter.is_comptime)
            {
                parameter_matching_info_add_param(matching_info, poly_parameter.infos.name, true, false, nullptr);
                auto& arg_info = matching_info->matched_parameters[matching_info->matched_parameters.size - 1];
                // Only comptime parameters should be provided to instanciate, so in this case we have to "fake" the argument info, as no expression exists
                arg_info.is_set = true;
                arg_info.state = Parameter_State::ANALYSED;
                arg_info.argument_is_temporary_value = false;
                arg_info.argument_type = types.unknown_type; // Normally this value is never used
            }
            else {
                if (!find_and_add_parameter(matching_info, instanciate.arguments, poly_parameter.infos.name)) {
                    parameters_missing = true;
                }
            }
        }

        // Check that all implicit parameters are set
        for (int i = 0; i < poly_base->implicit_parameter_infos.size; i++) {
            auto& implicit = poly_base->implicit_parameter_infos[i];
            if (!find_and_add_parameter(matching_info, instanciate.arguments, implicit.id)) {
                parameters_missing = true;
            }
        }

        if (parameters_missing) {
            parameter_matching_analyse_in_unknown_context(matching_info);
            EXIT_ERROR(types.unknown_type);
        }

        // Otherwise try to instanciate function
        auto result = instanciate_polymorphic_callable(
            matching_info, expression_context_make_unknown(), upcast(instanciate.arguments), Parser::Section::ENCLOSURE
        );
        if (result.type == Instanciation_Result_Type::FUNCTION) {
            expression_info->options.polymorphic_function.instance_fn = result.options.function;
            EXIT_FUNCTION(result.options.function);
        }
        else {
            EXIT_ERROR(types.unknown_type);
        }

        panic("");
        break;
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
            EXIT_ERROR(types.unknown_type);
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
            operand_context = expression_context_make_specific_type(destination_type, cast->is_pointer_cast ? Cast_Mode::POINTER_EXPLICIT : Cast_Mode::EXPLICIT);
        }
        else
        {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
                operand_context = expression_context_make_specific_type(context.expected_type.type, cast->is_pointer_cast ? Cast_Mode::POINTER_INFERRED : Cast_Mode::INFERRED);
            }
            else {
                log_semantic_error("No context is available for auto cast", expr);
                operand_context = expression_context_make_unknown(true);
            }
        }

        auto result_type = semantic_analyser_analyse_expression_value(cast->operand, operand_context);
        EXIT_VALUE(result_type, get_info(cast->operand)->cast_info.result_value_is_temporary);
    }
    case AST::Expression_Type::LITERAL_READ:
    {
        auto& read = expr->options.literal_read;
        void* value_ptr;
        Datatype* literal_type;

        // Variables which can hold the specified values
        void* value_nullptr = 0;
        Upp_String value_string;
        u8 value_u8;
        u16 value_u16;
        u32 value_u32;
        u64 value_u64;
        i8  value_i8;
        i16 value_i16;
        i32 value_i32;
        i32 value_i64;
        f32 value_f32;
        f64 value_f64;

        switch (read.type)
        {
        case Literal_Type::BOOLEAN:
            literal_type = upcast(types.bool_type);
            value_ptr = &read.options.boolean;
            break;
        case Literal_Type::FLOAT_VAL:
        {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED && 
                types_are_equal(datatype_get_non_const_type(context.expected_type.type), upcast(types.f64_type)))
            {
                literal_type = context.expected_type.type;
                value_ptr = &read.options.float_val;
            }
            else
            {
                literal_type = upcast(types.f32_type);
                value_f32 = (float)read.options.float_val;
                value_ptr = &value_f32;
            }
            break;
        }
        case Literal_Type::INTEGER:
        {
            bool check_for_auto_conversion = false;
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
            {
                Datatype* expected = datatype_get_non_const_type(context.expected_type.type);
                if (expected->type == Datatype_Type::PRIMITIVE)
                {
                    auto primitive_type = downcast<Datatype_Primitive>(expected)->primitive_type;
                    if (primitive_type == Primitive_Type::INTEGER) {
                        check_for_auto_conversion = true;
                    }
                    else if (primitive_type == Primitive_Type::FLOAT)
                    {
                        if (expected->memory_info.value.size == 4) {
                            literal_type = upcast(types.f32_type);
                            value_f32 = (float)read.options.int_val;
                            value_ptr = &value_f32;
                        }
                        else if (expected->memory_info.value.size == 8) {
                            literal_type = upcast(types.f64_type);
                            value_f64 = (double)read.options.int_val;
                            value_ptr = &value_f64;
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
                Datatype* expected = datatype_get_non_const_type(context.expected_type.type);
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
                        value_i8 = (i8)value;
                        value_ptr = &value_i8;
                        size_is_valid = value <= INT8_MAX && value >= INT8_MIN;
                        break;
                    }
                    case 2: {
                        literal_type = upcast(types.i16_type);
                        value_i16 = (i16)value;
                        value_ptr = &value_i16;
                        size_is_valid = value <= INT16_MAX && value >= INT16_MIN;
                        break;
                    }
                    case 4: {
                        literal_type = upcast(types.i32_type);
                        value_i32 = (i32)value;
                        value_ptr = &value_i32;
                        size_is_valid = value <= INT32_MAX && value >= INT32_MIN;
                        break;
                    }
                    case 8: {
                        literal_type = upcast(types.i64_type);
                        value_i64 = (i64)value;
                        value_ptr = &value_i64;
                        size_is_valid = true; // Cannot check size as i64 is the max value of the lexer
                        break;
                    }
                    default: panic("");
                    }
                }
                else
                {
                    if (read.options.int_val < 0) {
                        log_semantic_error("Using a negative literal in an unsigned context requires a cast", expr);
                        EXIT_ERROR(context.expected_type.type);
                    }

                    u64 value = (u64)read.options.int_val;
                    switch (size)
                    {
                    case 1: {
                        literal_type = upcast(types.u8_type);
                        value_u8 = (u8)value;
                        value_ptr = &value_u8;
                        size_is_valid = value <= UINT8_MAX;
                        break;
                    }
                    case 2: {
                        literal_type = upcast(types.u16_type);
                        value_u16 = (u16)value;
                        value_ptr = &value_u16;
                        size_is_valid = value <= UINT16_MAX;
                        break;
                    }
                    case 4: {
                        literal_type = upcast(types.u32_type);
                        value_u32 = (u32)value;
                        value_ptr = &value_u32;
                        size_is_valid = value <= UINT32_MAX;
                        break;
                    }
                    case 8: {
                        literal_type = upcast(types.u64_type);
                        value_u64 = (u64)value;
                        value_ptr = &value_u64;
                        size_is_valid = value <= UINT64_MAX;
                        break;
                    }
                    default: panic("");
                    }
                }

                if (!size_is_valid)
                {
                    log_semantic_error("Literal value is outside the range of expected type. To still use this value a cast is required", expr);
                    EXIT_ERROR(type_system_make_constant(context.expected_type.type));
                }
                literal_type = context.expected_type.type;
            }
            else
            {
                literal_type = upcast(types.i32_type);
                value_i32 = (i32)read.options.int_val;
                value_ptr = &value_i32;
            }
            break;
        }
        case Literal_Type::STRING: {
            String* string = read.options.string;
            value_string.bytes.size = string->size + 1;
            value_string.bytes.data = (const u8*)string->characters;

            literal_type = upcast(types.string);
            value_ptr = &value_string;
            break;
        }
        case Literal_Type::NULL_VAL:
        {
            literal_type = types.byte_pointer_optional;
            value_ptr = &value_nullptr;
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
            {
                bool is_const = context.expected_type.type->mods.is_constant;
                Datatype* expected = datatype_get_non_const_type(context.expected_type.type);
                bool is_optional_pointer = false;
                bool is_pointer = datatype_is_pointer(expected, &is_const);

                // Special handling for null, so that we can assign null to values
                // in cast_pointer null and cast_pointer{*int}null
                if (context.expected_type.cast_mode == Cast_Mode::POINTER_EXPLICIT || context.expected_type.cast_mode == Cast_Mode::POINTER_INFERRED)
                {
                    if (is_pointer) {
                        literal_type = context.expected_type.type;
                    }
                }
                else if (is_pointer && is_optional_pointer) {
                    literal_type = context.expected_type.type;
                }
                else if (expected->type == Datatype_Type::OPTIONAL_TYPE) 
                {
                    Datatype_Optional* opt = downcast<Datatype_Optional>(expected);
                    type_wait_for_size_info_to_finish(upcast(opt));
                    int size = opt->base.memory_info.value.size;

                    byte* memory = (byte*) malloc(size);
                    SCOPE_EXIT(free(memory));
                    memory_set_bytes(memory, size, 0);

                    expression_info_set_constant(info, expected, array_create_static<byte>(memory, size), AST::upcast(expr));
                    return info;
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

        String* enum_name = compiler.predefined_ids.anon_enum;
        if (expr->base.parent->type == AST::Node_Type::DEFINITION) {
            AST::Definition* definition = downcast<AST::Definition>(expr->base.parent);
            if (definition->is_comptime && definition->symbols.size == 1) {
                enum_name = definition->symbols[0]->name;
            }
        }

        Datatype_Enum* enum_type = type_system_make_enum_empty(enum_name);
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
        EXIT_ERROR(types.unknown_type);
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
                EXIT_ERROR(types.unknown_type);
            }
        }
        panic("invalid_code_path");
        EXIT_ERROR(types.unknown_type);
    }
    case AST::Expression_Type::FUNCTION_SIGNATURE:
    {
        auto& sig = expr->options.function_signature;
        Dynamic_Array<Function_Parameter> parameters = dynamic_array_create<Function_Parameter>(sig.parameters.size);
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
            dynamic_array_push_back(&parameters, parameter);
        }
        Datatype* return_type = 0;
        if (sig.return_value.available) {
            return_type = semantic_analyser_analyse_expression_type(sig.return_value.value);
        }
        EXIT_TYPE(upcast(type_system_make_function(parameters, return_type)));
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
        Datatype_Template_Parameter* polymorphic_count = 0;

        auto comptime = expression_calculate_comptime_value(array_node.size_expr, "Array size must be know at compile time");
        if (comptime.available)
        {
            array_size_known = true;
            array_size = upp_constant_to_value<int>(comptime.value);
            if (array_size <= 0) {
                log_semantic_error("Array size must be greater than zero", array_node.size_expr);
                array_size_known = false;
            }
        }
        else if (array_node.size_expr->type == AST::Expression_Type::TEMPLATE_PARAMETER)
        {
            auto polymorphic_type_opt = hashtable_find_element(&semantic_analyser.valid_template_parameters, array_node.size_expr);
            if (polymorphic_type_opt == 0) {
                log_semantic_error("Implicit polymorphic parameter only valid in function header!", expr);
            }
            else {
                auto poly_type = *polymorphic_type_opt;
                assert(!poly_type->is_reference, "We can never be the reference if we are at the symbol, e.g. $T is not a symbol-read like T");
                polymorphic_count = poly_type;
            }
        }

        Datatype* element_type = semantic_analyser_analyse_expression_type(array_node.type_expr);
        auto result = type_system_make_array(element_type, array_size_known, array_size);

        // Handle implicit polymorphic symbols for array size, e.g. foo :: (a: [$C]int)
        if (polymorphic_count != 0) {
            auto array_type = downcast<Datatype_Array>(datatype_get_non_const_type(result));
            array_type->polymorphic_count_variable = polymorphic_count;
            array_type->base.contains_type_template = true;
        }

        EXIT_TYPE(upcast(result));
    }
    case AST::Expression_Type::SLICE_TYPE: {
        EXIT_TYPE(upcast(type_system_make_slice(semantic_analyser_analyse_expression_type(expr->options.slice_type))));
    }
    case AST::Expression_Type::CONST_TYPE: {
        EXIT_TYPE(upcast(type_system_make_constant(semantic_analyser_analyse_expression_type(expr->options.const_type))));
    }
    case AST::Expression_Type::OPTIONAL_TYPE: 
    {
        auto child_type = semantic_analyser_analyse_expression_type(expr->options.optional_child_type);

        // Handle optional byte-pointer and function_pointer
        bool is_constant = child_type->mods.is_constant;
        child_type = datatype_get_non_const_type(child_type);
        if (datatype_get_non_const_type(child_type)->type == Datatype_Type::BYTE_POINTER) {
            auto byte_ptr = downcast<Datatype_Bytepointer>(child_type);
            if (!byte_ptr->is_optional) {
                if (is_constant) {
                    EXIT_TYPE(type_system_make_constant(types.byte_pointer_optional));
                }
                EXIT_TYPE(types.byte_pointer_optional);
            }
        }
        else if (datatype_get_non_const_type(child_type)->type == Datatype_Type::FUNCTION) {
            auto function_type = downcast<Datatype_Function>(child_type);
            if (!function_type->is_optional) {
                function_type = type_system_make_function_optional(function_type);
                if (is_constant) {
                    EXIT_TYPE(type_system_make_constant(upcast(function_type)));
                }
                EXIT_TYPE(upcast(function_type));
            }
        }

        EXIT_TYPE(upcast(type_system_make_optional(child_type)));
    }
    case AST::Expression_Type::OPTIONAL_POINTER: 
    {
        auto child_type = semantic_analyser_analyse_expression_type(expr->options.optional_child_type);
        EXIT_TYPE(upcast(type_system_make_pointer(child_type, true)));
    }
    case AST::Expression_Type::OPTIONAL_CHECK: {
        auto value_type = semantic_analyser_analyse_expression_value(expr->options.optional_check_value, expression_context_make_unknown());
        auto non_const = datatype_get_non_const_type(value_type);
        bool is_pointer = datatype_is_pointer(non_const);
        if (!is_pointer) {
            log_semantic_error("Optional-Check is only valid on pointers currently", expr, Parser::Section::FIRST_TOKEN);
            log_error_info_given_type(value_type);
            EXIT_ERROR(upcast(types.bool_type));
        }
        EXIT_VALUE(upcast(types.bool_type), true);
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
        EXIT_VALUE(result_type, true);
    }
    case AST::Expression_Type::STRUCT_INITIALIZER:
    {
        auto& init_node = expr->options.struct_initializer;

        Datatype* type_for_init = nullptr;
        if (init_node.type_expr.available) {
            type_for_init = semantic_analyser_analyse_expression_type(init_node.type_expr.value);
        }
        else {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
                type_for_init = context.expected_type.type;
            }
            else {
                if (!context.unknown_due_to_error) {
                    log_semantic_error("Could not determine type for auto struct initializer from context", expr, Parser::Section::WHOLE_NO_CHILDREN);
                }
                type_for_init = types.unknown_type;
            }
        }

        // Make sure that type is a struct
        type_for_init = datatype_get_non_const_type(type_for_init);
        type_wait_for_size_info_to_finish(type_for_init);
        if (type_for_init->type == Datatype_Type::STRUCT || type_for_init->type == Datatype_Type::SUBTYPE)
        {
            Datatype_Struct* struct_type = downcast<Datatype_Struct>(type_for_init->base_type);
            Struct_Content* content = type_mods_get_subtype(struct_type, type_for_init->mods);
            Subtype_Index* final_subtype = type_for_init->mods.subtype_index;
            analyse_member_initializer_recursive(init_node.arguments, struct_type, content, 0, &final_subtype);

            // Create result type
            auto final_type = type_system_make_type_with_mods(upcast(struct_type), type_mods_make(false, 0, 0, 0, final_subtype));
            EXIT_VALUE(final_type, true);
        }
        else
        {
            if (type_for_init->type != Datatype_Type::UNKNOWN_TYPE) {
                log_semantic_error("Struct initializer requires struct type for initialization", expr, Parser::Section::WHOLE_NO_CHILDREN);
                log_error_info_given_type(type_for_init);
                type_for_init = types.unknown_type;
            }
            analyse_member_initializers_in_unknown_context_recursive(init_node.arguments);
        }
        EXIT_ERROR(type_for_init);
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
                Datatype* expected = datatype_get_non_const_type(context.expected_type.type);

                if (expected->type == Datatype_Type::ARRAY) {
                    element_type = downcast<Datatype_Array>(expected)->element_type;
                }
                else if (expected->type == Datatype_Type::SLICE) {
                    element_type = downcast<Datatype_Slice>(expected)->element_type;
                }
                else {
                    element_type = 0;
                }
            }
            if (element_type == 0) {
                log_semantic_error("Could not determine array element type from context", expr);
                element_type = types.unknown_type;
            }
        }

        int array_element_count = init_node.values.size;
        // There are no 0-sized arrays, only 0-sized slices. So if we encounter an empty initializer, e.g. type.[], we return an empty slice
        if (array_element_count == 0) {
            Datatype* result_type = upcast(type_system_make_slice(element_type));
            EXIT_VALUE(result_type, true);
        }

        for (int i = 0; i < init_node.values.size; i++) {
            semantic_analyser_analyse_expression_value(init_node.values[i], expression_context_make_specific_type(element_type));
        }
        Datatype* result_type = upcast(type_system_make_array(element_type, true, array_element_count));
        EXIT_VALUE(result_type, true);
    }
    case AST::Expression_Type::ARRAY_ACCESS:
    {
        info->specifics.overload.function = 0;
        info->specifics.overload.switch_left_and_right = false;

        auto& access_node = expr->options.array_access;
        Datatype* array_type = semantic_analyser_analyse_expression_value(access_node.array_expr, expression_context_make_auto_dereference());
        bool array_is_const = array_type->mods.is_constant;
        array_type = array_type->base_type; // Remove const modifier
        if (datatype_is_unknown(array_type)) {
            EXIT_ERROR(types.unknown_type);
        }

        bool type_is_valid = false;
        Datatype* result_type = types.unknown_type;
        bool result_is_temporary = false;
        Type_Mods expected_mods = type_mods_make(true, 0, 0, 0);
        if (array_type->type == Datatype_Type::ARRAY || array_type->type == Datatype_Type::SLICE)
        {
            type_is_valid = true;
            if (array_type->type == Datatype_Type::ARRAY) {
                result_type = downcast<Datatype_Array>(array_type)->element_type;
                result_is_temporary = get_info(access_node.array_expr)->cast_info.result_value_is_temporary;
                if (array_is_const) {
                    result_type = type_system_make_constant(result_type); // If the array is const, the values are also const
                }
            }
            else {
                result_type = downcast<Datatype_Slice>(array_type)->element_type;
                result_is_temporary = false;
            }

            semantic_analyser_analyse_expression_value(
                access_node.index_expr, expression_context_make_specific_type(upcast(types.i32_type))
            );
        }

        // Check for operator overloads
        auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;
        if (!type_is_valid)
        {
            result_is_temporary = true; // When calling a custom function the return type is for sure temporary
            Custom_Operator_Key key;
            key.type = AST::Context_Change_Type::ARRAY_ACCESS;
            key.options.array_access.array_type = array_type;
            Custom_Operator* overload = operator_context_query_custom_operator(operator_context, key);
            if (overload != 0)
            {
                auto& custom_access = overload->array_access;
                assert(!custom_access.is_polymorphic, "");
                auto function = custom_access.options.function;
                semantic_analyser_analyse_expression_value(
                    access_node.index_expr,
                    expression_context_make_specific_type(upcast(function->signature->parameters[1].type))
                );
                type_is_valid = true;
                expected_mods = custom_access.options.function->signature->parameters[0].type->mods;
                assert(function->signature->return_type.available, "");
                result_type = function->signature->return_type.value;
                info->specifics.overload.function = function;
            }
        }

        // Check for polymorphic operator overload
        if (!type_is_valid && array_type->type == Datatype_Type::STRUCT)
        {
            auto struct_type = downcast<Datatype_Struct>(array_type);
            if (struct_type->workload != 0 && struct_type->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE)
            {
                Custom_Operator_Key key;
                key.type = AST::Context_Change_Type::ARRAY_ACCESS;
                key.options.array_access.array_type = upcast(struct_type->workload->polymorphic.instance.parent->body_workload->struct_type);

                Custom_Operator* overload = operator_context_query_custom_operator(operator_context, key);
                if (overload != 0)
                {
                    auto& array_access = overload->array_access;
                    assert(array_access.is_polymorphic, "Must be the case for base structure");

                    Parameter_Matching_Info matching_info = parameter_matching_info_create_empty(Call_Type::POLYMORPHIC_FUNCTION, 1);
                    SCOPE_EXIT(parameter_matching_info_destroy(&matching_info));
                    matching_info.options.poly_function = array_access.options.poly_base;
                    parameter_matching_info_add_analysed_param(&matching_info, access_node.array_expr);
                    parameter_matching_info_add_unanalysed_param(&matching_info, access_node.index_expr);
                    Instanciation_Result result = instanciate_polymorphic_callable(&matching_info, context, upcast(expr), Parser::Section::ENCLOSURE);
                    if (result.type == Instanciation_Result_Type::FUNCTION) {
                        type_is_valid = true;
                        info->specifics.overload.function = result.options.function;
                        result_type = result.options.function->signature->return_type.value;
                        expected_mods = result.options.function->signature->parameters[0].type->mods; // Not sure if this works after poly-instanciation
                    }
                }
            }
        }

        if (type_is_valid) {
            if (!try_updating_expression_type_mods(access_node.array_expr, expected_mods)) {
                type_is_valid = false;
            }
        }
        if (!type_is_valid) {
            log_semantic_error("Type not valid for array access", access_node.array_expr);
            log_error_info_given_type(array_type);
            EXIT_ERROR(types.unknown_type);
        }
        EXIT_VALUE(result_type, result_is_temporary);
    }
    case AST::Expression_Type::MEMBER_ACCESS:
    {
        auto& member_node = expr->options.member_access;
        auto access_expr_info = semantic_analyser_analyse_expression_any(member_node.expr, expression_context_make_unknown());
        bool result_is_temporary = get_info(member_node.expr)->cast_info.result_value_is_temporary;
        auto& ids = compiler.predefined_ids;

        info->specifics.member_access.type = Member_Access_Type::STRUCT_MEMBER_ACCESS;
        auto search_struct_type_for_polymorphic_parameter_access = [&](Datatype_Struct* struct_type) -> Optional<Polymorphic_Value> {
            if (struct_type->workload == 0) {
                return optional_make_failure<Polymorphic_Value>();
            }

            auto struct_workload = struct_type->workload;
            Polymorphic_Base_Info* polymorphic = 0;
            if (struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE)
            {
                polymorphic = &struct_workload->polymorphic.base->info;
                // I believe other workloads need to wait for base to finish... e.g. x: Node(int).T --> T needs to wait for header to finish
                if (semantic_analyser.current_workload != upcast(struct_workload->polymorphic.base)) {
                    analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(struct_workload->polymorphic.base));
                    workload_executer_wait_for_dependency_resolution();
                }
            }
            else if (struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
                polymorphic = &struct_workload->polymorphic.instance.parent->info;
            }
            else {
                return optional_make_failure<Polymorphic_Value>();
            }

            // Try to find structure parameter with this base_name
            int value_access_index = -1;
            for (int i = 0; i < polymorphic->parameters.size; i++) {
                auto& parameter = polymorphic->parameters[i];
                if (parameter.infos.name == member_node.name) {
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
                info->specifics.member_access.options.poly_access.index = value_access_index;
                info->specifics.member_access.options.poly_access.struct_workload = struct_workload;

                return optional_make_success(struct_workload->base.polymorphic_values[value_access_index]);
            }

            return optional_make_failure<Polymorphic_Value>();
        };

        switch (access_expr_info->result_type)
        {
        case Expression_Result_Type::TYPE:
        {
            bool is_const = access_expr_info->options.type->type == Datatype_Type::CONSTANT;
            Datatype* datatype = datatype_get_non_const_type(access_expr_info->options.type);

            // Handle Struct-Subtypes and polymorphic value access, e.g. Node.Expression / Node(int).T
            if (datatype->mods.pointer_level == 0 &&
                (datatype->base_type->type == Datatype_Type::STRUCT_INSTANCE_TEMPLATE || datatype->base_type->type == Datatype_Type::STRUCT))
            {
                auto base_type = datatype->base_type;
                Datatype_Struct* base_struct;
                if (base_type->type == Datatype_Type::STRUCT) {
                    base_struct = downcast<Datatype_Struct>(base_type);
                }
                else {
                    base_struct = downcast<Datatype_Struct_Instance_Template>(base_type)->struct_base->body_workload->struct_type;
                }

                // Check if it's a polymorphic parameter access
                auto poly_parameter_access = search_struct_type_for_polymorphic_parameter_access(base_struct);
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

                // Check if it's a valid subtype
                Struct_Content* content = type_mods_get_subtype(base_struct, datatype->mods);
                int subtype_index = -1;
                for (int i = 0; i < content->subtypes.size; i++) {
                    if (content->subtypes[i]->name == member_node.name) {
                        subtype_index = i;
                    }
                }

                if (subtype_index != -1) {
                    Datatype* result = type_system_make_subtype(datatype, member_node.name, subtype_index);
                    if (is_const) { // Not sure if this makes sense here...
                        result = type_system_make_constant(result);
                    }
                    info->specifics.member_access.type = Member_Access_Type::STRUCT_SUBTYPE;
                    EXIT_TYPE(result);
                }
            }

            if (datatype_is_unknown(datatype)) {
                semantic_analyser_set_error_flag(true);
                EXIT_ERROR(types.unknown_type);
            }

            if (datatype->type != Datatype_Type::ENUM) {
                log_semantic_error("Member access for given type not possible", member_node.expr);
                log_error_info_given_type(datatype);
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
            EXIT_ERROR(types.unknown_type);
        }
        case Expression_Result_Type::DOT_CALL: {
            log_semantic_error("Cannot use member access ('x.y') on dot calls", member_node.expr);
            log_error_info_expression_result_type(access_expr_info->result_type);
            EXIT_ERROR(types.unknown_type);
        }
        case Expression_Result_Type::FUNCTION:
        case Expression_Result_Type::HARDCODED_FUNCTION:
        case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
            log_semantic_error("Cannot use member access ('x.y') on functions", member_node.expr);
            log_error_info_expression_result_type(access_expr_info->result_type);
            EXIT_ERROR(types.unknown_type);
        }
        case Expression_Result_Type::POLYMORPHIC_STRUCT: {
            log_semantic_error("Cannot access members of uninstanciated polymorphic struct", member_node.expr);
            log_error_info_expression_result_type(access_expr_info->result_type);
            EXIT_ERROR(types.unknown_type);
        }
        case Expression_Result_Type::VALUE:
        case Expression_Result_Type::CONSTANT:
        {
            auto& access_info = info->specifics.member_access;
            auto datatype = access_expr_info->cast_info.result_type;

            // Handle optional .value access
            if (member_node.name == ids.value)
            {
                if (datatype->mods.pointer_level > 0 && datatype->mods.optional_flags != 0) 
                {
                    Datatype* first_opt_ptr = datatype;
                    int deref_count = 0;
                    while (first_opt_ptr != nullptr)
                    {
                        if (first_opt_ptr->type == Datatype_Type::POINTER) {
                            auto ptr = downcast<Datatype_Pointer>(first_opt_ptr);
                            if (ptr->is_optional) {
                                break;
                            }
                            else {
                                first_opt_ptr = ptr->element_type;
                                deref_count += 1;
                                continue;
                            }
                        }
                        else if (first_opt_ptr->type == Datatype_Type::CONSTANT) {
                            first_opt_ptr = downcast<Datatype_Constant>(first_opt_ptr)->element_type;
                            continue;
                        }
                        else {
                            panic("");
                        }
                    }

                    info->specifics.member_access.type = Member_Access_Type::OPTIONAL_PTR_ACCESS;
                    info->specifics.member_access.options.optional_deref_count = deref_count;
                    bool is_temporary = false;
                    if (deref_count == 0) {
                        is_temporary = get_info(member_node.expr)->cast_info.result_value_is_temporary;
                    }
                    EXIT_VALUE(upcast(type_system_make_pointer(downcast<Datatype_Pointer>(first_opt_ptr)->element_type, false)), is_temporary);
                }

                // Optional function/byte pointers
                bool is_optional;
                bool is_pointer = datatype_is_pointer(datatype_get_non_const_type(datatype), &is_optional);
                if (is_optional && is_pointer) 
                {
                    bool is_temporary = get_info(member_node.expr)->cast_info.result_value_is_temporary;
                    info->specifics.member_access.type = Member_Access_Type::OPTIONAL_PTR_ACCESS;
                    info->specifics.member_access.options.optional_deref_count = 0;

                    bool is_const = datatype->mods.is_constant;
                    datatype = datatype_get_non_const_type(datatype);
                    Datatype* result_type = 0;
                    if (datatype->type == Datatype_Type::BYTE_POINTER) {
                        result_type = types.byte_pointer;
                    }
                    else if (datatype->type == Datatype_Type::FUNCTION) {
                        result_type = upcast(downcast<Datatype_Function>(datatype)->non_optional_type);
                    }
                    else {
                        panic("");
                    }

                    if (is_const) {
                        result_type = type_system_make_constant(result_type);
                    }
                    EXIT_VALUE(result_type, is_temporary);
                }
            }

            if (datatype->mods.pointer_level > 0)
            {
                const char* error_msg = "";
                if (!try_updating_expression_type_mods(
                    member_node.expr, type_mods_make(datatype->mods.is_constant, 0, 0, 0, datatype->mods.subtype_index), &error_msg)) 
                {
                    log_semantic_error(error_msg, expr, Parser::Section::WHOLE_NO_CHILDREN);
                }
            }

            bool is_const = datatype->type == Datatype_Type::CONSTANT;
            datatype = datatype_get_non_const_type(datatype);
            if (datatype_is_unknown(datatype)) {
                semantic_analyser_set_error_flag(true);
                EXIT_ERROR(types.unknown_type);
            }

            // Check for normal member accesses (Struct members + array/slice members) (Not overloads)
            if (datatype->base_type->type == Datatype_Type::STRUCT)
            {
                Datatype_Struct* structure = downcast<Datatype_Struct>(datatype->base_type);

                // Search for poly_parameter access
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

                type_wait_for_size_info_to_finish(datatype);
                Struct_Content* content = type_mods_get_subtype(structure, datatype->mods);

                // Check tag access
                if (content->subtypes.size > 0 && member_node.name == ids.tag)
                {
                    access_info.type = Member_Access_Type::STRUCT_MEMBER_ACCESS;
                    access_info.options.member = content->tag_member;
                    if (is_const) {
                        access_info.options.member.type = type_system_make_constant(access_info.options.member.type);
                    }
                    EXIT_VALUE(access_info.options.member.type, result_is_temporary);
                }

                // Check member access
                for (int i = 0; i < content->members.size; i++)
                {
                    auto& member = content->members[i];
                    if (member.id == member_node.name)
                    {
                        access_info.type = Member_Access_Type::STRUCT_MEMBER_ACCESS;
                        access_info.options.member = member;
                        if (is_const) {
                            access_info.options.member.type = type_system_make_constant(access_info.options.member.type);
                        }
                        EXIT_VALUE(access_info.options.member.type, result_is_temporary);
                    }
                }

                // Check subtype access
                for (int i = 0; i < content->subtypes.size; i++)
                {
                    auto subtype = content->subtypes[i];
                    if (subtype->name == member_node.name)
                    {
                        access_info.type = Member_Access_Type::STRUCT_UP_OR_DOWNCAST;
                        assert(datatype->type == Datatype_Type::STRUCT || datatype->type == Datatype_Type::SUBTYPE, "");
                        auto result_type = type_system_make_subtype(datatype, subtype->name, i);
                        if (is_const) {
                            result_type = type_system_make_constant(result_type);
                        }
                        EXIT_VALUE(result_type, result_is_temporary);
                    }
                }

                // Check upper-type access
                if (datatype->mods.subtype_index->indices.size > 0)
                {
                    auto parent_subtype = type_mods_get_subtype(structure, datatype->mods, datatype->mods.subtype_index->indices.size - 1);
                    if (parent_subtype->name == member_node.name)
                    {
                        access_info.type = Member_Access_Type::STRUCT_UP_OR_DOWNCAST;
                        assert(datatype->type == Datatype_Type::SUBTYPE, "");
                        auto result_type = downcast<Datatype_Subtype>(datatype)->base_type;
                        if (is_const) {
                            result_type = type_system_make_constant(result_type);
                        }
                        EXIT_VALUE(result_type, result_is_temporary);
                    }
                }
            }
            else if ((datatype->type == Datatype_Type::ARRAY || datatype->type == Datatype_Type::SLICE) && (member_node.name == ids.size || member_node.name == ids.data))
            {
                if (datatype->type == Datatype_Type::ARRAY)
                {
                    auto array = downcast<Datatype_Array>(datatype);
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
                        EXIT_VALUE(upcast(type_system_make_pointer(array->element_type)), true);
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
                    if (is_const) {
                        member.type = type_system_make_constant(member.type);
                    }
                    info->specifics.member_access.type = Member_Access_Type::STRUCT_MEMBER_ACCESS;
                    info->specifics.member_access.options.member = member;
                    EXIT_VALUE(member.type, result_is_temporary);
                }
            }
            else if (datatype->type == Datatype_Type::OPTIONAL_TYPE)
            {
                type_wait_for_size_info_to_finish(datatype);
                auto opt = downcast<Datatype_Optional>(datatype);
                if (member_node.name == ids.value) 
                {
                    info->specifics.member_access.type = Member_Access_Type::STRUCT_MEMBER_ACCESS;
                    info->specifics.member_access.options.member = opt->value_member;
                    Datatype* result_type = opt->value_member.type;
                    if (is_const) {
                        result_type = type_system_make_constant(result_type);
                    }
                    EXIT_VALUE(result_type, result_is_temporary)
                }
                else if (member_node.name == ids.is_available)
                {
                    info->specifics.member_access.type = Member_Access_Type::STRUCT_MEMBER_ACCESS;
                    info->specifics.member_access.options.member = opt->is_available_member;
                    Datatype* result_type = opt->is_available_member.type;
                    if (is_const) {
                        result_type = type_system_make_constant(result_type);
                    }
                    EXIT_VALUE(result_type, result_is_temporary)
                }
            }

            // Check for dot-calls/custom member accesses
            {
                Custom_Operator_Key key;
                key.type = AST::Context_Change_Type::DOT_CALL;
                key.options.dot_call.datatype = datatype->base_type;
                key.options.dot_call.id = member_node.name;

                auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;
                Custom_Operator* custom_operator = operator_context_query_custom_operator(operator_context, key);

                // Check for polymorphic overload if normal overload was not found
                if (custom_operator == 0 && datatype->base_type->type == Datatype_Type::STRUCT)
                {
                    auto structure = downcast<Datatype_Struct>(datatype->base_type);
                    if (structure->workload != 0 && structure->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
                        auto base_struct = structure->workload->polymorphic.instance.parent->body_workload->struct_type;
                        key.options.dot_call.datatype = upcast(base_struct);
                        custom_operator = operator_context_query_custom_operator(operator_context, key);
                    }
                }

                if (custom_operator != 0)
                {
                    auto& dotcall = custom_operator->dot_call;
                    // Type and name have to be correct to even get here
                    if (try_updating_expression_type_mods(member_node.expr, dotcall.mods)) {
                        if (custom_operator->dot_call.dot_call_as_member_access)
                        {
                            ModTree_Function* function = nullptr;
                            if (dotcall.is_polymorphic)
                            {
                                Parameter_Matching_Info matching_info = parameter_matching_info_create_empty(Call_Type::POLYMORPHIC_DOT_CALL, 1);
                                SCOPE_EXIT(parameter_matching_info_destroy(&matching_info));
                                matching_info.options.poly_dotcall = dotcall.options.poly_base;
                                parameter_matching_info_add_analysed_param(&matching_info, member_node.expr);

                                // Instanciate
                                Instanciation_Result instance_result = instanciate_polymorphic_callable(
                                    &matching_info, context, upcast(expr), Parser::Section::WHOLE_NO_CHILDREN
                                );
                                if (instance_result.type == Instanciation_Result_Type::FUNCTION) {
                                    function = instance_result.options.function;
                                }
                                else {
                                    EXIT_ERROR(types.unknown_type);
                                }
                            }
                            else {
                                function = dotcall.options.function;
                            }

                            // Set dot-call in Expression-Info and return
                            if (function->signature->return_type.available) {
                                expression_info_set_value(info, function->signature->return_type.value, true);
                            }
                            else {
                                expression_info_set_no_value(info);
                            }
                            info->specifics.member_access.type = Member_Access_Type::DOT_CALL_AS_MEMBER;
                            info->specifics.member_access.options.dot_call_function = function;
                            return info;
                        }
                        else {
                            info->specifics.member_access.type = Member_Access_Type::DOT_CALL;
                            expression_info_set_dot_call(info, member_node.expr, custom_operator);
                            return info;
                        }
                    }
                }
            }

            // Error if no member access was found
            log_semantic_error("Member access is not valid", expr);
            log_error_info_id(member_node.name);
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
            if (!context.unknown_due_to_error) {
                log_semantic_error("Could not determine context for auto enum", expr);
            }
            EXIT_ERROR(types.unknown_type);
        }
        Datatype* expected = context.expected_type.type;

        if (expected->type != Datatype_Type::ENUM) {
            log_semantic_error("Context requires a type that is not an enum, so .NAME syntax is not valid", expr);
            log_error_info_given_type(context.expected_type.type);
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
            log_semantic_error("Enum does not contain this member", expr);
            log_error_info_id(id);
        }
        else {
            value = found->value;
        }

        expression_info_set_constant_enum(info, expected, value);
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
            Expression_Context operand_context;
            if (is_negate && context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED &&
                context.expected_type.type->type == Datatype_Type::PRIMITIVE && context.expected_type.cast_mode == Cast_Mode::IMPLICIT)
            {
                auto primitive = downcast<Datatype_Primitive>(context.expected_type.type);
                if (primitive->primitive_type != Primitive_Type::BOOLEAN) {
                    operand_context = context;
                }
            }
            else {
                operand_context = expression_context_make_unknown();
            }

            auto expr_type = semantic_analyser_analyse_expression_value(unary_node.expr, operand_context);
            if (datatype_is_unknown(expr_type)) {
                EXIT_ERROR(types.unknown_type);
            }
            auto operand_type = expr_type->base_type;

            // Check for primitive operand
            bool type_is_valid = false;
            Type_Mods expected_mods = type_mods_make(true, 0, 0, 0);
            Datatype* result_type = types.unknown_type;
            if (is_negate)
            {
                if (operand_type->type == Datatype_Type::PRIMITIVE) {
                    auto primitive = downcast<Datatype_Primitive>(operand_type);
                    if (primitive->is_signed && primitive->primitive_type != Primitive_Type::BOOLEAN) {
                        type_is_valid = true;
                        result_type = operand_type;
                    }
                    else {
                        log_semantic_error("Negate only works on signed primitive values", expr, Parser::Section::FIRST_TOKEN);
                        EXIT_ERROR(types.unknown_type);
                    }
                }
            }
            else {
                if (types_are_equal(operand_type, upcast(types.bool_type))) {
                    type_is_valid = true;
                    result_type = operand_type;
                }
            }

            // If type is not valid check for overloads
            if (!type_is_valid)
            {
                Custom_Operator_Key key;
                key.type = AST::Context_Change_Type::UNARY_OPERATOR;
                key.options.unop.unop = is_negate ? AST::Unop::NEGATE : AST::Unop::NOT;
                key.options.unop.type = operand_type;

                auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;
                Custom_Operator* overload = operator_context_query_custom_operator(operator_context, key);
                if (overload != 0)
                {
                    type_is_valid = true;
                    result_type = overload->unop.function->signature->return_type.value;
                    expected_mods = overload->unop.function->signature->parameters[0].type->mods;
                    info->specifics.overload.function = overload->unop.function;
                    semantic_analyser_register_function_call(overload->unop.function);
                }
            }

            // Check pointer level
            if (type_is_valid) {
                if (!try_updating_expression_type_mods(unary_node.expr, expected_mods)) {
                    type_is_valid = false;
                }
            }

            if (!type_is_valid) {
                log_semantic_error("Operand type not valid", unary_node.expr, Parser::Section::FIRST_TOKEN);
                EXIT_ERROR(types.unknown_type);
            }
            EXIT_VALUE(result_type, true);
        }
        case AST::Unop::POINTER:
        {
            // TODO: I think I can check if the context is a specific type + pointer and continue with child type
            auto operand_result = semantic_analyser_analyse_expression_any(unary_node.expr, expression_context_make_unknown());

            // Handle constant type_handles correctly
            if (operand_result->result_type == Expression_Result_Type::CONSTANT &&
                datatype_get_non_const_type(operand_result->options.constant.type)->type == Datatype_Type::TYPE_HANDLE)
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
                Datatype* operand_type = operand_result->cast_info.result_type;
                if (datatype_is_unknown(operand_type)) {
                    semantic_analyser_set_error_flag(true);
                    EXIT_ERROR(operand_type);
                }
                if (!expression_has_memory_address(unary_node.expr)) {
                    log_semantic_error("Cannot get memory address of a temporary value", expr);
                }
                EXIT_VALUE(upcast(type_system_make_pointer(operand_type)), true);
            }
            case Expression_Result_Type::TYPE: {
                EXIT_TYPE(upcast(type_system_make_pointer(operand_result->options.type)));
            }
            case Expression_Result_Type::DOT_CALL: {
                log_semantic_error("Cannot get pointer to dot call", expr);
                EXIT_ERROR(types.unknown_type);
                break;
            }
            case Expression_Result_Type::NOTHING: {
                log_semantic_error("Cannot get pointer to nothing", expr);
                EXIT_ERROR(types.unknown_type);
                break;
            }
            case Expression_Result_Type::FUNCTION:
            case Expression_Result_Type::POLYMORPHIC_FUNCTION:
            case Expression_Result_Type::HARDCODED_FUNCTION: {
                log_semantic_error("Cannot get pointer to a function (Function pointers don't require *)", expr);
                EXIT_ERROR(types.unknown_type);
            }
            case Expression_Result_Type::POLYMORPHIC_STRUCT: {
                log_semantic_error("Cannot get pointer to a polymorphic struct (Must be instanciated)", expr);
                EXIT_ERROR(types.unknown_type);
            }
            default: panic("");
            }
            panic("");
            break;
        }
        case AST::Unop::DEREFERENCE:
        {
            auto operand_type = datatype_get_non_const_type(semantic_analyser_analyse_expression_value(unary_node.expr, expression_context_make_unknown()));
            Datatype* result_type = types.unknown_type;
            if (operand_type->type == Datatype_Type::POINTER) {
                auto ptr = downcast<Datatype_Pointer>(operand_type);
                if (ptr->is_optional) {
                    log_semantic_error("Cannot dereference optional pointer, use .value instead", expr);
                    log_error_info_given_type(operand_type);
                }
                result_type = ptr->element_type;
            }
            else {
                log_semantic_error("Cannot dereference non-pointer value", expr);
                log_error_info_given_type(operand_type);
            }
            EXIT_VALUE(result_type, false);
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

        // Evaluate operands
        Datatype* left_type;
        Datatype* right_type;
        {
            // If we are dealing with auto-expression, make sure to analyse in the right order
            bool left_requires_context = expression_is_auto_expression(binop_node.left);
            bool right_requires_context = expression_is_auto_expression(binop_node.right);

            Expression_Context unknown_context = expression_context_make_unknown();
            if ((left_requires_context && right_requires_context) || (!left_requires_context && !right_requires_context)) {
                left_type = semantic_analyser_analyse_expression_value(binop_node.left, unknown_context);
                right_type = semantic_analyser_analyse_expression_value(binop_node.right, unknown_context);
            }
            else if (left_requires_context && !right_requires_context) {
                right_type = semantic_analyser_analyse_expression_value(binop_node.right, unknown_context);
                if (is_pointer_comparison) {
                    left_type = semantic_analyser_analyse_expression_value(binop_node.left, expression_context_make_specific_type(right_type));
                }
                else {
                    left_type = semantic_analyser_analyse_expression_value(binop_node.left, expression_context_make_specific_type(right_type->base_type));
                }
            }
            else if (!left_requires_context && right_requires_context) {
                left_type = semantic_analyser_analyse_expression_value(binop_node.left, unknown_context);
                if (is_pointer_comparison) {
                    right_type = semantic_analyser_analyse_expression_value(binop_node.right, expression_context_make_specific_type(left_type));
                }
                else {
                    right_type = semantic_analyser_analyse_expression_value(binop_node.right, expression_context_make_specific_type(left_type->base_type));
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
            if (!types_are_equal(left_type, right_type)) {
                log_semantic_error("Pointer comparison only works if both types are the same", expr, Parser::Section::WHOLE);
            }
            EXIT_VALUE(upcast(types.bool_type), true);
        }

        // Check if types are valid for overload
        bool types_are_valid = false;
        Datatype* result_type = types.unknown_type;

        Type_Mods expected_mods_left = type_mods_make(true, 0, 0, 0);
        Type_Mods expected_mods_right = type_mods_make(true, 0, 0, 0);
        left_type = left_type->base_type;
        right_type = right_type->base_type;

        // Check if overload is a primitive operation (ints, floats, bools)
        if (types_are_equal(left_type, right_type))
        {
            result_type = left_type;

            bool int_valid = false;
            bool float_valid = false;
            bool bool_valid = false;
            bool enum_valid = false;
            bool type_type_valid = false;
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

            Datatype* operand_type = left_type;
            if (operand_type->type == Datatype_Type::PRIMITIVE)
            {
                auto primitive = downcast<Datatype_Primitive>(operand_type);
                if (primitive->primitive_type == Primitive_Type::INTEGER) {
                    types_are_valid = int_valid;
                }
                else if (primitive->primitive_type == Primitive_Type::FLOAT) {
                    types_are_valid = float_valid;
                }
                else if (primitive->primitive_type == Primitive_Type::BOOLEAN) {
                    types_are_valid = bool_valid;
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

        // Check for operator overloads if it isn't a primitive operation
        auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;
        if (!types_are_valid)
        {
            Custom_Operator_Key key;
            key.type = AST::Context_Change_Type::BINARY_OPERATOR;
            key.options.binop.binop = binop_node.type;
            key.options.binop.left_type = left_type;
            key.options.binop.right_type = right_type;
            auto overload = operator_context_query_custom_operator(operator_context, key);
            if (overload != nullptr)
            {
                auto& custom_binop = overload->binop;
                if (custom_binop.switch_left_and_right) {
                    expected_mods_left = custom_binop.function->signature->parameters[1].type->mods;
                    expected_mods_right = custom_binop.function->signature->parameters[0].type->mods;
                }
                else {
                    expected_mods_left = custom_binop.function->signature->parameters[0].type->mods;
                    expected_mods_right = custom_binop.function->signature->parameters[1].type->mods;
                }
                info->specifics.overload.function = custom_binop.function;
                info->specifics.overload.switch_left_and_right = custom_binop.switch_left_and_right;
                semantic_analyser_register_function_call(custom_binop.function);

                types_are_valid = true;
                result_type = custom_binop.function->signature->return_type.value;
            }
        }

        // Check that expected pointer levels are correct (And apply auto operations if possible)
        if (types_are_valid && !is_pointer_comparison)
        {
            if (!try_updating_expression_type_mods(binop_node.left, expected_mods_left)) {
                types_are_valid = false;
            }
            if (!try_updating_expression_type_mods(binop_node.right, expected_mods_right)) {
                types_are_valid = false;
            }
        }

        if (!types_are_valid) {
            log_semantic_error("Types aren't valid for binary operation", expr);
            log_error_info_binary_op_type(left_type, right_type);
            EXIT_ERROR(types.unknown_type);
        }
        EXIT_VALUE(result_type, true);
    }
    default: {
        panic("Not all expression covered!\n");
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
}

bool cast_possible_in_mode(Cast_Mode mode, Cast_Mode allowed_mode)
{
    switch (allowed_mode)
    {
    case Cast_Mode::IMPLICIT: return true;
    case Cast_Mode::NONE: return false;
    case Cast_Mode::EXPLICIT: return mode == Cast_Mode::EXPLICIT;
    case Cast_Mode::INFERRED: return mode == Cast_Mode::INFERRED || mode == Cast_Mode::EXPLICIT;
    case Cast_Mode::POINTER_EXPLICIT: return mode == Cast_Mode::POINTER_EXPLICIT;
    case Cast_Mode::POINTER_INFERRED: return mode == Cast_Mode::POINTER_INFERRED || mode == Cast_Mode::POINTER_EXPLICIT;
    default: panic("");
    }
    return false;
};


// If expr == nullptr, then we don't check for custom casts. otherwise, we expect expression to be already analysed.
Expression_Cast_Info semantic_analyser_check_if_cast_possible(
    bool is_temporary_value, Datatype* source_type, Datatype* destination_type, Cast_Mode cast_mode, AST::Expression* expr)
{
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;

    Expression_Cast_Info result = cast_info_make_empty(source_type, is_temporary_value);
    result.cast_type = Cast_Type::INVALID;
    result.initial_type = source_type;
    result.result_type = destination_type;
    result.initial_value_is_temporary = is_temporary_value;
    result.result_value_is_temporary = true;

    // Check for simple cases (Equality, const-equality or unknown)
    if (types_are_equal(destination_type, source_type)) {
        result.cast_type = Cast_Type::NO_CAST;
        result.result_value_is_temporary = is_temporary_value;
        return result;
    }
    if (types_are_equal(datatype_get_non_const_type(destination_type), datatype_get_non_const_type(source_type))) {
        // Note: We can cast from const int <-> int and backwards, as these are just values
        result.cast_type = Cast_Type::NO_CAST;
        result.result_value_is_temporary = is_temporary_value;
        return result;
    }
    if (datatype_is_unknown(source_type) || datatype_is_unknown(destination_type)) {
        result.cast_type = Cast_Type::UNKNOWN;
        result.result_value_is_temporary = false;
        return result;
    }

    // Check pointer casts
    if (cast_mode == Cast_Mode::POINTER_EXPLICIT || cast_mode == Cast_Mode::POINTER_INFERRED)
    {
        Cast_Mode allowed_mode = Cast_Mode::NONE;
        bool src_is_opt = false;
        bool dst_is_opt = false;
        bool src_is_ptr = datatype_is_pointer(source_type, &src_is_opt);
        bool dst_is_ptr = datatype_is_pointer(destination_type, &dst_is_opt);

        // Check for from/to u64
        if (src_is_ptr && types_are_equal(destination_type, upcast(types.u64_type))) {
            result.cast_type = Cast_Type::POINTER_TO_U64;
            allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::POINTER_TO_POINTER);
        }
        else if (dst_is_ptr && types_are_equal(source_type, upcast(types.u64_type))) {
            result.cast_type = Cast_Type::U64_TO_POINTER;
            allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::POINTER_TO_POINTER);
        }
        else if (src_is_ptr && dst_is_ptr) {
            result.cast_type = Cast_Type::POINTERS;
            allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::POINTER_TO_POINTER);
            if (src_is_opt && !dst_is_opt) {
                allowed_mode = Cast_Mode::NONE;
            }
        }
        else {
            result.cast_type = Cast_Type::INVALID;
            result.options.error_msg = "For cast_pointer the cast must be between either pointers or pointer <-> u64";
            allowed_mode = Cast_Mode::NONE;
        }

        if (cast_possible_in_mode(cast_mode, allowed_mode)) {
            // Success
            result.result_value_is_temporary = true;
        }
        else {
            result.cast_type = Cast_Type::INVALID;
            result.result_value_is_temporary = false;
            result.options.error_msg = "Cast mode does not match allowed mode";
        }
        return result;
    }

    // from/to byte_pointer and any casts (Which have higher precedence than other cast types)
    {
        Cast_Mode allowed_mode = Cast_Mode::NONE;
        bool result_is_temporary = true;
        bool is_opt_pointer = false;
        if (source_type->mods.pointer_level == 0 && 
            source_type->base_type->type == Datatype_Type::BYTE_POINTER && 
            datatype_is_pointer(destination_type, &is_opt_pointer)) 
        {
            bool byteptr_is_opt = downcast<Datatype_Bytepointer>(source_type->base_type)->is_optional;
            result.cast_type = Cast_Type::POINTERS;
            allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::FROM_BYTE_POINTER);
            if (byteptr_is_opt && !is_opt_pointer) {
                allowed_mode = Cast_Mode::NONE;
            }
        }
        else if (destination_type->mods.pointer_level == 0 &&
            destination_type->base_type->type == Datatype_Type::BYTE_POINTER &&
            datatype_is_pointer(source_type, &is_opt_pointer)) 
        {
            bool byte_is_opt = downcast<Datatype_Bytepointer>(destination_type->base_type)->is_optional;
            result.cast_type = Cast_Type::POINTERS;
            allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::TO_BYTE_POINTER);
            if (is_opt_pointer && !byte_is_opt) {
                allowed_mode = Cast_Mode::NONE;
            }
        }
        else if (types_are_equal(datatype_get_non_const_type(source_type), upcast(types.any_type))) {
            result.cast_type = Cast_Type::FROM_ANY;
            allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::FROM_ANY);
            result_is_temporary = false;
        }
        else if (types_are_equal(datatype_get_non_const_type(destination_type), upcast(types.any_type))) {
            result.cast_type = Cast_Type::TO_ANY;
            allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::TO_ANY);
        }
        else if (datatype_get_non_const_type(destination_type)->type == Datatype_Type::OPTIONAL_TYPE) {
            auto opt = downcast<Datatype_Optional>(datatype_get_non_const_type(destination_type));
            if (types_are_equal(datatype_get_non_const_type(opt->child_type), datatype_get_non_const_type(source_type))) {
                result.cast_type = Cast_Type::TO_OPTIONAL;
                allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::TO_OPTIONAL);
            }
        }

        if (cast_possible_in_mode(cast_mode, allowed_mode)) {
            result.result_value_is_temporary = result_is_temporary;
            return result;
        }
    }

    // Check if type-mods update works
    if (types_are_equal(source_type->base_type, destination_type->base_type))
    {
        result.cast_type = Cast_Type::NO_CAST;
        result.result_type = source_type;
        result.deref_count = 0;

        if (try_updating_type_mods(result, destination_type->mods, &result.options.error_msg)) {
            result.result_type = destination_type;
            return result;
        }
        else {
            result.cast_type = Cast_Type::INVALID;
            result.result_type = destination_type;
            return result;
        }
    }

    // Check for built-in casts
    {
        Datatype* source = source_type->base_type;
        Datatype* destination = datatype_get_non_const_type(destination_type);

        // Figure out type of cast + what mode is allowed
        Cast_Mode allowed_mode = Cast_Mode::NONE;

        // Check built-in cast types
        switch (source->type)
        {
        case Datatype_Type::ARRAY:
        {
            if (destination->type == Datatype_Type::SLICE)
            {
                auto source_array = downcast<Datatype_Array>(source);
                auto dest_slice = downcast<Datatype_Slice>(destination);
                if (types_are_equal(datatype_get_non_const_type(source_array->element_type), datatype_get_non_const_type(dest_slice->element_type)))
                {
                    bool array_is_const = source_type->mods.is_constant || source_array->element_type->type == Datatype_Type::CONSTANT;
                    bool slice_ptr_is_const = dest_slice->element_type->type == Datatype_Type::CONSTANT;
                    // We can only cast to slice if we respect the constant rules
                    if (!(array_is_const && !slice_ptr_is_const)) {
                        result.cast_type = Cast_Type::ARRAY_TO_SLICE;
                        allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::ARRAY_TO_SLICE);
                    }
                }
            }
            break;
        }
        case Datatype_Type::ENUM:
        {
            if (destination->type == Datatype_Type::PRIMITIVE)
            {
                auto primitive = downcast<Datatype_Primitive>(destination);
                if (primitive->primitive_type == Primitive_Type::INTEGER) {
                    result.cast_type = Cast_Type::ENUM_TO_INT;
                    allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::ENUM_TO_INT);
                }
            }
            break;
        }
        case Datatype_Type::FUNCTION:
        {
            if (destination->type == Datatype_Type::FUNCTION)
            {
                // Note: Casting between two different function signatures only works if they have the same parameter/return types.
                //       In C this would always result in the same type, but in upp the parameter names/default values change the function type
                auto src_fn = downcast<Datatype_Function>(source);
                auto dst_fn = downcast<Datatype_Function>(destination);
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
                    result.cast_type = Cast_Type::POINTERS; // Not sure if we want to have different types for this
                    allowed_mode = Cast_Mode::IMPLICIT; // Maybe we want to add this to the expression context at some point?
                }
            }
            break;
        }
        case Datatype_Type::PRIMITIVE:
        {
            auto src_primitive = downcast<Datatype_Primitive>(source);
            if (destination->type == Datatype_Type::PRIMITIVE)
            {
                auto dst_primitive = downcast<Datatype_Primitive>(destination);
                auto src_size = source->memory_info.value.size;
                auto dst_size = destination->memory_info.value.size;

                // Figure out allowed mode and cast type
                if (src_primitive->primitive_type == Primitive_Type::INTEGER && dst_primitive->primitive_type == Primitive_Type::INTEGER)
                {
                    result.cast_type = Cast_Type::INTEGERS;
                    Cast_Mode signed_mode = Cast_Mode::IMPLICIT;
                    if (src_primitive->is_signed != dst_primitive->is_signed) {
                        if (src_primitive->is_signed) {
                            signed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::INTEGER_SIGNED_TO_UNSIGNED);
                        }
                        else {
                            signed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::INTEGER_UNSIGNED_TO_SIGNED);
                        }
                    }

                    Cast_Mode size_mode = Cast_Mode::IMPLICIT;
                    if (dst_size != src_size)
                    {
                        if (dst_size > src_size) {
                            size_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::INTEGER_SIZE_UPCAST);
                        }
                        else {
                            size_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::INTEGER_SIZE_DOWNCAST);
                        }
                    }

                    // For integer cast to work, both size and signed must be castable
                    allowed_mode = (Cast_Mode)math_minimum((int)size_mode, (int)signed_mode);
                }
                else if (dst_primitive->primitive_type == Primitive_Type::FLOAT && src_primitive->primitive_type == Primitive_Type::INTEGER)
                {
                    result.cast_type = Cast_Type::INT_TO_FLOAT;
                    allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::INT_TO_FLOAT);
                }
                else if (dst_primitive->primitive_type == Primitive_Type::INTEGER && src_primitive->primitive_type == Primitive_Type::FLOAT)
                {
                    result.cast_type = Cast_Type::FLOAT_TO_INT;
                    allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::FLOAT_TO_INT);
                }
                else if (dst_primitive->primitive_type == Primitive_Type::FLOAT && src_primitive->primitive_type == Primitive_Type::FLOAT)
                {
                    result.cast_type = Cast_Type::FLOATS;
                    if (dst_size > src_size) {
                        allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::FLOAT_SIZE_UPCAST);
                    }
                    else {
                        allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::FLOAT_SIZE_DOWNCAST);
                    }
                }
                else { // Booleans can never be cast
                    allowed_mode = Cast_Mode::NONE;
                }
            }
            else if (destination->type == Datatype_Type::ENUM)
            {
                // TODO: Int to enum casting should check if int can hold max/min enum value
                if (src_primitive->primitive_type == Primitive_Type::INTEGER) {
                    result.cast_type = Cast_Type::INT_TO_ENUM;
                    allowed_mode = operator_context_get_cast_mode_option(operator_context, Cast_Option::INT_TO_ENUM);
                }
            }
            break;
        }
        default: break;
        }

        if (cast_possible_in_mode(cast_mode, allowed_mode) && result.cast_type != Cast_Type::INVALID) {
            result.deref_count = source_type->mods.pointer_level; // Dereference to base level for built-in casts
            result.result_value_is_temporary = true;
            return result;
        }
    }

    // Check overloads
    if (expr != 0)
    {
        Custom_Operator_Key key;
        key.type = AST::Context_Change_Type::CAST;
        key.options.custom_cast.from_type = source_type->base_type;
        key.options.custom_cast.to_type = destination_type; // Destination type currently has to match perfectly for overload to work, e.g. no deref afterwards
        Custom_Operator* overload = operator_context_query_custom_operator(operator_context, key);

        // Search for polymorphic overloads
        if (overload == 0 && source_type->base_type->type == Datatype_Type::STRUCT)
        {
            auto struct_type = downcast<Datatype_Struct>(source_type->base_type);
            if (struct_type->workload != 0 && struct_type->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE)
            {
                key.options.custom_cast.from_type = upcast(struct_type->workload->polymorphic.instance.parent->body_workload->struct_type);;
                overload = operator_context_query_custom_operator(operator_context, key);
                if (overload == 0) {
                    // Check overload with polymorphic result type
                    key.options.custom_cast.to_type = nullptr;
                    overload = operator_context_query_custom_operator(operator_context, key);
                }
            }
        }

        // Test if type_mods can match
        if (overload != 0)
        {
            result.cast_type = Cast_Type::NO_CAST;
            if (!try_updating_type_mods(result, overload->custom_cast.mods)) {
                overload = nullptr;
            }
        }
        // Check if overload has correct cast_mode
        if (overload != 0) {
            if (!cast_possible_in_mode(cast_mode, overload->custom_cast.cast_mode)) {
                overload = 0;
            }
        }

        if (overload != 0)
        {
            auto& custom_cast = overload->custom_cast;
            if (!custom_cast.is_polymorphic) {
                // Note: Destination type is always correct here because it was used in the key as the to_type value
                auto function = custom_cast.options.function;
                result.cast_type = Cast_Type::CUSTOM_CAST;
                result.options.custom_cast_function = function;
                result.result_type = destination_type;
                result.result_value_is_temporary = true;
                semantic_analyser_register_function_call(function);
                return result;
            }
            else
            {
                Parameter_Matching_Info matching_info = parameter_matching_info_create_empty(Call_Type::POLYMORPHIC_FUNCTION, 1);
                SCOPE_EXIT(parameter_matching_info_destroy(&matching_info));
                matching_info.options.poly_function = custom_cast.options.poly_base;
                parameter_matching_info_add_analysed_param(&matching_info, expr);
                Instanciation_Result instance_result = instanciate_polymorphic_callable(
                    &matching_info, expression_context_make_specific_type(destination_type), upcast(expr)
                );

                if (instance_result.type == Instanciation_Result_Type::FUNCTION) {
                    auto function = instance_result.options.function;
                    // Note: Here we need a further check as the to_type of the key could have been set to null
                    if (types_are_equal(function->signature->return_type.value, destination_type)) {
                        result.cast_type = Cast_Type::CUSTOM_CAST;
                        result.options.custom_cast_function = function;
                        result.result_value_is_temporary = true;
                        result.result_type = destination_type;
                        semantic_analyser_register_function_call(function);
                        return result;
                    }
                }
            }
        }
    }

    // Return Invalid if no casts were found
    result.cast_type = Cast_Type::INVALID;
    result.result_type = destination_type;
    result.result_value_is_temporary = false;
    result.deref_count = 0;
    return result;
}

Expression_Cast_Info expression_context_apply(
    Datatype* initial_type, Expression_Context context, bool log_errors, AST::Expression* expression, Parser::Section error_section)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;

    // Set active expression info
    Expression_Info* _backup_info = semantic_analyser.current_workload->current_expression;
    SCOPE_EXIT(semantic_analyser.current_workload->current_expression = _backup_info);
    if (expression != 0 && log_errors) {
        semantic_analyser.current_workload->current_expression = get_info(expression);
    }

    assert(!(context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED && datatype_is_unknown(context.expected_type.type)),
        "Should be checked when in context_make_specific_type");

    Expression_Cast_Info result;
    result.initial_type = initial_type;
    result.result_type = initial_type;
    result.initial_value_is_temporary = semantic_analyser.current_workload->current_expression->cast_info.initial_value_is_temporary;
    result.result_value_is_temporary = result.initial_value_is_temporary;
    result.deref_count = 0;
    result.cast_type = Cast_Type::INVALID;
    result.options.error_msg = "";

    switch (context.type)
    {
    case Expression_Context_Type::UNKNOWN: {
        result.cast_type = Cast_Type::NO_CAST;
        return result;
    }
    case Expression_Context_Type::AUTO_DEREFERENCE:
    {
        // Auto dereference now always forces pointer value to be 0
        result.deref_count = initial_type->mods.pointer_level;
        if (result.deref_count > 0) {
            if (initial_type->mods.optional_flags != 0) {
                log_semantic_error("Cannot auto-dereference optional pointer", expression, error_section);
            }
            result.result_value_is_temporary = false;
        }
        else {
            result.result_value_is_temporary = result.initial_value_is_temporary;
        }

        // Make result type
        Type_Mods result_mods = type_mods_make(initial_type->mods.is_constant, 0, 0, 0, initial_type->mods.subtype_index);
        result.result_type = type_system_make_type_with_mods(initial_type->base_type, result_mods);
        result.cast_type = Cast_Type::NO_CAST;
        return result;
    }
    case Expression_Context_Type::SPECIFIC_TYPE_EXPECTED: {
        result = semantic_analyser_check_if_cast_possible(
            get_info(expression)->cast_info.initial_value_is_temporary, initial_type, context.expected_type.type, context.expected_type.cast_mode, expression
        );

        // Check for errors
        if (log_errors)
        {
            if (result.cast_type == Cast_Type::INVALID)
            {
                semantic_analyser_set_error_flag(false);
                log_semantic_error("Cannot cast to required type", expression, error_section);
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
        if (datatype_is_unknown(result->cast_info.result_type)) {
            if (result->cast_info.result_type->type != Datatype_Type::TEMPLATE_PARAMETER &&
                result->cast_info.result_type->type != Datatype_Type::STRUCT_INSTANCE_TEMPLATE) {
                semantic_analyser_set_error_flag(true);
            }
            return result->cast_info.result_type;
        }
        if (!types_are_equal(datatype_get_non_const_type(result->cast_info.result_type), types.type_handle))
        {
            log_semantic_error("Expression cannot be converted to type", expression);
            log_error_info_given_type(result->cast_info.result_type);
            return types.unknown_type;
        }

        // Otherwise try to convert to constant
        Upp_Constant constant;
        if (result->result_type == Expression_Result_Type::VALUE) {
            auto comptime_opt = expression_calculate_comptime_value(expression, "Expression is a type, but it isn't known at compile time");
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
        if (type_index >= (u32)type_system.internal_type_infos.size) {
            // Note: Always log this error, because this should never happen!
            log_semantic_error("Expression contains invalid type handle", expression);
            final_type = upcast(types.unknown_type);
        }
        else {
            final_type = type_system.types[type_index];
        }
        expression_info_set_type(result, final_type);
        return final_type;
    }
    case Expression_Result_Type::DOT_CALL: {
        log_semantic_error("Expected a type, given dot_call", expression);
        log_error_info_expression_result_type(result->result_type);
        return types.unknown_type;
    }
    case Expression_Result_Type::NOTHING: {
        log_semantic_error("Expected a type, given nothing", expression);
        log_error_info_expression_result_type(result->result_type);
        return types.unknown_type;
    }
    case Expression_Result_Type::POLYMORPHIC_STRUCT: {
        log_semantic_error("Expected a specific type, given polymorphic struct", expression);
        log_error_info_expression_result_type(result->result_type);
        return types.unknown_type;
    }
    case Expression_Result_Type::HARDCODED_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::FUNCTION: {
        log_semantic_error("Expected a type, given a function", expression);
        log_error_info_expression_result_type(result->result_type);
        return types.unknown_type;
    }
    default: panic("");
    }

    panic("Shouldn't happen");
    return types.unknown_type;
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
            result->options.value_type = compiler.type_system.predefined_types.unknown_type;
            context.type = Expression_Context_Type::UNKNOWN;
            result->cast_info = cast_info_make_empty(compiler.type_system.predefined_types.unknown_type, false);
        }
        else if (result->result_type == Expression_Result_Type::NOTHING && no_value_expected) {
            // Here we set the result type to error, but we don't treat it as an error (Note: This only affects how the ir-code currently generates code)
            result->result_type = Expression_Result_Type::VALUE;
            result->contains_errors = false;
            result->options.value_type = upcast(compiler.type_system.predefined_types.empty_struct_type);
            context.type = Expression_Context_Type::UNKNOWN;
            result->cast_info = cast_info_make_empty(result->cast_info.result_type, false);
        }
    }

    switch (result->result_type)
    {
    case Expression_Result_Type::CONSTANT:
    case Expression_Result_Type::VALUE: {
        return result->cast_info.result_type; // Here context was already applied (See analyse_expression_any), so we return
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
    case Expression_Result_Type::DOT_CALL:
    {
        log_semantic_error("Dot_Call cannot be used as value", expression);
        return types.unknown_type;
    }
    case Expression_Result_Type::HARDCODED_FUNCTION:
    {
        log_semantic_error("Cannot take address of hardcoded function", expression);
        return types.unknown_type;
    }
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    {
        log_semantic_error("Cannot convert polymorphic function to function pointer", expression);
        return types.unknown_type;
    }
    case Expression_Result_Type::POLYMORPHIC_STRUCT:
    {
        log_semantic_error("Cannot convert polymorphic struct to type_handle", expression);
        return types.unknown_type;
    }
    case Expression_Result_Type::NOTHING: panic("Should be handled in previous code path");
    default: panic("");
    }

    result->cast_info = expression_context_apply(expression_info_get_type(result, true), context, true, expression);
    return result->cast_info.result_type;
}



// OPERATOR CONTEXT
Custom_Operator* operator_context_query_custom_operator_recursive(Operator_Context* context, Custom_Operator_Key key, Hashset<Operator_Context*>& visited)
{
    if (hashset_contains(&visited, context)) {
        return nullptr;
    }
    hashset_insert_element(&visited, context);

    auto change_workload = context->workloads[(int)key.type];
    if (change_workload != 0) {
        analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(change_workload));
        workload_executer_wait_for_dependency_resolution();
    }

    auto result = hashtable_find_element(&context->custom_operators, key);
    if (result != 0) {
        return result;
    }

    // Otherwise wait for import workloads
    auto import_workload = context->workloads[(int)AST::Context_Change_Type::IMPORT];
    if (import_workload != 0) {
        analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(import_workload));
        workload_executer_wait_for_dependency_resolution();
    }

    for (int i = 0; i < context->context_imports.size; i++) {
        auto result = operator_context_query_custom_operator_recursive(context->context_imports[i], key, visited);
        if (result != nullptr) {
            return result;
        }
    }
    return nullptr;
}

Custom_Operator* operator_context_query_custom_operator(Operator_Context* context, Custom_Operator_Key key)
{
    Hashset<Operator_Context*> visited = hashset_create_pointer_empty<Operator_Context*>(4);
    SCOPE_EXIT(hashset_destroy(&visited));
    return operator_context_query_custom_operator_recursive(context, key, visited);
}

Cast_Mode operator_context_get_cast_mode_option(Operator_Context* context, Cast_Option option) 
{
    Custom_Operator_Key key;
    key.type = AST::Context_Change_Type::CAST_OPTION;
    key.options.cast_option = option;

    Custom_Operator* result = operator_context_query_custom_operator(context, key);
    if (result != 0) {
        return result->cast_mode;
    }

    // Otherwise return default option
    if (option == Cast_Option::FROM_BYTE_POINTER || option == Cast_Option::TO_BYTE_POINTER || option == Cast_Option::POINTER_TO_POINTER) {
        return Cast_Mode::POINTER_INFERRED;
    }
    return Cast_Mode::INFERRED;
}

u64 custom_operator_key_hash(Custom_Operator_Key* key)
{
    int type_as_int = (int)key->type;
    u64 hash = hash_i32(&type_as_int);
    switch (key->type)
    {
    case AST::Context_Change_Type::ARRAY_ACCESS: {
        auto& access = key->options.array_access;
        hash = hash_combine(hash, hash_pointer(access.array_type));
        break;
    }
    case AST::Context_Change_Type::BINARY_OPERATOR: {
        auto& binop = key->options.binop;
        int op_as_int = (int)binop.binop;
        hash = hash_combine(hash, hash_i32(&op_as_int));
        hash = hash_combine(hash, hash_pointer(binop.left_type));
        hash = hash_combine(hash, hash_pointer(binop.right_type));
        break;
    }
    case AST::Context_Change_Type::UNARY_OPERATOR: {
        auto& unop = key->options.unop;
        int op_as_int = (int)unop.unop;
        hash = hash_combine(hash, hash_i32(&op_as_int));
        hash = hash_combine(hash, hash_pointer(unop.type));
        break;
    }
    case AST::Context_Change_Type::CAST: {
        auto& cast = key->options.custom_cast;
        hash = hash_combine(hash, hash_pointer(cast.from_type));
        hash = hash_combine(hash, hash_pointer(cast.to_type));
        break;
    }
    case AST::Context_Change_Type::DOT_CALL: {
        auto& dot_call = key->options.dot_call;
        hash = hash_combine(hash, hash_pointer(dot_call.datatype));
        hash = hash_combine(hash, hash_pointer(dot_call.id->characters)); // Should work because all strings are in string pool
        break;
    }
    case AST::Context_Change_Type::ITERATOR: {
        auto& iter = key->options.iterator;
        hash = hash_combine(hash, hash_pointer(iter.datatype));
        break;
    }
    case AST::Context_Change_Type::CAST_OPTION: {
        int option_value = (int) key->options.cast_option;
        hash = hash_combine(hash, hash_i32(&option_value));
        break;
    }
    case AST::Context_Change_Type::INVALID: {
        hash = 234129345;
        break;
    }
    case AST::Context_Change_Type::IMPORT: {
        hash = 8947234;
        break;
    }
    default: panic("");
    }
    return hash;
}

bool custom_operator_key_equals(Custom_Operator_Key* a, Custom_Operator_Key* b) {
    if (a->type != b->type) {
        return false;
    }
    switch (a->type)
    {
    case AST::Context_Change_Type::ARRAY_ACCESS: {
        return types_are_equal(a->options.array_access.array_type, b->options.array_access.array_type);
    }
    case AST::Context_Change_Type::BINARY_OPERATOR: {
        return types_are_equal(a->options.binop.left_type, b->options.binop.left_type) &&
            types_are_equal(a->options.binop.right_type, b->options.binop.right_type) &&
            a->options.binop.binop == b->options.binop.binop;
    }
    case AST::Context_Change_Type::UNARY_OPERATOR: {
        return types_are_equal(a->options.unop.type, b->options.unop.type) && a->options.unop.unop == b->options.unop.unop;
    }
    case AST::Context_Change_Type::CAST: {
        return types_are_equal(a->options.custom_cast.from_type, b->options.custom_cast.from_type) &&
            types_are_equal(a->options.custom_cast.to_type, b->options.custom_cast.to_type);
    }
    case AST::Context_Change_Type::DOT_CALL: {
        return types_are_equal(a->options.dot_call.datatype, b->options.dot_call.datatype) && a->options.dot_call.id == b->options.dot_call.id;
    }
    case AST::Context_Change_Type::ITERATOR: {
        return types_are_equal(a->options.iterator.datatype, b->options.iterator.datatype);
    }
    case AST::Context_Change_Type::CAST_OPTION: {
        return types_are_equal(a->options.iterator.datatype, b->options.iterator.datatype);
    }
    case AST::Context_Change_Type::INVALID: {
        return true;
    }
    case AST::Context_Change_Type::IMPORT: {
        return true;
    }
    default: panic("");
    }

    return true;
}

Operator_Context* symbol_table_install_new_operator_context_and_add_workloads(
    Symbol_Table* symbol_table, Dynamic_Array<AST::Context_Change*> context_changes, Workload_Base* wait_for_workload
)
{
    // Create new operator context
    auto context = new Operator_Context;
    context->context_imports = dynamic_array_create<Operator_Context*>();
    context->custom_operators = hashtable_create_empty<Custom_Operator_Key, Custom_Operator>(1, custom_operator_key_hash, custom_operator_key_equals);
    for (int i = 0; i < (int)AST::Context_Change_Type::MAX_ENUM_VALUE; i++) {
        context->workloads[i] = nullptr;
    }

    // Add parent to imports if exists
    auto parent_context = symbol_table->operator_context;
    if (parent_context != 0) {
        dynamic_array_push_back(&context->context_imports, parent_context);
    }

    // Create workloads
    auto parent_workload = semantic_analyser.current_workload;
    for (int i = 0; i < context_changes.size; i++)
    {
        auto change = context_changes[i];
        if (context->workloads[(int)change->type] != 0) { // Only create one workload per change type
            continue;
        }

        auto workload = workload_executer_allocate_workload<Workload_Operator_Context_Change>(nullptr, parent_workload->current_pass);
        workload->context = context;
        workload->context_type_to_analyse = change->type;
        workload->change_nodes = context_changes;
        if (wait_for_workload != 0) {
            analysis_workload_add_dependency_internal(upcast(workload), wait_for_workload);
        }
        context->workloads[(int)change->type] = workload;
    }

    dynamic_array_push_back(&semantic_analyser.allocated_operator_contexts, context);
    symbol_table->operator_context = context;
    return context;
}

void analyse_operator_context_change(AST::Context_Change* change_node, Operator_Context* context)
{
    auto& ids = compiler.predefined_ids;
    auto& types = compiler.type_system.predefined_types;
    bool success = true;

    auto parameter_set_analysed = [](Parameter_Match& param) {
        param.state = Parameter_State::ANALYSED;
        if (param.is_set) {
            auto info = get_info(param.expression);
            param.argument_type = info->cast_info.result_type;
            param.argument_is_temporary_value = info->cast_info.initial_value_is_temporary;
        }
    };
    // Returns enum value as integer or -1 if error
    auto analyse_parameter_as_comptime_enum = [&success](Parameter_Match& param, int max_enum_value) -> int {
        assert(param.param_type != 0 && param.state == Parameter_State::NOT_ANALYSED, "");
        if (!param.is_set) return -1;

        param.argument_type = semantic_analyser_analyse_expression_value(param.expression, expression_context_make_specific_type(upcast(param.param_type)));
        param.state = Parameter_State::ANALYSED;
        auto result = expression_calculate_comptime_value(param.expression, "Argument has to be comptime known");
        if (!result.available) {
            success = false;
            return -1;
        }

        // Check if enum value is valid
        i32 enum_value = upp_constant_to_value<i32>(result.value);
        if (enum_value <= 0 || enum_value >= max_enum_value) {
            log_semantic_error("Enum value is invalid", param.expression);
            success = false;
            return -1;
        }
        return enum_value;
    };
    auto analyse_parameter_as_comptime_bool = [&success](Parameter_Match& param) -> bool {
        assert(param.param_type != 0 && param.state == Parameter_State::NOT_ANALYSED, "");
        if (!param.is_set) return false;

        param.argument_type = semantic_analyser_analyse_expression_value(
            param.expression, expression_context_make_specific_type(upcast(compiler.type_system.predefined_types.bool_type))
        );
        param.state = Parameter_State::ANALYSED;
        auto result = expression_calculate_comptime_value(param.expression, "Argument has to be comptime known");
        if (!result.available) {
            success = false;
            return false;
        }
        return upp_constant_to_value<bool>(result.value) != 0;
    };
    auto analyse_expression_info_as_function =
        [&](AST::Expression* expr, Expression_Info* expr_info,
            int expected_parameter_count, bool expected_return_value, Type_Mods* type_mods) -> ModTree_Function*
    {
        if (expr == nullptr) {
            success = false;
            return nullptr;
        }
        if (expr_info->result_type == Expression_Result_Type::VALUE && datatype_is_unknown(expr_info->options.value_type)) {
            success = false;
            return nullptr;
        }

        if (expr_info->result_type != Expression_Result_Type::FUNCTION) {
            success = false;
            log_semantic_error("Expression must be a function", expr);
            return nullptr;
        }

        ModTree_Function* function = expr_info->options.function;
        auto signature = function->signature;
        if (signature->parameters.size != expected_parameter_count) {
            log_semantic_error("Function does not have the required number of arguments for overload", expr);
            success = false;
            return nullptr;
        }

        if (signature->parameters.size != 0) {
            *type_mods = signature->parameters[0].type->mods;
        }

        if (!signature->return_type.available && expected_return_value) {
            log_semantic_error("Function must have return type for custom operator", expr);
            log_error_info_given_type(upcast(signature));
            success = false;
            return nullptr;
        }

        return function;
    };
    // Returns unknown on error and sets error flag
    auto poly_function_check_first_argument = [&](Polymorphic_Base_Info* poly_base, AST::Node* error_report_node, Type_Mods* type_mods) -> Datatype*
    {
        *type_mods = type_mods_make(0, 0, 0, 0);
        if (poly_base->parameter_nodes.size == 0) {
            log_semantic_error("Poly function must have at least one argument for custom operator", error_report_node);
            success = false;
            return types.unknown_type;
        }

        auto& param = poly_base->parameters[0];
        if (param.depends_on.size > 0) {
            log_semantic_error("Poly function first argument must not have any dependencies", error_report_node);
            success = false;
            return types.unknown_type;
        }

        Datatype* type = param.infos.type->base_type;
        *type_mods = param.infos.type->mods;
        if (type->type != Datatype_Type::STRUCT_INSTANCE_TEMPLATE) {
            log_semantic_error("Poly function first argument has to be a polymorphic struct", error_report_node);
            success = false;
            return types.unknown_type;
        }

        auto instance = downcast<Datatype_Struct_Instance_Template>(type);
        return upcast(instance->struct_base->body_workload->struct_type);
    };
    auto poly_function_check_argument_count_and_comptime = [&](Polymorphic_Base_Info* poly_base, AST::Node* error_report_node, int required_parameter_count) {
        // Check that no parameters are comptime
        for (int i = 0; i < poly_base->parameter_nodes.size; i++) {
            if (poly_base->parameters[i].is_comptime) {
                log_semantic_error("Poly function must not contain comptime parameters for custom operator", error_report_node);
                success = false;
                break;
            }
        }

        if (poly_base->parameter_nodes.size != required_parameter_count) {
            log_semantic_error("Poly function does not have the required parameter count for this custom operator", error_report_node);
            success = false;
            return;
        }
    };

    switch (change_node->type)
    {
    case AST::Context_Change_Type::IMPORT: 
    {
        auto& path = change_node->options.import_path;
        auto symbol = path_lookup_resolve_to_single_symbol(path, true);

        // Check if symbol is module
        if (symbol->type != Symbol_Type::MODULE)
        {
            if (symbol->type == Symbol_Type::ERROR_SYMBOL) {
                return;
            }
            log_semantic_error("Operator context import requires a module to be passed, but other symbol was specified", upcast(path));
            log_error_info_symbol(symbol);
            return;
        }

        // Wait for other module to finish module analysis (Which may install a new operator context)
        auto other_module = symbol->options.module_progress;
        analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(other_module->module_analysis));
        workload_executer_wait_for_dependency_resolution();

        // Check if we already have this import
        auto other_context = symbol->options.module_progress->module_analysis->symbol_table->operator_context;
        for (int i = 0; i < context->context_imports.size; i++) {
            if (context->context_imports[i] == other_context) {
                return;
            }
        }
        if (other_context == context) {
            return;
        }

        dynamic_array_push_back(&context->context_imports, other_context);
        return;
    }
    case AST::Context_Change_Type::CAST_OPTION:
    {
        auto matching_info = get_info(change_node->options.arguments, true);
        *matching_info = parameter_matching_info_create_empty(Call_Type::CONTEXT_OPTION, 2);
        parameter_matching_info_add_param(matching_info, ids.option, true, false, upcast(types.cast_option));
        parameter_matching_info_add_param(matching_info, ids.option, true, false, upcast(types.cast_mode));
        if (!arguments_match_to_parameters(change_node->options.arguments, matching_info, true)) {
            success = false;
        }

        auto& param_cast_option = matching_info->matched_parameters[0];
        auto& param_cast_mode = matching_info->matched_parameters[1];
        Cast_Option option = (Cast_Option)analyse_parameter_as_comptime_enum(param_cast_option, (int)Cast_Option::MAX_ENUM_VALUE);
        Cast_Mode cast_mode = (Cast_Mode)analyse_parameter_as_comptime_enum(param_cast_mode, (int)Cast_Mode::MAX_ENUM_VALUE);

        if (success)
        {
            if (option == Cast_Option::FROM_BYTE_POINTER || option == Cast_Option::POINTER_TO_POINTER || option == Cast_Option::TO_BYTE_POINTER)
            {
                if (cast_mode == Cast_Mode::INFERRED || cast_mode == Cast_Mode::EXPLICIT) {
                    log_semantic_error("Cannot set cast mode of pointer-casts to non-pointer modes", param_cast_mode.expression);
                    success = false;
                }
            }
            else {
                if (cast_mode == Cast_Mode::POINTER_EXPLICIT || cast_mode == Cast_Mode::POINTER_INFERRED) {
                    log_semantic_error("Cannot set cast mode of normal casts to pointer modes", param_cast_mode.expression);
                    success = false;
                }
            }
        }

        if (success) {
            Custom_Operator_Key key;
            key.type = AST::Context_Change_Type::CAST_OPTION;
            key.options.cast_option = option;
            Custom_Operator op;
            op.cast_mode = cast_mode;
            hashtable_insert_element(&context->custom_operators, key, op);
        }
        else {
            parameter_matching_analyse_in_unknown_context(matching_info);
        }
        break;
    }
    case AST::Context_Change_Type::BINARY_OPERATOR:
    {
        auto matching_info = get_info(change_node->options.arguments, true);
        *matching_info = parameter_matching_info_create_empty(Call_Type::CONTEXT_OPTION, 3);
        parameter_matching_info_add_param(matching_info, ids.binop, true, false, upcast(types.string));
        parameter_matching_info_add_param(matching_info, ids.function, true, false, nullptr);
        parameter_matching_info_add_param(matching_info, ids.commutative, false, false, upcast(types.bool_type));
        if (!arguments_match_to_parameters(change_node->options.arguments, matching_info, true)) {
            success = false;
        }

        auto& param_binop = matching_info->matched_parameters[0];
        auto& param_function = matching_info->matched_parameters[1];
        auto& param_commutative = matching_info->matched_parameters[2];

        AST::Binop binop = AST::Binop::ADDITION;
        if (param_binop.is_set)
        {
            auto binop_expr = param_binop.expression;
            if (binop_expr->type == AST::Expression_Type::LITERAL_READ && binop_expr->options.literal_read.type == Literal_Type::STRING)
            {
                auto expr_info = get_info(binop_expr, true);
                expression_info_set_value(expr_info, upcast(types.string), true);
                parameter_set_analysed(param_binop);

                auto binop_str = binop_expr->options.literal_read.options.string;
                if (binop_str->size == 1)
                {
                    int c = binop_str->characters[0];
                    if (c == '+') {
                        binop = AST::Binop::ADDITION;
                    }
                    else if (c == '-') {
                        binop = AST::Binop::SUBTRACTION;
                    }
                    else if (c == '*') {
                        binop = AST::Binop::MULTIPLICATION;
                    }
                    else if (c == '/') {
                        binop = AST::Binop::DIVISION;
                    }
                    else if (c == '%') {
                        binop = AST::Binop::MODULO;
                    }
                    else {
                        log_semantic_error("Binop string must be one of +,-,*,/,%", upcast(binop_expr));
                        success = false;
                    }
                }
                else {
                    log_semantic_error("Binop string must be one of +,-,*,/,%", upcast(binop_expr));
                    success = false;
                }
            }
            else {
                log_semantic_error("Binop type must be string literal", upcast(binop_expr));
                success = false;
            }
        }

        bool is_commutative = analyse_parameter_as_comptime_bool(param_commutative);

        ModTree_Function* function = nullptr;
        if (param_function.is_set) {
            Expression_Info* info = semantic_analyser_analyse_expression_any(param_function.expression, expression_context_make_unknown());
            parameter_set_analysed(param_function);
            Type_Mods unused;
            function = analyse_expression_info_as_function(param_function.expression, info, 2, true, &unused);
        }

        if (success)
        {
            Custom_Operator op;
            op.binop.function = function;
            op.binop.switch_left_and_right = false;
            op.binop.left_mods = function->signature->parameters[0].type->mods;
            op.binop.right_mods = function->signature->parameters[1].type->mods;
            Custom_Operator_Key key;
            key.type = AST::Context_Change_Type::BINARY_OPERATOR;
            key.options.binop.binop = binop;
            key.options.binop.left_type = function->signature->parameters[0].type->base_type;
            key.options.binop.right_type = function->signature->parameters[1].type->base_type;
            hashtable_insert_element(&context->custom_operators, key, op);

            if (is_commutative) {
                Custom_Operator commutative_op = op;
                commutative_op.binop.switch_left_and_right = true;
                commutative_op.binop.left_mods = op.binop.right_mods;
                commutative_op.binop.right_mods = op.binop.left_mods;
                Custom_Operator_Key commutative_key = key;
                commutative_key.options.binop.left_type = key.options.binop.right_type;
                commutative_key.options.binop.right_type = key.options.binop.left_type;
                hashtable_insert_element(&context->custom_operators, commutative_key, commutative_op);
            }
        }
        else {
            parameter_matching_analyse_in_unknown_context(matching_info);
        }

        break;
    }
    case AST::Context_Change_Type::UNARY_OPERATOR:
    {
        auto matching_info = get_info(change_node->options.arguments, true);
        *matching_info = parameter_matching_info_create_empty(Call_Type::CONTEXT_OPTION, 2);
        parameter_matching_info_add_param(matching_info, ids.unop, true, false, upcast(types.string));
        parameter_matching_info_add_param(matching_info, ids.function, true, false, nullptr);
        if (!arguments_match_to_parameters(change_node->options.arguments, matching_info, true)) {
            success = false;
        }

        auto& param_unop = matching_info->matched_parameters[0];
        auto& param_function = matching_info->matched_parameters[1];

        AST::Unop unop = AST::Unop::NEGATE;
        if (param_unop.is_set)
        {
            auto unop_expr = param_unop.expression;
            if (unop_expr->type == AST::Expression_Type::LITERAL_READ && unop_expr->options.literal_read.type == Literal_Type::STRING)
            {
                auto expr_info = get_info(unop_expr, true);
                expression_info_set_value(expr_info, upcast(types.string), true);
                parameter_set_analysed(param_unop);

                auto unop_str = unop_expr->options.literal_read.options.string;
                if (unop_str->size == 1)
                {
                    int c = unop_str->characters[0];
                    if (c == '-') {
                        unop = AST::Unop::NEGATE;
                    }
                    else if (c == '!') {
                        unop = AST::Unop::NOT;
                    }
                    else {
                        log_semantic_error("Unop string must be either ! or -", upcast(unop_expr));
                        success = false;
                    }
                }
                else {
                    log_semantic_error("Unop string must be either ! or -", upcast(unop_expr));
                    success = false;
                }
            }
            else {
                log_semantic_error("Unop type must be string literal", upcast(unop_expr));
                success = false;
            }
        }

        ModTree_Function* function = nullptr;
        Type_Mods mods;
        if (param_function.is_set) {
            Expression_Info* info = semantic_analyser_analyse_expression_any(param_function.expression, expression_context_make_unknown());
            parameter_set_analysed(param_function);
            function = analyse_expression_info_as_function(param_function.expression, info, 1, true, &mods);
        }

        if (success)
        {
            Custom_Operator op;
            op.unop.function = function;
            op.unop.mods = mods;
            Custom_Operator_Key key;
            key.type = AST::Context_Change_Type::UNARY_OPERATOR;
            key.options.unop.unop = unop;
            key.options.unop.type = function->signature->parameters[0].type->base_type;
            hashtable_insert_element(&context->custom_operators, key, op);
        }
        else {
            parameter_matching_analyse_in_unknown_context(matching_info);
        }
        break;
    }
    case AST::Context_Change_Type::CAST: 
    {
        auto matching_info = get_info(change_node->options.arguments, true);
        *matching_info = parameter_matching_info_create_empty(Call_Type::CONTEXT_OPTION, 2);
        parameter_matching_info_add_param(matching_info, ids.function, true, false, nullptr);
        parameter_matching_info_add_param(matching_info, ids.cast_mode, true, false, upcast(types.cast_mode));
        if (!arguments_match_to_parameters(change_node->options.arguments, matching_info, true)) {
            success = false;
        }

        auto& param_function = matching_info->matched_parameters[0];
        auto& param_cast_mode = matching_info->matched_parameters[1];

        Cast_Mode cast_mode = (Cast_Mode)analyse_parameter_as_comptime_enum(param_cast_mode, (int)Cast_Mode::MAX_ENUM_VALUE);

        // Analyse function
        Custom_Operator op;
        Custom_Operator_Key key;
        key.type = AST::Context_Change_Type::CAST;
        op.custom_cast.cast_mode = cast_mode;

        if (param_function.is_set)
        {
            auto expr = param_function.expression;
            Expression_Info* fn_info = semantic_analyser_analyse_expression_any(expr, expression_context_make_unknown());
            parameter_set_analysed(param_function);
            if (fn_info->result_type == Expression_Result_Type::FUNCTION)
            {
                ModTree_Function* function = analyse_expression_info_as_function(expr, fn_info, 1, true, &op.custom_cast.mods);
                if (function != nullptr) {
                    op.custom_cast.is_polymorphic = false;
                    op.custom_cast.options.function = function;
                    key.options.custom_cast.from_type = function->signature->parameters[0].type->base_type;
                    key.options.custom_cast.to_type = function->signature->return_type.value;
                }
            }
            else if (fn_info->result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION)
            {
                Polymorphic_Base_Info* poly_base = &fn_info->options.polymorphic_function.base->base;
                op.custom_cast.is_polymorphic = true;
                op.custom_cast.options.poly_base = fn_info->options.polymorphic_function.base;
                key.options.custom_cast.from_type = poly_function_check_first_argument(poly_base, upcast(expr), &op.custom_cast.mods);
                poly_function_check_argument_count_and_comptime(poly_base, upcast(expr), 1);

                // Check return type
                if (poly_base->return_type_index != -1)
                {
                    // If the return type is a normal type, just use it as key, otherwise 
                    auto& return_param = poly_base->parameters[poly_base->return_type_index];
                    if (return_param.depends_on.size > 0 || return_param.infos.type->contains_type_template) {
                        key.options.custom_cast.to_type = nullptr;
                    }
                    else {
                        key.options.custom_cast.to_type = return_param.infos.type;
                    }
                }
                else {
                    success = false;
                    log_semantic_error("For custom casts polymorphic function must have a return type", expr);
                }
            }
            else if (fn_info->result_type == Expression_Result_Type::VALUE && datatype_is_unknown(expression_info_get_type(fn_info, false))) {
                success = false;
            }
            else {
                success = false;
                log_semantic_error("Function argument must be either a normal or polymorphic function", expr);
            }
        }

        if (success) {
            hashtable_insert_element(&context->custom_operators, key, op);
        }
        else {
            parameter_matching_analyse_in_unknown_context(matching_info);
        }
        break;
    }
    case AST::Context_Change_Type::ARRAY_ACCESS:
    {
        auto matching_info = get_info(change_node->options.arguments, true);
        *matching_info = parameter_matching_info_create_empty(Call_Type::CONTEXT_OPTION, 1);
        parameter_matching_info_add_param(matching_info, ids.function, true, false, nullptr);
        if (!arguments_match_to_parameters(change_node->options.arguments, matching_info, true)) {
            success = false;
        }

        auto& param_function = matching_info->matched_parameters[0];

        Custom_Operator op;
        Custom_Operator_Key key;
        key.type = AST::Context_Change_Type::ARRAY_ACCESS;

        if (param_function.is_set)
        {
            auto expr = param_function.expression;
            auto fn_info = semantic_analyser_analyse_expression_any(expr, expression_context_make_unknown());
            parameter_set_analysed(param_function);
            if (fn_info->result_type == Expression_Result_Type::FUNCTION)
            {
                ModTree_Function* function = analyse_expression_info_as_function(expr, fn_info, 2, true, &op.array_access.mods);
                op.array_access.is_polymorphic = false;
                op.array_access.options.function = function;
                if (function != nullptr) {
                    key.options.array_access.array_type = function->signature->parameters[0].type->base_type;
                    op.array_access.mods = function->signature->parameters[0].type->mods;
                }
            }
            else if (fn_info->result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION)
            {
                Polymorphic_Base_Info* poly_base = &fn_info->options.polymorphic_function.base->base;
                op.array_access.is_polymorphic = true;
                op.array_access.options.poly_base = fn_info->options.polymorphic_function.base;
                key.options.array_access.array_type = poly_function_check_first_argument(poly_base, upcast(expr), &op.array_access.mods);
                poly_function_check_argument_count_and_comptime(poly_base, upcast(expr), 2);
            }
            else if (fn_info->result_type == Expression_Result_Type::VALUE && datatype_is_unknown(expression_info_get_type(fn_info, false))) {
                success = false;
            }
            else {
                success = false;
                log_semantic_error("Function argument must be either a normal or polymorphic function", expr);
            }
        }

        if (success) {
            hashtable_insert_element(&context->custom_operators, key, op);
        }
        else {
            parameter_matching_analyse_in_unknown_context(matching_info);
        }

        break;
    }
    case AST::Context_Change_Type::DOT_CALL:
    {
        auto matching_info = get_info(change_node->options.arguments, true);
        *matching_info = parameter_matching_info_create_empty(Call_Type::CONTEXT_OPTION, 3);
        parameter_matching_info_add_param(matching_info, ids.function, true, false, nullptr);
        parameter_matching_info_add_param(matching_info, ids.as_member_access, false, false, upcast(types.bool_type));
        parameter_matching_info_add_param(matching_info, ids.name, false, false, upcast(types.string));
        if (!arguments_match_to_parameters(change_node->options.arguments, matching_info, true)) {
            success = false;
        }

        auto& param_function = matching_info->matched_parameters[0];
        auto& param_as_member_access = matching_info->matched_parameters[1];
        auto& param_name = matching_info->matched_parameters[2];

        Custom_Operator op;
        Custom_Operator_Key key;
        key.type = AST::Context_Change_Type::DOT_CALL;

        op.dot_call.dot_call_as_member_access = false;
        if (param_as_member_access.is_set) {
            op.dot_call.dot_call_as_member_access = analyse_parameter_as_comptime_bool(param_as_member_access);
        }
        const bool as_member_access = op.dot_call.dot_call_as_member_access;

        if (param_function.is_set)
        {
            auto expr = param_function.expression;
            auto fn_info = semantic_analyser_analyse_expression_any(expr, expression_context_make_unknown());
            parameter_set_analysed(param_function);
            if (fn_info->result_type == Expression_Result_Type::FUNCTION)
            {
                ModTree_Function* function = fn_info->options.function;
                auto& parameters = function->signature->parameters;
                if (parameters.size == 0) {
                    log_semantic_error("Dotcall function must have at least one parameter", upcast(expr));
                    success = false;
                }
                else if (parameters.size != 1 && as_member_access) {
                    log_semantic_error("Dotcall function as member access must have exactly one parameter", upcast(expr));
                    success = false;
                }
                else {
                    key.options.dot_call.datatype = parameters[0].type->base_type;
                    op.dot_call.mods = parameters[0].type->mods;
                }

                Symbol* symbol = 0;
                if (function->function_type == ModTree_Function_Type::NORMAL) {
                    symbol = function->options.normal.symbol;
                }
                if (symbol != nullptr) {
                    key.options.dot_call.id = symbol->id;
                }
                else {
                    key.options.dot_call.id = nullptr;
                    if (!param_name.is_set) {
                        log_semantic_error("Dotcall with unnamed function requires the use of the name argument", upcast(expr));
                    }
                }

                op.dot_call.is_polymorphic = false;
                op.dot_call.options.function = function;
                op.dot_call.dot_call_as_member_access = as_member_access;
            }
            else if (fn_info->result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION)
            {
                Polymorphic_Base_Info* poly_base = &fn_info->options.polymorphic_function.base->base;
                op.dot_call.is_polymorphic = true;
                op.dot_call.options.poly_base = fn_info->options.polymorphic_function.base;
                key.options.dot_call.datatype = poly_function_check_first_argument(poly_base, upcast(expr), &op.dot_call.mods);
                key.options.dot_call.id = fn_info->options.polymorphic_function.base->base.name;
                poly_function_check_argument_count_and_comptime(poly_base, upcast(expr), poly_base->parameter_nodes.size);
            }
            else if (fn_info->result_type == Expression_Result_Type::VALUE && datatype_is_unknown(expression_info_get_type(fn_info, false))) {
                success = false;
            }
            else {
                success = false;
                log_semantic_error("Function argument must be either a normal or polymorphic function", expr);
            }
        }

        // If name is given, use name instead
        if (param_name.is_set)
        {
            auto name_expr = param_name.expression;
            if (name_expr->type == AST::Expression_Type::LITERAL_READ && name_expr->options.literal_read.type == Literal_Type::STRING) {
                key.options.dot_call.id = name_expr->options.literal_read.options.string;
                auto expr_info = get_info(name_expr, true);
                expression_info_set_value(expr_info, upcast(types.string), true);
                parameter_set_analysed(param_name);
            }
            else {
                log_semantic_error("Dotcall name must be a string literal", upcast(name_expr));
                success = false;
            }
        }

        if (success) {
            hashtable_insert_element(&context->custom_operators, key, op);
        }
        else {
            parameter_matching_analyse_in_unknown_context(matching_info);
        }
        break;
    }
    case AST::Context_Change_Type::ITERATOR: 
    {
        auto matching_info = get_info(change_node->options.arguments, true);
        *matching_info = parameter_matching_info_create_empty(Call_Type::CONTEXT_OPTION, 4);
        parameter_matching_info_add_param(matching_info, ids.create_fn, true, false, nullptr);
        parameter_matching_info_add_param(matching_info, ids.has_next_fn, true, false, nullptr);
        parameter_matching_info_add_param(matching_info, ids.next_fn, true, false, nullptr);
        parameter_matching_info_add_param(matching_info, ids.value_fn, true, false, nullptr);
        if (!arguments_match_to_parameters(change_node->options.arguments, matching_info, true)) {
            success = false;
        }

        auto& param_create_fn = matching_info->matched_parameters[0];
        auto& param_has_next_fn = matching_info->matched_parameters[1];
        auto& param_next_fn = matching_info->matched_parameters[2];
        auto& param_value_fn = matching_info->matched_parameters[3];

        Custom_Operator_Key key;
        key.type = AST::Context_Change_Type::ITERATOR;
        Custom_Operator op;
        auto& iter = op.iterator;
        iter.is_polymorphic = false; // Depends on the type of the create function expression

        Datatype* iterator_type = types.unknown_type; // Note: Iterator type is not available for polymorphic functions

        if (param_create_fn.is_set && success)
        {
            auto function_node = param_create_fn.expression;
            auto fn_info = semantic_analyser_analyse_expression_any(function_node, expression_context_make_unknown());
            parameter_set_analysed(param_create_fn);
            if (fn_info->result_type == Expression_Result_Type::FUNCTION)
            {
                ModTree_Function* function = fn_info->options.function;
                iter.options.normal.create = function;
                auto& parameters = function->signature->parameters;
                if (parameters.size == 1) {
                    key.options.iterator.datatype = parameters[0].type->base_type;
                    op.iterator.iterable_mods = parameters[0].type->mods;
                    if (types_are_equal(key.options.iterator.datatype, types.unknown_type)) {
                        success = false;
                    }
                }
                else {
                    log_semantic_error("Iterator create function must have exactly one argument", upcast(function_node));
                    success = false;
                }

                if (function->signature->return_type.available) {
                    iterator_type = function->signature->return_type.value;
                    if (datatype_is_unknown(iterator_type)) {
                        success = false;
                    }
                }
                else {
                    log_semantic_error("iterator_create function must return a value (iterator)", upcast(function_node));
                    success = false;
                }
            }
            else if (fn_info->result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION)
            {
                Polymorphic_Base_Info* poly_base = &fn_info->options.polymorphic_function.base->base;
                iter.is_polymorphic = true;
                iter.options.polymorphic.fn_create = fn_info->options.polymorphic_function.base;

                poly_function_check_argument_count_and_comptime(poly_base, upcast(function_node), 1);
                key.options.iterator.datatype = poly_function_check_first_argument(poly_base, upcast(function_node), &op.iterator.iterable_mods);

                // Function must have return type
                if (poly_base->return_type_index == -1) {
                    log_semantic_error("iterator_create function must return a value (iterator)", upcast(function_node));
                    success = false;
                }
            }
            else if (fn_info->result_type == Expression_Result_Type::VALUE && datatype_is_unknown(expression_info_get_type(fn_info, false))) {
                success = false;
            }
            else {
                log_semantic_error("add_iterator argument must be a function", upcast(function_node));
                success = false;
            }
        }

        // Function_index: 1 = has_next, 2 = next, 3 = get_value
        auto analyse_iter_fn = [&](Parameter_Match& function_param, int function_index)
        {
            if (!function_param.is_set || !success) return;

            auto fn_expr = function_param.expression;
            auto fn_info = semantic_analyser_analyse_expression_any(fn_expr, expression_context_make_unknown());
            parameter_set_analysed(function_param);

            ModTree_Function** function_pointer_to_set = nullptr;
            Polymorphic_Function_Base** poly_pointer_to_set = nullptr;
            bool should_have_return_type = false;
            bool return_should_be_boolean = false;
            if (function_index == 1) {
                function_pointer_to_set = &op.iterator.options.normal.has_next;
                poly_pointer_to_set = &op.iterator.options.polymorphic.has_next;
                should_have_return_type = true;
                return_should_be_boolean = true;
            }
            else if (function_index == 2) {
                function_pointer_to_set = &op.iterator.options.normal.next;
                poly_pointer_to_set = &op.iterator.options.polymorphic.next;
                should_have_return_type = false;
                return_should_be_boolean = false;
            }
            else if (function_index == 3) {
                function_pointer_to_set = &op.iterator.options.normal.get_value;
                poly_pointer_to_set = &op.iterator.options.polymorphic.get_value;
                should_have_return_type = true;
                return_should_be_boolean = false;
            }
            else {
                panic("");
            }

            if (fn_info->result_type == Expression_Result_Type::FUNCTION)
            {
                ModTree_Function* function = fn_info->options.function;
                if (!iter.is_polymorphic) {
                    *function_pointer_to_set = function;
                }
                else {
                    log_semantic_error("Expected polymorphic function, as iter create function is also polymorphic", upcast(fn_expr));
                    success = false;
                }

                auto& parameters = function->signature->parameters;
                if (parameters.size == 1)
                {
                    Datatype* arg_type = parameters[0].type->base_type;
                    if (datatype_is_unknown(arg_type)) {
                        success = false;
                    }
                    else if (!(types_are_equal(arg_type->base_type, iterator_type->base_type) &&
                        datatype_check_if_auto_casts_to_other_mods(arg_type, iterator_type->mods, false)))
                    {
                        log_semantic_error("Function parameter type must be compatible with iterator type (Create function return type)", upcast(fn_expr));
                        log_error_info_given_type(arg_type);
                        log_error_info_expected_type(iterator_type);
                        success = false;
                    }
                }
                else {
                    log_semantic_error("Iterator has_next function must have exactly one argument", upcast(fn_expr));
                    success = false;
                }

                // Check return value
                if (function->signature->return_type.available)
                {
                    if (!should_have_return_type) {
                        log_semantic_error("Function should not have a return value", upcast(fn_expr));
                        success = false;
                    }
                    else {
                        if (return_should_be_boolean && !types_are_equal(upcast(types.bool_type), function->signature->return_type.value)) {
                            log_semantic_error("Function return type should be bool", upcast(fn_expr));
                            success = false;
                        }
                    }
                }
                else {
                    if (should_have_return_type) {
                        log_semantic_error("Function should have a return value", upcast(fn_expr));
                        success = false;
                    }
                }
            }
            else if (fn_info->result_type == Expression_Result_Type::POLYMORPHIC_FUNCTION)
            {
                Polymorphic_Base_Info* poly_base = &fn_info->options.polymorphic_function.base->base;
                if (iter.is_polymorphic) {
                    *poly_pointer_to_set = fn_info->options.polymorphic_function.base;
                }
                else {
                    log_semantic_error("Expected normal (non-polymorphic) function for has_next function", upcast(fn_expr));
                    success = false;
                    return;
                }

                // Check parameters
                poly_function_check_argument_count_and_comptime(poly_base, upcast(fn_expr), 1);
                Type_Mods unused;
                Datatype* arg_type = poly_function_check_first_argument(poly_base, upcast(fn_expr), &unused);
                if (!success) return;

                // Note: We don't know the iterator type from the make function, as this is currently not provided by the
                //      polymorphic base analysis. So here we only check that everything works
                if (types_are_equal(arg_type->base_type, types.unknown_type)) {
                    success = false;
                }

                // Check return type
                if (poly_base->return_type_index != -1)
                {
                    Datatype* return_type = poly_base->parameters[poly_base->return_type_index].infos.type;
                    if (!should_have_return_type) {
                        log_semantic_error("Function should not have a return value", upcast(fn_expr));
                        success = false;
                    }
                    else {
                        if (return_should_be_boolean && !types_are_equal(upcast(types.bool_type), return_type)) {
                            log_semantic_error("Function return type should be bool", upcast(fn_expr));
                            success = false;
                        }
                    }
                }
                else {
                    if (should_have_return_type) {
                        log_semantic_error("Function should have a return value", upcast(fn_expr));
                        success = false;
                    }
                }
            }
            else if (fn_info->result_type == Expression_Result_Type::VALUE && datatype_is_unknown(expression_info_get_type(fn_info, false))) {
                success = false;
            }
            else {
                log_semantic_error("Argument must be a function", upcast(fn_expr));
                success = false;
            }
        };

        analyse_iter_fn(param_has_next_fn, 1);
        analyse_iter_fn(param_next_fn, 2);
        analyse_iter_fn(param_value_fn, 3);

        if (success) {
            hashtable_insert_element(&context->custom_operators, key, op);
        }
        else {
            parameter_matching_analyse_in_unknown_context(matching_info);
        }
        break;
    }
    case AST::Context_Change_Type::INVALID: break;
    default: panic("");
    }
}



// STATEMENTS
bool code_block_is_loop(AST::Code_Block* block)
{
    if (block != 0 && block->base.parent->type == AST::Node_Type::STATEMENT) {
        auto parent = (AST::Statement*) block->base.parent;
        return parent->type == AST::Statement_Type::WHILE_STATEMENT || parent->type == AST::Statement_Type::FOR_LOOP;
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
            if (is_continue && !code_block_is_loop(found_block)) {
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
        // if (expression_node->type != AST::Expression_Type::FUNCTION_CALL) {
        //     log_semantic_error("Expression statement must be a function call", statement);
        // }
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
        switch_type = datatype_get_non_const_type(switch_type);
        auto& switch_info = get_info(statement)->specifics.switch_statement;
        switch_info.base_content = nullptr;

        // Check switch value
        Struct_Content* struct_content = nullptr;
        Datatype* condition_type = switch_type;
        if (switch_type->type == Datatype_Type::STRUCT)
        {
            type_wait_for_size_info_to_finish(switch_type);
            struct_content = type_mods_get_subtype(downcast<Datatype_Struct>(switch_type->base_type), switch_type->mods);
            if (struct_content->subtypes.size != 0) {
                switch_type = struct_content->tag_member.type;
                switch_info.base_content = struct_content;
            }
            else {
                log_semantic_error("Switch value must be a struct with subtypes or an enum!", switch_node.condition);
                switch_type = types.unknown_type;
                struct_content = nullptr;
            }
        }
        else if (switch_type->type != Datatype_Type::ENUM)
        {
            log_semantic_error("Switch only works on either enum or struct subtypes", switch_node.condition);
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
            case_info->variable_symbol = 0;

            // Analyse case value
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

            // If a variable name is given, create a new symbol for it
            Symbol_Table* restore_table = semantic_analyser.current_workload->current_symbol_table;
            SCOPE_EXIT(semantic_analyser.current_workload->current_symbol_table = restore_table);
            if (case_node->variable_definition.available && case_info->is_valid)
            {
                Symbol_Table* case_table = symbol_table_create_with_parent(restore_table, Symbol_Access_Level::INTERNAL);
                semantic_analyser.current_workload->current_symbol_table = case_table;
                Symbol* var_symbol = symbol_table_define_symbol(
                    case_table, case_node->variable_definition.value->name, Symbol_Type::VARIABLE, upcast(case_node->variable_definition.value),
                    Symbol_Access_Level::INTERNAL
                );
                var_symbol->options.variable_type = types.unknown_type;
                case_info->variable_symbol = var_symbol;

                if (struct_content != nullptr)
                {
                    // Variable is a pointer to the subtype
                    Struct_Content* subtype = struct_content->subtypes[case_info->case_value - 1];
                    auto result_subtype = type_system_make_type_with_mods(
                        condition_type->base_type,
                        type_mods_make(condition_type->mods.is_constant, 1, 0, 0, subtype->index)
                    );
                    var_symbol->options.variable_type = result_subtype;
                }
                else {
                    log_semantic_error("Case variables are only valid if the switch value is a struct with subtypes", upcast(case_node), Parser::Section::END_TOKEN);
                }
            }

            // Analyse block and block flow
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
                log_semantic_error("Not all cases are handled by switch", statement, Parser::Section::KEYWORD);
            }
        }
        return switch_flow;
    }
    case AST::Statement_Type::WHILE_STATEMENT:
    {
        auto& while_node = statement->options.while_statement;
        semantic_analyser_analyse_expression_value(while_node.condition, expression_context_make_specific_type(upcast(types.bool_type)));

        semantic_analyser_analyse_block(while_node.block);
        EXIT(Control_Flow::SEQUENTIAL); // Loops are always sequential, since the condition may not be met before the first iteration
    }
    case AST::Statement_Type::FOR_LOOP:
    {
        auto& for_loop = statement->options.for_loop;

        // Create new table for loop variable
        auto symbol_table = symbol_table_create_with_parent(semantic_analyser.current_workload->current_symbol_table, Symbol_Access_Level::INTERNAL);
        info->specifics.for_loop.symbol_table = symbol_table;

        // Analyse loop variable 
        {
            Symbol* symbol = symbol_table_define_symbol(
                symbol_table, for_loop.loop_variable_definition->name, Symbol_Type::VARIABLE, upcast(for_loop.loop_variable_definition), Symbol_Access_Level::INTERNAL
            );
            info->specifics.for_loop.loop_variable_symbol = symbol;
            Expression_Context context = expression_context_make_unknown();
            if (for_loop.loop_variable_type.available) {
                context = expression_context_make_specific_type(semantic_analyser_analyse_expression_type(for_loop.loop_variable_type.value));
            }
            symbol->options.variable_type = semantic_analyser_analyse_expression_value(for_loop.initial_value, context);
            if (!for_loop.loop_variable_type.available) {
                symbol->options.variable_type = datatype_get_non_const_type(symbol->options.variable_type);
            }
        }
        // Use new symbol table for condition + increment
        RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->current_symbol_table, symbol_table);

        // Analyse condition
        semantic_analyser_analyse_expression_value(for_loop.condition, expression_context_make_specific_type(upcast(types.bool_type)));

        // Analyse increment statement
        {
            auto flow = semantic_analyser_analyse_statement(for_loop.increment_statement);
            assert(flow == Control_Flow::SEQUENTIAL, "");
        }

        // Analyse block
        semantic_analyser_analyse_block(for_loop.body_block);
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
        auto symbol_table = symbol_table_create_with_parent(semantic_analyser.current_workload->current_symbol_table, Symbol_Access_Level::INTERNAL);
        loop_info.symbol_table = symbol_table;

        // Analyse expression
        Datatype* expr_type = semantic_analyser_analyse_expression_value(for_loop.expression, expression_context_make_unknown());
        if (datatype_is_unknown(expr_type)) {
            EXIT(Control_Flow::SEQUENTIAL);
        }
        Datatype* iterable_type = expr_type->base_type;

        // Create loop variable symbol
        Symbol* symbol = symbol_table_define_symbol(
            symbol_table, for_loop.loop_variable_definition->name, Symbol_Type::VARIABLE,
            upcast(for_loop.loop_variable_definition), Symbol_Access_Level::INTERNAL
        );
        loop_info.loop_variable_symbol = symbol;
        symbol->options.variable_type = types.unknown_type; // Should be updated by further code

        // Find loop-variable type
        Type_Mods expected_mods = type_mods_make(true, 0, 0, 0);
        if (iterable_type->type == Datatype_Type::SLICE || iterable_type->type == Datatype_Type::ARRAY)
        {
            Datatype* element_type = nullptr;
            if (iterable_type->type == Datatype_Type::ARRAY) {
                symbol->options.variable_type = upcast(type_system_make_pointer(downcast<Datatype_Array>(iterable_type)->element_type));
            }
            else if (iterable_type->type == Datatype_Type::SLICE) {
                symbol->options.variable_type = upcast(type_system_make_pointer(downcast<Datatype_Slice>(iterable_type)->element_type));
            }
            else {
                log_semantic_error("Currently only arrays and slices are supported for foreach loop", for_loop.expression);
                EXIT(Control_Flow::SEQUENTIAL);
            }
        }
        else
        {
            // Check for custom iterator
            auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;

            Custom_Operator_Key key;
            key.type = AST::Context_Change_Type::ITERATOR;
            key.options.iterator.datatype = iterable_type;
            Custom_Operator* op = operator_context_query_custom_operator(operator_context, key);

            // Check for polymorphic overload
            if (op == nullptr && expr_type->type == Datatype_Type::STRUCT) {
                auto struct_type = downcast<Datatype_Struct>(expr_type);
                if (struct_type->workload != nullptr && struct_type->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
                    key.options.iterator.datatype = upcast(struct_type->workload->polymorphic.instance.parent->body_workload->struct_type);
                    op = operator_context_query_custom_operator(operator_context, key);
                }
            }

            if (op != nullptr)
            {
                expected_mods = op->iterator.iterable_mods;
                loop_info.is_custom_op = true;

                bool success = true;
                if (op->iterator.is_polymorphic)
                {
                    // Now I have to instanciate 4 different functions here + check pointer levels I guezz
                    auto& poly_bases = op->iterator.options.polymorphic;

                    Parameter_Matching_Info matching_info = parameter_matching_info_create_empty(Call_Type::POLYMORPHIC_FUNCTION, 1);
                    SCOPE_EXIT(parameter_matching_info_destroy(&matching_info));

                    // Prepare instanciation data
                    parameter_matching_info_add_param(&matching_info, compiler.predefined_ids.value, true, false, nullptr);
                    auto expr_info = get_info(for_loop.expression);
                    auto& param_info = matching_info.matched_parameters[0];
                    param_info.state = Parameter_State::ANALYSED;
                    param_info.is_set = true;
                    param_info.expression = for_loop.expression;
                    param_info.argument_type = expr_info->cast_info.result_type;
                    param_info.argument_is_temporary_value = expr_info->cast_info.result_value_is_temporary;

                    // create function
                    Datatype* iterator_type = types.unknown_type;
                    {
                        matching_info.options.poly_function = poly_bases.fn_create;
                        Polymorphic_Base_Info* poly_base = &matching_info.options.poly_function->base;
                        Instanciation_Result result = instanciate_polymorphic_callable(
                            &matching_info, expression_context_make_unknown(), upcast(statement), Parser::Section::KEYWORD
                        );
                        if (result.type == Instanciation_Result_Type::FUNCTION) {
                            loop_info.custom_op.fn_create = result.options.function;
                            assert(result.options.function->signature->return_type.available, "");
                            iterator_type = result.options.function->signature->return_type.value;
                        }
                        else {
                            log_semantic_error("Could not instanciate create function of custom iterator", upcast(statement));
                            success = false;
                        }

                        // Update expression type, as instanciation may change it
                        expr_type = get_info(for_loop.expression)->cast_info.result_type;
                    }

                    // has_next function
                    if (success)
                    {
                        // Set parameter to iterator
                        param_info.expression = nullptr;
                        param_info.argument_type = iterator_type;
                        param_info.argument_is_temporary_value = false;

                        // Instanciate
                        matching_info.options.poly_function = poly_bases.has_next;
                        Polymorphic_Base_Info* poly_base = &matching_info.options.poly_function->base;
                        Instanciation_Result result = instanciate_polymorphic_callable(
                            &matching_info, expression_context_make_unknown(), upcast(statement), Parser::Section::KEYWORD
                        );
                        if (result.type == Instanciation_Result_Type::FUNCTION) {
                            loop_info.custom_op.fn_has_next = result.options.function;
                        }
                        else {
                            log_semantic_error("Could not instanciate has_next function of custom iterator", upcast(statement));
                            success = false;
                        }
                    }

                    // next function
                    if (success)
                    {
                        // Re-set parameter to iterator
                        param_info.expression = nullptr;
                        param_info.argument_type = iterator_type;
                        param_info.argument_is_temporary_value = false;

                        // Instanciate
                        matching_info.options.poly_function = poly_bases.next;
                        Polymorphic_Base_Info* poly_base = &matching_info.options.poly_function->base;
                        Instanciation_Result result = instanciate_polymorphic_callable(
                            &matching_info, expression_context_make_unknown(), upcast(statement), Parser::Section::KEYWORD
                        );
                        if (result.type == Instanciation_Result_Type::FUNCTION) {
                            loop_info.custom_op.fn_next = result.options.function;
                        }
                        else {
                            success = false;
                            log_semantic_error("Could not instanciate next function of custom iterator", upcast(statement));
                        }
                    }

                    // get_value
                    if (success)
                    {
                        // Re-set parameter to iterator
                        param_info.expression = nullptr;
                        param_info.argument_type = iterator_type;
                        param_info.argument_is_temporary_value = false;

                        // Instanciate
                        matching_info.options.poly_function = poly_bases.get_value;
                        Polymorphic_Base_Info* poly_base = &matching_info.options.poly_function->base;
                        Instanciation_Result result = instanciate_polymorphic_callable(
                            &matching_info, expression_context_make_unknown(), upcast(statement), Parser::Section::KEYWORD
                        );
                        if (result.type == Instanciation_Result_Type::FUNCTION) {
                            loop_info.custom_op.fn_get_value = result.options.function;
                        }
                        else {
                            success = false;
                            log_semantic_error("Could not instanciate get_value of custom iterator", upcast(statement));
                        }
                    }

                    // Note: Not quite sure if we need to re-check parameters mods and return types, but I guess it cannot hurt...
                    if (success)
                    {
                        // Check create function
                        if (datatype_is_unknown(iterator_type)) {
                            success = false;
                        }

                        ModTree_Function* functions[3] = { loop_info.custom_op.fn_get_value, loop_info.custom_op.fn_has_next, loop_info.custom_op.fn_next };
                        for (int i = 0; i < 3; i++)
                        {
                            ModTree_Function* function = functions[i];
                            if (function->signature->parameters.size != 1) {
                                log_semantic_error("Instanciated function did not have exactly 1 parameter", upcast(for_loop.expression));
                                success = false;
                                continue;
                            }

                            Datatype* param_type = function->signature->parameters[0].type;
                            if (!types_are_equal(param_type->base_type, iterator_type->base_type)) {
                                log_semantic_error("Instanciated function parameter type did not match iterator type", upcast(for_loop.expression));
                                success = false;
                                continue;
                            }

                            if (!datatype_check_if_auto_casts_to_other_mods(iterator_type, param_type->mods, false)) {
                                log_semantic_error("Instanciated function parameter mods were not compatible with iterator_type mods", upcast(for_loop.expression));
                                success = false;
                                continue;
                            }
                        }

                        if (!loop_info.custom_op.fn_get_value->signature->return_type.available) {
                            log_semantic_error("Get value function instanciation did not return a value", upcast(for_loop.expression));
                            success = false;
                        }
                    }
                }
                else {
                    loop_info.custom_op.fn_create = op->iterator.options.normal.create;
                    loop_info.custom_op.fn_has_next = op->iterator.options.normal.has_next;
                    loop_info.custom_op.fn_next = op->iterator.options.normal.next;
                    loop_info.custom_op.fn_get_value = op->iterator.options.normal.get_value;
                }

                if (success) {
                    auto& op = loop_info.custom_op;
                    semantic_analyser_register_function_call(op.fn_create);
                    semantic_analyser_register_function_call(op.fn_get_value);
                    semantic_analyser_register_function_call(op.fn_next);
                    semantic_analyser_register_function_call(op.fn_has_next);
                    symbol->options.variable_type = op.fn_get_value->signature->return_type.value;

                    int it_ptr_lvl = op.fn_create->signature->return_type.value->mods.pointer_level;
                    loop_info.custom_op.has_next_pointer_diff = it_ptr_lvl - op.fn_has_next->signature->parameters[0].type->mods.pointer_level;
                    loop_info.custom_op.next_pointer_diff = it_ptr_lvl - op.fn_next->signature->parameters[0].type->mods.pointer_level;
                    loop_info.custom_op.get_value_pointer_diff = it_ptr_lvl - op.fn_get_value->signature->parameters[0].type->mods.pointer_level;
                }
            }
            else
            {
                log_semantic_error("Cannot loop over given datatype", for_loop.expression);
                log_error_info_given_type(expr_type);
            }
        }

        if (!try_updating_expression_type_mods(for_loop.expression, expected_mods)) {
            log_semantic_error("Pointer level invalid for this iterable type", for_loop.expression);
        }

        // Create index variable if available
        if (for_loop.index_variable_definition.available)
        {
            // Use current symbol table so collisions are handled
            RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->current_symbol_table, symbol_table);
            Symbol* index_symbol = symbol_table_define_symbol(
                symbol_table, for_loop.index_variable_definition.value->name, Symbol_Type::VARIABLE,
                upcast(for_loop.index_variable_definition.value), Symbol_Access_Level::INTERNAL
            );
            loop_info.index_variable_symbol = index_symbol;
            index_symbol->options.variable_type = upcast(types.i32_type);
        }
        RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->current_symbol_table, symbol_table);

        // Analyse body
        semantic_analyser_analyse_block(for_loop.body_block);
        EXIT(Control_Flow::SEQUENTIAL);
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
    case AST::Statement_Type::BINOP_ASSIGNMENT:
    {
        auto& assignment = statement->options.binop_assignment;

        int x = 15;
        const int* xp = &x;

        // Initialize info as non-overloaded (primitive binop)
        info->specifics.overload.function = 0;
        info->specifics.overload.switch_arguments = false;

        // Analyse expr side
        Datatype* left_type = semantic_analyser_analyse_expression_value(assignment.left_side, expression_context_make_auto_dereference());
        if (!expression_has_memory_address(assignment.left_side)) {
            log_semantic_error("Left side must have a memory address/cannot be a temporary value for assignment", upcast(statement), Parser::Section::WHOLE_NO_CHILDREN);
        }
        if (left_type->type == Datatype_Type::CONSTANT) {
            log_semantic_error("Trying to assign to a constant value", upcast(statement), Parser::Section::WHOLE_NO_CHILDREN);
        }

        // Analyse right side
        Datatype* right_type = semantic_analyser_analyse_expression_value(assignment.right_side, expression_context_make_auto_dereference());

        // Check for unknowns
        if (datatype_is_unknown(left_type) || datatype_is_unknown(right_type)) {
            EXIT(Control_Flow::SEQUENTIAL);
        }

        // Check if binop is a primitive operation (ints, floats, bools, pointers)
        bool types_are_valid = false;
        if (types_are_equal(left_type->base_type, right_type->base_type))
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

            Datatype* operand_type = left_type->base_type;
            if (operand_type->base_type->type == Datatype_Type::PRIMITIVE)
            {
                auto primitive = downcast<Datatype_Primitive>(operand_type->base_type);
                if (primitive->primitive_type == Primitive_Type::INTEGER) {
                    types_are_valid = int_valid;
                }
                else if (primitive->primitive_type == Primitive_Type::FLOAT) {
                    types_are_valid = float_valid;
                }
            }
        }

        // Check for operator overloads if it isn't a primitive operation
        auto& operator_context = semantic_analyser.current_workload->current_symbol_table->operator_context;
        if (!types_are_valid)
        {
            Custom_Operator_Key key;
            key.type = AST::Context_Change_Type::BINARY_OPERATOR;
            key.options.binop.binop = assignment.binop;
            key.options.binop.left_type = left_type;
            key.options.binop.right_type = right_type;
            auto overload = operator_context_query_custom_operator(operator_context, key);
            if (overload != nullptr)
            {
                auto function = overload->binop.function;
                assert(function->signature->return_type.available, "");
                if (!types_are_equal(left_type, function->signature->return_type.value)) {
                    log_semantic_error(
                        "Overload for this binop not valid, as assignment requires return type to be the same",
                        upcast(statement), Parser::Section::WHOLE_NO_CHILDREN
                    );
                }
                info->specifics.overload.function = function;
                info->specifics.overload.switch_arguments = overload->binop.switch_left_and_right;
                semantic_analyser_register_function_call(function);
                types_are_valid = true;
            }
        }

        if (!types_are_valid) {
            log_semantic_error("Types aren't valid for binary operation", upcast(statement), Parser::Section::WHOLE_NO_CHILDREN);
            log_error_info_binary_op_type(left_type, right_type);
            EXIT(Control_Flow::SEQUENTIAL);
        }

        EXIT(Control_Flow::SEQUENTIAL);
        break;
    }
    case AST::Statement_Type::ASSIGNMENT:
    {
        auto& assignment_node = statement->options.assignment;
        info->specifics.is_struct_split = false; // Note: Struct split has been removed currently

        // Analyse expr side
        Datatype* all_left_types = 0;
        bool left_side_all_same_type = true;
        for (int i = 0; i < assignment_node.left_side.size; i++)
        {
            auto expr = assignment_node.left_side[i];
            Expression_Context context = expression_context_make_unknown();
            if (assignment_node.type == AST::Assignment_Type::DEREFERENCE) {
                context = expression_context_make_auto_dereference();
            }
            auto left_type = semantic_analyser_analyse_expression_value(expr, context);

            // Check for errors
            if (!expression_has_memory_address(expr)) {
                log_semantic_error("Cannot assign to a temporary value", upcast(expr));
            }
            if (left_type->type == Datatype_Type::CONSTANT) {
                log_semantic_error("Trying to assign to a constant value", expr);
            }
            bool is_pointer = datatype_is_pointer(left_type);
            if (assignment_node.type == AST::Assignment_Type::POINTER && !is_pointer && !datatype_is_unknown(left_type)) {
                log_semantic_error("Pointer assignment requires left-side to be a pointer!", upcast(statement), Parser::Section::WHOLE_NO_CHILDREN);
                left_type = upcast(type_system_make_pointer(left_type));
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
                assignment_node.right_side[0], expression_context_make_specific_type(all_left_types)
            );
        }
        else
        {
            if (assignment_node.left_side.size != assignment_node.right_side.size) {
                log_semantic_error("Left side and right side of assignment have different count", upcast(statement), Parser::Section::WHOLE_NO_CHILDREN);
            }

            // Analyse right side
            for (int i = 0; i < assignment_node.right_side.size; i++) {
                auto right_node = assignment_node.right_side[i];
                Expression_Context context;
                if (i < assignment_node.left_side.size) {
                    auto left_node = assignment_node.left_side[i];
                    context = expression_context_make_specific_type(expression_info_get_type(get_info(left_node), false));
                }
                else {
                    context = expression_context_make_unknown(true);
                }
                semantic_analyser_analyse_expression_value(right_node, context);
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
        auto& predefined_types = compiler.type_system.predefined_types;
        info->specifics.is_struct_split = false; // Note: removed currently

        // Check if this was already handled at block start
        if (definition->is_comptime) {
            EXIT(Control_Flow::SEQUENTIAL);
        }

        // Initialize symbols
        for (int i = 0; i < symbol_nodes.size; i++) {
            auto symbol = get_info(symbol_nodes[i])->symbol;
            symbol->type = Symbol_Type::VARIABLE_UNDEFINED;
            symbol->options.variable_type = compiler.type_system.predefined_types.unknown_type;
        }

        // Analyse types
        bool all_types_are_equal = true;
        Datatype* equal_type = 0;
        for (int i = 0; i < types.size; i++)
        {
            // Analyse type
            auto type_node = types[i];
            Datatype* type = semantic_analyser_analyse_expression_type(type_node);

            // Check for errors
            bool is_pointer = datatype_is_pointer(type);
            if (definition->assignment_type == AST::Assignment_Type::DEREFERENCE && type->mods.pointer_level > 0) {
                log_semantic_error("Type must not be a pointer-type with normal value definition 'x: foo ='", type_node);
                type = type_system_make_type_with_mods(
                    type->base_type, type_mods_make(type->mods.is_constant, 0, 0, 0, type->mods.subtype_index)
                );
            }
            else if (definition->assignment_type == AST::Assignment_Type::POINTER && !is_pointer) {
                log_semantic_error("Type must be a pointer-type with pointer definition ': foo =*'", type_node);
                type = upcast(type_system_make_pointer(type));
            }

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
                    auto symbol = get_info(symbol_nodes[j])->symbol;
                    symbol->options.variable_type = type;
                }
            }
            else
            {
                if (i < symbol_nodes.size) {
                    auto symbol = get_info(symbol_nodes[i])->symbol;
                    symbol->options.variable_type = type;
                }
                else {
                    log_semantic_error("No symbol exists on the left side of definition for this type", type_node);
                }
            }
        }

        // Analyse values
        for (int i = 0; i < values.size; i++)
        {
            // Figure out context
            Expression_Context context;
            if (types.size == 0)
            {
                if (definition->assignment_type == AST::Assignment_Type::DEREFERENCE) {
                    context = expression_context_make_auto_dereference();
                }
                else {
                    context = expression_context_make_unknown();
                }
            }
            else if (types.size == 1) {
                auto type_info = get_info(types[0]);
                if (type_info->result_type == Expression_Result_Type::TYPE) {
                    context = expression_context_make_specific_type(type_info->options.type);
                }
                else {
                    context = expression_context_make_unknown(true);
                }
            }
            else { // types.size > 1
                if (values.size == 1) { // Value broadcast
                    if (all_types_are_equal) {
                        context = expression_context_make_specific_type(equal_type);
                    }
                    else {
                        log_semantic_error("For value broadcast all specified types must be equal", values[i]);
                        context = expression_context_make_unknown(true);
                    }
                }
                else
                {
                    if (i < types.size) {
                        auto type_info = get_info(types[i]);
                        if (type_info->result_type == Expression_Result_Type::TYPE) {
                            context = expression_context_make_specific_type(type_info->options.type);
                        }
                        else {
                            context = expression_context_make_unknown(true);
                        }
                    }
                    else {
                        log_semantic_error("No type is specified in the definition for this value", values[i]);
                        context = expression_context_make_unknown(true);
                    }
                }
            }

            // Analyse value
            Datatype* value_type = semantic_analyser_analyse_expression_value(values[i], context);
            Type_Mods original_mods = value_type->mods;

            // Note: Constant tag gets removed when infering the type, e.g. x := 15
            if (types.size == 0) {
                value_type = datatype_get_non_const_type(value_type);
            }

            // If no types are given, check if value matches the definition type
            if (types.size == 0)
            {
                bool value_is_pointer = datatype_is_pointer(value_type);
                if (definition->assignment_type == AST::Assignment_Type::POINTER && !value_is_pointer) {
                    if (try_updating_expression_type_mods(values[i], type_mods_make(original_mods.is_constant, 1, 0, 0, value_type->mods.subtype_index))) {
                        value_type = get_info(values[i])->cast_info.result_type;
                    }
                    else {
                        log_semantic_error("Pointer assignment ':=*' expected a value that is a pointer!", values[i]);
                        value_type = upcast(type_system_make_pointer(value_type));
                    }
                }
            }

            // Update symbol type
            if (values.size == 1) { // Value broadcast
                if (types.size == 0) {
                    for (int j = 0; j < symbol_nodes.size; j++) {
                        auto symbol = get_info(symbol_nodes[j])->symbol;
                        symbol->options.variable_type = value_type;
                    }
                }
            }
            else
            {
                if (i < symbol_nodes.size) {
                    if (types.size == 0) {
                        auto symbol = get_info(symbol_nodes[i])->symbol;
                        symbol->options.variable_type = value_type;
                    }
                }
                else {
                    log_semantic_error("No symbol/variable exists for this value", values[i]);
                }
            }
        }

        // Set symbols to defined
        for (int i = 0; i < symbol_nodes.size; i++) {
            get_info(symbol_nodes[i])->symbol->type = Symbol_Type::VARIABLE;
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

    // Handle import statements 
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

    // Create symbols and workloads for definitions inside the block
    {
        auto fn = semantic_analyser.current_workload->current_function;

        bool dont_define_comptime_symbols = false;
        if (fn->function_type == ModTree_Function_Type::NORMAL)
        {
            auto progress = fn->options.normal.progress;
            if (progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
                dont_define_comptime_symbols = true;
            }
        }

        for (int i = 0; i < block->statements.size; i++) {
            if (block->statements[i]->type == AST::Statement_Type::DEFINITION) {
                auto definition = block->statements[i]->options.definition;
                if (!(dont_define_comptime_symbols && definition->is_comptime)) {
                    analyser_create_symbol_and_workload_for_definition(definition, upcast(last_import_workload));
                }
            }
        }
    }

    // Create operator context workloads if changes exist
    if (block->context_changes.size != 0) {
        auto operator_context = symbol_table_install_new_operator_context_and_add_workloads(
            block_info->symbol_table, block->context_changes, upcast(last_import_workload)
        );
    }

    // If any imports were found, wait for all imports to finish before analysing block
    if (last_import_workload != 0) {
        analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(last_import_workload));
        workload_executer_wait_for_dependency_resolution();
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
        log_semantic_error("Main function not defined", upcast(compiler.main_source->root), Parser::Section::END_TOKEN);
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
    if (!types_are_equal(upcast(main_symbol->options.function->signature), upcast(type_system.predefined_types.type_print_line))) {
        log_semantic_error("Main function does not have correct signature", main_symbol->definition_node);
        log_error_info_symbol(main_symbol);
        return;
    }
    semantic_analyser.program->main_function = main_symbol->options.function;
}

void function_progress_destroy(Function_Progress* progress) {
    if (progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
        polymorphic_base_info_destroy(&progress->polymorphic.function_base.base);
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
                Analysis_Info* info = *iter.value;
                AST_Info_Key* key = iter.key;
                if (key->base->type == AST::Node_Type::ARGUMENTS) {
                    parameter_matching_info_destroy(&info->parameter_matching_info);
                }

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
        symbol_table_install_new_operator_context_and_add_workloads(root_table, dynamic_array_create<AST::Context_Change*>(), nullptr);
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
            return result;
        };
        symbols.type_c_char = define_type_symbol("c_char", upcast(types.c_char_type));
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
        symbols.type_string = define_type_symbol("string", types.string);
        symbols.type_type = define_type_symbol("Type_Handle", types.type_handle);
        symbols.type_type_information = define_type_symbol("Type_Info", upcast(types.type_information_type));
        symbols.type_any = define_type_symbol("Any", upcast(types.any_type));
        symbols.type_empty = define_type_symbol("_", upcast(types.empty_struct_type));
        symbols.type_void_pointer = define_type_symbol("byte_pointer", upcast(types.byte_pointer));

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

        define_hardcoded_symbol("bitwise_not", Hardcoded_Type::BITWISE_NOT);
        define_hardcoded_symbol("bitwise_and", Hardcoded_Type::BITWISE_AND);
        define_hardcoded_symbol("bitwise_or", Hardcoded_Type::BITWISE_OR);
        define_hardcoded_symbol("bitwise_xor", Hardcoded_Type::BITWISE_XOR);
        define_hardcoded_symbol("bitwise_shift_left", Hardcoded_Type::BITWISE_SHIFT_LEFT);
        define_hardcoded_symbol("bitwise_shift_right", Hardcoded_Type::BITWISE_SHIFT_RIGHT);

        // NOTE: Error symbol is required so that unresolved symbol-reads can point to something,
        //       but it shouldn't be possible to reference the error symbol by base_name, so the 
        //       current little 'hack' is to use the identifier 0_ERROR and because it starts with a 0, it
        //       can never be used as a symbol read. Other approaches would be to have a custom symbol-table for the error symbol
        //       that isn't connected to anything.
        symbols.error_symbol = define_type_symbol("0_ERROR_SYMBOL", types.unknown_type);
        symbols.error_symbol->type = Symbol_Type::ERROR_SYMBOL;
    }
}

Semantic_Analyser* semantic_analyser_initialize()
{
    semantic_analyser.errors = dynamic_array_create<Semantic_Error>(64);
    semantic_analyser.comptime_value_allocator = stack_allocator_create_empty(2048);
    semantic_analyser.workload_executer = workload_executer_initialize();
    semantic_analyser.allocated_function_progresses = dynamic_array_create<Function_Progress*>(1);
    semantic_analyser.allocated_operator_contexts = dynamic_array_create<Operator_Context*>(1);
    semantic_analyser.progress_allocator = stack_allocator_create_empty(2048);
    semantic_analyser.global_variable_memory_pool = stack_allocator_create_empty(1024);
    semantic_analyser.program = 0;
    semantic_analyser.ast_to_pass_mapping = hashtable_create_pointer_empty<AST::Node*, Node_Passes>(1);
    semantic_analyser.symbol_lookup_visited = hashset_create_pointer_empty<Symbol_Table*>(1);
    semantic_analyser.allocated_passes = dynamic_array_create<Analysis_Pass*>(1);
    semantic_analyser.ast_to_info_mapping = hashtable_create_empty<AST_Info_Key, Analysis_Info*>(1, ast_info_key_hash, ast_info_equals);
    semantic_analyser.allocated_symbol_tables = dynamic_array_create<Symbol_Table*>(16);
    semantic_analyser.allocated_symbols = dynamic_array_create<Symbol*>(16);
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
            Analysis_Info* info = *iter.value;
            AST_Info_Key* key = iter.key;
            if (key->base->type == AST::Node_Type::ARGUMENTS) {
                parameter_matching_info_destroy(&info->parameter_matching_info);
            }
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
        dynamic_array_destroy(&context->context_imports);
        hashtable_destroy(&context->custom_operators);
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
void error_information_append_to_rich_text(const Error_Information& info, Rich_Text::Rich_Text* text, Datatype_Format format)
{
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
        Rich_Text::set_text_color(text, symbol_type_to_color(info.options.symbol->type));
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
        datatype_append_to_rich_text(info.options.type, text, format);
        break;
    case Error_Information_Type::EXPECTED_TYPE:
        Rich_Text::append_formated(text, "Expected Type: ");
        datatype_append_to_rich_text(info.options.type, text, format);
        break;
    case Error_Information_Type::FUNCTION_TYPE:
        Rich_Text::append_formated(text, "Function Type: ");
        datatype_append_to_rich_text(info.options.type, text, format);
        break;
    case Error_Information_Type::BINARY_OP_TYPES:
        Rich_Text::append_formated(text, "Left: ");
        datatype_append_to_rich_text(info.options.binary_op_types.left_type, text, format);
        Rich_Text::set_text_color(text);
        Rich_Text::append_formated(text, ", Right: ");
        datatype_append_to_rich_text(info.options.binary_op_types.right_type, text, format);
        break;
    case Error_Information_Type::EXPRESSION_RESULT_TYPE:
    {
        Rich_Text::append(text, "Given: ");
        switch (info.options.expression_type)
        {
        case Expression_Result_Type::NOTHING:
            Rich_Text::append(text, "Nothing/void");
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
        case Expression_Result_Type::TYPE:
            Rich_Text::append(text, "Type");
            break;
        case Expression_Result_Type::DOT_CALL:
            Rich_Text::append(text, "Dot_Call");
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

void error_information_append_to_string(const Error_Information& info, String* string, Datatype_Format format)
{
    Rich_Text::Rich_Text text = Rich_Text::create(vec3(1.0f));
    SCOPE_EXIT(Rich_Text::destroy(&text));
    error_information_append_to_rich_text(info, &text, format);
    Rich_Text::append_to_string(&text, string, 2);
}

void semantic_analyser_append_all_errors_to_string(String* string, int indentation)
{
    auto& errors = semantic_analyser.errors;
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
            error_information_append_to_string(info, string);
            string_append(string, "\t");
        }
    }
}


