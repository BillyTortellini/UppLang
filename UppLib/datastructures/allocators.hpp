#pragma once

#include "../utility/datatypes.hpp"
#include "array.hpp"
#include "../utility/hash_functions.hpp"
#include "../math/scalars.hpp"


// Note about 0-sized allocations:
//   I don't want to deal with 0 sized allocation/deallocation, so we crash if this is the case
//   The only use case is allocating arrays/slices, as 0-sized slices are OK 


// Allocator_Base
enum class Allocation_Type
{
	ALLOCATE,   // Uses new_size and alignment, ignores curr_data, returns allocated data
	DEALLOCATE, // Uses curr_data and curr_size, ignores new_size, returns nullptr
	RESIZE,     // Uses new_size, curr_size and curr_data, returns nullptr if resize didn't work
};

struct Allocator_Base;

typedef void* (*allocate_function)(
	Allocator_Base* allocator, Allocation_Type allocation_type, uint new_size, uint alignment_or_curr_size, void* curr_data
);

struct Allocator_Base
{
	allocate_function allocate_fn;

	// Helper function so we don't have to use the function pointer directly
	void* allocate_raw(uint size, uint alignment);
	bool resize(void* memory, uint old_size, uint new_size);
	void deallocate(void* memory, uint allocation_size);

	template<typename T> 
	T* allocate() {	return (T*)allocate_raw(sizeof(T), alignof(T)); } 
	
	template<typename T> 
	Array<T> allocate_array(int size) {
		if (size == 0) {
			Array<T> result;
			result.data = nullptr;
			result.size = 0;
			return result;
		}
		return array_create_static<T>((T*) allocate_raw(sizeof(T) * size, alignof(T)), size); 
	} 

	template<typename T> 
	void deallocate_array(Array<T> array) {
		if (array.size == 0) {
			return;
		}
		deallocate(array.data, sizeof(T) * array.size);
	} 
};



// System_Allocator
struct System_Allocator
{
	Allocator_Base base;
	static System_Allocator create();
	Allocator_Base* upcast();
};

extern System_Allocator global_system_allocator;



// Arena
struct Arena;

struct Arena_Checkpoint
{
	Arena* arena;
	void* data;
	void rewind();
};

struct Arena_Buffer
{
	// Note: every arena buffer starts with this struct, which forms a linked list
	//		to previously allocated buffers
	void* data;
	uint capacity;
};

struct Arena
{
	Allocator_Base base;
	Allocator_Base* parent_allocator;
	Arena_Buffer buffer;
	void* next;

	// If parent_allocator == 0, system allocator is used
	static Arena create(uint capacity = 0, Allocator_Base* parent_allocator = nullptr); 
	void destroy();

	void* allocate_raw(uint size, u32 alignment);
	bool resize(void* memory, uint old_size, uint new_size);
	void reset(bool keep_largest_buffer = false);

	template<typename T> 
	T* allocate() {	return (T*)allocate_raw(sizeof(T), alignof(T)); } 
	
	template<typename T> 
	Array<T> allocate_array(int size) { 
		if (size == 0) {
			Array<T> result;
			result.data = nullptr;
			result.size = 0;
			return result;
		}
		return array_create_static<T>((T*) allocate_raw(sizeof(T) * size, alignof(T)), size); 
	} 

	Arena_Checkpoint make_checkpoint();
	void rewind_to_checkpoint(Arena_Checkpoint checkpoint);
	void Arena::rewind_to_address(void* pointer);

	Allocator_Base* upcast();
};



// Scratch_Arena utility
struct Scratch_Arena_Pair
{
	Arena* main_arena;
	Arena* fallback_arena;
};

Scratch_Arena_Pair scratch_arena_pair_make(Arena* main_arena, Arena* fallback_arena);
void scratch_arena_set_arenas(Scratch_Arena_Pair scratch_arenas);
Scratch_Arena_Pair scratch_arena_get_arenas_in_use();
Arena* scratch_arena_retrieve(Arena* arena_to_avoid);

#define SCRATCH_ARENA_MAKE_SCOPED(permanent_arena) \
	Arena* scratch_arena = scratch_arena_retrieve(permanent_arena); \
	Arena_Checkpoint _scratch_arena_checkpoint = scratch_arena->make_checkpoint(); \
	SCOPE_EXIT(_scratch_arena_checkpoint.rewind());



