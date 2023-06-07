#pragma once

#include "opengl_function_pointers.hpp"
#include "../datastructures/array.hpp"
#include "../utility/datatypes.hpp"

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
