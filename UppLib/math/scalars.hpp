#pragma once

#include "../utility/datatypes.hpp"

// ------------------------
// --- SCALAR FUNCTIONS ---
// ------------------------
// Take one or multiple scalars and produce another scalar
#define PI 3.14159265359f

// Basic functions
template<typename T>
T math_maximum(T a, T b) {
    return a > b ? a : b;
}

template<typename T>
T math_minimum(T a, T b) {
    return a < b ? a : b;
}

template<typename T>
T math_absolute(T a) {
    return math_maximum(-a, a);
}

template<typename T>
T math_clamp(T x, T minimum, T maximum) {
    return math_minimum(math_maximum(x, minimum), maximum);
}

template<typename T>
T math_interpolate_linear(T t1, T t2, float a) {
    return t1 * (1.0f-a) + a * t2;
}

// Rounding functions
float math_floor(float x);
double math_floor(double x);
float math_ceil(float x);
double math_ceil(double x);
u64 math_round_previous_multiple(u64 x, u64 modulo);
u64 math_round_next_multiple(u64 x, u64 m);
i32 math_round_next_multiple(i32 x, i32 m);

// MODULO FUNCTIONS
// These are used because the c++ % operator is actually the Mathematical remainder operator, not the Mathematical modulo
int math_modulo(int x, int modulo);
double math_modulo(double x, double modulo);
float math_modulo(float x, float modulo);
float math_remainder(float x, float modulo);

// Modulo in an interval
float math_modulo_in_interval(float x, float min, float max);

// TRIGONOMETRIC FUNCTIONS
float math_cosine(float x);
float math_sine(float x);
float math_tangent(float x);
float math_arcsine(float x);
float math_arccosine(float x);
float math_arctangent(float x);
float math_arctangent_2(float x, float y);

// Exponential functions
float math_power(float x, float y);
double math_power(double x, double y);

float math_square_root(float x);
double math_square_root(double x);

// Helpers
float math_radians_to_degree(float radians);
float math_degree_to_radians(float degree);
