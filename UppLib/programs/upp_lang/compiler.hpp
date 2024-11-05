#pragma once

#include "../../win32/timing.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"
#include "../../utility/random.hpp"
#include "type_system.hpp"
#include "compiler_misc.hpp"
#include "constant_pool.hpp"

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

struct Predefined_IDs
{
    // Other
    String* main;
    String* id_struct;
    String* empty_string;
    String* invalid_symbol_name;
    String* cast_mode;
    String* cast_mode_none;
    String* cast_mode_explicit;
    String* cast_mode_inferred;
    String* cast_mode_implicit;
    String* cast_mode_pointer_explicit;
    String* cast_mode_pointer_inferred;
    String* byte;
    String* value;
    String* is_available;
    String* uninitialized_token; // _

    String* lambda_function;
    String* bake_function;

    String* function;
    String* create_fn;
    String* next_fn;
    String* has_next_fn;
    String* value_fn;
    String* name;
    String* as_member_access;
    String* commutative;
    String* binop;
    String* unop;
    String* global;
    String* option;
    String* lib;
    String* lib_dir;
    String* source;
    String* header;
    String* header_dir;
    String* definition;

    // Hardcoded functions
    String* type_of;
    String* type_info;

    // Members
    String* data;
    String* size;
    String* tag;
    String* anon_struct;
    String* anon_enum;
    String* string;
    String* allocator;
    String* bytes;

    // Context members 
    String* id_import;
    String* set_option;
    String* set_cast_option;
    String* add_binop;
    String* add_unop;
    String* add_cast;
    String* add_array_access;
    String* add_dot_call;
    String* add_iterator;

    String* cast_option;
    String* cast_option_enum_values[(int)Cast_Option::MAX_ENUM_VALUE];
};

// Compiler
struct Compiler
{
    // Compiler internals
    Dynamic_Array<Source_Code*> program_sources;
    Source_Code* main_source;
    Source_Code* last_main_source;
    bool last_compile_generated_code;
    bool generate_code; // This indicates if we want to compile (E.g. user pressed CTRL-B or F5)

    // Helpers
    Identifier_Pool identifier_pool;
    Predefined_IDs predefined_ids;
    Fiber_Pool* fiber_pool;

    Constant_Pool constant_pool;
    Type_System type_system;
    Extern_Sources extern_sources;
    Random random;

    // Stages
    Semantic_Analyser* semantic_analyser;
    IR_Generator* ir_generator;
    Bytecode_Generator* bytecode_generator;
    C_Generator* c_generator;
    C_Compiler* c_compiler;

    // Timing stuff
    Timer* timer;
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

Compiler* compiler_initialize(Timer* timer);
void compiler_destroy();

// Returns 0 if file does not exist
Source_Code* compiler_add_source(String file_path, bool opened_in_editor, bool imported);

void compiler_compile_clean(Source_Code* main_source, Compile_Type compile_type);
Module_Progress* compiler_import_and_queue_analysis_workload(AST::Import* import_node); // Returns 0 if file couldn't be read
Exit_Code compiler_execute();

bool compiler_errors_occured();
Source_Code* compiler_find_ast_source_code(AST::Node* base);
void compiler_switch_timing_task(Timing_Task task);
void compiler_run_testcases(Timer* timer, bool force_run);