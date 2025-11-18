#include "allocators.hpp"

#include "Windows.h"
#include "../math/scalars.hpp"


// Makes sure that the current buffer has a capacity of at least new_capacity.
// if not, a new buffer is allocated
// returns true if a new buffer was allocated
static bool arena_reserve_buffer_capacity(Arena* arena, usize new_capacity)
{
	if (new_capacity <= arena->buffer.capacity) return false;
	new_capacity = math_maximum(128ull, integer_next_power_of_2(new_capacity));

	// Allocate new buffer
	Arena_Buffer new_buffer;
	new_buffer.data = malloc(new_capacity);
	new_buffer.capacity = new_capacity;

	// Store linked list to old buffers
	Arena_Buffer* header = (Arena_Buffer*)new_buffer.data;
	*header = arena->buffer;

	// Store new buffer in arena
	arena->buffer = new_buffer;
	arena->next = (void*) ((usize)new_buffer.data + sizeof(Arena_Buffer));

	return true;
}

Arena Arena::create(usize capacity)
{
	Arena result;
	result.buffer.data = nullptr;
	result.buffer.capacity = 0;
	result.next = nullptr;
	arena_reserve_buffer_capacity(&result, capacity);
	return result;
}

void Arena::destroy() {
	reset(false);
}

void* Arena::allocate_raw(usize size, u32 alignment)
{
	assert(size != 0 && alignment != 0, "");
	usize result_address = math_round_next_multiple((usize)next, (usize)alignment);
	bool resized = arena_reserve_buffer_capacity(this, result_address - (usize)buffer.data + size + sizeof(Arena_Buffer));
	if (resized) {
		result_address = (usize)next;
	}
	next = (void*)(result_address + size);
	assert(result_address + size <= (usize)buffer.data + buffer.capacity, "Otherwise we shoot out of our buffer!");
	return (void*)result_address;
}

bool Arena::resize(void* memory, usize old_size, usize new_size)
{
	assert(memory != nullptr, "");
	usize address = (usize)memory;
	// Check if address was last allocation
	if (address + old_size != (usize)next) return false;
	assert(address > (usize)buffer.data || address == 0, "Resize not from this buffer!"); // With last check + header inside buffers this should be true

	// Check if we have enough space for resize
	if (address + new_size <= (usize)buffer.data + buffer.capacity) {
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
	}
	else {
		buffer.data = nullptr;
		buffer.capacity = 0;
	}

	while (curr.data != nullptr)
	{
		Arena_Buffer next = *(Arena_Buffer*)curr.data;
		free(curr.data);
		curr = next;
	}
}

Arena_Checkpoint Arena::make_checkpoint()
{
	Arena_Checkpoint checkpoint;
	checkpoint.data = next;
	return checkpoint;
}

// Note: This never deallocates memory
void Arena::rewind_to_checkpoint(Arena_Checkpoint checkpoint)
{
	usize address = (usize)checkpoint.data;
	if (address >= (usize)buffer.data && address <= (usize)buffer.data + buffer.capacity) {
		next = checkpoint.data;
	}
	else {
		next = (void*)((usize)buffer.data + sizeof(Arena_Buffer));
	}
}



// FREE LIST
Free_List Free_List::create(Arena* arena, usize element_size) 
{
	Free_List result;
	result.arena = arena;
	result.element_size = math_maximum(sizeof(void*), element_size);
	return result;
}

void* Free_List::allocate_raw(usize size)
{
	assert(size <= element_size, "");
	if (next != nullptr) {
		void* result = next;
		next = *(void**)next;
		return result;
	}
	return arena->allocate_raw(size, sizeof(void*));
}

void Free_List::deallocate_raw(void* data)
{
	*(void**)data = next; // Store list of free allocations
	next = data;
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
