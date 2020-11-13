#pragma once

#include "../utility/utils.hpp"
#include "dynamic_array.hpp"

template<typename T>
struct Array
{
    T* data;
    int size;

    T& operator[](int index){
        return data[index];
    }
};

template<typename T>
Array<T> array_create_empty(int size) {
    Array<T> result;
    if (size > 0) {
        result.data = new T[size];
    }
    result.size = size;
    return result;
}

template<typename T>
Array<T> array_create_copy(T* data, int size) {
    Array<T> result = array_create_empty<T>(size);
    memory_copy(result.data, data, size * sizeof(T));
    return result;
}

template<typename T>
struct DynamicArray;

template<typename T>
DynamicArray<T> array_to_dynamic_array(Array<T>* value) {
    DynamicArray<T> result;
    result.data = value->data;
    result.size = value->size;
    result.capacity = value->size;
    return result;
}

// Static arrays should not be called destroy upon
template<typename T>
Array<T> array_create_static(T* data, int size) {
    Array<T> result;
    result.data = data;
    result.size = size;
    return result;
}

template<typename T>
Array<byte> array_to_bytes(Array<T>* value) {
    return array_create_static<byte>((byte*)value->data, value->size * sizeof(T));
}

template<typename T>
void array_destroy(Array<T>* array) {
    if (array->size > 0) {
        delete[] array->data;
    }
}
