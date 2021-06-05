#pragma once

#include "../utility/datatypes.hpp"

i64 timing_current_cpu_tick();

struct Timer
{
    i64 timing_performance_frequency;
    i64 timing_start_time;
};

Timer timer_make();
double timer_current_time_in_seconds(Timer* timer);
void timer_sleep_until(Timer* timer, double until_in_seconds);
void timer_sleep_for(Timer* timer, double seconds);