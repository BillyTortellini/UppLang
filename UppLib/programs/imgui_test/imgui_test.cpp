#include "imgui_test.hpp"

#include <iostream>

#include "../../win32/timing.hpp"
#include "Windows.h"

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

#include "../../utility/ui_system.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H



void imgui_test_entry()
{
	timer_initialize();

	// Create window
	Window* window = window_create("Test", 0);
	SCOPE_EXIT(window_destroy(window));
	window_load_position(window, "window_pos.set");
	opengl_state_set_clear_color(vec4(0.0f));
	window_set_vsync(window, true);

	// Prepare rendering core
	Window_State* window_state = window_get_window_state(window);
	rendering_core_initialize(window_state->width, window_state->height, window_state->dpi);
	SCOPE_EXIT(rendering_core_destroy());
	Camera_3D* camera = camera_3D_create(math_degree_to_radians(90), 0.1f, 100.0f);
	SCOPE_EXIT(camera_3D_destroy(camera));

	ui_system_initialize();
	SCOPE_EXIT(ui_system_shutdown());

	// Window Loop
	double time_last_update_start = timer_current_time_in_seconds();
	while (true)
	{
		double time_frame_start = timer_current_time_in_seconds();
		float time_since_last_update = (float)(time_frame_start - time_last_update_start);
		time_last_update_start = time_frame_start;

		// Input Handling
		Input* input = window_get_input(window);
		{
			int msg_count = 0;
			if (!window_handle_messages(window, false, &msg_count)) {
				break;
			}

			if (input->close_request_issued ||
				(input->key_pressed[(int)Key_Code::ESCAPE] && (input->key_down[(int)Key_Code::SHIFT] || input->key_down[(int)Key_Code::CTRL])))
			{
				window_save_position(window, "window_pos.set");
				window_close(window);
				break;
			}
			if (input->key_pressed[(int)Key_Code::F11]) {
				Window_State* state = window_get_window_state(window);
				window_set_fullscreen(window, !state->fullscreen);
			}
		}
		rendering_core_prepare_frame(timer_current_time_in_seconds(), window_state->width, window_state->height);

		ui_system_start_frame(input);
		ui_system_push_test_windows();
		Render_Pass* pass_2d = rendering_core_query_renderpass("2D-Pass", pipeline_state_make_alpha_blending(), nullptr);
		ui_system_end_frame_and_render(window, input, pass_2d);

		// End of frame handling
		{
			rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
			window_swap_buffers(window);
			input_reset(input); // Clear input for next frame

			// Sleep
			const int TARGET_FPS = 60;
			const double SECONDS_PER_FRAME = 1.0 / TARGET_FPS;
			timer_sleep_until(time_frame_start + SECONDS_PER_FRAME);
		}
	}
}
