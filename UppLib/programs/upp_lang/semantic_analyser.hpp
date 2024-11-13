#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "../../datastructures/list.hpp"
#include "../../utility/rich_text.hpp"

#include "type_system.hpp"
#include "compiler_misc.hpp"
#include "parser.hpp"
#include "constant_pool.hpp"
#include "symbol_table.hpp"

struct Symbol;
struct Symbol_Table;
struct Compiler;
struct ModTree_Function;
struct Semantic_Error;
struct Error_Information;
struct Expression_Info;
struct Analysis_Pass;

struct Workload_Definition;
struct Workload_Base;
struct Workload_Import_Resolve;
struct Workload_Structure_Polymorphic;
struct Workload_Structure_Body;

struct Function_Progress;
struct Bake_Progress;
struct Module_Progress;
struct Datatype_Template;
struct Poly_Value;

namespace Parser
{
    enum class Section;
}

namespace AST
{
    struct Node;
    struct Code_Block;
    struct Path_Lookup;
    struct Expression;
}



enum class ModTree_Function_Type
{
    NORMAL,
    BAKE,
    EXTERN
};

// Modtree TODO: Rename this into something more sensible, like Upp-Function
struct ModTree_Function
{
    Datatype_Function* signature;
    int function_slot_index; // Index in functions slots array
    String* name; // Symbol name, or "bake_function"/"lambda_function"

    ModTree_Function_Type function_type;
    union {
        struct {
            Symbol* symbol; // May be 0 if function is anonymous
            Symbol_Table* parameter_table; 
            Function_Progress* progress;
        } normal;
        Bake_Progress* bake;
        Workload_Definition* extern_definition;
    } options;

    // Infos
    bool contains_errors; // NOTE: contains_errors (No errors in this function) != is_runnable (This + all called functions are runnable)
    bool is_runnable;
    Dynamic_Array<ModTree_Function*> called_from;
    Dynamic_Array<ModTree_Function*> calls;
};

struct ModTree_Global
{
    Datatype* type;
    int index;

    Symbol* symbol; // May be null?
    bool is_extern;

    // Initial value
    bool has_initial_value;
    AST::Expression* init_expr;
    Workload_Definition* definition_workload; // For code generation

    // Used by interpreter
    void* memory;
};

struct ModTree_Program
{
    Dynamic_Array<ModTree_Function*> functions;
    Dynamic_Array<ModTree_Global*> globals;
    ModTree_Function* main_function;
};



// WORKLOADS
enum class Polymorphic_Analysis_Type
{
    NON_POLYMORPHIC,
    POLYMORPHIC_BASE,
    POLYMORPHIC_INSTANCE
};

enum class Poly_Value_Type
{
    SET,
    UNSET,
    TEMPLATED_TYPE, // Used for header-analysis (inferred type reads and struct-template values)
};

struct Poly_Value
{
    Poly_Value_Type type;
    union {
        Datatype* unset_type;
        Datatype* template_type;
        Upp_Constant value;
    } options;
};

enum class Analysis_Workload_Type
{
    EVENT, // Empty workload, which can have dependencies and dependents

    MODULE_ANALYSIS, // This is basically just symbol discovery
    IMPORT_RESOLVE,  
    OPERATOR_CONTEXT_CHANGE,

    FUNCTION_HEADER,
    FUNCTION_BODY,
    FUNCTION_CLUSTER_COMPILE,

    STRUCT_POLYMORPHIC,
    STRUCT_BODY,

    BAKE_ANALYSIS,
    BAKE_EXECUTION,

    DEFINITION,
};

struct Workload_Base
{
    Analysis_Workload_Type type;
    bool is_finished;
    bool was_started;
    Fiber_Pool_Handle fiber_handle;

    // Information required to be consistent during workload switches
    // Note: These members are automatically set in functions like analyse_expression, analyse_statement...
    //       Also note that some of these may not be set depending on the workload type
    ModTree_Function* current_function;
    Expression_Info* current_expression;
    bool statement_reachable;
    Symbol_Table* current_symbol_table;
    Symbol_Access_Level symbol_access_level;
    Analysis_Pass* current_pass;
    Dynamic_Array<AST::Code_Block*> block_stack; // NOTE: This is here because it is required by Bake-Analysis and code-block, also for statement blocks...

    int real_error_count;
    int errors_due_to_unknown_count;
    bool ignore_unknown_errors;

