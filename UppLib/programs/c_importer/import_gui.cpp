#include "import_gui.hpp"

#include "../../rendering/opengl_utils.hpp"
#include "../../rendering/gpu_buffers.hpp"
#include "../../rendering/cameras.hpp"
#include "../../rendering/camera_controllers.hpp"
#include "../../rendering/texture.hpp"
#include "../../rendering/text_renderer.hpp"
#include "../../rendering/rendering_core.hpp"
#include "../../win32/window.hpp"
#include "../../win32/timing.hpp"
#include "../../utility/file_io.hpp"
#include "../../utility/gui.hpp"
#include "../../utility/fuzzy_search.hpp"
#include "../../rendering/renderer_2d.hpp"

#include "../../math/umath.hpp"
#include "../../datastructures/hashtable.hpp"
#include "../../datastructures/dynamic_array.hpp"
#include "../../utility/hash_functions.hpp"
#include "../upp_lang/compiler_misc.hpp"
#include "../upp_lang/c_backend.hpp"
#include "c_importer.hpp"

#include <cstdio>
#include <iostream>

/*
Idea of translation:
 2. Figure out which types need to be translated (Are used by imports)
 3. 
*/

struct Symbol_Import
{
    String* name;
    C_Import_Symbol* c_symbol;
    bool enabled;
};

struct Importer
{
    C_Import_Package* package;
    Dynamic_Array<Symbol_Import> symbols_to_import;
    Hashtable<C_Import_Type*, String> type_translations; // Type access string in upp
    String* text; // Text that's beeing currently worked on
    String struct_definitions;
};

Importer importer;

void importer_initialize()
{
    importer.symbols_to_import = dynamic_array_create<Symbol_Import>();
    importer.type_translations = hashtable_create_pointer_empty<C_Import_Type*, String>(32);
    importer.text = 0;
    importer.package = 0;
    importer.struct_definitions = string_create_empty(16);
}

void importer_reset()
{
    dynamic_array_reset(&importer.symbols_to_import);
    hashtable_for_each_value(&importer.type_translations, string_destroy);
    hashtable_reset(&importer.type_translations);
    string_reset(&importer.struct_definitions);
}

