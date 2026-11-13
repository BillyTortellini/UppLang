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



void rendering_core_initialize(int backbuffer_width, int backbuffer_height, float monitor_dpi)
{
    auto& result = rendering_core;

    result.arena = Arena::create();
    result.free_list_strings = Free_List::create<String_List_Node>(result.arena.upcast());

    result.pipeline_state = pipeline_state_make_default();
    pipeline_state_set_unconditional(&result.pipeline_state);
    result.file_listener = file_listener_create();
    result.opengl_state = opengl_state_create();

    result.ubo_render_information = gpu_buffer_create_empty(sizeof(Render_Information), GPU_Buffer_Type::UNIFORM_BUFFER, GPU_Buffer_Usage::DYNAMIC);
    gpu_buffer_bind_indexed(&result.ubo_render_information, 0);
    result.ubo_camera_data = gpu_buffer_create_empty(sizeof(Camera_3D_UBO_Data), GPU_Buffer_Type::UNIFORM_BUFFER, GPU_Buffer_Usage::DYNAMIC);
    gpu_buffer_bind_indexed(&result.ubo_camera_data, 1);

    result.render_information.backbuffer_height = 0;
    result.render_information.backbuffer_width = 0;
    result.render_information.monitor_dpi = monitor_dpi;
    result.render_information.backbuffer_width = backbuffer_width;
    result.render_information.backbuffer_height = backbuffer_height;

    result.render_event_listeners = DynArray<Render_Event_Listener>::create(&result.arena);
    result.vertex_attributes = DynArray<Vertex_Attribute_Base*>::create(&result.arena);
    result.vertex_descriptions = DynArray<Vertex_Description*>::create(&result.arena);
    result.meshes = DynTable<String, Mesh*>::create(&result.arena, hash_string_pointer, string_equals_pointers);
    result.shaders = DynTable<String, Shader*>::create(&result.arena, hash_string_pointer, string_equals_pointers);
    result.render_passes = DynTable<String, Render_Pass*>::create(&result.arena, hash_string_pointer, string_equals_pointers);
    result.framebuffers = DynTable<String, Framebuffer*>::create(&result.arena, hash_string_pointer, string_equals_pointers);

    // Generate predefined objects
    {
        auto& predef = result.predefined;
        // Attributes
        predef.position3D = vertex_attribute_make<vec3>(string_make("Position3D"));
        predef.position2D = vertex_attribute_make<vec2>(string_make("Position2D"));
        predef.texture_coordinates = vertex_attribute_make<vec2>(string_make("TextureCoordinates"));
        predef.normal = vertex_attribute_make<vec3>(string_make("Normal"));
        predef.tangent = vertex_attribute_make<vec3>(string_make("Tangent"));
        predef.bitangent = vertex_attribute_make<vec3>(string_make("Bitangent"));
        predef.color3 = vertex_attribute_make<vec3>(string_make("Color3"));
        predef.color4 = vertex_attribute_make<vec4>(string_make("Color4"));
        predef.rgba_int = vertex_attribute_make<u32>(string_make("RGBA_Int"));
        predef.index = vertex_attribute_make<uint32>(string_make("IndexBuffer"));

        // Meshes
        predef.quad = rendering_core_query_mesh(
            string_make("fullscreen_quad_mesh"), vertex_description_create({ predef.position2D }), false);
        mesh_push_attribute(
            predef.quad, predef.position2D, {
                vec2(-1.0f, -1.0f),
                vec2(1.0f, -1.0f),
                vec2(1.0f, 1.0f),

                vec2(-1.0f, -1.0f),
                vec2(1.0f, 1.0f),
                vec2(-1.0f, 1.0f),
            }
        );

        predef.cube = rendering_core_query_mesh(
            string_make("unit_cube"), vertex_description_create({ predef.position3D, predef.normal, predef.index}), false);
        mesh_push_attribute(
            predef.cube, predef.position3D, {
                // Front face
                vec3(-1.0f, -1.0f,  1.0f),
                vec3( 1.0f, -1.0f,  1.0f),
                vec3( 1.0f,  1.0f,  1.0f),
                vec3(-1.0f,  1.0f,  1.0f),

                // Back face (CW!)
                vec3(-1.0f, -1.0f, -1.0f),
                vec3( 1.0f, -1.0f, -1.0f),
                vec3( 1.0f,  1.0f, -1.0f),
                vec3(-1.0f,  1.0f, -1.0f),

                // Right face
                vec3( 1.0f, -1.0f,  1.0f),
                vec3( 1.0f, -1.0f, -1.0f),
                vec3( 1.0f,  1.0f, -1.0f),
                vec3( 1.0f,  1.0f,  1.0f),

                // Left face (CW!)
                vec3( 1.0f, -1.0f,  1.0f),
                vec3( 1.0f, -1.0f, -1.0f),
                vec3( 1.0f,  1.0f, -1.0f),
                vec3( 1.0f,  1.0f,  1.0f),

                // Top face
                vec3(-1.0f,  1.0f,  1.0f),
                vec3( 1.0f,  1.0f,  1.0f),
                vec3( 1.0f,  1.0f, -1.0f),
                vec3(-1.0f,  1.0f, -1.0f),

                // Bottom face (CW!)
                vec3(-1.0f,  1.0f,  1.0f),
                vec3( 1.0f,  1.0f,  1.0f),
                vec3( 1.0f,  1.0f, -1.0f),
                vec3(-1.0f,  1.0f, -1.0f),
            }
        );

        // Generate indices
        auto indices = mesh_push_attribute_slice(predef.cube, predef.index, 6 * 6);
        for (int face = 0; face < 6; face++) {
            int counter_clockwise_order[] = { 0, 1, 2, 0, 2, 3 };
            int clockwise_order[] = { 0, 2, 1, 0, 3, 2 };
            for (int i = 0; i < 6; i++) {
                indices[face * 6 + i] = face * 4 + (face % 2 == 0 ? counter_clockwise_order[i] : clockwise_order[i]);
            }
        }

        // Generate normals
        vec3 normals[6] = {
            vec3(0, 0, 1),
            vec3(0, 0, -1),
            vec3(1, 0, 0),
            vec3(-1, 0, 0),
            vec3(0, 1, 0),
            vec3(0, -1, 0)
        };
        auto normal_data = mesh_push_attribute_slice(predef.cube, predef.normal, 4 * 6);
        for (int face = 0; face < 6; face++) {
            normal_data[face * 4 + 0] = normals[face];
            normal_data[face * 4 + 1] = normals[face];
            normal_data[face * 4 + 2] = normals[face];
            normal_data[face * 4 + 3] = normals[face];
        }

        // Render pass
        predef.main_pass = rendering_core_query_renderpass(string_make("main"), pipeline_state_make_default(), nullptr);
    }
}

