#pragma once

#include "../utility/datatypes.hpp"
#include "array.hpp"
#include "../utility/hash_functions.hpp"
#include "../math/scalars.hpp"

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
	void Arena::rewind_to_address(void* pointer);

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

	void rollback_to_size(int new_size)
	{
		assert((usize)new_size <= size, "Can only make array smaller");
		size = (usize)new_size;
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
    usize element_count;
    u64(*hash_function)(K*);
    bool(*equals_function)(K*, K*);

	static DynTable<K, V> create(Arena* arena, u64(*hash_fn)(K*), bool(*equals_fn)(K*, K*), usize expected_element_count = 0) 
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

	static DynTable<K, V> create_pointer(Arena* arena, usize expected_element_count = 0) 
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

	void reserve(usize expected_element_count)
	{
		usize min_size = (usize)((float)expected_element_count / DYNSET_MAX_LOAD_FACTOR) + 1;
		if (entries.size >= min_size) return;

		// Calculate new size
		min_size = math_maximum((entries.size * 3) / 2 + 1, (int)min_size);
		usize new_size = find_next_suitable_prime_hashset_size(min_size);
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
			insert_with_hash(old_entry.key, old_entry.value, old_entry.hash);
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

	bool insert_with_hash(K key, V value, u64 key_hash)
	{
		reserve(element_count + 1);
		for (int sonding_index = 0; sonding_index < entries.size; sonding_index++)
		{
			auto& entry = entries[hash_to_entry_index(key_hash, sonding_index)];

			bool entry_is_free = entry.state != DynSet_Entry_State::OCCUPIED;
			if (!entry_is_free)
			{
				// Check if element is already inserted (No duplicate values)
				if (entry.hash == key_hash && equals_function(&entry.key, &key)) {
					return false;
				}

				// Check if the entry value can be moved (Improvement by Brent paper)
				auto& other_entry = entries[hash_to_entry_index(entry.hash, entry.sonding_index + 1)];
				if (other_entry.state != DynSet_Entry_State::OCCUPIED)
				{
					// Move current entry to other_entry
					other_entry = entry;
					other_entry.sonding_index += 1;
					entry_is_free = true;
				}
			}
			
			if (entry_is_free) 
			{
				entry.state = DynSet_Entry_State::OCCUPIED;
				entry.key = key;
				entry.value = value;
				entry.sonding_index = sonding_index;
				entry.hash = key_hash;
				element_count += 1;
				return true;
			}
		}

		panic("Table is full, should not happen after reserve!");
		return false;
	}

	// Returns true if value was inserted, false if it already exists?
	bool insert(K key, V value) {
		return insert_with_hash(key, value, hash_function(&key));
	}

	V* find(K& key)
	{
		u64 key_hash = hash_function(&key);
		for (int sonding_index = 0; sonding_index < entries.size; sonding_index++)
		{
			auto& entry = entries[hash_to_entry_index(key_hash, sonding_index)];
			if (entry.state == DynSet_Entry_State::FREE) {
				return nullptr;
			}
			else if (entry.state == DynSet_Entry_State::OCCUPIED) {
				if (entry.hash == key_hash && equals_function(&entry.key, &key)) {
					return &entry.value;
				}
			}
		}
		return nullptr;
	}

	bool contains(K& key)
	{
		return find(key) != nullptr;
	}

	// Returns true if the operation succeeded, otherwise false
	bool remove(K& key)
	{
		u64 key_hash = hash_function(&key);
		for (int sonding_index = 0; sonding_index < entries.size; sonding_index++)
		{
			auto& entry = entries[hash_to_entry_index(key_hash, sonding_index)];

			if (entry.state == DynSet_Entry_State::FREE) {
				return false;
			}
			else if (entry.state == DynSet_Entry_State::OCCUPIED) {
				if (entry.hash == key_hash && equals_function(&entry.key, &key)) {
					entry.state = DynSet_Entry_State::FREE_AGAIN;
					element_count -= 1;
					return true;
				}
			}
		}
		return false;
	}
};

