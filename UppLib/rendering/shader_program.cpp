#include "shader_program.hpp"

#include "opengl_utils.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../math/vectors.hpp"
#include "rendering_core.hpp"
#include "../utility/file_listener.hpp"
#include "opengl_utils.hpp"
#include "texture_2D.hpp"

Uniform_Value uniform_value_make_i32(const char* uniform_name, i32 data)
{
    Uniform_Value result;
    result.uniform_name = uniform_name;
    result.data_i32 = data;
    result.type = Uniform_Value_Type::I32;
    return result;
}
Uniform_Value uniform_value_make_u32(const char* uniform_name, u32 data)
{
    Uniform_Value result;
    result.uniform_name = uniform_name;
    result.data_u32 = data;
    result.type = Uniform_Value_Type::U32;
    return result;
}
Uniform_Value uniform_value_make_float(const char* uniform_name, float data)
{
    Uniform_Value result;
    result.uniform_name = uniform_name;
    result.data_float = data;
    result.type = Uniform_Value_Type::FLOAT;
    return result;
}
Uniform_Value uniform_value_make_texture_2D_binding(const char* uniform_name, Texture_2D* texture)
{
    Uniform_Value result;
    result.uniform_name = uniform_name;
    result.texture_2D_id = texture->texture_id;
    result.type = Uniform_Value_Type::TEXTURE_2D_BINDING;
    return result;
}
Uniform_Value uniform_value_make_vec2(const char* uniform_name, vec2 data)
{
    Uniform_Value result;
    result.uniform_name = uniform_name;
    result.data_vec2 = data;
    result.type = Uniform_Value_Type::VEC2;
    return result;
}
Uniform_Value uniform_value_make_vec3(const char* uniform_name, vec3 data)
{
    Uniform_Value result;
    result.uniform_name = uniform_name;
    result.data_vec3 = data;
    result.type = Uniform_Value_Type::VEC3;
    return result;
}
Uniform_Value uniform_value_make_vec4(const char* uniform_name, vec4 data)
{
    Uniform_Value result;
    result.uniform_name = uniform_name;
    result.data_vec4 = data;
    result.type = Uniform_Value_Type::VEC4;
    return result;
}
Uniform_Value uniform_value_make_mat2(const char* uniform_name, mat2 data)
{
    Uniform_Value result;
    result.uniform_name = uniform_name;
    result.data_mat2 = data;
    result.type = Uniform_Value_Type::MAT2;
    return result;
}
Uniform_Value uniform_value_make_mat3(const char* uniform_name, mat3 data)
{
    Uniform_Value result;
    result.uniform_name = uniform_name;
    result.data_mat3 = data;
    result.type = Uniform_Value_Type::MAT3;
    return result;
}
Uniform_Value uniform_value_make_mat4(const char* uniform_name, mat4 data)
{
    Uniform_Value result;
    result.uniform_name = uniform_name;
    result.data_mat4 = data;
    result.type = Uniform_Value_Type::MAT4;
    return result;
}


