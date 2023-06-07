#pragma once

#include "../datastructures/array.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../datastructures/hashtable.hpp"
#include "../datastructures/string.hpp"
#include "../math/umath.hpp"
#include "gpu_buffers.hpp"
#include "cameras.hpp"
#include "opengl_state.hpp"

struct File_Listener;
struct Texture;
struct Framebuffer;



// SHADER
struct Uniform_Info
{
    Shader_Datatype type;
    String uniform_name;
    int array_size; 
    int location;
};

struct Vertex_Attribute_Base;
struct Shader_Input_Info
{
    Vertex_Attribute_Base* attribute; 
    String variable_name;
    int location;
};

struct Shader
{
    GLuint program_id;
    const char* filename;
    Dynamic_Array<Shader_Input_Info> input_layout;
    Dynamic_Array<Uniform_Info> uniform_infos;
    Dynamic_Array<String> allocated_strings;
};



// VERTEX DESCRIPTION
struct Vertex_Attribute_Base
{
    Shader_Datatype type;
    String name; // Static string
    u32 binding_location;
};

template<typename T>
struct Vertex_Attribute : Vertex_Attribute_Base {};

struct Vertex_Description
{
    Array<Vertex_Attribute_Base*> attributes;
};

Vertex_Attribute_Base* vertex_attribute_make_base(Shader_Datatype type, const char* name);
template<typename T>
Vertex_Attribute<T>* vertex_attribute_make(const char* name) {
    return (Vertex_Attribute<T>*) vertex_attribute_make_base(shader_datatype_of<T>(), name);
}
Vertex_Description* vertex_description_create(std::initializer_list<Vertex_Attribute_Base*> attributes);



// MESH
struct Attribute_Buffer
{
    Dynamic_Array<byte> attribute_data;
    GPU_Buffer gpu_buffer;
    int element_count; // Size in elements, not bytes!
};

struct Mesh
{
    Vertex_Description* description;
    Array<Attribute_Buffer> buffers;
    GLuint vao;

    int vertex_count; // Element count of largest attached buffer, neede when pushing indices
    bool reset_every_frame;
    bool queried_this_frame;
    bool dirty; // If CPU and GPU data are synchronized
    bool drawing_has_index_buffer;
    int draw_count;
};

template<typename T>
Array<T> mesh_push_attribute_slice(Mesh* mesh, Vertex_Attribute<T>* attribute, int size) 
{
    int index = -1;
    for (int i = 0; i < mesh->description->attributes.size; i++) {
        if (mesh->description->attributes[i] == static_cast<Vertex_Attribute_Base*>(attribute)) {
            index = i;
            break;
        }
    }
    if (index == -1) {
        panic("Mesh did not contain attribute : %s\n", attribute->name.characters);
    }

    mesh->dirty = true;

    auto& buffer = mesh->buffers[index];
    buffer.element_count += size;
    if (mesh->description->attributes[index] != rendering_core.predefined.index) {
        mesh->vertex_count = math_maximum(mesh->vertex_count, buffer.element_count);
    }

    int before_size = buffer.attribute_data.size;
    dynamic_array_reserve(&buffer.attribute_data, buffer.attribute_data.size + size * sizeof(T));
    buffer.attribute_data.size = buffer.attribute_data.size + size * sizeof(T);

    Array<T> result;
    result.size = size;
    result.data = (T*) &buffer.attribute_data.data[before_size];
    return result;
}

template<typename T>
void mesh_push_attribute(Mesh* mesh, Vertex_Attribute<T>* attribute, std::initializer_list<T> data) 
{
    auto buffer = mesh_push_attribute_slice(mesh, attribute, (int) data.size());
    int i = 0; 
    for (auto& elem : data) {
        buffer[i] = elem;
        i += 1;
    }
}

// NOTE: You need to be carefull updating the index buffer, because you need to be aware of the
//       previously pushed vertices, since indices are offsets...
//       This function helps with that by offsetting the given values to the maximum vertex size,
//       so call this before pushing your vertex data
void mesh_push_indices(Mesh* mesh, std::initializer_list<uint32> indices, bool add_offset);



// UNIFORM_VALUE
struct Uniform_Value
{
public:
    Uniform_Value() {};
    Shader_Datatype datatype;
    const char* name;
    union
    {
        byte buffer[sizeof(mat4)]; // Has to be the size of the largest member!
        i32 data_i32;
        u32 data_u32;
        struct {
            Texture* texture;
            Sampling_Mode sampling_mode;
        } texture;
        float data_float;
        vec2 data_vec2;
        vec3 data_vec3;
        vec4 data_vec4;
        mat2 data_mat2;
        mat3 data_mat3;
        mat4 data_mat4;
    };
};

