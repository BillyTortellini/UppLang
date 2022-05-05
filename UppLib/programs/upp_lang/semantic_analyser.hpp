#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "../../datastructures/list.hpp"

#include "type_system.hpp"
#include "compiler_misc.hpp"
#include "rc_analyser.hpp"

/*
    What can differentiate ModTree from IR_Code?
        Module Hierarchy:          Keep    vs. Flatten
        Resolve Identifiers to:    Symbols vs. Structures (Functions, Data_Accesses...)
        Variable Definitions:      Keep Position vs. in Block vs. In Function
        Expression Results:        As Tree       vs. Flatten (In Registers)
        Data Access:               Through expressions vs. Data_Access structure (Resolves expression evaluation order)
*/

struct Type_Signature;
struct Symbol;
struct Compiler;
struct Symbol_Table;
struct ModTree_Block;
struct ModTree_Function;
struct ModTree_Variable;
struct ModTree_Expression;
struct Upp_Constant;
enum class Constant_Status;
struct Semantic_Error;
struct Error_Information;
struct Dependency_Analyser;
struct Analysis_Workload;
struct Analysis_Progress;

namespace AST
{
    struct Code_Block;
    enum class Section;
}

/*
    MODTREE
*/
enum class ModTree_Binary_Operation_Type
{
    ADDITION,
    SUBTRACTION,
    DIVISION,
    MULTIPLICATION,
    MODULO,
    AND,
    OR,
    EQUAL,
    NOT_EQUAL,
    LESS,
    LESS_OR_EQUAL,
    GREATER,
    GREATER_OR_EQUAL,
};

enum class ModTree_Unary_Operation_Type
{
    NEGATE,
    LOGICAL_NOT,
    ADDRESS_OF,
    DEREFERENCE,
    TEMPORARY_TO_STACK, // Puts temporary values onto the stack, e.g. 5 is temporary
};

enum class ModTree_Cast_Type
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
    ARRAY_SIZED_TO_UNSIZED, // Implicit
    TO_ANY,
    FROM_ANY,
};

struct Member_Initializer
{
    Struct_Member member;
    ModTree_Expression* init_expr;
    AST::Base* init_node;
};

enum class ModTree_Call_Type
{
    FUNCTION,
    EXTERN_FUNCTION,
    HARDCODED_FUNCTION,
    FUNCTION_POINTER,
};

enum class ModTree_Expression_Type
{
    BINARY_OPERATION,
    UNARY_OPERATION,
    CONSTANT_READ,
    FUNCTION_CALL,
    VARIABLE_READ,
    FUNCTION_POINTER_READ,
    ARRAY_ACCESS,
    MEMBER_ACCESS,
    NEW_ALLOCATION,
    ARRAY_INITIALIZER,
    STRUCT_INITIALIZER,
    CAST,

    ERROR_EXPR, // This is used if an operation creates an error, but analysis can still continue
};

struct ModTree_Expression
{
    ModTree_Expression_Type expression_type;
    Type_Signature* result_type;
    union
    {
        struct
        {
            ModTree_Binary_Operation_Type operation_type;
            ModTree_Expression* left_operand;
            ModTree_Expression* right_operand;
        } binary_operation;
        struct
        {
            ModTree_Unary_Operation_Type operation_type;
            ModTree_Expression* operand;
        } unary_operation;
        Upp_Constant constant_read;
        struct {
            ModTree_Call_Type call_type;
            union {
                ModTree_Expression* pointer_expression;
                ModTree_Function* function;
                ModTree_Extern_Function* extern_function;
                ModTree_Hardcoded_Function* hardcoded_function;
            } options;
            Dynamic_Array<ModTree_Expression*> arguments;
        } function_call;
        struct {
            bool is_extern;
            ModTree_Function* function;
            ModTree_Extern_Function* extern_function;
        } function_pointer_read;
        ModTree_Variable* variable_read;
        Dynamic_Array<ModTree_Expression*> array_initializer;
        Dynamic_Array<Member_Initializer> struct_initializer;
        struct {
            ModTree_Expression* array_expression;
            ModTree_Expression* index_expression;
        } array_access;
        struct {
            ModTree_Expression* structure_expression;
            Struct_Member member;
        } member_access;
        struct {
            ModTree_Expression* cast_argument;
            ModTree_Cast_Type type;
        } cast;
        struct {
            int allocation_size;
            Optional<ModTree_Expression*> element_count; // If true this is an array allocation
        } new_allocation;
    } options;
};