void shader_program_retrieve_shader_variable_information(Shader_Program* program)
{
    // Reset arrays
    dynamic_array_reset(&program->attribute_informations);
    dynamic_array_reset(&program->uniform_informations);

    if (program->program_id == 0) {
        return;
    }

     // Get Attribute Information
    {
        // Get Attribute count
        GLint attribute_count;
        glGetProgramiv(program->program_id, GL_ACTIVE_ATTRIBUTES, &attribute_count);
        dynamic_array_reserve(&program->attribute_informations, attribute_count);

        int longest_attribute_name_length;
        glGetProgramiv(program->program_id, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &longest_attribute_name_length);

        // Loop over all attribs
        for (int i = 0; i < attribute_count; i++)
        {
            Shader_Variable_Information info;
            info.name_handle = string_create_empty(longest_attribute_name_length);

            // Get Infos
            /* INFO: Inbuilts (Staring with prefix gl_, like gl_VertexID) are also attributes, but glGetActiveAttrib returns -1 in this case */
            glGetActiveAttrib(program->program_id, (GLuint)i,
                longest_attribute_name_length, NULL, &info.size, &info.type, info.name_handle.characters);
            info.name_handle.size = (int)strlen(info.name_handle.characters); // Actualize string length
            info.location = glGetAttribLocation(program->program_id, info.name_handle.characters);

            // Put attribute info in dynamic array
            dynamic_array_push_back(&program->attribute_informations, info);
        }
    }

    // Get Uniform Information
    {
        GLint uniform_count;
        glGetProgramiv(program->program_id, GL_ACTIVE_UNIFORMS, &uniform_count);
        dynamic_array_reserve(&program->uniform_informations, uniform_count);

        int longest_uniform_name_length;
        glGetProgramiv(program->program_id, GL_ACTIVE_UNIFORM_MAX_LENGTH, &longest_uniform_name_length);

        // Loop over all uniforms
        for (int i = 0; i < uniform_count; i++)
        {
            Shader_Variable_Information info;
            info.name_handle = string_create_empty(longest_uniform_name_length);

            // Get Infos
            glGetActiveUniform(program->program_id, (GLuint)i, 256, NULL,
                &info.size, &info.type, (GLchar*)info.name_handle.characters);
            info.name_handle.size = (int)strlen(info.name_handle.characters); // Actualize string length
            info.location = glGetUniformLocation(program->program_id, info.name_handle.characters);

            // Put uniform data in dynamic array
            dynamic_array_push_back(&program->uniform_informations, info);
        }
    }
}

void shader_program_file_changed_callback(void* userdata, const char* filename)
{
    Shader_Program* shader_program = (Shader_Program*)userdata;
    // Delete old shader if it exists
    if (shader_program->program_id) {
        glDeleteProgram(shader_program->program_id);
    }

    // Recompile shader
    shader_program->program_id = opengl_utils_create_program_from_filepaths(shader_program->shader_filepaths);
    if (shader_program->program_id != 0) {
        logg("Recompiled shader SUCCESSFULL.\n");
    }
    shader_program_retrieve_shader_variable_information(shader_program);
}

Shader_Program* shader_program_create(Rendering_Core* core, const char* filepath) {
    return shader_program_create_from_multiple_sources(core, { filepath });
}

Shader_Program* shader_program_create_from_multiple_sources(Rendering_Core* core, std::initializer_list<const char*> shader_filepaths)
{
    Shader_Program* shader_program = new Shader_Program();
    shader_program->file_listener = core->file_listener;
    shader_program->shader_filepaths = array_create_from_list(shader_filepaths);
    shader_program->uniform_cache = dynamic_array_create_empty<Uniform_Value>(16);

    // Setup file watchers
    {
        int watched_file_count = 0;
        shader_program->watched_files = array_create_empty<Watched_File*>(shader_program->shader_filepaths.size);
        for (int i = 0; i < shader_program->watched_files.size; i++)
        {
            shader_program->watched_files.data[i] = file_listener_add_file(core->file_listener,
                shader_program->shader_filepaths.data[i], shader_program_file_changed_callback, shader_program);
            if (shader_program->watched_files.data[i] == 0) {
                break;
            }
            watched_file_count++;
        }
        // Destroy file watchers if we could not set all up (Setup fails)
        if (watched_file_count != shader_program->shader_filepaths.size) {
            for (int i = 0; i < watched_file_count; i++) {
                file_listener_remove_file(core->file_listener, shader_program->watched_files.data[i]);
            }

            array_destroy<const char*>(&shader_program->shader_filepaths);
            delete shader_program;
            panic("Could not create shader, could not open filepath or something");
            return 0;
        }
    }

    // Initialize Dynamic arrays
    shader_program->attribute_informations = dynamic_array_create_empty<Shader_Variable_Information>(8);
    shader_program->uniform_informations = dynamic_array_create_empty<Shader_Variable_Information>(8);

    // Compile shader (Does not matter if it works, because we have hot reloading)
    shader_program->program_id = opengl_utils_create_program_from_filepaths(shader_program->shader_filepaths);
    shader_program_retrieve_shader_variable_information(shader_program);

    return shader_program;
}

