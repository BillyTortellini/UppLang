#pragma once

#include "semantic_analyser.hpp"

/*
    General Architecture Notes for Bytecode:
        * Each Codeword can have up to 2 operands (registers) + 1 destination register
        * Jumps are either conditional or non_conditional
        * Stack: Grows upwards, and is dynamic_array of ints
        * Function calls are done with the call keyword, which pushes the next_instruction index on the stack and jumps
        * Arguments are pushed by the caller onto the stack in order, then call pushes return index
        * All arguemnts are call-by-value

    Problem: After function calls registers need to be reset...
        Push all registers onto stack before calling the function

    How is this stuff done again in real assembly language?
        - Limited amount of registers
        - Caller and Callee owned registers
        - Loading value to and from stack
*/
namespace Instruction_Type
{
    enum ENUM
    {
        INT_ADDITION,
        INT_SUBTRACT,
        INT_MULTIPLY,
        INT_DIVISION,
        INT_MODULO,
        INT_NEGATE,
        FLOAT_ADDITION,
        FLOAT_SUBTRACT,
        FLOAT_MULTIPLY,
        FLOAT_DIVISION,
        FLOAT_NEGATE,
        BOOLEAN_AND,
        BOOLEAN_OR,
        BOOLEAN_NOT,
        COMPARE_INT_GREATER_THAN,
        COMPARE_INT_GREATER_EQUAL,
        COMPARE_INT_LESS_THAN,
        COMPARE_INT_LESS_EQUAL,
        COMPARE_FLOAT_GREATER_THAN,
        COMPARE_FLOAT_GREATER_EQUAL,
        COMPARE_FLOAT_LESS_THAN,
        COMPARE_FLOAT_LESS_EQUAL,
        COMPARE_REGISTERS_4BYTE_EQUAL,
        COMPARE_REGISTERS_4BYTE_NOT_EQUAL,
        COMPARE_REGISTERS_1BYTE_EQUAL,
        COMPARE_REGISTERS_1BYTE_NOT_EQUAL,
        LOAD_INTERMEDIATE_4BYTE, // Loads intermediate 4 bytes, like load_intermediate_4bytes reg_a, 15
        LOAD_FROM_STACK, // Loads 4 bytes into register from stack offset, first operand is stack offset, dest operand is goal reg
        MOVE,  // Moves reg values to other reg, first
        JUMP, // Just jump to instruction number source_operand_1, all instructions are in one big array, even though different functions exist
        JUMP_CONDITIONAL, // Checks a 1 byte register (operand1), and jumps (operand2) if it is true, jump_conditional reg_cond, jmp
        RETURN,
        EXIT, 
    };
}

struct Bytecode_Instruction
{
    Instruction_Type::ENUM instruction_type;
    int source_operand_1;
    int source_operand_2;
    int destination_operand;
};

struct Variable_Location
{
    int variable_name;
    Variable_Type::ENUM variable_type;
    bool in_register;
    bool on_stack;
    int register_index;  // If in register
    int stack_base_offset; // If on stack
};

struct Bytecode_Generator
{
    DynamicArray<Bytecode_Instruction> instructions;
    DynamicArray<Variable_Location> variable_locations;
    DynamicArray<int> break_instructions_to_fill_out;
    DynamicArray<int> continue_instructions_to_fill_out;

    Semantic_Analyser* analyser;
    int stack_base_offset;
    int entry_point_index;
    int next_free_register;
};

Bytecode_Generator bytecode_generator_create();
void bytecode_generator_destroy(Bytecode_Generator* generator);
void bytecode_generator_generate(Bytecode_Generator* generator, Semantic_Analyser* analyser);