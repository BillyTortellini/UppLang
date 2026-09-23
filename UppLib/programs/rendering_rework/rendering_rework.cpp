#include "rendering_rework.hpp"

#include <iostream>

#include "../../upplib.hpp"
#include "../../win32/timing.hpp"

#include "../../win32/window.hpp"
#include "../../rendering/opengl_function_pointers.hpp"
#include "../../utility/file_io.hpp"
#include "../../utility/random.hpp"
#include "../../rendering/renderer_2d.hpp"

#include "../../math/umath.hpp"
#include "../../datastructures/allocators.hpp"
#include "../../utility/hash_functions.hpp"

#include "../../win32/windows_helper_functions.hpp"
#include "../../utility/file_listener.hpp"
#include "../../utility/directory_crawler.hpp"

struct GRX_Core;
struct GRX_Shader;

struct GRX_Shader
{
    GRX_Core* core;
    String filename; // Filename in shader directory
    GLint program_id; // If success
    Watched_File* watched_file;
};

struct GRX_Core
{
    Arena arena;
    Window* window;
    File_Listener* file_listener;
    bool logging_enabled;

    // Shaders
    String shader_directory;
    DynArray<GRX_Shader> shaders;
};

void grx_core_log(GRX_Core* core, String msg)
{
    if (!core->logging_enabled) return;
    printf("GRX_Core log: %.*s\n", msg.size, msg.characters);
}

void grx_core_log(GRX_Core* core, String* msg){
    grx_core_log(core, *msg);
}

void grx_core_log_cstring(GRX_Core* core, const char* msg) {
    grx_core_log(core, string_create_static(msg));
}

void grx_core_initialize(GRX_Core* core, Window* window, File_Listener* file_listener, String shader_directory)
{
    core->arena = Arena::create();
    core->window = window;
    core->file_listener = file_listener;
    core->shader_directory = string_copy(shader_directory, &core->arena);
    core->logging_enabled = true;

    Arena tmp_arena_stack = Arena::create();
    Arena* tmp_arena = &tmp_arena_stack;
    SCOPE_EXIT(tmp_arena->destroy());
    String tmp_string = string_create(tmp_arena);

    // Load all shaders from folder
    {
        Directory_Crawler* crawler = directory_crawler_create();
        SCOPE_EXIT(directory_crawler_destroy(crawler));

        directory_crawler_set_path(crawler, shader_directory);
        Array<File_Info> file_infos = directory_crawler_get_content(crawler);
        if (file_infos.size == 0) {
            grx_core_log_cstring(core, "Either shader directory does not exist or it's empty");
        }

        for (int i = 0; i < file_infos.size; i++)
        {
            File_Info file_info = file_infos[i];

            // Ignore directory and files with non .shader ending
            if (file_info.is_directory) 
            {
                grx_core_log(
                    core,
                    tmp_string.reset()->append("Ignoring sub-directory of shader directory: \"")->append(file_info.name)->append("\"")
                );
                continue;
            }
            if (!string_ends_with(file_info.name, ".shader")) 
            {
                grx_core_log(
                    core,
                    tmp_string.reset()->append("Shader directory contains file with non .shader ending: \"")->append(file_info.name)->append("\"")
                );
                continue;
            }

            // Load file
            // String file_content = file_io_load_text_file()
        }
        
    }
}

void grx_core_destroy(GRX_Core* core)
{
    core->arena.destroy();
}



void rendering_rework_entry()
{
    Window* window = window_create("Test", 0);
    SCOPE_EXIT(window_destroy(window));
    Window_State* window_state = window_get_window_state(window);
    rendering_core_initialize(window_state->width, window_state->height, window_state->dpi);
    SCOPE_EXIT(rendering_core_destroy());

    GLint maxAttribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribs);
    logg("Maximum attribs: %d\n", maxAttribs);

    opengl_print_all_extensions(window_get_hdc_pointer(window));


    // Background
    // Camera_3D* camera = camera_3D_create(math_degree_to_radians(90), 0.1f, 100.0f);
    // SCOPE_EXIT(camera_3D_destroy(camera));
    // Camera_Controller_Arcball camera_controller_arcball;
    {
        window_set_cursor_constrain(window, false);
        window_set_cursor_visibility(window, true);
        window_set_cursor_reset_into_center(window, false);
        // camera_controller_arcball = camera_controller_arcball_make(vec3(0.0f), 2.0f);
        // camera->position = vec3(0, 0, 1.0f);
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

    // Window Loop
    int frame = 0;
    double time_last_update_start = timer_current_time_in_seconds();
    float angle = 0.0f;
    window_set_focus(window);
    while (true)
    {
        double time_frame_start = timer_current_time_in_seconds();
        float time_since_last_update = (float)(time_frame_start - time_last_update_start);
        time_last_update_start = time_frame_start;

        // Quick and dirty fix, as with new VisualStudio/Windows11 the console gets focused instead of the Window!
        if (frame == 1) {
            window_set_focus(window);
        }

        frame += 1;

        // Input Handling
        Input* input = window_get_input(window);
        {
            int msg_count = 0;
            if (!window_handle_messages(window, false, &msg_count)) {
                break;
            }

            if (input->close_request_issued || 
                (input->key_pressed[(int)Key_Code::ESCAPE] && (input->key_down[(int)Key_Code::SHIFT] || input->key_down[(int) Key_Code::CTRL]))) 
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

        double time_input_end = timer_current_time_in_seconds();

        // Rendering
        {
            SCOPE_EXIT(window_swap_buffers(window));
        }

        input_reset(input); // Clear input for next frame
        double time_render_end = timer_current_time_in_seconds();

        // Sleep
        {
            double time_calculations = timer_current_time_in_seconds() - time_frame_start;
            /*
            logg("FRAME_TIMING:\n---------------\n");
            logg("input        ... %3.2fms\n", 1000.0f * (float)(time_input_end - time_frame_start));
            logg("render       ... %3.2fms\n", 1000.0f * (float)(time_render_end - time_input_end));
            logg("TSLF: %3.2fms, calculation time: %3.2fms\n", time_since_last_update*1000, time_calculations*1000);
            */

            // Sleep
            const int TARGET_FPS = 60;
            const double SECONDS_PER_FRAME = 1.0 / TARGET_FPS;
            timer_sleep_until(time_frame_start + SECONDS_PER_FRAME);
        }
    }

}
