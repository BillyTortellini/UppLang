#include "upp_lang.hpp"
#include "../../upplib.hpp"
#include "../../win32/timing.hpp"

#include "../../rendering/opengl_utils.hpp"
#include "../../rendering/shader_program.hpp"
#include "../../rendering/mesh_gpu_data.hpp"
#include "../../rendering/cameras.hpp"
#include "../../rendering/camera_controllers.hpp"
#include "../../rendering/texture.hpp"
#include "../../rendering/text_renderer.hpp"
#include "text_editor.hpp"
#include "../../rendering/mesh_utils.hpp"
#include "../../win32/window.hpp"

#include "../../math/umath.hpp"
#include "../../datastructures/hashtable.hpp"

#include "compiler.hpp"

u64 int_hash(int* i) {
    return (u64)(*i * 17);
}
bool int_equals(int* a, int* b) {
    return (*a == *b);
}

void upp_lang_main() 
{
    {
        String source_code = string_create("main :: ()->void{ x:int; x = 17+3*2%; return x;}");
        LexerResult result = lexer_parse_string(&source_code);
        SCOPE_EXIT(lexer_result_destroy(&result));

        Parser parser = parser_parse(&result);
        SCOPE_EXIT(parser_destroy(&parser));
        String printed_ast = string_create_empty(256);
        SCOPE_EXIT(string_destroy(&printed_ast));
        ast_node_root_append_to_string(&printed_ast, &parser.root, &result);
        logg("Ast: \n%s\n", printed_ast.characters);
    }
    {
        for (int i = 0; i < 200; i++)
        {
            Hashtable<int, const char*> table = hashtable_create_empty<int, const char*>(3, &int_hash, &int_equals);
            SCOPE_EXIT(hashtable_destroy(&table));
            hashtable_insert_element(&table, 7, "Hello there\n");
            for (int j = 0; j < 32; j++) {
                hashtable_insert_element(&table, j*472, "Hello there\n");
            }
            const char** result = hashtable_find_element(&table, 7);
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

    Window* window = window_create("Test", 0);
    SCOPE_EXIT(window_destroy(window));

    timing_initialize();

    FileListener* file_listener = file_listener_create();
    SCOPE_EXIT(file_listener_destroy(file_listener));

    OpenGLState opengl_state = opengl_state_create();
    SCOPE_EXIT(opengl_state_destroy(&opengl_state));
    opengl_state_query_current_state(&opengl_state);

    // Text Editor
    WindowState* window_state = window_get_window_state(window);
    TextRenderer* text_renderer = text_renderer_create_from_font_atlas_file(
        &opengl_state, file_listener, "resources/fonts/glyph_atlas.atlas", window_state->width, window_state->height);
    SCOPE_EXIT(text_renderer_destroy(text_renderer));
    Text_Editor_Logic text_editor_logic = text_editor_logic_create();
    SCOPE_EXIT(text_editor_logic_destroy(&text_editor_logic));
    Text_Editor text_editor = text_editor_create(text_renderer, file_listener, &opengl_state);
    {
        String test_string = string_create_static("main :: (x : int) -> void \n{\n\n}");
        text_editor_set_string(&text_editor, &test_string);
    }
    SCOPE_EXIT(text_editor_destroy(&text_editor));

    // Sample bitmap (Distance field)
    Texture_Bitmap bitmap = texture_bitmap_create_test_bitmap(16);
    Array<float> sdf = texture_bitmap_create_distance_field(&bitmap);
    //texture_bitmap_print_distance_field(sdf, bitmap.width);
    Texture texture_test = texture_create_from_bytes((byte*)sdf.data, bitmap.width, bitmap.height, 1, GL_FLOAT, texture_filtermode_make_linear(), &opengl_state);
    Mesh_GPU_Data mesh_quad = mesh_utils_create_quad_2D(&opengl_state);
    ShaderProgram* shader_image_simple = optional_unwrap(
        shader_program_create(file_listener, { "resources/shaders/image_simple.vert", "resources/shaders/image_simple.frag" })
    );
    SCOPE_EXIT(shader_program_destroy(shader_image_simple));
    SCOPE_EXIT(mesh_gpu_data_destroy(&mesh_quad));

    // Fullscreen test shader
    ShaderProgram* shader_test = optional_unwrap(
        shader_program_create(file_listener, { "resources/shaders/test.vert", "resources/shaders/test.frag" })
    );
    SCOPE_EXIT(shader_program_destroy(shader_test));

    // Triangle shader
    ShaderProgram* shader_simple = optional_unwrap(
        shader_program_create(file_listener, { "resources/shaders/simple.vert", "resources/shaders/simple.frag" })
    );
    SCOPE_EXIT(shader_program_destroy(shader_simple));
    Mesh_GPU_Data mesh_triangle;
    SCOPE_EXIT(mesh_gpu_data_destroy(&mesh_triangle));
    {
        vec3 triangle_vertices[] = {
            vec3(-0.5f, -0.5f, 0.0f),
            vec3(0.5f, -0.5f, 0.0f),
            vec3(0.0f, 0.5f, 0.0f),
        };
        u32 triangle_indices[] = {
            0, 1, 2
        };

        mesh_triangle = mesh_gpu_data_create(
            &opengl_state,
            vertex_gpu_buffer_create_with_attribute_information(
                array_create_static((byte*)triangle_vertices, sizeof(triangle_vertices)),
                vertex_attribute_information_make(GL_FLOAT, 3, DefaultVertexAttributeLocation::POSITION_3D, 0, sizeof(vec3)),
                GL_STATIC_DRAW
            ),
            index_gpu_buffer_create(
                &opengl_state,
                array_create_static(triangle_indices, sizeof(triangle_vertices)),
                GL_TRIANGLES, GL_STATIC_DRAW
            )
        );
    }

    // Initialize rendering options
    Camera_3D camera;
    {
        glClearColor(0, 0.0f, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        WindowState* state = window_get_window_state(window);
        glViewport(0, 0, state->width, state->height);
        window_set_vsync(window, false);

        camera = camera_3d_make(state->width, state->height, math_degree_to_radians(90), 0.1f, 100.0f);
        window_set_size(window, 600, 600);
        window_set_position(window, -1234, 96);
        window_set_fullscreen(window, true);
        window_set_vsync(window, true);
    }

    // Initialize Camera Controllers
    Camera_Controller_Arcball camera_controller_arcball;
    {
        window_set_cursor_constrain(window, false);
        window_set_cursor_visibility(window, true);
        window_set_cursor_reset_into_center(window, false);

        camera_controller_arcball = camera_controller_arcball_make(vec3(0.0f), 2.0f);
        camera.position = vec3(0, 0, 1.0f);
    }

    // Window Loop
    double time_last_update_start = timing_current_time_in_seconds();
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
                window_close(window);
                break;
            }
            if (input->client_area_resized) {
                WindowState* state = window_get_window_state(window);
                glViewport(0, 0, state->width, state->height);
                camera_3d_update_projection_window_size(&camera, state->width, state->height);
                text_renderer_update_window_size(text_renderer, state->width, state->height);
            }
            if (input->key_pressed[KEY_CODE::F11]) {
                WindowState* state = window_get_window_state(window);
                window_set_fullscreen(window, !state->fullscreen);
            }
            camera_controller_arcball_update(&camera_controller_arcball, &camera, input);
            text_editor_logic_update(&text_editor_logic, &text_editor, input);

            file_listener_check_if_files_changed(file_listener);
        }

        // Rendering
        {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Render triangle
            shader_program_set_uniform(shader_simple, &opengl_state, "time", (float)timing_current_time_in_seconds());
            shader_program_set_uniform(shader_simple, &opengl_state, "uniform_mvp", camera.view_projection_matrix);
            mesh_gpu_data_draw_with_shader_program(&mesh_triangle, shader_simple, &opengl_state);

            WindowState* window_state = window_get_window_state(window);
            text_editor_render(&text_editor, &opengl_state, window_state->width, window_state->height, window_state->dpi);

            /*
            shader_program_set_uniform(shader_test, &opengl_state, "time", (float)timing_current_time_in_seconds());
            shader_program_set_uniform(shader_test, &opengl_state, "aspect_ratio", (float)window_state->width / window_state->height);
            shader_program_set_uniform(shader_test, &opengl_state, "view_matrix", camera.view_matrix);
            shader_program_set_uniform(shader_test, &opengl_state, "camera_position", camera.position);
            mesh_gpu_data_draw_with_shader_program(&mesh_quad, shader_test, &opengl_state);
            */

            window_swap_buffers(window);
        }
        input_reset(input); // Clear input for next frame

        // Sleep
        {
            double time_calculations = timing_current_time_in_seconds()  - time_frame_start;
            //logg("TSLF: %3.2fms, calculation time: %3.2fms\n", time_since_last_update*1000, time_calculations*1000);

            // Sleep
            const int TARGET_FPS = 60;
            const double SECONDS_PER_FRAME = 1.0 / TARGET_FPS;
            timing_sleep_until(time_frame_start + SECONDS_PER_FRAME);
        }
    }
}