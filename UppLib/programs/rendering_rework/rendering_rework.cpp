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
#include "../../rendering/opengl_utils.hpp"
#include "../../rendering/gpu_buffers.hpp"

#include <Windows.h>





// File listener
typedef void (*file_change_callback_fn)(String filepath, void* userdata);

struct File_Change_Callback
{
    file_change_callback_fn callback_fn;
    void* userdata;

    static File_Change_Callback make(file_change_callback_fn callback_fn, void* userdata);
};

File_Change_Callback File_Change_Callback::make(file_change_callback_fn callback_fn, void* userdata) {
    File_Change_Callback result;
    result.callback_fn = callback_fn;
    result.userdata = userdata;
    return result;
}

struct Monitored_Directory;
struct Monitored_File
{
    String filepath; // Full path to file
    int directory_index;
    u64 last_write_time;
    List<File_Change_Callback> callbacks;
};

struct Monitored_Directory
{
    String path;
    HANDLE handle;
    int callback_count; // If 0, then we don't need to look for changes
    DynArray<Monitored_File> files;
};

struct File_Monitor
{
    Arena* arena;
    Free_List free_list_callbacks;
    DynArray<Monitored_Directory> dirs;

    static File_Monitor* create(Arena* arena);
    void check_for_changes(); // Non-blocking, should probably be called once per frame
    bool add_file_change_callback(String filepath, File_Change_Callback callback);    // Returns true if worked
    bool remove_file_change_callback(String filepath, File_Change_Callback callback); // Returns true if the callback was in the system
    Monitored_File* find_file(String filepath, bool create_if_exists);
};

File_Monitor* File_Monitor::create(Arena* arena)
{
    File_Monitor* result = arena->allocate<File_Monitor>();
    result->arena = arena;
    result->dirs = DynArray<Monitored_Directory>::create(arena);
    result->free_list_callbacks = List<File_Change_Callback>::create_suitable_free_list(arena);
    return result;
}

Monitored_File* File_Monitor::find_file(String filepath, bool create_if_exists)
{
    Monitored_File* monitored_file = nullptr;

    SCRATCH_ARENA_MAKE_SCOPED(this->arena);
    DynString full_path = DynString::create(scratch_arena);
    full_path.append(filepath);
    filepath_relative_to_absolute_path(&full_path);

    String dir_path = filepath_get_parts(full_path.to_string()).directory;

    bool file_info_queried = false;
    File_Info file_info;

    int directory_index = -1;
    for (int i = 0; i < this->dirs.size; i++)
    {
        Monitored_Directory* stored_dir = &this->dirs[i];
        if (stored_dir->path == dir_path) {
            directory_index = i;
            break;
        }
    }

    // Create new directory if necessary
    if (directory_index == -1)
    {
        if (!create_if_exists) {
            return nullptr;
        }

        // Check if file exist before creating a new directory
        if (!file_info_queried) {
            file_info = file_io_get_file_info(filepath);
            file_info_queried = true;
        }
        if (file_info.status != File_Info_Status::SUCCESS) {
            return nullptr;
        }

        Monitored_Directory new_dir;
        new_dir.callback_count = 0;
        new_dir.files = DynArray<Monitored_File>::create(this->arena);
        new_dir.path = string_allocate_copy(dir_path, this->arena->upcast());
        new_dir.handle = FindFirstChangeNotificationA(
            DynString::create(scratch_arena).reset()->append(dir_path)->to_cstring(),
            FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE
        );
        this->dirs.push_back(new_dir);
        directory_index = this->dirs.size - 1;
    }

    // Check if file already exists
    Monitored_File* file = nullptr;
    Monitored_Directory* dir = &this->dirs[directory_index];
    for (int i = 0; i < dir->files.size; i++)
    {
        if (dir->files[i].filepath == full_path.to_string()) {
            file = &dir->files[i];
            break;
        }
    }

    // Create new file if necessary
    if (file == nullptr)
    {
        if (!create_if_exists) {
            return nullptr;
        }

        // Check if file exist before creating a new directory
        if (!file_info_queried) {
            file_info = file_io_get_file_info(filepath);
            file_info_queried = true;
        }
        if (file_info.status != File_Info_Status::SUCCESS) {
            return nullptr;
        }

        Monitored_File new_file;
        new_file.callbacks = List<File_Change_Callback>::create(this->free_list_callbacks.upcast());
        new_file.directory_index = directory_index;
        new_file.filepath = string_allocate_copy(full_path.to_string(), this->arena->upcast());
        new_file.last_write_time = file_info.last_write_access_time;
        dir->files.push_back(new_file);
        file = &dir->files[dir->files.size - 1];
    }

    // Add callback to file
    return file;
}

