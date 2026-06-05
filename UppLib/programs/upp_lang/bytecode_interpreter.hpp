#pragma once

#include "../../datastructures/array.hpp"
#include "../../utility/datatypes.hpp"
#include "../../utility/random.hpp"
#include "semantic_analyser.hpp"
#include "bytecode_generator.hpp"

struct Bytecode_Instruction;
struct Compilation_Data;
struct Bytecode_Thread;
struct Upp_Function;

Bytecode_Thread* bytecode_thread_create(
	Compilation_Data* compilation_data, Arena* arena, int max_instruction_executions, int max_heap_consumption, int stack_size, bool allow_global_access
);
void bytecode_thread_set_initial_state(Bytecode_Thread* thread, Upp_Function* entry_function);
Exit_Code bytecode_thread_execute(Bytecode_Thread* thread);
void* bytecode_thread_get_return_value_ptr(Bytecode_Thread* thread);
void bytecode_thread_print_state(Bytecode_Thread* thread);
// Returns if successfull (Only not sucessfull if integer divide by 0)
bool bytecode_execute_ir_operation(
	IR_Operation operation, void* dst, void* src1, void* src2, Bytecode_Type dst_type, Bytecode_Type left_type, Bytecode_Type right_type
);
