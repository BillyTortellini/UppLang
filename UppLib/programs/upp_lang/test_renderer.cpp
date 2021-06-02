#include "Test_Renderer.hpp"

#include "../../rendering/mesh_utils.hpp"

Test_Renderer test_renderer_create(Rendering_Core* core, Camera_3D* camera)
{
    Test_Renderer result;

    // Mesh creation
    {
        vec3 positions[] = {
            vec3(-0.5f, -0.5f, 0.0f),
            vec3(0.5f, -0.5f, 0.0f),
            vec3(0.0f, 0.5f, 0.0f),
        };
        u32 indices[] = {
            0, 1, 2,
        };
        Vertex_Attribute attributes[] = {
            vertex_attribute_make(Vertex_Attribute_Type::POSITION_3D)
        };
        result.mesh = mesh_gpu_buffer_create_with_single_vertex_buffer(
            core,
            gpu_buffer_create(array_as_bytes(&array_create_static(positions, 3)), GPU_Buffer_Type::VERTEX_BUFFER, GPU_Buffer_Usage::STATIC),
            array_create_static(attributes, 1),
            gpu_buffer_create(array_as_bytes(&array_create_static(indices, 3)), GPU_Buffer_Type::INDEX_BUFFER, GPU_Buffer_Usage::STATIC),
            Mesh_Topology::TRIANGLES,
            3
        );
    }
    result.quad_mesh = mesh_utils_create_quad_2D(core);
    // Shader creation
    result.shader = shader_program_create(core, "resources/shaders/test.glsl");
    result.render_to_texture_shader = shader_program_create(core, "resources/shaders/upp_lang/background.glsl");
    result.target_framebuffer = framebuffer_create_fullscreen(core, Framebuffer_Depth_Stencil_State::NO_DEPTH);
    framebuffer_add_color_attachment(
        result.target_framebuffer, core, 0,
        texture_2D_create_empty(
            core, 
            Texture_2D_Type::RED_GREEN_BLUE_U8, 
            core->render_information.window_width, 
            core->render_information.window_height,
            texture_sampling_mode_make_bilinear()
        ),
        true
    );
    result.texture_pass = render_pass_create(result.target_framebuffer, pipeline_state_make_default(), true, true, true);
    result.window_pass = render_pass_create(0, pipeline_state_make_default(), true, true, true);
    return result;
}

void test_renderer_destroy(Test_Renderer* renderer, Rendering_Core* core)
{
    shader_program_destroy(renderer->shader);
    shader_program_destroy(renderer->render_to_texture_shader);
    framebuffer_destroy(renderer->target_framebuffer, core);
    mesh_gpu_buffer_destroy(&renderer->mesh);
    mesh_gpu_buffer_destroy(&renderer->quad_mesh);
    render_pass_destroy(renderer->texture_pass);
    render_pass_destroy(renderer->window_pass);
}

void test_renderer_update(Test_Renderer* renderer, Input* input)
{
}

void test_renderer_render(Test_Renderer* renderer, Rendering_Core* core)
{
    render_pass_add_draw_call(renderer->texture_pass, renderer->render_to_texture_shader, &renderer->quad_mesh);
    shader_program_set_uniform_texture_2D(renderer->shader, "texture_fb", renderer->target_framebuffer->color_attachments[0].texture);
    render_pass_add_draw_call(renderer->window_pass, renderer->shader, &renderer->mesh);

    render_pass_execute(renderer->texture_pass, core);
    render_pass_execute(renderer->window_pass, core);
}