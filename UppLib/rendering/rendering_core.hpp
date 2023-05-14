#pragma once

#include "../datastructures/array.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../datastructures/hashtable.hpp"
#include "../datastructures/string.hpp"
#include "../math/umath.hpp"
#include "opengl_function_pointers.hpp"
#include "gpu_buffers.hpp"
#include "cameras.hpp"
#include "framebuffer.hpp"

struct File_Listener;



// OPENGL STATE
enum class Texture_Binding_Type
{
    TEXTURE_1D = GL_TEXTURE_1D,
    TEXTURE_2D = GL_TEXTURE_2D,
    TEXTURE_3D = GL_TEXTURE_3D,
    CUBE_MAP = GL_TEXTURE_CUBE_MAP,
    TEXTURE_2D_MULTISAMPLED = GL_TEXTURE_2D_MULTISAMPLE,
};

enum class Framebuffer_Clear_Type
{
    NONE,
    COLOR,
    DEPTH,
    COLOR_AND_DEPTH
};

struct OpenGL_State
{
    GLint active_program;
    GLint active_vao; // Active Vao
    GLint active_framebuffer;
    vec4 clear_color;

    // Texture handling
    Array<GLuint> texture_unit_bindings; 
    int texture_unit_next_bindable_index;
    int texture_unit_highest_accessed_index; // Stores how many indices have been accessed 
};

OpenGL_State opengl_state_create();
void opengl_state_destroy(OpenGL_State* state);
void opengl_state_set_clear_color(vec4 clear_color);

void opengl_state_bind_program(GLuint program_id);
void opengl_state_bind_vao(GLuint vao);
void opengl_state_bind_framebuffer(GLuint framebuffer);
GLint opengl_state_bind_texture_to_next_free_unit(Texture_Binding_Type binding_target, GLuint texture_id);



// PIPELINE STATE
enum class Blend_Operand
{
    ONE = GL_ONE,
    ZERO = GL_ZERO,
    SOURCE_COLOR = GL_SRC_COLOR,
    ONE_MINUS_SOURCE_COLOR = GL_ONE_MINUS_SRC_ALPHA,
    DESTINTAION_COLOR = GL_DST_COLOR,
    ONE_MINUS_DESTINATION_COLOR = GL_ONE_MINUS_DST_COLOR,
    SOURCE_ALPHA = GL_SRC_ALPHA,
    ONE_MINUS_SOURCE_ALPHA = GL_ONE_MINUS_SRC_ALPHA,
    DESTINTAION_ALPHA = GL_DST_ALPHA, 
    ONE_MINUS_DESTINATION_ALPHA = GL_ONE_MINUS_DST_ALPHA,
    CUSTOM_COLOR = GL_CONSTANT_COLOR,
    ONE_MINUS_CUSTOM_COLOR = GL_ONE_MINUS_CONSTANT_COLOR,
    CUSTOM_ALPHA = GL_CONSTANT_ALPHA,
    ONE_MINUS_CUSTOM_ALPHA = GL_ONE_MINUS_CONSTANT_ALPHA
};

enum class Blend_Equation
{
    ADDITION = GL_FUNC_ADD,
    SUBTRACTION = GL_FUNC_SUBTRACT, // Source - Destination
    REVERSE_SUBTRACT = GL_FUNC_REVERSE_SUBTRACT, // Destination - Source
    MINIMUM = GL_MIN,
    MAXIMUM = GL_MAX
};

/*
    Blending in OpenGL is done using the equation:
        C_src * F_src blend_op C_dest * F_dest
    Where 
        F_src, F_dest ... the source and destination operands in Blending state,
        C_src         ... Color output of the fragment shader
        C_dest        ... Color stored in the current framebuffer
        blend_op      ... Operation that should be performed
*/
struct Blending_State
{
    bool blending_enabled;
    Blend_Operand source;
    Blend_Operand destination;
    Blend_Equation equation;
    vec4 custom_color;
};

enum class Depth_Test_Type
{
    IGNORE_DEPTH, // Always draw over
    TEST_DEPTH_DONT_WRITE, // Doesnt update depth values after drawing
    TEST_DEPTH // Do depth testing
};

// Per Default LESS is choosen
enum class Depth_Pass_Function
{
    ALWAYS = GL_ALWAYS,
    NEVER = GL_NEVER,
    LESS = GL_LESS,
    EQUAL = GL_EQUAL,
    NOT_EQUAL = GL_NOTEQUAL,
    LESS_THAN = GL_LESS,
    LESS_EQUAL = GL_LEQUAL,
    GREATER_THAN = GL_GREATER,
    GREATER_EQUAL = GL_GEQUAL,
};

struct Depth_Test_State
{
    Depth_Test_Type test_type;
    Depth_Pass_Function pass_function;
};

enum class Front_Face_Defintion
{
    CLOCKWISE = GL_CW,
    COUNTER_CLOCKWISE = GL_CCW,
};