bool File_Monitor::add_file_change_callback(String filepath, File_Change_Callback callback)
{
    Monitored_File* file =  this->find_file(filepath, true);
    if (file == nullptr) return false;
    file->callbacks.append(callback);
    this->dirs[file->directory_index].callback_count += 1;
    return true;
}

bool File_Monitor::remove_file_change_callback(String filepath, File_Change_Callback callback) 
{
    Monitored_File* file =  this->find_file(filepath, false);
    if (file == nullptr) return false;
    auto node = file->callbacks.head;
    while (node != nullptr)
    {
        File_Change_Callback stored_callback = node->value;
        if (callback.callback_fn == stored_callback.callback_fn && callback.userdata == stored_callback.userdata) 
        {
            file->callbacks.remove_node(node);
            Monitored_Directory* dir = &this->dirs[file->directory_index];
            dir->callback_count -= 1;
            assert(dir->callback_count >= 0, "");
            return true;
        }
        node = node->next;
    }
    return false; // Found file, but did not find callback
}

void File_Monitor::check_for_changes()
{
    for (int i = 0; i < this->dirs.size; i++)
    {
        Monitored_Directory* dir = &this->dirs[i];
        assert(dir->callback_count >= 0, "");
        if (dir->callback_count == 0) continue;

        DWORD wait_result = WaitForSingleObject(dir->handle, 0);
        if (wait_result != WAIT_OBJECT_0) { // == Wait_Object_0 means that the object was signaled
            continue;
        }
        // After wait has been satisfied, we need to renew the call
        BOOL success = FindNextChangeNotification(dir->handle);
        if (!success) {
            logg("FindNextChangeNotification error?!\n");
        }

        // Check for each watched file in dir if the last-write time changed
        for (int j = 0; j < dir->files.size; j++)
        {
            Monitored_File* file = &dir->files[j];
            File_Info new_file_info = file_io_get_file_info(file->filepath);
            if (new_file_info.status != File_Info_Status::SUCCESS) continue;
            if (file->last_write_time != new_file_info.last_write_access_time)
            {
                file->last_write_time = new_file_info.last_write_access_time;
                auto node = file->callbacks.head;
                while (node != nullptr)
                {
                    File_Change_Callback* callback = &node->value;
                    callback->callback_fn(file->filepath, callback->userdata);
                    node = node->next;
                }
            }
        }
    }
}



struct GRX_Core;
struct GRX_Shader;

struct GRX_Shader
{
    GRX_Core* core;
    String filepath;
    GLuint program_id;
    bool valid; // Set to true if shader was compiled successfully
};

struct GRX_Core
{
    Arena* arena;
    File_Monitor* file_monitor;
    bool logging_enabled;

    // Shaders
    String shader_directory; // base-path for querying shaders
    DynTable<String, GRX_Shader*> shaders;
};

void grx_core_log(GRX_Core* core, String msg)
{
    if (!core->logging_enabled) return;
    printf("GRX_Core log: %.*s\n", msg.size, msg.characters);
}

