#include "opengl_state.hpp"

#include "rendering_core.hpp"

String shader_datatype_as_string(Shader_Datatype type)
{
    switch (type)
    {
    case Shader_Datatype::FLOAT: return string_create_static("float");
    case Shader_Datatype::UINT32: return string_create_static("uint");
    case Shader_Datatype::VEC2: return string_create_static("vec2");
    case Shader_Datatype::VEC3: return string_create_static("vec3");
    case Shader_Datatype::VEC4: return string_create_static("vec4");
    case Shader_Datatype::MAT2: return string_create_static("mat2");
    case Shader_Datatype::MAT3: return string_create_static("mat3");
    case Shader_Datatype::MAT4: return string_create_static("mat4");
    case Shader_Datatype::TEXTURE_2D_BINDING: return string_create_static("sampler2D");
    default: panic("");
    }

    return string_create_static(" E R R O R");
}

// TEXTURE
bool texture_type_is_float(Texture_Type type)
{
    switch (type)
    {
    case Texture_Type::DEPTH:
    case Texture_Type::DEPTH_STENCIL:
    case Texture_Type::RED_F16:
    case Texture_Type::RED_GREEN_F16:
    case Texture_Type::RED_GREEN_BLUE_F16:
    case Texture_Type::RED_GREEN_BLUE_ALPHA_F16:
    case Texture_Type::RED_F32:
    case Texture_Type::RED_GREEN_F32:
    case Texture_Type::RED_GREEN_BLUE_F32:
    case Texture_Type::RED_GREEN_BLUE_ALPHA_F32:
        return true;
    case Texture_Type::RED_U8:
    case Texture_Type::RED_GREEN_U8:
    case Texture_Type::RED_GREEN_BLUE_U8:
    case Texture_Type::RED_GREEN_BLUE_ALPHA_U8:
        return false;
    }
    panic("Should not happen!");
    return false;
}

bool texture_type_is_int(Texture_Type type) {
    return !texture_type_is_float(type);
}

int texture_type_channel_count(Texture_Type type) 
{
    switch (type)
    {
    case Texture_Type::RED_U8:
    case Texture_Type::RED_F16:
    case Texture_Type::RED_F32:
    case Texture_Type::DEPTH:
    case Texture_Type::DEPTH_STENCIL:
        return 1;
    case Texture_Type::RED_GREEN_U8:
    case Texture_Type::RED_GREEN_F16:
    case Texture_Type::RED_GREEN_F32:
        return 2;
    case Texture_Type::RED_GREEN_BLUE_U8:
    case Texture_Type::RED_GREEN_BLUE_F16:
    case Texture_Type::RED_GREEN_BLUE_F32:
        return 3;
    case Texture_Type::RED_GREEN_BLUE_ALPHA_U8:
    case Texture_Type::RED_GREEN_BLUE_ALPHA_F16:
    case Texture_Type::RED_GREEN_BLUE_ALPHA_F32:
        return 4;
    }
    panic("Should not happen!");
    return 1;
}

int texture_type_pixel_byte_size(Texture_Type type)
{
    switch (type)
    {
    case Texture_Type::DEPTH:
    case Texture_Type::DEPTH_STENCIL: return 4;
    case Texture_Type::RED_F16: return 2 * 1;
    case Texture_Type::RED_GREEN_F16: return 2 * 2;
    case Texture_Type::RED_GREEN_BLUE_F16: return 2 * 3;
    case Texture_Type::RED_GREEN_BLUE_ALPHA_F16: return 2 * 4;
    case Texture_Type::RED_F32: return 4 * 1;
    case Texture_Type::RED_GREEN_F32: return 4 * 2;
    case Texture_Type::RED_GREEN_BLUE_F32: return 4 * 3;
    case Texture_Type::RED_GREEN_BLUE_ALPHA_F32: return 4 * 4;
    case Texture_Type::RED_U8: return 1;
    case Texture_Type::RED_GREEN_U8: return 2;
    case Texture_Type::RED_GREEN_BLUE_U8: return 3;
    case Texture_Type::RED_GREEN_BLUE_ALPHA_U8: return 4;
    }
    panic("Should not happen!");
    return false;
}

