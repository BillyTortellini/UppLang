
#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "source_code.hpp"
#include "compiler_misc.hpp"
#include "semantic_analyser.hpp"
#include "symbol_table.hpp"
#include "../../datastructures/allocators.hpp"

namespace AST
{
    struct Node;
    struct Expression;
    struct Call_Node;
    struct Code_Block;
};

struct Bytecode_Instruction;
struct Call_Signature;
struct Fiber_Pool;
struct Source_Code;
struct Upp_Module;
struct Workload_Root;
struct Upp_Global;
struct Symbol_Table;
struct Pattern_Variable;
struct Datatype_Format;
struct Type_System;
struct Call_Signature;
struct Modtree_Function;
struct Datatype_Struct;
struct Workload_Structure_Header;
struct Analysis_Pass;
struct Call_Info;
struct Symbol;
struct IR_Generator;
struct C_Generator;



// Editor_Info
enum class Editor_Info_Type
{
    EXPRESSION_INFO,
    SYMBOL_LOOKUP,
    CALL_INFORMATION,
    ARGUMENT,
    MARKUP,
    ERROR_ITEM
};

struct Editor_Info_Symbol {
    Symbol* symbol;
    bool is_definition;
    Analysis_Pass* pass;
    bool add_color;
};

struct Editor_Info_Expression 
{
    AST::Expression* expr;
    Expression_Info* info;
    Analysis_Pass* analysis_pass;
    bool is_member_access;
    struct {
        Datatype* value_type; // Since the Editor cannot query Analysis-Pass, we store member-access infos here...
        bool has_definition;
        Text_Index definition_index;
        Compilation_Unit* member_definition_unit;
    } member_access_info;
};

struct Editor_Info_Call 
{
    Call_Info* callable_call;
    AST::Call_Node* call_node;
};

struct Editor_Info_Argument {
    AST::Call_Node* call_node;
    int argument_index;
    int test;
};

union Editor_Info_Option 
{
    Editor_Info_Option() {};
    Editor_Info_Expression expression;
    Editor_Info_Symbol symbol_info;
    Editor_Info_Argument argument_info;
    Editor_Info_Call call_info;
    Syntax_Color markup_color;
    int error_index;
};

struct Editor_Info
{
    Editor_Info_Type type;
    Editor_Info_Option options;
    Analysis_Pass* pass;
    int analysis_item_index;
};

struct Compilation_Data
{
    // Compiler data
    Compile_Type compile_type;
    Compilation_Unit* main_unit;
    Dynamic_Array<Compilation_Unit*> compilation_units;
    Dynamic_Array<Compiler_Error_Info> compiler_errors; // List of parser and semantic errors
    Dynamic_Array<Semantic_Error> semantic_errors;

    // Program
    Dynamic_Array<Upp_Function*> functions;
    Dynamic_Array<Upp_Global*> globals;
    DynArray<Bytecode_Instruction> bytecode;

    // Known functions
    Upp_Function* main_function;
    Upp_Function* entry_function;
    Upp_Function* default_allocate_function;
    Upp_Function* default_free_function;
    Upp_Function* default_reallocate_function;

    // Resources
    Fiber_Pool* fiber_pool; 
    Identifier_Pool identifier_pool;
    Constant_Pool* constant_pool;
    Type_System* type_system;
    Extern_Sources extern_sources;

    // Stages
    Workload_Executer* workload_executer;
    IR_Generator* ir_generator;
    C_Generator* c_generator;

    // Semantic-Analysis information
    Workload_Root* root_workload;
    Symbol_Table* root_symbol_table;
    Upp_Module* builtin_module;

    Hashtable<AST::Node*, Node_Passes> ast_to_pass_mapping;
    Hashtable<AST_Info_Key, Analysis_Info*> ast_to_info_mapping;
    Hashtable<AST::Code_Block*, Symbol_Table*> code_block_comptimes; // To prevent re-analysis of comptime-definitions in code-blocks

    Symbol* error_symbol;
    Upp_Global* global_allocator; // Datatype: Allocator
    Upp_Global* system_allocator; // Datatype: Allocator

    // Editor_Info
    Dynamic_Array<Editor_Info> semantic_infos;
    int next_analysis_item_index;

    // Call_Signatures and callables
    Hashset<Call_Signature*> call_signatures; // Callables get duplicated
    Hashtable<Custom_Operator, Custom_Operator*> custom_operator_deduplication;
    Call_Signature* hardcoded_function_signatures[(int)Hardcoded_Type::MAX_ENUM_VALUE];
    Call_Signature* context_change_type_signatures[(int)Custom_Operator_Type::MAX_ENUM_VALUE];
    Call_Signature* cast_signature;
    Call_Signature* empty_call_signature;

    // Allocations
    Arena arena;
    Arena tmp_arena;
    Dynamic_Array<Symbol_Table*> allocated_symbol_tables;
    Dynamic_Array<Symbol*> allocated_symbols;
    Dynamic_Array<Analysis_Pass*> allocated_passes;
    Dynamic_Array<Custom_Operator_Table*> allocated_custom_operator_tables;
    Dynamic_Array<AST::Node*> allocated_nodes; // Why is this here, I thought this was somewhere else

    // Timing stuff
    Timing_Task task_current;
    double task_last_start_time;
    double time_compile_start;
    double time_lexing;
    double time_parsing;
    double time_analysing;
    double time_code_gen;
    double time_output;
    double time_code_exec;
    double time_reset;
};

Compilation_Data* compilation_data_create(Fiber_Pool* fiber_pool);
Compilation_Unit* compilation_data_add_compilation_unit_unique(Compilation_Data* compilation_data, String filepath, bool load_file_if_new);
void compilation_data_destroy(Compilation_Data* data);
void compilation_data_finish_semantic_analysis(Compilation_Data* compilation_data);
void compilation_data_update_source_code_information(Compilation_Data* compilation_data);



// Note: Call_Signatures get deduplicated (Because function-pointer-types get deduplicated, so we need to do this anyway)
Call_Signature* call_signature_create_empty();
void call_signature_destroy(Call_Signature* signature);
// Note: Returned pointer is invalidated if another parameter is added
Call_Parameter* call_signature_add_parameter(
    Call_Signature* signature, String* name, Datatype* datatype, 
    bool required, bool requires_named_addressing, bool must_not_be_set);

Call_Parameter* call_signature_add_return_type(Call_Signature* signature, Datatype* datatype, Compilation_Data* compilation_data);
// Takes ownership of signature, returns deduplicated signature
Call_Signature* call_signature_register(Call_Signature* signature, Compilation_Data* compilation_data); 
void call_signature_append_to_string(Call_Signature* signature, String* string, Type_System* type_system, Datatype_Format format);

Source_Code* source_code_load_from_file(String filepath);



