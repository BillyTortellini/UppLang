#include "game.hpp"

#include <iostream>

#include "../../upplib.hpp"
#include "../../win32/timing.hpp"

#include "../../rendering/opengl_utils.hpp"
#include "../../rendering/gpu_buffers.hpp"
#include "../../rendering/cameras.hpp"
#include "../../rendering/camera_controllers.hpp"
#include "../../rendering/texture.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../win32/window.hpp"
#include "../../win32/process.hpp"
#include "../../utility/file_io.hpp"
#include "../../utility/random.hpp"
#include "../../rendering/renderer_2d.hpp"

#include "../../math/umath.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/hashset.hpp"
#include "../../utility/hash_functions.hpp"

#include "../../datastructures/block_allocator.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "../../win32/windows_helper_functions.hpp"

void game_entry()
{
    //test_things();
    //return;

    Window* window = window_create("Test", 0);
    SCOPE_EXIT(window_destroy(window));
    Window_State* window_state = window_get_window_state(window);
    rendering_core_initialize(window_state->width, window_state->height, window_state->dpi);
    SCOPE_EXIT(rendering_core_destroy());

    GLint maxAttribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribs);
    logg("Maximum attribs: %d\n", maxAttribs);

    Timer timer = timer_make();

    Text_Renderer* text_renderer = text_renderer_create_from_font_atlas_file("resources/fonts/glyph_atlas.atlas");
    SCOPE_EXIT(text_renderer_destroy(text_renderer));

    Renderer_2D* renderer_2D = renderer_2D_create(text_renderer);
    SCOPE_EXIT(renderer_2D_destroy(renderer_2D));

    // Background
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
        //window_set_size(window, 800, 600);
        //window_set_position(window, -1234, 96);
        //window_set_fullscreen(window, true);
        window_set_vsync(window, false);

        opengl_state_set_clear_color(vec4(0.0f));
        window_set_vsync(window, true);
    }
    Pipeline_State pipeline_state = pipeline_state_make_default();
    pipeline_state.blending_state.blending_enabled = true;
    rendering_core_update_pipeline_state(pipeline_state);

    vec2 player_pos = vec2(0.0f);

    // Window Loop
    int last_animation_required_frame = -100;
    int frame = 0;
    double time_last_update_start = timer_current_time_in_seconds(&timer);
    float angle = 0.0f;
    while (true)
    {
        double time_frame_start = timer_current_time_in_seconds(&timer);
        float time_since_last_update = (float)(time_frame_start - time_last_update_start);
        time_last_update_start = time_frame_start;

        frame += 1;
        bool wait_for_messages = true;
        if (frame - last_animation_required_frame < 10) {
            wait_for_messages = false;
        }

        // Input Handling
        Input* input = window_get_input(window);
        {
            int msg_count = 0;
            if (!window_handle_messages(window, wait_for_messages, &msg_count)) {
                break;
            }
            if (msg_count > 0) { // After a window message, animated for 10 frames
                last_animation_required_frame = frame;
            }

            if (input->close_request_issued || input->key_pressed[(int)Key_Code::ESCAPE]) 
            {
                window_save_position(window, "window_pos.set");
                window_close(window);
                break;
            }
            if (input->key_pressed[(int)Key_Code::F11]) {
                Window_State* state = window_get_window_state(window);
                window_set_fullscreen(window, !state->fullscreen);
            }

            camera_controller_arcball_update(&camera_controller_arcball, camera, input, window_state->width, window_state->height);
            bool animations_running = true;
            if (animations_running) {
                last_animation_required_frame = frame;
            }

            vec2 dir = vec2(0.0f);
            if (input->key_down[(int)Key_Code::W]) {
                dir.y -= 1.0f;
            }
            if (input->key_down[(int)Key_Code::A]) {
                dir.x -= 1.0f;
            }
            if (input->key_down[(int)Key_Code::S]) {
                dir.y += 1.0f;
            }
            if (input->key_down[(int)Key_Code::D]) {
                dir.x += 1.0f;
            }

            float len = vector_length(dir);
            if (len > 0.001f) {
                dir = vector_normalize_safe(dir);
            }
            float speed = 2.0f;
            player_pos = player_pos + dir * speed * time_since_last_update;
        }

        double time_input_end = timer_current_time_in_seconds(&timer);

        // Rendering
        {
            rendering_core_prepare_frame(timer_current_time_in_seconds(&timer), window_state->width, window_state->height);
            SCOPE_EXIT(
                text_renderer_reset(text_renderer);
            renderer_2D_reset(renderer_2D);
            rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
            window_swap_buffers(window);
            );

            // Draw Background
            Shader* shader = rendering_core_query_shader("upp_lang/background.glsl");
            render_pass_draw(
                rendering_core.predefined.main_pass, shader, rendering_core.predefined.quad, Mesh_Topology::TRIANGLES, 
                { uniform_make("sphere_pos", player_pos)}
            );
        }

        input_reset(input); // Clear input for next frame
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