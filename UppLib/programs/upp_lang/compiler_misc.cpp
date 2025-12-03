#include "compiler_misc.hpp"

#include "type_system.hpp"
#include "compiler.hpp"

const char* cast_type_to_string(Cast_Type type)
{
    switch (type)
    {
	case Cast_Type::INTEGERS: return "INTEGERS";
	case Cast_Type::FLOATS: return "FLOATS";
	case Cast_Type::FLOAT_TO_INT: return "FLOAT_TO_INT";
	case Cast_Type::INT_TO_FLOAT: return "INT_TO_FLOAT";
	case Cast_Type::POINTERS: return "POINTERS";
	case Cast_Type::POINTER_TO_ADDRESS: return "POINTER_TO_ADDRESS";
	case Cast_Type::ADDRESS_TO_POINTER: return "ADDRESS_TO_POINTER";
	case Cast_Type::ENUMS: return "ENUMS";
	case Cast_Type::ENUM_TO_INT: return "ENUM_TO_INT";
	case Cast_Type::INT_TO_ENUM: return "INT_TO_ENUM";
	case Cast_Type::ARRAY_TO_SLICE: return "ARRAY_TO_SLICE";
	case Cast_Type::TO_ANY: return "TO_ANY";
	case Cast_Type::FROM_ANY: return "FROM_ANY";
	case Cast_Type::CUSTOM_CAST: return "CUSTOM_CAST";
	case Cast_Type::NO_CAST: return "NO_CAST";
	case Cast_Type::UNKNOWN: return "UNKNOWN";
	case Cast_Type::TO_BASE_TYPE: return "TO_BASE_TYPE";
	case Cast_Type::TO_SUB_TYPE: return "TO_SUB_TYPE";
	case Cast_Type::INVALID: return "INVALID";
	default: panic("");
	}

	panic("");
	return "FRICK";
}

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

void hardcoded_type_append_to_string(String* string, Hardcoded_Type hardcoded)
{
	switch (hardcoded)
	{
	case Hardcoded_Type::ASSERT_FN: string_append_formated(string, "ASSERT"); break;
	case Hardcoded_Type::TYPE_INFO: string_append_formated(string, "TYPE_INFO"); break;
	case Hardcoded_Type::TYPE_OF: string_append_formated(string, "TYPE_OF"); break;
	case Hardcoded_Type::SIZE_OF: string_append_formated(string, "SIZE_OF"); break;
	case Hardcoded_Type::ALIGN_OF: string_append_formated(string, "ALIGN_OF"); break;
	case Hardcoded_Type::PANIC_FN: string_append_formated(string, "PANIC"); break;
	case Hardcoded_Type::RETURN_TYPE: string_append_formated(string, "RETURN_TYPE"); break;
	case Hardcoded_Type::STRUCT_TAG: string_append_formated(string, "STRUCT_TAG"); break;

	case Hardcoded_Type::MEMORY_COPY: string_append_formated(string, "MEMORY_COPY"); break;
	case Hardcoded_Type::MEMORY_COMPARE: string_append_formated(string, "MEMORY_COMPARE"); break;
	case Hardcoded_Type::MEMORY_ZERO: string_append_formated(string, "MEMORY_ZERO"); break;

	case Hardcoded_Type::SYSTEM_ALLOC: string_append_formated(string, "SYSTEM_ALLOC"); break;
	case Hardcoded_Type::SYSTEM_FREE: string_append_formated(string, "SYSTEM_FREE"); break;

	case Hardcoded_Type::BITWISE_NOT: string_append_formated(string, "BITWISE_NOT"); break;
	case Hardcoded_Type::BITWISE_AND: string_append_formated(string, "BITWISE_AND"); break;
	case Hardcoded_Type::BITWISE_OR: string_append_formated(string, "BITWISE_OR"); break;
	case Hardcoded_Type::BITWISE_XOR: string_append_formated(string, "BITWISE_XOR"); break;
	case Hardcoded_Type::BITWISE_SHIFT_LEFT: string_append_formated(string, "BITWISE_SHIFT_LEFT"); break;
	case Hardcoded_Type::BITWISE_SHIFT_RIGHT: string_append_formated(string, "BITWISE_SHIFT_RIGHT"); break;
	case Hardcoded_Type::PRINT_I32:
		string_append_formated(string, "PRINT_I32");
		break;
	case Hardcoded_Type::PRINT_F32:
		string_append_formated(string, "PRINT_F32");
		break;
	case Hardcoded_Type::PRINT_BOOL:
		string_append_formated(string, "PRINT_BOOL");
		break;
	case Hardcoded_Type::PRINT_LINE:
		string_append_formated(string, "PRINT_LINE");
		break;
	case Hardcoded_Type::PRINT_STRING:
		string_append_formated(string, "PRINT_STRING");
		break;
	case Hardcoded_Type::READ_I32:
		string_append_formated(string, "READ_I32");
		break;
	case Hardcoded_Type::READ_F32:
		string_append_formated(string, "READ_F32");
		break;
	case Hardcoded_Type::READ_BOOL:
		string_append_formated(string, "READ_BOOL");
		break;
	case Hardcoded_Type::RANDOM_I32:
		string_append_formated(string, "RANDOM_I32");
		break;
	default: panic("Should not happen");
	}
}

