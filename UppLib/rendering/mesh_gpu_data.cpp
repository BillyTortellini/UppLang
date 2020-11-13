#include "mesh_gpu_data.hpp"

/* 
What is stored inside one VBO?
    One or multiple vertex attributes (Interleaved or blocked)

What does a Mesh need in order to be drawn?
    At least one Vertex attribute + Vertex data 
    Element buffer + Fixed topology
*/

VertexAttributeInformation vertex_attribute_information_make(GLenum type, GLint size, 
    GLint shader_location, int offset, int stride) 
{
    VertexAttributeInformation result;
    result.type = type;
    result.size = size;
    result.shader_location = shader_location;
    result.offset = offset;
    result.stride = stride;
    return result;
}

Vertex_GPU_Buffer vertex_gpu_buffer_create(Array<byte> data, GLenum usage)
{
    Vertex_GPU_Buffer result;
    result.attribute_informations = dynamic_array_create_empty<VertexAttributeInformation>(4);
    result.usage = usage;

    // Generate and load data to gpu
    glGenBuffers(1, &result.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, result.vbo);
    result.buffer_size = data.size;
    glBufferData(GL_ARRAY_BUFFER, result.buffer_size, data.data, usage);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return result;
}

Vertex_GPU_Buffer vertex_gpu_buffer_create_empty_with_attribute_information(int buffer_size, VertexAttributeInformation information, GLenum usage)
{
    Array<byte> placeholder;
    placeholder.data = 0;
    placeholder.size = buffer_size;
    return vertex_gpu_buffer_create_with_attribute_information(placeholder, information, usage);
}

Vertex_GPU_Buffer vertex_gpu_buffer_create_empty(int buffer_size, GLenum usage) {
    Array<byte> placeholder;
    placeholder.data = 0;
    placeholder.size = buffer_size;
    return vertex_gpu_buffer_create(placeholder, usage);
}

Vertex_GPU_Buffer vertex_gpu_buffer_create_with_attribute_information(Array<byte> data, VertexAttributeInformation information, GLenum usage)
{
    Vertex_GPU_Buffer result = vertex_gpu_buffer_create(data, usage);
    vertex_gpu_buffer_attach_attribute_information(&result, information);
    return result;
}

void vertex_gpu_buffer_attach_attribute_information(Vertex_GPU_Buffer* vertex_data, VertexAttributeInformation information) {
    dynamic_array_push_back(&vertex_data->attribute_informations, information);
}

void vertex_gpu_buffer_attach_attribute_informations(Vertex_GPU_Buffer* vertex_data, Array<VertexAttributeInformation> informations) {
    for (int i = 0; i < informations.size; i++) {
        vertex_gpu_buffer_attach_attribute_information(vertex_data, informations.data[i]);
    }
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
    glDeleteBuffers(1, &vertex_data->vbo);
    dynamic_array_destroy(&vertex_data->attribute_informations);
}

void vertex_gpu_buffer_update_data(Vertex_GPU_Buffer* vertex_gpu_buffer, Array<byte> data)
{
    if (data.size <= 0) {
        return;
    }
    // Check if we need to resize the buffer
    glBindBuffer(GL_ARRAY_BUFFER, vertex_gpu_buffer->vbo);
    if (vertex_gpu_buffer->buffer_size < data.size) {
        // We need to resize the buffer, glBufferData does this
        glBufferData(GL_ARRAY_BUFFER, data.size, data.data, vertex_gpu_buffer->usage);
        vertex_gpu_buffer->buffer_size = data.size;
    }
    else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, data.size, data.data);
    }
}

Index_GPU_Buffer index_gpu_buffer_create(OpenGLState* state, Array<GLuint> indices, GLenum topology, GLenum usage) 
{
    Index_GPU_Buffer result;
    result.topology = topology;
    result.index_count = indices.size;
    result.usage = usage;

    // Create GPU buffer and store indices there
    glGenBuffers(1, &result.ebo);
    opengl_state_bind_element_buffer(state, result.ebo);
    result.buffer_size = indices.size * sizeof(GLuint);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, result.buffer_size, indices.data, usage);

    // Unbind
    opengl_state_bind_element_buffer(state, 0);

    return result;
}

Index_GPU_Buffer index_gpu_buffer_create_empty(OpenGLState* state, int index_count, GLenum topology, GLenum usage)
{
    Array<GLuint> placeholder;
    placeholder.data = 0;
    placeholder.size = index_count;
    Index_GPU_Buffer result = index_gpu_buffer_create(state, placeholder, topology, usage);
    result.index_count = 0;
    return result;
}

