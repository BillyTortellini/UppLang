#include "opengl_utils.hpp"

#include "../utility/utils.hpp"
#include "../datastructures/string.hpp"
#include "../utility/file_io.hpp"
#include <gl/GL.h>

bool opengl_utils_check_shader_compilation_status(GLuint shader_id)
{
    if (shader_id == 0) {
        logg("Shader id to check was invalid, seems like glCreateShader failed!\n");
        return false;
    }

    // Check if compilation worked
    GLint isCompiled = 0;
    glGetShaderiv(shader_id, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &maxLength);

        String error_message = string_create_empty(maxLength);
        SCOPE_EXIT(string_destroy(&error_message));

        glGetShaderInfoLog(shader_id, maxLength, &maxLength, error_message.characters);
        logg("ERROR COMPILING SHADER:\n");
        logg("Could not compile shader, error msg: \n %s\n", error_message.characters);
        return false;
    }

    return true;
}

GLuint opengl_utils_create_shader_from_file(const char* filepath)
{
    // Check extension 
    GLenum shaderType = 0;
    if (string_ends_with(filepath, ".frag")) {
        shaderType = GL_FRAGMENT_SHADER;
    }
    else if (string_ends_with(filepath, ".vert")) {
        shaderType = GL_VERTEX_SHADER;
    }
    else if (string_ends_with(filepath, ".geom")) {
        shaderType = GL_GEOMETRY_SHADER;
    }
    else if (string_ends_with(filepath, ".tese")) {
        shaderType = GL_TESS_EVALUATION_SHADER;
    }
    else if (string_ends_with(filepath, ".tesc")) {
        shaderType = GL_TESS_CONTROL_SHADER;
    }
    else {
        logg("CreateShaderFromFile: Could not determine shadertype from file extension of: \"%s\"\n", filepath);
        return 0;
    }

    // Load shader file
    Optional<String> shader_file_content_optional = file_io_load_text_file(filepath);
    SCOPE_EXIT(file_io_unload_text_file(&shader_file_content_optional));
    if (!shader_file_content_optional.available) {
        logg("Could not load file shaderfile \"%s\"\n", filepath);
        return 0;
    }

    // Create shader id
    GLuint result = glCreateShader(shaderType);
    if (result == 0) {
        logg("glCreateShader failed!\n");
        return 0;
    }

    // Compile
    glShaderSource(result, 1, &shader_file_content_optional.value.characters, NULL);
    glCompileShader(result);

    if (!opengl_utils_check_shader_compilation_status(result)) {
        logg("Shader creation failed\n");
        glDeleteShader(result);
        return 0;
    }
    return result;
}

bool opengl_utils_link_program_and_check_errors(GLuint program_id) 
{
    // Link shaders to program and check if errors occured
    glLinkProgram(program_id);
    int isLinked = 0;
    glGetProgramiv(program_id, GL_LINK_STATUS, &isLinked);
    if (isLinked == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &maxLength);

        String error_message = string_create_empty(maxLength);
        SCOPE_EXIT(string_destroy(&error_message));
        glGetProgramInfoLog(program_id, maxLength, &maxLength, (GLchar*)error_message.characters);
        logg("PROGRAM LINKING FAILED!\n");
        logg("Could not link program, error msg: \n %s\n", error_message.characters);
        return false;
    }
    return true;
}

GLuint opengl_utils_create_program_from_single_file(const char* filepath)
{
    if (!string_ends_with(filepath, ".glsl")) {
        return 0;
    }

    // Create program 
    GLuint program_id = glCreateProgram();
    if (program_id == 0) {
        logg("glCreateProgram failed!\n");
        return 0;
    }
    bool success = true;
    SCOPE_EXIT(if (!success) glDeleteProgram(program_id));

    // Load file
    Optional<String> file_content_optional = file_io_load_text_file(filepath);
    SCOPE_EXIT(file_io_unload_text_file(&file_content_optional));
    if (!file_content_optional.available) {
        logg("Could not load file %s\n", filepath);
        return 0;
    }
    String file_content = file_content_optional.value;

    int possible_shader_defines_count = 6;
    const char* possible_shader_defines[] = {
        "VERTEX_SHADER", 
        "FRAGMENT_SHADER",
        "GEOMETRY_SHADER",
        "COMPUTE_SHADER",
        "TESSELATION_CONTROL_SHADER",
        "TESSELATION_EVALUATION_SHADER",
    };
    GLenum possible_shader_define_types[] = {
        GL_VERTEX_SHADER,
        GL_FRAGMENT_SHADER,
        GL_GEOMETRY_SHADER,
        GL_COMPUTE_SHADER,
        GL_TESS_CONTROL_SHADER,
        GL_TESS_EVALUATION_SHADER,
    };

    for (int i = 0; i < possible_shader_defines_count && success; i++)
    {
        if (string_contains_substring(&file_content, &string_create_static(possible_shader_defines[i]))) 
        {
            GLint shader_id = glCreateShader(possible_shader_define_types[i]);
            const char* sources[] = {"#version 430 core\n", "#define ", possible_shader_defines[i], "\n", file_content.characters};
            glShaderSource(shader_id, 5, sources, 0);
            glCompileShader(shader_id);
            
            if (!opengl_utils_check_shader_compilation_status(shader_id)) {
                glDeleteShader(shader_id);
                success = false;
                return 0;
            }
            else {
                glAttachShader(program_id, shader_id);
                glDeleteShader(shader_id); // Shader will be delete when shader program is destroyed
            }
        }
    }

    if (!opengl_utils_link_program_and_check_errors(program_id)) {
        success = false;
        return 0;
    }

    return program_id;
}

