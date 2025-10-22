#include "allocators.hpp"

#include "Windows.h"
#include "intrin.h"


// Math helpers
u32 integer_highest_set_bit_index(u64 value)
{
	assert(value != 0, "");
	unsigned long index;
	char ret_val = _BitScanReverse(&index, value);
	assert(ret_val != 0, "Should be the case");
	return index;
}

u32 integer_next_power_of_2(u64 value)
{
	u32 highest_bit = integer_highest_set_bit_index(value);
	if ((1ull << highest_bit) == value) return highest_bit;
	return highest_bit + 1;
}

u64 integer_next_multiple(u64 x, u64 m) {
    if (x % m == 0) {
        return x;
    }
    return x + (m - x % m);
}

u64 integer_maximum(u64 a, u64 b) {
	return a > b ? a : b;
}

u64 integer_minimum(u64 a, u64 b) {
	return a < b ? a : b;
}


// Allocator interface
void* Allocator::allocate_raw(u64 size, u32 alignment) {
	return vtable->allocate(this, size, alignment);
}
void Allocator::deallocate_raw(void* data, u64 size) {
	vtable->deallocate(this, data, size);
}
bool Allocator::resize(void* data, u64 old_size, u64 new_size) {
	return vtable->resize(this, data, old_size, new_size);
}

Allocator_VTable allocator_vtable_make(allocate_fn allocate, deallocate_fn deallocate, resize_fn resize)
{
	Allocator_VTable result;
	result.allocate = allocate;
	result.deallocate = deallocate;
	result.resize = resize;
	return result;
}



// Virtual Arena (VTable boilerplate)
extern Allocator_VTable virtual_arena_vtable;

void* virtual_arena_allocate_raw(Allocator* allocator, u64 size, u32 alignment) 
{
	Virtual_Arena* arena = (Virtual_Arena*)allocator;
	assert(arena->base.vtable == &virtual_arena_vtable, "");
	return arena->allocate_raw(size, alignment);
}

void virtual_arena_deallocate_raw(Allocator* allocator, void* data, u64 size) 
{
	Virtual_Arena* arena = (Virtual_Arena*)allocator;
	assert(arena->base.vtable == &virtual_arena_vtable, "");
	arena->deallocate_raw(data, size);
}

bool virtual_arena_resize(Allocator* allocator, void* data, u64 old_size, u64 new_size) 
{
	Virtual_Arena* arena = (Virtual_Arena*)allocator;
	assert(arena->base.vtable == &virtual_arena_vtable, "");
	return arena->resize(data, old_size, new_size);
}

static Allocator_VTable virtual_arena_vtable = allocator_vtable_make(virtual_arena_allocate_raw, virtual_arena_deallocate_raw, virtual_arena_resize);


// Virtual_Arena implementation
Virtual_Arena Virtual_Arena::create(u64 capacity) 
{
	assert(capacity != 0, "");

	Virtual_Arena result;
	result.base.vtable = &virtual_arena_vtable;
	result.commit_size = 0;
	capacity = integer_maximum(4096, integer_next_power_of_2(capacity)); // Minimum capacity is page-size
	result.capacity = capacity;
	result.buffer = VirtualAlloc(nullptr, capacity, MEM_RESERVE, PAGE_READWRITE);
	assert(result.buffer != 0, "");
	result.next = result.buffer;
	return result;
}

void Virtual_Arena::destroy() 
{
	bool success = VirtualFree(buffer, 0, MEM_RELEASE);
	assert(success, "");
}

void virtual_arena_commit_memory(Virtual_Arena* arena, u64 new_commit_size)
{
	if (arena->commit_size >= new_commit_size) return;
	u64 next_commit_size = integer_maximum(arena->commit_size << 1, integer_next_power_of_2(new_commit_size));
	assert(new_commit_size < arena->capacity, "Ran out of virtual arena space");
	arena->buffer = VirtualAlloc(arena->buffer, next_commit_size, MEM_COMMIT, PAGE_READWRITE);
	assert(arena->buffer != 0, "");
	arena->commit_size = new_commit_size;
}

