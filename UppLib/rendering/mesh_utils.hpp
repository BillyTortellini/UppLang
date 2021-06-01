#pragma once

#include "gpu_buffers.hpp"
#include "rendering_core.hpp"

Mesh_GPU_Buffer mesh_utils_create_quad_2D(Rendering_Core* core);
Mesh_GPU_Buffer mesh_utils_create_cube(Rendering_Core* core, vec3 color);