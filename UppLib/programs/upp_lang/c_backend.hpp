#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"

struct Compiler;
struct Intermediate_Generator;

struct C_Generator
{
    Compiler* compiler;
    Intermediate_Generator* im_generator;
    String output_string;
    DynamicArray<int> array_index_stack;
    int current_function_index;
};

C_Generator c_generator_create();
void c_generator_destroy(C_Generator* generator);
void c_generator_generate(C_Generator* generator, Compiler* compiler);