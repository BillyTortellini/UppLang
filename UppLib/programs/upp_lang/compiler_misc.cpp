#include "compiler_misc.hpp"

#include "type_system.hpp"
#include "compiler.hpp"

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
    case Hardcoded_Type::ASSERT_FN:
        string_append_formated(string, "ASSERT");
        break;
    case Hardcoded_Type::TYPE_INFO:
        string_append_formated(string, "TYPE_INFO");
        break;
    case Hardcoded_Type::TYPE_OF:
        string_append_formated(string, "TYPE_OF");
        break;
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
    case Hardcoded_Type::MALLOC_SIZE_I32:
        string_append_formated(string, "MALLOC_SIZE_I32");
        break;
    case Hardcoded_Type::FREE_POINTER:
        string_append_formated(string, "FREE_POINTER");
        break;
    default: panic("Should not happen");
    }
}

bool exit_code_is_valid(int value)
{
    return value >= (int)Exit_Code::SUCCESS && value <= (int)Exit_Code::TYPE_INFO_WAITING_FOR_TYPE_FINISHED;
}

void exit_code_append_to_string(String* string, Exit_Code code)
{
    switch (code)
    {
    case Exit_Code::ASSERTION_FAILED:
        string_append_formated(string, "ASSERTION_FAILED");
        break;
    case Exit_Code::OUT_OF_BOUNDS:
        string_append_formated(string, "OUT_OF_BOUNDS");
        break;
    case Exit_Code::RETURN_VALUE_OVERFLOW:
        string_append_formated(string, "RETURN_VALUE_OVERFLOW");
        break;
    case Exit_Code::STACK_OVERFLOW:
        string_append_formated(string, "STACK_OVERFLOW");
        break;
    case Exit_Code::SUCCESS:
        string_append_formated(string, "SUCCESS");
        break;
    case Exit_Code::COMPILATION_FAILED:
        string_append_formated(string, "COMPILATION_FAILED");
        break;
    case Exit_Code::EXTERN_FUNCTION_CALL_NOT_IMPLEMENTED:
        string_append_formated(string, "EXTERN_FUNCTION_CALL_NOT_IMPLEMENTED");
        break;
    case Exit_Code::ANY_CAST_INVALID:
        string_append_formated(string, "ANY_CAST_INVALID");
        break;
    case Exit_Code::INSTRUCTION_LIMIT_REACHED:
        string_append_formated(string, "INSTRUCTION_LIMIT_REACHED");
        break;
    case Exit_Code::INVALID_SWITCH_CASE:
        string_append_formated(string, "INVALID_SWITCH_CASE");
        break;
    case Exit_Code::CODE_ERROR_OCCURED:
        string_append_formated(string, "CODE_ERROR_OCCURED");
        break;
    case Exit_Code::TYPE_INFO_WAITING_FOR_TYPE_FINISHED:
        string_append_formated(string, "TYPE_INFO_WAITING_FOR_TYPE_FINISHED");
        break;
    default: panic("Hey");
    }
}




// Extern sources
Extern_Sources extern_sources_create()
{
    Extern_Sources result;
    result.extern_functions = dynamic_array_create_empty<Extern_Function_Identifier>(8);
    result.headers_to_include = dynamic_array_create_empty<String*>(8);
    result.source_files_to_compile = dynamic_array_create_empty<String*>(8);
    result.lib_files = dynamic_array_create_empty<String*>(8);
    result.extern_type_signatures = hashtable_create_pointer_empty<Datatype*, String*>(8);
    return result;
}

void extern_sources_destroy(Extern_Sources* sources)
{
    dynamic_array_destroy(&sources->extern_functions);
    dynamic_array_destroy(&sources->headers_to_include);
    dynamic_array_destroy(&sources->source_files_to_compile);
    dynamic_array_destroy(&sources->lib_files);
    hashtable_destroy(&sources->extern_type_signatures);
}



// Identifier Pool
Identifier_Pool identifier_pool_create()
{
    Identifier_Pool result;
    result.identifier_lookup_table = hashtable_create_empty<String, String*>(128, hash_string, string_equals);
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
}

String* identifier_pool_add(Identifier_Pool* pool, String identifier)
{
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
    pool->allocated_fibers = dynamic_array_create_empty<Fiber_Info>(1);
    pool->next_free_index = dynamic_array_create_empty<int>(1);
    if (!fiber_initialize()) {
        panic("Couldn't create fiber_pool, fiber initialization failed!\n");
    }
    pool->main_fiber = fiber_get_current();
    return pool;
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




