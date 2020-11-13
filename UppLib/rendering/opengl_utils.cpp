#include "opengl_utils.hpp"

#include "../utility/utils.hpp"
#include "../datastructures/string.hpp"
#include "../utility/file_io.hpp"
#include <gl/GL.h>


GLuint opengl_utils_create_shader_from_source(const char* source, GLenum type)
{
    // Create shader id
    GLuint id = glCreateShader(type);
    if (id == 0) {
        logg("glCreateShader failed!\n");
        return 0;
    }

    // Compile
    glShaderSource(id, 1, &source, NULL);
    glCompileShader(id);

    // Check if compilation worked
    GLint isCompiled = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &maxLength);

        String error_message = string_create_empty(maxLength);
        SCOPE_EXIT(string_destroy(&error_message));

        glGetShaderInfoLog(id, maxLength, &maxLength, error_message.characters);
        logg("ERROR COMPILING SHADER:\n");
        logg("Could not compile shader, error msg: \n %s\n", error_message.characters);
        glDeleteShader(id);
        id = 0;
    }

    return id;
}

GLuint opengl_utils_create_shader_from_file(const char* filepath)
{
    // Check extension (Supported extensions are .frag, .vert)
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

    GLuint result = opengl_utils_create_shader_from_source(shader_file_content_optional.value.characters, shaderType);
    if (result == 0) {
        logg("Shader creation failed\n");
    }
    return result;
}

GLuint opengl_utils_create_program_from_filepaths(Array<const char*> filepaths)
{
    Array<GLuint> shader_ids = array_create_empty<GLuint>(filepaths.size);
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
    glLinkProgram(program_id);
    int isLinked = 0;
    glGetProgramiv(program_id, GL_LINK_STATUS, &isLinked);
    if (isLinked == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &maxLength);

        String error_message = string_create_empty(maxLength);
        SCOPE_EXIT(string_destroy(&error_message));
        glGetProgramInfoLog(program_id, maxLength, &maxLength, (GLchar*) error_message.characters);
        logg("PROGRAM LINKING FAILED!\n");
        logg("Could not link program, error msg: \n %s\n", error_message.characters);
    }

    // Cleanup shaders (Meaning that they will be removed by opengl if the program will be deleted
    for (int i = 0; i < shader_ids.size; i++) {
        glDetachShader(program_id, shader_ids.data[i]);
        glDeleteShader(shader_ids.data[i]);
    }

    return program_id;
}

void opengl_utils_destroy_shader(GLuint shader_id) {
    glDeleteShader(shader_id);
}

void opengl_utils_destroy_program(GLuint program_id) {
    glDeleteProgram(program_id);
}

const char* opengl_utils_type_to_string(GLenum type) {
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

