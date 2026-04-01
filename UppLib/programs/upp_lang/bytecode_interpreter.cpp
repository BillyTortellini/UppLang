#include "bytecode_interpreter.hpp"

#include <iostream>
#include "../../utility/random.hpp"
#include "compiler.hpp"
#include <Windows.h>
#include "ir_code.hpp"
#include "editor_analysis_info.hpp"

struct Bytecode_Thread
{
    Compilation_Data* compilation_data;
    Arena* arena; // Where all heap allocations go
    int max_instruction_executions;
    int max_heap_consumption;
    bool allow_global_access;

    // Run-Information
    int instruction_index;
    byte* stack_pointer;
    Array<byte> stack;
    int heap_memory_consumption;
    int executed_instruction_count;

    // Result infos
    Exit_Code exit_code;
};


Bytecode_Thread* bytecode_thread_create(
	Compilation_Data* compilation_data, Arena* arena, int max_instruction_executions, int max_heap_consumption, int stack_size, bool allow_global_access
)
{
    Bytecode_Thread* result = arena->allocate<Bytecode_Thread>();
    memory_zero(result);
    result->arena = arena;
    result->compilation_data = compilation_data;

    result->max_instruction_executions = max_instruction_executions;
    result->max_heap_consumption = max_heap_consumption;
    result->allow_global_access = allow_global_access;
    result->stack.size = stack_size;
    result->stack.data = (byte*) arena->allocate_raw(stack_size, 16); // Allocate raw instead of allocate_array so we can have 16 byte alignment

    return result;
}

void bytecode_execute_cast_instr(Instruction_Type instr_type, void* dest, void* src, Bytecode_Type dest_type, Bytecode_Type src_type)
{
    switch (instr_type)
    {
    case Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE: 
    {
        u64 source_unsigned = 0;
        i64 source_signed = 0;
        bool source_is_signed = false;
        switch (src_type) {
        case Bytecode_Type::INT8: source_is_signed = true; source_signed = *(i8*)(src); break;
        case Bytecode_Type::INT16: source_is_signed = true; source_signed = *(i16*)(src); break;
        case Bytecode_Type::INT32: source_is_signed = true; source_signed = *(i32*)(src); break;
        case Bytecode_Type::INT64: source_is_signed = true; source_signed = *(i64*)(src); break;
        case Bytecode_Type::UINT8: source_is_signed = false; source_unsigned = *(u8*)(src); break;
        case Bytecode_Type::UINT16: source_is_signed = false; source_unsigned = *(u16*)(src); break;
        case Bytecode_Type::UINT32: source_is_signed = false; source_unsigned = *(u32*)(src); break;
        case Bytecode_Type::UINT64: source_is_signed = false; source_unsigned = *(u64*)(src); break;
        default: panic("what the frigg\n");
        }
        switch (dest_type) {
        case Bytecode_Type::INT8:   *(i8*)(dest)  = (i8) (source_is_signed ? source_signed : source_unsigned); break;
        case Bytecode_Type::INT16:  *(i16*)(dest) = (i16)(source_is_signed ? source_signed : source_unsigned); break;
        case Bytecode_Type::INT32:  *(i32*)(dest) = (i32)(source_is_signed ? source_signed : source_unsigned); break;
        case Bytecode_Type::INT64:  *(i64*)(dest) = (i64)(source_is_signed ? source_signed : source_unsigned); break;
        case Bytecode_Type::UINT8:  *(u8*)(dest)  = (u8) (source_is_signed ? source_signed : source_unsigned); break;
        case Bytecode_Type::UINT16: *(u16*)(dest) = (u16)(source_is_signed ? source_signed : source_unsigned); break;
        case Bytecode_Type::UINT32: *(u32*)(dest) = (u32)(source_is_signed ? source_signed : source_unsigned); break;
        case Bytecode_Type::UINT64: *(u64*)(dest) = (u64)(source_is_signed ? source_signed : source_unsigned); break;
        default: panic("what the frigg\n");
        }
        break;
    }
    case Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE: 
    {
        double source = 0.0;
        switch (src_type) {
        case Bytecode_Type::FLOAT32: source = *(float*)(src); break;
        case Bytecode_Type::FLOAT64: source = *(double*)(src); break;
        default: panic("what the frigg\n");
        }
        switch (dest_type) {
        case Bytecode_Type::FLOAT32: *(float*)(dest) = (float)source; break;
        case Bytecode_Type::FLOAT64: *(double*)(dest) = (double)source; break;
        default: panic("what the frigg\n");
        }
        break;
    }
    case Instruction_Type::CAST_FLOAT_INTEGER: 
    {
        double source = 0.0;
        switch (src_type) {
        case Bytecode_Type::FLOAT32: source = *(float*)(src); break;
        case Bytecode_Type::FLOAT64: source = *(double*)(src); break;
        default: panic("what the frigg\n");
        }
        switch (dest_type) {
        case Bytecode_Type::INT8:   *(i8*)(dest) = (i8)source; break;
        case Bytecode_Type::INT16:  *(i16*)(dest) = (i16)source; break;
        case Bytecode_Type::INT32:  *(i32*)(dest) = (i32)source; break;
        case Bytecode_Type::INT64:  *(i64*)(dest) = (i64)source; break;
        case Bytecode_Type::UINT8:  *(u8*)(dest) = (u8)source; break;
        case Bytecode_Type::UINT16: *(u16*)(dest) = (u16)source; break;
        case Bytecode_Type::UINT32: *(u32*)(dest) = (u32)source; break;
        case Bytecode_Type::UINT64: *(u64*)(dest) = (u64)source; break;
        default: panic("what the frigg\n");
        }
        break;
    }
    case Instruction_Type::CAST_INTEGER_FLOAT:
    {
        u64 source_unsigned;
        i64 source_signed;
        bool source_is_signed = false;
        switch (src_type) {
        case Bytecode_Type::INT8:   source_is_signed = true; source_signed = *(i8*)(src); break;
        case Bytecode_Type::INT16:  source_is_signed = true; source_signed = *(i16*)(src); break;
        case Bytecode_Type::INT32:  source_is_signed = true; source_signed = *(i32*)(src); break;
        case Bytecode_Type::INT64:  source_is_signed = true; source_signed = *(i64*)(src); break;
        case Bytecode_Type::UINT8:  source_is_signed = false; source_unsigned = *(u8*)(src); break;
        case Bytecode_Type::UINT16: source_is_signed = false; source_unsigned = *(u16*)(src); break;
        case Bytecode_Type::UINT32: source_is_signed = false; source_unsigned = *(u32*)(src); break;
        case Bytecode_Type::UINT64: source_is_signed = false; source_unsigned = *(u64*)(src); break;
        default: panic("what the frigg\n");
        }
        switch (dest_type) {
        case Bytecode_Type::FLOAT32: *(float*)(dest) = (float)(source_is_signed ? source_signed : source_unsigned); break;
        case Bytecode_Type::FLOAT64: *(double*)(dest) = (double)(source_is_signed ? source_signed : source_unsigned); break;
        default: panic("what the frigg\n");
        }
        break;
    }
    default: panic("");
    }
}

