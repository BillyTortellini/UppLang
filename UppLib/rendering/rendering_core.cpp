#include "rendering_core.hpp"

#include "../utility/utils.hpp"
#include "../utility/file_listener.hpp"
#include "../utility/file_io.hpp"
#include "../datastructures/string.hpp"
#include "../rendering/opengl_utils.hpp"
#include "../rendering/texture.hpp"
#include "../rendering/framebuffer.hpp"

// GLOBAL
Rendering_Core rendering_core;



Render_Information render_information_make(int viewport_width, int viewport_height, int window_width, int window_height, float monitor_dpi, float current_time)
{
    Render_Information info;
    info.viewport_width = (float)viewport_width;
    info.viewport_height = (float)viewport_height;
    info.window_width = window_width;
    info.window_height = window_height;
    info.monitor_dpi = monitor_dpi;
    info.current_time_in_seconds = current_time;
    return info;
}

void rendering_core_initialize(int window_width, int window_height, float monitor_dpi)
{
    auto& result = rendering_core;
    result.pipeline_state = pipeline_state_make_default();
    pipeline_state_set_unconditional(&result.pipeline_state);
    result.file_listener = file_listener_create();
    result.opengl_state = opengl_state_create();

    result.ubo_render_information = gpu_buffer_create_empty(sizeof(Render_Information), GPU_Buffer_Type::UNIFORM_BUFFER, GPU_Buffer_Usage::DYNAMIC);
    gpu_buffer_bind_indexed(&result.ubo_render_information, 0);
    result.ubo_camera_data = gpu_buffer_create_empty(sizeof(Camera_3D_UBO_Data), GPU_Buffer_Type::UNIFORM_BUFFER, GPU_Buffer_Usage::DYNAMIC);
    gpu_buffer_bind_indexed(&result.ubo_camera_data, 1);

    result.render_information.viewport_height = 0;
    result.render_information.viewport_width = 0;
    result.render_information.monitor_dpi = monitor_dpi;
    result.render_information.window_width = window_width;
    result.render_information.window_height = window_height;

    result.window_size_listeners = dynamic_array_create_empty<Window_Size_Listener>(1);
    result.vertex_attributes = dynamic_array_create_empty<Vertex_Attribute_Base*>(1);
    result.vertex_descriptions = dynamic_array_create_empty<Vertex_Description*>(1);
    result.meshes = hashtable_create_empty<String, Mesh*>(4, hash_string, string_equals);
    result.shaders = hashtable_create_empty<String, Shader*>(4, hash_string, string_equals);
    result.render_passes = hashtable_create_empty<String, Render_Pass*>(4, hash_string, string_equals);
    result.framebuffers = hashtable_create_empty<String, Framebuffer*>(4, hash_string, string_equals);

    result.predefined.position3D = vertex_attribute_make<vec3>("Position3D");
    result.predefined.position2D = vertex_attribute_make<vec2>("Position2D");
    result.predefined.textureCoordinates = vertex_attribute_make<vec2>("TextureCoordinates");
    result.predefined.normal = vertex_attribute_make<vec3>("Normal");
    result.predefined.tangent = vertex_attribute_make<vec3>("Tangent");
    result.predefined.bitangent = vertex_attribute_make<vec3>("Bitangent");
    result.predefined.color3 = vertex_attribute_make<vec3>("Color3");
    result.predefined.color4 = vertex_attribute_make<vec4>("Color4");
    result.predefined.index = vertex_attribute_make<uint32>("IndexBuffer");

    result.main_pass = rendering_core_query_renderpass("main", pipeline_state_make_default(), nullptr);
}

void rendering_core_destroy()
{
    auto& core = rendering_core;
    gpu_buffer_destroy(&core.ubo_camera_data);
    gpu_buffer_destroy(&core.ubo_render_information);
    file_listener_destroy(core.file_listener);
    opengl_state_destroy(&core.opengl_state);
    dynamic_array_destroy(&core.window_size_listeners);

    for (int i = 0; i < core.vertex_descriptions.size; i++) {
        auto desc = core.vertex_descriptions[i];
        array_destroy(&desc->attributes);
        delete desc;
    }
    dynamic_array_destroy(&core.vertex_descriptions);

    for (int i = 0; i < core.vertex_attributes.size; i++) {
        auto attrib = core.vertex_attributes[i];
        delete core.vertex_attributes[i];
    }
    dynamic_array_destroy(&core.vertex_attributes);

    {
        auto it = hashtable_iterator_create(&core.meshes);
        while (hashtable_iterator_has_next(&it)) {
            Mesh* mesh = *(it.value);
            for (int i = 0; i < mesh->buffers.size; i++) {
                auto buffer = mesh->buffers[i];
                dynamic_array_destroy(&buffer.attribute_data);
                gpu_buffer_destroy(&buffer.gpu_buffer);
            }
            array_destroy(&mesh->buffers);
            hashtable_iterator_next(&it);
            glDeleteVertexArrays(1, &mesh->vao);
            delete mesh;
        }
        hashtable_destroy(&core.meshes);
    }

    {
        auto it = hashtable_iterator_create(&core.shaders);
        while (hashtable_iterator_has_next(&it)) {
            Shader* shader = *(it.value);
            if (shader->program_id != 0) {
                glDeleteProgram(shader->program_id);
                shader->program_id = 0;
            }
            for (int i = 0; i < shader->allocated_strings.size; i++) {
                string_destroy(&shader->allocated_strings[i]);
            }
            dynamic_array_destroy(&shader->allocated_strings);
            dynamic_array_destroy(&shader->uniform_infos);
            dynamic_array_destroy(&shader->input_layout);
            hashtable_iterator_next(&it);
            delete shader;
        }
        hashtable_destroy(&core.shaders);
    }

    {
        auto it = hashtable_iterator_create(&core.render_passes);
        while (hashtable_iterator_has_next(&it)) {
            Render_Pass* pass = *(it.value);
            dynamic_array_destroy(&pass->commands);
            dynamic_array_destroy(&pass->dependents);
            delete pass;
            hashtable_iterator_next(&it);
        }
        hashtable_destroy(&core.render_passes);
    }
    {
        auto it = hashtable_iterator_create(&core.framebuffers);
        while (hashtable_iterator_has_next(&it)) {
            Framebuffer* buffer = *(it.value);
            framebuffer_destroy(buffer);
            hashtable_iterator_next(&it);
        }
        hashtable_destroy(&core.framebuffers);
    }
}

