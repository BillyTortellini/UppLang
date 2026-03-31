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
struct Upp_Function;
struct Semantic_Error;
struct Error_Information;
struct Expression_Info;
struct Analysis_Pass;
struct IR_Code_Block;
struct Upp_Struct;

struct Workload_Definition;
struct Workload_Base;
struct Workload_Import_Resolve;
struct Workload_Structure_Header;
struct Workload_Structure_Body;
struct Workload_Bake;
struct Workload_Function_Header;
struct Workload_Function_Body;

struct Datatype_Struct;
struct Bake_Progress;
struct Datatype_Pattern_Variable;
struct Datatype_Array;
struct Pattern_Variable_State;
struct Poly_Header;
struct Poly_Instance;
struct Compilation_Data;
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

enum class Poly_Type
{
    NORMAL, // Non-polymorphic
    BASE,
    PARTIAL, // Only some values are filled out
    INSTANCE
};

struct Upp_Function
{
    Call_Signature* signature; // Note: Signature is nullptr until function-header is analysed
    int function_index; // Index in functions array
    String* name; // Symbol name, or "bake_function"/"lambda_function"
    Symbol* symbol; // May be nullptr if function is anonymous
    AST::Expression* function_node;

    Workload_Function_Header* header_workload; // Points to base header workload if it's an instance
    Workload_Function_Body* body_workload;
    Workload_Bake* bake_workload;
    Workload_Definition* extern_definition_workload;
    Analysis_Pass* body_pass; // Used later in ir-generator

    Poly_Type poly_type;
    struct {
        Poly_Header* poly_header;
        Poly_Instance* instance;
    } options;

    bool is_extern;
    bool contains_errors;

    // Code-Generation
    IR_Code_Block* ir_block;
    int bytecode_start_instruction;
    int bytecode_end_instruction;
};

struct Upp_Struct
{
    Datatype_Struct* datatype;
    Symbol* symbol;
    AST::Expression* struct_node;

    Workload_Structure_Header* header_workload; 
    Workload_Structure_Body*   body_workload;  

    Poly_Type poly_type;
    struct {
        Poly_Header* header;
        Poly_Instance* instance;
    } options;

    bool is_union;
    bool is_extern_struct; // C-Generator needs to know if the type is already defined in a header
    DynArray<Datatype_Array*> types_waiting_for_size_finish; 
};

struct Upp_Global
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



// WORKLOADS
struct Workload_Base;
struct Semantic_Context
{
    // Data required for analysis
    Compilation_Data* compilation_data;
    Workload_Base* current_workload;
    Symbol_Table* current_symbol_table;
    Symbol_Access_Level symbol_access_level;
    Analysis_Pass* current_pass;

    bool can_create_workloads;
    bool error_logging_enabled;
    bool error_flagging_enabled;

    // Current position info's
    Upp_Function* current_function;
    Expression_Info* current_expression;
    bool statement_reachable;

    // Scratch data during analysis
    DynArray<AST::Code_Block*> block_stack;
    Arena* scratch_arena;
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
    ROOT, // Root workload, which spawns all other workloads

    MODULE_ANALYSIS, // This is basically just symbol discovery
    OPERATOR_CONTEXT_CHANGE,

    FUNCTION_HEADER,
    FUNCTION_BODY,

    STRUCT_HEADER,
    STRUCT_BODY,

    BAKE_ANALYSIS,

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

    // Errors
    int real_error_count;
    int errors_due_to_unknown_count;
    int error_checkpoint_count; // If error_checkpoint_count > 0, then errors aren't logged...

    // Polymorphic info
    int polymorphic_instanciation_depth; 
    Workload_Base* parent_workload;
};

struct Workload_Root
{
    Workload_Base base;
};

// Analyses context changes of a single type
struct Workload_Custom_Operator
{
    Workload_Base base;
    Custom_Operator_Type type_to_analyse; // Note: Dependencies are split on different custom-operator types
    Analysis_Pass* analysis_pass;
    Dynamic_Array<AST::Custom_Operator_Node*> change_nodes;
    Symbol_Table* symbol_table;
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
    Upp_Function* function;
    Symbol_Table* symbol_table;
};

struct Workload_Function_Body
{
    Workload_Base base;
    Upp_Function* function;
    Symbol_Table* parameter_table;
};

struct Workload_Definition
{
    Workload_Base base;
    Symbol* symbol;
    Symbol_Table* symbol_table;
    Analysis_Pass* analysis_pass;
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

struct Workload_Bake
{
    Workload_Base base;

    Symbol_Table* symbol_table;
    AST::Expression* bake_node;

