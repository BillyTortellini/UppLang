#pragma once

#include "opengl_function_pointers.hpp"
#include "../datastructures/array.hpp"
#include "../utility/datatypes.hpp"
#include "shader_program.hpp"
#include "opengl_state.hpp"

struct GPU_Buffer
{
    GLuint id;
    int size;
    GLenum binding_target; // GL_ARRAY_BUFFER/GL_UNIFORM_BUFFER...
    GLenum usage;
};

struct VertexAttributeInformation
{
    // Type information
    GLenum type; // Common: GL_BYTE, GL_SHORT, GL_INT, GL_FLOAT, GL_DOUBLE and other
    GLint size; // The number of types (E.g. for vec3 type = GL_FLOAT and size = 3)
    GLint shader_location; // Note: Is not actually part of vbo, but of vao
    bool instanced;

    // How the data is stored inside the vbo:
    int offset;
    int stride;
};

VertexAttributeInformation vertex_attribute_information_make(GLenum type, GLint size, GLint shader_location, int offset, int stride, bool instanced = false);

namespace VertexAttributeInformationType
{
    enum ENUM {
        FLOAT,INT,VEC2,VEC3,VEC4,MAT2,MAT3,MAT4
    };
};

struct VertexAttributeInformationMaker {
    int stride;
    DynamicArray<VertexAttributeInformation> infos;
};

void vertex_attribute_information_maker_create();
void vertex_attribute_information_maker_destroy();
void vertex_attribute_information_maker_reset();
void vertex_attribute_information_maker_add(int location, VertexAttributeInformationType::ENUM type, bool instanced = false);
Array<VertexAttributeInformation> vertex_attribute_information_maker_make();

struct Vertex_GPU_Buffer
{
    GPU_Buffer vertex_buffer;
    Array<VertexAttributeInformation> attribute_informations;
};

struct Mesh_GPU_Data
{
    GLuint vao;
    DynamicArray<Vertex_GPU_Buffer> vertex_buffers;
    GPU_Buffer index_buffer;
    GLenum topology;
    int index_count;
};

GPU_Buffer gpu_buffer_create(Array<byte> data, GLenum binding, GLenum usage);
GPU_Buffer gpu_buffer_create_empty(int size, GLenum binding, GLenum usage);
void gpu_buffer_destroy(GPU_Buffer* buffer);
void gpu_buffer_update(GPU_Buffer* buffer, Array<byte> data);
void gpu_buffer_bind_indexed(GPU_Buffer* buffer, int index);

Vertex_GPU_Buffer vertex_gpu_buffer_create(GPU_Buffer buffer, Array<VertexAttributeInformation> informations); // Copies array, takes ownership of buffer
void vertex_gpu_buffer_destroy(Vertex_GPU_Buffer* gpu_vertex_buffer);
bool vertex_gpu_buffer_contains_shader_variable(Vertex_GPU_Buffer* vertex_buffer, ShaderVariableInformation* variable_info);

// Takes ownership of gpu buffers, copies informations array
Mesh_GPU_Data mesh_gpu_data_create(
    OpenGLState* state,
    GPU_Buffer vertex_buffer,
    Array<VertexAttributeInformation> informations,
    GPU_Buffer index_buffer,
    GLenum topology,
    int index_count
);
void mesh_gpu_data_destroy(Mesh_GPU_Data* mesh);
void mesh_gpu_data_attach_vertex_buffer(Mesh_GPU_Data* mesh_data, OpenGLState* state, GPU_Buffer vertex_buffer, Array<VertexAttributeInformation> informations);
void mesh_gpu_data_update_index_buffer(Mesh_GPU_Data* mesh_data, Array<uint32> data, OpenGLState* state);

bool mesh_gpu_data_check_compatability_with_shader(Mesh_GPU_Data* mesh, ShaderProgram* shader_program);
void mesh_gpu_data_draw(Mesh_GPU_Data* mesh, OpenGLState* state);
void mesh_gpu_data_draw_with_shader_program(Mesh_GPU_Data* mesh, ShaderProgram* shader_program, OpenGLState* state);
void mesh_gpu_data_draw_instanced(Mesh_GPU_Data* mesh, OpenGLState* state, int instance_count);
void mesh_gpu_data_draw_with_shader_program_instanced(Mesh_GPU_Data* mesh, ShaderProgram* shader_program, OpenGLState* state, int instance_count);
