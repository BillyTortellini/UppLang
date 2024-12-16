#pragma once

struct Debugger;

Debugger* debugger_create();
void debugger_destroy(Debugger* debugger);

bool debugger_start_process(Debugger* debugger, const char* exe_filename, const char* pdb_filename);
void debugger_wait_next_event(Debugger* debugger); // Returns true if process has closed
bool debugger_continue_from_last_event(Debugger* debugger);
bool debugger_last_event_was_breakpoint_or_step(Debugger* debugger);
void debugger_wait_for_console_command(Debugger* debugger);

