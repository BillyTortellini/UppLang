#include "compiler_misc.hpp"

#include "../../datastructures/allocators.hpp"
#include "type_system.hpp"
#include "../../win32/process.hpp"
#include "../../win32/thread.hpp"

const char* timing_task_to_string(Timing_Task task)
{
	switch (task)
	{
	case Timing_Task::LEXING: return "LEXING";
	case Timing_Task::PARSING: return "PARSING";
	case Timing_Task::ANALYSIS: return "ANALYSIS";
	case Timing_Task::CODE_GEN: return "CODE_GEN";
	case Timing_Task::CODE_EXEC: return "CODE_EXEC";
	case Timing_Task::RESET: return "RESET";
	case Timing_Task::OUTPUT: return "OUTPUT";
	case Timing_Task::FINISH: return "FINISH";
	default: panic("");
	}
	return "";
}

Hardcoded_Type_Info hardcoded_type_get_info(Hardcoded_Type type)
{
	auto make_info = [&](
		Hardcoded_Type_Class type_class, const char* symbol_name) -> Hardcoded_Type_Info 
	{
		Hardcoded_Type_Info info;
		info.type_class = type_class;
		info.symbol_name = symbol_name;
		info.ir_operation = (Primitive_Operation)-1;
		info.builtin_fn = (IR_Builtin_Function)-1;
		return info;
	};

	auto make_info_builtin = [&](
		Hardcoded_Type_Class type_class, const char* symbol_name, IR_Builtin_Function builtin) -> Hardcoded_Type_Info 
	{
		Hardcoded_Type_Info info;
		info.type_class = type_class;
		info.symbol_name = symbol_name;
		info.ir_operation = (Primitive_Operation) -1;
		info.builtin_fn = builtin;
		return info;
	};
	
	auto make_info_operation = [&](
		Hardcoded_Type_Class type_class, const char* symbol_name, Primitive_Operation op) -> Hardcoded_Type_Info 
	{
		Hardcoded_Type_Info info;
		info.type_class = type_class;
		info.symbol_name = symbol_name;
		info.ir_operation = op;
		info.builtin_fn = (IR_Builtin_Function) -1;
		return info;
	};

	switch (type)
	{
	case Hardcoded_Type::ASSERT_FN: return make_info(Hardcoded_Type_Class::ESSENTIAL, "assert");
	case Hardcoded_Type::TYPE_INFO: return make_info_builtin(Hardcoded_Type_Class::ESSENTIAL, "type_info", IR_Builtin_Function::TYPE_INFO);
	case Hardcoded_Type::TYPE_OF: return make_info(Hardcoded_Type_Class::ESSENTIAL, "type_of");
	case Hardcoded_Type::SIZE_OF: return make_info(Hardcoded_Type_Class::ESSENTIAL, "size_of");
	case Hardcoded_Type::ALIGN_OF: return make_info(Hardcoded_Type_Class::ESSENTIAL, "align_of");
	case Hardcoded_Type::PANIC_FN: return make_info(Hardcoded_Type_Class::ESSENTIAL, "panic");
	case Hardcoded_Type::RETURN_TYPE: return make_info(Hardcoded_Type_Class::UTILITY, "return_type");
	case Hardcoded_Type::STRUCT_TAG: return make_info(Hardcoded_Type_Class::UTILITY, "struct_tag");

	case Hardcoded_Type::ENUM_VALUE_AS_STRING: return make_info(Hardcoded_Type_Class::UTILITY, "enum_value_as_string");
	case Hardcoded_Type::ENUM_TYPE_MIN_VALUE: return make_info(Hardcoded_Type_Class::UTILITY, "enum_type_min_value");
	case Hardcoded_Type::ENUM_TYPE_MAX_VALUE: return make_info(Hardcoded_Type_Class::UTILITY, "enum_type_max_value");
	case Hardcoded_Type::ENUM_TYPE_IS_CONTINOUS: return make_info(Hardcoded_Type_Class::UTILITY, "enum_type_is_continous");

	case Hardcoded_Type::CAST_PRIMITIVE: return make_info_operation(Hardcoded_Type_Class::ESSENTIAL, "cast", Primitive_Operation::PRIMITIVE_CAST);
	case Hardcoded_Type::CAST_POINTER: return make_info_operation(Hardcoded_Type_Class::ESSENTIAL, "cast_pointer", Primitive_Operation::PRIMITIVE_CAST);
	case Hardcoded_Type::RAWPTR_TO_UINT: return make_info_operation(Hardcoded_Type_Class::ESSENTIAL, "rawptr_to_uint", Primitive_Operation::PRIMITIVE_CAST);
	case Hardcoded_Type::UINT_TO_RAWPTR: return make_info_operation(Hardcoded_Type_Class::ESSENTIAL, "uint_to_rawptr", Primitive_Operation::PRIMITIVE_CAST);
	case Hardcoded_Type::CAST_ANY: return make_info(Hardcoded_Type_Class::ESSENTIAL, "cast_any");

	case Hardcoded_Type::MEMORY_COPY: 
		return make_info_builtin(Hardcoded_Type_Class::ESSENTIAL, "memory_copy", IR_Builtin_Function::MEMORY_COPY);
	case Hardcoded_Type::MEMORY_COPY_NO_OVERLAP: 
		return make_info_builtin(Hardcoded_Type_Class::ESSENTIAL, "memory_copy_no_overlap", IR_Builtin_Function::MEMORY_COPY_NO_OVERLAP);
	case Hardcoded_Type::MEMORY_COMPARE: 
		return make_info_builtin(Hardcoded_Type_Class::ESSENTIAL, "memory_compare", IR_Builtin_Function::MEMORY_COMPARE);
	case Hardcoded_Type::MEMORY_ZERO: 
		return make_info_builtin(Hardcoded_Type_Class::ESSENTIAL, "memory_zero", IR_Builtin_Function::MEMORY_ZERO);

	case Hardcoded_Type::SYSTEM_ALLOC: return make_info_builtin(Hardcoded_Type_Class::ESSENTIAL, "system_allocate", IR_Builtin_Function::SYSTEM_ALLOC);
	case Hardcoded_Type::SYSTEM_FREE: return make_info_builtin(Hardcoded_Type_Class::ESSENTIAL, "system_free", IR_Builtin_Function::SYSTEM_FREE);

	case Hardcoded_Type::PRINT_INT: return make_info_builtin(Hardcoded_Type_Class::OUTPUT, "print_int", IR_Builtin_Function::PRINT_INT);
	case Hardcoded_Type::PRINT_FLOAT: return make_info_builtin(Hardcoded_Type_Class::OUTPUT, "print_float", IR_Builtin_Function::PRINT_FLOAT);
	case Hardcoded_Type::PRINT_STRING: return make_info_builtin(Hardcoded_Type_Class::OUTPUT, "print_string", IR_Builtin_Function::PRINT_STRING);

	case Hardcoded_Type::BITWISE_NOT:     return make_info_operation(Hardcoded_Type_Class::BITWISE_NOT, "bitwise_not", Primitive_Operation::BITWISE_NOT);
	case Hardcoded_Type::HIGHEST_SET_BIT: return make_info_operation(Hardcoded_Type_Class::BIT_INDEX, "hightest_set_bit", Primitive_Operation::HIGHEST_SET_BIT);
	case Hardcoded_Type::LOWEST_SET_BIT:  return make_info_operation(Hardcoded_Type_Class::BIT_INDEX, "lowest_set_bit", Primitive_Operation::LOWEST_SET_BIT);
	case Hardcoded_Type::BITSHIFT_LEFT:   return make_info_operation(Hardcoded_Type_Class::BITSHIFT, "bitshift_left", Primitive_Operation::BITWISE_SHIFT_LEFT);
	case Hardcoded_Type::BITSHIFT_RIGHT:  return make_info_operation(Hardcoded_Type_Class::BITSHIFT, "bitshift_right", Primitive_Operation::BITWISE_SHIFT_RIGHT);
	case Hardcoded_Type::BITWISE_AND:     return make_info_operation(Hardcoded_Type_Class::BITWISE_BINOP, "bitwise_and", Primitive_Operation::BITWISE_AND);
	case Hardcoded_Type::BITWISE_OR:      return make_info_operation(Hardcoded_Type_Class::BITWISE_BINOP, "bitwise_or", Primitive_Operation::BITWISE_OR);
	case Hardcoded_Type::BITWISE_XOR:     return make_info_operation(Hardcoded_Type_Class::BITWISE_BINOP, "bitwise_xor", Primitive_Operation::BITWISE_XOR);

	case Hardcoded_Type::FLOAT_ABS:       return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,   "float_abs", Primitive_Operation::FLOAT_ABS);
	case Hardcoded_Type::FLOAT_MODULO:    return make_info_operation(Hardcoded_Type_Class::FLOAT_BINARY,  "float_modulo", Primitive_Operation::MODULO);
	case Hardcoded_Type::FLOAT_REMAINDER: return make_info_operation(Hardcoded_Type_Class::FLOAT_BINARY,  "float_remainder", Primitive_Operation::FLOAT_REMAINDER);
	case Hardcoded_Type::CEIL:          return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "round_up", Primitive_Operation::ROUND_UP);
	case Hardcoded_Type::FLOOR:         return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "round_down", Primitive_Operation::ROUND_DOWN);
	case Hardcoded_Type::TRUNCATE:      return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "round_towards_zero", Primitive_Operation::ROUND_TOWARDS_ZERO);
	case Hardcoded_Type::ROUND_NEAREST: return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "round_nearest", Primitive_Operation::ROUND_NEAREST);
	case Hardcoded_Type::EXP:           return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "exp", Primitive_Operation::EXP);
	case Hardcoded_Type::LN:            return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "ln", Primitive_Operation::LN);
	case Hardcoded_Type::LOG10:         return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "log10", Primitive_Operation::LOG10);
	case Hardcoded_Type::LOG2:          return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "log2", Primitive_Operation::LOG2);
	case Hardcoded_Type::POW:           return make_info_operation(Hardcoded_Type_Class::FLOAT_BINARY,    "pow", Primitive_Operation::POW);
	case Hardcoded_Type::SQUARE_ROOT:   return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "sqare_root", Primitive_Operation::SQUARE_ROOT);
	case Hardcoded_Type::CUBE_ROOT:     return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "cube_root", Primitive_Operation::CUBE_ROOT);
	case Hardcoded_Type::SIN:           return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "sin", Primitive_Operation::SIN);
	case Hardcoded_Type::COS:           return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "cos", Primitive_Operation::COS);
	case Hardcoded_Type::TAN:           return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "tan", Primitive_Operation::TAN);
	case Hardcoded_Type::ASIN:          return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "asin", Primitive_Operation::ASIN);
	case Hardcoded_Type::ACOS:          return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "acos", Primitive_Operation::ACOS);
	case Hardcoded_Type::ATAN:          return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "atan", Primitive_Operation::ATAN);
	case Hardcoded_Type::ATAN2:         return make_info_operation(Hardcoded_Type_Class::FLOAT_BINARY,    "atan2", Primitive_Operation::ATAN2);
	case Hardcoded_Type::SINH:          return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "sinh", Primitive_Operation::SINH);
	case Hardcoded_Type::COSH:          return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "cosh", Primitive_Operation::COSH);
	case Hardcoded_Type::TANH:          return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "tanh", Primitive_Operation::TANH);
	case Hardcoded_Type::ASINH:         return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "asinh", Primitive_Operation::ASINH);
	case Hardcoded_Type::ACOSH:         return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "acosh", Primitive_Operation::ACOSH);
	case Hardcoded_Type::ATANH:         return make_info_operation(Hardcoded_Type_Class::FLOAT_UNARY,     "atanh", Primitive_Operation::ATANH);
	case Hardcoded_Type::IS_NAN:        return make_info_operation(Hardcoded_Type_Class::FLOAT_PREDICATE, "float_is_nan", Primitive_Operation::IS_NAN);
	case Hardcoded_Type::IS_FINITE:     return make_info_operation(Hardcoded_Type_Class::FLOAT_PREDICATE, "float_is_finite", Primitive_Operation::IS_FINITE);
	case Hardcoded_Type::IS_INFINITE:   return make_info_operation(Hardcoded_Type_Class::FLOAT_PREDICATE, "float_is_infinite", Primitive_Operation::IS_INFINITE);

	default: panic("Should not happen");
	}

	return make_info((Hardcoded_Type_Class)-1, "INVALID");
}

