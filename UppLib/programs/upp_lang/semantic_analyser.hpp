#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/allocators.hpp"
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
struct Datatype_Struct;
struct Bake_Progress;
struct Datatype_Pattern_Variable;
struct Pattern_Variable_State;
struct Poly_Header;
struct Compiler_Analysis_Data;
struct Compilation_Unit;

namespace Parser
{
    enum class Node_Section;
}

namespace AST
{
    struct Node;
    struct Code_Block;
    struct Path_Lookup;
    struct Expression;
    struct Module;
}



struct Upp_Module
{
    AST::Module* node; // Is only null for compiler-generated modules
    Symbol_Table* symbol_table;
    bool is_file_module;
    union {
        Compilation_Unit* compilation_unit;
        Symbol* module_symbol;
    } options;
};

enum class ModTree_Function_Type
{
    NORMAL,
    BAKE,
    EXTERN
};

// Modtree TODO: Rename this into something more sensible, like Upp-Function
struct ModTree_Function
{
    Call_Signature* signature;
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

// The pattern_values define what happens when a pattern expression is analysed.
enum class Pattern_Variable_State_Type
{
    SET,     // Comptime value was assigned to this variable
    UNSET,   // Causes panic when accessed, means that this variable wasn't matched yet
    PATTERN, // Returns pattern-type on access
};

struct Pattern_Variable_State
{
    Pattern_Variable_State_Type type;
    union {
        Upp_Constant value; // Only valid if state = SET
        Datatype* pattern_type;
    } options;
};

enum class Analysis_Workload_Type
{
    MODULE_ANALYSIS, // This is basically just symbol discovery
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

    // Dependencies
    List<Workload_Base*> dependencies;
    List<Workload_Base*> dependents;

    // Note: Clustering is required for Workloads where cyclic dependencies on the same workload-type are allowed,
    //       like recursive functions or structs containing pointers to themselves
    Workload_Base* cluster;
    Dynamic_Array<Workload_Base*> reachable_clusters;

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

    // Errors
    int real_error_count;
    int errors_due_to_unknown_count;
    int error_checkpoint_count; // If error_checkpoint_count > 0, then errors aren't logged...

    // Polymorphic value access is rather complicated, here are some points to consider:
    //  * Multiple sets of polymorphic values can be active at once (Poly-Function defined in Poly-Function)
    //      Update: Don't think poly in poly works, but anonymous structs, lambdas and #bake have access to the values...
    //  * Child-Workloads inherit the polymorphic-values of their parents, so we need to store a parent-child relation
    //  * Polymorphic-Instances define their own poly-values
    Workload_Base* poly_parent_workload; // Note: This is a logical parent workload, e.g. function_body -> function_header -> module_analysis
    int polymorphic_instanciation_depth; 

    Array<Pattern_Variable_State> active_pattern_variable_states; // Non-owning
    Poly_Header* active_pattern_variable_states_origin;

    Arena scratch_arena;
};

// Analyses context changes of a single type
struct Workload_Custom_Operator
{
    Workload_Base base;
    Custom_Operator_Type type_to_analyse; // Note: Dependencies are split on different custom-operator types
    Dynamic_Array<AST::Custom_Operator_Node*> change_nodes;
    Custom_Operator_Table* operator_table;
};

struct Workload_Module_Analysis
{
    Workload_Base base;
    AST::Module* module_node;
};

struct Workload_Function_Header
{
    Workload_Base base;
    Function_Progress* progress;
    AST::Expression* function_node;
    // Note: this is an owning pointer, and it is always set, even if the function is not polymorphic
    //      But it may be null for instanciated functions, or inferred functions
    Poly_Header* poly_header;
};

struct Workload_Function_Body
{
    Workload_Base base;
    Function_Progress* progress;
    AST::Body_Node body_node;
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
struct Pattern_Variable
{
    int value_access_index; // Used to access values, index in pattern_variables array
    Datatype_Pattern_Variable* pattern_variable_type;

    // Origin infos
    Poly_Header* origin;
    String* name;
    Symbol* symbol;
    AST::Node* definition_node; // Could be either Expression::Pattern_Value or Argument
    bool is_comptime_parameter; // e.g. foo($A: int)
    int defined_in_parameter_index;
};

struct Poly_Instance;

struct Poly_Header
{
    // Note: pattern_variables and parameter_nodes arrays may all have different sizes
    Call_Signature* signature;
    Dynamic_Array<Pattern_Variable> pattern_variables;
    Hashset<Poly_Instance*> instances;
    Array<Pattern_Variable_State> base_analysis_states;

    // For convenience
    Dynamic_Array<AST::Parameter*> parameter_nodes;
    Symbol_Table* parameter_table;