void* Virtual_Arena::allocate_raw(u64 size, u32 alignment)
{
	u64 start = integer_next_multiple((u64)(next), alignment);
	virtual_arena_commit_memory(this, start + size - (u64)buffer);
	next = (void*)(start + size);
	return (void*)start;
}

void Virtual_Arena::deallocate_raw(void* data, u64 size)
{
	// Only deallocate if this was the last allocation made
	u64 address = (u64)data;
	if (address + size != (u64)next) return;
	assert(address >= (u64)buffer, "");
	next = data;
}

bool Virtual_Arena::resize(void* data, u64 old_size, u64 new_size)
{
	u64 address = (u64)data;
	if (address + old_size != (u64)next) return false; // Must be last allocator for resize
	assert(address >= (u64)buffer, "");

	// Should always work from here on out
	virtual_arena_commit_memory(this, address - (u64)buffer + new_size);
	next = (void*)(address + new_size);
	return true;
}



// Bin_Allocator (VTable boilerplate)
extern Allocator_VTable bin_allocator_vtable;

void* bin_allocator_allocate_raw(Allocator* allocator, u64 size, u32 alignment) 
{
	Bin_Allocator* bin_alloc = (Bin_Allocator*)allocator;
	assert(bin_alloc->base.vtable == &bin_allocator_vtable, "");
	return bin_alloc->allocate_raw(size);
}

void bin_allocator_deallocate_raw(Allocator* allocator, void* data, u64 size) 
{
	Bin_Allocator* bin_alloc = (Bin_Allocator*)allocator;
	assert(bin_alloc->base.vtable == &bin_allocator_vtable, "");
	bin_alloc->deallocate_raw(data, size);
}

bool bin_allocator_resize(Allocator* allocator, void* data, u64 old_size, u64 new_size) 
{
	Bin_Allocator* bin_alloc = (Bin_Allocator*)allocator;
	assert(bin_alloc->base.vtable == &bin_allocator_vtable, "");
	return false;
}

static Allocator_VTable bin_allocator_vtable = allocator_vtable_make(bin_allocator_allocate_raw, bin_allocator_deallocate_raw, bin_allocator_resize);


// Bin_Allocator implementation
Bin_Allocator Bin_Allocator::create(Allocator* parent_allocator, u64 max_allocation_size)
{
	Bin_Allocator result;
	result.parent_allocator = parent_allocator;
	result.base.vtable = &bin_allocator_vtable;

	// Minimum allocation size is 8, so we don't have 1, 2, or 4 byte bins
	result.max_allocation_size = integer_next_power_of_2(max_allocation_size);
	int bin_count = integer_highest_set_bit_index(result.max_allocation_size) - 2;
	result.bins = parent_allocator->allocate_array<void*>(bin_count);
	for (int i = 0; i < bin_count; i++) {
		result.bins[i] = nullptr;
	}
	return result;
}

void* Bin_Allocator::allocate_raw(u64 size) 
{
	assert(size != 0 && size < max_allocation_size, "");

	// Check if we already have memory for this size ready
	u32 bin_index = integer_next_power_of_2(size) - 3; // 2^3 = 8, which is the minimum bin-size
	void* bin_value = bins[bin_index];
	if (bin_value != nullptr) {
		bins[bin_index] = *((void**)bin_value);
		return bin_value;
	}

	// Otherwise we need to allocate a new value for the bin from parent
	return parent_allocator->allocate_raw(size, integer_minimum(16, integer_next_power_of_2(size)));
}

void Bin_Allocator::deallocate_raw(void* data, u64 size)
{
	assert(data != nullptr && size > 0, "");

	u32 bin_index = integer_next_power_of_2(size) - 3; // 2^3 = 8, which is the minimum bin-size
	*((void**)data) = bins[bin_index]; // Create list between deallocated memory areas
	bins[bin_index] = data;
}



