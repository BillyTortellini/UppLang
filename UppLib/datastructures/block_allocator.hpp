#pragma once

#include "../math/scalars.hpp"
#include "dynamic_array.hpp"
#include "array.hpp"

template<typename T>
union Block_Node
{
    T value;
    struct {
        Block_Node<T>* next;
        Block_Node<T>* prev;
    };
};

template<typename T>
struct Block_Allocator
{
    Dynamic_Array<Array<Block_Node<T>>> memory;
    Block_Node<T>* current_block;
    int used_block_count;
    int allocated_block_count;
};

template<typename T>
Block_Allocator<T> block_allocator_create_empty(int initial_capacity) 
{
    Block_Allocator<T> result;
    result.memory = dynamic_array_create_empty<Array<Block_Node<T>>>(1);
    dynamic_array_push_back(&result.memory, array_create_empty<Block_Node<T>>(math_maximum(initial_capacity, 0)));
    block_allocator_reset(&result);

    result.current_block = &result.memory[0][0];
    result.allocated_block_count = initial_capacity;
    result.used_block_count = 0;

    return result;
}

template<typename T>
void block_allocator_destroy(Block_Allocator<T>* allocator) {
    for (int i = 0; i < allocator->memory.size; i++) {
        array_destroy(&allocator->memory[i]);
    }
    dynamic_array_destroy(&allocator->memory);
}

template<typename T>
void block_allocator_reset(Block_Allocator<T>* allocator) 
{
    Block_Node<T>* prev = 0;
    for (int i = 0; i < allocator->memory.size; i++) 
    {
        Array<Block_Node<T>> mem_pool = allocator->memory[i];
        for (int j = 0; j < mem_pool.size; j++) 
        {
            Block_Node<T>* curr = &mem_pool[j];
            curr->prev = prev;
            curr->next = 0;
            if (prev != 0) {
                prev->next = curr;
            }
            prev = curr;
        }
    }
}

template<typename T>
T* block_allocator_allocate(Block_Allocator<T>* allocator) 
{
    allocator->used_block_count++;
    Block_Node<T>* curr = allocator->current_block;

    // Check if we need to allocate
    if (curr->next == 0)
    {
        Array<Block_Node<T>> new_memory = array_create_empty<Block_Node<T>>(allocator->allocated_block_count * 2);
        allocator->allocated_block_count = allocator->allocated_block_count * 3;
        dynamic_array_push_back(&allocator->memory, new_memory);
        Block_Node<T>* prev = allocator->current_block;
        for (int i = 0; i < new_memory.size; i++) {
            new_memory[i].prev = prev;
            new_memory[i].next = 0;
            prev->next = &new_memory[i];
            prev = &new_memory[i];
        }
    }
    if (curr->next == 0) {
        panic("Doesnt happen");
        return 0;
    }

    // Remove block
    curr->next->prev = curr->prev;
    allocator->current_block = curr->next;
    allocator->current_block->prev = 0;
    return &curr->value;
}

template<typename T>
void block_allocator_deallocate(Block_Allocator<T>* allocator, T* item)
{
    // Todo: Maybe check if we are actually inside allocator
    Block_Node<T>* returned_node = (Block_Node<T>*)item;
    Block_Node<T>* head = allocator->current_block;
    head->prev = returned_node;
    returned_node->next = head;
    returned_node->prev = 0;
    allocator->current_block = returned_node;
    allocator->used_block_count--;
}