    // Note: All workloads need information when accessing polymorphic values. The main two use-cases are:
    //        * Re-analysing the header during poly-instanciation of functions
    //        * Accessing polymorphic-symbols inside child-workloads (Anonymous functions/anonymous structs, bake)
    Array<Poly_Value> polymorphic_values; // May be null, in which case we shouldn't be able to access polymorphics at all
    int polymorphic_instanciation_depth; 
    bool is_polymorphic_base; // Polymorphic base workloads and children cannot create poly instances

    // Dependencies
    List<Workload_Base*> dependencies;
    List<Workload_Base*> dependents;

    // Note: Clustering is required for Workloads where cyclic dependencies on the same workload-type are allowed,
    //       like recursive functions or structs containing pointers to themselves
    Workload_Base* cluster;
    Dynamic_Array<Workload_Base*> reachable_clusters;
};

struct Workload_Event
{
    Workload_Base base;
    const char* description;
};

struct Workload_Module_Analysis;

// Analyses context changes of a single type
struct Workload_Operator_Context_Change
{
    Workload_Base base;
    AST::Context_Change_Type context_type_to_analyse;
    Dynamic_Array<AST::Context_Change*> change_nodes;
    Operator_Context* context;
};

struct Workload_Module_Analysis
{
    Workload_Base base;
    Module_Progress* progress;
    AST::Module* module_node;
    Symbol_Table* symbol_table;

    Workload_Import_Resolve* last_import_workload;
    Workload_Module_Analysis* parent_analysis;
};

struct Workload_Import_Resolve
{
    Workload_Base base;
    AST::Import* import_node;
    Symbol* symbol; // May be 0 if its an import
    Symbol* alias_for_symbol; // May be 0 if its an import
};

struct Workload_Function_Header
{
    Workload_Base base;
    Function_Progress* progress;
    AST::Expression* function_node;
};

struct Workload_Function_Body
{
    Workload_Base base;
    Function_Progress* progress;
    AST::Code_Block* body_node;
};

struct Workload_Function_Cluster_Compile
{
    Workload_Base base;
    Function_Progress* progress;
    Dynamic_Array<ModTree_Function*> functions;
};

struct Workload_Definition
{
    Workload_Base base;
    Symbol* symbol;
    bool is_extern_import;
    union {
        struct {
            bool is_comptime;
            AST::Assignment_Type assignment_type;
            AST::Expression* value_node; // May be null
            AST::Expression* type_node; // May be null
        } normal;
        AST::Extern_Import* extern_import;
    } options;
};

struct Workload_Bake_Analysis
{
    Workload_Base base;
    Bake_Progress* progress;
    AST::Expression* bake_node;
};

struct Workload_Bake_Execution
{
    Workload_Base base;
    Bake_Progress* progress;
    AST::Expression* bake_node;
};



// Polymorphism
struct Poly_Parameter
{
    Function_Parameter infos;

    // Polymorphic infos
    bool depends_on_other_parameters;
    bool contains_inferred_parameter;
    bool has_self_dependency;
    bool is_comptime;
    union {
        int value_access_index; // For comptime parameters
        int index_in_non_polymorphic_signature; // For normal parameters
    } options;
};

struct Inferred_Parameter
{
    int defined_in_parameter_index;
    AST::Expression* expression;
    String* id;
    Datatype_Template* template_parameter;
};

struct Poly_Instance;

struct Poly_Header
{
    String* name; // Either struct or function name
    bool is_function;

    // Parameters: List of parameters with comptime parameters + if return type exisits, it's the last value here
    Array<Poly_Parameter> parameters;
    int poly_value_count; // Number of comptime + inferred parameters

    // Order in which arguments need to be evaluated in for instanciation, -1 for return value
    Dynamic_Array<int> parameter_analysis_order; 
    Dynamic_Array<Inferred_Parameter> inferred_parameters;
    Dynamic_Array<Poly_Instance> instances;
    Array<Poly_Value> base_analysis_values;

    // For convenience
    int return_type_index; // -1 if no return-type exists
    AST::Expression* return_type_node;
    Dynamic_Array<AST::Parameter*> parameter_nodes;
    Symbol_Table* symbol_table;
};

struct Poly_Instance
{
    Poly_Header* header;
    Array<Poly_Value> instance_values;
    bool is_function;
    union {
        Function_Progress* function_instance;
        Workload_Structure_Body* struct_instance;
    } options;
};



// Structures
struct Workload_Structure_Body
{
    Workload_Base base;

    Datatype_Struct* struct_type;
    AST::Expression* struct_node;

