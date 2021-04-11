#pragma once

#include "semantic_analyser.hpp"

/*
    General Architecture Notes for Bytecode:
        * Each Codeword can have up to 2 operands (registers) + 1 destination register
        * Jumps are either conditional or non_conditional
        * Stack: Grows upwards, and is dynamic_array of ints

    Ideas:
        - Just save all registers on the stack before function call, arguments on stack --> Seems like a register stack would be useful
        - The abstract machine could also handle function calls, and save the registers itself --> register stack
        - I think there isnt a really good use for registers, since i could do all calculations in-memory on the stack,
            relative to the base pointor
        - But i could have two stacks, one for registers, and one for what exactly?
        I think i would like to try to have only one stack, so i can 
        What are the things that are on the stack? Local variables, function arguments, return address, return value

    So the runtime system will have an:
        - Stack 
        - Base_Index,
        - Stack_Index 
        - Instruction_Index
        - Return_Register 
            For return values, since doing this on the stack is hairy. Other methods would be
            to pass a pointer as an parameter, and the caller stores it there. 
            I think the best method is that the caller reserves space on the stack, before the arguments,
            and the callee writes to those values (Also works with multiple return values). On real
            machines, you would probably use registers for small values (32-64 bit, or maybe more).

    What about expression evaluation?
        Usually I would generate a register for each intermediate result, after
        the expression is evaluated, i can reset all the used registers
        But i can just reset after each statement, and this should be fine

    The stack grows upwards (Dynamic_Array), and it is indexed on a 4-byte index basis.
    Accessing the stack is done relative to the base_index. (Closures wont work this way, but IDC yet)
    Caller: Function calls look like this (Calling Convention):
        1. Push arguments onto the stack, from left to right
        2. Push return address onto stack   \
        3. Call function                     ---> This is both done using the call instruction
    Callee: Function Prolog:
        1. Push Base_Pointer on the stack
        2. Push Stack_Index onto Base_Index
        3. Move stack-pointer forwards, so that push/pop wont affect local variables   ---> This is done using the enter instruction
    Callee: Function End:
        1. Move return value into return register
        2. Move Base_Pointer to stack Pointer                                 |
        3. Pop into Base_Pointer (now points to calling function frame)       |
        4. Return (Pops index from stack and then jumps)                      ---> Are all done using the return keyword
    Caller: 
        1. Pop arguments from stack
        2. Move return value to variable on stack 

*/
namespace Instruction_Type
{
    enum ENUM
    {
        LOAD_INTERMEDIATE_4BYTE, // op1 = dest_reg, op2 = value
        MOVE,  // op1 = dest_reg, op2 = src_reg
        JUMP, // op1 = instruction_index
        JUMP_ON_TRUE, // op1 = instruction_index, op2 = cnd_reg
        JUMP_ON_FALSE, // op1 = instruction_index, op2 = cnd_reg
        CALL, // Pushes return address, op1 = instruction_index, op2 = register_in_use_count
        ENTER, // Does function prolog, op1 = stack size required for function
        RETURN, // Pops return address, op1 = return_value reg
        LOAD_RETURN_VALUE, // op1 = dst_reg
        EXIT, // op1 = return_value_register
        // Expression Instructions
        INT_ADDITION, // All binary operations work the following: op1 = destination, op2 = left_op, op3 = right_op
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
    };
}

struct Bytecode_Instruction
{
    Instruction_Type::ENUM instruction_type;
    int op1;
    int op2;
    int op3;
};

struct Variable_Location
{
    int variable_name;
    Variable_Type::ENUM variable_type;
    int stack_base_offset;
};

struct Function_Location
{
    int name_id;
    int function_entry_instruction;
};

struct Function_Call_Location
{
    int function_name;
    int call_instruction_location;
};

struct Bytecode_Generator
{
    DynamicArray<Bytecode_Instruction> instructions;
    DynamicArray<Variable_Location> variable_locations;
    DynamicArray<int> break_instructions_to_fill_out;
    DynamicArray<int> continue_instructions_to_fill_out;
    DynamicArray<Function_Location> function_locations;
    DynamicArray<Function_Call_Location> function_calls;

    Semantic_Analyser* analyser;
    int stack_base_offset;
    int max_stack_base_offset;
    int entry_point_index;
    int main_name_id;
    bool in_main_function;
};

Bytecode_Generator bytecode_generator_create();
void bytecode_generator_destroy(Bytecode_Generator* generator);
void bytecode_generator_generate(Bytecode_Generator* generator, Semantic_Analyser* analyser);
void bytecode_generator_append_bytecode_to_string(Bytecode_Generator* generator, String* string);