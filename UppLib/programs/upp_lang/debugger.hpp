#pragma once

#include "../../utility/datatypes.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../datastructures/string.hpp"

struct Debugger;
struct Compilation_Unit;
struct Compiler_Analysis_Data;

Debugger* debugger_create();
void debugger_destroy(Debugger* debugger);

bool debugger_start_process(Debugger* debugger, const char* exe_filename, const char* pdb_filename, const char* main_obj_filepath);
bool debugger_process_running(Debugger* debugger);
bool debugger_wait_and_handle_next_event(Debugger* debugger); // Returns true if process has closed
bool debugger_last_event_was_breakpoint_or_step(Debugger* debugger);
void debugger_wait_for_console_command(Debugger* debugger);

// Returns 0 if not found
u64 debugger_find_address_of_line(Debugger* debugger, int line_index);





struct Source_Debugger;

struct Source_Stack_Frame
{
    bool is_upp_function;
    union {
        struct {
            int slot;
            Compilation_Unit* unit;
            int line_index;
        } upp_function;
        struct {
            String symbol_name; // may be empty
            String dll_name; // may also be empty
            u64 offset_from_symbol_start;
        } other;
    } options;
};

struct Source_Debugger_State
{
    bool process_running;
    bool hit_breakpoint; //  if exception, otherwise we hit a breakpoint
    Dynamic_Array<Source_Stack_Frame> stack_frames;
};

struct Source_Breakpoint
{
    int line_index;
    Compilation_Unit* compilation_unit;
    Dynamic_Array<u64> addresses;
};

Source_Debugger* source_debugger_create();
void source_debugger_destroy(Source_Debugger* debugger);
void source_debugger_reset(Source_Debugger* debugger);

Source_Debugger_State* source_debugger_get_state(Source_Debugger* debugger);
bool source_debugger_start_process(
    Source_Debugger* debugger, const char* exe_filepath, const char* pdb_filepath, const char* main_obj_filepath, Compiler_Analysis_Data* analysis_data
);
void source_debugger_detach_process(Source_Debugger* debugger);
Source_Debugger_State* source_debugger_continue(Source_Debugger* debugger);
Source_Breakpoint* source_debugger_add_breakpoint(Source_Debugger* debugger, int line_index, Compilation_Unit* unit);






