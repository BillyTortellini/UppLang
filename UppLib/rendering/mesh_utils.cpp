#include "mesh_utils.hpp"

#include "../math/vectors.hpp"

struct Basic_Vertex_Data_2D
{
    vec2 position;
};

struct Basic_Vertex_Data_3D
{
    vec3 position;
    vec3 color;
};

Mesh_GPU_Buffer mesh_utils_create_quad_2D()
{
    vec2 positions[] = {
        vec2(-1.0f, -1.0f),
        vec2(1.0f, -1.0f),
        vec2(1.0f, 1.0f),
        vec2(-1.0f, 1.0f),
    };
    u32 indices[] = {
        0, 1, 2,
        0, 2, 3,
    };
    REMOVE_ME a;
    REMOVE_ME attrib_infos[] = {a};
    Mesh_GPU_Buffer result = mesh_gpu_buffer_create_with_single_vertex_buffer(
        gpu_buffer_create(
            array_as_bytes(&array_create_static(positions, 4)),
            GPU_Buffer_Type::VERTEX_BUFFER,
            GPU_Buffer_Usage::STATIC
        ),
        array_create_static(attrib_infos, 1),
        gpu_buffer_create(
            array_as_bytes(&array_create_static(indices, 6)),
            GPU_Buffer_Type::INDEX_BUFFER,
            GPU_Buffer_Usage::STATIC
        ),
        Mesh_Topology::TRIANGLES,
        6
    );
    return result;
}

Mesh_GPU_Buffer mesh_utils_create_cube(vec3 color)
{
    vec3 positions[] = {
        vec3(-1.0f, -1.0f, 1.0f),  color,
        vec3(1.0f, -1.0f, 1.0f),   color,
        vec3(1.0f, 1.0f, 1.0f),    color,
        vec3(-1.0f, 1.0f, 1.0f),   color,
        vec3(-1.0f, -1.0f, -1.0f), color,
        vec3(1.0f, -1.0f, -1.0f),  color,
        vec3(1.0f, 1.0f, -1.0f),   color,
        vec3(-1.0f, 1.0f, -1.0f),  color
    };
    u32 indices[] = {
        0, 1, 2, 0, 2, 3,
        1, 5, 6, 1, 6, 2,
        3, 2, 6, 3, 6, 7,
        0, 5, 1, 0, 4, 5,
        0, 3, 7, 0, 7, 4,
        4, 6, 5, 4, 7, 6
    };
    REMOVE_ME a;
    REMOVE_ME attrib_infos[] = {a};
    Mesh_GPU_Buffer result = mesh_gpu_buffer_create_with_single_vertex_buffer(
        gpu_buffer_create(
            array_as_bytes(&array_create_static(positions, 16)),
            GPU_Buffer_Type::VERTEX_BUFFER,
            GPU_Buffer_Usage::STATIC
        ),
        array_create_static(attrib_infos, 2),
        gpu_buffer_create(
            array_as_bytes(&array_create_static(indices, 36)),
            GPU_Buffer_Type::INDEX_BUFFER,
            GPU_Buffer_Usage::STATIC
        ),
        Mesh_Topology::TRIANGLES,
        36
    );
    return result;

}
