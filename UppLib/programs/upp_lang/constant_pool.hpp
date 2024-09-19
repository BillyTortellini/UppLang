#pragma once

#include "../../utility/datatypes.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "../../datastructures/hashtable.hpp"

struct Datatype;

struct Upp_Constant
{
    Datatype* type;
    byte* memory; // memory in stack-allocator of constant_pool
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

struct Constant_Pool
{
    Dynamic_Array<Upp_Constant> constants;
    Stack_Allocator constant_memory;
    Hashtable<Deduplication_Info, Upp_Constant> deduplication_table; 
};

Constant_Pool constant_pool_create();
void constant_pool_destroy(Constant_Pool* pool);
Constant_Pool_Result constant_pool_add_constant(Datatype* signature, Array<byte> bytes);

bool upp_constant_is_equal(Upp_Constant a, Upp_Constant b);
template<typename T>
T upp_constant_to_value(Upp_Constant constant) {
    assert(constant.type->memory_info.value.size == sizeof(T), "");
    return *(T*)constant.memory;
}



