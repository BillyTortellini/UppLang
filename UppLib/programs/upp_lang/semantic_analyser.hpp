#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "../../datastructures/list.hpp"

#include "type_system.hpp"
#include "compiler_misc.hpp"
#include "dependency_analyser.hpp"

struct Type_Signature;
struct Polymorphic_Function;
struct Symbol;
struct Compiler;
struct Symbol_Table;
struct ModTree_Function;
struct Upp_Constant;
enum class Constant_Status;
struct Semantic_Error;
struct Error_Information;
struct Dependency_Analyser;
struct Analysis_Workload;
struct Analysis_Progress;
struct Analysis_Pass;
struct Analysis_Item;

namespace Parser
{
    enum class Section;
}

namespace AST
{
    struct Code_Block;
}



// Modtree TODO: Rename this into something more sensible
struct ModTree_Function
{
    Analysis_Pass* body_pass;
    Type_Signature* signature;
    Symbol* symbol; // May be 0, is only used for aliases and pretty printing

    // Infos
    bool contains_errors; // NOTE: contains_errors (No errors in this function) != is_runnable (This + all called functions are runnable)
    bool is_runnable;
    Dynamic_Array<ModTree_Function*> called_from;
    Dynamic_Array<ModTree_Function*> calls;

    bool is_polymorphic; // If it is an instance from a polymorphic function
    Polymorphic_Function* polymorphic_base;
    int polymorphic_instance_index;
};

struct ModTree_Global
{
    Type_Signature* type;
    int index;

    bool has_initial_value;
    Analysis_Pass* init_pass;
    AST::Expression* init_expr;
};

struct ModTree_Program
{
    Dynamic_Array<ModTree_Function*> functions;
    Dynamic_Array<ModTree_Global*> globals; 
    ModTree_Function* main_function;
};

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
    Type_Signature* data_type;
};

struct Polymorphic_Value
{
    bool is_not_set; // E.g. during base analysis
    Upp_Constant constant;
};

struct Polymorphic_Instance
{
    Array<Polymorphic_Value> parameter_values;
    ModTree_Function* function;
    ModTree_Function* instanciation_site; // Where the instance comes from
    int recursive_instanciation_depth;
};

struct Polymorphic_Function
{
    int polymorphic_parameter_count;
    Array<AST::Parameter*> parameters;
    Dynamic_Array<Polymorphic_Instance> instances;
};




// Analysis Progress
enum class Analysis_Progress_Type
{
    FUNCTION,
    STRUCTURE,
    BAKE,
    DEFINITION,
};

struct Analysis_Progress
{
    Analysis_Progress_Type type;
};

enum class Struct_State
{
    DEFINED,
    SIZE_KNOWN,
    FINISHED
};

struct Struct_Progress
{
    Analysis_Progress base;
    Analysis_Pass* pass;

    Struct_State state;
    Type_Signature* struct_type;

    Analysis_Workload* analysis_workload;
    Analysis_Workload* reachable_resolve_workload;
};

struct Bake_Progress
{
    Analysis_Progress base;
    Analysis_Pass* pass;

    ModTree_Function* bake_function;
    Comptime_Result result;

    Analysis_Workload* analysis_workload;
    Analysis_Workload* execute_workload;
};

struct Definition_Progress
{
    Analysis_Progress base;
    Analysis_Pass* pass;
    Analysis_Workload* definition_workload;
    Symbol* symbol;
};

enum class Function_State
{
    DEFINED,
    HEADER_ANALYSED,
    BODY_ANALYSED,
    FINISHED, // Compiled if no errors occured
};

struct Function_Progress
{
    Analysis_Progress base;
    Analysis_Pass* header_pass;
    Analysis_Pass* body_pass;

    Function_State state;
    ModTree_Function* function;

    Analysis_Workload* header_workload;
    Analysis_Workload* body_workload;
    Analysis_Workload* compile_workload;
};



// Workload Executer
enum class Analysis_Workload_Type
{
    FUNCTION_HEADER,
    FUNCTION_BODY,
    FUNCTION_CLUSTER_COMPILE,
    STRUCT_ANALYSIS,
    STRUCT_REACHABLE_RESOLVE,
    BAKE_ANALYSIS,
    BAKE_EXECUTION,
    DEFINITION,
    PROJECT_IMPORT,
};

struct Analysis_Workload
{
    Analysis_Workload_Type type;
    Analysis_Progress* progress;
    bool is_finished;

    // Dependencies
    List<Analysis_Workload*> dependencies;
    List<Analysis_Workload*> dependents;
    Dynamic_Array<Symbol_Dependency*> symbol_dependencies;

