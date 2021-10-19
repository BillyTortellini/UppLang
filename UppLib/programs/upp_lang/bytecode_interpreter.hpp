#pragma once

#include "../../datastructures/array.hpp"
#include "../../utility/datatypes.hpp"
#include "../../utility/random.hpp"
#include "semantic_analyser.hpp"
#include "bytecode_generator.hpp"

struct Compiler;
struct Bytecode_Generator;
struct Bytecode_Instruction;
struct Constant_Pool;

struct Bytecode_Interpreter
{
    Bytecode_Generator* generator;
    Constant_Pool* constant_pool;
    Bytecode_Instruction* instruction_pointer;
    byte return_register[256];
    Array<byte> stack;
    Array<byte> globals;
    byte* stack_pointer;
    Exit_Code exit_code;
    Random random;
};

Bytecode_Interpreter bytecode_intepreter_create();
void bytecode_interpreter_destroy(Bytecode_Interpreter* interpreter);
bool bytecode_interpreter_execute_current_instruction(Bytecode_Interpreter* interpreter);
void bytecode_interpreter_execute_main(Bytecode_Interpreter* interpreter, Compiler* compiler);
void bytecode_interpreter_print_state(Bytecode_Interpreter* interpreter);
void bytecode_execute_binary_instr(Instruction_Type instr_type, Bytecode_Type type, void* dest, void* op_left, void* op_right);
void bytecode_execute_unary_instr(Instruction_Type instr_type, Bytecode_Type type, void* dest, void* operand);
void bytecode_execute_cast_instr(Instruction_Type instr_type, void* dest, void* src, Bytecode_Type dest_type, Bytecode_Type src_type);