void index_gpu_buffer_update_data(Index_GPU_Buffer* index_buffer, Array<GLuint> indices) 
{
    if (indices.size <= 0) {
        index_buffer->index_count = 0;
        return;
    }

    // Check if we need to resize the buffer
    int data_size = indices.size * sizeof(GLuint);
    glBindBuffer(GL_ARRAY_BUFFER, index_buffer->ebo);
    if (index_buffer->buffer_size < data_size) {
        // We need to resize the buffer, glBufferData does this
        glBufferData(GL_ARRAY_BUFFER, data_size, indices.data, index_buffer->usage);
        index_buffer->buffer_size = data_size;
    }
    else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, data_size, indices.data);
    }
    index_buffer->index_count = indices.size;
}

void index_gpu_buffer_destroy(Index_GPU_Buffer* index_buffer) {
    glDeleteBuffers(1, &index_buffer->ebo);
}

Mesh_GPU_Data mesh_gpu_data_create_empty(OpenGLState* state) 
{
    Mesh_GPU_Data result;
    result.index_buffer.ebo = 0;
    result.vertex_buffers = dynamic_array_create_empty<Vertex_GPU_Buffer>(3);
    glGenVertexArrays(1, &result.vao);
    return result;
}

Mesh_GPU_Data mesh_gpu_data_create(OpenGLState* state, Vertex_GPU_Buffer vertex_buffer, Index_GPU_Buffer index_buffer)
{
    Mesh_GPU_Data result = mesh_gpu_data_create_empty(state);
    mesh_gpu_data_attach_vertex_gpu_buffer(&result, state, vertex_buffer);
    mesh_gpu_data_set_index_gpu_buffer(&result, state, index_buffer);
    return result;
}

void mesh_gpu_data_attach_vertex_gpu_buffer(Mesh_GPU_Data* mesh_data, OpenGLState* state, Vertex_GPU_Buffer vertex_buffer)
{
    dynamic_array_push_back(&mesh_data->vertex_buffers, vertex_buffer);

    opengl_state_bind_vao(state, mesh_data->vao);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer.vbo);
    // Bind vertex attribs
    for (int i = 0; i < vertex_buffer.attribute_informations.size; i++)
    {
        VertexAttributeInformation* information = &vertex_buffer.attribute_informations.data[i];
        glVertexAttribPointer(information->shader_location, information->size,
            information->type, GL_FALSE, information->stride, (void*)(u64)information->offset);
        glEnableVertexAttribArray(information->shader_location);
    }
    opengl_state_bind_vao(state, 0);
}

void mesh_gpu_data_set_index_gpu_buffer(Mesh_GPU_Data* mesh_data, OpenGLState* state, Index_GPU_Buffer index_buffer) 
{
    // Check if we need to clean up our old index_buffer
    if (mesh_data->index_buffer.ebo != 0 && mesh_data->index_buffer.ebo != index_buffer.ebo) {
        index_gpu_buffer_destroy(&mesh_data->index_buffer);
    }
    // Set new index buffer
    mesh_data->index_buffer = index_buffer;
    // Bind new index buffer to VAO
    opengl_state_bind_vao(state, mesh_data->vao);
    opengl_state_bind_element_buffer(state, index_buffer.ebo);
    opengl_state_bind_vao(state, 0);
    opengl_state_bind_element_buffer(state, 0);
}

void mesh_gpu_data_destroy(Mesh_GPU_Data* mesh) 
{
    for (int i = 0; i < mesh->vertex_buffers.size; i++) {
        vertex_gpu_buffer_destroy(&mesh->vertex_buffers.data[i]);
    }
    dynamic_array_destroy(&mesh->vertex_buffers);
    index_gpu_buffer_destroy(&mesh->index_buffer);
    glDeleteVertexArrays(1, &mesh->vao);
}

Vertex_GPU_Buffer* mesh_gpu_data_get_vertex_gpu_buffer(Mesh_GPU_Data* mesh_data, int index) {
    return &mesh_data->vertex_buffers[index];
}

Index_GPU_Buffer* mesh_gpu_data_get_index_gpu_buffer(Mesh_GPU_Data* mesh_data) {
    return &mesh_data->index_buffer;
}

void mesh_gpu_data_draw(Mesh_GPU_Data* mesh, OpenGLState* state) {
    if (mesh->index_buffer.ebo == 0) {
        logg("ERROR drawing mesh, no index buffer was provided/index buffer index is 0\n");
        return;
    }
    opengl_state_bind_vao(state, mesh->vao);
    glDrawElements(mesh->index_buffer.topology, mesh->index_buffer.index_count, GL_UNSIGNED_INT, 0);
}

void mesh_gpu_data_draw_with_shader_program(Mesh_GPU_Data* mesh, ShaderProgram* shader_program, OpenGLState* state)
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
            return;
        }
    }

    // Draw
    shader_program_use(shader_program, state);
    mesh_gpu_data_draw(mesh, state);
}
