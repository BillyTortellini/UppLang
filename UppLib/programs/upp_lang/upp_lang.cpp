#include "upp_lang.hpp"
#include "../../upplib.hpp"
#include "../../win32/timing.hpp"

#include "../../rendering/opengl_utils.hpp"
#include "../../rendering/shader_program.hpp"
#include "../../rendering/gpu_buffers.hpp"
#include "../../rendering/cameras.hpp"
#include "../../rendering/camera_controllers.hpp"
#include "../../rendering/texture_2D.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/mesh_utils.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../win32/window.hpp"
#include "../../utility/file_io.hpp"
#include "../../utility/random.hpp"
#include "../../utility/gui.hpp"
#include "../../rendering/renderer_2d.hpp"

#include "../../math/umath.hpp"
#include "../../datastructures/hashtable.hpp"

#include "code_editor.hpp"
#include "Test_Renderer.hpp"

u64 int_hash(int* i) {
    return (u64)(*i * 17);
}
bool int_equals(int* a, int* b) {
    return (*a == *b);
}

void test_things()
{
    {
        for (int i = 0; i < 200; i++)
        {
            Hashtable<int, const char*> table = hashtable_create_empty<int, const char*>(3, &int_hash, &int_equals);
            hashtable_insert_element(&table, 7, "Hello there\n");
            for (int j = 0; j < 32; j++) {
                hashtable_insert_element(&table, j * 472, "Hello there\n");
            }
            const char** result = hashtable_find_element(&table, 7);
            hashtable_reset(&table);
            hashtable_destroy(&table);
        }

        Hashtable<int, const char*> table = hashtable_create_empty<int, const char*>(3, &int_hash, &int_equals);
        SCOPE_EXIT(hashtable_destroy(&table));
        hashtable_insert_element(&table, 1, "Hi what");
        hashtable_insert_element(&table, 2, "Hello there\n");
        hashtable_insert_element(&table, 3, "The frick dude");
        hashtable_insert_element(&table, 4, "Bombaz");
        hashtable_insert_element(&table, 5, "Tunerz");
        auto iter = hashtable_iterator_create(&table);
        while (hashtable_iterator_has_next(&iter)) {
            logg("%d = %s\n", iter.current_entry->key, iter.current_entry->value);
            hashtable_iterator_next(&iter);
        }
    }
}

void upp_lang_main()
{
    Window* window = window_create("Test", 0);
    SCOPE_EXIT(window_destroy(window));
    Window_State* window_state = window_get_window_state(window);

    Rendering_Core core = rendering_core_create(window_state->width, window_state->height, window_state->dpi);
    SCOPE_EXIT(rendering_core_destroy(&core));

    Render_Pass* background_pass = render_pass_create(0, pipeline_state_make_default(), true, true, true);
    SCOPE_EXIT(render_pass_destroy(background_pass));

    // Initialize all modules that need globals
    timing_initialize();
    random_initialize();

    Text_Renderer* text_renderer = text_renderer_create_from_font_atlas_file(&core, "resources/fonts/glyph_atlas.atlas");
    SCOPE_EXIT(text_renderer_destroy(text_renderer, &core));

    Renderer_2D* renderer_2D = renderer_2D_create(&core, text_renderer);
    SCOPE_EXIT(renderer_2D_destroy(renderer_2D, &core));
    GUI gui = gui_create(renderer_2D, window_get_input(window));
    SCOPE_EXIT(gui_destroy(&gui));

    Code_Editor code_editor = code_editor_create(text_renderer, &core);
    SCOPE_EXIT(code_editor_destroy(&code_editor));

    // Background
    Mesh_GPU_Buffer mesh_quad = mesh_utils_create_quad_2D(&core);
    SCOPE_EXIT(mesh_gpu_buffer_destroy(&mesh_quad));

    Shader_Program* background_shader = shader_program_create(&core, "resources/shaders/upp_lang/background.glsl");
    SCOPE_EXIT(shader_program_destroy(background_shader));

    Camera_3D* camera = camera_3D_create(&core, math_degree_to_radians(90), 0.1f, 100.0f);
    SCOPE_EXIT(camera_3D_destroy(camera, &core));
    Camera_Controller_Arcball camera_controller_arcball;
    {
        window_set_cursor_constrain(window, false);
        window_set_cursor_visibility(window, true);
        window_set_cursor_reset_into_center(window, false);
        camera_controller_arcball = camera_controller_arcball_make(vec3(0.0f), 2.0f);
        camera->position = vec3(0, 0, 1.0f);
    }

    Test_Renderer test_renderer = test_renderer_create(&core, camera);
    SCOPE_EXIT(test_renderer_destroy(&test_renderer, &core));

    // Set Window/Rendering Options
    {
        //window_set_size(window, 600, 600);
        window_set_position(window, -1234, 96);
        window_set_fullscreen(window, true);
        window_set_vsync(window, false);

        opengl_state_set_clear_color(&core.opengl_state, vec4(0.0f));
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        window_set_vsync(window, true);
    }

    // Window Loop
    double time_last_update_start = timing_current_time_in_seconds();
    float angle = 0.0f;
    while (true)
    {
        double time_frame_start = timing_current_time_in_seconds();
        float time_since_last_update = (float)(time_frame_start - time_last_update_start);
        time_last_update_start = time_frame_start;

        // Input Handling
        Input* input = window_get_input(window);
        {
            if (!window_handle_messages(window, false)) {
                break;
            }
            if (input->close_request_issued || input->key_pressed[KEY_CODE::ESCAPE]) {
                // Close window
                window_close(window);
                // Write text editor output to file
                String output = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&output););
                text_append_to_string(&code_editor.text_editor->text, &output);
                file_io_write_file("editor_text.txt", array_create_static((byte*)output.characters, output.size));
                break;
            }
            if (input->key_pressed[KEY_CODE::F11]) {
                Window_State* state = window_get_window_state(window);
                window_set_fullscreen(window, !state->fullscreen);
            }

            camera_controller_arcball_update(&camera_controller_arcball, camera, input, window_state->width, window_state->height);
            gui_update(&gui, input, window_state->width, window_state->height);
            code_editor_update(&code_editor, input, timing_current_time_in_seconds());
            test_renderer_update(&test_renderer, input);
        }

        // Rendering
        {
            rendering_core_prepare_frame(&core, camera, timing_current_time_in_seconds(), window_state->width, window_state->height);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Draw Background
            render_pass_add_draw_call(background_pass, background_shader, &mesh_quad);
            render_pass_execute(background_pass, &core);

            // Text editor
            BoundingBox2 region = bounding_box_2_make_min_max(vec2(-1, -1), vec2(1, 1));
            code_editor_render(&code_editor, &core, region);

            //test_renderer_render(&test_renderer, &core);

            window_swap_buffers(window);
        }
        input_reset(input); // Clear input for next frame

        // Sleep
        {
            double time_calculations = timing_current_time_in_seconds() - time_frame_start;
            //logg("TSLF: %3.2fms, calculation time: %3.2fms\n", time_since_last_update*1000, time_calculations*1000);

            // Sleep
            const int TARGET_FPS = 60;
            const double SECONDS_PER_FRAME = 1.0 / TARGET_FPS;
            timing_sleep_until(time_frame_start + SECONDS_PER_FRAME);
        }
    }
}