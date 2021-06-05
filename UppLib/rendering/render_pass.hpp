#pragma once

#include "../datastructures/dynamic_array.hpp"
#include "shader_program.hpp"
#include "rendering_core.hpp"

struct Mesh_GPU_Buffer;
struct Framebuffer;

enum class Draw_Call_Type
{
    SINGLE_DRAW,
    INSTANCED_DRAW,
};

struct Draw_Call
{
    Draw_Call_Type draw_call_type;
    int instance_count;
    Mesh_GPU_Buffer* mesh;
    Shader_Program* shader;
    Dynamic_Array<Uniform_Value> uniform_values;
};

Draw_Call draw_call_create();
void draw_call_destroy(Draw_Call* call);

struct Render_Pass
{
    Framebuffer* render_target; // If zero, render to default framebuffer
    Pipeline_State pipeline_state;
    bool clear_color;
    bool clear_depth;
    bool clear_stencil;
    Dynamic_Array<Draw_Call> draw_calls;
    Dynamic_Array<Draw_Call> draw_calls_cache;
};

Render_Pass* render_pass_create(Framebuffer* render_target, Pipeline_State pipeline_state, bool clear_color, bool clear_depth, bool clear_stencil);
void render_pass_destroy(Render_Pass* render_pass);

void render_pass_add_draw_call(Render_Pass* render_pass, Shader_Program* shader, Mesh_GPU_Buffer* mesh, std::initializer_list<Uniform_Value> uniforms);
void render_pass_add_draw_call_instanced(
    Render_Pass* render_pass, Shader_Program* shader, Mesh_GPU_Buffer* mesh, std::initializer_list<Uniform_Value> uniforms, int instance_count
);
void render_pass_execute(Render_Pass* render_pass, Rendering_Core* core);