struct ModTree_Switch_Case
{
    int value;
    ModTree_Expression* expression;
    ModTree_Block* body;
};

enum class ModTree_Statement_Type
{
    BLOCK,
    IF,
    WHILE,
    SWITCH,

    BREAK,
    CONTINUE,
    RETURN,
    EXIT,

    EXPRESSION,
    ASSIGNMENT,
    DELETION,
};

struct ModTree_Statement
{
    ModTree_Statement_Type type;
    union
    {
        ModTree_Block* block;
        ModTree_Block* break_to_block;
        ModTree_Block* continue_to_block;
        struct {
            ModTree_Expression* condition;
            ModTree_Block* if_block;
            ModTree_Block* else_block;
        } if_statement;
        struct {
            ModTree_Expression* condition;
            ModTree_Block* while_block;
        } while_statement;
        struct {
            ModTree_Expression* condition;
            Dynamic_Array<ModTree_Switch_Case> cases;
            ModTree_Block* default_block;
        } switch_statement;
        struct {
            ModTree_Expression* destination;
            ModTree_Expression* source;
        } assignment;
        struct {
            ModTree_Expression* expression;
            bool is_array;
        } deletion;
        ModTree_Expression* expression;
        Optional<ModTree_Expression*> return_value;
        Exit_Code exit_code;
    } options;
};

enum class Control_Flow
{
    SEQUENTIAL, // One sequential path exists, but there may be paths that aren't sequential
    STOPS,      // Execution never goes further than the given statement, but there may be paths that return
    RETURNS,    // All possible code path return
};

struct ModTree_Block
{
    Dynamic_Array<ModTree_Statement*> statements;
    Dynamic_Array<ModTree_Variable*> variables;

    // Infos
    AST::Code_Block* code_block;
    Control_Flow flow;
    bool control_flow_locked;
    int defer_start_index;
};

struct ModTree_Variable
{
    Type_Signature* data_type;
    Symbol* symbol; // May be null
};


struct ModTree_Parameter
{
    String* name;
    Type_Signature* data_type;
    bool is_comptime;
    union {
        ModTree_Variable* variable;
    } options;
};

enum class ModTree_Function_Type
{
    NORMAL,
    POLYMORPHIC_BASE,
    POLYMOPRHIC_INSTANCE,
};

struct ModTree_Function
{
    ModTree_Block* body;
    Dynamic_Array<ModTree_Parameter> parameters;
    Type_Signature* return_type;

    Symbol* symbol;
    Type_Signature* signature;

    // Polymorphic infos
    ModTree_Function_Type type;
    union 
    {
        struct {
            int poly_argument_count;
        } base;
        struct {
            ModTree_Function* instance_base_function;
            Dynamic_Array<Upp_Constant> poly_arguments;
        } instance;
    } options;

    // Infos
    bool contains_errors; // NOTE: contains_errors and is_runnable are actually 2 different things, but I only care about runnable for bake
    bool is_runnable;
    Dynamic_Array<ModTree_Function*> called_from;
    Dynamic_Array<ModTree_Function*> calls;
};

struct ModTree_Extern_Function
{
    Extern_Function_Identifier extern_function;
    Symbol* symbol; // May be null
};

struct ModTree_Hardcoded_Function
{
    Type_Signature* signature;
    Hardcoded_Function_Type hardcoded_type;
};