// Free_List
struct Free_List
{
	Allocator_Base base;
	Allocator_Base* parent_allocator;
	uint element_size;
	uint element_alignment;
	void* next; // List of allocations

	static Free_List create_with_info(Allocator_Base* parent_allocator, uint element_size, uint element_alignment);

	template<typename T>
	static Free_List create(Allocator_Base* parent_allocator) {
		return Free_List::create_with_info(parent_allocator, sizeof(T), alignof(T));
	}

	void* allocate_raw(uint size, uint alignment);
	void deallocate_raw(void* data);

	template<typename T> 
	T* allocate() {	return (T*)allocate_raw(sizeof(T), alignof(T)); } 

	template<typename T> 
	void deallocate(T* data) { deallocate_raw((void*)data); }

	Allocator_Base* upcast();
};



template<typename T>
struct DynArray
{
	Arena* arena;
	Array<T> buffer;
	uint size;

	static DynArray<T> create(Arena* arena, uint capacity = 0)
	{
		DynArray<T> result;
		result.arena = arena;
		result.buffer.data = nullptr;
		result.buffer.size = 0;
		result.size = 0;
		result.reserve(capacity);
		return result;
	}

	void reset() {
		size = 0;
	}

	void reserve(uint requested_size) 
	{
		if (buffer.size >= requested_size) return;

		// Figure out new size
		uint new_size = math_maximum(requested_size, (uint) (buffer.size * 3) / 2 + 1);

		if (buffer.data == nullptr) {
			buffer = arena->allocate_array<T>((int) new_size);
			return;
		}

		// Check if resize possible
		if (arena->resize(buffer.data, buffer.size * sizeof(T), new_size * sizeof(T))) {
			buffer.size = (int) new_size;
			return;
		}

		// Otherwise create new buffer and move data
		Array<T> new_buffer = arena->allocate_array<T>((int) new_size);
		memory_copy(new_buffer.data, buffer.data, size * sizeof(T));
		buffer = new_buffer;
	}

	void push_back(const T& value) {
		reserve(size + 1);
		buffer[size] = value;
		size += 1;
	}
	
	void insert_ordered(const T& value, int insert_before_index)
	{
		assert(insert_before_index >= 0 && insert_before_index <= size, "");
		reserve(size + 1);
		size += 1;
		for (int i = size - 1; i - 1 >= insert_before_index; i -= 1) {
			buffer[i] = buffer[i - 1];
		}
		buffer[insert_before_index] = value;
	}

	void remove_range_ordered(int start, int count)
	{
		assert(start >= 0 && start <= size, "");
		count = math_minimum(count, (int)size - start);
		if (count <= 0) return;

		for (int i = start + count; i < size; i++) {
			buffer[i - count] = buffer[i];
		}
		size -= count;
	}

	void swap_remove(int index) {
		assert(index >= 0 && index < size, "");
		if (index != size - 1) {
			buffer[index] = buffer[size - 1];
		}
		size -= 1;
	}

	void remove_ordered(int index) {
		for (int i = index; i + 1 < size; i++) {
			buffer.data[i] = buffer.data[i + 1];
		}
		size -= 1;
	}

	void rollback_to_size(int new_size)
	{
		assert((uint)new_size <= size, "Can only make array smaller");
		size = (uint)new_size;
	}

	T& last() {
		assert(size > 0, "");
		return buffer[size - 1];
	}

	T& operator[](int index) {
		assert(index < size && index >= 0, "");
		return buffer.data[index];
	}
};



// DynSet

u64 find_next_suitable_prime_hashset_size(u64 value);

const float DYNSET_MAX_LOAD_FACTOR = 0.7f;

enum class DynSet_Entry_State
{
	OCCUPIED,  // Value is valid
	FREE,      // entry is free
	FREE_AGAIN
};

template<typename T>
struct DynSet_Entry
{
	T value;
	u64 hash; // For faster comparison, so equal_fn does not need to be called every time
	DynSet_Entry_State state;
	int sonding_index; 
};

template<typename T>
struct DynSet
{
	Arena* arena;
    Array<DynSet_Entry<T>> entries;
    uint element_count;
    u64(*hash_function)(T*);
    bool(*equals_function)(T*, T*);

