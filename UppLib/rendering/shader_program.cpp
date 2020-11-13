#include "shader_program.hpp"

#include "opengl_utils.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../math/vectors.hpp"
#include "opengl_state.hpp"
#include "opengl_utils.hpp"

void shader_program_retrieve_shader_variable_information(ShaderProgram* program)
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
            ShaderVariableInformation info;
            info.name = string_create_empty(longest_attribute_name_length);

            // Get Infos
            /* INFO: Inbuilts (Staring with prefix gl_, like gl_VertexID) are also attributes, but glGetActiveAttrib returns -1 in this case */
            glGetActiveAttrib(program->program_id, (GLuint)i,
                longest_attribute_name_length, NULL, &info.size, &info.type, info.name.characters);
            info.name.size = (int)strlen(info.name.characters); // Actualize string length
            info.location = glGetAttribLocation(program->program_id, info.name.characters);

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
            ShaderVariableInformation info;
            info.name = string_create_empty(longest_uniform_name_length);

            // Get Infos
            glGetActiveUniform(program->program_id, (GLuint)i, 256, NULL,
                &info.size, &info.type, (GLchar*)info.name.characters);
            info.name.size = (int)strlen(info.name.characters); // Actualize string length
            info.location = glGetUniformLocation(program->program_id, info.name.characters);

            // Put uniform data in dynamic array
            dynamic_array_push_back(&program->uniform_informations, info);
        }
    }
}

void shader_program_file_changed_callback(void* userdata, const char* filename)
{
    ShaderProgram* shader_program = (ShaderProgram*)userdata;
    // Delete old shader if it exists
    if (shader_program->program_id) {
        opengl_utils_destroy_program(shader_program->program_id);
    }

    // Recompile shader
    shader_program->program_id = opengl_utils_create_program_from_filepaths(shader_program->shader_filepaths);
    if (shader_program->program_id != 0) {
        logg("Recompiled shader SUCCESSFULL.\n");
    }
    shader_program_retrieve_shader_variable_information(shader_program);
}

Optional<ShaderProgram*> shader_program_create(FileListener* file_listener, std::initializer_list<const char*> shader_filepaths)
{
    Optional<ShaderProgram*> result;
    result.available = false;

    ShaderProgram* shader_program = new ShaderProgram();
    shader_program->file_listener = file_listener;
    // Fill shader_file_names array
    {
        shader_program->shader_filepaths = array_create_empty<const char*>((int)shader_filepaths.size());
        int i = 0;
        for (auto filepath : shader_filepaths) {
            shader_program->shader_filepaths.data[i] = filepath;
            i++;
        }
    }

    // Setup file watchers
    {
        int watched_file_count = 0;
        shader_program->watched_files = array_create_empty<WatchedFile*>(shader_program->shader_filepaths.size);
        for (int i = 0; i < shader_program->watched_files.size; i++)
        {
            shader_program->watched_files.data[i] = file_listener_add_file(file_listener,
                shader_program->shader_filepaths.data[i], shader_program_file_changed_callback, shader_program);
            if (shader_program->watched_files.data[i] == 0) {
                break;
            }
            watched_file_count++;
        }
        // Destroy file watchers if we could not set all up (Setup fails)
        if (watched_file_count != shader_program->shader_filepaths.size) {
            for (int i = 0; i < watched_file_count; i++) {
                file_listener_remove_file(file_listener, shader_program->watched_files.data[i]);
            }

            array_destroy<const char*>(&shader_program->shader_filepaths);
            delete shader_program;
            return result;
        }
    }

    // Initialize Dynamic arrays
    shader_program->attribute_informations = dynamic_array_create_empty<ShaderVariableInformation>(8);
    shader_program->uniform_informations = dynamic_array_create_empty<ShaderVariableInformation>(8);

    // Compile shader (Does not matter if it works, because we have hot reloading)
    shader_program->program_id = opengl_utils_create_program_from_filepaths(shader_program->shader_filepaths);
    shader_program_retrieve_shader_variable_information(shader_program);

    result.value = shader_program;
    result.available = true;
    return result;
}

void shader_program_destroy(ShaderProgram* program)
{
    for (int i = 0; i < program->watched_files.size; i++) {
        file_listener_remove_file(program->file_listener, program->watched_files.data[i]);
    }
    array_destroy(&program->shader_filepaths);
    dynamic_array_destroy(&program->attribute_informations);
    dynamic_array_destroy(&program->uniform_informations);
    delete program;
}

void shader_program_destroy(Optional<ShaderProgram*> program) {
    if (program.available) {
        shader_program_destroy(program.value);
    }
}

void shader_program_use(ShaderProgram* program, OpenGLState* state) {
    if (state->program_id != program->program_id) {
        glUseProgram(program->program_id);
        state->program_id = program->program_id;
    }
}

