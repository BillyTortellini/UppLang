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
#include "../../datastructures/hashset.hpp"
#include "../../utility/hash_functions.hpp"

#include "code_editor.hpp"
#include "Test_Renderer.hpp"

struct Dummy
{
    int valX;
    int valY;
    bool alive;
};

Dummy dummy_make(int x, int y, bool alive)
{
    Dummy result;
    result.valX = x;
    result.valY = y;
    result.alive = alive;
    return result;
}

u64 dummy_hash(Dummy* a) {
    return hash_memory(array_create_static<byte>((byte*)a, sizeof(Dummy)));
}

bool dummy_compare(Dummy* a, Dummy* b) {
    return a->valX == b->valX && a->valY == b->valY && a->alive == b->alive;
}

Dummy dummy_make_random(Random* random)
{
    return dummy_make(
        (int)random_next_u32(random) % 20034,
        (int)random_next_u32(random) % 20034,
        random_next_bool(random, 0.15f)
    );
}

void dummy_print(Dummy* d) {
    u64 hash = dummy_hash(d);
    logg("Dummy {valx: %d, valy: %d, alive: %s} hash: %x\n", d->valX, d->valY, d->alive ? "true" : "false", hash);
}

void test_things()
{
    Random random = random_make_time_initalized();
    // Hashset tests
    {
        Hashset<Dummy> set;
        set = hashset_create_empty<Dummy>(32, dummy_hash, &dummy_compare);
        SCOPE_EXIT(hashset_destroy(&set));

        Dynamic_Array<Dummy> added = dynamic_array_create_empty<Dummy>(64);
        SCOPE_EXIT(dynamic_array_destroy(&added));
        for (int i = 0; i < 100000; i++)
        {
            Dummy d = dummy_make_random(&random);
            bool inserted = hashset_insert_element(&set, d);
            //dummy_print(&d);
            if (inserted && random_next_bool(&random, 0.4f)) {
                dynamic_array_push_back(&added, d);
            }
        }

        for (int i = 0; i < added.size; i++) {
            Dummy d = added[i];
            if (!hashset_contains(&set, d)) {
                logg("Dummy not found: ");
                dummy_print(&d);
                bool inside = hashset_contains(&set, d);
                panic("Hashset should contain this dummy");
            }
        }
    }

    // Pointer tests
    {
        int a = 17;
        int b = 32;
        int c = 1005;
        int* ap = &a;
        int* bp = &b;
        int* cp = &c;

        Hashtable<int*, const char*> table = hashtable_create_pointer_empty<int*, const char*>(16);
        SCOPE_EXIT(hashtable_destroy(&table));

        hashtable_insert_element(&table, ap, "A");
        hashtable_insert_element(&table, bp, "B");
        hashtable_insert_element(&table, cp, "C");

        const char** b_res = hashtable_find_element(&table, bp);
        const char** a_res = hashtable_find_element(&table, ap);
        const char** c_res = hashtable_find_element(&table, cp);
    }

    // Hashtable tests
    {
        for (int i = 0; i < 200; i++)
        {
            Hashtable<int, const char*> table = hashtable_create_empty<int, const char*>(3, &hash_i32, &equals_i32);
            hashtable_insert_element(&table, 7, "Hello there\n");
            for (int j = 0; j < 32; j++) {
                hashtable_insert_element(&table, j * 472, "Hello there\n");
            }
            const char** result = hashtable_find_element(&table, 7);
            hashtable_reset(&table);
            hashtable_destroy(&table);
        }

        Hashtable<int, const char*> table = hashtable_create_empty<int, const char*>(3, &hash_i32, &equals_i32);
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
    //test_things();

    Window* window = window_create("Test", 0);
    SCOPE_EXIT(window_destroy(window));
    Window_State* window_state = window_get_window_state(window);

    Rendering_Core core = rendering_core_create(window_state->width, window_state->height, window_state->dpi);
    SCOPE_EXIT(rendering_core_destroy(&core));

    Render_Pass* background_pass = render_pass_create(0, pipeline_state_make_default(), true, true, true);
    SCOPE_EXIT(render_pass_destroy(background_pass));

    Timer timer = timer_make();

    Text_Renderer* text_renderer = text_renderer_create_from_font_atlas_file(&core, "resources/fonts/glyph_atlas.atlas");
    SCOPE_EXIT(text_renderer_destroy(text_renderer, &core));

    Renderer_2D* renderer_2D = renderer_2D_create(&core, text_renderer);
    SCOPE_EXIT(renderer_2D_destroy(renderer_2D, &core));
    GUI gui = gui_create(renderer_2D, window_get_input(window), &timer);
    SCOPE_EXIT(gui_destroy(&gui));

    Code_Editor code_editor = code_editor_create(text_renderer, &core, &timer);
    SCOPE_EXIT(code_editor_destroy(&code_editor));

    // Background
    Mesh_GPU_Buffer mesh_quad = mesh_utils_create_quad_2D(&core);
    SCOPE_EXIT(mesh_gpu_buffer_destroy(&mesh_quad));

    Shader_Program* background_shader = shader_program_create(&core, { "resources/shaders/upp_lang/background.glsl" });
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

    Primitive_Renderer_2D* primitive_renderer_2D = primitive_renderer_2D_create(&core);
    SCOPE_EXIT(primitive_renderer_2D_destroy(primitive_renderer_2D, &core));

    // Set Window/Rendering Options
    {
        window_set_size(window, 800, 600);
        //window_set_position(window, -1234, 96);
        //window_set_fullscreen(window, true);
        window_set_vsync(window, false);

        opengl_state_set_clear_color(&core.opengl_state, vec4(0.0f));
        window_set_vsync(window, true);
    }
    Pipeline_State pipeline_state = pipeline_state_make_default();
    pipeline_state.blending_state.blending_enabled = true;
    rendering_core_update_pipeline_state(&core, pipeline_state);

    // Window Loop
    double time_last_update_start = timer_current_time_in_seconds(&timer);
    float angle = 0.0f;
    while (true)
    {
        double time_frame_start = timer_current_time_in_seconds(&timer);
        float time_since_last_update = (float)(time_frame_start - time_last_update_start);
        time_last_update_start = time_frame_start;

        // Input Handling
        Input* input = window_get_input(window);
        {
            if (!window_handle_messages(window, false)) {
                break;
            }
            if (input->close_request_issued || input->key_pressed[(int)Key_Code::ESCAPE]) {
                // Close window
                window_close(window);
                // Write text editor output to file
                String output = string_create_empty(256);
                SCOPE_EXIT(string_destroy(&output););
                text_append_to_string(&code_editor.text_editor->text, &output);
                file_io_write_file("editor_text.txt", array_create_static((byte*)output.characters, output.size));
                break;
            }
            if (input->key_pressed[(int)Key_Code::F11]) {
                Window_State* state = window_get_window_state(window);
                window_set_fullscreen(window, !state->fullscreen);
            }

            camera_controller_arcball_update(&camera_controller_arcball, camera, input, window_state->width, window_state->height);
            //gui_update(&gui, input, window_state->width, window_state->height);
            code_editor_update(&code_editor, input, timer_current_time_in_seconds(&timer));
            input_reset(input); // Clear input for next frame
        }

        double time_input_end = timer_current_time_in_seconds(&timer);

        // Rendering
        {
            rendering_core_prepare_frame(
                &core, camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH, timer_current_time_in_seconds(&timer), window_state->width, window_state->height
            );

            // Draw Background
            shader_program_draw_mesh(background_shader, &mesh_quad, &core, {});

            // Text editor
            Bounding_Box2 region = bounding_box_2_make_min_max(vec2(-1, -1), vec2(1, 1));
            code_editor_render(&code_editor, &core, region);

            /*
            primitive_renderer_2D_add_rectangle(primitive_renderer_2D, vec2(600, 300), vec2(50, 200), 0.0f, Anchor_2D::CENTER_CENTER, vec3(1.0f, 0.2f, 0.7f));
            primitive_renderer_2D_add_circle(primitive_renderer_2D, vec2(400, 400), 200.0f, 0.0f, vec3(0.0f, 1.0f, 0.3f));
            primitive_renderer_2D_add_circle(primitive_renderer_2D, vec2(500, 500), 150.0f, 0.0f, vec3(0.5f, 1.0f, 0.3f));
            primitive_renderer_2D_add_circle(primitive_renderer_2D, vec2(700, 400), 50.0f, 0.0f, vec3(1.0f, 0.5f, 1.0f));
            {
                float size = -2.0f;
                float y = 200;
                float biggerness = 0.3f;
                for (int i = 0; i < 30; i++) {
                    primitive_renderer_2D_add_line(primitive_renderer_2D, vec2(500.0f, y), vec2(700.0f, y + 50.0f),
                        Line_Cap::FLAT, Line_Cap::FLAT,
                        size, 0.0f, vec3(1.0f));
                    y = y + math_maximum(5.0f, size * 5.0f);
                    size = size + biggerness;
                }
            }
            {
                float size = 1.0f;
                float y = 200;
                float biggerness = 0.3f;
                y += math_sine(core.render_information.current_time_in_seconds) * 30.0f;
                for (int i = 0; i < 30; i++) {
                    primitive_renderer_2D_add_line(primitive_renderer_2D, vec2(720.0f, y), vec2(920.0f, y + 50.0f),
                        Line_Cap::FLAT, Line_Cap::ROUND,
                        size, 0.0f, vec3(1.0f));
                    y = y + math_maximum(5.0f, size * 5.0f);
                    size = size + biggerness;
                }
            }
            {
                vec2 center = vec2(300, 300);
                int spokes = 80;
                float width = 150.0f;
                float thickness = 1.0f;
                float t = core.render_information.current_time_in_seconds / 6.0f;
                for (int i = 0; i < spokes; i++) {
                    vec2 end = vec2(math_sine(2.0f * PI * ((float)i / spokes) + t), math_cosine(2.0f * PI * ((float)i / spokes) + t)) * width + center;
                    primitive_renderer_2D_add_line(primitive_renderer_2D, center, end,
                        Line_Cap::FLAT, Line_Cap::ROUND,
                        thickness, 0.0f, vec3(1.0f));
                }
            }
            {
                vec2 center = vec2(300, 600);
                int spokes = 20;
                float width = 150.0f;
                float thickness = 10.0f;
                float t = core.render_information.current_time_in_seconds / 3.0f;
                for (int i = 0; i < spokes; i++) {
                    vec2 end = vec2(math_sine(2.0f * PI * ((float)i / spokes) + t), math_cosine(2.0f * PI * ((float)i / spokes) + t)) * width + center;
                    primitive_renderer_2D_add_line(primitive_renderer_2D, center, end,
                        Line_Cap::FLAT, Line_Cap::FLAT,
                        thickness, 0.0f, vec3(1.0f));
                }
            }

            {
                float thickness = 5.0f;
                primitive_renderer_2D_start_line_train(primitive_renderer_2D, vec3(1.0f), 0.0f);
                primitive_renderer_2D_add_line_train_point(primitive_renderer_2D, vec2(300, 100), thickness);
                primitive_renderer_2D_add_line_train_point(primitive_renderer_2D, vec2(400, 100), thickness);
                primitive_renderer_2D_add_line_train_point(primitive_renderer_2D, vec2(300, 50), thickness);
                primitive_renderer_2D_add_line_train_point(primitive_renderer_2D, vec2(400, 70), thickness);
                primitive_renderer_2D_end_line_train(primitive_renderer_2D);
            }

            primitive_renderer_2D_render(primitive_renderer_2D, &core);
            */

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