    // Note: Clustering is required for Workloads where cyclic dependencies on the same workload-type are allowed,
    //       like recursive functions or structs containing pointers to themselves
    Analysis_Workload* cluster;
    Dynamic_Array<Analysis_Workload*> reachable_clusters;

    // Payload
    struct {
        struct {
            Dynamic_Array<ModTree_Function*> functions;
        } cluster_compile;
        struct {
            Dynamic_Array<Type_Signature*> struct_types;
            Dynamic_Array<Type_Signature*> unfinished_array_types;
        } struct_reachable;
        struct {
            AST::Project_Import* import;
        };
    } options;
};

struct Workload_Pair
{
    Analysis_Workload* workload;
    Analysis_Workload* depends_on;
};

struct Dependency_Information
{
    List_Node<Analysis_Workload*>* dependency_node;
    List_Node<Analysis_Workload*>* dependent_node;
    // Information for cyclic resolve
    bool only_symbol_read_dependency;
    Dynamic_Array<Symbol_Dependency*> symbol_reads;
};

struct Workload_Executer
{
    Dynamic_Array<Analysis_Workload*> all_workloads;
    Dynamic_Array<Analysis_Workload*> waiting_for_symbols_workloads;
    Dynamic_Array<Analysis_Workload*> runnable_workloads;
    Dynamic_Array<Analysis_Workload*> finished_workloads;
    bool progress_was_made;

    Hashtable<Workload_Pair, Dependency_Information> workload_dependencies;
    Hashtable<Analysis_Item*, Analysis_Progress*> progress_items;
    Hashtable<Type_Signature*, Struct_Progress*> progress_structs;
    Hashtable<ModTree_Function*, Function_Progress*> progress_functions;
    Hashtable<Symbol*, Definition_Progress*> progress_definitions;

    // Allocations
    Dynamic_Array<Analysis_Progress*> allocated_progresses;
};

void workload_executer_resolve();
void workload_executer_add_analysis_items(Code_Source* source);



// Analysis Information
enum class Info_Cast_Type
{
    INTEGERS, // Implicit to bigger
    FLOATS, // Implicit to bigger
    FLOAT_TO_INT,
    INT_TO_FLOAT, // Implicit
    POINTERS, // Implicit from/to void*
    POINTER_TO_U64,
    U64_TO_POINTER,
    ENUM_TO_INT,
    INT_TO_ENUM,
    ARRAY_TO_SLICE, // Implicit
    TO_ANY,
    FROM_ANY,

    NO_CAST, // No cast required
    INVALID, // No cast can create the desired result, but we still handle it as a cast
};

enum class Expression_Context_Type
{
    UNKNOWN,             // Type is not known
    AUTO_DEREFERENCE,    // Type is not known, but we want pointer level 0 
    SPECIFIC_TYPE,       // Type is known, pointer level items + implicit casting enabled
};

struct Expression_Context
{
    Expression_Context_Type type;
    Type_Signature* signature;
};

enum class Expression_Result_Type
{
    VALUE,
    TYPE,
    FUNCTION,
    HARDCODED_FUNCTION,
    POLYMORPHIC_FUNCTION,
    MODULE,
    CONSTANT,
};

struct Argument_Info
{
    bool valid; // Used in both polymorphic functions/named parameters and struct initializer
    int argument_index; // For named arguments/parameters this gives the according parameter index
    Struct_Member member; // For struct initializer
};

struct Expression_Info
{
    // All types in "options" union are before the expression context has been applied
    Expression_Result_Type result_type;
    union
    {
        Type_Signature* value_type;
        Type_Signature* type;
        ModTree_Function* function;
        struct {
            Polymorphic_Function* function;
            int instance_index; // If the function was instanciated from parent, otherwise 0 (base instance)
        } polymorphic;
        Hardcoded_Type hardcoded;
        Symbol_Table* module_table;
        Upp_Constant constant;
        int argument_index;
    } options;

    bool contains_errors; // If this expression contains any errors (Not recursive), currently only used for comptime-calculation
    union {
        Info_Cast_Type cast_type;
        Type_Signature* function_call_signature; // Somewhat usefull when not all arguments in a call are used (polymorphic funcitons, later named/default args)
    } specifics;

    Expression_Context context; // Maybe I don't even want to store the context
    struct { // Info for code-gen
        int deref_count;
        bool take_address_of;
        Info_Cast_Type cast;
        Type_Signature* after_cast_type;
    } context_ops; 
};

enum class Control_Flow
{
    SEQUENTIAL, // One sequential path exists, but there may be paths that aren't sequential
    STOPS,      // Execution never goes further than the given statement, but there may be paths that return
    RETURNS,    // All possible code path return
};

