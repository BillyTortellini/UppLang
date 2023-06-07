#include "gpu_buffers.hpp"

#include "../math/umath.hpp"
#include "opengl_state.hpp"

GPU_Buffer gpu_buffer_create_empty(int size, GPU_Buffer_Type type, GPU_Buffer_Usage usage)
{
    GPU_Buffer result;
    result.type = type;
    result.usage = usage;
    result.size = size;

    glGenBuffers(1, &result.id);
    glBindBuffer((GLenum) result.type, result.id);
    glBufferData((GLenum) result.type, size, 0, (GLenum) usage);
    return result;
}

GPU_Buffer gpu_buffer_create(Array<byte> data, GPU_Buffer_Type type, GPU_Buffer_Usage usage)
{
    GPU_Buffer result;
    result.type = type;
    result.usage = usage;
    result.size = data.size;

    glGenBuffers(1, &result.id);
    glBindBuffer((GLenum) result.type, result.id);
    glBufferData((GLenum) result.type, data.size, data.data, (GLenum) usage);
    return result;
}

void gpu_buffer_destroy(GPU_Buffer* buffer) {
    glDeleteBuffers(1, &buffer->id);
}

void gpu_buffer_update(GPU_Buffer* buffer, Array<byte> data) 
{
    opengl_state_bind_vao(0);
    glBindBuffer((GLenum) buffer->type, buffer->id);
    if (data.size > buffer->size) {
        glBufferData((GLenum)buffer->type, data.size, data.data, (GLenum) buffer->usage);
        buffer->size = data.size;
    }
    else {
        glBufferSubData((GLenum)buffer->type, 0, data.size, data.data);
    }
}

void gpu_buffer_bind_indexed(GPU_Buffer* buffer, int index) 
{
    if ((GLenum) buffer->type == GL_TRANSFORM_FEEDBACK_BUFFER ||
        (GLenum) buffer->type == GL_UNIFORM_BUFFER ||
        (GLenum) buffer->type == GL_ATOMIC_COUNTER_BUFFER ||
        (GLenum) buffer->type == GL_SHADER_STORAGE_BUFFER)
    {
        glBindBufferBase((GLenum) buffer->type, index, buffer->id);
    }
    else {
        panic("Bound gpu buffer that is not supposed to be bound as an INDEXED buffer!\n");
    }
}

