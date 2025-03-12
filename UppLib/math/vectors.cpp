#include "vectors.hpp"

// ---------------
// --- VECTORS ---
// ---------------
// Up to 4 dimensional vectors and typical operations
// of these vectors are defined here.
// Vectors are immutable

#define NORMALIZE_SAVE_MIN 0.000001f
#include <cmath>
#include "scalars.hpp"
#include "../datastructures/string.hpp"

// VEC 2
vec2 operator-(const vec2& v) {
    return vec2(-v.x, -v.y);
}
vec2 operator+(const vec2& v1, const vec2& v2) {
    return vec2(v1.x+v2.x, v1.y+v2.y);
}
vec2 operator-(const vec2& v1, const vec2& v2) {
    return vec2(v1.x-v2.x, v1.y-v2.y);
}
vec2 operator*(const vec2& v1, const vec2& v2) {
    return vec2(v1.x*v2.x, v1.y*v2.y);
}
vec2 operator/(const vec2& v1, const vec2& v2) {
    return vec2(v1.x/v2.x, v1.y/v2.y);
}

vec2 operator+(const vec2& v, float s) {
    return vec2(v.x+s, v.y+s);
}
vec2 operator-(const vec2& v, float s) {
    return vec2(v.x-s, v.y-s);
}
vec2 operator*(const vec2& v, float s) {
    return vec2(v.x*s, v.y*s);
}
vec2 operator/(const vec2& v, float s) {
    return vec2(v.x/s, v.y/s);
}

vec2 operator+(float s, const vec2& v) {
    return vec2(v.x+s, v.y+s);
}
vec2 operator-(float s, const vec2& v) {
    return vec2(v.x-s, v.y-s);
}
vec2 operator*(float s, const vec2& v) {
    return vec2(v.x*s, v.y*s);
}
vec2 operator/(float s, const vec2& v) {
    return vec2(v.x/s, v.y/s);
}

float vector_length(const vec2& v) {
    return sqrtf(v.x*v.x + v.y*v.y);
}

float vector_length_squared(const vec2& v) {
    return v.x*v.x + v.y*v.y;
}

float vector_distance_between(const vec2& v1, const vec2& v2) {
    return vector_length(v1-v2);
}

float vector_distance_between_squared(const vec2& v1, const vec2& v2) {
    return vector_length_squared(v1-v2);
}

vec2 vector_normalize(const vec2& v) {
    return v / vector_length(v);
}

vec2 vector_normalize_safe(const vec2& v) 
{
    float l = vector_length(v);
    if (l < NORMALIZE_SAVE_MIN) {
        return vec2(1.0f, 0.0f); 
    }
    else {
        return v / l;
    }
}

vec2 vector_normalize_safe(const vec2& v, float minimum_length) 
{
    float l = vector_length(v);
    if (l < minimum_length) {
        return vec2(1.0f, 0.0f); 
    }
    else {
        return v / l;
    }
}

float vector_dot(const vec2& v1, const vec2& v2) {
    return v1.x*v2.x + v1.y*v2.y;
}

float vector_cross(const vec2& v1, const vec2& v2) {
    return v1.x*v2.y - v2.x*v1.y;
}

vec2 vector_rotate_90_degree_clockwise(const vec2& v) {
    return vec2(v.y, -v.x);
}

vec2 vector_rotate_90_degree_counter_clockwise(const vec2& v) {
    return vec2(-v.y, v.x);
}

float vector_get_minimum_axis(const vec2& v) {
    return math_minimum(v.x, v.y);
}

float vector_get_maximum_axis(const vec2& v) {
    return math_maximum(v.x, v.y);
}

// VEC 3
vec3 operator-(const vec3& v) {
    return vec3(-v.x, -v.y, -v.z);
}
// Regular arithmetic operations
vec3 operator+(const vec3& v1, const vec3& v2) {
    return vec3(v1.x+v2.x, v1.y+v2.y, v1.z+v2.z);
}
vec3 operator-(const vec3& v1, const vec3& v2) {
    return vec3(v1.x-v2.x, v1.y-v2.y, v1.z-v2.z);
}
vec3 operator*(const vec3& v1, const vec3& v2) {
    return vec3(v1.x*v2.x, v1.y*v2.y, v1.z*v2.z);
}
vec3 operator/(const vec3& v1, const vec3& v2) {
    return vec3(v1.x/v2.x, v1.y/v2.y, v1.z/v2.z);
}

