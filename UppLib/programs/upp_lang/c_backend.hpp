#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/hashset.hpp"
#include "compiler_misc.hpp"

struct C_Generator;
struct C_Compiler;
struct IR_Code_Block;
struct Datatype;
struct String;
struct IR_Function;


// C_COMPILER
C_Compiler* c_compiler_initialize();
void c_compiler_shutdown();
void c_compiler_compile();
Exit_Code c_compiler_execute();



// C_GENERATOR
enum class C_Translation_Type
{
    FUNCTION,
    DATATYPE,
    REGISTER,
    PARAMETER,
    GLOBAL,
    CONSTANT
};

struct C_Translation
{
    C_Translation_Type type;
    union {
        int function_slot_index;
        int global_index;
        struct {
            IR_Function* function;
            int index;
        } parameter;
        struct {
            IR_Code_Block* code_block;
            int index;
        } register_translation;
        Datatype* datatype;
        struct {
            int index; // Index in constant pool
            bool requires_memory_address; 
        } constant;
    } options;
};

struct C_Line_Info
{
    // If the line has no information, then ir_block == nullptr, and instruction_index == 0
    IR_Code_Block* ir_block;
    int instruction_index;
    int line_start_index;
    int line_end_index;
};

struct C_Program_Translation
{
    String source_code;
    int line_offset; // Our line-indices start at 0, and the function-implementation starts at an offset
    Dynamic_Array<C_Line_Info> line_infos;
    Hashtable<C_Translation, String> name_mapping;
};



C_Generator* c_generator_initialize();
void c_generator_shutdown();

void c_generator_generate();
C_Program_Translation* c_generator_get_translation();