void rendering_core_add_window_size_listener(window_size_changed_callback callback, void* userdata)
{
    auto core = &rendering_core;
    Window_Size_Listener listener;
    listener.callback = callback;
    listener.userdata = userdata;
    dynamic_array_push_back(&core->window_size_listeners, listener);
}

void rendering_core_remove_window_size_listener(void* userdata)
{
    auto core = &rendering_core;
    int found = -1;
    for (int i = 0; i < core->window_size_listeners.size; i++) {
        if (core->window_size_listeners[i].userdata == userdata) {
            found = i;
        }
    }
    if (found == -1) panic("Should not happen i guess");
    dynamic_array_swap_remove(&core->window_size_listeners, found);
}

void renderpass_queue_if_no_dependencies(Render_Pass* pass, Dynamic_Array<Render_Pass*>* execution_order)
{
    if (pass->dependency_count == 0) {
        dynamic_array_push_back(execution_order, pass);
        pass->dependency_count = -1; // So we don't queue twice in a frame
    }
    for (int i = 0; i < pass->dependents.size; i++) {
        pass->dependents[i]->dependency_count -= 1;
        renderpass_queue_if_no_dependencies(pass->dependents[i], execution_order);
    }
}

void rendering_core_render(Camera_3D* camera, Framebuffer_Clear_Type clear_type, float current_time, int window_width, int window_height)
{
    auto& core = rendering_core;

    // Update file listeners and window size changed listeners
    {
        file_listener_check_if_files_changed(core.file_listener);
        if ((window_width != core.render_information.window_width || window_height != core.render_information.window_height) &&
            (window_width != 0 && window_height != 0))
        {
            core.render_information.window_width = window_width;
            core.render_information.window_height = window_height;
            for (int i = 0; i < core.window_size_listeners.size; i++) {
                auto& listener = core.window_size_listeners[i];
                listener.callback(listener.userdata);
            }
        }
    }

    // Update common UBO's
    {
        // Viewport
        core.render_information.viewport_width = window_width;
        core.render_information.viewport_height = window_height;
        glViewport(0, 0, window_width, window_height);

        // Time
        core.render_information.current_time_in_seconds = current_time;
        gpu_buffer_update(&core.ubo_render_information, array_create_static_as_bytes(&core.render_information, 1));

        // Camera
        Camera_3D_UBO_Data data = camera_3d_ubo_data_make(camera);
        gpu_buffer_update(&core.ubo_camera_data, array_create_static_as_bytes(&data, 1));

        // Bind default framebuffer
        rendering_core_clear_bound_framebuffer(clear_type);
    }

    // Upload all changed mesh data to gpu
    {
        auto it = hashtable_iterator_create(&core.meshes);
        while (hashtable_iterator_has_next(&it))
        {
            Mesh* mesh = *it.value;
            mesh->queried_this_frame = false; // Reset
            mesh->primitive_count = 0;
            Attribute_Buffer* index_buffer = 0;
            for (int i = 0; i < mesh->buffers.size; i++)
            {
                auto& buffer = mesh->buffers[i];
                auto attribute = mesh->description->attributes[i];

                // Calculate how many triangles we have
                {
                    if (attribute == core.predefined.index) {
                        index_buffer = &buffer;
                    }
                    int primitive_count = buffer.attribute_data.size / shader_datatype_get_info(attribute->type).byte_size;
                    if (mesh->primitive_count == 0) {
                        mesh->primitive_count = primitive_count;
                    }
                    if (mesh->primitive_count != primitive_count && attribute != core.predefined.index) {
                        logg("Mesh has different count of vertex attributes!");
                        mesh->primitive_count = math_minimum(mesh->primitive_count, primitive_count);
                    }
                }

                // Skip if nothing changed
                if (!buffer.dirty) {
                    continue;
                }

                // Upload data to gpu
                buffer.dirty = false;
                gpu_buffer_update(&buffer.gpu_buffer, dynamic_array_as_bytes(&buffer.attribute_data));
                if (mesh->reset_every_frame) {
                    buffer.dirty = true;
                    dynamic_array_reset(&buffer.attribute_data);
                }
            }

            if (index_buffer != 0) {
                mesh->primitive_count = index_buffer->attribute_data.size / sizeof(u32);
            }

            hashtable_iterator_next(&it);
        }
    }

    // Execute all renderpasses
    {
        // Generate execution queue based on dependencies of passes
        auto execution_queue = dynamic_array_create_empty<Render_Pass*>(core.render_passes.element_count);
        auto it = hashtable_iterator_create(&core.render_passes);
        
        while (hashtable_iterator_has_next(&it)) {
            Render_Pass* pass = *it.value;
            renderpass_queue_if_no_dependencies(pass, &execution_queue);
            pass->queried_this_frame = false; // Reset
            hashtable_iterator_next(&it);
        }
        if (execution_queue.size != core.render_passes.element_count) {
            panic("There is a cyclic dependency in the render-passes, shouldn't happen!");
        }

        // Execute the renderpasses in order
        for (int i = 0; i < execution_queue.size; i++) 
        {
            auto& pass = execution_queue[i];
            dynamic_array_reset(&pass->dependents);
            pass->dependency_count = 0;

            // Set opengl state
            rendering_core_update_pipeline_state(pass->pipeline_state);
            if (pass->output_buffer != 0) {
                opengl_state_bind_framebuffer(pass->output_buffer->framebuffer_id);
            }
            else {
                opengl_state_bind_framebuffer(0);
            }

            // Execute commands
            for (int i = 0; i < pass->commands.size; i++)
            {
                auto& command = pass->commands[i];
                if (command.type == Render_Pass_Command_Type::UNIFORM)
                {
                    auto& uniform = command.uniform;
                    opengl_state_bind_program(uniform.shader->program_id);

                    // Find uniform location in shader
                    Uniform_Info* info = 0;
                    {
                        for (int k = 0; k < uniform.shader->uniform_infos.size; k++) {
                            auto& inf = uniform.shader->uniform_infos[k];
                            if (string_equals_cstring(&inf.uniform_name, uniform.value.name)) {
                                info = &inf;
                                break;
                            }
                        }
                        if (info == 0) {
                            logg("Couldn't find uniform: %s in shader %s\n", uniform.value.name, uniform.shader->filename);
                            continue; // Next command
                        }
                    }

                    // Test uniform type
                    if (info->type != uniform.value.datatype || info->array_size != 1) {
                        logg("Uniform type does not match for uniform: %s in shader %s\n", uniform.value.name, uniform.shader->filename);
                        continue;
                    }

                    // Set uniform
                    auto value = uniform.value;
                    switch (uniform.value.datatype)
                    {
                    case Shader_Datatype::UINT32: glUniform1ui(info->location, value.data_u32); break;
                    case Shader_Datatype::FLOAT: glUniform1f(info->location, value.data_float); break;
                    case Shader_Datatype::VEC2: glUniform2fv(info->location, 1, (GLfloat*)&value.data_vec2); break;
                    case Shader_Datatype::VEC3: glUniform3fv(info->location, 1, (GLfloat*)&value.data_vec3); break;
                    case Shader_Datatype::VEC4: glUniform4fv(info->location, 1, (GLfloat*)&value.data_vec4); break;
                    case Shader_Datatype::MAT2: glUniformMatrix2fv(info->location, 1, GL_FALSE, (GLfloat*)&value.data_mat2); break;
                    case Shader_Datatype::MAT3: glUniformMatrix3fv(info->location, 1, GL_FALSE, (GLfloat*)&value.data_mat3); break;
                    case Shader_Datatype::MAT4: glUniformMatrix4fv(info->location, 1, GL_FALSE, (GLfloat*)&value.data_mat4); break;
                    case Shader_Datatype::TEXTURE_2D_BINDING:
                        glUniform1i(
                            info->location, 
                            opengl_state_bind_texture_to_next_free_unit(
                                Texture_Binding_Type::TEXTURE_2D, value.texture.texture->texture_id, value.texture.sampling_mode));
                        break;
                    }
                }
                else if (command.type == Render_Pass_Command_Type::DRAW_CALL)
                {
                    auto& mesh = command.draw_call.mesh;
                    auto& shader = command.draw_call.shader;
                    if (shader->program_id == 0) {
                        continue;
                    }

                    // Check if mesh and shader are compatible
                    {
                        bool compatible = true;
                        for (int k = 0; k < shader->input_layout.size; k++) {
                            bool found = false;
                            for (int m = 0; m < mesh->description->attributes.size; m++) {
                                if (shader->input_layout[k].attribute == mesh->description->attributes[m]) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                logg("Mesh does not contain all attributes, missing: %s\n", shader->input_layout[k].attribute->name.characters);
                                compatible = false;
                                break;
                            }
                        }
                        if (!compatible) {
                            continue;
                        }
                    }

                    // Bind necessary state
                    opengl_state_bind_program(shader->program_id);
                    opengl_state_bind_vao(mesh->vao);

                    // Draw
                    if (mesh->has_element_buffer) {
                        glDrawElements((GLenum)mesh->topology, mesh->primitive_count, GL_UNSIGNED_INT, (GLvoid*)0);
                    }
                    else {
                        glDrawArrays((GLenum)mesh->topology, 0, mesh->primitive_count);
                    }
                }
            }

            dynamic_array_reset(&pass->commands);
        }
    }
}

