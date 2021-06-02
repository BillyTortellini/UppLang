#pragma once

#include "../../rendering/shader_program.hpp"
#include "../../rendering/gpu_buffers.hpp"
#include "../../rendering/cameras.hpp"
#include "../../math/umath.hpp"
#include "../../win32/input.hpp"
#include "../../win32/window.hpp"
#include "../../rendering/texture_2D.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../rendering/render_pass.hpp"
#include "../../rendering/framebuffer.hpp"

struct Test_Renderer
{
    Shader_Program* shader;
    Shader_Program* render_to_texture_shader;
    Framebuffer* target_framebuffer;
    Mesh_GPU_Buffer mesh;
    Mesh_GPU_Buffer quad_mesh;
    Render_Pass* window_pass;
    Render_Pass* texture_pass;
};

Test_Renderer test_renderer_create(Rendering_Core* core, Camera_3D* camera);
void test_renderer_destroy(Test_Renderer* renderer, Rendering_Core* core);
void test_renderer_update(Test_Renderer* renderer, Input* input);
void test_renderer_render(Test_Renderer* renderer, Rendering_Core* core);



/*
    The goal of renderpasses is to make framebuffer bindings and pipeline state changes
    easy and transparent (Without forgetting bindings, setting viewports, clearing framebuffers, binding default buffer...)
    Also all rendering utils that don't need to know where to render the data (Text_Renderer, 2D_Renderer, GUI) can use this

    Maybe I can also setup the Renderpasses and all the subpasses in a way that I can set them up at program start, 
    then every frame the draw_calls are added, since all renderers have the subpasses they need to render, or maybe
    they are in the resource manager or whatever

    Currently I think the renderpasses should be as stable as possible, so no new renderpasses added at runtime,
    Maybe even Draw_Calls should only be created once, so if an object only needs one drawcall
    I think I want to do different things depending on the object, if i want to pass them renderpass, or a 

    Well seems like everything just gets a draw call for a subpass, and the main
    Loop determines what gets rendered to where

    But maybe I want a global renderpasses structure, where the renderpass dependencies are listed (Which renderpass reads from what texture),
    And renderpasses can be created by all types

    Use Cases:
        Draw Background
        Text_Editor gets a Text_Instance from Text_Renderer, which has a draw call.
        
         - G-Buffer Pass (Position, Normal, Albedo, Specular, Ambient, Roughness...)
         - Shadow Passes (Cascading Shadow Map)
         - Lights with Shadow pass
         - Light Volume pass
         - Volumetric Light pass
         - Post-Processing passes

    In Theory I would only need to call update on all objects, and not render, since
*/



/*
    I also think that seeing all the objects that are drawn would be nice, but for things like that
    I should just write a system specifically for that.
    I mean I could force all rendering through a render pass, collect all renderpasses in Rendering_Core,
    and then render all things in one go...

    One big reason why this may be usefull is because I dont have to manually do the busywork of 
    calling the rendering functions in the right order, when doing shadows I just have the Rendering_Pass where
    I can render into when looping over the objects

    Also grouping by shader_program may be a nice idea

    The Problem when creating a Rendering_Pipeline is that the order of Draw_Calls is crucial:

    I also want state sorting by:
        - Render_Target  ... Most expensive, and Order is impossible to change
        - Pipeline_State ... Not because it is performance sensitive, but because draw call order is given
        - Textures
        - Shaders
        - Meshes
    With shaders, you cannot really change that much

    Render_Passes could force the setting of the render_target and the pipeline state, also I could do draw call sorting and stupoi
*/

