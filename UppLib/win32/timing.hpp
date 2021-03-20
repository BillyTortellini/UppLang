#pragma once

#include "../utility/datatypes.hpp"

void timing_initialize();
i64 timing_current_tick();
double timing_current_time_in_seconds();
void timing_sleep_until(double until_in_seconds);
void timing_sleep_for(double seconds);