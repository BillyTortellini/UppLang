#include "timing.hpp"

#include <Windows.h>

#include "../utility/datatypes.hpp"
#include "../utility/utils.hpp"
#include "windows_helper_functions.hpp"

static i64 timing_performance_frequency;
static i64 timing_start_time;
void timing_initialize()
{
    bool res = QueryPerformanceFrequency((LARGE_INTEGER*) &timing_performance_frequency);    
    if (!res) {
        helper_print_last_error();
        panic("Could not initialize timing");
    };
    QueryPerformanceCounter((LARGE_INTEGER*)&timing_start_time);
}

i64 timing_current_tick() {
    return __rdtsc();
}

double timing_current_time_in_seconds() {
    i64 now;
    QueryPerformanceCounter((LARGE_INTEGER*)&now);
    now = now - timing_start_time;
    return (double)now/timing_performance_frequency;
}

void timing_sleep_until(double until_in_seconds) {
    double now_in_seconds = timing_current_time_in_seconds();
    double diff = until_in_seconds - now_in_seconds;
    if (diff <= 0.0) return;

    int ms = (int)(diff*1000);
    // To make sure we sleep as accurate as possible (Windows only allows for 1 ms over/below accuracy
    // we sleep one ms less and do busy waiting for the last ms
    ms -= 1;
    if (ms > 0) {
        timeBeginPeriod(1); // Sets schedular to be at most 1 ms behind after sleep
        Sleep(ms);
        timeEndPeriod(1);
    }

    // Busy wait until time actually passes
    int sleep_cycles = 0;
    do { sleep_cycles++; } while (timing_current_time_in_seconds() < until_in_seconds);
}

void timing_sleep_for(double seconds) {
    double start = timing_current_time_in_seconds();
    timing_sleep_until(start + seconds);
}