    Upp_Function* bake_function;
    Datatype* result_type;
    Optional<Upp_Constant> result;
};




// Polymorphism
struct Pattern_Variable
{
    Datatype_Pattern_Variable* pattern_variable_type;
    int index; // Used to access values, index in pattern_variables array
    DynArray<int> dependent_params;

    // Origin infos
    Poly_Header* origin;
    String* name;
    Symbol* symbol;
    AST::Node* definition_node; // Could be either Expression::Pattern_Value or Argument
    bool is_comptime_parameter; // e.g. foo($A: int)
    int defined_in_parameter_index;
};

struct Poly_Parameter_Info
{
    int parameter_index;
	DynArray<int> depends_on_variables;
};

struct Poly_Header
{
    Call_Signature* signature; // Contains all parameters + return-type + implicit parameters
    DynArray<Pattern_Variable> pattern_variables; // Contains all pattern-variables, e.g. comptime and implicit parameters
    DynArray<Poly_Parameter_Info> param_infos; // More information for each call-signature parameter
    DynSet<Poly_Instance*> instances;

    // Origin infos
    AST::Signature* signature_node;
    Symbol_Table* base_parameter_table;
    String* name; // Either struct or function name, used for dot-calls auto id
    bool is_function;
    union {
        Upp_Struct*   upp_struct;
        Upp_Function* function;
    } origin;
};

struct Poly_Instance
{
    // Note: Because of implicit polymorphism all parameter-types need to be stored for differentiation
    Poly_Header* header;
    Array<Pattern_Variable_State> variable_states;
    Array<Datatype*> parameter_types; // Does not contain implicit parameters, e.g. size == parameter_nodes.size
    union {
        Upp_Function* function_instance;
        Upp_Struct*   struct_instance;
    } options;
};



// Structures
struct Workload_Structure_Body
{
    Workload_Base base;
    Upp_Struct* upp_struct;
    Symbol_Table* symbol_table; // May be parameter-table in instance
    Symbol_Access_Level symbol_access_level;
};

struct Workload_Structure_Header
{
    Workload_Base base;
    Upp_Struct* upp_struct;
    Symbol_Table* symbol_table;
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
    Compilation_Data* compilation_data;
    Dynamic_Array<Workload_Base*> all_workloads; // Owning array
    Dynamic_Array<Workload_Base*> runnable_workloads;
    Dynamic_Array<Workload_Base*> finished_workloads;
    bool progress_was_made;
    Hashtable<Workload_Pair, Dependency_Information> workload_dependencies;
};

Workload_Executer* workload_executer_create(Compilation_Data* compilation_data);
void workload_executer_destroy(Workload_Executer* executer);
void workload_executer_resolve(Workload_Executer* executer, Compilation_Data* compilation_data);
Workload_Root* workload_executer_add_root_workload(Compilation_Data* compilation_data);
Workload_Module_Analysis* workload_executer_add_module_discovery(AST::Module* module_node, Compilation_Data* compilation_data);



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
    Upp_Function* custom_cast_function; // Optional
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
		Upp_Function* function;
		Poly_Function poly_function;
		Workload_Structure_Header* poly_struct;
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
        Upp_Function* function;
		Upp_Struct* struct_instance;
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
    AUTO_DEREFERENCE,       // 
};

struct Expression_Context
{
    Expression_Context_Type type;
    Datatype* datatype;
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
            bool is_temporary;
        } value;
        Datatype* datatype;
        Datatype* polymorphic_pattern; 
        Workload_Structure_Header* polymorphic_struct;
        Upp_Function* function;
        Poly_Function poly_function;
        Hardcoded_Type hardcoded;
        Symbol_Table* module_table;
        Upp_Constant constant;
    } options;

    bool is_valid; // If this expression contains any errors (Not recursive), currently only used for comptime-calculation (And code editor I guess?)
    union 
    {
        Call_Info* call_info; // Same as get_info(arguments)
        Datatype_Struct* struct_init_lowest_subtype; // Used by ir-code to set tags
        struct 
        {
            Member_Access_Type type;
            union {
                struct {
                    Upp_Struct* upp_struct;
                    int index;
                } poly_access;
                Struct_Member member;
            } options;
        } member_access;
        struct 
        {
            Upp_Function* function; // Is null if it's a primitive overload (e.g. not overloaded)
            bool switch_left_and_right;
        } overload;
    } specifics;

    Expression_Context context; // Maybe I don't even want to store the context
    Cast_Info cast_info;
};

