#include "cameras.hpp"

Camera_3D camera_3d_make(int width, int height, float fov_x, float near, float far) 
{
    Camera_3D result;
    result.fov_x = fov_x;
    result.near_distance = near;
    result.far_distance = far;
    result.width = width;
    result.height = height;
    result.position = vec3(0);
    result.view_direction = vec3(0, 0, -1);
    result.up = vec3(0, 1, 0);

    camera_3d_update_matrices(&result);
    return result;
}

void camera_3d_update_matrices(Camera_3D* camera) 
{
    float aspect_ratio = (float)camera->width/camera->height;
    camera->view_matrix = mat4_make_view_matrix_look_in_direction(camera->position, camera->view_direction);
    camera->projection_matrix = mat4_make_projection_matrix(camera->near_distance, camera->far_distance, camera->fov_x, aspect_ratio);
    camera->view_projection_matrix = camera->projection_matrix * camera->view_matrix;
}

void camera_3d_update_projection_window_size(Camera_3D* camera, int width, int height) 
{
    camera->width = width;
    camera->height = height;
    camera_3d_update_matrices(camera);
}

void camera_3d_update_field_of_view(Camera_3D* camera, float fov_x) 
{
    camera->fov_x = fov_x;
    camera_3d_update_matrices(camera);
}

void camera_3d_update_view(Camera_3D* camera, const vec3& position, const vec3& view_direction, const vec3& up)
{
    camera->position = position;
    camera->view_direction = view_direction;
    camera->up = up;
    camera_3d_update_matrices(camera);
}

void camera_3d_update_view(Camera_3D* camera, const vec3& position, const vec3& view_direction)
{
    camera->position = position;
    camera->view_direction = view_direction;
    camera_3d_update_matrices(camera);
}



