#pragma once

#include "intermediate_code.hpp"

/*
    Runtime system has:
        - Stack (Return addresses, register data, function arguments)
        - Stack_Pointer
        - Instruction_Pointer
        - Return_Register (s, when we have multiple return values)

    A Stack-Frame looks like this:
    [Param0] [Param1] [ParamX...] [Return_Address] [Old_Stack_Pointer] [Reg0] [Reg1] [Reg2] [Regs...]
*/

namespace Instruction_Type
{
    enum ENUM
    {
        MOVE_STACK_DATA,  // op1 = dest_reg, op2 = src_reg, op3 = size
        WRITE_MEMORY, // op1 = address_reg, op2 = value_reg, op3 = size
        READ_MEMORY, // op1 = dest_reg, op2 = address_reg, op3 = size
        MEMORY_COPY, // op1 = dest_address_reg, op2 = src_address_reg, op3 = size
        LOAD_GLOBAL, // op1 = dest_address_reg, op2 = global offset, op3 = size
        WRITE_GLOBAL, // op1 = dest_global offset, op2 = src_reg, op3 = size
        U64_ADD_CONSTANT_I32, // op1 = dest_reg, op2 = constant offset
        U64_MULTIPLY_ADD_I32, // op1 = dest_reg, op2 = base_reg, op3 = index_reg, op4 = size

        JUMP, // op1 = instruction_index
        JUMP_ON_TRUE, // op1 = instruction_index, op2 = cnd_reg
        JUMP_ON_FALSE, // op1 = instruction_index, op2 = cnd_reg
        CALL, // Pushes return address, op1 = instruction_index, op2 = stack_offset for new frame
        CALL_HARDCODED_FUNCTION, // op1 = hardcoded_function_type, op2 = stack_offset for new frame
        RETURN, // Pops return address, op1 = return_value reg, op2 = return_size (Capped at 16 bytes)
        EXIT, // op1 = return_value_register, op2 = return size (Capped at 16), op3 = exit_code

        LOAD_RETURN_VALUE, // op1 = dst_reg, op2 = size
        LOAD_REGISTER_ADDRESS, // op1 = dest_reg, op2 = register_to_load
        LOAD_GLOBAL_ADDRESS, // op1 = dest_reg, op2 = global offset
        LOAD_CONSTANT_F32, // op1 = dest_reg, op2 = value // Todo: Only works because we dont 64bit constants yet
        LOAD_CONSTANT_I32, // op1 = dest_reg, op2 = value // Todo: Only works because we dont 64bit constants yet
        LOAD_CONSTANT_BOOLEAN, // op1 = dest_reg, op2 = value // Todo: Only works because we dont 64bit constants yet
        LOAD_NULLPTR, // op1 = dest_reg

        CAST_INTEGER_DIFFERENT_SIZE, // op1 = dst_reg, op2 = src_reg, op3 = dst_prim_type, op4 = src_prim_type
        CAST_FLOAT_DIFFERENT_SIZE, // op1 = dst_reg, op2 = src_reg, op3 = dst_prim_type, op4 = src_prim_type
        CAST_FLOAT_INTEGER, // op1 = dst_reg, op2 = src_reg, op3 = dst_prim_type, op4 = src_prim_type
        CAST_INTEGER_FLOAT, // op1 = dst_reg, op2 = src_reg, op3 = dst_prim_type, op4 = src_prim_type

        // Expression Instructions, all binary operations work the following: op1 = dest_byte_offset, op2 = left_byte_offset, op3 = right_byte_offset
        // ! This has to be in sync with the intermediate code ENUM
        BINARY_OP_ARITHMETIC_ADDITION_U8,
        BINARY_OP_ARITHMETIC_SUBTRACTION_U8,
        BINARY_OP_ARITHMETIC_MULTIPLICATION_U8,
        BINARY_OP_ARITHMETIC_DIVISION_U8,
        BINARY_OP_ARITHMETIC_MODULO_U8,
        BINARY_OP_COMPARISON_EQUAL_U8,
        BINARY_OP_COMPARISON_NOT_EQUAL_U8,
        BINARY_OP_COMPARISON_GREATER_THAN_U8,
        BINARY_OP_COMPARISON_GREATER_EQUAL_U8,
        BINARY_OP_COMPARISON_LESS_THAN_U8,
        BINARY_OP_COMPARISON_LESS_EQUAL_U8,