void rendering_core_destroy()
{
    auto& core = rendering_core;
    SCRATCH_ARENA_MAKE_SCOPED(&core.arena);

    gpu_buffer_destroy(&core.ubo_camera_data);
    gpu_buffer_destroy(&core.ubo_render_information);
    file_listener_destroy(core.file_listener);
    opengl_state_destroy(&core.opengl_state);

    {
        auto meshes = core.meshes.copy_to_array(scratch_arena);
        for (int i = 0; i < meshes.size; i++) {
            Mesh* mesh = *(meshes[i].value);
            for (int i = 0; i < mesh->buffers.size; i++) {
                auto buffer = mesh->buffers[i];
                gpu_buffer_destroy(&buffer.gpu_buffer);
            }
            glDeleteVertexArrays(1, &mesh->vao);
        }
    }

    {
        auto framebuffers = core.framebuffers.copy_to_array(scratch_arena);
        for (int i = 0; i < framebuffers.size; i++) {
            Framebuffer* buffer = *(framebuffers[i].value);
            framebuffer_destroy(buffer);
        }
    }

    core.arena.destroy();
}

void rendering_core_add_render_event_listener(Render_Event event, render_event_callback_fn callback, void* userdata)
{
    auto core = &rendering_core;
    Render_Event_Listener listener;
    listener.callback = callback;
    listener.userdata = userdata;
    listener.event = event;
    core->render_event_listeners.push_back(listener);
}

void rendering_core_remove_render_event_listener(Render_Event event, render_event_callback_fn callback, void* userdata)
{
    auto core = &rendering_core;
    int found = -1;
    for (int i = 0; i < core->render_event_listeners.size; i++) {
        auto& listener = core->render_event_listeners[i];
        if (listener.userdata == userdata && listener.callback == callback && listener.event == event) {
            found = i;
        }
    }
    if (found == -1) panic("Should not happen i guess");
    core->render_event_listeners.swap_remove(found);
}

void renderpass_queue_if_no_dependencies(Render_Pass* pass, DynArray<Render_Pass*>* execution_order)
{
    if (pass->dependency_count == 0) 
    {
        execution_order->push_back(pass);
        pass->dependency_count = -1; // So we don't queue twice in a frame
        for (int i = 0; i < pass->dependents.size; i++) {
            pass->dependents[i]->dependency_count -= 1;
            renderpass_queue_if_no_dependencies(pass->dependents[i], execution_order);
        }
    }
}

