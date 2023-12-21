#pragma once

#include "../utility/datatypes.hpp"
#include "../utility/utils.hpp"
#include "array.hpp"
#include "hashset.hpp"
#include "../utility/hash_functions.hpp"

template <typename K, typename V>
struct Hashtable_Entry
{
    K key;
    V value;
    Hashtable_Entry<K, V>* next;
    u64 hash_value;
    bool valid;
};

template <typename K, typename V>
struct Hashtable
{
    Array<Hashtable_Entry<K, V>> entries;
    int element_count;
    u64(*hash_function)(K*);
    bool(*equals_function)(K*, K*);
};

template <typename K, typename V>
struct Hashtable_Iterator
{
    Hashtable<K, V>* table;
    Hashtable_Entry<K, V>* current_entry;
    int current_entry_index;
    // Things users should access
    K* key;
    V* value;
};

template <typename K, typename V>
Hashtable_Iterator<K,V> hashtable_iterator_create(Hashtable<K,V>* table) {
    Hashtable_Iterator<K, V> result;
    result.table = table;
    for (int i = 0; i < table->entries.size; i++) {
        if (table->entries[i].valid) {
            result.current_entry = &table->entries[i];
            result.current_entry_index = i;
            result.key = &result.current_entry->key;
            result.value = &result.current_entry->value;
            return result;
        }
    }
    result.current_entry = 0;
    return result;
}

template <typename K, typename V>
bool hashtable_iterator_has_next(Hashtable_Iterator<K, V>* iterator) {
    return iterator->current_entry != 0;
}

template <typename K, typename V>
void hashtable_iterator_next(Hashtable_Iterator<K, V>* iterator) 
{
    if (iterator->current_entry == 0) {
        return;
    }

    if (iterator->current_entry->next != 0) {
        iterator->current_entry = iterator->current_entry->next;
        iterator->key = &iterator->current_entry->key;
        iterator->value = &iterator->current_entry->value;
        return;
    }
    for (int i = iterator->current_entry_index + 1; i < iterator->table->entries.size; i++) {
        Hashtable<K, V>* table = iterator->table;
        if (table->entries[i].valid) {
            iterator->current_entry = &table->entries[i];
            iterator->current_entry_index = i;
            iterator->key = &iterator->current_entry->key;
            iterator->value = &iterator->current_entry->value;
            return;
        }
    }
    
    iterator->current_entry = 0;
    return;
}

template <typename K, typename V>
Hashtable<K, V> hashtable_create_empty(int capacity, u64(*hash_function)(K*), bool(*equals_function)(K*, K*)) 
{
    Hashtable<K, V> result;
    result.element_count = 0;
    result.entries = array_create_empty<Hashtable_Entry<K, V>>(primes_find_next_suitable_for_set_size(capacity));
    for (int i = 0; i < result.entries.size; i++) {
        result.entries[i].valid = false;
        result.entries[i].next = 0;
    }
    result.hash_function = hash_function;
    result.equals_function = equals_function;
    return result;
}

template <typename K, typename V>
Hashtable<K, V> hashtable_create_pointer_empty(int capacity) 
{
    return hashtable_create_empty<K, V>(
        capacity,
        [](K* key) -> u64 {return hash_pointer(*key); },
        [](K* a, K* b) -> bool { return (*a) == (*b); }
    );
}

template <typename K, typename V>
void hashtable_for_each(Hashtable<K, V>* table, void (*function)(K*, V*)) {
    Hashtable_Iterator<K, V> it = hashtable_iterator_create(table);
    while (hashtable_iterator_has_next(&it)) {
        function(it.key, it.value);
        hashtable_iterator_next(&it);
    }
}

template <typename K, typename V>
void hashtable_reset(Hashtable<K, V>* table)
{
    table->element_count = 0;
    for (int i = 0; i < table->entries.size; i++) {
        Hashtable_Entry<K,V>* entry = &table->entries[i];
        entry->valid = false;
        if (entry->next != 0) {
            Hashtable_Entry<K, V>* next = entry->next;
            entry->next = 0;
            while (next != 0) {
                Hashtable_Entry<K, V>* next_next = next->next;
                delete next;
                next = next_next;
            }
        }
    }
}

template <typename K, typename V>
void hashtable_destroy(Hashtable<K, V>* table) 
{
    // Deleate all items in entry lists
    for (int i = 0; i < table->entries.size; i++) {
        Hashtable_Entry<K,V>* entry = &table->entries[i];
        if (entry->next != 0) {
            entry = entry->next;
            while (entry != 0) {
                Hashtable_Entry<K, V>* next = entry->next;
                delete entry;
                entry = next;
            }
        }
    }
    array_destroy(&table->entries);
}

