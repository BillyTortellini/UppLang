#pragma once

// ---------------
// --- VECTORS ---
// ---------------
// Up to 4 dimensional vectors and typical operations
// of these vectors are defined here.
// Vectors are mutable

#include "../utility/datatypes.hpp"
#define NORMALIZE_SAFE_MIN 0.000001f

// VEC 2
struct vec2
{
    vec2(){}
    explicit vec2(float s) : x(s), y(s) {}
    explicit vec2(float x, float y) : x(x), y(y) {}
    explicit vec2(int s) : vec2((float)s, (float)s){};
    explicit vec2(int x, int y) : vec2((float)x, (float)y){}

    float x, y;

    vec2& operator+=(const vec2& v) {
        this->x += v.x;
        this->y += v.y;
        return *this;
    }
    vec2& operator-=(const vec2& v) {
        this->x -= v.x;
        this->y -= v.y;
        return *this;
    }
    vec2& operator*=(const vec2& v) {
        this->x *= v.x;
        this->y *= v.y;
        return *this;
    }
    vec2& operator/=(const vec2& v) {
        this->x /= v.x;
        this->y /= v.y;
        return *this;
    }
    vec2& operator+=(float s) {
        this->x += s;
        this->y += s;
        return *this;
    }
    vec2& operator-=(float s) {
        this->x -= s;
        this->y -= s;
        return *this;
    }
    vec2& operator*=(float s) {
        this->x *= s;
        this->y *= s;
        return *this;
    }
    vec2& operator/=(float s) {
        this->x /= s;
        this->y /= s;
        return *this;
    }
};

vec2 operator-(const vec2& v);
vec2 operator+(const vec2& v1, const vec2& v2);
vec2 operator-(const vec2& v1, const vec2& v2);
vec2 operator*(const vec2& v1, const vec2& v2);
vec2 operator/(const vec2& v1, const vec2& v2);

vec2 operator+(const vec2& v, float s); 
vec2 operator-(const vec2& v, float s); 
vec2 operator*(const vec2& v, float s); 
vec2 operator/(const vec2& v, float s); 

vec2 operator+(float s, const vec2& v); 
vec2 operator-(float s, const vec2& v); 
vec2 operator*(float s, const vec2& v); 
vec2 operator/(float s, const vec2& v); 

float vector_length(const vec2& v); 
float vector_length_squared(const vec2& v); 
float vector_distance_between(const vec2& v1, const vec2& v2); 
float vector_distance_between_squared(const vec2& v1, const vec2& v2); 
vec2 vector_normalize(const vec2& v); 
vec2 vector_normalize_safe(const vec2& v); 
vec2 vector_normalize_safe(const vec2& v, float minimum_length); 
float vector_dot(const vec2& v1, const vec2& v2); 
float vector_cross(const vec2& v1, const vec2& v2); 
vec2 vector_rotate_90_degree_clockwise(const vec2& v); 
vec2 vector_rotate_90_degree_counter_clockwise(const vec2& v); 
float vector_get_minimum_axis(const vec2& v);
float vector_get_maximum_axis(const vec2& v);


// VEC 3
struct vec3
{
    vec3(){};
    explicit vec3(float s) : x(s), y(s), z(s) {}
    explicit vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    explicit vec3(const vec2& v, float s) : x(v.x), y(v.y), z(s) {}
    explicit vec3(float s, const vec2& v) : x(s), y(v.x), z(v.y) {}

    float x,y,z;

    // Shorcut += -= *= /= functions
    vec3& operator+=(const vec3& v) {
        this->x += v.x;
        this->y += v.y;
        this->z += v.z;
        return *this;
    }
    vec3& operator-=(const vec3& v) {
        this->x -= v.x;
        this->y -= v.y;
        this->z -= v.z;
        return *this;
    }
    vec3& operator*=(const vec3& v) {
        this->x *= v.x;
        this->y *= v.y;
        this->z *= v.z;
        return *this;
    }
    vec3& operator/=(const vec3& v) {
        this->x /= v.x;
        this->y /= v.y;
        this->z /= v.z;
        return *this;
    }
    vec3& operator+=(float s) {
        this->x += s;
        this->y += s;
        this->z += s;
        return *this;
    }
    vec3& operator-=(float s) {
        this->x -= s;
        this->y -= s;
        this->z -= s;
        return *this;
    }
    vec3& operator*=(float s) {
        this->x *= s;
        this->y *= s;
        this->z *= s;
        return *this;
    }
    vec3& operator/=(float s) {
        this->x /= s;
        this->y /= s;
        this->z /= s;
        return *this;
    }
};

vec3 operator-(const vec3& v); 
// Regular arithmetic operations
vec3 operator+(const vec3& v1, const vec3& v2); 
vec3 operator-(const vec3& v1, const vec3& v2); 
vec3 operator*(const vec3& v1, const vec3& v2); 
vec3 operator/(const vec3& v1, const vec3& v2); 

