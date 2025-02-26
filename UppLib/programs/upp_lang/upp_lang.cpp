#include "upp_lang.hpp"

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

#include "syntax_editor.hpp"

#include "../../datastructures/block_allocator.hpp"
#include "../../datastructures/stack_allocator.hpp"
#include "../../win32/windows_helper_functions.hpp"

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
    logg("Dummy {valx: %d, valy: %d, alive: %s} hash: %x, ptr: %p\n", d->valX, d->valY, d->alive ? "true" : "false", hash, d);
}

void test_things()
{
    Random random = random_make_time_initalized();

    // Block allocator
    {
        Block_Allocator<Dummy> block = block_allocator_create_empty<Dummy>(4);
        SCOPE_EXIT(block_allocator_destroy(&block));
        const int count = 200;
        Dummy* dummies[count];

        for (int loops = 0; loops < 100; loops++) 
        {
            for (int i = 0; i < count; i++) {
                dummies[i] = block_allocator_allocate(&block);
                *dummies[i] = dummy_make_random(&random);
            }

            // Deallocate all
            for (int i = count - 1; i >= 0; i--) {
                block_allocator_deallocate(&block, dummies[i]);
            }
        }
        assert(block.used_block_count == 0, "HEY");
    }

    // Stack allocator
    {
        Stack_Allocator stack = stack_allocator_create_empty(32);
        SCOPE_EXIT(stack_allocator_destroy(&stack));

        const int count = 10;
        Dummy* dummies[count];

        for (int loops = 0; loops < 1; loops++) 
        {
            for (int i = 0; i < count; i++) {
                Dummy* d = stack_allocator_allocate<Dummy>(&stack);
                dummies[i] = d;
                *d= dummy_make_random(&random);
                d->valX = i;
                d->valY = i * 2;
                dummy_print(d);
            }

            logg("\nPrinting:\n");
            for (int i = 0; i < count; i++) {
                dummy_print(dummies[i]);
            }

            // Deallocate all
            stack_allocator_reset(&stack);
        }
        assert(stack.stack_pointer == 0, "HEY");
    }

    // Hashset tests
    {
        Hashset<Dummy> set;
        set = hashset_create_empty<Dummy>(32, dummy_hash, &dummy_compare);
        SCOPE_EXIT(hashset_destroy(&set));

        Dynamic_Array<Dummy> added = dynamic_array_create<Dummy>(64);
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
            assert(result != 0 && table.element_count == 33, "");
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

// Test implicit casting in C++
struct Base
{
    int base_value;
};

void print_base(Base* base) {
    if (base == nullptr) {
        printf("Base was numm");
        return;
    }
    printf("base_value: %d\n", base->base_value);
}

void upp_lang_main()
{
    //test_things();
    //return;

    timer_initialize();
    Window* window = window_create("Test", 0);
    SCOPE_EXIT(window_destroy(window));
    Window_State* window_state = window_get_window_state(window);
    rendering_core_initialize(window_state->width, window_state->height, window_state->dpi);
    SCOPE_EXIT(rendering_core_destroy());

    GLint maxAttribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribs);
    logg("Maximum attribs: %d\n", maxAttribs);

    Text_Renderer* text_renderer = text_renderer_create_from_font_atlas_file("resources/fonts/glyph_atlas.atlas");
    SCOPE_EXIT(text_renderer_destroy(text_renderer));

    Renderer_2D* renderer_2D = renderer_2D_create(text_renderer);
    SCOPE_EXIT(renderer_2D_destroy(renderer_2D));

    syntax_editor_initialize(text_renderer, renderer_2D, window, window_get_input(window));
    SCOPE_EXIT(syntax_editor_destroy());
    syntax_editor_load_state(string_create_static("upp_code/session.ses"));

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

    // Window Loop
    int last_animation_required_frame = -100;
    int frame = 0;
    double time_last_update_start = timer_current_time_in_seconds();
    float angle = 0.0f;
    while (true)
    {
        double time_frame_start = timer_current_time_in_seconds();
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

            if (input->close_request_issued || 
                (input->key_pressed[(int)Key_Code::ESCAPE] && (input->key_down[(int)Key_Code::SHIFT] || input->key_down[(int) Key_Code::CTRL]))) 
            {
                syntax_editor_save_state(string_create_static("upp_code/session.ses"));
                window_save_position(window, "window_pos.set");
                window_close(window);
                break;
            }
            if (input->key_pressed[(int)Key_Code::F11]) {
                Window_State* state = window_get_window_state(window);
                window_set_fullscreen(window, !state->fullscreen);
            }

            camera_controller_arcball_update(&camera_controller_arcball, camera, input, window_state->width, window_state->height);
            bool animations_running = false;
            syntax_editor_update(animations_running);
            if (animations_running) {
                last_animation_required_frame = frame;
            }
            //code_editor_update(&code_editor, input, timer_current_time_in_seconds(&timer));
        }

        double time_input_end = timer_current_time_in_seconds();

        // Rendering
        {
            rendering_core_prepare_frame(timer_current_time_in_seconds(), window_state->width, window_state->height);
            SCOPE_EXIT(
                text_renderer_reset(text_renderer);
            renderer_2D_reset(renderer_2D);
            rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
            window_swap_buffers(window);
            );

            // Draw Background
            //shader_program_draw_mesh(background_shader, &mesh_quad, &core, {});

            // Text editor
            Bounding_Box2 region = bounding_box_2_make_min_max(vec2(-1, -1), vec2(1, 1));
            //code_editor_render(&code_editor, &core, region);

            // Syntax editor
            syntax_editor_render();
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