    Polymorphic_Analysis_Type polymorphic_type;
    union {
        Workload_Structure_Polymorphic* base;
        struct {
            Workload_Structure_Polymorphic* parent;
            int instance_index;
        } instance;
    } polymorphic;
};

struct Workload_Structure_Polymorphic
{
    Workload_Base base;
    Workload_Structure_Body* body_workload;

    Poly_Header info;
};



// ANALYSIS_PROGRESS

struct Polymorphic_Function_Base
{
    Poly_Header base;
};

struct Function_Progress
{
    ModTree_Function* function;

    Workload_Function_Header* header_workload; // Points to base header workload if it's an instance
    Workload_Function_Body* body_workload;
    Workload_Function_Cluster_Compile* compile_workload;

    Polymorphic_Analysis_Type type;
    union {
        Polymorphic_Function_Base function_base;
        Polymorphic_Function_Base* instance_poly_base;
    } polymorphic;
};

struct Bake_Progress
{
    ModTree_Function* bake_function;
    Datatype* result_type;
    Optional<Upp_Constant> result;

    Workload_Bake_Analysis* analysis_workload;
    Workload_Bake_Execution* execute_workload;
};

struct Module_Progress
{
    Workload_Module_Analysis* module_analysis;
    Workload_Event* event_symbol_table_ready; // After all using workloads have ended
    Symbol* symbol; // May be 0 if root
};




// WORKLOAD EXECUTER
struct Workload_Pair
{
    Workload_Base* workload;
    Workload_Base* depends_on;
};

struct Dependency_Failure_Info
{
    bool* fail_indicator;
    AST::Symbol_Lookup* error_report_node;
};

struct Dependency_Information
{
    List_Node<Workload_Base*>* dependency_node;
    List_Node<Workload_Base*>* dependent_node;
    // Information for cyclic resolve
    bool can_be_broken;
    Dynamic_Array<Dependency_Failure_Info> fail_indicators;
};

struct Workload_Executer
{
    Dynamic_Array<Workload_Base*> all_workloads;
    Dynamic_Array<Workload_Base*> runnable_workloads;
    Dynamic_Array<Workload_Base*> finished_workloads;
    bool progress_was_made;

    Hashtable<Workload_Pair, Dependency_Information> workload_dependencies;
};

void workload_executer_resolve();
Module_Progress* workload_executer_add_module_discovery(AST::Module* module, bool is_root_module);



// Analysis Information
enum class Expression_Context_Type
{
    UNKNOWN,                // Type is not known
    AUTO_DEREFERENCE,       // Type is not known, but we want pointer level 0, e.g. a value (e.g. member-access, slice-access, ...)
    SPECIFIC_TYPE_EXPECTED, // Type is known, pointer level items + implicit casting enabled
};

struct Expression_Context
{
    Expression_Context_Type type;
    bool unknown_due_to_error; // If true the context is unknown because an error occured, otherwise there is no info
    struct {
        Datatype* type;
        Cast_Mode cast_mode;
    } expected_type;
};

enum class Cast_Type
{
    INTEGERS,
    FLOATS,
    FLOAT_TO_INT,
    INT_TO_FLOAT,
    POINTERS,
    POINTER_TO_U64,
    U64_TO_POINTER,
    ENUM_TO_INT,
    INT_TO_ENUM,
    ARRAY_TO_SLICE, 
    TO_ANY,
    FROM_ANY,
    TO_OPTIONAL,
    CUSTOM_CAST,

    NO_CAST, // No cast needed, source-type == destination-type
    UNKNOWN, // Either source or destination type are/contain error/unknown type
    INVALID, // Cast is not valid
};

// The dereferences/address_of is applied before the cast
struct Expression_Cast_Info
{
    Datatype* initial_type;
    Datatype* result_type;
    bool initial_value_is_temporary;
    bool result_value_is_temporary;

    int deref_count; // May be negative to indicate take-address of
    Cast_Type cast_type;

    union {
        ModTree_Function* custom_cast_function;
        const char* error_msg; // Null, except if the cast is invalid
    } options;
};

enum class Member_Access_Type
{
    STRUCT_MEMBER_ACCESS, // Includes subtype and tag access
    STRUCT_POLYMORHPIC_PARAMETER_ACCESS,
    ENUM_MEMBER_ACCESS,
    DOT_CALL_AS_MEMBER,
    DOT_CALL,
    OPTIONAL_PTR_ACCESS,
    STRUCT_SUBTYPE, // Generates a type, e.g. x: Node.Expression
    STRUCT_UP_OR_DOWNCAST, // a: Node, a.Expression.something --> The .Expression is a downcast
};

