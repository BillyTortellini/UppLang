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
bool PRINT_TIMING = true;
static Semantic_Analyser semantic_analyser;
static Workload_Executer workload_executer;

// PROTOTYPES
ModTree_Function* modtree_function_create_empty(Type_Signature* signature, Symbol* symbol, Function_Progress* progress, Symbol_Table* param_table);
Comptime_Result comptime_result_make_not_comptime();
Comptime_Result expression_calculate_comptime_value(AST::Expression* expr);
void analysis_workload_destroy(Workload_Base* workload);
Expression_Info* semantic_analyser_analyse_expression_any(AST::Expression* expression, Expression_Context context);
Type_Signature* semantic_analyser_analyse_expression_value(AST::Expression* rc_expression, Expression_Context context);
Type_Signature* semantic_analyser_analyse_expression_type(AST::Expression* rc_expression);
Type_Signature* semantic_analyser_try_convert_value_to_type(AST::Expression* expression, bool log_error);
Control_Flow semantic_analyser_analyse_block(AST::Code_Block* code_block);

bool workload_executer_switch_to_workload(Workload_Base* workload);
void analysis_workload_add_struct_dependency(Struct_Progress* my_workload, Struct_Progress* other_progress, Dependency_Type type, AST::Symbol_Lookup* lookup);
void analysis_workload_append_to_string(Workload_Base* workload, String* string);
void analysis_workload_add_dependency_internal(Workload_Base* workload, Workload_Base* dependency, AST::Symbol_Lookup* lookup);
void workload_executer_wait_for_dependency_resolution();



// HELPERS
namespace Helpers
{
    Analysis_Workload_Type get_workload_type(Workload_Using_Resolve* workload) { return Analysis_Workload_Type::USING_RESOLVE; };
    Analysis_Workload_Type get_workload_type(Workload_Module_Analysis* workload) { return Analysis_Workload_Type::MODULE_ANALYSIS; };
    Analysis_Workload_Type get_workload_type(Workload_Definition* workload) { return Analysis_Workload_Type::DEFINITION; };
    Analysis_Workload_Type get_workload_type(Workload_Struct_Analysis* workload) { return Analysis_Workload_Type::STRUCT_ANALYSIS; };
    Analysis_Workload_Type get_workload_type(Workload_Struct_Reachable_Resolve* workload) { return Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Header* workload) { return Analysis_Workload_Type::FUNCTION_HEADER; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Parameter* workload) { return Analysis_Workload_Type::FUNCTION_PARAMETER; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Body* workload) { return Analysis_Workload_Type::FUNCTION_BODY; };
    Analysis_Workload_Type get_workload_type(Workload_Function_Cluster_Compile* workload) { return Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE; };
    Analysis_Workload_Type get_workload_type(Workload_Bake_Analysis* workload) { return Analysis_Workload_Type::BAKE_ANALYSIS; };
    Analysis_Workload_Type get_workload_type(Workload_Bake_Execution* workload) { return Analysis_Workload_Type::BAKE_EXECUTION; };
    Analysis_Workload_Type get_workload_type(Workload_Event* workload) { return Analysis_Workload_Type::EVENT; };
};

Workload_Base* upcast(Workload_Using_Resolve* workload) {return &workload->base;}
Workload_Base* upcast(Workload_Module_Analysis* workload) {return &workload->base;}
Workload_Base* upcast(Workload_Function_Header* workload) {return &workload->base;}
Workload_Base* upcast(Workload_Function_Parameter* workload) {return &workload->base;}
Workload_Base* upcast(Workload_Function_Body* workload) {return &workload->base;}
Workload_Base* upcast(Workload_Function_Cluster_Compile* workload) {return &workload->base;}
Workload_Base* upcast(Workload_Struct_Analysis* workload) {return &workload->base;}
Workload_Base* upcast(Workload_Struct_Reachable_Resolve* workload) {return &workload->base;}
Workload_Base* upcast(Workload_Definition* workload) {return &workload->base;}
Workload_Base* upcast(Workload_Bake_Analysis* workload) {return &workload->base;}
Workload_Base* upcast(Workload_Bake_Execution* workload) {return &workload->base;}
Workload_Base* upcast(Workload_Event* workload) {return &workload->base;}

template <typename T>
T* downcast(Workload_Base* workload) {
    T* result = (T*)workload;
    assert(workload->type == Helpers::get_workload_type(result), "Invalid cast");
    return result;
}



Type_Signature* hardcoded_type_to_signature(Hardcoded_Type type)
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

Definition_Info* pass_get_node_info(Analysis_Pass* pass, AST::Definition* node, Info_Query query) {
    return &pass_get_base_info(pass, AST::upcast(node), query)->definition_info;
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

Definition_Info* get_info(AST::Definition* definition, bool create = false) {
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

void semantic_analyser_log_error(Semantic_Error_Type type, AST::Node* node) {
    Semantic_Error error;
    error.type = type;
    error.error_node = node;
    error.information = dynamic_array_create_empty<Error_Information>(2);
    dynamic_array_push_back(&semantic_analyser.errors, error);
    semantic_analyser_set_error_flag(false);
}

void semantic_analyser_log_error(Semantic_Error_Type type, AST::Expression* node) {
    semantic_analyser_log_error(type, AST::upcast(node));
}

void semantic_analyser_log_error(Semantic_Error_Type type, AST::Statement* node) {
    semantic_analyser_log_error(type, AST::upcast(node));
}

void semantic_analyser_add_error_info(Error_Information info) {
    auto& errors = semantic_analyser.errors;
    assert(errors.size != 0, "");
    Semantic_Error* last_error = &errors[errors.size - 1];
    dynamic_array_push_back(&last_error->information, info);
}

Error_Information error_information_make_empty(Error_Information_Type type) {
    Error_Information info;
    info.type = type;
    return info;
}

Error_Information error_information_make_text(const char* text) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXTRA_TEXT);
    info.options.extra_text = text;
    return info;
}

Error_Information error_information_make_argument_count(int given_argument_count, int expected_argument_count) {
    Error_Information info = error_information_make_empty(Error_Information_Type::ARGUMENT_COUNT);
    info.options.invalid_argument_count.expected = expected_argument_count;
    info.options.invalid_argument_count.given = given_argument_count;
    return info;
}

Error_Information error_information_make_invalid_member(Type_Signature* struct_signature, String* id) {
    Error_Information info = error_information_make_empty(Error_Information_Type::INVALID_MEMBER);
    info.options.invalid_member.member_id = id;
    info.options.invalid_member.struct_signature = struct_signature;
    return info;
}

Error_Information error_information_make_id(String* id) {
    assert(id != 0, "");
    Error_Information info = error_information_make_empty(Error_Information_Type::ID);
    info.options.id = id;
    return info;
}

Error_Information error_information_make_symbol(Symbol* symbol) {
    Error_Information info = error_information_make_empty(Error_Information_Type::SYMBOL);
    info.options.symbol = symbol;
    return info;
}

Error_Information error_information_make_exit_code(Exit_Code code) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXIT_CODE);
    info.options.exit_code = code;
    return info;
}

Error_Information error_information_make_given_type(Type_Signature* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::GIVEN_TYPE);
    info.options.type = type;
    return info;
}

Error_Information error_information_make_expected_type(Type_Signature* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXPECTED_TYPE);
    info.options.type = type;
    return info;
}

Error_Information error_information_make_function_type(Type_Signature* type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::FUNCTION_TYPE);
    info.options.type = type;
    return info;
}

Error_Information error_information_make_binary_op_type(Type_Signature* left_type, Type_Signature* right_type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::BINARY_OP_TYPES);
    info.options.binary_op_types.left_type = left_type;
    info.options.binary_op_types.right_type = right_type;
    return info;
}

Error_Information error_information_make_expression_result_type(Expression_Result_Type result_type) {
    Error_Information info = error_information_make_empty(Error_Information_Type::EXPRESSION_RESULT_TYPE);
    info.options.expression_type = result_type;
    return info;
}

Error_Information error_information_make_constant_status(Constant_Status status) {
    Error_Information info = error_information_make_empty(Error_Information_Type::CONSTANT_STATUS);
    info.options.constant_status = status;
    return info;
}

Error_Information error_information_make_cycle_workload(Workload_Base* workload) {
    Error_Information info = error_information_make_empty(Error_Information_Type::CYCLE_WORKLOAD);
    info.options.cycle_workload = workload;
    return info;
}



// Polymorphic
void polymorphic_base_destroy(Polymorphic_Base* base) {
    for (int i = 0; i < base->instances.size; i++) {
        auto instance = base->instances[i];
        array_destroy(&instance->parameter_values);
        delete instance;
    }
    dynamic_array_destroy(&base->instances);
    delete base;
}

 // Returns null if we reached instanciation depth
Polymorphic_Instance* polymorphic_base_make_instance_empty(Polymorphic_Base* base, AST::Node* instanciation_node) {
    // Check instance depth
    int instanciation_counter = 0;
    Polymorphic_Instance* recursive_base = 0; // The initial instance that lead to the creation of this instance, may be 0
    {
        ModTree_Function* current_function = semantic_analyser.current_workload->current_function;
        if (current_function != 0) 
        {
            recursive_base = current_function->progress->poly_instance;
            if (recursive_base != 0) {
                if (recursive_base->root_instance != 0) {
                    recursive_base = recursive_base->root_instance;
                }
                instanciation_counter = recursive_base->instances_generated;
                recursive_base->instances_generated += 1;
            }
        }

        // Stop recursive instanciates at some threshold
        if (instanciation_counter > 10) {
            semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, instanciation_node);
            semantic_analyser_add_error_info(error_information_make_text("Polymorphic function instanciation reached depth limit 10!"));
            // TODO: Add more error information, e.g. finding a cycle or printing the mother instances!

            // Also set all calling functions to contain errors (Somewhat important so that no more instances of the base function are created!)
            recursive_base->progress->function->contains_errors = true;
            return 0;
        }
    }

    Polymorphic_Instance* instance = new Polymorphic_Instance;
    instance->base = base;
    instance->instances_generated = 0;
    instance->root_instance = recursive_base;
    instance->instance_index = -1;
    instance->parameter_values = array_create_empty<Polymorphic_Value>(base->parameter_count);
    instance->progress = 0;
    return instance;
}

void polymorpic_instance_destroy_empty(Polymorphic_Instance* instance) {
    assert(instance->instance_index == -1, "");
    array_destroy(&instance->parameter_values);
    delete instance;
}

// Returns base instance, which needs to be filled out with base values...
Polymorphic_Base* polymorphic_base_create(Function_Progress* progress, int parameter_count)
{
    // Create base
    Polymorphic_Base* base = new Polymorphic_Base;
    dynamic_array_push_back(&semantic_analyser.polymorphic_functions, base);
    base->instances = dynamic_array_create_empty<Polymorphic_Instance*>(1);
    base->progress = progress;
    base->parameter_count = parameter_count;

    // Create first instance
    auto instance = polymorphic_base_make_instance_empty(base, upcast(progress->header_workload->function_node));
    for (int i = 0; i < instance->parameter_values.size; i++) {
        instance->parameter_values[i].is_not_set = true;
    }
    instance->instance_index = 0;
    instance->progress = progress;
    instance->root_instance = 0;
    instance->instances_generated = 0;
    dynamic_array_push_back(&base->instances, instance);
    progress->poly_instance = instance;
    progress->function->is_runnable = false; 

    return base;
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
    auto progress = stack_allocator_allocate<T>(&workload_executer.progress_allocator);
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
    if (semantic_analyser.current_workload != 0 && semantic_analyser.current_workload->current_symbol_table != 0) {
        workload->parent_table = semantic_analyser.current_workload->current_symbol_table;
    }

    // Add to workload queue
    dynamic_array_push_back(&executer.all_workloads, workload);
    dynamic_array_push_back(&executer.runnable_workloads, workload); // Note: There exists a check for dependencies before executing runnable workloads, so this is ok

    return result;
}

Function_Progress* function_progress_create(Symbol* symbol, AST::Expression* function_node)
{
    assert(function_node->type == AST::Expression_Type::FUNCTION, "Has to be function!");
    // Create progress
    auto progress = analysis_progress_allocate_internal<Function_Progress>();
    progress->function = modtree_function_create_empty(
        0, symbol, progress, 
        symbol_table_create_with_parent(semantic_analyser.current_workload->current_symbol_table, false)
    );
    progress->poly_instance = 0;

    // Set Symbol info
    if (symbol != 0) {
        symbol->type = Symbol_Type::FUNCTION;
        symbol->options.function = progress;
    }

    // Add workloads
    progress->header_workload = workload_executer_allocate_workload<Workload_Function_Header>(upcast(function_node));
    auto header_workload = progress->header_workload;

    header_workload->progress = progress;
    header_workload->function_node = function_node;
    header_workload->parameter_order = dynamic_array_create_empty<Workload_Function_Parameter*>(1);
    header_workload->base.parent_table = progress->function->parameter_table;

    progress->body_workload = workload_executer_allocate_workload<Workload_Function_Body>(upcast(function_node->options.function.body));
    progress->body_workload->body_node = function_node->options.function.body;
    progress->body_workload->progress = progress;
    progress->body_workload->base.parent_table = progress->function->parameter_table; // Sets correct symbol table for code
    progress->function->code_workload = upcast(progress->body_workload);

    progress->compile_workload = workload_executer_allocate_workload<Workload_Function_Cluster_Compile>(0);
    progress->compile_workload->functions = dynamic_array_create_empty<ModTree_Function*>(1);
    dynamic_array_push_back(&progress->compile_workload->functions, progress->function);
    progress->compile_workload->progress = progress;

    // Add dependencies between workloads
    analysis_workload_add_dependency_internal(upcast(progress->body_workload), upcast(progress->header_workload), 0);
    analysis_workload_add_dependency_internal(upcast(progress->compile_workload), upcast(progress->body_workload), 0);

    return progress;
}

Function_Progress* function_progress_create_polymorphic_instance(Polymorphic_Instance* empty_instance, Expression_Info* instance_expr_info, Analysis_Pass* header_pass)
{
    // Check if we have already instanciated the function with given parameters
    {
        auto base = empty_instance->base;
        for (int i = 0; i < base->instances.size; i++) {
            auto instance = base->instances[i];
            bool all_matching = true;
            for (int j = 0; j < instance->parameter_values.size; j++) {
                auto& inst_param = instance->parameter_values[j];
                auto& current_param = empty_instance->parameter_values[j];
                if (inst_param.is_not_set != current_param.is_not_set) {
                    all_matching = false;
                    break;
                }
                if (!inst_param.is_not_set) {
                    if (!constant_pool_compare_constants(&compiler.constant_pool, inst_param.constant, current_param.constant)) {
                        all_matching = false;
                        break;
                    }
                }
            }

            if (all_matching) {
                // Use given instance instead of creating a new one, destroy empty instance
                array_destroy(&empty_instance->parameter_values);
                delete empty_instance;

                // Set expression info to instanciation, so that ir-generator can pick the corresponding modtree-function
                // logg("Found matching instance %d, not re-instanciating!\n", i);
                assert(instance_expr_info->options.polymorphic.base == instance->base, "Base must match!");
                instance_expr_info->options.polymorphic.instance = instance;
                return instance->progress;
            }
        }
    }

    // Promote empty instance to actual instance
    empty_instance->instance_index = empty_instance->base->instances.size;
    dynamic_array_push_back(&empty_instance->base->instances, empty_instance);
    instance_expr_info->options.polymorphic.instance = empty_instance;

    // Re-Analyse base function signature (Now with known polymorphic values) to get non-polymorphic instance signature
    Type_Signature* function_signature = 0;
    {
        RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->current_polymorphic_values, empty_instance->parameter_values);
        RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->current_pass, header_pass);
        RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->current_symbol_table, empty_instance->base->progress->function->parameter_table);
        auto& signature_node = empty_instance->base->progress->header_workload->function_node->options.function.signature->options.function_signature;

        Type_Signature unfinished = type_system_make_function_empty(&compiler.type_system);
        for (int i = 0; i < signature_node.parameters.size; i++)
        {
            auto param = signature_node.parameters[i];
            if (param->is_comptime) {
                continue;
            }
            empty_function_add_parameter(&unfinished, param->name, semantic_analyser_analyse_expression_type(param->type));
        }
        auto& types = compiler.type_system.predefined_types;
        Type_Signature* return_type = types.void_type;
        if (signature_node.return_value.available) {
            return_type = semantic_analyser_analyse_expression_type(signature_node.return_value.value);
        }
        function_signature = empty_function_finish(&compiler.type_system, unfinished, return_type);
    }

    // Create new progress
    auto base_progress = empty_instance->base->progress;
    auto progress = analysis_progress_allocate_internal<Function_Progress>();
    empty_instance->progress = progress;
    progress->poly_instance = empty_instance;
    progress->function = modtree_function_create_empty(function_signature, base_progress->function->symbol, progress, base_progress->function->parameter_table);

    // Add workloads
    progress->header_workload = base_progress->header_workload; // NOTE: Not sure if we want to link base header workload here?

    progress->body_workload = workload_executer_allocate_workload<Workload_Function_Body>(upcast(base_progress->body_workload->body_node));
    progress->body_workload->body_node = base_progress->body_workload->body_node;
    progress->body_workload->progress = progress;
    progress->body_workload->base.parent_table = base_progress->function->parameter_table;
    progress->body_workload->base.current_polymorphic_values = empty_instance->parameter_values;
    progress->function->code_workload = upcast(progress->body_workload);

    progress->compile_workload = workload_executer_allocate_workload<Workload_Function_Cluster_Compile>(0);
    progress->compile_workload->functions = dynamic_array_create_empty<ModTree_Function*>(1);
    dynamic_array_push_back(&progress->compile_workload->functions, progress->function);
    progress->compile_workload->progress = progress;

    // Add dependencies between workloads
    // NOTE: The instance body waits on the base compile-workload, because errors which are caused by recursive instanciations require special handling
    analysis_workload_add_dependency_internal(upcast(progress->body_workload), upcast(base_progress->compile_workload), 0); 
    analysis_workload_add_dependency_internal(upcast(progress->compile_workload), upcast(progress->body_workload), 0);

    return progress;
}

Struct_Progress* struct_progress_create(Symbol* symbol, AST::Expression* struct_node)
{
    assert(struct_node->type == AST::Expression_Type::STRUCTURE_TYPE, "Has to be struct!");
    auto& struct_info = struct_node->options.structure;

    // Create progress
    auto progress = analysis_progress_allocate_internal<Struct_Progress>();
    progress->struct_type = type_system_make_struct_empty(&compiler.type_system, symbol, struct_info.type, progress);

    // Set Symbol
    if (symbol != 0) {
        symbol->type = Symbol_Type::TYPE;
        symbol->options.type = progress->struct_type;
    }

    // Add workloads
    progress->analysis_workload = workload_executer_allocate_workload<Workload_Struct_Analysis>(upcast(struct_node));
    progress->analysis_workload->progress = progress;
    progress->analysis_workload->dependency_type = Dependency_Type::MEMBER_IN_MEMORY;
    progress->analysis_workload->struct_node = struct_node;

    progress->reachable_resolve_workload = workload_executer_allocate_workload<Workload_Struct_Reachable_Resolve>(0);
    progress->reachable_resolve_workload->progress = progress;
    progress->reachable_resolve_workload->unfinished_array_types = dynamic_array_create_empty<Type_Signature*>(1);
    progress->reachable_resolve_workload->struct_types = dynamic_array_create_empty<Type_Signature*>(1);
    dynamic_array_push_back(&progress->reachable_resolve_workload->struct_types, progress->struct_type);

    // Add dependencies between workloads
    analysis_workload_add_dependency_internal(upcast(progress->reachable_resolve_workload), upcast(progress->analysis_workload), 0);

    return progress;
}

Bake_Progress* bake_progress_create(AST::Expression* bake_expr)
{
    assert(bake_expr->type == AST::Expression_Type::BAKE_EXPR ||
        bake_expr->type == AST::Expression_Type::BAKE_BLOCK, "Must be bake!");

    auto progress = analysis_progress_allocate_internal<Bake_Progress>();
    progress->bake_function = modtree_function_create_empty(
        type_system_make_function(&compiler.type_system, {}, compiler.type_system.predefined_types.void_type), 0, 0, 0
    );
    progress->result = comptime_result_make_not_comptime();

    // Create workloads
    progress->analysis_workload = workload_executer_allocate_workload<Workload_Bake_Analysis>(upcast(bake_expr));
    progress->analysis_workload->bake_node = bake_expr;
    progress->analysis_workload->progress = progress;
    progress->bake_function->code_workload = upcast(progress->analysis_workload);

    progress->execute_workload = workload_executer_allocate_workload<Workload_Bake_Execution>(0);
    progress->execute_workload->bake_node = bake_expr;
    progress->execute_workload->progress = progress;

    // Add dependencies between workloads
    analysis_workload_add_dependency_internal(upcast(progress->execute_workload), upcast(progress->analysis_workload), 0);

    return progress;
}

