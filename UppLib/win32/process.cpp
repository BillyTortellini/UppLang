#include "process.hpp"

#include <Windows.h>
#include "windows_helper_functions.hpp"

Optional<Process_Result> process_start(String command)
{
    HANDLE handle_stdout_read = 0;
    HANDLE handle_stdout_write = 0;
    HANDLE handle_stdin_read = 0;
    HANDLE handle_stdin_write = 0;
    SCOPE_EXIT(if (handle_stdout_read != 0) { CloseHandle(handle_stdout_read); handle_stdout_read = 0; });
    SCOPE_EXIT(if (handle_stdout_write != 0) { CloseHandle(handle_stdout_write); handle_stdout_write = 0; });
    SCOPE_EXIT(if (handle_stdin_read != 0) { CloseHandle(handle_stdin_read); handle_stdin_read = 0; });
    SCOPE_EXIT(if (handle_stdin_write != 0) { CloseHandle(handle_stdin_write); handle_stdin_write = 0; });
    {
        SECURITY_ATTRIBUTES security_attributes;
        security_attributes.nLength = sizeof(security_attributes);
        security_attributes.bInheritHandle = true;
        security_attributes.lpSecurityDescriptor = NULL;

        bool success = true;
        if (!CreatePipe(&handle_stdout_read, &handle_stdout_write, &security_attributes, 0)) {
            logg("Pipe problem");
            success = false;
        }
        if (success && !SetHandleInformation(handle_stdout_read, HANDLE_FLAG_INHERIT, 0)) {
            logg("Pipe problem");
            success = false;
        }
        if (!CreatePipe(&handle_stdin_read, &handle_stdin_write, &security_attributes, 0)) {
            logg("Pipe problem");
            success = false;
        }
        if (success && !SetHandleInformation(handle_stdin_read, HANDLE_FLAG_INHERIT, 0)) {
            logg("Pipe problem");
            success = false;
        }
        if (!success) {
            logg("Pipe problem was detected");
            return optional_make_failure<Process_Result>();
        }
    }

    STARTUPINFO start_info;
    ZeroMemory(&start_info, sizeof(start_info));
    start_info.cb = sizeof(start_info);
    start_info.dwFlags |= STARTF_USESTDHANDLES;
    start_info.hStdError = handle_stdout_write;
    start_info.hStdOutput = handle_stdout_write;
    start_info.hStdInput = handle_stdin_read;

    PROCESS_INFORMATION process_info;
    ZeroMemory(&process_info, sizeof(process_info));

    bool success = CreateProcessA(
        0,
        command.characters,
        NULL, // Security Stuff
        NULL, // Primary thread security stuff
        TRUE, // Inherit handles
        0, // Creation flags
        0, // Use Parents environment (But it only gets copied)
        0, // Use Parents directory
        &start_info,
        &process_info
    );
    if (!success) {
        helper_print_last_error();
        return optional_make_failure<Process_Result>();
    }

    SCOPE_EXIT(
        CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
    );

    // Close child pipes
    CloseHandle(handle_stdout_write);
    handle_stdout_write = 0;
    CloseHandle(handle_stdin_read);
    handle_stdin_read = 0;
    // Close parent unused pipes
    CloseHandle(handle_stdin_write);
    handle_stdin_write = 0;

    // Read from child
    Process_Result result;
    result.output = string_create_empty(1024);
    char buffer[1024];
    DWORD read_bytes = 0;
    while (true)
    {
        success = ReadFile(handle_stdout_read, buffer, 1024, &read_bytes, NULL);
        if (!success || read_bytes == 0) break;
        string_append_character_array(&result.output, array_create_static(buffer, read_bytes));
    }
    CloseHandle(handle_stdout_read);
    handle_stdout_read = 0;

    success = true;
    WaitForSingleObject(process_info.hProcess, INFINITE);
    DWORD exit_code = 0;
    if (GetExitCodeProcess(process_info.hProcess, &exit_code) == FALSE) {
        success = false;
        logg("Could not get exit code?\n");
        result.exit_code = 1;
    }
    else {
        result.exit_code = exit_code;
    }

    return optional_make_success(result);
}

int process_start_no_pipes(String command, bool wait_for_exit)
{
    STARTUPINFO start_info;
    ZeroMemory(&start_info, sizeof(start_info));
    start_info.cb = sizeof(start_info);

    PROCESS_INFORMATION process_info;
    ZeroMemory(&process_info, sizeof(process_info));

    bool success = CreateProcessA(
        0,
        command.characters,
        NULL, // Security Stuff
        NULL, // Primary thread security stuff
        FALSE, // Inherit handles
        0, // Creation flags
        0, // Use Parents environment (But it may just be copied)
        0, // Use Parents directory
        &start_info,
        &process_info
    );
    if (!success) {
        helper_print_last_error();
        return 1;
    }

    DWORD exit_code = 0;
    if (wait_for_exit) 
    {
        WaitForSingleObject(process_info.hProcess, INFINITE);
        if (GetExitCodeProcess(process_info.hProcess, &exit_code) == FALSE) {
            success = false;
            logg("Could not get exit code?\n");
            exit_code = 1;
        }
        else {
            exit_code = exit_code;
        }
    }
    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
    
    return exit_code;
}

void process_result_destroy(Optional<Process_Result>* result)
{
    if (result->available) {
        string_destroy(&result->value.output);
    }
}
