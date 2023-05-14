#pragma once

#include "../math/matrices.hpp"

struct Rendering_Core;

struct Camera_3D
{
    vec3 position;
    vec3 view_direction;
    vec3 up;
    float fov_x, fov_y, near_distance, far_distance;
    float aspect_ratio;

    mat4 view_matrix;
    mat4 projection_matrix;
    mat4 view_projection_matrix;
};

Camera_3D* camera_3D_create(float fov_x, float near_distance, float far_distance);
void camera_3D_destroy(Camera_3D* camera);
void camera_3D_update_matrices(Camera_3D* camera);
void camera_3D_update_field_of_view(Camera_3D* camera, float fov_x);
void camera_3D_update_view_with_up_vector(Camera_3D* camera, const vec3& position, const vec3& view_direction, const vec3& up);
void camera_3D_update_view(Camera_3D* camera, const vec3& position, const vec3& view_direction);



struct Camera_3D_UBO_Data
{
    mat4 view;
    mat4 inverse_view;
    mat4 projection;
    mat4 view_projection;

    vec4 camera_position; // Packed, w = 1.0
    vec4 camera_direction; // Packed, w = 1.0
    vec4 camera_up;        // Packed w = 1.0
    float near_distance;
    float far_distance;
    float field_of_view_x;
    float field_of_view_y;
};
Camera_3D_UBO_Data camera_3d_ubo_data_make(Camera_3D* camera);