        BINARY_OP_ARITHMETIC_ADDITION_U16,
        BINARY_OP_ARITHMETIC_SUBTRACTION_U16,
        BINARY_OP_ARITHMETIC_MULTIPLICATION_U16,
        BINARY_OP_ARITHMETIC_DIVISION_U16,
        BINARY_OP_ARITHMETIC_MODULO_U16,
        BINARY_OP_COMPARISON_EQUAL_U16,
        BINARY_OP_COMPARISON_NOT_EQUAL_U16,
        BINARY_OP_COMPARISON_GREATER_THAN_U16,
        BINARY_OP_COMPARISON_GREATER_EQUAL_U16,
        BINARY_OP_COMPARISON_LESS_THAN_U16,
        BINARY_OP_COMPARISON_LESS_EQUAL_U16,

        BINARY_OP_ARITHMETIC_ADDITION_U32,
        BINARY_OP_ARITHMETIC_SUBTRACTION_U32,
        BINARY_OP_ARITHMETIC_MULTIPLICATION_U32,
        BINARY_OP_ARITHMETIC_DIVISION_U32,
        BINARY_OP_ARITHMETIC_MODULO_U32,
        BINARY_OP_COMPARISON_EQUAL_U32,
        BINARY_OP_COMPARISON_NOT_EQUAL_U32,
        BINARY_OP_COMPARISON_GREATER_THAN_U32,
        BINARY_OP_COMPARISON_GREATER_EQUAL_U32,
        BINARY_OP_COMPARISON_LESS_THAN_U32,
        BINARY_OP_COMPARISON_LESS_EQUAL_U32,

        BINARY_OP_ARITHMETIC_ADDITION_U64,
        BINARY_OP_ARITHMETIC_SUBTRACTION_U64,
        BINARY_OP_ARITHMETIC_MULTIPLICATION_U64,
        BINARY_OP_ARITHMETIC_DIVISION_U64,
        BINARY_OP_ARITHMETIC_MODULO_U64,
        BINARY_OP_COMPARISON_EQUAL_U64,
        BINARY_OP_COMPARISON_NOT_EQUAL_U64,
        BINARY_OP_COMPARISON_GREATER_THAN_U64,
        BINARY_OP_COMPARISON_GREATER_EQUAL_U64,
        BINARY_OP_COMPARISON_LESS_THAN_U64,
        BINARY_OP_COMPARISON_LESS_EQUAL_U64,

        BINARY_OP_ARITHMETIC_ADDITION_I8,
        BINARY_OP_ARITHMETIC_SUBTRACTION_I8,
        BINARY_OP_ARITHMETIC_MULTIPLICATION_I8,
        BINARY_OP_ARITHMETIC_DIVISION_I8,
        BINARY_OP_ARITHMETIC_MODULO_I8,
        BINARY_OP_COMPARISON_EQUAL_I8,
        BINARY_OP_COMPARISON_NOT_EQUAL_I8,
        BINARY_OP_COMPARISON_GREATER_THAN_I8,
        BINARY_OP_COMPARISON_GREATER_EQUAL_I8,
        BINARY_OP_COMPARISON_LESS_THAN_I8,
        BINARY_OP_COMPARISON_LESS_EQUAL_I8,

        BINARY_OP_ARITHMETIC_ADDITION_I16,
        BINARY_OP_ARITHMETIC_SUBTRACTION_I16,
        BINARY_OP_ARITHMETIC_MULTIPLICATION_I16,
        BINARY_OP_ARITHMETIC_DIVISION_I16,
        BINARY_OP_ARITHMETIC_MODULO_I16,
        BINARY_OP_COMPARISON_EQUAL_I16,
        BINARY_OP_COMPARISON_NOT_EQUAL_I16,
        BINARY_OP_COMPARISON_GREATER_THAN_I16,
        BINARY_OP_COMPARISON_GREATER_EQUAL_I16,
        BINARY_OP_COMPARISON_LESS_THAN_I16,
        BINARY_OP_COMPARISON_LESS_EQUAL_I16,

        BINARY_OP_ARITHMETIC_ADDITION_I32,
        BINARY_OP_ARITHMETIC_SUBTRACTION_I32,
        BINARY_OP_ARITHMETIC_MULTIPLICATION_I32,
        BINARY_OP_ARITHMETIC_DIVISION_I32,
        BINARY_OP_ARITHMETIC_MODULO_I32,
        BINARY_OP_COMPARISON_EQUAL_I32,
        BINARY_OP_COMPARISON_NOT_EQUAL_I32,
        BINARY_OP_COMPARISON_GREATER_THAN_I32,
        BINARY_OP_COMPARISON_GREATER_EQUAL_I32,
        BINARY_OP_COMPARISON_LESS_THAN_I32,
        BINARY_OP_COMPARISON_LESS_EQUAL_I32,

