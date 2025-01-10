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
        "P:/Martin/Projects/UppLib/backend/test/main.obj",
        nullptr
    );
    if (!started) {
        printf("Couldn't start debugger\n");
        return;
    }

    Debugger_State state = debugger_get_state(debugger);
    while (state != Debugger_State::NO_ACTIVE_PROCESS) {
        debugger_wait_for_console_command(debugger);
        state = debugger_continue_until_next_breakpoint_or_exit(debugger);
    }

    printf("\n-----------\nProcess finished\n");
    std::cin.ignore();
}
