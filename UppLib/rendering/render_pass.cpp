#include "render_pass.hpp"

#include "framebuffer.hpp"

Draw_Call draw_call_create()
{
    Draw_Call result;
    result.uniform_values = dynamic_array_create_empty<Uniform_Value>(8);
    return result;
}

void draw_call_destroy(Draw_Call* draw_call) {
    dynamic_array_destroy(&draw_call->uniform_values);
}

Render_Pass* render_pass_create(Framebuffer* render_target, Pipeline_State pipeline_state, bool clear_color, bool clear_depth, bool clear_stencil)
{
    Render_Pass* result = new Render_Pass();
    result->clear_color= clear_color;
    result->clear_depth= clear_depth;
    result->clear_stencil= clear_stencil;
    result->pipeline_state = pipeline_state;
    result->render_target = render_target;
    result->draw_calls = dynamic_array_create_empty<Draw_Call>(32);
    result->draw_calls_cache = dynamic_array_create_empty<Draw_Call>(32);
    return result;
}

void render_pass_destroy(Render_Pass* render_pass)
{
    for (int i = 0; i < render_pass->draw_calls.size; i++) {
        draw_call_destroy(&render_pass->draw_calls[i]);
    }
    for (int i = 0; i < render_pass->draw_calls_cache.size; i++) {
        draw_call_destroy(&render_pass->draw_calls_cache[i]);
    }
    dynamic_array_destroy(&render_pass->draw_calls);
    dynamic_array_destroy(&render_pass->draw_calls_cache);
}

void render_pass_add_draw_call(Render_Pass* render_pass, Shader_Program* shader, Mesh_GPU_Buffer* mesh, std::initializer_list<Uniform_Value> uniforms)
{
    Draw_Call draw_call;
    if (render_pass->draw_calls_cache.size != 0) {
        draw_call = render_pass->draw_calls_cache[render_pass->draw_calls_cache.size - 1];
        dynamic_array_swap_remove(&render_pass->draw_calls_cache, render_pass->draw_calls_cache.size - 1);
        dynamic_array_reset(&draw_call.uniform_values);
    }
    else {
        draw_call = draw_call_create();
    }
    draw_call.draw_call_type = Draw_Call_Type::SINGLE_DRAW;
    draw_call.instance_count = 0;
    draw_call.mesh = mesh;
    draw_call.shader = shader;
    for (auto& uniform : uniforms) {
        dynamic_array_push_back(&draw_call.uniform_values, uniform);
    }
    dynamic_array_push_back(&render_pass->draw_calls, draw_call);
}

void render_pass_add_draw_call_instanced(
    Render_Pass* render_pass, Shader_Program* shader, Mesh_GPU_Buffer* mesh, std::initializer_list<Uniform_Value> uniforms, int instance_count
)
{
    Draw_Call draw_call;
    if (render_pass->draw_calls_cache.size != 0) {
        draw_call = render_pass->draw_calls_cache[render_pass->draw_calls_cache.size - 1];
        dynamic_array_swap_remove(&render_pass->draw_calls_cache, render_pass->draw_calls_cache.size - 1);
        dynamic_array_reset(&draw_call.uniform_values);
    }
    else {
        draw_call = draw_call_create();
    }
    draw_call.draw_call_type = Draw_Call_Type::INSTANCED_DRAW;
    draw_call.instance_count = 0;
    draw_call.mesh = mesh;
    draw_call.shader = shader;
    for (auto& uniform : uniforms) {
        dynamic_array_push_back(&draw_call.uniform_values, uniform);
    }
    dynamic_array_push_back(&render_pass->draw_calls, draw_call);
}

void render_pass_execute(Render_Pass* render_pass, Rendering_Core* core)
{
    rendering_core_update_pipeline_state(core, render_pass->pipeline_state);
    if (render_pass->render_target == 0) {
        rendering_core_update_viewport(core, core->render_information.window_width, core->render_information.window_height);
        opengl_state_bind_framebuffer(&core->opengl_state, 0);
    }
    else {
        rendering_core_update_viewport(core, render_pass->render_target->width, render_pass->render_target->height);
        opengl_state_bind_framebuffer(&core->opengl_state, render_pass->render_target->framebuffer_id);
    }
    GLbitfield flags = 0;
    if (render_pass->clear_color) {
        flags = flags | GL_COLOR_BUFFER_BIT;
    }
    if (render_pass->clear_depth) {
        flags = flags | GL_DEPTH_BUFFER_BIT;
    }
    if (render_pass->clear_stencil) {
        flags = flags | GL_STENCIL_BUFFER_BIT;
    }
    if (flags != 0) {
        glClear(flags);
    }

    for (int i = 0; i < render_pass->draw_calls.size; i++)
    {
        Draw_Call* call = &render_pass->draw_calls[i];
        for (int j = 0; j < call->uniform_values.size; j++) {
            shader_program_set_uniform_value(call->shader, call->uniform_values[j], core);
        }
        dynamic_array_reset(&call->uniform_values);

        if (call->draw_call_type == Draw_Call_Type::SINGLE_DRAW) {
            shader_program_draw_mesh(call->shader, call->mesh, core, {});
        }
        else {
            shader_program_draw_mesh_instanced(call->shader, call->mesh, call->instance_count, core, {});
        }
        dynamic_array_push_back(&render_pass->draw_calls_cache, *call);
    }
    dynamic_array_reset(&render_pass->draw_calls);
}

