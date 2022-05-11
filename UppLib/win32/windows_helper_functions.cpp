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

static char buffer[256];
Optional<String> open_file_selection_dialog()
{
    // open a file name
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = buffer;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(buffer);
    ofn.lpstrFilter = "All\0*.*\0Text\0*.TXT\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    int ret_val = GetOpenFileName(&ofn);
    if (ret_val == 0) return optional_make_failure<String>();
    return optional_make_success(string_create_static(ofn.lpstrFile));
}
