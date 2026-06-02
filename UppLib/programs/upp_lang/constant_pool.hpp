#pragma once

#include "../../utility/datatypes.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/allocators.hpp"
#include "../../datastructures/hashtable.hpp"
#include "compiler_misc.hpp"

struct Datatype;
struct Compilation_Data;
struct Datatype_Enum;

struct Upp_Constant
{
    Datatype* type;
    byte* memory; // memory in arena of corresponding constant pool
    int constant_index;
};

struct Constant_Pool_Result
{
    bool success;
    union {
        const char* error_message;
        Upp_Constant constant;
    } options;
};

// Constants are deduplicated based on the type and on the _shallow_ memory
struct Deduplication_Info
{
    Datatype* type;
    Array<byte> memory;
};

struct Predefined_Constants
{
    Upp_Constant nil;
    Upp_Constant bool_true;
    Upp_Constant bool_false;
    Upp_Constant usize_zero;
    Upp_Constant usize_one;
    Upp_Constant i32_zero;
    Upp_Constant i32_one;
    Upp_Constant u32_zero;
    Upp_Constant u32_one;
    Upp_Constant empty_string;
};

struct Constant_Pool
{
    Compilation_Data* compilation_data;
    Dynamic_Array<Upp_Constant> constants;
    Arena constant_memory;
    Hashtable<Deduplication_Info, Upp_Constant> deduplication_table; 
    Predefined_Constants predefined;

    Upp_Constant add_i32(i32 value);
    Upp_Constant add_u32(u32 value);
    Upp_Constant add_usize(usize value);
    Upp_Constant add_f32(f32 value);
    Upp_Constant add_f64(f64 value);
    Upp_Constant add_upp_string_assume_valid(Upp_String string);
    Upp_Constant add_string_assume_valid(String string);
    Upp_Constant add_bool(bool value);
    Upp_Constant add_enum_value_assume_valid(Datatype_Enum* enum_type, int value);
    Upp_Constant add_type_handle_assume_valid(Upp_Type_Handle type_handle);
};

Constant_Pool* constant_pool_create(Compilation_Data* compilation_data);
void constant_pool_destroy(Constant_Pool* pool);
Constant_Pool_Result constant_pool_add_constant(Constant_Pool* constant_pool, Datatype* signature, Array<byte> bytes);



bool upp_constant_is_equal(Upp_Constant a, Upp_Constant b);
template<typename T>
T upp_constant_to_value(Upp_Constant constant) {
    assert(constant.type->memory_info.value.size == sizeof(T), "");
    return *(T*)constant.memory;
}



