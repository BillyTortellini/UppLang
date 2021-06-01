#include "Test_Renderer.hpp"

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
    // Shader creation
    result.shader = shader_program_create(core, "resources/shaders/test.glsl");
    return result;
}

void test_renderer_destroy(Test_Renderer* renderer)
{
    shader_program_destroy(renderer->shader);
    mesh_gpu_buffer_destroy(&renderer->mesh);
}

void test_renderer_update(Test_Renderer* renderer, Input* input)
{
}

void test_renderer_render(Test_Renderer* renderer, Rendering_Core* core)
{
    mesh_gpu_buffer_draw_with_shader_program(&renderer->mesh, renderer->shader, core);
}