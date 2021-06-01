#pragma once

#include "../../rendering/shader_program.hpp"
#include "../../rendering/gpu_buffers.hpp"
#include "../../rendering/cameras.hpp"
#include "../../math/umath.hpp"
#include "../../win32/input.hpp"
#include "../../win32/window.hpp"

// Lets see what it takes to render a triangle
struct Test_Renderer
{
    Shader_Program* shader;
    Mesh_GPU_Buffer mesh;
};

Test_Renderer test_renderer_create(Rendering_Core* core, Camera_3D* camera);
void test_renderer_destroy(Test_Renderer* renderer);
void test_renderer_update(Test_Renderer* renderer, Input* input);
void test_renderer_render(Test_Renderer* renderer, Rendering_Core* core);



// Experimental, how I think i want it to be
struct Framebuffer
{
    int width, height;
    bool resize_with_window;
    bool has_depth;
    bool has_stencil;
};

struct Uniform_Data
{
    int uniform_location;
    int type;
    union
    {
        float f;
        vec2 v2;
        vec3 v3;
        vec4 v4;
    };
};

struct Draw_Call
{
    int mesh_data;
    int draw_call_type;
    Dynamic_Array<Uniform_Data> per_object_uniforms;
};

struct Render_Pass;
struct Render_Subpass
{
    Render_Pass* parent;
    int pipeline_state;
    Shader_Program* program;
    Dynamic_Array<Uniform_Data> per_frame_uniforms;
    Dynamic_Array<Draw_Call> draw_calls;
};

struct Render_Pass
{
    Framebuffer render_target;
    bool clear_target_before_draw;
    Dynamic_Array<Render_Subpass> subpass;
};