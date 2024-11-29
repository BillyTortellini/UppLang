
#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "source_code.hpp"
#include "compiler_misc.hpp"
#include "semantic_analyser.hpp"

void compiler_analysis_update_source_code_information();

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
};

Compiler_Analysis_Data* compiler_analysis_data_create(Timer* timer);
void compiler_analysis_data_destroy(Compiler_Analysis_Data* data);



