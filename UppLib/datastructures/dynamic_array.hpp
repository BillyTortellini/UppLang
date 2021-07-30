#pragma once

#include "../utility/utils.hpp"
#include "array.hpp"
#include "../math/scalars.hpp"

template <typename T>
struct Dynamic_Array
{
    int capacity;
    int size;
    T* data;

    T& operator[](int index){
        if (index >= size || index < 0) {
            panic("Dynamic_Array out of bounds access");
        }
        return data[index];
    }
};

template <typename T>
Dynamic_Array<T> dynamic_array_create_empty(int capacity) {
    Dynamic_Array<T> result;
    result.capacity = capacity;
    result.size = 0;
    result.data = new T[capacity];
    return result;
}

template<typename T>
Dynamic_Array<T> dynamic_array_create_copy(T* data, int size) {
    Dynamic_Array<T> result = dynamic_array_create_empty<T>(size);
    memory_copy(result.data, data, size * sizeof(T));
    result.size = size;
    return result;
}

template <typename T>
void dynamic_array_destroy(Dynamic_Array<T>* array) {
    delete[] array->data;
}

template <typename T>
void dynamic_array_reserve(Dynamic_Array<T>* array, int capacity) {
    if (array->capacity < capacity) {
        T* new_data = new T[capacity];
        memory_copy(new_data, array->data, sizeof(T) * array->size);
        delete[] array->data;
        array->capacity = capacity;
        array->data = new_data;
    }
}

template <typename T>
void dynamic_array_push_back(Dynamic_Array<T>* array, T item) {
    if (array->size >= array->capacity) {
        dynamic_array_reserve(array, array->capacity * 2);
    }
    array->data[array->size] = item;
    array->size++;
}

template <typename T>
void dynamic_array_swap_remove(Dynamic_Array<T>* array, int index) {
    if (index >= array->size) {
        panic("Swap remove called with invalid index\n");
        return;
    }
    if (array->size == 1) {
        array->size = 0;
        return;
    }
    T swap = array->data[index];
    array->data[index] = array->data[array->size-1];
    array->data[array->size-1] = swap;
    array->size--;
}

template<typename T>
void dynamic_array_remove_ordered(Dynamic_Array<T>* a, int index)
{
    for (int i = index; i+1 < a->size; i++) {
        a->data[i] = a->data[i+1];
    }
    a->size -= 1;
}

template<typename T>
void dynamic_array_insert_ordered(Dynamic_Array<T>* a, T item, int index)
{
    if (index >= a->size) {
        dynamic_array_push_back(a, item);
        return;
    }
    if (a->size + 1 > a->capacity) {
        dynamic_array_reserve(a, a->capacity*2);
    }
    for (int i = a->size; i-1 >= index; i--) {
        a->data[i] = a->data[i-1];
    }
    a->data[index] = item;
    a->size++;
}


template<typename T>
void dynamic_array_remove_range_ordered(Dynamic_Array<T>* a, int start_index, int end_index)
{
    if (a->size <= 0 || end_index < start_index) { return; }
    start_index = math_clamp(start_index, 0, a->size);
    end_index = math_clamp(end_index, 0, a->size);
    int length = end_index - start_index;
    for (int i = start_index; i < a->size - length; i++) {
        a->data[i] = a->data[i + length];
    }
    a->size = a->size - length;
}

template<typename T>
void dynamic_array_rollback_to_size(Dynamic_Array<T>* a, int size) {
    a->size = size;
}

template<typename T>
struct Array;

template <typename T>
Array<T> dynamic_array_as_array(Dynamic_Array<T>* array) {
    Array<T> new_array;
    new_array.data = array->data;
    new_array.size = array->size;
    return new_array;
}

// Prototype
template<typename T>
Array<T> array_create_static(T* data, int size);

template<typename T>
Array<byte> dynamic_array_as_bytes(Dynamic_Array<T>* value) {
    return array_create_static<byte>((byte*)value->data, value->size * sizeof(T));
}

template <typename T>
void dynamic_array_reset(Dynamic_Array<T>* array) {
    array->size = 0;
}

template<typename T>
Array<T> dynamic_array_make_slice(Dynamic_Array<T>* array, int start_index, int end_index)
{
    Array<T> result = dynamic_array_as_array(array);
    return array_make_slice(result, start_index, end_index);
}
