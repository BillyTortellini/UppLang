#include "bytecode_interpreter.hpp"

#include <iostream>
#include "../../utility/random.hpp"
#include "compiler.hpp"

Bytecode_Interpreter bytecode_intepreter_create()
{
    Bytecode_Interpreter result;
    result.stack = array_create_empty<byte>(8192);
    result.globals.data = 0;
    result.random = random_make_time_initalized();
    return result;
}

void bytecode_interpreter_destroy(Bytecode_Interpreter* interpreter) {
    array_destroy(&interpreter->stack);
    if (interpreter->globals.data != 0) {
        array_destroy(&interpreter->globals);
    }
}

// Returns true if we need to stop execution, e.g. on exit instruction
bool bytecode_interpreter_execute_current_instruction(Bytecode_Interpreter* interpreter)
{
    Bytecode_Instruction* i = interpreter->instruction_pointer;
    switch (i->instruction_type)
    {
    case Instruction_Type::MOVE_STACK_DATA:
        memory_copy(interpreter->stack_pointer + i->op1, interpreter->stack_pointer + i->op2, i->op3);
        break;
    case Instruction_Type::READ_GLOBAL:
        memory_copy(interpreter->stack_pointer + i->op1, interpreter->globals.data + i->op2, i->op3);
        break;
    case Instruction_Type::WRITE_GLOBAL:
        memory_copy(interpreter->globals.data + i->op1, interpreter->stack_pointer + i->op2, i->op3);
        break;
    case Instruction_Type::WRITE_MEMORY:
        memory_copy(*(void**)(interpreter->stack_pointer + i->op1), interpreter->stack_pointer + i->op2, i->op3);
        break;
    case Instruction_Type::READ_MEMORY: {
        void* result = *(void**)(interpreter->stack_pointer + i->op2);
        memory_copy(interpreter->stack_pointer + i->op1, *((void**)(interpreter->stack_pointer + i->op2)), i->op3);
        break;
    }
    case Instruction_Type::MEMORY_COPY:
        memory_copy(*(void**)(interpreter->stack_pointer + i->op1), *(void**)(interpreter->stack_pointer + i->op2), i->op3);
        break;
    case Instruction_Type::READ_CONSTANT:
        memory_copy(interpreter->stack_pointer + i->op1, interpreter->compiler->analyser.program->constant_pool.constant_memory.data + i->op2, i->op3);
        break;
    case Instruction_Type::U64_ADD_CONSTANT_I32:
        *(u64*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) + (i->op3);
        break;
    case Instruction_Type::U64_MULTIPLY_ADD_I32: {
        u64 offset = (u64)((*(u32*)(interpreter->stack_pointer + i->op3)) * (u64)i->op4);
        if ((i32)offset < 0) {
            interpreter->exit_code = IR_Exit_Code::OUT_OF_BOUNDS;
            return true;
        }
        *(u64**)(interpreter->stack_pointer + i->op1) = (u64*)(*(byte**)(interpreter->stack_pointer + i->op2) + offset);
        break;
    }
    case Instruction_Type::JUMP:
        interpreter->instruction_pointer = &interpreter->generator->instructions[i->op1];
        return false;
    case Instruction_Type::JUMP_ON_TRUE:
        if (*(interpreter->stack_pointer + i->op2) != 0) {
            interpreter->instruction_pointer = &interpreter->generator->instructions[i->op1];
            return false;
        }
        break;
    case Instruction_Type::JUMP_ON_FALSE:
        if (*(interpreter->stack_pointer + i->op2) == 0) {
            interpreter->instruction_pointer = &interpreter->generator->instructions[i->op1];
            return false;
        }
        break;
    case Instruction_Type::CALL_FUNCTION: {
        if (&interpreter->stack[interpreter->stack.size-1] - interpreter->stack_pointer < interpreter->generator->maximum_function_stack_depth) {
            interpreter->exit_code = IR_Exit_Code::STACK_OVERFLOW;
            return true;
        }
        byte* base_pointer = interpreter->stack_pointer;
        Bytecode_Instruction* next = interpreter->instruction_pointer + 1;
        interpreter->stack_pointer = interpreter->stack_pointer + i->op2;
        *((Bytecode_Instruction**)interpreter->stack_pointer) = next;
        *(byte**)(interpreter->stack_pointer + 8) = base_pointer;
        interpreter->instruction_pointer = &interpreter->generator->instructions[i->op1];

        return false;
    }
    case Instruction_Type::CALL_FUNCTION_POINTER: {
        if (&interpreter->stack[interpreter->stack.size-1] - interpreter->stack_pointer < interpreter->generator->maximum_function_stack_depth) {
            interpreter->exit_code = IR_Exit_Code::STACK_OVERFLOW;
            return true;
        }

        Bytecode_Instruction* jmp_to_instr = *(Bytecode_Instruction**)(interpreter->stack_pointer + i->op1);
        if (jmp_to_instr < interpreter->generator->instructions.data ||
            jmp_to_instr > &interpreter->generator->instructions.data[interpreter->generator->instructions.size]) {
            interpreter->exit_code = IR_Exit_Code::RETURN_VALUE_OVERFLOW;
            return true;
        }

        byte* base_pointer = interpreter->stack_pointer;
        Bytecode_Instruction* next = interpreter->instruction_pointer + 1;
        interpreter->stack_pointer = interpreter->stack_pointer + i->op2;
        *((Bytecode_Instruction**)interpreter->stack_pointer) = next;
        *(byte**)(interpreter->stack_pointer + 8) = base_pointer;

        interpreter->instruction_pointer = jmp_to_instr;

        return false;
    }
    case Instruction_Type::RETURN: {
        if (i->op2 > 256) {
            interpreter->exit_code = IR_Exit_Code::RETURN_VALUE_OVERFLOW;
            return true;
        }
        memory_copy(interpreter->return_register, interpreter->stack_pointer + i->op1, i->op2);
        Bytecode_Instruction* return_address = *(Bytecode_Instruction**)interpreter->stack_pointer;
        byte* stack_old_base = *(byte**)(interpreter->stack_pointer + 8);
        interpreter->instruction_pointer = return_address;
        interpreter->stack_pointer = stack_old_base;
        return false;
    }
    case Instruction_Type::EXIT: {
        interpreter->exit_code = (IR_Exit_Code) i->op1;
        /*
        if (interpreter->exit_code == Exit_Code::SUCCESS) {
            memory_copy(&interpreter->return_register[0], interpreter->stack_pointer + i->op1, i->op2);
        }
        */
        return true;
    }
    case Instruction_Type::CALL_HARDCODED_FUNCTION: 
    {
        IR_Hardcoded_Function_Type hardcoded_type = (IR_Hardcoded_Function_Type)i->op1;
        Type_Signature* function_sig = interpreter->compiler->analyser.program->hardcoded_functions[i->op1]->signature;
        byte* argument_start;
        {
            int start_offset = 0;
            for (int i = 0; i < function_sig->parameter_types.size; i++) {
                Type_Signature* type = function_sig->parameter_types[i];
                start_offset = align_offset_next_multiple(start_offset, type->alignment_in_bytes);
                start_offset += type->size_in_bytes;
            }
            start_offset = align_offset_next_multiple(start_offset, 8);
            argument_start = interpreter->stack_pointer + i->op2 - start_offset;
        }
        // Argument start only works if the argument type is of size 8, and only if the function has one argument
        memory_set_bytes(&interpreter->return_register[0], 256, 0);
        switch (hardcoded_type)
        {
        case IR_Hardcoded_Function_Type::MALLOC_SIZE_I32: {
            i32 size = *(i32*)argument_start;
            void* alloc_data = malloc(size);
            //logg("Allocated memory size: %5d, pointer: %p\n", size, alloc_data);
            memory_copy(interpreter->return_register, &alloc_data, 8);
            break;
        }
        case IR_Hardcoded_Function_Type::FREE_POINTER: {
            void* free_data = *(void**)argument_start;
            //logg("Interpreter Free pointer: %p\n", free_data);
            free(free_data);
            *(void**)argument_start = (void*)1;
            break;
        }
        case IR_Hardcoded_Function_Type::PRINT_I32: {
            i32 value = *(i32*)(argument_start);
            logg("%d", value); break;
        }
        case IR_Hardcoded_Function_Type::PRINT_F32: {
            logg("%3.2f", *(f32*)(argument_start)); break;
        }
        case IR_Hardcoded_Function_Type::PRINT_BOOL: {
            logg("%s", *(argument_start) == 0 ? "FALSE" : "TRUE"); break;
        }
        case IR_Hardcoded_Function_Type::PRINT_STRING: {
            //byte* argument_start = interpreter->stack_pointer + i->op2 - 24;
            char* str = *(char**)argument_start;
            int size = *(int*)(argument_start + 16);

            char* buffer = new char[size + 1];
            SCOPE_EXIT(delete[] buffer);
            memory_copy(buffer, str, size);
            buffer[size] = 0;
            logg("%s", buffer);
            break;
        }
        case IR_Hardcoded_Function_Type::PRINT_LINE: {
            logg("\n"); break;
        }
        case IR_Hardcoded_Function_Type::READ_I32: {
            logg("Please input an i32: ");
            i32 num;
            std::cin >> num;
            if (std::cin.fail()) {
                num = 0;
            }
            std::cin.ignore(10000, '\n');
            std::cin.clear();
            memory_copy(interpreter->return_register, &num, 4);
            break;
        }
        case IR_Hardcoded_Function_Type::READ_F32: {
            logg("Please input an f32: ");
            f32 num;
            std::cin >> num;
            if (std::cin.fail()) {
                num = 0;
            }
            std::cin.ignore(10000, '\n');
            std::cin.clear();
            memory_copy(interpreter->return_register, &num, 4);
            break;
        }
        case IR_Hardcoded_Function_Type::READ_BOOL: {
            logg("Please input an bool (As int): ");
            i32 num;
            std::cin >> num;
            if (std::cin.fail()) {
                num = 0;
            }
            std::cin.ignore(10000, '\n');
            std::cin.clear();
            if (num == 0) {
                interpreter->return_register[0] = 0;
            }
            else {
                interpreter->return_register[0] = 1;
            }
            break;
        }
        case IR_Hardcoded_Function_Type::RANDOM_I32: {
            i32 result = random_next_u32(&interpreter->random);
            memory_copy(interpreter->return_register, &result, 4);
            break;
        }
        default: {panic("What"); }
        }
        break;
    }
    case Instruction_Type::LOAD_RETURN_VALUE:
        memory_copy(interpreter->stack_pointer + i->op1, &interpreter->return_register[0], i->op2);
        break;
    case Instruction_Type::LOAD_REGISTER_ADDRESS:
        *(void**)(interpreter->stack_pointer + i->op1) = (void*)(interpreter->stack_pointer + i->op2);
        break;
    case Instruction_Type::LOAD_GLOBAL_ADDRESS:
        *(void**)(interpreter->stack_pointer + i->op1) = (void*)(interpreter->globals.data + i->op2);
        break;
    case Instruction_Type::LOAD_FUNCTION_LOCATION:
        *(Bytecode_Instruction**)(interpreter->stack_pointer + i->op1) = (Bytecode_Instruction*)&interpreter->generator->instructions[i->op2];
        break;
    case Instruction_Type::CAST_INTEGER_DIFFERENT_SIZE: {
        u64 source_unsigned = 0;
        i64 source_signed = 0;
        bool source_is_signed = false;
        switch ((Primitive_Type)i->op4) {
        case Primitive_Type::SIGNED_INT_8: source_is_signed = true; source_signed = *(i8*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::SIGNED_INT_16: source_is_signed = true; source_signed = *(i16*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::SIGNED_INT_32: source_is_signed = true; source_signed = *(i32*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::SIGNED_INT_64: source_is_signed = true; source_signed = *(i64*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::UNSIGNED_INT_8: source_is_signed = false; source_unsigned = *(u8*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::UNSIGNED_INT_16: source_is_signed = false; source_unsigned = *(u16*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::UNSIGNED_INT_32: source_is_signed = false; source_unsigned = *(u32*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::UNSIGNED_INT_64: source_is_signed = false; source_unsigned = *(u64*)(interpreter->stack_pointer + i->op2); break;
        default: panic("what the frigg\n");
        }
        switch ((Primitive_Type)i->op3) {
        case Primitive_Type::SIGNED_INT_8:    *(i8*)(interpreter->stack_pointer + i->op1) = (i8)source_is_signed ? source_signed : source_unsigned; break;
        case Primitive_Type::SIGNED_INT_16:   *(i16*)(interpreter->stack_pointer + i->op1) = (i16)source_is_signed ? source_signed : source_unsigned; break;
        case Primitive_Type::SIGNED_INT_32:   *(i32*)(interpreter->stack_pointer + i->op1) = (i32)source_is_signed ? source_signed : source_unsigned; break;
        case Primitive_Type::SIGNED_INT_64:   *(i64*)(interpreter->stack_pointer + i->op1) = (i64)source_is_signed ? source_signed : source_unsigned; break;
        case Primitive_Type::UNSIGNED_INT_8:  *(u8*)(interpreter->stack_pointer + i->op1) = (u8)source_is_signed ? source_signed : source_unsigned; break;
        case Primitive_Type::UNSIGNED_INT_16: *(u16*)(interpreter->stack_pointer + i->op1) = (u16)source_is_signed ? source_signed : source_unsigned; break;
        case Primitive_Type::UNSIGNED_INT_32: *(u32*)(interpreter->stack_pointer + i->op1) = (u32)source_is_signed ? source_signed : source_unsigned; break;
        case Primitive_Type::UNSIGNED_INT_64: *(u64*)(interpreter->stack_pointer + i->op1) = (u64)source_is_signed ? source_signed : source_unsigned; break;
        default: panic("what the frigg\n");
        }
        break;
    }
    case Instruction_Type::CAST_FLOAT_DIFFERENT_SIZE: {
        double source = 0.0;
        switch ((Primitive_Type)i->op4) {
        case Primitive_Type::FLOAT_32: source = *(float*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::FLOAT_64: source = *(double*)(interpreter->stack_pointer + i->op2); break;
        default: panic("what the frigg\n");
        }
        switch ((Primitive_Type)i->op3) {
        case Primitive_Type::FLOAT_32: *(float*)(interpreter->stack_pointer + i->op1) = (float)source; break;
        case Primitive_Type::FLOAT_64: *(double*)(interpreter->stack_pointer + i->op1) = (double)source; break;
        default: panic("what the frigg\n");
        }
        break;
    }
    case Instruction_Type::CAST_FLOAT_INTEGER: {
        double source = 0.0;
        switch ((Primitive_Type)i->op4) {
        case Primitive_Type::FLOAT_32: source = *(float*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::FLOAT_64: source = *(double*)(interpreter->stack_pointer + i->op2); break;
        default: panic("what the frigg\n");
        }
        switch ((Primitive_Type)i->op3) {
        case Primitive_Type::SIGNED_INT_8:    *(i8*)(interpreter->stack_pointer + i->op1) = (i8)source; break;
        case Primitive_Type::SIGNED_INT_16:   *(i16*)(interpreter->stack_pointer + i->op1) = (i16)source; break;
        case Primitive_Type::SIGNED_INT_32:   *(i32*)(interpreter->stack_pointer + i->op1) = (i32)source; break;
        case Primitive_Type::SIGNED_INT_64:   *(i64*)(interpreter->stack_pointer + i->op1) = (i64)source; break;
        case Primitive_Type::UNSIGNED_INT_8:  *(u8*)(interpreter->stack_pointer + i->op1) = (u8)source; break;
        case Primitive_Type::UNSIGNED_INT_16: *(u16*)(interpreter->stack_pointer + i->op1) = (u16)source; break;
        case Primitive_Type::UNSIGNED_INT_32: *(u32*)(interpreter->stack_pointer + i->op1) = (u32)source; break;
        case Primitive_Type::UNSIGNED_INT_64: *(u64*)(interpreter->stack_pointer + i->op1) = (u64)source; break;
        default: panic("what the frigg\n");
        }
        break;
    }
    case Instruction_Type::CAST_INTEGER_FLOAT:
    {
        u64 source_unsigned;
        i64 source_signed;
        bool source_is_signed = false;
        switch ((Primitive_Type)i->op4) {
        case Primitive_Type::SIGNED_INT_8: source_is_signed = true; source_signed = *(i8*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::SIGNED_INT_16: source_is_signed = true; source_signed = *(i16*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::SIGNED_INT_32: source_is_signed = true; source_signed = *(i32*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::SIGNED_INT_64: source_is_signed = true; source_signed = *(i64*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::UNSIGNED_INT_8: source_is_signed = false; source_unsigned = *(u8*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::UNSIGNED_INT_16: source_is_signed = false; source_unsigned = *(u16*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::UNSIGNED_INT_32: source_is_signed = false; source_unsigned = *(u32*)(interpreter->stack_pointer + i->op2); break;
        case Primitive_Type::UNSIGNED_INT_64: source_is_signed = false; source_unsigned = *(u64*)(interpreter->stack_pointer + i->op2); break;
        default: panic("what the frigg\n");
        }
        switch ((Primitive_Type)i->op3) {
        case Primitive_Type::FLOAT_32: *(float*)(interpreter->stack_pointer + i->op1) = (float)(source_is_signed ? source_signed : source_unsigned); break;
        case Primitive_Type::FLOAT_64: *(double*)(interpreter->stack_pointer + i->op1) = (double)(source_is_signed ? source_signed : source_unsigned); break;
        default: panic("what the frigg\n");
        }
        break;
    }

    /*
    -------------------------
    --- BINARY_OPERATIONS ---
    -------------------------
    */
    case Instruction_Type::BINARY_OP_ADDITION:
        switch ((Primitive_Type)i->op4)
        {
        case Primitive_Type::BOOLEAN:
            panic("What");
            break;
        case Primitive_Type::SIGNED_INT_8:
            *(i8*)(interpreter->stack_pointer + i->op1) = *(i8*)(interpreter->stack_pointer + i->op2) + *(i8*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_16:
            *(i16*)(interpreter->stack_pointer + i->op1) = *(i16*)(interpreter->stack_pointer + i->op2) + *(i16*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_32:
            *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) + *(i32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_64:
            *(i64*)(interpreter->stack_pointer + i->op1) = *(i64*)(interpreter->stack_pointer + i->op2) + *(i64*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) + *(u8*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_16:
            *(u16*)(interpreter->stack_pointer + i->op1) = *(u16*)(interpreter->stack_pointer + i->op2) + *(u16*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_32:
            *(u32*)(interpreter->stack_pointer + i->op1) = *(u32*)(interpreter->stack_pointer + i->op2) + *(u32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_64:
            *(u64*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) + *(u64*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::FLOAT_32:
            *(f32*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) + *(f32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::FLOAT_64:
            *(f64*)(interpreter->stack_pointer + i->op1) = *(f64*)(interpreter->stack_pointer + i->op2) + *(f64*)(interpreter->stack_pointer + i->op3);
            break;
        }
        break;
    case Instruction_Type::BINARY_OP_SUBTRACTION:
        switch ((Primitive_Type)i->op4)
        {
        case Primitive_Type::BOOLEAN:
            panic("What");
            break;
        case Primitive_Type::SIGNED_INT_8:
            *(i8*)(interpreter->stack_pointer + i->op1) = *(i8*)(interpreter->stack_pointer + i->op2) - *(i8*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_16:
            *(i16*)(interpreter->stack_pointer + i->op1) = *(i16*)(interpreter->stack_pointer + i->op2) - *(i16*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_32:
            *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) - *(i32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_64:
            *(i64*)(interpreter->stack_pointer + i->op1) = *(i64*)(interpreter->stack_pointer + i->op2) - *(i64*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) - *(u8*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_16:
            *(u16*)(interpreter->stack_pointer + i->op1) = *(u16*)(interpreter->stack_pointer + i->op2) - *(u16*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_32:
            *(u32*)(interpreter->stack_pointer + i->op1) = *(u32*)(interpreter->stack_pointer + i->op2) - *(u32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_64:
            *(u64*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) - *(u64*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::FLOAT_32:
            *(f32*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) - *(f32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::FLOAT_64:
            *(f64*)(interpreter->stack_pointer + i->op1) = *(f64*)(interpreter->stack_pointer + i->op2) - *(f64*)(interpreter->stack_pointer + i->op3);
            break;
        }
        break;
    case Instruction_Type::BINARY_OP_MULTIPLICATION:
        switch ((Primitive_Type)i->op4)
        {
        case Primitive_Type::BOOLEAN:
            panic("What");
            break;
        case Primitive_Type::SIGNED_INT_8:
            *(i8*)(interpreter->stack_pointer + i->op1) = *(i8*)(interpreter->stack_pointer + i->op2) * *(i8*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_16:
            *(i16*)(interpreter->stack_pointer + i->op1) = *(i16*)(interpreter->stack_pointer + i->op2) * *(i16*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_32:
            *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) * *(i32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_64:
            *(i64*)(interpreter->stack_pointer + i->op1) = *(i64*)(interpreter->stack_pointer + i->op2) * *(i64*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) * *(u8*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_16:
            *(u16*)(interpreter->stack_pointer + i->op1) = *(u16*)(interpreter->stack_pointer + i->op2) * *(u16*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_32:
            *(u32*)(interpreter->stack_pointer + i->op1) = *(u32*)(interpreter->stack_pointer + i->op2) * *(u32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_64:
            *(u64*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) * *(u64*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::FLOAT_32:
            *(f32*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) * *(f32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::FLOAT_64:
            *(f64*)(interpreter->stack_pointer + i->op1) = *(f64*)(interpreter->stack_pointer + i->op2) * *(f64*)(interpreter->stack_pointer + i->op3);
            break;
        }
        break;
    case Instruction_Type::BINARY_OP_DIVISION:
        switch ((Primitive_Type)i->op4)
        {
        case Primitive_Type::BOOLEAN:
            panic("What");
            break;
        case Primitive_Type::SIGNED_INT_8:
            *(i8*)(interpreter->stack_pointer + i->op1) = *(i8*)(interpreter->stack_pointer + i->op2) / *(i8*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_16:
            *(i16*)(interpreter->stack_pointer + i->op1) = *(i16*)(interpreter->stack_pointer + i->op2) / *(i16*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_32:
            *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) / *(i32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_64:
            *(i64*)(interpreter->stack_pointer + i->op1) = *(i64*)(interpreter->stack_pointer + i->op2) / *(i64*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) / *(u8*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_16:
            *(u16*)(interpreter->stack_pointer + i->op1) = *(u16*)(interpreter->stack_pointer + i->op2) / *(u16*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_32:
            *(u32*)(interpreter->stack_pointer + i->op1) = *(u32*)(interpreter->stack_pointer + i->op2) / *(u32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_64:
            *(u64*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) / *(u64*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::FLOAT_32:
            *(f32*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) / *(f32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::FLOAT_64:
            *(f64*)(interpreter->stack_pointer + i->op1) = *(f64*)(interpreter->stack_pointer + i->op2) / *(f64*)(interpreter->stack_pointer + i->op3);
            break;
        }
        break;
    case Instruction_Type::BINARY_OP_EQUAL:
        switch ((Primitive_Type)i->op4)
        {
        case Primitive_Type::BOOLEAN:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) == *(u8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i8*)(interpreter->stack_pointer + i->op2) == *(i8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_16:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i16*)(interpreter->stack_pointer + i->op2) == *(i16*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) == *(i32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i64*)(interpreter->stack_pointer + i->op2) == *(i64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) == *(u8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_16:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u16*)(interpreter->stack_pointer + i->op2) == *(u16*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u32*)(interpreter->stack_pointer + i->op2) == *(u32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) == *(u64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::FLOAT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) == *(f32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::FLOAT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(f64*)(interpreter->stack_pointer + i->op2) == *(f64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        }
        break;
    case Instruction_Type::BINARY_OP_NOT_EQUAL:
        switch ((Primitive_Type)i->op4)
        {
        case Primitive_Type::BOOLEAN:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) != *(u8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i8*)(interpreter->stack_pointer + i->op2) != *(i8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_16:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i16*)(interpreter->stack_pointer + i->op2) != *(i16*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) != *(i32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i64*)(interpreter->stack_pointer + i->op2) != *(i64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) != *(u8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_16:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u16*)(interpreter->stack_pointer + i->op2) != *(u16*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u32*)(interpreter->stack_pointer + i->op2) != *(u32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) != *(u64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::FLOAT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) != *(f32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::FLOAT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(f64*)(interpreter->stack_pointer + i->op2) != *(f64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        }
        break;
    case Instruction_Type::BINARY_OP_GREATER_THAN:
        switch ((Primitive_Type)i->op4)
        {
        case Primitive_Type::BOOLEAN:
            panic("what");
            break;
        case Primitive_Type::SIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i8*)(interpreter->stack_pointer + i->op2) > *(i8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_16:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i16*)(interpreter->stack_pointer + i->op2) > *(i16*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) > *(i32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i64*)(interpreter->stack_pointer + i->op2) > *(i64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) > *(u8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_16:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u16*)(interpreter->stack_pointer + i->op2) > *(u16*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u32*)(interpreter->stack_pointer + i->op2) > *(u32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) > *(u64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::FLOAT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) > *(f32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::FLOAT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(f64*)(interpreter->stack_pointer + i->op2) > *(f64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        }
        break;
    case Instruction_Type::BINARY_OP_GREATER_EQUAL:
        switch ((Primitive_Type)i->op4)
        {
        case Primitive_Type::BOOLEAN:
            panic("what");
            break;
        case Primitive_Type::SIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i8*)(interpreter->stack_pointer + i->op2) >= *(i8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_16:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i16*)(interpreter->stack_pointer + i->op2) >= *(i16*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) >= *(i32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i64*)(interpreter->stack_pointer + i->op2) >= *(i64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) >= *(u8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_16:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u16*)(interpreter->stack_pointer + i->op2) >= *(u16*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u32*)(interpreter->stack_pointer + i->op2) >= *(u32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) >= *(u64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::FLOAT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) >= *(f32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::FLOAT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(f64*)(interpreter->stack_pointer + i->op2) >= *(f64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        }
        break;
    case Instruction_Type::BINARY_OP_LESS_THAN:
        switch ((Primitive_Type)i->op4)
        {
        case Primitive_Type::BOOLEAN:
            panic("what");
            break;
        case Primitive_Type::SIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i8*)(interpreter->stack_pointer + i->op2) < *(i8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_16:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i16*)(interpreter->stack_pointer + i->op2) < *(i16*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) < *(i32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i64*)(interpreter->stack_pointer + i->op2) < *(i64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) < *(u8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_16:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u16*)(interpreter->stack_pointer + i->op2) < *(u16*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u32*)(interpreter->stack_pointer + i->op2) < *(u32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) < *(u64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::FLOAT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) < *(f32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::FLOAT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(f64*)(interpreter->stack_pointer + i->op2) < *(f64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        }
        break;
    case Instruction_Type::BINARY_OP_LESS_EQUAL:
        switch ((Primitive_Type)i->op4)
        {
        case Primitive_Type::BOOLEAN:
            panic("what");
            break;
        case Primitive_Type::SIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i8*)(interpreter->stack_pointer + i->op2) <= *(i8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_16:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i16*)(interpreter->stack_pointer + i->op2) <= *(i16*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) <= *(i32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::SIGNED_INT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(i64*)(interpreter->stack_pointer + i->op2) <= *(i64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) <= *(u8*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_16:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u16*)(interpreter->stack_pointer + i->op2) <= *(u16*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u32*)(interpreter->stack_pointer + i->op2) <= *(u32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::UNSIGNED_INT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) <= *(u64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::FLOAT_32:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) <= *(f32*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        case Primitive_Type::FLOAT_64:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(f64*)(interpreter->stack_pointer + i->op2) <= *(f64*)(interpreter->stack_pointer + i->op3) ? 1 : 0;
            break;
        }
        break;
    case Instruction_Type::BINARY_OP_MODULO:
        switch ((Primitive_Type)i->op4)
        {
        case Primitive_Type::BOOLEAN:
            panic("what");
            break;
        case Primitive_Type::SIGNED_INT_8:
            *(i8*)(interpreter->stack_pointer + i->op1) = *(i8*)(interpreter->stack_pointer + i->op2) % *(i8*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_16:
            *(i16*)(interpreter->stack_pointer + i->op1) = *(i16*)(interpreter->stack_pointer + i->op2) % *(i16*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_32:
            *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) % *(i32*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::SIGNED_INT_64:
            *(i64*)(interpreter->stack_pointer + i->op1) = *(i64*)(interpreter->stack_pointer + i->op2) % *(i64*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_8:
            *(u8*)(interpreter->stack_pointer + i->op1) = *(u8*)(interpreter->stack_pointer + i->op2) % *(u8*)(interpreter->stack_pointer + i->op3);
            break;
        case Primitive_Type::UNSIGNED_INT_16:
            *(u16*)(interpreter->stack_pointer + i->op1) = *(u16*)(interpreter->stack_pointer + i->op2) % *(u16*)(interpreter->stack_pointer + i->op3) ;
            break;
        case Primitive_Type::UNSIGNED_INT_32:
            *(u32*)(interpreter->stack_pointer + i->op1) = *(u32*)(interpreter->stack_pointer + i->op2) % *(u32*)(interpreter->stack_pointer + i->op3) ;
            break;
        case Primitive_Type::UNSIGNED_INT_64:
            *(u64*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) % *(u64*)(interpreter->stack_pointer + i->op3) ;
            break;
        case Primitive_Type::FLOAT_32:
            panic("what");
            break;
        case Primitive_Type::FLOAT_64:
            panic("what");
            break;
        }
        break;
    case Instruction_Type::BINARY_OP_AND:
        *(bool*)(interpreter->stack_pointer + i->op1) = *(bool*)(interpreter->stack_pointer + i->op2) && *(bool*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_OR:
        *(bool*)(interpreter->stack_pointer + i->op1) = *(bool*)(interpreter->stack_pointer + i->op2) || *(bool*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::UNARY_OP_NEGATE:
        switch ((Primitive_Type)i->op3)
        {
        case Primitive_Type::BOOLEAN:
            panic("What");
            break;
        case Primitive_Type::SIGNED_INT_8:
            *(i8*)(interpreter->stack_pointer + i->op1) = - *(i8*)(interpreter->stack_pointer + i->op2);
            break;
        case Primitive_Type::SIGNED_INT_16:
            *(i16*)(interpreter->stack_pointer + i->op1) = -*(i16*)(interpreter->stack_pointer + i->op2);
            break;
        case Primitive_Type::SIGNED_INT_32:
            *(i32*)(interpreter->stack_pointer + i->op1) = - *(i32*)(interpreter->stack_pointer + i->op2);
            break;
        case Primitive_Type::SIGNED_INT_64:
            *(i64*)(interpreter->stack_pointer + i->op1) = - *(i64*)(interpreter->stack_pointer + i->op2);
            break;
        case Primitive_Type::UNSIGNED_INT_8:
        case Primitive_Type::UNSIGNED_INT_16:
        case Primitive_Type::UNSIGNED_INT_32:
        case Primitive_Type::UNSIGNED_INT_64:
            panic("Should not happen?");
            break;
        case Primitive_Type::FLOAT_32:
            *(f32*)(interpreter->stack_pointer + i->op1) = - *(f32*)(interpreter->stack_pointer + i->op2);
            break;
        case Primitive_Type::FLOAT_64:
            *(f64*)(interpreter->stack_pointer + i->op1) = - *(f64*)(interpreter->stack_pointer + i->op2);
            break;
        }
        break;
    case Instruction_Type::UNARY_OP_NOT:
        *(bool*)(interpreter->stack_pointer + i->op1) = !*(bool*)(interpreter->stack_pointer + i->op2);
        break;
    default: {
        panic("Should not happen!\n");
        return true;
    }
    }

    interpreter->instruction_pointer = &interpreter->instruction_pointer[1];
    return false;
}

void bytecode_interpreter_print_state(Bytecode_Interpreter* interpreter)
{
    /*
    int current_instruction_index = interpreter->instruction_pointer - interpreter->generator->instructions.data;
    int current_function_index = -1;
    for (int i = 0; i < interpreter->generator->function_locations.size; i++)
    {
        int function_start_loc = interpreter->generator->function_locations[i];
        int function_end_loc_exclusive = interpreter->generator->instructions.size;
        if (i + 1 < interpreter->generator->function_locations.size) {
            function_end_loc_exclusive = interpreter->generator->function_locations[i + 1];
        }
        if (current_instruction_index >= function_start_loc && current_instruction_index < function_end_loc_exclusive) {
            current_function_index = i;
            break;
        }
    }
    if (current_function_index == -1) panic("Should not happen!\n");

    logg("\n\n\n\n---------------------- CURRENT STATE ----------------------\n");
    logg("Current Function: %s\n", lexer_identifer_to_string(&interpreter->compiler->lexer, func->name_handle).characters);
    logg("Current Stack offset: %d\n", interpreter->stack.data - interpreter->stack_pointer);
    logg("Instruction Index: %d\n", current_instruction_index);
    {
        String tmp = string_create_empty(64);
        SCOPE_EXIT(string_destroy(&tmp));
        bytecode_instruction_append_to_string(&tmp, interpreter->generator->instructions[current_instruction_index]);
        logg("Instruction: %s\n", tmp.characters);
    }
    */
    /*
    for (int i = 0; i < func->registers.size; i++)
    {
        Intermediate_Register* reg = &func->registers[i];
        int stack_offset = interpreter->generator->variable_stack_offsets[i];
        byte* reg_data_ptr = interpreter->stack_pointer + stack_offset;
        Type_Signature* reg_type = reg->type_signature;
        if (reg->type == Intermediate_Register_Type::PARAMETER) {
            logg("Parameter %d (Offset %d): ", reg->parameter_index, stack_offset);
        }
        else if (reg->type == Intermediate_Register_Type::VARIABLE) {
            logg("Variable %s (Offset %d): ",
                lexer_identifer_to_string(interpreter->generator->im_generator->analyser->parser->lexer, reg->name_id).characters,
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

void bytecode_interpreter_execute_main(Bytecode_Interpreter* interpreter, Compiler* compiler)
{
    interpreter->compiler = compiler;
    interpreter->generator = &compiler->bytecode_generator;
    memory_set_bytes(&interpreter->return_register, 256, 0);
    memory_set_bytes(interpreter->stack.data, 16, 0);
    interpreter->instruction_pointer = &interpreter->generator->instructions[interpreter->generator->entry_point_index];
    interpreter->stack_pointer = &interpreter->stack[0];
    if (interpreter->generator->global_data_size != 0) {
        if (interpreter->globals.data != 0) {
            array_destroy(&interpreter->globals);
        }
        interpreter->globals = array_create_empty<byte>(interpreter->generator->global_data_size);
    }
    int current_instruction_index = interpreter->instruction_pointer - interpreter->generator->instructions.data;

    while (true) {
        //bytecode_interpreter_print_state(interpreter);
        if (bytecode_interpreter_execute_current_instruction(interpreter)) { break; }
    }
}