void rendering_core_update_pipeline_state(Pipeline_State new_state) {
    auto& core = rendering_core;
    pipeline_state_switch(core.pipeline_state, new_state);
    core.pipeline_state = new_state;
}

void rendering_core_clear_bound_framebuffer(Framebuffer_Clear_Type clear_type)
{
    switch (clear_type)
    {
    case Framebuffer_Clear_Type::NONE:
        break;
    case Framebuffer_Clear_Type::COLOR:
        glClear(GL_COLOR_BUFFER_BIT);
        break;
    case Framebuffer_Clear_Type::DEPTH:
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        break;
    case Framebuffer_Clear_Type::COLOR_AND_DEPTH:
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        break;
    default: panic("Should not happen");
    }
}

Vertex_Attribute_Base* vertex_attribute_make_base(Shader_Datatype type, const char* name)
{
    auto& core = rendering_core;
    String str = string_create_static(name);
    for (int i = 0; i < core.vertex_attributes.size; i++) {
        auto attrib = core.vertex_attributes[i];
        if (string_equals(&str, &attrib->name)) {
            if (type == attrib->type) {
                return attrib;
            }
            else {
                panic("Attributes names must be unique, otherwise we don't know what to bind with the shader program!\n");
            }
        }
    }

    Vertex_Attribute_Base* base = new Vertex_Attribute_Base();
    base->name = str;
    base->type = type;
    static int next_free_binding = 0;
    base->binding_location = next_free_binding;
    next_free_binding += math_round_next_multiple(shader_datatype_get_info(type).byte_size, 16) / 16;
    dynamic_array_push_back(&rendering_core.vertex_attributes, base);

    if (next_free_binding > 16) {
        panic("All bindings were exhausted, maybe we should do something smarter now!");
    }

    return base;
}

