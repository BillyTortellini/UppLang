#include "rendering_core.hpp"

#include "../utility/utils.hpp"
#include "../utility/file_listener.hpp"

OpenGL_State opengl_state_create()
{
    OpenGL_State result;
    result.active_program = 0;
    result.active_vao = 0;
    result.clear_color = vec4(0.0f);

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

void opengl_state_destroy(OpenGL_State* state) {
    array_destroy(&state->texture_unit_bindings);
}

void opengl_state_set_clear_color(OpenGL_State* state, vec4 clear_color)
{
    if (state->clear_color.x != clear_color.x ||
        state->clear_color.y != clear_color.y ||
        state->clear_color.z != clear_color.z ||
        state->clear_color.w != clear_color.w
        ) {
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    }
}

void opengl_state_bind_program(OpenGL_State* state, GLuint program_id) 
{
    if (state->active_program != program_id) {
        glUseProgram(program_id);
        state->active_program = program_id;
    }
}

void opengl_state_bind_vao(OpenGL_State* state, GLuint vao) {
    if (vao != state->active_vao) {
        glBindVertexArray(vao);
        state->active_vao = vao;
    }
}

GLint opengl_state_bind_texture_to_next_free_unit(OpenGL_State* state, Texture_Binding_Type binding_target, GLuint texture_id)
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
    glBindTexture((GLenum) binding_target, texture_id);

    // Update highest accessed
    if (state->texture_unit_highest_accessed_index < unit) {
        state->texture_unit_highest_accessed_index = unit;
    }

    return unit;
}



Pipeline_State pipeline_state_make_default()
{
    Blending_State blend_state;
    blend_state.blending_enabled = false;
    blend_state.custom_color = vec4(0.0f);
    blend_state.source = Blend_Operand::SOURCE_ALPHA;
    blend_state.destination = Blend_Operand::ONE_MINUS_SOURCE_ALPHA;
    blend_state.equation = Blend_Equation::ADDITION;

    Face_Culling_State culling_state;
    culling_state.culling_enabled = false;
    culling_state.cull_mode = Face_Culling_Mode::CULL_BACKFACE;
    culling_state.front_face_definition = Front_Face_Defintion::COUNTER_CLOCKWISE;

    Depth_Test_State depth_test_state;
    depth_test_state.test_type = Depth_Test_Type::IGNORE_DEPTH;
    depth_test_state.pass_function = Depth_Pass_Function::LESS;

    Pipeline_State state;
    state.blending_state = blend_state;
    state.culling_state = culling_state;
    state.depth_state = depth_test_state;
    state.polygon_filling_mode = Polygon_Filling_Mode::FILL;

    return state;
}

void pipeline_state_set_unconditional(Pipeline_State* state)
{
    if (state->blending_state.blending_enabled) {
        glEnable(GL_BLEND);
    }
    else {
        glDisable(GL_BLEND);
    }
    glBlendColor(state->blending_state.custom_color.x, state->blending_state.custom_color.y, 
        state->blending_state.custom_color.z, state->blending_state.custom_color.w
    );
    glBlendFunc((GLenum)state->blending_state.source, (GLenum)state->blending_state.destination);
    glBlendEquation((GLenum)state->blending_state.equation);

    if (state->culling_state.culling_enabled) {
        glEnable(GL_CULL_FACE);
    }
    else {
        glDisable(GL_CULL_FACE);
    }
    glCullFace((GLenum)state->culling_state.cull_mode);
    glFrontFace((GLenum)state->culling_state.front_face_definition);
    switch (state->depth_state.test_type)
    {
    case Depth_Test_Type::IGNORE_DEPTH:
        glDisable(GL_DEPTH_TEST);
        break;
    case Depth_Test_Type::TEST_DEPTH:
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        break;
    case Depth_Test_Type::TEST_DEPTH_DONT_WRITE:
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        break;
    }
    glDepthFunc((GLenum)state->depth_state.pass_function);
    glPolygonMode(GL_FRONT_AND_BACK, (GLenum)state->polygon_filling_mode);
}

void rendering_core_update_pipeline_state(Rendering_Core* core, Pipeline_State new_state)
{
    if (core->pipeline_state.blending_state.blending_enabled != new_state.blending_state.blending_enabled)
    {
        Blending_State* current = &core->pipeline_state.blending_state;
        Blending_State* updated = &new_state.blending_state;
        if (updated->blending_enabled)
        {
            glEnable(GL_BLEND);
            if (current->custom_color.x != updated->custom_color.x ||
                current->custom_color.y != updated->custom_color.y ||
                current->custom_color.z != updated->custom_color.z ||
                current->custom_color.w != updated->custom_color.w) {
                glBlendColor(updated->custom_color.x, updated->custom_color.y, updated->custom_color.z, updated->custom_color.w);
            }

            if (current->destination != updated->destination || current->source != updated->source) {
                glBlendFunc((GLenum)updated->source, (GLenum)updated->destination);
            }

            if (current->equation != updated->equation) {
                glBlendEquation((GLenum)updated->equation);
            }
        }
        else {
            glDisable(GL_BLEND);
        }
    }

    if (core->pipeline_state.culling_state.culling_enabled != new_state.culling_state.culling_enabled)
    {
        Face_Culling_State* current = &core->pipeline_state.culling_state;
        Face_Culling_State* updated = &new_state.culling_state;
        if (updated->culling_enabled)
        {
            glEnable(GL_CULL_FACE);
            if (current->cull_mode != updated->cull_mode) {
                glCullFace((GLenum)updated->cull_mode);
            }
            if (current->front_face_definition != updated->front_face_definition) {
                glFrontFace((GLenum)updated->front_face_definition);
            }
        }
        else {
            glDisable(GL_CULL_FACE);
        }
    }

    {
        Depth_Test_State* current = &core->pipeline_state.depth_state;
        Depth_Test_State* updated = &new_state.depth_state;

        if (current->test_type != updated->test_type)
        {
            switch (updated->test_type)
            {
            case Depth_Test_Type::IGNORE_DEPTH:
                glDisable(GL_DEPTH_TEST);
                break;
            case Depth_Test_Type::TEST_DEPTH:
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
                break;
            case Depth_Test_Type::TEST_DEPTH_DONT_WRITE:
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                break;
            }
        }
        if (current->pass_function != updated->pass_function) {
            glDepthFunc((GLenum)updated->pass_function);
        }
    }

    {
        Polygon_Filling_Mode current = core->pipeline_state.polygon_filling_mode;
        Polygon_Filling_Mode updated = new_state.polygon_filling_mode;
        if (current != updated) {
            glPolygonMode(GL_FRONT_AND_BACK, (GLenum)updated);
        }
    }

    core->pipeline_state = new_state;
}

