#pragma once

#include "../../win32/timing.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"
#include "type_system.hpp"
#include "compiler_misc.hpp"

struct Lexer;
struct Compiler;
struct AST_Parser;
struct Semantic_Analyser;
struct Intermediate_Generator;
struct Bytecode_Generator;
struct Bytecode_Interpreter;
struct C_Generator;
struct C_Compiler;
struct C_Importer;
struct IR_Generator;
struct Type_Signature;
struct Dependency_Analyser;
namespace AST
{
    struct Base;
    struct Module;
}

struct Upp_Constant_Reference
{
    // Where the pointer is stored in the buffer, e.g. *(void**)&pool.buffer[ptr_offset] = &pool.buffer[buffer_destination_offset];
    int ptr_offset;
    int buffer_destination_offset; 
};

enum class Constant_Status
{
    SUCCESS,
    CONTAINS_VOID_TYPE,
    CONTAINS_INVALID_POINTER_NOT_NULL,
    CANNOT_SAVE_FUNCTIONS_YET,
    CANNOT_SAVE_C_UNIONS_CONTAINING_REFERENCES,
    CONTAINS_INVALID_UNION_TAG,
    OUT_OF_MEMORY,
    INVALID_SLICE_SIZE
};
const char* constant_status_to_string(Constant_Status status);

struct Constant_Result
{
    Constant_Status status;
    Upp_Constant constant;
};

struct Constant_Pool
{
    Type_System* type_system;
    Dynamic_Array<Upp_Constant> constants;
    Dynamic_Array<Upp_Constant_Reference> references;
    Dynamic_Array<byte> buffer;
    Hashtable<void*, int> saved_pointers;
    int max_buffer_size;
};
Constant_Result constant_pool_add_constant(Constant_Pool* pool, Type_Signature* signature, Array<byte> bytes);
bool constant_pool_compare_constants(Constant_Pool* pool, Upp_Constant a, Upp_Constant b);

struct Extern_Sources
{
    /*
    TODO:
        - DLLs
        - LIBs
    */
    Dynamic_Array<String*> headers_to_include;
    Dynamic_Array<String*> source_files_to_compile;
    Dynamic_Array<String*> lib_files;
    Dynamic_Array<Extern_Function_Identifier> extern_functions;
    Hashtable<Type_Signature*, String*> extern_type_signatures; // Extern types to name id, e.g. HWND should not create its own structure, but use name HWND as type
};

struct Identifier_Pool
{
    Hashtable<String, String*> identifier_lookup_table;
};
Identifier_Pool identifier_pool_create();
void identifier_pool_destroy(Identifier_Pool* pool);
String* identifier_pool_add(Identifier_Pool* pool, String identifier);
void identifier_pool_print(Identifier_Pool* pool);

Token_Range token_range_make(int start_index, int end_index);

struct Compiler_Error
{
    const char* message;
    Token_Range range;
};



#include "lexer.hpp"

enum class Code_Origin_Type
{
    MAIN_PROJECT,
    LOADED_FILE,
    GENERATED
};

struct Code_Origin
{
    Code_Origin_Type type;
    String* id_filename; // May be null, otherwise pointer to string in identifier_pool
    AST::Base* load_node;
};

struct Code_Source
{
    Code_Origin origin;
    AST::Module* source;
};

enum class Timing_Task
{
    LEXING,
    PARSING,
    RC_GEN,
    ANALYSIS,
    CODE_GEN,
    RESET,
    CODE_EXEC,
    OUTPUT,
    FINISH,
};

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
    Lexer* lexer;
    AST_Parser* parser;
    Dependency_Analyser* rc_analyser;
    Semantic_Analyser* semantic_analyser;
    IR_Generator* ir_generator;
    Bytecode_Generator* bytecode_generator;
    Bytecode_Interpreter* bytecode_interpreter;
    C_Generator* c_generator;
    C_Compiler* c_compiler;
    C_Importer* c_importer;

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
};

Compiler compiler_create(Timer* timer);
void compiler_destroy(Compiler* compiler);

void compiler_compile(Compiler* compiler, AST::Module* source_code, bool generate_code);
Exit_Code compiler_execute(Compiler* compiler);
void compiler_add_source_code(Compiler* compiler, AST::Module* source_code, Code_Origin origin); // Takes ownership of source_code
bool compiler_errors_occured(Compiler* compiler);
void compiler_switch_timing_task(Compiler* compiler, Timing_Task task);
void compiler_run_testcases(Timer* timer);

Text_Slice token_range_to_text_slice(Token_Range range, Compiler* compiler);
void exit_code_append_to_string(String* string, Exit_Code code);
void hardcoded_function_type_append_to_string(String* string, Hardcoded_Function_Type hardcoded);