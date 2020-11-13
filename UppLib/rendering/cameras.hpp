#pragma once

#include "../math/matrices.hpp"

struct Camera_3D
{
    vec3 position;
    vec3 view_direction;
    vec3 up;
    float fov_x, near_distance, far_distance;
    int width, height;

    mat4 view_matrix;
    mat4 projection_matrix;
    mat4 view_projection_matrix;
};

Camera_3D camera_3d_make(int width, int height, float fov_x, float near_distance, float far_distance);
void camera_3d_update_matrices(Camera_3D* camera);
void camera_3d_update_projection_window_size(Camera_3D* camera, int width, int height);
void camera_3d_update_field_of_view(Camera_3D* camera, float fov_x);
void camera_3d_update_view(Camera_3D* camera, const vec3& position, const vec3& view_direction, const vec3& up);
void camera_3d_update_view(Camera_3D* camera, const vec3& position, const vec3& view_direction);