void bytecode_execute_unary_instr(Instruction_Type instr_type, Bytecode_Type type, void* dest, void* operand)
{
    switch (instr_type)
    {
    case Instruction_Type::UNARY_OP_NEGATE: {
        switch (type)
        {
        case Bytecode_Type::BOOL:
            panic("What");
            break;
        case Bytecode_Type::INT8:
            *(i8*)(dest) = -*(i8*)(operand);
            break;
        case Bytecode_Type::INT16:
            *(i16*)(dest) = -*(i16*)(operand);
            break;
        case Bytecode_Type::INT32:
            *(i32*)(dest) = -*(i32*)(operand);
            break;
        case Bytecode_Type::INT64:
            *(i64*)(dest) = -*(i64*)(operand);
            break;
        case Bytecode_Type::UINT8:
        case Bytecode_Type::UINT16:
        case Bytecode_Type::UINT32:
        case Bytecode_Type::UINT64:
            panic("Should not happen?");
            break;
        case Bytecode_Type::FLOAT32:
            *(f32*)(dest) = -*(f32*)(operand);
            break;
        case Bytecode_Type::FLOAT64:
            *(f64*)(dest) = -*(f64*)(operand);
            break;
        default: panic("");
        }
        break;
    }
    case Instruction_Type::UNARY_OP_BITWISE_NOT: 
    {
        switch (type)
        {
        case Bytecode_Type::INT8:   *(i8*) (dest) = ~*(i8*) (operand); break;
        case Bytecode_Type::INT16:  *(i16*)(dest) = ~*(i16*)(operand); break;
        case Bytecode_Type::INT32:  *(i32*)(dest) = ~*(i32*)(operand); break;
        case Bytecode_Type::INT64:  *(i64*)(dest) = ~*(i64*)(operand); break;
        case Bytecode_Type::UINT8:  *(u8*) (dest) = ~*(u8*) (operand); break;
        case Bytecode_Type::UINT16: *(u16*)(dest) = ~*(u16*)(operand); break;
        case Bytecode_Type::UINT32: *(u32*)(dest) = ~*(u32*)(operand); break;
        case Bytecode_Type::UINT64: *(u64*)(dest) = ~*(u64*)(operand); break;
        case Bytecode_Type::BOOL:
        case Bytecode_Type::FLOAT32:
        case Bytecode_Type::FLOAT64: panic("What"); break;
        default: panic("");
        }
        break;
    }
    case Instruction_Type::UNARY_OP_NOT:
        *(bool*)(dest) = !*(bool*)(operand);
        break;
    default: panic("");
    }
}