// Writes to importer.text
void output_c_import_type(C_Import_Type* type)
{
    {
        String* generated = hashtable_find_element(&importer.type_translations, type);
        if (generated != nullptr) {
            string_append_string(importer.text, generated);
            return;
        }
    }

    String* backup = importer.text;
    SCOPE_EXIT(importer.text = backup);

    String access_name = string_create_empty(8);
    importer.text = &access_name;

    bool is_const = ((int)type->qualifiers & (int)C_Type_Qualifiers::CONST_QUAL) != 0;
    bool is_signed = ((int)type->qualifiers & (int)C_Type_Qualifiers::SIGNED) != 0;
    bool is_unsigned = ((int)type->qualifiers & (int)C_Type_Qualifiers::UNSIGNED) != 0;
    if (type->type != C_Import_Type_Type::POINTER) {
        if (is_const) {
            string_append(&access_name, "const ");
        }
    }
    // Note: Not sure how to handle the other modifiers...

    switch (type->type)
    {
    case C_Import_Type_Type::PRIMITIVE: 
    {
        if (type->primitive == C_Import_Primitive::BOOL) {
            string_append_formated(&access_name, "bool");
            break;
        }

        if (!is_unsigned) 
        {
            switch (type->primitive)
            {
            case C_Import_Primitive::CHAR:        string_append(&access_name, "i8"); break; // Not sure if this is the best way because of char/unsiged char differences
            case C_Import_Primitive::SHORT:       string_append(&access_name, "i16"); break;
            case C_Import_Primitive::INT:         string_append(&access_name, "int"); break;
            case C_Import_Primitive::LONG:        string_append(&access_name, "i64"); assert(type->byte_size == 8, ""); break;
            case C_Import_Primitive::LONG_LONG:   string_append(&access_name, "i64"); assert(type->byte_size == 8, ""); break;
            case C_Import_Primitive::FLOAT:       string_append(&access_name, "float"); break;
            case C_Import_Primitive::DOUBLE:      string_append(&access_name, "f64"); break; 
            case C_Import_Primitive::LONG_DOUBLE: string_append(&access_name, "f64"); assert(type->byte_size == 8, "");  break;
            default: panic("");
            }
        }
        else
        {
            switch (type->primitive)
            {
            case C_Import_Primitive::CHAR:        string_append(&access_name, "u8"); break; // Not sure if this is the best way because of char/unsiged char differences
            case C_Import_Primitive::SHORT:       string_append(&access_name, "u16"); break;
            case C_Import_Primitive::INT:         string_append(&access_name, "u32"); break;
            case C_Import_Primitive::LONG:        string_append(&access_name, "u64"); assert(type->byte_size == 8, ""); break;
            case C_Import_Primitive::LONG_LONG:   string_append(&access_name, "u64"); assert(type->byte_size == 8, ""); break;
            case C_Import_Primitive::FLOAT:       string_append(&access_name, "float"); break;
            case C_Import_Primitive::DOUBLE:      string_append(&access_name, "f64"); break; 
            case C_Import_Primitive::LONG_DOUBLE: string_append(&access_name, "f64"); assert(type->byte_size == 8, "");  break;
            default: panic("");
            }
        }
        break;
    }
    case C_Import_Type_Type::POINTER: {
        output_c_import_type(type->pointer_child_type);
        string_append(&access_name, "*");
        if (is_const) {
            string_append(&access_name, "const");
        }
        break;
    }
    case C_Import_Type_Type::UNKNOWN_TYPE: {
        break;
    }
    case C_Import_Type_Type::ARRAY:
    case C_Import_Type_Type::ENUM:
    case C_Import_Type_Type::FUNCTION_SIGNATURE:
    case C_Import_Type_Type::STRUCTURE:
    default: panic("");
    }
}

void output_c_import_package_interface(C_Import_Package& package, String* output_filename)
{
    String result = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&result));

    // Generate all types
    for (int i = 0; i < package.type_system.registered_types.size; i++)
    {
        C_Import_Type* c_type = package.type_system.registered_types[i];
    }
}