template <typename K, typename V>
V* hashtable_find_element(Hashtable<K, V>* table, K key)
{
    u64 hash = table->hash_function(&key);
    int entry_index = hash % table->entries.size;
    Hashtable_Entry<K,V>* entry = &table->entries.data[entry_index];
    while (entry != 0 && entry->valid) {
        if (entry->hash_value == hash && table->equals_function(&entry->key, &key)) {
            return &entry->value;
        }
        entry = entry->next;
    }
    return 0;
}

template <typename K, typename V>
struct Key_Value_Reference
{
    K* key;
    V* value;
};

template <typename K, typename V>
Optional<Key_Value_Reference<K, V>> hashtable_find_element_key_and_value(Hashtable<K, V>* table, K key)
{
    u64 hash = table->hash_function(&key);
    int entry_index = hash % table->entries.size;
    Hashtable_Entry<K,V>* entry = &table->entries.data[entry_index];
    while (entry != 0 && entry->valid) {
        if (entry->hash_value == hash && table->equals_function(&entry->key, &key)) {
            Key_Value_Reference<K, V> ref;
            ref.key = &entry->key;
            ref.value = &entry->value;
            return optional_make_success(ref);;
        }
        entry = entry->next;
    }
    return optional_make_failure<Key_Value_Reference<K, V>>();
}

template <typename K, typename V>
void hashtable_reserve(Hashtable<K, V>* table, int capacity) 
{
    if (table->entries.size > capacity) {
        return;
    }
    int new_capacity = primes_find_next_suitable_for_set_size(capacity);
    Hashtable<K, V> new_table = hashtable_create_empty<K, V>(new_capacity, table->hash_function, table->equals_function);
    Hashtable_Iterator<K, V> iterator = hashtable_iterator_create(table);
    while (hashtable_iterator_has_next(&iterator)) {
        hashtable_insert_element(&new_table, iterator.current_entry->key, iterator.current_entry->value);
        hashtable_iterator_next(&iterator);
    }
    // Destroy old table data
    hashtable_destroy(table);
    *table = new_table;
}

// Returns true if element was inserted, else false (If key already exists)
template <typename K, typename V>
bool hashtable_insert_element(Hashtable<K, V>* table, K key, V value)
{
    if ((float)(table->element_count + 1) / table->entries.size > HASHSET_RESIZE_PERCENTAGE) {
        hashtable_reserve(table, table->element_count + 1);
    }

    u64 hash = table->hash_function(&key);
    int entry_index = (hash % table->entries.size);
    Hashtable_Entry<K, V>* entry = & (table->entries.data[entry_index]);
    if (!entry->valid) {
        entry->valid = true;
        entry->key = key;
        entry->value = value;
        entry->hash_value = hash;
        entry->next = 0;
        table->element_count++;
        return true;
    }

    while (true) {
        if (entry->hash_value == hash && table->equals_function(&entry->key, &key)) {
            return false;
        }
        if (entry->next == 0) { // Insert element as next
            Hashtable_Entry<K, V>* next = new Hashtable_Entry<K, V>();
            next->valid = true;
            next->key = key;
            next->value = value;
            next->hash_value = hash;
            next->next = 0;
            entry->next = next;
            table->element_count++;
            return true;
        }
        entry = entry->next;
    }

    return false;
}

// Returns true if the element was removed, otherwise false (Value was not in set)
template <typename K, typename V>
bool hashtable_remove_element(Hashtable<K, V>* table, K key)
{
    u64 hash = table->hash_function(&key);
    int entry_index = (hash % table->entries.size);
    Hashtable_Entry<K, V>* entry = &(table->entries.data[entry_index]);
    if (!entry->valid) {
        return false;
    }

    Hashtable_Entry<K, V>* next = entry->next;
    if (entry->hash_value == hash && table->equals_function(&entry->key, &key)) {
        if (next != 0) {
            *entry = *next;
            delete next;
            assert(entry->valid, "Next needs to be valid");
        }
        else {
            entry->valid = false;
        }
        table->element_count -= 1;
        return true;
    }

    while (next != 0)
    {
        if (next->hash_value == hash && table->equals_function(&next->key, &key)) {
            entry->next = next->next;
            delete next;
            table->element_count -= 1;
            return true;
        }
        else {
            entry = next;
            next = next->next;
        }
    }

    return false;
}