void render_mesh_with_shader_if_compatible(Shader* shader, Mesh* mesh, Mesh_Topology topology, int element_count, int element_start)
{
    SCRATCH_ARENA_MAKE_SCOPED(nullptr);
    DynString tmp_string = DynString::create(scratch_arena);

    if (shader->program_id == 0) {
        return;
    }

    // Check if mesh and shader are compatible
    {
        bool compatible = true;
        for (int k = 0; k < shader->inputs.size; k++) {
            bool found = false;
            for (int m = 0; m < mesh->description->attributes.size; m++) {
                if (shader->inputs[k].attribute == mesh->description->attributes[m]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                logg("Mesh does not contain all attributes, missing: %s\n", 
                    tmp_string.reset()->append(shader->inputs[k].attribute->name)->to_cstring()
                );
                compatible = false;
                break;
            }
        }
        if (!compatible) {
            return;
        }
    }

    // Bind necessary state
    opengl_state_bind_program(shader->program_id);
    opengl_state_bind_vao(mesh->vao);

    // Draw
    if (mesh->has_index_buffer) {
        glDrawElements((GLenum)topology, element_count, GL_UNSIGNED_INT, (GLvoid*)(i64)(element_start * sizeof(GLuint)));
    }
    else {
        glDrawArrays((GLenum)topology, element_start, element_count);
    }
}

void rendering_core_prepare_frame(float current_time, int backbuffer_width, int backbuffer_height) 
{
    auto& core = rendering_core;
    auto& info = core.render_information;

    SCRATCH_ARENA_MAKE_SCOPED(&core.arena);

    info.current_time_in_seconds = current_time;

    // Update file listeners and window size changed listeners
    {
        file_listener_check_if_files_changed(core.file_listener);

        bool window_size_changed = 
            (backbuffer_width != info.backbuffer_width || backbuffer_height != info.backbuffer_height) &&
            (backbuffer_width != 0 && backbuffer_height != 0);
        if (window_size_changed) {
            info.backbuffer_height = backbuffer_height;
            info.backbuffer_width = backbuffer_width;
        }

        // Update Event listeners
        for (int i = 0; i < core.render_event_listeners.size; i++) {
            auto& listener = core.render_event_listeners[i];
            if (listener.event == Render_Event::WINDOW_SIZE_CHANGED && window_size_changed) {
                listener.callback(listener.userdata);
            }
            else if (listener.event == Render_Event::FRAME_START) {
                listener.callback(listener.userdata);
            }
        }
    }

    // Reset data from last frame
    {
        auto meshes = core.meshes.copy_values_to_array(scratch_arena);
        for (int i = 0; i < meshes.size; i++)
        {
            Mesh* mesh = meshes[i];
            mesh->queried_this_frame = false; // Reset
            if (mesh->reset_every_frame) 
            {
                mesh->dirty = true;
                mesh->vertex_count = 0;
                mesh->index_count = 0;
                for (int j = 0; j < mesh->buffers.size; j++) {
                    auto& buffer = mesh->buffers[j];
                    buffer.attribute_data.reset();
                }
            }
        }
    }
}

void rendering_core_render(Camera_3D* camera, Framebuffer_Clear_Type clear_type)
{
    auto& core = rendering_core;
    auto& info = core.render_information;

    SCRATCH_ARENA_MAKE_SCOPED(&core.arena);
    DynString tmp_string = DynString::create(scratch_arena);

    // Prepare frame
    {
        // Update UBOs
        Camera_3D_UBO_Data data = camera_3d_ubo_data_make(camera);
        gpu_buffer_update(&core.ubo_camera_data, object_as_bytes(&data));
        gpu_buffer_update(&core.ubo_render_information, object_as_bytes(&core.render_information));

        // Clear default framebuffer
        opengl_state_bind_framebuffer(0);
        rendering_core_clear_bound_framebuffer(clear_type);
    }

    // Upload all mesh data to the GPU ('dirty' meshes only)
    {
        auto meshes = core.meshes.copy_values_to_array(scratch_arena);
        for (int i = 0; i < meshes.size; i++)
        {
            Mesh* mesh = meshes[i];
            if (!mesh->dirty) {
                continue;
            }
            mesh->dirty = false;

            for (int i = 0; i < mesh->buffers.size; i++)
            {
                auto& buffer = mesh->buffers[i];
                auto attribute = mesh->description->attributes[i];
                if (attribute != rendering_core.predefined.index) {
                    int vertex_count_in_buffer = buffer.attribute_data.size / buffer.element_byte_size;
                    int buffer_alignment       = buffer.attribute_data.size % buffer.element_byte_size;
                    assert(vertex_count_in_buffer == mesh->vertex_count && buffer_alignment == 0, "Mesh contains attributes with different sizes!\n");
                }
                gpu_buffer_update(&buffer.gpu_buffer, buffer.attribute_data.as_array());
            }
        }
    }

    // Execute all renderpasses
    {
        // Generate execution queue based on dependencies of passes
        auto execution_queue = DynArray<Render_Pass*>::create(scratch_arena);
        {
            auto passes = core.render_passes.copy_values_to_array(scratch_arena);
            for (int i = 0; i < passes.size; i++)
            {
                Render_Pass* pass = passes[i];
                renderpass_queue_if_no_dependencies(pass, &execution_queue);
                pass->queried_this_frame = false; // Reset
            }
            if (execution_queue.size != core.render_passes.element_count) {
                panic("There is a cyclic dependency in the render-passes, shouldn't happen!");
            }
        }

        // Execute the renderpasses in order
        for (int i = 0; i < execution_queue.size; i++)
        {
            auto& pass = execution_queue[i];
            pass->dependents.reset();
            pass->dependency_count = 0;

            if (pass->commands.size == 0) {
                continue;
            }

            // Set opengl state
            rendering_core_update_pipeline_state(pass->pipeline_state);
            if (pass->output_buffer != nullptr) {
                opengl_state_bind_framebuffer(pass->output_buffer->framebuffer_id);
                glViewport(0, 0, pass->output_buffer->width, pass->output_buffer->height);
            }
            else {
                opengl_state_bind_framebuffer(0);
                glViewport(0, 0, info.backbuffer_width, info.backbuffer_height);
            }

            // Execute commands
            for (int i = 0; i < pass->commands.size; i++)
            {
                auto& command = pass->commands[i];
                if (command.type == Render_Pass_Command_Type::UNIFORM)
                {
                    auto& uniform = command.uniform;
                    if (uniform.shader->program_id == 0) {
                        continue;
                    }
                    opengl_state_bind_program(uniform.shader->program_id);

                    // Find uniform location in shader
                    Shader_Uniform* info = 0;
                    {
                        for (int k = 0; k < uniform.shader->uniforms.size; k++) {
                            auto& inf = uniform.shader->uniforms[k];
                            if (string_equals(tmp_string.reset()->append(inf.name)->to_string(), string_make(uniform.value.name))) {
                                info = &inf;
                                break;
                            }
                        }
                        if (info == nullptr) {
                            logg("Couldn't find uniform: %s in shader\n", uniform.value.name);
                            continue; // Next command
                        }
                    }

                    // Test uniform type
                    if (info->type != uniform.value.datatype || info->array_size != 1) {
                        logg("Uniform type does not match for uniform: %s in shader\n", uniform.value.name);
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
                else if (command.type == Render_Pass_Command_Type::DRAW_CALL) {
                    auto& call = command.draw_call;
                    if (call.mesh->has_index_buffer) {
                        render_mesh_with_shader_if_compatible(call.shader, call.mesh, call.topology, call.mesh->index_count, 0);
                    }
                    else {
                        render_mesh_with_shader_if_compatible(call.shader, call.mesh, call.topology, call.mesh->vertex_count, 0);
                    }
                }
                else if (command.type == Render_Pass_Command_Type::DRAW_CALL_COUNT) {
                    auto& call = command.draw_call_count;
                    render_mesh_with_shader_if_compatible(call.shader, call.mesh, call.topology, call.element_count, call.element_start);
                }
            }

            pass->commands.reset();
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

Vertex_Attribute_Base* vertex_attribute_make_base(Shader_Datatype type, String name)
{
    auto& core = rendering_core;

    // Check if attribute already exists
    for (int i = 0; i < core.vertex_attributes.size; i++) {
        auto attrib = core.vertex_attributes[i];
        if (string_equals(name, attrib->name)) {
            if (type == attrib->type) {
                return attrib;
            }
            else {
                panic("Attributes names must be unique, otherwise we don't know what to bind with the shader program!\n");
            }
        }
    }

    // Create new attributes
    Vertex_Attribute_Base* base = core.arena.allocate<Vertex_Attribute_Base>();
    base->name = string_allocate_copy(name, core.arena.upcast());
    base->type = type;
    static int next_free_binding = 0;
    base->binding_location = next_free_binding;
    next_free_binding += math_round_next_multiple(shader_datatype_get_info(type).byte_size, 16) / 16;
    core.vertex_attributes.push_back(base);

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
        if (description->attributes.size != attributes.size()) {
            continue;
        }

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
    Vertex_Description* description = core.arena.allocate<Vertex_Description>();
    description->attributes = core.arena.allocate_array<Vertex_Attribute_Base*>(attributes.size());
    int i = 0;
    for (auto attribute : attributes) {
        description->attributes[i] = attribute;
        i += 1;
    }

    core.vertex_descriptions.push_back(description);
    return description;
}

Mesh* rendering_core_query_mesh(String name, Vertex_Description* description, bool reset_every_frame)
{
    auto& core = rendering_core;
    auto found = core.meshes.find(name);
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

    Mesh* mesh = core.arena.allocate<Mesh>();
    String name_copy = string_allocate_copy(name, core.arena.upcast());
    core.meshes.insert(name_copy, mesh);
    mesh->description = description;
    mesh->queried_this_frame = true;
    mesh->reset_every_frame = reset_every_frame;
    mesh->dirty = false;
    mesh->vertex_count = 0;
    mesh->has_index_buffer = false;
    mesh->index_count = 0;

    // Generate buffers
    mesh->buffers = core.arena.allocate_array<Attribute_Buffer>(description->attributes.size);
    for (int i = 0; i < mesh->buffers.size; i++) 
    {
        auto& buffer = mesh->buffers[i];
        auto attribute = description->attributes[i];

        bool is_index = attribute == core.predefined.index;
        if (is_index) {
            mesh->has_index_buffer = true;
        }

        buffer.element_byte_size = shader_datatype_get_info(attribute->type).byte_size;
        buffer.mesh = mesh;
        buffer.is_index_buffer = is_index;
        buffer.attribute_data = DynArray<byte>::create(&core.arena);
        buffer.gpu_buffer = gpu_buffer_create_empty(
            1,
            is_index ? GPU_Buffer_Type::INDEX_BUFFER : GPU_Buffer_Type::VERTEX_BUFFER,
            mesh->reset_every_frame ? GPU_Buffer_Usage::DYNAMIC : GPU_Buffer_Usage::STATIC
        );
    }

    glGenVertexArrays(1, &mesh->vao);
    opengl_state_bind_vao(mesh->vao);
    SCOPE_EXIT(opengl_state_bind_vao(0));
    for (int i = 0; i < mesh->buffers.size; i++)
    {
        auto& buffer = mesh->buffers[i];
        auto attrib = mesh->description->attributes[i];
        if (attrib == core.predefined.index) {
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
                assert(attrib->type != Shader_Datatype::TEXTURE_2D_BINDING, "attrib cannot be shader thing");
                if (attrib->type == Shader_Datatype::UINT32) {
                    glVertexAttribIPointer(
                        attrib->binding_location,
                        info.byte_size / 4,
                        info.vertexAttribType,
                        info.byte_size,
                        (void*)offset
                    );

                }
                else {
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
    }

    return mesh;
}

void mesh_push_indices(Mesh* mesh, std::initializer_list<uint32> indices, bool add_offset) {
    auto index_data = mesh_push_attribute_slice(mesh, rendering_core.predefined.index, (int) indices.size());
    int i = 0;
    for (auto index : indices) {
        index_data[i] = index + (add_offset ? mesh->vertex_count : 0);
        i += 1;
    }
}

void attribute_buffer_report_change_to_mesh(Attribute_Buffer* buffer)
{
    assert(buffer->attribute_data.size % buffer->element_byte_size == 0, "");
    Mesh* mesh = buffer->mesh;
    if (buffer->is_index_buffer) {
        mesh->index_count = buffer->attribute_data.size / sizeof(u32); // Indices are always u32 currently
    }
    else {
        mesh->vertex_count = math_maximum(mesh->vertex_count, (int) buffer->attribute_data.size / buffer->element_byte_size);
    }
}

void attribute_buffer_push_raw_data(Attribute_Buffer* buffer, void* data, int size)
{
    assert(size % buffer->element_byte_size == 0, "Otherwise we push invalid datatype!");
    buffer->attribute_data.reserve_space(size);
    memory_copy(&buffer->attribute_data.data[buffer->attribute_data.size], data, size);
    buffer->attribute_data.size += size;
    attribute_buffer_report_change_to_mesh(buffer);
    buffer->mesh->dirty = true;
}


// SHADERS
Shader* shader_create_empty()
{
    Rendering_Core* core = &rendering_core;

    Shader* shader = core->arena.allocate<Shader>();
    shader->compiled = false;
    shader->program_id = 0;

    shader->filename = string_make("");
    shader->watched_file = nullptr;

    shader->inputs   = DynArray<Shader_Input>::create(&core->arena);
    shader->uniforms = DynArray<Shader_Uniform>::create(&core->arena);

    return shader;
}

void shader_reset(Shader* shader)
{
    Rendering_Core* core = &rendering_core;

    if (shader->program_id != 0) {
        glDeleteProgram(shader->program_id);
    }
    shader->program_id = 0;
    shader->compiled = false;

    for (int i = 0; i < shader->inputs.size; i++) {
        Shader_Input& input = shader->inputs[i];
        input.name.destroy();
    }
    shader->inputs.reset();

    for (int i = 0; i < shader->uniforms.size; i++) {
        Shader_Uniform& uniform = shader->uniforms[i];
        uniform.name.destroy();
    }
    shader->uniforms.reset();
}

void shader_add_shader_stage(Shader* shader, Shader_Stage stage, String source_code)
{
    assert(!shader->compiled, "Reset must be called before recompiling");
    if (shader->program_id == 0) {
        shader->program_id = glCreateProgram();
        assert(shader->program_id != 0, "");
    }

    GLenum gl_stage;
    switch (stage)
    {
    case Shader_Stage::VERTEX: gl_stage = GL_VERTEX_SHADER; break;
    case Shader_Stage::GEOMETRY: gl_stage = GL_GEOMETRY_SHADER; break;
    case Shader_Stage::FRAGMENT: gl_stage = GL_FRAGMENT_SHADER; break;
    default: panic("");
    }

    GLint shader_id = glCreateShader(gl_stage);
    const char* sources[] = { source_code.data };
    const GLint lengths[] = { source_code.size };
    glShaderSource(shader_id, 1, sources, lengths);
    // logg("compiling shader: \n\n%s\n\n", source_code.data);
    glCompileShader(shader_id);
    opengl_utils_check_shader_compilation_status(shader_id);
    glAttachShader(shader->program_id, shader_id);
    glDeleteShader(shader_id); // Will only be deleted after the program is deleted
}

bool shader_compile(Shader* shader)
{
    Rendering_Core* core = &rendering_core;
    SCRATCH_ARENA_MAKE_SCOPED(&core->arena);
    DynString tmp_string = DynString::create(scratch_arena);

    assert(!shader->compiled, "Reset must be called before recompiling a shader!");

    // Compile program
    if (!opengl_utils_link_program_and_check_errors(shader->program_id)) {
        glDeleteProgram(shader->program_id);
        shader->program_id = 0;
        return false;
    }

    // Recheck attribute information (Lookout for unused attributes)
    for (int i = 0; i < shader->inputs.size; i++)
    {
        auto& input = shader->inputs[i];
        GLint attrib_location = glGetAttribLocation(
            shader->program_id, tmp_string.reset()->append(input.name)->to_cstring()
        );
        // Check if attribute is actually in use
        if (attrib_location == -1) {
            input.name.destroy();
            shader->inputs.swap_remove(i);
            i -= 1;
            continue;
        }
        assert(attrib_location == input.location, "Must not happen");
    }

    // Query uniform Information
    {
        GLint uniform_count;
        glGetProgramiv(shader->program_id, GL_ACTIVE_UNIFORMS, &uniform_count);
        for (int i = 0; i < shader->uniforms.size; i++) {
            auto& uniform_info = shader->uniforms[i];
            uniform_info.name.destroy();
        }
        shader->uniforms.reset()->reserve_capacity(uniform_count);

        int longest_uniform_name_length;
        glGetProgramiv(shader->program_id, GL_ACTIVE_UNIFORM_MAX_LENGTH, &longest_uniform_name_length);
        tmp_string.reserve_capacity(longest_uniform_name_length + 1);

        // Loop over all uniforms
        for (int i = 0; i < uniform_count; i++)
        {
            // Get Infos
            GLenum type; // Type of uniform
            GLint array_size; // Size, e.g. for arrays? or vector components?
            glGetActiveUniform(
                shader->program_id, (GLuint)i, tmp_string.capacity, &tmp_string.size,
                &array_size, &type, (GLchar*)tmp_string.data
            );
            tmp_string.size = strlen(tmp_string.data);

            Shader_Uniform info;
            info.array_size = array_size;
            info.name = String_List::create(core->free_list_strings.upcast(), tmp_string.to_string());
            info.location = glGetUniformLocation(shader->program_id, tmp_string.data);

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

            shader->uniforms.push_back(info);
        }
    }

    shader->compiled = true;
    return true;
}

void shader_hotreload_fn(void* userdata, String filename)
{
    Rendering_Core* core = &rendering_core;
    SCRATCH_ARENA_MAKE_SCOPED(&core->arena);
    DynString tmp_string = DynString::create(scratch_arena);

    Shader* shader = (Shader*)userdata;
    // logg("Compiling shader: %s\n", filename);

    DynString shader_code_dyn = DynString::create(scratch_arena);
    bool file_load_success = file_io_load_text_file(filename, &shader_code_dyn);
    if (!file_load_success) {
        panic("File listener file wasnt able to read!");
    }
    String shader_code = shader_code_dyn.to_string();

    shader_reset(shader);

    // Recompile and add shader-stages to the program
    {
        int possible_shader_defines_count = 6;
        String possible_shader_defines[] = {
            string_make("VERTEX"),
            string_make("FRAGMENT"),
            string_make("GEOMETRY"),
            //string_create_static("COMPUTE"),
            //string_create_static("TESSELATION_CONTROL"),
            //string_create_static("TESSELATION_EVALUATION"),
        };
        Shader_Stage possible_shader_define_types[] = {
            Shader_Stage::VERTEX,
            Shader_Stage::FRAGMENT,
            Shader_Stage::GEOMETRY
            // GL_COMPUTE_SHADER,
            // GL_TESS_CONTROL_SHADER,
            // GL_TESS_EVALUATION_SHADER,
        };
        // Note: This array corresponds to the Shader_Datatypes enum!
        const int type_count = 9;
        String type_names[] = {
            string_make("float"),
            string_make("uint"),
            string_make("vec2"),
            string_make("vec3"),
            string_make("vec4"),
            string_make("mat2"),
            string_make("mat3"),
            string_make("mat4"),
            string_make("sampler2D"),
        };

        // Split input into lines
        tmp_string.reset();
        tmp_string.append("#version 430 core\n\n");
        Array<String> lines = string_split(shader_code, '\n', scratch_arena);
        Shader_Stage shader_stage = Shader_Stage::SHADER_STAGE_COUNT;
        int slot_index = 0;
        bool inside_code = false; // Code only starts after #define
        for (int i = 0; i < lines.size; i++)
        {
            String line = lines[i];

            // Do pattern matching
            // Shader type defines
            String escape_sequence = string_make("//@");
            String ifdef = string_make("#ifdef");
            String endif = string_make("#endif");
            if (string_test_substring(line, 0, ifdef))
            {
                // Split line into words
                Array<String> words = string_split(line, ' ', scratch_arena);
                if (words.size != 2) {
                    logg("Shader error, couldn't parse #ifdef!\n");
                    continue; // Next line
                }
                String stage_name = words[1];
                if (stage_name.size > 0 && stage_name[words[1].size - 1] == '\r') {
                    stage_name.size -= 1;
                }

                bool worked = false;
                for (int j = 0; j < possible_shader_defines_count; j++)
                {
                    if (!string_equals(stage_name, possible_shader_defines[j])) {
                        continue;
                    }
                    shader_stage = possible_shader_define_types[j];
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

            if (string_test_substring(line, 0, endif)) 
            {
                shader_add_shader_stage(shader, shader_stage, tmp_string.to_string());
                tmp_string.reset();
                tmp_string.append("#version 430 core\n\n");
                inside_code = false;
            }

            // Skip none code lines
            if (!inside_code) {
                continue;
            }

            // Input layout
            if (shader_stage == Shader_Stage::VERTEX &&
                string_test_substring(line, 0, string_make("in")) ||
                string_test_substring(line, 0, string_make("inout")))
            {
                Shader_Input input_info;

                // Split line into words
                Array<String> words = string_split(line, ' ', scratch_arena);
                if (words.size < 3) {
                    logg("Shader error, couldn't parse in/inout attribute!\n");
                    continue; // Next line
                }

                // Variable base_name
                {
                    String var_name = words[2];
                    if (var_name.size == 0) {
                        panic("Shouldnt happen with split...");
                    }
                    if (var_name.size >= 1 && var_name[var_name.size - 1] == ';') {
                        var_name.size -= 1;
                    }
                    if (var_name.size == 0) {
                        logg("Shader error, expected variable name!\n");
                        continue;
                    }
                    input_info.name = String_List::create(core->free_list_strings.upcast(), var_name);
                }

                // Parse type + set location
                Shader_Datatype datatype;
                {
                    String type = words[1];
                    int type_index = -1;
                    for (int j = 0; j < type_count; j++) {
                        if (string_equals(type_names[j], type)) {
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

                // Parse attribute base_name
                {
                    String suffix = words[words.size - 1];
                    if (suffix.size < escape_sequence.size + 1) {
                        logg("Expected valid suffix for inout qualifier");
                        continue;
                    }
                    if (!string_test_substring(suffix, 0, escape_sequence)) {
                        logg("Expected valid suffix");
                        continue;
                    }
                    if (suffix.size > 0 && suffix[suffix.size - 1] == '\r') {
                        suffix.size -= 1;
                    }
                    if (suffix.size <= 0) {
                        logg("Expected valid suffix");
                        continue;
                    }
                    String attribute_name = string_make_substring(suffix, escape_sequence.size, suffix.size);
                    input_info.attribute = vertex_attribute_make_base(datatype, attribute_name);
                    input_info.location = input_info.attribute->binding_location;
                }

                // Store Vertex attribute
                shader->inputs.push_back(input_info);
                tmp_string.append_formated("layout (location = %d) ", input_info.attribute->binding_location);
            }

            // Add line to buffer if its not a special line
            tmp_string.append(line)->append("\n");
        }

        // Add the final shader_stage to the program
        if (inside_code) {
            logg("Last endif is missing in shader!\n");
            shader_add_shader_stage(shader, shader_stage, tmp_string.to_string());
        }
    }

    shader_compile(shader);
}

Shader* rendering_core_query_shader(String filename)
{
    auto& core = rendering_core;
    auto found = core.shaders.find(filename);
    if (found != nullptr) {
        return *found;
    }

    SCRATCH_ARENA_MAKE_SCOPED(&core.arena);
    DynString tmp_string = DynString::create(scratch_arena);

    Shader* shader = shader_create_empty();
    String fullpath = tmp_string.reset()->append("resources/shaders/")->append(filename)->to_string();
    shader->filename = string_allocate_copy(filename, core.arena.upcast());
    core.shaders.insert(filename, shader);

    // Setup hot-reloading
    shader->watched_file = file_listener_add_file(
        core.file_listener, 
        fullpath, 
        shader_hotreload_fn,
        (void*) shader
    );
    assert(shader->watched_file != nullptr, "");

    // Compile shader
    shader_hotreload_fn((void*)shader, fullpath);

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
    return val;
}

Uniform_Value uniform_make(const char* name, float val) { auto result = uniform_make_base(name, val); result.data_float = val; return result; }
Uniform_Value uniform_make(const char* name, vec2 val) { auto result = uniform_make_base(name, val); result.data_vec2 = val; return result; }
Uniform_Value uniform_make(const char* name, vec3 val) { auto result = uniform_make_base(name, val); result.data_vec3 = val; return result; }
Uniform_Value uniform_make(const char* name, vec4 val) { auto result = uniform_make_base(name, val); result.data_vec4 = val; return result; }
Uniform_Value uniform_make(const char* name, mat2 val) { auto result = uniform_make_base(name, val); result.data_mat2 = val; return result; }
Uniform_Value uniform_make(const char* name, mat3 val) { auto result = uniform_make_base(name, val); result.data_mat3 = val; return result; }
Uniform_Value uniform_make(const char* name, mat4 val) { auto result = uniform_make_base(name, val); result.data_mat4 = val; return result; }
Uniform_Value uniform_make(const char* name, int val) { auto result = uniform_make_base(name, val); result.data_i32 = val; return result; }

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
        info.name = string_make(name);
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


Render_Pass* rendering_core_query_renderpass(String name, Pipeline_State pipeline_state, Framebuffer* output_buffer)
{
    auto& core = rendering_core;
    auto found = core.render_passes.find(name);
    if (found != nullptr) 
    {
        auto render_pass = *found;
        if (render_pass == core.predefined.main_pass) {
            panic("You shouldn't query the main pass!");
        }
        if (render_pass->queried_this_frame) {
            panic("Renderpass already queried this frame!\n");
        }
        render_pass->queried_this_frame = true;
        render_pass->output_buffer = output_buffer;
        render_pass->pipeline_state = pipeline_state;
        render_pass->name = name;
        return render_pass;
    }

    Render_Pass* render_pass = core.arena.allocate<Render_Pass>();
    render_pass->name = string_allocate_copy(name, core.arena.upcast());
    core.render_passes.insert(render_pass->name, render_pass);
    render_pass->commands = DynArray<Render_Pass_Command>::create(&core.arena);
    render_pass->pipeline_state = pipeline_state;
    render_pass->queried_this_frame = true;
    render_pass->output_buffer = output_buffer;
    render_pass->dependency_count = 0;
    render_pass->dependents = DynArray<Render_Pass*>::create(&core.arena);
    return render_pass;
}

void render_pass_set_uniforms(Render_Pass* pass, Shader* shader, std::initializer_list<Uniform_Value> uniforms)
{
    for (const auto& uniform : uniforms) {
        Render_Pass_Command command;
        command.type = Render_Pass_Command_Type::UNIFORM;
        command.uniform.shader = shader;
        command.uniform.value = uniform;
        pass->commands.push_back(command);
    }
}

void render_pass_draw_count(
    Render_Pass* pass, Shader* shader, Mesh* mesh, Mesh_Topology topology,
    std::initializer_list<Uniform_Value> uniforms, int element_start, int element_count)
{
    render_pass_set_uniforms(pass, shader, uniforms);
    Render_Pass_Command draw;
    draw.type = Render_Pass_Command_Type::DRAW_CALL_COUNT;
    auto& call = draw.draw_call_count;
    call.mesh = mesh;
    call.shader = shader;
    call.topology = topology;
    call.element_start = element_start;
    call.element_count = element_count;
    pass->commands.push_back(draw);

}

void render_pass_draw(Render_Pass* pass, Shader* shader, Mesh* mesh, Mesh_Topology topology, std::initializer_list<Uniform_Value> uniforms)
{
    render_pass_set_uniforms(pass, shader, uniforms);
    Render_Pass_Command draw;
    draw.type = Render_Pass_Command_Type::DRAW_CALL;
    draw.draw_call.mesh = mesh;
    draw.draw_call.shader = shader;
    draw.draw_call.topology = topology;
    pass->commands.push_back(draw);
}

void render_pass_add_dependency(Render_Pass* pass, Render_Pass* depends_on)
{
    for (int i = 0; i < depends_on->dependents.size; i++) {
        if (depends_on->dependents[i] == pass) {
            return;
        }
    }

    pass->dependency_count += 1;
    depends_on->dependents.push_back(pass);
}

Framebuffer* rendering_core_query_framebuffer_fullscreen(String name, Texture_Type type, Depth_Type depth)
{
    auto& core = rendering_core;
    auto found = core.framebuffers.find(name);
    if (found != 0) {
        // Note: if a framebuffer is queried with different attributes (E.g. bitmap type or sampling mode)
        auto framebuffer = *found;
        assert(framebuffer->resize_with_window, "Cannot set width of framebuffer for fullscreen!");
        return *found;
    }

    // Create framebuffer
    auto& info = core.render_information;
    Framebuffer* result = framebuffer_create(type, depth, true, info.backbuffer_width, info.backbuffer_height);
    core.framebuffers.insert(string_allocate_copy(name, core.arena.upcast()), result);
    return result;
}

Framebuffer* rendering_core_query_framebuffer(String name, Texture_Type type, Depth_Type depth, int width, int height)
{
    auto& core = rendering_core;
    auto found = core.framebuffers.find(name);
    if (found != 0) 
    {
        // Note: if a framebuffer is queried with different attributes (E.g. bitmap type or sampling mode)
        auto framebuffer = *found;
        if (framebuffer->width != width || framebuffer->height != height) {
            assert(framebuffer->resize_with_window, "Cannot create framebuffer as fullscreen and then resize!");
            framebuffer_resize(framebuffer, width, height);
        }
        if (found) {
            return *found;
        }
    }

    // Create framebuffer
    Framebuffer* result = framebuffer_create(type, depth, false, width, height);
    core.framebuffers.insert(string_allocate_copy(name, core.arena.upcast()), result);
    return result;
}


