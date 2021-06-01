#include "gpu_buffers.hpp"

#include "../math/umath.hpp"
#include "rendering_core.hpp"
#include "shader_program.hpp"

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

Vertex_Attribute vertex_attribute_make(Vertex_Attribute_Type type, bool instanced)
{
    Vertex_Attribute result;
    result.location = (int)type;
    result.instanced = instanced;
    result.stride = -1;
    result.offset = -1;
    switch (type)
    {
    case Vertex_Attribute_Type::NORMAL:
    case Vertex_Attribute_Type::TANGENT:
    case Vertex_Attribute_Type::BITANGENT:
    case Vertex_Attribute_Type::POSITION_3D:
    case Vertex_Attribute_Type::COLOR3:
        result.gl_type = GL_FLOAT;
        result.size = 3;
        result.byte_count = 4 * 3;
        break;
    case Vertex_Attribute_Type::POSITION_2D:
    case Vertex_Attribute_Type::UV_COORDINATES_0:
    case Vertex_Attribute_Type::UV_COORDINATES_1:
    case Vertex_Attribute_Type::UV_COORDINATES_2:
    case Vertex_Attribute_Type::UV_COORDINATES_3:
        result.gl_type = GL_FLOAT;
        result.size = 2;
        result.byte_count = 4 * 2;
        break;
    case Vertex_Attribute_Type::COLOR4:
        result.gl_type = GL_FLOAT;
        result.size = 4;
        result.byte_count = 4 * 4;
        break;
    default: panic("Lol");
    }
    return result;
}

Vertex_Attribute vertex_attribute_make_custom(Vertex_Attribute_Data_Type type, GLint shader_location, bool instanced)
{
    Vertex_Attribute result;
    switch (type)
    {
    case Vertex_Attribute_Data_Type::INT: 
        result.size = 1;
        result.gl_type = GL_INT;
        result.byte_count = 4 * 1;
        break;
    case Vertex_Attribute_Data_Type::FLOAT:
        result.size = 1;
        result.gl_type = GL_FLOAT;
        result.byte_count = 4 * 1;
        break;
    case Vertex_Attribute_Data_Type::VEC2:
        result.size = 2;
        result.gl_type = GL_FLOAT;
        result.byte_count = 4 * 2;
        break;
    case Vertex_Attribute_Data_Type::VEC3:
        result.size = 3;
        result.gl_type = GL_FLOAT;
        result.byte_count = 4 * 3;
        break;
    case Vertex_Attribute_Data_Type::VEC4:
        result.size = 4;
        result.gl_type = GL_FLOAT;
        result.byte_count = 4 * 4;
        break;
    case Vertex_Attribute_Data_Type::MAT2:
        result.size = 4;
        result.gl_type = GL_FLOAT;
        result.byte_count = 4 * 2 * 2;
        break;
    case Vertex_Attribute_Data_Type::MAT3:
        result.size = 3*3;
        result.gl_type = GL_FLOAT;
        result.byte_count = 4 * 3 * 3;
        break;
    case Vertex_Attribute_Data_Type::MAT4:
        result.size = 4*4;
        result.gl_type = GL_FLOAT;
        result.byte_count = 4 * 4 * 4;
        break;
    default:
        panic("Called with invalid parameter");
    }
    result.location = shader_location;
    result.offset = -1;
    result.stride = -1;
    result.instanced = instanced;
    return result;
}

