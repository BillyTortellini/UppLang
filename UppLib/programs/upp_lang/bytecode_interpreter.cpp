#include "bytecode_interpreter.hpp"

#include <iostream>
#include "../../utility/random.hpp"
#include "compiler.hpp"
#include <Windows.h>
#include "ir_code.hpp"

Bytecode_Thread* bytecode_thread_create(int instruction_limit)
{
    Bytecode_Thread* result = new Bytecode_Thread;
    result->heap_allocator = stack_allocator_create_empty(1024);
    result->instruction_limit = instruction_limit;
    result->waiting_for_type_finish_type = 0;
    return result;
}

void bytecode_thread_destroy(Bytecode_Thread* thread) {
    stack_allocator_destroy(&thread->heap_allocator);
    delete thread;
}

void bytecode_execute_cast_instr(Instruction_Type instr_type, void* dest, void* src, Bytecode_Type dest_type, Bytecode_Type src_type)
{
    switch (instr_type)
    {
    case Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE: {
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
        case Bytecode_Type::INT8:   *(i8*)(dest) = (i8)source_is_signed ? source_signed : source_unsigned; break;
        case Bytecode_Type::INT16:  *(i16*)(dest) = (i16)source_is_signed ? source_signed : source_unsigned; break;
        case Bytecode_Type::INT32:  *(i32*)(dest) = (i32)source_is_signed ? source_signed : source_unsigned; break;
        case Bytecode_Type::INT64:  *(i64*)(dest) = (i64)source_is_signed ? source_signed : source_unsigned; break;
        case Bytecode_Type::UINT8:  *(u8*)(dest) = (u8)source_is_signed ? source_signed : source_unsigned; break;
        case Bytecode_Type::UINT16: *(u16*)(dest) = (u16)source_is_signed ? source_signed : source_unsigned; break;
        case Bytecode_Type::UINT32: *(u32*)(dest) = (u32)source_is_signed ? source_signed : source_unsigned; break;
        case Bytecode_Type::UINT64: *(u64*)(dest) = (u64)source_is_signed ? source_signed : source_unsigned; break;
        default: panic("what the frigg\n");
        }
        break;
    }
    case Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE: {
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
    case Instruction_Type::CAST_FLOAT_INTEGER: {
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
    case Instruction_Type::UNARY_OP_NEGATE:
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
        case Bytecode_Type::BOOL:
            return false;
        case Bytecode_Type::INT8:
            *(i8*)(dest) = *(i8*)(op_left)+*(i8*)(op_right);
            break;
        case Bytecode_Type::INT16:
            *(i16*)(dest) = *(i16*)(op_left)+*(i16*)(op_right);
            break;
        case Bytecode_Type::INT32:
            *(i32*)(dest) = *(i32*)(op_left)+*(i32*)(op_right);
            break;
        case Bytecode_Type::INT64:
            *(i64*)(dest) = *(i64*)(op_left)+*(i64*)(op_right);
            break;
        case Bytecode_Type::UINT8:
            *(u8*)(dest) = *(u8*)(op_left)+*(u8*)(op_right);
            break;
        case Bytecode_Type::UINT16:
            *(u16*)(dest) = *(u16*)(op_left)+*(u16*)(op_right);
            break;
        case Bytecode_Type::UINT32:
            *(u32*)(dest) = *(u32*)(op_left)+*(u32*)(op_right);
            break;
        case Bytecode_Type::UINT64:
            *(u64*)(dest) = *(u64*)(op_left)+*(u64*)(op_right);
            break;
        case Bytecode_Type::FLOAT32:
            *(f32*)(dest) = *(f32*)(op_left)+*(f32*)(op_right);
            break;
        case Bytecode_Type::FLOAT64:
            *(f64*)(dest) = *(f64*)(op_left)+*(f64*)(op_right);
            break;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_SUBTRACTION:
        switch (type)
        {
        case Bytecode_Type::BOOL:
            return false;
        case Bytecode_Type::INT8:
            *(i8*)(dest) = *(i8*)(op_left)-*(i8*)(op_right);
            break;
        case Bytecode_Type::INT16:
            *(i16*)(dest) = *(i16*)(op_left)-*(i16*)(op_right);
            break;
        case Bytecode_Type::INT32:
            *(i32*)(dest) = *(i32*)(op_left)-*(i32*)(op_right);
            break;
        case Bytecode_Type::INT64:
            *(i64*)(dest) = *(i64*)(op_left)-*(i64*)(op_right);
            break;
        case Bytecode_Type::UINT8:
            *(u8*)(dest) = *(u8*)(op_left)-*(u8*)(op_right);
            break;
        case Bytecode_Type::UINT16:
            *(u16*)(dest) = *(u16*)(op_left)-*(u16*)(op_right);
            break;
        case Bytecode_Type::UINT32:
            *(u32*)(dest) = *(u32*)(op_left)-*(u32*)(op_right);
            break;
        case Bytecode_Type::UINT64:
            *(u64*)(dest) = *(u64*)(op_left)-*(u64*)(op_right);
            break;
        case Bytecode_Type::FLOAT32:
            *(f32*)(dest) = *(f32*)(op_left)-*(f32*)(op_right);
            break;
        case Bytecode_Type::FLOAT64:
            *(f64*)(dest) = *(f64*)(op_left)-*(f64*)(op_right);
            break;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_MULTIPLICATION:
        switch (type)
        {
        case Bytecode_Type::BOOL:
            return false;
        case Bytecode_Type::INT8:
            *(i8*)(dest) = *(i8*)(op_left) * *(i8*)(op_right);
            break;
        case Bytecode_Type::INT16:
            *(i16*)(dest) = *(i16*)(op_left) * *(i16*)(op_right);
            break;
        case Bytecode_Type::INT32:
            *(i32*)(dest) = *(i32*)(op_left) * *(i32*)(op_right);
            break;
        case Bytecode_Type::INT64:
            *(i64*)(dest) = *(i64*)(op_left) * *(i64*)(op_right);
            break;
        case Bytecode_Type::UINT8:
            *(u8*)(dest) = *(u8*)(op_left) * *(u8*)(op_right);
            break;
        case Bytecode_Type::UINT16:
            *(u16*)(dest) = *(u16*)(op_left) * *(u16*)(op_right);
            break;
        case Bytecode_Type::UINT32:
            *(u32*)(dest) = *(u32*)(op_left) * *(u32*)(op_right);
            break;
        case Bytecode_Type::UINT64:
            *(u64*)(dest) = *(u64*)(op_left) * *(u64*)(op_right);
            break;
        case Bytecode_Type::FLOAT32:
            *(f32*)(dest) = *(f32*)(op_left) * *(f32*)(op_right);
            break;
        case Bytecode_Type::FLOAT64:
            *(f64*)(dest) = *(f64*)(op_left) * *(f64*)(op_right);
            break;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_DIVISION:
        switch (type)
        {
        case Bytecode_Type::BOOL:
            return false;
        case Bytecode_Type::INT8:
            if (*(i8*)op_right == 0) return false;
            *(i8*)(dest) = *(i8*)(op_left) / *(i8*)(op_right);
            break;
        case Bytecode_Type::INT16:
            if (*(i16*)op_right == 0) return false;
            *(i16*)(dest) = *(i16*)(op_left) / *(i16*)(op_right);
            break;
        case Bytecode_Type::INT32: {
            if (*(i32*)op_right == 0) return false;
            *(i32*)(dest) = *(i32*)(op_left) / *(i32*)(op_right);
            break;
        }
        case Bytecode_Type::INT64:
            if (*(i64*)op_right == 0) return false;
            *(i64*)(dest) = *(i64*)(op_left) / *(i64*)(op_right);
            break;
        case Bytecode_Type::UINT8:
            if (*(u8*)op_right == 0) return false;
            *(u8*)(dest) = *(u8*)(op_left) / *(u8*)(op_right);
            break;
        case Bytecode_Type::UINT16:
            if (*(u16*)op_right == 0) return false;
            *(u16*)(dest) = *(u16*)(op_left) / *(u16*)(op_right);
            break;
        case Bytecode_Type::UINT32:
            if (*(u32*)op_right == 0) return false;
            *(u32*)(dest) = *(u32*)(op_left) / *(u32*)(op_right);
            break;
        case Bytecode_Type::UINT64:
            if (*(u64*)op_right == 0) return false;
            *(u64*)(dest) = *(u64*)(op_left) / *(u64*)(op_right);
            break;
        case Bytecode_Type::FLOAT32:
            *(f32*)(dest) = *(f32*)(op_left) / *(f32*)(op_right);
            break;
        case Bytecode_Type::FLOAT64:
            *(f64*)(dest) = *(f64*)(op_left) / *(f64*)(op_right);
            break;
        default: return false;
        }
        break;
    case Instruction_Type::BINARY_OP_EQUAL:
        switch (type)
        {
        case Bytecode_Type::BOOL:
            *(u8*)(dest) = *(u8*)(op_left) == *(u8*)(op_right) ? 1 : 0;
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
    default: return false;
    }
    return true;
}

void interpreter_safe_memcopy(Bytecode_Thread* thread, void* dst, void* src, int size)
{
    if (memory_is_readable(dst, size) && memory_is_readable(src, size)) {
        memory_copy(dst, src, size);
        return;
    }
    thread->exit_code = Exit_Code::CODE_ERROR_OCCURED;
    thread->error_occured = true;
}

// Returns true if we need to stop execution, e.g. on exit instruction
bool bytecode_thread_execute_current_instruction(Bytecode_Thread* thread)
{
    auto& globals = compiler.semantic_analyser->program->globals;
    auto& constant_pool = compiler.constant_pool;
    auto& generator = compiler.bytecode_generator;
    auto& instructions = compiler.bytecode_generator->instructions;

    Bytecode_Instruction* i = &instructions[thread->instruction_index];
    switch (i->instruction_type)
    {
    case Instruction_Type::MOVE_STACK_DATA:
        interpreter_safe_memcopy(thread, thread->stack_pointer + i->op1, thread->stack_pointer + i->op2, i->op3);
        break;
    case Instruction_Type::READ_GLOBAL:
        interpreter_safe_memcopy(thread, thread->stack_pointer + i->op1, globals[i->op2]->memory, i->op3);
        break;
    case Instruction_Type::WRITE_GLOBAL:
        interpreter_safe_memcopy(thread, globals[i->op1]->memory, thread->stack_pointer + i->op2, i->op3);
        break;
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
        interpreter_safe_memcopy(thread, thread->stack_pointer + i->op1, constant_pool.constants[i->op2].memory, i->op3);
        break;
    case Instruction_Type::U64_ADD_CONSTANT_I32:
        *(u64*)(thread->stack_pointer + i->op1) = *(u64*)(thread->stack_pointer + i->op2) + (i->op3);
        break;
    case Instruction_Type::U64_MULTIPLY_ADD_I32: {
        u64 offset = (u64)((*(u32*)(thread->stack_pointer + i->op3)) * (u64)i->op4);
        if ((i32)offset < 0) {
            thread->exit_code = Exit_Code::OUT_OF_BOUNDS;
            return true;
        }
        *(u64**)(thread->stack_pointer + i->op1) = (u64*)(*(byte**)(thread->stack_pointer + i->op2) + offset);
        break;
    }
    case Instruction_Type::JUMP:
        thread->instruction_index = i->op1;
        return false;
    case Instruction_Type::JUMP_ON_TRUE:
        if (*(thread->stack_pointer + i->op2) != 0) {
            thread->instruction_index = i->op1;
            return false;
        }
        break;
    case Instruction_Type::JUMP_ON_FALSE:
        if (*(thread->stack_pointer + i->op2) == 0) {
            thread->instruction_index = i->op1;
            return false;
        }
        break;
    case Instruction_Type::CALL_FUNCTION: {
        if (&thread->stack[INTERPRETER_STACK_SIZE - 1] - thread->stack_pointer <= generator->maximum_function_stack_depth) {
            thread->exit_code = Exit_Code::STACK_OVERFLOW;
            return true;
        }
        byte* base_pointer = thread->stack_pointer;
        thread->stack_pointer = thread->stack_pointer + i->op2; 
        *((int*)thread->stack_pointer) = thread->instruction_index + 1; // Push return address
        *(byte**)(thread->stack_pointer + 8) = base_pointer; // Push current stack_pointer
        thread->instruction_index = i->op1; // Jump to function

        return false;
    }
    case Instruction_Type::CALL_FUNCTION_POINTER: {
        if (&thread->stack[INTERPRETER_STACK_SIZE - 1] - thread->stack_pointer < generator->maximum_function_stack_depth) {
            thread->exit_code = Exit_Code::STACK_OVERFLOW;
            return true;
        }

        // Check if function pointer is 0 and other things
        int jmp_to_instr_index;
        {
            int function_index = (int)(*(i64*)(thread->stack_pointer + i->op1)) - 1;
            auto& functions = compiler.semantic_analyser->program->functions;
            if (function_index < 0 || function_index >= functions.size) {
                thread->exit_code = Exit_Code::CODE_ERROR_OCCURED;
                return true;
            }

            auto ir_function = *hashtable_find_element(&compiler.ir_generator->function_mapping, functions[function_index]);
            jmp_to_instr_index = *hashtable_find_element(&compiler.bytecode_generator->function_locations, ir_function);
        }

        if (jmp_to_instr_index < 0 || jmp_to_instr_index > instructions.size) {
            thread->exit_code = Exit_Code::RETURN_VALUE_OVERFLOW;
            return true;
        }

        byte* base_pointer = thread->stack_pointer;
        thread->stack_pointer = thread->stack_pointer + i->op2;
        *((int*)thread->stack_pointer) = thread->instruction_index + 1; // Push return address
        *(byte**)(thread->stack_pointer + 8) = base_pointer; // Push current stack_pointer
        thread->instruction_index = jmp_to_instr_index; // Jump to function

        return false;
    }
    case Instruction_Type::RETURN: {
        // Copy result into return register
        if (i->op2 > 256) {
            thread->exit_code = Exit_Code::RETURN_VALUE_OVERFLOW;
            return true;
        }
        memory_copy(thread->return_register, thread->stack_pointer + i->op1, i->op2);

        // Check if we finished execution
        if (thread->stack_pointer == &thread->stack[0]) {
            thread->exit_code = Exit_Code::SUCCESS;
            return true;
        }

        // Restore stack and instruction pointer
        int return_address = *(int*)thread->stack_pointer;
        byte* stack_old_base = *(byte**)(thread->stack_pointer + 8);
        thread->instruction_index = return_address;
        thread->stack_pointer = stack_old_base;
        return false;
    }
    case Instruction_Type::EXIT: {
        thread->exit_code = (Exit_Code)i->op1;
        return true;
    }
    case Instruction_Type::CALL_HARDCODED_FUNCTION:
    {
        Hardcoded_Type hardcoded_type = (Hardcoded_Type)i->op1;
        memory_set_bytes(&thread->return_register[0], 256, 0);
        switch (hardcoded_type)
        {
        case Hardcoded_Type::MALLOC_SIZE_I32: {
            byte* argument_start = thread->stack_pointer + i->op2 - 8;
            i32 size = *(i32*)argument_start;
            assert(size != 0, "");
            void* alloc_data = malloc(size);
            // logg("Allocated memory size: %5d, pointer: %p\n", size, alloc_data);
            memory_copy(thread->return_register, &alloc_data, 8);
            break;
        }
        case Hardcoded_Type::FREE_POINTER: {
            byte* argument_start = thread->stack_pointer + i->op2 - 8;
            void* free_data = *(void**)argument_start;
            // logg("Interpreter Free pointer: %p\n", free_data);
            free(free_data);
            *(void**)argument_start = (void*)1;
            break;
        }
        case Hardcoded_Type::PRINT_I32: {
            byte* argument_start = thread->stack_pointer + i->op2 - 8;
            i32 value = *(i32*)(argument_start);
            logg("%d", value); break;
        }
        case Hardcoded_Type::PRINT_F32: {
            byte* argument_start = thread->stack_pointer + i->op2 - 8;
            logg("%3.2f", *(f32*)(argument_start)); break;
        }
        case Hardcoded_Type::PRINT_BOOL: {
            byte* argument_start = thread->stack_pointer + i->op2 - 8;
            logg("%s", *(argument_start) == 0 ? "FALSE" : "TRUE"); break;
        }
        case Hardcoded_Type::PRINT_STRING: {
            byte* argument_start = thread->stack_pointer + i->op2 - sizeof(Upp_C_String);
            Upp_C_String string = *(Upp_C_String*)argument_start;

            // Check if string size is correct
            if (string.slice.size == 0) {break;}
            if (string.slice.size >= 10000) {
                thread->error_occured = true;
                thread->exit_code = Exit_Code::OUT_OF_BOUNDS;
                return true;
            }
            // Check if pointer data is correct
            if (!memory_is_readable((void*)string.slice.data_ptr, string.slice.size)) {
                thread->error_occured = true;
                thread->exit_code = Exit_Code::OUT_OF_BOUNDS;
                return true;
            }
            // Check if string is correctly null-terminated
            if (!string.slice.data_ptr[string.slice.size - 1] == '\0') {
                thread->error_occured = true;
                thread->exit_code = Exit_Code::CODE_ERROR_OCCURED;
                return true;
            }

            logg("%s", (const char*) string.slice.data_ptr);
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
            interpreter_safe_memcopy(thread, thread->return_register, &num, 4);
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
            interpreter_safe_memcopy(thread, thread->return_register, &num, 4);
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
                thread->return_register[0] = 0;
            }
            else {
                thread->return_register[0] = 1;
            }
            break;
        }
        case Hardcoded_Type::RANDOM_I32: {
            i32 result = random_next_u32(&compiler.random);
            interpreter_safe_memcopy(thread, thread->return_register, &result, 4);
            break;
        }
        case Hardcoded_Type::TYPE_INFO: {
            byte* argument_start = thread->stack_pointer + i->op2 - 8;
            int type_index = *(int*)(argument_start);
            if (type_index > compiler.type_system.types.size || type_index < 0) {
                thread->error_occured = true;
                thread->exit_code = Exit_Code::CODE_ERROR_OCCURED;
                return true;
            }
            if (type_size_is_unfinished(compiler.type_system.types[type_index])) {
                thread->error_occured = true;
                thread->exit_code = Exit_Code::TYPE_INFO_WAITING_FOR_TYPE_FINISHED;
                thread->waiting_for_type_finish_type = compiler.type_system.types[type_index];
                return true;
            }
            *((Internal_Type_Information**)&thread->return_register[0]) = compiler.type_system.internal_type_infos[type_index];
            break;
        }
        default: {panic("What"); }
        }
        break;
    }
    case Instruction_Type::LOAD_RETURN_VALUE:
        interpreter_safe_memcopy(thread, thread->stack_pointer + i->op1, &thread->return_register[0], i->op2);
        break;
    case Instruction_Type::LOAD_REGISTER_ADDRESS:
        *(void**)(thread->stack_pointer + i->op1) = (void*)(thread->stack_pointer + i->op2);
        break;
    case Instruction_Type::LOAD_GLOBAL_ADDRESS:
        *(void**)(thread->stack_pointer + i->op1) = (void*)(globals[i->op2]->memory);
        break;
    case Instruction_Type::LOAD_CONSTANT_ADDRESS:
        *(void**)(thread->stack_pointer + i->op1) = (void*)(constant_pool.constants[i->op2].memory);
        break;
    case Instruction_Type::LOAD_FUNCTION_LOCATION:
        *(i64*)(thread->stack_pointer + i->op1) = (i64)i->op2; // Note: Function pointers are encoded as function indices in interpreter
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
    case Instruction_Type::BINARY_OP_AND:
    case Instruction_Type::BINARY_OP_OR: {
        if (!bytecode_execute_binary_instr(
            i->instruction_type,
            (Bytecode_Type)i->op4,
            thread->stack_pointer + i->op1,
            thread->stack_pointer + i->op2,
            thread->stack_pointer + i->op3
        )) {
            thread->error_occured = true;
            thread->exit_code = Exit_Code::CODE_ERROR_OCCURED;
        }
        break;
    }
    case Instruction_Type::UNARY_OP_NOT:
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
        return true;
    }
    }

    thread->instruction_index += 1;
    return false;
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
        String tmp = string_create_empty(64);
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

void bytecode_thread_set_initial_state(Bytecode_Thread* thread, int function_start_index)
{
    // Reset/Zero out previous runs
    memory_set_bytes(&thread->return_register, 256, 0);
    memory_set_bytes(&thread->stack[0], 16, 0); // Sets return address and old stack pointer to 0
    thread->stack_pointer = &thread->stack[0];
    thread->instruction_index = function_start_index;
}

void bytecode_thread_execute(Bytecode_Thread* thread)
{
    Timing_Task before_task = compiler.task_current;
    compiler_switch_timing_task(Timing_Task::CODE_EXEC);
    thread->error_occured = false;
    thread->waiting_for_type_finish_type = 0;
    thread->exit_code = Exit_Code::SUCCESS;

    int executed_instruction_count = 0;
    __try
    {
        while (!thread->error_occured) {
            //bytecode_thread_print_state(thread);
            if (bytecode_thread_execute_current_instruction(thread)) { break; }
            executed_instruction_count++;
            if (thread->instruction_limit > 0 && executed_instruction_count > thread->instruction_limit) {
                thread->exit_code = Exit_Code::INSTRUCTION_LIMIT_REACHED;
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
        GetExceptionCode() == EXCEPTION_STACK_OVERFLOW) {
        thread->exit_code = Exit_Code::CODE_ERROR_OCCURED;
    }

    compiler_switch_timing_task(before_task);
}
