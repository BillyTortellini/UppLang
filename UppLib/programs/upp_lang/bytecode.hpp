#pragma once

#include "semantic_analyser.hpp"

/*
    General Architecture Notes for Bytecode:
        * Each Codeword can have up to 2 operands (registers) + 1 destination register
        * Jumps are either conditional or non_conditional
        * Stack: Grows upwards, has a limited size

    Ideas:
        - Just save all registers on the stack before function call, arguments on stack --> Seems like a register stack would be useful
        - The abstract machine could also handle function calls, and save the registers itself --> register stack
        - I think there isnt a really good use for registers, since i could do all calculations in-memory on the stack,
            relative to the base pointor
        - But i could have two stacks, one for registers, and one for what exactly?
        I think i would like to try to have only one stack, so i can 
        What are the things that are on the stack? Local variables, function arguments, return address, return value

    How do i do function calls, which can be easily translated to C or LLVM or my code and which runs fast? 
    I am thinking about intermediate Code, which cannot be executed directly.
    But how is this Code different, and why shouldn't it be able to be executed directly?
    Types: When I want to translate to C or LLVM, I still want to have:
        - The underlying type of a variable in a register (u8, u16, u32, u64, f32, f64, bool, structs/unions/sized arrays)
        - Function calls
        - Ifs, Loops
        Function calls should be translatable, which means i need to know the arguments (The register numbers) + return values
        I also want a mapping from expressions to ast-nodes
        So variables and expression results will become the same thing, a register.
        This does not work since we need to be able to assign values to variables!
        So each function has a set of local variables, and a set of registers which are expression results

    So the runtime system will have a:
        - Stack (Return addresses, function arguments)
        - Registers (Unlimited, but each programm has a maximum number)
        - Base_Index,
        - Stack_Index 
        - Instruction_Index
        - Return_Register (s, when we have multiple return values)
            For return values, since doing this on the stack is hairy. Other methods would be
            to pass a pointer as an parameter, and the caller stores it there. 
            I think the best method is that the caller reserves space on the stack, before the arguments,
            and the callee writes to those values (Also works with multiple return values). On real
            machines, you would probably use registers for small values (32-64 bit, or maybe more).
            And large values would be put on the stack and passed by pointer.

    What about expression evaluation?
        Generate a unique register for each intermediate result.
        Each register has 64 bit, and only one Primitive type can be contained in one register.

    The stack grows upwards (Dynamic_Array), and it is indexed on a 4-byte index basis.
    Accessing the stack is done relative to the base_index. (Closures wont work this way, but IDC yet)
    Caller: Function calls look like this (Calling Convention):
        1. Save all registers from 0 to argument count on the stack
        2. Fill the arguments into the first registers, from left to right
        3. Push return address onto stack    \
        4. Jump to function entry point
        5. Push old base pointer onto stack  |
        6. Adjust base pointer               |
        7. Call function                     \---> This is all done using the call instruction
    Callee: Function Prolog:
        // Nothing really
    Callee: Function End:
        1. Move return value into return register
        2. Reset base pointer (old one on stack)
        3. Return to return address
    Caller: 
        1. Move return value into register
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
        CALL, // Pushes return address, op1 = instruction_index
        RETURN, // Pops return address, op1 = return_value reg
        LOAD_RETURN_VALUE, // op1 = dst_reg
        EXIT, // op1 = return_value_register
        LOAD_REGISTER_ADDRESS, // op1 = dest_reg, op2 = register_to_load
        STORE_TO_ADDRESS, // op1 = address_register, op2 = value_register
        LOAD_FROM_ADDRESS, // op1 = dest_register, op2 = address_register
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
    int type_index;
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
    int maximum_function_stack_depth;
};

Bytecode_Generator bytecode_generator_create();
void bytecode_generator_destroy(Bytecode_Generator* generator);
void bytecode_generator_generate(Bytecode_Generator* generator, Semantic_Analyser* analyser);
void bytecode_generator_append_bytecode_to_string(Bytecode_Generator* generator, String* string);