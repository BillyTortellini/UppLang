#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/hashset.hpp"
#include "compiler_misc.hpp"
#include "constant_pool.hpp"


// C_COMPILER
struct C_Compiler;

C_Compiler* c_compiler_initialize();
void c_compiler_shutdown();
void c_compiler_compile();
Exit_Code c_compiler_execute();



// C_GENERATOR
struct C_Generator;

C_Generator* c_generator_initialize();
void c_generator_shutdown();

void c_generator_generate();
