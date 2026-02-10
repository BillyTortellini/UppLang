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
struct Intermediate_Generator;
struct Bytecode_Generator;
struct Bytecode_Thread;
struct C_Generator;
struct C_Compiler;
struct IR_Generator;
struct Source_Code;
struct Code_History;
struct Compilation_Data;
struct Upp_Module;



namespace AST
{
    struct Node;
    struct Module;
}

struct Compilation_Unit
{
    String filepath; // For deduplication, full-filepath, owned by this structure

    // All data may be nullptr until loaded...
    Source_Code* code;
    AST::Module* root;
    Upp_Module* module;

    Dynamic_Array<AST::Node*> allocated_nodes;
    Dynamic_Array<Error_Message> parser_errors;
};

// Compiler
struct Compiler
{
    // Permanent data (Stays across compiles)
    Identifier_Pool identifier_pool;
    Fiber_Pool* fiber_pool;
};

Compiler* compiler_create();
void compiler_destroy(Compiler* compiler);

// Expects file_path to be a full path (For deduplication)
// If source_code is null, and we haven't loaded the file previously, the file is loaded
// Returns 0 if file does not exist
void compilation_unit_destroy(Compilation_Unit* unit);

void compilation_data_compile(Compilation_Data* compilation_data, Compilation_Unit* main_unit, Compile_Type compile_type);
Compilation_Unit* compiler_import_file(Compilation_Data* compilation_data, AST::Import* import_node); // Returns 0 if file could not be read
bool compiler_can_execute_c_compiled(Compilation_Data* compilation_data);
Exit_Code compiler_execute(Compilation_Data* compilation_data);

bool compilation_data_errors_occured(Compilation_Data* compilation_data);
Compilation_Unit* compiler_find_ast_compilation_unit(Compilation_Data* compilation_data, AST::Node* base);
void compilation_data_switch_timing_task(Compilation_Data* compilation_data, Timing_Task task);
Exit_Code compiler_execute(Compilation_Data* compilation_data);
void compiler_run_testcases(bool force_run);
void compiler_parse_unit(Compilation_Unit* unit, Compilation_Data* compilation_data);