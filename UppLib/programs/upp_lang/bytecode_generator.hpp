#pragma once

#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"
#include "../../datastructures/hashtable.hpp"
#include "semantic_analyser.hpp"

struct Upp_Function;
struct IR_Code_Block;

/*
    Runtime system has:
        - Stack (Return addresses, register data, function arguments)
        - Stack_Pointer
        - Instruction_Pointer

    A Stack-Frame looks like this:
    [Return_Instruction + 4byte padding] [Old_Stack_Pointer] [Return_Value_Buffer] [Param0] [Param1] [Param N...] [Reg 0] [Reg 1] [Reg N] [Tmp-Regs]
*/

enum class Bytecode_Type
{
    INT8,
    INT16,
    INT32,
    INT64,
    UINT8,
    UINT16,
    UINT32,
    UINT64,
    FLOAT32,
    FLOAT64,
    BOOL
};
Bytecode_Type datatype_to_bytecode_type(Datatype* primitive);
int bytecode_type_get_byte_size(Bytecode_Type type);

enum class Instruction_Type
{
    MOVE_STACK_DATA,  // op1 = dest_reg, op2 = src_reg, op3 = size
    WRITE_MEMORY, // op1 = address_reg, op2 = value_reg, op3 = size
    READ_MEMORY, // op1 = dest_reg, op2 = address_reg, op3 = size
    MEMORY_COPY, // op1 = dest_address_reg, op2 = src_address_reg, op3 = size
    READ_GLOBAL, // op1 = dest_address_reg, op2 = global index, op3 = size
    WRITE_GLOBAL, // op1 = dest_global index, op2 = src_reg, op3 = size
    READ_CONSTANT, // op1 = dest_reg, op2 = constant index, op3 = constant size
    U64_ADD_CONSTANT_I32, // op1 = dest_reg, op2 = src, op3 = constant offset
    U64_MULTIPLY_ADD_I32, // op1 = dest_reg, op2 = base_reg, op3 = index_reg, op4 = size

    JUMP, // op1 = instruction_index
    JUMP_ON_TRUE, // op1 = instruction_index, op2 = cnd_reg
    JUMP_ON_FALSE, // op1 = instruction_index, op2 = cnd_reg
    JUMP_ON_INT_EQUAL, // op1 = instruction_index, op2 = cnd_reg, op3 = int_value
    CALL_FUNCTION, // Pushes return address, op1 = function_index+1, op2 = stack_offset for new frame
    CALL_FUNCTION_POINTER, // op1 = src_reg, op2 = stack_offset for new frame
    CALL_BUILTIN_FUNCTION, // op1 = builtin_function, op2 = stack_offset for new frame (Parameter locations)
    RETURN, // Returns to previously called function, return-value should be stored on correct place on the stack by this point
    EXIT, // op1 = exit_code, op2 + op3 = Encoded pointer to error msg, see 

    LOAD_REGISTER_ADDRESS, // op1 = dest_reg, op2 = register_to_load
    LOAD_GLOBAL_ADDRESS, // op1 = dest_reg, op2 = global index
    LOAD_FUNCTION_LOCATION, // op1 = dest_reg, op2 = function_slot_index (Update: Only puts function_slot_index + 1 as i64 into dest_reg)
    LOAD_CONSTANT_ADDRESS, // op1 = dest_reg, op2 = constant index

    // op1 = dst_reg, op2 = src1, op3 = src2, op4 = packed operation_type + src/dst-type
    IR_OPERATION, 
};

struct Bytecode_Instruction
{
    Instruction_Type instruction_type;
    int op1;
    int op2;
    int op3;
    int op4;
};

void bytecode_generator_compile_function(Compilation_Data* compilation_data, Upp_Function* function);
void bytecode_instruction_append_to_string(String* string, Bytecode_Instruction instruction);
void bytecode_generator_append_bytecode_to_string(Compilation_Data* compilation_data, String* string);
Exit_Code exit_code_from_exit_instruction(Bytecode_Instruction& exit_instr);

int bytecode_pack_operation_and_types_to_int(Primitive_Operation operation, Bytecode_Type dst_type, Bytecode_Type left_type, Bytecode_Type right_type);
void bytecode_unpack_operation_and_types_from_int(
    int value, Primitive_Operation& out_op, Bytecode_Type &out_dst, Bytecode_Type& out_left, Bytecode_Type& out_right
);