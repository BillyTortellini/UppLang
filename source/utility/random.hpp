#pragma once

#include "datatypes.hpp"

struct Random {
    u32 state;
};

Random random_make(u32 seed, int warm_up_period);
Random random_make_time_initalized();

bool random_next_bool(Random* random, float probability);
float random_next_float(Random* random);
u32 random_next_u32(Random* random);
