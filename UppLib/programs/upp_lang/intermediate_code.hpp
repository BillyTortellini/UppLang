#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "semantic_analyser.hpp"
struct Compiler;

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
    MOVE_DATA, // Dest, Src     | Moves data between variables, globals and memory
    LOAD_CONSTANT_F32, // Dest
    LOAD_CONSTANT_I32, // Dest
    LOAD_CONSTANT_BOOL, // Dest
    LOAD_NULLPTR, // Dest
    LOAD_STRING_POINTER, // Dest, constant string value is a char pointer
    LOAD_FUNCTION_POINTER, // Dest, intermediate function index

    IF_BLOCK, // Source 1 is condition, the statement comes before the condition and the blocks
    WHILE_BLOCK, // Source 1 is condition, the statement comes before the condition and the blocks
    CALL_FUNCTION, // Arguments + Destination Register
    CALL_HARDCODED_FUNCTION, // Arguments + Destination Register, i32_value = Hardcoded_Function_Type
    CALL_FUNCTION_POINTER, // Source 1 is variable
    BREAK, // No additional data
    CONTINUE, // No additional data
    RETURN, // Source 1 is return register
    EXIT, // Source 1 is return register, Exit-Code

    ADDRESS_OF, // Dest, Src | Returns address of src register
    CALCULATE_MEMBER_ACCESS_POINTER, // Destination, Source, offset in constant_i32_value | !Different Behavior depending on Memory_Access!
    CALCULATE_ARRAY_ACCESS_POINTER, // Destination, Source1: base_ptr, Source2: index_access, type_size in constant_i32_value | !Different Behavior depending on Memory_Access!

    CAST_PRIMITIVE_TYPES, // Destination, Source
    CAST_POINTERS, // Destination, Source
    CAST_POINTER_TO_U64, // Destination, Source
    CAST_U64_TO_POINTER, // Destination, Source

    // Binary operations all work the same: destination, source1 and source2, 
    BINARY_OP_ARITHMETIC_ADDITION,
    BINARY_OP_ARITHMETIC_SUBTRACTION,
    BINARY_OP_ARITHMETIC_MULTIPLICATION,
    BINARY_OP_ARITHMETIC_DIVISION,
    BINARY_OP_ARITHMETIC_MODULO, // Only works for integer types

    BINARY_OP_COMPARISON_EQUAL, // Works for all primitive types + Pointers
    BINARY_OP_COMPARISON_NOT_EQUAL, // Works for all primitive types + Pointers
    BINARY_OP_COMPARISON_GREATER_THAN, // For integer and floats
    BINARY_OP_COMPARISON_GREATER_EQUAL, // For integer and floats
    BINARY_OP_COMPARISON_LESS_THAN, // For integer and floats
    BINARY_OP_COMPARISON_LESS_EQUAL, // For integer and floats

    BINARY_OP_BOOLEAN_AND,
    BINARY_OP_BOOLEAN_OR,

    // Unary operations
    UNARY_OP_BOOLEAN_NOT,
    UNARY_OP_ARITHMETIC_NEGATE,
};
bool intermediate_instruction_type_is_unary_operation(Intermediate_Instruction_Type instruction_type);
bool intermediate_instruction_type_is_binary_operation(Intermediate_Instruction_Type instruction_type);
void intermediate_instruction_type_binop_append_to_string(String* string, Intermediate_Instruction_Type instruction_type);
void intermediate_instruction_type_unary_operation_append_to_string(String* string, Intermediate_Instruction_Type instruction_type);

enum class Data_Access_Type
{
    VARIABLE_ACCESS, // Accessed through current function variables
    INTERMEDIATE_ACCESS, // Accessed through current function intermediate results
    PARAMETER_ACCESS, // Accessed through current function parameters
    GLOBAL_ACCESS, // Accessed through global variables
};

struct Data_Access
{
    bool is_pointer_access;
    Data_Access_Type access_type; 
    int access_index;
};

struct Intermediate_Instruction
{
    Intermediate_Instruction_Type type;
    Data_Access destination;
    Data_Access source1;
    Data_Access source2;
    // Binary/Unary operations
    Type_Signature* operand_types;
    // While/If block
    int condition_calculation_instruction_start;
    int condition_calculation_instruction_end_exclusive;
    int true_branch_instruction_start;
    int true_branch_instruction_end_exclusive;
    int false_branch_instruction_start;
    int false_branch_instruction_end_exclusive;
    // Function call
    int intermediate_function_index;
    Dynamic_Array<Data_Access> arguments;
    Hardcoded_Function_Type hardcoded_function_type;
    // Return thing
    bool return_has_value;
    // Exit instruction
    Exit_Code exit_code;
    // Casting
    Type_Signature* cast_from;
    Type_Signature* cast_to;
    // Constant loading
    union {
        float constant_f32_value;
        int constant_i32_value;
        bool constant_bool_value;
        const char* constant_string_value;
    };
};

struct Intermediate_Variable
{
    int name_handle;
    Type_Signature* type;
};

struct Intermediate_Function
{
    int name_handle;
    Type_Signature* function_type;
    Dynamic_Array<Intermediate_Variable> local_variables;
    Dynamic_Array<Type_Signature*> intermediate_results;
    Dynamic_Array<Intermediate_Instruction> instructions;
    // I think the next two arent in use right now
    Dynamic_Array<int> instruction_to_ast_node_mapping;
    Dynamic_Array<int> register_to_ast_mapping;
};

struct Name_Mapping
{
    int name_handle;
    int access_index;
    Data_Access_Type access_type;
};

struct Intermediate_Generator
{
    int main_function_index;
    Dynamic_Array<Intermediate_Function> functions;
    Dynamic_Array<Intermediate_Variable> global_variables;
    Dynamic_Array<int> function_to_ast_node_mapping;

    // Temporary data for generation
    Compiler* compiler;
    int current_function_index;
    Dynamic_Array<Name_Mapping> name_mappings;
};

Intermediate_Generator intermediate_generator_create();
void intermediate_generator_destroy(Intermediate_Generator* generator);
void intermediate_generator_generate(Intermediate_Generator* generator, Compiler* compiler);
void intermediate_generator_append_to_string(String* string, Intermediate_Generator* generator);
Type_Signature* intermediate_generator_get_access_signature(Intermediate_Generator* generator, Data_Access access, int function_index);
