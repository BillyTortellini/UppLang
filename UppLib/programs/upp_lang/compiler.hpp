#pragma once

#include "../../win32/timing.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"
#include "type_system.hpp"
#include "compiler_misc.hpp"

struct Compiler;
struct AST_Parser;
struct Semantic_Analyser;
struct Intermediate_Generator;
struct Bytecode_Generator;
struct Bytecode_Interpreter;
struct C_Generator;
struct C_Compiler;
struct IR_Generator;
struct Type_Signature;
struct Dependency_Analyser;
struct Analysis_Item;
struct Source_Code;

namespace AST
{
    struct Base;
    struct Module;
}

// Compiler
struct Compiler
{
    // Compiler internals
    Dynamic_Array<Code_Source*> code_sources;
    Code_Source* main_source;
    bool generate_code; // This indicates if we want to compile (E.g. user pressed CTRL-B or F5)

    // Helpers
    Identifier_Pool identifier_pool;
    Constant_Pool constant_pool;
    Type_System type_system;
    Extern_Sources extern_sources;

    // Stages
    Dependency_Analyser* dependency_analyser;
    Semantic_Analyser* semantic_analyser;
    IR_Generator* ir_generator;
    Bytecode_Generator* bytecode_generator;
    Bytecode_Interpreter* bytecode_interpreter;
    C_Generator* c_generator;
    C_Compiler* c_compiler;

    // Timing stuff
    Timer* timer;
    Timing_Task task_current;
    double task_last_start_time;
    double time_lexing;
    double time_parsing;
    double time_rc_gen;
    double time_analysing;
    double time_code_gen;
    double time_output;
    double time_code_exec;
    double time_reset;

    // IDs
    String* id_data;
    String* id_size;
    String* id_tag;
    String* id_main;
    String* id_type_of;
    String* id_type_info;
};

extern Compiler compiler;

Compiler* compiler_initialize(Timer* timer);
void compiler_destroy();

void compiler_compile(Source_Code* source_code, bool generate_code, String project_path); // Takes ownership of project path
Exit_Code compiler_execute();
void compiler_add_source_code(Source_Code* source_code, Code_Origin origin, String file_path); // Takes ownership of source_code and file_path
bool compiler_errors_occured();
Code_Source* code_source_from_ast(AST::Base* base);
void compiler_switch_timing_task(Timing_Task task);
void compiler_run_testcases(Timer* timer);