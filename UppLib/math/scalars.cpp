#include "scalars.hpp"

#include <cmath>

u64 math_round_previous_multiple(u64 x, u64 modulo) { 
    return x - x % modulo;
}

u64 math_round_next_multiple(u64 x, u64 m) {
    if (x % m == 0) {
        return x;
    }
    return x + (m - x % m);
}

i32 math_round_next_multiple(i32 x, i32 m) {
    if (x % m == 0) {
        return x;
    }
    return x + (m - x % m);
}

float math_floor(float x) {
    return floorf(x);
}
double math_floor(double x) {
    return floorl(x);
}
float math_ceil(float x) {
    return ceilf(x);
}
double math_ceil(double x) {
    return ceill(x);
}

int math_modulo(int x, int modulo) {
    return ((x % modulo) + modulo) % modulo;
}

double math_modulo(double x, double modulo) {
    return x - modulo*math_floor(x/modulo);
}

float math_modulo(float x, float modulo) {
    return x - modulo*math_floor(x/modulo);
}

float math_remainder(float x, float modulo) {
    return x - modulo * floorf(x/modulo);
}

float math_modulo_in_interval(float x, float min, float max) {
    return (float)math_modulo(x - min, max - min) + min;
}

float math_cosine(float x) { return cosf(x); }
float math_sine(float x) { return sinf(x); }
float math_tangent(float x) { return tanf(x); }
float math_arcsine(float x) { return asinf(x); }
float math_arccosine(float x) { return acosf(x); }
float math_arctangent(float x) { return atanf(x); }
float math_arctangent_2(float y, float x) { return atan2f(y, x); }
float math_square_root(float x) { return sqrtf(x); }
double math_square_root(double x) { return sqrtl(x); }

float math_power(float x, float y) { return powf(x, y); }
double math_power(double x, double y) { return powl(x, y); }

float math_radians_to_degree(float radians) {
    return radians / PI * 180.0f;
}
// degreeToRadians
float math_degree_to_radians(float degree) {
    return degree / (180.0f) * PI;
}