void grx_shader_compile(GRX_Shader* shader)
{
    GRX_Core* core = shader->core;
    SCRATCH_ARENA_MAKE_SCOPED(core->arena);
    DynString tmp_string = DynString::create(scratch_arena);
    String filepath = shader->filepath;

    shader->valid = false;
    if (shader->program_id == 0) {
        shader->program_id = glCreateProgram();
        assert(shader->program_id != 0, "glCreateProgram failed");
    }

    bool success = true;
    auto helper_log_error = [&](String str, int line_number = -1) 
    {
        ARENA_MAKE_SCOPED_CHECKPOINT(scratch_arena);
        DynString output = DynString::create(scratch_arena);
        output.reset();
        if (line_number != -1) {
            output.append(filepath_get_parts(filepath).filename)->append_formated(".%d ", line_number);
        }
        output.append("Error: ")->append(str)->append("\n");
        grx_core_log(core, output.to_string());
        success = false;
        shader->valid = false;
    };

    // Load file
    String file_content = string_make("");
    {
        DynString dyn_file_content = DynString::create(scratch_arena);
        bool file_loaded = file_io_load_text_file(filepath, &dyn_file_content);
        if (!file_loaded)
        {
            helper_log_error(string_make("Could not load shader file"), 0);
            return;
        }
        file_content = dyn_file_content.to_string();
    }

    // Split file_content into Shader-Parts and shared-code
    DynString common_code = DynString::create(scratch_arena);
    DynArray<Shader_Part> parts = DynArray<Shader_Part>::create(scratch_arena);
    {
        int index = 0;
        bool currently_inside_part = false;
        DynString current_part_string = DynString::create(scratch_arena);
        Shader_Part_Type current_part_type;
        int line_index = 0;
        while (index < file_content.size)
        {
            // Find line end
            int line_start = index;
            int next_line_end = string_find_character_index(file_content, '\n', line_start).unwrap(file_content.size);
            line_index += 1; // We start at line 1

            String line = string_make_substring(file_content, index, next_line_end);
            index = next_line_end + 1; // Skip \n or end of file

            // Handle line
            line = string_get_whitespace_trimmed(line);

            // Parse line info
            bool is_part_start = false;
            Shader_Part_Type part_type;
            bool is_part_end = false;
            if (string_starts_with(line, string_make("//@VERTEX"))) {
                is_part_start = true;
                part_type = Shader_Part_Type::VERTEX;
            }
            else if (string_starts_with(line, string_make("//@FRAGMENT"))) {
                is_part_start = true;
                part_type = Shader_Part_Type::FRAGMENT;
            }
            else if (string_starts_with(line, string_make("//@END"))) {
                is_part_end = true;
            }

            // Handle parsed info
            if (is_part_start) 
            {
                if (currently_inside_part) {
                    helper_log_error(string_make("Encountered part start, but we are aleady in a block"), line_index);
                    continue;
                }
                for (int i = 0; i < parts.size; i++) {
                    if (parts[i].type == part_type) {
                        helper_log_error(string_make("Part already exists"), line_index);
                        break;
                    }
                }
                currently_inside_part = true;
                current_part_type = part_type;
                current_part_string = DynString::create(scratch_arena);
            }
            else if (is_part_end)
            {
                if (!currently_inside_part) {
                     helper_log_error(string_make("Encountered //@END, but we aren't in a block"), line_index);
                     success = false;
                     continue;
                 }
                currently_inside_part = false;

                Shader_Part new_part;
                new_part.type = current_part_type;
                new_part.source_code = current_part_string.to_string();
                current_part_string = DynString::create(scratch_arena);
                parts.push_back(new_part);
            }
            else
            {
                String line = string_make_substring(file_content, line_start, next_line_end);
                if (currently_inside_part) {
                    current_part_string.append(line)->append("\n");
                }
                else {
                    common_code.append(line)->append("\n");
                }
            }
        }
    }

    if (!success) {
        return;
    }

    // Create full code for each part (Add shared code at start + handle #includes)
    DynSet<String> already_loaded_filepaths = DynSet<String>::create(scratch_arena, string_hash_pointer, string_equals_pointers);
    DynString include_file_content = DynString::create(scratch_arena);
    for (int i = 0; i < parts.size; i++)
    {
        Shader_Part& part = parts[i];
        switch (part.type)
        {
        case Shader_Part_Type::VERTEX: {
            break;
        }
        case Shader_Part_Type::FRAGMENT: {
            break;
        }
        default: panic(""); break;
        }

        DynString final_string_on_stack = DynString::create(scratch_arena);
        DynString* final_string = &final_string_on_stack;
        final_string->append("#version 430 core\n\n");
        final_string->append("// COMMON CODE:\n\n");
        final_string->append(common_code);
        final_string->append("\n\n");
        final_string->append("// ENTRY CODE:\n\n");
        final_string->append(part.source_code);
        final_string->append("\n");

        // Handle includes
        already_loaded_filepaths.reset();
        int index = 0;
        while (index < final_string->size)
        {
            // Find line start/end
            int line_start = index;
            int line_end = string_find_character_index(final_string->to_string(), '\n', index).unwrap(final_string->size);
            String line_string = string_make_substring(final_string->to_string(), line_start, line_end);
            line_string = string_get_whitespace_trimmed(line_string);

            // Check if it's an include
            String_Split_Pair split = string_split_once(line_string, ' ');
            split.a = string_get_whitespace_trimmed(split.a);
            split.b = string_get_whitespace_trimmed(split.b);
            if (split.a != string_make("#include")) {
                index = line_end + 1;
                continue;
            }

            // Check if filename is correct
            if (!string_starts_with(split.b, string_make("\"")) || !string_ends_with(split.b, string_make("\""))) {
                helper_log_error(
                    tmp_string.reset()->append("#include filename did not start/end in quotation ")->append_in_quotation(split.b)->to_string(),
                    0
                );
                index = line_end + 1;
                continue;
            }

            // Get full filepath and check if it's already loaded
            String include_filepath = string_make_substring(split.b, 1, split.b.size - 1);
            tmp_string.reset()->append(include_filepath);
            filepath_relative_to_full_path_with_start(&tmp_string, filepath);
            bool already_loaded = !already_loaded_filepaths.insert(filepath);
            if (already_loaded) {
                index = line_end + 1;
                continue;
            }
            
            bool include_file_loaded = file_io_load_text_file(include_filepath, include_file_content.reset());
            if (!include_file_loaded) {
                helper_log_error(
                    tmp_string.reset()->append("Could not load include file ")->append_in_quotation(include_filepath)->to_string()
                );
                index = line_end + 1;
                continue;
            }

            // Otherwise, we delete the #include from the file-content, and paste the loaded file instead
            dynstring_remove_substring(final_string, line_start, line_end);
            dynstring_insert(final_string, include_file_content.to_string(), line_start);
        }

        part.source_code = final_string->to_string();

        // logg(tmp_string.reset()->append("Shader part:\n")->append(final_string)->append("\n")->to_cstring());
    }

    // Afterwards we need to compile and link the shaders...
    Shader_Compile_Result compile_result = opengl_utils_compile_shader_from_parts(parts.as_array(), scratch_arena, shader->program_id);
    if (compile_result.success) 
    {
        shader->program_id = compile_result.program_id;
        shader->valid = true;
        grx_core_log(
            core,
            tmp_string.reset()
                ->append("Successfully compiled shader ")
                ->append_in_quotation(filepath_get_parts(shader->filepath).filename)->to_string()
        );
    }
    else 
    {
        shader->valid = false;
        logg(
            "Could not compile shader \"%.*s\", errors: \n%.*s\n\n",
            filepath.size, filepath.characters,
            compile_result.error_message.size, compile_result.error_message.characters
        );
    }
}

