#pragma once

#include "../../datastructures/array.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "../../utility/datatypes.hpp"
#include "../../utility/random.hpp"
#include "semantic_analyser.hpp"
#include "bytecode_generator.hpp"

struct Bytecode_Generator;
struct Bytecode_Instruction;

const int INTERPRETER_STACK_SIZE = 8192;

struct Bytecode_Thread
{
    int instruction_index;
    byte* stack_pointer;
    byte return_register[256];
    byte stack[INTERPRETER_STACK_SIZE];
    Hashtable<void*, int> heap_allocations;
    int heap_memory_consumption;

    // Result infos
    Exit_Code exit_code;
    Datatype* waiting_for_type_finish_type;
    bool error_occured;

    // General settings
    int instruction_limit;
};

Bytecode_Thread* bytecode_thread_create(int instruction_limit = -1);
void bytecode_thread_destroy(Bytecode_Thread* thread);

void bytecode_thread_set_initial_state(Bytecode_Thread* thread, int function_start_index);
void bytecode_thread_execute(Bytecode_Thread* thread);
bool bytecode_thread_execute_current_instruction(Bytecode_Thread* thread);

bool bytecode_execute_binary_instr(Instruction_Type instr_type, Bytecode_Type type, void* dest, void* op_left, void* op_right);
void bytecode_execute_unary_instr(Instruction_Type instr_type, Bytecode_Type type, void* dest, void* operand);
void bytecode_execute_cast_instr(Instruction_Type instr_type, void* dest, void* src, Bytecode_Type dest_type, Bytecode_Type src_type);

void bytecode_thread_print_state(Bytecode_Thread* thread);