	static DynSet<T> create(Arena* arena, u64(*hash_fn)(T*), bool(*equals_fn)(T*, T*), uint expected_element_count = 0) 
	{
		DynSet<T> result;
		result.arena = arena;
		result.hash_function = hash_fn;
		result.equals_function = equals_fn;
		result.entries.data = nullptr;
		result.entries.size = 0;
		result.element_count = 0;
		if (expected_element_count != 0) {
			result.reserve(expected_element_count);
		}
		return result;
	}

	static DynSet<T> create_pointer(Arena* arena, uint expected_element_count = 0) 
	{
		return DynSet<T>::create(
			arena, 
			[](T* key) -> u64 { return hash_pointer(*key); },
			[](T* a, T* b) -> bool { return (*a) == (*b); }, 
			expected_element_count
		);
	}

	void reset()
	{
		for (int i = 0; i < entries.size; i++) {
			entries[i].state = DynSet_Entry_State::FREE;
		}
		element_count = 0;
	}

	void reserve(uint expected_element_count)
	{
		uint min_size = (uint)((float)expected_element_count / DYNSET_MAX_LOAD_FACTOR) + 1;
		if (entries.size >= min_size) return;
		min_size = math_maximum((entries.size * 3) / 2 + 1, (int)min_size);
		uint new_size = find_next_suitable_prime_hashset_size(min_size);
		assert(new_size >= expected_element_count, "");

		if (entries.data == nullptr) {
			entries = arena->allocate_array<DynSet_Entry<T>>((int)new_size);
			reset();
			return;
		}

		Arena_Checkpoint checkpoint;
		Array<DynSet_Entry<T>> old_entries;
		if (arena->resize(entries.data, entries.size * sizeof(DynSet_Entry<T>), new_size * sizeof(DynSet_Entry<T>)))
		{
			// Resize is annoying, because we need a temporary copy of the old values need to copy over our old values
			checkpoint = arena->make_checkpoint();
			old_entries = arena->allocate_array<DynSet_Entry<T>>(entries.size);
			memory_copy(old_entries.data, entries.data, entries.size * sizeof(DynSet_Entry<T>));
			entries.size = (int)new_size;
		}
		else
		{
			// Allocate new buffer
			old_entries = entries;
			entries = arena->allocate_array<DynSet_Entry<T>>((int)new_size);
			checkpoint = arena->make_checkpoint();
		}

		// Reset current buffer/initialize new buffer
		reset();

		// Re-insert old entries into new entries
		for (int i = 0; i < old_entries.size; i++) 
		{
			auto& old_entry = old_entries[i];
			if (old_entry.state != DynSet_Entry_State::OCCUPIED) continue;
			insert_with_hash(old_entry.value, old_entry.hash);
		}
		arena->rewind_to_checkpoint(checkpoint);
	}

	int hash_to_entry_index(u64 hash_value, int sonding_index)
	{
		u64 sonding_increment = hash_combine(hash_value, 0xFE57D3AC94BF1E27) % entries.size;
		if (sonding_increment == 0) {
			// Must not be null, otherwise we wouldn't reach all element entries
			// This is a large prime not on the list of hashtable-sizes, so this % entries.size != 0
			sonding_increment = 87178291199ull % entries.size; 
			assert(sonding_increment != 0, "");
		}

		// NOTE: We modulo before calculation because integer overflow
		//	will result in invalid calculations (Because the modulo will change in unpredictable ways)
		u64 hash_pos = hash_value % entries.size;
		return (hash_pos + sonding_index * sonding_increment) % entries.size;
	}

	bool insert_with_hash(T& value, u64 value_hash)
	{
		reserve(element_count + 1);
		for (int sonding_index = 0; sonding_index < entries.size; sonding_index += 1)
		{
			auto& entry = entries[hash_to_entry_index(value_hash, sonding_index)];

			// Insert at entry if it isn't occupied
			if (entry.state != DynSet_Entry_State::OCCUPIED) 
			{
				entry.state = DynSet_Entry_State::OCCUPIED;
				entry.value = value;
				entry.sonding_index = sonding_index;
				entry.hash = value_hash;
				element_count += 1;
				return true;
			}

			// Check if element is already inserted (Set needs to be a set, no duplicate values)
			if (entry.hash == value_hash && equals_function(&entry.value, &value)) {
				return false;
			}

			// Check if the entry value can be moved (Improvement by Brent paper)
			{
				auto& other_entry = entries[hash_to_entry_index(entry.hash, entry.sonding_index + 1)];
				if (other_entry.state != DynSet_Entry_State::OCCUPIED) 
				{
					// Move current entry to other_entry
					other_entry = entry;
					other_entry.sonding_index += 1;

					// Store value in now freed current entry
					entry.state = DynSet_Entry_State::OCCUPIED;
					entry.value = value;
					entry.sonding_index = sonding_index;
					entry.hash = value_hash;
					element_count += 1;
					return true;
				}
			}
		}

		panic("Table is full, should not happen after reserve!");
		return false;
	}