Sampling_Mode sampling_mode_make(Texture_Minification_Mode min_mode, Texture_Magnification_Mode mag_mode,
    Texture_Wrapping_Mode u_wrapping, Texture_Wrapping_Mode v_wrapping)
{
    Sampling_Mode result;
    result.magnification = mag_mode;
    result.minification = min_mode;
    result.u_wrapping = u_wrapping;
    result.v_wrapping = v_wrapping;
    return result;
}

Sampling_Mode sampling_mode_nearest()
{
    return sampling_mode_make(
        Texture_Minification_Mode::NEAREST_PIXEL_VALUE,
        Texture_Magnification_Mode::NEAREST_PIXEL_VALUE,
        Texture_Wrapping_Mode::CLAMP_TO_EDGE,
        Texture_Wrapping_Mode::CLAMP_TO_EDGE
    );
}

Sampling_Mode sampling_mode_bilinear()
{
    return sampling_mode_make(
        Texture_Minification_Mode::BILINEAR_INTERPOLATION,
        Texture_Magnification_Mode::BILINEAR_INTERPOLATION,
        Texture_Wrapping_Mode::CLAMP_TO_EDGE,
        Texture_Wrapping_Mode::CLAMP_TO_EDGE
    );
}

Sampling_Mode sampling_mode_trilinear()
{
    return sampling_mode_make(
        Texture_Minification_Mode::TRILINEAR_INTERPOLATION,
        Texture_Magnification_Mode::BILINEAR_INTERPOLATION,
        Texture_Wrapping_Mode::CLAMP_TO_EDGE,
        Texture_Wrapping_Mode::CLAMP_TO_EDGE
    );
}



// OPENGL STATE
OpenGL_State opengl_state_create()
{
    OpenGL_State result;
    result.active_program = 0;
    result.active_vao = 0;
    result.clear_color = vec4(0.0f);
    result.active_framebuffer = 0;
    result.texture2D_binding = 0;

    // Initialize bitmap unit tracking
    int texture_unit_count;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &texture_unit_count);
    texture_unit_count = math_minimum(2048, texture_unit_count);
    result.texture_unit_bindings = array_create<Texture_Unit_Binding>(texture_unit_count);
    for (int i = 0; i < texture_unit_count; i++) {
        auto& binding = result.texture_unit_bindings[i];
        binding.bound_texture_id = 0;
        binding.sampling_mode = sampling_mode_bilinear();
    }
    result.highest_used_texture_unit = -1;
    result.next_free_texture_unit = 1; // Note: start with 1, since 0 is always used for normal buffer bindings
    //logg("Texture unit count combined: %d\n", texture_unit_count);

    return result;
}

void opengl_state_destroy(OpenGL_State* state) {
    array_destroy(&state->texture_unit_bindings);
}

void opengl_state_set_clear_color(vec4 clear_color)
{
    auto state = &rendering_core.opengl_state;
    if (state->clear_color.x != clear_color.x ||
        state->clear_color.y != clear_color.y ||
        state->clear_color.z != clear_color.z ||
        state->clear_color.w != clear_color.w
        ) {
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    }
}

void opengl_state_bind_program(GLuint program_id) 
{
    auto state = &rendering_core.opengl_state;
    if (state->active_program != program_id) {
        glUseProgram(program_id);
        state->active_program = program_id;
    }
}

void opengl_state_bind_vao(GLuint vao) {
    auto state = &rendering_core.opengl_state;
    if (vao != state->active_vao) {
        glBindVertexArray(vao);
        state->active_vao = vao;
    }
}

void opengl_state_bind_framebuffer(GLuint framebuffer)
{
    auto state = &rendering_core.opengl_state;
    if (state->active_framebuffer != framebuffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        state->active_framebuffer = framebuffer;
    }
}

void opengl_state_bind_texture(Texture_Binding_Type binding_target, GLuint texture_id)
{
    auto& state = rendering_core.opengl_state;
    if (binding_target == Texture_Binding_Type::TEXTURE_2D) {
        if (state.texture2D_binding == texture_id) {
            return;
        }
        state.texture2D_binding = texture_id;
    }
    glActiveTexture(GL_TEXTURE0); 
    glBindTexture((GLenum)binding_target, texture_id);
}

