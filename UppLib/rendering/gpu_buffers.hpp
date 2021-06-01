#pragma once

#include "opengl_function_pointers.hpp"
#include "../datastructures/array.hpp"
#include "../utility/datatypes.hpp"

struct Shader_Program;
struct Rendering_Core;
struct Shader_Variable_Information;

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



enum class Vertex_Attribute_Type
{
    POSITION_3D = 0,
    POSITION_2D = 1,
    UV_COORDINATES_0 = 2,
    UV_COORDINATES_1 = 3,
    UV_COORDINATES_2 = 4,
    UV_COORDINATES_3 = 5,
    NORMAL = 6,
    TANGENT = 7,
    BITANGENT = 8,
    COLOR3 = 9,
    COLOR4 = 10,
    // TODO: Bone weights and indices

    MINIMUM_OPENGL_ATTRIBUTE_COUNT = 16,
    PADDING_ATTRIBUTE = 6969,
};

enum class Vertex_Attribute_Data_Type
{
    FLOAT, 
    INT, 
    VEC2, 
    VEC3, 
    VEC4, 
    MAT2, 
    MAT3, 
    MAT4
};

struct Vertex_Attribute
{
    // Type information
    GLenum gl_type; // Common: GL_BYTE, GL_SHORT, GL_INT, GL_FLOAT, GL_DOUBLE and other
    GLint size; // The number of types (E.g. for vec3 type = GL_FLOAT and size = 3)
    GLint location; // Note: Is not actually part of vbo, but of vao
    int byte_count;
    bool instanced;

    // How the data is stored inside the vbo:
    int offset;
    int stride;
};

Vertex_Attribute vertex_attribute_make(Vertex_Attribute_Type type, bool instanced = false);
Vertex_Attribute vertex_attribute_make_custom(Vertex_Attribute_Data_Type type, GLint shader_location, bool instanced = false);

struct Bound_Vertex_GPU_Buffer
{
    GPU_Buffer gpu_buffer;
    Array<Vertex_Attribute> attribute_informations;
};
bool bound_vertex_gpu_buffer_contains_shader_variable(Bound_Vertex_GPU_Buffer* vertex_buffer, Shader_Variable_Information* variable_info);



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
    Rendering_Core* core,
    GPU_Buffer index_buffer,
    Mesh_Topology topology,
    int index_count
);

// Takes ownership of gpu buffers, copies informations array
Mesh_GPU_Buffer mesh_gpu_buffer_create_with_single_vertex_buffer(
    Rendering_Core* core,
    GPU_Buffer vertex_buffer,
    Array<Vertex_Attribute> informations,
    GPU_Buffer index_buffer,
    Mesh_Topology topology,
    int index_count
);

void mesh_gpu_buffer_destroy(Mesh_GPU_Buffer* mesh);

int mesh_gpu_buffer_attach_vertex_buffer(Mesh_GPU_Buffer* mesh, Rendering_Core* core, GPU_Buffer vertex_buffer, Array<Vertex_Attribute> informations);
void mesh_gpu_buffer_update_index_buffer(Mesh_GPU_Buffer* mesh, Rendering_Core* core, Array<uint32> data);
bool mesh_gpu_buffer_check_compatability_with_shader(Mesh_GPU_Buffer* mesh, Shader_Program* shader_program);

void mesh_gpu_buffer_draw(Mesh_GPU_Buffer* mesh, Rendering_Core* core);
void mesh_gpu_buffer_draw_with_shader_program(Mesh_GPU_Buffer* mesh, Shader_Program* shader_program, Rendering_Core* core);
void mesh_gpu_buffer_draw_instanced(Mesh_GPU_Buffer* mesh, Rendering_Core* core, int instance_count);
void mesh_gpu_buffer_draw_with_shader_program_instanced(Mesh_GPU_Buffer* mesh, Shader_Program* shader_program, Rendering_Core* core, int instance_count);
