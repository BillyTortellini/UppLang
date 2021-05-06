#include "random.hpp"

#include "../win32/timing.hpp"

uint32 g_xor_shift;
uint32 random_next_int() {
    uint32 a = g_xor_shift;
    a ^= a << 13;
    a ^= a >> 17;
    a ^= a << 5;
    g_xor_shift = a;
    return a;
}

float random_next_float() {
    uint32 random = random_next_int();
    random = random % 100000;
    double r = (double)random / 100000.0;
    return (float)r;
}

bool random_next_bool(float probability) {
    return random_next_float() < probability;
}

void random_initialize() {
    uint32 a = 0;
    // Initialize with current time
    while (a == 0) {
        a = (uint32)timing_current_tick();
    }
    g_xor_shift = a;
    // Run for some iterators to get the generator "warm"
    for (int i = 0; i < 10000; i++) {
        random_next_int();
    }
}
