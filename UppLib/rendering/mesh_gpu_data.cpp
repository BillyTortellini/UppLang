#include "mesh_gpu_data.hpp"

#include "../math/umath.hpp"

VertexAttributeInformationMaker g_vertex_attribute_information_maker;

void vertex_attribute_information_maker_create() {
    g_vertex_attribute_information_maker.stride = 0;
    g_vertex_attribute_information_maker.infos = dynamic_array_create_empty<VertexAttributeInformation>(8);
}

void vertex_attribute_information_maker_destroy() {
    dynamic_array_destroy(&g_vertex_attribute_information_maker.infos);
}

void vertex_attribute_information_maker_reset() {
    g_vertex_attribute_information_maker.stride = 0;
    dynamic_array_reset(&g_vertex_attribute_information_maker.infos);
}

void vertex_attribute_information_maker_add(int location, VertexAttributeInformationType::ENUM type, bool instanced) 
{
    VertexAttributeInformation info;
    info.instanced = instanced;
    info.offset = g_vertex_attribute_information_maker.stride;
    info.shader_location = location;
    int type_size = 0;
    switch (type)
    {
    case VertexAttributeInformationType::INT: 
        type_size = sizeof(int);
        info.size = 1;
        info.type = GL_INT;
        break;
    case VertexAttributeInformationType::FLOAT:
        type_size = sizeof(float);
        info.size = 1;
        info.type = GL_FLOAT;
        break;
    case VertexAttributeInformationType::VEC2:
        type_size = sizeof(vec2);
        info.size = 2;
        info.type = GL_FLOAT;
        break;
    case VertexAttributeInformationType::VEC3:
        type_size = sizeof(vec3);
        info.size = 3;
        info.type = GL_FLOAT;
        break;
    case VertexAttributeInformationType::VEC4:
        type_size = sizeof(vec4);
        info.size = 4;
        info.type = GL_FLOAT;
        break;
    case VertexAttributeInformationType::MAT2:
        type_size = sizeof(mat2);
        info.size = 4;
        info.type = GL_FLOAT;
        break;
    case VertexAttributeInformationType::MAT3:
        type_size = sizeof(mat3);
        info.size = 3*3;
        info.type = GL_FLOAT;
        break;
    case VertexAttributeInformationType::MAT4:
        type_size = sizeof(mat4);
        info.size = 4*4;
        info.type = GL_FLOAT;
        break;
    default:
        panic("Called with invalid parameter");
    }
    g_vertex_attribute_information_maker.stride += type_size;
    dynamic_array_push_back(&g_vertex_attribute_information_maker.infos, info);
}

Array<VertexAttributeInformation> vertex_attribute_information_maker_make() {
    for (int i = 0; i < g_vertex_attribute_information_maker.infos.size; i++) {
        VertexAttributeInformation& info = g_vertex_attribute_information_maker.infos[i];
        info.stride = g_vertex_attribute_information_maker.stride;
    }
    return dynamic_array_to_array(&g_vertex_attribute_information_maker.infos);
}


GPU_Buffer gpu_buffer_create_empty(int size, GLenum binding, GLenum usage)
{
    GPU_Buffer result;
    result.binding_target = binding;
    result.usage = usage;
    result.size = size;

    glGenBuffers(1, &result.id);
    glBindBuffer(result.binding_target, result.id);
    glBufferData(binding, size, 0, usage);
    return result;
}

GPU_Buffer gpu_buffer_create(Array<byte> data, GLenum binding, GLenum usage)
{
    GPU_Buffer result;
    result.binding_target = binding;
    result.usage = usage;
    result.size = data.size;

    glGenBuffers(1, &result.id);
    glBindBuffer(result.binding_target, result.id);
    glBufferData(binding, data.size, data.data, usage);
    return result;
}

void gpu_buffer_destroy(GPU_Buffer* buffer) {
    glDeleteBuffers(1, &buffer->id);
}