struct Statement_Info
{
    Control_Flow flow;
    struct {
        AST::Code_Block* block; // Continue/break
    } specifics;
};

struct Code_Block_Info
{
    Control_Flow flow;
    bool control_flow_locked;
};

struct Case_Info
{
    int is_valid;
    int case_value; // Currently we only switch over enums/ints
};

union Analysis_Info
{
    Expression_Info info_expr;
    Statement_Info info_stat;
    Code_Block_Info info_block;
    Case_Info info_case;
    Argument_Info arg_info;
};

struct Analysis_Pass
{
    Analysis_Item* item;
    Array<Analysis_Info> infos;
};

Analysis_Info* analysis_pass_get_info(Analysis_Pass* pass, AST::Node* node);
Expression_Info* analysis_pass_get_info(Analysis_Pass* pass, AST::Expression* expression);
Case_Info* analysis_pass_get_info(Analysis_Pass* pass, AST::Switch_Case* sw_case);
Argument_Info* analysis_pass_get_info(Analysis_Pass* pass, AST::Argument* argument);
Statement_Info* analysis_pass_get_info(Analysis_Pass* pass, AST::Statement* statement);
Code_Block_Info* analysis_pass_get_info(Analysis_Pass* pass, AST::Code_Block* block);
Type_Signature* expression_info_get_type(Expression_Info* info);



// ANALYSER
struct Semantic_Analyser
{
    // Result
    Dynamic_Array<Semantic_Error> errors;
    ModTree_Program* program;

    // Temporary stuff needed for analysis
    Compiler* compiler;
    Workload_Executer* workload_executer;
    Analysis_Pass* current_pass;
    Analysis_Workload* current_workload;
    ModTree_Function* current_function;
    Expression_Info* current_expression;

    Dynamic_Array<Polymorphic_Function*> polymorphic_functions;

    bool statement_reachable;
    int error_flag_count;

    Stack_Allocator allocator_values;
    Hashset<ModTree_Function*> visited_functions;
    Dynamic_Array<AST::Code_Block*> block_stack;

    ModTree_Global* global_type_informations;

    Type_Signature* type_assert;
    Type_Signature* type_free;
    Type_Signature* type_malloc;
    Type_Signature* type_type_of;
    Type_Signature* type_type_info;
    Type_Signature* type_print_bool;
    Type_Signature* type_print_i32;
    Type_Signature* type_print_f32;
    Type_Signature* type_print_line;
    Type_Signature* type_print_string;
    Type_Signature* type_read_i32;
    Type_Signature* type_read_f32;
    Type_Signature* type_read_bool;
    Type_Signature* type_random_i32;
};

Semantic_Analyser* semantic_analyser_initialize();
void semantic_analyser_destroy();
void semantic_analyser_reset(Compiler* compiler);
void semantic_analyser_finish();
Type_Signature* hardcoded_type_to_signature(Hardcoded_Type type);



// ERRORS
enum class Semantic_Error_Type
{
    TEMPLATE_ARGUMENTS_INVALID_COUNT,
    TEMPLATE_ARGUMENTS_NOT_ON_TEMPLATE,
    TEMPLATE_ARGUMENTS_REQUIRED,

    CYCLIC_DEPENDENCY_DETECTED,

    EXTERN_HEADER_DOES_NOT_CONTAIN_SYMBOL, // Error_node = is identifier_node
    EXTERN_HEADER_PARSING_FAILED, // Error_node = EXTERN_HEADER_IMPORT

    EXPECTED_TYPE,
    EXPECTED_VALUE,
    EXPECTED_CALLABLE,
    INVALID_EXPRESSION_TYPE,

    INVALID_TYPE,
    INVALID_TYPE_VOID_USAGE,
    INVALID_TYPE_FUNCTION_CALL, // Expression
    INVALID_TYPE_FUNCTION_IMPORT_EXPECTED_FUNCTION_POINTER,
    INVALID_TYPE_ARGUMENT,
    INVALID_TYPE_ARRAY_ACCESS, // x: int; x[5];
    INVALID_TYPE_ARRAY_ACCESS_INDEX, // x: int; x[5];
    INVALID_TYPE_ARRAY_ALLOCATION_SIZE, // new [false]int;
    INVALID_TYPE_ARRAY_SIZE, // x: [bool]int;
    INVALID_TYPE_ON_MEMBER_ACCESS,
    INVALID_TYPE_IF_CONDITION,
    INVALID_TYPE_WHILE_CONDITION,
    INVALID_TYPE_UNARY_OPERATOR,
    INVALID_TYPE_BINARY_OPERATOR,
    INVALID_TYPE_ASSIGNMENT,
    INVALID_TYPE_RETURN,
    INVALID_TYPE_DELETE,
    INVALID_TYPE_ENUM_VALUE,
    INVALID_TYPE_EXPECTED_POINTER,
    INVALID_TYPE_CAST_RAW_REQUIRES_POINTER,
    INVALID_TYPE_CAST_PTR_REQUIRES_U64,
    INVALID_TYPE_CAST_PTR_DESTINATION_MUST_BE_PTR,