    // Origin infos
    String* name; // Either struct or function name, for dot-calls auto id
    int partial_pattern_count;
    bool is_function;
    union {
        Workload_Structure_Polymorphic* struct_workload;
        Function_Progress* function_progress;
    } origin;
};

enum class Poly_Instance_Type
{
	FUNCTION,
	STRUCTURE,
	STRUCT_PATTERN,
};

struct Poly_Instance
{
    Poly_Header* header;
    Array<Pattern_Variable_State> variable_states;
    // Note: we need all pattern instances because of "implicit" polymorphism, e.g.
    //      foo :: (a: Node(_)) 
    // Because in such cases no variables are available to differentiate instances
    // Also for convenience struct-pattern instances have these set to {nullptr, 0}
    Array<Datatype*> partial_pattern_instances;
    Poly_Instance_Type type;
    union {
        Function_Progress* function_instance;
        Workload_Structure_Body* struct_instance;
        Datatype_Struct_Pattern* struct_pattern;
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
            Poly_Instance* poly_instance;
        } instance;
    } polymorphic;
};

struct Workload_Structure_Polymorphic
{
    Workload_Base base;
    Workload_Structure_Body* body_workload;
    Poly_Header* poly_header;
};



// ANALYSIS_PROGRESS

// Note: This type is only used so we can have pointers to a polymorphic-function
struct Function_Progress
{
    ModTree_Function* function;

    Workload_Function_Header* header_workload; // Points to base header workload if it's an instance
    Workload_Function_Body* body_workload;
    Workload_Function_Cluster_Compile* compile_workload;

    Polymorphic_Analysis_Type type;
    Poly_Function poly_function; // If instance, this points to base progress, otherwise it points to itself
};

struct Bake_Progress
{
    ModTree_Function* bake_function;
    Datatype* result_type;
    Optional<Upp_Constant> result;

    Workload_Bake_Analysis* analysis_workload;
    Workload_Bake_Execution* execute_workload;
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
    Dynamic_Array<Workload_Base*> runnable_workloads;
    Dynamic_Array<Workload_Base*> finished_workloads;
    bool progress_was_made;

    Hashtable<Workload_Pair, Dependency_Information> workload_dependencies;
};

void workload_executer_resolve();
Workload_Module_Analysis* workload_executer_add_module_discovery(AST::Module* module_node, bool is_root_module);



// ANALYSIS-INFOS (AST-Annotations)
enum class Parameter_Value_Type
{
    NOT_SET,             // No parameter was provided, e.g. use default value
    ARGUMENT_EXPRESSION, // Parameter is given as an argument expression
    DATATYPE_KNOWN,      // Datatype is known, used for return-value types, instanciate (where only types are known) and loop iterator
	COMPTIME_VALUE       // Used during instanciate_pattern of struct-patterns
};

struct Parameter_Value
{
    Parameter_Value_Type value_type;
	union {
		int argument_index; // -1 if not set
		Upp_Constant constant;
        Datatype* datatype;
	} options;
};

struct Argument_Info
{
	AST::Expression* expression;
	Optional<String*> name;
	int parameter_index; // -1 if no matching parameter was found
};

struct Cast_Info
{
    Cast_Type cast_type;
    ModTree_Function* custom_cast_function; // Optional
    Datatype* result_type;
};

struct Expression_Value_Info 
{
    Datatype* initial_type;
    Datatype* result_type;
    bool initial_value_is_temporary;
    bool result_value_is_temporary;
};

enum class Call_Origin_Type
{
    FUNCTION,
    POLY_FUNCTION,
    POLY_STRUCT,
    STRUCT_INITIALIZER,
    UNION_INITIALIZER,
	SLICE_INITIALIZER, // Struct-initializer syntax for slices
	HARDCODED,
    FUNCTION_POINTER,
	CONTEXT_CHANGE,
    CAST,
	ERROR_OCCURED, // In case of non-resolvable overloads or invalid symbol
};

struct Call_Origin
{
	Call_Signature* signature;
    Call_Origin_Type type;
    union {
		ModTree_Function* function;
		Poly_Function poly_function;
		Workload_Structure_Polymorphic* poly_struct;
		Hardcoded_Type hardcoded;
		Datatype_Slice* slice_type;
		Datatype_Function_Pointer* function_pointer;
		Custom_Operator_Type context_change_type;
		Datatype_Struct* structure;
    } options;
};

// Note:
// Call_Info is mostly used for convenience when generating code, e.g. in IR-Generator.
// This is why we store struct/union/slice initializer seperately, even though the result-type could be used to 
// find out which one is currently used
struct Call_Info
{
	Call_Origin origin;
    Array<Parameter_Value> parameter_values;
	Array<Argument_Info> argument_infos;
    AST::Call_Node* call_node; // May be null

