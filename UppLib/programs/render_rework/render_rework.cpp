#include "render_rework.hpp"

#include "../../utility/utils.hpp"
#include "../../win32/timing.hpp"
#include "../../win32/window.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../rendering/texture.hpp"
#include "../../rendering/texture_bitmap.hpp"
#include "../../rendering/camera_controllers.hpp"
#include "../../utility/random.hpp"
#include "../../utility/gui.hpp"
#include "../../rendering/framebuffer.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/renderer_2d.hpp"
#include "../../rendering/basic2D.hpp"
#include <algorithm>
#include <iostream>



struct Ring_Buffer
{
    double values[120];
    int next_free;

    double average;
    double max;
    double min;
    double standard_deviation;
};

Ring_Buffer ring_buffer_make(double initial_value)
{
    Ring_Buffer result;
    for (int i = 0; i < 120; i++) {
        result.values[i] = 0;
    }
    result.next_free = 0;
    return result;
}

void ring_buffer_update_stats(Ring_Buffer& buffer)
{
    buffer.average = 0;
    buffer.max = -1000000.0;
    buffer.min = 1000000.0;
    for (int i = 0; i < 120; i++) {
        buffer.average += buffer.values[i];
        buffer.max = math_maximum(buffer.max, buffer.values[i]);
        buffer.min = math_minimum(buffer.max, buffer.values[i]);
    }
    buffer.average = buffer.average / 120.0;
    // Calculate variance
    buffer.standard_deviation = 0.0;
    for (int i = 0; i < 120; i++) {
        double diff = buffer.values[i] - buffer.average;
        buffer.standard_deviation += diff * diff;
    }
    buffer.standard_deviation = math_square_root(buffer.standard_deviation);
}

void ring_buffer_set_value(Ring_Buffer& buffer, double value) {
    buffer.values[buffer.next_free] = value;
    buffer.next_free = (buffer.next_free + 1) % 120;
}

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
        window_set_vsync(window, false);
        opengl_state_set_clear_color(vec4(0.0f));
    }

    Texture_Bitmap bitmap = texture_bitmap_create_test_bitmap(64);
    Texture* texture = texture_create_from_texture_bitmap(&bitmap, false);
    Texture_Bitmap bitmap2 = texture_bitmap_create_empty(32, 32, 3);
    auto random = random_make_time_initalized();
    for (int i = 0; i < 32 * 32 * 3; i += 3) {
        bitmap2.data[i + 0] = (byte)random_next_u32(&random);
        bitmap2.data[i + 1] = (byte)random_next_u32(&random);
        bitmap2.data[i + 2] = (byte)random_next_u32(&random);
    }
    Texture* texture2 = texture_create_from_texture_bitmap(&bitmap2, false);

    Text_Renderer* text_renderer = text_renderer_create_from_font_atlas_file("resources/fonts/glyph_atlas.atlas");
    SCOPE_EXIT(text_renderer_destroy(text_renderer));

    Renderer_2D* renderer_2D = renderer_2D_create(text_renderer);
    SCOPE_EXIT(renderer_2D_destroy(renderer_2D));

    gui_initialize(text_renderer, window);
    SCOPE_EXIT(gui_destroy());

    // Window Loop
    double game_start_time = timer_current_time_in_seconds(&timer);
    double frame_start_time = game_start_time;
    while (true)
    {
        double now = timer_current_time_in_seconds(&timer);
        double tslf = now - frame_start_time;
        frame_start_time = now;

        // Handle input
        Input* input = window_get_input(window);
        double handle_message_time;
        SCOPE_EXIT(input_reset(input));
        {
            // Input and Logic
            double now = timer_current_time_in_seconds(&timer);
            int msg_count = 0;
            if (!window_handle_messages(window, false, &msg_count)) {
                break;
            }
            handle_message_time = timer_current_time_in_seconds(&timer) - now;
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
        }

        // Rendering
        {
            rendering_core_prepare_frame(timer_current_time_in_seconds(&timer), window_state->width, window_state->height);
            now = timer_current_time_in_seconds(&timer);

            gui_push_example_gui();
            auto gui_pass = rendering_core_query_renderpass("GUI_Pass", pipeline_state_make_alpha_blending(), nullptr);
            render_pass_add_dependency(gui_pass, rendering_core.predefined.main_pass);
            gui_update_and_render(gui_pass);

            renderer_2D_reset(renderer_2D);
            text_renderer_reset(text_renderer);
        }

        rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
        window_swap_buffers(window);
        glFlush();

        // Sleep + Timing
        {
            // Sleep
            const int TARGET_FPS = 60;
            const double SECONDS_PER_FRAME = 1.0 / TARGET_FPS;
            timer_sleep_until(&timer, frame_start_time + SECONDS_PER_FRAME);
        }
    }
}