void shader_program_destroy(Shader_Program* program)
{
    for (int i = 0; i < program->watched_files.size; i++) {
        file_listener_remove_file(program->file_listener, program->watched_files.data[i]);
    }
    array_destroy(&program->shader_filepaths);
    dynamic_array_destroy(&program->attribute_informations);
    dynamic_array_destroy(&program->uniform_informations);
    dynamic_array_destroy(&program->uniform_cache);
    glDeleteProgram(program->program_id);
    delete program;
}

void shader_program_bind(Shader_Program* program, Rendering_Core* core) {
    opengl_state_bind_program(&core->opengl_state, program->program_id);
}

void shader_program_print_variable_information(Shader_Program* program) 
{
    String message = string_create_empty(1024);
    SCOPE_EXIT(string_destroy(&message));

    string_append(&message, "\nPrinting Shader Program\n");
    string_append(&message, "\tFiles: ");
    for (int i = 0; i < program->shader_filepaths.size; i++) {
        string_append(&message, program->shader_filepaths.data[i]);
        string_append(&message, ", ");
    }

    string_append_formated(&message, "\n\tUniforms(#%d): \n", program->uniform_informations.size);
    for (int i = 0; i < program->uniform_informations.size; i++) {
        Shader_Variable_Information* info = &program->uniform_informations.data[i];
        string_append_formated(&message, "\t\tLocation: %d, size: %d, type: %s name: \"%s\"\n", 
            info->location, info->size, opengl_utils_datatype_to_string(info->type), info->name_handle.characters);
    }

    string_append_formated(&message, "\n\tAttributes(#%d): \n", program->attribute_informations.size);
    for (int i = 0; i < program->attribute_informations.size; i++) {
        Shader_Variable_Information* info = &program->attribute_informations.data[i];
        string_append_formated(&message, "\t\tLocation: %d, size: %d, type: %s name: \"%s\"\n", 
            info->location, info->size, opengl_utils_datatype_to_string(info->type), info->name_handle.characters);
    }

    logg("%s", message.characters);
}

Shader_Variable_Information* shader_program_find_shader_variable_information_by_name(Shader_Program* program, const char* name_handle) 
{
    for (int i = 0; i < program->uniform_informations.size; i++) {
        Shader_Variable_Information* info = &program->uniform_informations.data[i];
        if (strcmp(name_handle, info->name_handle.characters) == 0) {
            return info;
        }
    }
    return 0;
}

void shader_program_set_uniform_i32(Shader_Program* program,const char* name_handle, int value) {
    dynamic_array_push_back(&program->uniform_cache, uniform_value_make_i32(name_handle, value));
}
void shader_program_set_uniform_u32(Shader_Program* program, const char* name_handle, u32 value) {
    dynamic_array_push_back(&program->uniform_cache, uniform_value_make_u32(name_handle, value));
}
void shader_program_set_uniform_texture_2D(Shader_Program* program, const char* name_handle, Texture_2D* texture) {
    dynamic_array_push_back(&program->uniform_cache, uniform_value_make_texture_2D_binding(name_handle, texture));
}
void shader_program_set_uniform_float(Shader_Program* program, const char* name_handle, float value) {
    dynamic_array_push_back(&program->uniform_cache, uniform_value_make_float(name_handle, value));
}
void shader_program_set_uniform_vec2(Shader_Program* program, const char* name_handle, const vec2& value) {
    dynamic_array_push_back(&program->uniform_cache, uniform_value_make_vec2(name_handle, value));
}
void shader_program_set_uniform_vec3(Shader_Program* program, const char* name_handle, const vec3& value) {
    dynamic_array_push_back(&program->uniform_cache, uniform_value_make_vec3(name_handle, value));
}
void shader_program_set_uniform_vec4(Shader_Program* program, const char* name_handle, const vec4& value) {
    dynamic_array_push_back(&program->uniform_cache, uniform_value_make_vec4(name_handle, value));
}
void shader_program_set_uniform_mat2(Shader_Program* program, const char* name_handle, const mat2& value) {
    dynamic_array_push_back(&program->uniform_cache, uniform_value_make_mat2(name_handle, value));
}
void shader_program_set_uniform_mat3(Shader_Program* program, const char* name_handle, const mat3& value) {
    dynamic_array_push_back(&program->uniform_cache, uniform_value_make_mat3(name_handle, value));
}
void shader_program_set_uniform_mat4(Shader_Program* program, const char* name_handle, const mat4& value) {
    dynamic_array_push_back(&program->uniform_cache, uniform_value_make_mat4(name_handle, value));
}