    INVALID_TYPE_COMPTIME_DEFINITION,
    COMPTIME_DEFINITION_MUST_BE_COMPTIME_KNOWN,
    COMPTIME_DEFINITION_MUST_BE_INFERED,
    COMPTIME_DEFINITION_REQUIRES_INITAL_VALUE,

    CONSTANT_POOL_ERROR,

    MODULE_NOT_VALID_IN_THIS_CONTEXT,

    ENUM_VALUE_MUST_BE_COMPILE_TIME_KNOWN,
    ENUM_VALUE_MUST_BE_UNIQUE,
    ENUM_MEMBER_NAME_MUST_BE_UNIQUE,
    ENUM_DOES_NOT_CONTAIN_THIS_MEMBER,

    SWITCH_REQUIRES_ENUM,
    SWITCH_CASES_MUST_BE_COMPTIME_KNOWN,
    SWITCH_MUST_HANDLE_ALL_CASES,
    SWITCH_MUST_NOT_BE_EMPTY,
    SWITCH_ONLY_ONE_DEFAULT_ALLOWED,
    SWITCH_CASE_TYPE_INVALID,
    SWITCH_CASE_MUST_BE_UNIQUE,

    VARIABLE_NOT_DEFINED_YET,

    SYMBOL_EXPECTED_MODUL_IN_IDENTIFIER_PATH,
    SYMBOL_EXPECTED_TYPE_ON_TYPE_IDENTIFIER,
    SYMBOL_MODULE_INVALID,

    SYMBOL_TABLE_UNRESOLVED_SYMBOL,
    SYMBOL_TABLE_SYMBOL_ALREADY_DEFINED,
    SYMBOL_TABLE_MODULE_ALREADY_DEFINED,

    FUNCTION_CALL_ARGUMENT_SIZE_MISMATCH,
    AUTO_MEMBER_KNOWN_CONTEXT_IS_REQUIRED,
    AUTO_MEMBER_MUST_BE_IN_ENUM_CONTEXT,
    AUTO_CAST_KNOWN_CONTEXT_IS_REQUIRED,

    EXPRESSION_INVALID_CAST,
    EXPRESSION_MEMBER_NOT_FOUND,
    EXPRESSION_ADDRESS_MUST_NOT_BE_OF_TEMPORARY_RESULT,
    CANNOT_TAKE_POINTER_OF_FUNCTION,
    EXPRESSION_BINARY_OP_TYPES_MUST_MATCH,
    EXPRESSION_STATEMENT_MUST_BE_FUNCTION_CALL,

    BAKE_FUNCTION_MUST_NOT_REFERENCE_GLOBALS,
    BAKE_FUNCTION_DID_NOT_SUCCEED,
    BAKE_BLOCK_RETURN_MUST_NOT_BE_EMPTY,
    BAKE_BLOCK_RETURN_TYPE_DIFFERS_FROM_PREVIOUS_RETURN,

    EXPRESSION_CONTAINS_INVALID_TYPE_HANDLE,
    TYPE_NOT_KNOWN_AT_COMPILE_TIME,
    EXPRESSION_IS_NOT_A_TYPE,

    COMPTIME_ARGUMENT_NOT_KNOWN_AT_COMPTIME,

    MAIN_CANNOT_BE_TEMPLATED,
    MAIN_NOT_DEFINED,
    MAIN_UNEXPECTED_SIGNATURE,
    MAIN_CANNOT_BE_CALLED,
    MAIN_MUST_BE_FUNCTION,

    BREAK_NOT_INSIDE_LOOP_OR_SWITCH,
    BREAK_LABLE_NOT_FOUND,
    CONTINUE_NOT_INSIDE_LOOP,
    CONTINUE_LABEL_NOT_FOUND,
    CONTINUE_REQUIRES_LOOP_BLOCK,
    LABEL_ALREADY_IN_USE,

    ARRAY_SIZE_NOT_COMPILE_TIME_KNOWN,
    ARRAY_SIZE_MUST_BE_GREATER_ZERO,