Vertex_Description* vertex_description_create(std::initializer_list<Vertex_Attribute_Base*> attributes)
{
    auto& core = rendering_core;
    // Check if the description already exists
    for (int i = 0; i < core.vertex_descriptions.size; i++)
    {
        auto& description = core.vertex_descriptions[i];
        bool all_found = true;
        for (auto required_attribute : attributes)
        {
            bool found = false;
            for (int k = 0; k < description->attributes.size; k++)
            {
                auto existing_attribute = description->attributes[k];
                if (required_attribute == existing_attribute) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                all_found = false;
                break;
            }
        }
        if (all_found) {
            return description;
        }
    }

    // Sanity check
    bool index_buffer_found = false;
    for (auto required_attribute : attributes) {
        if (required_attribute == core.predefined.index) {
            if (index_buffer_found) {
                panic("Vertex description cannont contain 2 index buffer!");
            }
            index_buffer_found = true;
        }
    }

    // Create new description
    Vertex_Description* description = new Vertex_Description;
    description->attributes = array_create_from_list(attributes);
    dynamic_array_push_back(&core.vertex_descriptions, description);
    return description;
}

Mesh* rendering_core_query_mesh(const char* name, Vertex_Description* description, Mesh_Topology topology, bool reset_every_frame)
{
    auto& core = rendering_core;
    auto found = hashtable_find_element(&core.meshes, string_create_static(name));
    if (found != 0) {
        auto mesh = *found;
        if (description != mesh->description) {
            panic("Found mesh with the same name but different description, but names must be unique!");
        }
        if (mesh->queried_this_frame) {
            panic("Mesh was already queried, names must be unique!!!");
        }
        mesh->queried_this_frame = true;
        return mesh;
    }

    Mesh* mesh = new Mesh;
    hashtable_insert_element(&core.meshes, string_create_static(name), mesh);
    mesh->description = description;
    mesh->buffers = array_create_empty<Attribute_Buffer>(description->attributes.size);
    mesh->queried_this_frame = true;
    mesh->has_element_buffer = false;
    mesh->reset_every_frame = reset_every_frame;
    mesh->topology = topology;
    mesh->primitive_count = 0;
    for (int i = 0; i < mesh->buffers.size; i++) {
        auto& buffer = mesh->buffers[i];
        auto attribute = description->attributes[i];
        buffer.dirty = mesh - reset_every_frame;
        buffer.gpu_buffer = gpu_buffer_create_empty(
            1,
            attribute == core.predefined.index ? GPU_Buffer_Type::INDEX_BUFFER : GPU_Buffer_Type::VERTEX_BUFFER,
            mesh->reset_every_frame ? GPU_Buffer_Usage::DYNAMIC : GPU_Buffer_Usage::STATIC
        );
        buffer.attribute_data = dynamic_array_create_empty<byte>(1);

        if (attribute == core.predefined.index) {
            mesh->has_element_buffer = true;
        }
    }
    hashtable_insert_element(&core.meshes, string_create_static(name), mesh);

    glGenVertexArrays(1, &mesh->vao);
    opengl_state_bind_vao(mesh->vao);
    SCOPE_EXIT(opengl_state_bind_vao(0));
    for (int i = 0; i < mesh->buffers.size; i++)
    {
        auto& buffer = mesh->buffers[i];
        auto attrib = mesh->description->attributes[i];
        if (attrib == core.predefined.index)
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.gpu_buffer.id); // Binds index_buffer to vao
        }
        else
        {
            auto info = shader_datatype_get_info(attrib->type);
            glBindBuffer(GL_ARRAY_BUFFER, buffer.gpu_buffer.id);
            // We can bind only 4 float at a time, this is how opengl works...
            for (int i = 0; i < math_round_next_multiple(info.byte_size, 16) / 16; i++)
            {
                glEnableVertexAttribArray(attrib->binding_location + i);
                int64 offset = i * 16;
                glVertexAttribPointer(
                    attrib->binding_location,
                    info.byte_size / 4, // WARNING: This only works for 4 byte values like float, integers, vectors of floats and matrices!!!
                    info.vertexAttribType,
                    GL_FALSE,
                    info.byte_size,
                    (void*)offset
                );
            }
        }
    }

    return mesh;
}