bool shader_program_set_uniform_value(Shader_Program* program, Uniform_Value value, Rendering_Core* core)
{
    shader_program_bind(program, core);
    Shader_Variable_Information* info = shader_program_find_shader_variable_information_by_name(program, value.uniform_name);
    if (info == nullptr) {
        return false;
    }
    bool valid;
    switch (value.type)
    {
    case Uniform_Value_Type::I32: valid = info->type != GL_INT || info->type == GL_SAMPLER_2D && info->size == 1; break;
    case Uniform_Value_Type::U32: valid = info->type != GL_UNSIGNED_INT || info->type == GL_SAMPLER_2D && info->size == 1; break;
    case Uniform_Value_Type::FLOAT: valid = info->type != GL_FLOAT && info->size != 1; break;
    case Uniform_Value_Type::VEC2: valid = info->type != GL_FLOAT_VEC2 && info->size != 1; break;
    case Uniform_Value_Type::VEC3: valid = info->type != GL_FLOAT_VEC3 && info->size != 1; break;
    case Uniform_Value_Type::VEC4: valid = info->type != GL_FLOAT_VEC4 && info->size != 1; break;
    case Uniform_Value_Type::MAT2: valid = info->type != GL_FLOAT_MAT2 && info->size != 1; break;
    case Uniform_Value_Type::MAT3: valid = info->type != GL_FLOAT_MAT3 && info->size != 1; break;
    case Uniform_Value_Type::MAT4: valid = info->type != GL_FLOAT_MAT4 && info->size != 1; break;
    case Uniform_Value_Type::TEXTURE_2D_BINDING: valid = info->type != GL_UNSIGNED_INT || info->type == GL_SAMPLER_2D && info->size == 1; break;
    }
    if (!valid) return false;

    switch (value.type)
    {
    case Uniform_Value_Type::I32: glUniform1i(info->location, value.data_i32); break;
    case Uniform_Value_Type::U32: glUniform1ui(info->location, value.data_u32); break;
    case Uniform_Value_Type::FLOAT: glUniform1f(info->location, value.data_float); break;
    case Uniform_Value_Type::VEC2: glUniform2fv(info->location, 1, (GLfloat*)&value); break;
    case Uniform_Value_Type::VEC3: glUniform3fv(info->location, 1, (GLfloat*)&value); break;
    case Uniform_Value_Type::VEC4: glUniform4fv(info->location, 1, (GLfloat*)&value); break;
    case Uniform_Value_Type::MAT2: glUniformMatrix2fv(info->location, 1, GL_FALSE, (GLfloat*)&value); break;
    case Uniform_Value_Type::MAT3: glUniformMatrix3fv(info->location, 1, GL_FALSE, (GLfloat*)&value); break;
    case Uniform_Value_Type::MAT4: glUniformMatrix4fv(info->location, 1, GL_FALSE, (GLfloat*)&value); break;
    case Uniform_Value_Type::TEXTURE_2D_BINDING: 
        glUniform1i(info->location, opengl_state_bind_texture_to_next_free_unit(&core->opengl_state, Texture_Binding_Type::TEXTURE_2D, value.texture_2D_id)); 
        break;
    }
    return true;
}