Module_Progress* module_progress_create(AST::Module* module, Symbol* symbol) {
    // Create progress
    auto progress = analysis_progress_allocate_internal<Module_Progress>();
    progress->symbol = symbol;

    // Create analysis workload
    progress->module_analysis = workload_executer_allocate_workload<Workload_Module_Analysis>(upcast(module));
    {
        auto analysis = progress->module_analysis;
        analysis->module_node = module;
        analysis->symbol_table = 0;
        analysis->last_import_workload = 0;
        analysis->base.parent_table = semantic_analyser.root_symbol_table; 
        analysis->progress = progress;
        analysis->parent_analysis = 0;
        if (semantic_analyser.current_workload != 0 && semantic_analyser.current_workload->current_symbol_table != 0) {
            analysis->base.parent_table = semantic_analyser.current_workload->current_symbol_table;
        }
    }

    // Create event workload
    progress->event_symbol_table_ready = workload_executer_allocate_workload<Workload_Event>(0);
    progress->event_symbol_table_ready->description = "Symbol table ready event";
    analysis_workload_add_dependency_internal(upcast(progress->event_symbol_table_ready), upcast(progress->module_analysis), 0);

    return progress;
}


// Create correct workloads for comptime definitions, for non-comptime checks if its a variable or a global and sets the symbol correctly
Workload_Base* analyser_create_symbol_and_workload_for_definition(AST::Definition* definition)
{
    Symbol_Table* current_table = semantic_analyser.current_workload->current_symbol_table;
    Symbol* symbol = symbol_table_define_symbol(current_table, definition->name, Symbol_Type::DEFINITION_UNFINISHED, AST::upcast(definition), false);
    get_info(definition, true)->symbol = symbol; // Set definition symbol (Editor information)

    // Create workload for functions, structs and modules directly
    Workload_Base* result = 0;
    bool create_definition_workload = true; // If not function/struct/module and not a variable
    if (definition->is_comptime) {
        // Check if it's a 'named' construct (function, struct, module)
        if (definition->value.available)
        {
            auto value = definition->value.value;
            bool type_valid_for_definition = false;
            create_definition_workload = false;
            switch (value->type)
            {
            case AST::Expression_Type::MODULE: {
                symbol->type = Symbol_Type::MODULE;
                symbol->options.module_progress = module_progress_create(value->options.module, symbol);
                result = upcast(symbol->options.module_progress->module_analysis);
                break;
            }
            case AST::Expression_Type::FUNCTION: {
                result = upcast(function_progress_create(symbol, value)->header_workload);
                break;
            }
            case AST::Expression_Type::STRUCTURE_TYPE: {
                result = upcast(struct_progress_create(symbol, value)->analysis_workload);
                break;
            }
            default: {
                create_definition_workload = true;
                type_valid_for_definition = true;
                break;
            }
            }

            if (!type_valid_for_definition && definition->type.available) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, definition->type.value);
                semantic_analyser_add_error_info(error_information_make_text("Type is not valid for comptime definitons of structs/functions/modules!"));
            }
        }
    }
    else if (definition->base.parent->type == AST::Node_Type::STATEMENT) { // Here we know that it is a Variable
        symbol->type = Symbol_Type::VARIABLE_UNDEFINED;
        symbol->internal = true;
        create_definition_workload = false;
    }

    if (create_definition_workload) {
        auto definition_workload = workload_executer_allocate_workload<Workload_Definition>(upcast(definition));
        definition_workload->definition_node = definition;
        definition_workload->symbol = symbol;
        symbol->options.definition_workload = definition_workload;
        result = upcast(definition_workload);
    }
    return result;
}



// MOD_TREE (Note: Doesn't set symbol to anything!)
ModTree_Function* modtree_function_create_empty(Type_Signature* signature, Symbol* symbol, Function_Progress* progress, Symbol_Table* param_table)
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

