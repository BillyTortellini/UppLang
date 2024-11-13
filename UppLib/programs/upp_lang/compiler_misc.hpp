#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "../../datastructures/string.hpp"
#include "../../win32/process.hpp"

#include "lexer.hpp"

struct Datatype;
struct String;
struct Type_System;
struct Module_Progress;
struct ModTree_Function;
struct Datatype_Struct;

enum class Timing_Task
{
    LEXING,
    PARSING,
    ANALYSIS,
    CODE_GEN,
    RESET,
    CODE_EXEC,
    OUTPUT,
    FINISH,
};

const char* timing_task_to_string(Timing_Task task);

enum class Extern_Compiler_Setting
{
    LIBRARY,           // .lib filename
    LIBRARY_DIRECTORY, // Search path for lib files
    HEADER_FILE,       // Header file to include (should contain extern function + extern struct definitions)
    INCLUDE_DIRECTORY, // Directory for C-Compiler to search for header files
    SOURCE_FILE,       // .cpp file for compiler
    DEFINITION,        // Definitions to use before any header includes (e.g. #define _DEBUG)

    MAX_ENUM_VALUE
};

enum class Hardcoded_Type
{
    TYPE_OF,
    TYPE_INFO,
    ASSERT_FN,
    SIZE_OF,
    ALIGN_OF,
    PANIC_FN,

    MEMORY_COPY,
    MEMORY_ZERO,
    MEMORY_COMPARE,

    BITWISE_NOT,
    BITWISE_AND,
    BITWISE_OR,
    BITWISE_XOR,
    BITWISE_SHIFT_LEFT,
    BITWISE_SHIFT_RIGHT,

    MALLOC_SIZE_U64,
    FREE_POINTER,
    REALLOCATE,

    PRINT_I32,
    PRINT_F32,
    PRINT_BOOL,
    PRINT_LINE,
    PRINT_STRING,
    READ_I32,
    READ_F32,
    READ_BOOL,
    RANDOM_I32,
};

void hardcoded_type_append_to_string(String* string, Hardcoded_Type hardcoded);

enum class Exit_Code_Type
{
    SUCCESS,
    COMPILATION_FAILED, // Code did not because there were compile errors
    CODE_ERROR,
    EXECUTION_ERROR, // Stack overflow, return value overflow, instruction limit reached
    INSTRUCTION_LIMIT_REACHED,
    TYPE_INFO_WAITING_FOR_TYPE_FINISHED,

    MAX_ENUM_VALUE
};
const char* exit_code_type_as_string(Exit_Code_Type type);

struct Exit_Code
{
    Exit_Code_Type type;
    const char* error_msg; // May be null
};

Exit_Code exit_code_make(Exit_Code_Type type, const char* error_msg = 0);
void exit_code_append_to_string(String* string, Exit_Code code);


// Code Source
struct Source_Code;
namespace AST {
    struct Module;
}

// Extern Sources
struct Extern_Sources
{
    Dynamic_Array<ModTree_Function*> extern_functions;
    Dynamic_Array<String*> compiler_settings[(int)Extern_Compiler_Setting::MAX_ENUM_VALUE];
};

Extern_Sources extern_sources_create();
void extern_sources_destroy(Extern_Sources* sources);



// Identifier Pool
struct Identifier_Pool
{
    Hashtable<String, String*> identifier_lookup_table;
};

Identifier_Pool identifier_pool_create();
void identifier_pool_destroy(Identifier_Pool* pool);
String* identifier_pool_add(Identifier_Pool* pool, String identifier);
void identifier_pool_print(Identifier_Pool* pool);



// Fiber Pool
struct Fiber_Pool;
struct Fiber_Pool_Handle { // Handle to a fiber from a fiber pool
    Fiber_Pool* pool;
    int pool_index;
}; 

Fiber_Pool* fiber_pool_create();
void fiber_pool_destroy(Fiber_Pool* pool);
Fiber_Pool_Handle fiber_pool_get_handle(Fiber_Pool* pool, fiber_entry_fn entry_fn, void* userdata);
bool fiber_pool_switch_to_handel(Fiber_Pool_Handle handle); // Returns true if fiber finished, or if fiber waits for more stuff to happen
void fiber_pool_switch_to_main_fiber(Fiber_Pool* pool);
void fiber_pool_check_all_handles_completed(Fiber_Pool* pool);
void fiber_pool_test(); // Just tests the fiber pool if everything works correctly