Exit_Code exit_code_make(Exit_Code_Type type, const char* error_msg)
{
	Exit_Code result;
	result.type = type;
	result.error_msg = error_msg;
	return result;
}

const char* exit_code_type_as_string(Exit_Code_Type type)
{
	switch (type)
	{
	case Exit_Code_Type::SUCCESS: return "SUCCESS";
	case Exit_Code_Type::COMPILATION_FAILED: return "COMPILATION_FAILED";
	case Exit_Code_Type::CODE_ERROR: return "CODE_ERROR";
	case Exit_Code_Type::EXECUTION_ERROR: return "EXECUTION_ERROR";
	case Exit_Code_Type::INSTRUCTION_LIMIT_REACHED: return "INSTRUCTION_LIMIT_REACHED";
	case Exit_Code_Type::TYPE_INFO_WAITING_FOR_TYPE_FINISHED: return "TYPE_INFO_WAITING_FOR_TYPE_FINISH";
	default: panic("");
	}
	return "";
}

void exit_code_append_to_string(String* string, Exit_Code code)
{
	string_append_formated(string, exit_code_type_as_string(code.type));
	if (code.error_msg != 0) {
		string_append_formated(string, ", %s", code.error_msg);
	}
}



// Extern sources
Extern_Sources extern_sources_create()
{
	Extern_Sources result;
	result.extern_functions = dynamic_array_create<ModTree_Function*>();
	for (int i = 0; i < (int)Extern_Compiler_Setting::MAX_ENUM_VALUE; i++) {
		result.compiler_settings[i] = dynamic_array_create<String*>();
	}
	return result;
}

void extern_sources_destroy(Extern_Sources* sources)
{
	dynamic_array_destroy(&sources->extern_functions);
	for (int i = 0; i < (int)Extern_Compiler_Setting::MAX_ENUM_VALUE; i++) {
		dynamic_array_destroy(&sources->compiler_settings[i]);
	}
}