bool bytecode_execute_binary_instr(Instruction_Type instr_type, Bytecode_Type type, void* dest, void* op_left, void* op_right)
{
    switch (instr_type)
    {
    case Instruction_Type::BINARY_OP_ADDITION:
        switch (type)
        {
        case Bytecode_Type::INT8:    *(i8*) (dest) = *(i8*) (op_left) + *(i8*) (op_right); break;
        case Bytecode_Type::INT16:   *(i16*)(dest) = *(i16*)(op_left) + *(i16*)(op_right); break;
        case Bytecode_Type::INT32:   *(i32*)(dest) = *(i32*)(op_left) + *(i32*)(op_right); break;
        case Bytecode_Type::INT64:   *(i64*)(dest) = *(i64*)(op_left) + *(i64*)(op_right); break;
        case Bytecode_Type::UINT8:   *(u8*) (dest) = *(u8*) (op_left) + *(u8*) (op_right); break;
        case Bytecode_Type::UINT16:  *(u16*)(dest) = *(u16*)(op_left) + *(u16*)(op_right); break;
        case Bytecode_Type::UINT32:  *(u32*)(dest) = *(u32*)(op_left) + *(u32*)(op_right); break;
        case Bytecode_Type::UINT64:  *(u64*)(dest) = *(u64*)(op_left) + *(u64*)(op_right); break;
        case Bytecode_Type::FLOAT32: *(f32*)(dest) = *(f32*)(op_left) + *(f32*)(op_right); break;
        case Bytecode_Type::FLOAT64: *(f64*)(dest) = *(f64*)(op_left) + *(f64*)(op_right); break;
        case Bytecode_Type::BOOL: return false;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_SUBTRACTION:
        switch (type)
        {
        case Bytecode_Type::INT8:    *(i8*) (dest) = *(i8*) (op_left) - *(i8*) (op_right); break;
        case Bytecode_Type::INT16:   *(i16*)(dest) = *(i16*)(op_left) - *(i16*)(op_right); break;
        case Bytecode_Type::INT32:   *(i32*)(dest) = *(i32*)(op_left) - *(i32*)(op_right); break;
        case Bytecode_Type::INT64:   *(i64*)(dest) = *(i64*)(op_left) - *(i64*)(op_right); break;
        case Bytecode_Type::UINT8:   *(u8*) (dest) = *(u8*) (op_left) - *(u8*) (op_right); break;
        case Bytecode_Type::UINT16:  *(u16*)(dest) = *(u16*)(op_left) - *(u16*)(op_right); break;
        case Bytecode_Type::UINT32:  *(u32*)(dest) = *(u32*)(op_left) - *(u32*)(op_right); break;
        case Bytecode_Type::UINT64:  *(u64*)(dest) = *(u64*)(op_left) - *(u64*)(op_right); break;
        case Bytecode_Type::FLOAT32: *(f32*)(dest) = *(f32*)(op_left) - *(f32*)(op_right); break;
        case Bytecode_Type::FLOAT64: *(f64*)(dest) = *(f64*)(op_left) - *(f64*)(op_right); break;
        case Bytecode_Type::BOOL: return false;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_MULTIPLICATION:
        switch (type)
        {
        case Bytecode_Type::INT8:    *(i8*) (dest) = *(i8*) (op_left) * *(i8*) (op_right); break;
        case Bytecode_Type::INT16:   *(i16*)(dest) = *(i16*)(op_left) * *(i16*)(op_right); break;
        case Bytecode_Type::INT32:   *(i32*)(dest) = *(i32*)(op_left) * *(i32*)(op_right); break;
        case Bytecode_Type::INT64:   *(i64*)(dest) = *(i64*)(op_left) * *(i64*)(op_right); break;
        case Bytecode_Type::UINT8:   *(u8*) (dest) = *(u8*) (op_left) * *(u8*) (op_right); break;
        case Bytecode_Type::UINT16:  *(u16*)(dest) = *(u16*)(op_left) * *(u16*)(op_right); break;
        case Bytecode_Type::UINT32:  *(u32*)(dest) = *(u32*)(op_left) * *(u32*)(op_right); break;
        case Bytecode_Type::UINT64:  *(u64*)(dest) = *(u64*)(op_left) * *(u64*)(op_right); break;
        case Bytecode_Type::FLOAT32: *(f32*)(dest) = *(f32*)(op_left) * *(f32*)(op_right); break;
        case Bytecode_Type::FLOAT64: *(f64*)(dest) = *(f64*)(op_left) * *(f64*)(op_right); break;
        case Bytecode_Type::BOOL: return false;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_DIVISION:
        switch (type)
        {
        case Bytecode_Type::INT8:    *(i8*) (dest) = *(i8*) (op_left) / *(i8*) (op_right); break;
        case Bytecode_Type::INT16:   *(i16*)(dest) = *(i16*)(op_left) / *(i16*)(op_right); break;
        case Bytecode_Type::INT32:   *(i32*)(dest) = *(i32*)(op_left) / *(i32*)(op_right); break;
        case Bytecode_Type::INT64:   *(i64*)(dest) = *(i64*)(op_left) / *(i64*)(op_right); break;
        case Bytecode_Type::UINT8:   *(u8*) (dest) = *(u8*) (op_left) / *(u8*) (op_right); break;
        case Bytecode_Type::UINT16:  *(u16*)(dest) = *(u16*)(op_left) / *(u16*)(op_right); break;
        case Bytecode_Type::UINT32:  *(u32*)(dest) = *(u32*)(op_left) / *(u32*)(op_right); break;
        case Bytecode_Type::UINT64:  *(u64*)(dest) = *(u64*)(op_left) / *(u64*)(op_right); break;
        case Bytecode_Type::FLOAT32: *(f32*)(dest) = *(f32*)(op_left) / *(f32*)(op_right); break;
        case Bytecode_Type::FLOAT64: *(f64*)(dest) = *(f64*)(op_left) / *(f64*)(op_right); break;
        case Bytecode_Type::BOOL: return false;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_EQUAL:
        switch (type)
        {
        case Bytecode_Type::BOOL:
            *(u8*)(dest) = *(bool*)(op_left) == *(bool*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT8:
            *(u8*)(dest) = *(i8*)(op_left) == *(i8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT16:
            *(u8*)(dest) = *(i16*)(op_left) == *(i16*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT32:
            *(u8*)(dest) = *(i32*)(op_left) == *(i32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT64:
            *(u8*)(dest) = *(i64*)(op_left) == *(i64*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT8:
            *(u8*)(dest) = *(u8*)(op_left) == *(u8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT16:
            *(u8*)(dest) = *(u16*)(op_left) == *(u16*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT32:
            *(u8*)(dest) = *(u32*)(op_left) == *(u32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT64:
            *(u8*)(dest) = *(u64*)(op_left) == *(u64*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::FLOAT32:
            *(u8*)(dest) = *(f32*)(op_left) == *(f32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::FLOAT64:
            *(u8*)(dest) = *(f64*)(op_left) == *(f64*)(op_right) ? 1 : 0;
            break;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_NOT_EQUAL:
        switch (type)
        {
        case Bytecode_Type::BOOL:
            *(u8*)(dest) = *(u8*)(op_left) != *(u8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT8:
            *(u8*)(dest) = *(i8*)(op_left) != *(i8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT16:
            *(u8*)(dest) = *(i16*)(op_left) != *(i16*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT32:
            *(u8*)(dest) = *(i32*)(op_left) != *(i32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT64:
            *(u8*)(dest) = *(i64*)(op_left) != *(i64*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT8:
            *(u8*)(dest) = *(u8*)(op_left) != *(u8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT16:
            *(u8*)(dest) = *(u16*)(op_left) != *(u16*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT32:
            *(u8*)(dest) = *(u32*)(op_left) != *(u32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT64:
            *(u8*)(dest) = *(u64*)(op_left) != *(u64*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::FLOAT32:
            *(u8*)(dest) = *(f32*)(op_left) != *(f32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::FLOAT64:
            *(u8*)(dest) = *(f64*)(op_left) != *(f64*)(op_right) ? 1 : 0;
            break;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_GREATER_THAN:
        switch (type)
        {
        case Bytecode_Type::BOOL:
            return false;
        case Bytecode_Type::INT8:
            *(u8*)(dest) = *(i8*)(op_left) > * (i8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT16:
            *(u8*)(dest) = *(i16*)(op_left) > * (i16*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT32:
            *(u8*)(dest) = *(i32*)(op_left) > * (i32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT64:
            *(u8*)(dest) = *(i64*)(op_left) > * (i64*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT8:
            *(u8*)(dest) = *(u8*)(op_left) > * (u8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT16:
            *(u8*)(dest) = *(u16*)(op_left) > * (u16*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT32:
            *(u8*)(dest) = *(u32*)(op_left) > * (u32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT64:
            *(u8*)(dest) = *(u64*)(op_left) > * (u64*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::FLOAT32:
            *(u8*)(dest) = *(f32*)(op_left) > * (f32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::FLOAT64:
            *(u8*)(dest) = *(f64*)(op_left) > * (f64*)(op_right) ? 1 : 0;
            break;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_GREATER_EQUAL:
        switch (type)
        {
        case Bytecode_Type::BOOL:
            return false;
        case Bytecode_Type::INT8:
            *(u8*)(dest) = *(i8*)(op_left) >= *(i8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT16:
            *(u8*)(dest) = *(i16*)(op_left) >= *(i16*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT32:
            *(u8*)(dest) = *(i32*)(op_left) >= *(i32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT64:
            *(u8*)(dest) = *(i64*)(op_left) >= *(i64*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT8:
            *(u8*)(dest) = *(u8*)(op_left) >= *(u8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT16:
            *(u8*)(dest) = *(u16*)(op_left) >= *(u16*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT32:
            *(u8*)(dest) = *(u32*)(op_left) >= *(u32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT64:
            *(u8*)(dest) = *(u64*)(op_left) >= *(u64*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::FLOAT32:
            *(u8*)(dest) = *(f32*)(op_left) >= *(f32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::FLOAT64:
            *(u8*)(dest) = *(f64*)(op_left) >= *(f64*)(op_right) ? 1 : 0;
            break;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_LESS_THAN:
        switch (type)
        {
        case Bytecode_Type::BOOL:
            return false;
        case Bytecode_Type::INT8:
            *(u8*)(dest) = *(i8*)(op_left) < *(i8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT16:
            *(u8*)(dest) = *(i16*)(op_left) < *(i16*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT32:
            *(u8*)(dest) = *(i32*)(op_left) < *(i32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT64:
            *(u8*)(dest) = *(i64*)(op_left) < *(i64*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT8:
            *(u8*)(dest) = *(u8*)(op_left) < *(u8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT16:
            *(u8*)(dest) = *(u16*)(op_left) < *(u16*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT32:
            *(u8*)(dest) = *(u32*)(op_left) < *(u32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT64:
            *(u8*)(dest) = *(u64*)(op_left) < *(u64*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::FLOAT32:
            *(u8*)(dest) = *(f32*)(op_left) < *(f32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::FLOAT64:
            *(u8*)(dest) = *(f64*)(op_left) < *(f64*)(op_right) ? 1 : 0;
            break;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_LESS_EQUAL:
        switch (type)
        {
        case Bytecode_Type::BOOL:
            return false;
            break;
        case Bytecode_Type::INT8:
            *(u8*)(dest) = *(i8*)(op_left) <= *(i8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT16:
            *(u8*)(dest) = *(i16*)(op_left) <= *(i16*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT32:
            *(u8*)(dest) = *(i32*)(op_left) <= *(i32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::INT64:
            *(u8*)(dest) = *(i64*)(op_left) <= *(i64*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT8:
            *(u8*)(dest) = *(u8*)(op_left) <= *(u8*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT16:
            *(u8*)(dest) = *(u16*)(op_left) <= *(u16*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT32:
            *(u8*)(dest) = *(u32*)(op_left) <= *(u32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::UINT64:
            *(u8*)(dest) = *(u64*)(op_left) <= *(u64*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::FLOAT32:
            *(u8*)(dest) = *(f32*)(op_left) <= *(f32*)(op_right) ? 1 : 0;
            break;
        case Bytecode_Type::FLOAT64:
            *(u8*)(dest) = *(f64*)(op_left) <= *(f64*)(op_right) ? 1 : 0;
            break;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_MODULO:
        switch (type)
        {
        case Bytecode_Type::BOOL:
            return false;
            break;
        case Bytecode_Type::INT8:
            if (*(i8*)op_right == 0) return false;
            *(i8*)(dest) = *(i8*)(op_left) % *(i8*)(op_right);
            break;
        case Bytecode_Type::INT16:
            if (*(i16*)op_right == 0) return false;
            *(i16*)(dest) = *(i16*)(op_left) % *(i16*)(op_right);
            break;
        case Bytecode_Type::INT32:
            if (*(i32*)op_right == 0) return false;
            *(i32*)(dest) = *(i32*)(op_left) % *(i32*)(op_right);
            break;
        case Bytecode_Type::INT64:
            if (*(i64*)op_right == 0) return false;
            *(i64*)(dest) = *(i64*)(op_left) % *(i64*)(op_right);
            break;
        case Bytecode_Type::UINT8:
            if (*(u8*)op_right == 0) return false;
            *(u8*)(dest) = *(u8*)(op_left) % *(u8*)(op_right);
            break;
        case Bytecode_Type::UINT16:
            if (*(u16*)op_right == 0) return false;
            *(u16*)(dest) = *(u16*)(op_left) % *(u16*)(op_right);
            break;
        case Bytecode_Type::UINT32:
            if (*(u32*)op_right == 0) return false;
            *(u32*)(dest) = *(u32*)(op_left) % *(u32*)(op_right);
            break;
        case Bytecode_Type::UINT64:
            if (*(u64*)op_right == 0) return false;
            *(u64*)(dest) = *(u64*)(op_left) % *(u64*)(op_right);
            break;
        case Bytecode_Type::FLOAT32:
            return false;
            break;
        case Bytecode_Type::FLOAT64:
            return false;
            break;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_AND:
        *(bool*)(dest) = *(bool*)(op_left) && *(bool*)(op_right);
        break;
    case Instruction_Type::BINARY_OP_OR:
        *(bool*)(dest) = *(bool*)(op_left) || *(bool*)(op_right);
        break;
    case Instruction_Type::BINARY_OP_BITWISE_AND:
        switch (type)
        {
        case Bytecode_Type::INT8:    *(i8*) (dest) = *(i8*) (op_left) & *(i8*) (op_right); break;
        case Bytecode_Type::INT16:   *(i16*)(dest) = *(i16*)(op_left) & *(i16*)(op_right); break;
        case Bytecode_Type::INT32:   *(i32*)(dest) = *(i32*)(op_left) & *(i32*)(op_right); break;
        case Bytecode_Type::INT64:   *(i64*)(dest) = *(i64*)(op_left) & *(i64*)(op_right); break;
        case Bytecode_Type::UINT8:   *(u8*) (dest) = *(u8*) (op_left) & *(u8*) (op_right); break;
        case Bytecode_Type::UINT16:  *(u16*)(dest) = *(u16*)(op_left) & *(u16*)(op_right); break;
        case Bytecode_Type::UINT32:  *(u32*)(dest) = *(u32*)(op_left) & *(u32*)(op_right); break;
        case Bytecode_Type::UINT64:  *(u64*)(dest) = *(u64*)(op_left) & *(u64*)(op_right); break;
        case Bytecode_Type::FLOAT32: 
        case Bytecode_Type::FLOAT64:
        case Bytecode_Type::BOOL: return false;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_BITWISE_OR:
        switch (type)
        {
        case Bytecode_Type::INT8:    *(i8*) (dest) = *(i8*) (op_left) | *(i8*) (op_right); break;
        case Bytecode_Type::INT16:   *(i16*)(dest) = *(i16*)(op_left) | *(i16*)(op_right); break;
        case Bytecode_Type::INT32:   *(i32*)(dest) = *(i32*)(op_left) | *(i32*)(op_right); break;
        case Bytecode_Type::INT64:   *(i64*)(dest) = *(i64*)(op_left) | *(i64*)(op_right); break;
        case Bytecode_Type::UINT8:   *(u8*) (dest) = *(u8*) (op_left) | *(u8*) (op_right); break;
        case Bytecode_Type::UINT16:  *(u16*)(dest) = *(u16*)(op_left) | *(u16*)(op_right); break;
        case Bytecode_Type::UINT32:  *(u32*)(dest) = *(u32*)(op_left) | *(u32*)(op_right); break;
        case Bytecode_Type::UINT64:  *(u64*)(dest) = *(u64*)(op_left) | *(u64*)(op_right); break;
        case Bytecode_Type::FLOAT32: 
        case Bytecode_Type::FLOAT64:
        case Bytecode_Type::BOOL: return false;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_BITWISE_XOR:
        switch (type)
        {
        case Bytecode_Type::INT8:    *(i8*) (dest) = *(i8*) (op_left) ^ *(i8*) (op_right); break;
        case Bytecode_Type::INT16:   *(i16*)(dest) = *(i16*)(op_left) ^ *(i16*)(op_right); break;
        case Bytecode_Type::INT32:   *(i32*)(dest) = *(i32*)(op_left) ^ *(i32*)(op_right); break;
        case Bytecode_Type::INT64:   *(i64*)(dest) = *(i64*)(op_left) ^ *(i64*)(op_right); break;
        case Bytecode_Type::UINT8:   *(u8*) (dest) = *(u8*) (op_left) ^ *(u8*) (op_right); break;
        case Bytecode_Type::UINT16:  *(u16*)(dest) = *(u16*)(op_left) ^ *(u16*)(op_right); break;
        case Bytecode_Type::UINT32:  *(u32*)(dest) = *(u32*)(op_left) ^ *(u32*)(op_right); break;
        case Bytecode_Type::UINT64:  *(u64*)(dest) = *(u64*)(op_left) ^ *(u64*)(op_right); break;
        case Bytecode_Type::FLOAT32: 
        case Bytecode_Type::FLOAT64:
        case Bytecode_Type::BOOL: return false;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_BITWISE_SHIFT_LEFT:
        switch (type)
        {
        case Bytecode_Type::INT8:    *(i8*) (dest) = *(i8*) (op_left) << *(i8*) (op_right); break;
        case Bytecode_Type::INT16:   *(i16*)(dest) = *(i16*)(op_left) << *(i16*)(op_right); break;
        case Bytecode_Type::INT32:   *(i32*)(dest) = *(i32*)(op_left) << *(i32*)(op_right); break;
        case Bytecode_Type::INT64:   *(i64*)(dest) = *(i64*)(op_left) << *(i64*)(op_right); break;
        case Bytecode_Type::UINT8:   *(u8*) (dest) = *(u8*) (op_left) << *(u8*) (op_right); break;
        case Bytecode_Type::UINT16:  *(u16*)(dest) = *(u16*)(op_left) << *(u16*)(op_right); break;
        case Bytecode_Type::UINT32:  *(u32*)(dest) = *(u32*)(op_left) << *(u32*)(op_right); break;
        case Bytecode_Type::UINT64:  *(u64*)(dest) = *(u64*)(op_left) << *(u64*)(op_right); break;
        case Bytecode_Type::FLOAT32: 
        case Bytecode_Type::FLOAT64:
        case Bytecode_Type::BOOL: return false;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_BITWISE_SHIFT_RIGHT:
        switch (type)
        {
        case Bytecode_Type::INT8:    *(i8*) (dest) = *(i8*) (op_left) >> *(i8*) (op_right); break;
        case Bytecode_Type::INT16:   *(i16*)(dest) = *(i16*)(op_left) >> *(i16*)(op_right); break;
        case Bytecode_Type::INT32:   *(i32*)(dest) = *(i32*)(op_left) >> *(i32*)(op_right); break;
        case Bytecode_Type::INT64:   *(i64*)(dest) = *(i64*)(op_left) >> *(i64*)(op_right); break;
        case Bytecode_Type::UINT8:   *(u8*) (dest) = *(u8*) (op_left) >> *(u8*) (op_right); break;
        case Bytecode_Type::UINT16:  *(u16*)(dest) = *(u16*)(op_left) >> *(u16*)(op_right); break;
        case Bytecode_Type::UINT32:  *(u32*)(dest) = *(u32*)(op_left) >> *(u32*)(op_right); break;
        case Bytecode_Type::UINT64:  *(u64*)(dest) = *(u64*)(op_left) >> *(u64*)(op_right); break;
        case Bytecode_Type::FLOAT32: 
        case Bytecode_Type::FLOAT64:
        case Bytecode_Type::BOOL: return false;
        default: return false;
        }
        break;
    default: return false;
    }
    return true;
}

void interpreter_safe_memcopy(Bytecode_Thread* thread, void* dst, void* src, int size)
{
    if (memory_is_readable(dst, size) && memory_is_readable(src, size)) {
        memory_copy(dst, src, size);
    }
    else {
        thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Invalid memory access");
    }
}

// Returns true if we need to stop execution, e.g. on exit instruction
void bytecode_thread_execute_current_instruction(Bytecode_Thread* thread)
{
    auto compilation_data = thread->compilation_data;
    auto& globals = compilation_data->globals;
    auto& constant_pool = compilation_data->constant_pool;
    auto& instructions = compilation_data->bytecode;

    Bytecode_Instruction* i = &instructions[thread->instruction_index];
    switch (i->instruction_type)
    {
    case Instruction_Type::MOVE_STACK_DATA:
        interpreter_safe_memcopy(thread, thread->stack_pointer + i->op1, thread->stack_pointer + i->op2, i->op3);
        break;
    case Instruction_Type::READ_GLOBAL: 
    {
        if (!thread->allow_global_access) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Cannot read extern global");
            return;
        }

        auto global = globals[i->op2];
        if (global->is_extern) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Cannot read extern global");
            return;
        }
        interpreter_safe_memcopy(thread, thread->stack_pointer + i->op1, globals[i->op2]->memory, i->op3);
        break;
    }
    case Instruction_Type::WRITE_GLOBAL: {
        auto global = globals[i->op2];
        if (global->is_extern) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Cannot write to extern global");
            return;
        }
        interpreter_safe_memcopy(thread, globals[i->op1]->memory, thread->stack_pointer + i->op2, i->op3);
        break;
    }
    case Instruction_Type::WRITE_MEMORY:
        interpreter_safe_memcopy(thread, *(void**)(thread->stack_pointer + i->op1), thread->stack_pointer + i->op2, i->op3);
        break;
    case Instruction_Type::READ_MEMORY: {
        void* result = *(void**)(thread->stack_pointer + i->op2);
        interpreter_safe_memcopy(thread, thread->stack_pointer + i->op1, *((void**)(thread->stack_pointer + i->op2)), i->op3);
        break;
    }
    case Instruction_Type::MEMORY_COPY:
        interpreter_safe_memcopy(thread, *(void**)(thread->stack_pointer + i->op1), *(void**)(thread->stack_pointer + i->op2), i->op3);
        break;
    case Instruction_Type::READ_CONSTANT:
        interpreter_safe_memcopy(thread, thread->stack_pointer + i->op1, constant_pool->constants[i->op2].memory, i->op3);
        break;
    case Instruction_Type::U64_ADD_CONSTANT_I32:
        *(u64*)(thread->stack_pointer + i->op1) = *(u64*)(thread->stack_pointer + i->op2) + (i->op3);
        break;
    case Instruction_Type::U64_MULTIPLY_ADD_I32: {
        u64 offset = (u64)((*(u32*)(thread->stack_pointer + i->op3)) * (u64)i->op4);
        if ((i32)offset < 0) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Multiply-Add (Used in array index calculations) went out of bounds");
            return;
        }
        *(u64**)(thread->stack_pointer + i->op1) = (u64*)(*(byte**)(thread->stack_pointer + i->op2) + offset);
        break;
    }
    case Instruction_Type::JUMP:
        thread->instruction_index = i->op1;
        return; // Return so we don't increment instruction-index
    case Instruction_Type::JUMP_ON_TRUE:
        if (*(thread->stack_pointer + i->op2) != 0) {
            thread->instruction_index = i->op1;
            return;
        }
        break;
    case Instruction_Type::JUMP_ON_FALSE:
        if (*(thread->stack_pointer + i->op2) == 0) {
            thread->instruction_index = i->op1;
            return;
        }
        break;
    case Instruction_Type::JUMP_ON_INT_EQUAL: {
        int value = *(int*)(thread->stack_pointer + i->op2);
        if (value == i->op3) {
            thread->instruction_index = i->op1;
            return;
        }
        break;
    }
    case Instruction_Type::CALL_FUNCTION_POINTER: 
    case Instruction_Type::CALL_FUNCTION: 
    {
        int function_index = 0;
        {
            if (i->instruction_type == Instruction_Type::CALL_FUNCTION) {
                function_index = i->op1 - 1;
            }
            else {
                function_index = (int)(*(i64*)(thread->stack_pointer + i->op1)) - 1;
            }
            if (function_index < 0 || function_index >= compilation_data->functions.size) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Function call failed call-instruction has invalid function index");
                return;
            }
        }

        Upp_Function* function = nullptr;
        {
            function = compilation_data->functions[function_index];
            if (function->is_extern) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Call to extern function not possible in bytecode");
                return;
            }
            else if (function->contains_errors) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Call to function with errors");
                return;
            }
            else if (function->poly_type == Poly_Type::BASE || function->poly_type == Poly_Type::PARTIAL) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Call to poly-base");
                return;
            }
            else if (function->bytecode_start_instruction == -1) {
                thread->exit_code = exit_code_make(Exit_Code_Type::CALL_TO_UNFINISHED_FUNCTION);
                thread->exit_code.options.waiting_for_function = function;
                return;
            }
        }

        // Check for stack-overflow
        int remaining_stack_size = &thread->stack[thread->stack.size - 1] - thread->stack_pointer;
        if (remaining_stack_size <= function->bytecode_maximum_stack_offset) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Stack overflow on normal function call");
            return;
        }

        byte* base_pointer = thread->stack_pointer;
        thread->stack_pointer = thread->stack_pointer + i->op2; 
        *((int*)thread->stack_pointer) = thread->instruction_index + 1; // Push return address
        *(byte**)(thread->stack_pointer + 8) = base_pointer; // Push current stack_pointer
        thread->instruction_index = function->bytecode_start_instruction; // Jump to function

        return;
    }
    case Instruction_Type::RETURN: 
    {
        // Check if we finished execution
        if (thread->stack_pointer == &thread->stack[0]) {
            thread->exit_code = exit_code_make(Exit_Code_Type::SUCCESS);
            return;
        }

        // Restore stack and instruction pointer
        int return_address = *(int*)thread->stack_pointer;
        byte* stack_old_base = *(byte**)(thread->stack_pointer + 8);
        thread->instruction_index = return_address;
        thread->stack_pointer = stack_old_base;
        return;
    }
    case Instruction_Type::EXIT: {
        thread->exit_code = exit_code_from_exit_instruction(*i);
        return;
    }
    case Instruction_Type::CALL_HARDCODED_FUNCTION:
    {
        Hardcoded_Type hardcoded_type = (Hardcoded_Type)i->op1;
        assert((u32)hardcoded_type < (int)Hardcoded_Type::MAX_ENUM_VALUE, "");
        Call_Signature* hardcoded_signature = compilation_data->hardcoded_function_signatures[(int)hardcoded_type];

        byte* return_buffer = thread->stack_pointer + i->op2 + 16; // Return buffer is after [return_instruction] [prev_stack_frame] [return_buffer] [params]...
        switch (hardcoded_type)
        {
        case Hardcoded_Type::SYSTEM_ALLOC: 
        {
            // System-alloc (u64 size) => address
            byte* argument_start = return_buffer + 8;
            u64 size = *(u64*)argument_start;
            if (size == 0) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Called malloc with size 0");
                return;
            }
            if (thread->heap_memory_consumption + size > thread->max_heap_consumption) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Reached maximum heap allocations");
                return;
            }

            void* alloc_data = thread->arena->allocate_raw(size, 16);
            thread->heap_memory_consumption += size;

            // logg("Allocated memory size: %5d, pointer: %p\n", size, alloc_data);
            memory_copy(return_buffer, &alloc_data, sizeof(void*));
            break;
        }
        case Hardcoded_Type::SYSTEM_FREE: 
        {
            // System-free fn (ptr: address)
            byte* argument_start = return_buffer;
            void* free_data = *(void**)argument_start;

            if (free_data == nullptr) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Free called on nullptr");
                return;
            }

            // logg("Interpreter Free pointer: %p\n", free_data);
            break;
        }
        case Hardcoded_Type::MEMORY_COPY: 
        {
            // fn (dst: address, src: address, size: usize)
            byte* argument_start = return_buffer;
            void* destination = *(void**)argument_start;
            void* source = *(void**)(argument_start + 8);
            u64 size = *(u64*)(argument_start + 16);
            interpreter_safe_memcopy(thread, destination, source, size);
            break;
        }
        case Hardcoded_Type::MEMORY_COMPARE: 
        {
            // fn (a: address, b: address, size: usize) => bool
            byte* argument_start = return_buffer + 8; // 1 byte for return-buffer, 7 byte alignment
            void* destination = *(void**)argument_start;
            void* source = *(void**)(argument_start + 8);
            u64 size = *(u64*)(argument_start + 16);
            if (!memory_is_readable(destination, size) || !memory_is_readable(source, size)) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Memory compare called with invalid pointers/size");
                return;
            }
            int cmp = memcmp(destination, source, size);
            *return_buffer = cmp == 0;
            break;
        }
        case Hardcoded_Type::MEMORY_ZERO: 
        {
            // fn (a: address, size: usize)
            byte* argument_start = return_buffer;
            void* destination = *(void**)argument_start;
            u64 size = *(u64*)(argument_start + 8);
            if (!memory_is_readable(destination, size)) {
                thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Memory zero called with invalid pointers/size");
                return;
            }
            memset(destination, 0, size);
            break;
        }
        case Hardcoded_Type::PRINT_I32: {
            byte* argument_start = return_buffer;
            i32 value = *(i32*)(argument_start);
            logg("%d", value); break;
        }
        case Hardcoded_Type::PRINT_F32: {
            byte* argument_start = return_buffer;
            logg("%3.2f", *(f32*)(argument_start)); break;
        }
        case Hardcoded_Type::PRINT_BOOL: {
            byte* argument_start = return_buffer;
            logg("%s", *(argument_start) == 0 ? "FALSE" : "TRUE"); break;
        }
        case Hardcoded_Type::PRINT_STRING: {
            byte* argument_start = return_buffer;
            Upp_String string = *(Upp_String*)argument_start;

            // Check if c_string size is correct
            if (string.size == 0) {break;}
            if (string.size >= 10000) {
                thread->exit_code = exit_code_make(
                    Exit_Code_Type::CODE_ERROR, 
                    "Print_string failed, slice size was too large, > 10000");
                return;
            }
            // Check if pointer data is correct
            if (!memory_is_readable((void*)string.data, string.size)) {
                thread->exit_code = exit_code_make(
                    Exit_Code_Type::CODE_ERROR, 
                    "Print c_string failed, memory of c_string was not readable");
                return;
            }

            logg("%.*s", string.size, (const char*) string.data);
            break;
        }
        case Hardcoded_Type::PRINT_LINE: {
            logg("\n"); break;
        }
        case Hardcoded_Type::READ_I32: {
            logg("Please input an i32: ");
            i32 num;
            std::cin >> num;
            if (std::cin.fail()) {
                num = 0;
            }
            std::cin.ignore(10000, '\n');
            std::cin.clear();
            interpreter_safe_memcopy(thread, return_buffer, &num, 4);
            break;
        }
        case Hardcoded_Type::READ_F32: {
            logg("Please input an f32: ");
            f32 num;
            std::cin >> num;
            if (std::cin.fail()) {
                num = 0;
            }
            std::cin.ignore(10000, '\n');
            std::cin.clear();
            interpreter_safe_memcopy(thread, return_buffer, &num, 4);
            break;
        }
        case Hardcoded_Type::READ_BOOL: {
            logg("Please input an bool (As int): ");
            i32 num;
            std::cin >> num;
            if (std::cin.fail()) {
                num = 0;
            }
            std::cin.ignore(10000, '\n');
            std::cin.clear();
            if (num == 0) {
                *return_buffer = false;
            }
            else {
                *return_buffer = true;
            }
            break;
        }
        case Hardcoded_Type::TYPE_INFO: 
        {
            auto& type_system = thread->compilation_data->type_system;

            byte* argument_start = thread->stack_pointer + i->op2 + 16;
            int type_index = *(int*)(argument_start);
            if (type_index > type_system->types.size || type_index < 0) {
                thread->exit_code = exit_code_make(
                    Exit_Code_Type::CODE_ERROR, 
                    "type_info failed, type-handle was invalid value");
                return;
            }
            if (type_size_is_unfinished(type_system->types[type_index])) {
                thread->exit_code = exit_code_make(Exit_Code_Type::TYPE_INFO_WAITING_FOR_TYPE_FINISHED);
                thread->exit_code.options.waiting_for_type_finish_type = type_system->types[type_index];
                return;
            }
            *((Internal_Type_Information**)return_buffer) = type_system->types[type_index]->internal_info;
            break;
        }
        default: {panic("What"); }
        }
        break;
    }
    case Instruction_Type::LOAD_REGISTER_ADDRESS:
        *(void**)(thread->stack_pointer + i->op1) = (void*)(thread->stack_pointer + i->op2);
        break;
    case Instruction_Type::LOAD_GLOBAL_ADDRESS: 
    {
        if (!thread->allow_global_access) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Trying to load global variable address");
            return;
        }
        auto global = globals[i->op2];
        if (global->is_extern) {
            thread->exit_code = exit_code_make(Exit_Code_Type::EXECUTION_ERROR, "Cannot load extern global address");
            return;
        }
        *(void**)(thread->stack_pointer + i->op1) = (void*)(globals[i->op2]->memory);
        break;
    }
    case Instruction_Type::LOAD_CONSTANT_ADDRESS:
        *(void**)(thread->stack_pointer + i->op1) = (void*)(constant_pool->constants[i->op2].memory);
        break;
    case Instruction_Type::LOAD_FUNCTION_LOCATION:
        *(i64*)(thread->stack_pointer + i->op1) = (i64)(i->op2 + 1); // Note: Function pointers are encoded as function indices in interpreter
        break;
    case Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE: 
    case Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE: 
    case Instruction_Type::CAST_FLOAT_INTEGER: 
    case Instruction_Type::CAST_INTEGER_FLOAT: {
        bytecode_execute_cast_instr(
            i->instruction_type,
            thread->stack_pointer + i->op1,
            thread->stack_pointer + i->op2,
            (Bytecode_Type)i->op3,
            (Bytecode_Type)i->op4
        );
        break;
    }

    /*
    -------------------------
    --- BINARY_OPERATIONS ---
    -------------------------
    */
    case Instruction_Type::BINARY_OP_ADDITION:
    case Instruction_Type::BINARY_OP_SUBTRACTION:
    case Instruction_Type::BINARY_OP_MULTIPLICATION:
    case Instruction_Type::BINARY_OP_DIVISION:
    case Instruction_Type::BINARY_OP_EQUAL:
    case Instruction_Type::BINARY_OP_NOT_EQUAL:
    case Instruction_Type::BINARY_OP_GREATER_THAN:
    case Instruction_Type::BINARY_OP_GREATER_EQUAL:
    case Instruction_Type::BINARY_OP_LESS_THAN:
    case Instruction_Type::BINARY_OP_LESS_EQUAL:
    case Instruction_Type::BINARY_OP_MODULO:
    case Instruction_Type::BINARY_OP_BITWISE_AND:
    case Instruction_Type::BINARY_OP_BITWISE_OR:
    case Instruction_Type::BINARY_OP_BITWISE_XOR:
    case Instruction_Type::BINARY_OP_BITWISE_SHIFT_LEFT:
    case Instruction_Type::BINARY_OP_BITWISE_SHIFT_RIGHT:
    case Instruction_Type::BINARY_OP_AND:
    case Instruction_Type::BINARY_OP_OR: 
    {
        if (!bytecode_execute_binary_instr(
            i->instruction_type,
            (Bytecode_Type)i->op4,
            thread->stack_pointer + i->op1,
            thread->stack_pointer + i->op2,
            thread->stack_pointer + i->op3
        )) {
            thread->exit_code = exit_code_make(Exit_Code_Type::CODE_ERROR, "Binary op execution failed");
        }
        break;
    }
    case Instruction_Type::UNARY_OP_NOT:
    case Instruction_Type::UNARY_OP_BITWISE_NOT:
    case Instruction_Type::UNARY_OP_NEGATE:
        bytecode_execute_unary_instr(
            i->instruction_type,
            (Bytecode_Type)i->op3,
            thread->stack_pointer + i->op1,
            thread->stack_pointer + i->op2
        );
        break;
    default: {
        panic("Should not happen!\n");
    }
    }

    if (thread->exit_code.type != Exit_Code_Type::RUNNING) {
        return;
    }
    thread->instruction_index += 1;
    thread->executed_instruction_count += 1;
}

void bytecode_thread_print_state(Bytecode_Thread* interpreter)
{
    /*
    int current_instruction_index = thread->instruction_index - thread->generator->instructions.data;
    int current_function_index = -1;
    for (int i = 0; i < thread->generator->function_locations.size; i++)
    {
        int function_start_loc = thread->generator->function_locations[i];
        int function_end_loc_exclusive = thread->generator->instructions.size;
        if (i + 1 < thread->generator->function_locations.size) {
            function_end_loc_exclusive = thread->generator->function_locations[i + 1];
        }
        if (current_instruction_index >= function_start_loc && current_instruction_index < function_end_loc_exclusive) {
            current_function_index = i;
            break;
        }
    }
    if (current_function_index == -1) panic("Should not happen!\n");

    logg("\n\n\n\n---------------------- CURRENT STATE ----------------------\n");
    logg("Current Function: %s\n", identifier_pool_index_to_string(&thread->compiler->code_source, func->name_handle).characters);
    logg("Current Stack offset: %d\n", thread->stack.data - thread->stack_pointer);
    logg("Instruction Index: %d\n", current_instruction_index);
    {
        String tmp = string_create(64);
        SCOPE_EXIT(string_destroy(&tmp));
        bytecode_instruction_append_to_string(&tmp, thread->generator->instructions[current_instruction_index]);
        logg("Instruction: %s\n", tmp.characters);
    }
    */
    /*
    for (int i = 0; i < func->registers.size; i++)
    {
        Intermediate_Register* reg = &func->registers[i];
        int stack_offset = thread->generator->variable_stack_offsets[i];
        byte* reg_data_ptr = thread->stack_pointer + stack_offset;
        Datatype* reg_type = reg->type_signature;
        if (reg->type == Intermediate_Register_Type::PARAMETER) {
            logg("Parameter %d (Offset %d): ", reg->defined_in_parameter_index, stack_offset);
        }
        else if (reg->type == Intermediate_Register_Type::VARIABLE) {
            logg("Variable %s (Offset %d): ",
                identifier_pool_index_to_string(thread->generator->im_generator->analyser->parser->code_source, reg->id).characters,
                stack_offset
            );
        }
        else if (reg->type == Intermediate_Register_Type::EXPRESSION_RESULT) {
            logg("Expression %d (Offset %d): ", i, stack_offset);
        }

        type_signature_print_value(reg_type, reg_data_ptr);
        logg("\n");
    }
    */
}

void bytecode_thread_set_initial_state(Bytecode_Thread* thread, Upp_Function* entry_function)
{
    // Note parameters would be possible, but then we would need to initialze the stack correctly first
    assert(entry_function->signature->param_count() == 0, "Entry function cannot have parameters currently");
    assert(entry_function->bytecode_start_instruction != -1, "Function should be compiled at this point");

    thread->stack_pointer = &thread->stack[0];
    thread->instruction_index = entry_function->bytecode_start_instruction;
    thread->executed_instruction_count = 0;
    thread->heap_memory_consumption = 0;
}

Exit_Code bytecode_thread_execute(Bytecode_Thread* thread)
{
    Timing_Task before_task = thread->compilation_data->task_current;
    compilation_data_switch_timing_task(thread->compilation_data, Timing_Task::CODE_EXEC);

    thread->exit_code = exit_code_make(Exit_Code_Type::RUNNING);
    __try
    {
        while (true) 
        {
            //bytecode_thread_print_state(thread);
            bytecode_thread_execute_current_instruction(thread);
            if (thread->exit_code.type != Exit_Code_Type::RUNNING) {
                break;
            }
            if (thread->max_instruction_executions > 0 && thread->executed_instruction_count >= thread->max_instruction_executions) {
                thread->exit_code = exit_code_make(Exit_Code_Type::INSTRUCTION_LIMIT_REACHED);
                break;
            }
        }
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
        GetExceptionCode() == EXCEPTION_ARRAY_BOUNDS_EXCEEDED ||
        GetExceptionCode() == EXCEPTION_DATATYPE_MISALIGNMENT ||
        GetExceptionCode() == EXCEPTION_GUARD_PAGE ||
        GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ||
        GetExceptionCode() == EXCEPTION_INT_DIVIDE_BY_ZERO ||
        GetExceptionCode() == EXCEPTION_INVALID_HANDLE ||
        GetExceptionCode() == EXCEPTION_PRIV_INSTRUCTION ||
        GetExceptionCode() == EXCEPTION_STACK_OVERFLOW) 
    {
        thread->exit_code = exit_code_make(Exit_Code_Type::CODE_ERROR, "Internal exception occured (Division by 0, invalid memory access, ...)");
    }

    compilation_data_switch_timing_task(thread->compilation_data, before_task);
    return thread->exit_code;
}

void* bytecode_thread_get_return_value_ptr(Bytecode_Thread* thread) {
    return &thread->stack[16]; // Return value starts at offset 16 in stack frame
}
