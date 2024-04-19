#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/hashset.hpp"
#include "compiler_misc.hpp"
#include "constant_pool.hpp"

struct C_Compiler
{
    Dynamic_Array<String> source_files;
    Dynamic_Array<String> lib_files;
    bool initialized;
    bool last_compile_successfull;
};

C_Compiler c_compiler_create();
void c_compiler_destroy(C_Compiler* compiler);

void c_compiler_add_source_file(C_Compiler* compiler, String file_name);
void c_compiler_add_lib_file(C_Compiler* compiler, String file_name);
void c_compiler_compile(C_Compiler* compiler);
Exit_Code c_compiler_execute(C_Compiler* compiler);



struct Compiler;
struct IR_Program;
struct Datatype;
struct IR_Function;
struct IR_Code_Block;
struct Upp_Constant;

struct C_Type_Definition_Dependency
{
    Dynamic_Array<int> outgoing_dependencies;
    Dynamic_Array<int> incoming_dependencies;
    Datatype* signature;
    int dependency_count;
};

struct C_Generator
{
    Compiler* compiler;
    IR_Program* program;
    String section_enum_implementations;
    String section_string_data;
    String section_struct_prototypes;
    String section_struct_implementations;
    String section_type_declarations; // Array types, strings, ...
    String section_function_prototypes;
    String section_constants;
    String section_globals;
    String section_function_implementations;
    int name_counter;

    Hashtable<Datatype*, int> type_to_dependency_mapping;
    Dynamic_Array<C_Type_Definition_Dependency> type_dependencies;

    Dynamic_Array<String> translation_constant_to_name;
    Hashtable<Datatype*, String> translation_type_to_name;
    Hashtable<IR_Function*, String> translation_function_to_name;
    Hashtable<IR_Code_Block*, String> translation_code_block_to_name;
    Dynamic_Array<int> array_index_stack;
    int current_function_index;

    Upp_Constant global_type_informations;
};

C_Generator c_generator_create();
void c_generator_destroy(C_Generator* generator);
void c_generator_generate(C_Generator* generator, Compiler* compiler);
