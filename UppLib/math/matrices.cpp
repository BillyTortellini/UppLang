#include "matrices.hpp"

#include "scalars.hpp"

// MAT2
vec2 operator*(const mat2& m, const vec2& v) {
    return m.columns[0]*v.x + m.columns[1]*v.y;
}

mat2 operator*(const mat2& m1, const mat2& m2) {
    return mat2(m1*m2.columns[0], m1*m2.columns[1]);    
}

mat2 matrix_transpose(const mat2& m) {
    return mat2(vec2(m.columns[0].x, m.columns[1].x),
            vec2(m.columns[0].y, m.columns[1].y));
}

mat2 mat2_make_rotation_matrix(float angle) {
    float c = math_cosine(angle);
    float s = math_sine(angle);
    return mat2(vec2(c, s), vec2(-s, c));
}

mat2 mat2_make_scale_matrix(const vec2& s) {
    return mat2(vec2(s.x, 0.0f), vec2(0.0f, s.y));
}

vec3 operator*(const mat3& m, const vec3& v) {
    return m.columns[0]*v.x + m.columns[1]*v.y + m.columns[2]*v.z;
}

vec2 operator*(const mat3& m, const vec2& v) {
    vec3 r = m * vec3(v, 1.0f);
    return vec2(r.x, r.y);
}


// MAT3
mat3 operator*(const mat3& m1, const mat3& m2) {
    return mat3(m1*m2.columns[0], m1*m2.columns[1], m1*m2.columns[2]);    
}

mat3 mat3_make_rotation_matrix_around_x(float angle) {
    mat3 result;
    result.columns[0] = vec3(1.0f, 0.0f, 0.0f);
    result.columns[1] = vec3(0.0f, math_cosine(angle), math_sine(angle));
    result.columns[2] = vec3(0.0f, -math_sine(angle), math_cosine(angle));
    return result;
}

mat3 mat3_make_rotation_matrix_around_y(float angle) {
    mat3 result;
    result.columns[0] = vec3(math_cosine(angle), 0.0f, math_sine(angle));
    result.columns[1] = vec3(0.0f, 1.0f, 0.0f);
    result.columns[2] = vec3(-math_sine(angle), 0.0f,  math_cosine(angle));
    return result;
}

mat3 mat3_make_rotation_matrix_around_z(float angle) {
    mat3 result;
    result.columns[0] = vec3(math_cosine(angle), -math_sine(angle), 0.0f);
    result.columns[1] = vec3(math_sine(angle), math_cosine(angle), 0.0f);
    result.columns[2] = vec3(0.0f, 0.0f, 1.0f);
    return result;
}

mat3 mat3_make_rotation_matrix_from_angles(float yaw, float pitch, float roll) {
    return 
        mat3_make_rotation_matrix_around_y(yaw) *
        mat3_make_rotation_matrix_around_x(pitch) *
        mat3_make_rotation_matrix_around_z(roll);
}

mat3 mat3_make_scaling_matrix(const vec3& s) {
    return mat3(vec3(s.x, 0.0f, 0.0f),
            vec3(0.0f, s.y, 0.0f),
            vec3(0.0f, 0.0f, s.z));
}

mat3 mat3_make_translation_matrix(const vec2& t) {
    return mat3(vec3(1.0f, 0.0f, 0.0f),
            vec3(0.0f, 1.0f, 0.0f),
            vec3(t.x, t.y, 1.0f));
}

mat3 matrix_transpose(const mat3& m) {
    return mat3(vec3(m.columns[0].x, m.columns[1].x, m.columns[2].x),
            vec3(m.columns[0].y, m.columns[1].y, m.columns[2].y),
            vec3(m.columns[0].z, m.columns[1].z, m.columns[2].z));
}



// MAT4
vec4 operator*(const mat4& m, const vec4& v) {
    return m.columns[0]*v.x + m.columns[1]*v.y + m.columns[2]*v.z + m.columns[3]*v.w;
}

vec3 operator*(const mat4& m, const vec3& v) {
    vec4 r = m * vec4(v, 1.0f);
    return vec3(r.x, r.y, r.z);
}

mat4 operator*(const mat4& m1, const mat4& m2) {
    return mat4(m1*m2.columns[0], m1*m2.columns[1], m1*m2.columns[2], m1*m2.columns[3]);    
}

mat4 mat4_make_translation_matrix(const vec3& t) {
    return mat4(vec4(1.0f, 0.0f, 0.0f, 0.0f),
            vec4(0.0f, 1.0f, 0.0f, 0.0f),
            vec4(0.0f, 0.0f, 1.0f, 0.0f),
            vec4(t.x, t.y, t.z, 1.0f));
}

mat4 matrix_transpose(const mat4& m) {
    return mat4(vec4(m.columns[0].x, m.columns[1].x, m.columns[2].x, m.columns[3].x),
            vec4(m.columns[0].y, m.columns[1].y, m.columns[2].y, m.columns[3].y),
            vec4(m.columns[0].z, m.columns[1].z, m.columns[2].z, m.columns[3].z),
            vec4(m.columns[0].w, m.columns[1].w, m.columns[2].w, m.columns[3].w));
}

mat4 mat4_make_projection_matrix(float near_plane_distance, float far_plane_distance, float fovX, float aspectRatio) 
{
    mat4 projection;

    float fovY;
    if (aspectRatio > 1.0f) {
        fovY = fovX / aspectRatio;
    }
    else {
        fovY = fovX;
        fovX = fovX * aspectRatio;
    }
    float sx = 1.0f / math_tangent(fovX/2.0f);
    float sy = 1.0f / math_tangent(fovY/2.0f);
    float& f = far_plane_distance;
    float& n = near_plane_distance;
    projection.columns[0] = vec4(sx, 0.0f, 0.0f, 0.0f);
    projection.columns[1] = vec4(0.0f, sy, 0.0f, 0.0f);
    projection.columns[2] = vec4(0.0f, 0.0f, -(f+n)/(f-n), -1.0f);
    projection.columns[3] = vec4(0.0f, 0.0f, (-2.0f*n*f)/(f-n), 0.0f);

    return projection;
}

mat4 mat4_make_view_matrix_look_in_direction(const vec3& pos, const vec3& dir, const vec3& up) 
{
    mat4 view;

    vec3 d = vector_normalize_safe(-dir);
    vec3 u = vector_normalize_safe(up);
    vec3 r = vector_normalize_safe(vector_cross(u, d));
    u = vector_cross(d, r);

    view = mat4(matrix_transpose(mat3(r, u, d)));
    view = view * mat4_make_translation_matrix(-pos);

    return view;
}

mat4 mat4_make_view_matrix_look_in_direction(const vec3& pos, const vec3& dir) 
{
    return mat4_make_view_matrix_look_in_direction(pos, dir, vec3(0.0f, 1.0f, 0.0f));
}

mat4 mat4_make_view_matrix_look_at_position(const vec3& pos, const vec3& at, const vec3& up) {
    return mat4_make_view_matrix_look_in_direction(pos, at - pos, up);
}

mat4 mat4_make_view_matrix_look_at_position(const vec3& pos, const vec3& at) {
    return mat4_make_view_matrix_look_in_direction(pos, at - pos, vec3(0.0f, 1.0f, 0.0f));
}

