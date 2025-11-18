#pragma once

#include "../utility/datatypes.hpp"
#include "array.hpp"
#include "../utility/hash_functions.hpp"
#include "../math/scalars.hpp"

struct Arena_Checkpoint
{
	void* data;
};

struct Arena_Buffer
{
	// Note: every arena buffer starts with this struct, which forms a linked list
	//		to previously allocated buffers
	void* data;
	usize capacity;
};

struct Arena
{
	Arena_Buffer buffer;
	void* next;

	static Arena create(usize capacity = 0); 
	void destroy();

	void* allocate_raw(usize size, u32 alignment);
	bool resize(void* memory, usize old_size, usize new_size);
	void reset(bool keep_largest_buffer = false);

	Arena_Checkpoint make_checkpoint();
	void rewind_to_checkpoint(Arena_Checkpoint checkpoint);

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
};

// Note: Alignment of free-list is always alignof(usize), so this could cause problems for sse types...
struct Free_List
{
	Arena* arena;
	usize element_size;
	void* next; // List of allocations

	static Free_List create(Arena* arena, usize element_size);
	void* allocate_raw(usize size);
	void deallocate_raw(void* data);

	template<typename T> 
	T* allocate() {	return (T*)allocate_raw(sizeof(T), alignof(T)); } 

	template<typename T> 
	void deallocate(T* data) { deallocate_raw((void*)data); }
};

template<typename T>
struct DynArray
{
	Arena* arena;
	Array<T> buffer;
	usize size;

	static DynArray<T> create(Arena* arena, usize capacity = 0)
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

	void reserve(usize requested_size) 
	{
		if (buffer.size >= requested_size) return;

		// Figure out new size
		usize new_size = math_maximum(requested_size, (usize) (buffer.size * 3) / 2 + 1);

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

	void remove_range_ordered(int start_index, int end_index)
	{
	    if (size <= 0 || end_index < start_index) { return; }
	    start_index = math_clamp(start_index, 0, (int)size);
	    end_index = math_clamp(end_index, 0, (int)size);
	    int length = end_index - start_index;
	    for (int i = start_index; i < size - length; i++) {
			buffer.data[i] = buffer.data[i + length];
	    }
	    size = size - length;
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


u64 find_next_suitable_prime_hashset_size(u64 value);

const float DYNSET_MAX_LOAD_FACTOR = 0.7f;

template<typename T>
struct DynSet
{
	Arena* arena;
    Array<DynSet_Entry<T>> entries;
    usize element_count;
    u64(*hash_function)(T*);
    bool(*equals_function)(T*, T*);

	static DynSet<T> create(Arena* arena, u64(*hash_fn)(T*), bool(*equals_fn)(T*, T*), usize expected_element_count = 0) 
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

	static DynSet<T> create_pointer(Arena* arena, usize expected_element_count = 0) 
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

	void reserve(usize expected_element_count)
	{
		usize min_size = (usize)((float)expected_element_count / DYNSET_MAX_LOAD_FACTOR) + 1;
		if (entries.size >= min_size) return;
		min_size = math_maximum((entries.size * 3) / 2 + 1, (int)min_size);
		usize new_size = find_next_suitable_prime_hashset_size(min_size);
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
			memory_copy(entries.data, old_entries.data, entries.size * sizeof(DynSet_Entry<T>));
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
			insert_internal(old_entry.value, old_entry.hash);
		}
		arena->rewind_to_checkpoint(checkpoint);
	}

	int hash_to_entry_index(u64 hash_value, int sonding_index)
	{
		u64 sonding_increment = hash_combine(hash_value, 0xFE57D3AC94BF1E27);
		if (sonding_increment % entries.size == 0) { // Must not be null, otherwise we wouldn't reach all element entries
			sonding_increment = 87178291199ull; // This is a large prime not on the list of hashtable-sizes
		}
		// NOTE: We modulo before calculation because integer overflow
		//	will result in invalid calculations (Because the modulo will change in unpredictable ways)
		u64 hash_pos = hash_value % entries.size;
		sonding_increment = sonding_increment % entries.size;
		return (hash_pos + sonding_index * sonding_increment) % entries.size;
	}

	bool insert_internal(T& value, u64 value_hash)
	{
		int sonding_index = 0;
		while (true)
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

			// Otherwise insert at next sonding index
			sonding_index += 1;
		}
	}

	// Returns true if value was inserted, false if it already exists?
	bool insert(T& value)
	{
		reserve(element_count + 1);
		u64 value_hash = hash_function(&value);
		return insert_internal(value, value_hash);
	}

	bool contains(T& value)
	{
		int sonding_index = 0;
		u64 value_hash = hash_function(&value);
		while (true)
		{
			auto& entry = entries[hash_to_entry_index(value_hash, sonding_index)];

			if (entry.state == DynSet_Entry_State::FREE) {
				return false;
			}
			else if (entry.state == DynSet_Entry_State::OCCUPIED) {
				if (entry.hash == value_hash && equals_function(&entry.value, &value)) {
					return true;
				}
			}

			// Otherwise search at next sonding-index
			sonding_index += 1;
		}
	}

	// Returns true if the operation succeeded, otherwise false
	bool remove(T& value)
	{
		int sonding_index = 0;
		u64 value_hash = hash_function(&value);
		while (true)
		{
			auto& entry = entries[hash_to_entry_index(value_hash, sonding_index)];

			if (entry.state == DynSet_Entry_State::FREE) {
				return false;
			}
			else if (entry.state == DynSet_Entry_State::OCCUPIED) {
				if (entry.hash == value_hash && equals_function(&entry.value, &value)) {
					entry.state = DynSet_Entry_State::FREE_AGAIN;
					return true;
				}
			}

			// Otherwise search at next sonding-index
			sonding_index += 1;
		}
	}
};
