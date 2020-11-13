#include "opengl_state.hpp"

#include "../utility/utils.hpp"

OpenGLState opengl_state_create() 
{
    OpenGLState result;
    opengl_state_query_current_state(&result);

    // Set values to default, since querying is not trivial for some options
    result.blend_destination = GL_ONE;
    result.blend_source = GL_ONE;
    result.depth_test_enabled = false;
    result.depth_write_depth_values = true;
    result.depth_pass_function = GL_LESS;
    result.fill_polygon = true;
    result.culling_enabled = false;
    result.cull_face = GL_BACK;
    result.front_face = GL_CCW;

    // Initialize texture unit tracking
    int texture_unit_count;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &texture_unit_count);
    result.texture_unit_bindings = array_create_empty<GLuint>(texture_unit_count);
    for (int i = 0; i < texture_unit_count; i++) {
        result.texture_unit_bindings.data[i] = 0;
    }
    result.texture_unit_highest_accessed_index = 0;
    result.texture_unit_next_bindable_index = 0;
    //logg("Texture unit count combined: %d\n", texture_unit_count);

    return result;
}

void opengl_state_destroy(OpenGLState* state) {
    array_destroy(&state->texture_unit_bindings);
}

void opengl_state_query_current_state(OpenGLState* state)
{
    glGetIntegerv(GL_CURRENT_PROGRAM, &state->program_id);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state->vao);
    //glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state->vbo);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &state->element_buffer_object);
}

void opengl_state_use_program(OpenGLState* state, GLuint program_id) {
    if (state->program_id != program_id) {
        glUseProgram(program_id);
        state->program_id = program_id;
    }
}

void opengl_state_bind_vao(OpenGLState* state, GLuint vao) {
    if (vao != state->vao) {
        glBindVertexArray(vao);
        state->vao = vao;
    }
}

void opengl_state_bind_element_buffer(OpenGLState* state, GLuint element_buffer_object) 
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer_object);
    state->element_buffer_object = element_buffer_object;
}

void opengl_state_bind_texture(OpenGLState* state, GLenum texture_target, GLuint texture_id)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(texture_target, texture_id);
    state->texture_unit_bindings[0] = texture_id;
}

GLint opengl_state_bind_texture_to_unit(OpenGLState* state, GLenum texture_target, GLuint texture_id)
{
    // Check if texture is already bound to a texture_unit
    for (int i = 0; i <= state->texture_unit_highest_accessed_index; i++) {
        if (state->texture_unit_bindings.data[i] == texture_id) {
            return i;
        }
    }

    // If it is not already bound, bind it to next free index
    GLint unit = state->texture_unit_next_bindable_index;
    state->texture_unit_bindings.data[unit] = texture_id;
    state->texture_unit_next_bindable_index = (state->texture_unit_next_bindable_index + 1) % state->texture_unit_bindings.size;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(texture_target, texture_id);

    // Update highest accessed
    if (state->texture_unit_highest_accessed_index < unit) {
        state->texture_unit_highest_accessed_index = unit;
    }

    return unit;
}

void opengl_state_set_blending_state(OpenGLState* state, bool enabled, GLenum blend_source, GLenum blend_destination, GLenum blend_equation)
{
    if (enabled != state->blending_enable) {
        state->blending_enable = enabled;
        if (enabled) {
            glEnable(GL_BLEND);
        }
        else {
            glDisable(GL_BLEND);
        }
    }
    if (!enabled) {
        return;
    }
    
    if (blend_source != state->blend_source || blend_destination != state->blend_destination)
    {
        state->blend_source = blend_source;
        state->blend_destination = blend_destination;
        glBlendFunc(blend_source, blend_destination);
    }
    if (blend_equation != state->blend_equation)
    {
        state->blend_equation = blend_equation;
        glBlendEquation(blend_equation);
    }
}

void opengl_state_set_depth_testing(OpenGLState* state, bool depth_test_enabled, bool write_depth_values, GLenum depth_pass_function)
{
    if (state->depth_test_enabled != depth_test_enabled) {
        state->depth_test_enabled = depth_test_enabled;
        if (depth_test_enabled) {
            glEnable(GL_DEPTH_TEST);
        }
        else {
            glDisable(GL_DEPTH_TEST);
        }
    }
    if (state->depth_write_depth_values != write_depth_values) {
        state->depth_write_depth_values = write_depth_values;
        if (write_depth_values) {
            glDepthMask(GL_TRUE);
        }
        else {
            glDepthMask(GL_FALSE);
        }
    }
    if (state->depth_pass_function != depth_pass_function) {
        state->depth_pass_function = depth_pass_function;
        glDepthFunc(depth_pass_function);
    }
}

void opengl_state_set_polygon_filling(OpenGLState* state, bool filling)
{
    if (state->fill_polygon != filling) {
        state->fill_polygon = filling;
        if (filling) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
        else {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }
    }
}

void opengl_state_set_face_culling(OpenGLState* state, bool culling_enabled, GLenum front_face, GLenum cull_face)
{
    if (state->culling_enabled != culling_enabled) {
        state->culling_enabled = culling_enabled;
        if (culling_enabled) {
            glEnable(GL_CULL_FACE);
        }
        else {
            glDisable(GL_CULL_FACE);
        }
    }
    if (state->front_face != front_face) {
        state->front_face = front_face;
        glFrontFace(front_face);
    }
    if (state->cull_face != cull_face) {
        state->cull_face = cull_face;
        glCullFace(cull_face);
    }
}
