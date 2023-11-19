#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"
#include "../../win32/process.hpp"

#include "lexer.hpp"

struct Type_Signature;
struct String;
struct Type_System;
struct Module_Progress;

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

enum class Hardcoded_Type
{
    TYPE_OF,
    TYPE_INFO,
    ASSERT_FN,

    MALLOC_SIZE_I32,
    FREE_POINTER,

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

enum class Exit_Code
{
    SUCCESS,
    OUT_OF_BOUNDS,
    STACK_OVERFLOW,
    RETURN_VALUE_OVERFLOW,
    EXTERN_FUNCTION_CALL_NOT_IMPLEMENTED,
    ASSERTION_FAILED,
    COMPILATION_FAILED,
    INSTRUCTION_LIMIT_REACHED,
    CODE_ERROR_OCCURED,
    ANY_CAST_INVALID,
    INVALID_SWITCH_CASE,
};

bool exit_code_is_valid(int value);
void exit_code_append_to_string(String* string, Exit_Code code);


// Code Source
struct Source_Code;
namespace AST {
    struct Module;
}
namespace Parser {
    struct Parsed_Code;
}

enum class Code_Origin
{
    MAIN_PROJECT,
    LOADED_FILE,
};

struct Code_Source
{
    Code_Origin origin;
    String file_path;

    Source_Code* code;
    Parser::Parsed_Code* parsed_code;
    Module_Progress* module_progress; // Analysis progress, may be 0 if not analysed yet
};



// Constant Pool
struct Upp_Constant
{
    Type_Signature* type;
    int offset;
    int constant_index;
};

struct Upp_Constant_Reference
{
    // Where the pointer is stored in the buffer, e.g. *(void**)&pool.buffer[ptr_offset] = &pool.buffer[buffer_destination_offset];
    int ptr_offset;
    int buffer_destination_offset; 
};

struct Constant_Pool_Result
{
    bool success;
    const char* error_message;
    Upp_Constant constant;
};

struct Constant_Pool;
struct Constant_Deduplication {
    Constant_Pool* pool;
    int data_size_in_byte;
    bool is_pool_data;
    int offset;
    void* data;
};

struct Constant_Pool
{
    Type_System* type_system;
    Dynamic_Array<Upp_Constant> constants;
    Dynamic_Array<Upp_Constant_Reference> references;
    Dynamic_Array<byte> buffer;
    Hashtable<void*, int> saved_pointers;
    Hashtable<Constant_Deduplication, int> deduplication_table;
    int max_buffer_size;
};

Constant_Pool constant_pool_create(Type_System* type_system);
void constant_pool_destroy(Constant_Pool* pool);
Constant_Pool_Result constant_pool_add_constant(Constant_Pool* pool, Type_Signature* signature, Array<byte> bytes);
bool constant_pool_compare_constants(Constant_Pool* pool, Upp_Constant a, Upp_Constant b);
void* upp_constant_get_pointer(Constant_Pool* pool, Upp_Constant constant);
template<typename T>
T upp_constant_to_value(Constant_Pool* pool, Upp_Constant constant)
{
    assert(constant.type->size == sizeof(T), "");
    return *((T*)&pool->buffer[constant.offset]);
}



// Extern Sources
struct Extern_Function_Identifier
{
    Type_Signature* function_signature;
    String* id;
};

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