GRX_Core* grx_core_create(Arena* arena, File_Monitor* file_monitor, String shader_directory)
{
    SCRATCH_ARENA_MAKE_SCOPED(arena);
    DynString tmp_string = dynstring_create(scratch_arena);

    GRX_Core* core = arena->allocate<GRX_Core>();
    core->arena = arena;
    core->file_monitor = file_monitor;
    core->logging_enabled = true;
    core->shaders = DynTable<String, GRX_Shader*>::create(arena, hash_string_pointer, string_equals_pointers);

    // Copy shader directory as absolute directory
    {
        tmp_string.reset()->append(shader_directory);
        filepath_relative_to_absolute_path(&tmp_string);
        core->shader_directory = string_allocate_copy(tmp_string.to_string(), arena->upcast());
    }

    return core;
}

void shader_hotreload_entry(String filepath, void* userdata)
{
    GRX_Shader* shader = (GRX_Shader*)userdata;
    grx_shader_compile(shader);
}

GRX_Shader* grx_core_query_shader(GRX_Core* core, String filepath)
{
    SCRATCH_ARENA_MAKE_SCOPED(core->arena);

    DynString full_filepath = DynString::create(scratch_arena);
    full_filepath.append(filepath);
    filepath_relative_to_full_path_with_start(&full_filepath, core->shader_directory);

    auto shader_query = core->shaders.query(full_filepath.to_string(), true);
    if (shader_query.exists) {
        return shader_query.get_value();
    }

    // Otherwise create new Shader
    GRX_Shader* new_shader = core->arena->allocate<GRX_Shader>();
    new_shader->core = core;
    new_shader->filepath = string_allocate_copy(full_filepath.to_string(), core->arena->upcast());
    new_shader->program_id = 0;
    new_shader->valid = false;
    grx_shader_compile(new_shader);

    // Add hot-reloading to shader
    if (file_io_get_file_info(new_shader->filepath).status == File_Info_Status::SUCCESS) 
    {
        core->file_monitor->add_file_change_callback(
            new_shader->filepath, 
            File_Change_Callback::make(shader_hotreload_entry, (void*)new_shader)
        );
    }

    shader_query.insert_key_value(new_shader->filepath, new_shader);
    return new_shader;
}