// Arena (Vtable boilerplate)
extern Allocator_VTable arena_vtable;

void* arena_allocate_raw(Allocator* allocator, u64 size, u32 alignment) 
{
	Arena* arena = (Arena*)allocator;
	assert(arena->base.vtable == &arena_vtable, "");
	return arena->allocate_raw(size, alignment);
}

void arena_deallocate_raw(Allocator* allocator, void* data, u64 size) 
{
	Arena* arena = (Arena*)allocator;
	assert(arena->base.vtable == &arena_vtable, "");
	arena->deallocate_raw(data, size);
}

bool arena_resize(Allocator* allocator, void* data, u64 old_size, u64 new_size) 
{
	Arena* arena = (Arena*)allocator;
	assert(arena->base.vtable == &arena_vtable, "");
	return arena->resize(data, old_size, new_size);
}

static Allocator_VTable arena_vtable = allocator_vtable_make(arena_allocate_raw, arena_deallocate_raw, arena_resize);


// Arena implementation
struct Arena_Buffer_Header
{
	void* buffer;
	u64 capacity;
};

// Resizes current buffer or creates a new one so that we have at least capacity bytes available
// returns true if new allocation happened
bool arena_reserve(Arena* arena, u64 new_capacity)
{
	if (new_capacity <= arena->capacity) return false;
	new_capacity = integer_minimum(128, integer_next_power_of_2(new_capacity));
	if (arena->parent_allocator->resize(arena->buffer, arena->capacity, new_capacity)) {
		arena->capacity = new_capacity;
		return false;
	}

	void* new_buffer = arena->parent_allocator->allocate_raw(new_capacity, 16); // 16 byte alignment for header
	Arena_Buffer_Header* header = (Arena_Buffer_Header*)new_buffer;
	header->buffer = arena->buffer;
	header->capacity = arena->capacity;
	arena->buffer = new_buffer;
	arena->capacity = new_capacity;
	arena->next = (void*) ((u64)new_buffer + sizeof(Arena_Buffer_Header));
	return true;
}

Arena Arena::create(Allocator* parent_allocator, u64 capacity)
{
	Arena result;
	result.parent_allocator = parent_allocator;
	result.base.vtable = &arena_vtable;
	result.capacity = 0;
	result.buffer = nullptr;
	result.next = nullptr;
	arena_reserve(&result, capacity);
	return result;
}

void* Arena::allocate_raw(u64 size, u32 alignment)
{
	assert(size != 0 && alignment != 0, "");
	u64 result_address = integer_next_multiple((u64)next, alignment);
	bool resized = arena_reserve(this, result_address - (u64)buffer);
	if (resized) {
		result_address = (u64)next;
	}
	next = (void*)(result_address + size);
	return (void*)result_address;
}

void Arena::deallocate_raw(void* data, u64 size)
{
	u64 address = (u64)data;
	// Check if address was last allocation
	if (address + size != (u64)next) return;
	assert(address > (u64)buffer, "Resize not from this buffer!"); // With last check + header inside buffers this should be true
	next = data;
}

void Arena::destroy()
{
	Arena_Buffer_Header curr;
	curr.buffer = buffer;
	curr.capacity = capacity;
	while (curr.buffer != nullptr)
	{
		Arena_Buffer_Header next = *((Arena_Buffer_Header*)curr.buffer);
		parent_allocator->deallocate_raw(curr.buffer, curr.capacity);
		curr = next;
	}
	buffer = nullptr;
}

