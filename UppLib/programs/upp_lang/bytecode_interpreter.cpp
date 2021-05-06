#include "bytecode_interpreter.hpp"

#include <iostream>
#include "../../utility/random.hpp"

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
    case Instruction_Type::READ_MEMORY: {
        void* result = *(void**)(interpreter->stack_pointer + i->op2);
        memory_copy(interpreter->stack_pointer + i->op1, *((void**)(interpreter->stack_pointer + i->op2)), i->op3);
        break;
    }
    case Instruction_Type::MEMORY_COPY:
        memory_copy(*(void**)(interpreter->stack_pointer + i->op1), *(void**)(interpreter->stack_pointer + i->op2), i->op3);
        break;
    case Instruction_Type::U64_ADD_CONSTANT_I32:
        *(u64*)(interpreter->stack_pointer + i->op1) = *(u64*)(interpreter->stack_pointer + i->op2) + (i->op3);
        break;
    case Instruction_Type::U64_MULTIPLY_ADD_I32: {
        u64 offset = (u64)((*(u32*)(interpreter->stack_pointer + i->op3)) * (u64)i->op4);
        if ((i32)offset < 0) {
            interpreter->exit_code = Exit_Code::OUT_OF_BOUNDS;
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
    case Instruction_Type::CALL: {
        if (&interpreter->stack[interpreter->stack.size-1] - interpreter->stack_pointer < interpreter->generator->maximum_function_stack_depth) {
            interpreter->exit_code = Exit_Code::STACK_OVERFLOW;
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
        if (i->op2 > 256) {
            interpreter->exit_code = Exit_Code::RETURN_VALUE_OVERFLOW;
            return true;
        }
        memory_copy(&interpreter->return_register[0], interpreter->stack_pointer + i->op1, i->op2);
        Bytecode_Instruction* return_address = *(Bytecode_Instruction**)interpreter->stack_pointer;
        byte* stack_old_base = *(byte**)(interpreter->stack_pointer + 8);
        interpreter->instruction_pointer = return_address;
        interpreter->stack_pointer = stack_old_base;
        return false;
    }
    case Instruction_Type::EXIT: {
        interpreter->exit_code = (Exit_Code) i->op3;
        if (interpreter->exit_code == Exit_Code::SUCCESS) {
            memory_copy(&interpreter->return_register[0], interpreter->stack_pointer + i->op1, i->op2);
        }
        return true;
    }
    case Instruction_Type::CALL_HARDCODED_FUNCTION: 
    {
        Hardcoded_Function_Type type = (Hardcoded_Function_Type)i->op1;
        byte* argument_start = interpreter->stack_pointer + i->op2 - 8; // Check if this is correct
        memory_set_bytes(&interpreter->return_register[0], 256, 0);
        switch (type)
        {
        case Hardcoded_Function_Type::PRINT_I32: {
            logg("%d", *(i32*)(argument_start)); break;
        }
        case Hardcoded_Function_Type::PRINT_F32: {
            logg("%3.2f", *(f32*)(argument_start)); break;
        }
        case Hardcoded_Function_Type::PRINT_BOOL: {
            logg("%s", *(argument_start) == 0 ? "FALSE" : "TRUE"); break;
        }
        case Hardcoded_Function_Type::PRINT_LINE: {
            logg("\n"); break;
        }
        case Hardcoded_Function_Type::READ_I32: {
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
        case Hardcoded_Function_Type::READ_F32: {
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
        case Hardcoded_Function_Type::READ_BOOL: {
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
        case Hardcoded_Function_Type::RANDOM_I32: {
            i32 result = random_next_int();
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
        *(interpreter->stack_pointer + i->op1) = *(i32*)(interpreter->stack_pointer + i->op2) > * (i32*)(interpreter->stack_pointer + i->op3);
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
        *(interpreter->stack_pointer + i->op1) = *(f32*)(interpreter->stack_pointer + i->op2) > * (f32*)(interpreter->stack_pointer + i->op3);
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

    // Debug output
    if (false)
    {
        int start = (int)Instruction_Type::BINARY_OP_ARITHMETIC_ADDITION_I32;
        int end = (int)Instruction_Type::UNARY_OP_BOOLEAN_NOT;
        int index = (int)i->instruction_type;
        if (index >= start && index <= end)
        {
            int counter = interpreter->instruction_pointer - interpreter->generator->instructions.data;
            bool unary = i->instruction_type == Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_F32 ||
                i->instruction_type == Instruction_Type::UNARY_OP_ARITHMETIC_NEGATE_I32 ||
                i->instruction_type == Instruction_Type::UNARY_OP_BOOLEAN_NOT;
            logg("%d Stack index: %d\n", counter, (int)(interpreter->stack_pointer - interpreter->stack.data));
            logg("%d dest = %6d / %3.2f / %s\n",
                counter,
                *(int*)(interpreter->stack_pointer + i->op1),
                *(float*)(interpreter->stack_pointer + i->op1),
                (*(bool*)(interpreter->stack_pointer + i->op1)) ? "True" : "False");
            logg("%d src1 = %6d / %3.2f / %s\n",
                counter,
                *(int*)(interpreter->stack_pointer + i->op2),
                *(float*)(interpreter->stack_pointer + i->op2),
                (*(bool*)(interpreter->stack_pointer + i->op2)) ? "True" : "False");
            if (!unary) {
                logg("%d src2 = %6d / %3.2f / %s\n",
                    counter,
                    *(int*)(interpreter->stack_pointer + i->op3),
                    *(float*)(interpreter->stack_pointer + i->op3),
                    (*(bool*)(interpreter->stack_pointer + i->op3)) ? "True" : "False");
            }
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