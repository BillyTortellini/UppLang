#pragma once

#include <initializer_list>
#include "../utility/utils.hpp"
#include "../math/scalars.hpp"
#include <algorithm>

template<typename T>
struct Array
{
    T* data;
    int size;

    T& operator[](int index){
        if (index >= size || index < 0) {
            panic("Array out of bounds access");
        }
        return data[index];
    }
};

template<typename T>
Array<T> array_create(int size) {
    Array<T> result;
    if (size > 0) {
        result.data = new T[size];
    }
    else {
        result.data = 0;
    }
    result.size = size;
    return result;
}

template<typename T>
Array<T> array_create_from_list(std::initializer_list<T> list) {
    Array<T> result = array_create<T>((int)list.size());
    int i = 0;
    for (auto item : list) {
        result[i] = item;
        i++;
    }
    return result;
}

template<typename T>
Array<T> array_create_copy(T* data, int size) {
    Array<T> result = array_create<T>(size);
    memory_copy(result.data, data, size * sizeof(T));
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
Array<byte> array_create_static_as_bytes(T* data, int size) {
    return array_as_bytes(&array_create_static(data, size));
}

template<typename T>
Array<byte> array_as_bytes(Array<T>* value) {
    return array_create_static<byte>((byte*)value->data, value->size * sizeof(T));
}

template<typename T>
void array_destroy(Array<T>* array) {
    if (array->size > 0) {
        delete[] array->data;
    }
}

template<typename T>
Array<T> array_make_slice(Array<T>* array, int start_index, int end_index)
{
    end_index = math_clamp(end_index, 0, math_maximum(0, array->size));
    start_index = math_clamp(start_index, 0, end_index);
    Array<T> result;
    result.data = &array->data[start_index];
    result.size = end_index - start_index;
    return result;
}

template<typename T>
void array_bubble_sort(Array<T> array, bool (*in_order_fn)(T* a, T* b))
{
    for (int i = 0; i < array.size; i++)
    {
        for (int j = i + 1; j < array.size; j++) {
            if (!in_order_fn(&array[i], &array[j])) {
                T swap = array[i];
                array[i] = array[j];
                array[j] = swap;
            }
        }
    }
}

template<typename T, typename Comparator>
void array_sort(Array<T> array, Comparator comparator) {
    std::sort(&array.data[0], &array.data[array.size], comparator);
}
