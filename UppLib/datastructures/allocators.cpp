#include "allocators.hpp"

#include <stdlib.h> // Alligned alloc

#include "Windows.h"
#include "../math/scalars.hpp"



// Allocator_Base
void* Allocator_Base::allocate_raw(uint size, uint alignment) {
	return this->allocate_fn(this, Allocation_Type::ALLOCATE, size, alignment, nullptr);
}

bool Allocator_Base::resize(void* memory, uint old_size, uint new_size) {
	return this->allocate_fn(this, Allocation_Type::RESIZE, old_size, new_size, memory) != nullptr;
}

void Allocator_Base::deallocate(void* memory, uint allocation_size) {
	this->allocate_fn(this, Allocation_Type::DEALLOCATE, allocation_size, allocation_size, memory);
}



// System_Allocator
static void* system_allocator_allocate_function(
	Allocator_Base* base, Allocation_Type allocation_type, uint new_size, uint alignment_or_old_size, void* current_data)
{
	switch (allocation_type)
	{
	case Allocation_Type::ALLOCATE: 
	{
		if (new_size == 0) {
			return nullptr;
		}
		// Note: alignment may not hold...
		return malloc(new_size);
	}
	case Allocation_Type::RESIZE: 
	{
		// Note: Resize is not the same as reallocate, so we dont do realloc() here
		return nullptr;
	}
	case Allocation_Type::DEALLOCATE: 
	{
		if (alignment_or_old_size == 0) {
			return nullptr;
		}
		free(current_data);
		return nullptr;
	}
	default: panic("");
	}
	return nullptr;
}

System_Allocator System_Allocator::create()
{
	System_Allocator result;
	result.base.allocate_fn = system_allocator_allocate_function;
	return result;
}

Allocator_Base* System_Allocator::upcast() {
	return &this->base;
}

// Note: This doesn't need to be thread_local, as new/delete is internally synchronized
System_Allocator global_system_allocator = System_Allocator::create();



// Arena
static void* arena_allocate_function(
	Allocator_Base* base, Allocation_Type allocation_type, uint new_size, uint alignment_or_old_size, void* current_data)
{
	Arena* arena = (Arena*)base;

	switch (allocation_type)
	{
	case Allocation_Type::ALLOCATE: 
	{
		return arena->allocate_raw(new_size, alignment_or_old_size);
	}
	case Allocation_Type::RESIZE: 
	{
		return arena->resize(current_data, alignment_or_old_size, new_size) ? current_data : nullptr;
	}
	case Allocation_Type::DEALLOCATE: 
	{
		// Arenas don't deallocate, altough we could pop the last allocation if we wanted to,
		// but this isn't as usefull as one might think because of alignment
		// Although one could store the last aligment and fix this, it's not a priority we have
		return nullptr;
	}
	default: panic("");
	}
	return nullptr;
}

// Makes sure that the current buffer has a capacity of at least new_capacity.
// if not, a new buffer is allocated
// returns true if a new buffer was allocated
static bool arena_reserve_buffer_capacity(Arena* arena, uint new_capacity)
{
	if (new_capacity <= arena->buffer.capacity) return false;

	// Try resizing the current allocation
	if (arena->buffer.data != nullptr) 
	{
		if (arena->parent_allocator->resize(arena->buffer.data, arena->buffer.capacity, new_capacity)) 
		{
			arena->buffer.capacity = new_capacity;
			return false;
		}
	}

	// Figure out new capacity (Power of 2)
	new_capacity = math_maximum(128ull, integer_next_power_of_2(new_capacity));

	// Allocate new buffer
	Arena_Buffer new_buffer;
	new_buffer.data = arena->parent_allocator->allocate_raw(new_capacity, alignof(Arena_Buffer));
	new_buffer.capacity = new_capacity;

	// Store linked list to old buffers
	Arena_Buffer* header = (Arena_Buffer*)new_buffer.data;
	*header = arena->buffer;

	// Store new buffer in arena
	arena->buffer = new_buffer;
	arena->next = (void*) (((uint)new_buffer.data) + sizeof(Arena_Buffer));

	return true;
}

Arena Arena::create(uint capacity, Allocator_Base* parent_allocator)
{
	Arena result;
	result.base.allocate_fn = arena_allocate_function;
	result.parent_allocator = parent_allocator == nullptr ? &global_system_allocator.base : parent_allocator;
	result.buffer.data = nullptr;
	result.buffer.capacity = 0;
	result.next = nullptr;
	arena_reserve_buffer_capacity(&result, capacity);
	return result;
}

void Arena::destroy() {
	reset(false);
}

void* Arena::allocate_raw(uint size, u32 alignment)
{
	assert(size > 0 && alignment > 0, "");
	uint result_address = math_round_next_multiple((uint)next, (uint)alignment);
	bool resized = arena_reserve_buffer_capacity(this, result_address - (uint)buffer.data + size + sizeof(Arena_Buffer));
	if (resized) {
		result_address = (uint)next;
	}
	next = (void*)(result_address + size);
	assert(result_address + size <= (uint)buffer.data + buffer.capacity, "Otherwise we shoot out of our buffer!");
	return (void*)result_address;
}

bool Arena::resize(void* memory, uint old_size, uint new_size)
{
	assert(memory != nullptr, "");
	uint address = (uint)memory;
	// Check if address was last allocation
	if (address + old_size != (uint)next) return false;
	assert(address > (uint)buffer.data || address == 0, "Resize not from this buffer!"); // With last check + header inside buffers this should be true

	// Check if we have enough space for resize
	if (address + new_size <= (uint)buffer.data + buffer.capacity) {
		next = (void*)(address + new_size);
		return true;
	}

	return false;
}

