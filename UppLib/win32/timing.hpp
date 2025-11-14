#pragma once

#include "../utility/datatypes.hpp"

void timer_initialize();
i64 timer_current_cpu_tick();
double timer_current_time_in_seconds();
void timer_sleep_until(double until_in_seconds);
void timer_sleep_for(double seconds);

void timing_start();
void timing_log(const char* event);
void timing_end();