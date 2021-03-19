#pragma once

#include "dynamic_array.hpp"

struct String;

struct String_Pool {
    DynamicArray<String*> pool;
    int in_use_count;
    int expected_capacity;
};

String_Pool string_pool_create(int expected_string_count, int expected_capacity);
void string_pool_destroy(String_Pool* pool);
void string_pool_reset(String_Pool* pool);
String* string_pool_get_string(String_Pool* pool);