void Arena::reset(bool keep_largest_buffer)
{
	Arena_Buffer curr = buffer;
	if (curr.data == 0) return;

	if (keep_largest_buffer) {
		curr = *(Arena_Buffer*)curr.data; // Skip deallocation of current
		((Arena_Buffer*)buffer.data)->data = nullptr;
		((Arena_Buffer*)buffer.data)->capacity = 0;
	}
	else {
		buffer.data = nullptr;
		buffer.capacity = 0;
	}

	while (curr.data != nullptr)
	{
		Arena_Buffer next = *(Arena_Buffer*)curr.data;
		this->parent_allocator->deallocate(curr.data, curr.capacity);
		curr = next;
	}
}

Arena_Checkpoint Arena::make_checkpoint()
{
	Arena_Checkpoint checkpoint;
	checkpoint.data = next;
	checkpoint.arena = this;
	return checkpoint;
}

// Note: This never deallocates memory
void Arena::rewind_to_address(void* pointer)
{
	uint address = (uint)pointer;
	uint buffer_start = ((uint)buffer.data + sizeof(Arena_Buffer));
	if (address >= buffer_start && address <= buffer_start + buffer.capacity) {
		next = pointer;
	}
	else {
		next = (void*)buffer_start;
	}
}

void Arena::rewind_to_checkpoint(Arena_Checkpoint checkpoint) {
	checkpoint.arena->rewind_to_address(checkpoint.data);
}

void Arena_Checkpoint::rewind() {
	arena->rewind_to_checkpoint(*this);
}

Allocator_Base* Arena::upcast() {
	return &this->base;
}



// Scratch_Arena
static thread_local Scratch_Arena_Pair scratch_arenas;

Scratch_Arena_Pair scratch_arena_pair_make(Arena* main_arena, Arena* fallback_arena) {
	Scratch_Arena_Pair pair;
	pair.main_arena = main_arena;
	pair.fallback_arena = fallback_arena;
	return pair;
}

void scratch_arena_set_arenas(Scratch_Arena_Pair new_scratch_arenas) {
	scratch_arenas = new_scratch_arenas;
}

Scratch_Arena_Pair scratch_arena_get_arenas_in_use() {
	return scratch_arenas;
}

Arena* scratch_arena_retrieve(Arena* arena_to_avoid) {
	return scratch_arenas.main_arena == arena_to_avoid ? scratch_arenas.fallback_arena : scratch_arenas.main_arena;
}



// FREE LIST
void* free_list_allocate_function(
	Allocator_Base* base, Allocation_Type allocation_type, uint new_size, uint alignment_or_old_size, void* current_data)
{
	Free_List* free_list = (Free_List*)base;
	switch (allocation_type)
	{
	case Allocation_Type::ALLOCATE: 
	{
		return free_list->allocate_raw(new_size, alignment_or_old_size);
	}
	case Allocation_Type::RESIZE: 
	{
		// Free_List has no resize, as sizes are fixed...
		return nullptr;
	}
	case Allocation_Type::DEALLOCATE: 
	{
		free_list->deallocate_raw(current_data);
		return nullptr;
	}
	default: panic("");
	}
	return nullptr;
}

Free_List Free_List::create_with_info(Allocator_Base* parent_allocator, uint element_size, uint element_alignment)
{
	Free_List result;
	result.base.allocate_fn = free_list_allocate_function;
	result.parent_allocator = parent_allocator;
	result.element_alignment = math_maximum(alignof(void*), element_alignment);
	result.element_size = math_maximum(sizeof(void*), element_size);
	result.next = nullptr;
	return result;
}

void* Free_List::allocate_raw(uint size, uint alignment)
{
	assert(size == this->element_size || alignment == this->element_alignment, "");
	if (next != nullptr) {
		void* result = next;
		next = *(void**)next;
		return result;
	}
	return parent_allocator->allocate_raw(size, alignment);
}

void Free_List::deallocate_raw(void* data)
{
	*(void**)data = next; // Store list of free allocations
	next = data;
}

Allocator_Base* Free_List::upcast() {
	return &this->base;
}



// Contains prime values and values inbetween.
// Because we resize with a factor of 1.5, the
static u64 prime_values_power_2_and_between[] = {
	1, 2,   // primes >= 2^0 = 1
	2, 3,   // primes >= 2^1 = 2
	5, 7,   // primes >= 2^2 = 4
	11, 13, // primes >= 2^3 = 8
	17, 29, // primes >= 2^4 = 16
	37, 53, // primes >= 2^5 = 32
	67, 97, // ...
	131, 193,
	257, 389,
	521, 769,
	1031, 1543,
	2053, 3079,
	4099, 6151,
	8209, 12289,
	16411, 24593,
	32771, 49157,
	65537, 98317,
	131101, 196613,
	262147, 393241,
	524309, 786433,
	1048583, 1572869,
	2097169, 3145739,
	4194319, 6291469,
	8388617, 12582917,
	16777259, 25165843,
	33554467, 50331653,
	67108879, 100663319,
	134217757, 201326611,
	268435459, 402653189,
	536870923, 805306457,
	1073741827, 1610612741,
};

u64 find_next_suitable_prime_hashset_size(u64 value)
{
	if (value <= 1) return 1;
	u8 set_bit = integer_highest_set_bit_index(value);
	int search_index = set_bit * 2;
	while (true)
	{
		u64 candidate = prime_values_power_2_and_between[search_index];
		if (candidate >= value) return candidate;
		search_index += 1;
	}
	panic("should not happen");
	return 0;
}

