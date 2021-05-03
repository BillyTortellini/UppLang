#include "bytecode_interpreter.hpp"

Bytecode_Interpreter bytecode_intepreter_create()
{
    Bytecode_Interpreter result;
    result.stack = array_create_empty<byte>(8192);
    return result;
}

void bytecode_interpreter_destroy(Bytecode_Interpreter* interpreter) {
    array_destroy(&interpreter->stack);
}

// Returns true if we need to stop execution, e.g. on exit instruction
bool bytecode_interpreter_execute_current_instruction(Bytecode_Interpreter* interpreter)
{
    Bytecode_Instruction* i = interpreter->instruction_pointer;
    switch (i->instruction_type)
    {
    case Instruction_Type::MOVE_REGISTERS:
        memory_copy(interpreter->stack_pointer + i->op1, interpreter->stack_pointer + i->op2, i->op3);
        break;
    case Instruction_Type::WRITE_MEMORY:
        memory_copy(*(void**)(interpreter->stack_pointer + i->op1), interpreter->stack_pointer + i->op2, i->op3);
        break;
    case Instruction_Type::READ_MEMORY:
        memory_copy(interpreter->stack_pointer + i->op1, (void*)*(u64*)(interpreter->stack_pointer + i->op2), i->op3);
        break;
    case Instruction_Type::MEMORY_COPY:
        memory_copy((void*)*(u64*)(interpreter->stack_pointer + i->op1), (void*)*(u64*)(interpreter->stack_pointer + i->op2), i->op3);
        break;
    case Instruction_Type::U64_ADD_CONSTANT_I32:
        *(u64*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) + (i->op3);
        break;
    case Instruction_Type::U64_MULTIPLY_ADD_I32:
        *(u64*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) + 
            (u64)((*(u32*)(interpreter->stack_pointer + i->op3)) * (u64)i->op4);
        break;
    case Instruction_Type::JUMP:
        interpreter->instruction_pointer = &interpreter->generator->instructions[i->op1];
        return false;
    case Instruction_Type::JUMP_ON_TRUE:
        if (*(interpreter->stack_pointer + i->op1) != 0) {
            interpreter->instruction_pointer = &interpreter->generator->instructions[i->op2];
        }
        return false;
    case Instruction_Type::JUMP_ON_FALSE:
        if (*(interpreter->stack_pointer + i->op1) == 0) {
            interpreter->instruction_pointer = &interpreter->generator->instructions[i->op2];
        }
        return false;
    case Instruction_Type::CALL: {
        if (&interpreter->stack[interpreter->stack.size-1] - interpreter->stack_pointer < interpreter->generator->maximum_function_stack_depth) {
            logg("Stack overflow!\n");
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
    case Instruction_Type::RETURN: {
        memory_copy(&interpreter->return_register[0], interpreter->stack_pointer + i->op1, i->op2);
        Bytecode_Instruction* return_address = *(Bytecode_Instruction**)interpreter->stack_pointer;
        byte* stack_old_base = *(byte**)(interpreter->stack_pointer + 8);
        interpreter->instruction_pointer = return_address;
        interpreter->stack_pointer = stack_old_base;
        return false;
    }
    case Instruction_Type::EXIT: {
        memory_copy(&interpreter->return_register[0], interpreter->stack_pointer + i->op1, i->op2);
        Bytecode_Instruction* return_address = (Bytecode_Instruction*)interpreter->stack_pointer;
        byte* stack_old_base = (byte*)(interpreter->stack_pointer + 8);
        interpreter->instruction_pointer = return_address;
        interpreter->stack_pointer = stack_old_base;
        return true;
    }
    case Instruction_Type::LOAD_RETURN_VALUE:
        memory_copy(interpreter->stack_pointer + i->op1, &interpreter->return_register[0], i->op2);
        break;
    case Instruction_Type::LOAD_REGISTER_ADDRESS:
        *(void**)(interpreter->stack_pointer + i->op1) = (void*)(interpreter->stack_pointer + i->op2);
        break;
    case Instruction_Type::LOAD_CONSTANT_F32:
        *(f32*)(interpreter->stack_pointer + i->op1) = *((f32*)&i->op2);
        break;
    case Instruction_Type::LOAD_CONSTANT_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = i->op2;
        break;
    case Instruction_Type::LOAD_CONSTANT_BOOLEAN:
        *(interpreter->stack_pointer + i->op1) = (byte)i->op2;
        break;

    case Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) + *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) - *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) * *(i32*)(interpreter->stack_pointer + i->op3);
        logg("%d = %d * %d", *(i32*)(interpreter->stack_pointer + i->op1),
                *(i32*)(interpreter->stack_pointer + i->op2),
                *(i32*)(interpreter->stack_pointer + i->op3)
            );
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) / *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_MODULO_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) % *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_EQUAL_I32:
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) == *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_I32:
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) != *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_I32:
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) > *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_I32:
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) >= *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_I32:
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) < *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_I32:
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) <= *(i32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32:
        *(i32*)(interpreter->stack_pointer + i->op1) = -(*(i32*)(interpreter->stack_pointer + i->op2));
        break;

    case Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_F32:
        *(f32*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) + *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_SUBTRACTION_F32:
        *(f32*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) - *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_MULTIPLICATION_F32:
        *(f32*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) * *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_ARITHMETIC_DIVISION_F32:
        *(f32*)(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) / *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_EQUAL_F32:
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) == *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_F32:
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) != *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_GREATER_THAN_F32:
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) > *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_GREATER_EQUAL_F32:
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) >= *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_LESS_THAN_F32:
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) < *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_LESS_EQUAL_F32:
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) <= *(f32*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32:
        *(f32*)(interpreter->stack_pointer + i->op1) = -(*(f32*)(interpreter->stack_pointer + i->op2));
        break;

    case Instruction_Type::BINARY_OP_COMPARISON_EQUAL_BOOL:
        *(interpreter->stack_pointer + i->op1) = *(bool*)(interpreter->stack_pointer + i->op2) == *(bool*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_COMPARISON_NOT_EQUAL_BOOL:
        *(interpreter->stack_pointer + i->op1) = *(bool*)(interpreter->stack_pointer + i->op2) != *(bool*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_BOOLEAN_AND:
        *(interpreter->stack_pointer + i->op1) = *(bool*)(interpreter->stack_pointer + i->op2) && *(bool*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::BINARY_OP_BOOLEAN_OR:
        *(interpreter->stack_pointer + i->op1) = *(bool*)(interpreter->stack_pointer + i->op2) || *(bool*)(interpreter->stack_pointer + i->op3);
        break;
    case Instruction_Type::UNARY_OP_BOOLEAN_NOT:
        *(interpreter->stack_pointer + i->op1) = !(*(bool*)(interpreter->stack_pointer + i->op2));
        break;

    default: {
        panic("Should not happen!\n");
        return true;
    }
    }

    interpreter->instruction_pointer = &interpreter->instruction_pointer[1];
    return false;
}

void bytecode_interpreter_execute_main(Bytecode_Interpreter* interpreter, Bytecode_Generator* generator)
{
    interpreter->generator = generator;
    memory_set_bytes(&interpreter->return_register, 16, 0);
    interpreter->instruction_pointer = &generator->instructions[generator->entry_point_index];
    interpreter->stack_pointer = &interpreter->stack[0];

    while (true) {
        if (bytecode_interpreter_execute_current_instruction(interpreter)) { break; }
    }
}