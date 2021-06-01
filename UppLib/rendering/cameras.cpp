#include "cameras.hpp"

#include "rendering_core.hpp"

void camera_3D_update_projection_window_size(void* userdata, Rendering_Core* core) 
{
    Camera_3D* camera = (Camera_3D*)userdata;
    camera->aspect_ratio = ((float)core->render_information.window_width / core->render_information.viewport_height);
    camera->projection_matrix = mat4_make_projection_matrix(camera->near_distance, camera->far_distance, camera->fov_x, camera->aspect_ratio);
}

Camera_3D* camera_3D_create(Rendering_Core* core, float fov_x, float near_distance, float far_distance)
{
    Camera_3D* result = new Camera_3D();
    result->fov_x = fov_x;
    result->fov_y = fov_x * ((float)core->render_information.window_width / core->render_information.viewport_height);
    result->near_distance = near_distance;
    result->far_distance = far_distance;
    result->position = vec3(0);
    result->view_direction = vec3(0, 0, -1);
    result->up = vec3(0, 1, 0);
    camera_3D_update_projection_window_size(result, core);
    rendering_core_add_window_size_listener(core, &camera_3D_update_projection_window_size, result);

    camera_3D_update_matrices(result);
    return result;
}

void camera_3D_destroy(Camera_3D* camera, Rendering_Core* core)
{
    rendering_core_remove_window_size_listener(core, camera);
    delete camera;
}

void camera_3D_update_matrices(Camera_3D* camera) 
{
    camera->view_matrix = mat4_make_view_matrix_look_in_direction(camera->position, camera->view_direction);
    camera->view_projection_matrix = camera->projection_matrix * camera->view_matrix;
}

void camera_3D_update_field_of_view(Camera_3D* camera, float fov_x) 
{
    camera->fov_x = fov_x;
    camera->fov_y = fov_x * camera->aspect_ratio;
    camera_3D_update_matrices(camera);
}

void camera_3D_update_view_with_up_vector(Camera_3D* camera, const vec3& position, const vec3& view_direction, const vec3& up)
{
    camera->position = position;
    camera->view_direction = view_direction;
    camera->up = up;
    camera_3D_update_matrices(camera);
}

void camera_3D_update_view(Camera_3D* camera, const vec3& position, const vec3& view_direction)
{
    camera->position = position;
    camera->view_direction = view_direction;
    camera_3D_update_matrices(camera);
}

Camera_3D_UBO_Data camera_3d_ubo_data_make(Camera_3D* camera)
{
    Camera_3D_UBO_Data data;
    data.camera_direction = vec4(camera->view_direction, 1.0f);
    data.camera_position = vec4(camera->position, 1.0f);
    data.camera_up = vec4(camera->up, 1.0f);
    data.near_distance = camera->near_distance;
    data.far_distance = camera->far_distance;
    data.field_of_view_x = camera->fov_x;
    data.field_of_view_y = camera->fov_y;
    data.view = camera->view_matrix;
    data.projection = camera->projection_matrix;
    data.view_projection = camera->view_projection_matrix;
    data.inverse_view = matrix_transpose(camera->view_matrix);
    return data;
}