// Identifier Pool
Identifier_Pool identifier_pool_create()
{
	Identifier_Pool result;
	result.identifier_lookup_table = hashtable_create_empty<String, String*>(128, hash_string, string_equals);
	result.add_identifier_semaphore = semaphore_create(1, 1);

	// Add predefined IDs
	{
		// Make dummy lock for predefined things
		Identifier_Pool_Lock lock;
		lock.pool = &result;

		auto& ids = result.predefined_ids;
		auto add_id = [&](const char* id) -> String* {
			return identifier_pool_add(&lock, string_create_static(id));
			};

		ids.size = add_id("size");
		ids.data = add_id("data");
		ids.tag = add_id("tag");
		ids.anon_struct = add_id("Anonymous");
		ids.anon_enum = add_id("Anon_Enum");
		ids.main = add_id("main");
		ids.type_of = add_id("type_of");
		ids.type_info = add_id("type_info");
		ids.empty_string = add_id("");
		ids.root_module = add_id("_ROOT_");
		ids.invalid_symbol_name = add_id("__INVALID_SYMBOL_NAME");
		ids.id_struct = add_id("Struct");
		ids.byte = add_id("byte");
		ids.value = add_id("value");
		ids.is_available = add_id("is_available");
		ids.uninitialized_token = add_id("_");
		ids.return_type_name = add_id("!return_type_name");
		ids.operators = add_id("operators");
		ids.dot_calls = add_id("dot_calls");
		ids.c_string = add_id("c_string");
		ids.string = add_id("string");
		ids.allocator = add_id("Allocator");
		ids.bytes = add_id("bytes");
		ids.lambda_function = add_id("lambda_function");
		ids.bake_function = add_id("bake_function");

		ids.hashtag_instanciate = add_id("#instanciate");
		ids.hashtag_bake = add_id("#bake");
		ids.hashtag_get_overload = add_id("#get_overload");
		ids.hashtag_get_overload_poly = add_id("#get_overload_poly");
	    ids.hashtag_add_binop = add_id("#add_binop");
	    ids.hashtag_add_unop = add_id("#add_unop");
	    ids.hashtag_add_cast = add_id("#add_cast");
	    ids.hashtag_add_auto_cast_type = add_id("#add_auto_cast_type");
	    ids.hashtag_add_iterator = add_id("#add_iterator");
	    ids.hashtag_add_array_access = add_id("#add_array_access");

		ids.defer_restore = add_id("defer_restore");
		ids.cast = add_id("cast");
		ids.defer = add_id("defer");
		ids.from = add_id("from");

		ids.function = add_id("function");
		ids.create_fn = add_id("create_fn");
		ids.next_fn = add_id("next_fn");
		ids.has_next_fn = add_id("has_next_fn");
		ids.value_fn = add_id("value_fn");
		ids.name = add_id("name");
		ids.as_member_access = add_id("as_member_access");
		ids.commutative = add_id("commutative");
		ids.binop = add_id("binop");
		ids.unop = add_id("unop");
		ids.option = add_id("option");
		ids.global = add_id("global");
		ids.lib = add_id("lib");
		ids.lib_dir = add_id("lib_dir");
		ids.source = add_id("source");
		ids.header = add_id("header");
		ids.header_dir = add_id("header_dir");
		ids.definition = add_id("definition");

		ids.cast_type = add_id("Cast_Type");
		ids.cast_type_enum_values[(int)Cast_Type::INTEGERS] = add_id("INTEGERS");
		ids.cast_type_enum_values[(int)Cast_Type::FLOATS] = add_id("FLOATS");
		ids.cast_type_enum_values[(int)Cast_Type::ENUMS] = add_id("ENUMS");
		ids.cast_type_enum_values[(int)Cast_Type::FLOAT_TO_INT] = add_id("FLOAT_TO_INT");
		ids.cast_type_enum_values[(int)Cast_Type::INT_TO_FLOAT] = add_id("INT_TO_FLOAT");
		ids.cast_type_enum_values[(int)Cast_Type::ENUM_TO_INT] = add_id("ENUM_TO_INT");
		ids.cast_type_enum_values[(int)Cast_Type::INT_TO_ENUM] = add_id("INT_TO_ENUM");
		ids.cast_type_enum_values[(int)Cast_Type::POINTERS] = add_id("POINTERS");
		ids.cast_type_enum_values[(int)Cast_Type::POINTER_TO_ADDRESS] = add_id("POINTER_TO_ADDRESS");
		ids.cast_type_enum_values[(int)Cast_Type::ADDRESS_TO_POINTER] = add_id("ADDRESS_TO_POINTER");
		ids.cast_type_enum_values[(int)Cast_Type::TO_SUB_TYPE] = add_id("TO_SUB_TYPE");
		ids.cast_type_enum_values[(int)Cast_Type::TO_BASE_TYPE] = add_id("TO_BASE_TYPE");
		ids.cast_type_enum_values[(int)Cast_Type::ARRAY_TO_SLICE] = add_id("ARRAY_TO_SLICE");
		ids.cast_type_enum_values[(int)Cast_Type::TO_ANY] = add_id("TO_ANY");
		ids.cast_type_enum_values[(int)Cast_Type::FROM_ANY] = add_id("FROM_ANY");
		ids.cast_type_enum_values[(int)Cast_Type::CUSTOM_CAST] = add_id("CUSTOM_CAST");
		ids.cast_type_enum_values[(int)Cast_Type::NO_CAST] = add_id("NO_CAST");
		ids.cast_type_enum_values[(int)Cast_Type::UNKNOWN] = add_id("UNKNOWN");
		ids.cast_type_enum_values[(int)Cast_Type::INVALID] = add_id("INVALID");
	}

	return result;
}

