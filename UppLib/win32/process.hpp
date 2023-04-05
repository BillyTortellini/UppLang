#pragma once

#include "../datastructures/string.hpp"

struct Process_Result
{
    int exit_code;
    String output;
};

Optional<Process_Result> process_start(String command);
int process_start_no_pipes(String command, bool wait_for_exit);
void process_result_destroy(Optional<Process_Result>* result);

struct Thread_Handle
{
    unsigned long thread_id;
    void* handle;
};


struct Fiber_Handle
{
    void* handle;
};

typedef void (*fiber_entry_fn)(void* userdata);

bool fiber_initialize(); // Afterwards fiber can be retrieved with fiber_get_current
Fiber_Handle fiber_get_current();
Fiber_Handle fiber_create(fiber_entry_fn entry_fn, void* user_data);
void fiber_switch_to(Fiber_Handle fiber);
void fiber_delete(Fiber_Handle fiber); // If this is the currently running fiber, the executing thread stops
void test_fibers();

/*
typedef unsigned long (*thread_entry_fn)(void*);
Optional<Thread_Handle> thread_create(thread_entry_fn entry_fn, void* userdata)
{
    Thread_Handle result;
    result.handle = CreateThread(NULL, 0, entry_fn, userdata, 0, &result.thread_id);
    if (result.handle != 0) return optional_make_success(result);
    return optional_make_failure<Thread_Handle>();
}

unsigned long thread_get_return_value(Thread_Handle handle)
{

}
*/
