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

struct Symbol_Import
{
    String* name;
    C_Import_Symbol* c_symbol;
    bool enabled;
};

const int LIST_COUNT = 5;

struct Importer
{
    C_Import_Package* package;
    Dynamic_Array<Symbol_Import> symbols_to_import;
    Hashtable<C_Import_Type*, String> type_translations; // Type access string in upp
    String* text; // Text that's beeing currently worked on
    String header_filepath;
    String struct_definitions;

    Dynamic_Array<String> lists[LIST_COUNT];
    Dynamic_Array<String>* include_dirs;
    Dynamic_Array<String>* libs;
    Dynamic_Array<String>* sources;
    Dynamic_Array<String>* lib_dirs;
    Dynamic_Array<String>* defines;

    int name_counter;
};

Importer importer;

void importer_initialize()
{
    importer.symbols_to_import = dynamic_array_create<Symbol_Import>();
    importer.type_translations = hashtable_create_pointer_empty<C_Import_Type*, String>(32);
    importer.text = 0;
    importer.package = 0;
    importer.struct_definitions = string_create_empty(16);
    importer.name_counter = 0;
    importer.header_filepath = string_create_empty(16);

    for (int i = 0; i < LIST_COUNT; i++) {
        importer.lists[i] = dynamic_array_create<String>();
    }
    importer.include_dirs = &importer.lists[0];
    importer.sources = &importer.lists[1];
    importer.lib_dirs = &importer.lists[2];
    importer.libs = &importer.lists[3];
    importer.defines = &importer.lists[4];
}