bool Arena::resize(void* memory, u64 old_size, u64 new_size)
{
	u64 address = (u64)memory;
	// Check if address was last allocation
	if (address + old_size != (u64)next) return false;
	assert(address > (u64)buffer, "Resize not from this buffer!"); // With last check + header inside buffers this should be true

	// Check if we have enough space for resize
	if (address + new_size <= (u64)buffer + capacity) {
		next = (void*)(address + new_size);
		return true;
	}

	// Otherwise check if we can resize
	u64 required_capacity = integer_next_power_of_2(address + new_size - (u64)buffer);
	if (parent_allocator->resize(buffer, capacity, required_capacity)) {
		capacity = required_capacity;
		next = (void*)(address + new_size);
		return true;
	}

	return false;
}

Arena_Checkpoint Arena::make_checkpoint()
{
	Arena_Checkpoint checkpoint;
	checkpoint.data = next;
	return checkpoint;
}

void Arena::rewind_to_checkpoint(Arena_Checkpoint checkpoint)
{
	Arena_Buffer_Header current;
	current.buffer = buffer;
	current.capacity = capacity;
	u64 address = (u64)checkpoint.data;
	while (!(address >= (u64)current.buffer && address <= (u64)current.buffer + current.capacity))
	{
		assert(current.buffer != nullptr, "checkpoint must be somewhere in here...");
		// Clear buffer
		Arena_Buffer_Header prev_buffer = *((Arena_Buffer_Header*)current.buffer);
		parent_allocator->deallocate_raw(current.buffer, current.capacity);
		current = prev_buffer;
	}

	buffer = current.buffer;
	capacity = current.capacity;
	next = checkpoint.data;
}



// Free_List (Vtable boilerplate)
extern Allocator_VTable free_list_vtable;

void* free_list_allocate_raw(Allocator* allocator, u64 size, u32 alignment) 
{
	Free_List* list = (Free_List*)allocator;
	assert(list->base.vtable == &free_list_vtable, "");
	return list->allocate_raw(size, alignment);
}

void free_list_deallocate_raw(Allocator* allocator, void* data, u64 size) 
{
	Free_List* list = (Free_List*)allocator;
	assert(list->base.vtable == &free_list_vtable, "");
	list->deallocate_raw(data);
}

bool free_list_resize(Allocator* allocator, void* data, u64 old_size, u64 new_size) 
{
	Free_List* list = (Free_List*)allocator;
	assert(list->base.vtable == &free_list_vtable, "");
	return false;
}

static Allocator_VTable free_list_vtable = allocator_vtable_make(free_list_allocate_raw, free_list_deallocate_raw, free_list_resize);


// Free_List implementation
Free_List Free_List::create(Allocator* parent_allocator, u64 allocation_size) 
{
	Free_List result;
	result.base.vtable = &free_list_vtable;
	result.parent_allocator = parent_allocator;
	result.next = nullptr;
	result.allocation_size = allocation_size;
	return result;
}