Exit_Code exit_code_make(Exit_Code_Type type, const char* error_msg)
{
	Exit_Code result;
	result.type = type;
	result.options.error_msg = error_msg;
	return result;
}

const char* exit_code_type_as_string(Exit_Code_Type type)
{
	switch (type)
	{
	case Exit_Code_Type::SUCCESS: return "SUCCESS";
	case Exit_Code_Type::RUNNING: return "RUNNING";
	case Exit_Code_Type::COMPILATION_FAILED: return "COMPILATION_FAILED";
	case Exit_Code_Type::CODE_ERROR: return "CODE_ERROR";
	case Exit_Code_Type::EXECUTION_ERROR: return "EXECUTION_ERROR";
	case Exit_Code_Type::INSTRUCTION_LIMIT_REACHED: return "INSTRUCTION_LIMIT_REACHED";
	case Exit_Code_Type::TYPE_INFO_WAITING_FOR_TYPE_FINISHED: return "TYPE_INFO_WAITING_FOR_TYPE_FINISH";
	case Exit_Code_Type::CALL_TO_UNFINISHED_FUNCTION: return "CALL_TO_UNFINISHED_FUNCTION";
	default: panic("");
	}
	return "";
}

void exit_code_append_to_string(DynString* string, Exit_Code code)
{
	dynstring_append_formated(string, exit_code_type_as_string(code.type));
	if (code.type == Exit_Code_Type::TYPE_INFO_WAITING_FOR_TYPE_FINISHED || code.type == Exit_Code_Type::CALL_TO_UNFINISHED_FUNCTION) {
		return;
	}
	if (code.options.error_msg != 0) {
		dynstring_append_formated(string, ", %s", code.options.error_msg);
	}
}



