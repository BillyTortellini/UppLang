#include "stack_allocator.hpp"

#include "../math/scalars.hpp"

Stack_Allocator stack_allocator_create_empty(int byte_size)
{
    Stack_Allocator result;
    result.memory = dynamic_array_create<Array<byte>>(1);
    Array<byte> pool = array_create<byte>(math_maximum(32, byte_size));
    dynamic_array_push_back(&result.memory, pool);
    result.current_pool_index = 0;
    result.stack_pointer = 0;
    return result;
}

void stack_allocator_destroy(Stack_Allocator* allocator)
{
    for (int i = 0; i < allocator->memory.size; i++) {
        array_destroy(&allocator->memory[i]);
    }
    dynamic_array_destroy(&allocator->memory);
}

void* stack_allocator_allocate_size(Stack_Allocator* allocator, size_t size, size_t alignment)
{
    size_t curr_size = allocator->memory[allocator->current_pool_index].size;
    size_t next_aligned = math_round_next_multiple(allocator->stack_pointer, alignment);
    size_t end = next_aligned + size;
    while (end >= curr_size) 
    {
        if (allocator->current_pool_index < allocator->memory.size - 1) 
        {
            allocator->current_pool_index++;
            curr_size = allocator->memory[allocator->current_pool_index].size;
        }
        else 
        {
            Array<byte> new_pool = array_create<byte>(curr_size * 2);
            dynamic_array_push_back(&allocator->memory, new_pool);
            allocator->current_pool_index = allocator->memory.size - 1;
            curr_size = allocator->memory[allocator->current_pool_index].size;
        }
        next_aligned = 0;
        end = size;
    }
    allocator->stack_pointer = next_aligned;
    void* data = &allocator->memory[allocator->current_pool_index][allocator->stack_pointer];
    memory_set_bytes(data, size, 0);
    allocator->stack_pointer = end;
    return data;
}

bool stack_allocator_contains_address_range(Stack_Allocator* allocator, void* address, int byte_count) {
    for (int i = allocator->memory.size - 1; i >= 0;i--) { // Note: Travers memories in reverse order, because there is a higher chance to find the address in larger regions
        auto& memory = allocator->memory[i];
        if ((void*)memory.data <= address && (void*)(memory.data + memory.size) >= (void*)(((byte*)address) + byte_count)) {
            return true;
        }
    }
    return false;
}

void stack_allocator_reset(Stack_Allocator* allocator) {
    allocator->current_pool_index = 0;
    allocator->stack_pointer = 0;
}

Stack_Checkpoint stack_checkpoint_make(Stack_Allocator* allocator)
{
    Stack_Checkpoint result;
    result.allocator = allocator;
    result.current_pool_index = allocator->current_pool_index;
    result.stack_pointer = allocator->stack_pointer;
    return result;
}

void stack_checkpoint_rewind(Stack_Checkpoint checkpoint) {
    checkpoint.allocator->current_pool_index = checkpoint.current_pool_index;
    checkpoint.allocator->stack_pointer = checkpoint.stack_pointer;
}