void gpu_buffer_update(GPU_Buffer* buffer, Array<byte> data) 
{
    glBindBuffer(buffer->binding_target, buffer->id);
    if (data.size > buffer->size) {
        glBufferData(buffer->binding_target, data.size, data.data, buffer->usage);
        buffer->size = data.size;
    }
    else {
        glBufferSubData(buffer->binding_target, 0, data.size, data.data);
    }
}

void gpu_buffer_bind_indexed(GPU_Buffer* buffer, int index) 
{
    if (buffer->binding_target == GL_TRANSFORM_FEEDBACK_BUFFER ||
        buffer->binding_target == GL_UNIFORM_BUFFER ||
        buffer->binding_target == GL_ATOMIC_COUNTER_BUFFER ||
        buffer->binding_target == GL_SHADER_STORAGE_BUFFER)
    {
        glBindBufferBase(buffer->binding_target, index, buffer->id);
    }
    else {
        panic("Bound gpu buffer that is not supposed to be bound as an INDEXED buffer!\n");
    }
}

VertexAttributeInformation vertex_attribute_information_make(GLenum type, GLint size, 
    GLint shader_location, int offset, int stride, bool instanced) 
{
    VertexAttributeInformation result;
    result.type = type;
    result.size = size;
    result.shader_location = shader_location;
    result.offset = offset;
    result.stride = stride;
    result.instanced = instanced;
    return result;
}

Vertex_GPU_Buffer vertex_gpu_buffer_create(GPU_Buffer buffer, Array<VertexAttributeInformation> informations) // Copies array
{
    Vertex_GPU_Buffer result;
    result.attribute_informations = array_create_copy(informations.data, informations.size);
    result.vertex_buffer = buffer;
    return result;
}

