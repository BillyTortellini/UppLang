#include "hardcoded_functions.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <ctime>


Upp_String upp_create_static_string(u8* data, i32 size) {
	Upp_String result; result.character_buffer.data = data; result.character_buffer.size = 0; result.size = size; return result;
}

void print_string(Upp_String str)
{
	printf("%.*s", str.size, str.character_buffer.data);
	return;
}

void print_i32(i32 x) 
{
	printf("%d", x);
	return;
}

void print_f32(f32 x) {
	printf("%3.2f", x);
	return;
}

void print_bool(bool x) {
	printf("%s", x ? "TRUE" : "FALSE" );
	return;
}

void print_line() {
	printf("\n");
	return;
}

i32 read_i32() 
{
	printf("Please input an i32: ");
    i32 num;
    std::cin >> num;
    if (std::cin.fail()) {
        num = 0;
    }
    std::cin.ignore(10000, '\n');
    std::cin.clear();
	return num;
}

f32 read_f32() 
{
	printf("Please input an f32: ");
    f32 num;
    std::cin >> num;
    if (std::cin.fail()) {
        num = 0;
    }
    std::cin.ignore(10000, '\n');
    std::cin.clear();
	return num;
}

u8 read_bool() 
{
	printf("Please input an bool (As int): ");
    i32 num;
    std::cin >> num;
    if (std::cin.fail()) {
        num = 0;
    }
    std::cin.ignore(10000, '\n');
    std::cin.clear();
	return num == 0 ? 0 : 1;
}

uint32 g_xor_shift;
i32 random_i32() {
    uint32 a = g_xor_shift;
    a ^= a << 13;
    a ^= a >> 17;
    a ^= a << 5;
    g_xor_shift = a;
    return (i32)a;
}

void random_initialize() {
    uint32 a = 0;
    // Initialize with current time
    while (a == 0) {
        a = (uint32) time(NULL);
    }
    g_xor_shift = a;
    // Run for some iterators to get the generator "warm"
    for (int i = 0; i < 10000; i++) {
        random_i32();
    }
}

void* malloc_size_i32(i32 x) {
	return malloc(x);
}
void free_pointer(void* ptr) {
	free(ptr);
	return;
}