vec3 operator+(const vec3& v, float s) {
    return vec3(v.x+s, v.y+s, v.z+s);
}
vec3 operator-(const vec3& v, float s) {
    return vec3(v.x-s, v.y-s, v.z-s);
}
vec3 operator*(const vec3& v, float s) {
    return vec3(v.x*s, v.y*s, v.z*s);
}
vec3 operator/(const vec3& v, float s) {
    return vec3(v.x/s, v.y/s, v.z/s);
}

vec3 operator+(float s, const vec3& v) {
    return vec3(v.x+s, v.y+s, v.z+s);
}
vec3 operator-(float s, const vec3& v) {
    return vec3(v.x-s, v.y-s, v.z-s);
}
vec3 operator*(float s, const vec3& v) {
    return vec3(v.x*s, v.y*s, v.z*s);
}
vec3 operator/(float s, const vec3& v) {
    return vec3(v.x/s, v.y/s, v.z/s);
}

// Special vector operations
float vector_length(const vec3& v) {
    return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}
float vector_length_squared(const vec3& v) {
    return v.x*v.x + v.y*v.y + v.z*v.z;
}
float vector_distance_between(const vec3& v1, const vec3& v2) {
    return vector_length(v1-v2);
}
float vector_distance_between_squared(const vec3& v1, const vec3& v2) {
    return vector_length_squared(v1-v2);
}
vec3 vector_normalize(const vec3& v) {
    return v / vector_length(v);
}
vec3 vector_normalize_safe(const vec3& v) {
    float l = vector_length(v);
    if (l < NORMALIZE_SAVE_MIN) {
        return vec3(1.0f, 0.0f, 0.0f); // TODO Check if this should be null vector
    }
    else {
        return v / l;
    }
}
vec3 vector_normalize_safe(const vec3& v, float minimum_length) {
    float l = vector_length(v);
    if (l < minimum_length) {
        return vec3(1.0f, 0.0f, 0.0f); // TODO Check if this should be null vector
    }
    else {
        return v / l;
    }
}
float vector_dot(const vec3& v1, const vec3& v2) {
    return v1.x*v2.x + v1.y*v2.y + v1.z*v2.z;
}
vec3 vector_cross(const vec3& v1, const vec3& v2) {
    return vec3(v1.y*v2.z - v1.z*v2.y, 
            v1.z*v2.x - v1.x*v2.z,
            v1.x*v2.y - v1.y*v2.x);
}
vec3 vector_homogenize(const vec3& v) {
    return vec3(v.x/v.z, v.y/v.z, 1.0f);
}
float vector_get_minimum_axis(const vec3& v) {
    return math_minimum(v.x, math_minimum(v.y, v.z));
}
float vector_get_maximum_axis(const vec3& v) {
    return math_maximum(v.x, math_minimum(v.y, v.z));
}


// VEC 4
vec4 operator-(const vec4& v) {
    return vec4(-v.x, -v.y, -v.z, -v.w);
}
// Regular arithmetic operations
vec4 operator+(const vec4& v1, const vec4& v2) {
    return vec4(v1.x+v2.x, v1.y+v2.y, v1.z+v2.z, v1.w+v2.w);
}
vec4 operator-(const vec4& v1, const vec4& v2) {
    return vec4(v1.x-v2.x, v1.y-v2.y, v1.z-v2.z, v1.w-v2.w);
}
vec4 operator*(const vec4& v1, const vec4& v2) {
    return vec4(v1.x*v2.x, v1.y*v2.y, v1.z*v2.z, v1.w*v2.w);
}
vec4 operator/(const vec4& v1, const vec4& v2) {
    return vec4(v1.x/v2.x, v1.y/v2.y, v1.z/v2.z, v1.w/v2.w);
}

