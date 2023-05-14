#pragma once

#include <initializer_list>

#include "../utility/utils.hpp"
#include "../datastructures/array.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../datastructures/string.hpp"
#include "../rendering/opengl_function_pointers.hpp"
#include "../math/umath.hpp"
#include "rendering_core.hpp"

struct Watched_File;
struct File_Listener;
struct Texture_2D;
struct Mesh_GPU_Buffer;

struct Shader_Variable_Information
{
    GLint location;
    GLenum type;
    GLsizei size; // Size of array, if variable is array, else 1
    String name_handle;
};

struct Shader_Program
{
    GLuint program_id;
    Array<const char*> shader_filepaths;
    Array<Watched_File*> watched_files;
    File_Listener* file_listener;

    Dynamic_Array<Shader_Variable_Information> uniform_informations;
    Dynamic_Array<Shader_Variable_Information> attribute_informations;
};

// For shader_program_create to work, the files need to exist, the shader does not need to compile
Shader_Program* shader_program_create(std::initializer_list<const char*> shader_filepaths);
void shader_program_destroy(Shader_Program* program);

void shader_program_draw_mesh(Shader_Program* program, Mesh_GPU_Buffer* mesh, std::initializer_list<Uniform_Value> uniforms);
void shader_program_draw_mesh_instanced(
    Shader_Program* program, Mesh_GPU_Buffer* mesh, int instance_count, std::initializer_list<Uniform_Value> uniforms
);
bool shader_program_set_uniform_value(Shader_Program* program, Uniform_Value value);
void shader_program_bind(Shader_Program* program);
void shader_program_print_variable_information(Shader_Program* program);
Shader_Variable_Information* shader_program_find_shader_variable_information_by_name(Shader_Program* program, const char* name_handle);
bool shader_program_check_compatability_with_mesh(Shader_Program* shader_program, Mesh_GPU_Buffer* mesh);



/*
-------------------------------------------------------------------------------
--- HOW TO DRAW SOMETHING IN OPENGL AND HOW I MAY WRAP IT IN UTIL FUNCTIONS ---
-------------------------------------------------------------------------------
    -> Create shader_program
         * From Shader_files (color.vert, color.frag)
           Set input locations (1, 2 or 3 for a_position, a_uv, a_color, a_normal, a_tangent, a_bitangent)
    -> Create mesh 
         * Create VAO and bind it (To save ebo and vbo attribute bindings)
         * Create VBO from mesh data (Either interleaved attributes or multiple blocks, does NOT matter)
            - Provide data and attributes (mesh_add_vertex_data_single_attribute(Array<byte> data, GLenum type, GLenum size)
            - Set attributes according to data
         * Create EBO to save element data
            - Create new ebo and copy data to buffer
            - Save ebo count and triangle topology
    -> Draw 
         * Bind shaderprogram
         * Set per_frame uniforms (View-Matrices (View, projection), time...)
            - GetUniformLocation, or query before and cache
            - SetUniform...
         * Set per_object uniforms (Model-Matrix (Model, normal), color, interpolation parameters)
            - GetUniformLocation, or query before
            - SetUniform...
         * Set OpenGL state
            - Set Framebuffer
            - Set textures...
         * Bind VAO
         * Draw with element count

     Problems with this approach:
        1. Shaderprograms are hard to write, hard to debug.
        2. OpenGL State sollte nicht doppelt gesetzt werden, manchmal vergisst man etwas zu setzen, man
           weiß nicht wann etwas zu setzen ist...
        3. Attribute locations müssen an den jeweiligen Shader angepasst werden -> suckt, 
           mesh-daten können nicht ohne gedanken an Shader erstellt werden! Kein feedback bei fehler
        4. Uniforms setten is anstrengend, uniforms können nicht existieren, datentypen können nicht stimmen, man kanns vergessen, 
           kein feedback bei fehler

    Solutions:
        1. Hotreloading, lets one debug a LOT faster.
           Also errors in shaderprograms don't cause any problems, since they only write to the pixelbuffer 
           (NO SIDEEFFECTS, at least almost always (Feedback...)), so we dont need to worry about hot reloading being wrong.
        2. OpenGLState struct that gets passed to all functions that set state, and only uses the functions that actually need it
        3. What informations do we need to automate this?
            Mesh need to tell what the attribute data is (Position, normal, whatever, depends on usage)
            Shader has information about what inputs are used, and their locations (location + datatype + name) for each input

            What do we have in shaders? Types + names
            To do automatic assignments from shader to mesh, the MESH needs to specify its attributes (Type) and to which names it corresponds.
                Telling each mesh and each attribute what it does is hard, there should be some standards that could be used
            What about special mesh attributes, that are not standard? (Maybe a float for aliasing lines)
            You would like to specify what these new attributes are usually called, and when drawing it should use them.

            Problem: These types of meshes do a lot of stuff, which is too much
            First approach: just mesh that binds to static locations
            

    Previous attempt: What is shitty/not fun?
     -> Shader program uniforms must be always set before used (Matrices for projection, time, and other stuff)
     -> Shader program input locations need to be synchronized with mesh locations -> sucks
    Meshes, what do i need?
    Vertices
*/

