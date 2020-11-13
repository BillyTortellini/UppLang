#pragma once

#include "opengl_function_pointers.hpp"
#include "../datastructures/array.hpp"
#include "../utility/datatypes.hpp"
#include "shader_program.hpp"
#include "opengl_state.hpp"

struct VertexAttributeInformation
{
    GLenum type; // Common: GL_BYTE, GL_SHORT, GL_INT, GL_FLOAT, GL_DOUBLE and other
    GLint size; // The number of types (E.g. for vec3 type = GL_FLOAT and size = 3)
    GLint shader_location; // Is not actually part of vbo, but part of vao
    // Where this data is stored
    int offset;
    int stride;
};

VertexAttributeInformation vertex_attribute_information_make(GLenum type, GLint size, GLint shader_location, int offset, int stride);

struct Vertex_GPU_Buffer
{
    GLuint vbo;
    int buffer_size;
    DynamicArray<VertexAttributeInformation> attribute_informations;
    GLenum usage;
};

struct Index_GPU_Buffer
{
    GLuint ebo;
    int index_count;
    int buffer_size;
    GLenum usage;
    GLenum topology;
};

struct Mesh_GPU_Data
{
    GLuint vao;
    DynamicArray<Vertex_GPU_Buffer> vertex_buffers;
    Index_GPU_Buffer index_buffer;
};

Vertex_GPU_Buffer vertex_gpu_buffer_create(Array<byte> data, GLenum usage);
Vertex_GPU_Buffer vertex_gpu_buffer_create_empty(int buffer_size, GLenum usage);
Vertex_GPU_Buffer vertex_gpu_buffer_create_empty_with_attribute_information(int buffer_size, VertexAttributeInformation information, GLenum usage);
Vertex_GPU_Buffer vertex_gpu_buffer_create_with_attribute_information(Array<byte> data, VertexAttributeInformation information, GLenum usage);
void vertex_gpu_buffer_destroy(Vertex_GPU_Buffer* gpu_vertex_buffer);
void vertex_gpu_buffer_attach_attribute_information(Vertex_GPU_Buffer* vertex_data, VertexAttributeInformation information);
void vertex_gpu_buffer_attach_attribute_informations(Vertex_GPU_Buffer* vertex_data, Array<VertexAttributeInformation> information);
void vertex_gpu_buffer_update_data(Vertex_GPU_Buffer* vertex_gpu_buffer, Array<byte> data);

Index_GPU_Buffer index_gpu_buffer_create(OpenGLState* state, Array<GLuint> indices, GLenum topology, GLenum usage);
Index_GPU_Buffer index_gpu_buffer_create_empty(OpenGLState* state, int buffer_size, GLenum topology, GLenum usage);
void index_gpu_buffer_update_data(Index_GPU_Buffer* index_buffer, Array<GLuint> indices);
void index_gpu_buffer_destroy(Index_GPU_Buffer* index_buffer);

Mesh_GPU_Data mesh_gpu_data_create(OpenGLState* state, Vertex_GPU_Buffer vertex_buffer, Index_GPU_Buffer index_buffer);
Mesh_GPU_Data mesh_gpu_data_create_empty(OpenGLState* state);
void mesh_gpu_data_destroy(Mesh_GPU_Data* mesh);
void mesh_gpu_data_attach_vertex_gpu_buffer(Mesh_GPU_Data* mesh_data, OpenGLState* state, Vertex_GPU_Buffer vertex_buffer);
void mesh_gpu_data_set_index_gpu_buffer(Mesh_GPU_Data* mesh_data, OpenGLState* state, Index_GPU_Buffer index_buffer);
Vertex_GPU_Buffer* mesh_gpu_data_get_vertex_gpu_buffer(Mesh_GPU_Data* mesh_data, int index);
Index_GPU_Buffer* mesh_gpu_data_get_index_gpu_buffer(Mesh_GPU_Data* mesh_data);
void mesh_gpu_data_draw(Mesh_GPU_Data* mesh, OpenGLState* state);
void mesh_gpu_data_draw_with_shader_program(Mesh_GPU_Data* mesh, ShaderProgram* shader_program, OpenGLState* state);
