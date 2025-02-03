#include "console_debugger.hpp"

#include "../../utility/datatypes.hpp"  
#include "../../utility/utils.hpp"    
#include "../upp_lang/debugger.hpp"

#include <cstdio>
#include <iostream>

void console_debugger_entry()
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

    Debug_Process_State state = debugger_get_state(debugger).process_state;
    while (state != Debug_Process_State::NO_ACTIVE_PROCESS) {
        debugger_wait_for_console_command(debugger);
        debugger_resume_until_next_halt_or_exit(debugger);
        state = debugger_get_state(debugger).process_state;
    }

    printf("\n-----------\nProcess finished\n");
    std::cin.ignore();
}
