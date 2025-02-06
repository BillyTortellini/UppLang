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
struct Watched_File;
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

enum class Shader_Stage
{
    VERTEX = 0,
    GEOMETRY = 1,
    FRAGMENT = 2,

    SHADER_STAGE_COUNT = 3
};

struct Shader
{
    GLuint program_id;
    bool compiled;
    Dynamic_Array<Shader_Input_Info> input_layout;
    Dynamic_Array<Uniform_Info> uniform_infos;
    Dynamic_Array<String> allocated_strings;
};

Shader* shader_create_empty();
void shader_destroy(Shader* shader);
void shader_reset(Shader* shader);
void shader_add_shader_stage(Shader* shader, Shader_Stage stage, String source_code);
bool shader_compile(Shader* shader);

struct Hotreload_Shader
{
    Shader* shader;
    Watched_File* watched_file;
    const char* filename;
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
struct Mesh;

struct Attribute_Buffer
{
    Dynamic_Array<byte> attribute_data;
    GPU_Buffer gpu_buffer;
    int element_byte_size;
    Mesh* mesh;
    bool is_index_buffer;
};

struct Mesh
{
    Vertex_Description* description;
    Array<Attribute_Buffer> buffers;
    GLuint vao;

    bool reset_every_frame;
    bool queried_this_frame;
    bool dirty; // If CPU and GPU data are synchronized

    // Attribute infos
    int vertex_count; // The vertex count of the attribute buffer with the highest pushed elements
    bool has_index_buffer;
    int index_count;
};

// Crashes if attribute is not in mesh
template<typename T>
Attribute_Buffer* mesh_get_raw_attribute_buffer(Mesh* mesh, Vertex_Attribute<T>* attribute) 
{
    for (int i = 0; i < mesh->description->attributes.size; i++) {
        if (mesh->description->attributes[i] == static_cast<Vertex_Attribute_Base*>(attribute)) {
            return &mesh->buffers[i];
        }
    }
    panic("Currently this function crashes here");
    return 0;
}

// This updates vertex-count and index-count of mesh
void attribute_buffer_report_change_to_mesh(Attribute_Buffer* buffer); 
void attribute_buffer_push_raw_data(Attribute_Buffer* buffer, void* data, int size); // Note: Also reports size-change to mesh and sets is_dirty

template<typename T>
void attribute_buffer_push_value(Attribute_Buffer* buffer, T value) {
    attribute_buffer_push_raw_data(buffer, &value, sizeof(T));
}

template<typename T>
Array<T> attribute_buffer_allocate_slice(Attribute_Buffer* buffer, int element_count) {
    assert(sizeof(T) == buffer->element_byte_size, "Otherwise we push invalid datatype!");
    dynamic_array_reserve(&buffer->attribute_data, buffer->attribute_data.size + element_count * sizeof(T));

    Array<T> result;
    result.data = (T*) &buffer->attribute_data.data[buffer->attribute_data.size];
    result.size = element_count;
    buffer->attribute_data.size += sizeof(T) * element_count;
    attribute_buffer_report_change_to_mesh(buffer);
    return result;
}

template<typename T>
Array<T> mesh_push_attribute_slice(Mesh* mesh, Vertex_Attribute<T>* attribute, int size) {
    Attribute_Buffer* buffer = mesh_get_raw_attribute_buffer(mesh, attribute);
    return attribute_buffer_allocate_slice<T>(buffer, size);
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
    const char* name;
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


enum class Render_Event
{
    FRAME_START,
    WINDOW_SIZE_CHANGED
};

typedef void(*render_event_callback_fn)(void* userdata);
struct Render_Event_Listener
{
    Render_Event event;
    render_event_callback_fn callback;
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
    Dynamic_Array<Render_Event_Listener> render_event_listeners;

    Dynamic_Array<Vertex_Attribute_Base*> vertex_attributes;
    Dynamic_Array<Vertex_Description*> vertex_descriptions;
    Hashtable<String, Mesh*> meshes;
    Hashtable<String, Hotreload_Shader> shaders;
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

void rendering_core_add_render_event_listener(Render_Event event, render_event_callback_fn callback, void* userdata);
void rendering_core_remove_render_event_listener(Render_Event event, render_event_callback_fn callback, void* userdata);

Mesh* rendering_core_query_mesh(const char* name, Vertex_Description* description, bool reset_every_frame);
Shader* rendering_core_query_shader(const char* filename);
Render_Pass* rendering_core_query_renderpass(const char* name, Pipeline_State pipeline_state, Framebuffer* output);
Framebuffer* rendering_core_query_framebuffer_fullscreen(const char* name, Texture_Type type, Depth_Type depth);
Framebuffer* rendering_core_query_framebuffer(const char* name, Texture_Type type, Depth_Type depth, int width, int height);