GLuint opengl_utils_create_program_from_filepaths(Array<const char*> filepaths)
{
    if (filepaths.size == 1) {
        if (string_ends_with(filepaths[0], ".glsl")) {
            return opengl_utils_create_program_from_single_file(filepaths[0]);
        }
    }

    Array<GLuint> shader_ids = array_create_empty<GLuint>(filepaths.size);
    SCOPE_EXIT(array_destroy(&shader_ids));

    int compiled_shader_count = 0;
    for (int i = 0; i < filepaths.size; i++) {
        shader_ids.data[i] = opengl_utils_create_shader_from_file(filepaths.data[i]);
        if (shader_ids.data[i] == 0) {
            logg("Could not create shader_program because of file \"%s\"\n", filepaths.data[i]);
            break;
        }
        compiled_shader_count++;
    }

    // Free shaders and exit if not all shaders could be compiled
    if (compiled_shader_count != filepaths.size) {
        for (int i = 0; i < compiled_shader_count; i++) {
            glDeleteShader(shader_ids.data[i]);
        }
        return 0;
    }

    // Create program from all compiled shader files
    GLuint program_id = glCreateProgram();
    if (program_id == 0) {
        logg("glCreateProgram failed!\n");
        return 0;
    }

    // Attach all shaders
    for (int i = 0; i < shader_ids.size; i++) {
        glAttachShader(program_id, shader_ids.data[i]);
    }

    // Link shaders to program and check if errors occured
    if (!opengl_utils_link_program_and_check_errors(program_id)) {
        glDeleteProgram(program_id);
    }

    // Cleanup shaders (Meaning that they will be removed by opengl if the program will be deleted
    for (int i = 0; i < shader_ids.size; i++) {
        glDetachShader(program_id, shader_ids.data[i]);
        glDeleteShader(shader_ids.data[i]);
    }

    return program_id;
}

const char* opengl_utils_datatype_to_string(GLenum type) {
    switch (type)
    {
    case GL_FLOAT_MAT2:
        return "GL_FLOAT_MAT2";
    case GL_FLOAT_MAT3:
        return "GL_FLOAT_MAT3";
    case GL_FLOAT_MAT4:
        return "GL_FLOAT_MAT4";
    case GL_FLOAT:
        return "GL_FLOAT";
    case GL_INT:
        return "GL_INT";
    case GL_UNSIGNED_INT:
        return "GL_UNSIGNED_INT";
    case GL_BOOL:
        return "GL_BOOL";
    case GL_FLOAT_VEC2:
        return "GL_FLOAT_VEC2";
    case GL_FLOAT_VEC3:
        return "GL_FLOAT_VEC3";
    case GL_FLOAT_VEC4:
        return "GL_FLOAT_VEC4";
    case GL_SAMPLER_2D:
        return "GL_SAMPLER_2D";
    case GL_SAMPLER_2D_SHADOW:
        return "GL_SAMPLER_2D_SHADOW";
    default:
        return "Unrecognised type";
    }
}

const char* opengl_utils_shader_type_to_string(GLenum type)
{
    switch (type)
    {
    case GL_VERTEX_SHADER:
        return "GL_VERTEX_SHADER";
    case GL_FRAGMENT_SHADER:
        return "GL_FRAGMENT_SHADER";
    case GL_TESS_EVALUATION_SHADER:
        return "GL_TESS_EVALUATION_SHADER";
    case GL_TESS_CONTROL_SHADER:
        return "GL_TESS_CONTROL_SHADER";
    case GL_GEOMETRY_SHADER:
        return "GL_GEOMETRY_SHADER";
    case GL_COMPUTE_SHADER:
        return "GL_COMPUTE_SHADER";
    }
    return "INVALID VALUE";
}
