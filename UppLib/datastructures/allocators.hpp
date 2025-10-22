#pragma once

#include "../utility/utils.hpp"
#include "array.hpp"
#include "../win32/thread.hpp"	

#define INSERT_TEMPLATE_ALLOC_FUNCTIONS_ALIGN \
template<typename T> \
T* allocate() {	return (T*)allocate_raw(sizeof(T), alignof(T)); } \
\
template<typename T> \
Array<T> allocate_array(int size) { \
	return array_create_static<T>((T*) allocate_raw(sizeof(T) * size, alignof(T)), size); \
} 

#define INSERT_TEMPLATE_ALLOC_FUNCTIONS \
template<typename T> \
T* allocate() {	return (T*)allocate_raw(sizeof(T)); } \
\
template<typename T> \
Array<T> allocate_array(int size) { \
	return array_create_static<T>((T*) allocate_raw(sizeof(T) * size, alignof(T)), size); \
} 

#define INSERT_TEMPLATE_DEALLOC_FUNCTIONS \
template<typename T> \
void deallocate(T* data) { return deallocate_raw(data, sizeof(T)); } \
\
template<typename T> \
Array<T> deallocate_array(Array<T> array) { \
	deallocate_raw(array.data, sizeof(T) * array.size); \
}

#define INSERT_TEMPLATE_DEALLOC_FUNCTIONS_NO_SIZE \
template<typename T> \
void deallocate(T* data) { return deallocate_raw(data); } \
\
template<typename T> \
Array<T> deallocate_array(Array<T> array) { \
	deallocate_raw(array.data); \
}


struct Allocator* allocator;

typedef void* (*allocate_fn)(Allocator* allocator, u64 size, u32 alignment);
typedef void (*deallocate_fn)(Allocator* allocator, void* data, u64 size);
typedef bool (*resize_fn)(Allocator* allocator, void* data, u64 old_size, u64 new_size);

struct Allocator_VTable
{
	allocate_fn allocate;
	deallocate_fn deallocate;
	resize_fn resize;
};

struct Allocator
{
	Allocator_VTable* vtable = nullptr;

	// I add these functions for convenience, so dot calls work with them
	void* allocate_raw(u64 size, u32 alignment);
	void deallocate_raw(void* data, u64 size);
	bool resize(void* data, u64 old_size, u64 new_size);

	INSERT_TEMPLATE_ALLOC_FUNCTIONS_ALIGN;
	INSERT_TEMPLATE_DEALLOC_FUNCTIONS;
};



// Fixed size virtual arena, which commits more memory if necessary
struct Virtual_Arena
{
	Allocator base;

	void* buffer;
	u64 capacity; // Maximum amount of memory that can be allocated 
	u64 commit_size; // Size of memory that is commited
	void* next;

	static Virtual_Arena create(u64 capacity);
	void destroy();

	void* allocate_raw(u64 size, u32 alignment);
	void deallocate_raw(void* data, u64 size);
	bool resize(void* data, u64 old_size, u64 new_size);

	INSERT_TEMPLATE_ALLOC_FUNCTIONS_ALIGN;
	INSERT_TEMPLATE_DEALLOC_FUNCTIONS;
};

// Has free-lists up to a specific size of allocations
struct Bin_Allocator
{
	Allocator base;
	Allocator* parent_allocator;
	Array<void*> bins; // Allocated in parent allocator
	u64 max_allocation_size;

	static Bin_Allocator create(Allocator* parent_allocator, u64 max_allocation_size);

	void* allocate_raw(u64 size); // Size must be > 0, Note: alignment for bin-allocator is always correct, as it uses max-alignment or alloc-size
	void deallocate_raw(void* data, u64 size);

	INSERT_TEMPLATE_ALLOC_FUNCTIONS;
	INSERT_TEMPLATE_DEALLOC_FUNCTIONS;
};

struct Arena_Checkpoint
{
	void* data;
};

struct Arena
{
	Allocator base;
	Allocator* parent_allocator;

	void* buffer;
	u64 capacity;
	void* next;

	static Arena create(Allocator* parent_allocator, u64 capacity); // Capacity may be null
	void destroy();

	void* allocate_raw(u64 size, u32 alignment);
	void deallocate_raw(void* data, u64 size);
	bool resize(void* memory, u64 old_size, u64 new_size);

	Arena_Checkpoint make_checkpoint();
	void rewind_to_checkpoint(Arena_Checkpoint checkpoint);

	INSERT_TEMPLATE_ALLOC_FUNCTIONS;
	INSERT_TEMPLATE_DEALLOC_FUNCTIONS;
};

struct Free_List
{
	Allocator base;
	Allocator* parent_allocator;
	void* next;
	u64 allocation_size;

	static Free_List create(Allocator* parent_allocator, u64 allocation_size);
	void* allocate_raw(u64 size, u32 alignment);
	void deallocate_raw(void* data);

	INSERT_TEMPLATE_ALLOC_FUNCTIONS;
	INSERT_TEMPLATE_DEALLOC_FUNCTIONS_NO_SIZE;
};

struct System_Allocator
{
	Allocator base;

	static System_Allocator* get_instance(); // There is only one global system allocator, as more are unnecessary

	void* allocate_raw(u64 size, u32 alignment);
	void deallocate_raw(void* data);
	// Note: realloc exists, but it doesn't have the same sytnax as our resize...

	INSERT_TEMPLATE_ALLOC_FUNCTIONS;
	INSERT_TEMPLATE_DEALLOC_FUNCTIONS_NO_SIZE;
};

struct Mutex_Allocator
{
	Allocator base;
	Allocator* parent_allocator;
	Semaphore semaphore;

	static Mutex_Allocator create(Allocator* parent_allocator);
	void destroy(); // Only destroys the mutex, allocations stay the same

	void* allocate_raw(u64 size, u32 alignment);
	void deallocate_raw(void* data, u64 size);
	bool resize(void* data, u64 old_size, u64 new_size);

	INSERT_TEMPLATE_ALLOC_FUNCTIONS;
	INSERT_TEMPLATE_DEALLOC_FUNCTIONS;
};

#undef INSERT_TEMPLATE_ALLOC_FUNCTIONS

#define MAKE_UPCAST_FN(type, virtual_table) \
inline Allocator* upcast(type value); \
inline Allocator* upcast(type* value);

MAKE_UPCAST_FN(Virtual_Arena, virtual_arena_vtable)
MAKE_UPCAST_FN(Bin_Allocator, bin_allocator_vtable)
MAKE_UPCAST_FN(Arena, arena_vtable)
MAKE_UPCAST_FN(Free_List, free_list_vtable)
MAKE_UPCAST_FN(System_Allocator, system_allocator_vtable)
MAKE_UPCAST_FN(Mutex_Allocator, mutex_allocator_vtable)

#undef MAKE_UPCAST_FN
