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
    vec3 normals;
    vec2 texture_coordinates;
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
    Mesh_GPU_Data result = mesh_gpu_data_create(
        state,
        vertex_gpu_buffer_create_with_attribute_information(
            array_create_static((byte*)positions, sizeof(positions)),
            vertex_attribute_information_make(GL_FLOAT, 2, DefaultVertexAttributeLocation::POSITION_2D, 0, sizeof(vec2)),
            GL_STATIC_DRAW
        ),
        index_gpu_buffer_create(
            state,
            array_create_static<GLuint>(indices, 6),
            GL_TRIANGLES,
            GL_STATIC_DRAW
        )
    );
    return result;
}

 

