#pragma once

#include "intermediate_code.hpp"

/*
    General Architecture Notes for Bytecode:
        * Each Codeword can have up to 2 operands (registers) + 1 destination register
        * Jumps are either conditional or non_conditional
        * Stack: Grows upwards, has a limited size

    So the runtime system will have a:
        - Stack (Return addresses, register data, function arguments)
        - Stack_Pointer
        - Instruction_Pointer
        - Return_Register (s, when we have multiple return values)

    The stack grows upwards (Dynamic_Array), and it is indexed on a one-byte index basis.
    Accessing the stack is done relative to the Stack_Pointer. 
    A Functions Stack-Frame looks like this:
    [ParamReg0] [ParamReg1] [ParamRegs...] [Return_Address] [Old_Stack_Pointer] [Reg0] [Reg1] [Reg2] [Regs...]
*/
namespace Instruction_Type
{
    enum ENUM
    {
        MOVE_REGISTERS,  // op1 = dest_reg, op2 = src_reg, op3 = size
        WRITE_MEMORY, // op1 = address_register, op2 = value_register, op3 = size
        READ_MEMORY, // op1 = dest_register, op2 = address_register, op3 = size
        MEMORY_COPY, // op1 = dest_address_register, op2 = src_address_register, op3 = size
        U64_ADD_CONSTANT_I32, // op1 = dest_reg, op2 = constant offset
        U64_MULTIPLY_ADD_I32, // op1 = dest_reg, op2 = base_register, op3 = index_register, op4 = size

        JUMP, // op1 = instruction_index
        JUMP_ON_TRUE, // op1 = instruction_index, op2 = cnd_reg
        JUMP_ON_FALSE, // op1 = instruction_index, op2 = cnd_reg
        CALL, // Pushes return address, op1 = instruction_index, op2 = stack_offset for new frame
        CALL_HARDCODED_FUNCTION, // op1 = hardcoded_function_type, op2 = stack_offset for new frame
        RETURN, // Pops return address, op1 = return_value reg, op2 = return_size (Capped at 16 bytes)
        EXIT, // op1 = return_value_register, op2 = return size (Capped at 16)
        EXIT_ERROR, // Returns, op1 = error value

        LOAD_RETURN_VALUE, // op1 = dst_reg, op2 = size
        LOAD_REGISTER_ADDRESS, // op1 = dest_reg, op2 = register_to_load, // TODO: Also only works because we are lucky
        LOAD_CONSTANT_F32, // op1 = dest_reg, op2 = value // Todo: Only works because we dont 64bit constants yet
        LOAD_CONSTANT_I32, // op1 = dest_reg, op2 = value // Todo: Only works because we dont 64bit constants yet
        LOAD_CONSTANT_BOOLEAN, // op1 = dest_reg, op2 = value // Todo: Only works because we dont 64bit constants yet

        // Expression Instructions, all binary operations work the following: op1 = dest_byte_offset, op2 = left_byte_offset, op3 = right_byte_offset
        // ! This has to be in sync with the intermediate code ENUM
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
        UNARY_OP_ARITHMETIC_NEGATE_I32,

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
        UNARY_OP_ARITHMETIC_NEGATE_F32,

        BINARY_OP_COMPARISON_EQUAL_BOOL,
        BINARY_OP_COMPARISON_NOT_EQUAL_BOOL,
        BINARY_OP_BOOLEAN_AND,
        BINARY_OP_BOOLEAN_OR,
        UNARY_OP_BOOLEAN_NOT,
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
    // Data required for generation
    Intermediate_Generator* im_generator;
    DynamicArray<int> break_instructions_to_fill_out;
    DynamicArray<int> continue_instructions_to_fill_out;

    // Result data
    DynamicArray<Bytecode_Instruction> instructions;
    DynamicArray<int> function_locations;
    DynamicArray<Function_Call_Location> function_calls;
    DynamicArray<int> register_stack_locations;

    int entry_point_index;
    int maximum_function_stack_depth;
    int stack_offset_end_of_variables;
};

Bytecode_Generator bytecode_generator_create();
void bytecode_generator_destroy(Bytecode_Generator* generator);
void bytecode_generator_generate(Bytecode_Generator* generator, Intermediate_Generator* im_generator);
void bytecode_generator_append_bytecode_to_string(Bytecode_Generator* generator, String* string);