        BINARY_OP_ARITHMETIC_ADDITION_I64,
        BINARY_OP_ARITHMETIC_SUBTRACTION_I64,
        BINARY_OP_ARITHMETIC_MULTIPLICATION_I64,
        BINARY_OP_ARITHMETIC_DIVISION_I64,
        BINARY_OP_ARITHMETIC_MODULO_I64,
        BINARY_OP_COMPARISON_EQUAL_I64,
        BINARY_OP_COMPARISON_NOT_EQUAL_I64,
        BINARY_OP_COMPARISON_GREATER_THAN_I64,
        BINARY_OP_COMPARISON_GREATER_EQUAL_I64,
        BINARY_OP_COMPARISON_LESS_THAN_I64,
        BINARY_OP_COMPARISON_LESS_EQUAL_I64,

        BINARY_OP_ARITHMETIC_ADDITION_F32,
        BINARY_OP_ARITHMETIC_SUBTRACTION_F32,
        BINARY_OP_ARITHMETIC_MULTIPLICATION_F32,
        BINARY_OP_ARITHMETIC_DIVISION_F32,
        BINARY_OP_COMPARISON_EQUAL_F32,
        BINARY_OP_COMPARISON_NOT_EQUAL_F32,
        BINARY_OP_COMPARISON_GREATER_THAN_F32,
        BINARY_OP_COMPARISON_GREATER_EQUAL_F32,
        BINARY_OP_COMPARISON_LESS_THAN_F32,
        BINARY_OP_COMPARISON_LESS_EQUAL_F32,

        BINARY_OP_ARITHMETIC_ADDITION_F64,
        BINARY_OP_ARITHMETIC_SUBTRACTION_F64,
        BINARY_OP_ARITHMETIC_MULTIPLICATION_F64,
        BINARY_OP_ARITHMETIC_DIVISION_F64,
        BINARY_OP_COMPARISON_EQUAL_F64,
        BINARY_OP_COMPARISON_NOT_EQUAL_F64,
        BINARY_OP_COMPARISON_GREATER_THAN_F64,
        BINARY_OP_COMPARISON_GREATER_EQUAL_F64,
        BINARY_OP_COMPARISON_LESS_THAN_F64,
        BINARY_OP_COMPARISON_LESS_EQUAL_F64,

        BINARY_OP_COMPARISON_EQUAL_BOOL,
        BINARY_OP_COMPARISON_NOT_EQUAL_BOOL,
        BINARY_OP_BOOLEAN_AND,
        BINARY_OP_BOOLEAN_OR,

        BINARY_OP_COMPARISON_EQUAL_POINTER,
        BINARY_OP_COMPARISON_NOT_EQUAL_POINTER,

        UNARY_OP_ARITHMETIC_NEGATE_I8,
        UNARY_OP_ARITHMETIC_NEGATE_I16,
        UNARY_OP_ARITHMETIC_NEGATE_I32,
        UNARY_OP_ARITHMETIC_NEGATE_I64,
        UNARY_OP_ARITHMETIC_NEGATE_F32,
        UNARY_OP_ARITHMETIC_NEGATE_F64,
        UNARY_OP_BOOLEAN_NOT
    };
}

struct Bytecode_Instruction
{
    Instruction_Type::ENUM instruction_type;
    int op1;
    int op2;
    int op3;
    int op4;
};

struct Function_Call_Location
{
    int function_index;
    int call_instruction_location;
};

struct Bytecode_Generator
{
    // Result data
    DynamicArray<Bytecode_Instruction> instructions;
    DynamicArray<int> function_locations;

    int global_data_size;
    int entry_point_index;
    int maximum_function_stack_depth;

    // Data required for generation
    Intermediate_Generator* im_generator;
    DynamicArray<Function_Call_Location> function_calls;
    DynamicArray<int> break_instructions_to_fill_out;
    DynamicArray<int> continue_instructions_to_fill_out;

    DynamicArray<int> parameter_stack_offsets;
    DynamicArray<int> variable_stack_offsets;
    DynamicArray<int> intermediate_stack_offsets;
    DynamicArray<int> global_offsets;
    int tmp_stack_offset;
};

Bytecode_Generator bytecode_generator_create();
void bytecode_generator_destroy(Bytecode_Generator* generator);
void bytecode_generator_generate(Bytecode_Generator* generator, Intermediate_Generator* im_generator);
void bytecode_instruction_append_to_string(String* string, Bytecode_Instruction instruction);
void bytecode_generator_append_bytecode_to_string(Bytecode_Generator* generator, String* string);
void bytecode_generator_calculate_function_variable_and_parameter_offsets(Bytecode_Generator* generator, int function_index);