void shader_file_changed_callback(void* userdata, const char* filename)
{
    Shader* shader = (Shader*)userdata;

    // Load file
    auto shader_code_opt = file_io_load_text_file(filename);
    SCOPE_EXIT(file_io_unload_text_file(&shader_code_opt));
    if (!shader_code_opt.available) {
        panic("File listener file wasnt able to read!");
    }

    // Reset shader
    {
        if (shader->program_id != 0) {
            glDeleteProgram(shader->program_id);
        }
        shader->program_id = glCreateProgram();
        if (shader->program_id == 0) {
            panic("Shouldn_t happen!\n");
        }

        dynamic_array_reset(&shader->uniform_infos);
        dynamic_array_reset(&shader->input_layout);
        for (int i = 0; i < shader->allocated_strings.size; i++) {
            string_destroy(&shader->allocated_strings[i]);
        }
        dynamic_array_reset(&shader->allocated_strings);
    }

    // Recompile and add shader-stages to the program
    {
        int possible_shader_defines_count = 6;
        String possible_shader_defines[] = {
            string_create_static("VERTEX"),
            string_create_static("FRAGMENT"),
            string_create_static("GEOMETRY"),
            string_create_static("COMPUTE"),
            string_create_static("TESSELATION_CONTROL"),
            string_create_static("TESSELATION_EVALUATION"),
        };
        GLenum possible_shader_define_types[] = {
            GL_VERTEX_SHADER,
            GL_FRAGMENT_SHADER,
            GL_GEOMETRY_SHADER,
            GL_COMPUTE_SHADER,
            GL_TESS_CONTROL_SHADER,
            GL_TESS_EVALUATION_SHADER,
        };
        // Note: This array corresponds to the Shader_Datatypes enum!
        const int type_count = 9;
        String type_names[] = {
            string_create_static("float"),
            string_create_static("uint"),
            string_create_static("vec2"),
            string_create_static("vec3"),
            string_create_static("vec4"),
            string_create_static("mat2"),
            string_create_static("mat3"),
            string_create_static("mat4"),
            string_create_static("sampler2D"),
        };

        auto createAndAttachShader = [](GLenum shadertype, GLuint program_id, String& shadercode) {
            GLint shader_id = glCreateShader(shadertype);
            const char* sources[] = { "#version 430 core\n\n", shadercode.characters };
            glShaderSource(shader_id, 2, sources, 0);
            logg("compiling shader: \n\n%s\n\n", shadercode.characters);
            glCompileShader(shader_id);
            opengl_utils_check_shader_compilation_status(shader_id);
            glAttachShader(program_id, shader_id);
            glDeleteShader(shader_id); // Will only be deleted after the program is deleted
            string_clear(&shadercode);
        };

        // Split input into lines
        String buffer = string_create_empty(256);
        SCOPE_EXIT(string_destroy(&buffer));
        Array<String> lines = string_split(shader_code_opt.value, '\n');
        SCOPE_EXIT(string_split_destroy(lines));
        GLenum shaderType = GL_INVALID_ENUM;
        int slot_index = 0;
        bool inside_code = false; // Code only starts after #define
        for (int i = 0; i < lines.size; i++)
        {
            auto line = lines[i];

            // Do pattern matching
            // Shader type defines
            String escape_sequence = string_create_static("//@");
            String ifdef = string_create_static("#ifdef");
            String endif = string_create_static("#endif");
            if (string_compare_substring(&line, 0, &ifdef))
            {
                // Split line into words
                Array<String> words = string_split(line, ' ');
                SCOPE_EXIT(string_split_destroy(words));
                if (words.size != 2) {
                    logg("Shader error, couldn't parse #ifdef!\n");
                    continue; // Next line
                }
                String stage_name = words[1];
                if (stage_name.size > 0 && stage_name.characters[words[1].size - 1] == '\r') {
                    stage_name.size -= 1;
                }

                bool worked = false;
                for (int j = 0; j < possible_shader_defines_count; j++)
                {
                    if (!string_equals(&stage_name, &possible_shader_defines[j])) {
                        continue;
                    }
                    shaderType = possible_shader_define_types[j];
                    worked = true;
                    break;
                }
                if (!worked) {
                    logg("Could not comprehend ifdef\n");
                }
                else {
                    inside_code = true;
                }
                continue; // GOTO next line
            }

            if (string_compare_substring(&line, 0, &endif)) {
                createAndAttachShader(shaderType, shader->program_id, buffer);
                string_reset(&buffer);
                inside_code = false;
            }

            // Skip none code lines
            if (!inside_code) {
                continue;
            }

            // Input layout
            if (shaderType == GL_VERTEX_SHADER &&
                string_compare_substring(&line, 0, &string_create_static("in")) ||
                string_compare_substring(&line, 0, &string_create_static("inout")))
            {
                Shader_Input_Info input_info;

                // Split line into words
                Array<String> words = string_split(line, ' ');
                SCOPE_EXIT(string_split_destroy(words));
                if (words.size < 3) {
                    logg("Shader error, couldn't parse in/inout attribute!\n");
                    continue; // Next line
                }

                // Variable name
                {
                    String var_name = words[2];
                    if (var_name.size == 0) {
                        panic("Shouldnt happen with split...");
                    }
                    if (var_name.size >= 1 && var_name.characters[var_name.size - 1] == ';') {
                        var_name.size -= 1;
                    }
                    if (var_name.size == 0) {
                        logg("Shader error, expected variable name!\n");
                        continue;
                    }
                    input_info.variable_name = string_copy(var_name);
                    dynamic_array_push_back(&shader->allocated_strings, input_info.variable_name);
                }

                // Parse type + set location
                Shader_Datatype datatype;
                {
                    String type = words[1];
                    int type_index = -1;
                    for (int j = 0; j < type_count; j++) {
                        if (string_equals(&type_names[j], &type)) {
                            type_index = j;
                            break;
                        }
                    }
                    if (type_index == -1) {
                        logg("Shader error, couldn't parse input type!\n");
                        continue;
                    }
                    datatype = (Shader_Datatype)type_index;
                }

                // Parse attribute name
                {
                    String suffix = words[words.size - 1];
                    if (suffix.size < escape_sequence.size + 1) {
                        logg("Expected valid suffix for inout qualifier");
                        continue;
                    }
                    if (!string_compare_substring(&suffix, 0, &escape_sequence)) {
                        logg("Expected valid suffix");
                        continue;
                    }
                    if (suffix.size > 0 && suffix.characters[suffix.size - 1] == '\r') {
                        suffix.size -= 1;
                    }
                    if (suffix.size <= 0) {
                        logg("Expected valid suffix");
                        continue;
                    }
                    String attribute_name = string_create_substring(&suffix, escape_sequence.size, suffix.size);
                    dynamic_array_push_back(&shader->allocated_strings, attribute_name);
                    input_info.attribute = vertex_attribute_make_base(datatype, attribute_name.characters);
                    input_info.location = input_info.attribute->binding_location;
                }

                // Store Vertex attribute
                dynamic_array_push_back(&shader->input_layout, input_info);
                string_append_formated(&buffer, "layout (location = %d) ", input_info.attribute->binding_location);
            }

            // Add line to buffer if its not a special line
            string_append_string(&buffer, &line);
            string_append(&buffer, "\n");
        }

        // Add the final shader_stage to the program
        if (inside_code) {
            logg("Last endif is missing in shader!\n");
            createAndAttachShader(shaderType, shader->program_id, buffer);
        }
    }

    // Link program
    if (!opengl_utils_link_program_and_check_errors(shader->program_id)) {
        glDeleteProgram(shader->program_id);
        shader->program_id = 0;
        return;
    }

    // Recheck attribute information (Lookout for unused attributes)
    for (int i = 0; i < shader->input_layout.size; i++)
    {
        auto& info = shader->input_layout[i];
        GLint attrib_location = glGetAttribLocation(shader->program_id, info.variable_name.characters);
        // Check if attribute is actually in use
        if (attrib_location == -1) {
            dynamic_array_swap_remove(&shader->input_layout, i);
            i -= 1;
            continue;
        }
        assert(attrib_location == info.location, "Must not happen");
    }

    // Query uniform Information
    {
        GLint uniform_count;
        glGetProgramiv(shader->program_id, GL_ACTIVE_UNIFORMS, &uniform_count);
        dynamic_array_reserve(&shader->uniform_infos, uniform_count);

        int longest_uniform_name_length;
        glGetProgramiv(shader->program_id, GL_ACTIVE_UNIFORM_MAX_LENGTH, &longest_uniform_name_length);
        String buffer = string_create_empty(longest_uniform_name_length);
        SCOPE_EXIT(string_destroy(&buffer));

        // Loop over all uniforms
        for (int i = 0; i < uniform_count; i++)
        {
            // Get Infos
            GLenum type; // Type of uniform
            GLint array_size; // Size, e.g. for arrays? or vector components?
            glGetActiveUniform(
                shader->program_id, (GLuint)i, buffer.capacity, &buffer.size,
                &array_size, &type, (GLchar*)buffer.characters
            );

            Uniform_Info info;
            info.array_size = array_size;
            info.uniform_name = string_copy(buffer);
            info.location = glGetUniformLocation(shader->program_id, buffer.characters);
            dynamic_array_push_back(&shader->allocated_strings, info.uniform_name);

            switch (type)
            {
            case GL_FLOAT: info.type = Shader_Datatype::FLOAT; break;
            case GL_FLOAT_VEC2: info.type = Shader_Datatype::VEC2; break;
            case GL_FLOAT_VEC3: info.type = Shader_Datatype::VEC3; break;
            case GL_FLOAT_VEC4: info.type = Shader_Datatype::VEC4; break;
            case GL_FLOAT_MAT2: info.type = Shader_Datatype::MAT2; break;
            case GL_FLOAT_MAT3: info.type = Shader_Datatype::MAT3; break;
            case GL_FLOAT_MAT4: info.type = Shader_Datatype::MAT4; break;
            case GL_UNSIGNED_INT: info.type = Shader_Datatype::UINT32; break;
            case GL_SAMPLER_2D: info.type = Shader_Datatype::TEXTURE_2D_BINDING; break;
            default: panic("Unrecognized or unimplemented datatype encountered\n");
            }

            dynamic_array_push_back(&shader->uniform_infos, info);
        }
    }
}

