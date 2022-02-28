#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/string.hpp"

struct Type_Signature;
struct String;

struct Upp_Constant
{
    Type_Signature* type;
    int offset;
    int constant_index;
};

struct Token_Range
{
    int start_index;
    int end_index;
};

enum class Hardcoded_Function_Type
{
    PRINT_I32,
    PRINT_F32,
    PRINT_BOOL,
    PRINT_LINE,
    PRINT_STRING,
    READ_I32,
    READ_F32,
    READ_BOOL,
    RANDOM_I32,
    MALLOC_SIZE_I32,
    FREE_POINTER,

    HARDCODED_FUNCTION_COUNT, // Should always be last element
};

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

struct Extern_Function_Identifier
{
    Type_Signature* function_signature;
    String* id;
};

