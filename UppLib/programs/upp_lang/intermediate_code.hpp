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

// How do I do array access and member access?
// Also loading immediate data, e.g. constants
enum class Intermediate_Instruction_Type
{
    MOVE_CONSTANT, // Dest
    MOVE_REGISTER, // Src Dest
    ADDRESS_OF_REGISTER, // Src Dest ,Should be resolved at compile time by the backend compiler
    IF_BLOCK,
    WHILE_BLOCK,
    CALL_FUNCTION, // Arguments + Destination Register
    RETURN, // Src
    EXIT,
    // Operations
    BINARY_OP_ARITHMETIC_ADDITION,
    BINARY_OP_ARITHMETIC_SUBTRACTION,
    BINARY_OP_ARITHMETIC_MULTIPLICATION,
    BINARY_OP_ARITHMETIC_DIVISION,
    BINARY_OP_ARITHMETIC_MODULO,
    BINARY_OP_COMPARISON_GREATER_THAN,
    BINARY_OP_COMPARISON_GREATER_EQUAL,
    BINARY_OP_COMPARISON_LESS_THAN,
    BINARY_OP_COMPARISON_LESS_EQUAL,
    BINARY_OP_COMPARISON_EQUAL,
    BINARY_OP_COMPARISON_NOT_EQUAL,
    BINARY_OP_BOOLEAN_AND,
    BINARY_OP_BOOLEAN_OR,
    UNARY_OP_BOOLEAN_NOT,
    UNARY_OP_ARITHMETIC_NEGATE,
};

struct Intermediate_Instruction
{
    Intermediate_Instruction_Type type;
    int source_register;
    int destination_register;
    int left_operand_register;
    int right_operand_register;
    u64 constant_value;
    // While/If block
    int condition_register;
    int true_branch_instruction_start;
    int true_branch_instruction_size;
    int false_branch_instruction_start;
    int false_branch_instruction_size;
    // Function call
    int function_node_index;
    DynamicArray<int> argument_registers;
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
    int type_index;
    int parameter_index;
};

struct Intermediate_Function
{
    DynamicArray<Intermediate_Register> registers;
    DynamicArray<Intermediate_Instruction> instructions;
    DynamicArray<int> instruction_to_ast_node_mapping;
    DynamicArray<int> register_to_ast_mapping;
};

struct Variable_Mapping
{
    int name;
    int register_index;
};

struct Intermediate_Generator
{
    DynamicArray<Intermediate_Function> functions;
    DynamicArray<int> function_to_ast_node_mapping;
    DynamicArray<Variable_Mapping> variable_mappings;
    Semantic_Analyser* analyser;
    int main_function_index;
};

Intermediate_Generator intermediate_generator_create();
void intermediate_generator_destroy(Intermediate_Generator* generator);
void intermediate_generator_generate(Intermediate_Generator* generator, Semantic_Analyser* analyser);
