#include "opengl_utils.hpp"

#include "../utility/utils.hpp"
#include "../datastructures/string.hpp"
#include "../utility/file_io.hpp"
#include <gl/GL.h>

GLenum shader_part_type_to_opengl_type(Shader_Part_Type type)
{
    switch (type)
    {
    case Shader_Part_Type::VERTEX: return GL_VERTEX_SHADER;
    case Shader_Part_Type::FRAGMENT: return GL_FRAGMENT_SHADER;
    case Shader_Part_Type::TESSELATION_EVALUATION: return GL_TESS_EVALUATION_SHADER;
    case Shader_Part_Type::TESSELATION_CONTROL: return GL_TESS_CONTROL_SHADER;
    case Shader_Part_Type::GEOMETRY: return GL_GEOMETRY_SHADER;
    case Shader_Part_Type::COMPUTE: return GL_COMPUTE_SHADER;
    default: panic("");
    }
    return (GLenum)0;
}

void opengl_utils_detach_all_shaders_from_program(GLuint program_id)
{
    SCRATCH_ARENA_MAKE_SCOPED(nullptr);
    if (!glIsProgram(program_id)) {
        return;
    }

    GLsizei shader_count;
    glGetProgramiv(program_id, GL_ATTACHED_SHADERS, &shader_count);
    if (shader_count <= 0) {
        return;
    }
    Array<GLuint> attached_shader_ids = scratch_arena->allocate_array<GLuint>((int)shader_count);
    glGetAttachedShaders(program_id, shader_count, &shader_count, attached_shader_ids.data);
    for (int i = 0; i < attached_shader_ids.size; i++) {
        glDetachShader(program_id, attached_shader_ids[i]);
    }
}

// Returns true if no errors
bool opengl_utils_check_shader_compile_status(GLuint shader_id, DynString* out_errors)
{
    if (shader_id == 0) {
        out_errors->append("Shader_id is 0");
        return false;
    }

    // Check if compilation worked
    GLint isCompiled = 0;
    glGetShaderiv(shader_id, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &maxLength);

        out_errors->append("Shader-Compilation Error:\n");
        out_errors->reserve_capacity(out_errors->size + maxLength + 1);
        glGetShaderInfoLog(shader_id, maxLength, &maxLength, &out_errors->characters[out_errors->size]);
        out_errors->size += maxLength;
        out_errors->append("\n");
        return false;
    }

    return true;
}

bool opengl_utils_check_program_link_status(GLuint program_id, DynString* out_errors)
{
    int isLinked = 0;
    glGetProgramiv(program_id, GL_LINK_STATUS, &isLinked);
    if (isLinked == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &maxLength);

        out_errors->append("Program Linking failed:\n");
        out_errors->reserve_capacity(out_errors->size + maxLength + 1);
        glGetProgramInfoLog(program_id, maxLength, &maxLength, &out_errors->characters[out_errors->size]);
        out_errors->size += maxLength;
        out_errors->append("\n");
        return false;
    }
    return true;
}

Shader_Compile_Result opengl_utils_compile_shader_from_parts(Array<Shader_Part> parts, Arena* arena, GLuint program_id)
{
    Shader_Compile_Result result;
    result.success = true;
    result.program_id = program_id;

    // If program != 0, detach all shaders
    if (result.program_id != 0) 
    {
        if (glIsProgram(result.program_id)) {
            opengl_utils_detach_all_shaders_from_program(result.program_id);
        }
        else {
            result.program_id = glCreateProgram();
            assert(program_id != 0, "glCreateProgram failed");
        }
    }

    DynString error_string = DynString::create(arena);
    for (int i = 0; i < parts.size; i++)
    {
        Shader_Part& part = parts[i];
        GLuint shader_id = glCreateShader(shader_part_type_to_opengl_type(part.type));
        assert(shader_id != 0, "glCreateShader failed");

        GLint length = part.source_code.size;
        glAttachShader(result.program_id, shader_id); // We can attach shaders at all times
        glShaderSource(shader_id, 1, &part.source_code.characters, &length);
        glCompileShader(shader_id);
        bool compile_success = opengl_utils_check_shader_compile_status(shader_id, &error_string);
        if (!compile_success) {
            result.success = false;
        }
        glDeleteShader(shader_id); // This only flags for deletion, which is delayed until it is no longer in use
    }

    // If all shaders were compiled successfully, try to link program
    // We always link the program even if shaders aren't compiled
    glLinkProgram(result.program_id);
    if (result.success) {
        bool link_success = opengl_utils_check_program_link_status(result.program_id, &error_string);
        if (!link_success) {
            result.success = false;
        }
    }

    // Also we always detach shaders so they are deleted once the shader is not in use anymore?
    opengl_utils_detach_all_shaders_from_program(result.program_id);
    result.error_message = error_string.to_string();
    return result;
}

