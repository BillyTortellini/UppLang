#include "render_rework.hpp"

#include "../../utility/utils.hpp"
#include "../../win32/timing.hpp"
#include "../../win32/window.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../rendering/mesh_utils.hpp"
#include "../../rendering/shader_program.hpp"
#include "../../rendering/camera_controllers.hpp"

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
    auto mesh = rendering_core_query_mesh(
        "mesh", vertex_description_create({ core.predefined.position2D }), Mesh_Topology::TRIANGLES, false
    );
    mesh_push_attribute(
        mesh, core.predefined.position2D, {
            vec2(-0.5f, -0.5f),
            vec2(0.5f, -0.5f),
            vec2(0.0f, 0.5f),
        }
    );

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
            auto circle = rendering_core_query_mesh(
                "circleMesh", vertex_description_create({ core.predefined.position2D }), Mesh_Topology::TRIANGLES, true
            );
            vec2 offset(-0.5f, 0.5f);
            float radius = 0.3;
            int division = 16;
            for (int i = 0; i < division; i++) {
                mesh_push_attribute(
                    circle, core.predefined.position2D, {
                        vec2(0),
                        vec2(math_cosine(2 * PI / division * i), math_sine(2 * PI / division * i)) * radius,
                        vec2(math_cosine(2 * PI / division * (i + 1)), math_sine(2 * PI / division * (i + 1))) * radius,
                    }
                );
            }

            auto shader = rendering_core_query_shader("resources/shaders/test.glsl");
            render_pass_draw(core.main_pass, shader, mesh, {uniform_make("offset", vec2(0)), uniform_make("scale", 1.0f)});
            render_pass_draw(core.main_pass, shader, circle, {uniform_make("offset", vec2(-.5f, .5f)), uniform_make("scale", 0.3f)});

            rendering_core_render(
                camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH, timer_current_time_in_seconds(&timer), window_state->width, window_state->height
            );
            window_swap_buffers(window);
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