enum class Expression_Result_Type
{
    VALUE,
    TYPE,
    CONSTANT,
    FUNCTION,
    DOT_CALL,
    HARDCODED_FUNCTION,
    POLYMORPHIC_FUNCTION,
    POLYMORPHIC_STRUCT,
    NOTHING, // Functions returning void
};

struct Expression_Info
{
    // All types in "options" union are before the expression context has been applied
    Expression_Result_Type result_type;
    union
    {
        Datatype* type;
        Workload_Structure_Polymorphic* polymorphic_struct;
        ModTree_Function* function;
        struct {
            Polymorphic_Function_Base* base;
            ModTree_Function* instance_fn;
        } polymorphic_function;
        struct {
            AST::Expression* first_argument;
            bool is_polymorphic;
            union {
                ModTree_Function* function;
                struct {
                    Polymorphic_Function_Base* base;
                    ModTree_Function* instance;
                } polymorphic;
            } options;
        } dot_call;
        Hardcoded_Type hardcoded;
        Symbol_Table* module_table;
        Upp_Constant constant;
    } options;

    bool is_valid; // If this expression contains any errors (Not recursive), currently only used for comptime-calculation (And code editor I guess?)
    union {
        Datatype_Function* function_call_signature; // Used by code-generation for accessing default values
        Datatype_Primitive* bitwise_primitive_type;
        Function_Parameter* implicit_parameter;
        struct {
            Member_Access_Type type;
            union {
                struct {
                    Workload_Structure_Body* struct_workload;
                    int index; // Either normal member index, or polymorphic parameter index
                } poly_access;
                Struct_Member member;
                ModTree_Function* dot_call_function;
                int optional_deref_count;
            } options;
        } member_access;
        bool is_optional_pointer_check;
        struct {
            ModTree_Function* function; // Is null if it's a primitive overload (e.g. not overloaded)
            bool switch_left_and_right;
        } overload;
    } specifics;

    Expression_Context context; // Maybe I don't even want to store the context
    Expression_Cast_Info cast_info;
};

enum class Parameter_State
{
    NOT_ANALYSED,
    ANALYSED
};

struct Parameter_Match
{
    // Matching info
    String* name;
    Datatype* param_type; // May be null
    bool required; // Default values and implicit parameters don't require values
    bool requires_named_addressing; // Implicit arguments and #instanciate
    bool must_not_be_set; // #instanciate must not set specific arguments

    // Argument info (Can be used to instanciate)
    AST::Expression* expression; // may be 0 (instanciate), otherwise the expression of the correspoding argument
    Datatype* argument_type; // Type of analysed expression
    bool argument_is_temporary_value; // Required when expression == 0, to check if type_mods are compatible
    int argument_index; // -1 if argument is not set

    // Analysis info
    Parameter_State state;
    bool is_set;
    bool reanalyse_param_type_flag;
    bool ignore_during_code_generation; // If polymorphic_function, the argument shouldn't generate code during code-generation
};

enum class Call_Type
{
    FUNCTION,
    FUNCTION_POINTER,
    HARDCODED,
    DOT_CALL,

    POLYMORPHIC_STRUCT,
    POLYMORPHIC_FUNCTION,
    POLYMORPHIC_DOT_CALL,
    INSTANCIATE,

    CONTEXT_OPTION,
    STRUCT_INITIALIZER,
    UNION_INITIALIZER
};

struct Parameter_Matching_Info
{
    Dynamic_Array<Parameter_Match> matched_parameters;
    AST::Arguments* arguments; // May be null

    // Call to-infos
    Call_Type call_type;
    union 
    {
        ModTree_Function* function;
        Datatype_Function* pointer_call;
        Hardcoded_Type hardcoded;
        ModTree_Function* dot_call_function;
        Polymorphic_Function_Base* poly_function;
        Polymorphic_Function_Base* instanciate;
        Polymorphic_Function_Base* poly_dotcall;
        Workload_Structure_Polymorphic* poly_struct;
        struct {
            bool valid; // E.g. if the name exists
            Datatype_Struct* structure;
            Struct_Content* content;
            bool subtype_valid;
            bool supertype_valid;
        } struct_init;
    } options;

    bool has_return_value;
    Datatype* return_type; // Unknown if no return value exists
};