GLint opengl_state_bind_texture_to_next_free_unit(Texture_Binding_Type binding_target, GLuint texture_id, Sampling_Mode sampling_mode)
{
    auto state = &rendering_core.opengl_state;

    Texture_Unit_Binding new_binding;
    new_binding.bound_texture_id = texture_id;
    new_binding.sampling_mode = sampling_mode;

    // Check if bitmap is already bound to a texture_unit
    {
        auto compare_binding = [](Texture_Unit_Binding& a, Texture_Unit_Binding& b) -> bool {
            return
                a.bound_texture_id == b.bound_texture_id &&
                a.sampling_mode.magnification == b.sampling_mode.magnification &&
                a.sampling_mode.minification == b.sampling_mode.minification &&
                a.sampling_mode.u_wrapping == b.sampling_mode.u_wrapping &&
                a.sampling_mode.v_wrapping == b.sampling_mode.v_wrapping;
        };

        for (int i = 0; i <= state->highest_used_texture_unit; i++) {
            if (compare_binding(new_binding, state->texture_unit_bindings.data[i])) {
                return i;
            }
        }
    }

    // Find next free unit and book-keeping
    GLint index = state->next_free_texture_unit;
    state->next_free_texture_unit += 1;
    if (state->next_free_texture_unit > state->texture_unit_bindings.size) {
        state->next_free_texture_unit = 1; // Loop around once we've hit the maximum
    }
    state->highest_used_texture_unit = math_maximum(state->highest_used_texture_unit, index);
    state->texture_unit_bindings.data[index] = new_binding;
    if (binding_target == Texture_Binding_Type::TEXTURE_2D) {
        state->texture2D_binding = texture_id;
    }

    // Bind
    glActiveTexture(GL_TEXTURE0 + index);
    glBindTexture((GLenum)binding_target, texture_id);

    glTexParameteri((GLenum)binding_target, GL_TEXTURE_MIN_FILTER, (GLenum)sampling_mode.minification);
    glTexParameteri((GLenum)binding_target, GL_TEXTURE_MAG_FILTER, (GLenum)sampling_mode.magnification);
    glTexParameteri((GLenum)binding_target, GL_TEXTURE_WRAP_S, (GLenum)sampling_mode.u_wrapping);
    glTexParameteri((GLenum)binding_target, GL_TEXTURE_WRAP_T, (GLenum)sampling_mode.v_wrapping);

    return index;
}



// PIPELINE STATE
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

Pipeline_State pipeline_state_make_alpha_blending(Depth_Test_Type depth_test_type)
{
    Blending_State blend_state;
    blend_state.blending_enabled = true;
    blend_state.custom_color = vec4(0.0f);
    blend_state.source = Blend_Operand::SOURCE_ALPHA;
    blend_state.destination = Blend_Operand::ONE_MINUS_SOURCE_ALPHA;
    blend_state.equation = Blend_Equation::ADDITION;

    Face_Culling_State culling_state;
    culling_state.culling_enabled = false;
    culling_state.cull_mode = Face_Culling_Mode::CULL_BACKFACE;
    culling_state.front_face_definition = Front_Face_Defintion::COUNTER_CLOCKWISE;

    Depth_Test_State depth_test_state;
    depth_test_state.test_type = depth_test_type;
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

void pipeline_state_switch(Pipeline_State current_state, Pipeline_State new_state)
{
    {
        Blending_State* current = &current_state.blending_state;
        Blending_State* updated = &new_state.blending_state;
        if (current->blending_enabled != updated->blending_enabled) {
            if (updated->blending_enabled) {
                glEnable(GL_BLEND);
            }
            else {
                glDisable(GL_BLEND);
            }
        }

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

    {
        Face_Culling_State* current = &current_state.culling_state;
        Face_Culling_State* updated = &new_state.culling_state;
        if (current->culling_enabled != updated->culling_enabled) {
            if (updated->culling_enabled) {
                glEnable(GL_CULL_FACE);
            }
            else {
                glDisable(GL_CULL_FACE);
            }
        }

        if (current->cull_mode != updated->cull_mode) {
            glCullFace((GLenum)updated->cull_mode);
        }
        if (current->front_face_definition != updated->front_face_definition) {
            glFrontFace((GLenum)updated->front_face_definition);
        }
    }

    {
        Depth_Test_State* current = &current_state.depth_state;
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
        Polygon_Filling_Mode current = current_state.polygon_filling_mode;
        Polygon_Filling_Mode updated = new_state.polygon_filling_mode;
        if (current != updated) {
            glPolygonMode(GL_FRONT_AND_BACK, (GLenum)updated);
        }
    }
}