void identifier_pool_destroy(Identifier_Pool* pool)
{
	auto iter = hashtable_iterator_create(&pool->identifier_lookup_table);
	while (hashtable_iterator_has_next(&iter)) {
		String* str = *iter.value;
		string_destroy(str);
		delete str;
		hashtable_iterator_next(&iter);
	}
	hashtable_destroy(&pool->identifier_lookup_table);
	semaphore_destroy(pool->add_identifier_semaphore);
}

Identifier_Pool_Lock identifier_pool_lock_aquire(Identifier_Pool* pool) {
	semaphore_wait(pool->add_identifier_semaphore);
	Identifier_Pool_Lock result;
	result.pool = pool;
	return result;
}

void identifier_pool_lock_release(Identifier_Pool_Lock& lock) {
	assert(lock.pool != nullptr, "");
	semaphore_increment(lock.pool->add_identifier_semaphore, 1);
	lock.pool = nullptr;
}

String* identifier_pool_add(Identifier_Pool_Lock* lock, String identifier)
{
	auto pool = lock->pool;
	String** found = hashtable_find_element(&pool->identifier_lookup_table, identifier);
	if (found != 0) {
		return *found;
	}
	else {
		String* copy = new String;
		*copy = string_create_empty(identifier.size);
		string_append_string(copy, &identifier);
		hashtable_insert_element(&pool->identifier_lookup_table, *copy, copy);
		return copy;
	}
}

String* identifier_pool_lock_and_add(Identifier_Pool* pool, String identifier) {
	Identifier_Pool_Lock lock = identifier_pool_lock_aquire(pool);
	String* str = identifier_pool_add(&lock, identifier);
	identifier_pool_lock_release(lock);
	return str;
}

void identifier_pool_print(Identifier_Pool* pool)
{
	String msg = string_create_empty(256);
	SCOPE_EXIT(string_destroy(&msg));
	string_append_formated(&msg, "Identifiers: ");

	auto iter = hashtable_iterator_create(&pool->identifier_lookup_table);
	int i = 0;
	while (hashtable_iterator_has_next(&iter)) {
		String* str = *iter.value;
		string_append_formated(&msg, "\n\t%d: %s", i, str->characters);
		hashtable_iterator_next(&iter);
		i++;
	}
	string_append_formated(&msg, "\n");
	logg("%s", msg.characters);
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

void fiber_pool_instance_entry(void* userdata) {
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

Fiber_Pool_Handle fiber_pool_get_handle(Fiber_Pool* pool, fiber_entry_fn entry_fn, void* userdata) {
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

void fiber_pool_check_all_handles_completed(Fiber_Pool* pool) {
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


