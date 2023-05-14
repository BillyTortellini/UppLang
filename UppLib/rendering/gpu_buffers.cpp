#include "gpu_buffers.hpp"

#include "../math/umath.hpp"
#include "shader_program.hpp"
#include "../rendering/rendering_core.hpp"

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

Mesh_GPU_Buffer mesh_gpu_buffer_create_without_vertex_buffer(
    GPU_Buffer index_buffer,
    Mesh_Topology topology,
    int index_count
)
{
    Mesh_GPU_Buffer result;
    glGenVertexArrays(1, &result.vao);

    // Bind index buffer
    opengl_state_bind_vao(result.vao);
    result.topology = topology;
    result.index_buffer = index_buffer;
    result.index_count = index_count;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, result.index_buffer.id); // Binds index_buffer to vao

    opengl_state_bind_vao(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return result;
}

// Takes ownership of gpu buffers, copies informations array
Mesh_GPU_Buffer mesh_gpu_buffer_create_with_single_vertex_buffer(
    GPU_Buffer vertex_buffer,
    Array<REMOVE_ME> informations,
    GPU_Buffer index_buffer,
    Mesh_Topology topology,
    int index_count
)
{
    if (index_buffer.type != GPU_Buffer_Type::INDEX_BUFFER) {
        panic("Index buffer should be of index buffer type!");
    }
    if (vertex_buffer.type != GPU_Buffer_Type::VERTEX_BUFFER) {
        panic("Vertex buffer should be of vertex buffer type!");
    }

    Mesh_GPU_Buffer result = mesh_gpu_buffer_create_without_vertex_buffer(index_buffer, topology, index_count);
    result.vertex_buffers = dynamic_array_create_empty<Bound_Vertex_GPU_Buffer>(3);

    return result;
}

void mesh_gpu_buffer_destroy(Mesh_GPU_Buffer* mesh) 
{
    for (int i = 0; i < mesh->vertex_buffers.size; i++) {
        gpu_buffer_destroy(&mesh->vertex_buffers[i].gpu_buffer);
        array_destroy(&mesh->vertex_buffers[i].attribute_informations);
    }
    dynamic_array_destroy(&mesh->vertex_buffers);
    gpu_buffer_destroy(&mesh->index_buffer);
    glDeleteVertexArrays(1, &mesh->vao);
}

int mesh_gpu_buffer_attach_vertex_buffer(Mesh_GPU_Buffer* mesh, GPU_Buffer vertex_buffer, Array<REMOVE_ME> informations)
{
    return mesh->vertex_buffers.size - 1;
}

void mesh_gpu_buffer_update_index_buffer(Mesh_GPU_Buffer* mesh, Array<uint32> data)
{
    opengl_state_bind_vao(0); // Without this, we may change the index buffer to vao binding from another vao
    gpu_buffer_update(&mesh->index_buffer, array_as_bytes(&data));
    mesh->index_count = data.size;
}
