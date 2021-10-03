#pragma once

#include "../datastructures/string.hpp"

struct Process_Result
{
    int exit_code;
    String output;
};

Optional<Process_Result> process_start(String command);
int process_start_no_pipes(String command, bool wait_for_exit);
void process_result_destroy(Optional<Process_Result>* result);