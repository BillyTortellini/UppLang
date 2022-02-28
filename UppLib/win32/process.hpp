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
