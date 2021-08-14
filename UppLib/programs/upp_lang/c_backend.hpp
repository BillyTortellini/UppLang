#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "../../datastructures/hashtable.hpp"

struct Compiler;
struct IR_Program;
struct Type_Signature;
struct IR_Function;
struct IR_Code_Block;

struct C_Generator
{
    Compiler* compiler;
    IR_Program* program;
    String section_string_data;
    String section_struct_prototypes;
    String section_struct_implementations;
    String section_type_declarations; // Array types, strings, ...
    String section_function_prototypes;
    String section_globals;
    String section_function_implementations;
    int name_counter;

    Hashtable<Type_Signature*, String> translation_type_to_name;
    Hashtable<IR_Function*, String> translation_function_to_name;
    Hashtable<IR_Code_Block*, String> translation_code_block_to_name;
    Hashtable<int, String> translation_string_data_to_name;
    Dynamic_Array<int> array_index_stack;
    int current_function_index;
};

C_Generator c_generator_create();
void c_generator_destroy(C_Generator* generator);
void c_generator_generate(C_Generator* generator, Compiler* compiler);