Shader* rendering_core_query_shader(const char* filename)
{
    auto& core = rendering_core;
    auto found = hashtable_find_element(&core.shaders, string_create_static(filename));
    if (found != 0) {
        auto shader = *found;
        if (shader->filename != filename) {
            panic("Found shader with the same name but different filename!\n");
        }
        return shader;
    }

    Shader* shader = new Shader;
    hashtable_insert_element(&core.shaders, string_create_static(filename), shader);
    shader->filename = filename;
    shader->program_id = 0;
    shader->input_layout = dynamic_array_create_empty<Shader_Input_Info>(1);
    shader->uniform_infos = dynamic_array_create_empty<Uniform_Info>(1);
    shader->allocated_strings = dynamic_array_create_empty<String>(1);

    if (file_listener_add_file(core.file_listener, shader->filename, shader_file_changed_callback, shader) == 0) {
        panic("Shader file does not exist!");
    }
    shader_file_changed_callback(shader, filename);

    return shader;
}

template<> Shader_Datatype shader_datatype_of<Texture*>() { return Shader_Datatype::TEXTURE_2D_BINDING; };
template<> Shader_Datatype shader_datatype_of<float>() { return Shader_Datatype::FLOAT; };
template<> Shader_Datatype shader_datatype_of<uint32>() { return Shader_Datatype::UINT32; };
template<> Shader_Datatype shader_datatype_of<vec2>() { return Shader_Datatype::VEC2; };
template<> Shader_Datatype shader_datatype_of<vec3>() { return Shader_Datatype::VEC3; };
template<> Shader_Datatype shader_datatype_of<vec4>() { return Shader_Datatype::VEC4; };
template<> Shader_Datatype shader_datatype_of<mat2>() { return Shader_Datatype::MAT2; };
template<> Shader_Datatype shader_datatype_of<mat3>() { return Shader_Datatype::MAT3; };
template<> Shader_Datatype shader_datatype_of<mat4>() { return Shader_Datatype::MAT4; };


