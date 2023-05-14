#pragma once

#include "opengl_function_pointers.hpp"
#include "../datastructures/array.hpp"
#include "../utility/datatypes.hpp"
#include "../datastructures/dynamic_array.hpp"

struct Rendering_Core;

enum class Mesh_Topology
{
    POINTS = GL_POINTS,
    LINES = GL_LINES,
    LINE_STRIP = GL_LINE_STRIP,
    LINE_LOOP = GL_LINE_LOOP,
    TRIANGLES = GL_TRIANGLES,
    TRIANGLE_STRIP = GL_TRIANGLE_STRIP, // Note: Primitive Restart can be used in index buffer
    TRIANGLE_FAN = GL_TRIANGLE_FAN,
};

enum class GPU_Buffer_Type
{
    VERTEX_BUFFER = GL_ARRAY_BUFFER,
    INDEX_BUFFER = GL_ELEMENT_ARRAY_BUFFER,
    UNIFORM_BUFFER = GL_UNIFORM_BUFFER,
    TRANSFORM_FEEDBACK_BUFFER = GL_TRANSFORM_FEEDBACK_BUFFER,
    ATOMIC_COUNTER_BUFFER = GL_ATOMIC_COUNTER_BUFFER,
    SHADER_STORAGE_BUFFER = GL_SHADER_STORAGE_BUFFER
};

enum class GPU_Buffer_Usage
{
    STATIC = GL_STATIC_DRAW,
    DYNAMIC = GL_DYNAMIC_DRAW,
};

struct GPU_Buffer
{
    GLuint id;
    int size;
    GPU_Buffer_Type type;
    GPU_Buffer_Usage usage;
};

GPU_Buffer gpu_buffer_create(Array<byte> data, GPU_Buffer_Type type, GPU_Buffer_Usage usage);
GPU_Buffer gpu_buffer_create_empty(int size, GPU_Buffer_Type type, GPU_Buffer_Usage usage);
void gpu_buffer_destroy(GPU_Buffer* buffer);
void gpu_buffer_update(GPU_Buffer* buffer, Array<byte> data);
void gpu_buffer_bind_indexed(GPU_Buffer* buffer, int index);


struct REMOVE_ME {

};


struct Bound_Vertex_GPU_Buffer
{
    GPU_Buffer gpu_buffer;
    Array<REMOVE_ME> attribute_informations;
};

struct Mesh_GPU_Buffer
{
    GLuint vao;
    Dynamic_Array<Bound_Vertex_GPU_Buffer> vertex_buffers;
    GPU_Buffer index_buffer;
    Mesh_Topology topology;
    int index_count;
};

// Takes GPU_Buffer ownership
Mesh_GPU_Buffer mesh_gpu_buffer_create_without_vertex_buffer(
    GPU_Buffer index_buffer,
    Mesh_Topology topology,
    int index_count
);

// Takes ownership of gpu buffers, copies informations array
Mesh_GPU_Buffer mesh_gpu_buffer_create_with_single_vertex_buffer(
    GPU_Buffer vertex_buffer,
    Array<REMOVE_ME> informations,
    GPU_Buffer index_buffer,
    Mesh_Topology topology,
    int index_count
);

void mesh_gpu_buffer_destroy(Mesh_GPU_Buffer* mesh);
int mesh_gpu_buffer_attach_vertex_buffer(Mesh_GPU_Buffer* mesh, GPU_Buffer vertex_buffer, Array<REMOVE_ME> informations);
void mesh_gpu_buffer_update_index_buffer(Mesh_GPU_Buffer* mesh, Array<uint32> data);
