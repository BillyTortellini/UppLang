#pragma once

#include "compiler.hpp"

enum InstructionType
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
    JUMP, // Just jump to instruction number 4, all instructions are in one big array, even though different functions exist
    JUMP_CONDITIONAL, // Checks a 1 byte register, and jumps if it is true, jump_conditional reg_b
};

struct Bytecode_Instruction
{
    InstructionType symbol_type;
    int source_operand_1;
    int source_operand_2;
    int destination_operand;
};

struct Bytecode
{
    DynamicArray<Bytecode_Instruction> instructions;
    int entry_point_index;
};

Bytecode bytecode_create();
void bytecode_destroy(Bytecode* bytecode);
Bytecode bytecode_create_from_ast(Bytecode* bytecode, Ast_Node_Root* root);