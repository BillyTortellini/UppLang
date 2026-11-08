#pragma once

#include "opengl_function_pointers.hpp"
#include "../datastructures/array.hpp"
#include "../datastructures/string.hpp"

bool opengl_utils_check_shader_compilation_status(GLuint shader_id);
bool opengl_utils_link_program_and_check_errors(GLuint program_id);

GLuint opengl_utils_create_shader_from_file(String filepath); // Type gets deferred from file extension
GLuint opengl_utils_create_program_from_filepaths(Array<String> filepaths);
GLuint opengl_utils_create_program_from_single_file(String filepath);

const char* opengl_utils_datatype_to_string(GLenum type);
const char* opengl_utils_shader_type_to_string(GLenum type);
