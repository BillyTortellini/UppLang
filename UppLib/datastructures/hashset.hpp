#pragma once

#include "../utility/datatypes.hpp"
#include "array.hpp"

const float HASHSET_RESIZE_PERCENTAGE = 0.8f;

int primes_find_next_suitable_for_set_size(int capacity);

template <typename T>
struct Hashset_Entry
{
    T value;
    Hashset_Entry<T>* next;
    bool valid;
};

template <typename T>
struct Hashset
{
    Array<Hashset_Entry<T>> entries;
    int element_count;
    u64(*hash_function)(T*);
    bool(*equals_function)(T*, T*);
};

template <typename T>
struct Hashset_Iterator
{
    Hashset<T>* set;
    Hashset_Entry<T>* current_entry;
    int current_entry_index;
    // Things users should access
    T* value;
};

template <typename T>
Hashset_Iterator<T> hashset_iterator_create(Hashset<T>* set) {
    Hashset_Iterator<T> result;
    result.set = set;
    for (int i = 0; i < set->entries.size; i++) {
        if (set->entries[i].valid) {
            result.current_entry = &set->entries[i];
            result.current_entry_index = i;
            result.value = &result.current_entry->value;
            return result;
        }
    }
    result.current_entry = 0;
    result.value = 0;
    return result;
}

template <typename T>
bool hashset_iterator_has_next(Hashset_Iterator<T>* iterator) {
    return iterator->current_entry != 0;
}

template <typename T>
void hashset_iterator_next(Hashset_Iterator<T>* iterator) 
{
    if (iterator->current_entry == 0) {
        return;
    }

    if (iterator->current_entry->next != 0) {
        iterator->current_entry = iterator->current_entry->next;
        iterator->value = &iterator->current_entry->value;
        return;
    }
    for (int i = iterator->current_entry_index + 1; i < iterator->set->entries.size; i++) {
        Hashset<T>* set = iterator->set;
        if (set->entries[i].valid) {
            iterator->current_entry = &set->entries[i];
            iterator->current_entry_index = i;
            iterator->value = &iterator->current_entry->value;
            return;
        }
    }
    
    iterator->current_entry = 0;
    return;
}

template <typename T>
Hashset<T> hashset_create_empty(int capacity, u64(*hash_function)(T*), bool(*equals_function)(T*, T*)) 
{
    Hashset<T> result;
    result.element_count = 0;
    result.entries = array_create_empty<Hashset_Entry<T>>(primes_find_next_suitable_for_set_size(capacity));
    for (int i = 0; i < result.entries.size; i++) {
        result.entries[i].valid = false;
        result.entries[i].next = 0;
    }
    result.hash_function = hash_function;
    result.equals_function = equals_function;
    return result;
}

template <typename K>
Hashset<K> hashset_create_pointer_empty(int capacity) 
{
    return hashset_create_empty<K>(
        capacity,
        [](K* key) -> u64 {return hash_pointer(*key); },
        [](K* a, K* b) -> bool { return (*a) == (*b); }
    );
}

template <typename T>
void hashset_reset(Hashset<T>* set)
{
    for (int i = 0; i < set->entries.size; i++) {
        Hashset_Entry<T>* entry = &set->entries[i];
        entry->valid = false;
        if (entry->next != 0) {
            Hashset_Entry<T>* next = entry->next;
            entry->next = 0;
            while (next != 0) {
                Hashset_Entry<T>* next_next = next->next;
                delete next;
                next = next_next;
            }
        }
    }
}

template <typename T>
void hashset_destroy(Hashset<T>* set) 
{
    // Deleate all items in entry lists
    for (int i = 0; i < set->entries.size; i++) {
        Hashset_Entry<T>* entry = &set->entries[i];
        if (entry->next != 0) {
            entry = entry->next;
            while (entry != 0) {
                Hashset_Entry<T>* next = entry->next;
                delete entry;
                entry = next;
            }
        }
    }
    array_destroy(&set->entries);
}

template <typename T>
void hashset_reserve(Hashset<T>* set, int capacity);

template <typename T>
bool hashset_contains(Hashset<T>* set, T elem)
{
    u64 hash = set->hash_function(&elem);
    int entry_index = hash % set->entries.size;
    Hashset_Entry<T>* entry = &set->entries.data[entry_index];
    if (!entry->valid) return false;
    while (entry != 0) {
        if (set->equals_function(&entry->value, &elem)) {
            return true;
        }
        entry = entry->next;
    }
    return false;
}

// Returns true if element was inserted, else false (If value already exists)
template <typename T>
bool hashset_insert_element(Hashset<T>* set, T value)
{
    if ((float)(set->element_count + 1) / set->entries.size > HASHSET_RESIZE_PERCENTAGE) {
        hashset_reserve(set, set->element_count + 1);
    }

    u64 hash = set->hash_function(&value);
    int entry_index = (hash % set->entries.size);
    Hashset_Entry<T>* entry = &(set->entries.data[entry_index]);
    if (!entry->valid) {
        entry->valid = true;
        entry->value = value;
        entry->next = 0;
        set->element_count++;
        return true;
    }

    while (true) {
        if (set->equals_function(&entry->value, &value)) {
            return false;
        }
        if (entry->next == 0) { // Insert element as next
            Hashset_Entry<T>* next = new Hashset_Entry<T>();
            entry->next = next;
            next->valid = true;
            next->value = value;
            next->next = 0;
            set->element_count++;
            return true;
        }
        entry = entry->next;
    }

    return false;
}

template <typename T>
void hashset_reserve(Hashset<T>* set, int capacity) 
{
    if (set->entries.size > capacity) {
        return;
    }
    int new_capacity = primes_find_next_suitable_for_set_size(capacity);
    Hashset<T> new_set = hashset_create_empty<T>(new_capacity, set->hash_function, set->equals_function);
    Hashset_Iterator<T> iterator = hashset_iterator_create(set);
    while (hashset_iterator_has_next(&iterator)) {
        hashset_insert_element<T>(&new_set, *iterator.value);
        hashset_iterator_next(&iterator);
    }
    // Destroy old set data
    hashset_destroy(set);
    *set = new_set;
}

// Returns true if the element was removed, otherwise false (Value was not in set)
template <typename T>
bool hashset_remove_element(Hashset<T>* set, T value)
{
    u64 hash = set->hash_function(&value);
    int entry_index = (hash % set->entries.size);
    Hashset_Entry<T>* entry = &(set->entries.data[entry_index]);
    if (!entry->valid) {
        return false;
    }

    if (set->equals_function(&entry->value, &value)) {
        entry->valid = false;
        if (entry->next != 0) {
            set->entries[entry_index] = entry->next;
            set->entries[entry_index].valid = true;
            delete entry->next;
        }
        return true;
    }

    Hashset_Entry<T>* next = entry->next;
    while (next != 0)
    {
        if (set->equals_function(&next->value, &value)) {
            *entry = *next;
            delete next;
            entry->valid = true;
            return true;
        }
        else {
            entry = next;
            next = next->next;
        }
    }

    return false;
}

