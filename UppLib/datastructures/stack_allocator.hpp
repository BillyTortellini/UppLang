#pragma once

#include "dynamic_array.hpp"
#include "array.hpp"

struct Stack_Allocator
{
    Dynamic_Array<Array<byte>> memory;
    int current_pool_index;
    size_t stack_pointer;
};

Stack_Allocator stack_allocator_create_empty(int byte_size);
void stack_allocator_destroy(Stack_Allocator* allocator);

void* stack_allocator_allocate_size(Stack_Allocator* allocator, size_t size, size_t alignment);
void stack_allocator_reset(Stack_Allocator* allocator);
bool stack_allocator_contains_address_range(Stack_Allocator* allocator, void* address, int byte_count);

template<typename T>
T* stack_allocator_allocate(Stack_Allocator* allocator) {
    return (T*)stack_allocator_allocate_size(allocator, sizeof(T), alignof(T));
}



struct Stack_Checkpoint {
    Stack_Allocator* allocator;
    int current_pool_index;
    size_t stack_pointer;
};

Stack_Checkpoint stack_checkpoint_make(Stack_Allocator* allocator);
void stack_checkpoint_rewind(Stack_Checkpoint checkpoint);



