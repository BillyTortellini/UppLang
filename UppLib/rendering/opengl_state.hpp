#pragma once

#include "../datastructures/array.hpp"
#include "opengl_function_pointers.hpp"

struct OpenGLState
{
    GLint program_id;
    GLint vao;
    GLint element_buffer_object;

    // Texture handling
    Array<GLuint> texture_unit_bindings;
    int texture_unit_next_bindable_index;
    int texture_unit_highest_accessed_index; // Stores how many indices have been accessed 

    // Blending
    bool blending_enable;
    GLenum blend_source, blend_destination; // GL_ZERO, GL_ONE, GL_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA...
    GLenum blend_equation; // GL_FUNC_ADD, GL_FUNC_SUBTRACT, ...(min, max, reverse_subtract)

    // Depth testing
    bool depth_test_enabled, depth_write_depth_values;
    GLenum depth_pass_function; // GL_LESS, GL_ALWAYS, ...(never, less, equal, greater)

    // Face culling
    bool culling_enabled;
    GLenum front_face; // GL_CCW, GL_CW
    GLenum cull_face; // GL_BACK, GL_FRONT, GL_FRONT_AND_BACK

    // Polygon filling
    bool fill_polygon;
};

OpenGLState opengl_state_create();
void opengl_state_destroy(OpenGLState* state);
void opengl_state_query_current_state(OpenGLState* state);

void opengl_state_use_program(OpenGLState* state, GLuint program_id);
void opengl_state_bind_vao(OpenGLState* state, GLuint vao);
void opengl_state_bind_element_buffer(OpenGLState* state, GLuint element_buffer_object);
// Returns bound texture unit (Target may be TEXTURE_2D, ....)
GLint opengl_state_bind_texture_to_unit(OpenGLState* state, GLenum texture_target, GLuint texture_id);
void opengl_state_bind_texture(OpenGLState* state, GLenum texture_target, GLuint texture_id);
void opengl_state_set_blending_state(OpenGLState* state, bool enabled, GLenum blend_source, GLenum blend_destination, GLenum blend_equation);
void opengl_state_set_depth_testing(OpenGLState* state, bool depth_test_enabled, bool write_depth_values, GLenum depth_pass_function);
void opengl_state_set_face_culling(OpenGLState* state, bool culling_enabled, GLenum front_face, GLenum cull_face);
void opengl_state_set_polygon_filling(OpenGLState* state, bool filling);