ModTree_Global* modtree_program_add_global(Type_Signature* type)
{
    auto global = new ModTree_Global;
    global->type = type;
    global->has_initial_value = false;
    global->init_expr = 0;
    global->index = semantic_analyser.program->globals.size;
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
Comptime_Result comptime_result_make_available(void* data, Type_Signature* type) {
    Comptime_Result result;
    result.type = Comptime_Result_Type::AVAILABLE;
    result.data = data;
    result.data_type = type;
    return result;
}

Comptime_Result comptime_result_make_unavailable(Type_Signature* type) {
    Comptime_Result result;
    result.type = Comptime_Result_Type::UNAVAILABLE;
    result.data_type = type;
    result.data = 0;
    return result;
}

Comptime_Result comptime_result_make_not_comptime() {
    Comptime_Result result;
    result.type = Comptime_Result_Type::NOT_COMPTIME;
    result.data = 0;
    result.data_type = 0;
    return result;
}

Comptime_Result comptime_result_apply_cast(Comptime_Result value, Info_Cast_Type cast_type, Type_Signature* result_type)
{
    auto& analyser = semantic_analyser;
    if (value.type == Comptime_Result_Type::NOT_COMPTIME) {
        return comptime_result_make_not_comptime();
    }
    else if (value.type == Comptime_Result_Type::UNAVAILABLE) {
        return comptime_result_make_unavailable(result_type);
    }

    Instruction_Type instr_type = (Instruction_Type)-1;
    switch (cast_type)
    {
    case Info_Cast_Type::INVALID: return comptime_result_make_unavailable(result_type); // Invalid means an error was already logged
    case Info_Cast_Type::NO_CAST: return value;
    case Info_Cast_Type::FLOATS: instr_type = Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE; break;
    case Info_Cast_Type::FLOAT_TO_INT: instr_type = Instruction_Type::CAST_FLOAT_INTEGER; break;
    case Info_Cast_Type::INT_TO_FLOAT: instr_type = Instruction_Type::CAST_INTEGER_FLOAT; break;
    case Info_Cast_Type::INTEGERS: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
    case Info_Cast_Type::ENUM_TO_INT: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
    case Info_Cast_Type::INT_TO_ENUM: instr_type = Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE; break;
    case Info_Cast_Type::POINTERS: return comptime_result_make_not_comptime();
    case Info_Cast_Type::POINTER_TO_U64: return comptime_result_make_not_comptime();
    case Info_Cast_Type::U64_TO_POINTER: return comptime_result_make_not_comptime();
    case Info_Cast_Type::ARRAY_TO_SLICE: {
        Upp_Slice_Base* slice = stack_allocator_allocate<Upp_Slice_Base>(&analyser.allocator_values);
        slice->data_ptr = value.data;
        slice->size = value.data_type->options.array.element_count;
        return comptime_result_make_available(slice, result_type);
    }
    case Info_Cast_Type::TO_ANY: {
        Upp_Any* any = stack_allocator_allocate<Upp_Any>(&analyser.allocator_values);
        any->type = result_type->internal_index;
        any->data = value.data;
        return comptime_result_make_available(any, result_type);
    }
    case Info_Cast_Type::FROM_ANY: {
        Upp_Any* given = (Upp_Any*)value.data;
        if (given->type >= compiler.type_system.types.size) {
            return comptime_result_make_not_comptime();
        }
        Type_Signature* any_type = compiler.type_system.types[given->type];
        if (!type_signature_equals(any_type, value.data_type)) {
            return comptime_result_make_not_comptime();
        }
        return comptime_result_make_available(given->data, any_type);
    }
    default: panic("");
    }
    if ((int)instr_type == -1) panic("");

    void* result_buffer = stack_allocator_allocate_size(&analyser.allocator_values, result_type->size, result_type->alignment);
    bytecode_execute_cast_instr(
        instr_type, result_buffer, value.data,
        type_signature_to_bytecode_type(result_type),
        type_signature_to_bytecode_type(value.data_type)
    );
    return comptime_result_make_available(result_buffer, result_type);
}

Comptime_Result expression_calculate_comptime_value_without_context_cast(AST::Expression* expr)
{
    auto& analyser = semantic_analyser;
    Predefined_Types& types = compiler.type_system.predefined_types;

    auto info = get_info(expr);
    if (info->contains_errors) {
        return comptime_result_make_unavailable(expression_info_get_type(info));
    }

    switch (info->result_type)
    {
    case Expression_Result_Type::CONSTANT: {
        auto& upp_const = info->options.constant;
        return comptime_result_make_available(&compiler.constant_pool.buffer[upp_const.offset], upp_const.type);
    }
    case Expression_Result_Type::TYPE: {
        return comptime_result_make_available(&info->options.type->internal_index, types.type_type);
    }
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::FUNCTION: // TODO: Function pointer reads should work in the future
    case Expression_Result_Type::HARDCODED_FUNCTION: {
        return comptime_result_make_not_comptime();
    }
    case Expression_Result_Type::VALUE:
        break; // Rest of function
    default:panic("");
    }

    auto result_type = expression_info_get_type(info);
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
        return comptime_result_make_unavailable(types.unknown_type);
    }
    case AST::Expression_Type::PATH_LOOKUP: {
        auto symbol = get_info(expr->options.path_lookup)->symbol;
        if (symbol->type == Symbol_Type::COMPTIME_VALUE) {
            auto& upp_const = symbol->options.constant;
            return comptime_result_make_available(&compiler.constant_pool.buffer[upp_const.offset], upp_const.type);
        }
        else if (symbol->type == Symbol_Type::PARAMETER && symbol->options.parameter.is_polymorphic) 
        {
            auto& param = symbol->options.parameter;
            if (analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_PARAMETER ||
                analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_HEADER) 
            {
                // This means we are currently analysing the same header, e.g. the parameter cannot be set yet
                return comptime_result_make_unavailable(param.workload->base_type);
            }
            else if (analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_BODY) {
                // Check if the value is set (e.g. if we are in the base-body or in an instance-body)
                const auto& poly_value = analyser.current_workload->current_polymorphic_values[param.workload->execution_order_index];
                if (poly_value.is_not_set) { // Is the case if we are in the body analysis of the base-function
                    return comptime_result_make_unavailable(param.workload->base_type);
                }
                return comptime_result_make_available(&compiler.constant_pool.buffer[poly_value.constant.offset], poly_value.constant.type);
            }
            else {
                panic("In which hellish landscape are we where we access parameters outside of body/header analysis");
            }
            panic("Must not happen");
            return comptime_result_make_unavailable(types.unknown_type);
        }
        else if (symbol->type == Symbol_Type::ERROR_SYMBOL) {
            return comptime_result_make_unavailable(types.unknown_type);
        }
        return comptime_result_make_not_comptime();
    }
    case AST::Expression_Type::BINARY_OPERATION:
    {
        Comptime_Result left_val = expression_calculate_comptime_value(expr->options.binop.left);
        Comptime_Result right_val = expression_calculate_comptime_value(expr->options.binop.right);
        if (left_val.type != Comptime_Result_Type::AVAILABLE || right_val.type != Comptime_Result_Type::AVAILABLE) {
            if (left_val.type == Comptime_Result_Type::NOT_COMPTIME && right_val.type != Comptime_Result_Type::NOT_COMPTIME) {
                return comptime_result_make_not_comptime();
            }
            else {
                return comptime_result_make_unavailable(result_type);
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
        case AST::Binop::INVALID: return comptime_result_make_unavailable(result_type);
        default: panic("");
        }

        void* result_buffer = stack_allocator_allocate_size(&analyser.allocator_values, result_type->size, result_type->alignment);
        if (bytecode_execute_binary_instr(instr_type, type_signature_to_bytecode_type(left_val.data_type), result_buffer, left_val.data, right_val.data)) {
            return comptime_result_make_available(result_buffer, result_type);
        }
        else {
            return comptime_result_make_not_comptime();
        }
        break;
    }
    case AST::Expression_Type::UNARY_OPERATION:
    {
        Comptime_Result value = expression_calculate_comptime_value(expr->options.unop.expr);
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
            return comptime_result_make_not_comptime();
        case AST::Unop::DEREFERENCE:
            return comptime_result_make_not_comptime();
        default: panic("");
        }

        if (value.type == Comptime_Result_Type::NOT_COMPTIME) {
            return comptime_result_make_not_comptime();
        }
        else if (value.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(result_type);
        }

        void* result_buffer = stack_allocator_allocate_size(&analyser.allocator_values, result_type->size, result_type->alignment);
        bytecode_execute_unary_instr(instr_type, type_signature_to_bytecode_type(value.data_type), result_buffer, value.data);
        return comptime_result_make_available(result_buffer, result_type);
    }
    case AST::Expression_Type::CAST: {
        auto operand = expression_calculate_comptime_value(expr->options.cast.operand);
        return comptime_result_apply_cast(operand, info->specifics.cast_type, result_type);
    }
    case AST::Expression_Type::ARRAY_ACCESS:
    {
        Type_Signature* element_type = result_type;
        Comptime_Result value_array = expression_calculate_comptime_value(expr->options.array_access.array_expr);
        Comptime_Result value_index = expression_calculate_comptime_value(expr->options.array_access.index_expr);
        if (value_array.type == Comptime_Result_Type::NOT_COMPTIME || value_index.type == Comptime_Result_Type::NOT_COMPTIME) {
            return comptime_result_make_not_comptime();
        }
        else if (value_array.type == Comptime_Result_Type::UNAVAILABLE || value_index.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(result_type);
        }
        assert(type_signature_equals(value_index.data_type, types.i32_type), "Must be i32 currently");

        byte* base_ptr = 0;
        int array_size = 0;
        if (value_array.data_type->type == Signature_Type::ARRAY) {
            base_ptr = (byte*)value_array.data;
            array_size = value_array.data_type->options.array.element_count;
        }
        else if (value_array.data_type->type == Signature_Type::SLICE) {
            Upp_Slice_Base* slice = (Upp_Slice_Base*)value_array.data;
            base_ptr = (byte*)slice->data_ptr;
            array_size = slice->size;
        }
        else {
            panic("");
        }

        int index = *(int*)value_index.data;
        int element_offset = index * element_type->size;
        if (index >= array_size) {
            return comptime_result_make_not_comptime();
        }
        if (!memory_is_readable(base_ptr, element_type->size)) {
            return comptime_result_make_not_comptime();
        }
        return comptime_result_make_available(&base_ptr[element_offset], element_type);
    }
    case AST::Expression_Type::MEMBER_ACCESS: {
        Comptime_Result value_struct = expression_calculate_comptime_value(expr->options.member_access.expr);
        if (value_struct.type == Comptime_Result_Type::NOT_COMPTIME) {
            return comptime_result_make_not_comptime();
        }
        else if (value_struct.type == Comptime_Result_Type::UNAVAILABLE) {
            return comptime_result_make_unavailable(result_type);
        }
        auto member = type_signature_find_member_by_id(value_struct.data_type, expr->options.member_access.name);
        assert(member.available, "I think after analysis this member should exist");
        byte* raw_data = (byte*)value_struct.data;
        return comptime_result_make_available(&raw_data[member.value.offset], result_type);
    }
    case AST::Expression_Type::FUNCTION_CALL: {
        return comptime_result_make_not_comptime();
    }
    case AST::Expression_Type::NEW_EXPR: {
        return comptime_result_make_not_comptime(); // New is always uninitialized, so it cannot have a comptime value (Future: Maybe new with values)
    }
    case AST::Expression_Type::ARRAY_INITIALIZER:
    {
        return comptime_result_make_not_comptime();
        // NOTE: Maybe this works in the future, but it dependes if we can always finish the struct size after analysis
        /*
        Type_Signature* element_type = result_type->options.array.element_type;
        if (element_type->size == 0 && element_type->alignment == 0) {
            return comptime_analysis_make_error();
        }
        void* result_buffer = stack_allocator_allocate_size(&analyser->allocator_values, result_type->size, result_type->alignment);
        for (int i = 0; i < expr->options.array_initializer.size; i++)
        {
            ModTree_Expression* element_expr = expr->options.array_initializer[i];
            Comptime_Analysis value_element = expression_calculate_comptime_value(analyser, element_expr);
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
        return comptime_result_make_not_comptime();
        /*
        Type_Signature* struct_type = result_type->options.array.element_type;
        void* result_buffer = stack_allocator_allocate_size(&analyser->allocator_values, result_type->size, result_type->alignment);
        for (int i = 0; i < expr->options.struct_initializer.size; i++)
        {
            Member_Initializer* initializer = &expr->options.struct_initializer[i];
            Comptime_Analysis value_member = expression_calculate_comptime_value(analyser, initializer->init_expr);
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
    return comptime_result_make_not_comptime();
}

Comptime_Result expression_calculate_comptime_value(AST::Expression* expr)
{
    auto result_no_context = expression_calculate_comptime_value_without_context_cast(expr);
    if (result_no_context.type != Comptime_Result_Type::AVAILABLE) {
        return result_no_context;
    }

    auto info = get_info(expr);
    if (info->context_ops.deref_count != 0 || info->context_ops.take_address_of) {
        // Cannot handle pointers for comptime currently
        return comptime_result_make_not_comptime();
    }

    return comptime_result_apply_cast(result_no_context, info->context_ops.cast, info->context_ops.after_cast_type);
}

bool expression_has_memory_address(AST::Expression* expr)
{
    auto info = get_info(expr);
    auto type = info->context_ops.after_cast_type;
    if (type->size == 0 && type->alignment != 0) return false; // I forgot if this case has any real use cases currently

    switch (info->result_type)
    {
    case Expression_Result_Type::FUNCTION:
    case Expression_Result_Type::HARDCODED_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::TYPE:
    case Expression_Result_Type::CONSTANT: // Constant memory must not be written to. (e.g. 5 = 10)
        return false;
    case Expression_Result_Type::VALUE:
        break;
    default:panic("");
    }

    if (info->context_ops.cast == Info_Cast_Type::FROM_ANY) {
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
        return info->specifics.cast_type == Info_Cast_Type::FROM_ANY;
    case AST::Expression_Type::ERROR_EXPR:
        // Errors shouldn't generate other errors
        return true;
    }

    // All other are expressions generating temporary results
    return false;
}



// Context
Expression_Context expression_context_make_unknown() {
    Expression_Context context;
    context.type = Expression_Context_Type::UNKNOWN;
    return context;
}

Expression_Context expression_context_make_auto_dereference() {
    Expression_Context context;
    context.type = Expression_Context_Type::AUTO_DEREFERENCE;
    return context;
}

Expression_Context expression_context_make_specific_type(Type_Signature* signature) {
    if (signature->type == Signature_Type::UNKNOWN_TYPE) {
        return expression_context_make_unknown();
    }
    Expression_Context context;
    context.type = Expression_Context_Type::SPECIFIC_TYPE;
    context.signature = signature;
    return context;
}



// Result
void expression_info_set_value(Expression_Info* info, Type_Signature* result_type)
{
    info->result_type = Expression_Result_Type::VALUE;
    info->options.value_type = result_type;
    info->context_ops.after_cast_type = result_type;
}

void expression_info_set_error(Expression_Info* info, Type_Signature* result_type)
{
    info->result_type = Expression_Result_Type::VALUE;
    info->options.value_type = result_type;
    info->context_ops.after_cast_type = result_type;
    info->contains_errors = true;
}

void expression_info_set_function(Expression_Info* info, ModTree_Function* function)
{
    info->result_type = Expression_Result_Type::FUNCTION;
    info->options.function = function;
    info->context_ops.after_cast_type = function->signature;
}

void expression_info_set_hardcoded(Expression_Info* info, Hardcoded_Type hardcoded)
{
    info->result_type = Expression_Result_Type::HARDCODED_FUNCTION;
    info->options.hardcoded = hardcoded;
    info->context_ops.after_cast_type = hardcoded_type_to_signature(hardcoded);
}

void expression_info_set_type(Expression_Info* info, Type_Signature* type)
{
    info->result_type = Expression_Result_Type::TYPE;
    info->options.type = type;
    info->context_ops.after_cast_type = compiler.type_system.predefined_types.type_type;
}

void expression_info_set_constant(Expression_Info* info, Upp_Constant constant) {
    info->result_type = Expression_Result_Type::CONSTANT;
    info->options.constant = constant;
    info->context_ops.after_cast_type = constant.type;
}

void expression_info_set_polymorphic_function(Expression_Info* info, Polymorphic_Base* poly_base) {
    info->result_type = Expression_Result_Type::POLYMORPHIC_FUNCTION;
    info->options.polymorphic.base = poly_base;
    info->options.polymorphic.instance = 0;
    info->context_ops.after_cast_type = compiler.type_system.predefined_types.unknown_type;
}

void expression_info_set_constant(Expression_Info* info, Type_Signature* signature, Array<byte> bytes, AST::Node* error_report_node)
{
    auto& analyser = semantic_analyser;
    Constant_Result result = constant_pool_add_constant(&compiler.constant_pool, signature, bytes);
    if (result.status != Constant_Status::SUCCESS)
    {
        assert(error_report_node != 0, "Error"); // Error report node may only be null if we know that adding the constant cannot fail.
        semantic_analyser_log_error(Semantic_Error_Type::CONSTANT_POOL_ERROR, error_report_node);
        semantic_analyser_add_error_info(error_information_make_constant_status(result.status));
        expression_info_set_error(info, signature);
        return;
    }
    expression_info_set_constant(info, result.constant);
}

void expression_info_set_constant_enum(Expression_Info* info, Type_Signature* enum_type, i32 value) {
    expression_info_set_constant(info, enum_type, array_create_static((byte*)&value, sizeof(i32)), 0);
}

void expression_info_set_constant_i32(Expression_Info* info, i32 value) {
    expression_info_set_constant(info, compiler.type_system.predefined_types.i32_type, array_create_static((byte*)&value, sizeof(i32)), 0);
}

// Returns result type of a value before a cast
Type_Signature* expression_info_get_type(Expression_Info* info)
{
    auto& types = compiler.type_system.predefined_types;
    switch (info->result_type)
    {
    case Expression_Result_Type::CONSTANT: info->options.constant.type;
    case Expression_Result_Type::VALUE: return info->options.value_type;
    case Expression_Result_Type::FUNCTION: return info->options.function->signature;
    case Expression_Result_Type::HARDCODED_FUNCTION: return hardcoded_type_to_signature(info->options.hardcoded);
    case Expression_Result_Type::TYPE: return types.type_type;
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
        if (info->options.polymorphic.instance != 0) {
            return info->options.polymorphic.instance->progress->function->signature;
        }
        else {
            return info->options.polymorphic.instance->base->progress->function->signature;
        }
    default: panic("");
    }
    return types.unknown_type;
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

    workload_executer.progress_allocator = stack_allocator_create_empty(1024);

    workload_executer.progress_was_made = false;
    return &workload_executer;
}

void workload_executer_destroy()
{
    auto& executer = workload_executer;
    stack_allocator_destroy(&executer.progress_allocator);

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
            dynamic_array_destroy(&dep_info->symbol_lookups);
        }
        hashtable_destroy(&executer.workload_dependencies);
    }
}

void analysis_workload_destroy(Workload_Base* workload)
{
    list_destroy(&workload->dependencies);
    list_destroy(&workload->dependents);
    dynamic_array_destroy(&workload->reachable_clusters);
    dynamic_array_destroy(&workload->block_stack);
    if (workload->type == Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE) {
        auto reachable = downcast<Workload_Struct_Reachable_Resolve>(workload);
        dynamic_array_destroy(&reachable->struct_types);
        dynamic_array_destroy(&reachable->unfinished_array_types);
    }
    else if (workload->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE) {
        auto cluster = downcast<Workload_Function_Cluster_Compile>(workload);
        dynamic_array_destroy(&cluster->functions);
    }
    else if (workload->type == Analysis_Workload_Type::FUNCTION_HEADER) {
        dynamic_array_destroy(&downcast<Workload_Function_Header>(workload)->parameter_order);
    }
    delete workload;
}

// This will set the read + the non-resolved symbol paths to error
void path_lookup_set_error_symbol(AST::Path_Lookup* path, Workload_Base* workload)
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

    // Set whole path result to error
    {
        auto info = pass_get_node_info(workload->current_pass, path, Info_Query::TRY_READ);
        if (info == 0) {
            info = pass_get_node_info(workload->current_pass, path, Info_Query::CREATE);
        }
        info->symbol = error_symbol;
    }
}


Symbol* symbol_lookup_resolve(AST::Symbol_Lookup* lookup, Symbol_Table* symbol_table, bool search_parents, bool internals_ok)
{
    auto info = pass_get_node_info(semantic_analyser.current_workload->current_pass, lookup, Info_Query::CREATE_IF_NULL);
    auto error = semantic_analyser.predefined_symbols.error_symbol;

    // Find all symbols with this id
    auto results = dynamic_array_create_empty<Symbol*>(1);
    SCOPE_EXIT(dynamic_array_destroy(&results));
    symbol_table_find_symbol_all(symbol_table, lookup->name, search_parents, internals_ok, lookup, &results);
    if (results.size == 0) {
        semantic_analyser_log_error(Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL, upcast(lookup));
        info->symbol = error;
    }
    else if (results.size == 1) {
        info->symbol = results[0];
    }
    else { // size > 1
        if (internals_ok && results[0]->internal) {
            // So that we can have variable 'overloads', and we found a internal symbol first, we'll take that one
            info->symbol = results[0];
        }
        else {
            semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, upcast(lookup));
            semantic_analyser_add_error_info(error_information_make_text("Multiple results found for this symbol, cannot decided"));
            info->symbol = error;
        }
    }

    // Handled aliases
    if (info->symbol->type == Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL) {
        analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(info->symbol->options.alias_workload), lookup);
        workload_executer_wait_for_dependency_resolution();
        info->symbol = info->symbol->options.alias_workload->alias_for;
        assert(info->symbol->type != Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL, "Chained aliases should never happen here!");
    }

    return info->symbol;
}

// Resolves the whole path (e.g. all nodes in Path)
Symbol* path_lookup_resolve(AST::Path_Lookup* path)
{
    auto& analyser = semantic_analyser;
    auto table = semantic_analyser.current_workload->current_symbol_table;
    auto error = semantic_analyser.predefined_symbols.error_symbol;

    // Resolve path
    for (int i = 0; i < path->parts.size; i++) 
    {
        auto part = path->parts[i];
        // Find symbol of path part
        Symbol* symbol = symbol_lookup_resolve(part, table, i == 0, path->parts.size == 1);
        if (symbol == 0) {
            semantic_analyser_log_error(Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL, upcast(part));
            path_lookup_set_error_symbol(path, semantic_analyser.current_workload);
            return error;
        }
        if (symbol == error) {
            path_lookup_set_error_symbol(path, semantic_analyser.current_workload);
            return error;
        }

        // Check if we are at the end of the path
        if (part == path->last) {
            // Set result of whole path (not indiviual part) to the last symbol
            get_info(path, true)->symbol = symbol;
            return symbol;
        }

        // Check if we can continue
        if (symbol->type == Symbol_Type::MODULE) {
            auto current = semantic_analyser.current_workload->type;
            if (current == Analysis_Workload_Type::USING_RESOLVE) {
                analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(symbol->options.module_progress->module_analysis), part);
            }
            else {
                analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(symbol->options.module_progress->event_symbol_table_ready), part);
            }
            workload_executer_wait_for_dependency_resolution();
            table = symbol->options.module_progress->module_analysis->symbol_table;
        }
        else {
            // Report error and exit
            if (symbol->type == Symbol_Type::DEFINITION_UNFINISHED) {
                // FUTURE: It may be possible that symbol resolution needs to create dependencies itself, which would happen here!
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, upcast(part));
                semantic_analyser_add_error_info(error_information_make_text("Expected module, not a definition (global/comptime)"));
            }
            else {
                semantic_analyser_log_error(Semantic_Error_Type::SYMBOL_EXPECTED_MODUL_IN_IDENTIFIER_PATH, upcast(part));
                semantic_analyser_add_error_info(error_information_make_symbol(symbol));

            }
            path_lookup_set_error_symbol(path, semantic_analyser.current_workload);
            return semantic_analyser.predefined_symbols.error_symbol;
        }
    }

    panic("");
    return 0;
}

void analysis_workload_add_dependency_internal(Workload_Base* workload, Workload_Base* dependency, AST::Symbol_Lookup* lookup)
{
    auto& executer = workload_executer;
    if (dependency->is_finished) return;

    Workload_Pair pair = workload_pair_create(workload, dependency);
    Dependency_Information* infos = hashtable_find_element(&executer.workload_dependencies, pair);
    if (infos == 0) {
        Dependency_Information info;
        info.dependency_node = list_add_at_end(&workload->dependencies, dependency);
        info.dependent_node = list_add_at_end(&dependency->dependents, workload);
        info.symbol_lookups = dynamic_array_create_empty<AST::Symbol_Lookup*>(1);
        info.only_symbol_read_dependency = false;
        if (lookup != 0) {
            info.only_symbol_read_dependency = true;
            dynamic_array_push_back(&info.symbol_lookups, lookup);
        }
        bool inserted = hashtable_insert_element(&executer.workload_dependencies, pair, info);
        assert(inserted, "");
    }
    else {
        if (lookup != 0) {
            dynamic_array_push_back(&infos->symbol_lookups, lookup);
        }
        else {
            infos->only_symbol_read_dependency = false;
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
        new_info.symbol_lookups = info.symbol_lookups; // Note: Takes ownership
        new_info.only_symbol_read_dependency = info.only_symbol_read_dependency;
        hashtable_insert_element(&graph->workload_dependencies, new_pair, new_info);
    }
    else {
        dynamic_array_append_other(&new_infos->symbol_lookups, &info.symbol_lookups);
        if (new_infos->only_symbol_read_dependency) {
            new_infos->only_symbol_read_dependency = info.only_symbol_read_dependency;
        }
        dynamic_array_destroy(&info.symbol_lookups);
    }
}

void workload_executer_remove_dependency(Workload_Base* workload, Workload_Base* depends_on, bool allow_add_to_runnables)
{
    auto graph = &workload_executer;
    Workload_Pair pair = workload_pair_create(workload, depends_on);
    Dependency_Information* info = hashtable_find_element(&graph->workload_dependencies, pair);
    assert(info != 0, "");
    list_remove_node(&workload->dependencies, info->dependency_node);
    list_remove_node(&depends_on->dependents, info->dependent_node);
    dynamic_array_destroy(&info->symbol_lookups);
    bool worked = hashtable_remove_element(&graph->workload_dependencies, pair);
    if (allow_add_to_runnables && workload->dependencies.count == 0) {
        dynamic_array_push_back(&graph->runnable_workloads, workload);
    }
}

Workload_Base* analysis_workload_find_associated_cluster(Workload_Base* workload)
{
    assert(workload->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE || workload->type == Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE, "");
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

void analysis_workload_add_cluster_dependency(Workload_Base* add_to_workload, Workload_Base* dependency, AST::Symbol_Lookup* lookup)
{
    auto graph = &workload_executer;
    assert((add_to_workload->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE && dependency->type == Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE) ||
        (add_to_workload->type == Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE && dependency->type == Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE), "");
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
        analysis_workload_add_dependency_internal(merge_into, merge_from, lookup);
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
                workload_executer_remove_dependency(merge_cluster, merge_dependency, false);
            }
            node = next;
        }
        list_reset(&merge_cluster->dependencies);

        // Merge all analysis item values
        switch (merge_into->type)
        {
        case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
        {
            auto& functions_into = downcast<Workload_Function_Cluster_Compile>(merge_into)->functions;
            auto& functions_from = downcast<Workload_Function_Cluster_Compile>(merge_cluster)->functions;
            dynamic_array_append_other(&functions_into, &functions_from);
            dynamic_array_reset(&functions_from);
            break;
        }
        case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE:
        {
            auto struct_into = downcast<Workload_Struct_Reachable_Resolve>(merge_into);
            auto struct_from = downcast<Workload_Struct_Reachable_Resolve>(merge_cluster);
            dynamic_array_append_other(&struct_into->struct_types, &struct_from->struct_types);
            dynamic_array_reset(&struct_from->struct_types);
            dynamic_array_append_other(&struct_into->unfinished_array_types, &struct_from->unfinished_array_types);
            dynamic_array_reset(&struct_from->unfinished_array_types);
            break;
        }
        default: panic("Clustering only on function clusters and reachable resolve cluster!");
        }

        // Add reachables to merged
        dynamic_array_append_other(&merge_into->reachable_clusters, &merge_cluster->reachable_clusters);
        dynamic_array_reset(&merge_cluster->reachable_clusters);
        analysis_workload_add_dependency_internal(merge_cluster, merge_into, 0);
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

void analysis_workload_add_struct_dependency(Struct_Progress* progress, Struct_Progress* other, Dependency_Type type, AST::Symbol_Lookup* lookup)
{
    auto graph = &workload_executer;
    if (progress->reachable_resolve_workload->base.is_finished || other->reachable_resolve_workload->base.is_finished) return;

    switch (type)
    {
    case Dependency_Type::NORMAL: {
        // Struct member references another struct, but not as a type
        // E.g. Foo :: struct { value: Bar.{...}; }
        analysis_workload_add_dependency_internal(upcast(progress->analysis_workload), upcast(other->reachable_resolve_workload), lookup);
        break;
    }
    case Dependency_Type::MEMBER_IN_MEMORY: {
        // Struct member references other member in memory
        // E.g. Foo :: struct { value: Bar; }
        analysis_workload_add_dependency_internal(upcast(progress->analysis_workload), upcast(other->analysis_workload), lookup);
        analysis_workload_add_cluster_dependency(upcast(progress->reachable_resolve_workload), upcast(other->reachable_resolve_workload), lookup);
        break;
    }
    case Dependency_Type::MEMBER_REFERENCE:
    {
        // Struct member contains some sort of reference to other member
        // E.g. Foo :: struct { value: *Bar; }
        // This means we need to unify the Reachable-Clusters
        analysis_workload_add_cluster_dependency(upcast(progress->reachable_resolve_workload), upcast(other->reachable_resolve_workload), lookup);
        break;
    }
    default: panic("");
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
    double time_per_workload_type[10];
    memory_set_bytes(&time_per_workload_type[0], sizeof(double) * 10, 0);
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
                    workload_executer_remove_dependency(dependent, workload, true);
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
                bool only_reads_was_found = false;
                for (int i = 0; i < workload_cycle.size; i++)
                {
                    Workload_Base* workload = workload_cycle[i];
                    Workload_Base* depends_on = i + 1 == workload_cycle.size ? workload_cycle[0] : workload_cycle[i + 1];
                    Workload_Pair pair = workload_pair_create(workload, depends_on);
                    Dependency_Information infos = *hashtable_find_element(&executer.workload_dependencies, pair);
                    if (infos.only_symbol_read_dependency) {
                        only_reads_was_found = true;
                        for (int j = 0; j < infos.symbol_lookups.size; j++) {
                            path_lookup_set_error_symbol(downcast<AST::Path_Lookup>(infos.symbol_lookups[j]->base.parent), workload);
                            semantic_analyser_log_error(Semantic_Error_Type::CYCLIC_DEPENDENCY_DETECTED, upcast(infos.symbol_lookups[j]));
                            for (int k = 0; k < workload_cycle.size; k++) {
                                Workload_Base* workload = workload_cycle[k];
                                semantic_analyser_add_error_info(error_information_make_cycle_workload(workload));
                            }
                        }
                        workload_executer_remove_dependency(workload, depends_on, true);
                    }
                }
                assert(only_reads_was_found, "");
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
            case Analysis_Workload_Type::BAKE_ANALYSIS: str =             "Bake Analysis   "; break;
            case Analysis_Workload_Type::BAKE_EXECUTION: str =            "Bake Execute    "; break;
            case Analysis_Workload_Type::DEFINITION: str =                "Definition      "; break;
            case Analysis_Workload_Type::MODULE_ANALYSIS: str =           "Module Analysis "; break;
            case Analysis_Workload_Type::USING_RESOLVE: str =             "Using Resolve   "; break;
            case Analysis_Workload_Type::EVENT: str =                     "Event           "; break;

            case Analysis_Workload_Type::FUNCTION_HEADER: str =           "Header          "; break;
            case Analysis_Workload_Type::FUNCTION_PARAMETER: str =        "Parameter       "; break;
            case Analysis_Workload_Type::FUNCTION_BODY: str =             "Body            "; break;
            case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE: str =  "Cluster Compile "; break;

            case Analysis_Workload_Type::STRUCT_ANALYSIS: str =           "Struct Analysis "; break;
            case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE: str =  "Struct Reachable"; break;
            }
            logg("Time in %s %3.4fms\n", str, time_per_workload_type[i] * 1000);
        }
        logg("SUUM:                    %3.4fms\n\n", (end_time-start_time) * 1000);
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

void analysis_workload_entry(void* userdata)
{
    Workload_Base* workload = (Workload_Base*)userdata;
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    analyser.current_workload = workload;

    workload->current_function = 0;
    workload->current_expression = 0;
    workload->current_polymorphic_values.data = 0;
    workload->current_polymorphic_values.size = -1;
    workload->statement_reachable = true;
    workload->current_symbol_table = workload->parent_table;

    switch (workload->type)
    {
    case Analysis_Workload_Type::MODULE_ANALYSIS:
    {
        auto analysis = downcast<Workload_Module_Analysis>(workload);
        auto module_node = analysis->module_node;

        // Create and set symbol table
        analysis->symbol_table = symbol_table_create_with_parent(workload->current_symbol_table, false);
        get_info(module_node, true)->symbol_table = analysis->symbol_table;
        RESTORE_ON_SCOPE_EXIT(workload->current_symbol_table, analysis->symbol_table);

        // Handle Usings
        if (analysis->parent_analysis != 0) {
            analysis->last_import_workload = analysis->parent_analysis->last_import_workload;
            if (analysis->last_import_workload != 0) {
                analysis_workload_add_dependency_internal(
                    upcast(analysis->progress->event_symbol_table_ready),
                    upcast(analysis->last_import_workload),
                    0
                );
            }
        }

        auto last_normal_usings = dynamic_array_create_empty<Workload_Using_Resolve*>(1);
        SCOPE_EXIT(dynamic_array_destroy(&last_normal_usings));
        for (int i = 0; i < module_node->using_nodes.size; i++) 
        {
            auto using_node = module_node->using_nodes[i];

            // Check for general using errors
            if (using_node->type == AST::Using_Type::NORMAL) {
                if (using_node->path->parts.size == 1) {
                    semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, upcast(using_node));
                    semantic_analyser_add_error_info(error_information_make_text("Using must be a path, not a single symbol!"));
                    continue;
                }
            }
            if (using_node->path->last->name->size == 0) {
                // NOTE: This may happen for usage in the Syntax-Editor, look at the parser for more info.
                //       Also i think this is kinda ugly because it's such a special case, but we'll see
                continue;
            }

            // Create workload
            auto using_workload = workload_executer_allocate_workload<Workload_Using_Resolve>(upcast(using_node));
            using_workload->using_node = using_node;
            using_workload->symbol = 0;
            using_workload->alias_for = 0;

            // Add dependencies
            if (analysis->last_import_workload != 0) {
                analysis_workload_add_dependency_internal(upcast(using_workload), upcast(analysis->last_import_workload), 0);
            }
            analysis_workload_add_dependency_internal(upcast(analysis->progress->event_symbol_table_ready), upcast(using_workload), 0);

            if (using_node->type == AST::Using_Type::NORMAL) {
                dynamic_array_push_back(&last_normal_usings, using_workload);
            }
            else {
                for (int i = 0; i < last_normal_usings.size; i++) {
                    analysis_workload_add_dependency_internal(upcast(using_workload), upcast(last_normal_usings[i]), 0);
                }
                dynamic_array_reset(&last_normal_usings);
            }

            // Define symbol if it's a normal using or an alias
            if (using_node->type == AST::Using_Type::NORMAL) {
                using_workload->symbol = symbol_table_define_symbol(
                    workload->current_symbol_table, using_node->path->last->name, Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL, upcast(using_node), false
                );
                using_workload->symbol->options.alias_workload = using_workload;
            }
            else {
                analysis->last_import_workload = using_workload;
            }

            // PREVIOUS LOAD FILE IMPORT
            //auto code = compiler_add_project_import(import);
            //if (code == 0) {
            //    semantic_analyser_log_error(Semantic_Error_Type::OTHERS_COULD_NOT_LOAD_FILE, &module_node->imports[i]->base);
            //    semantic_analyser_add_error_info(error_information_make_text("Could not load file"));
            //    continue;
            //}
            // Note: Currently all imports add their symbols to the root table (Only one 'global' scope)
            // semantic_analyser_do_module_discovery(code->source_parse->root, true);
        }

        // Create workloads for definitions
        for (int i = 0; i < module_node->definitions.size; i++) {
            auto workload = analyser_create_symbol_and_workload_for_definition(module_node->definitions[i]);
            // If I added a new module I need to set myself as the parent
            if (workload != 0 && workload->type == Analysis_Workload_Type::MODULE_ANALYSIS) {
                auto new_progress = downcast<Workload_Module_Analysis>(workload);
                new_progress->parent_analysis = analysis;
            }
            else {
                // All other workloads need to wait until the symbol table is ready!
                analysis_workload_add_dependency_internal(workload, upcast(analysis->progress->event_symbol_table_ready), 0);
            }
        }
        break;
    }
    case Analysis_Workload_Type::EVENT: {
        // INFO: Events are only proxies for empty workloads
        break;
    }
    case Analysis_Workload_Type::USING_RESOLVE:
    {
        auto using_workload = downcast<Workload_Using_Resolve>(workload);
        auto node = using_workload->using_node;

        if (node->type == AST::Using_Type::NORMAL) {
            using_workload->alias_for = path_lookup_resolve(node->path);
        }
        else {
            Symbol* symbol = path_lookup_resolve(node->path); 
            assert(symbol->type != Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL, "Must not happen here");
            if (symbol->type == Symbol_Type::MODULE) 
            {
                auto progress = symbol->options.module_progress;
                // Wait for symbol discovery to finish (Probably not even important)
                analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(progress->module_analysis), node->path->last);
                workload_executer_wait_for_dependency_resolution();

                // If transitive we need to wait now until the last of their usings finish
                auto last_import = progress->module_analysis->last_import_workload;
                if (node->type == AST::Using_Type::SYMBOL_IMPORT_TRANSITIV && last_import != 0 && last_import != using_workload) {
                    analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(progress->module_analysis->last_import_workload), node->path->last);
                    workload_executer_wait_for_dependency_resolution();
                }

                // Refresh symbol after dependency wait
                symbol = get_info(node->path->last)->symbol;
                if (symbol->type != Symbol_Type::ERROR_SYMBOL) {
                    // Add using
                    symbol_table_add_include_table(
                        semantic_analyser.current_workload->current_symbol_table,
                        progress->module_analysis->symbol_table,
                        node->type == AST::Using_Type::SYMBOL_IMPORT_TRANSITIV,
                        false,
                        upcast(node)
                    );
                }
            }
            else if (symbol->type != Symbol_Type::ERROR_SYMBOL) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, upcast(node));
                semantic_analyser_add_error_info(error_information_make_text("Cannot import from non module"));
                semantic_analyser_add_error_info(error_information_make_symbol(symbol));
            }
        }
        break;
    }
    case Analysis_Workload_Type::DEFINITION:
    {
        auto def_workload = downcast<Workload_Definition>(workload);
        auto definition = def_workload->definition_node;
        assert(!(!definition->type.available && !definition->value.available), "Syntax should not allow no type and no definition!");

        Symbol* symbol = def_workload->symbol;

        if (!definition->is_comptime) // Global variable definition
        {
            Type_Signature* type = 0;
            if (definition->type.available) {
                type = semantic_analyser_analyse_expression_type(definition->type.value);
            }
            if (definition->value.available)
            {
                auto value_type = semantic_analyser_analyse_expression_value(
                    definition->value.value, type != 0 ? expression_context_make_specific_type(type) : expression_context_make_unknown()
                );
                if (type == 0) {
                    type = value_type;
                }
            }
            auto global = modtree_program_add_global(type);
            if (definition->value.available) {
                global->has_initial_value = true;
                global->init_expr = definition->value.value;
                global->definition_workload = downcast<Workload_Definition>(workload);
            }

            symbol->type = Symbol_Type::GLOBAL;
            symbol->options.global = global;
        }
        else // Comptime definition
        {
            if (definition->type.available) {
                semantic_analyser_analyse_expression_type(definition->type.value);
                semantic_analyser_log_error(Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_INFERED, definition->type.value);
            }
            if (!definition->value.available) {
                semantic_analyser_log_error(Semantic_Error_Type::COMPTIME_DEFINITION_REQUIRES_INITAL_VALUE, AST::upcast(definition));
                return;
            }

            auto result = semantic_analyser_analyse_expression_any(definition->value.value, expression_context_make_unknown());
            switch (result->result_type)
            {
            case Expression_Result_Type::VALUE:
            {
                Comptime_Result comptime = expression_calculate_comptime_value(definition->value.value);
                switch (comptime.type)
                {
                case Comptime_Result_Type::AVAILABLE:
                    break;
                case Comptime_Result_Type::UNAVAILABLE: {
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    return;
                }
                case Comptime_Result_Type::NOT_COMPTIME: {
                    semantic_analyser_log_error(Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_COMPTIME_KNOWN, definition->value.value);
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    return;
                }
                default: panic("");
                }

                Constant_Result result = constant_pool_add_constant(
                    &compiler.constant_pool, comptime.data_type, array_create_static((byte*)comptime.data, comptime.data_type->size)
                );
                if (result.status != Constant_Status::SUCCESS) {
                    semantic_analyser_log_error(Semantic_Error_Type::CONSTANT_POOL_ERROR, definition->value.value);
                    semantic_analyser_add_error_info(error_information_make_constant_status(result.status));
                    symbol->type = Symbol_Type::ERROR_SYMBOL;
                    break;
                }

                symbol->type = Symbol_Type::COMPTIME_VALUE;
                symbol->options.constant = result.constant;
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
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(definition));
                semantic_analyser_add_error_info(error_information_make_text("Creating aliases for hardcoded functions currently not supported!\n"));
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
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(definition));
                semantic_analyser_add_error_info(error_information_make_text("Creating symbol/function aliases currently not supported!\n"));
                break;
            }
            case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
                symbol->type = Symbol_Type::ERROR_SYMBOL;
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(definition));
                semantic_analyser_add_error_info(error_information_make_text("Creating aliases for polymorphic functions not supported!"));
                break;
            }
            case Expression_Result_Type::TYPE: {
                // TODO: Maybe also disallow this if this is an alias, as above
                symbol->type = Symbol_Type::TYPE;
                symbol->options.type = result->options.type;
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
        ModTree_Function* function = header_workload->progress->function;
        workload->current_function = function;
        auto& signature_node = header_workload->function_node->options.function.signature->options.function_signature;

        // Analyse header (Create param symbols + create parameter workloads)
        int non_poly_parameter_count = 0;
        int polymorphic_parameter_count = 0;
        {
            for (int i = 0; i < signature_node.parameters.size; i++)
            {
                auto param_node = signature_node.parameters[i];

                // Create symbol
                Symbol* symbol = symbol_table_define_symbol(
                    header_workload->base.current_symbol_table, param_node->name, Symbol_Type::PARAMETER, AST::upcast(param_node), true
                );

                // Make workload
                auto param_workload = workload_executer_allocate_workload<Workload_Function_Parameter>(upcast(param_node));
                param_workload->header = header_workload;
                param_workload->base_type = compiler.type_system.predefined_types.unknown_type;
                param_workload->param_node = param_node;
                param_workload->base.parent_table = header_workload->base.current_symbol_table;
                param_workload->execution_order_index = -1;
                param_workload->symbol = symbol;
                analysis_workload_add_dependency_internal(upcast(header_workload), upcast(param_workload), 0);

                // Set symbol information
                symbol->options.parameter.workload = param_workload;
                symbol->options.parameter.ast_index = i;
                symbol->options.parameter.type_index = non_poly_parameter_count;
                symbol->options.parameter.is_polymorphic = param_node->is_comptime;
                if (!param_node->is_comptime) {
                    non_poly_parameter_count += 1;
                }
                else {
                    polymorphic_parameter_count += 1;
                }

                // Set analysis info
                get_info(param_node, true)->symbol = symbol;
            }
            workload_executer_wait_for_dependency_resolution(); // Wait for all parameter workloads to finish
        }

        // Create type-signature (Non-polymorphic function parameters)
        {
            auto unfinished_signature = type_system_make_function_empty(&type_system);
            for (int i = 0; i < signature_node.parameters.size; i++) {
                auto param = signature_node.parameters[i];
                if (param->is_comptime) {
                    continue;
                }
                empty_function_add_parameter(&unfinished_signature, param->name, get_info(param)->symbol->options.parameter.workload->base_type);
            }

            Type_Signature* return_type = types.void_type;
            if (signature_node.return_value.available) {
                return_type = semantic_analyser_analyse_expression_type(signature_node.return_value.value);
            }
            function->signature = empty_function_finish(&type_system, unfinished_signature, return_type);
        }

        // Set as polymorphic base if the function is polymorphic
        if (polymorphic_parameter_count != 0) {
            auto base = polymorphic_base_create(header_workload->progress, polymorphic_parameter_count);
            if (function->symbol != 0) {
                function->symbol->type = Symbol_Type::POLYMORPHIC_FUNCTION;
                function->symbol->options.polymorphic_function = base;
            }
        }


        break;
    }
    case Analysis_Workload_Type::FUNCTION_PARAMETER:
    {
        auto param_workload = downcast<Workload_Function_Parameter>(workload);
        param_workload->base.current_function = param_workload->header->progress->function;
        param_workload->base_type = semantic_analyser_analyse_expression_type(param_workload->param_node->type);
        param_workload->execution_order_index = param_workload->header->parameter_order.size;
        dynamic_array_push_back(&param_workload->header->parameter_order, param_workload);
        break;
    }
    case Analysis_Workload_Type::FUNCTION_BODY:
    {
        auto body_workload = downcast<Workload_Function_Body>(workload);
        auto code_block = body_workload->body_node;
        auto function = body_workload->progress->function;

        // Check if we are polymorphic instance and base function already contained errors...
        if (body_workload->progress->poly_instance != 0) {
            auto instance = body_workload->progress->poly_instance;
            // Note: I think I cannot just set contains errors to 
            if (instance->instance_index != 0 && instance->base->instances[0]->progress->function->contains_errors) {
                function->contains_errors = true;
                function->is_runnable = false;
                break;
            }
            workload->current_polymorphic_values = instance->parameter_values;
        }

        // Prepare analysis
        workload->current_function = function;

        // Analyse body
        Control_Flow flow = semantic_analyser_analyse_block(code_block);
        if (flow != Control_Flow::RETURNS && !type_signature_equals(function->signature->options.function.return_type, types.void_type)) {
            semantic_analyser_log_error(Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT, upcast(code_block));
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
    case Analysis_Workload_Type::STRUCT_ANALYSIS:
    {
        auto workload_structure = downcast<Workload_Struct_Analysis>(workload);

        auto& struct_node = workload_structure->struct_node->options.structure;
        Type_Signature* struct_signature = workload_structure->progress->struct_type;
        workload_structure->dependency_type = Dependency_Type::MEMBER_IN_MEMORY;

        for (int i = 0; i < struct_node.members.size; i++)
        {
            auto member_node = struct_node.members[i];
            if (member_node->value.available) {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_MEMBER_MUST_NOT_HAVE_VALUE, AST::upcast(member_node));
            }
            if (!member_node->type.available) {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_MEMBER_REQUIRES_TYPE, AST::upcast(member_node->value.value));
                continue;
            }

            Struct_Member member;
            member.id = member_node->name;
            member.offset = 0;
            member.type = semantic_analyser_analyse_expression_type(member_node->type.value);
            assert(!(member.type->size == 0 && member.type->alignment == 0), "Must not happen with current Dependency-System");
            dynamic_array_push_back(&struct_signature->options.structure.members, member);
        }
        type_system_finish_type(&type_system, struct_signature);
        break;
    }
    case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE:
    {
        auto reachable = downcast<Workload_Struct_Reachable_Resolve>(workload);
        for (int i = 0; i < reachable->unfinished_array_types.size; i++)
        {
            Type_Signature* array_type = reachable->unfinished_array_types[i];
            assert(array_type->type == Signature_Type::ARRAY, "");
            assert(!(array_type->options.array.element_type->size == 0 && array_type->options.array.element_type->alignment == 0), "");
            array_type->size = array_type->options.array.element_type->size * array_type->options.array.element_count;
            array_type->alignment = array_type->options.array.element_type->alignment;
            type_system_finish_type(&type_system, array_type);
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
            function->signature = type_system_make_function(&type_system, {}, types.void_type);
            auto flow = semantic_analyser_analyse_block(code_block);
            if (flow != Control_Flow::RETURNS) {
                semantic_analyser_log_error(Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT, AST::upcast(code_block));
            }
        }
        else if (node->type == AST::Expression_Type::BAKE_EXPR)
        {
            auto& bake_expr = node->options.bake_expr;
            auto result_type = semantic_analyser_analyse_expression_value(bake_expr, expression_context_make_unknown());
            function->signature = type_system_make_function(&type_system, {}, result_type);
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
        auto& interpreter = compiler.bytecode_interpreter;
        auto& ir_gen = compiler.ir_generator;

        auto bake_function = execute->progress->bake_function;

        // Check if function compilation succeeded
        if (bake_function->contains_errors) {
            progress->result = comptime_result_make_unavailable(bake_function->signature->options.function.return_type);
            return;
        }
        for (int i = 0; i < bake_function->calls.size; i++) {
            if (!bake_function->calls[i]->is_runnable) {
                bake_function->is_runnable = false;
                progress->result = comptime_result_make_unavailable(bake_function->signature->options.function.return_type);
                return;
            }
        }

        // Compile
        ir_generator_queue_function(bake_function);
        ir_generator_generate_queued_items(true);

        // Set Global Type Informations
        {
            bytecode_interpreter_prepare_run(interpreter);
            Upp_Slice<Internal_Type_Information>* info_slice = (Upp_Slice<Internal_Type_Information>*)
                & interpreter->globals.data[
                    compiler.bytecode_generator->global_data_offsets[analyser.global_type_informations->index]
                ];
            info_slice->size = type_system.internal_type_infos.size;
            info_slice->data_ptr = type_system.internal_type_infos.data;
        }

        // Execute
        IR_Function* ir_func = *hashtable_find_element(&ir_gen->function_mapping, bake_function);
        int func_start_instr_index = *hashtable_find_element(&compiler.bytecode_generator->function_locations, ir_func);
        RESTORE_ON_SCOPE_EXIT(interpreter->instruction_limit_enabled, true);
        RESTORE_ON_SCOPE_EXIT(interpreter->instruction_limit, 5000);
        bytecode_interpreter_run_function(interpreter, func_start_instr_index);
        if (interpreter->exit_code != Exit_Code::SUCCESS) {
            semantic_analyser_log_error(Semantic_Error_Type::BAKE_FUNCTION_DID_NOT_SUCCEED, execute->bake_node);
            semantic_analyser_add_error_info(error_information_make_exit_code(interpreter->exit_code));
            progress->result = comptime_result_make_unavailable(bake_function->signature->options.function.return_type);
            return;
        }

        void* value_ptr = interpreter->return_register;
        progress->result = comptime_result_make_available(value_ptr, bake_function->signature->options.function.return_type);
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
        Hashtable<C_Import_Type*, Type_Signature*> type_conversion_table = hashtable_create_pointer_empty<C_Import_Type*, Type_Signature*>(256);
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
                semantic_analyser_log_error(analyser, error);
                continue;
            }

            switch (import_symbol->type)
            {
            case C_Import_Symbol_Type::TYPE:
            {
                Type_Signature* type = import_c_type(analyser, import_symbol->data_type, &type_conversion_table);
                if (type->type == Signature_Type::STRUCT) {
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
                assert(extern_fn->signature->type == Signature_Type::FUNCTION, "HEY");

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
                semantic_analyser_log_error(analyser, error);
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
                Type_Signature** signature = hashtable_find_element(&type_conversion_table, import_sym->data_type);
                if (signature != 0)
                {
                    Type_Signature* type = *signature;
                    if (type->type == Signature_Type::STRUCT) {
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
        semantic_analyser_log_error(analyser, error);
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
        if (result.options.type->type != Signature_Type::FUNCTION) {
            Semantic_Error error;
            error.type = Semantic_Error_Type::INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER;
            error.error_node = extern_node->child_start;
            semantic_analyser_log_error(analyser, error);
            break;
        }

        ModTree_Function* extern_fn = new ModTree_Function;
        extern_fn->parent_module = work->parent_module;
        extern_fn->function_type = ModTree_Function_Type::EXTERN_FUNCTION;
        extern_fn->options.extern_function.id = extern_node->id;
        extern_fn->signature = result.options.type;
        extern_fn->options.extern_function.function_signature = extern_fn->signature;
        assert(extern_fn->signature->type == Signature_Type::FUNCTION, "HEY");
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
    case Analysis_Workload_Type::USING_RESOLVE: {
        auto using_node = downcast<Workload_Using_Resolve>(workload)->using_node;
        string_append_formated(string, "Using ");
        AST::path_lookup_append_to_string(using_node->path, string);
        if (using_node->type == AST::Using_Type::SYMBOL_IMPORT) {
            string_append_formated(string, "~*");
        }
        else if (using_node->type == AST::Using_Type::SYMBOL_IMPORT_TRANSITIV) {
            string_append_formated(string, "~**");
        }
        break;
    }
    case Analysis_Workload_Type::DEFINITION: {
        auto definition = downcast<Workload_Definition>(workload)->definition_node;
        if (definition->is_comptime) {
            string_append_formated(string, "Comptime \"%s\"", definition->name->characters);
        }
        else {
            string_append_formated(string, "Global \"%s\"", definition->name->characters);
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
        Symbol* symbol = param->header->progress->function->symbol;
        const char* fn_id = symbol == 0 ? "Lambda" : symbol->id->characters;
        string_append_formated(string, "Paramter: %s of %s", param->param_node->name->characters, fn_id);
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
    case Analysis_Workload_Type::STRUCT_ANALYSIS: {
        Symbol* symbol = downcast<Workload_Struct_Analysis>(workload)->progress->struct_type->options.structure.symbol;
        const char* struct_id = symbol == 0 ? "Anonymous_Struct" : symbol->id->characters;
        string_append_formated(string, "Struct-Analysis \"%s\"", struct_id);
        break;
    }
    case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE: {
        //const char* struct_id = symbol == 0 ? "Anonymous_Struct" : symbol->id->characters;
        string_append_formated(string, "Struct-Reachable-Resolve [");
        auto cluster = downcast<Workload_Struct_Reachable_Resolve>(analysis_workload_find_associated_cluster(workload));
        for (int i = 0; i < cluster->struct_types.size; i++) {
            Symbol* symbol = cluster->struct_types[i]->options.structure.symbol;
            const char* fn_id = symbol == 0 ? "Anonymous" : symbol->id->characters;
            string_append_formated(string, "%s, ", fn_id);
        }
        string_append_formated(string, "]");


        break;
    }
    default: panic("");
    }
}

void workload_executer_add_module_discovery(AST::Module* module) {
    semantic_analyser.root_module = module_progress_create(module, 0);
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
            upcast(progress->compile_workload),
            0
        );
        break;
    }
    case Analysis_Workload_Type::FUNCTION_BODY: {
        analysis_workload_add_cluster_dependency(
            upcast(downcast<Workload_Function_Body>(analyser.current_workload)->progress->compile_workload),
            upcast(progress->compile_workload),
            0
        );
        break;
    }
    default: return;
    }
}

Info_Cast_Type semantic_analyser_check_cast_type(Type_Signature* source_type, Type_Signature* destination_type, bool implicit_cast)
{
    auto& analyser = semantic_analyser;
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    if (type_signature_equals(source_type, types.unknown_type) || type_signature_equals(destination_type, types.unknown_type)) {
        semantic_analyser_set_error_flag(true);
        return Info_Cast_Type::NO_CAST;
    }
    if (type_signature_equals(source_type, destination_type)) return Info_Cast_Type::NO_CAST;
    bool cast_valid = false;
    Info_Cast_Type cast_type = Info_Cast_Type::INVALID;
    // Pointer casting
    if (source_type->type == Signature_Type::POINTER || destination_type->type == Signature_Type::POINTER)
    {
        if (source_type->type == Signature_Type::POINTER && destination_type->type == Signature_Type::POINTER)
        {
            cast_type = Info_Cast_Type::POINTERS;
            if (implicit_cast) {
                cast_valid = type_signature_equals(source_type, types.void_ptr_type) ||
                    type_signature_equals(destination_type, types.void_ptr_type);
            }
            else {
                cast_valid = true;
            }
        }
    }
    else if (source_type->type == Signature_Type::FUNCTION || destination_type->type == Signature_Type::FUNCTION) {
        // Check if types are the same
        auto& src_fn = source_type->options.function;
        auto& dst_fn = destination_type->options.function;
        cast_valid = true;
        cast_type = Info_Cast_Type::POINTERS;
        if (src_fn.parameters.size != dst_fn.parameters.size) {
            cast_valid = false;
        }
        else {
            for (int i = 0; i < src_fn.parameters.size; i++) {
                auto& param1 = src_fn.parameters[i];
                auto& param2 = dst_fn.parameters[i];
                if (!type_signature_equals(param1.type, param2.type)) {
                    cast_valid = false;
                }
            }
        }
        if (!type_signature_equals(src_fn.return_type, dst_fn.return_type)) {
            cast_valid = false;
        }
    }
    // Primitive Casting:
    else if (source_type->type == Signature_Type::PRIMITIVE && destination_type->type == Signature_Type::PRIMITIVE)
    {
        if (source_type->options.primitive.type == Primitive_Type::INTEGER && destination_type->options.primitive.type == Primitive_Type::INTEGER)
        {
            cast_type = Info_Cast_Type::INTEGERS;
            cast_valid = true;
            if (implicit_cast) {
                if (source_type->options.primitive.is_signed == destination_type->options.primitive.is_signed) {
                    cast_valid = source_type->size < destination_type->size;
                }
                else {
                    if (source_type->options.primitive.is_signed) {
                        cast_valid = false;
                    }
                    else {
                        cast_valid = destination_type->size > source_type->size; // E.g. u8 to i32, u32 to i64
                    }
                }
            }
        }
        else if (destination_type->options.primitive.type == Primitive_Type::FLOAT && source_type->options.primitive.type == Primitive_Type::INTEGER)
        {
            cast_type = Info_Cast_Type::INT_TO_FLOAT;
            cast_valid = true;
        }
        else if (destination_type->options.primitive.type == Primitive_Type::INTEGER && source_type->options.primitive.type == Primitive_Type::FLOAT)
        {
            cast_type = Info_Cast_Type::FLOAT_TO_INT;
            cast_valid = !implicit_cast;
        }
        else if (destination_type->options.primitive.type == Primitive_Type::FLOAT && source_type->options.primitive.type == Primitive_Type::FLOAT)
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
    else if (source_type->type == Signature_Type::ARRAY && destination_type->type == Signature_Type::SLICE)
    {
        if (source_type->options.array.element_type == destination_type->options.array.element_type) {
            cast_type = Info_Cast_Type::ARRAY_TO_SLICE;
            cast_valid = true;
        }
    }
    // Enum casting
    else if (source_type->type == Signature_Type::ENUM || destination_type->type == Signature_Type::ENUM)
    {
        bool enum_is_source = false;
        Type_Signature* other = 0;
        if (source_type->type == Signature_Type::ENUM) {
            enum_is_source = true;
            other = destination_type;
        }
        else {
            enum_is_source = false;
            other = source_type;
        }

        if (other->type == Signature_Type::PRIMITIVE && other->options.primitive.type == Primitive_Type::INTEGER) {
            cast_type = enum_is_source ? Info_Cast_Type::ENUM_TO_INT : Info_Cast_Type::INT_TO_ENUM;
            cast_valid = !implicit_cast;
        }
    }
    // Any casting
    else if (type_signature_equals(destination_type, types.any_type))
    {
        cast_valid = true;
        cast_type = Info_Cast_Type::TO_ANY;
    }
    else if (type_signature_equals(source_type, types.any_type)) {
        cast_valid = !implicit_cast;
        cast_type = Info_Cast_Type::FROM_ANY;
    }

    if (cast_valid) return cast_type;
    return Info_Cast_Type::INVALID;
}

void expression_info_set_cast(Expression_Info* info, Info_Cast_Type cast_type, Type_Signature* result_type)
{
    info->context_ops.after_cast_type = result_type;
    info->context_ops.cast = cast_type;
}

#define SET_ACTIVE_EXPR_INFO(new_info)\
    Expression_Info* _backup_info = semantic_analyser.current_workload->current_expression; \
    semantic_analyser.current_workload->current_expression = new_info; \
    SCOPE_EXIT(semantic_analyser.current_workload->current_expression = _backup_info);

Expression_Info* semantic_analyser_analyse_expression_internal(AST::Expression* expr, Expression_Context context)
{
    auto& analyser = semantic_analyser;
    auto type_system = &compiler.type_system;
    auto& types = type_system->predefined_types;
    auto info = get_info(expr, true);
    SET_ACTIVE_EXPR_INFO(info);
    info->context = context;
    info->context_ops.after_cast_type = 0;
    info->context_ops.cast = Info_Cast_Type::NO_CAST;
    info->context_ops.deref_count = 0;
    info->context_ops.take_address_of = false;
    expression_info_set_error(info, types.unknown_type); // Just initialize with some values
    info->contains_errors = false; // To undo the previous set error

#define EXIT_VALUE(val) expression_info_set_value(info, val); return info;
#define EXIT_TYPE(type) expression_info_set_type(info, type); return info;
#define EXIT_ERROR(type) expression_info_set_error(info, type); return info;
#define EXIT_HARDCODED(hardcoded) expression_info_set_hardcoded(info, hardcoded); return info;
#define EXIT_FUNCTION(function) expression_info_set_function(info, function); return info;

    // Special handling for Structs, because of Cluster-Compile
    Dependency_Type _backup_type;
    bool switch_type_back = false;
    if (semantic_analyser.current_workload->type == Analysis_Workload_Type::STRUCT_ANALYSIS) {
        switch_type_back = true;
        auto& dependency_type = downcast<Workload_Struct_Analysis>(semantic_analyser.current_workload)->dependency_type;
        _backup_type = dependency_type;

        using AST::Expression_Type;
        if (dependency_type != Dependency_Type::NORMAL) {
            if (expr->type == Expression_Type::FUNCTION_SIGNATURE || expr->type == Expression_Type::SLICE_TYPE ||
                (expr->type == Expression_Type::UNARY_OPERATION && expr->options.unop.type == AST::Unop::POINTER)) {
                dependency_type = Dependency_Type::MEMBER_REFERENCE;
            }
            else if (!(expr->type == Expression_Type::PATH_LOOKUP ||
                expr->type == Expression_Type::ARRAY_TYPE ||
                expr->type == Expression_Type::STRUCTURE_TYPE))
            {
                // Reset to normal if we don't have a type expression
                dependency_type = Dependency_Type::NORMAL;
            }
        }
    }
    SCOPE_EXIT(
        if (switch_type_back) {
            downcast<Workload_Struct_Analysis>(semantic_analyser.current_workload)->dependency_type = _backup_type;
        }
    );

    switch (expr->type)
    {
    case AST::Expression_Type::ERROR_EXPR: {
        semantic_analyser_set_error_flag(false);// Error due to parsing, dont log error message because we already have parse error messages
        EXIT_ERROR(types.unknown_type);
    }
    case AST::Expression_Type::FUNCTION_CALL:
    {
        // Analyse call expression
        auto& call = expr->options.call;
        auto function_expr_info = semantic_analyser_analyse_expression_any(call.expr, expression_context_make_auto_dereference());

        // Initialize all argument infos as valid 
        for (int i = 0; i < call.arguments.size; i++) {
            auto argument = get_info(call.arguments[i], true);
            argument->valid = true;
            argument->argument_index = i;
        }
        info->specifics.function_call_signature = 0;

        // Handle Type-Of (Or in the future other compiler given functions)
        if (function_expr_info->result_type == Expression_Result_Type::HARDCODED_FUNCTION && function_expr_info->options.hardcoded == Hardcoded_Type::TYPE_OF)
        {
            if (call.arguments.size != 1) {
                semantic_analyser_log_error(Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH, call.expr);
                semantic_analyser_add_error_info(error_information_make_argument_count(call.arguments.size, 1));
                EXIT_ERROR(types.unknown_type);
            }
            auto& arg = call.arguments[0];
            if (arg->name.available) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, &arg->base);
                semantic_analyser_add_error_info(error_information_make_text("Argument name for type_of must not be given"));
            }

            auto arg_result = semantic_analyser_analyse_expression_any(arg->value, expression_context_make_unknown());
            switch (arg_result->result_type)
            {
            case Expression_Result_Type::VALUE: {
                EXIT_TYPE(arg_result->options.type);
            }
            case Expression_Result_Type::HARDCODED_FUNCTION: {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, arg->value);
                semantic_analyser_add_error_info(error_information_make_text("Cannot use type_of on hardcoded functions!"));
                EXIT_ERROR(types.unknown_type);
            }
            case Expression_Result_Type::CONSTANT: {
                EXIT_TYPE(arg_result->options.constant.type);
            }
            case Expression_Result_Type::FUNCTION: {
                EXIT_TYPE(arg_result->options.function->signature);
            }
            case Expression_Result_Type::TYPE: {
                EXIT_TYPE(types.type_type);
            }
            case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_EXPRESSION_TYPE, arg->value);
                semantic_analyser_add_error_info(error_information_make_expression_result_type(arg_result->result_type));
                EXIT_ERROR(types.unknown_type);
            }
            default: panic("");
            }

            panic("");
            EXIT_ERROR(arg_result->options.type);
        }

        Type_Signature* function_signature = 0; // Note: I still want to analyse all arguments even if the signature is null
        switch (function_expr_info->result_type)
        {
        case Expression_Result_Type::FUNCTION: {
            function_signature = function_expr_info->options.function->signature;
            break;
        }
        case Expression_Result_Type::HARDCODED_FUNCTION:
        {
            function_signature = hardcoded_type_to_signature(function_expr_info->options.hardcoded);
            break;
        }
        case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
            auto poly_base = function_expr_info->options.polymorphic.base;
            auto poly_header = poly_base->progress->header_workload;
            auto& arguments = call.arguments;

            // Early exit on simple errors
            {
                if (arguments.size != poly_header->parameter_order.size) {
                    // TODO: In theory I could do something smarter here, with default values and named parameters I will need to do something else
                    semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(expr));
                    semantic_analyser_add_error_info(error_information_make_text("Argument count did not match parameter count!"));
                    function_signature = 0;
                    break;
                }
                if (poly_base->progress->function->contains_errors) {
                    function_signature = 0;
                    break;
                }
            }

            // Get instanciation depth (Exit when recursion is too high)
            auto empty_instance = polymorphic_base_make_instance_empty(poly_base, upcast(expr));
            if (empty_instance == 0) {
                break;
            }

            // Evaluate polymorphic parameters
            bool success = true;
            SCOPE_EXIT(if (!success) { polymorpic_instance_destroy_empty(empty_instance); });
            Analysis_Pass* header_pass = analysis_pass_allocate(semantic_analyser.current_workload, upcast(expr));
            // Evaluate polymorphic parameters in evaluation order
            for (int i = 0; i < poly_header->parameter_order.size; i++)
            {
                auto& parameter = poly_header->parameter_order[i];
                if (!parameter->param_node->is_comptime) continue; // Skip non-comptime parameters
                auto& poly_value = empty_instance->parameter_values[i];
                auto argument = arguments[parameter->symbol->options.parameter.ast_index];
                get_info(argument)->valid = false;

                // Re-analyse base-header to get valid poly-argument type (Since this type can change with filled out polymorphic values)
                Type_Signature* argument_type = 0;
                {
                    RESTORE_ON_SCOPE_EXIT(semantic_analyser.current_workload->current_pass, header_pass);
                    RESTORE_ON_SCOPE_EXIT(analyser.current_workload->current_polymorphic_values, empty_instance->parameter_values);
                    RESTORE_ON_SCOPE_EXIT(analyser.current_workload->current_symbol_table, poly_base->progress->function->parameter_table);
                    argument_type = semantic_analyser_analyse_expression_type(parameter->param_node->type);
                }

                // Analyse Argument and try to get comptime value
                if (type_signature_equals(argument_type, types.type_type)) {
                    // In this case we are looking for a comptime value for a type, so well call analyse type
                    auto result = semantic_analyser_analyse_expression_type(argument->value);
                }
                else {
                    semantic_analyser_analyse_expression_value(argument->value, expression_context_make_specific_type(argument_type));
                }
                auto comptime_result = expression_calculate_comptime_value(argument->value);
                switch (comptime_result.type)
                {
                case Comptime_Result_Type::AVAILABLE: {
                    if (comptime_result.data_type == types.unknown_type) {
                        panic("Lets panic for now, I'm not quite sure if this happens in recursive instanciations -_-");
                    }
                    Constant_Result result = constant_pool_add_constant(
                        &compiler.constant_pool,
                        comptime_result.data_type,
                        array_create_static((byte*)comptime_result.data, comptime_result.data_type->size)
                    );
                    if (result.status != Constant_Status::SUCCESS) {
                        semantic_analyser_log_error(Semantic_Error_Type::CONSTANT_POOL_ERROR, AST::upcast(argument->value));
                        semantic_analyser_add_error_info(error_information_make_constant_status(result.status));
                        poly_value.is_not_set = true;
                        success = false;
                    }
                    else {
                        poly_value.is_not_set = false;
                        poly_value.constant = result.constant;
                    }
                    break;
                }
                case Comptime_Result_Type::UNAVAILABLE: {
                    poly_value.is_not_set = true;
                    break;
                }
                case Comptime_Result_Type::NOT_COMPTIME:
                    semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(argument->value));
                    semantic_analyser_add_error_info(error_information_make_text("For instanciation values must be comptime!"));
                    success = false;
                    break;
                }
            }

            if (!success)
            {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(expr));
                semantic_analyser_add_error_info(error_information_make_text("Some values couldn't be calculated at comptime!"));
                break;
            }

            // NOTE: Now there should be no more errors
            function_signature = function_progress_create_polymorphic_instance(empty_instance, function_expr_info, header_pass)->function->signature;

            break;
        }
        case Expression_Result_Type::VALUE:
        {
            // TODO: Check if this is comptime known, then we dont need a function pointer call
            function_signature = function_expr_info->options.type;
            if (function_signature->type != Signature_Type::FUNCTION) {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL, expr);
                semantic_analyser_add_error_info(error_information_make_given_type(function_signature));
                function_signature = 0;
            }
            break;
        }
        case Expression_Result_Type::CONSTANT:
        case Expression_Result_Type::TYPE: {
            semantic_analyser_log_error(Semantic_Error_Type::EXPECTED_CALLABLE, expr);
            semantic_analyser_add_error_info(error_information_make_expression_result_type(function_expr_info->result_type));
            function_signature = 0;
            break;
        }
        default: panic("");
        }
        info->specifics.function_call_signature = function_signature;

        // Handle unknown function
        auto& arguments = call.arguments;
        if (function_signature == 0) {
            // Analyse all expressions with unknown context
            for (int i = 0; i < arguments.size; i++) {
                if (!get_info(arguments[i])->valid) { // Skip already analysed
                    continue;
                }
                semantic_analyser_analyse_expression_value(arguments[i]->value, expression_context_make_unknown());
            }
            EXIT_ERROR(types.unknown_type);
        }

        // Analyse arguments
        {
            auto& parameters = function_signature->options.function.parameters;
            int valid_argument_count = 0;
            bool size_mismatch = false;
            for (int i = 0; i < arguments.size; i++) {
                auto info = get_info(arguments[i]);
                if (info->valid) {
                    auto context = expression_context_make_unknown();
                    if (valid_argument_count < parameters.size) {
                        context = expression_context_make_specific_type(parameters[valid_argument_count].type);
                    }
                    else {
                        size_mismatch = true;
                    }
                    semantic_analyser_analyse_expression_value(arguments[i]->value, context);
                    info->argument_index = valid_argument_count;
                    valid_argument_count += 1;
                }
            }
            if (valid_argument_count != parameters.size || size_mismatch) {
                semantic_analyser_log_error(Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH, expr);
                semantic_analyser_add_error_info(error_information_make_argument_count(arguments.size, parameters.size));
                semantic_analyser_add_error_info(error_information_make_function_type(function_signature));
            }
        }

        EXIT_VALUE(function_signature->options.function.return_type);
    }
    case AST::Expression_Type::PATH_LOOKUP:
    {
        auto path = expr->options.path_lookup;
        // NOTE: The symbol is only resolved if another workload of the !same! Analysis_item has already solved it
        //       e.g. only in Template-instanciations currently.
        //       This behaviour should probably change to be more flexible with other features (e.g. conditional compilation)
        //       Currently we also DON'T add dependencies to resolved symbols, which isn't a problem for templates _now_
        //       because instances already have to wait for their parent analysis to be completed, e.g. symbols to be resolved.

        // Resolve symbol
        Symbol* symbol = path_lookup_resolve(path);
        assert(symbol != 0, "In error cases this should be set to error, never 0!");

        // Check and wait for all potential dependencies to finish
        {
            auto& executer = *analyser.workload_executer;
            auto workload = analyser.current_workload;
            switch (symbol->type)
            {
            case Symbol_Type::DEFINITION_UNFINISHED:
            {
                analysis_workload_add_dependency_internal(analyser.current_workload, upcast(symbol->options.definition_workload), path->last);
                break;
            }
            case Symbol_Type::FUNCTION:
            {
                analysis_workload_add_dependency_internal(workload, upcast(symbol->options.function->header_workload), path->last);
                break;
            }
            case Symbol_Type::POLYMORPHIC_FUNCTION:
            {
                analysis_workload_add_dependency_internal(workload, upcast(symbol->options.polymorphic_function->progress->header_workload), path->last);
                break;
            }
            case Symbol_Type::PARAMETER:
            {
                analysis_workload_add_dependency_internal(workload, upcast(symbol->options.parameter.workload), path->last);
                break;
            }
            case Symbol_Type::TYPE:
            {
                Type_Signature* type = symbol->options.type;
                if (type->type == Signature_Type::STRUCT)
                {
                    Struct_Progress* other_progress = type->options.structure.progress;
                    if (other_progress != 0) {
                        if (workload->type == Analysis_Workload_Type::STRUCT_ANALYSIS) {
                            auto current = downcast<Workload_Struct_Analysis>(workload);
                            analysis_workload_add_struct_dependency(current->progress, other_progress, current->dependency_type, path->last);
                        }
                        else {
                            analysis_workload_add_dependency_internal(workload, upcast(other_progress->reachable_resolve_workload), path->last);
                        }
                    }
                    else {
                        // Progress may be 0 if its a predefined struct
                        assert(!(type->size == 0 && type->alignment == 0), "");
                    }
                }
                break;
            }
            default: break;
            }

            workload_executer_wait_for_dependency_resolution();
            // Refresh symbol since it may have changed
            symbol = get_info(path)->symbol;
            assert(symbol != 0, "Must be given by dependency analysis");
        }

        switch (symbol->type)
        {
        case Symbol_Type::ERROR_SYMBOL: {
            semantic_analyser_set_error_flag(true);
            EXIT_ERROR(types.unknown_type);
        }
        case Symbol_Type::DEFINITION_UNFINISHED: {
            panic("Should not happen, we just waited on this workload to finish!");
        }
        case Symbol_Type::ALIAS_OR_IMPORTED_SYMBOL: {
            panic("Aliases should already be handled, this should only point to a valid symbol");
        }
        case Symbol_Type::HARDCODED_FUNCTION: {
            EXIT_HARDCODED(symbol->options.hardcoded);
        }
        case Symbol_Type::FUNCTION: {
            semantic_analyser_register_function_call(symbol->options.function->function);
            EXIT_FUNCTION(symbol->options.function->function);
        }
        case Symbol_Type::GLOBAL: {
            EXIT_VALUE(symbol->options.global->type);
        }
        case Symbol_Type::TYPE: {
            EXIT_TYPE(symbol->options.type);
        }
        case Symbol_Type::VARIABLE: {
            assert(analyser.current_workload->type != Analysis_Workload_Type::FUNCTION_HEADER, "Function headers can never access variable symbols!");
            EXIT_VALUE(symbol->options.variable_type);
        }
        case Symbol_Type::VARIABLE_UNDEFINED: {
            semantic_analyser_log_error(Semantic_Error_Type::VARIABLE_NOT_DEFINED_YET, expr);
            EXIT_ERROR(types.unknown_type);
        }
        case Symbol_Type::PARAMETER: {
            auto& param = symbol->options.parameter;
            if (analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_PARAMETER ||
                analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_HEADER)
            {
                // This means we are in the base analysis and just found a parameter-dependency
                if (!param.is_polymorphic) {
                    semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, expr);
                    semantic_analyser_add_error_info(error_information_make_text("Function headers cannot access normal parameters!"));
                    EXIT_VALUE(param.workload->base_type);
                }
                else if (type_signature_equals(param.workload->base_type, types.type_type)) { // Not sure if this is a hack or required...
                    EXIT_TYPE(types.unknown_type);
                }
                EXIT_VALUE(param.workload->base_type);
            }
            else if (analyser.current_workload->type == Analysis_Workload_Type::FUNCTION_BODY) {
                // Inside function body, where we either access the function signature, or a polymorphic value
                if (param.is_polymorphic) {
                    const auto& poly_value = analyser.current_workload->current_polymorphic_values[param.workload->execution_order_index];
                    if (poly_value.is_not_set) { // Is the case if we are in the body analysis of the base-function
                        EXIT_ERROR(param.workload->base_type);
                    }
                    expression_info_set_constant(info, poly_value.constant);
                    return info;
                }
                else {
                    EXIT_VALUE(analyser.current_workload->current_function->signature->options.function.parameters[param.type_index].type);
                }
            }
            else {
                panic("In which hellish landscape are we where we access parameters outside of body/header analysis");
            }

            panic("Cannot happen!");
            break;
        }
        case Symbol_Type::COMPTIME_VALUE: {
            expression_info_set_constant(info, symbol->options.constant);
            return info;
        }
        case Symbol_Type::MODULE: {
            semantic_analyser_log_error(Semantic_Error_Type::SYMBOL_MODULE_INVALID, expr);
            semantic_analyser_add_error_info(error_information_make_symbol(symbol));
            EXIT_ERROR(types.unknown_type);
        }
        case Symbol_Type::POLYMORPHIC_FUNCTION: {
            expression_info_set_polymorphic_function(info, symbol->options.polymorphic_function);
            return info;
        }
        default: panic("HEY");
        }

        panic("HEY");
        break;
    }
    case AST::Expression_Type::CAST:
    {
        auto cast = &expr->options.cast;
        Type_Signature* destination_type = 0;
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
                if (context.type != Expression_Context_Type::SPECIFIC_TYPE) {
                    semantic_analyser_log_error(Semantic_Error_Type::AUTO_CAST_KNOWN_CONTEXT_IS_REQUIRED, expr);
                    EXIT_ERROR(types.unknown_type);
                }
                destination_type = context.signature;
            }
            break;
        }
        case AST::Cast_Type::RAW_TO_PTR:
        {
            if (destination_type == 0) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_INVALID_CAST, expr);
                destination_type = types.unknown_type;
            }
            else {
                operand_context = expression_context_make_specific_type(types.u64_type);
                if (destination_type->type != Signature_Type::POINTER) {
                    semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_CAST_PTR_DESTINATION_MUST_BE_PTR, expr);
                    semantic_analyser_add_error_info(error_information_make_given_type(destination_type));
                }
            }
            break;
        }
        case AST::Cast_Type::PTR_TO_RAW:
        {
            if (destination_type != 0) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_INVALID_CAST, expr);
            }
            destination_type = types.u64_type;
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
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_INVALID_CAST, expr);
                semantic_analyser_add_error_info(error_information_make_given_type(operand_type));
                semantic_analyser_add_error_info(error_information_make_expected_type(destination_type));
            }
            break;
        }
        case AST::Cast_Type::PTR_TO_RAW: {
            if (operand_type->type != Signature_Type::POINTER) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_INVALID_CAST, expr);
                semantic_analyser_add_error_info(error_information_make_given_type(operand_type));
                semantic_analyser_add_error_info(error_information_make_expected_type(destination_type));
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

        info->specifics.cast_type = cast_type;
        EXIT_VALUE(destination_type);
    }
    case AST::Expression_Type::LITERAL_READ:
    {
        auto& read = expr->options.literal_read;
        void* value_ptr;
        Type_Signature* literal_type;
        void* null_pointer = 0;
        Upp_String string_buffer;

        // Missing: float, nummptr
        switch (read.type)
        {
        case Literal_Type::BOOLEAN:
            literal_type = types.bool_type;
            value_ptr = &read.options.boolean;
            break;
        case Literal_Type::FLOAT_VAL:
            literal_type = types.f32_type;
            value_ptr = &read.options.float_val;
            break;
        case Literal_Type::INTEGER:
            literal_type = types.i32_type;
            value_ptr = &read.options.int_val;
            break;
        case Literal_Type::NULL_VAL:
            literal_type = types.void_ptr_type;
            value_ptr = &null_pointer;
            break;
        case Literal_Type::STRING: {
            String* string = read.options.string;
            string_buffer.character_buffer_data = string->characters;
            string_buffer.character_buffer_size = string->capacity;
            string_buffer.size = string->size;

            literal_type = types.string_type;
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
        Type_Signature* enum_type = type_system_make_enum_empty(type_system, 0);
        int next_member_value = 1;
        for (int i = 0; i < members.size; i++)
        {
            auto& member_node = members[i];
            if (member_node->value.available)
            {
                semantic_analyser_analyse_expression_value(member_node->value.value, expression_context_make_specific_type(types.i32_type));
                Comptime_Result comptime = expression_calculate_comptime_value(member_node->value.value);
                switch (comptime.type)
                {
                case Comptime_Result_Type::AVAILABLE:
                    next_member_value = *(i32*)comptime.data;
                    break;
                case Comptime_Result_Type::UNAVAILABLE:
                    break;
                case Comptime_Result_Type::NOT_COMPTIME:
                    semantic_analyser_log_error(Semantic_Error_Type::ENUM_VALUE_MUST_BE_COMPILE_TIME_KNOWN, member_node->value.value);
                    break;
                default: panic("");
                }
            }

            Enum_Item member;
            member.id = member_node->name;
            member.value = next_member_value;
            next_member_value++;
            dynamic_array_push_back(&enum_type->options.enum_type.members, member);
        }

        // Finish up enum
        enum_type->size = types.i32_type->size;
        enum_type->alignment = types.i32_type->alignment;
        for (int i = 0; i < enum_type->options.enum_type.members.size; i++)
        {
            auto member = &enum_type->options.enum_type.members[i];
            for (int j = i + 1; j < enum_type->options.enum_type.members.size; j++)
            {
                auto other = &enum_type->options.enum_type.members[j];
                if (other->id == member->id) {
                    semantic_analyser_log_error(Semantic_Error_Type::ENUM_MEMBER_NAME_MUST_BE_UNIQUE, AST::upcast(expr));
                    semantic_analyser_add_error_info(error_information_make_id(other->id));
                }
                if (other->value == member->value) {
                    semantic_analyser_log_error(Semantic_Error_Type::ENUM_VALUE_MUST_BE_UNIQUE, AST::upcast(expr));
                    semantic_analyser_add_error_info(error_information_make_id(other->id));
                }
            }
        }
        type_system_finish_type(type_system, enum_type);
        EXIT_TYPE(enum_type);
    }
    case AST::Expression_Type::MODULE: {
        semantic_analyser_log_error(Semantic_Error_Type::MODULE_NOT_VALID_IN_THIS_CONTEXT, AST::upcast(expr));
        semantic_analyser_add_error_info(error_information_make_text("Anonymous modules can never be useful since nothing inside them can be referenced!"));
        EXIT_ERROR(types.unknown_type);
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
                workload = upcast(function_progress_create(0, expr)->header_workload);
                break;
            }
            case AST::Expression_Type::STRUCTURE_TYPE: {
                workload = upcast(struct_progress_create(0, expr)->analysis_workload);
                break;
            }
            case AST::Expression_Type::BAKE_BLOCK:
            case AST::Expression_Type::BAKE_EXPR: {
                workload = upcast(bake_progress_create(expr)->analysis_workload);
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
            analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(progress->execute_workload), 0);
            workload_executer_wait_for_dependency_resolution();

            // Handle result
            auto bake_result = &progress->result;
            switch (bake_result->type)
            {
            case Comptime_Result_Type::AVAILABLE: {
                expression_info_set_constant(
                    info, bake_result->data_type, array_create_static((byte*)bake_result->data, bake_result->data_type->size), AST::upcast(expr)
                );
                return info;
            }
            case Comptime_Result_Type::UNAVAILABLE:
                EXIT_ERROR(bake_result->data_type);
            case Comptime_Result_Type::NOT_COMPTIME:
                panic("Should not happen with bake!");
                break;
            default: panic("");
            }
            EXIT_ERROR(types.unknown_type);
            break;
        };
        case Analysis_Workload_Type::FUNCTION_HEADER: {
            // Wait for workload
            auto progress = downcast<Workload_Function_Header>(workload)->progress;
            analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(progress->header_workload), 0);
            workload_executer_wait_for_dependency_resolution();

            // Return value
            semantic_analyser_register_function_call(progress->function);
            EXIT_FUNCTION(progress->function);
            break;
        }
        case Analysis_Workload_Type::STRUCT_ANALYSIS: {
            // Wait for workload
            auto progress = downcast<Workload_Struct_Analysis>(workload)->progress;
            if (semantic_analyser.current_workload->type == Analysis_Workload_Type::STRUCT_ANALYSIS) {
                auto current = downcast<Workload_Struct_Analysis>(semantic_analyser.current_workload)->progress;
                analysis_workload_add_struct_dependency(current, progress, current->analysis_workload->dependency_type, 0);
            }
            else {
                analysis_workload_add_dependency_internal(semantic_analyser.current_workload, upcast(progress->reachable_resolve_workload), 0);
            }
            workload_executer_wait_for_dependency_resolution();

            // Return value
            auto struct_type = progress->struct_type;
            assert(!(struct_type->size == 0 && struct_type->alignment == 0), "");
            EXIT_TYPE(struct_type);
            break;
        }
        case Analysis_Workload_Type::MODULE_ANALYSIS: {
            semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, expr);
            semantic_analyser_add_error_info(error_information_make_text("Inner Imports and anonymous modules aren't currently supported!"));
            EXIT_ERROR(types.unknown_type);
        }
        case Analysis_Workload_Type::DEFINITION:
        case Analysis_Workload_Type::EVENT:
        case Analysis_Workload_Type::STRUCT_REACHABLE_RESOLVE:
        case Analysis_Workload_Type::BAKE_EXECUTION:
        case Analysis_Workload_Type::FUNCTION_BODY:
        case Analysis_Workload_Type::USING_RESOLVE:
        case Analysis_Workload_Type::FUNCTION_CLUSTER_COMPILE:
        default:
        {
            panic("Invalid codepath, ");
            EXIT_ERROR(types.unknown_type);
            break;
        }
        }

        panic("Invalid code path");
        EXIT_ERROR(types.unknown_type);
        break;
    }
    case AST::Expression_Type::FUNCTION_SIGNATURE:
    {
        auto& sig = expr->options.function_signature;
        auto unfinished = type_system_make_function_empty(type_system);
        for (int i = 0; i < sig.parameters.size; i++)
        {
            auto& param = sig.parameters[i];
            if (param->is_comptime) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE, AST::upcast(param));
                semantic_analyser_add_error_info(error_information_make_text("Comptime parameters are only allowed in function definitions, not in signatures!"));
            }
            else {
                empty_function_add_parameter(&unfinished, param->name, semantic_analyser_analyse_expression_type(param->type));
            }
        }
        Type_Signature* return_type = types.void_type;
        if (sig.return_value.available) {
            return_type = semantic_analyser_analyse_expression_type(sig.return_value.value);
        }
        EXIT_TYPE(empty_function_finish(type_system, unfinished, return_type));
    }
    case AST::Expression_Type::ARRAY_TYPE:
    {
        auto& array_node = expr->options.array_type;

        // Analyse size value
        {
            // Special handling for struct dependencies, FUTURE: Maybe rework/remove this here...
            Dependency_Type _backup_type;
            bool do_backup = false;
            if (semantic_analyser.current_workload->type == Analysis_Workload_Type::STRUCT_ANALYSIS) {
                do_backup = true;
                auto analysis = downcast<Workload_Struct_Analysis>(semantic_analyser.current_workload);
                _backup_type = analysis->dependency_type;
                analysis->dependency_type = Dependency_Type::NORMAL;
            }

            semantic_analyser_analyse_expression_value(
                array_node.size_expr, expression_context_make_specific_type(types.i32_type)
            );

            if (do_backup) {
                downcast<Workload_Struct_Analysis>(semantic_analyser.current_workload)->dependency_type = _backup_type;
            }
        }

        Comptime_Result comptime = expression_calculate_comptime_value(array_node.size_expr);
        int array_size = 0;
        bool array_size_known = false;
        switch (comptime.type)
        {
        case Comptime_Result_Type::AVAILABLE: {
            array_size_known = true;
            array_size = *(i32*)comptime.data;
            if (array_size <= 0) {
                semantic_analyser_log_error(Semantic_Error_Type::ARRAY_SIZE_MUST_BE_GREATER_ZERO, array_node.size_expr);
                array_size_known = false;
            }
            break;
        }
        case Comptime_Result_Type::UNAVAILABLE:
            array_size_known = false;
            break;
        case Comptime_Result_Type::NOT_COMPTIME:
            semantic_analyser_log_error(Semantic_Error_Type::ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN, array_node.size_expr);
            array_size_known = false;
            break;
        default: panic("");
        }

        Type_Signature* element_type = semantic_analyser_analyse_expression_type(array_node.type_expr);
        Type_Signature* array_type = type_system_make_array(type_system, element_type, array_size_known, array_size);

        // Add to unfinished array size if necessary
        if (element_type->size == 0 && element_type->alignment == 0)
        {
            assert(analyser.current_workload->type == Analysis_Workload_Type::STRUCT_ANALYSIS, "");
            auto progress = downcast<Workload_Struct_Analysis>(analyser.current_workload)->progress;
            assert(!progress->reachable_resolve_workload->base.is_finished, "Finished structs cannot be of size + alignment 0");
            dynamic_array_push_back(
                &downcast<Workload_Struct_Reachable_Resolve>(
                    analysis_workload_find_associated_cluster(upcast(progress->reachable_resolve_workload))
                    )->unfinished_array_types,
                array_type
            );
        }
        EXIT_TYPE(array_type);
    }
    case AST::Expression_Type::SLICE_TYPE:
    {
        EXIT_TYPE(type_system_make_slice(type_system, semantic_analyser_analyse_expression_type(expr->options.slice_type)));
    }
    case AST::Expression_Type::NEW_EXPR:
    {
        auto& new_node = expr->options.new_expr;
        Type_Signature* allocated_type = semantic_analyser_analyse_expression_type(new_node.type_expr);
        assert(!(allocated_type->size == 0 && allocated_type->alignment == 0), "HEY");

        Type_Signature* result_type = 0;
        if (new_node.count_expr.available)
        {
            result_type = type_system_make_slice(type_system, allocated_type);
            semantic_analyser_analyse_expression_value(new_node.count_expr.value, expression_context_make_specific_type(types.i32_type));
        }
        else {
            result_type = type_system_make_pointer(type_system, allocated_type);
        }
        EXIT_VALUE(result_type);
    }
    case AST::Expression_Type::STRUCT_INITIALIZER:
    {
        auto& init_node = expr->options.struct_initializer;
        Type_Signature* struct_signature = 0;
        if (init_node.type_expr.available) {
            struct_signature = semantic_analyser_analyse_expression_type(init_node.type_expr.value);
        }
        else {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE) {
                struct_signature = context.signature;
            }
            else {
                semantic_analyser_log_error(Semantic_Error_Type::AUTO_STRUCT_INITIALIZER_COULD_NOT_DETERMINE_TYPE, expr);
                EXIT_ERROR(types.unknown_type);
            }
        }

        if (struct_signature->type != Signature_Type::STRUCT) {
            semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_TYPE_MUST_BE_STRUCT, expr);
            semantic_analyser_add_error_info(error_information_make_given_type(struct_signature));
            EXIT_ERROR(struct_signature);
        }
        if (init_node.arguments.size == 0) {
            semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING, expr);
            EXIT_ERROR(struct_signature);
        }
        assert(!(struct_signature->size == 0 && struct_signature->alignment == 0), "");

        // Analyse arguments
        for (int i = 0; i < init_node.arguments.size; i++)
        {
            auto& argument = init_node.arguments[i];
            if (!argument->name.available) {
                semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE_NAMED_ARGUMENTS, AST::upcast(argument));
                continue;
            }
            String* member_id = argument->name.value;
            Struct_Member* found_member = 0;
            for (int i = 0; i < struct_signature->options.structure.members.size; i++) {
                if (struct_signature->options.structure.members[i].id == member_id) {
                    found_member = &struct_signature->options.structure.members[i];
                }
            }
            auto context = expression_context_make_unknown();
            auto arg_info = get_info(argument, true);
            if (found_member != 0)
            {
                arg_info->valid = true;
                arg_info->member = *found_member;
                context = expression_context_make_specific_type(found_member->type);
            }
            else {
                arg_info->valid = false;
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_DOES_NOT_EXIST, AST::upcast(argument));
                semantic_analyser_add_error_info(error_information_make_id(member_id));
                // TODO: Find out if we want to analyse expressions even if we don't have the context for it
            }
            semantic_analyser_analyse_expression_value(argument->value, context);
        }

        // Check for errors (Different for unions/structs)
        if (struct_signature->options.structure.struct_type != AST::Structure_Type::STRUCT)
        {
            if (init_node.arguments.size == 0) {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING, expr);
            }
            else if (init_node.arguments.size != 1) {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_CAN_ONLY_SET_ONE_UNION_MEMBER, expr);
            }
            else if (struct_signature->options.structure.struct_type == AST::Structure_Type::UNION)
            {
                auto& member = get_info(init_node.arguments[0])->member;
                if (member.offset == struct_signature->options.structure.tag_member.offset) {
                    semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_CANNOT_SET_UNION_TAG, AST::upcast(init_node.arguments[0]));
                }
            }
        }
        else
        {
            // Check that all members aren't initilized more than once
            int valid_count = 0;
            for (int i = 0; i < init_node.arguments.size; i++)
            {
                auto info = get_info(init_node.arguments[i]);
                if (!info->valid) continue;
                valid_count += 1;
                for (int j = i + 1; j < init_node.arguments.size; j++)
                {
                    auto other = get_info(init_node.arguments[j]);
                    if (!other->valid) continue;
                    if (info->member.id == other->member.id) {
                        semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_INITIALIZED_TWICE, AST::upcast(init_node.arguments[j]));
                        semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_INITIALIZED_TWICE, AST::upcast(init_node.arguments[i]));
                    }
                }
            }
            // Check if all members are initiliazed
            if (valid_count != struct_signature->options.structure.members.size) {
                semantic_analyser_log_error(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING, expr);
            }
        }

        EXIT_VALUE(struct_signature);
    }
    case AST::Expression_Type::ARRAY_INITIALIZER:
    {
        auto& init_node = expr->options.array_initializer;
        Type_Signature* element_type = 0;
        if (init_node.type_expr.available) {
            element_type = semantic_analyser_analyse_expression_type(init_node.type_expr.value);
        }
        else
        {
            if (context.type == Expression_Context_Type::SPECIFIC_TYPE)
            {
                if (context.signature->type == Signature_Type::ARRAY) {
                    element_type = context.signature->options.array.element_type;
                }
                else if (context.signature->type == Signature_Type::SLICE) {
                    element_type = context.signature->options.slice.element_type;
                }
                else {
                    element_type = 0;
                }
            }
            if (element_type == 0) {
                semantic_analyser_log_error(Semantic_Error_Type::ARRAY_AUTO_INITIALIZER_COULD_NOT_DETERMINE_TYPE, expr);
                EXIT_ERROR(types.unknown_type);
            }
        }

        assert(!(element_type->size == 0 && element_type->alignment == 0), "");
        int array_element_count = init_node.values.size;
        // There are no 0-sized arrays, only 0-sized slices. So if we encounter an empty initializer, e.g. type.[], we return an empty slice
        if (array_element_count == 0) {
            EXIT_VALUE(type_system_make_slice(type_system, element_type));
        }

        for (int i = 0; i < init_node.values.size; i++) {
            semantic_analyser_analyse_expression_value(init_node.values[i], expression_context_make_specific_type(element_type));
        }
        EXIT_VALUE(type_system_make_array(type_system, element_type, true, array_element_count));
    }
    case AST::Expression_Type::ARRAY_ACCESS:
    {
        auto& access_node = expr->options.array_access;
        auto array_type = semantic_analyser_analyse_expression_value(
            access_node.array_expr, expression_context_make_auto_dereference()
        );

        Type_Signature* element_type = 0;
        if (array_type->type == Signature_Type::ARRAY) {
            element_type = array_type->options.array.element_type;
        }
        else if (array_type->type == Signature_Type::SLICE) {
            element_type = array_type->options.slice.element_type;
        }
        else {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS, expr);
            semantic_analyser_add_error_info(error_information_make_given_type(array_type));
            element_type = types.unknown_type;
        }

        semantic_analyser_analyse_expression_value(
            access_node.index_expr, expression_context_make_specific_type(types.i32_type)
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
            Type_Signature* enum_type = access_expr_info->options.type;
            if (enum_type->type == Signature_Type::STRUCT && enum_type->options.structure.struct_type == AST::Structure_Type::UNION) {
                enum_type = enum_type->options.structure.tag_member.type;
            }
            if (enum_type->type != Signature_Type::ENUM) {
                semantic_analyser_log_error(Semantic_Error_Type::OTHERS_TYPE_MEMBER_ACCESS_MUST_BE_ENUM, member_node.expr);
                semantic_analyser_add_error_info(error_information_make_given_type(enum_type));
                EXIT_ERROR(types.unknown_type);
            }

            Enum_Item* found = 0;
            for (int i = 0; i < enum_type->options.enum_type.members.size; i++) {
                auto member = &enum_type->options.enum_type.members[i];
                if (member->id == member_node.name) {
                    found = member;
                    break;
                }
            }

            int value = 0;
            if (found == 0) {
                semantic_analyser_log_error(Semantic_Error_Type::ENUM_DOES_NOT_CONTAIN_THIS_MEMBER, member_node.expr);
                semantic_analyser_add_error_info(error_information_make_id(member_node.name));
            }
            else {
                value = found->value;
            }
            expression_info_set_constant_enum(info, enum_type, value);
            return info;
        }
        case Expression_Result_Type::FUNCTION:
        case Expression_Result_Type::HARDCODED_FUNCTION:
        case Expression_Result_Type::POLYMORPHIC_FUNCTION: {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_EXPRESSION_TYPE, member_node.expr);
            semantic_analyser_add_error_info(error_information_make_expression_result_type(access_expr_info->result_type));
            EXIT_ERROR(types.unknown_type);
        }
        case Expression_Result_Type::VALUE:
        case Expression_Result_Type::CONSTANT:
        {
            auto struct_signature = access_expr_info->context_ops.after_cast_type;
            if (struct_signature->type == Signature_Type::STRUCT)
            {
                Struct_Member* found = 0;
                for (int i = 0; i < struct_signature->options.structure.members.size; i++) {
                    Struct_Member* member = &struct_signature->options.structure.members[i];
                    if (member->id == member_node.name) {
                        found = member;
                    }
                }
                if (found == 0) {
                    semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND, expr);
                    semantic_analyser_add_error_info(error_information_make_id(member_node.name));
                    EXIT_ERROR(types.unknown_type);
                }
                EXIT_VALUE(found->type);
            }
            else if (struct_signature->type == Signature_Type::ARRAY || struct_signature->type == Signature_Type::SLICE)
            {
                if (member_node.name != compiler.id_size && member_node.name != compiler.id_data) {
                    semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND, expr);
                    semantic_analyser_add_error_info(error_information_make_id(member_node.name));
                    EXIT_ERROR(types.unknown_type);
                }

                if (struct_signature->type == Signature_Type::ARRAY)
                {
                    if (member_node.name == compiler.id_size) {
                        if (struct_signature->options.array.count_known) {
                            expression_info_set_constant_i32(info, struct_signature->options.array.element_count);
                        }
                        else {
                            EXIT_ERROR(types.i32_type);
                        }
                        return info;
                    }
                    else
                    { // Data access
                        EXIT_VALUE(type_system_make_pointer(type_system, struct_signature->options.array.element_type));
                    }
                }
                else // Slice
                {
                    Struct_Member member;
                    if (member_node.name == compiler.id_size) {
                        member = struct_signature->options.slice.size_member;
                    }
                    else {
                        member = struct_signature->options.slice.data_member;
                    }
                    EXIT_VALUE(member.type);
                }
            }
            else
            {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS, expr);
                semantic_analyser_add_error_info(error_information_make_given_type(struct_signature));
                EXIT_ERROR(types.unknown_type);
            }
            panic("");
            break;
        }
        default: panic("");
        }
        panic("Should not happen");
        EXIT_ERROR(types.unknown_type);
    }
    case AST::Expression_Type::AUTO_ENUM:
    {
        String* id = expr->options.auto_enum;
        if (context.type != Expression_Context_Type::SPECIFIC_TYPE) {
            semantic_analyser_log_error(Semantic_Error_Type::AUTO_MEMBER_KNOWN_CONTEXT_IS_REQUIRED, expr);
            EXIT_ERROR(types.unknown_type);
        }
        if (context.signature->type != Signature_Type::ENUM) {
            semantic_analyser_log_error(Semantic_Error_Type::AUTO_MEMBER_MUST_BE_IN_ENUM_CONTEXT, expr);
            EXIT_ERROR(types.unknown_type);
        }

        Type_Signature* enum_type = context.signature;
        Enum_Item* found = 0;
        for (int i = 0; i < enum_type->options.enum_type.members.size; i++) {
            auto member = &enum_type->options.enum_type.members[i];
            if (member->id == id) {
                found = member;
                break;
            }
        }

        int value = 0;
        if (found == 0) {
            semantic_analyser_log_error(Semantic_Error_Type::ENUM_DOES_NOT_CONTAIN_THIS_MEMBER, expr);
            semantic_analyser_add_error_info(error_information_make_id(id));
        }
        else {
            value = found->value;
        }

        expression_info_set_constant_enum(info, enum_type, value);
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
                is_negate ? expression_context_make_auto_dereference() : expression_context_make_specific_type(types.bool_type)
            );

            if (is_negate)
            {
                bool valid = false;
                if (operand_type->type == Signature_Type::PRIMITIVE) {
                    valid = operand_type->options.primitive.is_signed && operand_type->options.primitive.type != Primitive_Type::BOOLEAN;
                }
                if (!valid) {
                    semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR, expr);
                    semantic_analyser_add_error_info(error_information_make_given_type(operand_type));
                }
            }

            EXIT_VALUE(is_negate ? operand_type : types.bool_type);
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
                // Check if it's actually a type, since the * can be used both as address of and as pointer type...
                // Note: Maybe this should already be done in analyse_expression_any, but I'm not sure yet
                auto maybe_type = semantic_analyser_try_convert_value_to_type(unary_node.expr, false);
                if (maybe_type != 0) {
                    EXIT_TYPE(type_system_make_pointer(type_system, maybe_type));
                }

                Type_Signature* operand_type = operand_result->context_ops.after_cast_type;
                if (!expression_has_memory_address(unary_node.expr)) {
                    semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT, expr);
                }
                EXIT_VALUE(type_system_make_pointer(type_system, operand_type));
            }
            case Expression_Result_Type::TYPE: {
                EXIT_TYPE(type_system_make_pointer(type_system, operand_result->options.type));
            }
            case Expression_Result_Type::FUNCTION:
            case Expression_Result_Type::POLYMORPHIC_FUNCTION:
            case Expression_Result_Type::HARDCODED_FUNCTION: {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_EXPRESSION_TYPE, expr);
                semantic_analyser_add_error_info(error_information_make_expression_result_type(operand_result->result_type));
                EXIT_ERROR(types.unknown_type);
            }
            default: panic("");
            }
            panic("");
            break;
        }
        case AST::Unop::DEREFERENCE:
        {
            auto operand_type = semantic_analyser_analyse_expression_value(unary_node.expr, expression_context_make_unknown());
            Type_Signature* result_type = types.unknown_type;
            if (operand_type->type != Signature_Type::POINTER || type_signature_equals(operand_type, types.void_ptr_type)) {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR, expr);
                semantic_analyser_add_error_info(error_information_make_given_type(operand_type));
            }
            else {
                result_type = operand_type->options.pointer_child;
            }
            EXIT_VALUE(result_type);
        }
        default:panic("");
        }
        panic("");
        EXIT_ERROR(types.unknown_type);
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
            operand_context = expression_context_make_specific_type(types.bool_type);
            break;
        case AST::Binop::INVALID:
            operand_context = expression_context_make_unknown();
            break;
        default: panic("");
        }

        // Evaluate operands
        auto left_type = semantic_analyser_analyse_expression_value(binop_node.left, operand_context);
        if (enum_valid && left_type->type == Signature_Type::ENUM) {
            operand_context = expression_context_make_specific_type(left_type);
        }
        auto right_type = semantic_analyser_analyse_expression_value(binop_node.right, operand_context);

        // Check for unknowns
        if (type_signature_equals(left_type, types.unknown_type) || type_signature_equals(right_type, types.unknown_type)) {
            EXIT_ERROR(types.unknown_type);
        }

        // Try implicit casting if types dont match
        Type_Signature* operand_type = left_type;
        bool types_are_valid = true;

        if (!type_signature_equals(left_type, right_type))
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
            if (operand_type->type == Signature_Type::POINTER) {
                types_are_valid = ptr_valid;
            }
            else if (operand_type->type == Signature_Type::PRIMITIVE)
            {
                if (operand_type->options.primitive.type == Primitive_Type::INTEGER && !int_valid) {
                    types_are_valid = false;
                }
                if (operand_type->options.primitive.type == Primitive_Type::FLOAT && !float_valid) {
                    types_are_valid = false;
                }
                if (operand_type->options.primitive.type == Primitive_Type::BOOLEAN && !bool_valid) {
                    types_are_valid = false;
                }
            }
            else if (operand_type->type == Signature_Type::ENUM) {
                types_are_valid = enum_valid;
            }
            else if (operand_type->type == Signature_Type::TYPE_TYPE) {
                types_are_valid = type_type_valid;
            }
            else {
                types_are_valid = false;
            }
        }

        if (!types_are_valid) {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR, expr);
            semantic_analyser_add_error_info(error_information_make_binary_op_type(left_type, right_type));
        }
        EXIT_VALUE(result_type_is_bool ? types.bool_type : operand_type);
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

void expression_context_apply(AST::Expression* expr, Expression_Context context)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto info = get_info(expr);
    SET_ACTIVE_EXPR_INFO(info);
    auto initial_type = expression_info_get_type(info);
    auto final_type = initial_type;

    // Special Case Handling: Expression_Statements are the only things which can expect void type
    if (type_signature_equals(initial_type, types.void_type)) {
        if (!(context.type == Expression_Context_Type::SPECIFIC_TYPE && type_signature_equals(context.signature, types.void_type))) {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, expr);
        }
        info->context_ops.after_cast_type = types.unknown_type;
        return;
    }
    if (context.type == Expression_Context_Type::SPECIFIC_TYPE && type_signature_equals(context.signature, types.void_type)) {
        context.type = Expression_Context_Type::UNKNOWN;
    }

    // Do nothing if context is unknown
    if (context.type == Expression_Context_Type::UNKNOWN) return;

    // Check for unknowns
    if (type_signature_equals(initial_type, types.unknown_type) ||
        (context.type == Expression_Context_Type::SPECIFIC_TYPE && type_signature_equals(context.signature, types.unknown_type))) {
        semantic_analyser_set_error_flag(true);
        info->context_ops.after_cast_type = types.unknown_type;
        return;
    }

    // Auto pointer dereferencing/address of
    {
        int wanted_pointer_depth = 0;
        switch (context.type)
        {
        case Expression_Context_Type::UNKNOWN:
            return;
        case Expression_Context_Type::AUTO_DEREFERENCE:
            wanted_pointer_depth = 0;
            break;
        case Expression_Context_Type::SPECIFIC_TYPE:
        {
            Type_Signature* temp = context.signature;
            wanted_pointer_depth = 0;
            while (temp->type == Signature_Type::POINTER) {
                wanted_pointer_depth++;
                temp = temp->options.pointer_child;
            }
            break;
        }
        default: panic("");
        }

        // Find out given pointer level
        int given_pointer_depth = 0;
        Type_Signature* iter = initial_type;
        while (iter->type == Signature_Type::POINTER) {
            given_pointer_depth++;
            iter = iter->options.pointer_child;
        }

        if (given_pointer_depth + 1 == wanted_pointer_depth && context.type == Expression_Context_Type::SPECIFIC_TYPE)
        {
            // Auto address of
            info->context_ops.take_address_of = true;
            final_type = type_system_make_pointer(&type_system, initial_type);
        }
        else
        {
            // Auto-Dereference to given level
            info->context_ops.deref_count = given_pointer_depth - wanted_pointer_depth;
            for (int i = 0; i < info->context_ops.deref_count; i++) {
                assert(final_type->type == Signature_Type::POINTER, "");
                final_type = final_type->options.pointer_child;
            }
        }
    }

    info->context_ops.after_cast_type = final_type;
    // Implicit casting
    if (context.type == Expression_Context_Type::SPECIFIC_TYPE)
    {
        if (!type_signature_equals(final_type, context.signature))
        {
            Info_Cast_Type cast_type = semantic_analyser_check_cast_type(final_type, context.signature, true);
            if (cast_type == Info_Cast_Type::INVALID) {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE, expr);
                semantic_analyser_add_error_info(error_information_make_given_type(initial_type));
                semantic_analyser_add_error_info(error_information_make_expected_type(context.signature));
            }
            expression_info_set_cast(info, cast_type, context.signature);
        }
    }
}

Expression_Info* semantic_analyser_analyse_expression_any(AST::Expression* expression, Expression_Context context)
{
    auto& type_system = compiler.type_system;
    auto result = semantic_analyser_analyse_expression_internal(expression, context);
    SET_ACTIVE_EXPR_INFO(result);
    if (result->result_type != Expression_Result_Type::VALUE && result->result_type != Expression_Result_Type::CONSTANT) return result;
    expression_context_apply(expression, context);
    return result;
}

Type_Signature* semantic_analyser_try_convert_value_to_type(AST::Expression* expression, bool log_error)
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
        if (type_signature_equals(result->context_ops.after_cast_type, types.unknown_type)) {
            semantic_analyser_set_error_flag(true);
            return types.unknown_type;
        }
        if (!type_signature_equals(result->context_ops.after_cast_type, types.type_type))
        {
            if (log_error) {
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_IS_NOT_A_TYPE, expression);
                semantic_analyser_add_error_info(error_information_make_given_type(result->context_ops.after_cast_type));
                return types.unknown_type;
            }
            else {
                return 0;
            }
        }

        if (result->result_type == Expression_Result_Type::VALUE)
        {
            Comptime_Result comptime = expression_calculate_comptime_value(expression);
            Type_Signature* result_type = types.unknown_type;
            switch (comptime.type)
            {
            case Comptime_Result_Type::AVAILABLE:
            {
                u64 type_index = *(u64*)comptime.data;
                if (type_index >= type_system.internal_type_infos.size) {
                    if (log_error) {
                        semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE, expression);
                        return types.unknown_type;
                    }
                    else {
                        return 0;
                    }
                }
                return type_system.types[type_index];
            }
            case Comptime_Result_Type::UNAVAILABLE:
                return types.unknown_type;
            case Comptime_Result_Type::NOT_COMPTIME:
                if (log_error) {
                    semantic_analyser_log_error(Semantic_Error_Type::TYPE_NOT_KNOWN_AT_COMPILE_TIME, expression);
                    return types.unknown_type;
                }
                else {
                    return 0;
                }
            default: panic("");
            }
        }
        else {
            auto type_index = upp_constant_to_value<u64>(&compiler.constant_pool, result->options.constant);
            if (type_index >= type_system.internal_type_infos.size) {
                // Note: Always log this error, because this should never happen!
                semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE, expression);
                return types.unknown_type;
            }
            return type_system.types[type_index];
        }

        panic("");
        break;
    }
    case Expression_Result_Type::HARDCODED_FUNCTION:
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    case Expression_Result_Type::FUNCTION: {
        if (log_error) {
            semantic_analyser_log_error(Semantic_Error_Type::EXPECTED_TYPE, expression);
            semantic_analyser_add_error_info(error_information_make_expression_result_type(result->result_type));
            return types.unknown_type;
        }
        return 0;
    }
    default: panic("");
    }
    panic("Shouldn't happen");
    return 0;
}

Type_Signature* semantic_analyser_analyse_expression_type(AST::Expression* expression)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto result = semantic_analyser_analyse_expression_any(expression, expression_context_make_auto_dereference());
    SET_ACTIVE_EXPR_INFO(result);
    return semantic_analyser_try_convert_value_to_type(expression, true);
}

Type_Signature* semantic_analyser_analyse_expression_value(AST::Expression* expression, Expression_Context context)
{
    auto& type_system = compiler.type_system;
    auto& types = type_system.predefined_types;
    auto result = semantic_analyser_analyse_expression_any(expression, context);
    SET_ACTIVE_EXPR_INFO(result);
    switch (result->result_type)
    {
    case Expression_Result_Type::VALUE: {
        return result->context_ops.after_cast_type; // Here context was already applied, so we return
    }
    case Expression_Result_Type::TYPE: {
        expression_info_set_constant(result, types.type_type, array_create_static_as_bytes(&result->options.type->internal_index, 1), AST::upcast(expression));
        break;
    }
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
        semantic_analyser_log_error(Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION, expression);
        return types.unknown_type;
    }
    case Expression_Result_Type::POLYMORPHIC_FUNCTION:
    {
        semantic_analyser_log_error(Semantic_Error_Type::EXPECTED_VALUE, expression);
        semantic_analyser_add_error_info(error_information_make_expression_result_type(result->result_type));
        return types.unknown_type;
    }
    default: panic("");
    }

    expression_context_apply(expression, context);
    return result->context_ops.after_cast_type;
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
        Type_Signature* expected_return_type = current_function->signature->options.function.return_type;

        if (return_stat.available)
        {
            Expression_Context context;
            if (analyser.current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS) {
                context = expression_context_make_unknown();
            }
            else {
                context = expression_context_make_specific_type(expected_return_type);
            }
            auto return_type = semantic_analyser_analyse_expression_value(return_stat.value, context);
            bool is_unknown = type_signature_equals(return_type, types.unknown_type);

            if (analyser.current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS)
            {
                if (type_signature_equals(expected_return_type, types.void_type)) {
                    current_function->signature = type_system_make_function(&type_system, {}, return_type);
                }
                else if (!type_signature_equals(expected_return_type, return_type) && !is_unknown) {
                    semantic_analyser_log_error(Semantic_Error_Type::BAKE_BLOCK_RETURN_TYPE_DIFFERS_FROM_PREVIOUS_RETURN, statement);
                    semantic_analyser_add_error_info(error_information_make_given_type(return_type));
                    semantic_analyser_add_error_info(error_information_make_expected_type(expected_return_type));
                }
            }
            else if (!type_signature_equals(expected_return_type, return_type) && !is_unknown) {
                semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_RETURN, statement);
                semantic_analyser_add_error_info(error_information_make_expected_type(types.void_type));
                semantic_analyser_add_error_info(error_information_make_given_type(return_type));
            }
        }
        else
        {
            if (analyser.current_workload->type == Analysis_Workload_Type::BAKE_ANALYSIS) {
                semantic_analyser_log_error(Semantic_Error_Type::BAKE_BLOCK_RETURN_MUST_NOT_BE_EMPTY, statement);
            }
            else
            {
                if (!type_signature_equals(expected_return_type, types.void_type)) {
                    semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_RETURN, statement);
                    semantic_analyser_add_error_info(error_information_make_given_type(types.void_type));
                    semantic_analyser_add_error_info(error_information_make_expected_type(expected_return_type));
                }
            }
        }

        if (inside_defer()) {
            semantic_analyser_log_error(Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED, statement);
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
            semantic_analyser_log_error(
                is_continue ? Semantic_Error_Type::CONTINUE_LABEL_NOT_FOUND : Semantic_Error_Type::BREAK_LABLE_NOT_FOUND, statement
            );
            semantic_analyser_add_error_info(error_information_make_id(search_id));
            EXIT(Control_Flow::RETURNS);
        }
        else
        {
            info->specifics.block = found_block;
            if (is_continue && !code_block_is_while(found_block)) {
                semantic_analyser_log_error(Semantic_Error_Type::CONTINUE_REQUIRES_LOOP_BLOCK, statement);
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
            semantic_analyser_log_error(Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS, statement);
            EXIT(Control_Flow::SEQUENTIAL);
        }
        EXIT(Control_Flow::SEQUENTIAL);
    }
    case AST::Statement_Type::EXPRESSION_STATEMENT:
    {
        auto& expression_node = statement->options.expression;
        if (expression_node->type != AST::Expression_Type::FUNCTION_CALL) {
            semantic_analyser_log_error(Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL, statement);
        }
        // Note(Martin): This is a special case, in expression statements a void_type is valid
        semantic_analyser_analyse_expression_value(expression_node, expression_context_make_specific_type(types.void_type));
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
            if_node.condition, expression_context_make_specific_type(types.bool_type)
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
        if (switch_type->type == Signature_Type::STRUCT && switch_type->options.structure.struct_type == AST::Structure_Type::UNION) {
            switch_type = switch_type->options.structure.tag_member.type;
        }
        else if (switch_type->type != Signature_Type::ENUM)
        {
            semantic_analyser_log_error(Semantic_Error_Type::SWITCH_REQUIRES_ENUM, switch_node.condition);
            semantic_analyser_add_error_info(error_information_make_given_type(switch_type));
        }

        Expression_Context case_context = switch_type->type == Signature_Type::ENUM ?
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
                Comptime_Result comptime = expression_calculate_comptime_value(case_node->value.value);
                switch (comptime.type)
                {
                case Comptime_Result_Type::AVAILABLE:
                    case_info->case_value = *((int*)comptime.data);
                    case_info->is_valid = true;
                    break;
                case Comptime_Result_Type::NOT_COMPTIME:
                    case_info->is_valid = false;
                    case_info->case_value = -1;
                    break;
                case Comptime_Result_Type::UNAVAILABLE:
                    semantic_analyser_set_error_flag(true);
                    break;
                default: panic("");
                }
                if (comptime.type == Comptime_Result_Type::NOT_COMPTIME) {
                    semantic_analyser_log_error(Semantic_Error_Type::SWITCH_CASES_MUST_BE_COMPTIME_KNOWN, case_node->value.value);
                    break;
                }
            }
            else
            {
                // Default case
                if (default_found) {
                    semantic_analyser_log_error(Semantic_Error_Type::SWITCH_ONLY_ONE_DEFAULT_ALLOWED, statement);
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
                    semantic_analyser_log_error(Semantic_Error_Type::SWITCH_CASE_MUST_BE_UNIQUE, AST::upcast(other_case));
                    is_unique = false;
                    break;
                }
            }
            if (is_unique) {
                unique_count++;
            }
        }

        // Check if all possible cases are handled
        if (!default_found && switch_type->type == Signature_Type::ENUM) {
            if (unique_count < switch_type->options.enum_type.members.size) {
                semantic_analyser_log_error(Semantic_Error_Type::SWITCH_MUST_HANDLE_ALL_CASES, statement);
            }
        }
        return switch_flow;
    }
    case AST::Statement_Type::WHILE_STATEMENT:
    {
        auto& while_node = statement->options.while_statement;
        semantic_analyser_analyse_expression_value(while_node.condition, expression_context_make_specific_type(types.bool_type));

        auto flow = semantic_analyser_analyse_block(while_node.block);
        if (flow == Control_Flow::RETURNS) {
            EXIT(flow);
        }
        EXIT(Control_Flow::SEQUENTIAL); // While is always sequential, since the condition may be wrong
    }
    case AST::Statement_Type::DELETE_STATEMENT:
    {
        auto delete_type = semantic_analyser_analyse_expression_value(statement->options.delete_expr, expression_context_make_unknown());
        if (delete_type->type != Signature_Type::POINTER && delete_type->type != Signature_Type::SLICE) {
            semantic_analyser_log_error(Semantic_Error_Type::INVALID_TYPE_DELETE, statement->options.delete_expr);
            semantic_analyser_add_error_info(error_information_make_given_type(delete_type));
        }
        EXIT(Control_Flow::SEQUENTIAL);
    }
    case AST::Statement_Type::ASSIGNMENT:
    {
        auto& assignment_node = statement->options.assignment;
        auto left_type = semantic_analyser_analyse_expression_value(assignment_node.left_side, expression_context_make_unknown());
        auto right_type = semantic_analyser_analyse_expression_value(assignment_node.right_side, expression_context_make_specific_type(left_type));
        if (!expression_has_memory_address(assignment_node.left_side)) {
            semantic_analyser_log_error(Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS, assignment_node.left_side);
        }
        EXIT(Control_Flow::SEQUENTIAL);
    }
    case AST::Statement_Type::DEFINITION:
    {
        auto definition = statement->options.definition;
        if (definition->is_comptime) {
            // NOTE: This is already handled at block start
            EXIT(Control_Flow::SEQUENTIAL);
        }
        assert(!(!definition->value.available && !definition->type.available), "");

        Type_Signature* type = 0;
        if (definition->type.available) {
            type = semantic_analyser_analyse_expression_type(definition->type.value);
        }
        if (definition->value.available)
        {
            auto value_type = semantic_analyser_analyse_expression_value(
                definition->value.value, type != 0 ? expression_context_make_specific_type(type) : expression_context_make_unknown()
            );
            if (type == 0) {
                type = value_type;
            }
        }

        Symbol* symbol = get_info(definition)->symbol;
        assert(symbol->type == Symbol_Type::VARIABLE_UNDEFINED, "Variable should be undefined here");
        symbol->type = Symbol_Type::VARIABLE;
        symbol->options.variable_type = type;
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

    // Analyse order independent symbols inside code-block (Comptimes and variables)
    {
        auto progress = semantic_analyser.current_workload->current_function->progress;
        bool define_comptimes = progress->poly_instance != 0 && progress->poly_instance->instance_index != 0;
        for (int i = 0; i < block->statements.size; i++) {
            if (block->statements[i]->type == AST::Statement_Type::DEFINITION) {
                auto definition = block->statements[i]->options.definition;
                if (!(definition->is_comptime && define_comptimes)) {
                    analyser_create_symbol_and_workload_for_definition(definition);
                }
            }
        }
    }

    auto& block_stack = semantic_analyser.current_workload->block_stack;
    if (block->block_id.available)
    {
        for (int i = 0; i < block_stack.size; i++) {
            auto prev = block_stack[i];
            if (prev != 0 && prev->block_id.available && prev->block_id.value == block->block_id.value) {
                semantic_analyser_log_error(Semantic_Error_Type::LABEL_ALREADY_IN_USE, &block->base);
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
    Symbol** main_symbol_opt = hashtable_find_element(&semantic_analyser.root_module->module_analysis->symbol_table->symbols, compiler.id_main);
    ModTree_Function* main_function = 0;
    if (main_symbol_opt == 0) {
        semantic_analyser_log_error(Semantic_Error_Type::MAIN_NOT_DEFINED, (AST::Node*)0);
    }
    else
    {
        auto main_symbol = *main_symbol_opt;
        if (main_symbol->type != Symbol_Type::FUNCTION) {
            semantic_analyser_log_error(Semantic_Error_Type::MAIN_MUST_BE_FUNCTION, main_symbol->definition_node);
            semantic_analyser_add_error_info(error_information_make_symbol(main_symbol));
        }
        else {
            main_function = main_symbol->options.function->function;
        }
    }
    semantic_analyser.program->main_function = main_function;
}

void semantic_analyser_reset()
{
    auto& type_system = compiler.type_system;

    // Reset analyser data
    {
        semantic_analyser.root_module = 0;
        semantic_analyser.current_workload = 0;

        // Errors
        for (int i = 0; i < semantic_analyser.errors.size; i++) {
            dynamic_array_destroy(&semantic_analyser.errors[i].information);
        }
        dynamic_array_reset(&semantic_analyser.errors);

        // Allocators
        stack_allocator_reset(&semantic_analyser.allocator_values);

        // Modtree-Program
        for (int i = 0; i < semantic_analyser.polymorphic_functions.size; i++) {
            polymorphic_base_destroy(semantic_analyser.polymorphic_functions[i]);
        }
        dynamic_array_reset(&semantic_analyser.polymorphic_functions);

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

        auto define_type_symbol = [&](const char* name, Type_Signature* type) -> Symbol* {
            Symbol* result = symbol_table_define_symbol(root, identifier_pool_add(pool, string_create_static(name)), Symbol_Type::TYPE, 0, false);
            result->options.type = type;
            if (type->type == Signature_Type::STRUCT) {
                type->options.structure.symbol = result;
            }
            return result;
        };
        symbols.type_bool = define_type_symbol("bool", types.bool_type);
        symbols.type_int = define_type_symbol("int", types.i32_type);
        symbols.type_float = define_type_symbol("float", types.f32_type);
        symbols.type_u8 = define_type_symbol("u8", types.u8_type);
        symbols.type_u16 = define_type_symbol("u16", types.u16_type);
        symbols.type_u32 = define_type_symbol("u32", types.u32_type);
        symbols.type_u64 = define_type_symbol("u64", types.u64_type);
        symbols.type_i8 = define_type_symbol("i8", types.i8_type);
        symbols.type_i16 = define_type_symbol("i16", types.i16_type);
        symbols.type_i32 = define_type_symbol("i32", types.i32_type);
        symbols.type_i64 = define_type_symbol("i64", types.i64_type);
        symbols.type_f32 = define_type_symbol("f32", types.f32_type);
        symbols.type_f64 = define_type_symbol("f64", types.f64_type);
        symbols.type_byte = define_type_symbol("byte", types.u8_type);
        symbols.type_void = define_type_symbol("void", types.void_type);
        symbols.type_string = define_type_symbol("String", types.string_type);
        symbols.type_type = define_type_symbol("Type", types.type_type);
        symbols.type_type_information = define_type_symbol("Type_Information", types.type_information_type);
        symbols.type_any = define_type_symbol("Any", types.any_type);
        symbols.type_empty = define_type_symbol("_", types.empty_struct_type);

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
        symbols.error_symbol = define_type_symbol("0_ERROR_SYMBOL", types.unknown_type);
        symbols.error_symbol->type = Symbol_Type::ERROR_SYMBOL;
    }

    // Add predefined Globals
    semantic_analyser.global_type_informations = modtree_program_add_global(
        type_system_make_slice(&type_system, compiler.type_system.predefined_types.type_information_type)
    );
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
    semantic_analyser.allocator_values = stack_allocator_create_empty(2048);
    semantic_analyser.workload_executer = workload_executer_initialize();
    semantic_analyser.polymorphic_functions = dynamic_array_create_empty<Polymorphic_Base*>(1);
    semantic_analyser.program = 0;
    semantic_analyser.ast_to_pass_mapping = hashtable_create_pointer_empty<AST::Node*, Node_Passes>(1);
    semantic_analyser.symbol_lookup_visited = hashset_create_pointer_empty<Symbol_Table*>(1);
    semantic_analyser.allocated_passes = dynamic_array_create_empty<Analysis_Pass*>(1);
    semantic_analyser.ast_to_info_mapping = hashtable_create_empty<AST_Info_Key, Analysis_Info*>(1, ast_info_key_hash, ast_info_equals);
    semantic_analyser.allocated_symbol_tables = dynamic_array_create_empty<Symbol_Table*>(16);
    semantic_analyser.allocated_symbols = dynamic_array_create_empty<Symbol*>(16);
    return &semantic_analyser;
}

void semantic_analyser_destroy()
{
    hashset_destroy(&semantic_analyser.symbol_lookup_visited);

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

    for (int i = 0; i < semantic_analyser.polymorphic_functions.size; i++) {
        polymorphic_base_destroy(semantic_analyser.polymorphic_functions[i]);
    }
    dynamic_array_destroy(&semantic_analyser.polymorphic_functions);

    stack_allocator_destroy(&semantic_analyser.allocator_values);

    if (semantic_analyser.program != 0) {
        modtree_program_destroy();
    }
}



// ERRORS
void semantic_error_get_infos_internal(Semantic_Error e, const char** result_str, Parser::Section* result_section)
{
    const char* string = "";
    Parser::Section section;
#define HANDLE_CASE(type, msg, sec) \
    case type: \
        string = msg;\
        section = sec; \
        break;

    switch (e.type)
    {
        HANDLE_CASE(Semantic_Error_Type::BAKE_BLOCK_RETURN_MUST_NOT_BE_EMPTY, "Return inside bake must not be empty", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::BAKE_BLOCK_RETURN_TYPE_DIFFERS_FROM_PREVIOUS_RETURN, "Return type differs from previous return type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE, "Invalid Type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE, "Invalid Type Handle!", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::TYPE_NOT_KNOWN_AT_COMPILE_TIME, "Type not known at compile time", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_IS_NOT_A_TYPE, "Expression used as a type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_EXPRESSION_TYPE, "Expression type not valid in this context", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPECTED_CALLABLE, "Expected: Callable", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPECTED_TYPE, "Expected: Type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPECTED_VALUE, "Expected: Value", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::TEMPLATE_ARGUMENTS_INVALID_COUNT, "Invalid Template Argument count", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE, "Template arguments invalid", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_MEMBER_ACCESS_INVALID_ON_FUNCTION, "Functions do not have any members to access", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_TYPE_MEMBER_ACCESS_MUST_BE_ENUM, "Member access on types requires enums", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::TEMPLATE_ARGUMENTS_REQUIRED, "Symbol is templated", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_REQUIRES_ENUM, "Switch requires enum", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_CASES_MUST_BE_COMPTIME_KNOWN, "Switch case must be compile time known", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_MUST_HANDLE_ALL_CASES, "Switch does not handle all cases", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_MUST_NOT_BE_EMPTY, "Switch must not be empty", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_ONLY_ONE_DEFAULT_ALLOWED, "Switch only one default case allowed", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_CASE_MUST_BE_UNIQUE, "Switch case must be unique", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SWITCH_CASE_TYPE_INVALID, "Switch case type must be enum value", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL, "Extern header does not contain this symbol", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXTERN_HEADER_PARSING_FAILED, "Parsing extern header failed", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_VOID_USAGE, "Invalid use of void type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_FUNCTION_CALL, "Expected function pointer type on function call", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER, "Expected function type on function import", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ARGUMENT, "Argument type does not match function parameter type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ARRAY_INITIALIZER_REQUIRES_TYPE_SYMBOL, "Array initializer requires type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_REQUIRES_TYPE_SYMBOL, "Struct initilizer requires type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBERS_MISSING, "Struct member/s missing", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_CANNOT_SET_UNION_TAG, "Cannot set union tag in initializer", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_INITIALIZED_TWICE, "Member already initialized", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_TYPE_MUST_BE_STRUCT, "Initializer type must either be struct/union/enum", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_MEMBER_DOES_NOT_EXIST, "Struct does not contain member", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_INVALID_MEMBER_TYPE, "Member type does not match", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_INITIALIZER_CAN_ONLY_SET_ONE_UNION_MEMBER, "Only one union member may be active at one time", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::AUTO_STRUCT_INITIALIZER_COULD_NOT_DETERMINE_TYPE, "Auto struct type could not be determined by context", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ARRAY_AUTO_INITIALIZER_COULD_NOT_DETERMINE_TYPE, "Could not determine array type by context", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ARRAY_INITIALIZER_INVALID_TYPE, "Array initializer member invalid type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS, "Array access only works on array types", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ARRAY_ACCESS_INDEX, "Array access index must be of type i32", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ARRAY_ALLOCATION_SIZE, "Array allocation size must be of type i32", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ARRAY_SIZE, "Array size must be of type i32", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ON_MEMBER_ACCESS, "Member access only valid on struct/array or pointer to struct/array types", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_IF_CONDITION, "If condition must be boolean", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_WHILE_CONDITION, "While condition must be boolean", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_UNARY_OPERATOR, "Unary operator type invalid", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_BINARY_OPERATOR, "Binary operator types invalid", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ASSIGNMENT, "Invalid assignment type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_RETURN, "Invalid return type", Parser::Section::KEYWORD);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_DELETE, "Only pointer or unsized array types can be deleted", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_EXPECTED_TYPE_ON_TYPE_IDENTIFIER, "Expected Type symbol", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_ALREADY_DEFINED, "Symbol was already defined", Parser::Section::IDENTIFIER);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_MODULE_INVALID, "Expected Variable or Function symbol for Variable read", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_EXPECTED_MODUL_IN_IDENTIFIER_PATH, "Expected module in indentifier path", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_TABLE_UNRESOLVED_SYMBOL, "Could not resolve symbol", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED, "Symbol already defined", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::SYMBOL_TABLE_MODULE_ALREADY_DEFINED, "Module already defined", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_ENUM_VALUE, "Enum value must be of i32 type!", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_EXPECTED_POINTER, "Invalid type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::BAKE_FUNCTION_DID_NOT_SUCCEED, "Bake error", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::AUTO_MEMBER_MUST_BE_IN_ENUM_CONTEXT, "Auto Member must be in used in a Context where an enum is required", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::AUTO_MEMBER_KNOWN_CONTEXT_IS_REQUIRED, "Auto Member must be used inside known Context", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::AUTO_CAST_KNOWN_CONTEXT_IS_REQUIRED, "Auto cast not able to extract destination type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::BAKE_FUNCTION_MUST_NOT_REFERENCE_GLOBALS, "Bake function must not reference globals!", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::CONSTANT_POOL_ERROR, "Could not add value to constant pool", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_COMPTIME_DEFINITION, "Value does not match given type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_COMPTIME_KNOWN, "Comptime definition value must be known at compile time", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::COMPTIME_DEFINITION_REQUIRES_INITAL_VALUE, "Comptime definition requires initial value", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::COMPTIME_DEFINITION_MUST_BE_INFERED, "Comptime definitions must be infered!", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH, "Parameter count does not match argument count", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_CAST_PTR_REQUIRES_U64, "cast_ptr only casts from u64 to pointers", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::INVALID_TYPE_CAST_PTR_DESTINATION_MUST_BE_PTR, "cast_ptr only casts from u64 to pointers", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_INVALID_CAST, "Invalid cast", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_MEMBER_NOT_FOUND, "Struct/Array does not contain member", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::LABEL_ALREADY_IN_USE, "Label already in use", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::BREAK_LABLE_NOT_FOUND, "Label cannot be found", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::BREAK_NOT_INSIDE_LOOP_OR_SWITCH, "Break is not inside a loop or switch", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::CONTINUE_LABEL_NOT_FOUND, "Label cannot be found", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::CONTINUE_NOT_INSIDE_LOOP, "Continue is not inside a loop", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::CONTINUE_REQUIRES_LOOP_BLOCK, "Continue only works for loop lables", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_BINARY_OP_TYPES_MUST_MATCH, "Binary op types do not match and cannot be implicitly casted", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL, "Expression does not do anything", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_MUST_CONTAIN_MEMBER, "Struct must contain at least one member", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_MEMBER_ALREADY_DEFINED, "Struct member is already defined", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_MEMBER_MUST_NOT_HAVE_VALUE, "Struct member must not have value", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::STRUCT_MEMBER_REQUIRES_TYPE, "Struct member requires type", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_WHILE_ONLY_RUNS_ONCE, "While loop always exits", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_WHILE_ALWAYS_RETURNS, "While loop always returns", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_WHILE_NEVER_STOPS, "While loop always continues", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_STATEMENT_UNREACHABLE, "Unreachable statement", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_DEFER_NO_RETURNS_ALLOWED, "No returns allowed inside of defer", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_MISSING_RETURN_STATEMENT, "Function is missing a return statement", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_FUNCTION_HEADER, "Unfinished workload function header", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_CODE_BLOCK, "Unfinished workload code block", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_UNFINISHED_WORKLOAD_TYPE_SIZE, "Unfinished workload type size", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MAIN_CANNOT_BE_TEMPLATED, "Main function cannot be templated", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MAIN_MUST_BE_FUNCTION, "Main must be a function", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MAIN_NOT_DEFINED, "Main function not found", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MAIN_UNEXPECTED_SIGNATURE, "Main unexpected signature", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MAIN_CANNOT_BE_CALLED, "Cannot call main function again", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_COULD_NOT_LOAD_FILE, "Could not load file", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_MAIN, "Cannot take address of main", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION, "Cannot take address of hardcoded function", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS, "Left side of assignment does not have a memory address", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE_TEMPLATED_GLOBALS, "Templated globals not implemented yet", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE_NAMED_ARGUMENTS, "Named arguments aren't supported yet", Parser::Section::IDENTIFIER);
        HANDLE_CASE(Semantic_Error_Type::ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN, "Array size not known at compile time", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ARRAY_SIZE_MUST_BE_GREATER_ZERO, "Array size must be greater zero!", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE_NESTED_TEMPLATED_MODULES, "Nested template modules not implemented yet", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES, "Extern imports inside templates not allowed", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE_EXTERN_GLOBAL_IMPORT, "Extern global variable import not implemented yet", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE, "Missing feature", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ENUM_MEMBER_NAME_MUST_BE_UNIQUE, "Enum member name must be unique", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ENUM_VALUE_MUST_BE_COMPILE_TIME_KNOWN, "enum value must be compile time known", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ENUM_VALUE_MUST_BE_UNIQUE, "Enum value must be unique", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::ENUM_DOES_NOT_CONTAIN_THIS_MEMBER, "Enum member does not exist", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::CANNOT_TAKE_POINTER_OF_FUNCTION, "Cannot take pointer of function", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT, "Cannot take address of temporary result!", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::MISSING_FEATURE_NESTED_DEFERS, "Nested defers not implemented yet", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::CYCLIC_DEPENDENCY_DETECTED, "Cyclic workload dependencies detected", Parser::Section::WHOLE);
        HANDLE_CASE(Semantic_Error_Type::VARIABLE_NOT_DEFINED_YET, "Variable not defined yet", Parser::Section::WHOLE);
    default: panic("ERROR");
    }
#undef HANDLE_CASE
    if (result_str != 0) {
        *result_str = string;
    }
    if (result_section != 0) {
        *result_section = section;
    }
}

Parser::Section semantic_error_get_section(Semantic_Error e)
{
    Parser::Section result;
    semantic_error_get_infos_internal(e, 0, &result);
    return result;
}

void semantic_error_append_to_string(Semantic_Error e, String* string)
{
    const char* type_text;
    semantic_error_get_infos_internal(e, &type_text, 0);
    string_append_formated(string, type_text);

    for (int k = 0; k < e.information.size; k++)
    {
        Error_Information* info = &e.information[k];
        switch (info->type)
        {
        case Error_Information_Type::EXTRA_TEXT:
            string_append_formated(string, "\n %s", info->options.extra_text);
            break;
        case Error_Information_Type::CYCLE_WORKLOAD:
            string_append_formated(string, "\n  ");
            analysis_workload_append_to_string(info->options.cycle_workload, string);
            break;
        case Error_Information_Type::ARGUMENT_COUNT:
            string_append_formated(string, "\n  Given argument count: %d, required: %d",
                info->options.invalid_argument_count.given, info->options.invalid_argument_count.expected);
            break;
        case Error_Information_Type::ID:
            string_append_formated(string, "\n  Name: %s", info->options.id->characters);
            break;
        case Error_Information_Type::INVALID_MEMBER: {
            string_append_formated(string, "\n  Accessed member name: %s", info->options.invalid_member.member_id->characters);
            string_append_formated(string, "\n  Available struct members ");
            for (int i = 0; i < info->options.invalid_member.struct_signature->options.structure.members.size; i++) {
                Struct_Member* member = &info->options.invalid_member.struct_signature->options.structure.members[i];
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
            type_signature_append_to_string(string, info->options.type);
            break;
        case Error_Information_Type::EXPECTED_TYPE:
            string_append_formated(string, "\n  Expected Type: ");
            type_signature_append_to_string(string, info->options.type);
            break;
        case Error_Information_Type::FUNCTION_TYPE:
            string_append_formated(string, "\n  Function Type: ");
            type_signature_append_to_string(string, info->options.type);
            break;
        case Error_Information_Type::BINARY_OP_TYPES:
            string_append_formated(string, "\n  Left Operand type:  ");
            type_signature_append_to_string(string, info->options.binary_op_types.left_type);
            string_append_formated(string, "\n  Right Operand type: ");
            type_signature_append_to_string(string, info->options.binary_op_types.right_type);
            break;
        case Error_Information_Type::EXPRESSION_RESULT_TYPE:
        {
            string_append_formated(string, "\nGiven: ");
            switch (info->options.expression_type)
            {
            case Expression_Result_Type::HARDCODED_FUNCTION:
                string_append_formated(string, "Hardcoded function");
                break;
            case Expression_Result_Type::POLYMORPHIC_FUNCTION:
                string_append_formated(string, "Polymorphic function");
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
            string_append_formated(string, "\n  %s", constant_status_to_string(info->options.constant_status));
            break;
        default: panic("");
        }
    }
}