enum class Control_Flow
{
    SEQUENTIAL, // One sequential path exists, but there may be paths that stop/return
    STOPS,      // Execution never goes further than the given statement, but there may be paths that return
    RETURNS,    // All possible code paths return
};

struct Statement_Info
{
    Control_Flow flow;
    struct {
        AST::Code_Block* block; // Continue/break
        bool is_struct_split; // Definition or assignment
        struct {
            ModTree_Function* function;
            bool switch_arguments;
        } overload; // Binop assignments (function is null if no overload)
        struct {
            Symbol_Table* symbol_table;
            Symbol* loop_variable_symbol;
        } for_loop;
        struct {
            Symbol_Table* symbol_table;
            Symbol* loop_variable_symbol;
            Symbol* index_variable_symbol; // May be null

            bool is_custom_op;
            struct {
                ModTree_Function* fn_create;
                ModTree_Function* fn_has_next;
                ModTree_Function* fn_next;
                ModTree_Function* fn_get_value;
                int has_next_pointer_diff;
                int next_pointer_diff;
                int get_value_pointer_diff;
            } custom_op;
        } foreach_loop;
        struct {
            Struct_Content* base_content; // May be null for simple enum switch
        } switch_statement;
    } specifics;
};

struct Code_Block_Info
{
    Symbol_Table* symbol_table;
    Control_Flow flow;
    bool control_flow_locked;
};

struct Case_Info
{
    bool is_valid;
    int case_value; // Currently we only switch over enums/ints
    Symbol* variable_symbol;
};

struct Parameter_Info {
    Symbol* symbol;
};

struct Definition_Symbol_Info {
    Symbol* symbol;
};

struct Symbol_Lookup_Info {
    Symbol* symbol; // Resolved symbol
};

struct Path_Lookup_Info {
    Symbol* symbol; // Resolved symbol
};

struct Module_Info {
    Symbol_Table* symbol_table;
};

union Analysis_Info
{
    Expression_Info info_expr;
    Statement_Info info_stat;
    Code_Block_Info info_block;
    Case_Info info_case;
    Parameter_Matching_Info parameter_matching_info;
    Parameter_Info param_info;
    Definition_Symbol_Info definition_symbol_info;
    Symbol_Lookup_Info symbol_lookup_info;
    Path_Lookup_Info path_info;
    Module_Info module_info;
};

enum class Info_Query
{
    CREATE,
    READ_NOT_NULL,  // Value must be there, otherwise panic
    TRY_READ,       // May return 0
    CREATE_IF_NULL, // Always returns info (Creates one if not existing)
};

Expression_Info* pass_get_node_info(Analysis_Pass* pass, AST::Expression* node, Info_Query query);
Case_Info* pass_get_node_info(Analysis_Pass* pass, AST::Switch_Case* node, Info_Query query);
Statement_Info* pass_get_node_info(Analysis_Pass* pass, AST::Statement* node, Info_Query query);
Code_Block_Info* pass_get_node_info(Analysis_Pass* pass, AST::Code_Block* node, Info_Query query);
Symbol_Lookup_Info* pass_get_node_info(Analysis_Pass* pass, AST::Symbol_Lookup* node, Info_Query query);
Definition_Symbol_Info* pass_get_node_info(Analysis_Pass* pass, AST::Definition_Symbol* node, Info_Query query);
Parameter_Info* pass_get_node_info(Analysis_Pass* pass, AST::Parameter* node, Info_Query query);
Path_Lookup_Info* pass_get_node_info(Analysis_Pass* pass, AST::Path_Lookup* node, Info_Query query);
Module_Info* pass_get_node_info(Analysis_Pass* pass, AST::Module* node, Info_Query query);
Parameter_Matching_Info* pass_get_node_info(Analysis_Pass* pass, AST::Arguments* node, Info_Query query);

Datatype* expression_info_get_type(Expression_Info* info, bool before_context_is_applied);



// HELPERS
struct Predefined_Symbols
{
    // Symbols for primitive types
    Symbol* type_c_char;
    Symbol* type_bool;
    Symbol* type_int;
    Symbol* type_float;
    Symbol* type_u8;
    Symbol* type_u16;
    Symbol* type_u32;
    Symbol* type_u64;
    Symbol* type_i8;
    Symbol* type_i16;
    Symbol* type_i32;
    Symbol* type_i64;
    Symbol* type_f32;
    Symbol* type_f64;
    Symbol* type_byte;
    Symbol* type_c_string;
    Symbol* type_allocator;
    Symbol* global_allocator_symbol;
    Symbol* default_allocator_symbol;

