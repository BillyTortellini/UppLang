#include "render_rework.hpp"

#include "../../utility/utils.hpp"
#include "../../win32/timing.hpp"
#include "../../win32/window.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../rendering/texture.hpp"
#include "../../rendering/texture_bitmap.hpp"
#include "../../rendering/camera_controllers.hpp"
#include "../../utility/random.hpp"
#include "../../rendering/framebuffer.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/renderer_2d.hpp"

void render_rework()
{
    Window* window = window_create("Test", 0);
    SCOPE_EXIT(window_destroy(window));
    Window_State* window_state = window_get_window_state(window);
    rendering_core_initialize(window_state->width, window_state->height, window_state->dpi);
    SCOPE_EXIT(rendering_core_destroy());

    Timer timer = timer_make();

    Camera_3D* camera = camera_3D_create(math_degree_to_radians(90), 0.1f, 100.0f);
    SCOPE_EXIT(camera_3D_destroy(camera));
    Camera_Controller_Arcball camera_controller_arcball;
    {
        window_set_cursor_constrain(window, false);
        window_set_cursor_visibility(window, true);
        window_set_cursor_reset_into_center(window, false);
        camera_controller_arcball = camera_controller_arcball_make(vec3(0.0f), 2.0f);
        camera->position = vec3(0, 0, 1.0f);
    }

    // Set Window/Rendering Options
    {
        window_load_position(window, "window_pos.set");
        window_set_vsync(window, true);
        opengl_state_set_clear_color(vec4(0.0f));
    }
    Pipeline_State pipeline_state = pipeline_state_make_default();
    pipeline_state.blending_state.blending_enabled = true;
    rendering_core_update_pipeline_state(pipeline_state);

    // Mesh_GPU_Buffer mesh_quad = mesh_utils_create_quad_2D();
    // SCOPE_EXIT(mesh_gpu_buffer_destroy(&mesh_quad));

    // Shader_Program* background_shader = shader_program_create({ "resources/shaders/upp_lang/background.glsl" });
    // SCOPE_EXIT(shader_program_destroy(background_shader));

    auto& core = rendering_core;
    auto mesh = rendering_core_query_mesh("mesh", vertex_description_create({ core.predefined.position2D }), false);
    mesh_push_attribute(
        mesh, core.predefined.position2D, {
            vec2(-0.5f, -0.5f),
            vec2(0.5f, -0.5f),
            vec2(0.0f, 0.5f),
        }
    );

    auto quad_mesh = rendering_core_query_mesh("quad", mesh->description, false);
    mesh_push_attribute(
        quad_mesh, core.predefined.position2D, {
            vec2(-1.f, -1.0f),
            vec2(1.0f, -1.0f),
            vec2(1.0f, 1.0f),
            vec2(-1.0f, -1.0f),
            vec2(1.0f, 1.0f),
            vec2(-1.0f, 1.0f),
        }
    );

    Texture_Bitmap bitmap = texture_bitmap_create_test_bitmap(64);
    Texture* texture = texture_create_from_texture_bitmap(&bitmap, false);
    Texture_Bitmap bitmap2 = texture_bitmap_create_empty(32, 32, 3);
    auto random = random_make_time_initalized();
    for (int i = 0; i < 32 * 32* 3; i+=3) {
        bitmap2.data[i + 0] = (byte) random_next_u32(&random);
        bitmap2.data[i + 1] = (byte) random_next_u32(&random);
        bitmap2.data[i + 2] = (byte) random_next_u32(&random);
    }
    Texture* texture2 = texture_create_from_texture_bitmap(&bitmap2, false);

    Text_Renderer* text_renderer = text_renderer_create_from_font_atlas_file("resources/fonts/glyph_atlas.atlas");
    SCOPE_EXIT(text_renderer_destroy(text_renderer));

    Renderer_2D* renderer_2D = renderer_2D_create(text_renderer);
    SCOPE_EXIT(renderer_2D_destroy(renderer_2D));

    // Window Loop
    double time_last_update_start = timer_current_time_in_seconds(&timer);
    while (true)
    {
        double time_frame_start = timer_current_time_in_seconds(&timer);
        float time_since_last_update = (float)(time_frame_start - time_last_update_start);
        time_last_update_start = time_frame_start;

        Input* input = window_get_input(window);
        {
            // Input and Logic
            if (!window_handle_messages(window, true)) {
                break;
            }
            if (input->close_request_issued || input->key_pressed[(int)Key_Code::ESCAPE]) {
                window_save_position(window, "window_pos.set");
                window_close(window);
                break;
            }
            if (input->key_pressed[(int)Key_Code::F11]) {
                Window_State* state = window_get_window_state(window);
                window_set_fullscreen(window, !state->fullscreen);
            }

            camera_controller_arcball_update(&camera_controller_arcball, camera, input, window_state->width, window_state->height);
            input_reset(input); // Clear input for next frame
        }

        double time_input_end = timer_current_time_in_seconds(&timer);

        // Rendering
        {
            rendering_core_prepare_frame(timer_current_time_in_seconds(&timer), window_state->width, window_state->height);
            SCOPE_EXIT(
                renderer_2D_reset(renderer_2D);
                text_renderer_reset(text_renderer);
                rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
                window_swap_buffers(window);
            );

            auto main_pass = core.predefined.main_pass;
            auto circle1 = rendering_core_query_mesh(
                "circleMesh1", vertex_description_create({ core.predefined.position2D }), true
            );
            auto circle2 = rendering_core_query_mesh(
                "circleMesh2", vertex_description_create({ core.predefined.position2D, core.predefined.index, core.predefined.texture_coordinates }), true
            );

            // Generate circle data
            {
                float radius = 0.3f;
                int division = 16;
                auto time = rendering_core.render_information.current_time_in_seconds;
                vec2 offset = vec2(math_cosine(time), math_sine(time));
                for (int i = 0; i < division; i++) {
                    mesh_push_attribute(
                        circle1, core.predefined.position2D, {
                            offset + vec2(0),
                            offset + vec2(math_cosine(2 * PI / division * i), math_sine(2 * PI / division * i)) * radius,
                            offset + vec2(math_cosine(2 * PI / division * (i + 1)), math_sine(2 * PI / division * (i + 1))) * radius,
                        }
                    );
                }
                
                time += PI;
                offset = vec2(math_cosine(time), math_sine(time));

                auto indices = mesh_push_attribute_slice(circle2, core.predefined.index, division * 3);
                auto positions = mesh_push_attribute_slice(circle2, core.predefined.position2D, division * 3);
                auto uvs = mesh_push_attribute_slice(circle2, core.predefined.texture_coordinates, division * 3);
                for (int i = 0; i < division; i++) {
                    int k = i * 3;
                    indices[k + 0] = k + 0;
                    indices[k + 1] = k + 1;
                    indices[k + 2] = k + 2;
                    positions[k + 0] = offset + vec2(0);
                    positions[k + 1] = offset + vec2(math_cosine(2 * PI / division * k), math_sine(2 * PI / division * k)) * radius;
                    positions[k + 2] = offset + vec2(math_cosine(2 * PI / division * (k + 1)), math_sine(2 * PI / division * (k + 1))) * radius;
                    uvs[k + 0] = vec2(0);
                    uvs[k + 1] = vec2(math_cosine(2 * PI / division * k), math_sine(2 * PI / division * k)) * radius;
                    uvs[k + 2] = vec2(math_cosine(2 * PI / division * (k + 1)), math_sine(2 * PI / division * (k + 1))) * radius;
                }

                logg("What\n");
            }

            auto const_color_2d = rendering_core_query_shader("const_color_2D.glsl");
            //render_pass_draw(main_pass, const_color_2d, circle1, Mesh_Topology::TRIANGLES, { uniform_make("u_color", vec4(1.0f)) });
            render_pass_draw(main_pass, const_color_2d, circle2, Mesh_Topology::TRIANGLES, { uniform_make("u_color", vec4(1.0f, 0.0f, 0.0f, 1.0f)) });
            //auto color_shader_2d = shader_generator_make({ predefined.position2D, predefined.uniform_color }, );

            //auto bg_buffer = rendering_core_query_framebuffer("bg_buffer", Texture_Type::RED_GREEN_BLUE_U8, Depth_Type::NO_DEPTH, 512, 512);
            //{
            //    auto bg_pass = rendering_core_query_renderpass("bg_pass", pipeline_state_make_default(), bg_buffer);
            //    render_pass_add_dependency(main_pass, bg_pass);
            //    auto bg_shader = rendering_core_query_shader("upp_lang/background.glsl");
            //    render_pass_draw(bg_pass, bg_shader, quad_mesh, Mesh_Topology::TRIANGLES, {});
            //}

            //{
            //    auto shader = rendering_core_query_shader("test.glsl");
            //    render_pass_set_uniforms(main_pass, shader, { uniform_make("image", bg_buffer->color_texture, sampling_mode_bilinear()) });
            //    render_pass_draw(main_pass, shader, mesh, Mesh_Topology::TRIANGLES, { uniform_make("offset", vec2(0)), uniform_make("scale", 1.0f) });
            //}


            auto text_pass_state = pipeline_state_make_default();
            text_pass_state.blending_state.blending_enabled = true;
            text_pass_state.blending_state.source = Blend_Operand::SOURCE_ALPHA;
            text_pass_state.blending_state.destination = Blend_Operand::ONE_MINUS_SOURCE_ALPHA;
            text_pass_state.blending_state.equation = Blend_Equation::ADDITION;
            text_pass_state.depth_state.test_type = Depth_Test_Type::IGNORE_DEPTH;
            auto text_pass = rendering_core_query_renderpass("Text pass", text_pass_state, 0);
            render_pass_add_dependency(text_pass, main_pass);

            text_renderer_add_text(text_renderer, &string_create_static("H"), vec2(0.0f), 0.1f, 0.0f);
            text_renderer_draw(text_renderer, text_pass);
            text_renderer_add_text(text_renderer, &string_create_static("e"), vec2(0.0f, -0.3f), 0.03f, 0.0f);
            text_renderer_draw(text_renderer, text_pass);

            //renderer_2D_add_rectangle(renderer_2D, vec2(0.0f), vec2(0.2, 0.3f), vec3(1.0f), 0.0f);
            renderer_2D_draw(renderer_2D, text_pass);
        }

        double time_render_end = timer_current_time_in_seconds(&timer);

        // Sleep
        {
            double time_calculations = timer_current_time_in_seconds(&timer) - time_frame_start;
            /*
            logg("FRAME_TIMING:\n---------------\n");
            logg("input        ... %3.2fms\n", 1000.0f * (float)(time_input_end - time_frame_start));
            logg("render       ... %3.2fms\n", 1000.0f * (float)(time_render_end - time_input_end));
            logg("TSLF: %3.2fms, calculation time: %3.2fms\n", time_since_last_update*1000, time_calculations*1000);
            */

            // Sleep
            const int TARGET_FPS = 60;
            const double SECONDS_PER_FRAME = 1.0 / TARGET_FPS;
            timer_sleep_until(&timer, time_frame_start + SECONDS_PER_FRAME);
        }
    }
}