	// Returns true if value was inserted, false if it already exists?
	bool insert(T& value) {
		return insert_with_hash(value, hash_function(&value));
	}

	T* find(T& value)
	{
		u64 value_hash = hash_function(&value);
		for (int sonding_index = 0; sonding_index < entries.size; sonding_index++)
		{
			auto& entry = entries[hash_to_entry_index(value_hash, sonding_index)];
			if (entry.state == DynSet_Entry_State::FREE) {
				return nullptr;
			}
			else if (entry.state == DynSet_Entry_State::OCCUPIED) {
				if (entry.hash == value_hash && equals_function(&entry.value, &value)) {
					return &entry.value;
				}
			}
		}
		return nullptr;
	}

	bool contains(T& value)
	{
		return find(value) != nullptr;
	}

	// Returns true if the operation succeeded, otherwise false
	bool remove(T& value)
	{
		u64 value_hash = hash_function(&value);
		for (int sonding_index = 0; sonding_index < entries.size; sonding_index++)
		{
			auto& entry = entries[hash_to_entry_index(value_hash, sonding_index)];
			if (entry.state == DynSet_Entry_State::FREE) {
				return false;
			}
			else if (entry.state == DynSet_Entry_State::OCCUPIED) {
				if (entry.hash == value_hash && equals_function(&entry.value, &value)) {
					entry.state = DynSet_Entry_State::FREE_AGAIN;
					element_count -= 1;
					return true;
				}
			}
		}
		return false;
	}
};



// DynTable
struct DynTable_Query_Result
{
	u64 hash;
	int index; // If value is in table, then this is the entry-index, otherwise it's the sonding index for insert
	bool value_is_in_table;
};

template<typename K, typename V>
struct DynTable_Entry
{
	K key;
	V value;
	u64 hash; // For faster comparison, so equal_fn does not need to be called every time
	int sonding_index; 
	DynSet_Entry_State state;
};

template<typename K, typename V>
struct DynTable
{
	Arena* arena;
    Array<DynTable_Entry<K, V>> entries;
    uint element_count;
    u64(*hash_function)(K*);
    bool(*equals_function)(K*, K*);

	static DynTable<K, V> create(Arena* arena, u64(*hash_fn)(K*), bool(*equals_fn)(K*, K*), uint expected_element_count = 0) 
	{
		DynTable<K, V> result;
		result.arena = arena;
		result.hash_function = hash_fn;
		result.equals_function = equals_fn;
		result.entries.data = nullptr;
		result.entries.size = 0;
		result.element_count = 0;
		if (expected_element_count != 0) {
			result.reserve(expected_element_count);
		}
		return result;
	}

	static DynTable<K, V> create_pointer(Arena* arena, uint expected_element_count = 0) 
	{
		return DynTable<K, V>::create(
			arena, 
			[](K* key) -> u64 { return hash_pointer(*key); },
			[](K* a, K* b) -> bool { return (*a) == (*b); }, 
			expected_element_count
		);
	}

	void reset()
	{
		for (int i = 0; i < entries.size; i++) {
			entries[i].state = DynSet_Entry_State::FREE;
		}
		element_count = 0;
	}