    // Symbols for 'compiler' provided structs
    Symbol* type_type;
    Symbol* type_type_information;
    Symbol* type_any;
    Symbol* type_empty;
    Symbol* type_void_pointer;

    // Symbols for hardcoded types
    Symbol* hardcoded_type_info;
    Symbol* hardcoded_type_of;
    Symbol* hardcoded_assert;
    Symbol* hardcoded_print_bool;
    Symbol* hardcoded_print_i32;
    Symbol* hardcoded_print_f32;
    Symbol* hardcoded_print_string;
    Symbol* hardcoded_print_line;
    Symbol* hardcoded_read_i32;
    Symbol* hardcoded_read_f32;
    Symbol* hardcoded_read_bool;
    Symbol* hardcoded_random_i32;

    // Error-Symbol (Used when Symbol-Reads cannot be resolved, e.g. symbol not defined)
    Symbol* error_symbol;
};

// I currently need this so that a workload can analyse the same node multiple times
struct Analysis_Pass 
{
    Workload_Base* origin_workload;
};

struct AST_Info_Key
{
    Analysis_Pass* pass;
    AST::Node* base;
};

struct Node_Passes
{
    Dynamic_Array<Analysis_Pass*> passes;
    AST::Node* base;
};


// ERRORS
enum class Error_Information_Type
{
    ARGUMENT_COUNT,
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
    COMPTIME_MESSAGE,
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
        Datatype* type;
        Datatype_Function* function;
        struct {
            Datatype_Struct* struct_signature;
            String* member_id;
        } invalid_member;
        Workload_Base* cycle_workload;
        struct {
            Datatype* left_type;
            Datatype* right_type;
        } binary_op_types;
        Expression_Result_Type expression_type;
        const char* constant_message;
        const char* comptime_message;
    } options;
};

struct Semantic_Error
{
    const char* msg;
    AST::Node* error_node; // May be null
    Parser::Section section;
    Dynamic_Array<Error_Information> information;
};

void log_semantic_error(const char* msg, AST::Node* node, Parser::Section node_section = Parser::Section::WHOLE);
void semantic_analyser_set_error_flag(bool error_due_to_unknown);
void error_information_append_to_string(const Error_Information& info, String* string, Datatype_Format format = datatype_format_make_default());
void error_information_append_to_rich_text(const Error_Information& info, Rich_Text::Rich_Text* text, Datatype_Format format = datatype_format_make_default());
void semantic_analyser_append_all_errors_to_string(String* string, int indentation);




struct IR_Function;
struct Modtree_Function;
struct Function_Slot
{
    int index; // Not plus one !
    ModTree_Function* modtree_function; // May be null
    IR_Function* ir_function; // May be null
};

// ANALYSER
struct Semantic_Analyser
{
    // Result
    Dynamic_Array<Semantic_Error> errors;
    ModTree_Program* program;
    Hashtable<AST::Node*, Node_Passes> ast_to_pass_mapping;
    Hashtable<AST_Info_Key, Analysis_Info*> ast_to_info_mapping;

    // Other
    Dynamic_Array<Function_Slot> function_slots;
    Workload_Base* current_workload;
    Module_Progress* root_module;
    Predefined_Symbols predefined_symbols;
    Workload_Executer* workload_executer;
    Hashtable<AST::Expression*, Datatype_Template*> valid_template_parameters;
    ModTree_Global* global_allocator; // *Allocator
    ModTree_Global* default_allocator; // Allocator

    // Symbol tables
    Symbol_Table* root_symbol_table;
    Hashset<Symbol_Table*> symbol_lookup_visited;

    // Allocations
    Stack_Allocator comptime_value_allocator;
    Stack_Allocator global_variable_memory_pool;
    Dynamic_Array<Symbol_Table*> allocated_symbol_tables;
    Dynamic_Array<Symbol*> allocated_symbols;
    Dynamic_Array<Analysis_Pass*> allocated_passes;
    Dynamic_Array<Function_Progress*> allocated_function_progresses;
    Dynamic_Array<Operator_Context*> allocated_operator_contexts;
    Stack_Allocator progress_allocator;
};

Semantic_Analyser* semantic_analyser_initialize();
void semantic_analyser_destroy();
void semantic_analyser_reset();
void semantic_analyser_finish();
Function_Progress* analysis_workload_try_get_function_progress(Workload_Base* workload);

Datatype_Function* hardcoded_type_to_signature(Hardcoded_Type type);



