#include "windows_helper_functions.hpp"

#include <Windows.h>

#include "../utility/utils.hpp"

void helper_print_last_error()
{
    LPVOID msg_buffer;
    DWORD error_code = GetLastError();
    if (error_code == 0) {
        logg("WIN32 no last error is recorded.");
        return;
    }

    FormatMessage(
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS |
        FORMAT_MESSAGE_ALLOCATE_BUFFER,
        NULL,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&msg_buffer,
        0, NULL);

    logg("WIN32 last error was: %d %s\n", error_code, (char*)msg_buffer);
    LocalFree(msg_buffer);
}
