#pragma once

#include "bytecode.hpp"

struct Bytecode_Interpreter
{
    Bytecode_Generator* generator;
    Bytecode_Instruction* instruction_pointer;
    byte return_register[256];
    Array<byte> stack;
    byte* stack_pointer;
    Exit_Code exit_code;
};

Bytecode_Interpreter bytecode_intepreter_create();
void bytecode_interpreter_destroy(Bytecode_Interpreter* interpreter);
bool bytecode_interpreter_execute_current_instruction(Bytecode_Interpreter* interpreter);
void bytecode_interpreter_execute_main(Bytecode_Interpreter* interpreter, Bytecode_Generator* generator);