// Writes to importer.text
void output_c_import_type(C_Import_Type* type, bool decay_array_type)
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
            case C_Import_Primitive::FLOAT:       string_append(&access_name, "float"); break;
            case C_Import_Primitive::DOUBLE:      string_append(&access_name, "f64"); break; 
            case C_Import_Primitive::LONG_DOUBLE: string_append(&access_name, "f64"); assert(type->byte_size == 8, "");  break;
            default:  // Integer types
            {
                switch (type->byte_size)
                {
                case 1: string_append(&access_name, "i8"); break; // Not sure if this is the best way because of char/unsiged char differences
                case 2: string_append(&access_name, "i16"); break;
                case 4: string_append(&access_name, "int"); break;
                case 8: string_append(&access_name, "i64"); break;
                default: panic("Unknown byte size");
                }
                break;
            }
            }
        }
        else
        {
            switch (type->primitive)
            {
            case C_Import_Primitive::FLOAT:       string_append(&access_name, "float"); break;
            case C_Import_Primitive::DOUBLE:      string_append(&access_name, "f64"); break; 
            case C_Import_Primitive::LONG_DOUBLE: string_append(&access_name, "f64"); assert(type->byte_size == 8, "");  break;
            default:  // Integer types
            {
                switch (type->byte_size)
                {
                case 1: string_append(&access_name, "u8"); break; // Not sure if this is the best way because of char/unsiged char differences
                case 2: string_append(&access_name, "u16"); break;
                case 4: string_append(&access_name, "u32"); break;
                case 8: string_append(&access_name, "u64"); break;
                default: panic("Unknown byte size");
                }
                break;
            }
            }
        }
        break;
    }
    case C_Import_Type_Type::POINTER: 
    {
        // Check for void pointer...
        if (type->pointer_child_type->type == C_Import_Type_Type::PRIMITIVE) {
            if (type->pointer_child_type->primitive == C_Import_Primitive::VOID_TYPE) {
                string_append(&access_name, "byte_pointer");
                break;
            }
        }

        // Check for function pointer...
        if (type->pointer_child_type->type == C_Import_Type_Type::FUNCTION_SIGNATURE) {
            // In Upp function pointers don't have a *
            output_c_import_type(type->pointer_child_type, true);
            break;
        }

        string_append(&access_name, "*");
        output_c_import_type(type->pointer_child_type, true);
        break;
    }
    case C_Import_Type_Type::UNKNOWN_TYPE: {
        // Note: I don't think that the size of unknown types is correctly set by importer, but we'll try anyway
        string_append_formated(
            &importer.struct_definitions, "extern struct Unknown_Import_Type_%d(%d, %d)\n", importer.name_counter, type->byte_size, type->alignment);
        string_append_formated(&access_name, "Unknown_Import_Type_%d", importer.name_counter);
        importer.name_counter += 1;
        break;
    }
    case C_Import_Type_Type::ARRAY: {
        // Problem: We now translate from C to Upp, the question here becomes: When are arrays downgraded to pointers?
        // Note: If the array size isn't given, the type is automatically degraded to pointer in C-Importer, e.g. int values[];
        if (decay_array_type) {
            output_c_import_type(type->pointer_child_type, true);
            string_append(&access_name, "*");
        }
        else {
            string_append_formated(&access_name, "[%d]", type->array.array_size);
            output_c_import_type(type->array.element_type, false);
        }
        break;
    }
    case C_Import_Type_Type::ENUM: {
        // I'm assuming that most enums have a name, and if not, we generate one
        if (type->enumeration.is_anonymous) {
            string_append_formated(&access_name, "Anon_Import_Enum_%d");
            importer.name_counter += 1;
        }
        else {
            string_append(&access_name, type->enumeration.id->characters);
        }

        string_append_formated(&importer.struct_definitions, "%s :: enum\n", access_name.characters);
        for (int i = 0; i < type->enumeration.members.size; i++) {
            auto& member = type->enumeration.members[i];
            string_append_formated(&importer.struct_definitions, "    %s :: %d\n", member.id->characters, member.value);
        }
        string_append(&importer.struct_definitions, "\n");
        break;
    }
    case C_Import_Type_Type::FUNCTION_SIGNATURE:
    {
        auto& sig = type->function_signature;
        string_append(&access_name, "(");
        for (int i = 0; i < sig.parameters.size; i++) 
        {
            auto& param = sig.parameters[i];

            if (((int)param.type->qualifiers & (int)C_Type_Qualifiers::CONST_QUAL) == 0) {
                // In Upp all parameters are constant by default, but in C they are mutable by default
                string_append(&access_name, "mut ");
            }

            if (param.has_name) {
                string_append(&access_name, param.id->characters);
            }
            else {
                string_append_formated(&access_name, "param_%d", i);
            }

            string_append(&access_name, ": ");
            output_c_import_type(param.type, true);

            if (i != sig.parameters.size - 1) {
                string_append(&access_name, ", ");
            }
        }
        string_append(&access_name, ")");

        if (!(sig.return_type->type == C_Import_Type_Type::PRIMITIVE && sig.return_type->primitive == C_Import_Primitive::VOID_TYPE)) {
            string_append(&access_name, " -> ");
            output_c_import_type(sig.return_type, true);
        }

        break;
    }
    case C_Import_Type_Type::STRUCTURE:
    {
        auto& structure = type->structure;
        // Generate name
        if (structure.is_anonymous) {
            string_append_formated(&access_name, "Anon_Import_Struct_%d", importer.name_counter);
            importer.name_counter += 1;
        }
        else {
            string_append(&access_name, structure.id->characters);
        }

        // Struct can contain pointers to themselves, so we need to store the access_name before generating members
        hashtable_insert_element(&importer.type_translations, type, access_name);

        // Note: Since this function is called recursively, we cannot just append to struct_definitions here, but we have to write to intermediate string
        String definition = string_create_empty(8);
        SCOPE_EXIT(string_destroy(&definition));
        string_append_formated(&definition, "%s :: %s\n", access_name.characters, structure.is_union ? "union" : "struct");

        // Note: From headers we sometimes only know the forward definition, but not the content, in this case the c-generator return 0/0 as size
        //      There is probably a better way to handle these cases
        if (structure.contains_bitfield || (type->byte_size == 0 && type->alignment == 0)) {
            string_append_formated(&definition, "\tpadding__: [%d]u8\n\n", type->byte_size == 0 ? 1 : type->byte_size);
        }
        else
        {
            // Note: There could be some translation problems with anonymous structs here, 
            //       as it is not possible to create a variable of an anonymous struct in C, but in Upp it is.
            for (int i = 0; i < structure.members.size; i++)
            {
                auto& member = structure.members[i];
                string_append(&definition, "\t");
                string_append(&definition, member.id->characters);
                string_append(&definition, ": ");
                importer.text = &definition;
                output_c_import_type(member.type, false);
                string_append(&definition, "\n");
            }
            string_append(&definition, "\n");
        }
        string_append_formated(&definition, "extern struct %s\n", access_name.characters);

        string_append(&importer.struct_definitions, definition.characters);
        string_append(backup, access_name.characters);
        return;
    }
    default: panic("");
    }

    // Cache access name, so types don't get duplicated
    hashtable_insert_element(&importer.type_translations, type, access_name);

    // Output access name
    string_append(backup, access_name.characters);
    return;
}

