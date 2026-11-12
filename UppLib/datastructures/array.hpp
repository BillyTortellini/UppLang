#pragma once

#include <initializer_list>
#include "../utility/utils.hpp"
#include "../math/scalars.hpp"
#include <algorithm>

template<typename T>
struct Array
{
    T* data;
    uint size;

    Array<T> get_slice(int start, int end) { // End not included
        assert(start >= 0 && start <= end && end <= size, "");
        Array<T> result;
        result.data = &data[start];
        result.size = end - start;
        return result;
    }

    T& operator[](uint index){
        assert(index < size, "Array out of bounds access");
        return data[index];
    }

    T& at(uint index) {
        assert(index >= 0 && (uint)index < size, "Array out of bounds access");
        return data[index];
    }

    Array<byte> to_bytes() {
        Array<byte> result;
        result.data = (byte*)this->data;
        result.size = this->size * sizeof(T);
        return result;
    }
};

template<typename T>
Array<T> array_make(T* data, uint size) {
    Array<T> result;
    result.data = data;
    result.size = size;
    return result;
}

template<typename T>
Array<T> array_make_empty() {
    return array_make<T>(nullptr, (uint)0);
}

template<typename T>
void array_bubble_sort(Array<T> array, bool (*in_order_fn)(T* a, T* b))
{
    for (uint i = 0; i < array.size; i++)
    {
        for (uint j = i + 1; j < array.size; j++) {
            if (!in_order_fn(&array[i], &array[j])) {
                T swap = array[i];
                array[i] = array[j];
                array[j] = swap;
            }
        }
    }
}

// Note: this comparator is not used directly, but we need it for std::sort (see array_sort)
template<typename T>
struct Array_Element_Comparator
{
    void* userdata;
    bool(*in_order_fn)(T* a, T* b, void* userdata);

    bool operator()(T& a, T& b) {
        return in_order_fn(&a, &b, userdata);
    }
};

template<typename T>
void array_sort(Array<T> array, bool(*in_order_fn)(T* a, T* b, void* userdata), void* userdata)
{
    Array_Element_Comparator<T> comparator;
    comparator.userdata = userdata;
    comparator.in_order_fn = in_order_fn;
    std::sort(&array.data[0], &array.data[array.size], comparator);
}

template<typename T>
void array_stable_sort(Array<T> array, bool(*in_order_fn)(T* a, T* b, void* userdata), void* userdata)
{
    Array_Element_Comparator<T> comparator;
    comparator.userdata = userdata;
    comparator.in_order_fn = in_order_fn;
    std::stable_sort(&array.data[0], &array.data[array.size], comparator);
}

template<typename T>
Array<byte> object_as_bytes(T* data) {
    return array_make((byte*)data, (uint) sizeof(T));
}


// Note: These functions should not be used anymore, use Arenas instead.
//      I just keep them around for old code
template<typename T>
Array<T> array_new(uint size) {
    return array_make<T>(new T[size], (uint)size);
}

template<typename T>
Array<T> array_new_copy(T* data, uint size) {
    Array<T> array = array_make<T>(new T[size], size);
    memory_copy(array.data, data, sizeof(T) * size);
    return array;
}

template<typename T>
void array_delete(Array<T>* array) {
    delete[] array->data;
    array->data = nullptr;
}

