#pragma once

#include "../../datastructures/string.hpp"
#include "../../datastructures/Allocators.hpp"
#include "compiler_misc.hpp" // Exit_Code

struct IR_Code_Block;
struct Datatype;
struct Upp_Function;
struct Compilation_Data;

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
        Upp_Function* function;
        int global_index;
        struct {
            Upp_Function* function;
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
    DynString source_code;
    int line_offset; // Our line-indices start at 0, and the function-implementation starts at an offset
    DynArray<C_Line_Info> line_infos;
    DynTable<C_Translation, String> name_mapping;
};

C_Program_Translation* c_backend_generate_program(Compilation_Data* compilation_data);
void c_backend_compile_program(Compilation_Data* compilation_data);
Exit_Code c_backend_execute_program(Compilation_Data* compilation_data);