// Extern sources
Extern_Sources extern_sources_create(Arena* arena)
{
	Extern_Sources result;
	result.extern_functions = DynArray<Upp_Function*>::create(arena);
	for (int i = 0; i < (int)Extern_Compiler_Setting::MAX_ENUM_VALUE; i++) {
		result.compiler_settings[i] = DynArray<Identifier>::create(arena);
	}
	return result;
}



// FIBER POOL
struct Fiber_Startup_Info // Just a helper struct for change up initial fibers in pool
{
	Fiber_Pool* pool;
	int index_in_pool;
};

struct Fiber_Info
{
	Fiber_Handle handle;
	bool has_task_to_run; // If the fiber is currently executing a task or if its waiting for a new one
	fiber_entry_fn next_entry;
	void* next_userdata;
};

struct Fiber_Pool
{
	Fiber_Handle main_fiber; // Fiber that created the fiber pool
	Dynamic_Array<Fiber_Info> allocated_fibers;
	Dynamic_Array<int> next_free_index;
};

Fiber_Pool* fiber_pool_create() {
	Fiber_Pool* pool = new Fiber_Pool;
	pool->allocated_fibers = dynamic_array_create<Fiber_Info>(1);
	pool->next_free_index = dynamic_array_create<int>(1);
	if (!fiber_initialize()) {
		panic("Couldn't create fiber_pool, fiber initialization failed!\n");
	}
	pool->main_fiber = fiber_get_current();
	return pool;
}