struct ModTree_Program
{
    Dynamic_Array<ModTree_Function*> functions;
    Dynamic_Array<ModTree_Variable*> globals;
    Dynamic_Array<ModTree_Hardcoded_Function*> hardcoded_functions;
    Dynamic_Array<ModTree_Extern_Function*> extern_functions;
    ModTree_Function* entry_function;
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





/*
DEPENDENCY GRAPH
*/
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
    Struct_State state;
    Type_Signature* struct_type;
    Analysis_Workload* analysis_workload;
    Analysis_Workload* reachable_resolve_workload;
};

struct Bake_Progress
{
    Analysis_Progress base;
    ModTree_Function* bake_function;
    Comptime_Result result;
    Analysis_Workload* analysis_workload;
    Analysis_Workload* execute_workload;
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
    Function_State state;
    ModTree_Function* function;
    Analysis_Workload* header_workload;
    Analysis_Workload* body_workload;
    Analysis_Workload* compile_workload;
};

struct Definition_Progress
{
    Analysis_Progress base;
    Analysis_Workload* definition_workload;
    Symbol* symbol;
};

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
};

struct Analysis_Workload
{
    Analysis_Workload_Type type;
    Analysis_Progress* progress;
    Analysis_Item* analysis_item;
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
    Dynamic_Array<Symbol_Dependency*> symbol_reads;
    bool only_symbol_read_dependency;
};

struct Workload_Executer
{
    Dynamic_Array<Analysis_Workload*> workloads;
    Dynamic_Array<Analysis_Workload*> runnable_workloads;
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
void workload_executer_add_analysis_items(Dependency_Analyser* dependency_analyser);





/*
ANALYSER
*/
struct Analysis_Workload;
struct Semantic_Analyser
{
    // Result
    Dynamic_Array<Semantic_Error> errors;
    ModTree_Program* program;

    // ModTree stuff
    ModTree_Function* global_init_function;
    ModTree_Hardcoded_Function* free_function;
    ModTree_Hardcoded_Function* malloc_function;
    ModTree_Function* assert_function;
    ModTree_Variable* global_type_informations;

    // Temporary stuff needed for analysis
    Compiler* compiler;
    Workload_Executer* workload_executer;
    Analysis_Workload* current_workload;
    ModTree_Function* current_function;
    bool statement_reachable;
    int error_flag_count;

    Hashset<String*> loaded_filenames;
    Stack_Allocator allocator_values;
    Hashset<ModTree_Function*> visited_functions;
    Dynamic_Array<ModTree_Block*> block_stack;
    Dynamic_Array<AST::Code_Block*> defer_stack;

    String* id_size;
    String* id_data;
    String* id_main;
    String* id_tag;
    String* id_type_of;
    String* id_type_info;
};

Semantic_Analyser* semantic_analyser_initialize();
void semantic_analyser_destroy();
void semantic_analyser_reset(Compiler* compiler);
void semantic_analyser_finish();



// ERRORS
enum class Expression_Result_Type
{
    EXPRESSION,
    TYPE,
    FUNCTION,
    HARDCODED_FUNCTION,
    EXTERN_FUNCTION,
    MODULE,
};

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

    OTHERS_TYPE_MEMBER_ACCESS_MUST_BE_ENUM,
    OTHERS_MEMBER_ACCESS_INVALID_ON_FUNCTION,
    OTHERS_STRUCT_MUST_CONTAIN_MEMBER,
    OTHERS_STRUCT_MEMBER_ALREADY_DEFINED,
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
    CONSTANT_STATUS
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
    AST::Base* error_node;
    AST::Section section;
    Dynamic_Array<Error_Information> information;
};

struct Token_Range;
void semantic_analyser_log_error(Semantic_Error_Type type, AST::Base* node);
void semantic_analyser_add_error_info(Error_Information info);
void semantic_analyser_set_error_flag(bool error_due_to_unknown);
void semantic_error_append_to_string(Semantic_Error e, String* string);
AST::Section semantic_error_get_section(Semantic_Error e);
