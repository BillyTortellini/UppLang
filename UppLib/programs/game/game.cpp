#include "game.hpp"

#include "../../utility/datatypes.hpp"  
#include "../../utility/utils.hpp"    
#include "../upp_lang/debugger.hpp"

#include <cstdio>
#include <iostream>

void game_entry()
{
    Debugger* debugger = debugger_create();
    SCOPE_EXIT(debugger_destroy(debugger));

    bool started = debugger_start_process(
        debugger,
        "P:/Martin/Projects/UppLib/backend/test/main.exe",
        "P:/Martin/Projects/UppLib/backend/test/main.pdb",
        "P:/Martin/Projects/UppLib/backend/test/main.obj"
    );
    if (!started) {
        printf("Couldn't start debugger\n");
        return;
    }

    debugger_wait_for_console_command(debugger);
    while (debugger_process_running(debugger)) {
        if (debugger_wait_and_handle_next_event(debugger)) {
            break;
        }
        if (debugger_last_event_was_breakpoint_or_step(debugger)) {
            debugger_wait_for_console_command(debugger);
        }
    }

    printf("\n-----------\nProcess finished\n");
    std::cin.ignore();
}