void fiber_pool_set_current_fiber_to_main(Fiber_Pool* pool)
{
	pool->main_fiber = fiber_get_current();
}

void fiber_pool_destroy(Fiber_Pool* pool) {
	for (int i = 0; i < pool->allocated_fibers.size; i++) {
		fiber_delete(pool->allocated_fibers[i].handle);
	}
	dynamic_array_destroy(&pool->allocated_fibers);
	dynamic_array_destroy(&pool->next_free_index);
	delete pool;
}

void fiber_pool_instance_entry(void* userdata) 
{
	// Copy startup info to local stack
	Fiber_Startup_Info startup = *((Fiber_Startup_Info*)userdata);
	auto pool = startup.pool;
	// Switch to main fiber after copy and wait for commands
	fiber_switch_to(pool->main_fiber);

	while (true)
	{
		Fiber_Info info = pool->allocated_fibers[startup.index_in_pool];
		assert(info.has_task_to_run, "Trying to run a fiber without a given task!");
		if (info.next_entry == 0) {
			panic("Fiber pool instance started without properly setting the entryfn, should never happen!");
			break;
		}
		info.next_entry(info.next_userdata);
		pool->allocated_fibers[startup.index_in_pool].has_task_to_run = false;

		// When the fiber's job finishes, we add it back to the pool, and switch to the main fiber
		dynamic_array_push_back(&pool->next_free_index, startup.index_in_pool);
		pool->allocated_fibers[startup.index_in_pool].next_entry = 0; // Just for error detection
		pool->allocated_fibers[startup.index_in_pool].next_userdata = 0;
		fiber_switch_to(pool->main_fiber);
	}
}