template<typename T>
Uniform_Value uniform_make_base(const char* name, T data) {
    Uniform_Value val;
    val.datatype = shader_datatype_of<T>();
    val.name = name;
    memcpy(&val.buffer[0], &data, sizeof(T));
    return val;
}

Uniform_Value uniform_make(const char* name, float val) { return uniform_make_base(name, val); }
Uniform_Value uniform_make(const char* name, vec2 val) { return uniform_make_base(name, val); }
Uniform_Value uniform_make(const char* name, vec3 val) {return uniform_make_base(name, val); }
Uniform_Value uniform_make(const char* name, vec4 val) {return uniform_make_base(name, val); }
Uniform_Value uniform_make(const char* name, mat2 val) {return uniform_make_base(name, val); }
Uniform_Value uniform_make(const char* name, mat3 val) {return uniform_make_base(name, val); }
Uniform_Value uniform_make(const char* name, mat4 val) { return uniform_make_base(name, val); }
Uniform_Value uniform_make(const char* name, int val) { return uniform_make_base(name, val); }

Uniform_Value uniform_make(const char* name, Texture* data, Sampling_Mode sampling_mode) {
    Uniform_Value val;
    val.datatype = shader_datatype_of<Texture*>();
    val.name = name;
    val.texture.texture = data;
    val.texture.sampling_mode = sampling_mode;
    return val;
}

