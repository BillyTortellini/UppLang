
#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "source_code.hpp"
#include "compiler_misc.hpp"
#include "semantic_analyser.hpp"
#include "symbol_table.hpp"

namespace AST
{
    struct Node;
};


// Semantic_Info
enum class Semantic_Info_Type
{
    EXPRESSION_INFO,
    SYMBOL_LOOKUP,
    CALL_INFORMATION,
    ARGUMENT,
    MARKUP,
    ERROR_ITEM
};

struct Semantic_Info_Symbol {
    Symbol* symbol;
    bool is_definition;
    Analysis_Pass* pass;
};

struct Semantic_Info_Expression {
    AST::Expression* expr;
    Expression_Info* info;
    bool is_member_access;
    struct {
        Datatype* value_type; // Since the Editor cannot query Analysis-Pass, we store member-access infos here...
        bool has_definition;
        Text_Index definition_index;
        Compilation_Unit* member_definition_unit;
    } member_access_info;
};

struct Semantic_Info_Call {
    Parameter_Matching_Info* matching_info;
    AST::Arguments* arguments_node;
};

struct Semantic_Info_Argument {
    AST::Arguments* arguments_node;
    int argument_index;
};

union Semantic_Info_Option 
{
    Semantic_Info_Option() {};
    Semantic_Info_Expression expression;
    Semantic_Info_Symbol symbol_info;
    Semantic_Info_Call call_info;
    Semantic_Info_Argument argument_info;
    vec3 markup_color;
    int error_index;
};

struct Semantic_Info
{
    Semantic_Info_Type type;
    Semantic_Info_Option options;
    Analysis_Pass* pass;
    int analysis_item_index;
};



struct Compiler_Analysis_Data
{
    // Compiler data
    Dynamic_Array<Compiler_Error_Info> compiler_errors; // List of parser and semantic errors
    Constant_Pool constant_pool;
    Type_System type_system;
    Extern_Sources extern_sources;

    // Semantic analyser
    ModTree_Program* program;
    Dynamic_Array<Function_Slot> function_slots;
    Dynamic_Array<Semantic_Error> semantic_errors;

    Hashtable<AST::Node*, Node_Passes> ast_to_pass_mapping;
    Hashtable<AST_Info_Key, Analysis_Info*> ast_to_info_mapping;
    Module_Progress* root_module;

    // Workload executer
    Dynamic_Array<Workload_Base*> all_workloads;

    // Allocations
    Stack_Allocator global_variable_memory_pool;
    Stack_Allocator progress_allocator;
    Dynamic_Array<Symbol_Table*> allocated_symbol_tables;
    Dynamic_Array<Symbol*> allocated_symbols;
    Dynamic_Array<Analysis_Pass*> allocated_passes;
    Dynamic_Array<Function_Progress*> allocated_function_progresses;
    Dynamic_Array<Operator_Context*> allocated_operator_contexts;
    Dynamic_Array<Dynamic_Array<Dot_Call_Info>*> allocated_dot_calls;
    Dynamic_Array<AST::Node*> allocated_nodes;

    // Semantic_Info
    Dynamic_Array<Semantic_Info> semantic_infos;
    int next_analysis_item_index;
};

Compiler_Analysis_Data* compiler_analysis_data_create();
void compiler_analysis_data_destroy(Compiler_Analysis_Data* data);

Dynamic_Array<Dot_Call_Info>* compiler_analysis_data_allocate_dot_calls(Compiler_Analysis_Data* data, int capacity = 0);
void compiler_analysis_update_source_code_information();