void output_import_interface(String* output_filename)
{
    string_reset(&importer.struct_definitions);
    hashtable_for_each_value(&importer.type_translations, string_destroy);
    hashtable_reset(&importer.type_translations);
    importer.text = 0;
    importer.name_counter = 0;

    String result = string_create_empty(256);
    SCOPE_EXIT(string_destroy(&result));

    // Append sources
    {
        for (int i = 0; i < importer.libs->size; i++) {
            string_append_formated(&result, "extern lib \"%s\"\n", importer.libs->data[i].characters);
        }
        for (int i = 0; i < importer.lib_dirs->size; i++) {
            string_append_formated(&result, "extern lib_dir \"%s\"\n", importer.lib_dirs->data[i].characters);
        }
        for (int i = 0; i < importer.sources->size; i++) {
            string_append_formated(&result, "extern source \"%s\"\n", importer.sources->data[i].characters);
        }
        for (int i = 0; i < importer.include_dirs->size; i++) {
            string_append_formated(&result, "extern header_dir \"%s\"\n", importer.include_dirs->data[i].characters);
        }
        string_append_formated(&result, "extern header \"%s\"\n", importer.header_filepath.characters);
        for (int i = 0; i < importer.defines->size; i++) {
            string_append_formated(&result, "extern definition \"%s\"\n", importer.defines->data[i].characters);
        }
        string_append(&result, "\n");
    }

    String tmp = string_create_empty(16);
    SCOPE_EXIT(string_destroy(&tmp));
    for (int i = 0; i < importer.symbols_to_import.size; i++)
    {
        auto& symbol = importer.symbols_to_import[i];
        if (!symbol.enabled) continue;

        switch (symbol.c_symbol->type)
        {
        case C_Import_Symbol_Type::FUNCTION: {
            importer.text = &result;
            string_append_formated(importer.text, "extern function %s: ", symbol.name->characters);
            output_c_import_type(symbol.c_symbol->data_type, false);
            string_append(importer.text, "\n");
            break;
        }
        case C_Import_Symbol_Type::GLOBAL_VARIABLE: {
            importer.text = &result;
            string_append_formated(importer.text, "extern global %s: ", symbol.name->characters);
            output_c_import_type(symbol.c_symbol->data_type, false);
            string_append(importer.text, "\n");
            break;
        }
        case C_Import_Symbol_Type::TYPE: {
            // Write access to tmp
            string_reset(&tmp);
            importer.text = &tmp;
            output_c_import_type(symbol.c_symbol->data_type, false);
            break;
        }
        default: panic("");
        }
    }

    // Append struct definitions
    string_append(&result, "\n\n");
    string_append_string(&result, &importer.struct_definitions);

    // Write to file
    file_io_write_file(output_filename->characters, array_create_static<byte>((byte*)result.characters, result.size));
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
    importer_initialize();

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
    C_Importer c_importer = c_importer_create();
    SCOPE_EXIT(c_importer_destroy(&c_importer));
    Identifier_Pool identifier_pool = identifier_pool_create();
    SCOPE_EXIT(identifier_pool_destroy(&identifier_pool));

    Dynamic_Array<Fuzzy_Item> fuzzy_search_results = dynamic_array_create<Fuzzy_Item>();

    Optional<C_Import_Package> import_package;
    import_package.available = false;
    c_compiler_initialize();

    vec2 default_char_size = text_renderer_get_aligned_char_size(text_renderer, 0.4f);

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
            auto window = gui_add_node(gui_root_handle(), gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_rect(vec4(0.6f, 0.8f, 0.6f, 1.0f)));
            gui_node_set_position_fixed(window, vec2(0), Anchor::CENTER_CENTER, true);
            auto file_dialog = gui_add_node(window, gui_size_make_fill(), gui_size_make_fit(), gui_drawable_make_none());
            gui_node_set_layout(file_dialog, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::CENTER);
            gui_push_text_description(file_dialog, "Header filepath: ");
            gui_push_text_edit(file_dialog, &importer.header_filepath, default_char_size);

            if (gui_push_button(file_dialog, string_create_static("Open file")))
            {
                String tmp = string_create();
                SCOPE_EXIT(string_destroy(&tmp));
                bool available = file_io_open_file_selection_dialog(&tmp);
                if (available) {
                    string_reset(&importer.header_filepath);
                    string_append_string(&importer.header_filepath, &tmp);
                    string_replace_character(&importer.header_filepath, '\\', '/');
                }
            }

            if (gui_push_button(file_dialog, string_create_static("Parse")) && importer.header_filepath.size != 0) 
            {
                import_package = c_importer_import_header(&c_importer, importer.header_filepath, &identifier_pool, *importer.include_dirs, *importer.defines);
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
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_rect(vec4(0.0f, 0.0f, 0.0f, 1.0f)));
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding

            // Options
            {
                auto desc = gui_push_text_description(window, "Options: ");
                if (gui_push_button(desc, string_create_static("Select all"))) {
                    for (int i = 0; i < importer.symbols_to_import.size; i++) {
                        importer.symbols_to_import[i].enabled = true;
                    }
                }
                if (gui_push_button(desc, string_create_static("Deselect all"))) {
                    for (int i = 0; i < importer.symbols_to_import.size; i++) {
                        importer.symbols_to_import[i].enabled = false;
                    }
                }


                static const char* last_open_name = "";
                Dynamic_Array<String>** active_list_ptr = gui_store_primitive<Dynamic_Array<String>*>(desc, nullptr);
                if (gui_push_button(desc, string_create_static("Include dirs"))) {
                    *active_list_ptr = importer.include_dirs;
                    last_open_name = "Include dirs";
                }
                if (gui_push_button(desc, string_create_static("Libs"))) {
                    last_open_name = "Libs";
                    *active_list_ptr = importer.libs;
                }
                if (gui_push_button(desc, string_create_static("Lib-Dirs"))) {
                    last_open_name = "Lib-Dirs";
                    *active_list_ptr = importer.lib_dirs;
                }
                if (gui_push_button(desc, string_create_static("Sources"))) {
                    last_open_name = "Sources";
                    *active_list_ptr = importer.sources;
                }
                if (gui_push_button(desc, string_create_static("Defines"))) {
                    last_open_name = "Defines";
                    *active_list_ptr = importer.defines;
                }

                // Edit active list
                if (*active_list_ptr != nullptr)
                {
                    Dynamic_Array<String>* active_list = *active_list_ptr;
                    auto window = gui_add_node(
                        gui_root_handle(), gui_size_make(400, true, false, false, 0), gui_size_make(300, true, false, false, 0), gui_drawable_make_rect(vec4(1.0f), 1)
                    );
                    gui_node_set_position_fixed(window, vec2(0.0f), Anchor::CENTER_CENTER, true);
                    gui_push_text(window, string_create_static(last_open_name));

                    bool should_close = gui_push_button(window, string_create_static("Close"));

                    // Show a text edit + a remove button for each entry
                    for (int i = 0; i < active_list->size; i++) {
                        String& string = active_list->data[i];
                        auto node = gui_add_node(window, gui_size_make_fill(), gui_size_make_fit(), gui_drawable_make_none());
                        gui_node_set_layout(node, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::CENTER);
                        bool should_remove = gui_push_button(node, string_create_static("Remove"));

                        if (gui_push_button(node, string_create_static("Open")))
                        {
                            String tmp = string_create();
                            SCOPE_EXIT(string_destroy(&tmp));
                            bool available = file_io_open_file_selection_dialog(&tmp);
                            if (available) {
                                string_reset(&string);
                                string_append_string(&string, &tmp);
                                string_replace_character(&string, '\\', '/');
                            }
                        }

                        gui_push_text_edit(node, &string, default_char_size);


                        if (should_remove) {
                            dynamic_array_swap_remove(active_list, i);
                            i = i - 1;
                            continue;
                        }
                    }
                    
                    if (gui_push_button(window, string_create_static("Add Entry"))) {
                        String new_string = string_create_empty(8);
                        dynamic_array_push_back(active_list, new_string);
                    }

                    if (should_close) {
                        *active_list_ptr = nullptr;
                    }
                }
            }
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_rect(vec4(0.0f, 0.0f, 0.0f, 1.0f)));
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding

            // Import section
            gui_push_text(window, string_create_static("Available Imports:"));
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_rect(vec4(0.0f, 0.0f, 0.0f, 1.0f)));
            gui_add_node(window, gui_size_make_fill(), gui_size_make_fixed(2.0f), gui_drawable_make_none()); // Padding

            {
                auto filter_area = gui_push_text_description(window, "Filter: ");
                String* filter = gui_store_string(filter_area, "");
                auto info = gui_push_text_edit(filter_area, filter, default_char_size);

                // Rank all import symbols by filter with fuzzy search
                if (info.text_changed)
                {
                    fuzzy_search_start_search(*filter, 30);
                    for (int i = 0; i < importer.symbols_to_import.size; i++) {
                        fuzzy_search_add_item(*importer.symbols_to_import[i].name, i);
                    }
                    fuzzy_search_results = fuzzy_search_get_results(true, 14);
                }
            }

            auto horizontal = gui_add_node(window, gui_size_make_fill(), gui_size_make_fill(), gui_drawable_make_none());
            gui_node_set_layout(horizontal, GUI_Stack_Direction::LEFT_TO_RIGHT, GUI_Alignment::MAX);
            auto import_area = gui_push_scroll_area(horizontal, gui_size_make_fill(), gui_size_make_fill());
            auto selected_area = gui_push_scroll_area(horizontal, gui_size_make_preferred(200), gui_size_make_fill());
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
            gui_push_text_edit(bottom, out_filename, default_char_size);
            if (gui_push_button(bottom, string_create_static("Create file")) && import_package.available && out_filename->size > 0) 
            {
                printf("Generating output\n");
                output_import_interface(out_filename);
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