void shader_program_print_variable_information(ShaderProgram* program) 
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
        ShaderVariableInformation* info = &program->uniform_informations.data[i];
        string_append_formated(&message, "\t\tLocation: %d, size: %d, type: %s name: \"%s\"\n", 
            info->location, info->size, opengl_utils_type_to_string(info->type), info->name.characters);
    }

    string_append_formated(&message, "\n\tAttributes(#%d): \n", program->attribute_informations.size);
    for (int i = 0; i < program->attribute_informations.size; i++) {
        ShaderVariableInformation* info = &program->attribute_informations.data[i];
        string_append_formated(&message, "\t\tLocation: %d, size: %d, type: %s name: \"%s\"\n", 
            info->location, info->size, opengl_utils_type_to_string(info->type), info->name.characters);
    }

    logg("%s", message.characters);
}

ShaderVariableInformation* shader_program_find_shader_variable_information_by_name(ShaderProgram* program, const char* name) 
{
    for (int i = 0; i < program->uniform_informations.size; i++) {
        ShaderVariableInformation* info = &program->uniform_informations.data[i];
        if (strcmp(name, info->name.characters) == 0) {
            return info;
        }
    }
    return 0;
}

bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, int value) 
{
    opengl_state_use_program(state, program->program_id);
    ShaderVariableInformation* info = shader_program_find_shader_variable_information_by_name(program, name);
    if (info == nullptr) {
        return false;
    }
    if ((info->type != GL_INT || info->type == GL_SAMPLER_2D) && info->size != 1) {
        return false;
    }
    glUniform1i(info->location, value);
    return true;
}

bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, u32 value)
{
    opengl_state_use_program(state, program->program_id);
    ShaderVariableInformation* info = shader_program_find_shader_variable_information_by_name(program, name);
    if (info == nullptr) {
        return false;
    }
    if ((info->type != GL_UNSIGNED_INT || info->type == GL_SAMPLER_2D) && info->size != 1) {
        return false;
    }
    glUniform1ui(info->location, value);
    return true;
}

bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, float value)
{
    opengl_state_use_program(state, program->program_id);
    ShaderVariableInformation* info = shader_program_find_shader_variable_information_by_name(program, name);
    if (info == nullptr) {
        return false;
    }
    if (info->type != GL_FLOAT && info->size != 1) {
        return false;
    }
    glUniform1f(info->location, value);
    return true;
}

bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, const vec2& value) {
    opengl_state_use_program(state, program->program_id);
    ShaderVariableInformation* info = shader_program_find_shader_variable_information_by_name(program, name);
    if (info == nullptr) {
        return false;
    }
    if (info->type != GL_FLOAT_VEC2 && info->size != 1) {
        return false;
    }
    glUniform2fv(info->location, 1, (GLfloat*)&value);
    return true;
}

bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, const vec3& value) {
    opengl_state_use_program(state, program->program_id);
    ShaderVariableInformation* info = shader_program_find_shader_variable_information_by_name(program, name);
    if (info == nullptr) {
        return false;
    }
    if (info->type != GL_FLOAT_VEC3 && info->size != 1) {
        return false;
    }
    glUniform3fv(info->location, 1, (GLfloat*)&value);
    return true;
}
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, const vec4& value) {
    opengl_state_use_program(state, program->program_id);
    ShaderVariableInformation* info = shader_program_find_shader_variable_information_by_name(program, name);
    if (info == nullptr) {
        return false;
    }
    if (info->type != GL_FLOAT_VEC4 && info->size != 1) {
        return false;
    }
    glUniform4fv(info->location, 1, (GLfloat*)&value);
    return true;
}
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, const mat2& value) {
    opengl_state_use_program(state, program->program_id);
    ShaderVariableInformation* info = shader_program_find_shader_variable_information_by_name(program, name);
    if (info == nullptr) {
        return false;
    }
    if (info->type != GL_FLOAT_MAT2 && info->size != 1) {
        return false;
    }
    glUniformMatrix2fv(info->location, 1, GL_FALSE, (GLfloat*) &value);
    return true;
}
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, const mat3& value) {
    opengl_state_use_program(state, program->program_id);
    ShaderVariableInformation* info = shader_program_find_shader_variable_information_by_name(program, name);
    if (info == nullptr) {
        return false;
    }
    if (info->type != GL_FLOAT_MAT3 && info->size != 1) {
        return false;
    }
    glUniformMatrix3fv(info->location, 1, GL_FALSE, (GLfloat*) &value);
    return true;
}
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, const mat4& value) {
    opengl_state_use_program(state, program->program_id);
    ShaderVariableInformation* info = shader_program_find_shader_variable_information_by_name(program, name);
    if (info == nullptr) {
        return false;
    }
    if (info->type != GL_FLOAT_MAT4 && info->size != 1) {
        return false;
    }
    glUniformMatrix4fv(info->location, 1, GL_FALSE, (GLfloat*) &value);
    return true;
}
