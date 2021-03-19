#include "mesh_utils.hpp"

#include "../math/vectors.hpp"
#include "mesh_gpu_data.hpp"
#include "opengl_state.hpp"

struct Basic_Vertex_Data_2D
{
    vec2 position;
};

struct Basic_Vertex_Data_3D
{
    vec3 position;
    vec3 color;
};

Mesh_GPU_Data mesh_utils_create_quad_2D(OpenGLState* state)
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
    VertexAttributeInformation attrib_infos[] = { vertex_attribute_information_make(GL_FLOAT, 2, 1, 0, 4 * 2) };
    Mesh_GPU_Data result = mesh_gpu_data_create(
        state,
        gpu_buffer_create(
            array_as_bytes(&array_create_static(positions, 4)),
            GL_ARRAY_BUFFER,
            GL_STATIC_DRAW
        ),
        array_create_static(attrib_infos, 1),
        gpu_buffer_create(
            array_as_bytes(&array_create_static(indices, 6)),
            GL_ELEMENT_ARRAY_BUFFER,
            GL_STATIC_DRAW
        ),
        GL_TRIANGLES,
        6
    );
    return result;
}

Mesh_GPU_Data mesh_utils_create_cube(OpenGLState* state, vec3 color)
{
    vec3 positions[] = {
        vec3(-1.0f, -1.0f, 1.0f), color,
        vec3(1.0f, -1.0f, 1.0f), color,
        vec3(1.0f, 1.0f, 1.0f), color,
        vec3(-1.0f, 1.0f, 1.0f), color,
        vec3(-1.0f, -1.0f, -1.0f), color,
        vec3(1.0f, -1.0f, -1.0f), color,
        vec3(1.0f, 1.0f, -1.0f), color,
        vec3(-1.0f, 1.0f, -1.0f), color
    };
    u32 indices[] = {
        0, 1, 2, 0, 2, 3,
        1, 5, 6, 1, 6, 2,
        3, 2, 6, 3, 6, 7,
        0, 5, 1, 0, 4, 5,
        0, 3, 7, 0, 7, 4,
        4, 6, 5, 4, 7, 6
    };
    VertexAttributeInformation attrib_infos[] = { 
        vertex_attribute_information_make(GL_FLOAT, 3, 0, 0, sizeof(vec3)*2),
        vertex_attribute_information_make(GL_FLOAT, 3, 1, sizeof(vec3), sizeof(vec3)*2),
    };
    Mesh_GPU_Data result = mesh_gpu_data_create(
        state,
        gpu_buffer_create(
            array_as_bytes(&array_create_static(positions, 16)),
            GL_ARRAY_BUFFER,
            GL_STATIC_DRAW
        ),
        array_create_static(attrib_infos, 2),
        gpu_buffer_create(
            array_as_bytes(&array_create_static(indices, 36)),
            GL_ELEMENT_ARRAY_BUFFER,
            GL_STATIC_DRAW
        ),
        GL_TRIANGLES,
        36
    );
    return result;

}
