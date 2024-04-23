#pragma once

#include "../../utility/datatypes.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "../../datastructures/hashtable.hpp"

struct Datatype;
struct ModTree_Function;

struct Constant_Handle
{
    int index;
};

struct Upp_Constant
{
    Datatype* type;
    byte* memory;
    int constant_index;
    int array_size; // Signifies if this is an array or a single element
};

struct Upp_Constant_Reference
{
    Upp_Constant constant;
    int pointer_member_byte_offset;
    Upp_Constant points_to; 
};

struct Upp_Constant_Function_Reference
{
    Upp_Constant constant;
    int offset_from_constant_start;
    ModTree_Function* points_to; // May not be null? otherwise we just wouldn't record it I guess
};

struct Constant_Pool_Result
{
    bool success;
    const char* error_message;
    Upp_Constant constant;
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
    Dynamic_Array<Upp_Constant_Reference> references; // Required for serialization
    Dynamic_Array<Upp_Constant_Function_Reference> function_references; // Required for serialization
    Stack_Allocator constant_memory;
    Hashtable<void*, Upp_Constant> saved_pointers;
    Hashtable<Deduplication_Info, Upp_Constant> deduplication_table; 
    
    // Statistics tracking
    int deepcopy_counts;
    int added_internal_constants;
    int duplication_checks;
    double time_contains_reference;
    double time_in_comparison;
    double time_in_hash;
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