bool vertex_gpu_buffer_contains_shader_variable(Vertex_GPU_Buffer* vertex_buffer, ShaderVariableInformation* variable_info) 
{
    for (int i = 0; i < vertex_buffer->attribute_informations.size; i++) 
    {
        VertexAttributeInformation* attrib_info = &vertex_buffer->attribute_informations.data[i];
        bool matches = attrib_info->shader_location == variable_info->location;
        // Check if data types match
        {
            // Check Vectors cause they need special attention
            if (variable_info->type == GL_FLOAT_VEC2) {
                matches = matches && (attrib_info->size == 2 && attrib_info->type == GL_FLOAT);
            }
            else if (variable_info->type == GL_FLOAT_VEC3) {
                matches = matches && (attrib_info->size == 3 && attrib_info->type == GL_FLOAT);
            }
            else if (variable_info->type == GL_FLOAT_VEC4) {
                matches = matches && (attrib_info->size == 4 && attrib_info->type == GL_FLOAT);
            }
            else {
                matches = matches && (attrib_info->size == variable_info->size && attrib_info->type == variable_info->type);
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

void vertex_gpu_buffer_destroy(Vertex_GPU_Buffer* vertex_data) {
    gpu_buffer_destroy(&vertex_data->vertex_buffer);
    array_destroy(&vertex_data->attribute_informations);
}

// Takes ownership of gpu buffers, copies informations array
Mesh_GPU_Data mesh_gpu_data_create(
    OpenGLState* state, 
    GPU_Buffer vertex_buffer, 
    Array<VertexAttributeInformation> informations, 
    GPU_Buffer index_buffer, 
    GLenum topology,
    int index_count
)
{
    Mesh_GPU_Data result;
    glGenVertexArrays(1, &result.vao);
    opengl_state_bind_vao(state, result.vao);
    // Bind index buffer
    result.topology = topology;
    result.index_buffer = index_buffer;
    result.index_count = index_count;
    opengl_state_bind_element_buffer(state, result.index_buffer.id);

    // Bind vertex buffer
    result.vertex_buffers = dynamic_array_create_empty<Vertex_GPU_Buffer>(3);
    mesh_gpu_data_attach_vertex_buffer(&result, state, vertex_buffer, informations);
    
    opengl_state_bind_vao(state, 0);

    return result;
}

void mesh_gpu_data_attach_vertex_buffer(Mesh_GPU_Data* mesh_data, OpenGLState* state, GPU_Buffer vertex_buffer, Array<VertexAttributeInformation> informations)
{
    dynamic_array_push_back(&mesh_data->vertex_buffers, 
        vertex_gpu_buffer_create(vertex_buffer, informations)
    );

    opengl_state_bind_vao(state, mesh_data->vao);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer.id);
    // Bind vertex attribs
    for (int i = 0; i < informations.size; i++)
    {
        VertexAttributeInformation* info = &informations[i];
        glVertexAttribPointer(info->shader_location, info->size,
            info->type, GL_FALSE, info->stride, (void*)(u64)info->offset);
        glEnableVertexAttribArray(info->shader_location);
        if (info->instanced) {
            glVertexAttribDivisor(info->shader_location, 1);
        }
    }
    opengl_state_bind_vao(state, 0);
}

void mesh_gpu_data_destroy(Mesh_GPU_Data* mesh) 
{
    for (int i = 0; i < mesh->vertex_buffers.size; i++) {
        vertex_gpu_buffer_destroy(&mesh->vertex_buffers[i]);
    }
    dynamic_array_destroy(&mesh->vertex_buffers);
    gpu_buffer_destroy(&mesh->index_buffer);
    glDeleteVertexArrays(1, &mesh->vao);
}

bool mesh_gpu_data_check_compatability_with_shader(Mesh_GPU_Data* mesh, ShaderProgram* shader_program)
{
    // Check if we fulfill all shader_program attribute inputs
    for (int i = 0; i < shader_program->attribute_informations.size; i++) 
    {
        ShaderVariableInformation* variable_info = &shader_program->attribute_informations.data[i];
        if (variable_info->location == -1) continue; // Skip non-active attributes (Or built in attributes, like gl_VertexID)
        bool mesh_contains_attribute = false;

        // Loop over all attached vertex buffers and see if it contains the attribute
        for (int j = 0; j < mesh->vertex_buffers.size; j++) 
        {
            Vertex_GPU_Buffer* vertex_buffer = &mesh->vertex_buffers.data[j];
            if (vertex_gpu_buffer_contains_shader_variable(vertex_buffer, variable_info)) {
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

void mesh_gpu_data_draw(Mesh_GPU_Data* mesh, OpenGLState* state) {
    opengl_state_bind_vao(state, mesh->vao);
    glDrawElements(mesh->topology, mesh->index_count, GL_UNSIGNED_INT, 0);
    //opengl_state_bind_vao(state, 0);
}

void mesh_gpu_data_draw_instanced(Mesh_GPU_Data* mesh, OpenGLState* state, int instance_count)
{
    opengl_state_bind_vao(state, mesh->vao);
    glDrawElementsInstanced(mesh->topology, mesh->index_count, GL_UNSIGNED_INT, 0, instance_count);
}


void mesh_gpu_data_draw_with_shader_program(Mesh_GPU_Data* mesh, ShaderProgram* shader_program, OpenGLState* state)
{
    if (!mesh_gpu_data_check_compatability_with_shader(mesh, shader_program)) {
        return;
    }
    // Draw
    shader_program_use(shader_program, state);
    mesh_gpu_data_draw(mesh, state);
}

void mesh_gpu_data_draw_with_shader_program_instanced(Mesh_GPU_Data* mesh, ShaderProgram* shader_program, OpenGLState* state, int instance_count)
{
    if (!mesh_gpu_data_check_compatability_with_shader(mesh, shader_program)) {
        return;
    }
    // Draw
    shader_program_use(shader_program, state);
    mesh_gpu_data_draw_instanced(mesh, state, instance_count);
}

void mesh_gpu_data_update_index_buffer(Mesh_GPU_Data* mesh_data, Array<uint32> data, OpenGLState* state)
{
    opengl_state_bind_vao(state, 0); // Without this, we may change the index buffer to vao binding from another vao
    gpu_buffer_update(&mesh_data->index_buffer, array_as_bytes(&data));
    mesh_data->index_count = data.size;
}
