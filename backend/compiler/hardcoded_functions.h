#pragma once

#include "datatypes.h"

typedef int TEMPLATE_TYPE;
struct Unsized_Array_U8 {u8* data; i32 size; i32 padding;};
struct Upp_String {Unsized_Array_U8 character_buffer; i32 size;};
Upp_String upp_create_static_string(u8* data, i32 size);

void print_i32(i32 x);
void print_f32(f32 x);
void print_bool(bool x);
void print_line();
void print_string(Upp_String str);
i32 read_i32();
f32 read_f32();
i32 random_i32();
void* malloc_size_i32(i32 x);
void free_pointer(void* ptr);
void random_initialize();