bool bound_vertex_gpu_buffer_contains_shader_variable(Bound_Vertex_GPU_Buffer* vertex_buffer, Shader_Variable_Information* variable_info) 
{
    for (int i = 0; i < vertex_buffer->attribute_informations.size; i++) 
    {
        Vertex_Attribute* attrib_info = &vertex_buffer->attribute_informations.data[i];
        bool matches = attrib_info->location == variable_info->location;
        // Check if data types match
        {
            // Check Vectors cause they need special attention
            if (variable_info->type == GL_FLOAT_VEC2) {
                matches = matches && (attrib_info->size == 2 && attrib_info->gl_type == GL_FLOAT);
            }
            else if (variable_info->type == GL_FLOAT_VEC3) {
                matches = matches && (attrib_info->size == 3 && attrib_info->gl_type == GL_FLOAT);
            }
            else if (variable_info->type == GL_FLOAT_VEC4) {
                matches = matches && (attrib_info->size == 4 && attrib_info->gl_type == GL_FLOAT);
            }
            else {
                matches = matches && (attrib_info->size == variable_info->size && attrib_info->gl_type == variable_info->type);
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

void vertex_gpu_buffer_destroy(Bound_Vertex_GPU_Buffer* vertex_data) {
}

Mesh_GPU_Buffer mesh_gpu_buffer_create_without_vertex_buffer(
    Rendering_Core* core,
    GPU_Buffer index_buffer,
    Mesh_Topology topology,
    int index_count
)
{
    Mesh_GPU_Buffer result;
    glGenVertexArrays(1, &result.vao);

    // Bind index buffer
    opengl_state_bind_vao(&core->opengl_state, result.vao);
    result.topology = topology;
    result.index_buffer = index_buffer;
    result.index_count = index_count;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, result.index_buffer.id); // Binds index_buffer to vao

    opengl_state_bind_vao(&core->opengl_state, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return result;
}

// Takes ownership of gpu buffers, copies informations array
Mesh_GPU_Buffer mesh_gpu_buffer_create_with_single_vertex_buffer(
    Rendering_Core* core,
    GPU_Buffer vertex_buffer,
    Array<Vertex_Attribute> informations,
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

    Mesh_GPU_Buffer result = mesh_gpu_buffer_create_without_vertex_buffer(
        core, index_buffer, topology, index_count
    );

    result.vertex_buffers = dynamic_array_create_empty<Bound_Vertex_GPU_Buffer>(3);
    mesh_gpu_buffer_attach_vertex_buffer(&result, core, vertex_buffer, informations);

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

int mesh_gpu_buffer_attach_vertex_buffer(Mesh_GPU_Buffer* mesh, Rendering_Core* core, GPU_Buffer vertex_buffer, Array<Vertex_Attribute> informations)
{
    {
        Bound_Vertex_GPU_Buffer buffer;
        buffer.gpu_buffer = vertex_buffer;
        buffer.attribute_informations = array_create_copy(informations.data, informations.size);
        int stride = 0;
        for (int i = 0; i < informations.size; i++) {
            Vertex_Attribute* attrib = &informations[i];
            if (attrib->offset == -1) {
                attrib->offset = stride;
            }
            stride += attrib->byte_count;
        }
        for (int i = 0; i < informations.size; i++) {
            Vertex_Attribute* attrib = &informations[i];
            if (attrib->stride == -1) {
                attrib->stride = stride;
            }
        }
        dynamic_array_push_back(&mesh->vertex_buffers, buffer);
    }

    opengl_state_bind_vao(&core->opengl_state, mesh->vao);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer.id);
    // Bind vertex attribs
    for (int i = 0; i < informations.size; i++)
    {
        Vertex_Attribute* info = &informations[i];
        glVertexAttribPointer(info->location, info->size,
            info->gl_type, GL_FALSE, info->stride, (void*)(u64)info->offset);
        glEnableVertexAttribArray(info->location);
        if (info->instanced) {
            glVertexAttribDivisor(info->location, 1);
        }
    }
    opengl_state_bind_vao(&core->opengl_state, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return mesh->vertex_buffers.size - 1;
}

bool mesh_gpu_buffer_check_compatability_with_shader(Mesh_GPU_Buffer* mesh, Shader_Program* shader_program)
{
    // Check if we fulfill all shader_program attribute inputs
    for (int i = 0; i < shader_program->attribute_informations.size; i++)
    {
        Shader_Variable_Information* variable_info = &shader_program->attribute_informations.data[i];
        if (variable_info->location == -1) continue; // Skip non-active attributes (Or built in attributes, like gl_VertexID)
        bool mesh_contains_attribute = false;

        // Loop over all attached vertex buffers and see if it contains the attribute
        for (int j = 0; j < mesh->vertex_buffers.size; j++)
        {
            Bound_Vertex_GPU_Buffer* vertex_buffer = &mesh->vertex_buffers.data[j];
            if (bound_vertex_gpu_buffer_contains_shader_variable(vertex_buffer, variable_info)) {
                mesh_contains_attribute = true;
            }
        }

        if (!mesh_contains_attribute) {
            logg("Could not render mesh with shader_program, because it does not contain attribute location %d\n", variable_info->location);
            return false;
        }
    }
    return true;
}

void mesh_gpu_buffer_update_index_buffer(Mesh_GPU_Buffer* mesh, Rendering_Core* core, Array<uint32> data)
{
    opengl_state_bind_vao(&core->opengl_state, 0); // Without this, we may change the index buffer to vao binding from another vao
    gpu_buffer_update(&mesh->index_buffer, array_as_bytes(&data));
    mesh->index_count = data.size;
}

void mesh_gpu_buffer_draw(Mesh_GPU_Buffer* mesh, Rendering_Core* core) {
    opengl_state_bind_vao(&core->opengl_state, mesh->vao);
    glDrawElements((GLenum)mesh->topology, mesh->index_count, GL_UNSIGNED_INT, 0);
}

void mesh_gpu_buffer_draw_instanced(Mesh_GPU_Buffer* mesh, Rendering_Core* core, int instance_count)
{
    opengl_state_bind_vao(&core->opengl_state, mesh->vao);
    glDrawElementsInstanced((GLenum)mesh->topology, mesh->index_count, GL_UNSIGNED_INT, 0, instance_count);
}

void mesh_gpu_buffer_draw_with_shader_program(Mesh_GPU_Buffer* mesh, Shader_Program* shader_program, Rendering_Core* core)
{
    if (!mesh_gpu_buffer_check_compatability_with_shader(mesh, shader_program)) {
        return;
    }
    // Draw
    shader_program_bind(shader_program, core);
    mesh_gpu_buffer_draw(mesh, core);
}

void mesh_gpu_buffer_draw_with_shader_program_instanced(Mesh_GPU_Buffer* mesh, Shader_Program* shader_program, Rendering_Core* core, int instance_count)
{
    if (!mesh_gpu_buffer_check_compatability_with_shader(mesh, shader_program)) {
        return;
    }
    // Draw
    shader_program_bind(shader_program, core);
    mesh_gpu_buffer_draw_instanced(mesh, core, instance_count);
}
