#pragma once

#include "intermediate_code.hpp"

struct C_Generator
{
    Intermediate_Generator* im_generator;
    String output_string;
    int current_function_index;
};

C_Generator c_generator_create();
void c_generator_destroy(C_Generator* generator);
void c_generator_generate(C_Generator* generator, Intermediate_Generator* im_generator);