vec3 operator+(const vec3& v, float s); 
vec3 operator-(const vec3& v, float s); 
vec3 operator*(const vec3& v, float s); 
vec3 operator/(const vec3& v, float s); 

vec3 operator+(float s, const vec3& v); 
vec3 operator-(float s, const vec3& v); 
vec3 operator*(float s, const vec3& v); 
vec3 operator/(float s, const vec3& v); 

// Special vector operations
float vector_length(const vec3& v); 
float vector_length_squared(const vec3& v); 
float vector_distance_between(const vec3& v1, const vec3& v2); 
float vector_distance_between_squared(const vec3& v1, const vec3& v2); 
vec3 vector_normalize(const vec3& v); 
vec3 vector_normalize_safe(const vec3& v); 
vec3 vector_normalize_safe(const vec3& v, float minimum_length); 
float vector_dot(const vec3& v1, const vec3& v2); 
vec3 vector_cross(const vec3& v1, const vec3& v2); 
vec3 vector_homogenize(const vec3& v); 
float vector_get_minimum_axis(const vec3& v);
float vector_get_maximum_axis(const vec3& v);

// VEC 4
struct vec4
{
    vec4(){};
    explicit vec4(float s) : x(s), y(s), z(s), w(s) {}
    explicit vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    explicit vec4(const vec2& v, float z, float w) : x(v.x), y(v.y), z(z), w(w) {}
    explicit vec4(float x, const vec2& v, float w) : x(x), y(v.x), z(v.y), w(w) {}
    explicit vec4(float x, float y, const vec2& v) : x(x), y(y), z(v.x), w(v.y) {}
    explicit vec4(const vec2& v1, const vec2& v2) : x(v1.x), y(v1.y), z(v2.x), w(v2.y) {}
    explicit vec4(const vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}
    explicit vec4(float x, const vec3& v) : x(x), y(v.x), z(v.y), w(v.z) {}

    float x,y,z,w;

    // Shorcut += -= *= /= functions
    vec4& operator+=(const vec4& v) {
        this->x += v.x;
        this->y += v.y;
        this->z += v.z;
        this->w += v.w;
        return *this;
    }
    vec4& operator-=(const vec4& v) {
        this->x -= v.x;
        this->y -= v.y;
        this->z -= v.z;
        this->w -= v.w;
        return *this;
    }
    vec4& operator*=(const vec4& v) {
        this->x *= v.x;
        this->y *= v.y;
        this->z *= v.z;
        this->w *= v.w;
        return *this;
    }
    vec4& operator/=(const vec4& v) {
        this->x /= v.x;
        this->y /= v.y;
        this->z /= v.z;
        this->w /= v.w;
        return *this;
    }
    vec4& operator+=(float s) {
        this->x += s;
        this->y += s;
        this->z += s;
        this->w += s;
        return *this;
    }
    vec4& operator-=(float s) {
        this->x -= s;
        this->y -= s;
        this->z -= s;
        this->w -= s;
        return *this;
    }
    vec4& operator*=(float s) {
        this->x *= s;
        this->y *= s;
        this->z *= s;
        this->w *= s;
        return *this;
    }
    vec4& operator/=(float s) {
        this->x /= s;
        this->y /= s;
        this->z /= s;
        this->w /= s;
        return *this;
    }
};

vec4 operator-(const vec4& v); 
// Regular arithmetic operations
vec4 operator+(const vec4& v1, const vec4& v2); 
vec4 operator-(const vec4& v1, const vec4& v2); 
vec4 operator*(const vec4& v1, const vec4& v2); 
vec4 operator/(const vec4& v1, const vec4& v2); 

vec4 operator+(const vec4& v, float s); 
vec4 operator-(const vec4& v, float s); 
vec4 operator*(const vec4& v, float s); 
vec4 operator/(const vec4& v, float s); 

vec4 operator+(float s, const vec4& v); 
vec4 operator-(float s, const vec4& v); 
vec4 operator*(float s, const vec4& v); 
vec4 operator/(float s, const vec4& v); 

// Special vector operations
float vector_length(const vec4& v); 
float vector_length_squared(const vec4& v); 
float vector_distance_between(const vec4& v1, const vec4& v2); 
float vector_distance_between_squared(const vec4& v1, const vec4& v2); 
vec4 vector_normalize(const vec4& v); 
vec4 vector_normalize_safe(const vec4& v); 
vec4 vector_normalize_safe(const vec4& v, float minimum_length); 
float vector_dot(const vec4& v1, const vec4& v2); 
vec4 vector_homogenize(const vec4& v); 
float vector_get_minimum_axis(const vec4& v);
float vector_get_maximum_axis(const vec4& v);

vec4 vec4_color_from_rgb(u8 r, u8 g, u8 b);
vec4 vec4_color_from_code(const char* c_str);
