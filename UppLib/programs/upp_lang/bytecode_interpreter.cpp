#include "bytecode_interpreter.hpp"

Bytecode_Interpreter bytecode_intepreter_create()
{
    Bytecode_Interpreter result;
    result.stack = array_create_empty<int>(1024);
    return result;
}

void bytecode_interpreter_destroy(Bytecode_Interpreter* interpreter) {
    array_destroy(&interpreter->stack);
}

// Returns true if we need to stop execution, e.g. on exit instruction
bool bytecode_interpreter_execute_current_instruction(Bytecode_Interpreter* interpreter)
{
    Bytecode_Instruction* instruction = &interpreter->generator->instructions[interpreter->instruction_pointer];
    switch (instruction->instruction_type)
    {
    case Instruction_Type::LOAD_INTERMEDIATE_4BYTE: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] = instruction->op2;
        break;
    }
    case Instruction_Type::LOAD_REGISTER_ADDRESS: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] = interpreter->base_pointer + instruction->op2;
        break;
    }
    case Instruction_Type::LOAD_FROM_ADDRESS: {
        int address = interpreter->stack[interpreter->base_pointer + instruction->op2];
        if (address < 0 || address >= interpreter->stack.size) {
            __debugbreak();
            panic("Read from invalid address");
        }
        interpreter->stack[interpreter->base_pointer + instruction->op1] = interpreter->stack[address];
        break;
    }
    case Instruction_Type::STORE_TO_ADDRESS: {
        int address = interpreter->stack[interpreter->base_pointer + instruction->op1];
        if (address < 0 || address >= interpreter->stack.size) {
            __debugbreak();
            panic("Write to invalid address");
        }
        interpreter->stack[address] = interpreter->stack[interpreter->base_pointer + instruction->op2];
        break;
    }
    case Instruction_Type::MOVE: { // op1 = dest_reg, op2 = src_reg
        interpreter->stack[interpreter->base_pointer + instruction->op1] = interpreter->stack[interpreter->base_pointer + instruction->op2];
        break;
    }
    case Instruction_Type::JUMP: {
        interpreter->instruction_pointer = instruction->op1;
        return false;
    }
    case Instruction_Type::JUMP_ON_TRUE: {
        if (interpreter->stack[interpreter->base_pointer + instruction->op2] != 0) {
            interpreter->instruction_pointer = instruction->op1;
            return false;
        }
        break;
    }
    case Instruction_Type::JUMP_ON_FALSE: {
        if (interpreter->stack[interpreter->base_pointer + instruction->op2] == 0) {
            interpreter->instruction_pointer = instruction->op1;
            return false;
        }
        break;
    }
    case Instruction_Type::CALL:
    {
        // Pushes return address + old base onto stack, op1 = instruction_index, op2 = register_in_use_count
        int old_base = interpreter->base_pointer;
        interpreter->base_pointer += instruction->op2;
        if (interpreter->base_pointer + 2 + interpreter->generator->maximum_function_stack_depth >= interpreter->stack.size) {
            logg("Stack overflow!\n");
            return true;
        }
        interpreter->stack[interpreter->base_pointer] = interpreter->instruction_pointer + 1;
        interpreter->base_pointer += 1;
        interpreter->stack[interpreter->base_pointer] = old_base;
        interpreter->base_pointer += 1;
        interpreter->instruction_pointer = instruction->op1;
        return false;
    }
    case Instruction_Type::RETURN:
    {
        // Pops return address, op1 = return_value reg
        interpreter->return_register = interpreter->stack[interpreter->base_pointer + instruction->op1];
        int old_base = interpreter->stack[interpreter->base_pointer - 1];
        int return_addr = interpreter->stack[interpreter->base_pointer - 2];
        interpreter->base_pointer = old_base;
        interpreter->instruction_pointer = return_addr;
        return false;
    }
    case Instruction_Type::LOAD_RETURN_VALUE: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] = interpreter->return_register;
        break;
    }
    case Instruction_Type::EXIT: {
        interpreter->return_register = interpreter->stack[interpreter->base_pointer + instruction->op1];
        return true;
    }
    case Instruction_Type::INT_ADDITION: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            interpreter->stack[interpreter->base_pointer + instruction->op2] +
            interpreter->stack[interpreter->base_pointer + instruction->op3];
        break;
    }
    case Instruction_Type::INT_SUBTRACT: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            interpreter->stack[interpreter->base_pointer + instruction->op2] -
            interpreter->stack[interpreter->base_pointer + instruction->op3];
        break;
    }
    case Instruction_Type::INT_MULTIPLY: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            interpreter->stack[interpreter->base_pointer + instruction->op2] *
            interpreter->stack[interpreter->base_pointer + instruction->op3];
        break;
    }
    case Instruction_Type::INT_DIVISION: {
        if (interpreter->stack[interpreter->base_pointer + instruction->op3] == 0) {
            logg("Division by zero!!\n");
            return true;
        }
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            interpreter->stack[interpreter->base_pointer + instruction->op2] /
            interpreter->stack[interpreter->base_pointer + instruction->op3];
        break;
    }
    case Instruction_Type::INT_MODULO: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            interpreter->stack[interpreter->base_pointer + instruction->op2] %
            interpreter->stack[interpreter->base_pointer + instruction->op3];
        break;
    }
    case Instruction_Type::INT_NEGATE: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            -interpreter->stack[interpreter->base_pointer + instruction->op2];
        break;
    }
    case Instruction_Type::FLOAT_ADDITION: {
        *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op1] =
            *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op2] +
            *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op3];
        break;
    }
    case Instruction_Type::FLOAT_SUBTRACT: {
        *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op1] =
            *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op2] -
            *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op3];
        break;
    }
    case Instruction_Type::FLOAT_MULTIPLY: {
        *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op1] =
            *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op2] *
            *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op3];
        break;
    }
    case Instruction_Type::FLOAT_DIVISION: {
        *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op1] =
            *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op2] /
            *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op3];
        break;
    }
    case Instruction_Type::FLOAT_NEGATE: {
        *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op1] =
            -*(float*)&interpreter->stack[interpreter->base_pointer + instruction->op2];
        break;
    }
    case Instruction_Type::BOOLEAN_AND: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (interpreter->stack[interpreter->base_pointer + instruction->op2] &&
                interpreter->stack[interpreter->base_pointer + instruction->op3]) ? 1 : 0;
        break;
    }
    case Instruction_Type::BOOLEAN_OR: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (interpreter->stack[interpreter->base_pointer + instruction->op2] ||
                interpreter->stack[interpreter->base_pointer + instruction->op3]) ? 1 : 0;
        break;
    }
    case Instruction_Type::BOOLEAN_NOT: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (!(interpreter->stack[interpreter->base_pointer + instruction->op2])) ? 1 : 0;
        break;
    }
    case Instruction_Type::COMPARE_INT_GREATER_THAN: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (interpreter->stack[interpreter->base_pointer + instruction->op2] >
                interpreter->stack[interpreter->base_pointer + instruction->op3]) ? 1 : 0;
        break;
    }
    case Instruction_Type::COMPARE_INT_GREATER_EQUAL: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (interpreter->stack[interpreter->base_pointer + instruction->op2] >=
                interpreter->stack[interpreter->base_pointer + instruction->op3]) ? 1 : 0;
        break;
    }
    case Instruction_Type::COMPARE_INT_LESS_THAN: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (interpreter->stack[interpreter->base_pointer + instruction->op2] <
                interpreter->stack[interpreter->base_pointer + instruction->op3]) ? 1 : 0;
        break;
    }
    case Instruction_Type::COMPARE_INT_LESS_EQUAL: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (interpreter->stack[interpreter->base_pointer + instruction->op2] <=
                interpreter->stack[interpreter->base_pointer + instruction->op3]) ? 1 : 0;
        break;
    }
    case Instruction_Type::COMPARE_FLOAT_GREATER_THAN: {
        *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (*(float*)&interpreter->stack[interpreter->base_pointer + instruction->op2] >
                * (float*)&interpreter->stack[interpreter->base_pointer + instruction->op3]) ? 1 : 0;
        break;
    }
    case Instruction_Type::COMPARE_FLOAT_GREATER_EQUAL: {
        *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (*(float*)&interpreter->stack[interpreter->base_pointer + instruction->op2] >=
                *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op3]) ? 1 : 0;
        break;
    }
    case Instruction_Type::COMPARE_FLOAT_LESS_THAN: {
        *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (*(float*)&interpreter->stack[interpreter->base_pointer + instruction->op2] <
                *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op3]) ? 1 : 0;
        break;
    }
    case Instruction_Type::COMPARE_FLOAT_LESS_EQUAL: {
        *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (*(float*)&interpreter->stack[interpreter->base_pointer + instruction->op2] <=
                *(float*)&interpreter->stack[interpreter->base_pointer + instruction->op3]) ? 1 : 0;
        break;
    }
    case Instruction_Type::COMPARE_REGISTERS_4BYTE_EQUAL: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (interpreter->stack[interpreter->base_pointer + instruction->op2] ==
                interpreter->stack[interpreter->base_pointer + instruction->op3]) ? 1 : 0;
        break;
    }
    case Instruction_Type::COMPARE_REGISTERS_4BYTE_NOT_EQUAL: {
        interpreter->stack[interpreter->base_pointer + instruction->op1] =
            (interpreter->stack[interpreter->base_pointer + instruction->op2] !=
                interpreter->stack[interpreter->base_pointer + instruction->op3]) ? 1 : 0;
        break;
    }
    default: {
        panic("Should not happen!\n");
        return true;
    }
    }

    interpreter->instruction_pointer += 1;
    return false;
}

void bytecode_interpreter_execute_main(Bytecode_Interpreter* interpreter, Bytecode_Generator* generator)
{
    interpreter->generator = generator;
    interpreter->return_register = -1;
    interpreter->instruction_pointer = generator->entry_point_index;
    interpreter->base_pointer = 0;

    while (true) {
        if (bytecode_interpreter_execute_current_instruction(interpreter)) { break; }
    }
}