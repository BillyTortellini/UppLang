#include "timing.hpp"

#include <Windows.h>

#include "../utility/datatypes.hpp"
#include "../utility/utils.hpp"
#include "windows_helper_functions.hpp"

// Counts per second
i64 performance_frequency = 1;
i64 application_start_time = 0;

void timer_initialize() {
    bool res = QueryPerformanceFrequency((LARGE_INTEGER*) &performance_frequency);    
    if (!res) {
        helper_print_last_error();
        panic("From MSDN: This has to be supported for Windows XP or later, so we don't expect this to fail");
    }
    // Again, we expect this call to succeed
    QueryPerformanceCounter((LARGE_INTEGER*)&application_start_time);
}

i64 timer_current_cpu_tick() {
    return __rdtsc();
}

double timer_current_time_in_seconds()
{
    i64 now;
    QueryPerformanceCounter((LARGE_INTEGER*)&now);
    now = now - application_start_time;
    return (double)now/performance_frequency;
}

void timer_sleep_until(double until_in_seconds)
{
    double now_in_seconds = timer_current_time_in_seconds();
    double diff = until_in_seconds - now_in_seconds;
    if (diff <= 0.0) return;

    int ms = (int)(diff*1000);
    // To make sure we sleep as accurate as possible (Windows only allows for 1 ms over/below accuracy
    // we sleep one ms less and do busy waiting for the last ms
    ms -= 1;
    if (ms > 0) {
        timeBeginPeriod(1); 
        Sleep(ms);
        timeEndPeriod(1);
    }

    // Busy wait until time actually passes
    int sleep_cycles = 0;
    do { sleep_cycles++; } while (timer_current_time_in_seconds() < until_in_seconds);
}

void timer_sleep_for(double seconds) {
    double start = timer_current_time_in_seconds();
    timer_sleep_until(start + seconds);
}


thread_local double _time_counter = 0.0f;
thread_local bool _timer_started = false;
void timing_start() {
    _time_counter = timer_current_time_in_seconds();
    _timer_started = true;
}

void timing_log(const char* event) 
{
    if (!_timer_started) return;
    double now = timer_current_time_in_seconds();
    printf("%s took: %f\n", event, (float)(now - _time_counter));
    _time_counter = timer_current_time_in_seconds();
}

void timing_end()
{
    _timer_started = false;
}

