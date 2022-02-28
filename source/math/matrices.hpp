#pragma once

#include "vectors.hpp"

// -------------------------------
// ----- MATRIX CALCULATIONS -----
// -------------------------------
// Structs and functions of symetrical matricies
// up to 4x4 are contained in this file.

struct mat2
{
    mat2() {}
    explicit mat2(float s) { // Diagonal matrix with entries s
        columns[0] = vec2(s, 0.0f);
        columns[1] = vec2(0.0f, s);
    } 
    mat2(const vec2& v1, const vec2& v2) { // Diagonal matrix with entries s
        columns[0] = v1;
        columns[1] = v2;
    } 

    vec2 columns[2];
};

vec2 operator*(const mat2& m, const vec2& v);
mat2 operator*(const mat2& m1, const mat2& m2);
mat2 matrix_transpose(const mat2& m);
mat2 mat2_make_rotation_matrix(float angle);
mat2 mat2_make_scale_matrix(const vec2& s);

// Mat 3
struct mat3
{
    mat3(){}
    explicit mat3(float s) {
        columns[0] = vec3(s, 0.0f, 0.0f);
        columns[1] = vec3(0.0f, s, 0.0f);
        columns[2] = vec3(0.0f, 0.0f, s);
    }
    mat3(const vec3& v1, const vec3& v2, const vec3& v3) {
        columns[0] = v1;
        columns[1] = v2;
        columns[2] = v3;
    }
    mat3(const mat2& m) {
        columns[0] = vec3(m.columns[0], 0.0f);
        columns[1] = vec3(m.columns[1], 0.0f);
        columns[2] = vec3(0.0f, 0.0f, 1.0f);
    }
    mat3(float* data) {
        columns[0] = vec3(data[0], data[1], data[2]);
        columns[1] = vec3(data[3], data[4], data[5]);
        columns[2] = vec3(data[6], data[7], data[8]);
    }

    vec3 columns[3];
};

mat3 operator*(const mat3& m1, const mat3& m2);
mat3 matrix_transpose(const mat3& m);
mat3 mat3_make_rotation_matrix_from_angles(float yaw, float pitch, float roll);
mat3 mat3_make_scaling_matrix(const vec3& s);
mat3 mat3_make_translation_matrix(const vec2& t);
mat3 mat3_make_rotation_matrix_around_x(float angle);
mat3 mat3_make_rotation_matrix_around_y(float angle);
mat3 mat3_make_rotation_matrix_around_z(float angle);

// MATRIX 4x4
struct mat4
{
    mat4() {}
    explicit mat4(float s) {
        columns[0] = vec4(s, 0.0f, 0.0f, 0.0f);
        columns[1] = vec4(0.0f, s, 0.0f, 0.0f);
        columns[2] = vec4(0.0f, 0.0f, s, 0.0f);
        columns[3] = vec4(0.0f, 0.0f, 0.0f, s);
    }
    mat4(const vec4& v1, const vec4& v2, const vec4& v3, const vec4& v4) {
        columns[0] = v1;
        columns[1] = v2;
        columns[2] = v3;
        columns[3] = v4;
    }
    mat4(const mat3& m) {
        columns[0] = vec4(m.columns[0], 0.0f);
        columns[1] = vec4(m.columns[1], 0.0f);
        columns[2] = vec4(m.columns[2], 0.0f);
        columns[3] = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    float* getDataPtr() {
        return (float*)columns;
    }

    vec4 columns[4];
};

vec4 operator*(const mat4& m, const vec4& v);
vec3 operator*(const mat4& m, const vec3& v);
mat4 operator*(const mat4& m1, const mat4& m2);
mat4 matrix_transpose(const mat4& m);
mat4 mat4_make_translation_matrix(const vec3& t);

mat4 mat4_make_projection_matrix(float near_plane_distance, float far_plane_distance, float fovX, float aspectRatio);
mat4 mat4_make_view_matrix_look_in_direction(const vec3& pos, const vec3& dir, const vec3& up);
mat4 mat4_make_view_matrix_look_in_direction(const vec3& pos, const vec3& dir);
mat4 mat4_make_view_matrix_look_at_position(const vec3& pos, const vec3& at, const vec3& up);
mat4 mat4_make_view_matrix_look_at_position(const vec3& pos, const vec3& at);
mat4 mat4_make_scaling_matrix(const vec3& s);


