#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "semantic_analyser.hpp"

/*
    What do I want from this intermediate Code:
        * It should make it easier to generate "Backend"-Code for C, LLVM or my Bytecode (Which can be run @ compile-time)
        * Should be slightly more highlevel than executable code, so it can be smartly translated
        * Works in tandum with the type system
        * Each function should have local variables, arguments and intermediate results combined as registers
        * Determines what should be called as a value or as pointer (But the implementation of function calling is backend dependent)
        * Registers have types
        * Variable names should be thrown away, but reconstructible
        * Metaprogramming is going to be resolved here
        * Namespacing and the Module System is going to be resolved here
        * Operator overloading should happen here
        * Function overloading should happen here
        * Should contain all instructions explicitly (E.g. casting data types)
*/

// TODO: Check if we need this for global access, or if the type could actually be just a boolean
enum class Data_Access_Type
{
    MEMORY_ACCESS, // Through pointer, which is in register
    REGISTER_ACCESS,
    // GLOBAL_ACCESS, (TODO)
};

struct Data_Access
{
    Data_Access_Type type;
    int register_index; // If memory_access, this is the register that holds the pointer, otherwise this is the register that holds the data
};

enum class Exit_Code
{
    SUCCESS,
    OUT_OF_BOUNDS, 
    STACK_OVERFLOW,
    RETURN_VALUE_OVERFLOW,
};
void exit_code_append_to_string(String* string, Exit_Code code);

enum class Intermediate_Instruction_Type
{
    MOVE_DATA, // Dest, Src  | Moves data from between registers and memory
    LOAD_CONSTANT_F32, // Dest
    LOAD_CONSTANT_I32, // Dest
    LOAD_CONSTANT_BOOL, // Dest

    IF_BLOCK, // Use source 1 as condition
    WHILE_BLOCK, // Use source 1 as condition
    CALL_FUNCTION, // Arguments + Destination Register
    CALL_HARDCODED_FUNCTION, // Arguments + Destination Register, i32_value = Hardcoded_Function_Type
    BREAK, // Currently just breaks out of the active while loop
    CONTINUE, // Just continues the current while loop
    RETURN, // Source 1 is return register
    EXIT, // Source 1 is return register, exit code

    ADDRESS_OF, // Dest, Src | If Src is a Register_Access, it returns a pointer to the register, if src = Memory_Access, then it also returns pointer to register
    CALCULATE_MEMBER_ACCESS_POINTER, // Destination, Source, offset in constant_i32_value | !Different Behavior depending on Memory_Access!
    CALCULATE_ARRAY_ACCESS_POINTER, // Destination, Source1: base_ptr, Source2: index_access, type_size in constant_i32_value | !Different Behavior depending on Memory_Access!

    // Operations
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

struct Intermediate_Instruction
{
    Intermediate_Instruction_Type type;
    Data_Access destination;
    Data_Access source1;
    Data_Access source2;
    // While/If block
    int condition_calculation_instruction_start;
    int condition_calculation_instruction_end_exclusive;
    int true_branch_instruction_start;
    int true_branch_instruction_end_exclusive;
    int false_branch_instruction_start;
    int false_branch_instruction_end_exclusive;
    // Function call
    int intermediate_function_index;
    DynamicArray<Data_Access> arguments;
    Hardcoded_Function_Type hardcoded_function_type;
    // Return thing
    bool return_has_value;
    Exit_Code exit_code;
    // Load constants, TODO: Do better with global data
    union {
        float constant_f32_value;
        int constant_i32_value;
        bool constant_bool_value;
    };
};

enum class Intermediate_Register_Type
{
    VARIABLE,
    EXPRESSION_RESULT,
    PARAMETER
};

struct Intermediate_Register
{
    Intermediate_Register_Type type;
    Type_Signature* type_signature;
    int parameter_index;
    int name_id;
};

struct Intermediate_Function
{
    DynamicArray<Intermediate_Register> registers;
    DynamicArray<Intermediate_Instruction> instructions;
    DynamicArray<int> instruction_to_ast_node_mapping;
    DynamicArray<int> register_to_ast_mapping;
    int name_handle;
    Type_Signature* function_type;
};

struct Variable_Mapping
{
    int name_handle;
    int register_index;
};

struct Intermediate_Generator
{
    DynamicArray<Intermediate_Function> functions;
    DynamicArray<int> function_to_ast_node_mapping;
    DynamicArray<Variable_Mapping> variable_mappings;
    Semantic_Analyser* analyser;
    int main_function_index;
    int current_function_index;
};

Intermediate_Generator intermediate_generator_create();
void intermediate_generator_destroy(Intermediate_Generator* generator);
void intermediate_generator_generate(Intermediate_Generator* generator, Semantic_Analyser* analyser);
void intermediate_generator_append_to_string(String* string, Intermediate_Generator* generator);