Fiber_Pool_Handle fiber_pool_get_handle(Fiber_Pool* pool, fiber_entry_fn entry_fn, void* userdata) 
{
	// Setup new fiber if no free one is in pool
	if (pool->next_free_index.size == 0)
	{
		Fiber_Startup_Info startup;
		Fiber_Info info;
		info.handle = fiber_create(fiber_pool_instance_entry, &startup); // Note: startup is not filled out yet, but the pointer is still valid!
		info.next_entry = 0;
		info.next_userdata = 0;
		info.has_task_to_run = false;
		dynamic_array_push_back(&pool->allocated_fibers, info);
		dynamic_array_push_back(&pool->next_free_index, pool->allocated_fibers.size - 1);

		// Switch to fiber so it can grab its startup info, see 'fiber_pool_instance_entry'
		startup.pool = pool;
		startup.index_in_pool = pool->allocated_fibers.size - 1;
		fiber_switch_to(info.handle);
	}

	// Grab fiber from pool and setup to run the given function
	Fiber_Pool_Handle handle;
	handle.pool = pool;
	handle.pool_index = pool->next_free_index[pool->next_free_index.size - 1];
	pool->next_free_index.size = pool->next_free_index.size - 1; // Pop

	Fiber_Info* info = &pool->allocated_fibers[handle.pool_index];
	assert(!info->has_task_to_run, "We seem to be grabbing a function thats currently running!");
	info->next_entry = entry_fn;
	info->next_userdata = userdata;
	info->has_task_to_run = true;

	return handle;
}

bool fiber_pool_switch_to_handel(Fiber_Pool_Handle handle)
{
	Fiber_Info* info = &handle.pool->allocated_fibers[handle.pool_index];
	assert(info->has_task_to_run, "Fiber_Pool_Handle seems to be invalid, e.g. the task was already finished\n");
	fiber_switch_to(info->handle);
	// After return, return if task has completed
	info = &handle.pool->allocated_fibers[handle.pool_index]; // Refresh info pointer, just in case 
	return !info->has_task_to_run;
}

void fiber_pool_check_all_handles_completed(Fiber_Pool* pool) 
{
	for (int i = 0; i < pool->allocated_fibers.size; i++) {
		auto& info = pool->allocated_fibers[i];
		assert(!info.has_task_to_run, "Task must be completed!\n");
		info.next_entry = 0;
		info.next_userdata = 0;

		// Check that fiber is in next_free_entry list
		bool found = false;
		for (int j = 0; j < pool->next_free_index.size; j++) {
			if (pool->next_free_index[j] == i) {
				found = true;
				break;
			}
		}
		assert(found, "Finished fiber must be in next_free_entry list!\n");
	}

	// Reset next_free entry list
	assert(pool->next_free_index.size == pool->allocated_fibers.size, "Must be the same, since all fibers should be completed!\n");
	dynamic_array_reset(&pool->next_free_index);
	for (int i = 0; i < pool->allocated_fibers.size; i++) {
		dynamic_array_push_back(&pool->next_free_index, i);
	}
}

void fiber_pool_switch_to_main_fiber(Fiber_Pool* pool)
{
	fiber_switch_to(pool->main_fiber);
}



// Fiber Pool test
void test_print_int_task(void* userdata)
{
	int value = *((int*)userdata);
	logg("Fiber with userdata #%d working\n", value);
}

void test_pause_3_task(void* userdata)
{
	Fiber_Pool* pool = (Fiber_Pool*)userdata;
	logg("Wait 1\n");
	fiber_pool_switch_to_main_fiber(pool);
	logg("Wait 2\n");
	fiber_pool_switch_to_main_fiber(pool);
	logg("Wait 3\n");
	fiber_pool_switch_to_main_fiber(pool);
	logg("Finish\n");
}

void fiber_pool_test()
{
	Fiber_Pool* pool = fiber_pool_create();
	SCOPE_EXIT(fiber_pool_destroy(pool));

	int a = 1;
	int b = 2;
	int c = 3;
	Fiber_Pool_Handle handle1 = fiber_pool_get_handle(pool, test_print_int_task, &a);
	Fiber_Pool_Handle handle2 = fiber_pool_get_handle(pool, test_print_int_task, &b);
	bool finished = fiber_pool_switch_to_handel(handle1);
	assert(finished, "Must be finished now\n");
	finished = fiber_pool_switch_to_handel(handle2);
	assert(finished, "Must be finished now\n");

	Fiber_Pool_Handle pausing = fiber_pool_get_handle(pool, test_pause_3_task, pool);
	finished = false;
	while (!finished) {
		logg("switch to pausing\n");
		finished = fiber_pool_switch_to_handel(pausing);
	}
	logg("Returned from pausing!\n");

	assert(pool->allocated_fibers.size == 2, "Must not have allocated 3, since only max of 2 fibers at a time were active\n");
}

Optional<Datatype*> Call_Signature::return_type()
{
	assert(is_registered, "");
	if (return_type_index == -1) return optional_make_failure<Datatype*>();
	return optional_make_success(parameters[return_type_index].datatype);
}

int Call_Signature::param_count(bool with_return) 
{
	int param_count = parameters.size;
	if (return_type_index != -1) {
		param_count -= 1;
	}
	return param_count;
}
