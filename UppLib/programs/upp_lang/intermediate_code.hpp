#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "semantic_analyser.hpp"

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

    IF_BLOCK, // Source 1 is condition, the statement comes before the condition and the blocks
    WHILE_BLOCK, // Source 1 is condition, the statement comes before the condition and the blocks
    CALL_FUNCTION, // Arguments + Destination Register
    CALL_HARDCODED_FUNCTION, // Arguments + Destination Register, i32_value = Hardcoded_Function_Type
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

    // Operations
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
    UNARY_OP_BOOLEAN_NOT,
};
bool intermediate_instruction_type_is_unary_operation(Intermediate_Instruction_Type instruction_type);
bool intermediate_instruction_type_is_binary_operation(Intermediate_Instruction_Type instruction_type);
void intermediate_instruction_binop_append_to_string(String* string, Intermediate_Instruction_Type instruction_type);
void intermediate_instruction_unary_operation_append_to_string(String* string, Intermediate_Instruction_Type instruction_type);

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
struct Intermediate_Generator;
Type_Signature* data_access_get_type_signature(Intermediate_Generator* generator, Data_Access access, int function_index);

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
    DynamicArray<Intermediate_Variable> local_variables;
    DynamicArray<Type_Signature*> intermediate_results;
    DynamicArray<Intermediate_Instruction> instructions;
    // I think the next two arent in use right now
    DynamicArray<int> instruction_to_ast_node_mapping;
    DynamicArray<int> register_to_ast_mapping;
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
    DynamicArray<Intermediate_Function> functions;
    DynamicArray<Intermediate_Variable> global_variables;
    DynamicArray<int> function_to_ast_node_mapping;

    // Temporary data for generation
    Semantic_Analyser* analyser;
    int current_function_index;
    DynamicArray<Name_Mapping> name_mappings;
};

Intermediate_Generator intermediate_generator_create();
void intermediate_generator_destroy(Intermediate_Generator* generator);
void intermediate_generator_generate(Intermediate_Generator* generator, Semantic_Analyser* analyser);
void intermediate_generator_append_to_string(String* string, Intermediate_Generator* generator);