Render_Information render_information_make(int viewport_width, int viewport_height, int window_width, int window_height, float monitor_dpi, float current_time)
{
    Render_Information info;
    info.viewport_width = (float)viewport_width;
    info.viewport_height = (float)viewport_height;
    info.window_width = window_width;
    info.window_height = window_height;
    info.monitor_dpi = monitor_dpi;
    info.current_time_in_seconds = current_time;
    return info;
}

Rendering_Core rendering_core_create(int window_width, int window_height, float monitor_dpi)
{
    Rendering_Core result;
    result.pipeline_state = pipeline_state_make_default();
    pipeline_state_set_unconditional(&result.pipeline_state);
    result.file_listener = file_listener_create();
    result.opengl_state = opengl_state_create();

    result.ubo_render_information = gpu_buffer_create_empty(sizeof(Render_Information), GPU_Buffer_Type::UNIFORM_BUFFER, GPU_Buffer_Usage::DYNAMIC);
    gpu_buffer_bind_indexed(&result.ubo_render_information, 0);
    result.ubo_camera_data = gpu_buffer_create_empty(sizeof(Camera_3D_UBO_Data), GPU_Buffer_Type::UNIFORM_BUFFER, GPU_Buffer_Usage::DYNAMIC);
    gpu_buffer_bind_indexed(&result.ubo_camera_data, 1);

    result.render_information.viewport_height = 0;
    result.render_information.viewport_width = 0;
    result.render_information.monitor_dpi = monitor_dpi;
    result.render_information.window_width = window_width;
    result.render_information.window_height = window_height;
    rendering_core_update_viewport(&result, window_width, window_height);

    result.window_size_changed_listeners = dynamic_array_create_empty<window_size_changed_callback>(32);
    result.window_size_changed_listeners_userdata = dynamic_array_create_empty<void*>(32);

    return result;
}

void rendering_core_destroy(Rendering_Core* core)
{
    gpu_buffer_destroy(&core->ubo_camera_data);
    gpu_buffer_destroy(&core->ubo_render_information);
    file_listener_destroy(core->file_listener);
    opengl_state_destroy(&core->opengl_state);
    dynamic_array_destroy(&core->window_size_changed_listeners);
    dynamic_array_destroy(&core->window_size_changed_listeners_userdata);
}

void rendering_core_window_size_changed(Rendering_Core* core, int window_width, int window_height)
{
    if ((window_width != core->render_information.viewport_width || window_height != core->render_information.viewport_height) &&
        (window_width != 0 && window_height != 0))
    {
        core->render_information.window_width = window_width;
        core->render_information.window_height = window_height;
        rendering_core_update_viewport(core, window_width, window_height);
        for (int i = 0; i < core->window_size_changed_listeners.size; i++) {
            core->window_size_changed_listeners[i](core->window_size_changed_listeners_userdata[i], core);
        }
    }
}

void rendering_core_add_window_size_listener(Rendering_Core* core, window_size_changed_callback callback, void* userdata)
{
    dynamic_array_push_back(&core->window_size_changed_listeners, callback);
    dynamic_array_push_back(&core->window_size_changed_listeners_userdata, userdata);
}

void rendering_core_remove_window_size_listener(Rendering_Core* core, void* userdata)
{
    int found = -1;
    for (int i = 0; i < core->window_size_changed_listeners.size; i++) {
        if (core->window_size_changed_listeners_userdata[i] == userdata) {
            found = i;
        }
    }
    if (found == -1) panic("Should not happen i guess");
    dynamic_array_swap_remove(&core->window_size_changed_listeners, found);
    dynamic_array_swap_remove(&core->window_size_changed_listeners_userdata, found);
}

void rendering_core_update_3D_Camera_UBO(Rendering_Core* core, Camera_3D* camera)
{
    Camera_3D_UBO_Data data = camera_3d_ubo_data_make(camera);
    gpu_buffer_update(&core->ubo_camera_data, array_create_static_as_bytes(&data, 1));
}

void rendering_core_prepare_frame(Rendering_Core* core, Camera_3D* camera, float current_time)
{
    core->render_information.current_time_in_seconds = current_time;
    gpu_buffer_update(&core->ubo_render_information, array_create_static_as_bytes(&core->render_information, 1));
    rendering_core_update_3D_Camera_UBO(core, camera);
    file_listener_check_if_files_changed(core->file_listener);
}

void rendering_core_update_viewport(Rendering_Core* core, int width, int height)
{
    if (core->render_information.viewport_width != width || core->render_information.viewport_height != height)
    {
        core->render_information.viewport_width = width;
        core->render_information.viewport_height = height;
        glViewport(0, 0, width, height);
        gpu_buffer_update(&core->ubo_render_information, array_create_static_as_bytes(&core->render_information, 1));
    }
}