int run_import_gui()
{
    // Window/Rendering Stuff
    Window* window = window_create("C-Import GUI", 0);
    SCOPE_EXIT(window_destroy(window));
    Window_State* window_state = window_get_window_state(window);
    rendering_core_initialize(window_state->width, window_state->height, window_state->dpi);
    SCOPE_EXIT(rendering_core_destroy());

    Timer timer = timer_make();

    Text_Renderer* text_renderer = text_renderer_create_from_font_atlas_file("resources/fonts/glyph_atlas.atlas");
    SCOPE_EXIT(text_renderer_destroy(text_renderer));

    gui_initialize(text_renderer, window);
    SCOPE_EXIT(gui_destroy());

    // Background
    Camera_3D* camera = camera_3D_create(math_degree_to_radians(90), 0.1f, 100.0f);
    SCOPE_EXIT(camera_3D_destroy(camera));

    // Set Window/Rendering Options
    {
        window_load_position(window, "import_gui_pos.set");
        opengl_state_set_clear_color(vec4(0.0f));
        window_set_vsync(window, true);
    }
    Pipeline_State pipeline_state = pipeline_state_make_default();
    pipeline_state.blending_state.blending_enabled = true;
    rendering_core_update_pipeline_state(pipeline_state);

    Render_Pass* gui_pass = rendering_core_query_renderpass("GUI_Pass", pipeline_state_make_alpha_blending(), nullptr);



    // Application Data
    String header_filepath = string_create_empty(32);
    SCOPE_EXIT(string_destroy(&header_filepath));
    C_Importer c_importer = c_importer_create();
    SCOPE_EXIT(c_importer_destroy(&c_importer));
    Identifier_Pool identifier_pool = identifier_pool_create();
    SCOPE_EXIT(identifier_pool_destroy(&identifier_pool));

    Dynamic_Array<Fuzzy_Item> fuzzy_search_results = dynamic_array_create<Fuzzy_Item>();

    Optional<C_Import_Package> import_package;
    import_package.available = false;
    c_compiler_initialize();

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
        SCOPE_EXIT(input_reset(input));
        {
            if (!window_handle_messages(window, true)) {
                break;
            }
            if (input->close_request_issued || (input->key_pressed[(int)Key_Code::E] && input->key_down[(int)Key_Code::CTRL])) {
                window_save_position(window, "import_gui_pos.set");
                window_close(window);
                break;
            }
            if (input->key_pressed[(int)Key_Code::F11]) {
                Window_State* state = window_get_window_state(window);
                window_set_fullscreen(window, !state->fullscreen);
            }
        }

        // Generate GUI
        {
            auto window = gui_add_node(gui_root_handle(), gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_rect(vec4(0.6, 0.8f, 0.6f, 1.0f)));
            gui_node_set_position_fixed(window, vec2(0), Anchor::CENTER_CENTER, true);
            auto file_dialog = gui_add_node(window, gui_size_make_fill(), gui_size_make_fit(), gui_drawable_make_none());
            gui_node_set_layout(file_dialog, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::CENTER);
            gui_push_text_description(file_dialog, "Header filepath: ");
            gui_push_text_edit(file_dialog, &header_filepath);

            if (gui_push_button(file_dialog, string_create_static("Open file")))
            {
                auto result = file_io_open_file_selection_dialog();
                if (result.available) {
                    string_reset(&header_filepath);
                    string_append_string(&header_filepath, &result.value);
                }
            }

            if (gui_push_button(file_dialog, string_create_static("Parse")) && header_filepath.size != 0) 
            {
                import_package = c_importer_import_header(&c_importer, header_filepath, &identifier_pool);
                if (import_package.available) {
                    importer.package = &import_package.value;
                    dynamic_array_reset(&importer.symbols_to_import);

                    auto iter = hashtable_iterator_create(&import_package.value.symbol_table.symbols);
                    while (hashtable_iterator_has_next(&iter))
                    {
                        String* id = *iter.key;
                        C_Import_Symbol* symbol = iter.value;
                        SCOPE_EXIT(hashtable_iterator_next(&iter));

                        Symbol_Import symbol_import;
                        symbol_import.c_symbol = symbol;
                        symbol_import.enabled = false;
                        symbol_import.name = id;
                        dynamic_array_push_back(&importer.symbols_to_import, symbol_import);
                    }
                }
            }

            // Show a list of symbols or something
            gui_push_text(window, string_create_static("Available Imports:"));
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_rect(vec4(0.0f, 0.0f, 0.0f, 1.0f)));
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding

            {
                auto filter_area = gui_push_text_description(window, "Filter: ");
                String* filter = gui_store_string(filter_area, "");
                auto info = gui_push_text_edit(filter_area, filter);

                // Rank all import symbols by filter with fuzzy search
                if (info.text_changed)
                {
                    fuzzy_search_start_search(*filter);
                    for (int i = 0; i < importer.symbols_to_import.size; i++) {
                        fuzzy_search_add_item(*importer.symbols_to_import[i].name, i);
                    }
                    fuzzy_search_results = fuzzy_search_rank_results(true, 25);
                }
            }

            auto horizontal = gui_add_node(window, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_none());
            gui_node_set_layout(horizontal, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::MAX);
            auto import_area = gui_push_scroll_area(horizontal, gui_size_make_fill(), gui_size_make_fill());
            auto selected_area = gui_add_node(horizontal, gui_size_make_preferred(200), gui_size_make_fill(), gui_drawable_make_rect(vec4(0.8f)));
            gui_push_text(selected_area, string_create_static("Selected: "));
            gui_add_node(selected_area, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding
            gui_add_node(selected_area, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_rect(vec4(0.0f, 0.0f, 0.0f, 1.0f)));
            gui_add_node(selected_area, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding

            if (import_package.available)
            {
                String tmp = string_create_empty(128);
                SCOPE_EXIT(string_destroy(&tmp));
                auto gui_push_import_symbol = [&](int index) 
                {
                    auto& symbol = importer.symbols_to_import[index];
                    vec4 color = symbol.enabled ? vec4(0.5f, 1.0f, 0.5f, 1.0f) : vec4(0.4f);
                    gui_add_node(import_area, gui_size_make_fill(), gui_size_make_fixed(2), gui_drawable_make_none());
                    auto symbol_area = gui_add_node(import_area, gui_size_make_fill(), gui_size_make_fit(), gui_drawable_make_rect(color, 1, vec4(0,0,0,1), 4));
                    gui_node_set_padding(symbol_area, 2, 2, false);
                    gui_node_set_layout(symbol_area, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::CENTER);
                    gui_push_toggle(symbol_area, &symbol.enabled);

                    string_reset(&tmp);
                    string_append(&tmp, symbol.name->characters);
                    switch (symbol.c_symbol->type)
                    {
                    case C_Import_Symbol_Type::FUNCTION: string_append(&tmp, " fn: "); break;
                    case C_Import_Symbol_Type::GLOBAL_VARIABLE: string_append(&tmp, " global: "); break;
                    case C_Import_Symbol_Type::TYPE: string_append(&tmp, " type: "); break;
                    default: panic("");
                    }
                    c_import_type_append_to_string(symbol.c_symbol->data_type, &tmp, 0, false);

                    gui_push_text(symbol_area, tmp);
                };

                const int MAX_ITEMS = 200;
                if (fuzzy_search_results.size == 0) {
                    for (int i = 0; i < importer.symbols_to_import.size && i < MAX_ITEMS; i++) {
                        gui_push_import_symbol(i);
                    }
                }
                else {
                    for (int i = 0; i < fuzzy_search_results.size && i < MAX_ITEMS; i++) {
                        gui_push_import_symbol(fuzzy_search_results[i].user_index);
                    }
                }

                for (int i = 0; i < importer.symbols_to_import.size; i++) {
                    auto& symbol = importer.symbols_to_import[i];
                    if (!symbol.enabled) continue;
                    gui_push_text(selected_area, *symbol.name);
                }
            }

            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_rect(vec4(0.0f, 0.0f, 0.0f, 1.0f)));
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding
            auto bottom = gui_add_node(window, gui_size_make_fill(), gui_size_make_fit(), gui_drawable_make_none());
            gui_node_set_layout(bottom, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::CENTER);
            gui_push_text(bottom, string_create_static("Output_filename: "));
            String* out_filename = gui_store_string(bottom, "output.upp");
            gui_push_text_edit(bottom, out_filename);
            if (gui_push_button(bottom, string_create_static("Create file")) && import_package.available && out_filename->size > 0) 
            {
                printf("Generating output\n");
            }
        }


        // Rendering
        {
            rendering_core_prepare_frame(timer_current_time_in_seconds(&timer), window_state->width, window_state->height);
            gui_update_and_render(gui_pass);

            text_renderer_reset(text_renderer);
            rendering_core_render(camera, Framebuffer_Clear_Type::COLOR_AND_DEPTH);
            window_swap_buffers(window);
        }


        // Sleep
        {
            const int TARGET_FPS = 60;
            const double SECONDS_PER_FRAME = 1.0 / TARGET_FPS;
            timer_sleep_until(&timer, time_frame_start + SECONDS_PER_FRAME);
        }
    }

    return 0;
}
