#pragma once

#include "opengl_function_pointers.hpp"
#include "../datastructures/array.hpp"
#include "../math/umath.hpp"

struct Texture;



// TEXTURES
enum class Texture_Type
{
    DEPTH = GL_DEPTH_COMPONENT,
    DEPTH_STENCIL = GL_DEPTH_STENCIL,
    STENCIL = GL_STENCIL_INDEX,
    RED_U8 = GL_RED,
    RED_GREEN_U8 = GL_RG,
    RED_GREEN_BLUE_U8 = GL_RGB,
    RED_GREEN_BLUE_ALPHA_U8 = GL_RGBA,
    RED_F16 = GL_R16F,
    RED_GREEN_F16 = GL_RG16F,
    RED_GREEN_BLUE_F16 = GL_RGB16F,
    RED_GREEN_BLUE_ALPHA_F16 = GL_RGBA16F,
    RED_F32 = GL_R32F,
    RED_GREEN_F32 = GL_RG32F,
    RED_GREEN_BLUE_F32 = GL_RGB32F,
    RED_GREEN_BLUE_ALPHA_F32 = GL_RGBA32F,
};

bool texture_type_is_float(Texture_Type type);
bool texture_type_is_int(Texture_Type type);
int texture_type_channel_count(Texture_Type type);
int texture_type_pixel_byte_size(Texture_Type type);

enum class Texture_Binding_Type
{
    TEXTURE_1D = GL_TEXTURE_1D,
    TEXTURE_2D = GL_TEXTURE_2D,
    TEXTURE_3D = GL_TEXTURE_3D,
    CUBE_MAP = GL_TEXTURE_CUBE_MAP,
    TEXTURE_2D_MULTISAMPLED = GL_TEXTURE_2D_MULTISAMPLE,
};

enum class Texture_Minification_Mode
{
    NEAREST_PIXEL_VALUE = GL_NEAREST,
    BILINEAR_INTERPOLATION = GL_LINEAR,
    TRILINEAR_INTERPOLATION = GL_LINEAR_MIPMAP_LINEAR, // Requires mipmap
};

enum class Texture_Magnification_Mode
{
    NEAREST_PIXEL_VALUE = GL_NEAREST,
    BILINEAR_INTERPOLATION = GL_LINEAR,
};

enum class Texture_Wrapping_Mode
{
    CLAMP_TO_EDGE = GL_CLAMP_TO_EDGE,
    REPEAT = GL_REPEAT,
    MIRROR_REPEAT = GL_MIRRORED_REPEAT,
    //CLAMP_TO_BORDER_COLOR = GL_CLAMP_TO_BORDER, // Maybe support later, color needs to be saved somewhere
};

struct Sampling_Mode
{
    Texture_Minification_Mode minification;
    Texture_Magnification_Mode magnification;
    Texture_Wrapping_Mode u_wrapping;
    Texture_Wrapping_Mode v_wrapping;
};

Sampling_Mode sampling_mode_make(Texture_Minification_Mode min_mode, Texture_Magnification_Mode mag_mode,
    Texture_Wrapping_Mode u_wrapping, Texture_Wrapping_Mode v_wrapping);
Sampling_Mode sampling_mode_nearest();
Sampling_Mode sampling_mode_bilinear();
Sampling_Mode sampling_mode_trilinear();



// FRAMEBUFFER
enum class Framebuffer_Clear_Type
{
    NONE,
    COLOR,
    DEPTH,
    COLOR_AND_DEPTH
};

enum class Depth_Type
{
    NO_DEPTH,
    DEPTH_32_NO_STENCIL,
    DEPTH_24_STENCIL_8,
    RENDERBUFFER_DEPTH_32_NO_STENCIL,
    RENDERBUFFER_DEPTH_24_STENCIL_8
};



// MESH
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



// OPENGL STATE
struct Texture_Unit_Binding
{
    Sampling_Mode sampling_mode;
    GLint bound_texture_id;
};

struct OpenGL_State
{
    GLint active_program;
    GLint active_vao; // Active Vao
    GLint active_framebuffer;
    GLint texture2D_binding;
    vec4 clear_color;

    // Texture handling
    Array<Texture_Unit_Binding> texture_unit_bindings; 
    int next_free_texture_unit;
    int highest_used_texture_unit; // Stores how many indices have been accessed 
};

OpenGL_State opengl_state_create();
void opengl_state_destroy(OpenGL_State* state);
void opengl_state_set_clear_color(vec4 clear_color);

void opengl_state_bind_program(GLuint program_id);
void opengl_state_bind_vao(GLuint vao);
void opengl_state_bind_framebuffer(GLuint framebuffer);
void opengl_state_bind_texture(Texture_Binding_Type binding_target, GLuint texture_id);
GLint opengl_state_bind_texture_to_next_free_unit(Texture_Binding_Type binding_target, GLuint texture_id, Sampling_Mode sampling_mode);



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
void pipeline_state_set_unconditional(Pipeline_State* state);
void pipeline_state_switch(Pipeline_State current_state, Pipeline_State new_state);



// SHADER DATATYPE
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
    return Shader_Datatype::FLOAT;
}

template<> Shader_Datatype shader_datatype_of<Texture*>();
template<> Shader_Datatype shader_datatype_of<float>();
template<> Shader_Datatype shader_datatype_of<uint32>();
template<> Shader_Datatype shader_datatype_of<vec2>();
template<> Shader_Datatype shader_datatype_of<vec3>();
template<> Shader_Datatype shader_datatype_of<vec4>();
template<> Shader_Datatype shader_datatype_of<mat2>();
template<> Shader_Datatype shader_datatype_of<mat3>();
template<> Shader_Datatype shader_datatype_of<mat4>();