    ARRAY_INITIALIZER_REQUIRES_TYPE_SYMBOL,
    ARRAY_INITIALIZER_INVALID_TYPE,
    ARRAY_AUTO_INITIALIZER_COULD_NOT_DETERMINE_TYPE,

    STRUCT_INITIALIZER_REQUIRES_TYPE_SYMBOL,
    STRUCT_INITIALIZER_MEMBERS_MISSING,
    STRUCT_INITIALIZER_MEMBER_INITIALIZED_TWICE,
    STRUCT_INITIALIZER_TYPE_MUST_BE_STRUCT,
    STRUCT_INITIALIZER_MEMBER_DOES_NOT_EXIST,
    STRUCT_INITIALIZER_INVALID_MEMBER_TYPE,
    STRUCT_INITIALIZER_CAN_ONLY_SET_ONE_UNION_MEMBER,
    STRUCT_INITIALIZER_CANNOT_SET_UNION_TAG,
    AUTO_STRUCT_INITIALIZER_COULD_NOT_DETERMINE_TYPE,

    STRUCT_MUST_CONTAIN_MEMBER,
    STRUCT_MEMBER_ALREADY_DEFINED,
    STRUCT_MEMBER_REQUIRES_TYPE,
    STRUCT_MEMBER_MUST_NOT_HAVE_VALUE,

    MISSING_FEATURE_NAMED_ARGUMENTS,

    OTHERS_TYPE_MEMBER_ACCESS_MUST_BE_ENUM,
    OTHERS_MEMBER_ACCESS_INVALID_ON_FUNCTION,
    OTHERS_WHILE_ONLY_RUNS_ONCE,
    OTHERS_WHILE_ALWAYS_RETURNS,
    OTHERS_WHILE_NEVER_STOPS,
    OTHERS_STATEMENT_UNREACHABLE,
    OTHERS_DEFER_NO_RETURNS_ALLOWED,
    OTHERS_MISSING_RETURN_STATEMENT,
    OTHERS_UNFINISHED_WORKLOAD_FUNCTION_HEADER,
    OTHERS_UNFINISHED_WORKLOAD_CODE_BLOCK,
    OTHERS_UNFINISHED_WORKLOAD_TYPE_SIZE,
    OTHERS_CANNOT_TAKE_ADDRESS_OF_MAIN,
    OTHERS_ASSIGNMENT_REQUIRES_MEMORY_ADDRESS,
    OTHERS_RETURN_EXPECTED_NO_VALUE,
    OTHERS_CANNOT_TAKE_ADDRESS_OF_HARDCODED_FUNCTION,
    OTHERS_COULD_NOT_LOAD_FILE,

    MISSING_FEATURE_TEMPLATED_GLOBALS,
    MISSING_FEATURE_NESTED_TEMPLATED_MODULES,
    MISSING_FEATURE_EXTERN_IMPORT_IN_TEMPLATED_MODULES,
    MISSING_FEATURE_EXTERN_GLOBAL_IMPORT,
    MISSING_FEATURE,
    MISSING_FEATURE_NESTED_DEFERS
};

enum class Error_Information_Type
{
    ARGUMENT_COUNT,
    INVALID_MEMBER,
    ID,
    SYMBOL,
    EXIT_CODE,

    GIVEN_TYPE,
    EXPECTED_TYPE,
    FUNCTION_TYPE,
    BINARY_OP_TYPES,
    CYCLE_WORKLOAD,

    EXPRESSION_RESULT_TYPE,
    CONSTANT_STATUS,
    EXTRA_TEXT,
};

struct Error_Information
{
    Error_Information_Type type;
    union
    {
        struct {
            int expected;
            int given;
        } invalid_argument_count;
        const char* extra_text;
        String* id;
        Symbol* symbol;
        Exit_Code exit_code;
        Type_Signature* type;
        struct {
            Type_Signature* struct_signature;
            String* member_id;
        } invalid_member;
        Analysis_Workload* cycle_workload;
        struct {
            Type_Signature* left_type;
            Type_Signature* right_type;
        } binary_op_types;
        Expression_Result_Type expression_type;
        Constant_Status constant_status;
    } options;
};

struct Semantic_Error
{
    Semantic_Error_Type type;
    AST::Node* error_node;
    Dynamic_Array<Error_Information> information;
};

void semantic_analyser_add_error_info(Error_Information info);
void semantic_analyser_log_error(Semantic_Error_Type type, AST::Node* node);
void semantic_analyser_set_error_flag(bool error_due_to_unknown);
void semantic_error_append_to_string(Semantic_Error e, String* string);
Parser::Section semantic_error_get_section(Semantic_Error e);
