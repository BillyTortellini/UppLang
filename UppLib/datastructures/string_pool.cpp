#include "string_pool.hpp"

#include "string.hpp"

String_Pool string_pool_create(int expected_string_count, int expected_capacity) {
    String_Pool result;
    result.expected_capacity = expected_capacity;
    result.pool = dynamic_array_create<String*>(expected_string_count);
    for (int i = 0; i < expected_string_count; i++) {
        result.pool[i] = new String();
        *result.pool[i] = string_create_empty(expected_capacity);
    }
    result.in_use_count = 0;
    return result;
}

void string_pool_reset(String_Pool* pool) {
    pool->in_use_count = 0;
    for (int i = 0; i < pool->pool.size; i++) {
        string_reset(pool->pool[i]);
    }
}

String* string_pool_get_string(String_Pool* pool) {
    if (pool->in_use_count < pool->pool.size) {
        pool->in_use_count++;
        return pool->pool[pool->in_use_count - 1];
    }
    else {
        String* string = new String();
        *string = string_create_empty(pool->expected_capacity);
        dynamic_array_push_back(&pool->pool, string);
        return string;
    }
}

void string_pool_destroy(String_Pool* pool) {
    for (int i = 0; i < pool->pool.size; i++) {
        string_destroy(pool->pool[i]);
        delete pool->pool[i];
    }
    dynamic_array_destroy(&pool->pool);
}