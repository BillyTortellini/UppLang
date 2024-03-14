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
ModTree_Function* modtree_function_create_empty(Type_Function* signature, Symbol* symbol, Function_Progress* progress, Symbol_Table* param_table);
void analysis_workload_destroy(Workload_Base* workload);
Expression_Info* semantic_analyser_analyse_expression_any(AST::Expression* expression, Expression_Context context);
Type_Base* semantic_analyser_analyse_expression_value(AST::Expression* rc_expression, Expression_Context context);
Type_Base* semantic_analyser_analyse_expression_type(AST::Expression* rc_expression);
Type_Base* semantic_analyser_try_convert_value_to_type(AST::Expression* expression);
Control_Flow semantic_analyser_analyse_block(AST::Code_Block* code_block);

Expression_Post_Op expression_context_apply(
    Type_Base* initial_type, Expression_Context context,
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
void wait_for_type_size_to_finish(Type_Base* type, Dependency_Failure_Info failure_info = dependency_failure_info_make_none());


// HELPERS
namespace Helpers
{
    Analysis_Workload_Type get_workload_type(Workload_Import_Resolve* workload) { return Analysis_Workload_Type::IMPORT_RESOLVE; };
    Analysis_Workload_Type get_workload_type(Workload_Module_Analysis* workload) { return Analysis_Workload_Type::MODULE_ANALYSIS; };
    Analysis_Workload_Type get_workload_type(Workload_Definition* workload) { return Analysis_Workload_Type::DEFINITION; };
    Analysis_Workload_Type get_workload_type(Workload_Structure_Body* workload) { return Analysis_Workload_Type::STRUCT_BODY; };
    Analysis_Workload_Type get_workload_type(Workload_Structure_Polymorphic* workload) { return Analysis_Workload_Type::STRUCT_POLYMORPHIC; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Header* workload) { return Analysis_Workload_Type::FUNCTION_HEADER; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Parameter* workload) { return Analysis_Workload_Type::FUNCTION_PARAMETER; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Body* workload) { return Analysis_Workload_Type::FUNCTION_BODY; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Cluster_Compile* workload) { return Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE; };
    Analysis_Workload_Type get_workload_type(Workload_Bake_Analysis* workload) { return Analysis_Workload_Type::BAKE_ANALYSIS; };
    Analysis_Workload_Type get_workload_type(Workload_Bake_Execution* workload) { return Analysis_Workload_Type::BAKE_EXECUTION; };
    Analysis_Workload_Type get_workload_type(Workload_Event* workload) { return Analysis_Workload_Type::EVENT; };
};

Workload_Base* upcast(Workload_Base* workload) { return workload; }
Workload_Base* upcast(Workload_Import_Resolve* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Module_Analysis* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Function_Header* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Function_Parameter* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Function_Body* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Function_Cluster_Compile* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Structure_Body* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Structure_Polymorphic* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Definition* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Bake_Analysis* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Bake_Execution* workload) { return &workload->base; }
Workload_Base* upcast(Workload_Event* workload) { return &workload->base; }

template <typename T>
T* downcast(Workload_Base* workload) {
    T* result = (T*)workload;
    assert(workload->type == Helpers::get_workload_type(result), "Invalid cast");
    return result;
}



Type_Function* hardcoded_type_to_signature(Hardcoded_Type type)
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


// Errors
void semantic_analyser_set_error_flag(bool error_due_to_unknown)
{
    auto& analyser = semantic_analyser;
    if (analyser.current_workload != 0) {
        auto workload = analyser.current_workload;
        if (error_due_to_unknown) {
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

void log_error_info_invalid_member(Type_Struct* struct_signature, String* id) {
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

void log_error_info_given_type(Type_Base* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::GIVEN_TYPE);
    info.options.type = type;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_expected_type(Type_Base* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXPECTED_TYPE);
    info.options.type = type;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_missing_parameter(Function_Parameter parameter) {
    Error_Information info = error_information_make_empty(Error_Information_Type::MISSING_PARAMETER);
    info.options.parameter = parameter;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_function_type(Type_Function* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::FUNCTION_TYPE);
    info.options.function = type;
    dynamic_array_push_back(&semantic_analyser.errors[semantic_analyser.errors.size - 1].information, info);
}

void log_error_info_binary_op_type(Type_Base* left_type, Type_Base* right_type) {
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
    switch (semantic_analyser.current_workload->type) {
    case Analysis_Workload_Type::FUNCTION_BODY: return downcast<Workload_Function_Body>(semantic_analyser.current_workload)->progress; break;
    case Analysis_Workload_Type::FUNCTION_HEADER: return downcast<Workload_Function_Header>(semantic_analyser.current_workload)->progress; break;
    case Analysis_Workload_Type::FUNCTION_PARAMETER: return downcast<Workload_Function_Parameter>(semantic_analyser.current_workload)->progress; break;
    }
    return 0;
}

// Polymorphic
 // Returns null if we reached instanciation depth
// Polymorphic_Instance* polymorphic_base_make_instance_empty(Polymorphic_Base* base, AST::Node* instanciation_node) {
//     // Check instance depth
//     int current_instanciation_depth = 0;
//     Polymorphic_Instance* parent_instance = 0; // The initial instance that lead to the creation of this instance, may be 0
//     {
//         ModTree_Function* current_function = semantic_analyser.current_workload->current_function;
//         if (current_function != 0) {
//             parent_instance = current_function->progress->poly_instance;
//             if (parent_instance != 0) {
//                 current_instanciation_depth = parent_instance->instanciation_depth + 1;
//             }
//         }
// 
//         // Stop recursive instanciates at some threshold
//         if (current_instanciation_depth > 10) {
//             log_semantic_error("Polymorphic function instanciation reached depth limit 10!", instanciation_node, Parser::Section::FIRST_TOKEN);
//             // TODO: Add more error information, e.g. finding a cycle or printing the mother instances!
//             auto parent = parent_instance;
//             while (parent != 0) {
//                 parent->progress->function->contains_errors = true;
//                 parent = parent->parent_instance;
//             }
//             return 0;
//         }
//     }
// 
//     Polymorphic_Instance* instance = new Polymorphic_Instance;
//     instance->base = base;
//     instance->instanciation_depth = current_instanciation_depth;
//     instance->parent_instance = parent_instance;
//     instance->instance_index = -1;
//     instance->progress = 0;
//     instance->parameter_values = array_create_empty<Polymorphic_Value>(base->parameter_count);
//     for (int i = 0; i < instance->parameter_values.size; i++) {
//         instance->parameter_values[i].is_not_set = true;
//     }
//     return instance;
// }

// Returns base instance, which needs to be filled out with base values...
// Polymorphic_Base* polymorphic_base_create(Function_Progress* progress, int parameter_count)
// {
//     // Create base
//     Polymorphic_Base* base = new Polymorphic_Base;
//     dynamic_array_push_back(&semantic_analyser.polymorphic_functions, base);
//     base->instances = dynamic_array_create_empty<Polymorphic_Instance*>(1);
//     base->parameter_count = parameter_count;
// 
//     // Create first instance
//     auto instance = polymorphic_base_make_instance_empty(base, upcast(progress->header_workload->function_node));
//     for (int i = 0; i < instance->parameter_values.size; i++) {
//         instance->parameter_values[i].is_not_set = true;
//     }
//     instance->instance_index = 0;
//     instance->progress = progress;
//     instance->parent_instance = 0;
//     instance->instanciation_depth = 0;
//     dynamic_array_push_back(&base->instances, instance);
// 
//     base->base_instance = instance;
//     progress->poly_instance = instance;
//     progress->function->is_runnable = false; 
// 
//     return base;
// }



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
    workload->parent_table = semantic_analyser.root_symbol_table;
    workload->real_error_count = 0;
    workload->errors_due_to_unknown_count = 0;
    if (semantic_analyser.current_workload != 0 && semantic_analyser.current_workload->current_symbol_table != 0) {
        workload->parent_table = semantic_analyser.current_workload->current_symbol_table;
    }

    // Add to workload queue
    dynamic_array_push_back(&executer.all_workloads, workload);
    dynamic_array_push_back(&executer.runnable_workloads, workload); // Note: There exists a check for dependencies before executing runnable workloads, so this is ok

    return result;
}

// Note: If base progress != 0, then instance information (Progress-Type + instanciation depth) will not be set by this function
Function_Progress* function_progress_create_with_modtree_function(
    Symbol* symbol, AST::Expression* function_node, Type_Function* function_type, Function_Progress* base_progress = 0)
{
    assert(function_node->type == AST::Expression_Type::FUNCTION, "Has to be function!");

    Symbol_Table* parameter_table = 0;
    if (base_progress == 0) {
        parameter_table = symbol_table_create_with_parent(semantic_analyser.current_workload->current_symbol_table, false);
    }
    else {
        assert(base_progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE, "");
        parameter_table = base_progress->function->parameter_table;
    }

    // Create progress
    auto progress = new Function_Progress;
    dynamic_array_push_back(&semantic_analyser.allocated_function_progresses, progress);
    progress->type = Polymorphic_Analysis_Type::NORMAL;
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
        header_workload->parameter_order = dynamic_array_create_empty<Workload_Function_Parameter*>(1);
        header_workload->base.parent_table = progress->function->parameter_table;
    }
    else {
        progress->header_workload = base_progress->header_workload; // Note: Not sure if this is ever necessary, but we link to base progress header
    }

    progress->body_workload = workload_executer_allocate_workload<Workload_Function_Body>(upcast(function_node->options.function.body));
    progress->body_workload->body_node = function_node->options.function.body;
    progress->body_workload->progress = progress;
    progress->body_workload->base.parent_table = parameter_table; // Sets correct symbol table for code
    progress->function->code_workload = upcast(progress->body_workload);

    progress->compile_workload = workload_executer_allocate_workload<Workload_Function_Cluster_Compile>(0);
    progress->compile_workload->functions = dynamic_array_create_empty<ModTree_Function*>(1);
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

int get_current_polymorphic_instanciation_depth()
{
    auto workload = semantic_analyser.current_workload;
    if (workload == 0) return 0;
    if (workload->type == Analysis_Workload_Type::STRUCT_BODY) {
        auto struct_workload = downcast<Workload_Structure_Body>(semantic_analyser.current_workload);
        if (struct_workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
            return struct_workload->polymorphic_info.instance.instanciation_depth;
        }
        return 0;
    }
    
    auto current_function_progress = analysis_workload_try_get_function_progress(workload);
    if (current_function_progress != 0 &&
        current_function_progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
        return current_function_progress->polymorhic_instance.instanciation_depth;
    }

    return 0;
}

Workload_Structure_Body* workload_structure_create(AST::Expression* struct_node, Symbol* symbol, bool is_polymorphic_instance)
{
    assert(struct_node->type == AST::Expression_Type::STRUCTURE_TYPE, "Has to be struct!");
    auto& struct_info = struct_node->options.structure;

    // Create body workload
    auto body_workload = workload_executer_allocate_workload<Workload_Structure_Body>(upcast(struct_node));
    body_workload->arrays_depending_on_struct_size = dynamic_array_create_empty<Type_Array*>(1);
    body_workload->struct_type = type_system_make_struct_empty(struct_info.type, (symbol == 0 ? 0 : symbol->id), body_workload);
    body_workload->struct_node = struct_node;
    body_workload->polymorphic_type = Polymorphic_Analysis_Type::NORMAL;

    if (is_polymorphic_instance) {
        body_workload->polymorphic_type = Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
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
        poly_workload->body_workload = body_workload;
        poly_workload->instances = dynamic_array_create_empty<Structure_Polymorphic_Instance*>(1);
        poly_workload->parameters = array_create_empty<Struct_Parameter>(struct_info.parameters.size);
        poly_workload->symbol_table = semantic_analyser.current_workload->current_symbol_table;

        body_workload->polymorphic_type = Polymorphic_Analysis_Type::POLYMORPHIC_BASE;
        body_workload->polymorphic_info.polymorphic_base = poly_workload;
        analysis_workload_add_dependency_internal(upcast(body_workload), upcast(poly_workload));
    }

    return body_workload;
}




Bake_Progress* bake_progress_create(AST::Expression* bake_expr, Type_Base* expected_type)
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
        analysis->base.parent_table = parent_symbol_table;
        analysis->symbol_table = symbol_table_create_with_parent(parent_symbol_table, false);
        analysis->last_import_workload = 0;
        analysis->progress = progress;
        analysis->parent_analysis = 0;
        if (semantic_analyser.current_workload != 0 && semantic_analyser.current_workload->current_symbol_table != 0) {
            analysis->base.parent_table = semantic_analyser.current_workload->current_symbol_table;
        }
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
            current_table, definition->symbols[i]->name, initial_symbol_type, AST::upcast(definition->symbols[i]), is_local_variable
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
            return;
        }
        case AST::Expression_Type::STRUCTURE_TYPE: {
            // Note: Creating the progress also sets the symbol type
            auto workload = upcast(workload_structure_create(value, symbol, false));
            if (symbol_finish_workload != 0) {
                analysis_workload_add_dependency_internal(workload, symbol_finish_workload);
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

    symbol->type = Symbol_Type::DEFINITION_UNFINISHED;
    symbol->options.definition_workload = definition_workload;
}



// MOD_TREE (Note: Doesn't set symbol to anything!)
ModTree_Function* modtree_function_create_empty(Type_Function* signature, Symbol* symbol, Function_Progress* progress, Symbol_Table* param_table)
{
    ModTree_Function* function = new ModTree_Function;
    function->symbol = symbol;
    function->signature = signature;
    function->code_workload = 0;
    function->progress = progress;
    function->parameter_table = param_table;

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

ModTree_Global* modtree_program_add_global(Type_Base* type)
{
    wait_for_type_size_to_finish(type);

    auto global = new ModTree_Global;
    global->type = type;
    global->has_initial_value = false;
    global->init_expr = 0;
    global->index = semantic_analyser.program->globals.size;
    global->memory = stack_allocator_allocate_size(&semantic_analyser.global_variable_memory_pool, type->size, type->alignment);
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
    Type_Base* data_type;
};

Comptime_Result expression_calculate_comptime_value_internal(AST::Expression* expr);

Comptime_Result comptime_result_make_available(void* data, Type_Base* type) {
    Comptime_Result result;
    result.type = Comptime_Result_Type::AVAILABLE;
    result.data = data;
    result.data_type = type;
    result.message = "";
    return result;
}

Comptime_Result comptime_result_make_unavailable(Type_Base* type, const char* message) {
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

Comptime_Result comptime_result_apply_cast(Comptime_Result value, Info_Cast_Type cast_type, Type_Base* result_type)
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
    case Info_Cast_Type::INVALID: return comptime_result_make_unavailable(result_type, "Expression contains invalid cast"); // Invalid means an error was already logged
    case Info_Cast_Type::NO_CAST: return value;
    case Info_Cast_Type::FLOATS: instr_type = Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE; break;
    case Info_Cast_Type::FLOAT_TO_INT: instr_type = Instruction_Type::CAST_FLOAT_INTEGER; break;
    case Info_Cast_Type::INT_TO_FLOAT: instr_type = Instruction_Type::CAST_INTEGER_FLOAT; break;
    case Info_Cast_Type::INTEGERS: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
    case Info_Cast_Type::ENUM_TO_INT: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
    case Info_Cast_Type::INT_TO_ENUM: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
    case Info_Cast_Type::POINTERS: return comptime_result_make_not_comptime("Pointers aren't currently handled for comptime evaluation");
    case Info_Cast_Type::POINTER_TO_U64: return comptime_result_make_not_comptime("Pointers aren't currently handled for comptime evaluation");
    case Info_Cast_Type::U64_TO_POINTER: return comptime_result_make_not_comptime("Pointers aren't currently handled for comptime evaluation");
    case Info_Cast_Type::ARRAY_TO_SLICE: {
        Upp_Slice_Base* slice = stack_allocator_allocate<Upp_Slice_Base>(&analyser.comptime_value_allocator);
        slice->data = value.data;
        slice->size = downcast<Type_Array>(value.data_type)->element_count;
        return comptime_result_make_available(slice, result_type);
    }
    case Info_Cast_Type::TO_ANY: {
        Upp_Any* any = stack_allocator_allocate<Upp_Any>(&analyser.comptime_value_allocator);
        any->type = result_type->type_handle;
        any->data = value.data;
        return comptime_result_make_available(any, result_type);
    }
    case Info_Cast_Type::FROM_ANY: {
        Upp_Any* given = (Upp_Any*)value.data;
        if (given->type.index >= (u32)compiler.type_system.types.size) {
            return comptime_result_make_not_comptime("Any contained invalid type_id");
        }
        Type_Base* any_type = compiler.type_system.types[given->type.index];
        if (!types_are_equal(any_type, value.data_type)) {
            return comptime_result_make_not_comptime("Any type_handle value doesn't match result type, actually not sure if this can happen");
        }
        return comptime_result_make_available(given->data, any_type);
    }
    default: panic("");
    }
    if ((int)instr_type == -1) panic("");

    void* result_buffer = stack_allocator_allocate_size(&analyser.comptime_value_allocator, result_type->size, result_type->alignment);
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
        // TODO: Function pointer reads should work in the future
        return comptime_result_make_not_comptime("Function pointers for comptime values aren't supported currently");
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

    auto result_type = expression_info_get_type(info, true);
    wait_for_type_size_to_finish(result_type);
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
    case AST::Expression_Type::POLYMORPHIC_SYMBOL: {
        return comptime_result_make_not_comptime("Currently working on implicit polymorphism");
    }
    case AST::Expression_Type::PATH_LOOKUP:
    {
        auto symbol = get_info(expr->options.path_lookup)->symbol;
        if (symbol->type == Symbol_Type::COMPTIME_VALUE) {
            auto& upp_const = symbol->options.constant;
            return comptime_result_make_available(upp_const.memory, upp_const.type);
        }
        else if (symbol->type == Symbol_Type::PARAMETER)
        {
            auto& param = *(symbol->options.parameter);
            if (param.parameter_type == Parameter_Type::NORMAL) {
                return comptime_result_make_not_comptime("Function parameter is not polymorphic");
            }
            else {
                // Note: If we would be in a comptime instance, and the value would be available, the expression_result_type would have been constant,
                //       and we wouldn't be in this code-path (Look at analysis of path_lookup for comptime parameters)
                return comptime_result_make_unavailable(param.type, "Value not available in polymorphic base");
            }
        }
        else if (symbol->type == Symbol_Type::STRUCT_PARAMETER) {
            // Note: If we would be in a comptime instance, and the value would be available, the expression_result_type would have been constant,
            //       and we wouldn't be in this code-path (Look at analysis of path_lookup for comptime parameters)
            return comptime_result_make_unavailable(
                symbol->options.struct_parameter.workload->parameters[symbol->options.struct_parameter.parameter_index].parameter_type, 
                "Value not available in polymorphic base");
        }
        else if (symbol->type == Symbol_Type::ERROR_SYMBOL) {
            return comptime_result_make_unavailable(types.error_type, "Analysis contained errors");
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

        void* result_buffer = stack_allocator_allocate_size(&analyser.comptime_value_allocator, result_type->size, result_type->alignment);
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
        case AST::Unop::POINTER:
            return comptime_result_make_not_comptime("Address of not supported for comptime values");
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

        void* result_buffer = stack_allocator_allocate_size(&analyser.comptime_value_allocator, result_type->size, result_type->alignment);
        bytecode_execute_unary_instr(instr_type, type_base_to_bytecode_type(value.data_type), result_buffer, value.data);
        return comptime_result_make_available(result_buffer, result_type);
    }
    case AST::Expression_Type::CAST: {
        auto operand = expression_calculate_comptime_value_internal(expr->options.cast.operand);
        return comptime_result_apply_cast(operand, info->specifics.explicit_cast, result_type);
    }
    case AST::Expression_Type::ARRAY_ACCESS:
    {
        Type_Base* element_type = result_type;
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
        if (value_array.data_type->type == Type_Type::ARRAY) {
            base_ptr = (byte*)value_array.data;
            array_size = downcast<Type_Array>(value_array.data_type)->element_count;
        }
        else if (value_array.data_type->type == Type_Type::SLICE) {
            Upp_Slice_Base* slice = (Upp_Slice_Base*)value_array.data;
            base_ptr = (byte*)slice->data;
            array_size = slice->size;
        }
        else {
            panic("");
        }

        int index = *(int*)value_index.data;
        int element_offset = index * element_type->size;
        if (index >= array_size || index < 0) {
            return comptime_result_make_not_comptime("Array out of bounds access");
        }
        if (!memory_is_readable(base_ptr, element_type->size)) {
            return comptime_result_make_not_comptime("Slice/Array access to invalid memory");
        }
        return comptime_result_make_available(&base_ptr[element_offset], element_type);
    }
    case AST::Expression_Type::MEMBER_ACCESS: {
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
        return comptime_result_make_not_comptime("Array initializers not implemented yet for comptime values");
        // NOTE: Maybe this works in the future, but it dependes if we can always finish the struct size after analysis
        /*
        Type_Base* element_type = result_type->options.array.element_type;
        if (element_type->size == 0 && element_type->alignment == 0) {
            return comptime_analysis_make_error();
        }
        void* result_buffer = stack_allocator_allocate_size(&analyser->comptime_value_allocator, result_type->size, result_type->alignment);
        for (int i = 0; i < expr->options.array_initializer.size; i++)
        {
            ModTree_Expression* element_expr = expr->options.array_initializer[i];
            Comptime_Analysis value_element = expression_calculate_comptime_value_internal(analyser, element_expr);
            if (!value_element.available) {
                return value_element;
            }

            // Copy result into the array buffer
            {
                byte* raw = (byte*)result_buffer;
                int element_offset = element_type->size * i;
                memory_copy(&raw[element_offset], value_element.data, element_type->size);
            }
        }
        return comptime_analysis_make_success(result_buffer, result_type);
        */
    }
    case AST::Expression_Type::STRUCT_INITIALIZER:
    {
        // NOTE: Maybe this works in the future
        return comptime_result_make_not_comptime("Struct initializer not yet implemented for comptime values");
        /*
        Type_Base* struct_type = result_type->options.array.element_type;
        void* result_buffer = stack_allocator_allocate_size(&analyser->comptime_value_allocator, result_type->size, result_type->alignment);
        for (int i = 0; i < expr->options.struct_initializer.size; i++)
        {
            Member_Initializer* initializer = &expr->options.struct_initializer[i];
            Comptime_Analysis value_member = expression_calculate_comptime_value_internal(analyser, initializer->init_expr);
            if (!value_member.available) {
                return value_member;
            }
            // Copy member result into actual struct memory
            {
                byte* raw = (byte*)result_buffer;
                memory_copy(&raw[initializer->member.offset], value_member.data, initializer->member.type->size);
            }
        }
        return comptime_analysis_make_success(result_buffer, result_type);
        */
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
    if (info->post_op.deref_count != 0 || info->post_op.take_address_of) {
        // Cannot handle pointers for comptime currently
        return comptime_result_make_not_comptime("Pointer handling not supported by comptime values");
    }

    return comptime_result_apply_cast(result_no_context, info->post_op.implicit_cast, info->post_op.type_afterwards);
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

    auto bytes = array_create_static<byte>((byte*)comptime_result.data, comptime_result.data_type->size);
    Constant_Pool_Result result = constant_pool_add_constant(&compiler.constant_pool, comptime_result.data_type, bytes);
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
    auto type = info->post_op.type_afterwards;
    if (type->size == 0 && type->alignment != 0) return false; // I forgot if this case has any real use cases currently

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

    if (info->post_op.implicit_cast == Info_Cast_Type::FROM_ANY) {
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
    case AST::Expression_Type::CAST:
        // From Any is basically a pointer dereference
        return info->specifics.explicit_cast == Info_Cast_Type::FROM_ANY;
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

Expression_Context expression_context_make_no_value_expected() {
    Expression_Context context;
    context.type = Expression_Context_Type::NO_VALUE;
    return context;
}

Expression_Context expression_context_make_specific_type(Type_Base* signature) {
    if (type_is_unknown(signature)) {
        return expression_context_make_unknown(true);
    }
    Expression_Context context;
    context.type = Expression_Context_Type::SPECIFIC_TYPE_EXPECTED;
    context.signature = signature;
    return context;
}



// Result
void expression_info_set_value(Expression_Info* info, Type_Base* result_type)
{
    info->result_type = Expression_Result_Type::VALUE;
    info->options.value_type = result_type;
    info->post_op.type_afterwards = result_type;
}

void expression_info_set_error(Expression_Info* info, Type_Base* result_type)
{
    info->result_type = Expression_Result_Type::VALUE;
    info->options.value_type = result_type;
    info->post_op.type_afterwards = result_type;
    info->contains_errors = true;
}

void expression_info_set_function(Expression_Info* info, ModTree_Function* function)
{
    info->result_type = Expression_Result_Type::FUNCTION;
    info->options.function = function;
    info->post_op.type_afterwards = upcast(function->signature);
}

void expression_info_set_hardcoded(Expression_Info* info, Hardcoded_Type hardcoded)
{
    info->result_type = Expression_Result_Type::HARDCODED_FUNCTION;
    info->options.hardcoded = hardcoded;
    info->post_op.type_afterwards = upcast(hardcoded_type_to_signature(hardcoded));
}

void expression_info_set_type(Expression_Info* info, Type_Base* type)
{
    info->result_type = Expression_Result_Type::TYPE;
    info->options.type = type;
    info->post_op.type_afterwards = compiler.type_system.predefined_types.type_handle;

    if (type->type == Type_Type::STRUCT) {
        auto s = downcast<Type_Struct>(type);
        if (s->workload != 0) {
            if (s->workload->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
                info->result_type = Expression_Result_Type::POLYMORPHIC_STRUCT;
                info->options.polymorphic_struct = s->workload->polymorphic_info.polymorphic_base;
            }
        }
    }
}

void expression_info_set_no_value(Expression_Info* info) {
    info->result_type = Expression_Result_Type::NOTHING;
    info->options.type = compiler.type_system.predefined_types.error_type;
    info->post_op.type_afterwards = compiler.type_system.predefined_types.error_type;
}

void expression_info_set_constant(Expression_Info* info, Upp_Constant constant) {
    info->result_type = Expression_Result_Type::CONSTANT;
    info->options.constant = constant;
    info->post_op.type_afterwards = constant.type;
}

void expression_info_set_polymorphic_function(Expression_Info* info, Function_Progress* poly_base) {
    info->result_type = Expression_Result_Type::POLYMORPHIC_FUNCTION;
    info->options.polymorphic_function = poly_base;
    info->post_op.type_afterwards = upcast(poly_base->function->signature);
}

void expression_info_set_constant(Expression_Info* info, Type_Base* signature, Array<byte> bytes, AST::Node* error_report_node)
{
    auto& analyser = semantic_analyser;
    auto result = constant_pool_add_constant(&compiler.constant_pool, signature, bytes);
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

void expression_info_set_constant_enum(Expression_Info* info, Type_Base* enum_type, i32 value) {
    expression_info_set_constant(info, enum_type, array_create_static((byte*)&value, sizeof(i32)), 0);
}

void expression_info_set_constant_i32(Expression_Info* info, i32 value) {
    expression_info_set_constant(info, upcast(compiler.type_system.predefined_types.i32_type), array_create_static((byte*)&value, sizeof(i32)), 0);
}

// Returns result type of a value
Type_Base* expression_info_get_type(Expression_Info* info, bool before_context_is_applied)
{
    if (!before_context_is_applied) {
        return info->post_op.type_afterwards;
    }

    auto& types = compiler.type_system.predefined_types;
    switch (info->result_type)
    {
    case Expression_Result_Type::CONSTANT: info->options.constant.type;
    case Expression_Result_Type::VALUE: return info->options.value_type;
    case Expression_Result_Type::FUNCTION: return upcast(info->options.function->signature);
    case Expression_Result_Type::HARDCODED_FUNCTION: return upcast(hardcoded_type_to_signature(info->options.hardcoded));
    case Expression_Result_Type::TYPE: return types.type_handle;
    case Expression_Result_Type::NOTHING: return types.error_type;
    case Expression_Result_Type::POLYMORPHIC_FUNCTION: return upcast(info->options.polymorphic_function->function->signature);
    case Expression_Result_Type::POLYMORPHIC_STRUCT: return upcast(info->options.polymorphic_struct->body_workload->struct_type);
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
    case Analysis_Workload_Type::STRUCT_BODY: {
        auto str = downcast<Workload_Structure_Body>(workload);
        dynamic_array_destroy(&str->arrays_depending_on_struct_size);
        if (str->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
            array_destroy(&str->polymorphic_info.instance.parameter_values);
        }
        break;
    }
    case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE: {
        auto cluster = downcast<Workload_Function_Cluster_Compile>(workload);
        dynamic_array_destroy(&cluster->functions);
        break;
    }
    case Analysis_Workload_Type::FUNCTION_HEADER: {
        dynamic_array_destroy(&downcast<Workload_Function_Header>(workload)->parameter_order);
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
void symbol_lookup_resolve(AST::Symbol_Lookup* lookup, Symbol_Table* symbol_table, bool search_parents, bool internals_ok, Dynamic_Array<Symbol*>& results)
{
    // Find all symbols with this id
    symbol_table_query_id(symbol_table, lookup->name, search_parents, internals_ok, &results);

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
Symbol* symbol_lookup_resolve_to_single_symbol(AST::Symbol_Lookup* lookup, Symbol_Table* symbol_table, bool search_parents, bool internals_ok)
{
    auto info = pass_get_node_info(semantic_analyser.current_workload->current_pass, lookup, Info_Query::CREATE_IF_NULL);
    auto error = semantic_analyser.predefined_symbols.error_symbol;

    // Find all overloads
    auto results = dynamic_array_create_empty<Symbol*>(1);
    SCOPE_EXIT(dynamic_array_destroy(&results));
    symbol_lookup_resolve(lookup, symbol_table, search_parents, internals_ok, results);

    // Handle result array
    if (results.size == 0) {
        log_semantic_error("Could not resolve Symbol (No definition found)", upcast(lookup));
        info->symbol = error;
    }
    else if (results.size == 1) {
        info->symbol = results[0];
        dynamic_array_push_back(&info->symbol->references, lookup);
    }
    else { // size > 1
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
        Symbol* symbol = symbol_lookup_resolve_to_single_symbol(part, table, i == 0, false);
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

// Note: After resolving overloaded symbols, the caller should 1. the symbol_info for the symbol_read, and 2. add a reference to the symbol
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

    // Resolve symbol
    symbol_lookup_resolve(path->last, symbol_table, path->parts.size == 1, path->parts.size == 1, symbols);
    get_info(path, true)->symbol = error;
    return true;
}

Symbol* path_lookup_resolve_to_single_symbol(AST::Path_Lookup* path)
{
    auto& analyser = semantic_analyser;
    auto error = semantic_analyser.predefined_symbols.error_symbol;

    // Resolve path
    auto symbol_table = path_lookup_resolve_only_path_parts(path);
    if (symbol_table == 0) {
        return error;
    }

    // Resolve symbol
    Symbol* symbol = symbol_lookup_resolve_to_single_symbol(path->last, symbol_table, path->parts.size == 1, path->parts.size == 1);
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
            case Analysis_Workload_Type::EVENT: str = "Event           "; break;

            case Analysis_Workload_Type::FUNCTION_HEADER: str = "Header          "; break;
            case Analysis_Workload_Type::FUNCTION_PARAMETER: str = "Parameter       "; break;
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
    case Analysis_Workload_Type::FUNCTION_PARAMETER: {
        auto param = downcast<Workload_Function_Parameter>(workload);
        Symbol* function_symbol = param->progress->function->symbol;
        const char* fn_id = function_symbol == 0 ? "Lambda" : function_symbol->id->characters;
        string_append_formated(string, "Paramter: %s of %s", param->node->name->characters, fn_id);
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

void wait_for_type_size_to_finish(Type_Base* type, Dependency_Failure_Info failure_info)
{
    if (!type_size_is_unfinished(type)) {
        return;
    }
    auto waiting_for_type = type;
    while (waiting_for_type->type == Type_Type::ARRAY) {
        waiting_for_type = downcast<Type_Array>(waiting_for_type)->element_type;
    }
    assert(waiting_for_type->type == Type_Type::STRUCT, "");
    auto structure = downcast<Type_Struct>(waiting_for_type);

    analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(structure->workload), failure_info);
    workload_executer_wait_for_dependency_resolution();
}


// WORKLOAD EXECUTION AND OTHERS
Workload_Import_Resolve* create_import_workload(AST::Import* import_node)
{
    // Check for Syntax-Errors
    if (import_node->type != AST::Import_Type::FILE) {
        if (import_node->path->parts.size == 1 && import_node->alias_name == 0 && import_node->type == AST::Import_Type::SINGLE_SYMBOL) {
            log_semantic_error("Cannot import single symbol, or have an alias with same name!", upcast(import_node->path));
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
    auto workload = semantic_analyser.current_workload;
    if (import_node->type == AST::Import_Type::SINGLE_SYMBOL) {
        auto name = import_node->path->last->name;
        if (import_node->alias_name != 0) {
            name = import_node->alias_name;
        }
        import_workload->symbol = symbol_table_define_symbol(
            workload->current_symbol_table, name, Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL, upcast(import_node), false
        );
        import_workload->symbol->options.alias_workload = import_workload;
    }
    else if (import_node->type == AST::Import_Type::FILE && import_node->alias_name != 0) {
        import_workload->symbol = symbol_table_define_symbol(
            workload->current_symbol_table, import_node->alias_name, Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL, upcast(import_node), false
        );
        import_workload->symbol->options.alias_workload = import_workload;
    }
    return import_workload;
}

void search_for_implicit_polymorphic_parameters(AST::Expression* expression, Dynamic_Array<AST::Expression*>& append_to)
{
    if (expression->type == AST::Expression_Type::POLYMORPHIC_SYMBOL) {
        dynamic_array_push_back(&append_to, expression);
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
            search_for_implicit_polymorphic_parameters(downcast<AST::Expression>(child_node), append_to);
        }
        child_index += 1;
        child_node = AST::base_get_child(upcast(expression), child_index);
    }
}

void analyse_parameter_type_and_value(Function_Parameter& parameter, AST::Parameter* parameter_node)
{
    parameter.name = optional_make_success(parameter_node->name);
    if (parameter_node->is_comptime) {
        // Polymorphic index must have been set before
        assert(parameter.parameter_type == Parameter_Type::POLYMORPHIC, "");
    }
    else {
        parameter.parameter_type = Parameter_Type::NORMAL;
        parameter.normal.index_in_non_polymorphic_signature = -1; // Is set later
    }

    // Analyse parameter
    parameter.type = semantic_analyser_analyse_expression_type(parameter_node->type);

    // Analyse default value
    if (!parameter_node->default_value.available) {
        parameter.default_value.available = false;
        parameter.has_default_value = false;
        return;
    }
    parameter.has_default_value = true;
    auto default_value_type = semantic_analyser_analyse_expression_value(
        parameter_node->default_value.value, expression_context_make_specific_type(parameter.type));
    if (type_is_unknown(parameter.type) || default_value_type != parameter.type) {
        return;
    }

    // Default value must be comptime known and serializable
    parameter.default_value = expression_calculate_comptime_value(parameter_node->default_value.value, "Default values must be comptime");
    assert(parameter.default_value.value.type == default_value_type && default_value_type == parameter.type, "I think types must all match at this point");
    return;
}

void analysis_workload_entry(void* userdata)
{
    Workload_Base* workload = (Workload_Base*)userdata;
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    analyser.current_workload = workload;

    workload->current_function = 0;
    workload->current_expression = 0;
    workload->statement_reachable = true;
    workload->current_symbol_table = workload->parent_table;
    workload->polymorphic_values.data = 0;
    workload->polymorphic_values.size = 0;
    workload->implicit_polymorphic_types.data = 0;
    workload->implicit_polymorphic_types.size = 0;

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
                    import_workload->base.current_symbol_table, module_progress->module_analysis->symbol_table, false, false, upcast(node)
                );
            }
            else {
                import_workload->symbol->type = Symbol_Type::MODULE;
                import_workload->symbol->options.module_progress = module_progress;
            }
            break;
        }
        else if (node->type == AST::Import_Type::SINGLE_SYMBOL) {
            import_workload->alias_for_symbol = path_lookup_resolve_to_single_symbol(node->path);
        }
        else { // Import * or **
            Symbol* symbol = path_lookup_resolve_to_single_symbol(node->path);
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
                        false,
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
            Type_Base* type = 0;
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
        workload->current_function = function;

        Dynamic_Array<Function_Parameter> parameters = dynamic_array_create_empty<Function_Parameter>(parameter_nodes.size); // Note: Ownership is later transfered to type
        parameters.size = parameter_nodes.size;
        Dynamic_Array<AST::Expression*> implicit_parameter_nodes = dynamic_array_create_empty<AST::Expression*>(1);
        SCOPE_EXIT(dynamic_array_destroy(&implicit_parameter_nodes));
        Dynamic_Array<Type_Polymorphic*> implicit_parameters = dynamic_array_create_empty<Type_Polymorphic*>(1);
        SCOPE_EXIT(if (progress->type != Polymorphic_Analysis_Type::POLYMORPHIC_BASE) { dynamic_array_destroy(&implicit_parameters); });
        Dynamic_Array<int> polymorphic_parameter_indices = dynamic_array_create_empty<int>(1);
        SCOPE_EXIT(if (progress->type != Polymorphic_Analysis_Type::POLYMORPHIC_BASE) { dynamic_array_destroy(&polymorphic_parameter_indices); });

        // Analyse parameters (Create Symbols and Workloads and initialize parameter infos)
        for (int i = 0; i < parameter_nodes.size; i++)
        {
            auto param_node = parameter_nodes[i];
            auto& parameter = parameters[i];

            // Create symbol
            Symbol* symbol = symbol_table_define_symbol(
                header_workload->base.current_symbol_table, param_node->name, Symbol_Type::PARAMETER, AST::upcast(param_node), true
            );

            // Initialize parameter info
            parameter = function_parameter_make_empty(symbol, 0);
            parameter.index = i;
            if (param_node->is_comptime) {
                progress->type = Polymorphic_Analysis_Type::POLYMORPHIC_BASE;
                parameter.parameter_type = Parameter_Type::POLYMORPHIC;
                parameter.polymorphic_index = polymorphic_parameter_indices.size;
                dynamic_array_push_back(&polymorphic_parameter_indices, i);
            }

            // Create workload
            auto param_workload = workload_executer_allocate_workload<Workload_Function_Parameter>(upcast(param_node));
            parameter.workload = param_workload;
            param_workload->progress = progress;
            param_workload->node = param_node;
            param_workload->base.parent_table = header_workload->base.current_symbol_table;
            param_workload->parameter_index = i;
            analysis_workload_add_dependency_internal(upcast(header_workload), upcast(param_workload));

            // Set analysis info
            auto param_info = get_info(param_node, true);
            param_info->symbol = symbol;

            // Add implicit parameters
            dynamic_array_reset(&implicit_parameter_nodes);
            search_for_implicit_polymorphic_parameters(param_node->type, implicit_parameter_nodes);
            for (int j = 0; j < implicit_parameter_nodes.size; j++)
            {
                auto implicit_node = implicit_parameter_nodes[j];
                assert(implicit_node->type == AST::Expression_Type::POLYMORPHIC_SYMBOL, "");
                progress->type = Polymorphic_Analysis_Type::POLYMORPHIC_BASE;

                // Create symbol
                Symbol* symbol = symbol_table_define_symbol(
                    header_workload->base.current_symbol_table, implicit_node->options.polymorphic_symbol_id,
                    Symbol_Type::IMPLICIT_PARAMETER, AST::upcast(implicit_node), true
                );
                // Create type
                symbol->options.implicit = type_system_make_polymorphic(symbol, param_workload, progress, implicit_parameters.size);
                hashtable_insert_element(&semantic_analyser.implicit_parameter_node_mapping, implicit_node, symbol->options.implicit);
                // Store in implicit parameter array
                dynamic_array_push_back(&implicit_parameters, symbol->options.implicit);
            }
        }

        // Update Symbol to parameter mapping (Pointer could have changed when adding parameters)
        for (int i = 0; i < parameters.size; i++) {
            auto& param = parameters[i];
            if (param.workload != 0) {
                param.workload->parameter = &param;
            }
            if (param.symbol != 0) {
                param.symbol->options.parameter = &param;
                // if (param.parameter_type == Parameter_Type::IMPLICIT) {
                //     get_info(downcast<AST::Expression>(param.symbol->definition_node), true)->specifics.implicit_parameter = &param;
                // }
            }
        }

        // Switch progress to polymorphic_function if polymorphic_function parameters were encountered
        if (progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
            progress->polymorphic_base.comptime_argument_evaluation_order = dynamic_array_create_empty<int>(1);
            progress->polymorphic_base.instances = dynamic_array_create_empty<Function_Progress*>(1);
            progress->polymorphic_base.implicit_parameters = implicit_parameters;
            progress->polymorphic_base.polymorphic_parameter_indices = polymorphic_parameter_indices;
            function->is_runnable = false; // Polymorphic base cannot be runnable
            if (function->symbol != 0) {
                function->symbol->type = Symbol_Type::POLYMORPHIC_FUNCTION;
                function->symbol->options.polymorphic_function = progress;
            }
        }

        // Wait for all parameter workloads to finish
        workload_executer_wait_for_dependency_resolution();

        // Create type-signature 
        {
            Type_Base* return_type = 0;
            if (signature_node.return_value.available) {
                return_type = semantic_analyser_analyse_expression_type(signature_node.return_value.value);
            }
            function->signature = type_system_make_function(parameters, return_type);
        }
        break;
    }
    case Analysis_Workload_Type::FUNCTION_PARAMETER:
    {
        auto param_workload = downcast<Workload_Function_Parameter>(workload);
        workload->current_function = param_workload->progress->function;
        analyse_parameter_type_and_value(*param_workload->parameter, param_workload->node);
        // Set index in evaluation order if comptime
        if (param_workload->parameter->parameter_type == Parameter_Type::POLYMORPHIC || param_workload->parameter->type->contains_polymorphic_type)
        {
            assert(param_workload->progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE, "");
            auto& evaluation_order = param_workload->progress->polymorphic_base.comptime_argument_evaluation_order;
            dynamic_array_push_back(&evaluation_order, param_workload->parameter_index);
        }
        break;
    }
    case Analysis_Workload_Type::FUNCTION_BODY:
    {
        auto body_workload = downcast<Workload_Function_Body>(workload);
        auto code_block = body_workload->body_node;
        auto function = body_workload->progress->function;
        workload->current_function = function;

        // Check if we are polymorphic_function instance 
        if (body_workload->progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE)
        {
            auto& instance = body_workload->progress->polymorhic_instance;
            assert(instance.polymorphic_base->body_workload->base.is_finished, "There must be a wait before this to guarantee this");
            // If base function contains errors, we don't need to analyse the body
            if (instance.polymorphic_base->function->contains_errors) {
                function->contains_errors = true;
                function->is_runnable = false;
                break;
            }

            workload->polymorphic_values = instance.parameter_values;
            workload->implicit_polymorphic_types = instance.implicit_parameter_values;
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
        Symbol_Table* parameter_table = symbol_table_create_with_parent(analyser.current_workload->current_symbol_table, false);
        for (int i = 0; i < struct_node.parameters.size; i++) 
        {
            auto& param_node = struct_node.parameters[i];
            auto& struct_param = workload_poly->parameters[i];

            // Create symbol
            Symbol* symbol = symbol_table_define_symbol(
                parameter_table, param_node->name, Symbol_Type::STRUCT_PARAMETER, AST::upcast(param_node), true
            );
            symbol->options.struct_parameter.workload = workload_poly;
            symbol->options.struct_parameter.parameter_index = i;

            // Analyse base type
            struct_param.symbol = symbol;
            struct_param.parameter_type = semantic_analyser_analyse_expression_type(param_node->type);

            // Analyes default value
            struct_param.has_default_value = param_node->default_value.available;
            struct_param.default_value.available = false;
            if (param_node->default_value.available && !type_is_unknown(struct_param.parameter_type)) {
                semantic_analyser_analyse_expression_value(
                    param_node->default_value.value, expression_context_make_specific_type(struct_param.parameter_type));
                struct_param.default_value = expression_calculate_comptime_value(param_node->default_value.value, "Default values must be comptime");
            }
        }

        // Store/Set correct symbol table for base-analysis and instance analysis
        workload_poly->symbol_table = parameter_table;
        workload_poly->body_workload->base.current_symbol_table = parameter_table;
        break;
    }
    case Analysis_Workload_Type::STRUCT_BODY:
    {
        auto workload_structure = downcast<Workload_Structure_Body>(workload);

        auto& struct_node = workload_structure->struct_node->options.structure;
        Type_Struct* struct_signature = workload_structure->struct_type;
        auto& members = struct_signature->members;

        // Check if we are a instance analysis
        if (workload_structure->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
            auto& instance = workload_structure->polymorphic_info.instance;
            assert(instance.parent->base.is_finished, "At this point the base has to be finished");

            // If an error occured in the polymorphic base, set all struct members to error-type
            if (instance.parent->base.real_error_count != 0) {
                for (int i = 0; i < struct_node.members.size; i++) {
                    auto member_node = struct_node.members[i];
                    struct_add_member(struct_signature, member_node->symbols[0]->name, types.error_type);
                }
                type_system_finish_struct(struct_signature);
                break;
            }
            semantic_analyser.current_workload->current_symbol_table = instance.parent->symbol_table;
            semantic_analyser.current_workload->polymorphic_values = instance.parameter_values;
        }
        else if (workload_structure->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE) {
            semantic_analyser.current_workload->current_symbol_table = workload_structure->polymorphic_info.polymorphic_base->symbol_table;
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
            assert(!(member.type->size == 0 && member.type->alignment == 0), "Must not happen with current Dependency-System");

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

        // Add size dependencies to other structs
        for (int i = 0; i < members.size; i++)
        {
            auto& member = members[i];
            bool has_failed = false;
            wait_for_type_size_to_finish(member.type, dependency_failure_info_make(&has_failed, 0));

            if (has_failed) {
                member.type = type_system.predefined_types.error_type;
                log_semantic_error("Struct contains itself, this can only work with references", upcast(struct_node.members[i]), Parser::Section::IDENTIFIER);
            }
        }

        type_system_finish_struct(struct_signature);

        // Finish all arrays waiting on this struct 
        for (int i = 0; i < workload_structure->arrays_depending_on_struct_size.size; i++) {
            Type_Array* array = workload_structure->arrays_depending_on_struct_size[i];
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
            auto flow = semantic_analyser_analyse_block(code_block);
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
                wait_for_type_size_to_finish(thread->waiting_for_type_finish_type, dependency_failure_info_make(&cyclic_dependency_occured));
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

        // Add resutl to constant pool
        void* value_ptr = thread->return_register;
        assert(progress->bake_function->signature->return_type.available, "Bake return type must have been stated at this point...");
        auto result_type = progress->bake_function->signature->return_type.value;
        Constant_Pool_Result pool_result = constant_pool_add_constant(
            &compiler.constant_pool, result_type, array_create_static<byte>((byte*)value_ptr, result_type->size));
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
        Hashtable<C_Import_Type*, Type_Base*> type_conversion_table = hashtable_create_pointer_empty<C_Import_Type*, Type_Base*>(256);
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
                Type_Base* type = import_c_type(analyser, import_symbol->data_type, &type_conversion_table);
                if (type->type == Type_Type::STRUCT) {
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
                assert(extern_fn->signature->type == Type_Type::FUNCTION, "HEY");

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
                Type_Base** signature = hashtable_find_element(&type_conversion_table, import_sym->data_type);
                if (signature != 0)
                {
                    Type_Base* type = *signature;
                    if (type->type == Type_Type::STRUCT) {
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
        if (result.options.type->type != Type_Type::FUNCTION) {
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
        assert(extern_fn->signature->type == Type_Type::FUNCTION, "HEY");
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

Info_Cast_Type semantic_analyser_check_cast_type(Type_Base* source_type, Type_Base* destination_type, bool implicit_cast)
{
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    if (type_is_unknown(source_type) || type_is_unknown(destination_type)) {
        semantic_analyser_set_error_flag(true);
        return Info_Cast_Type::NO_CAST;
    }
    if (types_are_equal(source_type, destination_type)) return Info_Cast_Type::NO_CAST;
    bool cast_valid = false;
    Info_Cast_Type cast_type = Info_Cast_Type::INVALID;

    // Void pointer casting
    if (source_type->type == Type_Type::VOID_POINTER) {
        if (destination_type->type == Type_Type::POINTER) {
            return Info_Cast_Type::POINTERS;
        }
        return Info_Cast_Type::INVALID;
    }
    else if (destination_type->type == Type_Type::VOID_POINTER) {
        if (source_type->type == Type_Type::POINTER) {
            return Info_Cast_Type::POINTERS;
        }
        return Info_Cast_Type::INVALID;
    }

    // Pointer casting
    if (source_type->type == Type_Type::POINTER || destination_type->type == Type_Type::POINTER)
    {
        if (source_type->type == Type_Type::POINTER && destination_type->type == Type_Type::POINTER)
        {
            cast_type = Info_Cast_Type::POINTERS;
            cast_valid = !implicit_cast;
        }
    }
    else if (source_type->type == Type_Type::FUNCTION && destination_type->type == Type_Type::FUNCTION) {
        // Check if types are the same
        auto src_fn = downcast<Type_Function>(source_type);
        auto dst_fn = downcast<Type_Function>(destination_type);
        cast_valid = true;
        cast_type = Info_Cast_Type::POINTERS;
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
    }
    // Primitive Casting:
    else if (source_type->type == Type_Type::PRIMITIVE && destination_type->type == Type_Type::PRIMITIVE)
    {
        auto src_primitive = downcast<Type_Primitive>(source_type);
        auto dst_primitive = downcast<Type_Primitive>(destination_type);
        if (src_primitive->primitive_type == Primitive_Type::INTEGER && dst_primitive->primitive_type == Primitive_Type::INTEGER)
        {
            cast_type = Info_Cast_Type::INTEGERS;
            cast_valid = true;
            if (implicit_cast) {
                if (src_primitive->is_signed == dst_primitive->is_signed) {
                    cast_valid = source_type->size < destination_type->size;
                }
                else {
                    if (src_primitive->is_signed) {
                        cast_valid = false;
                    }
                    else {
                        cast_valid = destination_type->size > source_type->size; // E.g. u8 to i32, u32 to i64
                    }
                }
            }
        }
        else if (dst_primitive->primitive_type == Primitive_Type::FLOAT && src_primitive->primitive_type == Primitive_Type::INTEGER)
        {
            cast_type = Info_Cast_Type::INT_TO_FLOAT;
            cast_valid = true;
        }
        else if (dst_primitive->primitive_type == Primitive_Type::INTEGER && src_primitive->primitive_type == Primitive_Type::FLOAT)
        {
            cast_type = Info_Cast_Type::FLOAT_TO_INT;
            cast_valid = !implicit_cast;
        }
        else if (dst_primitive->primitive_type == Primitive_Type::FLOAT && src_primitive->primitive_type == Primitive_Type::FLOAT)
        {
            cast_type = Info_Cast_Type::FLOATS;
            cast_valid = true;
            if (implicit_cast) {
                cast_valid = source_type->size < destination_type->size;
            }
        }
        else { // Booleans can never be cast
            cast_valid = false;
        }
    }
    // Array casting
    else if (source_type->type == Type_Type::ARRAY && destination_type->type == Type_Type::SLICE)
    {
        auto src_array = downcast<Type_Array>(source_type);
        auto dst_slice = downcast<Type_Slice>(destination_type);
        if (types_are_equal(src_array->element_type, dst_slice->element_type)) {
            cast_type = Info_Cast_Type::ARRAY_TO_SLICE;
            cast_valid = true;
        }
    }
    // Enum casting
    else if (source_type->type == Type_Type::ENUM || destination_type->type == Type_Type::ENUM)
    {
        bool enum_is_source = false;
        Type_Base* other = 0;
        if (source_type->type == Type_Type::ENUM) {
            enum_is_source = true;
            other = destination_type;
        }
        else {
            enum_is_source = false;
            other = source_type;
        }

        if (other->type == Type_Type::PRIMITIVE && downcast<Type_Primitive>(other)->primitive_type == Primitive_Type::INTEGER) {
            cast_type = enum_is_source ? Info_Cast_Type::ENUM_TO_INT : Info_Cast_Type::INT_TO_ENUM;
            cast_valid = !implicit_cast;
        }
    }
    // Any casting
    else if (types_are_equal(destination_type, upcast(types.any_type)))
    {
        cast_valid = true;
        cast_type = Info_Cast_Type::TO_ANY;
    }
    else if (types_are_equal(source_type, upcast(types.any_type))) {
        cast_valid = !implicit_cast;
        cast_type = Info_Cast_Type::FROM_ANY;
    }

    if (cast_valid) return cast_type;
    return Info_Cast_Type::INVALID;
}

void expression_info_set_cast(Expression_Info* info, Info_Cast_Type cast_type, Type_Base* result_type)
{
    info->post_op.type_afterwards = result_type;
    info->post_op.implicit_cast = cast_type;
}

Expression_Info expression_info_make_empty(Expression_Context context)
{
    auto error_type = compiler.type_system.predefined_types.error_type;

    Expression_Info info;
    info.contains_errors = false;
    info.result_type = Expression_Result_Type::VALUE;
    info.options.value_type = error_type;
    info.context = context;
    info.post_op.type_afterwards = error_type;
    info.post_op.implicit_cast = Info_Cast_Type::NO_CAST;
    info.post_op.deref_count = 0;
    info.post_op.take_address_of = false;
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
        analysis_workload_add_dependency_internal(workload, upcast(symbol->options.polymorphic_function->header_workload), failure_info);
        break;
    }
    case Symbol_Type::IMPLICIT_PARAMETER: {
        // If I'm _not_ currently on the same workload, wait for parameter workload so that parameter order is generated correctly
        auto other = symbol->options.implicit->parameter_workload;
        if (upcast(other) != workload) {
            analysis_workload_add_dependency_internal(workload, upcast(other), failure_info);
        }
        break;
    }
    case Symbol_Type::PARAMETER:
    {
        if (symbol->options.parameter->workload != 0) {
            analysis_workload_add_dependency_internal(workload, upcast(symbol->options.parameter->workload), failure_info);
        }
        break;
    }
    case Symbol_Type::TYPE:
    {
        Type_Base* type = symbol->options.type;
        if (type->type == Type_Type::STRUCT)
        {
            auto struct_workload = downcast<Type_Struct>(type)->workload;
            if (struct_workload == 0) {
                assert(!type_size_is_unfinished(type), "");
                break;
            }

            switch (workload->type)
            {
            case Analysis_Workload_Type::DEFINITION: {
                analysis_workload_add_dependency_internal(workload, upcast(struct_workload), failure_info);
                break;
            }
            case Analysis_Workload_Type::BAKE_ANALYSIS: {
                analysis_workload_add_dependency_internal(
                    upcast(downcast<Workload_Bake_Analysis>(workload)->progress->execute_workload), 
                    upcast(struct_workload), 
                    failure_info
                );
                break;
            }
            case Analysis_Workload_Type::FUNCTION_BODY:
            case Analysis_Workload_Type::FUNCTION_HEADER:
            case Analysis_Workload_Type::FUNCTION_PARAMETER: {
                auto progress = analysis_workload_try_get_function_progress(workload);
                analysis_workload_add_dependency_internal(upcast(progress->compile_workload), upcast(struct_workload));
                break;
            }
            case Analysis_Workload_Type::STRUCT_BODY: break;
            default: panic("Invalid code path");
            }
        }
        break;
    }
    case Symbol_Type::STRUCT_PARAMETER: {
        // If i can access this symbol this means the analysis must have finished by now
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
    case Symbol_Type::IMPLICIT_PARAMETER:
    {
        auto poly = symbol->options.implicit;
        if (!poly->is_reference) {
            poly = poly->mirrored_type;
        }
        expression_info_set_type(&result, upcast(poly));
        // Check if instanciated values are available
        if (workload->implicit_polymorphic_types.data != 0) {
            auto instance_type = workload->implicit_polymorphic_types[symbol->options.implicit->index];
            if (instance_type != 0) {
                expression_info_set_type(&result, instance_type);
            }
        }
        break;
    }
    case Symbol_Type::PARAMETER:
    {
        auto& param = *(symbol->options.parameter);
        auto workload = semantic_analyser.current_workload;
        if (param.parameter_type == Parameter_Type::NORMAL) 
        {
            if (workload->type == Analysis_Workload_Type::FUNCTION_PARAMETER || workload->type == Analysis_Workload_Type::FUNCTION_HEADER) {
                log_semantic_error("Accessing parameter_nodes in function header is only valid for comptime parameter_nodes", upcast(error_report_node));
                expression_info_set_error(&result, param.type);
                return result;
            }
            auto progress = analysis_workload_try_get_function_progress(workload);
            assert(progress != 0, "Should be true after previous check");

            if (progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
                // Note: Here we cannot use param.type, since this would refer to the base type of a non-polymorphic_function parameter.
                //       Think about foo :: ($T: Type, a: *T), which would be pointer to error type as base
                expression_info_set_value(&result, progress->function->signature->parameters[param.normal.index_in_non_polymorphic_signature].type);
                return result;
            }
            else {
                expression_info_set_value(&result, param.type);
                return result;
            }
        }
        else 
        {
            if (workload->polymorphic_values.data == 0) {
                expression_info_set_value(&result, param.type);
                return result;
            }
            else {
                // Note: This may be the case during either Instanciation (Normal function) or inside the instance function analysis
                expression_info_set_constant(&result, workload->polymorphic_values[param.polymorphic_index]);
                return result;
            }
        }

        panic("Invalid");
        return result;
    }
    case Symbol_Type::STRUCT_PARAMETER: {
        auto& parameter = symbol->options.struct_parameter;
        assert(workload->type == Analysis_Workload_Type::STRUCT_BODY, "Struct parameters should only be accessible in struct body");
        auto current_struct_workload = downcast<Workload_Structure_Body>(workload);
        assert(current_struct_workload->polymorphic_type != Polymorphic_Analysis_Type::NORMAL, "");

        switch (current_struct_workload->polymorphic_type)
        {
        case Polymorphic_Analysis_Type::POLYMORPHIC_BASE: {
            assert(current_struct_workload->polymorphic_info.polymorphic_base == parameter.workload, "I think this should be the case for now...");
            expression_info_set_value(&result, parameter.workload->parameters[parameter.parameter_index].parameter_type);
            return result;
        }
        case Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE: {
            expression_info_set_constant(&result, current_struct_workload->polymorphic_info.instance.parameter_values[parameter.parameter_index]);
            return result;
        }
        default: panic("Normal shouldn't happen here!");
        }
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
    POLYMORPHIC_FUNCTION,
    HARDCODED,
    FUNCTION_POINTER,
    POLYMORPHIC_STRUCT,
    STRUCT_INITIALIZER,
};

struct Callable
{
    Callable_Type type;
    union {
        ModTree_Function* function;
        Hardcoded_Type hardcoded;
        Function_Progress* polymorphic_function;
        Workload_Structure_Polymorphic* polymorphic_struct;
        Type_Struct* structure;
    } options;

    int parameter_count;
    Optional<Type_Base*> return_value_type;
    Type_Function* function_signature; // Only valid for specific callable types, otherwise 0

    // Source info
    Symbol* symbol; // May be null
    Expression_Info expression_info; // May be empty

    // For convenience when resolving overloads
    Type_Base* overloading_param_type;
    bool overloading_arg_matches_type;
    bool overloading_arg_can_be_cast;
};

Optional<Callable> callable_try_create_from_expression_info(Expression_Info& info, Symbol* origin_symbol = 0)
{
    Callable callable;
    callable.function_signature = 0;
    callable.parameter_count = -1;
    callable.expression_info = info; // Copies info
    callable.return_value_type.available = false;
    callable.symbol = origin_symbol;

    switch (info.result_type)
    {
    case Expression_Result_Type::CONSTANT:
    case Expression_Result_Type::NOTHING: 
    case Expression_Result_Type::TYPE: {
        return optional_make_failure<Callable>();
    }
    case Expression_Result_Type::POLYMORPHIC_STRUCT:
    {
        callable.type = Callable_Type::POLYMORPHIC_STRUCT;
        callable.options.polymorphic_struct = info.options.polymorphic_struct;
        callable.parameter_count = info.options.polymorphic_struct->parameters.size;
        break;
    }
    case Expression_Result_Type::VALUE:
    {
        // TODO: Check if this is comptime known, then we dont need a function pointer call
        auto type = info.options.type;
        if (type->type == Type_Type::FUNCTION) {
            callable.type = Callable_Type::FUNCTION_POINTER;
            callable.function_signature = downcast<Type_Function>(type);
        }
        else {
            return optional_make_failure<Callable>();
        }
        break;
    }
    case Expression_Result_Type::FUNCTION: {
        callable.type = Callable_Type::FUNCTION;
        callable.options.function = info.options.function;
        callable.function_signature = info.options.function->signature;
        break;
    }
    case Expression_Result_Type::HARDCODED_FUNCTION:
    {
        callable.type = Callable_Type::HARDCODED;
        callable.options.hardcoded = info.options.hardcoded;
        callable.function_signature = hardcoded_type_to_signature(callable.options.hardcoded);
        callable.parameter_count = callable.function_signature->parameters.size;
        break;
    }
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    {
        callable.type = Callable_Type::POLYMORPHIC_FUNCTION;
        callable.function_signature = info.options.polymorphic_function->function->signature;
        callable.options.polymorphic_function = info.options.polymorphic_function;
        break;
    }
    default: panic("");
    }

    if (callable.parameter_count == -1) {
        assert(callable.function_signature != 0, "");
        callable.parameter_count = callable.function_signature->parameters.size;
        callable.return_value_type = callable.function_signature->return_type;
    }

    return optional_make_success(callable);
}

Callable callable_create_from_struct_initializer(Type_Struct* struct_type)
{
    Callable result;
    result.type = Callable_Type::STRUCT_INITIALIZER;
    result.function_signature = 0;
    result.options.structure = struct_type;
    result.parameter_count = struct_type->members.size;
    result.symbol = 0;
    result.return_value_type.available = false;
    return result;
}

struct Callable_Parameter_Info
{
    Type_Base* type;
    Optional<String*> name;
    bool has_default_value;
};

Callable_Parameter_Info callable_get_parameter_info(const Callable& callable, int parameter_index)
{
    Callable_Parameter_Info result;
    assert(parameter_index >= 0 && parameter_index < callable.parameter_count, "");

    if (callable.function_signature != 0) {
        auto& param = callable.function_signature->parameters[parameter_index];
        result.type = param.type;
        result.has_default_value = param.has_default_value;
        result.name = param.name;
        return result;
    }

    switch (callable.type)
    {
    case Callable_Type::POLYMORPHIC_STRUCT: {
        auto& param = callable.options.polymorphic_struct->parameters[parameter_index];
        result.type = param.parameter_type;
        result.name = optional_make_success(param.symbol->id);
        result.has_default_value = param.has_default_value;
        return result;
    }
    case Callable_Type::STRUCT_INITIALIZER: {
        auto& member = callable.options.structure->members[parameter_index];
        result.has_default_value = false;
        result.name = optional_make_success(member.id);
        result.type = member.type;
        return result;
    }
    default: panic("");
    }

    panic("Hey");
    return result;
}

// Checks for same identifier arguments, and named/unnamed argument order errors
bool arguments_check_for_naming_errors(Dynamic_Array<AST::Argument*> & arguments, int* out_unnamed_argument_count = 0)
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
            Callable_Parameter_Info param = callable_get_parameter_info(callable, j);

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


bool match_polymorphic_type(Type_Base* polymorphic_type, Type_Base* match_against, Hashset<Type_Base*>& already_visited, Array<Type_Base*>& implicit_parameter_types)
{
    if (!polymorphic_type->contains_polymorphic_type) {
        return types_are_equal(polymorphic_type, match_against);
    }
    if (hashset_contains(&already_visited, polymorphic_type)) {
        return true;
    }
    hashset_insert_element(&already_visited, polymorphic_type);

    // Note: Not sure about this assert, but I think when evaluating poly arguments in correct order this shouldn't happen
    assert(!match_against->contains_polymorphic_type, "");

    // Check if we found match
    if (polymorphic_type->type == Type_Type::POLYMORPHIC)
    {
        auto poly_type = downcast<Type_Polymorphic>(polymorphic_type);
        if (poly_type->is_reference) {
            return true; // Don't match references
        }
        assert(implicit_parameter_types[poly_type->index] == 0, "We shouldn't match the same parameter more than once");
        implicit_parameter_types[poly_type->index] = match_against;
        return true;
    }

    // Exit early if expected types don't match
    if (polymorphic_type->type != match_against->type) {
        return false;
    }

    switch (polymorphic_type->type)
    {
    case Type_Type::ARRAY:
        return match_polymorphic_type(downcast<Type_Array>(polymorphic_type)->element_type, downcast<Type_Array>(match_against)->element_type, already_visited, implicit_parameter_types);
    case Type_Type::SLICE:
        return match_polymorphic_type(downcast<Type_Slice>(polymorphic_type)->element_type, downcast<Type_Slice>(match_against)->element_type, already_visited, implicit_parameter_types);
    case Type_Type::POINTER:
        return match_polymorphic_type(downcast<Type_Pointer>(polymorphic_type)->points_to_type, downcast<Type_Pointer>(match_against)->points_to_type, already_visited, implicit_parameter_types);
    case Type_Type::STRUCT: {
        auto a = downcast<Type_Struct>(polymorphic_type);
        auto b = downcast<Type_Struct>(match_against);
        if (a->struct_type != b->struct_type || a->members.size != b->members.size) {
            return false;
        }
        for (int i = 0; i < a->members.size; i++) {
            if (!match_polymorphic_type(a->members[i].type, b->members[i].type, already_visited, implicit_parameter_types)) {
                return false;
            }
        }
        return true;
    }
    case Type_Type::FUNCTION: {
        auto a = downcast<Type_Function>(polymorphic_type);
        auto b = downcast<Type_Function>(match_against);
        if (a->parameters.size != b->parameters.size || a->return_type.available != b->return_type.available) {
            return false;
        }
        for (int i = 0; i < a->parameters.size; i++) {
            if (!match_polymorphic_type(a->parameters[i].type, b->parameters[i].type, already_visited, implicit_parameter_types)) {
                return false;
            }
        }
        if (a->return_type.available) {
            if (!match_polymorphic_type(a->return_type.value, b->return_type.value, already_visited, implicit_parameter_types)) {
                return false;
            }
        }
        return true;
    }
    case Type_Type::ENUM:
    case Type_Type::ERROR_TYPE:
    case Type_Type::VOID_POINTER:
    case Type_Type::PRIMITIVE:
    case Type_Type::TYPE_HANDLE: panic("Should be handled by previous code-path (E.g. non polymorphic!)");
    case Type_Type::POLYMORPHIC: panic("Previous code path should have handled this!");
    default: panic("");
    }

    panic("");
    return true;
}

const int MAX_POLYMORPHIC_INSTANCIATION_DEPTH = 10;

#define SET_ACTIVE_EXPR_INFO(new_info)\
    Expression_Info* _backup_info = semantic_analyser.current_workload->current_expression; \
    semantic_analyser.current_workload->current_expression = new_info; \
    SCOPE_EXIT(semantic_analyser.current_workload->current_expression = _backup_info);

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

        int unnamed_argument_count = 0;
        if (arguments_check_for_naming_errors(call.arguments, &unnamed_argument_count)) {
            semantic_analyser_analyse_expression_any(call.expr, expression_context_make_unknown());
            arguments_analyse_in_unknown_context(arguments);
            EXIT_ERROR(types.error_type);
        }

        // Find all overload candidates
        Dynamic_Array<Callable> candidates = dynamic_array_create_empty<Callable>(1);
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
            for (int i = 0; i < symbols.size; i++)
            {
                auto& symbol = symbols[i];
                auto callable_opt = callable_try_create_from_expression_info(
                    analyse_symbol_as_expression(symbol, expression_context_make_auto_dereference(), call.expr->options.path_lookup->last), 
                    symbol
                );
                if (callable_opt.available) {
                    auto callable = callable_opt.value;
                    bool add_to_candidates = true;

                    // I need to wait for polymorphic struct analysis, otherwise the parameters wouldn't be analysed
                    // TODO: This requires another seperate header workload
                    if (callable.type == Callable_Type::POLYMORPHIC_STRUCT) {
                        bool has_failed = false;
                        analysis_workload_add_dependency_internal(
                            semantic_analyser.current_workload,
                            upcast(callable.options.polymorphic_struct),
                            dependency_failure_info_make(&has_failed, call.expr->options.path_lookup->last));
                        workload_executer_wait_for_dependency_resolution();
                        if (has_failed) {
                            add_to_candidates = false;
                        }
                    }

                    if (add_to_candidates) {
                        dynamic_array_push_back(&candidates, callable);
                    }
                }
            }
            if (symbols.size == 1 && candidates.size == 0) {
                log_semantic_error("Symbol is not callable!", upcast(call.expr->options.path_lookup->last));
                log_error_info_symbol(symbols[0]);
                EXIT_ERROR(types.error_type);
            }
        }
        else
        {
            auto* info = semantic_analyser_analyse_expression_any(call.expr, expression_context_make_auto_dereference());
            auto callable_opt = callable_try_create_from_expression_info(*info);
            if (callable_opt.available) {
                dynamic_array_push_back(&candidates, callable_opt.value);
            }
            else {
                log_semantic_error("Expression is not callable!", upcast(call.expr->options.path_lookup->last));
                log_error_info_expression_result_type(info->result_type);
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
                    if (candidate.type == Callable_Type::POLYMORPHIC_FUNCTION || candidate.type == Callable_Type::POLYMORPHIC_STRUCT) {
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
                    if (!arguments_match_to_parameters(arguments, candidate, unnamed_argument_count, false)) {
                        dynamic_array_swap_remove(&candidates, i);
                        i -= 1;
                    }
                }
            }

            // Disambiguate overloads by argument types
            if (candidates.size > 1)
            {
                auto remove_candidates_based_on_better_type_match = [&](Dynamic_Array<Callable>& candidates, Type_Base* expected_type)
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
                            Expression_Post_Op post_op = expression_context_apply(
                                expected_type, expression_context_make_specific_type(candidate.overloading_param_type), false);
                            if (post_op.implicit_cast != Info_Cast_Type::INVALID) {
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
                        Type_Base* param_type = 0;
                        if (arg->name.available) {
                            // Find named argument
                            for (int k = 0; k < candidate.parameter_count; k++) {
                                auto& param = callable_get_parameter_info(candidate, k);
                                if (param.name.available && param.name.value == arg->name.value) {
                                    param_type = param.type;
                                    break;
                                }
                            }
                        }
                        else {
                            param_type = callable_get_parameter_info(candidate, i).type;
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
                    auto expected_return_type = context.signature;
                    bool can_differentiate_based_on_return_type = false;
                    Type_Base* last_return_type = 0;
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
        bool success = arguments_match_to_parameters(arguments, candidate, unnamed_argument_count, true, upcast(expr), Parser::Section::ENCLOSURE);
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
        switch (candidate.type)
        {
        case Callable_Type::FUNCTION:
        case Callable_Type::FUNCTION_POINTER: {
            break;
        }
        case Callable_Type::HARDCODED:
        {
            if (candidate.options.hardcoded == Hardcoded_Type::TYPE_OF)
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
        case Callable_Type::POLYMORPHIC_FUNCTION:
        {
            auto base_progress = candidate.options.polymorphic_function;
            assert(base_progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_BASE, "");
            auto& poly_base = base_progress->polymorphic_base;

            // Check if we've hit recursion limit for function instanciation
            int instanciation_depth = get_current_polymorphic_instanciation_depth() + 1;
            if (instanciation_depth > MAX_POLYMORPHIC_INSTANCIATION_DEPTH) {
                log_semantic_error("Reached maximum polymorphic instanciation depth 10, not analysing further", call.expr, Parser::Section::FIRST_TOKEN);
                arguments_analyse_in_unknown_context(arguments);
                EXIT_ERROR(types.error_type);
            }

            // Prepare instanciation data
            bool success = true;
            bool log_error_on_failure = false;
            Array<Upp_Constant> comptime_arguments = array_create_empty<Upp_Constant>(poly_base.polymorphic_parameter_indices.size);
            SCOPE_EXIT(if (!success) { array_destroy(&comptime_arguments); });
            Array<Type_Base*> implicit_parameter_types = array_create_empty<Type_Base*>(poly_base.implicit_parameters.size);
            SCOPE_EXIT(if (!success) { array_destroy(&implicit_parameter_types); });
            for (int i = 0; i < implicit_parameter_types.size; i++) {
                implicit_parameter_types[i] = 0;
            }
            Analysis_Pass* comptime_evaluation_pass = analysis_pass_allocate(semantic_analyser.current_workload, upcast(expr));
            Hashset<Type_Base*> matching_helper = hashset_create_pointer_empty<Type_Base*>(1);
            SCOPE_EXIT(hashset_destroy(&matching_helper));

            // Analyse polymorphic_function arguments in correct order
            for (int i = 0; i < poly_base.comptime_argument_evaluation_order.size && success; i++)
            {
                int parameter_index = poly_base.comptime_argument_evaluation_order[i];
                auto& parameter_node =
                    base_progress->header_workload->function_node->options.function.signature->options.function_signature.parameters[parameter_index];

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

                // Re-analyse base-header to get valid poly-argument type (Since this type can change with filled out polymorphic_function values)
                Type_Base* parameter_type = 0;
                {
                    RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->current_pass, comptime_evaluation_pass);
                    RESTORE_ON_SCOPE_EXIT(analyser.current_workload->polymorphic_values, comptime_arguments);
                    RESTORE_ON_SCOPE_EXIT(analyser.current_workload->current_symbol_table, base_progress->function->parameter_table);
                    RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->implicit_polymorphic_types, implicit_parameter_types);
                    parameter_type = semantic_analyser_analyse_expression_type(parameter_node->type);
                    if (type_is_unknown(parameter_type) && !parameter_type->contains_polymorphic_type) {
                        success = false;
                    }
                }

                // If this is a comptime argument, calculate comptime value
                if (arg_info->is_polymorphic)
                {
                    // Analyse Argument and try to get comptime value
                    if (types_are_equal(parameter_type, upcast(types.type_handle))) {
                        // In this case we are looking for a comptime value for a type, so well call analyse type
                        semantic_analyser_analyse_expression_type(argument_node->value);
                    }
                    else {
                        Expression_Context context = expression_context_make_unknown();
                        if (!parameter_type->contains_polymorphic_type) {
                            context = expression_context_make_specific_type(parameter_type);
                        }
                        semantic_analyser_analyse_expression_value(argument_node->value, context);
                    }
                    arg_info->already_analysed = true;

                    bool was_unavailable = false;
                    auto comptime_result = expression_calculate_comptime_value(
                        argument_node->value, "Parameter is polymorphic, but argument cannot be evaluated at comptime", &was_unavailable);
                    if (!comptime_result.available) {
                        success = false;
                        if (!was_unavailable) {
                            log_error_on_failure = true;
                        }
                    }
                    else {
                        comptime_arguments[i] = comptime_result.value;
                    }
                }

                // Do type-matching for implicit parameters
                if (parameter_type->contains_polymorphic_type)
                {
                    Type_Base* argument_type;
                    if (arg_info->already_analysed) {
                        argument_type = get_info(argument_node->value)->post_op.type_afterwards;
                    }
                    else {
                        argument_type = semantic_analyser_analyse_expression_value(argument_node->value, expression_context_make_unknown());
                    }
                    arg_info->already_analysed = true;
                    arg_info->context_application_missing = !arg_info->is_polymorphic; // Note: If matching succeeds, the types should be equal, this just adds another check if that's true
                    hashset_reset(&matching_helper);
                    bool match_success = match_polymorphic_type(parameter_type, argument_type, matching_helper, implicit_parameter_types);
                    if (!match_success) {
                        log_semantic_error("Could not match argument type with polymorphic type!", upcast(argument_node));
                        log_error_info_given_type(argument_type);
                        log_error_info_expected_type(parameter_type);
                        success = false;
                    }
                }
            }

            if (!success) {
                if (log_error_on_failure) {
                    log_semantic_error("Some values couldn't be calculated at comptime!", AST::upcast(expr), Parser::Section::ENCLOSURE);
                }
                arguments_analyse_in_unknown_context(arguments);
                EXIT_ERROR(types.error_type);
            }

            // Check if we have already instanciated the function with given parameters
            Function_Progress* instance = 0;
            {
                for (int i = 0; i < poly_base.instances.size; i++)
                {
                    auto& test_instance = poly_base.instances[i]->polymorhic_instance;
                    bool all_matching = true;
                    for (int j = 0; j < poly_base.polymorphic_parameter_indices.size; j++) {
                        if (!constant_pool_compare_constants(&compiler.constant_pool, test_instance.parameter_values[j], comptime_arguments[j])) {
                            all_matching = false;
                            break;
                        }
                    }
                    for (int j = 0; j < poly_base.implicit_parameters.size; j++) {
                        if (!types_are_equal(test_instance.implicit_parameter_values[j], implicit_parameter_types[j])) {
                            all_matching = false;
                            break;
                        }
                    }

                    if (all_matching) {
                        success = false; // Delete current array
                        instance = poly_base.instances[i];
                    }
                }
            }

            // Create new instance if necessary
            if (instance == 0)
            {
                // Analyse the rest of the function signature to get the instance function signature
                Type_Function* instance_function_type = 0;
                {
                    Analysis_Pass* instance_parameter_evaluation_pass = analysis_pass_allocate(semantic_analyser.current_workload, upcast(expr));
                    RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->polymorphic_values, comptime_arguments);
                    RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->current_pass, instance_parameter_evaluation_pass);
                    RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->current_symbol_table, base_progress->function->parameter_table);
                    RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->implicit_polymorphic_types, implicit_parameter_types);
                    auto& signature_node = base_progress->header_workload->function_node->options.function.signature->options.function_signature;
                    auto base_signature = base_progress->function->signature;

                    Type_Function unfinished = type_system_make_function_empty();
                    for (int i = 0; i < signature_node.parameters.size; i++)
                    {
                        auto param_node = signature_node.parameters[i];
                        if (param_node->is_comptime) {
                            continue;
                        }
                        Function_Parameter parameter = function_parameter_make_empty(0, 0);
                        analyse_parameter_type_and_value(parameter, param_node);
                        assert(!parameter.type->contains_polymorphic_type, "");
                        dynamic_array_push_back(&unfinished.parameters, parameter);
                    }
                    Type_Base* return_type = 0;
                    if (signature_node.return_value.available) {
                        return_type = semantic_analyser_analyse_expression_type(signature_node.return_value.value);
                    }
                    instance_function_type = type_system_finish_function(unfinished, return_type);
                }

                // Create new instance progress
                instance = function_progress_create_with_modtree_function(0, base_progress->header_workload->function_node, instance_function_type, base_progress);
                instance->type = Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
                instance->polymorhic_instance.instanciation_depth = instanciation_depth;
                instance->polymorhic_instance.parameter_values = comptime_arguments;
                instance->polymorhic_instance.polymorphic_base = base_progress;
                instance->polymorhic_instance.implicit_parameter_values = implicit_parameter_types;
                dynamic_array_push_back(&poly_base.instances, instance);
            }

            // Re-assign argument to parameter mapping with new instance function-signature
            for (int i = 0; i < arguments.size; i++) {
                auto arg = arguments[i];
                auto info = get_info(arg);
                if (info->is_polymorphic) {
                    continue;
                }
                info->argument_index = base_progress->function->signature->parameters[info->argument_index].normal.index_in_non_polymorphic_signature;
            }

            // Store instanciation info in expression_info
            call_expr_info->options.polymorphic_function = instance;
            candidate.function_signature = instance->function->signature;
            semantic_analyser_register_function_call(instance->function);
            break;
        }
        case Callable_Type::POLYMORPHIC_STRUCT: {
            auto base = candidate.options.polymorphic_struct;

            int instanciation_depth = get_current_polymorphic_instanciation_depth() + 1;
            if (instanciation_depth > MAX_POLYMORPHIC_INSTANCIATION_DEPTH) {
                log_semantic_error("Reached maximum polymorphic instanciation depth 10, not analysing further", call.expr, Parser::Section::FIRST_TOKEN);
                arguments_analyse_in_unknown_context(arguments);
                EXIT_ERROR(types.error_type);
            }

            // Analyse all arguments as comptime values
            bool error_occured = false;
            Array<Upp_Constant> comptime_values = array_create_empty<Upp_Constant>(base->parameters.size);
            SCOPE_EXIT(if (error_occured) { array_destroy(&comptime_values); });

            for (int i = 0; i < arguments.size; i++) {
                auto& arg = arguments[i];
                auto arg_info = get_info(arg);
                auto expected_type = base->parameters[arg_info->argument_index].parameter_type;

                if (!arg_info->already_analysed) { // If arguments have been analysed during overload disambiguation
                    auto context = expression_context_make_specific_type(expected_type);
                    if (arg_info->context_application_missing) {
                        auto value_info = get_info(arg->value);
                        value_info->post_op = expression_context_apply(expression_info_get_type(value_info, true), context, true, arg->value);
                    }
                    else {
                        semantic_analyser_analyse_expression_value(arg->value, context);
                    }
                }
                
                Optional<Upp_Constant> constant = expression_calculate_comptime_value(arg->value, "Polymorphic struct arguments must be comptime");
                if (constant.available) {
                    comptime_values[i] = constant.value;
                }
                else {
                    error_occured = true;
                }
            }

            if (error_occured) {
                EXIT_TYPE(types.error_type);
            }

            // Check if we have already instanciated the structure with given parameters
            {
                for (int i = 0; i < base->instances.size; i++)
                {
                    Structure_Polymorphic_Instance* test_instance = base->instances[i];
                    bool all_matching = true;
                    for (int j = 0; j < base->parameters.size; j++) {
                        if (!constant_pool_compare_constants(&compiler.constant_pool, test_instance->parameter_values[j], comptime_values[j])) {
                            all_matching = false;
                            break;
                        }
                    }

                    if (all_matching) {
                        error_occured = true; // So that array is deleted
                        int offset = offsetof(Workload_Structure_Body, polymorphic_info);
                        Workload_Structure_Body* body = (Workload_Structure_Body*) &(((byte*)test_instance)[-offset]);
                        assert(body->polymorphic_type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE, "");
                        EXIT_TYPE(upcast(body->struct_type));
                    }
                }
            }

            // Create new struct instance
            auto body_workload = workload_structure_create(base->body_workload->struct_node, 0, true);
            body_workload->polymorphic_type = Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
            Structure_Polymorphic_Instance* instance = &body_workload->polymorphic_info.instance;
            instance->instanciation_depth = get_current_polymorphic_instanciation_depth() + 1;
            instance->parameter_values = comptime_values;
            instance->parent = base;
            
            analysis_workload_add_dependency_internal(upcast(body_workload), upcast(base->body_workload));
            dynamic_array_push_back(&base->instances, instance);
            EXIT_TYPE(upcast(body_workload->struct_type));
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
            if (arg_info->context_application_missing) {
                auto value_info = get_info(arg->value);
                auto arg_context = expression_context_make_specific_type(params[arg_info->argument_index].type);
                value_info->post_op = expression_context_apply(
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
        Symbol* symbol = path_lookup_resolve_to_single_symbol(path);
        assert(symbol != 0, "In error cases this should be set to error, never 0!");

        // Analyse symbol
        *info = analyse_symbol_as_expression(symbol, context, path->last);
        return info;
    }
    case AST::Expression_Type::POLYMORPHIC_SYMBOL: {
        // Note: The mapping is only registered during header analysis, otherwise we have an invalid usage of this expression
        auto polymorphic_type_opt = hashtable_find_element(&semantic_analyser.implicit_parameter_node_mapping, expr);
        if (polymorphic_type_opt == 0) {
            log_semantic_error("Implicit polymorphic parameter only valid in function header!", expr);
            EXIT_ERROR(types.error_type);
        }

        // Check if instanciated values are available
        auto poly_type = *polymorphic_type_opt;
        if (poly_type->is_reference) {
            poly_type = poly_type->mirrored_type; // Note: Here we return the base type
        }
        auto result_type = upcast(poly_type);
        auto& workload = semantic_analyser.current_workload;
        if (workload->implicit_polymorphic_types.data != 0) {
            auto instance_type = workload->implicit_polymorphic_types[poly_type->index];
            if (instance_type != 0) {
                result_type = instance_type;
            }
        }
        EXIT_TYPE(result_type);
    }
    case AST::Expression_Type::CAST:
    {
        auto cast = &expr->options.cast;
        Type_Base* destination_type = 0;
        if (cast->to_type.available) {
            destination_type = semantic_analyser_analyse_expression_type(cast->to_type.value);
        }

        // Determine Context and Destination Type
        Expression_Context operand_context = expression_context_make_unknown();
        switch (cast->type)
        {
        case AST::Cast_Type::TYPE_TO_TYPE:
        {
            if (destination_type == 0)
            {
                if (context.type != Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
                    log_semantic_error("No context is available for auto cast", expr);
                    EXIT_ERROR(types.error_type);
                }
                destination_type = context.signature;
            }
            break;
        }
        case AST::Cast_Type::RAW_TO_PTR:
        {
            if (destination_type == 0) {
                log_semantic_error("Raw to pointer cast requires destination type", expr);
                destination_type = types.error_type;
            }
            else {
                operand_context = expression_context_make_specific_type(upcast(types.u64_type));
                if (destination_type->type != Type_Type::POINTER) {
                    log_semantic_error("Destination type must be pointer", expr);
                    log_error_info_given_type(destination_type);
                }
            }
            break;
        }
        case AST::Cast_Type::PTR_TO_RAW:
        {
            if (destination_type != 0) {
                log_semantic_error("Must not specify destination type when casting to raw", expr);
            }
            destination_type = upcast(types.u64_type);
            break;
        }
        default: panic("");
        }

        auto operand_type = semantic_analyser_analyse_expression_value(cast->operand, operand_context);
        Info_Cast_Type cast_type;
        // Determine Cast type
        switch (cast->type)
        {
        case AST::Cast_Type::TYPE_TO_TYPE:
        {
            assert(destination_type != 0, "");
            cast_type = semantic_analyser_check_cast_type(operand_type, destination_type, false);
            if (cast_type == Info_Cast_Type::INVALID) {
                log_semantic_error("No cast available between given types", expr);
                log_error_info_given_type(operand_type);
                log_error_info_expected_type(destination_type);
            }
            break;
        }
        case AST::Cast_Type::PTR_TO_RAW: {
            if (operand_type->type != Type_Type::POINTER) {
                log_semantic_error("To raw cast require a pointer value", expr);
                log_error_info_given_type(operand_type);
            }
            cast_type = Info_Cast_Type::POINTER_TO_U64;
            break;
        }
        case AST::Cast_Type::RAW_TO_PTR:
        {
            cast_type = Info_Cast_Type::U64_TO_POINTER;
            break;
        }
        default: panic("");
        }

        info->specifics.explicit_cast = cast_type;
        EXIT_VALUE(destination_type);
    }
    case AST::Expression_Type::LITERAL_READ:
    {
        auto& read = expr->options.literal_read;
        void* value_ptr;
        Type_Base* literal_type;
        void* null_pointer = 0;
        Upp_String string_buffer;

        // Missing: float, nummptr
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
        case Literal_Type::NULL_VAL:
            literal_type = types.void_pointer_type;
            value_ptr = &null_pointer;
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
        default: panic("");
        }
        expression_info_set_constant(info, literal_type, array_create_static<byte>((byte*)value_ptr, literal_type->size), AST::upcast(expr));
        return info;
    }
    case AST::Expression_Type::ENUM_TYPE:
    {
        auto& members = expr->options.enum_members;
        Type_Enum* enum_type = type_system_make_enum_empty(0);
        int next_member_value = 1; // Note: Enum values all start at 1, so 0 represents an invalid enum
        for (int i = 0; i < members.size; i++)
        {
            auto& member_node = members[i];
            if (member_node->value.available)
            {
                semantic_analyser_analyse_expression_value(member_node->value.value, expression_context_make_specific_type(upcast(types.i32_type)));
                auto constant = expression_calculate_comptime_value(member_node->value.value, "Enum value must be comptime known");
                if (constant.available) {
                    next_member_value = upp_constant_to_value<i32>(&compiler.constant_pool, constant.value);
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
    case AST::Expression_Type::FUNCTION:
    case AST::Expression_Type::STRUCTURE_TYPE:
    case AST::Expression_Type::BAKE_BLOCK:
    case AST::Expression_Type::BAKE_EXPR:
    {
        // Search for already generated workloads on this node
        Workload_Base* workload = 0;
        {
            auto passes = hashtable_find_element(&semantic_analyser.ast_to_pass_mapping, AST::upcast(expr));
            if (passes != 0) {
                assert(passes->passes.size == 1, "With the current system, only one workload should ever exist for a anonymous definition!");
                workload = passes->passes[0]->origin_workload;
            }
        }

        // Create new workload if none exists yet
        if (workload == 0) {
            switch (expr->type)
            {
            case AST::Expression_Type::FUNCTION: {
                workload = upcast(function_progress_create_with_modtree_function(0, expr, 0)->header_workload);
                break;
            }
            case AST::Expression_Type::STRUCTURE_TYPE: {
                workload = upcast(workload_structure_create(expr, 0, false));
                break;
            }
            case AST::Expression_Type::BAKE_BLOCK:
            case AST::Expression_Type::BAKE_EXPR: {
                Type_Base* expected_type = 0;
                if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
                    expected_type = context.signature;
                }
                workload = upcast(bake_progress_create(expr, expected_type)->analysis_workload);
                break;
            }
            default:panic("Cannot happen anymore");
            }
        }

        // Possibly wait for workload and then return result
        switch (workload->type)
        {
        case Analysis_Workload_Type::BAKE_ANALYSIS: {
            // Wait for bake to finish
            auto progress = downcast<Workload_Bake_Analysis>(workload)->progress;
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
            break;
        };
        case Analysis_Workload_Type::FUNCTION_HEADER: {
            // Wait for workload
            auto progress = downcast<Workload_Function_Header>(workload)->progress;
            analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(progress->header_workload));
            workload_executer_wait_for_dependency_resolution();

            // Return value
            semantic_analyser_register_function_call(progress->function);
            EXIT_FUNCTION(progress->function);
            break;
        }
        case Analysis_Workload_Type::STRUCT_BODY: {
            EXIT_TYPE(upcast(downcast<Workload_Structure_Body>(workload)->struct_type));
            break;
        }
        case Analysis_Workload_Type::MODULE_ANALYSIS: {
            log_semantic_error("Anonymous modules aren't currently supported", expr);
            EXIT_ERROR(types.error_type);
        }
        case Analysis_Workload_Type::DEFINITION:
        case Analysis_Workload_Type::EVENT:
        case Analysis_Workload_Type::BAKE_EXECUTION:
        case Analysis_Workload_Type::FUNCTION_BODY:
        case Analysis_Workload_Type::IMPORT_RESOLVE:
        case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
        default:
        {
            panic("Invalid codepath, ");
            EXIT_ERROR(types.error_type);
            break;
        }
        }

        panic("Invalid code path");
        EXIT_ERROR(types.error_type);
        break;
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
                log_semantic_error("Comptime parameter_nodes are only allowed in functions, not in signatures!", AST::upcast(param_node));
                continue;
            }
            Function_Parameter parameter = function_parameter_make_empty();
            analyse_parameter_type_and_value(parameter, param_node);
            dynamic_array_push_back(&unfinished.parameters, parameter);
        }
        Type_Base* return_type = 0;
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
            array_size = upp_constant_to_value<int>(&compiler.constant_pool, comptime.value);
            if (array_size <= 0) {
                log_semantic_error("Array size must be greater than zero", array_node.size_expr);
                array_size_known = false;
            }
        }

        Type_Base* element_type = semantic_analyser_analyse_expression_type(array_node.type_expr);
        EXIT_TYPE(upcast(type_system_make_array(element_type, array_size_known, array_size)));
    }
    case AST::Expression_Type::SLICE_TYPE:
    {
        EXIT_TYPE(upcast(type_system_make_slice(semantic_analyser_analyse_expression_type(expr->options.slice_type))));
    }
    case AST::Expression_Type::NEW_EXPR:
    {
        auto& new_node = expr->options.new_expr;
        Type_Base* allocated_type = semantic_analyser_analyse_expression_type(new_node.type_expr);
        assert(!(allocated_type->size == 0 && allocated_type->alignment == 0), "HEY");

        Type_Base* result_type = 0;
        if (new_node.count_expr.available)
        {
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
        Type_Base* type = 0;
        if (init_node.type_expr.available) {
            type = semantic_analyser_analyse_expression_type(init_node.type_expr.value);
        }
        else {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED) {
                type = context.signature;
            }
            else {
                log_semantic_error("Could not determine struct type from context", expr, Parser::Section::FIRST_TOKEN);
                EXIT_ERROR(types.error_type);
            }
        }

        // Check type errors
        if (type->type != Type_Type::STRUCT) {
            log_semantic_error("Struct initializer requires structure type", expr);
            log_error_info_given_type(type);
            arguments_analyse_in_unknown_context(arguments);
            EXIT_ERROR(type);
        }
        wait_for_type_size_to_finish(type);

        auto struct_signature = downcast<Type_Struct>(type);
        assert(!(struct_signature->base.size == 0 && struct_signature->base.alignment == 0), "");
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
            arguments, callable_create_from_struct_initializer(struct_signature), unnamed_argument_count, true, upcast(expr), Parser::Section::ENCLOSURE);
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
        Type_Base* element_type = 0;
        if (init_node.type_expr.available) {
            element_type = semantic_analyser_analyse_expression_type(init_node.type_expr.value);
        }
        else
        {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
            {
                if (context.signature->type == Type_Type::ARRAY) {
                    element_type = downcast<Type_Array>(context.signature)->element_type;
                }
                else if (context.signature->type == Type_Type::SLICE) {
                    element_type = downcast<Type_Slice>(context.signature)->element_type;
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

        assert(!(element_type->size == 0 && element_type->alignment == 0), "");
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

        Type_Base* element_type = 0;
        if (array_type->type == Type_Type::ARRAY) {
            element_type = downcast<Type_Array>(array_type)->element_type;
        }
        else if (array_type->type == Type_Type::SLICE) {
            element_type = downcast<Type_Slice>(array_type)->element_type;
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
        switch (access_expr_info->result_type)
        {
        case Expression_Result_Type::TYPE:
        {
            Type_Base* base_type = access_expr_info->options.type;
            if (base_type->type == Type_Type::STRUCT && downcast<Type_Struct>(base_type)->struct_type == AST::Structure_Type::UNION) {
                base_type = downcast<Type_Struct>(base_type)->tag_member.type;
            }
            if (base_type->type != Type_Type::ENUM) {
                log_semantic_error("Member access for given type not possible", member_node.expr);
                log_error_info_given_type(base_type);
                EXIT_ERROR(types.error_type);
            }
            auto enum_type = downcast<Type_Enum>(base_type);
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
            log_semantic_error("Polymorphic struct member access not implemented yet!", member_node.expr);
            log_error_info_expression_result_type(access_expr_info->result_type);
            EXIT_ERROR(types.error_type);
        }
        case Expression_Result_Type::VALUE:
        case Expression_Result_Type::CONSTANT:
        {
            auto base_type = access_expr_info->post_op.type_afterwards;
            if (base_type->type == Type_Type::STRUCT)
            {
                wait_for_type_size_to_finish(base_type);
                auto& members = downcast<Type_Struct>(base_type)->members;
                Struct_Member* found = 0;
                for (int i = 0; i < members.size; i++) {
                    Struct_Member* member = &members[i];
                    if (member->id == member_node.name) {
                        found = member;
                    }
                }
                if (found == 0) {
                    log_semantic_error("Struct doesn't contain a member with this name", expr);
                    log_error_info_id(member_node.name);
                    EXIT_ERROR(types.error_type);
                }
                EXIT_VALUE(found->type);
            }
            else if (base_type->type == Type_Type::ARRAY || base_type->type == Type_Type::SLICE)
            {
                if (member_node.name != compiler.id_size && member_node.name != compiler.id_data) {
                    log_semantic_error("Arrays/Slices only have either .size or .data as members", expr);
                    log_error_info_id(member_node.name);
                    EXIT_ERROR(types.error_type);
                }

                if (base_type->type == Type_Type::ARRAY)
                {
                    auto array = downcast<Type_Array>(base_type);
                    if (member_node.name == compiler.id_size) {
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
                    auto slice = downcast<Type_Slice>(base_type);
                    Struct_Member member;
                    if (member_node.name == compiler.id_size) {
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
        if (context.signature->type != Type_Type::ENUM) {
            log_semantic_error("Context requires a type that is not an enum, so .NAME syntax is not valid", expr);
            log_error_info_given_type(context.signature);
            EXIT_ERROR(types.error_type);
        }

        auto& members = downcast<Type_Enum>(context.signature)->members;
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

        expression_info_set_constant_enum(info, context.signature, value);
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
                if (operand_type->type == Type_Type::PRIMITIVE) {
                    auto primitive = downcast<Type_Primitive>(operand_type);
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
            switch (operand_result->result_type)
            {
            case Expression_Result_Type::VALUE:
            case Expression_Result_Type::CONSTANT:
            {
                Type_Base* operand_type = operand_result->post_op.type_afterwards;
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
            Type_Base* result_type = types.error_type;
            if (operand_type->type != Type_Type::POINTER || types_are_equal(operand_type, types.void_pointer_type)) {
                log_semantic_error("Cannot dereference non-pointer value", expr);
                log_error_info_given_type(operand_type);
            }
            else {
                result_type = downcast<Type_Pointer>(operand_type)->points_to_type;
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
        if (enum_valid && left_type->type == Type_Type::ENUM) {
            operand_context = expression_context_make_specific_type(left_type);
        }
        auto right_type = semantic_analyser_analyse_expression_value(binop_node.right, operand_context);

        // Check for unknowns
        if (type_is_unknown(left_type) || type_is_unknown(right_type)) {
            EXIT_ERROR(types.error_type);
        }

        // Try implicit casting if types dont match
        Type_Base* operand_type = left_type;
        bool types_are_valid = true;

        if (!types_are_equal(left_type, right_type))
        {
            auto left_cast = semantic_analyser_check_cast_type(left_type, right_type, true);
            auto right_cast = semantic_analyser_check_cast_type(right_type, left_type, true);
            if (left_cast != Info_Cast_Type::INVALID) {
                expression_info_set_cast(get_info(binop_node.left), left_cast, right_type);
                operand_type = right_type;
            }
            else if (right_cast != Info_Cast_Type::INVALID) {
                expression_info_set_cast(get_info(binop_node.right), right_cast, left_type);
                operand_type = left_type;
            }
            else {
                types_are_valid = false;
            }
        }

        // Check if given type is valid
        if (types_are_valid)
        {
            if (operand_type->type == Type_Type::POINTER) {
                types_are_valid = ptr_valid;
            }
            else if (operand_type->type == Type_Type::VOID_POINTER) {
                types_are_valid = ptr_valid;
            }
            else if (operand_type->type == Type_Type::PRIMITIVE)
            {
                auto primitive = downcast<Type_Primitive>(operand_type);
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
            else if (operand_type->type == Type_Type::ENUM) {
                types_are_valid = enum_valid;
            }
            else if (operand_type->type == Type_Type::TYPE_HANDLE) {
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

// If errors occured, cast_type is set to INVALID
Expression_Post_Op expression_context_apply(
    Type_Base* initial_type, Expression_Context context, bool log_errors, AST::Expression* error_report_node, Parser::Section error_section)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;

    // Set active expression info
    Expression_Info* _backup_info = semantic_analyser.current_workload->current_expression;
    SCOPE_EXIT(semantic_analyser.current_workload->current_expression = _backup_info);
    if (error_report_node != 0 && log_errors) {
        semantic_analyser.current_workload->current_expression = get_info(error_report_node);
    }

    Expression_Post_Op result;
    result.type_afterwards = initial_type;
    result.deref_count = 0;
    result.take_address_of = false;
    result.implicit_cast = Info_Cast_Type::INVALID;

    // Do nothing if context is unknown
    if (context.type == Expression_Context_Type::UNKNOWN) {
        result.implicit_cast = Info_Cast_Type::NO_CAST;
        return result;
    }

    // If either requested type or value type is unknown, just raise error flag
    if (type_is_unknown(initial_type) ||
        (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED && type_is_unknown(context.signature)))
    {
        if (log_errors) {
            semantic_analyser_set_error_flag(true);
        }
        result.type_afterwards = types.error_type;
        result.implicit_cast = Info_Cast_Type::INVALID;
        return result;
    }

    // Auto pointer dereferencing/address of
    {
        int wanted_pointer_depth = 0;
        switch (context.type)
        {
        case Expression_Context_Type::UNKNOWN: {
            result.implicit_cast = Info_Cast_Type::NO_CAST;
            return result;
        }
        case Expression_Context_Type::AUTO_DEREFERENCE:
            wanted_pointer_depth = 0;
            break;
        case Expression_Context_Type::SPECIFIC_TYPE_EXPECTED:
        {
            Type_Base* temp = context.signature;
            wanted_pointer_depth = 0;
            while (temp->type == Type_Type::POINTER) {
                wanted_pointer_depth++;
                temp = downcast<Type_Pointer>(temp)->points_to_type;
            }
            break;
        }
        default: panic("");
        }

        // Find out given pointer level
        int given_pointer_depth = 0;
        Type_Base* iter = initial_type;
        while (iter->type == Type_Type::POINTER) {
            given_pointer_depth++;
            iter = downcast<Type_Pointer>(iter)->points_to_type;
        }

        if (given_pointer_depth + 1 == wanted_pointer_depth && context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
        {
            // Auto address of
            result.take_address_of = true;
            result.type_afterwards = upcast(type_system_make_pointer(initial_type));
        }
        else
        {
            // Auto-Dereference to given level
            result.deref_count = given_pointer_depth - wanted_pointer_depth;
            for (int i = 0; i < result.deref_count; i++) {
                assert(result.type_afterwards->type == Type_Type::POINTER, "");
                result.type_afterwards = downcast<Type_Pointer>(result.type_afterwards)->points_to_type;
            }
        }
    }

    // Implicit casting
    if (context.type == Expression_Context_Type::SPECIFIC_TYPE_EXPECTED)
    {
        if (!types_are_equal(result.type_afterwards, context.signature))
        {
            Info_Cast_Type cast_type = semantic_analyser_check_cast_type(result.type_afterwards, context.signature, true);
            if (cast_type == Info_Cast_Type::INVALID) {
                if (log_errors) {
                    log_semantic_error("Cannot implicitly cast from given to expected type", error_report_node, error_section);
                    log_error_info_given_type(initial_type);
                    log_error_info_expected_type(context.signature);
                }
            }
            result.implicit_cast = cast_type;
            result.type_afterwards = context.signature;
        }
        else {
            result.implicit_cast = Info_Cast_Type::NO_CAST;
        }
    }
    else {
        result.implicit_cast = Info_Cast_Type::NO_CAST;
    }

    return result;
}

Expression_Info* semantic_analyser_analyse_expression_any(AST::Expression* expression, Expression_Context context)
{
    auto& type_system = compiler.type_system;
    auto result = semantic_analyser_analyse_expression_internal(expression, context);
    SET_ACTIVE_EXPR_INFO(result);

    // Handle nothing/void
    if (result->result_type == Expression_Result_Type::NOTHING && context.type != Expression_Context_Type::NO_VALUE) {
        log_semantic_error("Expected value from expression, but got void/nothing", expression);
        result->result_type = Expression_Result_Type::VALUE;
        result->contains_errors = true;
        result->options.value_type = compiler.type_system.predefined_types.error_type;
        context.type = Expression_Context_Type::UNKNOWN;
    }
    else if (result->result_type != Expression_Result_Type::NOTHING && !type_is_unknown(result->post_op.type_afterwards) && context.type == Expression_Context_Type::NO_VALUE) {
        log_semantic_error("Value is not used", expression);
        result->result_type = Expression_Result_Type::VALUE;
        result->contains_errors = true;
        result->options.value_type = compiler.type_system.predefined_types.error_type;
        context.type = Expression_Context_Type::UNKNOWN;
    }
    else if (result->result_type == Expression_Result_Type::NOTHING && context.type == Expression_Context_Type::NO_VALUE) {
        // Here we set the result type to error, but we don't treat it as an error
        result->result_type = Expression_Result_Type::VALUE;
        result->contains_errors = false;
        result->options.value_type = upcast(compiler.type_system.predefined_types.empty_struct_type);
        result->post_op.type_afterwards = upcast(compiler.type_system.predefined_types.empty_struct_type);
        context.type = Expression_Context_Type::UNKNOWN;
        return result;
    }


    if (result->result_type != Expression_Result_Type::VALUE && result->result_type != Expression_Result_Type::CONSTANT) return result;
    result->post_op = expression_context_apply(expression_info_get_type(result, true), context, true, expression);
    return result;
}

Type_Base* semantic_analyser_try_convert_value_to_type(AST::Expression* expression)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto result = get_info(expression);
    switch (result->result_type)
    {
    case Expression_Result_Type::TYPE:
        return result->options.type;
    case Expression_Result_Type::CONSTANT:
    case Expression_Result_Type::VALUE:
    {
        if (type_is_unknown(result->post_op.type_afterwards)) {
            semantic_analyser_set_error_flag(true);
            return types.error_type;
        }
        if (!types_are_equal(result->post_op.type_afterwards, types.type_handle))
        {
            log_semantic_error("Expression cannot be converted to type", expression);
            log_error_info_given_type(result->post_op.type_afterwards);
            return types.error_type;
        }

        // Otherwise try to convert to constant
        Upp_Constant constant;
        if (result->result_type == Expression_Result_Type::VALUE) {
            auto comptime_opt = expression_calculate_comptime_value(expression, "Expression is a type, but it isn't known at compile time");
            Type_Base* result_type = types.error_type;
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

        auto type_index = upp_constant_to_value<u32>(&compiler.constant_pool, constant);
        if (type_index >= (u32)type_system.internal_type_infos.size) {
            // Note: Always log this error, because this should never happen!
            log_semantic_error("Expression contains invalid type handle", expression);
            return types.error_type;
        }
        return type_system.types[type_index];
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

Type_Base* semantic_analyser_analyse_expression_type(AST::Expression* expression)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto result = semantic_analyser_analyse_expression_any(expression, expression_context_make_auto_dereference());
    SET_ACTIVE_EXPR_INFO(result);
    return semantic_analyser_try_convert_value_to_type(expression);
}

Type_Base* semantic_analyser_analyse_expression_value(AST::Expression* expression, Expression_Context context)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;

    auto result = semantic_analyser_analyse_expression_any(expression, context);
    SET_ACTIVE_EXPR_INFO(result);
    switch (result->result_type)
    {
    case Expression_Result_Type::VALUE: {
        return result->post_op.type_afterwards; // Here context was already applied, so we return
    }
    case Expression_Result_Type::TYPE: {
        expression_info_set_constant(result, types.type_handle, array_create_static_as_bytes(&result->options.type->type_handle, 1), AST::upcast(expression));
        break;
    }
    case Expression_Result_Type::NOTHING:
    case Expression_Result_Type::CONSTANT:
    {
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
    default: panic("");
    }

    result->post_op = expression_context_apply(expression_info_get_type(result, true), context, true, expression);
    return result->post_op.type_afterwards;
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
    assert(semantic_analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_BODY, "Must be in body otherwise no workload exists");
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
        Optional<Type_Base*> expected_return_type = optional_make_failure<Type_Base*>();
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
            bool is_unknown = type_is_unknown(return_type);
            if (expected_return_type.available) {
                is_unknown = is_unknown || type_is_unknown(expected_return_type.value);
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
        // Note(Martin): This is a special case, in expression statements a void_type is valid
        semantic_analyser_analyse_expression_value(expression_node, expression_context_make_no_value_expected());
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
        if (switch_type->type == Type_Type::STRUCT && downcast<Type_Struct>(switch_type)->struct_type == AST::Structure_Type::UNION) {
            switch_type = downcast<Type_Struct>(switch_type)->tag_member.type;
        }
        else if (switch_type->type != Type_Type::ENUM)
        {
            log_semantic_error("Switch only works on either enum or union types", switch_node.condition);
            log_error_info_given_type(switch_type);
        }

        Expression_Context case_context = switch_type->type == Type_Type::ENUM ?
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
                    case_info->case_value = upp_constant_to_value<int>(&compiler.constant_pool, comptime.value);
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
        if (!default_found && switch_type->type == Type_Type::ENUM) {
            if (unique_count < downcast<Type_Enum>(switch_type)->members.size) {
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
        if (delete_type->type != Type_Type::POINTER && delete_type->type != Type_Type::SLICE) {
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
            Type_Base* left_side_type = 0;
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
                Type_Base* right_type = semantic_analyser_analyse_expression_value(assignment_node.right_side[0], expression_context_make_unknown());
                if (right_type->type == Type_Type::STRUCT && downcast<Type_Struct>(right_type)->struct_type == AST::Structure_Type::STRUCT)
                {
                    info->specifics.is_struct_split = true;
                    // Found struct split, check if all members have correct type
                    auto& members = downcast<Type_Struct>(right_type)->members;
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
                semantic_analyser_analyse_expression_value(assignment_node.right_side[0], expression_context_make_specific_type(left_side_type));
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
                    semantic_analyser_analyse_expression_value(assignment_node.right_side[i], expression_context_make_specific_type(left_type));
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
                bool is_split = value_type->type == Type_Type::STRUCT && downcast<Type_Struct>(value_type)->struct_type == AST::Structure_Type::STRUCT;
                info->specifics.is_struct_split = is_split;

                // On split, report error for excess/missing symbols
                if (is_split) {
                    auto& members = downcast<Type_Struct>(value_type)->members;
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
                    Type_Base* given_type;
                    if (is_split) {
                        auto& members = downcast<Type_Struct>(value_type)->members;
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
                    if (symbol->type == Symbol_Type::VARIABLE_UNDEFINED || (symbol->type == Symbol_Type::VARIABLE && type_is_unknown(symbol->options.variable_type))) {
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
                    if (symbol->type == Symbol_Type::VARIABLE && !type_is_unknown(symbol->options.variable_type)) {
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

Control_Flow semantic_analyser_analyse_block(AST::Code_Block* block)
{
    auto block_info = get_info(block, true);
    block_info->control_flow_locked = false;
    block_info->flow = Control_Flow::SEQUENTIAL;

    // Create symbol table for block
    block_info->symbol_table = symbol_table_create_with_parent(semantic_analyser.current_workload->current_symbol_table, true);
    RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->current_symbol_table, block_info->symbol_table);

    // Create symbols and workloads for definitions inside the block
    {
        auto progress = semantic_analyser.current_workload->current_function->progress;
        assert(progress != 0, "Do bakes not use analyse block? Maybe this is wrong...");
        bool dont_define_comptimes = progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE;
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

    // Check if main is defined
    semantic_analyser.program->main_function = 0;
    Dynamic_Array<Symbol*>* main_symbols = hashtable_find_element(&semantic_analyser.root_module->module_analysis->symbol_table->symbols, compiler.id_main);
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
        auto& base = progress->polymorphic_base;
        dynamic_array_destroy(&base.instances);
        dynamic_array_destroy(&base.comptime_argument_evaluation_order);
        dynamic_array_destroy(&base.implicit_parameters);
        dynamic_array_destroy(&base.polymorphic_parameter_indices);
    }
    else if (progress->type == Polymorphic_Analysis_Type::POLYMORPHIC_INSTANCE) {
        auto& instance = progress->polymorhic_instance;
        array_destroy(&instance.parameter_values);
        array_destroy(&instance.implicit_parameter_values);
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
        hashtable_reset(&semantic_analyser.implicit_parameter_node_mapping);

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
        semantic_analyser.root_symbol_table = symbol_table_create();

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

    // Create predefined Symbols 
    {
        auto root = semantic_analyser.root_symbol_table;
        auto pool = &compiler.identifier_pool;
        auto& symbols = semantic_analyser.predefined_symbols;
        auto& types = compiler.type_system.predefined_types;

        auto define_type_symbol = [&](const char* name, Type_Base* type) -> Symbol* {
            Symbol* result = symbol_table_define_symbol(root, identifier_pool_add(pool, string_create_static(name)), Symbol_Type::TYPE, 0, false);
            result->options.type = type;
            if (type->type == Type_Type::STRUCT) {
                downcast<Type_Struct>(type)->name = optional_make_success(result->id);
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
            Symbol* result = symbol_table_define_symbol(root, identifier_pool_add(pool, string_create_static(name)), Symbol_Type::HARDCODED_FUNCTION, 0, false);
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
    semantic_analyser.progress_allocator = stack_allocator_create_empty(2048);
    semantic_analyser.global_variable_memory_pool = stack_allocator_create_empty(1024);
    semantic_analyser.program = 0;
    semantic_analyser.ast_to_pass_mapping = hashtable_create_pointer_empty<AST::Node*, Node_Passes>(1);
    semantic_analyser.symbol_lookup_visited = hashset_create_pointer_empty<Symbol_Table*>(1);
    semantic_analyser.allocated_passes = dynamic_array_create_empty<Analysis_Pass*>(1);
    semantic_analyser.ast_to_info_mapping = hashtable_create_empty<AST_Info_Key, Analysis_Info*>(1, ast_info_key_hash, ast_info_equals);
    semantic_analyser.allocated_symbol_tables = dynamic_array_create_empty<Symbol_Table*>(16);
    semantic_analyser.allocated_symbols = dynamic_array_create_empty<Symbol*>(16);
    semantic_analyser.implicit_parameter_node_mapping = hashtable_create_pointer_empty<AST::Expression*, Type_Polymorphic*>(1);
    return &semantic_analyser;
}

void semantic_analyser_destroy()
{
    hashset_destroy(&semantic_analyser.symbol_lookup_visited);
    hashtable_destroy(&semantic_analyser.implicit_parameter_node_mapping);
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
            type_append_to_string(string, info->options.parameter.type);
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
            type_append_to_string(string, info->options.type);
            break;
        case Error_Information_Type::EXPECTED_TYPE:
            string_append_formated(string, "\n  Expected Type: ");
            type_append_to_string(string, info->options.type);
            break;
        case Error_Information_Type::FUNCTION_TYPE:
            string_append_formated(string, "\n  Function Type: ");
            type_append_to_string(string, info->options.type);
            break;
        case Error_Information_Type::BINARY_OP_TYPES:
            string_append_formated(string, "\n  Left Operand type:  ");
            type_append_to_string(string, info->options.binary_op_types.left_type);
            string_append_formated(string, "\n  Right Operand type: ");
            type_append_to_string(string, info->options.binary_op_types.right_type);
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


