#pragma once

#include <initializer_list>

#include "../utility/utils.hpp"
#include "../datastructures/array.hpp"
#include "../datastructures/dynamic_array.hpp"
#include "../datastructures/string.hpp"
#include "../utility/file_listener.hpp"
#include "../rendering/opengl_function_pointers.hpp"

/*
    Shader program does the following stuff:
     * File listening and hot reloading
     * Handling
        * Attrib locations, types and names
        * Uniform locations, types and names
*/

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

namespace DefaultVertexAttributeLocation 
{
    enum ENUM
    {
        POSITION_3D = 0,
        POSITION_2D = 1,
        TEXTURE_COORDINATE_2D = 2,
        NORMAL = 3,
        COLOR3 = 4,
        COLOR4 = 5,
        TANGENT = 6,
        BITANGENT = 7,

        MINIMUM_OPENGL_ATTRIBUTE_COUNT = 16,
    };
}

struct ShaderVariableInformation
{
    GLint location;
    GLenum type;
    GLsizei size; // Size of array, if variable is array, else 1
    String name;
};

struct ShaderProgram
{
    GLuint program_id;
    Array<const char*> shader_filepaths;
    Array<WatchedFile*> watched_files;
    FileListener* file_listener;

    DynamicArray<ShaderVariableInformation> uniform_informations;
    DynamicArray<ShaderVariableInformation> attribute_informations;
};

struct ShaderProgram;
struct OpenGLState;

// For shader_program_create to work, the files need to exist, the shader does not need to compile
// Hot reloading is done by using the file_listeners interface to search for updates
Optional<ShaderProgram*> shader_program_create(FileListener* file_listener, std::initializer_list<const char*> shader_filepaths);
void shader_program_destroy(ShaderProgram* program);
void shader_program_destroy(Optional<ShaderProgram*> program);

void shader_program_use(ShaderProgram* program, OpenGLState* state);
void shader_program_print_variable_information(ShaderProgram* program);
ShaderVariableInformation* shader_program_find_shader_variable_information_by_name(ShaderProgram* program, const char* name);
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, int value);
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, u32 value);
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, float value);

struct vec2;
struct vec3;
struct vec4;
struct mat2;
struct mat3;
struct mat4;
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, const vec2& value);
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, const vec3& value);
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, const vec4& value);
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, const mat2& value);
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, const mat3& value);
bool shader_program_set_uniform(ShaderProgram* program, OpenGLState* state, const char* name, const mat4& value);
