#include "camera_controllers.hpp"

#include "../win32/input.hpp"
#include "../math/scalars.hpp"
#include "../math/spherical.hpp"
#include "../utility/utils.hpp"

Camera_Controller_Arcball camera_controller_arcball_make(vec3 center, float base_distance_to_center)
{
    Camera_Controller_Arcball result;
    result.base_distance_to_center = base_distance_to_center;
    result.center = center;
    result.spherical_coordinates = vec2(0);
    result.sensitivity_rotation = 1.0f;
    result.sensitivity_zoom = 1.0f;
    result.zoom_level = 0.0f;
    return result;
}

void camera_controller_arcball_update(Camera_Controller_Arcball* controller, Camera_3D* camera, Input* input, int window_width, int window_height)
{
    // Update sphereCoords
    if (input->mouse_down[(int)Mouse_Key_Code::LEFT]) {
        controller->spherical_coordinates -= 
            vec2(input->mouse_normalized_delta_x, input->mouse_normalized_delta_y) * 
            PI * 2.0f * controller->sensitivity_rotation;
    }
    controller->spherical_coordinates = math_normalize_spherical(controller->spherical_coordinates);
    vec3 view_direction = math_coordinates_spherical_to_euclidean(controller->spherical_coordinates);

    // Update distance
    controller->zoom_level -= input->mouse_wheel_delta * controller->sensitivity_zoom;
    float distance_to_center = controller->base_distance_to_center * math_power(1.3f, controller->zoom_level);

    // Update position correctly
    if (input->mouse_down[(int)Mouse_Key_Code::RIGHT])
    {
        mat4 view = mat4_make_view_matrix_look_in_direction(vec3(0), camera->view_direction, vec3(0, 1, 0));
        view = matrix_transpose(view);

        vec3 moveDir = vec3(-(float)input->mouse_delta_x, (float)input->mouse_delta_y, 0);
        moveDir.x /= (float)window_width * 0.5f;
        moveDir.y /= (float)window_height;
        float speed = 1.0f;
        moveDir = view * moveDir * distance_to_center;

        controller->center += moveDir * speed;
    }

    // Update camera
    camera_3D_update_view(camera, controller->center - view_direction * distance_to_center, view_direction);
}




Camera_Controller_Flying camera_controller_flying_make(float sensitivity, float speed, float speed_boost)
{
    Camera_Controller_Flying result;
    result.spherical_coordinates = vec2(0);
    result.sensitivity_rotation = sensitivity;
    result.speed = speed;
    result.speed_boost = speed_boost;
    return result;
}

void camera_controller_flying_update(Camera_Controller_Flying* controller, Camera_3D* camera, Input* input, float time_delta)
{
    // Update sphereCoords
    controller->spherical_coordinates -= vec2(input->mouse_normalized_delta_x, input->mouse_normalized_delta_y) * 
        controller->sensitivity_rotation * PI;
    controller->spherical_coordinates = math_normalize_spherical(controller->spherical_coordinates);
    camera->view_direction = math_coordinates_spherical_to_euclidean(controller->spherical_coordinates);

    // Update position correctly
    mat4 view = mat4_make_view_matrix_look_in_direction(vec3(0), camera->view_direction, vec3(0, 1, 0));
    view = matrix_transpose(view);
    vec3 movement_direction = vec3(0);
    if (input->key_down[(int)Key_Code::W]) {
        movement_direction += vec3(0, 0, -1);
    }
    if (input->key_down[(int)Key_Code::A]) {
        movement_direction += vec3(-1, 0, 0);
    }
    if (input->key_down[(int)Key_Code::S]) {
        movement_direction += vec3(0, 0, 1);
    }
    if (input->key_down[(int)Key_Code::D]) {
        movement_direction += vec3(1, 0, 0);
    }
    if (input->key_down[(int)Key_Code::SPACE]) {
        movement_direction += vec3(0, 1, 0);
    }
    if (input->key_down[(int)Key_Code::CTRL]) {
        movement_direction += vec3(0, -1, 0);
    }
    movement_direction *= controller->speed;
    if (input->key_down[(int)Key_Code::SHIFT]) {
        movement_direction *= controller->speed_boost;
    }
    movement_direction = view * movement_direction;
    camera->position += movement_direction * time_delta;

    // Update camera
    camera_3D_update_view(camera, camera->position, camera->view_direction);
}