Shader_Datatype_Info shader_datatype_get_info(Shader_Datatype type)
{
    auto maker = [](GLenum uniformType, GLenum attribType, const char* name, u32 byte_size) -> Shader_Datatype_Info {
        Shader_Datatype_Info info;
        info.uniformType = uniformType;
        info.vertexAttribType = attribType;
        info.name = name;
        info.byte_size = byte_size;
        return info;
    };

    switch (type)
    {
    case Shader_Datatype::FLOAT:              return maker(GL_FLOAT, GL_FLOAT, "float", sizeof(float));
    case Shader_Datatype::UINT32:             return maker(GL_UNSIGNED_INT, GL_UNSIGNED_INT, "unsigned int", sizeof(u32));
    case Shader_Datatype::VEC2:               return maker(GL_FLOAT_VEC2, GL_FLOAT, "vec2", sizeof(vec2));
    case Shader_Datatype::VEC3:               return maker(GL_FLOAT_VEC3, GL_FLOAT, "vec3", sizeof(vec3));
    case Shader_Datatype::VEC4:               return maker(GL_FLOAT_VEC4, GL_FLOAT, "vec4", sizeof(vec4));
    case Shader_Datatype::MAT2:               return maker(GL_FLOAT_MAT2, GL_FLOAT, "mat2", sizeof(mat2));
    case Shader_Datatype::MAT3:               return maker(GL_FLOAT_MAT3, GL_FLOAT, "mat3", sizeof(mat3));
    case Shader_Datatype::MAT4:               return maker(GL_FLOAT_MAT4, GL_FLOAT, "mat4", sizeof(mat4));
    case Shader_Datatype::TEXTURE_2D_BINDING: return maker(GL_SAMPLER_2D, GL_INVALID_ENUM, "sampler2D", sizeof(u32));
    default: panic("");
    }

    // Alibi return so that compiler is happy
    return maker(GL_FLOAT, GL_FLOAT, "float", sizeof(float));
}


Render_Pass* rendering_core_query_renderpass(const char* name, Pipeline_State pipeline_state, Framebuffer* output_buffer)
{
    auto& core = rendering_core;
    auto found = hashtable_find_element(&core.render_passes, string_create_static(name));
    if (found != 0) {
        auto render_pass = *found;
        if (render_pass == core.main_pass) {
            panic("You shouldn't query the main pass!");
        }
        if (render_pass->queried_this_frame) {
            panic("Renderpass already queried this frame!\n");
        }
        render_pass->queried_this_frame = true;
        render_pass->output_buffer = output_buffer;
        render_pass->pipeline_state = pipeline_state;
        return render_pass;
    }

    Render_Pass* render_pass = new Render_Pass;
    hashtable_insert_element(&core.render_passes, string_create_static(name), render_pass);
    render_pass->commands = dynamic_array_create_empty<Render_Pass_Command>(1);
    render_pass->pipeline_state = pipeline_state;
    render_pass->queried_this_frame = true;
    render_pass->output_buffer = output_buffer;
    render_pass->dependency_count = 0;
    render_pass->dependents = dynamic_array_create_empty<Render_Pass*>(1);
    return render_pass;
}

void render_pass_set_uniforms(Render_Pass* pass, Shader* shader, std::initializer_list<Uniform_Value> uniforms)
{
    for (const auto& uniform : uniforms) {
        Render_Pass_Command command;
        command.type = Render_Pass_Command_Type::UNIFORM;
        command.uniform.shader = shader;
        command.uniform.value = uniform;
        dynamic_array_push_back(&pass->commands, command);
    }
}

void render_pass_draw(Render_Pass* pass, Shader* shader, Mesh* mesh, std::initializer_list<Uniform_Value> uniforms)
{
    for (const auto& uniform : uniforms) {
        Render_Pass_Command command;
        command.type = Render_Pass_Command_Type::UNIFORM;
        command.uniform.shader = shader;
        command.uniform.value = uniform;
        dynamic_array_push_back(&pass->commands, command);
    }
    Render_Pass_Command draw;
    draw.type = Render_Pass_Command_Type::DRAW_CALL;
    draw.draw_call.mesh = mesh;
    draw.draw_call.shader = shader;
    dynamic_array_push_back(&pass->commands, draw);
}

void render_pass_add_dependency(Render_Pass* pass, Render_Pass* depends_on)
{
    pass->dependency_count += 1;
    dynamic_array_push_back(&depends_on->dependents, pass);
}

Framebuffer* rendering_core_query_framebuffer_fullscreen(const char* name, Texture_Type type, Depth_Type depth)
{
    auto& core = rendering_core;
    auto found = hashtable_find_element(&core.framebuffers, string_create_static(name));
    if (found != 0) {
        // Note: if a framebuffer is queried with different attributes (E.g. texture type or sampling mode)
        auto framebuffer = *found;
        assert(framebuffer->resize_with_window, "Cannot set width of framebuffer for fullscreen!");
        return *found;
    }

    // Create framebuffer
    auto& info = core.render_information;
    Framebuffer* result = framebuffer_create(type, depth, true, info.window_width, info.window_height);
    hashtable_insert_element(&core.framebuffers, string_create_static(name), result);
    return result;
}

Framebuffer* rendering_core_query_framebuffer(const char* name, Texture_Type type, Depth_Type depth, int width, int height)
{
    auto& core = rendering_core;
    auto found = hashtable_find_element(&core.framebuffers, string_create_static(name));
    if (found != 0) {
        // Note: if a framebuffer is queried with different attributes (E.g. texture type or sampling mode)
        auto framebuffer = *found;
        if (framebuffer->width != width || framebuffer->height != height) {
            assert(framebuffer->resize_with_window, "Cannot create framebuffer as fullscreen and then resize!");
            framebuffer_resize(framebuffer, width, height);
        }
        if (found)
            return *found;
    }

    // Create framebuffer
    Framebuffer* result = framebuffer_create(type, depth, false, width, height);
    hashtable_insert_element(&core.framebuffers, string_create_static(name), result);
    return result;
}