	void reserve(uint expected_element_count)
	{
		uint min_size = (uint)((float)expected_element_count / DYNSET_MAX_LOAD_FACTOR) + 1;
		if (entries.size >= min_size) return;

		// Calculate new size
		min_size = math_maximum((entries.size * 3) / 2 + 1, (int)min_size);
		uint new_size = find_next_suitable_prime_hashset_size(min_size);
		assert(new_size >= expected_element_count, "");

		// Handle empty table
		if (entries.data == nullptr) {
			entries = arena->allocate_array<DynTable_Entry<K, V>>((int)new_size);
			reset();
			return;
		}

		// Try to resize through arena, otherwise allocate new buffer
		Arena_Checkpoint checkpoint;
		Array<DynTable_Entry<K, V>> old_entries;
		if (arena->resize(entries.data, entries.size * sizeof(DynTable_Entry<K, V>), new_size * sizeof(DynTable_Entry<K, V>)))
		{
			// Resize is annoying, because we need a temporary copy of the old values to copy over our old values
			checkpoint = arena->make_checkpoint();
			old_entries = arena->allocate_array<DynTable_Entry<K,V>>(entries.size);
			memory_copy(old_entries.data, entries.data, entries.size * sizeof(DynTable_Entry<K,V>));
			entries.size = (int)new_size;
		}
		else
		{
			// Allocate new buffer
			old_entries = entries;
			entries = arena->allocate_array<DynTable_Entry<K,V>>((int)new_size);
			checkpoint = arena->make_checkpoint();
		}

		// Reset current buffer/initialize new buffer
		reset();

		// Re-insert old entries into new entries
		for (int i = 0; i < old_entries.size; i++) 
		{
			auto& old_entry = old_entries[i];
			if (old_entry.state != DynSet_Entry_State::OCCUPIED) continue;
			insert_with_query(query_with_hash(old_entry.key, old_entry.hash), old_entry.key, old_entry.value);
		}
		arena->rewind_to_checkpoint(checkpoint);
	}

	int hash_to_entry_index(u64 hash_value, int sonding_index)
	{
		// NOTE: We modulo the sonding-increment
		//     before position calculation because integer overflow would result in invalid modulo calculations
		u64 sonding_increment = hash_combine(hash_value, 0xFE57D3AC94BF1E27) % entries.size;
		if (sonding_increment == 0) {
			// Must not be null, otherwise we wouldn't reach all element entries
			// This is a large prime not on the list of hashtable-sizes, so this % entries.size != 0
			sonding_increment = 87178291199ull % entries.size; 
			assert(sonding_increment != 0, "");
		}

		u64 hash_pos = hash_value % entries.size;
		return (hash_pos + sonding_index * sonding_increment) % entries.size;
	}

	DynTable_Query_Result query_with_hash(K key, u64 hash, bool query_for_insert = true)
	{
		DynTable_Query_Result result;
		result.hash = hash;
		result.index = query_for_insert ? -1 : -2;
		result.value_is_in_table = false;

		if (query_for_insert) {
			reserve(element_count + 1);
		}

		for (int sonding_index = 0; sonding_index < entries.size; sonding_index++)
		{
			int entry_index = hash_to_entry_index(result.hash, sonding_index);
			auto& entry = entries[entry_index];

			switch (entry.state)
			{
			case DynSet_Entry_State::OCCUPIED: 
			{
				// Check if element is already inserted (No duplicate values)
				if (entry.hash == result.hash && equals_function(&entry.key, &key)) {
					result.value_is_in_table = true;
					result.index = entry_index;
					return result;
				}

				// Check if the entry value can be moved (Improvement by Brent paper)
				if (result.index == -1) 
				{
					int other_entry_index = hash_to_entry_index(entry.hash, entry.sonding_index + 1);
					auto& other_entry = entries[other_entry_index];
					if (other_entry.state != DynSet_Entry_State::OCCUPIED) {
						result.index = sonding_index; // We can use this sonding index for inserts
					}
				}
				break;
			}
			case DynSet_Entry_State::FREE: 
			{
				result.value_is_in_table = false;
				if (result.index == -1) {
					result.index = sonding_index; // We can insert at this sonding_index
				}
				return result;
			}
			case DynSet_Entry_State::FREE_AGAIN: 
			{
				if (result.index == -1) {
					result.index = sonding_index;
				}
				// If free again, we need to continue searching...
				break;
			}
			default: panic("");
			}
		}

		return result;
	}

	DynTable_Query_Result query(K key, bool query_for_insert = true) {
		return query_with_hash(key, hash_function(&key), query_for_insert);
	}

	void insert_with_query(DynTable_Query_Result result, const K& key, const V& value)
	{
		// Find entry to insert
		DynTable_Entry<K, V>* entry = nullptr;
		if (result.value_is_in_table) 
		{
			// If entry is in table, then we overwrite the value
			entry = &entries[result.index];
		}
		else
		{
			assert(result.index >= 0, "Must be true, otherwise we searched without insert query");
			reserve(element_count + 1);
			element_count += 1;
			entry = &entries[hash_to_entry_index(result.hash, result.index)];

			// Check if we need to relocate (Improvement by Brent)
			if (entry->state == DynSet_Entry_State::OCCUPIED) 
			{
				DynTable_Entry<K, V>* relocation_entry = &entries[hash_to_entry_index(entry->hash, entry->sonding_index + 1)];
				assert(relocation_entry->state != DynSet_Entry_State::OCCUPIED, "Must be true");
				// Move current entry to other_entry
				*relocation_entry = *entry;
				relocation_entry->sonding_index += 1;
			}

			entry->sonding_index = result.index;
		}

		entry->hash = result.hash;
		entry->key = key;
		entry->value = value;
		entry->state = DynSet_Entry_State::OCCUPIED;
	}