enum class Face_Culling_Mode
{
    CULL_BACKFACE = GL_BACK,
    CULL_FRONTFACE = GL_FRONT,
    CULL_FRONT_AND_BACK = GL_FRONT_AND_BACK
};

struct Face_Culling_State
{
    bool culling_enabled;
    Front_Face_Defintion front_face_definition;
    Face_Culling_Mode cull_mode;
};

enum class Polygon_Filling_Mode
{
    POINT = GL_POINT,
    LINE = GL_LINE,
    FILL = GL_FILL
};

struct Pipeline_State
{
    Blending_State blending_state;
    Depth_Test_State depth_state;
    Face_Culling_State culling_state;
    Polygon_Filling_Mode polygon_filling_mode;
};
Pipeline_State pipeline_state_make_default();



// SHADER 
enum class Shader_Datatype
{
    FLOAT,
    UINT32,
    VEC2,
    VEC3,
    VEC4,
    MAT2,
    MAT3,
    MAT4,
    TEXTURE_2D_BINDING
};

struct Shader_Datatype_Info
{
    GLenum uniformType; // Type reported by glGetUniform
    GLenum vertexAttribType; // Type for vertex-attribute setup (glVertexAttribPointer)
    const char* name; // Plain text name, as a variable type in glsl
    u32 byte_size; // Size in bytes
};

Shader_Datatype_Info shader_datatype_get_info(Shader_Datatype type);

template<typename T>
Shader_Datatype shader_datatype_of() {
    panic("Not a shader datatype!");
}

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
        int texture_2D_id;
        float data_float;
        vec2 data_vec2;
        vec3 data_vec3;
        vec4 data_vec4;
        mat2 data_mat2;
        mat3 data_mat3;
        mat4 data_mat4;
    };
};

template<typename T>
Uniform_Value uniform_value_make(const char* name, T data) {
    Uniform_Value val;
    val.name = name;
    memcpy(&val.buffer[0], &data, sizeof(T));
    return val;
}

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



// Vertex Descriptions
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

struct Predefined_Attributes
{
    Vertex_Attribute<vec3>* position3D;
    Vertex_Attribute<vec2>* position2D;
    Vertex_Attribute<vec2>* textureCoordinates;
    Vertex_Attribute<vec3>* normal;
    Vertex_Attribute<vec3>* tangent;
    Vertex_Attribute<vec3>* bitangent;
    Vertex_Attribute<vec3>* color3;
    Vertex_Attribute<vec4>* color4;
    // NOTE: This is an important predefined value because this is used to distinguish between
    //       normal vertex buffers and index buffer!
    Vertex_Attribute<uint32>* index; 
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
    bool dirty; // E.g. cpu data and gpu buffer are not synchronized
};

struct Mesh
{
    Vertex_Description* description;
    Array<Attribute_Buffer> buffers;
    GLuint vao;
    Mesh_Topology topology;

    bool queried_this_frame;
    int has_element_buffer;
    int primitive_count; // Calculated each frame
};

template<typename T>
Array<T> mesh_get_attribute_buffer(Mesh* mesh, Vertex_Attribute<T>* attribute, int size) 
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

    auto& buffer = mesh->buffers[index];
    buffer.dirty = true;
    dynamic_array_reserve(&buffer.attribute_data, size * sizeof(T));
    buffer.attribute_data.size = size * sizeof(T);

    Array<T> result;
    result.size = size;
    result.data = (T*) buffer.attribute_data.data;
    return result;
}



// RENDER-PASS
enum class Render_Pass_Command_Type
{
    UNIFORM,
    DRAW_CALL
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
            Shader* shader;
        } draw_call;
        struct {
            Shader* shader;
            Uniform_Value value;
        } uniform;
    };
};

struct Render_Pass
{
    Dynamic_Array<Render_Pass_Command> commands;
    Pipeline_State pipeline_state;
    bool queried_this_frame;
};

void render_pass_draw(Render_Pass* pass, Shader* shader, Mesh* mesh, std::initializer_list<Uniform_Value> uniforms);



// RENDERING CORE
struct Render_Information
{
    float viewport_width;
    float viewport_height;
    float window_width;
    float window_height;
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

    Predefined_Attributes predefined;
    Render_Pass* main_pass;
};

extern Rendering_Core rendering_core;

void rendering_core_initialize(int window_width, int window_height, float monitor_dpi);
void rendering_core_destroy();

void rendering_core_render(Camera_3D* camera, Framebuffer_Clear_Type clear_type, float current_time, int window_width, int window_height);

void rendering_core_update_pipeline_state(Pipeline_State new_state);
void rendering_core_clear_bound_framebuffer(Framebuffer_Clear_Type clear_type);

void rendering_core_add_window_size_listener(window_size_changed_callback callback, void* userdata);
void rendering_core_remove_window_size_listener(void* userdata);

Mesh* rendering_core_query_mesh(const char* name, Vertex_Description* description, Mesh_Topology topology, GPU_Buffer_Usage usage);
Shader* rendering_core_query_shader(const char* filename);
Render_Pass* rendering_core_query_renderpass(const char* name, Pipeline_State pipeline_state);

