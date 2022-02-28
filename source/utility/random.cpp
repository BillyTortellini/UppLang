#include "random.hpp"

#include "../win32/timing.hpp"

Random random_make(u32 seed, int warm_up_period)
{
    Random result;
    result.state = seed;
    // Run for some iterators to get the generator "warm"
    for (int i = 0; i < warm_up_period; i++) {
        random_next_u32(&result);
    }
    return result;
}

Random random_make_time_initalized()
{
    uint32 a = 0;
    while (a == 0) {
        a = (uint32)timing_current_cpu_tick();
    }
    return random_make(a, 5000);
}

u32 random_next_u32(Random* random) 
{
    uint32 a = random->state;
    a ^= a << 13;
    a ^= a >> 17;
    a ^= a << 5;
    random->state = a;
    return a;
}

float random_next_float(Random* random) {
    uint32 rand = random_next_u32(random);
    rand = rand % 100000;
    double r = (double)rand / 100000.0;
    return (float)r;
}

bool random_next_bool(Random* random, float probability) {
    return random_next_float(random) < probability;
}