bool opengl_utils_check_shader_compilation_status(GLuint shader_id)
{
    if (shader_id == 0) {
        logg("Shader id to check was invalid, seems like glCreateShader failed!\n");
        return false;
    }

    SCRATCH_ARENA_MAKE_SCOPED(nullptr);
    DynString tmp_string = DynString::create(scratch_arena);

    // Check if compilation worked
    GLint isCompiled = 0;
    glGetShaderiv(shader_id, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &maxLength);

        tmp_string.reserve_capacity(maxLength + 1);

        glGetShaderInfoLog(shader_id, maxLength, &maxLength, tmp_string.characters);
        logg("ERROR COMPILING SHADER:\n");
        logg("Could not compile shader, error msg: \n %s\n", tmp_string.characters);
        return false;
    }

    return true;
}

GLuint opengl_utils_create_shader_from_file(String filepath)
{
    SCRATCH_ARENA_MAKE_SCOPED(nullptr);

    // Check extension 
    GLenum shaderType = 0;
    if (string_ends_with(filepath, string_make(".frag"))) {
        shaderType = GL_FRAGMENT_SHADER;
    }
    else if (string_ends_with(filepath, string_make(".vert"))) {
        shaderType = GL_VERTEX_SHADER;
    }
    else if (string_ends_with(filepath, string_make(".geom"))) {
        shaderType = GL_GEOMETRY_SHADER;
    }
    else if (string_ends_with(filepath, string_make(".tese"))) {
        shaderType = GL_TESS_EVALUATION_SHADER;
    }
    else if (string_ends_with(filepath, string_make(".tesc"))) {
        shaderType = GL_TESS_CONTROL_SHADER;
    }
    else {
        logg("CreateShaderFromFile: Could not determine shadertype from file extension of: \"%.%s\"\n", filepath.size, filepath.characters);
        return 0;
    }

    // Load shader file
    DynString shader_file_content = DynString::create(scratch_arena);
    bool file_loaded = file_io_load_text_file(filepath, &shader_file_content);
    if (!file_loaded) {
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
    GLint length = shader_file_content.size;
    glShaderSource(result, 1, &shader_file_content.characters, &length);
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
    SCRATCH_ARENA_MAKE_SCOPED(nullptr);

    // Link shaders to program and check if errors occured
    glLinkProgram(program_id);
    int isLinked = 0;
    glGetProgramiv(program_id, GL_LINK_STATUS, &isLinked);
    if (isLinked == GL_FALSE)
    {
        GLint maxLength = 0;
        glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &maxLength);
        Array<char> bytes = scratch_arena->allocate_array<char>(maxLength + 1);

        glGetProgramInfoLog(program_id, maxLength, &maxLength, (GLchar*)&bytes[0]);
        logg("PROGRAM LINKING FAILED!\n");
        logg("Could not link program, error msg: \n %s\n", &bytes[0]);
        return false;
    }
    return true;
}

GLuint opengl_utils_create_program_from_single_file(String filepath)
{
    SCRATCH_ARENA_MAKE_SCOPED(nullptr);

    if (!string_ends_with(filepath, string_make(".glsl"))) {
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
    DynString file_content = DynString::create(scratch_arena);
    bool file_loaded = file_io_load_text_file(filepath, &file_content);
    if (!file_loaded) {
        logg("Could not load file %s\n", filepath);
        return 0;
    }

    int possible_shader_defines_count = 6;
    String possible_shader_defines[] = {
        string_make("VERTEX_SHADER"), 
        string_make("FRAGMENT_SHADER"),
        string_make("GEOMETRY_SHADER"),
        string_make("COMPUTE_SHADER"),
        string_make("TESSELATION_CONTROL_SHADER"),
        string_make("TESSELATION_EVALUATION_SHADER"),
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
        if (string_test_substring(file_content.to_string(), 0, possible_shader_defines[i]))
        {
            GLint shader_id = glCreateShader(possible_shader_define_types[i]);
            const char* sources[] = {"#version 430 core\n", "#define ", possible_shader_defines[i].characters, "\n", file_content.characters};
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

GLuint opengl_utils_create_program_from_filepaths(Array<String> filepaths)
{
    SCRATCH_ARENA_MAKE_SCOPED(nullptr);

    if (filepaths.size == 1) {
        if (string_ends_with(filepaths[0], string_make(".glsl"))) {
            return opengl_utils_create_program_from_single_file(filepaths[0]);
        }
    }

    Array<GLuint> shader_ids = scratch_arena->allocate_array<GLuint>(filepaths.size);

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