void rendering_rework_entry()
{
    Arena main_arena_on_stack = Arena::create();
    Arena* arena = &main_arena_on_stack;

    // Create window
    Window* window = window_create(string_make("Test"), 0, true);
    SCOPE_EXIT(window_destroy(window));
    window_set_cursor_constrain(window, false);
    window_set_cursor_visibility(window, true);
    window_set_cursor_reset_into_center(window, false);
    window_load_position(window, string_make("window_pos.set"));
    window_set_vsync(window, true);
    window_set_focus(window);

    // Create GRX_Core
    File_Monitor* file_monitor = File_Monitor::create(arena);
    GRX_Core* grx_core = grx_core_create(arena, file_monitor, string_make("resources/grx_shaders"));
    GRX_Shader* test_shader = grx_core_query_shader(grx_core, string_make("test.glsl"));

    DynArray<vec2> vertices = DynArray<vec2>::create(arena);
    vertices.push_back(vec2(-0.5, -0.5));
    vertices.push_back(vec2( 0.5, -0.5));
    vertices.push_back(vec2( 0.0,  0.5));

    GPU_Buffer ssbo = gpu_buffer_create(vertices.to_byte_array(), GPU_Buffer_Type::SHADER_STORAGE_BUFFER, GPU_Buffer_Usage::DYNAMIC);
    gpu_buffer_bind_indexed(&ssbo, 1);

    GLuint vao;
    glGenVertexArrays(1, &vao);

    // Window Loop
    int frame = 0;
    double time_last_update_start = timer_current_time_in_seconds();
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
                window_save_position(window, string_make("window_pos.set"));
                window_close(window);
                break;
            }
            if (input->key_pressed[(int)Key_Code::F11]) {
                Window_State* state = window_get_window_state(window);
                window_set_fullscreen(window, !state->fullscreen);
            }
        }

        // Update
        {
            file_monitor->check_for_changes();
        }

        double time_input_end = timer_current_time_in_seconds();

        // Rendering
        {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            Window_State* state = window_get_window_state(window);
            glViewport(0, 0, state->width, state->height);

            if (test_shader->valid) {
                glBindVertexArray(vao);
                glUseProgram(test_shader->program_id);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }

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