Expression_Value_Info expression_info_get_value_info(Expression_Info* info, Type_System* type_system);
Datatype* expression_info_get_type(Expression_Info* info, bool before_context_is_applied, Type_System* type_system);
bool expression_has_memory_address(AST::Expression* expr, Semantic_Context* semantic_context);

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
            Upp_Function* function;
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
                Upp_Function* fn_create;
                Upp_Function* fn_has_next;
                Upp_Function* fn_next;
                Upp_Function* fn_get_value;
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

Expression_Info* pass_get_node_info(Analysis_Pass* pass, AST::Expression* node, Info_Query query, Compilation_Data* compilation_data);
Case_Info* pass_get_node_info(Analysis_Pass* pass, AST::Switch_Case* node, Info_Query query, Compilation_Data* compilation_data);
Statement_Info* pass_get_node_info(Analysis_Pass* pass, AST::Statement* node, Info_Query query, Compilation_Data* compilation_data);
Code_Block_Info* pass_get_node_info(Analysis_Pass* pass, AST::Code_Block* node, Info_Query query, Compilation_Data* compilation_data);
Symbol_Lookup_Info* pass_get_node_info(Analysis_Pass* pass, AST::Symbol_Lookup* node, Info_Query query, Compilation_Data* compilation_data);
Definition_Symbol_Info* pass_get_node_info(Analysis_Pass* pass, AST::Definition_Symbol* node, Info_Query query, Compilation_Data* compilation_data);
Parameter_Info* pass_get_node_info(Analysis_Pass* pass, AST::Parameter* node, Info_Query query, Compilation_Data* compilation_data);
Path_Lookup_Info* pass_get_node_info(Analysis_Pass* pass, AST::Path_Lookup* node, Info_Query query, Compilation_Data* compilation_data);
Module_Info* pass_get_node_info(Analysis_Pass* pass, AST::Module* node, Info_Query query, Compilation_Data* compilation_data);
Call_Info* pass_get_node_info(Analysis_Pass* pass, AST::Call_Node* node, Info_Query query, Compilation_Data* compilation_data);




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

struct Matching_Constraint
{
	Datatype_Pattern_Variable* pattern_variable;
	Upp_Constant value;
};

struct Pattern_Matcher
{
    Compilation_Data* compilation_data;
	DynArray<Matching_Constraint> constraints;
	int max_match_depth;
};
Pattern_Matcher pattern_matcher_make(Compilation_Data* compilation_data, Arena* arena);
bool pattern_matcher_match_types(Pattern_Matcher& result, Datatype* type_a, Datatype* type_b, int match_depth = 0);
bool pattern_matcher_match_type_and_value(
    Pattern_Matcher& result, Datatype* datatype, Upp_Constant& value, int match_depth = 0 
);
bool pattern_match_result_check_constraints_pairwise(Pattern_Matcher& results);


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

void log_semantic_error(Semantic_Context* semantic_context, const char* msg, AST::Node* node, Node_Section node_section = Node_Section::WHOLE);
void semantic_analyser_set_error_flag(bool error_due_to_unknown, Semantic_Context* semantic_context);
void error_information_append_to_rich_string(
    const Error_Information& info, Compilation_Data* compilation_data, String* text, 
    Datatype_Format format = datatype_format_make_default()
);
void semantic_analyser_append_semantic_errors_to_string(Compilation_Data* compilation_data, String* string, int indentation);



// ANALYSER (Does not really exist anymore)
Semantic_Context semantic_context_make(
    Compilation_Data* compilation_data, Workload_Base* workload,
    Symbol_Table* symbol_table, Symbol_Access_Level symbol_access_level,
    Analysis_Pass* analysis_pass, Arena* scratch_arena
);
Upp_Function* analysis_workload_try_get_upp_function(Workload_Base* workload);

Upp_Function* upp_function_create_empty(Call_Signature* signature, String* name, Compilation_Data* compilation_data);
Upp_Global* compilation_data_add_global(Semantic_Context* semantic_context, Datatype* datatype, Symbol* symbol, bool is_extern);
Upp_Global* compilation_data_add_global_assert_type_finished(Compilation_Data* compilation_data, Datatype* datatype, Symbol* symbol, bool is_extern);

void log_error_info_symbol(Semantic_Context* context, Symbol* symbol);
void analysis_workload_destroy(Workload_Base* workload);
void analysis_workload_append_to_string(Workload_Base* workload, String* string);

Cast_Type check_if_type_modifier_update_valid(Type_Modifier_Info src_mods, Type_Modifier_Info dst_mods, bool source_is_temporary);
Cast_Info check_if_cast_possible(
    Datatype* from_type, Datatype* to_type, bool value_is_temporary, bool is_auto_cast, Semantic_Context* semantic_context
);
