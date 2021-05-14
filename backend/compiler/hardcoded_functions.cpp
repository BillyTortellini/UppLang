#include "hardcoded_functions.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>

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

i32 random_i32()
{
	return 69;
}

void* malloc_size_i32(i32 x) {
	return malloc(x);
}
void free_pointer(void* ptr) {
	free(ptr);
	return;
}