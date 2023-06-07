#pragma once

#include "cameras.hpp"
#include "../math/matrices.hpp"

struct Camera_Controller_Arcball
{
    vec2 spherical_coordinates;
    float sensitivity_rotation;
    float sensitivity_zoom;
    float zoom_level;
    float base_distance_to_center;
    vec3 center;
};

struct Input;
Camera_Controller_Arcball camera_controller_arcball_make(vec3 center, float base_distance_to_center);
void camera_controller_arcball_update(Camera_Controller_Arcball* controller, Camera_3D* camera, Input* input, int backbuffer_width, int backbuffer_height);

struct Camera_Controller_Flying
{
    vec2 spherical_coordinates;
    float sensitivity_rotation;
    float speed;
    float speed_boost;
};

Camera_Controller_Flying camera_controller_flying_make(float sensitivity, float speed, float speed_boost);
void camera_controller_flying_update(Camera_Controller_Flying* controller, Camera_3D* camera, Input* input, float time_delta);