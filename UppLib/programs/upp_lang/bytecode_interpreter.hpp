#pragma once

#include "../../datastructures/array.hpp"
#include "../../utility/datatypes.hpp"
#include "semantic_analyser.hpp"

struct Compiler;
struct Bytecode_Generator;
struct Bytecode_Instruction;

struct Bytecode_Interpreter
{
    Compiler* compiler;
    Bytecode_Generator* generator;
    Bytecode_Instruction* instruction_pointer;
    byte return_register[256];
    Array<byte> stack;
    Array<byte> globals;
    byte* stack_pointer;
    Exit_Code exit_code;
};

Bytecode_Interpreter bytecode_intepreter_create();
void bytecode_interpreter_destroy(Bytecode_Interpreter* interpreter);
bool bytecode_interpreter_execute_current_instruction(Bytecode_Interpreter* interpreter);
void bytecode_interpreter_execute_main(Bytecode_Interpreter* interpreter, Compiler* compiler);
void bytecode_interpreter_print_state(Bytecode_Interpreter* interpreter);