	void remove_with_query(DynTable_Query_Result result)
	{
		if (!result.value_is_in_table) return;
		element_count -= 1;
		assert(entries[result.index].state == DynSet_Entry_State::OCCUPIED, "");
		entries[result.index].state = DynSet_Entry_State::FREE_AGAIN;
	}

	K* query_to_key(DynTable_Query_Result result) {
		assert(result.value_is_in_table, "");
		return &entries[result.index].key;
	}

	V* query_to_value(DynTable_Query_Result result) {
		assert(result.value_is_in_table, "");
		return &entries[result.index].value;
	}


	// HELPER FUNCTIONS
	// Crashes if value already exists
	void insert(const K& key, const V& value)
	{
		auto result = query(key);
		assert(!result.value_is_in_table, "");
		insert_with_query(result, key, value);
	}

	// Returns true if value was in table
	bool remove_value(const K& key)
	{
		auto result = query(key, false);
		remove_with_query(result);
		return result.value_is_in_table;
	}

	V* find(const K& key) {
		auto result = query(key, false);
		if (!result.value_is_in_table) return nullptr;
		return query_to_value(result);
	}

	float average_sonding_count()
	{
		float sum_sonding_counts = 0.0f;
		for (int i = 0; i < entries.size; i++)
		{
			auto& entry = entries[i];
			if (entry.state != DynSet_Entry_State::OCCUPIED) continue;
			sum_sonding_counts += entry.sonding_index + 1;
		}
		return sum_sonding_counts / (float)element_count;
	}
};



// List
template<typename T>
struct List_Node
{
	T value;
	List_Node<T>* next;
	List_Node<T>* prev;
};

template<typename T>
struct List
{
	Allocator_Base* allocator;
	List_Node<T>* head;
	List_Node<T>* tail;
	int element_count;

	static List<T> create(Allocator_Base* allocator = nullptr) 
	{
		List<T> result;
		result.allocator = allocator == nullptr ? &global_system_allocator.base : allocator;
		result.head = nullptr;
		result.tail = nullptr;
		result.element_count = 0;
		return result;
	}

	void reset()
	{
		while (head != nullptr) {
			List_Node<T>* next = head->next;
			allocator->deallocate(head, sizeof(List_Node<T>));
			head = next;
		}
		tail = nullptr;
		element_count = 0;
	}

	void destroy() 
	{
		reset();
	}

	List_Node<T>* append(T item) 
	{
		element_count += 1;

		List_Node<T>* new_node = allocator->allocate<List_Node<T>>();
		new_node->value = item;

		if (head == nullptr) {
			head = new_node;
			tail = new_node;
			new_node->prev = nullptr;
			new_node->next = nullptr;
		}
		else {
			new_node->prev = tail;
			new_node->next = nullptr;
			tail->next = new_node;
			tail = new_node;
		}

		return new_node;
	}

	List_Node<T>* prepend(T item) 
	{
		element_count += 1;

		List_Node<T>* new_node = allocator->allocate<List_Node<T>>();
		new_node->value = item;

		if (head == nullptr) {
			new_node->prev = nullptr;
			new_node->next = nullptr;
			head = new_node;
			tail = new_node;
		}
		else {
			new_node->prev = nullptr;
			new_node->next = head;
			head->prev = new_node;
			head = new_node;
		}

		return new_node;
	}

	void remove_node(List_Node<T>* node) 
	{
		element_count -= 1;

		List_Node<T>* prev = node->prev;
		List_Node<T>* next = node->next;

		if (prev == nullptr) {
			head = next;
		}
		else {
			prev->next = next;
		}

		if (next == nullptr) {
			tail = prev;
		}
		else {
			next->prev = prev;
		}

		allocator->deallocate(node, sizeof(List_Node<T>));
	}

	void remove_item(T* item) {
		remove_node((List_Node<T>*) item);
	}
};

