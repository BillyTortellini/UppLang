#pragma once

#include "../datastructures/array.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../math/umath.hpp"
#include "opengl_function_pointers.hpp"
#include "gpu_buffers.hpp"
#include "cameras.hpp"

struct File_Listener;

enum class Texture_Binding_Type
{
    TEXTURE_1D = GL_TEXTURE_1D,
    TEXTURE_2D = GL_TEXTURE_2D,
    TEXTURE_3D = GL_TEXTURE_3D,
    CUBE_MAP = GL_TEXTURE_CUBE_MAP,
    TEXTURE_2D_MULTISAMPLED = GL_TEXTURE_2D_MULTISAMPLE,
};

struct OpenGL_State
{
    GLint active_program;
    GLint active_vao; // Active Vao
    vec4 clear_color;

    // Texture handling
    Array<GLuint> texture_unit_bindings; 
    int texture_unit_next_bindable_index;
    int texture_unit_highest_accessed_index; // Stores how many indices have been accessed 
};

OpenGL_State opengl_state_create();
void opengl_state_destroy(OpenGL_State* state);
void opengl_state_set_clear_color(OpenGL_State* state, vec4 clear_color);

void opengl_state_bind_program(OpenGL_State* state, GLuint program_id);
void opengl_state_bind_vao(OpenGL_State* state, GLuint vao);
GLint opengl_state_bind_texture_to_next_free_unit(OpenGL_State* state, Texture_Binding_Type binding_target, GLuint texture_id);



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



struct Render_Information
{
    float viewport_width;
    float viewport_height;
    float window_width;
    float window_height;
    float monitor_dpi;
    float current_time_in_seconds;
};

struct Rendering_Core;
typedef void(*window_size_changed_callback)(void* userdata, Rendering_Core* core);

struct Rendering_Core
{
    Pipeline_State pipeline_state;
    OpenGL_State opengl_state;
    File_Listener* file_listener;
    Render_Information render_information;
    GPU_Buffer ubo_render_information; // Binding 0
    GPU_Buffer ubo_camera_data;        // Binding 1
    Dynamic_Array<window_size_changed_callback> window_size_changed_listeners;
    Dynamic_Array<void*> window_size_changed_listeners_userdata;
};

Rendering_Core rendering_core_create(int window_width, int window_height, float monitor_dpi);
void rendering_core_destroy(Rendering_Core* core);

void rendering_core_window_size_changed(Rendering_Core* core, int window_width, int window_height);
void rendering_core_add_window_size_listener(Rendering_Core* core, window_size_changed_callback callback, void* userdata);
void rendering_core_remove_window_size_listener(Rendering_Core* core, void* userdata);
void rendering_core_prepare_frame(Rendering_Core* core, Camera_3D* camera, float current_time);
void rendering_core_update_viewport(Rendering_Core* core, int width, int height);
void rendering_core_update_3D_Camera_UBO(Rendering_Core* core, Camera_3D* camera);
void rendering_core_update_pipeline_state(Rendering_Core* core, Pipeline_State new_state);

