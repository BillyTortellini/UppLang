#include "test.hpp"

#include "../../upplib.hpp"
#include "../upp_lang/compilation_data.hpp"

#include <iostream>

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
#include "../../rendering/font_renderer.hpp"

#include "../../math/umath.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/hashset.hpp"
#include "../../utility/hash_functions.hpp"
#include "../../win32/windows_helper_functions.hpp"

void test_window();

void test_entry()
{
    test_window();
}

void test_window()
{
    timer_initialize();
    Window* window = window_create("Test", 0);
    SCOPE_EXIT(window_destroy(window));
    Window_State* window_state = window_get_window_state(window);
    rendering_core_initialize(window_state->width, window_state->height, window_state->dpi);
    SCOPE_EXIT(rendering_core_destroy());

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
        // window_load_position(window, "window_pos.set");
        // window_set_size(window, 800, 600);
        // window_set_position(window, -1234, 96);
        // window_set_fullscreen(window, true);
        // window_set_vsync(window, false);
        opengl_state_set_clear_color(vec4(0.0f));
        window_set_vsync(window, true);
    }

    Pipeline_State pipeline_state = pipeline_state_make_default();
    pipeline_state.blending_state.blending_enabled = true;
    rendering_core_update_pipeline_state(pipeline_state);

    Font_Renderer* font_renderer = Font_Renderer::create(1024);
    Raster_Font* raster_font = font_renderer->add_raster_font("resources/fonts/consola.ttf", 20);
    Raster_Font* small_font = font_renderer->add_raster_font("resources/fonts/consola.ttf", 10);
    Raster_Font* smaller = font_renderer->add_raster_font("resources/fonts/consola.ttf", 8);
    Raster_Font* larger = font_renderer->add_raster_font("resources/fonts/consola.ttf", 40);
    font_renderer->finish_atlas();

    Text_Renderer* text_renderer = text_renderer_create_from_font_atlas_file("resources/fonts/glyph_atlas.atlas");
    SCOPE_EXIT(text_renderer_destroy(text_renderer));

    // Window Loop
    double time_last_update_start = timer_current_time_in_seconds();
    window_set_focus(window);
    while (true)
    {
        double time_frame_start = timer_current_time_in_seconds();
        float time_since_last_update = (float)(time_frame_start - time_last_update_start);
        time_last_update_start = time_frame_start;

        // Input Handling
        Input* input = window_get_input(window);
        input_reset(input); // Clear input for new messages
        {
            int msg_count = 0;
            if (!window_handle_messages(window, false, &msg_count)) {
                break;
            }

            if (input->close_request_issued || 
                (input->key_pressed[(int)Key_Code::ESCAPE] && (input->key_down[(int)Key_Code::SHIFT] || input->key_down[(int) Key_Code::CTRL]))) 
            {
                window_close(window);
                break;
            }
            if (input->key_pressed[(int)Key_Code::F11]) {
                Window_State* state = window_get_window_state(window);
                window_set_fullscreen(window, !state->fullscreen);
            }
            camera_controller_arcball_update(&camera_controller_arcball, camera, input, window_state->width, window_state->height);
        }

        // Rendering
        {
            rendering_core_prepare_frame(timer_current_time_in_seconds(), window_state->width, window_state->height);
            SCOPE_EXIT(
                rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
                window_swap_buffers(window);
            );

            Render_Pass* pass = rendering_core_query_renderpass("Text pass", pipeline_state_make_alpha_blending(), nullptr);
            ivec2 size = ivec2(rendering_core.render_information.backbuffer_width, rendering_core.render_information.backbuffer_height);
            font_renderer->reset();

            ivec2 pos = size / 2;
            raster_font->push_line(pos, string_create_static("Hello world!"), vec3(1, 0, 0));
            pos.y += raster_font->char_size.y;
            raster_font->push_line(pos, string_create_static("Another one ppyyy||TEHS"), vec3(0, 0, 1));
            pos.y += raster_font->char_size.y;
            small_font->push_line(pos, string_create_static("This is the yssmall Font here"), vec3(1));
            pos.y += raster_font->char_size.y;
            smaller->push_line(pos, string_create_static("Well I don't even know"), vec3(1));
            pos.y += larger->char_size.y;
            larger->push_line(pos, string_create_static("Larger fondü"), vec3(1));

            font_renderer->add_draw_call(pass);
        }

        // Sleep
        {
            const int TARGET_FPS = 60;
            const double SECONDS_PER_FRAME = 1.0 / TARGET_FPS;
            timer_sleep_until(time_frame_start + SECONDS_PER_FRAME);
        }
    }
}
