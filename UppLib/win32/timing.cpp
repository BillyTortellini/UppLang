#include "timing.hpp"

#include <Windows.h>

#include "../utility/datatypes.hpp"
#include "../utility/utils.hpp"
#include "windows_helper_functions.hpp"

i64 timing_current_cpu_tick() {
    return __rdtsc();
}

Timer timer_make()
{
    Timer result;
    bool res = QueryPerformanceFrequency((LARGE_INTEGER*) &result.timing_performance_frequency);    
    if (!res) {
        helper_print_last_error();
        panic("Could not initialize timing");
    };
    QueryPerformanceCounter((LARGE_INTEGER*) &result.timing_start_time);
    return result;
}

double timer_current_time_in_seconds(Timer* timer)
{
    i64 now;
    QueryPerformanceCounter((LARGE_INTEGER*)&now);
    now = now - timer->timing_start_time;
    return (double)now/timer->timing_performance_frequency;
}

void timer_sleep_until(Timer* timer, double until_in_seconds)
{
    double now_in_seconds = timer_current_time_in_seconds(timer);
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
    do { sleep_cycles++; } while (timer_current_time_in_seconds(timer) < until_in_seconds);
}

void timer_sleep_for(Timer* timer, double seconds) {
    double start = timer_current_time_in_seconds(timer);
    timer_sleep_until(timer, start + seconds);
}

