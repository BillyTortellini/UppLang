#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"

struct Type_Signature;
struct String;
struct Type_System;

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

Constant_Pool constant_pool_create(Type_System* type_system);
void constant_pool_destroy(Constant_Pool* pool);
Constant_Result constant_pool_add_constant(Constant_Pool* pool, Type_Signature* signature, Array<byte> bytes);
bool constant_pool_compare_constants(Constant_Pool* pool, Upp_Constant a, Upp_Constant b);
const char* constant_status_to_string(Constant_Status status);



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