Uniform_Value uniform_make(const char* name, float val);
Uniform_Value uniform_make(const char* name, vec2 val);
Uniform_Value uniform_make(const char* name, vec3 val);
Uniform_Value uniform_make(const char* name, vec4 val);
Uniform_Value uniform_make(const char* name, mat2 val);
Uniform_Value uniform_make(const char* name, mat3 val);
Uniform_Value uniform_make(const char* name, mat4 val);
Uniform_Value uniform_make(const char* name, int val);
Uniform_Value uniform_make(const char* name, Texture* data, Sampling_Mode sampling_mode);



// RENDER-PASS
enum class Render_Pass_Command_Type
{
    UNIFORM,
    DRAW_CALL,
    DRAW_CALL_COUNT,
};

struct Mesh;
struct Shader;
struct Render_Pass_Command
{
    Render_Pass_Command() {};
    Render_Pass_Command_Type type;
    union {
        struct {
            Mesh* mesh;
            Mesh_Topology topology;
            Shader* shader;
        } draw_call;
        struct {
            Mesh* mesh;
            Shader* shader;
            Mesh_Topology topology;
            int element_start;
            int element_count;
        } draw_call_count;
        struct {
            Shader* shader;
            Uniform_Value value;
        } uniform;
    };
};

struct Render_Pass
{
    Dynamic_Array<Render_Pass_Command> commands;
    Dynamic_Array<Render_Pass*> dependents;
    int dependency_count;

    Pipeline_State pipeline_state;
    Framebuffer* output_buffer; // If null this is the default framebuffer
    bool queried_this_frame;
};

void render_pass_set_uniforms(Render_Pass* pass, Shader* shader, std::initializer_list<Uniform_Value> uniforms);
void render_pass_draw(Render_Pass* pass, Shader* shader, Mesh* mesh, Mesh_Topology topology, std::initializer_list<Uniform_Value> uniforms);
void render_pass_draw_count(
    Render_Pass* pass, Shader* shader, Mesh* mesh, Mesh_Topology topology, 
    std::initializer_list<Uniform_Value> uniforms, int element_start, int element_count);
void render_pass_add_dependency(Render_Pass* pass, Render_Pass* depends_on);



// RENDERING CORE
struct Predefined_Objects
{
    // Attributes
    Vertex_Attribute<vec3>* position3D;
    Vertex_Attribute<vec2>* position2D;
    Vertex_Attribute<vec2>* texture_coordinates;
    Vertex_Attribute<vec3>* normal;
    Vertex_Attribute<vec3>* tangent;
    Vertex_Attribute<vec3>* bitangent;
    Vertex_Attribute<vec3>* color3;
    Vertex_Attribute<vec4>* color4;
    // NOTE: This is an important predefined value because this is used to distinguish between
    //       normal vertex buffers and index buffer!
    Vertex_Attribute<uint32>* index; 

    // Meshes
    Mesh* quad;
    Mesh* cube;

    // Render Pass
    Render_Pass* main_pass;
};

struct Render_Information
{
    float backbuffer_width;
    float backbuffer_height;
    float monitor_dpi;
    float current_time_in_seconds;
};

typedef void(*window_size_changed_callback)(void* userdata);
struct Window_Size_Listener
{
    window_size_changed_callback callback;
    void* userdata;
};

struct Rendering_Core
{
    Pipeline_State pipeline_state;
    OpenGL_State opengl_state;
    File_Listener* file_listener;
    Render_Information render_information;
    GPU_Buffer ubo_render_information; // Binding 0
    GPU_Buffer ubo_camera_data;        // Binding 1
    Dynamic_Array<Window_Size_Listener> window_size_listeners;

    Dynamic_Array<Vertex_Attribute_Base*> vertex_attributes;
    Dynamic_Array<Vertex_Description*> vertex_descriptions;
    Hashtable<String, Mesh*> meshes;
    Hashtable<String, Shader*> shaders;
    Hashtable<String, Render_Pass*> render_passes;
    Hashtable<String, Framebuffer*> framebuffers;

    Predefined_Objects predefined;
};

extern Rendering_Core rendering_core;

void rendering_core_initialize(int backbuffer_width, int backbuffer_height, float monitor_dpi);
void rendering_core_destroy();

void rendering_core_prepare_frame(float current_time, int backbuffer_width, int backbuffer_height);
void rendering_core_render(Camera_3D* camera, Framebuffer_Clear_Type clear_type);

void rendering_core_update_pipeline_state(Pipeline_State new_state);
void rendering_core_clear_bound_framebuffer(Framebuffer_Clear_Type clear_type);

void rendering_core_add_window_size_listener(window_size_changed_callback callback, void* userdata);
void rendering_core_remove_window_size_listener(void* userdata);

Mesh* rendering_core_query_mesh(const char* name, Vertex_Description* description, bool reset_every_frame);
Shader* rendering_core_query_shader(const char* filename);
Render_Pass* rendering_core_query_renderpass(const char* name, Pipeline_State pipeline_state, Framebuffer* output);
Framebuffer* rendering_core_query_framebuffer_fullscreen(const char* name, Texture_Type type, Depth_Type depth);
Framebuffer* rendering_core_query_framebuffer(const char* name, Texture_Type type, Depth_Type depth, int width, int height);