vec4 operator+(const vec4& v, float s) {
    return vec4(v.x+s, v.y+s, v.z+s, v.w+s);
}
vec4 operator-(const vec4& v, float s) {
    return vec4(v.x-s, v.y-s, v.z-s, v.w-s);
}
vec4 operator*(const vec4& v, float s) {
    return vec4(v.x*s, v.y*s, v.z*s, v.w*s);
}
vec4 operator/(const vec4& v, float s) {
    return vec4(v.x/s, v.y/s, v.z/s, v.w/s);
}

vec4 operator+(float s, const vec4& v) {
    return vec4(v.x+s, v.y+s, v.z+s, v.w+s);
}
vec4 operator-(float s, const vec4& v) {
    return vec4(v.x-s, v.y-s, v.z-s, v.w-s);
}
vec4 operator*(float s, const vec4& v) {
    return vec4(v.x*s, v.y*s, v.z*s, v.w*s);
}
vec4 operator/(float s, const vec4& v) {
    return vec4(v.x/s, v.y/s, v.z/s, v.w/s);
}

// Special vector operations
float vector_length(const vec4& v) {
    return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z + v.w*v.w);
}

float vector_length_squared(const vec4& v) {
    return v.x*v.x + v.y*v.y + v.z*v.z + v.w*v.w;
}

float vector_distance_between(const vec4& v1, const vec4& v2) {
    return vector_length(v1-v2);
}

float vector_distance_between_squared(const vec4& v1, const vec4& v2) {
    return vector_length_squared(v1-v2);
}

vec4 vector_normalize(const vec4& v) {
    return v / vector_length(v);
}

vec4 vector_normalize_safe(const vec4& v) {
    float l = vector_length(v);
    if (l < NORMALIZE_SAVE_MIN) {
        return vec4(1.0f, 0.0f, 0.0f, 0.0f); // TODO Consider if this should be the nullvector
    }
    else {
        return v / l;
    }
}

vec4 vector_normalize_safe(const vec4& v, float minimum_length) {
    float l = vector_length(v);
    if (l < minimum_length) {
        return vec4(1.0f, 0.0f, 0.0f, 0.0f); // TODO Consider if this should be the nullvector
    }
    else {
        return v / l;
    }
}

float vector_dot(const vec4& v1, const vec4& v2) {
    return v1.x*v2.x + v1.y*v2.y + v1.z*v2.z + v1.w*v2.w; 
}

vec4 vector_homogenize(const vec4& v) {
    return vec4(v.x/v.w, v.y/v.w, v.z/v.w, 1.0f);
}

float vector_get_minimum_axis(const vec4& v) {
    return math_minimum(v.x, math_minimum(v.y, math_minimum(v.y, v.w)));
}

float vector_get_maximum_axis(const vec4& v) {
    return math_maximum(v.x, math_maximum(v.y, math_maximum(v.y, v.w)));
}


vec4 vec4_color_from_rgb(u8 r, u8 g, u8 b) {
	return vec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

vec4 vec4_color_from_code(const char* c_str)
{
	String str = string_create_static(c_str);
	auto get_hex_digit_value = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return 10 + c - 'a';
		if (c >= 'A' && c <= 'F') return 10 + c - 'A';
		return -1;
		};

	// Check that hex-code format is correct
	vec4 error = vec4(0, 0, 0, 1);
	if (str.size != 7) return error;
	if (str[0] != '#') return error;
	for (int i = 1; i < str.size; i++) {
		if (get_hex_digit_value(str[i]) == -1) return error;
	}

	u8 r = get_hex_digit_value(str[1]) * 16 + get_hex_digit_value(str[2]);
	u8 g = get_hex_digit_value(str[3]) * 16 + get_hex_digit_value(str[4]);
	u8 b = get_hex_digit_value(str[5]) * 16 + get_hex_digit_value(str[6]);

	return vec4_color_from_rgb(r, g, b);
}

vec3 vec3_color_from_code(const char* c_str)
{
    vec4 v = vec4_color_from_code(c_str);
    return vec3(v.x, v.y, v.z);
}

