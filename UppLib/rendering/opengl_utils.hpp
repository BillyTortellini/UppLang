#pragma once

#include "opengl_function_pointers.hpp"
#include "../datastructures/array.hpp"

GLuint opengl_utils_create_shader_from_source(const char* source, GLenum type);
GLuint opengl_utils_create_shader_from_file(const char* filepath); // Type gets deferred from file extension
GLuint opengl_utils_create_program_from_filepaths(Array<const char*> filepaths);

void opengl_utils_destroy_shader(GLuint shader_id);
void opengl_utils_destroy_program(GLuint program_id);

const char* opengl_utils_type_to_string(GLenum type);
