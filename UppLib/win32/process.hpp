#pragma once

#include "../datastructures/string.hpp"

struct Process_Result
{
    bool process_started;
    int exit_code;
};

// Output string may be null
Process_Result process_start(String command, bool wait_for_exit, DynString* output_string);



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
