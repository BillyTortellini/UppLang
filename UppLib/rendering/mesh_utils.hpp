#pragma once

#include "mesh_gpu_data.hpp"
#include "opengl_state.hpp"

Mesh_GPU_Data mesh_utils_create_quad_2D(OpenGLState* state);
Mesh_GPU_Data mesh_utils_create_cube(OpenGLState* state, vec3 color);