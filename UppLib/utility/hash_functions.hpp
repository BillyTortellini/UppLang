#pragma once

#include "datatypes.hpp"
#include "../datastructures/string.hpp"
#include "../datastructures/array.hpp"

u64 hash_memory(Array<byte> memory);
u64 hash_string(String* string);
u64 hash_i32(i32* i);
u64 hash_i64(i64* i);
u64 hash_u64(u64* i);
u64 hash_pointer(void* ptr);
u64 hash_combine(u64 a, u64 b);

bool equals_i32(i32* a, i32* b);
bool equals_i64(i64* a, i64* b);
bool equals_u64(u64* a, u64* b);
bool equals_pointer(void** a, void** b);
