#include "render_rework.hpp"

#include "../../utility/utils.hpp"
#include "../../win32/timing.hpp"
#include "../../win32/window.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../rendering/texture.hpp"
#include "../../rendering/texture_bitmap.hpp"
#include "../../rendering/camera_controllers.hpp"
#include "../../utility/random.hpp"
#include "../../rendering/framebuffer.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/renderer_2d.hpp"
#include "../../rendering/basic2D.hpp"

struct GUI_Renderer
{
    Mesh* mesh;
    Shader* shader;
};

GUI_Renderer gui_renderer_initialize() 
{
    auto& pre = rendering_core.predefined;

    GUI_Renderer result;
    result.mesh = rendering_core_query_mesh("gui_rect", vertex_description_create({pre.position3D, pre.color4}), true);
    result.shader = rendering_core_query_shader("gui_rect.glsl");
    return result;
}

void gui_draw_rect(GUI_Renderer* renderer, vec2 pos, Anchor anchor, vec2 size, vec4 color)
{
    auto& info = rendering_core.render_information;
    size = convertSizeFromTo(size, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
    vec2 center = convertPointFromTo(pos, Unit::PIXELS, Unit::NORMALIZED_SCREEN);
    vec2 bottom_left = anchor_switch(center, size, anchor, Anchor::BOTTOM_LEFT);

    float d = 0.0f;
    {
        auto& pre = rendering_core.predefined;
        float x = bottom_left.x;
        float y = bottom_left.y;
        float w = size.x;
        float h = size.y;
        mesh_push_attribute(
            renderer->mesh, pre.position3D,
            {
                vec3(x    , y    , d),
                vec3(x + w, y    , d),
                vec3(x + w, y + h, d),

                vec3(x    , y    , d),
                vec3(x + w, y + h, d),
                vec3(x    , y + h, d)
            }
        );
        mesh_push_attribute(renderer->mesh, pre.color4, {color, color, color, color, color, color});
    }
}

void render_gui(GUI_Renderer* renderer, Input* input, Text_Renderer* text_renderer)
{
    auto& core = rendering_core;
    auto& pre = core.predefined;

    // Make the rectangle a constant pixel size:
    int pixel_width = 100;
    int pixel_height = 100;

    const vec4 white = vec4(1.0f);
    const vec4 red = vec4(vec3(1, 0, 0), 1.0f);
    const vec4 green = vec4(vec3(0, 1, 0), 1.0f);
    const vec4 blue = vec4(vec3(0, 0, 1), 1.0f);
    const vec4 cyan = vec4(vec3(0, 1, 1), 1.0f);
    const vec4 yellow = vec4(vec3(1, 1, 0), 1.0f);
    const vec4 magenta = vec4(vec3(1, 0, 1), 1.0f);

    // gui_draw_rect(
    //     renderer, 
    //     convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN), 
    //     Anchor::CENTER_CENTER, 
    //     convertSize(vec2(4, 4), Unit::CENTIMETER), 
    //     red
    // );
    // gui_draw_rect(
    //     renderer, 
    //     convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN), 
    //     Anchor::CENTER_LEFT, 
    //     convertSize(vec2(4, 4), Unit::CENTIMETER), 
    //     green
    // );
    // gui_draw_rect(
    //     renderer, 
    //     convertPoint(vec2(-1.0f, 0.0f), Unit::NORMALIZED_SCREEN), 
    //     Anchor::CENTER_LEFT, 
    //     vec2(convertWidth(3, Unit::CENTIMETER), convertHeight(2.0f, Unit::NORMALIZED_SCREEN)), 
    //     green * 0.8f
    // );

    float text_height = convertHeight(1.3f, Unit::CENTIMETER);
    String text = string_create_static("Hello World");
    float width = text_renderer_line_width(text_renderer, text_height, text.size);
    vec2 pos = convertPoint(vec2(0.0f), Unit::NORMALIZED_SCREEN);
    Anchor anchor = Anchor::BOTTOM_CENTER;

    vec2 mousePos = convertPoint(vec2((float)input->mouse_x, (float)rendering_core.render_information.backbuffer_height - input->mouse_y), Unit::PIXELS);
    Bounding_Box2 bb = bounding_box_2_make_anchor(pos, vec2(width, text_height * 1.1), anchor);
    vec4 color = red;
    if (bounding_box_2_is_point_inside(bb, mousePos)) {
        color = green;
        if (input->mouse_down[(int)Mouse_Key_Code::LEFT]) {
            logg("Hello world!\n");
        }
    }

    gui_draw_rect(renderer, pos, anchor, vec2(width + 10, text_height * 1.1 + 10), white);
    gui_draw_rect(renderer, pos, anchor, vec2(width, text_height * 1.1), color);
    text_renderer_add_text(text_renderer, text, pos, anchor, text_height, vec3(1.0f));

    /* So what coordinates do we have here:
        * pixel coordinates             (0 - bb_width)
            + Integer precision
            + Absolute
            - Resolution dependent (E.g. no scaling)
        * normalized screen coordinates (-1.0f - 1.0f)
            o Required for rendering
            - Introduces stretching on non 1:1 aspect rations
            + Resolution Independent
        * Aspect-Ratio normalized coordinates (4 - 3 --> 4/3 - 1)
            o Normalized to either height, width, max or min of dimensions
            + No stretching
            - Arbitrary boundaries on the side of windows (e.g. at 1.34343 is the left boundary)
        * Pixel coordinates are almost fine except that they are integers.
        * I guess depending on the use case you want other sizes...
        * Conversion of widths and heights is different when we have different aspect ratios
        Some things we may want:
            - Size dependent on Window-Size (E.g. text scaling with Screen size for large Text and UI-elements in Games)
            - Size dependent on Screen-Size (For things that should always be e.g. readable, like Text in information UIs)
            - Fixed pixel size:
                Not flexible on high resolution devices, everything will be smaller, and on low-res monitors the size is too large.

       When sending the data to the gpu we want to have normalized screen coordinates
     */
    render_pass_draw(pre.main_pass, renderer->shader, renderer->mesh, Mesh_Topology::TRIANGLES, {});
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
        window_set_vsync(window, true);
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

    GUI_Renderer gui_renderer = gui_renderer_initialize();

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
            rendering_core_prepare_frame(timer_current_time_in_seconds(&timer), window_state->width, window_state->height);
            SCOPE_EXIT(
                renderer_2D_reset(renderer_2D);
                text_renderer_reset(text_renderer);
                rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
                window_swap_buffers(window);
            );

            render_gui(&gui_renderer, input, text_renderer);

            auto text_pass_state = pipeline_state_make_default();
            text_pass_state.blending_state.blending_enabled = true;
            text_pass_state.blending_state.source = Blend_Operand::SOURCE_ALPHA;
            text_pass_state.blending_state.destination = Blend_Operand::ONE_MINUS_SOURCE_ALPHA;
            text_pass_state.blending_state.equation = Blend_Equation::ADDITION;
            text_pass_state.depth_state.test_type = Depth_Test_Type::IGNORE_DEPTH;
            auto text_pass = rendering_core_query_renderpass("Text pass", text_pass_state, 0);
            render_pass_add_dependency(text_pass, rendering_core.predefined.main_pass);
            text_renderer_draw(text_renderer, text_pass);

            // text_renderer_add_text(text_renderer, &string_create_static("H"), vec2(0.0f), 0.1f, 0.0f);
            // text_renderer_draw(text_renderer, text_pass);
            // text_renderer_add_text(text_renderer, &string_create_static("e"), vec2(0.0f, -0.3f), 0.03f, 0.0f);
            // text_renderer_draw(text_renderer, text_pass);


            // auto main_pass = core.predefined.main_pass;
            // auto circle1 = rendering_core_query_mesh(
            //     "circleMesh1", vertex_description_create({ core.predefined.position2D }), true
            // );
            // auto circle2 = rendering_core_query_mesh(
            //     "circleMesh2", vertex_description_create({ core.predefined.position2D, core.predefined.index, core.predefined.texture_coordinates }), true
            // );

            // // Generate circle data
            // {
            //     float radius = 0.3f;
            //     int division = 16;
            //     auto time = rendering_core.render_information.current_time_in_seconds;
            //     vec2 offset = vec2(math_cosine(time), math_sine(time));
            //     for (int i = 0; i < division; i++) {
            //         mesh_push_attribute(
            //             circle1, core.predefined.position2D, {
            //                 offset + vec2(0),
            //                 offset + vec2(math_cosine(2 * PI / division * i), math_sine(2 * PI / division * i)) * radius,
            //                 offset + vec2(math_cosine(2 * PI / division * (i + 1)), math_sine(2 * PI / division * (i + 1))) * radius,
            //             }
            //         );
            //     }
            //     
            //     time += PI;
            //     offset = vec2(math_cosine(time), math_sine(time));

            //     auto indices = mesh_push_attribute_slice(circle2, core.predefined.index, division * 3);
            //     auto positions = mesh_push_attribute_slice(circle2, core.predefined.position2D, division * 3);
            //     auto uvs = mesh_push_attribute_slice(circle2, core.predefined.texture_coordinates, division * 3);
            //     for (int i = 0; i < division; i++) {
            //         int k = i * 3;
            //         indices[k + 0] = k + 0;
            //         indices[k + 1] = k + 1;
            //         indices[k + 2] = k + 2;
            //         positions[k + 0] = offset + vec2(0);
            //         positions[k + 1] = offset + vec2(math_cosine(2 * PI / division * k), math_sine(2 * PI / division * k)) * radius;
            //         positions[k + 2] = offset + vec2(math_cosine(2 * PI / division * (k + 1)), math_sine(2 * PI / division * (k + 1))) * radius;
            //         uvs[k + 0] = vec2(0);
            //         uvs[k + 1] = vec2(math_cosine(2 * PI / division * k), math_sine(2 * PI / division * k)) * radius;
            //         uvs[k + 2] = vec2(math_cosine(2 * PI / division * (k + 1)), math_sine(2 * PI / division * (k + 1))) * radius;
            //     }

            //     logg("What\n");
            // }

            // auto const_color_2d = rendering_core_query_shader("const_color_2D.glsl");
            // //render_pass_draw(main_pass, const_color_2d, circle1, Mesh_Topology::TRIANGLES, { uniform_make("u_color", vec4(1.0f)) });
            // render_pass_draw(main_pass, const_color_2d, circle2, Mesh_Topology::TRIANGLES, { uniform_make("u_color", vec4(1.0f, 0.0f, 0.0f, 1.0f)) });
            //auto color_shader_2d = shader_generator_make({ predefined.position2D, predefined.uniform_color }, );

            //auto bg_buffer = rendering_core_query_framebuffer("bg_buffer", Texture_Type::RED_GREEN_BLUE_U8, Depth_Type::NO_DEPTH, 512, 512);
            //{
            //    auto bg_pass = rendering_core_query_renderpass("bg_pass", pipeline_state_make_default(), bg_buffer);
            //    render_pass_add_dependency(main_pass, bg_pass);
            //    auto bg_shader = rendering_core_query_shader("upp_lang/background.glsl");
            //    render_pass_draw(bg_pass, bg_shader, quad_mesh, Mesh_Topology::TRIANGLES, {});
            //}

            //{
            //    auto shader = rendering_core_query_shader("test.glsl");
            //    render_pass_set_uniforms(main_pass, shader, { uniform_make("image", bg_buffer->color_texture, sampling_mode_bilinear()) });
            //    render_pass_draw(main_pass, shader, mesh, Mesh_Topology::TRIANGLES, { uniform_make("offset", vec2(0)), uniform_make("scale", 1.0f) });
            //}
            // renderer_2D_add_rectangle(renderer_2D, vec2(0.0f), vec2(0.2, 0.3f), vec3(1.0f), 0.0f);
            // renderer_2D_draw(renderer_2D, text_pass);
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