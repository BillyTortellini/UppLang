#pragma once

#include "../../win32/timing.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"
#include "../../utility/random.hpp"
#include "type_system.hpp"
#include "compiler_misc.hpp"
#include "constant_pool.hpp"
#include "../../win32/thread.hpp"

struct Compiler;
struct AST_Parser;
struct Semantic_Analyser;
struct Intermediate_Generator;
struct Module_Progress;
struct Bytecode_Generator;
struct Bytecode_Thread;
struct C_Generator;
struct C_Compiler;
struct IR_Generator;
struct Source_Code;
struct Code_History;
struct Compiler_Analysis_Data;


namespace AST
{
    struct Node;
    struct Module;
}

enum class Compile_Type
{
    ANALYSIS_ONLY,
    BUILD_CODE,
};

struct Compilation_Unit
{
    Source_Code* code;
    bool open_in_editor;
    int editor_tab_index; // -1 if not opened
    bool used_in_last_compile;
    String filepath; // For deduplication, full-filepath

    // Analysis-Info
    AST::Module* root;
    Dynamic_Array<AST::Node*> allocated_nodes;
    Dynamic_Array<Error_Message> parser_errors;
    Module_Progress* module_progress; // Analysis progress, may be 0 if not analysed yet
};

// Compiler
struct Compiler
{
    // Compiler internals
    Dynamic_Array<Compilation_Unit*> compilation_units;
    Compilation_Unit* main_unit;
    bool generate_code; // This indicates if we want to compile (E.g. user pressed CTRL-B or F5)
    Compiler_Analysis_Data* analysis_data;

    // Helpers
    Identifier_Pool identifier_pool;
    Fiber_Pool* fiber_pool;
    Random random;

    // Stages
    Semantic_Analyser* semantic_analyser;
    IR_Generator* ir_generator;
    Bytecode_Generator* bytecode_generator;
    C_Generator* c_generator;
    C_Compiler* c_compiler;
    Semaphore add_compilation_unit_semaphore;

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

extern Compiler compiler;

Compiler* compiler_initialize();
void compiler_destroy();

// Expects file_path to be a full path (For deduplication)
// If source_code is null, and we haven't loaded the file previously, the file is loaded
// Returns 0 if file does not exist
Compilation_Unit* compiler_add_compilation_unit(String file_path, bool open_in_editor, bool is_import_file);

void compiler_compile(Compilation_Unit* main_unit, Compile_Type compile_type);
Module_Progress* compiler_import_and_queue_analysis_workload(AST::Import* import_node); // Returns 0 if file couldn't be read
bool compiler_can_execute_c_compiled(Compiler_Analysis_Data* analysis_data);
Exit_Code compiler_execute(Compiler_Analysis_Data* analysis_data);

bool compiler_errors_occured(Compiler_Analysis_Data* analysis_data);
Compilation_Unit* compiler_find_ast_compilation_unit(AST::Node* base);
void compiler_switch_timing_task(Timing_Task task);
void compiler_run_testcases(bool force_run);