void* Free_List::allocate_raw(u64 size, u32 alignment)
{
	assert(size <= allocation_size, "");
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



// System allocator Vtable
extern Allocator_VTable system_allocator_vtable;

void* system_allcator_allocate_raw(Allocator* allocator, u64 size, u32 alignment) 
{
	System_Allocator* system_alloc = (System_Allocator*)allocator;
	assert(system_alloc->base.vtable == &system_allocator_vtable, "");
	return system_alloc->allocate_raw(size, alignment);
}

void system_allcator_deallocate_raw(Allocator* allocator, void* data, u64 size) 
{
	System_Allocator* system_alloc = (System_Allocator*)allocator;
	assert(system_alloc->base.vtable == &system_allocator_vtable, "");
	system_alloc->deallocate_raw(data);
}

bool system_allcator_resize(Allocator* allocator, void* data, u64 old_size, u64 new_size) 
{
	System_Allocator* system_alloc = (System_Allocator*)allocator;
	assert(system_alloc->base.vtable == &system_allocator_vtable, "");
	return false;
}

static Allocator_VTable system_allocator_vtable = allocator_vtable_make(system_allcator_allocate_raw, system_allcator_deallocate_raw, system_allcator_resize);



// System allocator implementation 
System_Allocator system_allocator_create() {
	System_Allocator system_alloc;
	system_alloc.base.vtable = &system_allocator_vtable;
	return system_alloc;
}

static System_Allocator system_allocator = system_allocator_create();

System_Allocator* System_Allocator::get_instance() {

	return &system_allocator;
}

void* System_Allocator::allocate_raw(u64 size, u32 alignment)
{
	return malloc(size);
}

void System_Allocator::deallocate_raw(void* data)
{
	free(data);
}



// Mutex allocator Vtable
extern Allocator_VTable mutex_allocator_vtable;

void* mutex_allocator_allocate_raw(Allocator* allocator, u64 size, u32 alignment) 
{
	Mutex_Allocator* mutex_alloc = (Mutex_Allocator*)allocator;
	assert(mutex_alloc->base.vtable == &mutex_allocator_vtable, "");
	return mutex_alloc->allocate_raw(size, alignment);
}

void mutex_allocator_deallocate_raw(Allocator* allocator, void* data, u64 size) 
{
	Mutex_Allocator* mutex_alloc = (Mutex_Allocator*)allocator;
	assert(mutex_alloc->base.vtable == &mutex_allocator_vtable, "");
	mutex_alloc->deallocate_raw(data, size);
}

bool mutex_allocator_resize(Allocator* allocator, void* data, u64 old_size, u64 new_size) 
{
	Mutex_Allocator* mutex_alloc = (Mutex_Allocator*)allocator;
	assert(mutex_alloc->base.vtable == &mutex_allocator_vtable, "");
	return mutex_alloc->resize(data, old_size, new_size);
}

static Allocator_VTable mutex_allocator_vtable = allocator_vtable_make(mutex_allocator_allocate_raw, mutex_allocator_deallocate_raw, mutex_allocator_resize);


// Mutext allocator implementation
Mutex_Allocator Mutex_Allocator::create(Allocator* parent_allocator)
{
	Mutex_Allocator result;
	result.base.vtable = &mutex_allocator_vtable;
	result.semaphore = semaphore_create(1, 1);
	result.parent_allocator = parent_allocator;
	return result;
}

void Mutex_Allocator::destroy() {
	semaphore_destroy(semaphore);
}

void* Mutex_Allocator::allocate_raw(u64 size, u32 alignment) {
	semaphore_wait(semaphore);
	void* result = parent_allocator->allocate_raw(size, alignment);
	semaphore_increment(semaphore, 1);
	return result;
}

void Mutex_Allocator::deallocate_raw(void* data, u64 size) {
	semaphore_wait(semaphore);
	parent_allocator->deallocate_raw(data, size);
	semaphore_increment(semaphore, 1);
}

bool Mutex_Allocator::resize(void* data, u64 old_size, u64 new_size) {
	semaphore_wait(semaphore);
	bool result = parent_allocator->resize(data, old_size, new_size);
	semaphore_increment(semaphore, 1);
	return result;
}



// Helpers
#define MAKE_UPCAST_FN(type, virtual_table) \
inline Allocator* upcast(type value) { assert(value.base.vtable == &virtual_table, "Error upcast"); return &value.base; } \
inline Allocator* upcast(type* value) { assert(value->base.vtable == &virtual_table, "Error upcast"); return &value->base; } \

MAKE_UPCAST_FN(Virtual_Arena, virtual_arena_vtable)
MAKE_UPCAST_FN(Bin_Allocator, bin_allocator_vtable)
MAKE_UPCAST_FN(Arena, arena_vtable)
MAKE_UPCAST_FN(Free_List, free_list_vtable)
MAKE_UPCAST_FN(System_Allocator, system_allocator_vtable)
MAKE_UPCAST_FN(Mutex_Allocator, mutex_allocator_vtable)

#undef MAKE_UPCAST_FN