	bool argument_matching_success;
	bool instanciated;
    union 
	{
        ModTree_Function* function;
		Datatype_Struct* struct_instance;
		Datatype_Struct_Pattern* struct_pattern;
		Datatype_Primitive* bitwise_primitive_type; // For bitwise hardcoded functions
        Cast_Info cast_info;
		struct 
		{
			bool subtype_valid;
			bool supertype_valid;
		} initializer_info;
    } instanciation_data;
};

enum class Expression_Context_Type
{
    NOT_SPECIFIED,          // Type is not known in given context
    ERROR_OCCURED,          // An error occured, so type may or may not be known
    SPECIFIC_TYPE_EXPECTED, // Type is known
};

struct Expression_Context
{
    Expression_Context_Type type;
    Datatype* datatype;
    bool auto_dereference;
};



// ANALYSIS INFO
enum class Expression_Result_Type
{
    VALUE,
    DATATYPE,
    CONSTANT,
    FUNCTION,
    HARDCODED_FUNCTION,
    POLYMORPHIC_STRUCT, 
    POLYMORPHIC_FUNCTION,
    POLYMORPHIC_PATTERN,
    NOTHING  // Functions returning void
};

struct Expression_Info
{
    // All types in "options" union are before the expression context has been applied
    Expression_Result_Type result_type;
    union
    {
        struct {
            Datatype* datatype; 
            int auto_dereference_count;
            bool is_temporary;
        } value;
        Datatype* datatype;
        Datatype* polymorphic_pattern; 
        Workload_Structure_Polymorphic* polymorphic_struct;
        ModTree_Function* function;
        Poly_Function poly_function;
        Hardcoded_Type hardcoded;
        Symbol_Table* module_table;
        Upp_Constant constant;
    } options;

    bool is_valid; // If this expression contains any errors (Not recursive), currently only used for comptime-calculation (And code editor I guess?)
    union 
    {
        Call_Info* call_info; // Same as get_info(arguments)
        struct {
            Member_Access_Type type;
            union {
                struct {
                    Workload_Structure_Body* struct_workload;
                    int index; // Either normal member index, or polymorphic parameter index
                } poly_access;
                Struct_Member member;
            } options;
        } member_access;
        struct {
            ModTree_Function* function; // Is null if it's a primitive overload (e.g. not overloaded)
            bool switch_left_and_right;
        } overload;
    } specifics;

    Expression_Context context; // Maybe I don't even want to store the context
    Cast_Info cast_info;
};

Expression_Value_Info expression_info_get_value_info(Expression_Info* info, Type_System* type_system = nullptr);
Datatype* expression_info_get_type(Expression_Info* info, bool before_context_is_applied);
bool expression_has_memory_address(AST::Expression* expr);

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
            Datatype_Struct* structure; // May be null for simple enum switch
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
    Upp_Module* upp_module;
};


struct Analysis_Info
{
    union {
        Expression_Info info_expr;
        Statement_Info info_stat;
        Code_Block_Info info_block;
        Case_Info info_case;
        Parameter_Info param_info;
        Definition_Symbol_Info definition_symbol_info;
        Symbol_Lookup_Info symbol_lookup_info;
        Path_Lookup_Info path_info;
        Module_Info module_info;
        Call_Info call_info; // For AST::Call_Node*
    };
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
Call_Info* pass_get_node_info(Analysis_Pass* pass, AST::Call_Node* node, Info_Query query);

Datatype* expression_info_get_type(Expression_Info* info, bool before_context_is_applied);



// HELPERS
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
        Datatype_Function_Pointer* function;
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
    Node_Section section;
    Dynamic_Array<Error_Information> information;
};

void log_semantic_error_outside(const char* msg, AST::Node* node, Node_Section node_section);
void semantic_analyser_set_error_flag(bool error_due_to_unknown);
void error_information_append_to_string(
    const Error_Information& info, Compiler_Analysis_Data* analysis_data, 
    String* string, Datatype_Format format = datatype_format_make_default()
);
void error_information_append_to_rich_text(
    const Error_Information& info, Compiler_Analysis_Data* analysis_data, Rich_Text::Rich_Text* text, 
    Datatype_Format format = datatype_format_make_default()
);
void semantic_analyser_append_semantic_errors_to_string(Compiler_Analysis_Data* analysis_data, String* string, int indentation);




struct IR_Function;
struct Modtree_Function;
struct Function_Slot
{
    int index; // Not plus one !
    ModTree_Function* modtree_function; // May be null
    IR_Function* ir_function; // May be null
    int bytecode_start_instruction; // -1 if not generate yet
    int bytecode_end_instruction;
};

// ANALYSER
struct Semantic_Analyser
{
    // Result
    Workload_Base* current_workload;
    Symbol* error_symbol;
    Workload_Executer* workload_executer;
    ModTree_Global* global_allocator; // Datatype: Allocator
    ModTree_Global* system_allocator; // Datatype: Allocator
    Hashtable<AST::Code_Block*, Symbol_Table*> code_block_comptimes; // To prevent re-analysis of code-block comptimes
};

Semantic_Analyser* semantic_analyser_initialize();
void semantic_analyser_destroy();
void semantic_analyser_reset();
void semantic_analyser_finish();
Function_Progress* analysis_workload_try_get_function_progress(Workload_Base* workload);



ModTree_Program* modtree_program_create();
void modtree_program_destroy(ModTree_Program* program);
void function_progress_destroy(Function_Progress* progress);
void analysis_workload_destroy(Workload_Base* workload);
void analysis_workload_append_to_string(Workload_Base* workload, String* string);

// If search_start_workload == nullptr, we start from semantic_analyser.current_workload
Workload_Base* pattern_variable_find_instance_workload(
    Pattern_Variable* variable,
    Workload_Base* search_start_workload = nullptr,
    bool called_from_editor = false
);
