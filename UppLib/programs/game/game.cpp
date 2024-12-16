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

    if (!debugger_start_process(debugger, "P:/Martin/Projects/UppLib/backend/test/main.exe", "P:/Martin/Projects/UppLib/backend/test/main.pdb")) {
        printf("Couldn't start debugger\n");
        return;
    }

    while (true)
    {
        debugger_wait_next_event(debugger);
        if (debugger_last_event_was_breakpoint_or_step(debugger)) {
            debugger_wait_for_console_command(debugger);
        }
        if (debugger_continue_from_last_event(debugger)) {
            break;
        }
    }

    printf("\n-----------\nProcess finished\n");
    std::cin.ignore();
}
